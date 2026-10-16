// 压平端到端测试：源码 → parse → SCode → 实例化 → flatten → dump，逐字对照 `omc X.mo` 实测输出。
// 期望值均已在安装版 omc 1.25.0 上核对（2026-09-18）。
#include "ymc_test.h"

#include <string>
#include <vector>

#include "absyn/absyn.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "flat/flat_dump.h"
#include "flat/flatten.h"
#include "inst/inst.h"
#include "parse/lexer.h"
#include "parse/parser.h"
#include "scode/absyn_to_scode.h"
#include "scode/scode.h"

using namespace ymc;

namespace {

// 全流水线（同 driver）：压平文件里最后一个顶层类。unsupported 时返回带标记的空串。
std::string flatten_src(const char* src, bool* unsupported = nullptr) {
  Interner in;
  Diagnostics d;
  Absyn a(in);
  Lexer lx(src, 0, in, d);
  std::vector<Token> toks = lx.tokenize();
  Parser p(toks, a, d);
  p.parse_program();
  SCode sc(a);
  translate_program(sc, a.top);
  Inst inst(in, a, sc);
  Instantiator I(inst, d);
  I.make_top_node();
  NodeId root = I.instantiate_root(sc.elems[sc.program.back()].name);
  if (!root.is_valid() || inst.nodes[root].is_empty() || d.has_errors()) return "<error>";
  // 未实现判定要合并 inst 与 flatten 两阶段（同 driver）：inst 阶段的 mark_unsupported 不体现在 FlattenResult 上。
  FlattenResult fr = flatten(inst, in, root, &I);
  const bool uns = I.unsupported() || fr.unsupported;
  if (unsupported) *unsupported = uns;
  if (uns) return "<unsupported:" + (I.unsupported() ? I.unsupported_reason() : fr.unsupported_reason) + ">";
  return dump_flat(fr.model, in);
}

}  // namespace

YMC_TEST(flatten_scalar_bindings_and_int_promotion) {
  YMC_CHECK_EQ(flatten_src("model M\n  Real x = 1.0;\n  Real y = x + 2;\n  Integer n = 3;\nend M;\n"),
               std::string("class M\n  Real x = 1.0;\n  Real y = x + 2.0;\n  Integer n = 3;\nend M;\n"));
}

YMC_TEST(flatten_single_inheritance) {
  YMC_CHECK_EQ(flatten_src("model A\n  Real a1;\n  Real a2 = 1.0;\nend A;\n"
                           "model B\n  Real b1;\n  extends A;\n  Real b2 = a2;\nend B;\n"),
               std::string("class B\n  Real b1;\n  Real a1;\n  Real a2 = 1.0;\n  Real b2 = a2;\nend B;\n"));
}

YMC_TEST(flatten_composite_component_prefixed) {
  YMC_CHECK_EQ(flatten_src("model A\n  Real a = 2.0;\nend A;\n"
                           "model M\n  A comp;\n  Real z = comp.a;\nend M;\n"),
               std::string("class M\n  Real comp.a = 2.0;\n  Real z = comp.a;\nend M;\n"));
}

YMC_TEST(flatten_variability_prefixes) {
  // parameter/constant 前缀（R13 §3.1）。omc 实测：constant/parameter 各带绑定。
  YMC_CHECK_EQ(flatten_src("model M\n  constant Real c = 3.0;\n  parameter Real p = 2.0;\n  Real x;\nend M;\n"),
               std::string("class M\n  constant Real c = 3.0;\n  parameter Real p = 2.0;\n  Real x;\nend M;\n"));
}

YMC_TEST(flatten_multilevel_inheritance) {
  YMC_CHECK_EQ(flatten_src("model A\n  Real a;\nend A;\n"
                           "model B\n  extends A;\n  Real b;\nend B;\n"
                           "model C\n  extends B;\n  Real c;\nend C;\n"),
               std::string("class C\n  Real a;\n  Real b;\n  Real c;\nend C;\n"));
}

// 有方程段 → v1 未实现，标 unsupported（driver 退 66 → 运行器记 SKIP，不是 FAIL）。
YMC_TEST(flatten_equality_equation) {
  // 纯 Equals 方程现在支持：段落 + int→real 强转右侧。
  YMC_CHECK_EQ(flatten_src("model M\n  Real x;\nequation\n  x = 1;\nend M;\n"),
               std::string("class M\n  Real x;\nequation\n  x = 1.0;\nend M;\n"));
}

YMC_TEST(flatten_connect_equation_still_unsupported) {
  bool uns = false;
  std::string t = flatten_src(
      "model M\n  Real a;\n  Real b;\nequation\n  connect(a, b);\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
}

// 算法段：直线标量赋值 + 整型→实型强制 + 组件/语句注释（期望值取自实跑 omc 1.25.0）。
YMC_TEST(flatten_algorithm_section_scalar_assign) {
  YMC_CHECK_EQ(flatten_src("model UT\n  Real x \"cx\";\n  Integer n;\nalgorithm\n  x := 1 \"sc\";\n  n := 2;\nend UT;\n"),
               std::string("class UT\n  Real x \"cx\";\n  Integer n;\nalgorithm\n  x := 1.0 \"sc\";\n  n := 2;\nend UT;\n"));
}

// 给参数赋值（非 initial algorithm）是 omc 错误 → 保守 SKIP（保 FAIL=0）。
YMC_TEST(flatten_algorithm_assign_to_parameter_unsupported) {
  bool uns = false;
  std::string t = flatten_src("model M\n  parameter Real p = 0.0;\nalgorithm\n  p := 5.0;\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
}

// 数组方程逐元素标量化（整体引用 + 一元负 + 标量广播 *；行优先）。期望值取自实跑 omc 1.25.0。
YMC_TEST(flatten_array_equation_scalarize) {
  YMC_CHECK_EQ(flatten_src("model AE\n  Real x[2];\n  Real y[2];\n  Real s;\nequation\n  x = -y;\n  y = s * x;\nend AE;\n"),
               std::string("class AE\n  Real x[1];\n  Real x[2];\n  Real y[1];\n  Real y[2];\n  Real s;\n"
                           "equation\n  x[1] = -y[1];\n  x[2] = -y[2];\n  y[1] = s * x[1];\n  y[2] = s * x[2];\nend AE;\n"));
}

// Modelica `/` 恒实除：整型操作数提实型（MLS §10.6.1）。期望值取自实跑 omc 1.25.0。
YMC_TEST(flatten_integer_division_is_real) {
  YMC_CHECK_EQ(flatten_src("model M\n  Integer i;\n  Integer j;\n  Real r;\nequation\n  r = i / j;\nend M;\n"),
               std::string("class M\n  Integer i;\n  Integer j;\n  Real r;\nequation\n  r = /*Real*/(i) / /*Real*/(j);\nend M;\n"));
}

// 矩阵乘（两侧皆数组的 `*`，NFTypeCheck.checkBinaryOperationMul）：维已知时展开成积和、方程逐元素标量化；矩阵 · 向量、
// 向量 · 向量、Σ 归约同理（C3 探针）。期望 = omc 1.25.0 实跑（2026-10-02）。
YMC_TEST(flatten_array_matrix_product) {
  YMC_CHECK_EQ(flatten_src("model M\n  Real a[2,2];\n  Real b[2,2];\n  Real c[2,2];\nequation\n  c = a * b;\nend M;\n"),
               std::string("class M\n  Real a[1,1];\n  Real a[1,2];\n  Real a[2,1];\n  Real a[2,2];\n"
                           "  Real b[1,1];\n  Real b[1,2];\n  Real b[2,1];\n  Real b[2,2];\n"
                           "  Real c[1,1];\n  Real c[1,2];\n  Real c[2,1];\n  Real c[2,2];\nequation\n"
                           "  c[1,1] = a[1,1] * b[1,1] + a[1,2] * b[2,1];\n  c[1,2] = a[1,1] * b[1,2] + a[1,2] * b[2,2];\n"
                           "  c[2,1] = a[2,1] * b[1,1] + a[2,2] * b[2,1];\n  c[2,2] = a[2,1] * b[1,2] + a[2,2] * b[2,2];\n"
                           "end M;\n"));
  YMC_CHECK_EQ(flatten_src("model M\n  Real y[3];\n  Real v[2];\n  Real A[2,2];\n  Real s = sum(y[i] for i in 1:3);\n"
                           "  Real w[2] = A * v;\n  Real d = v * v;\nend M;\n"),
               std::string("class M\n  Real y[1];\n  Real y[2];\n  Real y[3];\n  Real v[1];\n  Real v[2];\n"
                           "  Real A[1,1];\n  Real A[1,2];\n  Real A[2,1];\n  Real A[2,2];\n  Real s = y[1] + y[2] + y[3];\n"
                           "  Real w[1];\n  Real w[2];\n  Real d = v[1] * v[1] + v[2] * v[2];\nequation\n"
                           "  w = {A[1,1] * v[1] + A[1,2] * v[2], A[2,1] * v[1] + A[2,2] * v[2]};\nend M;\n"));
}

// 常量数组下标元素折叠（N-D 行主序 + 表达式内下标 + 整型→实型）。期望值取自实跑 omc 1.25.0。
YMC_TEST(flatten_constant_array_element_fold) {
  YMC_CHECK_EQ(
      flatten_src("model CE\n  constant Real m[2,3] = {{1,2,3},{4,5,6}};\n  Real a = m[2,3];\n"
                  "  Real b = m[1,1] + m[2,1]*2;\n  constant Integer k[3] = {7,8,9};\n  Real c = k[2];\n"
                  "  Integer d = k[3];\nend CE;\n"),
      std::string("class CE\n  constant Real m[1,1] = 1.0;\n  constant Real m[1,2] = 2.0;\n  constant Real m[1,3] = 3.0;\n"
                  "  constant Real m[2,1] = 4.0;\n  constant Real m[2,2] = 5.0;\n  constant Real m[2,3] = 6.0;\n"
                  "  Real a = 6.0;\n  Real b = 9.0;\n  constant Integer k[1] = 7;\n  constant Integer k[2] = 8;\n"
                  "  constant Integer k[3] = 9;\n  Real c = 8.0;\n  Integer d = 9;\nend CE;\n"));
}

// 非构造子数组绑定：Var 整取 `x=y` / 算子展开 `{2.0*y[i]}`；param 逐元素 `q[i]=p[i]` / `/*Real*/(ip[i])`。期望值取自实跑 omc 1.25.0。
YMC_TEST(flatten_non_literal_array_binding) {
  YMC_CHECK_EQ(
      flatten_src("model NLB\n  Real y[3];\n  Real x[3] = y;\n  Real w[3] = 2*y;\n  parameter Real p[3] = {1,2,3};\n"
                  "  parameter Real q[3] = p;\n  parameter Integer ip[3] = {1,2,3};\n  parameter Real r[3] = ip;\nend NLB;\n"),
      std::string("class NLB\n  Real y[1];\n  Real y[2];\n  Real y[3];\n  Real x[1];\n  Real x[2];\n  Real x[3];\n"
                  "  Real w[1];\n  Real w[2];\n  Real w[3];\n  parameter Real p[1] = 1.0;\n  parameter Real p[2] = 2.0;\n"
                  "  parameter Real p[3] = 3.0;\n  parameter Real q[1] = p[1];\n  parameter Real q[2] = p[2];\n"
                  "  parameter Real q[3] = p[3];\n  parameter Integer ip[1] = 1;\n  parameter Integer ip[2] = 2;\n"
                  "  parameter Integer ip[3] = 3;\n  parameter Real r[1] = /*Real*/(ip[1]);\n"
                  "  parameter Real r[2] = /*Real*/(ip[2]);\n  parameter Real r[3] = /*Real*/(ip[3]);\n"
                  "equation\n  x = y;\n  w = {2.0 * y[1], 2.0 * y[2], 2.0 * y[3]};\nend NLB;\n"));
}

// 非常量下标（x[n]，n 是变量）→ omc 把常量换成值、下标留成符号（60f 起做了，见 flatten_constant_array_subscripts）。
// 期望 = omc 1.25.0 实跑（2026-10-03）。
YMC_TEST(flatten_constant_array_variable_index) {
  YMC_CHECK_EQ(flatten_src("model M\n  constant Real x[3] = {1,2,3};\n  Integer n = 2;\n  Real y = x[n];\nend M;\n"),
               std::string("class M\n  constant Real x[1] = 1.0;\n  constant Real x[2] = 2.0;\n  constant Real x[3] = 3.0;\n"
                           "  Integer n = 2;\n  Real y = {1.0, 2.0, 3.0}[n];\nend M;\n"));
}

// 函数发射（首批）：纯标量顶层函数 → class 之前发射 `function f…end f;`，调用保留符号 `f(time)`。期望值取自实跑 omc 1.25.0。
YMC_TEST(flatten_user_function_emitted) {
  YMC_CHECK_EQ(
      flatten_src("function f\n  input Real x;\n  output Real y = 2*x;\nend f;\nmodel M\n  Real b = f(time);\nend M;\n"),
      std::string("function f\n  input Real x;\n  output Real y = 2.0 * x;\nend f;\n\n"
                  "class M\n  Real b = f(time);\nend M;\n"));
}

// 函数发射（increment 2）：protected 局部按依赖拓扑重排（t2 先于 t1）+ 算法体。期望值取自实跑 omc 1.25.0。
YMC_TEST(flatten_user_function_locals_and_algorithm) {
  YMC_CHECK_EQ(
      flatten_src("function g\n  input Real x;\n  output Real y;\nprotected\n  Real t1 = t2;\n  Real t2 = 2*x;\n"
                  "algorithm\n  y := t1 + 1;\nend g;\nmodel M\n  Real b = g(time);\nend M;\n"),
      std::string("function g\n  input Real x;\n  output Real y;\n  protected Real t2 = 2.0 * x;\n  protected Real t1 = t2;\n"
                  "algorithm\n  y := t1 + 1.0;\nend g;\n\nclass M\n  Real b = g(time);\nend M;\n"));
}

// 复合组件变率下沉 + 常量记录成员折叠：constant/parameter R 的字段继承变率；constant r.x 引用折成字面量。期望值取自实跑 omc 1.25.0。
YMC_TEST(flatten_composite_variability_and_member_fold) {
  YMC_CHECK_EQ(
      flatten_src("model CR\n  record R\n    Real x;\n    Real y;\n  end R;\n  constant R r = R(3.0, 4.0);\n"
                  "  Real a = r.x;\n  parameter R p = R(5.0, 6.0);\nend CR;\n"),
      std::string("class CR\n  constant Real r.x = 3.0;\n  constant Real r.y = 4.0;\n  Real a = 3.0;\n"
                  "  parameter Real p.x = 5.0;\n  parameter Real p.y = 6.0;\nend CR;\n"));
}

// for-语句：保留循环（不展开），符号迭代变量，数组元素赋值 + 整型→实型。期望值取自实跑 omc 1.25.0。
YMC_TEST(flatten_for_statement_kept) {
  YMC_CHECK_EQ(
      flatten_src("model FS\n  Real x[3];\nalgorithm\n  for i in 1:3 loop\n    x[i] := 2*i;\n  end for;\nend FS;\n"),
      std::string("class FS\n  Real x[1];\n  Real x[2];\n  Real x[3];\n"
                  "algorithm\n  for i in 1:3 loop\n    x[i] := /*Real*/(2 * i);\n  end for;\nend FS;\n"));
}

// 类型名维度：`x[Boolean]`→`x[false] x[true]`；`y[E]`→`y[E.a] …`（下标是 BoolLit/EnumLit）。期望值取自实跑 omc 1.25.0。
YMC_TEST(flatten_type_typed_dimensions) {
  YMC_CHECK_EQ(
      flatten_src("model TD\n  type E = enumeration(a, b, c);\n  Real x[Boolean];\n  Real y[E];\nend TD;\n"),
      std::string("class TD\n  Real x[false];\n  Real x[true];\n  Real y[E.a];\n  Real y[E.b];\n  Real y[E.c];\nend TD;\n"));
}

// `:` 维从数组构造子绑定推断形状（含 array(…) ≡ {…}、多维）。期望值取自实跑 omc 1.25.0。
YMC_TEST(flatten_colon_dim_from_array_constructor) {
  YMC_CHECK_EQ(
      flatten_src("model M\n  constant Real r[:] = array(1.0, 2.0, 3.0);\n"
                  "  constant Real m[:,:] = {{1.0, 2.0}, {3.0, 4.0}};\nend M;\n"),
      std::string("class M\n  constant Real r[1] = 1.0;\n  constant Real r[2] = 2.0;\n  constant Real r[3] = 3.0;\n"
                  "  constant Real m[1,1] = 1.0;\n  constant Real m[1,2] = 2.0;\n  constant Real m[2,1] = 3.0;\n"
                  "  constant Real m[2,2] = 4.0;\nend M;\n"));
}

YMC_TEST(flatten_builtin_attributes) {
  // 内建属性修饰：写序打乱 → 按类型属性表序产出；Real 属性 int→real 强转；Integer 不强转；
  // String 的 fixed、Real 的 unbounded 被丢弃；stateSelect 是枚举字面量（全路径）。期望值 = omc 1.25.0 实测。
  const char* src =
      "model M\n"
      "  Real r(start = 10, min = -100, max = 100, nominal = 1, unit = \"kg\", fixed = true, "
      "unbounded = true, quantity = \"m\", displayUnit = \"mm\");\n"
      "  Integer i(min = -100, max = 100, start = 10, fixed = true, quantity = \"c\");\n"
      "  Boolean b(start = false, fixed = true, quantity = \"flag\");\n"
      "  String s(start = \"hi\", fixed = true, quantity = \"name\");\n"
      "  Real p(stateSelect = StateSelect.prefer);\n"
      "end M;\n";
  const char* want =
      "class M\n"
      "  Real r(quantity = \"m\", unit = \"kg\", displayUnit = \"mm\", min = -100.0, max = 100.0, "
      "start = 10.0, fixed = true, nominal = 1.0);\n"
      "  Integer i(quantity = \"c\", min = -100, max = 100, start = 10, fixed = true);\n"
      "  Boolean b(quantity = \"flag\", start = false, fixed = true);\n"
      "  String s(quantity = \"name\", start = \"hi\");\n"
      "  Real p(stateSelect = StateSelect.prefer);\n"
      "end M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_modifier_merge) {
  // 复合组件修饰传播：嵌套 a(x=2.0, m(z=p))、final 沿子树传播、修饰值 `z=p` 在书写作用域解析。
  // 期望值 = omc 1.25.0 实测。
  const char* src =
      "model MOuter\n  Real z;\nend MOuter;\n"
      "model AInner\n  Real x;\n  MOuter m;\nend AInner;\n"
      "model MM\n  parameter Real p = 1.0;\n  final AInner a(x = 2.0, m(z = p));\nend MM;\n";
  const char* want =
      "class MM\n"
      "  parameter Real p = 1.0;\n"
      "  final Real a.x = 2.0;\n"
      "  final Real a.m.z = p;\n"
      "end MM;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_modifier_override_wins) {
  // 外层实例修饰 > 声明默认（R15 §3.4）：A a(x=2.0) 覆盖 A 里的 Real x = 1.0。
  const char* src = "model A\n  Real x = 1.0;\nend A;\nmodel M\n  A a(x = 2.0);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string("class M\n  Real a.x = 2.0;\nend M;\n"));
}

YMC_TEST(flatten_nested_class_modifier) {
  // 组件修饰里改嵌套类（类修饰 `a(B(x = 2.0))`）：挂到 a 的类实例里那个本地类上，b 实例化时并进去（NFInst.applyModifier，
  // NFInst.mo:2116-2160 v1.25.0；ADR-007 第 2 阶段 65a-4，此前 SKIP）。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-ut PKU2）。
  YMC_CHECK_EQ(flatten_src("model A\n  model B\n    Real x = 1.0;\n  end B;\n  B b;\nend A;\n"
                           "model M\n  A a(B(x = 2.0));\nend M;\n"),
               std::string("class M\n  Real a.b.x = 2.0;\nend M;\n"));
}

YMC_TEST(flatten_derived_type_collapses) {
  // 短类塌陷：`type MyReal = Real(start=1.0)` → 组件 `MyReal x` 塌成 `Real x(start = 1.0)`。期望 = omc 1.25.0。
  YMC_CHECK_EQ(flatten_src("type MyReal = Real(start = 1.0);\nmodel M\n  MyReal x;\nend M;\n"),
               std::string("class M\n  Real x(start = 1.0);\nend M;\n"));
}

YMC_TEST(flatten_derived_prefix_accumulates) {
  // 短类前缀累积：input + constant 沿链累积到组件（ClassAttributes2）。
  YMC_CHECK_EQ(flatten_src("model M\n  type IR = input Real;\n  type CIR = constant IR;\n  CIR x = 1;\nend M;\n"),
               std::string("class M\n  constant input Real x = 1.0;\nend M;\n"));
}

YMC_TEST(flatten_enum_type_and_literal_binding) {
  // 枚举类型组件塌成 `enumeration(lits)`；绑定 `E.one` 是枚举字面量打全路径（enum1）。
  YMC_CHECK_EQ(
      flatten_src("model M\n  type E = enumeration(one, two, three);\n  E e = E.one;\nend M;\n"),
      std::string("class M\n  enumeration(one, two, three) e = E.one;\nend M;\n"));
}

YMC_TEST(flatten_extends_modifier_binding) {
  // extends 修饰绑定覆盖：extends A(x=1) → 继承的 x 拿到绑定（ExtendsMod1）。
  YMC_CHECK_EQ(flatten_src("model A\n  Real x;\nend A;\nmodel M\n  extends A(x = 1);\nend M;\n"),
               std::string("class M\n  Real x = 1.0;\nend M;\n"));
}

YMC_TEST(flatten_extends_modifier_chain_attrs) {
  // extends 修饰属性沿继承链累积（PropagateExtends）：B extends A(x(unit)), C extends B(x(start))。
  YMC_CHECK_EQ(
      flatten_src("model A\n  Real x;\nend A;\n"
                  "model B\n  extends A(x(unit = \"kg\"));\nend B;\n"
                  "model C\n  extends B(x(start = 1));\nend C;\n"),
      std::string("class C\n  Real x(unit = \"kg\", start = 1.0);\nend C;\n"));
}

YMC_TEST(flatten_extends_modifier_on_nested_type) {
  // extends 修饰里改嵌套类型（类修饰，scodeinst mod9 的形态）：挂到 extends 实例类树里的类上，基类实例里已实例化的 x 补上
  //（ADR-007 第 2 阶段 65a-4，此前 SKIP）。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-ut PKU3）。
  YMC_CHECK_EQ(flatten_src("model A\n  type MyReal = Real;\n  MyReal x;\nend A;\n"
                           "model B\n  extends A(MyReal(start = 2.0));\nend B;\n"),
               std::string("class B\n  Real x(start = 2.0);\nend B;\n"));
}

YMC_TEST(flatten_inner_outer_scalar) {
  // inner/outer：outer x 不产变量，引用连到最近外层 inner x 的全名（InnerOuter1）。
  const char* src =
      "model A\n  outer Real x;\n  Real y;\nequation\n  y = x;\nend A;\n"
      "model M\n  inner Real x = 1.0;\n  A a1, a2;\nend M;\n";
  const char* want =
      "class M\n  Real x = 1.0;\n  Real a1.y;\n  Real a2.y;\n"
      "equation\n  a1.y = x;\n  a2.y = x;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_inner_outer_composite) {
  // 复合 outer：经它的引用（绑定、if 条件里的参数）换成最近 inner 的全名，嵌套两层的 c.b 也一样；inner 的类与 outer 声明
  // 类型不相干也照样压平（omc 不比类型）。期望值为 omc 1.25.0 对原文实跑（IO 探针 i1-i4）。
  const char* src1 =
      "model A\n  Real x;\n  parameter Integer k = 2;\nend A;\n"
      "model B\n  outer A a;\n  Real y = a.x;\n  Real z;\nequation\n  if a.k > 1 then\n    z = 1;\n  else\n    z = 2;\n  end if;\nend B;\n"
      "model C\n  B b;\nend C;\n"
      "model IO1\n  inner A a(x = time, k = 3);\n  C c;\n  B b;\nend IO1;\n";
  const char* want1 =
      "class IO1\n  Real a.x = time;\n  final parameter Integer a.k = 3;\n  Real c.b.y = a.x;\n  Real c.b.z;\n"
      "  Real b.y = a.x;\n  Real b.z;\nequation\n  c.b.z = 1.0;\n  b.z = 1.0;\nend IO1;\n";
  YMC_CHECK_EQ(flatten_src(src1), std::string(want1));
  const char* src2 =
      "model A\n  Real x;\nend A;\nmodel A2\n  Real x;\n  Real w;\nend A2;\n"
      "model B\n  outer A a;\n  Real y = a.x;\nend B;\n"
      "model IO2\n  inner A2 a(x = 1, w = 2);\n  B b;\nend IO2;\n";
  YMC_CHECK_EQ(flatten_src(src2),
               std::string("class IO2\n  Real a.x = 1.0;\n  Real a.w = 2.0;\n  Real b.y = a.x;\nend IO2;\n"));
}

YMC_TEST(flatten_outer_without_inner_generated) {
  // 没有匹配 inner 的 outer → omc 在根类末尾生成同名 inner（并告警，R11 P08；generateInner / insertGeneratedInners）。两个
  // 生成的 inner 次序与生成次序相反（w 在 a 前）。期望 = omc 1.25.0 实跑（2026-10-10，探针 gn-ut 的 GNU0 / GNU1）。
  YMC_CHECK_EQ(flatten_src("model GNU0A\n  outer Real x;\n  Real y = x;\nend GNU0A;\nmodel GNU0\n  GNU0A a;\nend GNU0;\n"),
               std::string("class GNU0\n  Real a.y = x;\n  Real x;\nend GNU0;\n"));
  YMC_CHECK_EQ(flatten_src("model GNU1\n  model A\n    parameter Real p = 3;\n    Real x = p * time;\n  end A;\n"
                           "  model W\n    parameter Real g = 9.81;\n  end W;\n  model B\n    outer A a;\n    outer W w;\n"
                           "    Real y = a.x * w.g;\n  end B;\n  B b;\n  Real z = 1;\nend GNU1;\n"),
               std::string("class GNU1\n  Real b.y = a.x * w.g;\n  Real z = 1.0;\n  parameter Real w.g = 9.81;\n"
                           "  parameter Real a.p = 3.0;\n  Real a.x = a.p * time;\nend GNU1;\n"));
}

YMC_TEST(flatten_array_scalarize_binding_and_subscript) {
  // 数组：变量行行主序标量化；连续数组绑定 → 数组方程；下标引用保留（Subscript1，omc 1.25.0 实测）。
  const char* src = "model M\n  Real x[3] = {1, 2, 3};\n  Real y = x[2];\nend M;\n";
  const char* want =
      "class M\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y = x[2];\n"
      "equation\n  x = {1.0, 2.0, 3.0};\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_array_multidim_rowmajor) {
  // 多维数组行主序标量化：m[2,2] → [1,1][1,2][2,1][2,2]。
  YMC_CHECK_EQ(
      flatten_src("model M\n  Real m[2,2];\nend M;\n"),
      std::string("class M\n  Real m[1,1];\n  Real m[1,2];\n  Real m[2,1];\n  Real m[2,2];\nend M;\n"));
}

YMC_TEST(flatten_whole_array_equation_scalarizes) {
  // 数组整体引用方程 `y = x` → 逐元素标量化（NFScalarize）。期望值取自实跑 omc 1.25.0。
  YMC_CHECK_EQ(
      flatten_src("model M\n  Real x[3];\n  Real y[3];\nequation\n  y = x;\nend M;\n"),
      std::string("class M\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y[1];\n  Real y[2];\n  Real y[3];\n"
                  "equation\n  y[1] = x[1];\n  y[2] = x[2];\n  y[3] = x[3];\nend M;\n"));
}

YMC_TEST(flatten_const_fold_and_inline) {
  // 常量内联 + 算术折叠：constant z = x + y → 3.0（CevalBinding1）。
  YMC_CHECK_EQ(
      flatten_src("model M\n  constant Real x = 1.0;\n  constant Real y = 2.0;\n  constant Real z = x + y;\nend M;\n"),
      std::string("class M\n  constant Real x = 1.0;\n  constant Real y = 2.0;\n  constant Real z = 3.0;\nend M;\n"));
}

YMC_TEST(flatten_const_inline_int_to_real) {
  // 常量 Integer 内联进 Real 绑定 + 强转（CevalConstant1）。
  YMC_CHECK_EQ(flatten_src("model M\n  constant Integer n = 3;\n  Real x = n;\nend M;\n"),
               std::string("class M\n  constant Integer n = 3;\n  Real x = 3.0;\nend M;\n"));
}

YMC_TEST(flatten_partial_fold_keeps_variable) {
  // 部分折叠：变量 + 常量子表达式 → 只折常量部分（`x + (1 + 2)` → `x + 3.0`）。
  YMC_CHECK_EQ(flatten_src("model M\n  Real x;\n  Real y = x + (1 + 2);\nend M;\n"),
               std::string("class M\n  Real x;\n  Real y = x + 3.0;\nend M;\n"));
}

YMC_TEST(flatten_final_parameter_not_inlined) {
  // omc 1.25.0 不内联 final parameter：`Real y = x` 保留（分歧账本）。
  YMC_CHECK_EQ(flatten_src("model M\n  final parameter Real x = 3.0;\n  Real y = x;\nend M;\n"),
               std::string("class M\n  final parameter Real x = 3.0;\n  Real y = x;\nend M;\n"));
}

YMC_TEST(flatten_const_relation_fold) {
  // 关系折叠（Int/Real）：constant Boolean b = 1 < 2 → true。
  YMC_CHECK_EQ(flatten_src("model M\n  constant Boolean b = 1 < 2;\nend M;\n"),
               std::string("class M\n  constant Boolean b = true;\nend M;\n"));
}

YMC_TEST(flatten_cross_scope_constant_inline) {
  // 跨作用域常量：嵌套类 B 的 `j = i` 引用外层 A 的 constant i，沿 parent_scope 内联（const10）。
  const char* src =
      "model A\n  constant Integer i = 3;\n  model B\n    constant Integer j = i;\n    Real x = j;\n  end B;\nend A;\n"
      "model C\n  A.B b;\nend C;\n";
  const char* want = "class C\n  constant Integer b.j = 3;\n  Real b.x = 3.0;\nend C;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_enum_relation_fold) {
  // 枚举关系按序数折叠（E.one=1 < E.two=2 → true）。期望 = omc 1.25.0（CevalRelation1）。
  YMC_CHECK_EQ(
      flatten_src("model M\n  type E = enumeration(one, two, three);\n  constant Boolean b = E.one < E.two;\nend M;\n"),
      std::string("class M\n  constant Boolean b = true;\nend M;\n"));
}

YMC_TEST(flatten_bool_and_string_relation_fold) {
  // 布尔关系 false<true（`true < false`→false）、字符串字典序（`\"1\" < \"2\"`→true）。
  YMC_CHECK_EQ(flatten_src("model M\n  constant Boolean b = true < false;\nend M;\n"),
               std::string("class M\n  constant Boolean b = false;\nend M;\n"));
  YMC_CHECK_EQ(flatten_src("model M\n  constant Boolean b = \"1\" < \"2\";\nend M;\n"),
               std::string("class M\n  constant Boolean b = true;\nend M;\n"));
}

YMC_TEST(flatten_logical_and_integer_enum_fold) {
  // 常量逻辑折叠（`false and true`→false）；Integer(enum) → 序数（CevalIntegerEnum1）。
  YMC_CHECK_EQ(flatten_src("model M\n  constant Boolean b = false and true;\nend M;\n"),
               std::string("class M\n  constant Boolean b = false;\nend M;\n"));
  YMC_CHECK_EQ(
      flatten_src("model M\n  type E = enumeration(one, two, three);\n  constant Integer i = Integer(E.two);\nend M;\n"),
      std::string("class M\n  constant Integer i = 2;\nend M;\n"));
}

YMC_TEST(flatten_package_qualified_constant) {
  // 顶层包常量 `P.c`、同包内引用 `d = c + 1.0`（限定名 SCODE 解析 + 求值）。期望 = omc 1.25.0。
  const char* src =
      "package P\n  constant Real c = 2.0;\n  constant Real d = c + 1.0;\nend P;\n"
      "model M\n  Real x = P.c;\n  Real y = P.d;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string("class M\n  Real x = 2.0;\n  Real y = 3.0;\nend M;\n"));
}

YMC_TEST(flatten_nested_package_constant) {
  // 嵌套包 `P.Q.n` + 上层引用 `m = Q.n + 1`（作用域链下钻 + 内联）。
  const char* src =
      "package P\n  package Q\n    constant Integer n = 3;\n  end Q;\n  constant Integer m = Q.n + 1;\nend P;\n"
      "model M\n  Integer a = P.Q.n;\n  Real b = P.m;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string("class M\n  Integer a = 3;\n  Real b = 4.0;\nend M;\n"));
}

YMC_TEST(flatten_structural_param_fixed_final) {
  // 参数用在 fixed → 求值成字面量 + 该参数及依赖标 final parameter（ceval3）。期望 = omc 1.25.0。
  const char* src =
      "model M\n  Real x(start = 2.0, fixed = c);\n"
      "  parameter Boolean c = p or q;\n  parameter Boolean p = false;\n  parameter Boolean q = true;\n"
      "equation\n  der(x) = -1;\nend M;\n";
  const char* want =
      "class M\n  Real x(start = 2.0, fixed = true);\n"
      "  final parameter Boolean c = true;\n  final parameter Boolean p = false;\n"
      "  final parameter Boolean q = true;\nequation\n  der(x) = -1.0;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_cross_package_extends) {
  // 跨包 extends：`extends P.Base` 把 P.Base 的成员并进来。期望 = omc 1.25.0。
  const char* src =
      "package P\n  model Base\n    Real x = 1.0;\n    Real y;\n  end Base;\nend P;\n"
      "model M\n  extends P.Base;\n  Real z = x;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real x = 1.0;\n  Real y;\n  Real z = x;\nend M;\n"));
}

YMC_TEST(flatten_array_param_and_size_dimension) {
  // 数组维度求值：参数维 `x[n]`（n→3 + n 标 final）、`y[size(x,1)]`（size 求值）。期望 = omc 1.25.0（Dim1/DimSize1）。
  const char* src =
      "model M\n  parameter Integer n = 3;\n  Real x[n];\n  Real y[size(x, 1)];\nend M;\n";
  const char* want =
      "class M\n  final parameter Integer n = 3;\n"
      "  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y[1];\n  Real y[2];\n  Real y[3];\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_connector_unconnected_flow) {
  // 连接器摊开成员 + 未连接 flow → `c.f = 0.0`（R13 §6-2）。期望 = omc 1.25.0（Connect1）。
  const char* src =
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model M\n  C c;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real c.e;\n  Real c.f;\nequation\n  c.f = 0.0;\nend M;\n"));
}

YMC_TEST(flatten_record_constructor_binding) {
  // 记录构造整体绑定 `R r = R(4,5)` = 类修饰 `R r(x=4,y=5)`：位置实参按声明序摊到字段，缺省实参保留默认。
  // 期望 = omc 1.25.0（RecordBinding1）。
  const char* src =
      "record R\n  Real x = 1.0;\n  Real y = 2.0;\n  Real z = 3.0;\nend R;\n"
      "model M\n  R r = R(4.0, 5.0);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real r.x = 4.0;\n  Real r.y = 5.0;\n  Real r.z = 3.0;\nend M;\n"));
}

YMC_TEST(flatten_record_constructor_named_arg) {
  // 命名实参 `R(y=5)` 按名匹配字段；未给的 x/z 保留默认。期望 = omc 1.25.0。
  const char* src =
      "record R\n  Real x = 1.0;\n  Real y = 2.0;\nend R;\n"
      "model M\n  R r = R(y = 5.0);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real r.x = 1.0;\n  Real r.y = 5.0;\nend M;\n"));
}

YMC_TEST(flatten_constant_record_variability_propagates) {
  // `constant R r = R(1.0)` → 字段继承 constant 变率（Half A）。期望值取自实跑 omc 1.25.0。
  YMC_CHECK_EQ(
      flatten_src("record R\n  Real x;\nend R;\nmodel M\n  constant R r = R(1.0);\nend M;\n"),
      std::string("class M\n  constant Real r.x = 1.0;\nend M;\n"));
}

YMC_TEST(flatten_user_function_constant_fold) {
  // 用户函数常量折叠：output 绑定 `y=x` + 算法 `y:=x*2`。期望 = omc 1.25.0（FuncSimple/CevalFunc1）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Real x;\n  output Real y = x;\nend f;\n"
                           "model M\n  Real a = f(1.0);\nend M;\n"),
               std::string("class M\n  Real a = 1.0;\nend M;\n"));
  YMC_CHECK_EQ(flatten_src("function f\n  input Real x;\n  output Real y;\nalgorithm\n  y := x * 2;\nend f;\n"
                           "model M\n  constant Real a = f(3.0);\nend M;\n"),
               std::string("class M\n  constant Real a = 6.0;\nend M;\n"));
}

YMC_TEST(flatten_user_function_default_and_named_args) {
  // 默认参数 + 命名实参：`f(4.0, z=2.0)` 中间参数 y 用默认 1.0 → 4+1+2=7。期望 = omc 1.25.0（FuncDefaultArg1）。
  const char* src =
      "function f\n  input Real x;\n  input Real y = 1.0;\n  input Real z;\n  output Real w = x + y + z;\nend f;\n"
      "model M\n  Real a = f(1.0, 2.0, 3.0);\n  Real b = f(4.0, z = 2.0);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string("class M\n  Real a = 6.0;\n  Real b = 7.0;\nend M;\n"));
}

YMC_TEST(flatten_if_equation_emit_and_fold) {
  // 运行时条件 → 发射 flat if；常量条件 → 折叠选中分支。期望 = omc 1.25.0（IfEquation1 / IfEquationEval2）。
  const char* emit =
      "model M\n  Real x;\nequation\n  if time > 1 then\n    x = 1;\n  elseif time > 2 then\n    x = 2;\n  else\n    x = 3;\n  end if;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(emit),
               std::string("class M\n  Real x;\nequation\n  if time > 1.0 then\n    x = 1.0;\n"
                           "  elseif time > 2.0 then\n    x = 2.0;\n  else\n    x = 3.0;\n  end if;\nend M;\n"));
  const char* fold =
      "model M\n  Real x;\nequation\n  if false then\n    x = 0;\n  elseif true then\n    x = 1;\n  else\n    x = 2;\n  end if;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(fold), std::string("class M\n  Real x;\nequation\n  x = 1.0;\nend M;\n"));
}

YMC_TEST(flatten_noevent_fold_and_continuity_guard) {
  // noEvent(常量)脱壳折叠；Boolean 绑定连续表达式（noEvent(time>1)）是错误 → SKIP（NoEvent1）。期望 = omc 1.25.0（CevalNoEvent1）。
  YMC_CHECK_EQ(flatten_src("model M\n  constant Real r = noEvent(1.0 / 4.0);\nend M;\n"),
               std::string("class M\n  constant Real r = 0.25;\nend M;\n"));
  bool uns = false;
  flatten_src("model M\n  Real x = time;\n  Boolean b = noEvent(x > 1);\nend M;\n", &uns);
  YMC_CHECK(uns);
}

YMC_TEST(flatten_when_equation) {
  // when/elsewhen 方程 + reinit（cond Int→Real、value Int→Real）。期望 = omc 1.25.0（When5/Reinit1）。
  const char* when5 =
      "model M\n  Real x;\nequation\n  when time > 0 then\n    x = 1;\n  elsewhen time > 1 then\n    x = 2;\n  end when;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(when5),
               std::string("class M\n  Real x;\nequation\n  when time > 0.0 then\n    x = 1.0;\n"
                           "  elsewhen time > 1.0 then\n    x = 2.0;\n  end when;\nend M;\n"));
  const char* reinit =
      "model M\n  Real x;\nequation\n  when time > 1 then\n    reinit(x, 2);\n  end when;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(reinit),
               std::string("class M\n  Real x;\nequation\n  when time > 1.0 then\n    reinit(x, 2.0);\n  end when;\nend M;\n"));
}

YMC_TEST(flatten_when_reinit_of_parameter_unsupported) {
  // reinit 目标须连续变量（Var）；参数目标是错误 → SKIP（ReinitInvalid2 邻域）。
  bool uns = false;
  flatten_src("model M\n  parameter Real x = 0.0;\nequation\n  when time > 1 then\n    reinit(x, 2);\n  end when;\nend M;\n", &uns);
  YMC_CHECK(uns);
}

YMC_TEST(flatten_terminate_equation) {
  // terminate(msg) 方程；msg 非 String → SKIP（TerminateInvalid1）。期望 = omc 1.25.0（Terminate1）。
  YMC_CHECK_EQ(flatten_src("model M\nequation\n  terminate(\"test\");\nend M;\n"),
               std::string("class M\nequation\n  terminate(\"test\");\nend M;\n"));
  bool uns = false;
  flatten_src("model M\nequation\n  terminate(1.0);\nend M;\n", &uns);
  YMC_CHECK(uns);
}

YMC_TEST(flatten_assert_equation) {
  // assert 方程：默认 level 省略、cond Int→Real 强转（`time > 1` → `time > 1.0`）。期望 = omc 1.25.0（Assert1）。
  YMC_CHECK_EQ(flatten_src("model M\nequation\n  assert(time > 1, \"test\");\nend M;\n"),
               std::string("class M\nequation\n  assert(time > 1.0, \"test\");\nend M;\n"));
  // 参数类型错（cond 非 Boolean）→ SKIP（AssertInvalid1）。
  bool uns = false;
  flatten_src("model M\nequation\n  assert(\"true\", \"msg\");\nend M;\n", &uns);
  YMC_CHECK(uns);
}

YMC_TEST(flatten_for_equation_expands) {
  // for 方程展开：常量范围逐值展开体方程，`x[i]=i` → `x[1]=1.0`…（迭代变量作下标与值）。期望 = omc 1.25.0（ForEquation1）。
  const char* src =
      "model M\n  Real x[3];\nequation\n  for i in 1:3 loop\n    x[i] = i;\n  end for;\nend M;\n";
  const char* want =
      "class M\n  Real x[1];\n  Real x[2];\n  Real x[3];\n"
      "equation\n  x[1] = 1.0;\n  x[2] = 2.0;\n  x[3] = 3.0;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_for_equation_nested_dependent_range) {
  // 嵌套 for，内层范围依赖外层迭代变量 `i+1:i+2`（eval_scode 解析迭代变量）。期望 = omc 1.25.0（ForEquation7 邻域）。
  const char* src =
      "model M\n  Real y;\nequation\n  for i in 1:2 loop\n    for j in i+1:i+2 loop\n      y = j;\n    end for;\n  end for;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real y;\nequation\n  y = 2.0;\n  y = 3.0;\n  y = 3.0;\n  y = 4.0;\nend M;\n"));
}

YMC_TEST(flatten_array_reduction_constant_fold) {
  // 常量数组规约 sum/product/min/max（含嵌套数组摊平；Int→Int、含 Real→Real）。期望 = omc 1.25.0（CevalSum1/Min2/…）。
  const char* src =
      "model M\n"
      "  constant Integer s = sum({1, 2, 3, 4});\n"
      "  constant Real p = product({1.0, 2.0, 3.0, 4.0});\n"
      "  constant Real lo = min({{3.0, 1.0}, {2.0, 5.0}});\n"
      "  constant Real hi = max({3.0, 1.0, 7.0, 2.0});\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  constant Integer s = 10;\n  constant Real p = 24.0;\n"
                           "  constant Real lo = 1.0;\n  constant Real hi = 7.0;\nend M;\n"));
}

YMC_TEST(flatten_function_returns_record_constant) {
  // 函数返回记录常量：output 字段修饰 `r(x=x,y=y)` 与算法字段赋值 `r.x:=x` 两种；`constant R r = f(…)` 摊字段。
  // 期望 = omc 1.25.0（CevalFuncRecord1/2）。
  const char* out_mod =
      "record R\n  Real x;\n  Real y;\nend R;\n"
      "function f\n  input Real x;\n  input Real y;\n  output R r(x = x, y = y);\nend f;\n"
      "model M\n  constant R r = f(1.0, 2.0);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(out_mod),
               std::string("class M\n  constant Real r.x = 1.0;\n  constant Real r.y = 2.0;\nend M;\n"));
  const char* algo =
      "record R\n  Real x;\n  Real y;\nend R;\n"
      "function f\n  input Real x;\n  input Real y;\n  output R r;\nalgorithm\n  r.x := x;\n  r.y := y;\nend f;\n"
      "model M\n  constant R r = f(1.0, 2.0);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(algo),
               std::string("class M\n  constant Real r.x = 1.0;\n  constant Real r.y = 2.0;\nend M;\n"));
}

YMC_TEST(flatten_user_function_control_flow) {
  // 函数体控制流常量折叠：if/elseif/else、for(累加)、while(倒数累加)。期望 = omc 1.25.0（CevalFuncIf1/For1/While1）。
  const char* if_src =
      "function f\n  input Real x;\n  output Real y;\nalgorithm\n"
      "  if x > 4 then\n    y := 3.0;\n  elseif x > 2 then\n    y := 5.0;\n  else\n    y := 7.0;\n  end if;\nend f;\n"
      "model M\n  constant Real a = f(1.0);\n  constant Real b = f(3.0);\n  constant Real c = f(5.0);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(if_src),
               std::string("class M\n  constant Real a = 7.0;\n  constant Real b = 5.0;\n  constant Real c = 3.0;\nend M;\n"));
  const char* for_src =
      "function f\n  input Integer n;\n  output Integer res;\nalgorithm\n  res := 0;\n"
      "  for i in 1:n loop\n    res := res + i;\n  end for;\nend f;\n"
      "model M\n  constant Integer a = f(10);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(for_src), std::string("class M\n  constant Integer a = 55;\nend M;\n"));
  const char* while_src =
      "function f\n  input Integer n;\n  output Integer res;\nprotected\n  Integer i;\nalgorithm\n"
      "  i := n;\n  res := 0;\n  while i > 0 loop\n    res := res + i;\n    i := i - 1;\n  end while;\nend f;\n"
      "model M\n  constant Integer a = f(10);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(while_src), std::string("class M\n  constant Integer a = 55;\nend M;\n"));
}

YMC_TEST(flatten_function_assign_to_input_unsupported) {
  // 给输入赋值是错误（omc 报错），ymc 折叠时拒绝 → SKIP（AssignInput1）。
  bool uns = false;
  flatten_src("function f\n  input Real x;\n  output Real y;\nalgorithm\n  x := x + 1;\n  y := x;\nend f;\n"
              "model M\n  constant Real a = f(2.0);\nend M;\n", &uns);
  YMC_CHECK(uns);
}

YMC_TEST(flatten_composite_array_scalarizes) {
  // 复合数组 `A a[2]` 逐元素以 `a[i]` 前缀展开成员；元素内 cref（`z = x`）带段下标 → `a[i].z = a[i].x`。
  // 期望 = omc 1.25.0（Prefix1 邻域）。
  const char* src =
      "model A\n  Real x;\n  Real z = x;\nend A;\n"
      "model M\n  A a[2];\nend M;\n";
  const char* want =
      "class M\n  Real a[1].x;\n  Real a[1].z = a[1].x;\n  Real a[2].x;\n  Real a[2].z = a[2].x;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_composite_array_modifier_distributes) {
  // 数组修饰 `A a[3](x={1,2,3})` 逐元素分发：a[i].x = 第 i 个值。期望 = omc 1.25.0（BindingArray1）。
  const char* src =
      "model A\n  Real x = 1.0;\nend A;\n"
      "model M\n  A a[3](x = {1, 2, 3});\nend M;\n";
  const char* want =
      "class M\n  Real a[1].x = 1.0;\n  Real a[2].x = 2.0;\n  Real a[3].x = 3.0;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_composite_array_element_ref) {
  // 表达式里显式下标引用复合数组元素 `a[1].x`：首段常量下标、在界内 → 照印（期望 = omc 1.25.0 实跑 CA1，2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model A\n  Real x;\nend A;\nmodel M\n  A a[2];\n  Real y = a[1].x;\nend M;\n"),
               std::string("class M\n  Real a[1].x;\n  Real a[2].x;\n  Real y = a[1].x;\nend M;\n"));
  // 越界 `a[3].x`：1.25.0 末段是标量时不查界（照印），末段带下标时报错（ArrayBounds2）——不一致，ymc 一律保守 SKIP。
  bool uns = false;
  std::string t = flatten_src(
      "model A\n  Real x;\nend A;\nmodel M\n  A a[2];\n  Real y = a[3].x;\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
}

YMC_TEST(flatten_param_array_binding_spreads_to_elements) {
  // 参数/常量数组绑定摊到每元素（Var 才留数组方程）；矩阵行含 cref 数组 → `x[i]` 元素引用。期望 = omc 1.25.0（BindingArray8）。
  const char* src =
      "model M\n  parameter Real x[4] = {1, 2, 3, 4};\n  parameter Real y[2, 4] = {x, {5, 6, 7, 8}};\nend M;\n";
  const char* want =
      "class M\n"
      "  parameter Real x[1] = 1.0;\n  parameter Real x[2] = 2.0;\n  parameter Real x[3] = 3.0;\n  parameter Real x[4] = 4.0;\n"
      "  parameter Real y[1,1] = x[1];\n  parameter Real y[1,2] = x[2];\n  parameter Real y[1,3] = x[3];\n  parameter Real y[1,4] = x[4];\n"
      "  parameter Real y[2,1] = 5.0;\n  parameter Real y[2,2] = 6.0;\n  parameter Real y[2,3] = 7.0;\n  parameter Real y[2,4] = 8.0;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_conditional_component_true_false) {
  // 条件组件：`if true` 保留（抹掉条件）、`if false` 移除。期望 = omc 1.25.0（Condition1/2）。
  YMC_CHECK_EQ(flatten_src("model M\n  Real x if true;\n  Real y if false;\nend M;\n"),
               std::string("class M\n  Real x;\nend M;\n"));
}

YMC_TEST(flatten_conditional_param_becomes_final) {
  // `x if b`（b 参数）：b 求值决定去留，且 b 变结构 final parameter。期望 = omc 1.25.0（Condition3 结构）。
  const char* src =
      "model M\n  parameter Boolean b1 = true;\n  parameter Boolean b2 = false;\n"
      "  Real x if b1;\n  Real y if b2;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  final parameter Boolean b1 = true;\n"
                           "  final parameter Boolean b2 = false;\n  Real x;\nend M;\n"));
}

YMC_TEST(flatten_conditional_connector_drops_connect) {
  // `C c3 if false` 移除 + 引用它的 connect 整条丢弃 → 只剩 connect(c1,c2)。期望 = omc 1.25.0（Condition5）。
  const char* src =
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model M\n  C c1;\n  C c2;\n  C c3 if false;\n"
      "equation\n  connect(c1, c2);\n  connect(c2, c3);\n  connect(c3, c1);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real c1.e;\n  Real c1.f;\n  Real c2.e;\n  Real c2.f;\n"
                           "equation\n  c1.e = c2.e;\n  -(c1.f + c2.f) = 0.0;\n  c1.f = 0.0;\n  c2.f = 0.0;\nend M;\n"));
}

YMC_TEST(flatten_reference_to_removed_component_unsupported) {
  // 方程/绑定引用条件为假被移除的组件 → omc 报错，ymc 无诊断 → 干净 SKIP（ConditionInvalidContext4）。
  bool uns = false;
  std::string t = flatten_src("model M\n  Real x if false;\n  Real y = x;\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
}

YMC_TEST(flatten_connect_both_outside) {
  // connect 两个顶层连接器：potential 相等 + flow 求和（both outside → -(a+b)）+ 各自边界 =0。期望 = omc 1.25.0（Connect2）。
  const char* src =
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model M\n  C c1, c2;\nequation\n  connect(c1, c2);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real c1.e;\n  Real c1.f;\n  Real c2.e;\n  Real c2.f;\n"
                           "equation\n  c1.e = c2.e;\n  -(c1.f + c2.f) = 0.0;\n  c1.f = 0.0;\n  c2.f = 0.0;\nend M;\n"));
}

YMC_TEST(flatten_connect_inside_outside) {
  // connect(内部连接器, 顶层连接器)：inside 取正、outside 取负 → `a.c1.f - c2.f = 0`；只有 outside 的 c2 得边界 =0。
  // inside 的 a.c1 已连接 → 不单列 =0。期望 = omc 1.25.0（Connect5 结构）。
  const char* src =
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model A\n  C c1;\nend A;\n"
      "model M\n  A a;\n  C c2;\nequation\n  connect(a.c1, c2);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real a.c1.e;\n  Real a.c1.f;\n  Real c2.e;\n  Real c2.f;\n"
                           "equation\n  a.c1.e = c2.e;\n  a.c1.f - c2.f = 0.0;\n  c2.f = 0.0;\nend M;\n"));
}

YMC_TEST(flatten_for_connect_array_disjoint) {
  // for 循环里逐元素 connect 数组连接器（`connect(c1[i], c2[i])`）：3 条互不相交 connect，逐 i 交替发射
  // potential 相等 + flow 求和，再按声明序补 flow 边界 =0。期望 = omc 1.25.0 实跑（ForConnect1，逐字节）。
  const char* src =
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model ForConnect1\n  C c1[3], c2[3];\n"
      "equation\n  for i in 1:3 loop\n    connect(c1[i], c2[i]);\n  end for;\nend ForConnect1;\n";
  YMC_CHECK_EQ(
      flatten_src(src),
      std::string("class ForConnect1\n"
                  "  Real c1[1].e;\n  Real c1[1].f;\n  Real c1[2].e;\n  Real c1[2].f;\n"
                  "  Real c1[3].e;\n  Real c1[3].f;\n  Real c2[1].e;\n  Real c2[1].f;\n"
                  "  Real c2[2].e;\n  Real c2[2].f;\n  Real c2[3].e;\n  Real c2[3].f;\n"
                  "equation\n"
                  "  c1[1].e = c2[1].e;\n  -(c1[1].f + c2[1].f) = 0.0;\n"
                  "  c1[2].e = c2[2].e;\n  -(c1[2].f + c2[2].f) = 0.0;\n"
                  "  c1[3].e = c2[3].e;\n  -(c1[3].f + c2[3].f) = 0.0;\n"
                  "  c1[1].f = 0.0;\n  c1[2].f = 0.0;\n  c1[3].f = 0.0;\n"
                  "  c2[1].f = 0.0;\n  c2[2].f = 0.0;\n  c2[3].f = 0.0;\nend ForConnect1;\n"));
}

YMC_TEST(flatten_whole_array_connect_array_members) {
  // 整体数组连接 `connect(c1, c2)`（c1,c2: C[2]）+ 数组成员 e[2]/f[2]：实参展开成元素按位配对
  // （makeConnections，NFConnections.mo:200-206），成员逐元素摊（Connection.scalarize）；顺序 = 连接器元素 →
  // potential → flow → 成员元素；flow 边界 =0 按变量声明序。期望 = omc 1.25.0 实跑（Connect4，逐字节）。
  const char* src =
      "connector C\n  Real e[2];\n  flow Real f[2];\nend C;\n"
      "model Connect4\n  C c1[2], c2[2];\nequation\n  connect(c1, c2);\nend Connect4;\n";
  YMC_CHECK_EQ(
      flatten_src(src),
      std::string("class Connect4\n"
                  "  Real c1[1].e[1];\n  Real c1[1].e[2];\n  Real c1[1].f[1];\n  Real c1[1].f[2];\n"
                  "  Real c1[2].e[1];\n  Real c1[2].e[2];\n  Real c1[2].f[1];\n  Real c1[2].f[2];\n"
                  "  Real c2[1].e[1];\n  Real c2[1].e[2];\n  Real c2[1].f[1];\n  Real c2[1].f[2];\n"
                  "  Real c2[2].e[1];\n  Real c2[2].e[2];\n  Real c2[2].f[1];\n  Real c2[2].f[2];\n"
                  "equation\n"
                  "  c1[1].e[1] = c2[1].e[1];\n  c1[1].e[2] = c2[1].e[2];\n"
                  "  -(c1[1].f[1] + c2[1].f[1]) = 0.0;\n  -(c1[1].f[2] + c2[1].f[2]) = 0.0;\n"
                  "  c1[2].e[1] = c2[2].e[1];\n  c1[2].e[2] = c2[2].e[2];\n"
                  "  -(c1[2].f[1] + c2[2].f[1]) = 0.0;\n  -(c1[2].f[2] + c2[2].f[2]) = 0.0;\n"
                  "  c1[1].f[1] = 0.0;\n  c1[1].f[2] = 0.0;\n  c1[2].f[1] = 0.0;\n  c1[2].f[2] = 0.0;\n"
                  "  c2[1].f[1] = 0.0;\n  c2[1].f[2] = 0.0;\n  c2[2].f[1] = 0.0;\n  c2[2].f[2] = 0.0;\n"
                  "end Connect4;\n"));
}

YMC_TEST(flatten_connect_members_sorted_by_name) {
  // 连接方程里的成员序 = 每组按名**升序**（potential 组、flow 组各自），不是声明序：makeConnectorType 遍历
  // ClassTree.enumerateComponents（按名 LookupTree 折叠，NFTyping.mo:339-354 / NFClassTree.mo:1418-1444）。
  // 变量行与 flow 边界 =0 仍按声明序。期望 = omc 1.25.0 实跑 YmcOrder.Flat（2026-09-27，逐字节）。
  const char* src =
      "connector C\n  Real e2;\n  Real e1;\n  flow Real f2;\n  flow Real f1;\nend C;\n"
      "model M\n  C a, b;\nequation\n  connect(a, b);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real a.e2;\n  Real a.e1;\n  Real a.f2;\n  Real a.f1;\n"
                           "  Real b.e2;\n  Real b.e1;\n  Real b.f2;\n  Real b.f1;\n"
                           "equation\n  a.e1 = b.e1;\n  a.e2 = b.e2;\n"
                           "  -(a.f1 + b.f1) = 0.0;\n  -(a.f2 + b.f2) = 0.0;\n"
                           "  a.f2 = 0.0;\n  a.f1 = 0.0;\n  b.f2 = 0.0;\n  b.f1 = 0.0;\nend M;\n"));
}

YMC_TEST(flatten_connect_nested_connector) {
  // 嵌套连接器 `N { C q; C p; }`：子连接器在 potential 位按名升序就地展开（splitImpl 递归，NFConnector.mo:342-348），
  // 每层 potential 在前 flow 在后。期望 = omc 1.25.0 实跑 YmcOrder.Nested（2026-09-27，逐字节）。
  const char* src =
      "connector C\n  Real e2;\n  Real e1;\n  flow Real f2;\n  flow Real f1;\nend C;\n"
      "connector N\n  C q;\n  C p;\nend N;\n"
      "model M\n  N n1, n2;\nequation\n  connect(n1, n2);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n"
                           "  Real n1.q.e2;\n  Real n1.q.e1;\n  Real n1.q.f2;\n  Real n1.q.f1;\n"
                           "  Real n1.p.e2;\n  Real n1.p.e1;\n  Real n1.p.f2;\n  Real n1.p.f1;\n"
                           "  Real n2.q.e2;\n  Real n2.q.e1;\n  Real n2.q.f2;\n  Real n2.q.f1;\n"
                           "  Real n2.p.e2;\n  Real n2.p.e1;\n  Real n2.p.f2;\n  Real n2.p.f1;\n"
                           "equation\n"
                           "  n1.p.e1 = n2.p.e1;\n  n1.p.e2 = n2.p.e2;\n"
                           "  -(n1.p.f1 + n2.p.f1) = 0.0;\n  -(n1.p.f2 + n2.p.f2) = 0.0;\n"
                           "  n1.q.e1 = n2.q.e1;\n  n1.q.e2 = n2.q.e2;\n"
                           "  -(n1.q.f1 + n2.q.f1) = 0.0;\n  -(n1.q.f2 + n2.q.f2) = 0.0;\n"
                           "  n1.q.f2 = 0.0;\n  n1.q.f1 = 0.0;\n  n1.p.f2 = 0.0;\n  n1.p.f1 = 0.0;\n"
                           "  n2.q.f2 = 0.0;\n  n2.q.f1 = 0.0;\n  n2.p.f2 = 0.0;\n  n2.p.f1 = 0.0;\nend M;\n"));
}

YMC_TEST(flatten_connect_nested_conditional_members) {
  // 条件为假的子连接器成员：含它的配对整对丢弃（Connection.split，NFConnection.mo:68）；三条 connect 共享连接器 c2
  // 但标量叶不相交 → 仍可不用并查集。期望 = omc 1.25.0（Condition6，2026-09-27 实跑与文件一致）。
  const char* src =
      "connector C1\n  Real e;\n  flow Real f;\nend C1;\n"
      "connector C2\n  C1 c1;\n  C1 c2;\nend C2;\n"
      "connector C3\n  C1 c1 if true;\n  C1 c2 if false;\nend C3;\n"
      "connector C4\n  C1 c1 if false;\n  C1 c2 if true;\nend C4;\n"
      "model Condition6\n  C2 c2;\n  C3 c3;\n  C4 c4;\n"
      "equation\n  connect(c2, c3);\n  connect(c2, c4);\n  connect(c3, c4);\nend Condition6;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class Condition6\n"
                           "  Real c2.c1.e;\n  Real c2.c1.f;\n  Real c2.c2.e;\n  Real c2.c2.f;\n"
                           "  Real c3.c1.e;\n  Real c3.c1.f;\n  Real c4.c2.e;\n  Real c4.c2.f;\n"
                           "equation\n"
                           "  c2.c1.e = c3.c1.e;\n  -(c2.c1.f + c3.c1.f) = 0.0;\n"
                           "  c2.c2.e = c4.c2.e;\n  -(c2.c2.f + c4.c2.f) = 0.0;\n"
                           "  c2.c1.f = 0.0;\n  c2.c2.f = 0.0;\n  c3.c1.f = 0.0;\n  c4.c2.f = 0.0;\nend Condition6;\n"));
}

YMC_TEST(flatten_connect_all_members_removed_empty_class) {
  // 连接器成员全部条件为假：无变量、无配对 → 空类（无 equation 段）。期望 = omc 1.25.0（Condition9）。
  const char* src =
      "connector C\n  Real x if false;\nend C;\n"
      "connector C2\n  C c if false;\nend C2;\n"
      "model Condition9\n  C2 c1;\n  C2 c2;\nequation\n  connect(c1, c2);\nend Condition9;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string("class Condition9\nend Condition9;\n"));
}

YMC_TEST(flatten_connect_short_class_connector_slice) {
  // 短类连接器 `connector InReal = input Real`：组件本身即唯一标量叶（splitImpl 非 COMPLEX 分支，NFConnector.mo:366）；
  // 切片实参 `y[2:p+1]` 按 Subscript.expandList 展成 y[2],y[3],y[4] 与整体 x 的 x[1..3] 按位配对（makeConnections）。
  // 无 flow → 只有 potential 相等。期望 = omc 1.25.0（ConnectArrays2，2026-09-27 实跑与文件一致）。
  const char* src =
      "model ConnectArrays2\n"
      "  connector InReal = input Real;\n  connector OutReal = output Real;\n"
      "  parameter Integer p = 3;\n  InReal x[p];\n  OutReal y[p+1];\n"
      "equation\n  connect(x,y[2:p+1]);\nend ConnectArrays2;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class ConnectArrays2\n  final parameter Integer p = 3;\n"
                           "  input Real x[1];\n  input Real x[2];\n  input Real x[3];\n"
                           "  output Real y[1];\n  output Real y[2];\n  output Real y[3];\n  output Real y[4];\n"
                           "equation\n  x[1] = y[2];\n  x[2] = y[3];\n  x[3] = y[4];\nend ConnectArrays2;\n"));
}

YMC_TEST(flatten_connect_descending_range_subscript) {
  // 范围下标按其自身顺序展开（Subscript.expandSlice → ExpandExp.expand，NFSubscript.mo:1053-1079）：
  // `c1[3:-1:1]` 与整体 c2 按位配对 → c1[3]~c2[1], c1[2]~c2[2], c1[1]~c2[3]。期望 = omc 1.25.0 实跑（YmcSlice.Desc）。
  const char* src =
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model M\n  C c1[3], c2[3];\nequation\n  connect(c1[3:-1:1], c2);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n"
                           "  Real c1[1].e;\n  Real c1[1].f;\n  Real c1[2].e;\n  Real c1[2].f;\n  Real c1[3].e;\n  Real c1[3].f;\n"
                           "  Real c2[1].e;\n  Real c2[1].f;\n  Real c2[2].e;\n  Real c2[2].f;\n  Real c2[3].e;\n  Real c2[3].f;\n"
                           "equation\n"
                           "  c1[3].e = c2[1].e;\n  -(c1[3].f + c2[1].f) = 0.0;\n"
                           "  c1[2].e = c2[2].e;\n  -(c1[2].f + c2[2].f) = 0.0;\n"
                           "  c1[1].e = c2[3].e;\n  -(c1[1].f + c2[3].f) = 0.0;\n"
                           "  c1[1].f = 0.0;\n  c1[2].f = 0.0;\n  c1[3].f = 0.0;\n"
                           "  c2[1].f = 0.0;\n  c2[2].f = 0.0;\n  c2[3].f = 0.0;\nend M;\n"));
}

YMC_TEST(flatten_direction_kept_only_under_connector_or_input_prefix) {
  // 方向保留规则（Variable.removeNonTopLevelDirection，NFVariable.mo:466-493）：顶层变量保留；前缀全是连接器/input 组件
  // 才保留（顶层连接器成员 `c.e`）；经过普通模型 `a` 的一律抹掉（`a.x`、`a.c.e`）。期望 = omc 1.25.0（Direction1，实跑与文件一致）。
  const char* src =
      "model A\n  input Real x;\n  C c;\nend A;\n"
      "connector C\n  input Real e;\nend C;\n"
      "model Direction1\n  output Real x;\n  A a;\n  C c;\nend Direction1;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class Direction1\n  output Real x;\n  Real a.x;\n  Real a.c.e;\n  input Real c.e;\nend Direction1;\n"));
}

YMC_TEST(flatten_connect_record_members_flow_pushdown) {
  // 连接器里的记录成员：字段按**声明序**拆叶（splitImpl 记录分支 ClassTree.getComponents，NFConnector.mo:354-360，
  // 不像连接器成员那样按名排序）；`flow R f` 的字段全部继承 flow（ConnectorType.merge，NFAttributes.mo:229-234）。
  // 未连接 → 每个字段 `= 0.0`。期望 = omc 1.25.0 实跑（RecConn 探针，2026-09-27，逐字节）。
  const char* src =
      "record R\n  Real r2;\n  Real r1;\nend R;\n"
      "connector C\n  R e;\n  flow R f;\nend C;\n"
      "model A\n  C c;\nend A;\n"
      "model M\n  A m1;\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real m1.c.e.r2;\n  Real m1.c.e.r1;\n  Real m1.c.f.r2;\n  Real m1.c.f.r1;\n"
                           "equation\n  m1.c.f.r2 = 0.0;\n  m1.c.f.r1 = 0.0;\nend M;\n"));
}

YMC_TEST(flatten_connect_record_members_connected) {
  // 两个 inside 连接器的记录成员相连：potential 字段相等、flow 字段求和（both-inside 反序 B+A = 集合插入序），
  // 已连接 → 不再单列 =0。期望 = omc 1.25.0（ConnectComplex1，实跑与文件一致）。
  const char* src =
      "record R\n  Real r1;\n  Real r2;\nend R;\n"
      "connector C\n  R e;\n  flow R f;\nend C;\n"
      "model M\n  C c;\nend M;\n"
      "model ConnectComplex1\n  M m1, m2;\nequation\n  connect(m1.c, m2.c);\nend ConnectComplex1;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class ConnectComplex1\n"
                           "  Real m1.c.e.r1;\n  Real m1.c.e.r2;\n  Real m1.c.f.r1;\n  Real m1.c.f.r2;\n"
                           "  Real m2.c.e.r1;\n  Real m2.c.e.r2;\n  Real m2.c.f.r1;\n  Real m2.c.f.r2;\n"
                           "equation\n  m1.c.e.r1 = m2.c.e.r1;\n  m1.c.e.r2 = m2.c.e.r2;\n"
                           "  m2.c.f.r1 + m1.c.f.r1 = 0.0;\n  m2.c.f.r2 + m1.c.f.r2 = 0.0;\nend ConnectComplex1;\n"));
}

// ---- 连接集（DisjointSets）顺序：全部期望来自 omc 1.25.0 实跑（2026-09-27），规则见 docs/research/connect-sets-order.md ----

YMC_TEST(flatten_connect_shared_endpoint_chain) {
  // 共享端点 c2：potential 集 head = 集合首元素（插入序 c2 先于 c3、c1），flow 集三项求和全 outside → `-(…)`；
  // 集合输出序 = 根节点下标反序（DisjointSets.union 秩规则 + extractSets）。期望 = omc（ChainOut 探针）。
  const char* src =
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model M\n  C c1, c2, c3;\nequation\n  connect(c1, c2);\n  connect(c2, c3);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real c1.e;\n  Real c1.f;\n  Real c2.e;\n  Real c2.f;\n  Real c3.e;\n  Real c3.f;\n"
                           "equation\n  c2.e = c3.e;\n  c2.e = c1.e;\n  -(c2.f + c3.f + c1.f) = 0.0;\n"
                           "  c1.f = 0.0;\n  c2.f = 0.0;\n  c3.f = 0.0;\nend M;\n"));
}

YMC_TEST(flatten_connect_order_inside_outside_declared_first) {
  // "不相交简化"的反例（OrderTrap 探针）：outside 端 p 先声明 → 其 INSIDE 单元素集 p.i 节点下标高于合并集的根
  //（inside 端 a.c.i 预插入且为根）→ 反序输出时 `p.i = 0.0` 排在合并集方程之前。
  const char* src =
      "connector C\n  Real v;\n  flow Real i;\nend C;\n"
      "model A\n  C c;\nend A;\n"
      "model M\n  C p;\n  A a;\nequation\n  connect(a.c, p);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real p.v;\n  Real p.i;\n  Real a.c.v;\n  Real a.c.i;\n"
                           "equation\n  a.c.v = p.v;\n  p.i = 0.0;\n  a.c.i - p.i = 0.0;\nend M;\n"));
}

YMC_TEST(flatten_connect_equations_precede_model_equations) {
  // 连接方程整体置于模型自身方程之前（NFFlatten.mo:2497 listAppend(conn_eql, flatModel.equations)；EqPlace 探针）。
  const char* src =
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model M\n  C c1, c2;\n  Real x;\nequation\n  x = 1;\n  connect(c1, c2);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real c1.e;\n  Real c1.f;\n  Real c2.e;\n  Real c2.f;\n  Real x;\n"
                           "equation\n  c1.e = c2.e;\n  -(c1.f + c2.f) = 0.0;\n  c1.f = 0.0;\n  c2.f = 0.0;\n  x = 1.0;\nend M;\n"));
}

YMC_TEST(flatten_connect_in_submodel_local_face) {
  // 子模型内的 connect(p, q)：面按书写类本地判定（crefFace/firstNonScope，NFComponentRef.mo:429-440）→ 两端 outside
  // `-(R1.p.f + R1.q.f)`；顶层 connect(R1.p, p) 里 R1.p 是 inside。集合序与 flow=0 交错同 SubConn 探针。
  const char* src =
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model Res\n  C p;\n  C q;\n  Integer n;\nequation\n  connect(p, q);\n  n = 1;\nend Res;\n"
      "model M\n  C p;\n  Res R1, R2;\nequation\n  connect(R1.p, p);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real p.e;\n  Real p.f;\n  Real R1.p.e;\n  Real R1.p.f;\n  Real R1.q.e;\n  Real R1.q.f;\n"
                           "  Integer R1.n;\n  Real R2.p.e;\n  Real R2.p.f;\n  Real R2.q.e;\n  Real R2.q.f;\n  Integer R2.n;\n"
                           "equation\n  R1.p.e = R1.q.e;\n  -(R1.p.f + R1.q.f) = 0.0;\n  R2.p.e = R2.q.e;\n  -(R2.p.f + R2.q.f) = 0.0;\n"
                           "  R1.p.e = p.e;\n  p.f = 0.0;\n  R1.p.f - p.f = 0.0;\n  R1.q.f = 0.0;\n  R2.p.f = 0.0;\n  R2.q.f = 0.0;\n"
                           "  R1.n = 1;\n  R2.n = 1;\nend M;\n"));
}

YMC_TEST(flatten_connect_in_submodel_array_element) {
  // 复合数组元素内的 connect：书写处前缀带下标（ConnectStmt.psubs）→ `R[1].p.e = R[1].q.e`；顶层 connect(R[1].p, p)。
  // p 声明在 R 之后 → p.f 的 INSIDE 单元素集节点下标最低 → `p.f = 0.0` 排在最后（ArrSub 探针）。
  const char* src =
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model Res\n  C p;\n  C q;\n  Integer n;\nequation\n  connect(p, q);\n  n = 1;\nend Res;\n"
      "model M\n  Res R[2];\n  C p;\nequation\n  connect(R[1].p, p);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real R[1].p.e;\n  Real R[1].p.f;\n  Real R[1].q.e;\n  Real R[1].q.f;\n  Integer R[1].n;\n"
                           "  Real R[2].p.e;\n  Real R[2].p.f;\n  Real R[2].q.e;\n  Real R[2].q.f;\n  Integer R[2].n;\n"
                           "  Real p.e;\n  Real p.f;\n"
                           "equation\n  R[1].p.e = R[1].q.e;\n  -(R[1].p.f + R[1].q.f) = 0.0;\n"
                           "  R[2].p.e = R[2].q.e;\n  -(R[2].p.f + R[2].q.f) = 0.0;\n  R[1].p.e = p.e;\n  R[1].p.f - p.f = 0.0;\n"
                           "  R[1].q.f = 0.0;\n  R[2].p.f = 0.0;\n  R[2].q.f = 0.0;\n  p.f = 0.0;\n  R[1].n = 1;\n  R[2].n = 1;\nend M;\n"));
}

// ---- cardinality（NFCardinalityTable / evaluateConnectionOperators / SimplifyModel）；期望 = omc 1.25.0（文件与实跑一致）----

YMC_TEST(flatten_cardinality_assert_true_removed) {
  // 未连接：cardinality(c) = 0（getOrDefault），`assert(0 == 0, …)` 折成 true 后删除（NFSimplifyModel.mo:215-219）。Cardinality1。
  const char* src =
      "model Cardinality1\n  connector C\n    Real e;\n    flow Real f;\n  end C;\n  C c;\n"
      "equation\n  assert(cardinality(c) == 0, \"cardinality(c) should be 0\");\nend Cardinality1;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class Cardinality1\n  Real c.e;\n  Real c.f;\nequation\n  c.f = 0.0;\nend Cardinality1;\n"));
}

YMC_TEST(flatten_cardinality_if_fold_in_submodel) {
  // 子模型内 `if cardinality(p) == 1 … elseif == 2 …`：计数按元素级连接（R1.p 在子模型内 connect(p,q) 与顶层
  // connect(R1.p, p) 各出现一次 → 2；R2.p → 1），替换后按 simplifyIfEqBranches 折叠：假分支丢弃、真分支内联体。
  // 顺序：连接集方程（含 `p.i = 0.0` 夹在合并集之前）在前，折叠出的 `R1.n_conn = 2; R2.n_conn = 1` 在后。Cardinality2。
  const char* src =
      "connector Pin\n  Real v;\n  flow Real i;\nend Pin;\n"
      "model Resistor\n  Pin p;\n  Pin n;\n  Pin q;\n  Integer n_conn;\nequation\n  connect(p, q);\n"
      "  if cardinality(p) == 1 then\n    n_conn = 1;\n  elseif cardinality(p) == 2 then\n    n_conn = 2;\n"
      "  else\n    n_conn = 0;\n  end if;\nend Resistor;\n"
      "model Cardinality2\n  Pin p;\n  Resistor R1, R2;\nequation\n  connect(R1.p, p);\nend Cardinality2;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class Cardinality2\n  Real p.v;\n  Real p.i;\n  Real R1.p.v;\n  Real R1.p.i;\n  Real R1.n.v;\n"
                           "  Real R1.n.i;\n  Real R1.q.v;\n  Real R1.q.i;\n  Integer R1.n_conn;\n  Real R2.p.v;\n  Real R2.p.i;\n"
                           "  Real R2.n.v;\n  Real R2.n.i;\n  Real R2.q.v;\n  Real R2.q.i;\n  Integer R2.n_conn;\n"
                           "equation\n  R1.p.v = R1.q.v;\n  -(R1.p.i + R1.q.i) = 0.0;\n  R2.p.v = R2.q.v;\n  -(R2.p.i + R2.q.i) = 0.0;\n"
                           "  R1.p.v = p.v;\n  p.i = 0.0;\n  R1.p.i - p.i = 0.0;\n  R1.n.i = 0.0;\n  R1.q.i = 0.0;\n  R2.p.i = 0.0;\n"
                           "  R2.n.i = 0.0;\n  R2.q.i = 0.0;\n  R1.n_conn = 2;\n  R2.n_conn = 1;\nend Cardinality2;\n"));
}

YMC_TEST(flatten_cardinality_short_class_connectors_shared) {
  // 顶层短类连接器 `connector BooleanInput = input Boolean`（find_class_elem 查顶层 program）；u 在两条 connect 里 → 2，
  // and1.u1/u2 各 1 → 三条 assert 全删；共享端点 u 的 potential 集 head=u（`u = and1.u2; u = and1.u1`）。Cardinality3。
  const char* src =
      "connector BooleanInput = input Boolean;\nconnector BooleanOutput = output Boolean;\n"
      "block And\n  BooleanInput u1;\n  BooleanInput u2;\n  BooleanOutput y;\nequation\n  y = u1 and u2;\nend And;\n"
      "model Cardinality3\n  BooleanInput u;\n  And and1;\nequation\n"
      "  assert(cardinality(u) == 2, \"cardinality(u) should be 2\");\n"
      "  assert(cardinality(and1.u1) == 1, \"cardinality(and1.u1) should be 1\");\n"
      "  assert(cardinality(and1.u2) == 1, \"cardinality(and1.u2) should be 1\");\n"
      "  connect(u, and1.u1);\n  connect(u, and1.u2);\nend Cardinality3;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class Cardinality3\n  input Boolean u;\n  Boolean and1.u1;\n  Boolean and1.u2;\n  Boolean and1.y;\n"
                           "equation\n  u = and1.u2;\n  u = and1.u1;\n  and1.y = and1.u1 and and1.u2;\nend Cardinality3;\n"));
}

YMC_TEST(flatten_cardinality_array_host_element) {
  // `cardinality(a1[1].c)` 与整体宿主数组连接 connect(a1.c, a2.c)（宿主段缺省下标取全部）：a1[1].c 出现 1 次 → assert 删。
  // 集合序：inside flow 预插入（根下标低）→ 两个 potential 集先于两个 flow 集。Cardinality4。
  const char* src =
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model A\n  C c;\nend A;\n"
      "model Cardinality4\n  A a1[2], a2[2];\nequation\n"
      "  assert(cardinality(a1[1].c) == 1, \"cardinality(a1[1].c) should be 1\");\n  connect(a1.c, a2.c);\nend Cardinality4;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class Cardinality4\n  Real a1[1].c.e;\n  Real a1[1].c.f;\n  Real a1[2].c.e;\n  Real a1[2].c.f;\n"
                           "  Real a2[1].c.e;\n  Real a2[1].c.f;\n  Real a2[2].c.e;\n  Real a2[2].c.f;\n"
                           "equation\n  a1[1].c.e = a2[1].c.e;\n  a1[2].c.e = a2[2].c.e;\n"
                           "  a2[1].c.f + a1[1].c.f = 0.0;\n  a2[2].c.f + a1[2].c.f = 0.0;\nend Cardinality4;\n"));
}

YMC_TEST(flatten_cardinality_outside_condition_unsupported) {
  // cardinality 只能出现在 if 方程/assert 的条件里（omc 报错 CardinalityInvalidContext1/2）→ 干净 SKIP。
  bool uns = false;
  std::string t = flatten_src(
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model M\n  C c;\n  Integer n = cardinality(c);\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
}

YMC_TEST(flatten_conflicting_direction_prefix_unsupported) {
  // 短类链上方向前缀冲突：`type RealOutput = output RealInput`，RealInput = input Real → omc 1.25.0 报错
  //（Prefixes.mergeDirection，NFPrefixes.mo:588-604；type4/ClassAttributes4/type5/conn8 实跑均报错）→ 干净 SKIP；
  // 同向重复（allowSame=true，NFAttributes.mo:274）不算冲突。
  bool uns = false;
  std::string t = flatten_src(
      "type RealInput = input Real;\ntype RealOutput = output RealInput;\n"
      "model A\n  RealInput ri;\n  RealOutput ro;\nend A;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
  YMC_CHECK_EQ(flatten_src("type RealInput = input Real;\nmodel A\n  input RealInput ri;\nend A;\n"),
               std::string("class A\n  input Real ri;\nend A;\n"));
}

// ---- stream 连接器（NFConnectEquations.mo:423-1142）；期望 = omc 1.25.0（文件与实跑一致，2026-09-27）----

static const char* kStreamS = "connector S\n  Real r;\n  flow Real f;\n  stream Real s;\nend S;\n";

YMC_TEST(flatten_stream_two_inside_instream_is_other) {
  // 两 inside 相连：stream 集不生成方程（generateStreamEquations :444）；inStream(a1.s.s) 按 INSIDE 面查到该集 → 对方
  //（generateInStreamExp :870-876）。InStreamTwoInside。
  const std::string src = std::string(kStreamS) +
      "model A\n  S s;\n  Real instream_s;\nequation\n  instream_s = inStream(s.s);\nend A;\n"
      "model InStreamTwoInside\n  A a1, a2;\n  Real instream_s_a1;\n  Real instream_s_a2;\nequation\n"
      "  connect(a1.s, a2.s);\n  instream_s_a1 = inStream(a1.s.s);\n  instream_s_a2 = inStream(a2.s.s);\nend InStreamTwoInside;\n";
  YMC_CHECK_EQ(flatten_src(src.c_str()),
               std::string("class InStreamTwoInside\n  Real a1.s.r;\n  Real a1.s.f;\n  Real a1.s.s;\n  Real a1.instream_s;\n"
                           "  Real a2.s.r;\n  Real a2.s.f;\n  Real a2.s.s;\n  Real a2.instream_s;\n  Real instream_s_a1;\n"
                           "  Real instream_s_a2;\nequation\n  a1.s.r = a2.s.r;\n  a2.s.f + a1.s.f = 0.0;\n"
                           "  a1.instream_s = a2.s.s;\n  a2.instream_s = a1.s.s;\n  instream_s_a1 = a2.s.s;\n"
                           "  instream_s_a2 = a1.s.s;\nend InStreamTwoInside;\n"));
}

YMC_TEST(flatten_stream_inside_outside_face_lookup) {
  // 一内一外：stream 集 `a.s.s = s.s`（:461-468），按根序排在 potential 与 flow 求和之间；子模型里的 inStream(s.s)
  // 展平成 inStream(a.s.s)，按 INSIDE 面查到顶层集合 → 一内一外 → 对方 s.s 的 inStream → s.s 本身。InStreamInsideOutside。
  const std::string src = std::string(kStreamS) +
      "model A\n  S s;\n  Real instream_s;\nequation\n  instream_s = inStream(s.s);\nend A;\n"
      "model InStreamInsideOutside\n  A a;\n  S s;\n  Real instream_s;\n  Real instream_s_a;\nequation\n"
      "  connect(a.s, s);\n  instream_s = inStream(s.s);\n  instream_s_a = inStream(a.s.s);\nend InStreamInsideOutside;\n";
  YMC_CHECK_EQ(flatten_src(src.c_str()),
               std::string("class InStreamInsideOutside\n  Real a.s.r;\n  Real a.s.f;\n  Real a.s.s;\n  Real a.instream_s;\n"
                           "  Real s.r;\n  Real s.f;\n  Real s.s;\n  Real instream_s;\n  Real instream_s_a;\n"
                           "equation\n  a.s.r = s.r;\n  a.s.s = s.s;\n  a.s.f - s.f = 0.0;\n  s.f = 0.0;\n"
                           "  a.instream_s = s.s;\n  instream_s = s.s;\n  instream_s_a = s.s;\nend InStreamInsideOutside;\n"));
}

YMC_TEST(flatten_stream_general_case_nominal_threshold) {
  // 三个 outside：每个 outside 一条 `$OMC$inStreamDiv(Σ PositiveMax(f, eps)*s / Σ PositiveMax(f, eps), 0)`（cons 反序、
  // sumMap 前插），eps = 1e-7 × nominal（1e-10 / 1.9999999999999998e-4），带注释。InStreamNominalThreshold。
  const std::string src = std::string(kStreamS) +
      "model A\n  S s1(f(nominal = 1e-3));\n  S s2(f(nominal = 2000));\n  S s3;\n"
      "  Real instream_s1;\n  Real instream_s2;\n  Real instream_s3;\nequation\n  connect(s1, s2);\n  connect(s2, s3);\n"
      "  instream_s1 = inStream(s1.s);\n  instream_s2 = inStream(s2.s);\nend A;\n"
      "model InStreamNominalThreshold\n  A a;\n  Real instream_a_s1;\n  Real instream_a_s2;\nequation\n"
      "  instream_a_s1 = inStream(a.s1.s);\n  instream_a_s2 = inStream(a.s2.s);\nend InStreamNominalThreshold;\n";
  const char* gen = " \" equation generated from stream connection\";\n";
  YMC_CHECK_EQ(
      flatten_src(src.c_str()),
      std::string("class InStreamNominalThreshold\n  Real a.s1.r;\n  Real a.s1.f(nominal = 0.001);\n  Real a.s1.s;\n"
                  "  Real a.s2.r;\n  Real a.s2.f(nominal = 2000.0);\n  Real a.s2.s;\n  Real a.s3.r;\n  Real a.s3.f;\n"
                  "  Real a.s3.s;\n  Real a.instream_s1;\n  Real a.instream_s2;\n  Real a.instream_s3;\n"
                  "  Real instream_a_s1;\n  Real instream_a_s2;\nequation\n  a.s2.r = a.s3.r;\n  a.s2.r = a.s1.r;\n"
                  "  -(a.s2.f + a.s3.f + a.s1.f) = 0.0;\n"
                  "  a.s1.s = $OMC$inStreamDiv(($OMC$PositiveMax(a.s3.f, 1e-7) * a.s3.s + "
                  "$OMC$PositiveMax(a.s2.f, 1.9999999999999998e-4) * a.s2.s) / ($OMC$PositiveMax(a.s3.f, 1e-7) + "
                  "$OMC$PositiveMax(a.s2.f, 1.9999999999999998e-4)), 0)") + gen +
          "  a.s3.s = $OMC$inStreamDiv(($OMC$PositiveMax(a.s1.f, 1e-10) * a.s1.s + "
          "$OMC$PositiveMax(a.s2.f, 1.9999999999999998e-4) * a.s2.s) / ($OMC$PositiveMax(a.s1.f, 1e-10) + "
          "$OMC$PositiveMax(a.s2.f, 1.9999999999999998e-4)), 0)" + gen +
          "  a.s2.s = $OMC$inStreamDiv(($OMC$PositiveMax(a.s1.f, 1e-10) * a.s1.s + "
          "$OMC$PositiveMax(a.s3.f, 1e-7) * a.s3.s) / ($OMC$PositiveMax(a.s1.f, 1e-10) + "
          "$OMC$PositiveMax(a.s3.f, 1e-7)), 0)" + gen +
          "  a.s1.f = 0.0;\n  a.s2.f = 0.0;\n  a.s3.f = 0.0;\n  a.instream_s1 = a.s1.s;\n  a.instream_s2 = a.s2.s;\n"
          "  instream_a_s1 = a.s1.s;\n  instream_a_s2 = a.s2.s;\nend InStreamNominalThreshold;\n");
}

YMC_TEST(flatten_actualstream_flow_direction) {
  // actualStream 按 flow 的字面量 min/max 定向（evaluateFlowDirection :1044-1076）：min=0 → inStream（对方）；max=0 → 本身；
  // 不定 → `if f > 0.0 then inStream(s) else s`。ActualStreamMinMax1。
  const std::string src = std::string(kStreamS) +
      "model A\n  S s1(f(min = 0)), s2(f(max = 0));\n  S s3(f(min = -5)), s4(f(max = 3));\nend A;\n"
      "model ActualStreamMinMax1\n  A a;\n  Real actual_stream_s1;\n  Real actual_stream_s2;\n"
      "  Real actual_stream_s3;\n  Real actual_stream_s4;\nequation\n  connect(a.s1, a.s2);\n  connect(a.s3, a.s4);\n"
      "  actual_stream_s1 = actualStream(a.s1.s);\n  actual_stream_s2 = actualStream(a.s2.s);\n"
      "  actual_stream_s3 = actualStream(a.s3.s);\n  actual_stream_s4 = actualStream(a.s4.s);\nend ActualStreamMinMax1;\n";
  YMC_CHECK_EQ(flatten_src(src.c_str()),
               std::string("class ActualStreamMinMax1\n  Real a.s1.r;\n  Real a.s1.f(min = 0.0);\n  Real a.s1.s;\n"
                           "  Real a.s2.r;\n  Real a.s2.f(max = 0.0);\n  Real a.s2.s;\n  Real a.s3.r;\n"
                           "  Real a.s3.f(min = -5.0);\n  Real a.s3.s;\n  Real a.s4.r;\n  Real a.s4.f(max = 3.0);\n"
                           "  Real a.s4.s;\n  Real actual_stream_s1;\n  Real actual_stream_s2;\n  Real actual_stream_s3;\n"
                           "  Real actual_stream_s4;\nequation\n  a.s1.r = a.s2.r;\n  a.s3.r = a.s4.r;\n"
                           "  a.s2.f + a.s1.f = 0.0;\n  a.s4.f + a.s3.f = 0.0;\n  actual_stream_s1 = a.s2.s;\n"
                           "  actual_stream_s2 = a.s2.s;\n  actual_stream_s3 = if a.s3.f > 0.0 then a.s4.s else a.s3.s;\n"
                           "  actual_stream_s4 = if a.s4.f > 0.0 then a.s3.s else a.s4.s;\nend ActualStreamMinMax1;\n"));
}

YMC_TEST(flatten_actualstream_structural_min_attribute) {
  // 参数值 min（`if cond then 0 else -1`）：evaluateFlowDirection 只认字面量 → 不定 → if 式；inStream 里 isNoFlow 对对方的
  // min 做结构化求值（Structural.markExp，NFConnectEquations.mo:955-976）→ 0 ≥ 0 → 对方无流被剔 → inStream = 本身 →
  // `if f > 0 then s else s` 化简成 s（simplifyIf :1330）；cond 打印 final、两处 min 折成 0.0（EvalConstants 效果）。
  // 期望 = omc 1.25.0（ActualStreamMinMax2，文件与实跑一致）。
  const std::string src = std::string(kStreamS) +
      "model A\n  parameter Boolean cond = true;\n"
      "  S s1(f(min = if cond then 0 else -1)), s2(f(min = if cond then 0 else 1));\nend A;\n"
      "model ActualStreamMinMax2\n  A a;\n  Real actual_stream_s1;\n  Real actual_stream_s2;\nequation\n"
      "  connect(a.s1, a.s2);\n  actual_stream_s1 = actualStream(a.s1.s);\n  actual_stream_s2 = actualStream(a.s2.s);\n"
      "end ActualStreamMinMax2;\n";
  YMC_CHECK_EQ(flatten_src(src.c_str()),
               std::string("class ActualStreamMinMax2\n  final parameter Boolean a.cond = true;\n  Real a.s1.r;\n"
                           "  Real a.s1.f(min = 0.0);\n  Real a.s1.s;\n  Real a.s2.r;\n  Real a.s2.f(min = 0.0);\n"
                           "  Real a.s2.s;\n  Real actual_stream_s1;\n  Real actual_stream_s2;\nequation\n"
                           "  a.s1.r = a.s2.r;\n  a.s2.f + a.s1.f = 0.0;\n  actual_stream_s1 = a.s1.s;\n"
                           "  actual_stream_s2 = a.s2.s;\nend ActualStreamMinMax2;\n"));
}

YMC_TEST(flatten_actualstream_parameter_subscript) {
  // 连接器下标 `c[n]`（参数）→ evaluateSubscripts 取值 c[1]，n 打印 final；未连接、方向不定 →
  // `if c[1].f > 0 then c[1].s else c[1].s` → c[1].s。期望 = omc 1.25.0（ActualStreamVariability2）。
  const char* src =
      "connector C\n  Real r;\n  flow Real f;\n  stream Real s;\nend C;\n"
      "model ActualStreamVariability2\n  C c[2];\n  parameter Integer n = 1;\n  Real as = actualStream(c[n].s);\n"
      "end ActualStreamVariability2;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class ActualStreamVariability2\n  Real c[1].r;\n  Real c[1].f;\n  Real c[1].s;\n"
                           "  Real c[2].r;\n  Real c[2].f;\n  Real c[2].s;\n  final parameter Integer n = 1;\n"
                           "  Real as = c[1].s;\nequation\n  c[1].f = 0.0;\n  c[2].f = 0.0;\nend ActualStreamVariability2;\n"));
}

YMC_TEST(flatten_simplify_if_branch_rules) {
  // simplifyIf（NFSimplifyExp.mo:1311-1345）：两支相等 → 该支；true/false → cond；false/true → not cond
  //（logicNegate：not 的 not 抵消，NFExpression.mo:5334-5344）。期望 = omc 1.25.0 实跑（IfSimp 探针，2026-09-27）。
  const char* src =
      "model IfSimp\n  Real x;\n  Real y;\n  Boolean b1 = if x > 0 then true else false;\n"
      "  Boolean b2 = if x > 0 then false else true;\n  Boolean b3 = if not (x > 1) then false else true;\n"
      "  Real z = if x > 2 then y else y;\nequation\n  x = time;\n  y = 2 * time;\nend IfSimp;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class IfSimp\n  Real x;\n  Real y;\n  Boolean b1 = x > 0.0;\n  Boolean b2 = not x > 0.0;\n"
                           "  Boolean b3 = x > 1.0;\n  Real z = y;\nequation\n  x = time;\n  y = 2.0 * time;\nend IfSimp;\n"));
}

YMC_TEST(flatten_instream_non_stream_operand_unsupported) {
  // inStream 的实参须是 stream 变量（omc 报错 InStreamNoStream1）→ 干净 SKIP。
  bool uns = false;
  std::string t = flatten_src((std::string(kStreamS) + "model M\n  S c;\n  Real as = inStream(c.f);\nend M;\n").c_str(), &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
}

// ---- expandable 连接器（NFExpandableConnectors.mo）；期望 = omc 1.25.0 实跑（文件 Result 与实跑一致者直接取用，2026-09-28）----

static const char* kExpRO = "expandable connector EC\nend EC;\nconnector RealOutput = output Real;\n";

YMC_TEST(flatten_expandable_virtual_scalar_two_connections) {
  // 未声明的 bus.speed 连两次：虚变量（无方向、带注释）前插在最前；两条连接同集 → head = bus.speed。ExpandableConnector1。
  const char* src =
      "connector RealOutput = output Real;\nconnector RealInput = input Real;\n"
      "expandable connector EngineBus\nend EngineBus;\n"
      "block Sensor\n  RealOutput speed;\nend Sensor;\nblock Actuator\n  RealInput speed;\nend Actuator;\n"
      "model ExpandableConnector1\n  EngineBus bus;\n  Sensor sensor;\n  Actuator actuator;\nequation\n"
      "  connect(bus.speed, sensor.speed);\n  connect(bus.speed, actuator.speed);\nend ExpandableConnector1;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class ExpandableConnector1\n  Real bus.speed \"virtual variable in expandable connector\";\n"
                           "  Real sensor.speed;\n  Real actuator.speed;\nequation\n  bus.speed = actuator.speed;\n"
                           "  bus.speed = sensor.speed;\nend ExpandableConnector1;\n"));
}

YMC_TEST(flatten_expandable_virtual_complex_flow_both_faces) {
  // 虚元素取另一端连接器类型：字段按声明序逐个前插（i 在 v 前）；虚 flow 另加 OUTSIDE 面预插入（collectFlows :158-163）
  // → 未连的 INSIDE 面单独成集 `bus.a.i = 0.0`。ExpandableConnectorComplex1。
  const char* src =
      "expandable connector Bus\nend Bus;\n"
      "connector PositivePin\n  Real v;\n  flow Real i;\nend PositivePin;\n"
      "model M\n  parameter Real A = 1;\n  parameter Real w = 1;\n  PositivePin a;\nequation\n  a.v = A*sin(w*time);\nend M;\n"
      "model ExpandableConnectorComplex1\n  Bus bus;\n  M m;\nequation\n  connect(m.a, bus.a);\n"
      "end ExpandableConnectorComplex1;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class ExpandableConnectorComplex1\n"
                           "  Real bus.a.i \"virtual variable in expandable connector\";\n"
                           "  Real bus.a.v \"virtual variable in expandable connector\";\n"
                           "  parameter Real m.A = 1.0;\n  parameter Real m.w = 1.0;\n  Real m.a.v;\n  Real m.a.i;\n"
                           "equation\n  m.a.v = bus.a.v;\n  bus.a.i = 0.0;\n  m.a.i - bus.a.i = 0.0;\n"
                           "  m.a.v = m.A * sin(m.w * time);\nend ExpandableConnectorComplex1;\n"));
}

YMC_TEST(flatten_expandable_virtual_order_hash_buckets) {
  // exp_set 是按末段名 djb2 散列的 UnorderedSet，toList = 桶序（非插入序）：a/b/c → b, c, a（YmcExpHash1 探针）；
  // Windows 版 1.25.0 的 long 是 32 位：speed/torque/temp → temp, speed, torque（64 位会是 torque, speed, temp；YmcExpHash2）。
  const std::string h1 = std::string(kExpRO) +
      "model YmcExpHash1\n  EC bus;\n  RealOutput s1, s2, s3;\nequation\n  connect(bus.a, s1);\n  connect(bus.b, s2);\n"
      "  connect(bus.c, s3);\nend YmcExpHash1;\n";
  const char* v = " \"virtual variable in expandable connector\";\n";
  YMC_CHECK_EQ(flatten_src(h1.c_str()),
               std::string("class YmcExpHash1\n  Real bus.b") + v + "  Real bus.c" + v + "  Real bus.a" + v +
                   "  output Real s1;\n  output Real s2;\n  output Real s3;\nequation\n  bus.a = s1;\n  bus.b = s2;\n"
                   "  bus.c = s3;\nend YmcExpHash1;\n");
  const std::string h2 = std::string(kExpRO) +
      "model YmcExpHash2\n  EC bus;\n  RealOutput s1, s2, s3;\nequation\n  connect(bus.speed, s1);\n"
      "  connect(bus.torque, s2);\n  connect(bus.temp, s3);\nend YmcExpHash2;\n";
  YMC_CHECK_EQ(flatten_src(h2.c_str()),
               std::string("class YmcExpHash2\n  Real bus.temp") + v + "  Real bus.speed" + v + "  Real bus.torque" + v +
                   "  output Real s1;\n  output Real s2;\n  output Real s3;\nequation\n  bus.speed = s1;\n"
                   "  bus.torque = s2;\n  bus.temp = s3;\nend YmcExpHash2;\n");
}

YMC_TEST(flatten_expandable_exp_set_rehash) {
  // 15 个未声明名进同一 exp_set：第 15 个入集前 size = 14 > 13 桶（loadFactor > 1）→ rehash 到 nextPrime(28) = 29 桶
  // （旧桶序逐个前插，Util/UnorderedSet.mo:539-563 / 834-861）。期望 = omc 1.25.0（YmcExpRehash1 探针）。
  std::string src = std::string(kExpRO) + "model YmcExpRehash1\n  EC bus;\n";
  auto two = [](int i) { return std::string(i < 10 ? "0" : "") + std::to_string(i); };
  for (int i = 1; i <= 15; ++i) src += "  RealOutput s" + two(i) + ";\n";
  src += "equation\n";
  for (int i = 1; i <= 15; ++i) src += "  connect(bus.n" + two(i) + ", s" + two(i) + ");\n";
  src += "end YmcExpRehash1;\n";
  std::string want = "class YmcExpRehash1\n";
  for (int i : {1, 2, 3, 10, 4, 11, 5, 12, 6, 13, 7, 14, 8, 15, 9})
    want += "  Real bus.n" + two(i) + " \"virtual variable in expandable connector\";\n";
  for (int i = 1; i <= 15; ++i) want += "  output Real s" + two(i) + ";\n";
  want += "equation\n";
  for (int i = 1; i <= 15; ++i) want += "  bus.n" + two(i) + " = s" + two(i) + ";\n";
  want += "end YmcExpRehash1;\n";
  YMC_CHECK_EQ(flatten_src(src.c_str()), want);
}

YMC_TEST(flatten_expandable_sets_numbered_by_root) {
  // 关联图的集合按根节点序编号（DisjointSets.union 按代码的秩规则，bus2 集的根被挂到后插的 bus2.z 下）→ bus1 集先增广，
  // 其虚变量最后前插反而排在最后。YmcExpTwoSets1 探针。
  const std::string src = std::string(kExpRO) +
      "model YmcExpTwoSets1\n  EC bus1, bus2;\n  RealOutput s1, s2, s3;\nequation\n  connect(bus2.x, s1);\n"
      "  connect(bus1.y, s2);\n  connect(bus2.z, s3);\nend YmcExpTwoSets1;\n";
  const char* v = " \"virtual variable in expandable connector\";\n";
  YMC_CHECK_EQ(flatten_src(src.c_str()),
               std::string("class YmcExpTwoSets1\n  Real bus2.x") + v + "  Real bus2.z" + v + "  Real bus1.y" + v +
                   "  output Real s1;\n  output Real s2;\n  output Real s3;\nequation\n  bus2.x = s1;\n  bus1.y = s2;\n"
                   "  bus2.z = s3;\nend YmcExpTwoSets1;\n");
}

YMC_TEST(flatten_expandable_present_marking_per_node) {
  // markComponentPresent 改的是组件节点，数组元素共用节点 → 只连了 a[1].ec.x，a[2].ec.x 也 present（YmcExpNode1 探针）；
  // 未连的已声明成员被删（ExpandableConnectorPresent1 的 ec.y）。
  const char* src =
      "expandable connector EC\n  Real x;\nend EC;\nmodel A\n  EC ec;\nend A;\nconnector RealOutput = output Real;\n"
      "model YmcExpNode1\n  A a[2];\n  RealOutput z;\nequation\n  connect(a[1].ec.x, z);\nend YmcExpNode1;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class YmcExpNode1\n  Real a[1].ec.x;\n  Real a[2].ec.x;\n  output Real z;\nequation\n"
                           "  a[1].ec.x = z;\nend YmcExpNode1;\n"));
  const char* src2 =
      "expandable connector EC\n  Real x;\n  Real y;\nend EC;\nconnector RealInput = input Real;\n"
      "model ExpandableConnectorPresent1\n  EC ec;\n  RealInput ri;\nequation\n  connect(ec.x, ri);\n"
      "end ExpandableConnectorPresent1;\n";
  YMC_CHECK_EQ(flatten_src(src2),
               std::string("class ExpandableConnectorPresent1\n  Real ec.x;\n  input Real ri;\nequation\n  ec.x = ri;\n"
                           "end ExpandableConnectorPresent1;\n"));
}

YMC_TEST(flatten_expandable_nested_and_declared_peer) {
  // 嵌套 expandable 同名成员随外层连接并集（addNestedExpandableConnectorsToSets）；已声明成员保留方向（ExpandableConnector7）。
  // 一侧已声明的 x 给另一侧当虚元素类型（YmcExpDeclOther1 探针）。
  const char* src =
      "connector RealOutput = output Real;\nexpandable connector EC\n  RealOutput x;\nend EC;\n"
      "expandable connector EC2\n  EC ec;\nend EC2;\n"
      "model ExpandableConnector7\n  RealOutput x;\n  EC2 ec1, ec2;\nequation\n  connect(ec1.ec.x, x);\n"
      "  connect(ec1, ec2);\nend ExpandableConnector7;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class ExpandableConnector7\n  output Real x;\n  output Real ec1.ec.x;\n  output Real ec2.ec.x;\n"
                           "equation\n  ec1.ec.x = ec2.ec.x;\n  ec1.ec.x = x;\nend ExpandableConnector7;\n"));
  const char* src2 =
      "expandable connector EC1\n  Real x;\nend EC1;\nexpandable connector EC2\nend EC2;\nconnector RealInput = input Real;\n"
      "model YmcExpDeclOther1\n  EC1 ec1;\n  EC2 ec2;\n  RealInput ri;\nequation\n  connect(ec1, ec2);\n"
      "  connect(ec1.x, ri);\nend YmcExpDeclOther1;\n";
  YMC_CHECK_EQ(flatten_src(src2),
               std::string("class YmcExpDeclOther1\n  Real ec2.x \"virtual variable in expandable connector\";\n"
                           "  Real ec1.x;\n  input Real ri;\nequation\n  ec1.x = ec2.x;\n  ec1.x = ri;\n"
                           "end YmcExpDeclOther1;\n"));
}

YMC_TEST(flatten_expandable_array_elements) {
  // expandable 数组逐元素各自增广：虚变量名带元素下标、各元素虚成员可不同（YmcExpArr2 探针）。
  const char* v = " \"virtual variable in expandable connector\";\n";
  const std::string a2 = std::string(kExpRO) +
      "model YmcExpArr2\n  EC ec[2];\n  RealOutput y1, y2;\nequation\n  connect(ec[1].x, y1);\n  connect(ec[2].z, y2);\n"
      "end YmcExpArr2;\n";
  YMC_CHECK_EQ(flatten_src(a2.c_str()),
               std::string("class YmcExpArr2\n  Real ec[2].z") + v + "  Real ec[1].x" + v +
                   "  output Real y1;\n  output Real y2;\nequation\n  ec[1].x = y1;\n  ec[2].z = y2;\nend YmcExpArr2;\n");
  // 组件节点被数组元素共用，增广类型"最后一次生效"（:428）：{ec[1], ec2[1], ec[1].x} 集的根后于 {ec[2], ec2[2]} → 最后增广 →
  // ec[2]↔ec2[2] 的拆分也带 x，输出引用未建的 ec[2].x / ec2[2].x（omc 1.25.0 照样打印，YmcExpArr3 探针）。
  const std::string a3 = std::string(kExpRO) +
      "model YmcExpArr3\n  EC ec[2], ec2[2];\n  RealOutput y1;\nequation\n  connect(ec, ec2);\n  connect(ec[1].x, y1);\n"
      "end YmcExpArr3;\n";
  YMC_CHECK_EQ(flatten_src(a3.c_str()),
               std::string("class YmcExpArr3\n  Real ec[1].x") + v + "  Real ec2[1].x" + v +
                   "  output Real y1;\nequation\n  ec[1].x = ec2[1].x;\n  ec[1].x = y1;\n  ec[2].x = ec2[2].x;\n"
                   "end YmcExpArr3;\n");
}

YMC_TEST(flatten_expandable_invalid_forms_unsupported) {
  // omc 报错的形态 → 干净 SKIP：expandable 直属 flow（ExpandableConnectorFlow1）、两端都未声明（NonDecl1）、
  // expandable 连非 expandable（InvalidExpandableConnector1）、虚元素再下探（NonDecl3）。
  const char* cases[] = {
      "expandable connector Bus\n  flow Real f;\nend Bus;\nmodel M\n  Bus bus;\nend M;\n",
      "expandable connector EC\nend EC;\nmodel M\n  EC ec1, ec2;\nequation\n  connect(ec1.c, ec2.c);\nend M;\n",
      "expandable connector EC\nend EC;\nconnector C\n  Real x;\nend C;\nmodel M\n  EC ec;\n  C c;\nequation\n"
      "  connect(ec, c);\nend M;\n",
      "connector RealInput = input Real;\nexpandable connector EC\nend EC;\nmodel M\n  RealInput ri;\n  EC ec;\n"
      "equation\n  connect(ec.c.ri, ri);\nend M;\n",
  };
  for (const char* s : cases) {
    bool uns = false;
    const std::string t = flatten_src(s, &uns);
    YMC_CHECK(uns);
    YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
  }
}

// ---- 常量数组求值（NFCeval evalBinary*/evalRange*/evalBuiltinFill…）；期望 = omc 1.25.0 实跑（P54a/b/c 探针）----

YMC_TEST(flatten_constant_array_ceval_ops) {
  // `:` 维取值形状；逐元素 +、String `.+` 拼接、空数组整体不出（i6）、Real 范围、矩阵幂、向量·向量标量积。
  const char* src =
      "model P54a\n  constant Integer i2[:] = {1, 2, 3} + {3, 4, 5};\n  constant String s4[:] = \"1\" .+ {\"1\", \"2\", \"3\"};\n"
      "  constant Integer i6[:] = zeros(0) - zeros(0);\n  constant Real y[:] = 3.0:0.5:5.0;\n"
      "  constant Real m[2, 2] = {{1, 2}, {3, 4}};\n  constant Real p[:, :] = m ^ 2;\n"
      "  constant Real d = {1, 2, 3} * {4, 5, 6};\nend P54a;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class P54a\n  constant Integer i2[1] = 4;\n  constant Integer i2[2] = 6;\n"
                           "  constant Integer i2[3] = 8;\n  constant String s4[1] = \"11\";\n  constant String s4[2] = \"12\";\n"
                           "  constant String s4[3] = \"13\";\n  constant Real y[1] = 3.0;\n  constant Real y[2] = 3.5;\n"
                           "  constant Real y[3] = 4.0;\n  constant Real y[4] = 4.5;\n  constant Real y[5] = 5.0;\n"
                           "  constant Real m[1,1] = 1.0;\n  constant Real m[1,2] = 2.0;\n  constant Real m[2,1] = 3.0;\n"
                           "  constant Real m[2,2] = 4.0;\n  constant Real p[1,1] = 7.0;\n  constant Real p[1,2] = 10.0;\n"
                           "  constant Real p[2,1] = 15.0;\n  constant Real p[2,2] = 22.0;\n  constant Real d = 32.0;\nend P54a;\n"));
}

YMC_TEST(flatten_constant_array_ceval_modifier_aware) {
  // 常量引用取已发射常量的最终值（修饰 `a(x = 1.0)` 后）：z = {x, y} * 2 → {2.0, 4.0}（P54b 探针）。
  const char* src =
      "model A\n  constant Real x;\n  constant Real y = 2;\n  constant Real z[:] = {x, y} * 2;\nend A;\n"
      "model P54b\n  A a(x = 1.0);\nend P54b;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class P54b\n  constant Real a.x = 1.0;\n  constant Real a.y = 2.0;\n  constant Real a.z[1] = 2.0;\n"
                           "  constant Real a.z[2] = 4.0;\nend P54b;\n"));
}

YMC_TEST(flatten_constant_array_builtins) {
  // transpose（换前两维，保 Integer）/ cross（Real）/ vector（摊平）/ linspace / diagonal（零取元素类型）/ scalar /
  // der（常量 → 同形 Real 0）。期望 = omc 1.25.0（P54d 探针）。
  const char* src =
      "model P54d\n  constant Integer t[:, :] = transpose({{1, 2}, {3, 4}, {5, 6}});\n"
      "  constant Real c[:] = cross({1, 0, 0}, {0, 1, 0});\n  constant Integer v[:] = vector({{1}, {2}});\n"
      "  constant Real l[:] = linspace(0, 1, 3);\n  constant Integer g[:, :] = diagonal({7, 8});\n"
      "  constant Real s = scalar({{2.5}});\n  constant Real d[:] = der({1, 2});\nend P54d;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class P54d\n  constant Integer t[1,1] = 1;\n  constant Integer t[1,2] = 3;\n"
                           "  constant Integer t[1,3] = 5;\n  constant Integer t[2,1] = 2;\n  constant Integer t[2,2] = 4;\n"
                           "  constant Integer t[2,3] = 6;\n  constant Real c[1] = 0.0;\n  constant Real c[2] = 0.0;\n"
                           "  constant Real c[3] = 1.0;\n  constant Integer v[1] = 1;\n  constant Integer v[2] = 2;\n"
                           "  constant Real l[1] = 0.0;\n  constant Real l[2] = 0.5;\n  constant Real l[3] = 1.0;\n"
                           "  constant Integer g[1,1] = 7;\n  constant Integer g[1,2] = 0;\n  constant Integer g[2,1] = 0;\n"
                           "  constant Integer g[2,2] = 8;\n  constant Real s = 2.5;\n  constant Real d[1] = 0.0;\n"
                           "  constant Real d[2] = 0.0;\nend P54d;\n"));
}

YMC_TEST(flatten_constant_function_arrays_and_comprehension) {
  // 用户函数值求值：输出维依赖输入（`output Real y[n]`，Undef 占位）、下标赋值、protected 局部带绑定、max(a, b)；
  // 推导式最右迭代器为最外维（c[j][i]）；标量内建对数组实参向量化（sqrt）。期望 = omc 1.25.0（P54e 探针）。
  const char* src =
      "function f\n  input Integer n;\n  output Real y[n];\nprotected\n  Real s = 0.5;\nalgorithm\n  for i in 1:n loop\n"
      "    y[i] := i * s;\n  end for;\n  y[1] := max(y[1], 2);\nend f;\n"
      "model P54e\n  constant Real a[:] = f(3);\n  constant Integer c[:, :] = {i * 10 + j for i in 1:2, j in 1:3};\n"
      "  constant Real d[:] = sqrt({4, 9});\nend P54e;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class P54e\n  constant Real a[1] = 2.0;\n  constant Real a[2] = 1.0;\n  constant Real a[3] = 1.5;\n"
                           "  constant Integer c[1,1] = 11;\n  constant Integer c[1,2] = 21;\n  constant Integer c[2,1] = 12;\n"
                           "  constant Integer c[2,2] = 22;\n  constant Integer c[3,1] = 13;\n  constant Integer c[3,2] = 23;\n"
                           "  constant Real d[1] = 2.0;\n  constant Real d[2] = 3.0;\nend P54e;\n"));
}

YMC_TEST(flatten_constant_range_forms) {
  // evalRangeReal（:915-938）首尾按原值：1.0:0.3:2.0 共 floor(1/0.3 + 5e-15) + 1 = 4 个，末个是 stop 本身 2.0（非 1.9）；
  // Integer 负步长、Boolean 范围（P54c 探针）。
  const char* src =
      "model P54c\n  constant Real x[:] = 1.0:0.3:2.0;\n  constant Integer k[:] = 5:-2:1;\n  constant Boolean b[:] = false:true;\n"
      "end P54c;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class P54c\n  constant Real x[1] = 1.0;\n  constant Real x[2] = 1.3;\n  constant Real x[3] = 1.6;\n"
                           "  constant Real x[4] = 2.0;\n  constant Integer k[1] = 5;\n  constant Integer k[2] = 3;\n"
                           "  constant Integer k[3] = 1;\n  constant Boolean b[1] = false;\n  constant Boolean b[2] = true;\n"
                           "end P54c;\n"));
}

// ---- 用户函数常量折叠（NFEvalFunction / NFFunction）；期望 = omc 1.25.0（文件 Result 与实跑一致者、YmcFnExtOrder 探针）----

YMC_TEST(flatten_function_fold_resolution_and_extends) {
  // 包限定 + 短类别名 `function f = P.f`，函数体在定义处（P）解析 f2（ExtendsShort2）；extends 成员就地展开：
  // F(a; extends Base(b); c) 的位置序 = a, b, c → 123（YmcFnExtOrder 探针）；递归 + if 表达式（CevalFuncRecursive1）。
  const char* s1 =
      "package P\n  function f\n    input Real x;\n    output Real y;\n  algorithm\n    y := f2(x);\n  end f;\n"
      "  function f2\n    input Real x;\n    output Real y = x * 2;\n  end f2;\nend P;\n"
      "model ExtendsShort2\n  function f = P.f;\n  Real x = f(2.0);\nend ExtendsShort2;\n";
  YMC_CHECK_EQ(flatten_src(s1), std::string("class ExtendsShort2\n  Real x = 4.0;\nend ExtendsShort2;\n"));
  const char* s2 =
      "partial function Base\n  input Real b;\nend Base;\n"
      "function F\n  input Real a;\n  extends Base;\n  input Real c;\n  output Real y = a * 100 + b * 10 + c;\nend F;\n"
      "model YmcFnExtOrder\n  constant Real x = F(1, 2, 3);\nend YmcFnExtOrder;\n";
  YMC_CHECK_EQ(flatten_src(s2), std::string("class YmcFnExtOrder\n  constant Real x = 123.0;\nend YmcFnExtOrder;\n"));
  const char* s3 =
      "function fac\n  input Integer n;\n  output Integer res;\nalgorithm\n  res := if n > 1 then n * fac(n - 1) else n;\n"
      "end fac;\nmodel CevalFuncRecursive1\n  constant Real x = fac(6);\nend CevalFuncRecursive1;\n";
  YMC_CHECK_EQ(flatten_src(s3), std::string("class CevalFuncRecursive1\n  constant Real x = 720.0;\nend CevalFuncRecursive1;\n"));
}

YMC_TEST(flatten_function_fold_records) {
  // protected 记录局部 `R r(x = x)` + 字段默认互引 `y = x`（CevalFuncRecord7）；`output R r` 无默认 → EMPTY 占位后逐字段
  // 赋值（CevalFuncRecord2）；记录构造实参 + 引用记录字段的输入默认（FuncDefaultArg4）。
  const char* s1 =
      "record R\n  Real x;\n  Real y = x;\nend R;\n"
      "function f\n  input Real x;\n  output Real y;\nprotected\n  R r(x = x);\nalgorithm\n  y := r.y;\nend f;\n"
      "model CevalFuncRecord7\n  constant Real x = f(1);\nend CevalFuncRecord7;\n";
  YMC_CHECK_EQ(flatten_src(s1), std::string("class CevalFuncRecord7\n  constant Real x = 1.0;\nend CevalFuncRecord7;\n"));
  const char* s2 =
      "record R\n  Real x;\n  Real y;\nend R;\n"
      "function f\n  input Real x;\n  input Real y;\n  output R r;\nalgorithm\n  r.x := x;\n  r.y := y;\nend f;\n"
      "model CevalFuncRecord2\n  constant R r = f(1.0, 2.0);\nend CevalFuncRecord2;\n";
  YMC_CHECK_EQ(flatten_src(s2),
               std::string("class CevalFuncRecord2\n  constant Real r.x = 1.0;\n  constant Real r.y = 2.0;\nend CevalFuncRecord2;\n"));
  const char* s3 =
      "record R\n  Real x;\nend R;\n"
      "function f\n  input R r;\n  input Real x = r.x;\n  output Real y = r.x * x;\nend f;\n"
      "model FuncDefaultArg4\n  Real x = f(R(2));\nend FuncDefaultArg4;\n";
  YMC_CHECK_EQ(flatten_src(s3), std::string("class FuncDefaultArg4\n  Real x = 4.0;\nend FuncDefaultArg4;\n"));
}

YMC_TEST(flatten_function_fold_misc) {
  // 隐式 for 范围（ImplicitRangeAlg2）；多输出函数在表达式里取首输出（FunctionMultiOutput3）；模型内嵌函数读调用实例的
  // 常量、常量叶经实例引用内联（func1）。
  const char* s1 =
      "function f\n  input Real x[:];\n  output Real sum = 0.0;\nalgorithm\n  for i loop\n    sum := sum + x[i];\n  end for;\n"
      "end f;\nmodel ImplicitRangeAlg2\n  Real x = f({1, 2, 3});\nend ImplicitRangeAlg2;\n";
  YMC_CHECK_EQ(flatten_src(s1), std::string("class ImplicitRangeAlg2\n  Real x = 6.0;\nend ImplicitRangeAlg2;\n"));
  const char* s2 =
      "function f\n  input Real x;\n  output Real y;\n  output Real z;\nalgorithm\n  y := x * 2;\n  z := x * 3;\nend f;\n"
      "model FunctionMultiOutput3\n  Real x = f(3);\n  Real y;\nequation\n  y = f(4);\nend FunctionMultiOutput3;\n";
  YMC_CHECK_EQ(flatten_src(s2),
               std::string("class FunctionMultiOutput3\n  Real x = 6.0;\n  Real y;\nequation\n  y = 8.0;\nend FunctionMultiOutput3;\n"));
  const char* s3 =
      "model A\n  constant Real z;\n  function f\n    input Real x;\n    output Real y;\n  algorithm\n    y := x * z;\n  end f;\n"
      "  constant Real w = f(z);\nend A;\n"
      "model B\n  A a1(z = 2.0);\n  A a2(z = 3.0);\n  Real x = a1.w;\n  Real y = a2.w;\nend B;\n";
  YMC_CHECK_EQ(flatten_src(s3),
               std::string("class B\n  constant Real a1.z = 2.0;\n  constant Real a1.w = 4.0;\n  constant Real a2.z = 3.0;\n"
                           "  constant Real a2.w = 9.0;\n  Real x = 4.0;\n  Real y = 9.0;\nend B;\n"));
}

// ---- 用户函数发射（加宽(55)-B：NFFunction.lookupFunction / fillArgs / sortLocals，DAEDumpTpl.dumpFunction）；期望 = omc 1.25.0 ----

YMC_TEST(flatten_function_emit_names_and_order) {
  // 压平名 = 找到处作用域链 + 调用名：实例类里的函数带实例路径（嵌套 `b.a`、继承 `a2`），根类里的带根名；函数块按全名
  // 字节序排（DAEDump.sortFunctions）。YmcFnName2 探针。
  const char* src =
      "model A\n  function h\n    input Real x;\n    output Real y = x * x;\n  end h;\n  Real v = h(time);\nend A;\n"
      "model A2\n  extends A;\nend A2;\nmodel B\n  A a;\nend B;\n"
      "model YmcFnName2\n  function fr\n    input Real x;\n    output Real y = x + 3;\n  end fr;\n"
      "  model C\n    Real w = fr(time);\n  end C;\n  B b;\n  C c;\n  A2 a2;\nend YmcFnName2;\n";
  const char* h = "  input Real x;\n  output Real y = x * x;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("function YmcFnName2.a2.h\n") + h + "end YmcFnName2.a2.h;\n\nfunction YmcFnName2.b.a.h\n" + h +
                   "end YmcFnName2.b.a.h;\n\nfunction YmcFnName2.fr\n  input Real x;\n  output Real y = x + 3.0;\n"
                   "end YmcFnName2.fr;\n\nclass YmcFnName2\n  Real b.a.v = YmcFnName2.b.a.h(time);\n"
                   "  Real c.w = YmcFnName2.fr(time);\n  Real a2.v = YmcFnName2.a2.h(time);\nend YmcFnName2;\n");
}

YMC_TEST(flatten_function_emit_arguments) {
  // 缺省实参 = 输入绑定里引用别的输入处代换成实参（`f(a)` → `f(a, 2.0 * a, 3)`），命名实参入槽，Integer→Real 实参插 Cast，
  // 参数实参不求值（`g(p)`）；函数体里包常量内联、局部常量保留引用、包内调用打类路径（YmcFnEmit1 / YmcFnPar1 探针）。
  const char* src =
      "package P\n  constant Real K = 4;\n  function a\n    input Real x;\n    output Real y;\n  protected\n"
      "    constant Real c = 3;\n  algorithm\n    y := b(x) * c + K;\n  end a;\n  function b\n    input Real x;\n"
      "    output Real y = x + 1;\n  end b;\nend P;\n"
      "function f\n  input Real u1;\n  input Real u2 = 2 * u1;\n  input Integer k = 3;\n  output Real y;\nalgorithm\n"
      "  y := u1 + u2 + k;\nend f;\nfunction g\n  input Real x;\n  output Real y = x;\nend g;\n"
      "model YmcFnEmit1\n  parameter Real p = 2;\n  Integer n = 1;\n  Real a = time;\n  Real r1 = f(a);\n"
      "  Real r2 = f(u2 = 3, u1 = a);\n  Real r3 = g(p);\n  Real r4 = g(n);\n  Real r5 = P.a(time);\n"
      "  Real r6 = f(a, k = n);\nend YmcFnEmit1;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("function P.a\n  input Real x;\n  output Real y;\n  protected constant Real c = 3.0;\nalgorithm\n"
                           "  y := P.b(x) * c + 4.0;\nend P.a;\n\nfunction P.b\n  input Real x;\n  output Real y = x + 1.0;\n"
                           "end P.b;\n\nfunction f\n  input Real u1;\n  input Real u2 = 2.0 * u1;\n  input Integer k = 3;\n"
                           "  output Real y;\nalgorithm\n  y := u1 + u2 + /*Real*/(k);\nend f;\n\nfunction g\n  input Real x;\n"
                           "  output Real y = x;\nend g;\n\nclass YmcFnEmit1\n  parameter Real p = 2.0;\n  Integer n = 1;\n"
                           "  Real a = time;\n  Real r1 = f(a, 2.0 * a, 3);\n  Real r2 = f(a, 3.0, 3);\n  Real r3 = g(p);\n"
                           "  Real r4 = g(/*Real*/(n));\n  Real r5 = P.a(time);\n  Real r6 = f(a, 2.0 * a, n);\nend YmcFnEmit1;\n"));
}

YMC_TEST(flatten_function_emit_external_and_tuple) {
  // external：未声明纯度 → impure、显式调用照印；`pure` + 隐式调用 → `y = fp(x)`（YmcFnExt1 探针）；数组实参后跟各维 size
  //（ExternalFunctionImplicit3）。多输出函数在表达式里取首输出 `f(time)[1]`（TupleOperation1）。
  const char* s1 =
      "function fe\n  input Real x;\n  output Real y;\nexternal \"C\" y = ext(x);\nend fe;\n"
      "pure function fp\n  input Real x;\n  output Real y;\nexternal \"C\";\nend fp;\n"
      "model YmcFnExt1\n  Real a = fe(time);\n  Real b = fp(time);\nend YmcFnExt1;\n";
  YMC_CHECK_EQ(flatten_src(s1),
               std::string("impure function fe\n  input Real x;\n  output Real y;\n\n  external \"C\" y = ext(x);\nend fe;\n\n"
                           "function fp\n  input Real x;\n  output Real y;\n\n  external \"C\" y = fp(x);\nend fp;\n\n"
                           "class YmcFnExt1\n  Real a = fe(time);\n  Real b = fp(time);\nend YmcFnExt1;\n"));
  const char* s2 =
      "function f\n  input Real x[3];\n  input Real y[2, 4];\n  output Real z;\n  external;\nend f;\n"
      "model ExternalFunctionImplicit3\n  Real x;\nalgorithm\n  x := f({1, 2, 3}, {{1, 2, 3, 4}, {5, 6, 7, 8}});\n"
      "end ExternalFunctionImplicit3;\n";
  YMC_CHECK_EQ(flatten_src(s2),
               std::string("impure function f\n  input Real[3] x;\n  input Real[2, 4] y;\n  output Real z;\n\n"
                           "  external \"C\" z = f(x, size(x, 1), y, size(y, 1), size(y, 2));\nend f;\n\n"
                           "class ExternalFunctionImplicit3\n  Real x;\nalgorithm\n"
                           "  x := f({1.0, 2.0, 3.0}, {{1.0, 2.0, 3.0, 4.0}, {5.0, 6.0, 7.0, 8.0}});\n"
                           "end ExternalFunctionImplicit3;\n"));
  const char* s3 =
      "function f\n  input Real t;\n  output Real x = 1.0;\n  output Real y = 2.0;\nend f;\n"
      "model TupleOperation1\n  Real x = -f(time);\nend TupleOperation1;\n";
  YMC_CHECK_EQ(flatten_src(s3),
               std::string("function f\n  input Real t;\n  output Real x = 1.0;\n  output Real y = 2.0;\nend f;\n\n"
                           "class TupleOperation1\n  Real x = -f(time)[1];\nend TupleOperation1;\n"));
}

YMC_TEST(flatten_function_emit_locals_prune_and_errors) {
  // 局部拓扑序（sortLocals）：`n` 先于 `z[n]`，常量绑定求值（FunctionCompOrder3）；被结构参数求值掉的调用不留函数块
  //（collectFunctions 在求值后收，ceval4）；循环外 break（BREAK_OUTSIDE_LOOP，Break2）、输出未赋值告警
  //（UNASSIGNED_FUNCTION_OUTPUT）→ SKIP。期望 = 文件 Result 且与 1.25.0 实跑一致。
  const char* s1 =
      "function f\n  input Real x;\n  output Real y;\nprotected\n  Real z[n] = ones(3);\n  parameter Integer n = 3;\n"
      "algorithm\n  y := z[1];\nend f;\nmodel FunctionCompOrder3\n  Real x = f(time);\nend FunctionCompOrder3;\n";
  YMC_CHECK_EQ(flatten_src(s1),
               std::string("function f\n  input Real x;\n  output Real y;\n  protected parameter Integer n = 3;\n"
                           "  protected Real[n] z = {1.0, 1.0, 1.0};\nalgorithm\n  y := z[1];\nend f;\n\n"
                           "class FunctionCompOrder3\n  Real x = f(time);\nend FunctionCompOrder3;\n"));
  const char* s2 =
      "model A\n  function f\n    input Integer i;\n    output Integer j=i+1;\n  end f;\n\n  parameter Integer n = 1;\n"
      "  parameter Integer m = f(n)+n;\n  Real x[m] = {1.0, 1.0, 1.0};\nend A;\n";
  YMC_CHECK_EQ(flatten_src(s2),
               std::string("class A\n  final parameter Integer n = 1;\n  final parameter Integer m = 3;\n  Real x[1];\n"
                           "  Real x[2];\n  Real x[3];\nequation\n  x = {1.0, 1.0, 1.0};\nend A;\n"));
  for (const char* s : {
           "function f\n  input Integer n;\n  output Integer res;\nalgorithm\n  res := n * n;\n  break;\nend f;\n"
           "model M\n  Integer k = 2;\n  Real x1 = f(k);\nend M;\n",
           "function f\n  input Real x;\n  output Real y;\nend f;\nmodel M\n  Real x1 = f(time);\nend M;\n"}) {
    bool uns = false;
    flatten_src(s, &uns);
    YMC_CHECK(uns);
  }
  // 纯函数顶层调 impure（`y := f(x)`）：omc 把 f2 标成 impure、告警走 stderr（ImpureCall1；加宽(63g) 起不再 SKIP）。期望 =
  // omc 1.25.0 实跑的 stdout（2026-10-10）。
  YMC_CHECK_EQ(flatten_src("impure function f\n  input Real x;\n  output Real y = x;\nend f;\nfunction f2\n  input Real x;\n"
                           "  output Real y;\nalgorithm\n  y := f(x);\nend f2;\nmodel M\n  Real x = f2(time);\nend M;\n"),
               std::string("impure function f\n  input Real x;\n  output Real y = x;\nend f;\n\nimpure function f2\n"
                           "  input Real x;\n  output Real y;\nalgorithm\n  y := f(x);\nend f2;\n\nclass M\n"
                           "  Real x = f2(time);\nend M;\n"));
}

YMC_TEST(flatten_function_emit_array_bodies) {
  // 函数体里的数组（B-ii）：zeros / ones 打成 fill、Real 目标把强转推进填充值（NFCall.typeCast）；向量积 `x * x`（标量）、
  // `scalar({x[1]})` 展开、`sum(y)` 符号保留；多输出取首（YmcFnArr2 探针）。scalar(size(u)) → `size(u, 1)`
  //（FuncBuiltinScalar2）。期望 = omc 1.25.0 实跑。
  const char* s1 =
      "function f\n  input Integer n;\n  input Real x[:];\n  output Real z;\n  output Real y[n];\nprotected\n"
      "  Real w[size(x, 1)] = zeros(size(x, 1));\n  Integer m[n] = ones(n);\nalgorithm\n  y := fill(2.0, n) + fill(1, n);\n"
      "  z := x * x + w * x + scalar({x[1]}) + sum(y);\nend f;\nmodel YmcFnArr2\n  Real a[3];\n  Real b = f(2, a);\n"
      "end YmcFnArr2;\n";
  YMC_CHECK_EQ(flatten_src(s1),
               std::string("function f\n  input Integer n;\n  input Real[:] x;\n  output Real z;\n  output Real[n] y;\n"
                           "  protected Real[size(x, 1)] w = fill(0.0, size(x, 1));\n  protected Integer[n] m = fill(1, n);\n"
                           "algorithm\n  y := fill(2.0, n) + fill(1.0, n);\n  z := x * x + w * x + x[1] + sum(y);\nend f;\n\n"
                           "class YmcFnArr2\n  Real a[1];\n  Real a[2];\n  Real a[3];\n  Real b = f(2, a)[1];\nend YmcFnArr2;\n"));
  const char* s2 =
      "model FuncBuiltinScalar2\n  function f\n    input Real u[:];\n    output Real y = n;\n  protected\n"
      "    Integer n = scalar(size(u)) - 1;\n  end f;\n\n  Real x[1] = {1};\n  Real y = f(x);\nend FuncBuiltinScalar2;\n";
  YMC_CHECK_EQ(flatten_src(s2),
               std::string("function FuncBuiltinScalar2.f\n  input Real[:] u;\n  output Real y = /*Real*/(n);\n"
                           "  protected Integer n = size(u, 1) - 1;\nend FuncBuiltinScalar2.f;\n\nclass FuncBuiltinScalar2\n"
                           "  Real x[1];\n  Real y = FuncBuiltinScalar2.f(x);\nequation\n  x = {1.0};\nend FuncBuiltinScalar2;\n"));
}

YMC_TEST(flatten_function_emit_array_calls) {
  // 模型语境的数组调用：整个绑定 → 数组等式整体保留（NFScalarize :343-346，IfExpression11）；调用处求不出的输出维留未知
  //（evaluateCallTypeDim 只代换输入，FuncUnknownDim3 `Real[n] result`）；常量调用求值成字面量（FuncUnknownDim2）；
  // 嵌在别的运算里的数组调用 → SKIP（YmcIf11a：须逐元素展开，未做）。期望 = omc 1.25.0 实跑。
  const char* s1 =
      "function f\n  input Integer n;\n  output Real x[2];\nalgorithm\n  x := if n <> 2 then {1.0, 2.0, 3.0} else {1.0, 2.0};\n"
      "end f;\n\nmodel IfExpression11\n  Integer n;\n  Real x[2] = f(n);\nend IfExpression11;\n";
  YMC_CHECK_EQ(flatten_src(s1),
               std::string("function f\n  input Integer n;\n  output Real[2] x;\nalgorithm\n"
                           "  x := if n <> 2 then {1.0, 2.0, 3.0} else {1.0, 2.0};\nend f;\n\nclass IfExpression11\n"
                           "  Integer n;\n  Real x[1];\n  Real x[2];\nequation\n  x = f(n);\nend IfExpression11;\n"));
  const char* s2 =
      "function f\n  input Real[n] v;\n  output Real[n] result = fill(1.0, n);\nprotected\n  Integer n = size(v, 1);\nend f;\n\n"
      "model FuncUnknownDim3\n  Real x[2] = f({time, 1});\nend FuncUnknownDim3;\n";
  YMC_CHECK_EQ(flatten_src(s2),
               std::string("function f\n  input Real[n] v;\n  output Real[n] result = fill(1.0, n);\n"
                           "  protected Integer n = size(v, 1);\nend f;\n\nclass FuncUnknownDim3\n  Real x[1];\n  Real x[2];\n"
                           "equation\n  x = f({time, 1.0});\nend FuncUnknownDim3;\n"));
  const char* s3 =
      "function f\n  input Integer n;\n  output Real x[:];\nalgorithm\n  x := ones(n);\nend f;\n\n"
      "model FuncUnknownDim2\n  Real x[3] = f(3);\nend FuncUnknownDim2;\n";
  YMC_CHECK_EQ(flatten_src(s3), std::string("class FuncUnknownDim2\n  Real x[1];\n  Real x[2];\n  Real x[3];\nequation\n"
                                            "  x = {1.0, 1.0, 1.0};\nend FuncUnknownDim2;\n"));
  bool uns = false;
  flatten_src("function f\n  input Integer n;\n  output Real x[2];\nalgorithm\n  x := {1.0, 2.0};\nend f;\n"
              "model M\n  Integer n;\n  Real y = f(n) * {1, 1};\nend M;\n", &uns);
  YMC_CHECK(uns);
}

YMC_TEST(flatten_function_emit_records) {
  // 记录类型的形参 / 局部（C-i）：按类型名打印，构造函数块随之发射（字段逐个 input、带默认，constant 字段也打 input，
  // `output R res`，YmcRec2 探针）；记录局部的字段修饰不打印、只进排序依赖（`lx` 先于 `r`，FunctionCompOrder4）；
  // 字段引用 `r.x`。期望 = omc 1.25.0 实跑。
  const char* s1 =
      "record R\n  Real x;\n  Real y = 2;\n  Integer n = 3;\n  parameter Real p = 4;\n  constant Real c = 5;\nend R;\n"
      "function f\n  input R r;\n  output Real z = r.x + r.y;\nend f;\nmodel YmcRec2\n  R r;\n  Real z = f(r);\nend YmcRec2;\n";
  YMC_CHECK_EQ(flatten_src(s1),
               std::string("function R \"Automatically generated record constructor for R\"\n  input Real x;\n"
                           "  input Real y = 2.0;\n  input Integer n = 3;\n  input Real p = 4.0;\n  input Real c = 5.0;\n"
                           "  output R res;\nend R;\n\nfunction f\n  input R r;\n  output Real z = r.x + r.y;\nend f;\n\n"
                           "class YmcRec2\n  Real r.x;\n  Real r.y = 2.0;\n  Integer r.n = 3;\n  parameter Real r.p = 4.0;\n"
                           "  constant Real r.c = 5.0;\n  Real z = f(r);\nend YmcRec2;\n"));
  const char* s2 =
      "record R\n  Real x;\nend R;\n\nfunction f\n  input Real x;\n  output Real y;\nprotected\n  R r(x = lx);\n"
      "  Real lx = x;\nalgorithm\n  y := r.x;\nend f;\n\nmodel FunctionCompOrder4\n  Real x = f(time);\nend FunctionCompOrder4;\n";
  YMC_CHECK_EQ(flatten_src(s2),
               std::string("function R \"Automatically generated record constructor for R\"\n  input Real x;\n  output R res;\n"
                           "end R;\n\nfunction f\n  input Real x;\n  output Real y;\n  protected Real lx = x;\n  protected R r;\n"
                           "algorithm\n  y := r.x;\nend f;\n\nclass FunctionCompOrder4\n  Real x = f(time);\n"
                           "end FunctionCompOrder4;\n"));
}

// ---- 加宽(56) 正确性：nf-live（旧前端用例去注解 vs 实跑 omc 1.25.0）揪出的静默错出；期望 = omc 1.25.0 实跑 ----

YMC_TEST(flatten_structural_params_modifier_aware) {
  // 条件组件的条件按修饰后的绑定求值，用到的参数（含修饰书写处的 b、c）成为结构参数：打 final、绑定代入值、引用处
  // 代入（NFEvalConstants.evaluateExpTraverser，`Real y = if b then 1 else 2` → `1.0`）。YmcCond2 探针（此前 ymc 用声明
  // 默认值 false 求条件，删掉了 foo.x 且打印错值）。
  const char* src =
      "block Foo\n  parameter Boolean enabled = false;\n  Real x = 1 if enabled;\nend Foo;\n"
      "model YmcCond2\n  parameter Boolean b = true;\n  parameter Boolean c = not b;\n  Foo foo(enabled = b);\n"
      "  Foo foo2(enabled = c);\n  Real y = if b then 1 else 2;\nend YmcCond2;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class YmcCond2\n  final parameter Boolean b = true;\n  final parameter Boolean c = false;\n"
                           "  final parameter Boolean foo.enabled = true;\n  Real foo.x = 1.0;\n"
                           "  final parameter Boolean foo2.enabled = false;\n  Real y = 1.0;\nend YmcCond2;\n"));
  // 复合数组各元素的条件各自求值（`Foo[2] foo(enabled = {true, false})`），只删 foo[2].x 及连到它的 connect。
  const char* src2 =
      "connector InConn = input Real;\nconnector OutConn = output Real;\nblock Foo\n  parameter Boolean enabled = false;\n"
      "  InConn x if enabled;\nend Foo;\nblock Src\n  OutConn x = time;\nend Src;\nmodel ConnectArrayCond2\n"
      "  Foo[2] foo(enabled = {true, false});\n  Src src;\n  Src src1;\nequation\n  connect(src.x, foo[1].x);\n"
      "  connect(src1.x, foo[2].x);\nend ConnectArrayCond2;\n";
  YMC_CHECK_EQ(flatten_src(src2),
               std::string("class ConnectArrayCond2\n  final parameter Boolean foo[1].enabled = true;\n  Real foo[1].x;\n"
                           "  final parameter Boolean foo[2].enabled = false;\n  Real src.x = time;\n  Real src1.x = time;\n"
                           "equation\n  src.x = foo[1].x;\nend ConnectArrayCond2;\n"));
}

YMC_TEST(flatten_direction_inheritance) {
  // 字段方向 = mergeDirection(外层, 自身)；只在所有前缀组件都是连接器或 input 组件时保留（YmcDir1 探针）。
  const char* src =
      "record R\n  Real x;\n  Real xi;\nend R;\nconnector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model A\n  input R ra;\n  output C ca;\n  Real v;\nend A;\n"
      "model YmcDir1\n  input R ri;\n  output R ro;\n  input C ci;\n  output C co;\n  A a;\n  parameter R rp(x = 1, xi = 2);\n"
      "end YmcDir1;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class YmcDir1\n  input Real ri.x;\n  input Real ri.xi;\n  Real ro.x;\n  Real ro.xi;\n"
                           "  input Real ci.e;\n  input Real ci.f;\n  output Real co.e;\n  output Real co.f;\n"
                           "  Real a.ra.x;\n  Real a.ra.xi;\n  Real a.ca.e;\n  Real a.ca.f;\n  Real a.v;\n"
                           "  parameter Real rp.x = 1.0;\n  parameter Real rp.xi = 2.0;\nequation\n  ci.f = 0.0;\n"
                           "  co.f = 0.0;\n  a.ca.f = 0.0;\nend YmcDir1;\n"));
}

YMC_TEST(flatten_simplify_rules) {
  // NFSimplifyExp：`(-a) + b` → `b - a`、`(-e1)/e2` → `e1/(-e2)`、`x - (-1)` → `x + 1.0`、`±1 * x`；`^` 两侧提实型；
  // if 表达式的实型转换推进两支（Expression.typeCast）；字符串常量拼接；range 化简（YmcSimp1 探针）。
  const char* src =
      "model YmcSimp1\n  parameter Integer pi = 3;\n  Real x, a, d, x1, x2, x3, i, w;\n  String s;\n  Real z[5];\nequation\n"
      "  der(x) = -x + a*(1-x*x);\n  d = (-a*x)/x;\n  x1 = x - (-1);\n  x2 = -1*x;\n  x3 = 1*x;\n  i = 3 * (2 ^ pi);\n"
      "  w = if time > 1 then 1 else 2;\n  s = \"te\" + \"st\";\n  a = time;\nalgorithm\n  for k in 1:4+1 loop\n"
      "    z[k] := k;\n  end for;\nend YmcSimp1;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class YmcSimp1\n  parameter Integer pi = 3;\n  Real x;\n  Real a;\n  Real d;\n  Real x1;\n"
                           "  Real x2;\n  Real x3;\n  Real i;\n  Real w;\n  String s;\n  Real z[1];\n  Real z[2];\n  Real z[3];\n"
                           "  Real z[4];\n  Real z[5];\nequation\n  der(x) = a * (1.0 - x * x) - x;\n  d = a * x / (-x);\n"
                           "  x1 = x + 1.0;\n  x2 = -x;\n  x3 = x;\n  i = 3.0 * 2.0 ^ /*Real*/(pi);\n"
                           "  w = if time > 1.0 then 1.0 else 2.0;\n  s = \"test\";\n  a = time;\nalgorithm\n"
                           "  for k in 1:5 loop\n    z[k] := /*Real*/(k);\n  end for;\nend YmcSimp1;\n"));
}

YMC_TEST(flatten_record_ctor_and_external_builtin) {
  // 等式里整记录引用 → 收记录构造函数（根类里的记录名带根名，EquationComponent1）；external "C" 的已知数学函数按内建
  // 打印、不发射、不折叠（LogCall1 `mylog(100)` → `log(100.0)`）。
  const char* s1 = "class EquationComponent1\n  record R\n    Real x,y;\n  end R;\n  R a,b;\nequation\n  a = b;\nend EquationComponent1;\n";
  YMC_CHECK_EQ(flatten_src(s1),
               std::string("function EquationComponent1.R \"Automatically generated record constructor for EquationComponent1.R\"\n"
                           "  input Real x;\n  input Real y;\n  output R res;\nend EquationComponent1.R;\n\n"
                           "class EquationComponent1\n  Real a.x;\n  Real a.y;\n  Real b.x;\n  Real b.y;\nequation\n  a = b;\n"
                           "end EquationComponent1;\n"));
  const char* s2 = "function mylog \"Natural logarithm\"\n  input Real x;\n  output Real y;\n  external \"C\" y=log(x);\nend mylog;\n"
                   "model LogCall1\n  Real res;\nequation\n  res = mylog(100);\nend LogCall1;\n";
  YMC_CHECK_EQ(flatten_src(s2), std::string("class LogCall1\n  Real res;\nequation\n  res = log(100.0);\nend LogCall1;\n"));
}

YMC_TEST(flatten_invalid_models_unsupported) {
  // omc 1.25.0 报错的模型不得静默出结果（nf-live ACCEPTS-INVALID / scodeinst incorrect 用例的模式）：未做错误文案的一律 SKIP。
  for (const char* s : {
           "model A\n  Real x;\nend A;\nmodel M\n  A a(x = 1, x = 2);\nend M;\n",                         // 重复修饰
           "model A\n  Real x = 10;\n  final Real y = 20;\nend A;\nmodel M\n  A a(y = 30);\nend M;\n",      // 改 final
           "model T\nprotected\n  Integer x = 2;\nend T;\nmodel M\n  T t(x = 3);\nend M;\n",                // 改 protected
           "model T\nprotected\n  Integer x = 2;\nend T;\nmodel M\n  T t;\nequation\n  t.x = 3;\nend M;\n", // 访问 protected
           "model M\n  Real x(start = 2.0);\n  Real y = x.start;\nend M;\n",                              // 内建类型无子组件
           "model M\n  discrete Real x;\nequation\n  x = sin(time);\nend M;\n",                           // discrete Real 不在 when
           "package P\n  constant Real c = 1;\nend P;\n",                                                  // 根类是 package
           "partial model A\n  Real x;\nend A;\nmodel M\n  A a;\nend M;\n",                                // partial 类型
           "model M\n  Integer i = 4000 / 100;\nend M;\n",                                                 // Real 绑 Integer
           "model M\n  Real x;\n  Real x;\nend M;\n",                                                     // 重复声明
           "model M\n  type E = enumeration(one, start);\n  E e;\nend M;\n",                               // 枚举字面量撞属性
           "model M\n  Real x = time;\n  Integer n = N;\nend M;\n",                                     // 解析不到的名字
           "model M\n  parameter Real R = 1.0 + 0.1*sin(time);\nend M;\n"}) {                             // 参数绑连续量
    bool uns = false;
    flatten_src(s, &uns);
    YMC_CHECK(uns);
  }
}

// ---- parameter / constant 连接器（generatePotentialEquations :202-236 / makeEqualityAssert :290-341）；期望 = omc 1.25.0 ----

YMC_TEST(flatten_parameter_connector_member_assert) {
  // 集首为 parameter 的 potential 集 → `assert(abs(a - b) <= 0.0, …)`，按根序夹在普通方程与 flow 求和之间。ConstantConnector3。
  const char* src =
      "model ConstantConnector3\n  connector C\n    parameter Real x = 0;\n    Real e;\n    flow Real f;\n  end C;\n\n"
      "  C c1, c2;\nequation\n  connect(c1, c2);\nend ConstantConnector3;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class ConstantConnector3\n  parameter Real c1.x = 0.0;\n  Real c1.e;\n  Real c1.f;\n"
                           "  parameter Real c2.x = 0.0;\n  Real c2.e;\n  Real c2.f;\nequation\n  c1.e = c2.e;\n"
                           "  assert(abs(c1.x - c2.x) <= 0.0, \"Connected constants/parameters must be equal\");\n"
                           "  -(c1.f + c2.f) = 0.0;\n  c1.f = 0.0;\n  c2.f = 0.0;\nend ConstantConnector3;\n"));
}

YMC_TEST(flatten_parameter_connector_types_and_constant_fold) {
  // 非 Real → `a == b`（YmcCCInt1 探针）；constant 代入值后化简：相等 → assert(true) 被删、不等 → `assert(false, …)`
  //（YmcCCConst3 探针：集 [c1, c3, c2] → c1−c3 为真被删，只剩 c1−c2）。
  const char* msg = ", \"Connected constants/parameters must be equal\");\n";
  const char* src =
      "model YmcCCInt1\n  connector CI = parameter Integer;\n  connector CB = parameter Boolean;\n  CI i1 = 1, i2 = 1;\n"
      "  CB b1 = true, b2 = true;\nequation\n  connect(i1, i2);\n  connect(b1, b2);\nend YmcCCInt1;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class YmcCCInt1\n  parameter Integer i1 = 1;\n  parameter Integer i2 = 1;\n"
                           "  parameter Boolean b1 = true;\n  parameter Boolean b2 = true;\nequation\n  assert(i1 == i2") +
                   msg + "  assert(b1 == b2" + msg + "end YmcCCInt1;\n");
  const char* src2 =
      "model YmcCCConst3\n  connector C = constant Real;\n  C c1 = 1, c2 = 2, c3 = 1;\nequation\n  connect(c1, c2);\n"
      "  connect(c1, c3);\nend YmcCCConst3;\n";
  YMC_CHECK_EQ(flatten_src(src2),
               std::string("class YmcCCConst3\n  constant Real c1 = 1.0;\n  constant Real c2 = 2.0;\n"
                           "  constant Real c3 = 1.0;\nequation\n  assert(false") + msg + "end YmcCCConst3;\n");
}

YMC_TEST(flatten_unbound_fixed_parameter_unsupported) {
  // verifyBinding（NFFlatten.mo:812-875）：无绑定、fixed（缺省）且无 start 的 parameter → omc 报错 UNBOUND_PARAMETER_ERROR
  //（UnboundParameter1、YmcUnbArr1 探针）→ SKIP。fixed = false 合法照印（YmcUnfixed1）；constant 无绑定 1.25.0 不报错、照印
  //（YmcUnbConst1；源码 HEAD 的 NO_CONSTANT_BINDING 晚于 1.25.0）。期望 = omc 1.25.0 实跑。
  for (const char* s : {"model M\n  parameter Real x;\nend M;\n", "model M\n  parameter Real x[2];\nend M;\n"}) {
    bool uns = false;
    const std::string t = flatten_src(s, &uns);
    YMC_CHECK(uns);
    YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
  }
  YMC_CHECK_EQ(flatten_src("model M\n  parameter Real x(fixed = false);\nend M;\n"),
               std::string("class M\n  parameter Real x(fixed = false);\nend M;\n"));
  YMC_CHECK_EQ(flatten_src("model M\n  constant Integer n;\nend M;\n"),
               std::string("class M\n  constant Integer n;\nend M;\n"));
}

YMC_TEST(flatten_connect_flow_sum_mixed_faces) {
  // 四元 flow 集 [inside, inside, outside, outside]（inside 预插入在前）：`b + a - c1 - c2`（取负折叠）；
  // potential 集 head = c1（合并序里最先插入）。期望 = omc（InInOutOut 探针）。
  const char* src =
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model A\n  C c;\nend A;\n"
      "model M\n  A a, b;\n  C c1, c2;\nequation\n  connect(a.c, c1);\n  connect(b.c, c2);\n  connect(c1, c2);\nend M;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class M\n  Real a.c.e;\n  Real a.c.f;\n  Real b.c.e;\n  Real b.c.f;\n"
                           "  Real c1.e;\n  Real c1.f;\n  Real c2.e;\n  Real c2.f;\n"
                           "equation\n  c1.e = c2.e;\n  c1.e = b.c.e;\n  c1.e = a.c.e;\n"
                           "  b.c.f + a.c.f - c1.f - c2.f = 0.0;\n  c1.f = 0.0;\n  c2.f = 0.0;\nend M;\n"));
}

YMC_TEST(flatten_flow_outside_connector_unsupported) {
  // `flow` 加在非连接器变量上：omc 告警后当普通变量；ymc 不产告警文本 → 干净 SKIP（NonConnectorFlow1）。
  bool uns = false;
  std::string t = flatten_src("model M\n  flow Real f = 1.0;\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
}

YMC_TEST(flatten_unbound_parameter_with_start) {
  // 无绑定的固定 parameter 带 start → omc 拿 start 当绑定（updateParameterBinding）+ 告警（stderr）。期望 = omc 1.25.0 实跑
  //（2026-10-07）。
  YMC_CHECK_EQ(flatten_src("model M\n  parameter Real x(start = 1.0);\nend M;\n"),
               std::string("class M\n  parameter Real x(start = 1.0) = 1.0;\nend M;\n"));
}

YMC_TEST(flatten_scalar_builtin_fold_and_symbolic) {
  // 期望值来自安装版 omc 1.25.0 实跑 YmcBuiltinUnitProbe（2026-09-18）。
  const char* src =
      "model YmcBuiltinUnitProbe\n"
      "  Real r1;\n"
      "  Real r2;\n"
      "  Integer i1;\n"
      "  Integer i2;\n"
      "  Real sqrtConst = sqrt(25);\n"
      "  Real sqrtSymbol = sqrt(r1 + r2);\n"
      "  Real absReal = abs(-2.0);\n"
      "  Integer absInt = abs(3);\n"
      "  Integer integerNeg = integer(-9.9);\n"
      "  Integer divNeg = div(-10, 3);\n"
      "  Real x;\n"
      "  Real dx = der(x);\n"
      "  Integer signSymbol = sign(i1 + i2);\n"
      "end YmcBuiltinUnitProbe;\n";
  const char* want =
      "class YmcBuiltinUnitProbe\n"
      "  Real r1;\n"
      "  Real r2;\n"
      "  Integer i1;\n"
      "  Integer i2;\n"
      "  Real sqrtConst = 5.0;\n"
      "  Real sqrtSymbol = sqrt(r1 + r2);\n"
      "  Real absReal = 2.0;\n"
      "  Integer absInt = 3;\n"
      "  Integer integerNeg = -10;\n"
      "  Integer divNeg = -3;\n"
      "  Real x;\n"
      "  Real dx = der(x);\n"
      "  Integer signSymbol = sign(/*Real*/(i1 + i2));\n"
      "end YmcBuiltinUnitProbe;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_array_attribute_split) {
  // 数组组件的属性修饰按元素切分（Modifier.propagate → SPLIT_PROXY，NFModifier.mo:444-513；压平时换成元素下标，
  // NFFlatten.mo:1625-1683）：字面量 / 矩阵逐元素下降，类型化数组值逐元素标量化（`2.0 * p[1]`、`p[1] + 1.0`），each 每元素
  // 同值，复合数组修饰递归切到叶属性（`h2[i].x[j]`），无值的 `each start` 等于没写，`:` 维无绑定时取 start 的形状。
  // 期望 = omc 1.25.0 实跑 YmcArrAttr1（2026-10-02）。
  const char* src =
      "model A\n  Real x[2](start = {1, 2});\nend A;\n"
      "model YmcArrAttr1\n"
      "  parameter Real p[3] = {1, 2, 3};\n"
      "  parameter Integer ni[2] = {1, 2};\n"
      "  Real a[3](start = 2*p);\n"
      "  Real b[3](start = p + {1, 1, 1});\n"
      "  Real c[2](min = -{1, 2});\n"
      "  Real d[2,2](start = [1, 2; 3, 4]);\n"
      "  Real h[3](min = {1, 2, 3}, max = {4, 5, 6}, each fixed = true);\n"
      "  Real r[2](start = ni);\n"
      "  A a1(x(start = {3, 4}));\n"
      "  A g[2](each x(start = {6, 7}));\n"
      "  A h2[2](x(start = {{1, 2}, {3, 4}}));\n"
      "  Real w[2](each start);\n"
      "  Real z[:](start = {1, 2});\n"
      "  parameter Real pf[2](each fixed = false, start = {1, 2});\n"
      "end YmcArrAttr1;\n";
  const char* want =
      "class YmcArrAttr1\n"
      "  parameter Real p[1] = 1.0;\n  parameter Real p[2] = 2.0;\n  parameter Real p[3] = 3.0;\n"
      "  parameter Integer ni[1] = 1;\n  parameter Integer ni[2] = 2;\n"
      "  Real a[1](start = 2.0 * p[1]);\n  Real a[2](start = 2.0 * p[2]);\n  Real a[3](start = 2.0 * p[3]);\n"
      "  Real b[1](start = p[1] + 1.0);\n  Real b[2](start = p[2] + 1.0);\n  Real b[3](start = p[3] + 1.0);\n"
      "  Real c[1](min = -1.0);\n  Real c[2](min = -2.0);\n"
      "  Real d[1,1](start = 1.0);\n  Real d[1,2](start = 2.0);\n  Real d[2,1](start = 3.0);\n  Real d[2,2](start = 4.0);\n"
      "  Real h[1](min = 1.0, max = 4.0, fixed = true);\n  Real h[2](min = 2.0, max = 5.0, fixed = true);\n"
      "  Real h[3](min = 3.0, max = 6.0, fixed = true);\n"
      "  Real r[1](start = /*Real*/(ni[1]));\n  Real r[2](start = /*Real*/(ni[2]));\n"
      "  Real a1.x[1](start = 3.0);\n  Real a1.x[2](start = 4.0);\n"
      "  Real g[1].x[1](start = 6.0);\n  Real g[1].x[2](start = 7.0);\n"
      "  Real g[2].x[1](start = 6.0);\n  Real g[2].x[2](start = 7.0);\n"
      "  Real h2[1].x[1](start = 1.0);\n  Real h2[1].x[2](start = 2.0);\n"
      "  Real h2[2].x[1](start = 3.0);\n  Real h2[2].x[2](start = 4.0);\n"
      "  Real w[1];\n  Real w[2];\n"
      "  Real z[1](start = 1.0);\n  Real z[2](start = 2.0);\n"
      "  parameter Real pf[1](start = 1.0, fixed = false);\n  parameter Real pf[2](start = 2.0, fixed = false);\n"
      "end YmcArrAttr1;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_array_attribute_split_rejects) {
  // omc 报错的切分形态（BuiltinAttribute24 维不符、Each3 缺 each、标量组件给数组值、多余元素）、无绑定参数缺省取 start
  //（omc 告警，UnboundParameter6）、以及 1.25.0 的两处求值分歧（分歧账本）：切分字面量叶类型混杂时 omc 逐元素各自类型化
  //（`{1, 2.0}` → `start = 1`、`p = {1, 2, 3.0}` → `a[1].p = 1`，typeSubscriptedExp2 NFTyping.mo:1655-1688）；函数求值把 cref
  // 实参的数组原地改写（`y := x; y[1] := 0` 改掉调用者的 constant，P8 探针）。都须干净 SKIP。
  const char* cases[] = {
      "model M\n  Real x[2,2](start = {3, 1});\nend M;\n",
      "model M\n  Real x[3](start = 1);\nend M;\n",
      "model M\n  Real y(start = {1, 2});\nend M;\n",
      "model M\n  Real x[2](start = {1, 2, 3});\nend M;\n",
      "model A\n  Real n[2];\nend A;\nmodel M\n  A a(each n(fixed = true));\nend M;\n",
      "model M\n  parameter Real x[3](each start = 1.0);\nend M;\n",
      "model M\n  Real g[2](start = {1, 2.0});\nend M;\n",
      "model A\n  Real p;\nend A;\nmodel M\n  A a[3](p = {1, 2, 3.0});\nend M;\n",
      "function f\n  input Real x[3];\n  output Real y[3];\nalgorithm\n  y := x;\n  y[1] := 0;\nend f;\n"
      "model M\n  constant Real a[3] = {1, 2, 3};\n  Real b[3] = f(a);\nend M;\n",
  };
  for (const char* s : cases) {
    bool uns = false;
    const std::string t = flatten_src(s, &uns);
    YMC_CHECK(uns);
    YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
  }
}

YMC_TEST(flatten_array_literal_operand_elementwise) {
  // 数组字面量作运算数按其形状定型（inst 给字面量的是粗类型，此前 `p + {1, 1, 1}` 被当数组 + 标量广播，打出
  // `p[1] + {1, 1, 1}`）；多维逐元素展开按维嵌套（ArrayAddSub1）。期望 = omc 1.25.0 实跑 P6 / ArrayAddSub1（2026-10-02）。
  const char* src =
      "model P6\n  parameter Real p[3] = {1, 2, 3};\n  parameter Real q[3] = p + {1, 1, 1};\n  Real r[3] = p + {1, 1, 1};\n"
      "  Real s[3];\nequation\n  s = p + {1, 1, 1};\nend P6;\n";
  const char* want =
      "class P6\n  parameter Real p[1] = 1.0;\n  parameter Real p[2] = 2.0;\n  parameter Real p[3] = 3.0;\n"
      "  parameter Real q[1] = p[1] + 1.0;\n  parameter Real q[2] = p[2] + 1.0;\n  parameter Real q[3] = p[3] + 1.0;\n"
      "  Real r[1];\n  Real r[2];\n  Real r[3];\n  Real s[1];\n  Real s[2];\n  Real s[3];\n"
      "equation\n  r = {p[1] + 1.0, p[2] + 1.0, p[3] + 1.0};\n  s[1] = p[1] + 1.0;\n  s[2] = p[2] + 1.0;\n"
      "  s[3] = p[3] + 1.0;\nend P6;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
  YMC_CHECK_EQ(flatten_src("model AddSub1\n  Real Add3[2, 2] = {{1, 1}, {2, 2}} + {{1, 2}, {3, 4}};\nend AddSub1;\n"),
               std::string("class AddSub1\n  Real Add3[1,1];\n  Real Add3[1,2];\n  Real Add3[2,1];\n  Real Add3[2,2];\n"
                           "equation\n  Add3 = {{2.0, 3.0}, {5.0, 6.0}};\nend AddSub1;\n"));
}

YMC_TEST(flatten_enum_literal_scope_path) {
  // 枚举字面量 / 枚举维下标打枚举类的 scopePath（makeEnumerationType，NFInst.mo:627-638；NFInstNode.scopePath :1311-1381）：
  // 根类里声明 → `E.a`；顶层 → `E0.a0`；包里 → `P.E3.x`；组件 m 的类里声明 → `m.F.u`（此前打成 `F.u`，静默错）。
  // 期望 = omc 1.25.0 实跑 P9 / P10（2026-10-02）。
  const char* src9 =
      "package P\n  type E3 = enumeration(x, y);\nend P;\ntype E0 = enumeration(a0, b0);\n"
      "model P9\n  type E = enumeration(a, b);\n  Real x[E];\n  Real y[P.E3];\n  Real z[E0];\n"
      "  Real w[E0](start = {1, 2});\n  parameter Real q[E0] = {3, 4};\nend P9;\n";
  YMC_CHECK_EQ(flatten_src(src9),
               std::string("class P9\n  Real x[E.a];\n  Real x[E.b];\n  Real y[P.E3.x];\n  Real y[P.E3.y];\n"
                           "  Real z[E0.a0];\n  Real z[E0.b0];\n  Real w[E0.a0](start = 1.0);\n  Real w[E0.b0](start = 2.0);\n"
                           "  parameter Real q[E0.a0] = 3.0;\n  parameter Real q[E0.b0] = 4.0;\nend P9;\n"));
  const char* src10 =
      "model A\n  type F = enumeration(u, v);\n  Real y[F];\n  F f = F.v;\n  parameter F g = F.u;\n"
      "  Real z = if f == F.v then 1 else 2;\nend A;\nmodel P10\n  A m;\nend P10;\n";
  YMC_CHECK_EQ(flatten_src(src10),
               std::string("class P10\n  Real m.y[m.F.u];\n  Real m.y[m.F.v];\n  enumeration(u, v) m.f = m.F.v;\n"
                           "  parameter enumeration(u, v) m.g = m.F.u;\n  Real m.z = if m.f == m.F.v then 1.0 else 2.0;\n"
                           "end P10;\n"));
}

YMC_TEST(flatten_colon_dim_from_typed_binding) {
  // `:` 维取类型化绑定的维（deduceDimensionFromExp，NFTyping.mo:781-826）：函数调用的输出维在调用处求出
  //（RealAddEw `Real x[:] = f(3, {4, 5, 6})`）。期望 = omc 1.25.0 实跑 RealAddEw（nf-live，2026-10-02）。
  const char* src =
      "function f\n  input Real r1;\n  input Real r2[:];\n  output Real o[size(r2, 1)];\nalgorithm\n  o := r1 .+ r2;\nend f;\n"
      "model RealAddEw\n  Real x[:] = f(3, {4, 5, 6});\nend RealAddEw;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string("class RealAddEw\n  Real x[1];\n  Real x[2];\n  Real x[3];\nequation\n"
                                             "  x = {7.0, 8.0, 9.0};\nend RealAddEw;\n"));
}

YMC_TEST(flatten_matrix_literal) {
  // 矩阵构造（typeMatrix / typeMatrixComma，NFTyping.mo:2294-2393 + makeCatExp）展开成嵌套数组：标量元素、向量元素按列拼接
  //（`[a, a]`）、单行 / 单列、混型转 Real、Integer 保持；方程里的嵌套字面量逐元素标量化。期望 = omc 1.25.0 实跑 M1（2026-10-02）。
  const char* src =
      "model M1\n  Real x, y;\n  Real a[2];\n  Real m[2,2] = [1, 2; 3, 4];\n  parameter Real p[2,2] = [1, 2; 3, 4];\n"
      "  Real m2[2,2] = [x, y; y, x];\n  Real v[1,3] = [1, 2, 3];\n  Real c[3,1] = [1; 2; 3];\n  Real m3[2,2] = [a, a];\n"
      "  Real m4[2,2];\n  Real m5[2,2] = [1, 2.5; x, 4];\n  Integer k[2,2] = [1, 2; 3, 4];\nequation\n  m4 = [x, 1; 2, y];\nend M1;\n";
  const char* want =
      "class M1\n  Real x;\n  Real y;\n  Real a[1];\n  Real a[2];\n"
      "  Real m[1,1];\n  Real m[1,2];\n  Real m[2,1];\n  Real m[2,2];\n"
      "  parameter Real p[1,1] = 1.0;\n  parameter Real p[1,2] = 2.0;\n  parameter Real p[2,1] = 3.0;\n  parameter Real p[2,2] = 4.0;\n"
      "  Real m2[1,1];\n  Real m2[1,2];\n  Real m2[2,1];\n  Real m2[2,2];\n"
      "  Real v[1,1];\n  Real v[1,2];\n  Real v[1,3];\n  Real c[1,1];\n  Real c[2,1];\n  Real c[3,1];\n"
      "  Real m3[1,1];\n  Real m3[1,2];\n  Real m3[2,1];\n  Real m3[2,2];\n"
      "  Real m4[1,1];\n  Real m4[1,2];\n  Real m4[2,1];\n  Real m4[2,2];\n"
      "  Real m5[1,1];\n  Real m5[1,2];\n  Real m5[2,1];\n  Real m5[2,2];\n"
      "  Integer k[1,1];\n  Integer k[1,2];\n  Integer k[2,1];\n  Integer k[2,2];\n"
      "equation\n  m = {{1.0, 2.0}, {3.0, 4.0}};\n  m2 = {{x, y}, {y, x}};\n  v = {{1.0, 2.0, 3.0}};\n"
      "  c = {{1.0}, {2.0}, {3.0}};\n  m3 = {{a[1], a[1]}, {a[2], a[2]}};\n  m5 = {{1.0, 2.5}, {x, 4.0}};\n"
      "  k = {{1, 2}, {3, 4}};\n  m4[1,1] = x;\n  m4[1,2] = 1.0;\n  m4[2,1] = 2.0;\n  m4[2,2] = y;\nend M1;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_constant_and_structural_subscripts) {
  // 下标求值：for 展开后的 `x[i - 1]`、常量 `x[N]`、`end` → 字面量；参数下标是结构参数（typeSubscripts 标
  // Structural.markSubscript，NFTyping.mo:2122-2127）→ 参数打 final、下标代入值（含修饰 `a.k = 2`、依赖链 `d = c + 1`）。
  // 期望 = omc 1.25.0 实跑 S1（2026-10-02）。
  const char* src =
      "model A\n  parameter Integer k = 1;\n  Real z[2];\nequation\n  z[k] = 3;\n  z[2] = 4;\nend A;\n"
      "model S1\n  constant Integer N = 3;\n  parameter Integer c = 1;\n  parameter Integer d = c + 1;\n  Real r[3];\n"
      "  Real x[N];\n  A a(k = 2);\nequation\n  r[c] = 1;\n  r[d] = 2;\n  r[end] = 3;\n  for i in 2:N loop\n"
      "    x[i - 1] = x[i] + 1;\n  end for;\n  x[N] = 0;\nend S1;\n";
  const char* want =
      "class S1\n  constant Integer N = 3;\n  final parameter Integer c = 1;\n  final parameter Integer d = 2;\n"
      "  Real r[1];\n  Real r[2];\n  Real r[3];\n  Real x[1];\n  Real x[2];\n  Real x[3];\n"
      "  final parameter Integer a.k = 2;\n  Real a.z[1];\n  Real a.z[2];\n"
      "equation\n  a.z[2] = 3.0;\n  a.z[2] = 4.0;\n  r[1] = 1.0;\n  r[2] = 2.0;\n  r[3] = 3.0;\n"
      "  x[1] = x[2] + 1.0;\n  x[2] = x[3] + 1.0;\n  x[3] = 0.0;\nend S1;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_array_slices) {
  // 切片下标 `:` / `a:b` / `{i, j}`（Subscript.WHOLE / SLICE）：连续数组绑定保持整体（`y = x[2:3]`、`w = z[1,:]`，下标间无
  // 空格），参数绑定按切片映射摊到元素（`q[2] = p[3]`），方程逐元素标量化。期望 = omc 1.25.0 实跑 SL2（2026-10-02）。
  const char* src =
      "model SL2\n  Real x[3] = {1, 2, 3};\n  Real y[2] = x[2:3];\n  Real z[2,3];\n  Real w[3] = z[1, :];\n"
      "  Real v[2] = z[:, 2];\n  Real u[2];\n  parameter Real p[3] = {1, 2, 3};\n  parameter Real q[2] = p[{1, 3}];\n"
      "equation\n  z[1, :] = {1, 2, 3};\n  z[2, :] = x;\n  u[1:2] = x[2:3] .* 2;\nend SL2;\n";
  const char* want =
      "class SL2\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y[1];\n  Real y[2];\n"
      "  Real z[1,1];\n  Real z[1,2];\n  Real z[1,3];\n  Real z[2,1];\n  Real z[2,2];\n  Real z[2,3];\n"
      "  Real w[1];\n  Real w[2];\n  Real w[3];\n  Real v[1];\n  Real v[2];\n  Real u[1];\n  Real u[2];\n"
      "  parameter Real p[1] = 1.0;\n  parameter Real p[2] = 2.0;\n  parameter Real p[3] = 3.0;\n"
      "  parameter Real q[1] = p[1];\n  parameter Real q[2] = p[3];\n"
      "equation\n  x = {1.0, 2.0, 3.0};\n  y = x[2:3];\n  w = z[1,:];\n  v = z[:,2];\n"
      "  z[1,1] = 1.0;\n  z[1,2] = 2.0;\n  z[1,3] = 3.0;\n  z[2,1] = x[1];\n  z[2,2] = x[2];\n  z[2,3] = x[3];\n"
      "  u[1] = x[2] * 2.0;\n  u[2] = x[3] * 2.0;\nend SL2;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_model_array_builtins) {
  // 模型语境的数组内建按 omc 展开（ExpandExp / SimplifyExp）：fill / zeros / ones / identity / diagonal / transpose / vector →
  // 数组字面量（参数维 `ones(n)` 把 n 打 final），sum / product → 元素运算链，常量 min 折叠。
  // 期望 = omc 1.25.0 实跑 B2（2026-10-02）。
  const char* src =
      "model B2\n  parameter Integer n = 3;\n  Real x[3] = fill(1.0, 3);\n  parameter Real p[3] = fill(2, 3);\n"
      "  Real y[2,2] = zeros(2, 2);\n  Real z[n] = ones(n);\n  Real a[3];\n  Real s = sum(a);\n  Real t = sum(a[1:2]);\n"
      "  Real pr = product(a);\n  Real mn = min({1, 2, 3});\n  Real m[2,2] = identity(2);\n"
      "  Real tr[2,2] = transpose([1, 2; 3, 4]);\n  Real d[2,2] = diagonal({1, 2});\n  Real w[3] = fill(time, 3);\n"
      "  Real sc = scalar({{5}});\n  Real v[2] = vector([1; 2]);\nequation\n  a = fill(time, 3);\nend B2;\n";
  const char* want =
      "class B2\n  final parameter Integer n = 3;\n  Real x[1];\n  Real x[2];\n  Real x[3];\n"
      "  parameter Real p[1] = 2.0;\n  parameter Real p[2] = 2.0;\n  parameter Real p[3] = 2.0;\n"
      "  Real y[1,1];\n  Real y[1,2];\n  Real y[2,1];\n  Real y[2,2];\n  Real z[1];\n  Real z[2];\n  Real z[3];\n"
      "  Real a[1];\n  Real a[2];\n  Real a[3];\n  Real s = a[1] + a[2] + a[3];\n  Real t = a[1] + a[2];\n"
      "  Real pr = a[1] * a[2] * a[3];\n  Real mn = 1.0;\n  Real m[1,1];\n  Real m[1,2];\n  Real m[2,1];\n  Real m[2,2];\n"
      "  Real tr[1,1];\n  Real tr[1,2];\n  Real tr[2,1];\n  Real tr[2,2];\n  Real d[1,1];\n  Real d[1,2];\n  Real d[2,1];\n"
      "  Real d[2,2];\n  Real w[1];\n  Real w[2];\n  Real w[3];\n  Real sc = 5.0;\n  Real v[1];\n  Real v[2];\n"
      "equation\n  x = {1.0, 1.0, 1.0};\n  y = {{0.0, 0.0}, {0.0, 0.0}};\n  z = {1.0, 1.0, 1.0};\n"
      "  m = {{1.0, 0.0}, {0.0, 1.0}};\n  tr = {{1.0, 3.0}, {2.0, 4.0}};\n  d = {{1.0, 0.0}, {0.0, 2.0}};\n"
      "  w = {time, time, time};\n  v = {1.0, 2.0};\n  a[1] = time;\n  a[2] = time;\n  a[3] = time;\nend B2;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_parameter_comprehension_binding) {
  // 参数 / 常量数组的推导式绑定逐元素代入迭代变量（applyIndexSubscriptArrayConstructor + replaceIterator）：`:` 维取范围
  // 长度，`p0 * 1` 化简成 `p0`。期望 = omc 1.25.0 实跑 C4（2026-10-02）。
  const char* src =
      "model C4\n  parameter Integer n = 3;\n  parameter Real p[n] = {i^2 for i in 1:n};\n"
      "  parameter Real q[:] = {2 * i for i in 1:4};\n  parameter Real p0 = 2;\n  parameter Real r[3] = {p0 * i for i in 1:3};\n"
      "  constant Integer c[3] = {i + 1 for i in 1:3};\nend C4;\n";
  const char* want =
      "class C4\n  final parameter Integer n = 3;\n  parameter Real p[1] = 1.0;\n  parameter Real p[2] = 4.0;\n"
      "  parameter Real p[3] = 9.0;\n  parameter Real q[1] = 2.0;\n  parameter Real q[2] = 4.0;\n  parameter Real q[3] = 6.0;\n"
      "  parameter Real q[4] = 8.0;\n  parameter Real p0 = 2.0;\n  parameter Real r[1] = p0;\n  parameter Real r[2] = p0 * 2.0;\n"
      "  parameter Real r[3] = p0 * 3.0;\n  constant Integer c[1] = 2;\n  constant Integer c[2] = 3;\n  constant Integer c[3] = 4;\n"
      "end C4;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_algorithm_control_statements) {
  // 模型算法段的 if / while / break / when / assert（NFFlatten.flattenStatement 照搬结构）；if 的常量条件按
  // simplifyIfStmtBranches（NFSimplifyModel.mo:575-616）化简：false 分支删、首个 true 分支去 if、后续 true 变 else；for 范围的
  // 参数是结构参数（typeRange :2455-2460 → `1:3`、n 打 final）。期望 = omc 1.25.0 实跑 AS1（2026-10-02）。
  const char* src =
      "model AS1\n  Real r;\n  Real x;\n  parameter Real v[3] = {1, -2, 3};\n  parameter Integer n = 3;\n  Integer i;\n"
      "algorithm\n  if false then\n    r := 1;\n  end if;\n  if true then\n    r := 1.5;\n  else\n    r := 2.1;\n  end if;\n"
      "  if time > 0.5 then\n    r := 3;\n  elseif true then\n    r := 4;\n  else\n    r := 5;\n  end if;\n  x := 0;\n"
      "  for k in 1:n loop\n    if v[k] > 0 then\n      x := x + v[k];\n    end if;\n  end for;\n  i := 0;\n"
      "  while i < 3 loop\n    i := i + 1;\n    if i > 1 then\n      break;\n    end if;\n  end while;\n"
      "  when x > 2 then\n    r := 0;\n  end when;\n  assert(x < 10, \"too big\");\nend AS1;\n";
  const char* want =
      "class AS1\n  Real r;\n  Real x;\n  parameter Real v[1] = 1.0;\n  parameter Real v[2] = -2.0;\n  parameter Real v[3] = 3.0;\n"
      "  final parameter Integer n = 3;\n  Integer i;\nalgorithm\n  r := 1.5;\n  if time > 0.5 then\n    r := 3.0;\n  else\n"
      "    r := 4.0;\n  end if;\n  x := 0.0;\n  for k in 1:3 loop\n    if v[k] > 0.0 then\n      x := x + v[k];\n    end if;\n"
      "  end for;\n  i := 0;\n  while i < 3 loop\n    i := i + 1;\n    if i > 1 then\n      break;\n    end if;\n"
      "  end while;\n  when x > 2.0 then\n    r := 0.0;\n  end when;\n  assert(x < 10.0, \"too big\");\nend AS1;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_when_condition_forms) {
  // when 条件：sample(a, b)（实参转 Real）、Boolean 数组 cref 按元素展开成条件向量、条件向量字面量、change(x)。
  // 期望 = omc 1.25.0 实跑 W1（2026-10-02）。
  const char* src =
      "model W1\n  parameter Real h = 0.1;\n  Real x(start = 5);\n  Real y;\n  Real z;\n  Real w;\n  Boolean b[2];\n"
      "  Boolean c;\n  Integer k;\nequation\n  der(x) = -x;\n  b = {time > 1, time > 2};\n  c = time > 3;\n"
      "  when sample(0, h) then\n    y = x;\n  end when;\n  when b then\n    z = x;\n  end when;\n"
      "  when {c, time > 4} then\n    w = x;\n  end when;\n  when change(k) then\n    k = pre(k) + 1;\n  end when;\nend W1;\n";
  const char* want =
      "class W1\n  parameter Real h = 0.1;\n  Real x(start = 5.0);\n  Real y;\n  Real z;\n  Real w;\n  Boolean b[1];\n"
      "  Boolean b[2];\n  Boolean c;\n  Integer k;\nequation\n  der(x) = -x;\n  b[1] = time > 1.0;\n  b[2] = time > 2.0;\n"
      "  c = time > 3.0;\n  when sample(0.0, h) then\n    y = x;\n  end when;\n  when {b[1], b[2]} then\n    z = x;\n"
      "  end when;\n  when {c, time > 4.0} then\n    w = x;\n  end when;\n  when change(k) then\n    k = pre(k) + 1;\n"
      "  end when;\nend W1;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_tuple_targets) {
  // 元组方程 / 元组赋值（NFSimplifyModel.simplifyTupleElement :430-464）：常量实参 → 求值后逐元素拆开（空位跳过）；否则保留
  // 元组形、空位打 `_`；单元素括号 `(v)` 只是括号 → 取首输出 `f(time)[1]`。期望 = omc 1.25.0 实跑 T1 / T2（2026-10-02）。
  const char* f1 = "function f\n  input Real x;\n  output Real a = x * 2;\n  output Real b = x * 3;\nend f;\n";
  YMC_CHECK_EQ(flatten_src((std::string(f1) +
                            "model T1\n  Real p, q, r, s, u, v;\nequation\n  (p, q) = f(1.0);\n  (r, s) = f(time);\n"
                            "algorithm\n  (u, v) := f(time);\n  (, u) := f(2.0);\nend T1;\n").c_str()),
               std::string("function f\n  input Real x;\n  output Real a = x * 2.0;\n  output Real b = x * 3.0;\nend f;\n\n"
                           "class T1\n  Real p;\n  Real q;\n  Real r;\n  Real s;\n  Real u;\n  Real v;\nequation\n"
                           "  p = 2.0;\n  q = 3.0;\n  (r, s) = f(time);\nalgorithm\n  (u, v) := f(time);\n  u := 6.0;\nend T1;\n"));
  const char* f2 =
      "function f\n  input Real x;\n  output Real a = x * 2;\n  output Real b = x * 3;\n  output Integer c = 1;\nend f;\n";
  YMC_CHECK_EQ(flatten_src((std::string(f2) +
                            "model T2\n  Real u, v, w;\n  Integer k;\nequation\n  (w, , k) = f(time);\nalgorithm\n"
                            "  (, u) := f(time);\n  (v) := f(time);\nend T2;\n").c_str()),
               std::string("function f\n  input Real x;\n  output Real a = x * 2.0;\n  output Real b = x * 3.0;\n"
                           "  output Integer c = 1;\nend f;\n\nclass T2\n  Real u;\n  Real v;\n  Real w;\n  Integer k;\n"
                           "equation\n  (w, _, k) = f(time);\nalgorithm\n  (_, u) := f(time);\n  v := f(time)[1];\nend T2;\n"));
}

YMC_TEST(flatten_imports_in_expressions) {
  // 表达式里经 import 的名字（MLS §13.2；NFImport / R12 §5）：限定 / 重命名 / `.*` 的常量内联，经 `.*` 的函数按真实路径
  // 发射（`A.B.f`），未被用到的同名冲突 import 不报错。期望 = omc 1.25.0 实跑 IM1（2026-10-02）。
  const char* src =
      "package A\n  constant Real PI = 3.14;\n  constant Real e = 2.7;\n  package B\n    constant Real c = 3.0e8;\n"
      "    function f\n      input Real x;\n      output Real y;\n    algorithm\n      y := 2 * x;\n    end f;\n  end B;\n"
      "  model M\n    Real z;\n  end M;\n  model N\n    Real z;\n  end N;\nend A;\n"
      "model IM1\n  import A.PI;\n  import my_e = A.e;\n  import A.B.*;\n  import Q = A.M;\n  import Q = A.N;\n"
      "  Real x = 3 * PI;\n  Real x2 = my_e;\n  Real x3 = c;\n  Real x4 = f(time);\n  parameter Real x5 = f(2);\nend IM1;\n";
  const char* want =
      "function A.B.f\n  input Real x;\n  output Real y;\nalgorithm\n  y := 2.0 * x;\nend A.B.f;\n\n"
      "class IM1\n  Real x = 9.42;\n  Real x2 = 2.7;\n  Real x3 = 3e8;\n  Real x4 = A.B.f(time);\n  parameter Real x5 = 4.0;\n"
      "end IM1;\n";
  YMC_CHECK_EQ(flatten_src(src), std::string(want));
}

YMC_TEST(flatten_parameter_constant_records) {
  // parameter / constant 记录（及记录数组）的变率前缀按 variabilityMin 下沉到每个字段：继承字段、数组字段、嵌套记录、自带
  // parameter 的字段、final 字段（final 保留）；constant 记录字段引用内联（`c.v[2]`、`c[2].x`）。
  // 期望 = omc 1.25.0 实跑 R1 / R2（2026-10-02）。
  const char* src1 =
      "record Inner\n  Real a = 1;\n  Integer n = 2;\nend Inner;\nrecord Base\n  Real b = 3;\nend Base;\n"
      "record R\n  extends Base;\n  Real x = 4;\n  Real v[2] = {5, 6};\n  Inner in1;\n  parameter Real p = 7;\n"
      "  final Real f = 8;\nend R;\nmodel R1\n  parameter R r;\n  constant R c;\n  parameter R r2(x = 10, in1(a = 11));\n"
      "  Real y = r.x + c.v[2] + r2.in1.a;\nend R1;\n";
  YMC_CHECK_EQ(flatten_src(src1),
               std::string("class R1\n  parameter Real r.b = 3.0;\n  parameter Real r.x = 4.0;\n  parameter Real r.v[1] = 5.0;\n"
                           "  parameter Real r.v[2] = 6.0;\n  parameter Real r.in1.a = 1.0;\n  parameter Integer r.in1.n = 2;\n"
                           "  parameter Real r.p = 7.0;\n  final parameter Real r.f = 8.0;\n  constant Real c.b = 3.0;\n"
                           "  constant Real c.x = 4.0;\n  constant Real c.v[1] = 5.0;\n  constant Real c.v[2] = 6.0;\n"
                           "  constant Real c.in1.a = 1.0;\n  constant Integer c.in1.n = 2;\n  constant Real c.p = 7.0;\n"
                           "  final constant Real c.f = 8.0;\n  parameter Real r2.b = 3.0;\n  parameter Real r2.x = 10.0;\n"
                           "  parameter Real r2.v[1] = 5.0;\n  parameter Real r2.v[2] = 6.0;\n  parameter Real r2.in1.a = 11.0;\n"
                           "  parameter Integer r2.in1.n = 2;\n  parameter Real r2.p = 7.0;\n  final parameter Real r2.f = 8.0;\n"
                           "  Real y = r.x + 6.0 + r2.in1.a;\nend R1;\n"));
  const char* src2 =
      "record R\n  Real x = 1;\n  Real v[2] = {2, 3};\nend R;\nmodel R2\n  parameter R r[2];\n  constant R c[2](x = {4, 5});\n"
      "  parameter R q[2](each x = 6);\n  Real y = r[1].x + c[2].x;\nend R2;\n";
  YMC_CHECK_EQ(flatten_src(src2),
               std::string("class R2\n  parameter Real r[1].x = 1.0;\n  parameter Real r[1].v[1] = 2.0;\n"
                           "  parameter Real r[1].v[2] = 3.0;\n  parameter Real r[2].x = 1.0;\n  parameter Real r[2].v[1] = 2.0;\n"
                           "  parameter Real r[2].v[2] = 3.0;\n  constant Real c[1].x = 4.0;\n  constant Real c[1].v[1] = 2.0;\n"
                           "  constant Real c[1].v[2] = 3.0;\n  constant Real c[2].x = 5.0;\n  constant Real c[2].v[1] = 2.0;\n"
                           "  constant Real c[2].v[2] = 3.0;\n  parameter Real q[1].x = 6.0;\n  parameter Real q[1].v[1] = 2.0;\n"
                           "  parameter Real q[1].v[2] = 3.0;\n  parameter Real q[2].x = 6.0;\n  parameter Real q[2].v[1] = 2.0;\n"
                           "  parameter Real q[2].v[2] = 3.0;\n  Real y = r[1].x + 5.0;\nend R2;\n"));
}

YMC_TEST(flatten_string_builtin) {
  // String(x, …)（MLS §3.7.2）：实参按重载补齐缺省打印；全常量 → 求值成字符串（%g 有效数字、minimumLength 补空格、format、
  // 枚举字面量名）；Real 结果带指数照 Windows 版 omc 1.25.0 补到三位（见 docs/open-questions.md），带宽度的 format 打出指数
  // → SKIP。期望 = omc 1.25.0 实跑 S1 / S3（2026-10-02）、`String(1e10)`（2026-10-14）。
  const char* src1 =
      "model S1\n  Real x = time;\n  Integer n = 3;\n  Boolean b = true;\n  parameter Real p = 1.5;\n  String s1 = String(x);\n"
      "  String s2 = String(n);\n  String s3 = String(b);\n  String s4 = String(p, significantDigits = 3);\n"
      "  String s5 = String(n, minimumLength = 4, leftJustified = false);\n  String s6 = String(2.5);\n"
      "  String s7 = String(x, format = \"6.2f\");\nend S1;\n";
  YMC_CHECK_EQ(flatten_src(src1),
               std::string("class S1\n  Real x = time;\n  Integer n = 3;\n  Boolean b = true;\n  parameter Real p = 1.5;\n"
                           "  String s1 = String(x, 6, 0, true);\n  String s2 = String(n, 0, true);\n"
                           "  String s3 = String(b, 0, true);\n  String s4 = String(p, 3, 0, true);\n"
                           "  String s5 = String(n, 4, false);\n  String s6 = \"2.5\";\n  String s7 = String(x, \"6.2f\");\n"
                           "end S1;\n"));
  const char* src3 =
      "type E = enumeration(one, two);\nmodel S3\n  String a = String(1/3);\n  String d = String(100.0);\n"
      "  String e = String(3, minimumLength = 4);\n  String f = String(3, minimumLength = 4, leftJustified = false);\n"
      "  String g = String(true, minimumLength = 6);\n  String h = String(2.5, significantDigits = 2);\n"
      "  String i = String(2.5, format = \"6.2f\");\n  String j = String(E.two);\n  String k = String(-0.5);\nend S3;\n";
  YMC_CHECK_EQ(flatten_src(src3),
               std::string("class S3\n  String a = \"0.333333\";\n  String d = \"100\";\n  String e = \"3   \";\n"
                           "  String f = \"   3\";\n  String g = \"true  \";\n  String h = \"2.5\";\n  String i = \"  2.50\";\n"
                           "  String j = \"two\";\n  String k = \"-0.5\";\nend S3;\n"));
  YMC_CHECK_EQ(flatten_src("model M\n  String b = String(1e10);\nend M;\n"),
               std::string("class M\n  String b = \"1e+010\";\nend M;\n"));
  bool uns = false;
  const std::string t = flatten_src("model M\n  String b = String(1e10, format = \"10.3e\");\nend M;\n", &uns);
  YMC_CHECK(uns);   // omc 打 "1.000e+010"（宽度 10 内含三位指数），补零后宽度要重算，未做
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
}

YMC_TEST(flatten_delay_builtin) {
  // delay（NFModelicaBuiltin delay2 → delay3(expr, d, d)；simplifyDelay：delayTime ≤ parameter 且为 0 → expr）。
  // 期望 = omc 1.25.0 实跑 D1（2026-10-02）。
  const char* src =
      "model D1\n  parameter Real T = 0.5;\n  Real x = sin(time);\n  Real y1 = delay(x, 0.1);\n  Real y2 = delay(x, T);\n"
      "  Real y3 = delay(x, T, 1.0);\n  Real y4 = delay(x, 0);\n  Real y5 = delay(x, time / 10, 1.0);\nend D1;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class D1\n  parameter Real T = 0.5;\n  Real x = sin(time);\n  Real y1 = delay(x, 0.1, 0.1);\n"
                           "  Real y2 = delay(x, T, T);\n  Real y3 = delay(x, T, 1.0);\n  Real y4 = x;\n"
                           "  Real y5 = delay(x, time / 10.0, 1.0);\nend D1;\n"));
}

YMC_TEST(flatten_record_cref_binding) {
  // 记录 cref 整体绑定 → 逐字段 cref（splitRecordCref，NFFlatten.mo:1027-1053）；常量源折叠；嵌套记录 / 记录数组元素源。
  // 整体绑定 + 字段修饰：字段绑定一律取整体绑定（含构造缺省实参），字段属性照常合并。期望 = omc 1.25.0 实跑 RB2 / RM1 / RM4
  //（2026-10-02）。
  const char* rb =
      "record R\n  Real x;\n  Real y;\nend R;\nrecord R2\n  R r;\n  Real z;\nend R2;\n"
      "model RB2\n  parameter R r1 = R(1, 2);\n  parameter R r2 = r1;\n  constant R c1 = R(3, 4);\n  R v1 = c1;\n"
      "  R v3(x = time, y = 2*time);\n  R v4 = v3;\n  final parameter R r4 = r1;\n  parameter R2 q1(r = r1, z = 3);\n"
      "  parameter R2 q2 = q1;\n  R ra[2](each x = 1, y = {2, 3});\n  R v6 = ra[2];\nend RB2;\n";
  YMC_CHECK_EQ(flatten_src(rb),
               std::string("class RB2\n  parameter Real r1.x = 1.0;\n  parameter Real r1.y = 2.0;\n"
                           "  parameter Real r2.x = r1.x;\n  parameter Real r2.y = r1.y;\n  constant Real c1.x = 3.0;\n"
                           "  constant Real c1.y = 4.0;\n  Real v1.x = 3.0;\n  Real v1.y = 4.0;\n  Real v3.x = time;\n"
                           "  Real v3.y = 2.0 * time;\n  Real v4.x = v3.x;\n  Real v4.y = v3.y;\n"
                           "  final parameter Real r4.x = r1.x;\n  final parameter Real r4.y = r1.y;\n"
                           "  parameter Real q1.r.x = r1.x;\n  parameter Real q1.r.y = r1.y;\n  parameter Real q1.z = 3.0;\n"
                           "  parameter Real q2.r.x = q1.r.x;\n  parameter Real q2.r.y = q1.r.y;\n  parameter Real q2.z = q1.z;\n"
                           "  Real ra[1].x = 1.0;\n  Real ra[1].y = 2.0;\n  Real ra[2].x = 1.0;\n  Real ra[2].y = 3.0;\n"
                           "  Real v6.x = ra[2].x;\n  Real v6.y = ra[2].y;\nend RB2;\n"));
  const char* rec = "record R\n  Real x = 1;\n  Real y = 2;\nend R;\n";
  YMC_CHECK_EQ(flatten_src((std::string(rec) +
                            "model RM1\n  R a;\n  R b(x(start = 3)) = a;\n  R d(y(min = 0)) = R(4, 5);\n"
                            "  R e(x(start = 3), y(fixed = true)) = R(y = 6, x = 7);\n  R c(x = 5) = a;\nend RM1;\n").c_str()),
               std::string("class RM1\n  Real a.x = 1.0;\n  Real a.y = 2.0;\n  Real b.x(start = 3.0) = a.x;\n  Real b.y = a.y;\n"
                           "  Real d.x = 4.0;\n  Real d.y(min = 0.0) = 5.0;\n  Real e.x(start = 3.0) = 7.0;\n"
                           "  Real e.y(fixed = true) = 6.0;\n  Real c.x = a.x;\n  Real c.y = a.y;\nend RM1;\n"));
  YMC_CHECK_EQ(flatten_src((std::string(rec) +
                            "model RM4\n  R d(y = 3) = R(x = 4);\n  R e(y(start = 5)) = R(x = 4);\n  parameter Real p = 7;\n"
                            "  R f(y = p) = R(x = 4);\nend RM4;\n").c_str()),
               std::string("class RM4\n  Real d.x = 4.0;\n  Real d.y = 2.0;\n  Real e.x = 4.0;\n  Real e.y(start = 5.0) = 2.0;\n"
                           "  parameter Real p = 7.0;\n  Real f.x = 4.0;\n  Real f.y = 2.0;\nend RM4;\n"));
}

YMC_TEST(flatten_record_equation_type_match) {
  // 不同记录类的等式：同名同序 = PLUG_COMPATIBLE 原样（RO5）；字段错位要 typeCastRecord 成 `R1(r2.x, r2.y)`（RO4，
  // 记录构造表达式未做）→ SKIP。期望 = omc 1.25.0 实跑 RO4 / RO5（2026-10-02，matchComplexTypes NFTypeCheck.mo:1835-2004）。
  const char* recs = "record R1\n  Real x;\n  Real y;\nend R1;\nrecord R2\n  Real y;\n  Real x;\nend R2;\n"
                     "record R3\n  Real x;\n  Real y;\nend R3;\n";
  YMC_CHECK_EQ(flatten_src((std::string(recs) + "model RO5\n  R1 r1;\n  R3 r3 = r1;\n  R3 r4;\nequation\n  r1 = r4;\nend RO5;\n").c_str()),
               std::string("function R1 \"Automatically generated record constructor for R1\"\n  input Real x;\n  input Real y;\n"
                           "  output R1 res;\nend R1;\n\nfunction R3 \"Automatically generated record constructor for R3\"\n"
                           "  input Real x;\n  input Real y;\n  output R3 res;\nend R3;\n\nclass RO5\n  Real r1.x;\n  Real r1.y;\n"
                           "  Real r3.x = r1.x;\n  Real r3.y = r1.y;\n  Real r4.x;\n  Real r4.y;\nequation\n  r1 = r4;\nend RO5;\n"));
  bool uns = false;
  const std::string out =
      flatten_src((std::string(recs) + "model RO4\n  R1 r1;\n  R2 r2;\nequation\n  r1 = r2;\nend RO4;\n").c_str(), &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:record equation type cast>"));
}

YMC_TEST(flatten_composite_array_modifiers) {
  // 复合数组的外来修饰逐元素切分（Modifier.propagate，NFModifier.mo:444-513）；复合数组元素里 Var 数组绑定移成方程时左侧带
  // 逐段下标（曾错出 `a.x = …`）。期望 = omc 1.25.0 实跑 EM1 / CA1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model A\n  Real x;\n  Real y = 1;\nend A;\nmodel B\n  A a[2];\nend B;\n"
                           "model EM1\n  B b[2](a.x = {{1, 2}, {3, 4}}, a(each y = 5));\n  B c(a(x = {6, 7}));\nend EM1;\n"),
               std::string("class EM1\n  Real b[1].a[1].x = 1.0;\n  Real b[1].a[1].y = 5.0;\n  Real b[1].a[2].x = 2.0;\n"
                           "  Real b[1].a[2].y = 5.0;\n  Real b[2].a[1].x = 3.0;\n  Real b[2].a[1].y = 5.0;\n"
                           "  Real b[2].a[2].x = 4.0;\n  Real b[2].a[2].y = 5.0;\n  Real c.a[1].x = 6.0;\n  Real c.a[1].y = 1.0;\n"
                           "  Real c.a[2].x = 7.0;\n  Real c.a[2].y = 1.0;\nend EM1;\n"));
  YMC_CHECK_EQ(flatten_src("model A\n  Real x[2];\nend A;\nmodel CA1\n  A a[2](x = {{1, 2}, {3, 4}});\nend CA1;\n"),
               std::string("class CA1\n  Real a[1].x[1];\n  Real a[1].x[2];\n  Real a[2].x[1];\n  Real a[2].x[2];\nequation\n"
                           "  a[1].x = {1.0, 2.0};\n  a[2].x = {3.0, 4.0};\nend CA1;\n"));
}

YMC_TEST(flatten_record_array_whole_binding) {
  // 记录数组整体绑定逐元素取第 i 个元素再拆字段（nthRecordElement + splitRecordCref，NFFlatten.mo:902-905 / :1038-1059）；
  // `:` 维按绑定字面量推；字段属性修饰合并。构造函数须解析到同一记录类（同名异包 → SKIP）。期望 = omc 1.25.0 实跑
  // CW1 / CI2（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("record R\n  Real x;\n  Real y = 9;\nend R;\n"
                           "model CW1\n  parameter R r1 = R(1, 2);\n  parameter R rs[2] = {R(3, 4), r1};\n  parameter R rt[2] = rs;\n"
                           "  parameter R ru[:] = {r1, R(5), rs[1]};\n  R rv[2](each x(start = 1)) = {r1, r1};\n"
                           "  R rw[2](x = {7, 8}) = rs;\nend CW1;\n"),
               std::string("class CW1\n  parameter Real r1.x = 1.0;\n  parameter Real r1.y = 2.0;\n  parameter Real rs[1].x = 3.0;\n"
                           "  parameter Real rs[1].y = 4.0;\n  parameter Real rs[2].x = r1.x;\n  parameter Real rs[2].y = r1.y;\n"
                           "  parameter Real rt[1].x = rs[1].x;\n  parameter Real rt[1].y = rs[1].y;\n"
                           "  parameter Real rt[2].x = rs[2].x;\n  parameter Real rt[2].y = rs[2].y;\n"
                           "  parameter Real ru[1].x = r1.x;\n  parameter Real ru[1].y = r1.y;\n  parameter Real ru[2].x = 5.0;\n"
                           "  parameter Real ru[2].y = 9.0;\n  parameter Real ru[3].x = rs[1].x;\n  parameter Real ru[3].y = rs[1].y;\n"
                           "  Real rv[1].x(start = 1.0) = r1.x;\n  Real rv[1].y = r1.y;\n  Real rv[2].x(start = 1.0) = r1.x;\n"
                           "  Real rv[2].y = r1.y;\n  Real rw[1].x = rs[1].x;\n  Real rw[1].y = rs[1].y;\n  Real rw[2].x = rs[2].x;\n"
                           "  Real rw[2].y = rs[2].y;\nend CW1;\n"));
  bool uns = false;
  const std::string out = flatten_src(
      "package P1\n  record S\n    Real a = 1;\n    Real b = 2;\n  end S;\nend P1;\npackage P2\n  record S\n    Real b = 3;\n"
      "    Real a = 4;\n  end S;\nend P2;\nmodel CI2\n  P1.S s = P2.S(a = time);\nend CI2;\n",
      &uns);
  YMC_CHECK(uns);   // omc：s.a = time、s.b = 3.0（按 P2.S 缺省构造再转型）；只比末段名会错出 s.b = 2.0
  YMC_CHECK_EQ(out, std::string("<unsupported:record constructor without instance>"));   // P2.S 无实例类节点
}

YMC_TEST(flatten_composite_array_field_expansion) {
  // 复合数组跨元素取字段 `r.x` / 前几维带下标 `a[1].x`：ExpandExp 展开成元素数组 `{r[1].x, r[2].x}`；参数下标成结构参数
  //（final）。期望 = omc 1.25.0 实跑 XR1 / XR2（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("record R\n  Real x;\n  Real y;\nend R;\nmodel XR1\n  R r[2](x = {1, 2}, y = {3, 4});\n  Real a[2] = r.x;\n"
                           "  parameter R p[2](x = {1, 2}, y = {3, 4});\n  parameter Real b[2] = p.x;\n  Real c = sum(r.y);\n"
                           "  Real d[2];\nequation\n  d = r.y;\nend XR1;\n"),
               std::string("class XR1\n  Real r[1].x = 1.0;\n  Real r[1].y = 3.0;\n  Real r[2].x = 2.0;\n  Real r[2].y = 4.0;\n"
                           "  Real a[1];\n  Real a[2];\n  parameter Real p[1].x = 1.0;\n  parameter Real p[1].y = 3.0;\n"
                           "  parameter Real p[2].x = 2.0;\n  parameter Real p[2].y = 4.0;\n  parameter Real b[1] = p[1].x;\n"
                           "  parameter Real b[2] = p[2].x;\n  Real c = r[1].y + r[2].y;\n  Real d[1];\n  Real d[2];\nequation\n"
                           "  a = {r[1].x, r[2].x};\n  d[1] = r[1].y;\n  d[2] = r[2].y;\nend XR1;\n"));
  YMC_CHECK_EQ(flatten_src("model A\n  Real x;\nend A;\nmodel XR2\n  A a[3, 2](x = {{1, 2}, {3, 4}, {5, 6}});\n  Real y[2] = a[1].x;\n"
                           "  Real z[3, 2] = a.x;\n  parameter Integer k = 2;\n  Real w[2] = a[k].x;\nend XR2;\n"),
               std::string("class XR2\n  Real a[1,1].x = 1.0;\n  Real a[1,2].x = 2.0;\n  Real a[2,1].x = 3.0;\n  Real a[2,2].x = 4.0;\n"
                           "  Real a[3,1].x = 5.0;\n  Real a[3,2].x = 6.0;\n  Real y[1];\n  Real y[2];\n  Real z[1,1];\n  Real z[1,2];\n"
                           "  Real z[2,1];\n  Real z[2,2];\n  Real z[3,1];\n  Real z[3,2];\n  final parameter Integer k = 2;\n"
                           "  Real w[1];\n  Real w[2];\nequation\n  y = {a[1,1].x, a[1,2].x};\n"
                           "  z = {{a[1,1].x, a[1,2].x}, {a[2,1].x, a[2,2].x}, {a[3,1].x, a[3,2].x}};\n"
                           "  w = {a[2,1].x, a[2,2].x};\nend XR2;\n"));
}

YMC_TEST(flatten_if_equation_structural_condition) {
  // if 方程的参数条件（此前各支都已折掉）= 结构条件：参数标 final、引用处代入值、按值选支（typeIfEquation NFTyping.mo:
  // 3581-3590 + flattenIfEquation NFFlatten.mo:1975-2010）；前面有运行时分支的参数条件原样保留。期望 = omc 1.25.0 实跑
  // IF1 / IF2（2026-10-02）、IF3（2026-10-06）。
  YMC_CHECK_EQ(flatten_src("model IF1\n  parameter Boolean b = true;\n  parameter Integer n = 2;\n  parameter Real p = 1.5;\n"
                           "  parameter Real q = 3;\n  Real x, y, w, v;\n  Real u = if b then 1 else 2;\nequation\n  if b then\n"
                           "    x = 1;\n  else\n    x = 2;\n  end if;\n  if n > 3 then\n    y = 1;\n  elseif n == 2 then\n    y = 2;\n"
                           "  else\n    y = 3;\n  end if;\n  if p > 1 then\n    w = p;\n  else\n    w = 0;\n  end if;\n  if q < 1 then\n"
                           "    v = 1;\n  end if;\nend IF1;\n"),
               std::string("class IF1\n  final parameter Boolean b = true;\n  final parameter Integer n = 2;\n"
                           "  final parameter Real p = 1.5;\n  final parameter Real q = 3.0;\n  Real x;\n  Real y;\n  Real w;\n"
                           "  Real v;\n  Real u = 1.0;\nequation\n  x = 1.0;\n  y = 2.0;\n  w = 1.5;\nend IF1;\n"));
  YMC_CHECK_EQ(flatten_src("model IF2\n  parameter Boolean b = true;\n  Real z;\nequation\n  if time > 1 then\n    z = 1;\n"
                           "  elseif b then\n    z = 2;\n  else\n    z = 3;\n  end if;\nend IF2;\n"),
               std::string("class IF2\n  parameter Boolean b = true;\n  Real z;\nequation\n  if time > 1.0 then\n    z = 1.0;\n"
                           "  elseif b then\n    z = 2.0;\n  else\n    z = 3.0;\n  end if;\nend IF2;\n"));
  // 条件参数的绑定不是字面量（`b = c > 1`）：固定检查顺绑定走（c 固定）→ 结构条件；b final、c 不 final（方程段的标记不连带
  // 绑定里的参数，markComponent 只进未定型的绑定）。
  YMC_CHECK_EQ(flatten_src("model IF3\n  parameter Real c = 2;\n  parameter Boolean b = c > 1;\n  Real x;\nequation\n"
                           "  if b then\n    x = 1;\n  else\n    x = 2;\n  end if;\nend IF3;\n"),
               std::string("class IF3\n  parameter Real c = 2.0;\n  final parameter Boolean b = true;\n  Real x;\nequation\n"
                           "  x = 1.0;\nend IF3;\n"));
}

YMC_TEST(flatten_subscripted_and_runtime_subscripts) {
  // `(e)[i]`：字面量数组 + 字面量下标折元素，运行时下标保留 ASUB `{1.0, 2.0, 3.0}[n]`（转型推进元素）；cref 的运行时整型
  // 下标保留符号（`m[1,n]`、`x[n + 1]`）；数组元素逐个带声明注释。期望 = omc 1.25.0 实跑 SE3（2026-10-02，stdout）。
  YMC_CHECK_EQ(flatten_src("model SE3\n  discrete Integer n(start = 1);\n  Real x[3] = {1, 2, 3} \"c x\";\n"
                           "  parameter Real p[2] = {4, 5} \"c p\";\n  Real y = ({1, 2, 3})[n];\n  Real z = ({1, 2, 3})[2];\n"
                           "  Real m[2, 3];\n  Real u = m[1, n];\n  Real v = x[n + 1];\nequation\n  m = {{1, 2, 3}, {4, 5, 6}};\n"
                           "  when time > 1 then\n    n = 2;\n  end when;\nend SE3;\n"),
               std::string("class SE3\n  discrete Integer n(start = 1);\n  Real x[1] \"c x\";\n  Real x[2] \"c x\";\n"
                           "  Real x[3] \"c x\";\n  parameter Real p[1] = 4.0 \"c p\";\n  parameter Real p[2] = 5.0 \"c p\";\n"
                           "  Real y = {1.0, 2.0, 3.0}[n];\n  Real z = 2.0;\n  Real m[1,1];\n  Real m[1,2];\n  Real m[1,3];\n"
                           "  Real m[2,1];\n  Real m[2,2];\n  Real m[2,3];\n  Real u = m[1,n];\n  Real v = x[n + 1];\nequation\n"
                           "  x = {1.0, 2.0, 3.0};\n  m[1,1] = 1.0;\n  m[1,2] = 2.0;\n  m[1,3] = 3.0;\n  m[2,1] = 4.0;\n"
                           "  m[2,2] = 5.0;\n  m[2,3] = 6.0;\n  when time > 1.0 then\n    n = 2;\n  end when;\nend SE3;\n"));
}

YMC_TEST(flatten_constant_arrays_and_colon_refs) {
  // 整体 constant 数组引用内联成数组字面量再折叠（绑定 / sum / 标量积 / 数组方程逐元素）；`:` 维数组被引用时取已发射元素的
  // 形状（`(x)[n]`、`2 * x`）。期望 = omc 1.25.0 实跑 CE3 / CD1（2026-10-02，stdout）。
  YMC_CHECK_EQ(flatten_src("model CE3\n  constant Integer a[3] = {4, 8, 12};\n  constant Real m[2, 2] = {{1, 2}, {3, 4}};\n"
                           "  Real y[3] = a;\n  Real s = sum(a);\n  Real z[2, 2];\n  Real w[3];\n  Real v = a * {1, 2, 3};\n"
                           "  parameter Real p[3] = a;\nequation\n  z = m;\n  w = 2 * a;\nend CE3;\n"),
               std::string("class CE3\n  constant Integer a[1] = 4;\n  constant Integer a[2] = 8;\n  constant Integer a[3] = 12;\n"
                           "  constant Real m[1,1] = 1.0;\n  constant Real m[1,2] = 2.0;\n  constant Real m[2,1] = 3.0;\n"
                           "  constant Real m[2,2] = 4.0;\n  Real y[1];\n  Real y[2];\n  Real y[3];\n  Real s = 24.0;\n"
                           "  Real z[1,1];\n  Real z[1,2];\n  Real z[2,1];\n  Real z[2,2];\n  Real w[1];\n  Real w[2];\n  Real w[3];\n"
                           "  Real v = 56.0;\n  parameter Real p[1] = 4.0;\n  parameter Real p[2] = 8.0;\n  parameter Real p[3] = 12.0;\n"
                           "equation\n  y = {4.0, 8.0, 12.0};\n  z[1,1] = 1.0;\n  z[1,2] = 2.0;\n  z[2,1] = 3.0;\n  z[2,2] = 4.0;\n"
                           "  w[1] = 8.0;\n  w[2] = 16.0;\n  w[3] = 24.0;\nend CE3;\n"));
  YMC_CHECK_EQ(flatten_src("model CD1\n  Real x[:] = {1, 2, 3};\n  discrete Integer n(start = 1);\n  Real z = (x)[n];\n"
                           "  Real w[3] = 2 * x;\nequation\n  when time > 1 then\n    n = 2;\n  end when;\nend CD1;\n"),
               std::string("class CD1\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  discrete Integer n(start = 1);\n  Real z = x[n];\n"
                           "  Real w[1];\n  Real w[2];\n  Real w[3];\nequation\n  x = {1.0, 2.0, 3.0};\n"
                           "  w = {2.0 * x[1], 2.0 * x[2], 2.0 * x[3]};\n  when time > 1.0 then\n    n = 2;\n  end when;\n"
                           "end CD1;\n"));
}

YMC_TEST(flatten_cat_and_expression_leaf_bindings) {
  // cat(k, …) 展开成数组字面量；数组字面量绑定的表达式叶（`{time, 2, 3}`、参数 `{k, 3}`）定型后核类型。期望 = omc 1.25.0
  // 实跑 CT3（2026-10-02）；Boolean 叶进 Real 数组 omc 报 "Array types mismatch"（BL1）→ SKIP。
  YMC_CHECK_EQ(flatten_src("model CT3\n  Real x[3] = {time, 2, 3};\n  Real b[5] = cat(1, x, {7, 8});\n  parameter Real k = 2;\n"
                           "  parameter Real p[2] = {k, 3};\n  Real m[2, 3] = cat(2, {{1}, {2}}, {{3, 4}, {5, 6}});\n"
                           "  Integer i[4] = cat(1, {1, 2}, {3, 4});\nend CT3;\n"),
               std::string("class CT3\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real b[1];\n  Real b[2];\n  Real b[3];\n  Real b[4];\n"
                           "  Real b[5];\n  parameter Real k = 2.0;\n  parameter Real p[1] = k;\n  parameter Real p[2] = 3.0;\n"
                           "  Real m[1,1];\n  Real m[1,2];\n  Real m[1,3];\n  Real m[2,1];\n  Real m[2,2];\n  Real m[2,3];\n"
                           "  Integer i[1];\n  Integer i[2];\n  Integer i[3];\n  Integer i[4];\nequation\n  x = {time, 2.0, 3.0};\n"
                           "  b = {x[1], x[2], x[3], 7.0, 8.0};\n  m = {{1.0, 3.0, 4.0}, {2.0, 5.0, 6.0}};\n  i = {1, 2, 3, 4};\n"
                           "end CT3;\n"));
  bool uns = false;
  const std::string out =
      flatten_src("model BL1\n  Boolean b = time > 1;\n  Real x[2] = {time, b};\nend BL1;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:array binding shape/type mismatch>"));
}

YMC_TEST(flatten_range_bindings) {
  // 字面量范围绑定：长度按 getRangeTypeInt / Real（NFTypeCheck.mo:2758-2833）；Var 数组整体保留 `x = 1:5`（Real 数组起 / 步 /
  // 止转 Real），参数逐元素 = 起 + (i - 1)·步。期望 = omc 1.25.0 实跑 RG1 / RG2（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model RG1\n  Integer x[5] = 1:5;\n  Real r[3] = 0.5:0.5:1.5;\n  parameter Integer p[3] = 2:4;\n"
                           "  Real v[4] = 1.0 : 2 : 8;\n  Integer w[3] = 3 : -1 : 1;\nend RG1;\n"),
               std::string("class RG1\n  Integer x[1];\n  Integer x[2];\n  Integer x[3];\n  Integer x[4];\n  Integer x[5];\n"
                           "  Real r[1];\n  Real r[2];\n  Real r[3];\n  parameter Integer p[1] = 2;\n  parameter Integer p[2] = 3;\n"
                           "  parameter Integer p[3] = 4;\n  Real v[1];\n  Real v[2];\n  Real v[3];\n  Real v[4];\n  Integer w[1];\n"
                           "  Integer w[2];\n  Integer w[3];\nequation\n  x = 1:5;\n  r = 0.5:0.5:1.5;\n  v = 1.0:2.0:8.0;\n"
                           "  w = 3:-1:1;\nend RG1;\n"));
  YMC_CHECK_EQ(flatten_src("model RG2\n  Real r[3] = 1:3;\n  Real s[4] = 1.0:2:8;\n  parameter Real q[3] = 1:3;\nend RG2;\n"),
               std::string("class RG2\n  Real r[1];\n  Real r[2];\n  Real r[3];\n  Real s[1];\n  Real s[2];\n  Real s[3];\n"
                           "  Real s[4];\n  parameter Real q[1] = 1.0;\n  parameter Real q[2] = 2.0;\n  parameter Real q[3] = 3.0;\n"
                           "equation\n  r = 1.0:3.0;\n  s = 1.0:2.0:8.0;\nend RG2;\n"));
}

YMC_TEST(flatten_qualified_package_constant_paths) {
  // 限定名常量路径沿路径取值：包常量数组整体 / 下标、记录常量字段（修饰 / 构造修饰 / 字段缺省）、记录常量数组（each 原样、
  // 非 each 逐元素切分、无修饰取字段缺省）。期望 = omc 1.25.0 实跑 QP1（2026-10-02）。
  const char* src =
      "package P\n  record R1\n    Real r = 5;\n  end R1;\n  record R2\n    R1 r1;\n    Real z = 3;\n  end R2;\n"
      "  constant Real x[2] = {1, 2};\n  constant R1 a(r = 2.0);\n  constant R2 b(r1 = R1(r = 4.0));\n"
      "  constant R2 c[2](each r1 = R1(r = 6.0), z = {7, 8});\n  constant R1 d[3];\nend P;\n"
      "model QP1\n  Real y[2] = P.x;\n  Real u = P.x[2];\n  constant Real v1 = P.a.r;\n  Real v2 = P.b.r1.r;\n"
      "  Real v3 = P.c[2].z + P.c[1].r1.r;\n  Real v4 = P.d[3].r;\n  Real v5 = P.b.z;\nend QP1;\n";
  YMC_CHECK_EQ(flatten_src(src),
               std::string("class QP1\n  Real y[1];\n  Real y[2];\n  Real u = 2.0;\n  constant Real v1 = 2.0;\n  Real v2 = 4.0;\n"
                           "  Real v3 = 14.0;\n  Real v4 = 5.0;\n  Real v5 = 3.0;\nequation\n  y = {1.0, 2.0};\nend QP1;\n"));
}

YMC_TEST(flatten_implicit_range_partial_subscripts) {
  // 隐式 for 范围（deduceIterationRange：迭代变量作下标处的维长）；下标少于维数 = 隐式尾随 `:`（打印仍只带给出的下标，
  // 1.25.0）；fixed = false 参数的 if 条件原样保留。期望 = omc 1.25.0 实跑 IR1 / PS1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model IR1\n  Real x[3, 2];\n  Real y[3];\n  parameter Boolean b(fixed = false);\n  Real z;\n"
                           "initial equation\n  b = true;\nequation\n  for i loop\n    x[i] = {y[i], 2 * y[i]};\n    y[i] = i;\n"
                           "  end for;\n  if b then\n    z = 1;\n  else\n    z = 2;\n  end if;\nend IR1;\n"),
               std::string("class IR1\n  Real x[1,1];\n  Real x[1,2];\n  Real x[2,1];\n  Real x[2,2];\n  Real x[3,1];\n  Real x[3,2];\n"
                           "  Real y[1];\n  Real y[2];\n  Real y[3];\n  parameter Boolean b(fixed = false);\n  Real z;\n"
                           "initial equation\n  b = true;\nequation\n  x[1,1] = y[1];\n  x[1,2] = 2.0 * y[1];\n  y[1] = 1.0;\n"
                           "  x[2,1] = y[2];\n  x[2,2] = 2.0 * y[2];\n  y[2] = 2.0;\n  x[3,1] = y[3];\n  x[3,2] = 2.0 * y[3];\n"
                           "  y[3] = 3.0;\n  if b then\n    z = 1.0;\n  else\n    z = 2.0;\n  end if;\nend IR1;\n"));
  YMC_CHECK_EQ(flatten_src("model PS1\n  Real x[2, 3];\n  Real y[3] = x[2];\n  Real s = sum(x[1]);\nequation\n"
                           "  x[1] = {1, 2, 3};\n  x[2] = {time, 2 * time, 3};\nend PS1;\n"),
               std::string("class PS1\n  Real x[1,1];\n  Real x[1,2];\n  Real x[1,3];\n  Real x[2,1];\n  Real x[2,2];\n  Real x[2,3];\n"
                           "  Real y[1];\n  Real y[2];\n  Real y[3];\n  Real s = x[1,1] + x[1,2] + x[1,3];\nequation\n  y = x[2];\n"
                           "  x[1,1] = 1.0;\n  x[1,2] = 2.0;\n  x[1,3] = 3.0;\n  x[2,1] = time;\n  x[2,2] = 2.0 * time;\n"
                           "  x[2,3] = 3.0;\nend PS1;\n"));
}

YMC_TEST(flatten_record_ctor_calls_and_equation_bindings) {
  // 记录构造调用打成构造函数调用、缺省补齐（RC1）；拆不开的记录绑定（运行时 if）挪成方程、字段无绑定；参数条件的 if 记录
  // 绑定选支并标结构参数（RE1）。期望 = omc 1.25.0 实跑 RC1 / RE1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("record R\n  Real x;\n  Real y = 2;\n  Integer n = 3;\nend R;\nmodel RC1\n  R v1;\n  R v2;\n  R v3;\n"
                           "  parameter Real p = 5;\nequation\n  v1 = R(time, 2*time, 1);\n  v2 = R(x = time);\n  v3 = R(p, n = 4);\n"
                           "end RC1;\n"),
               std::string("function R \"Automatically generated record constructor for R\"\n  input Real x;\n  input Real y = 2.0;\n"
                           "  input Integer n = 3;\n  output R res;\nend R;\n\nclass RC1\n  Real v1.x;\n  Real v1.y = 2.0;\n"
                           "  Integer v1.n = 3;\n  Real v2.x;\n  Real v2.y = 2.0;\n  Integer v2.n = 3;\n  Real v3.x;\n  Real v3.y = 2.0;\n"
                           "  Integer v3.n = 3;\n  parameter Real p = 5.0;\nequation\n  v1 = R(time, 2.0 * time, 1);\n"
                           "  v2 = R(time, 2.0, 3);\n  v3 = R(p, 2.0, 4);\nend RC1;\n"));
  YMC_CHECK_EQ(flatten_src("record P\n  Real a;\n  Real b = 1;\nend P;\nmodel RE1\n  P p1;\n"
                           "  P p2 = if time > 1 then P(1, 2) else P(a = 3);\n  parameter Real k = 2;\n"
                           "  parameter P p3 = if k > 3 then P(1, 2) else P(a = k);\nequation\n  p1 = P(time, 2 * time);\nend RE1;\n"),
               std::string("function P \"Automatically generated record constructor for P\"\n  input Real a;\n  input Real b = 1.0;\n"
                           "  output P res;\nend P;\n\nclass RE1\n  Real p1.a;\n  Real p1.b = 1.0;\n  Real p2.a;\n  Real p2.b;\n"
                           "  final parameter Real k = 2.0;\n  parameter Real p3.a = 2.0;\n  parameter Real p3.b = 1.0;\nequation\n"
                           "  p2 = if time > 1.0 then P(1.0, 2.0) else P(3.0, 1.0);\n  p1 = P(time, 2.0 * time);\nend RE1;\n"));
}

YMC_TEST(flatten_record_valued_functions) {
  // 返回记录的用户函数：函数体里整记录赋值 `r := R(…)`（RF1）/ 字段赋值 `r.a := x`（RF2）；模型里 `R r1 = f(time)` 挪成方程、
  // `parameter R r2 = f(p)` 挪成初始方程 + 字段 fixed = false。期望 = omc 1.25.0 实跑 RF1 / RF2（2026-10-02）。
  const char* head = "record R\n  Real a;\n  Real b;\nend R;\nfunction f\n  input Real x;\n  output R r;\nalgorithm\n";
  const char* model = "end f;\nmodel M\n  R r1 = f(time);\n  parameter Real p = 2;\n  parameter R r2 = f(p);\n  Real y = r1.a;\nend M;\n";
  const char* ctor = "function R \"Automatically generated record constructor for R\"\n  input Real a;\n  input Real b;\n"
                     "  output R res;\nend R;\n\nfunction f\n  input Real x;\n  output R r;\nalgorithm\n";
  const char* cls = "end f;\n\nclass M\n  Real r1.a;\n  Real r1.b;\n  parameter Real p = 2.0;\n  parameter Real r2.a(fixed = false);\n"
                    "  parameter Real r2.b(fixed = false);\n  Real y = r1.a;\ninitial equation\n  r2 = f(p);\nequation\n"
                    "  r1 = f(time);\nend M;\n";
  YMC_CHECK_EQ(flatten_src((std::string(head) + "  r := R(x, 2*x);\n" + model).c_str()),
               std::string(ctor) + "  r := R(x, 2.0 * x);\n" + cls);
  YMC_CHECK_EQ(flatten_src((std::string(head) + "  r.a := x;\n  r.b := 2*x;\n" + model).c_str()),
               std::string(ctor) + "  r.a := x;\n  r.b := 2.0 * x;\n" + cls);
}

YMC_TEST(flatten_record_final_fields_and_variability_floor) {
  // 记录构造的输入 = 非 protected、非 final 字段（NFRecord.collectRecordParam）；final 字段保留自身绑定，引用兄弟字段时按
  // 记录变率下沉后的变率判 / 求值（`constant R` 里的 `Real x` 是 constant）。期望 = omc 1.25.0 实跑 FC1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("record R\n  Real x;\n  final constant Real y = x / 2.0;\n  Real z;\nend R;\nmodel FC1\n"
                           "  constant R r = R(1.0, 3.0);\n  constant R q(x = 4.0, z = 5.0);\n  constant Real w = r.y + q.y;\nend FC1;\n"),
               std::string("class FC1\n  constant Real r.x = 1.0;\n  final constant Real r.y = 0.5;\n  constant Real r.z = 3.0;\n"
                           "  constant Real q.x = 4.0;\n  final constant Real q.y = 2.0;\n  constant Real q.z = 5.0;\n"
                           "  constant Real w = 2.5;\nend FC1;\n"));
}

YMC_TEST(flatten_builtin_named_arguments) {
  // homotopy / semiLinear 的命名实参按形参序重排；homotopy 实参提 Real。期望 = omc 1.25.0 实跑 NA2（2026-10-02）；smooth 的命名
  // 实参 1.25.0 报错（NA1）→ SKIP。
  YMC_CHECK_EQ(flatten_src("model NA2\n  Real x = time;\n  Real a = semiLinear(x, negativeSlope = 2, positiveSlope = 3);\n"
                           "  Real c = homotopy(simplified = 1, actual = x);\nend NA2;\n"),
               std::string("class NA2\n  Real x = time;\n  Real a = semiLinear(x, 3.0, 2.0);\n  Real c = homotopy(x, 1.0);\n"
                           "end NA2;\n"));
  bool uns = false;
  const std::string out = flatten_src("model NA1\n  Real x = time;\n  Real b = smooth(expr = x * x, p = 1);\nend NA1;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:named function argument>"));
}

YMC_TEST(flatten_array_comprehensions) {
  // 推导式保留成 `array(e for i in r)`（最右迭代变量在外层、参数范围求值、Integer 元素对 Real 整体转型 `/*Real[2, 3]*/`）；
  // 等式里整体保留、sum 里逐元素代入；smooth 的 Integer 实参插转型。期望 = omc 1.25.0 实跑 AC2 / AC3 / BT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model AC2\n  parameter Integer n = 3;\n  Real x[n] = {i for i in 1:n};\n  Real y[3] = {i * 2.0 for i in 1:3};\n"
                           "  Real z[3] = {time * i for i in 1:3};\n  Real w[2, 3] = {{i * j for j in 1:3} for i in 1:2};\n"
                           "  Real v[3] = {sin(i * time) for i in 1:3};\n  Real u[2, 3] = {i * j for j in 1:3, i in 1:2};\nend AC2;\n"),
               std::string("class AC2\n  final parameter Integer n = 3;\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y[1];\n  Real y[2];\n"
                           "  Real y[3];\n  Real z[1];\n  Real z[2];\n  Real z[3];\n  Real w[1,1];\n  Real w[1,2];\n  Real w[1,3];\n"
                           "  Real w[2,1];\n  Real w[2,2];\n  Real w[2,3];\n  Real v[1];\n  Real v[2];\n  Real v[3];\n  Real u[1,1];\n"
                           "  Real u[1,2];\n  Real u[1,3];\n  Real u[2,1];\n  Real u[2,2];\n  Real u[2,3];\nequation\n"
                           "  x = /*Real[3]*/(array(i for i in 1:3));\n  y = array(/*Real*/(i) * 2.0 for i in 1:3);\n"
                           "  z = array(time * /*Real*/(i) for i in 1:3);\n"
                           "  w = /*Real[2, 3]*/(array(array(i * j for j in 1:3) for i in 1:2));\n"
                           "  v = array(sin(/*Real*/(i) * time) for i in 1:3);\n"
                           "  u = /*Real[2, 3]*/(array(array(i * j for j in 1:3) for i in 1:2));\nend AC2;\n"));
  YMC_CHECK_EQ(flatten_src("model AC3\n  Real x[3];\n  Real y[3];\n  Real s;\nequation\n  x = {i * time for i in 1:3};\n"
                           "  y = {i for i in 1:3};\n  s = sum({x[i] * i for i in 1:3});\nend AC3;\n"),
               std::string("class AC3\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y[1];\n  Real y[2];\n  Real y[3];\n  Real s;\n"
                           "equation\n  x = array(/*Real*/(i) * time for i in 1:3);\n  y = /*Real[3]*/(array(i for i in 1:3));\n"
                           "  s = x[1] + x[2] * 2.0 + x[3] * 3.0;\nend AC3;\n"));
  YMC_CHECK_EQ(flatten_src("model BT1\n  discrete Integer i(start = 1);\n  Real a = smooth(0, i);\n  Integer b = sign(i);\n"
                           "  Real c = smooth(1, time);\n  Real d = max(i, time);\nequation\n  when time > 1 then\n    i = 2;\n"
                           "  end when;\nend BT1;\n"),
               std::string("class BT1\n  discrete Integer i(start = 1);\n  Real a = smooth(0, /*Real*/(i));\n"
                           "  Integer b = sign(/*Real*/(i));\n  Real c = smooth(1, time);\n  Real d = max(/*Real*/(i), time);\n"
                           "equation\n  when time > 1.0 then\n    i = 2;\n  end when;\nend BT1;\n"));
}

YMC_TEST(flatten_linspace_inlined_as_comprehension) {
  // linspace 早内联成 `array(x1 + (x2-x1)*(i-1)/(n-1) for i in 1:n)` 再化简；参数数组逐元素求值。期望 = omc 1.25.0 实跑 LS1
  //（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model LS1\n  parameter Integer n = 3;\n  Real l[4] = linspace(0, 1, 4);\n"
                           "  parameter Real q[3] = linspace(1, 2, 3);\n  Real v[n] = linspace(-1, time, n);\nequation\nend LS1;\n"),
               std::string("class LS1\n  final parameter Integer n = 3;\n  Real l[1];\n  Real l[2];\n  Real l[3];\n  Real l[4];\n"
                           "  parameter Real q[1] = 1.0;\n  parameter Real q[2] = 1.5;\n  parameter Real q[3] = 2.0;\n  Real v[1];\n"
                           "  Real v[2];\n  Real v[3];\nequation\n  l = array(/*Real*/(i - 1) / 3.0 for i in 1:4);\n"
                           "  v = array((time + 1.0) * /*Real*/(i - 1) / 2.0 - 1.0 for i in 1:3);\nend LS1;\n"));
}

YMC_TEST(flatten_empty_arrays) {
  // 维为 0 的变量不出行、空数组方程 / 赋值整条删去、引用换成 `{}`（下标引用 `{}[m - 1]`）、空内维的积 = 0、空 sum = 0。期望 =
  // omc 1.25.0 实跑 E1 / E2 / E12 与 EmptyArray2 原文（2026-10-02）；pre 的连续实参 omc 报错（E13）→ SKIP。
  YMC_CHECK_EQ(flatten_src("model E1\n  Real x[0];\n  Real y = sum(x);\n  Real z = x * x;\nend E1;\n"),
               std::string("class E1\n  Real y = 0.0;\n  Real z = 0.0;\nend E1;\n"));
  YMC_CHECK_EQ(flatten_src("model E2\n  Real x[0];\n  Real y[0] = x;\n  Real A[3, 0];\n  Real c[3] = A * x;\n  Real w[2, 0];\n"
                           "  Real v[2, 0];\nequation\n  w = v;\nend E2;\n"),
               std::string("class E2\n  Real c[1];\n  Real c[2];\n  Real c[3];\nequation\n  c = {0.0, 0.0, 0.0};\nend E2;\n"));
  YMC_CHECK_EQ(flatten_src("model E12\n  Real x[0];\n  Real y = time;\nalgorithm\n  x := fill(y, 0);\n  y := 2;\nend E12;\n"),
               std::string("class E12\n  Real y = time;\nalgorithm\n  y := 2.0;\nend E12;\n"));
  YMC_CHECK_EQ(flatten_src("model EmptyArray2\n  Real x[0];\n  Real y = time;\n  Real z;\nalgorithm\n  for m in 1:1 loop\n"
                           "    z := if m == 1 then y else x[m - 1];\n  end for;\nend EmptyArray2;\n"),
               std::string("class EmptyArray2\n  Real y = time;\n  Real z;\nalgorithm\n  for m in 1:1 loop\n"
                           "    z := if m == 1 then y else {}[m - 1];\n  end for;\nend EmptyArray2;\n"));
  bool uns = false;
  const std::string out = flatten_src("model E13\n  Real x[0];\n  Real y[0];\nequation\n  der(x) = y;\n  x = pre(y);\nend E13;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:array function argument>"));
}

YMC_TEST(flatten_vector_matrix_builtins) {
  // cross / skew 按 EarlyInline 体展开（符号实参照式保留、方程里逐元素化简）、ndims → 秩、matrix(v) → {{v1}, …}。期望 = omc
  // 1.25.0 实跑 AB2（2026-10-02）；非方阵 symmetric（omc 报类型错，FuncBuiltinSymmetric2）→ SKIP。
  YMC_CHECK_EQ(flatten_src("model AB2\n  Real x[3] = {time, 2*time, 3};\n  Real y[3] = {1, time, 2};\n  Real c[3] = cross(x, y);\n"
                           "  Real s[3,3] = skew(x);\n  Integer n = ndims(s);\n  Real mm[3,1] = matrix(x);\n  Real d[3];\nequation\n"
                           "  d = cross(y, {1, 2, 3});\nend AB2;\n"),
               std::string("class AB2\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y[1];\n  Real y[2];\n  Real y[3];\n  Real c[1];\n"
                           "  Real c[2];\n  Real c[3];\n  Real s[1,1];\n  Real s[1,2];\n  Real s[1,3];\n  Real s[2,1];\n  Real s[2,2];\n"
                           "  Real s[2,3];\n  Real s[3,1];\n  Real s[3,2];\n  Real s[3,3];\n  Integer n = 2;\n  Real mm[1,1];\n"
                           "  Real mm[2,1];\n  Real mm[3,1];\n  Real d[1];\n  Real d[2];\n  Real d[3];\nequation\n"
                           "  x = {time, 2.0 * time, 3.0};\n  y = {1.0, time, 2.0};\n"
                           "  c = {x[2] * y[3] - x[3] * y[2], x[3] * y[1] - x[1] * y[3], x[1] * y[2] - x[2] * y[1]};\n"
                           "  s = {{0.0, -x[3], x[2]}, {x[3], 0.0, -x[1]}, {-x[2], x[1], 0.0}};\n"
                           "  mm = {{x[1]}, {x[2]}, {x[3]}};\n  d[1] = y[2] * 3.0 - y[3] * 2.0;\n  d[2] = y[3] - y[1] * 3.0;\n"
                           "  d[3] = y[1] * 2.0 - y[2];\nend AB2;\n"));
  bool uns = false;
  const std::string out =
      flatten_src("model S2\n  Real x[:, :] = symmetric({{11, 12, 13}, {21, 22, 23}, {31, 32, 33}, {41, 42, 43}});\nend S2;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:symmetric argument shape>"));
}

YMC_TEST(flatten_comprehension_inside_array_expression) {
  // 推导式嵌在数组运算里：omc 1.25.0 整体保留并按数组算符打印（R1 探针 `x = array(/*Real*/(i) * time for i in 1:3) * 2.0`、
  // `y = -array(…)`；R2 探针 `s = array(…) * {1.0, 2.0, 3.0}`；62f 起走含数组调用的符号形态，期望 = omc 1.25.0 对原文实跑，
  // 2026-10-07）。参数数组的逐元素摊分与 sum 的实参照旧展开（期望 = R2 探针，2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model R1a\n  Real x[3];\nequation\n  x = 2 * {i * time for i in 1:3};\nend R1a;\n"),
               std::string("class R1a\n  Real x[1];\n  Real x[2];\n  Real x[3];\nequation\n"
                           "  x = array(/*Real*/(i) * time for i in 1:3) * 2.0;\nend R1a;\n"));
  YMC_CHECK_EQ(flatten_src("model R1b\n  Real y[3] = -{i * time for i in 1:3};\nend R1b;\n"),
               std::string("class R1b\n  Real y[1];\n  Real y[2];\n  Real y[3];\nequation\n  y = -array(/*Real*/(i) * time for i in 1:3);\n"
                           "end R1b;\n"));
  YMC_CHECK_EQ(flatten_src("model R2s\n  Real s = {i * time for i in 1:3} * {1, 2, 3};\nend R2s;\n"),
               std::string("class R2s\n  Real s = array(/*Real*/(i) * time for i in 1:3) * {1.0, 2.0, 3.0};\nend R2s;\n"));
  YMC_CHECK_EQ(flatten_src("model R2\n  parameter Real q[3] = 2 * linspace(1, 2, 3);\n  parameter Real r[3] = -{i for i in 1:3};\n"
                           "  parameter Real p[3] = {i * 1.5 for i in 1:3} + q;\n  Real t = sum(2 * {i * time for i in 1:2});\nend R2;\n"),
               std::string("class R2\n  parameter Real q[1] = 2.0;\n  parameter Real q[2] = 3.0;\n  parameter Real q[3] = 4.0;\n"
                           "  parameter Real r[1] = -1.0;\n  parameter Real r[2] = -2.0;\n  parameter Real r[3] = -3.0;\n"
                           "  parameter Real p[1] = 1.5 + q[1];\n  parameter Real p[2] = 3.0 + q[2];\n  parameter Real p[3] = 4.5 + q[3];\n"
                           "  Real t = 2.0 * time + 2.0 * 2.0 * time;\nend R2;\n"));
}

YMC_TEST(flatten_vectorized_calls) {
  // 向量化调用：`array(F(x[$i1], …) for $i1 in 1:n)`（迭代变量名每个调用从 $i1 起、多维嵌套末维在内），实参形态 = 整体 / 带
  // 下标 / `:` 切片的 cref 与书写的字面量；sum 的实参与参数数组摊分逐元素代入；作数组形参的实参原样；Integer 结果对 Real 数组
  // 整体转型；推导式的 `array(…)` 调用写法；属性的推导式值逐元素摊分。期望 = omc 1.25.0 实跑 VT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("function F\n  input Real a;\n  input Real b;\n  output Real c = a + b;\nend F;\nfunction G\n"
                           "  input Real a[3];\n  output Real c = a[1];\nend G;\nmodel VT1\n  parameter Real q[3] = {1, 2, 3};\n"
                           "  Real x[3] = {time, 1, 2};\n  Integer k[3] = {1, 2, 3};\n"
                           "  Real w[2, 3] = {{time, 1, 2}, {3, 4, 5}};\n  Real a[3] = F(x, 2);\n  Real c = sum(F(x, 1));\n"
                           "  parameter Real d[3] = F(q, 1);\n  Real e = G(F(x, 1));\n  Real g[3] = F({1, 2, time}, 2);\n"
                           "  Real n[2, 3] = F(w, 1);\n  Real p[3] = F(k, 1);\n  Real v[3] = F(w[1, :], 1);\n"
                           "  Real ac[3] = atan2(x, x);\n  Real ah[3] = abs(k);\n  Real aj[3] = sin(q);\n"
                           "  Real t[3](start = {i for i in 1:3});\n  Real u[3];\nequation\n"
                           "  u = array(F(x[i], i) for i in 1:3);\nend VT1;\n"),
               std::string("function F\n  input Real a;\n  input Real b;\n  output Real c = a + b;\nend F;\n\nfunction G\n"
                           "  input Real[3] a;\n  output Real c = a[1];\nend G;\n\nclass VT1\n  parameter Real q[1] = 1.0;\n"
                           "  parameter Real q[2] = 2.0;\n  parameter Real q[3] = 3.0;\n  Real x[1];\n  Real x[2];\n"
                           "  Real x[3];\n  Integer k[1];\n  Integer k[2];\n  Integer k[3];\n  Real w[1,1];\n  Real w[1,2];\n"
                           "  Real w[1,3];\n  Real w[2,1];\n  Real w[2,2];\n  Real w[2,3];\n  Real a[1];\n  Real a[2];\n"
                           "  Real a[3];\n  Real c = F(x[1], 1.0) + F(x[2], 1.0) + F(x[3], 1.0);\n"
                           "  parameter Real d[1] = F(q[1], 1.0);\n  parameter Real d[2] = F(q[2], 1.0);\n"
                           "  parameter Real d[3] = F(q[3], 1.0);\n  Real e = G(array(F(x[$i1], 1.0) for $i1 in 1:3));\n"
                           "  Real g[1];\n  Real g[2];\n  Real g[3];\n  Real n[1,1];\n  Real n[1,2];\n  Real n[1,3];\n"
                           "  Real n[2,1];\n  Real n[2,2];\n  Real n[2,3];\n  Real p[1];\n  Real p[2];\n  Real p[3];\n"
                           "  Real v[1];\n  Real v[2];\n  Real v[3];\n  Real ac[1];\n  Real ac[2];\n  Real ac[3];\n"
                           "  Real ah[1];\n  Real ah[2];\n  Real ah[3];\n  Real aj[1];\n  Real aj[2];\n  Real aj[3];\n"
                           "  Real t[1](start = 1.0);\n  Real t[2](start = 2.0);\n  Real t[3](start = 3.0);\n  Real u[1];\n"
                           "  Real u[2];\n  Real u[3];\nequation\n  x = {time, 1.0, 2.0};\n  k = {1, 2, 3};\n"
                           "  w = {{time, 1.0, 2.0}, {3.0, 4.0, 5.0}};\n  a = array(F(x[$i1], 2.0) for $i1 in 1:3);\n"
                           "  g = array(F({1.0, 2.0, time}[$i1], 2.0) for $i1 in 1:3);\n"
                           "  n = array(array(F(w[$i1,$i2], 1.0) for $i2 in 1:3) for $i1 in 1:2);\n"
                           "  p = array(F(/*Real*/(k[$i1]), 1.0) for $i1 in 1:3);\n"
                           "  v = array(F(w[1,$i1], 1.0) for $i1 in 1:3);\n  ac = array(atan2(x[$i1], x[$i1]) for $i1 in 1:3);\n"
                           "  ah = /*Real[3]*/(array(abs(k[$i1]) for $i1 in 1:3));\n  aj = array(sin(q[$i1]) for $i1 in 1:3);\n"
                           "  u = array(F(x[i], /*Real*/(i)) for i in 1:3);\nend VT1;\n"));
  // 嵌在数组运算里的向量化调用整体保留（omc `array(…) * 2.0`；62f 起走含数组调用的符号形态，期望 = omc 1.25.0 实跑，
  // 2026-10-07）。
  YMC_CHECK_EQ(flatten_src("function F\n  input Real a;\n  input Real b;\n  output Real c = a + b;\nend F;\nmodel M\n"
                           "  Real x[3] = {time, 1, 2};\n  Real w[2, 3] = {{time, 1, 2}, {3, 4, 5}};\n  Real l[3] = 2 * F(x, 1);\nend M;\n"),
               std::string("function F\n  input Real a;\n  input Real b;\n  output Real c = a + b;\nend F;\n\nclass M\n  Real x[1];\n"
                           "  Real x[2];\n  Real x[3];\n  Real w[1,1];\n  Real w[1,2];\n  Real w[1,3];\n  Real w[2,1];\n  Real w[2,2];\n"
                           "  Real w[2,3];\n  Real l[1];\n  Real l[2];\n  Real l[3];\nequation\n  x = {time, 1.0, 2.0};\n"
                           "  w = {{time, 1.0, 2.0}, {3.0, 4.0, 5.0}};\n  l = array(F(x[$i1], 1.0) for $i1 in 1:3) * 2.0;\nend M;\n"));
  // 非法 / 不复刻的形态必须 SKIP（V4 探针）：向量化维不一致（omc 内部错）、rem（1.25.0 打成 OpenModelica.Internal.realRem）。
  // 范围切片实参（omc `x[(2:3)[$i1]]`）64c 起照做，见 flatten_model_vectorization_slices 的 VCU4。
  const char* pre = "function F\n  input Real a;\n  input Real b;\n  output Real c = a + b;\nend F;\nmodel M\n"
                    "  Real x[3] = {time, 1, 2};\n  Real w[2, 3] = {{time, 1, 2}, {3, 4, 5}};\n";
  for (const auto& [line, why] : std::vector<std::pair<std::string, std::string>>{
           {"  Real aa[2, 3] = F(w, x);\n", "function: F"},
           {"  Real b6[3] = rem(x, 2);\n", "non-numeric function argument"}}) {
    bool uns = false;
    const std::string out = flatten_src((std::string(pre) + line + "end M;\n").c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_sum_product_min_max_of_arrays) {
  // 模型语境的 sum / product / min / max(数组)：实参化简后是字面量数组 → 一个字面量；否则 sum / product 展开成元素链、链
  // 本身不再化简（`1.0 + 2.0 + time`、`x[1] + 0.0 + x[2]`、`(-x[1]) + …`、Integer 链整体转型）；min / max 的数组构造子实参改写成
  // 归约式；常量实参的向量化调用整体保留、展开时逐元素求值。期望 = omc 1.25.0 实跑 SR1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("function F\n  input Real a;\n  input Real b;\n  output Real c = a + b;\nend F;\nmodel SR1\n"
                           "  parameter Real q[3] = {1, 2, 3};\n  Real x[3] = {time, 1, 2};\n  Real s1 = sum({1, 2, time});\n"
                           "  Real s3 = product({2, 3, time});\n  Real s4 = sum({i for i in 1:3});\n"
                           "  Real s5 = sum({i * 1.0 for i in 1:3});\n  Real s7 = sum({F(1, 2), F(2, 2)});\n"
                           "  parameter Real s12 = sum({1, 2, q[1]});\n  Integer s13 = sum({1, 2, 3});\n"
                           "  Real s15 = sum({x[1], 0, x[2]});\n  Real s20 = sum({1, 2} .* {3, 4});\n"
                           "  Real s27 = sum(linspace(0, 1, 3));\n  Real m2 = max({i for i in 1:3});\n"
                           "  Real m5 = max(linspace(0, 1, 3));\n  Real m6 = max(sin({1, 2, 3}));\n"
                           "  Real m9 = max({F(1, 2), F(2, 2)});\n  Real m20 = sum(-x);\n  Real bt = sum(sin({1, 2, 3}));\n"
                           "  Real bj[3] = sin({1, 2, 3});\nend SR1;\n"),
               std::string("class SR1\n  parameter Real q[1] = 1.0;\n  parameter Real q[2] = 2.0;\n"
                           "  parameter Real q[3] = 3.0;\n  Real x[1];\n  Real x[2];\n  Real x[3];\n"
                           "  Real s1 = 1.0 + 2.0 + time;\n  Real s3 = 2.0 * 3.0 * time;\n  Real s4 = /*Real*/(1 + 2 + 3);\n"
                           "  Real s5 = 1.0 + 2.0 + 3.0;\n  Real s7 = 7.0;\n  parameter Real s12 = 1.0 + 2.0 + q[1];\n"
                           "  Integer s13 = 6;\n  Real s15 = x[1] + 0.0 + x[2];\n  Real s20 = 11.0;\n"
                           "  Real s27 = 0.0 + 0.5 + 1.0;\n  Real m2 = /*Real*/(max(i for i in 1:3));\n"
                           "  Real m5 = max(/*Real*/(i - 1) / 2.0 for i in 1:3);\n"
                           "  Real m6 = max(sin({1.0, 2.0, 3.0}[$i1]) for $i1 in 1:3);\n  Real m9 = 4.0;\n"
                           "  Real m20 = (-x[1]) + (-x[2]) + (-x[3]);\n"
                           "  Real bt = 0.8414709848078965 + 0.9092974268256817 + 0.1411200080598672;\n  Real bj[1];\n"
                           "  Real bj[2];\n  Real bj[3];\nequation\n  x = {time, 1.0, 2.0};\n"
                           "  bj = array(sin({1.0, 2.0, 3.0}[$i1]) for $i1 in 1:3);\nend SR1;\n"));
  // 展开时成常量的用户函数调用 omc 会求值（`parameter Real bp[3] = F({1, 2, 3}, 2)` → `bp[1] = 3.0`，V4g 探针），未做 → SKIP。
  bool uns = false;
  const std::string out = flatten_src("function F\n  input Real a;\n  input Real b;\n  output Real c = a + b;\nend F;\nmodel BP\n"
                                      "  parameter Real bp[3] = F({1, 2, 3}, 2);\nend BP;\n",
                                      &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:constant user call after structural substitution>"));
}

YMC_TEST(flatten_der_pre_on_arrays) {
  // der / pre 的数组实参不向量化（Call.isVectorizeable 为 false）：显式等式逐元素标量化（`der(y[1]) = …`、`v[1] = pre(b[1]) and
  // b2[1]`），绑定整体保留（`k1 = der(x)`）、算术绑定逐元素展开；edge 向量化、change 整体保留。期望 = omc 1.25.0 实跑 DP1
  //（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model DP1\n  parameter Real A[3, 3] = {{1, 0, 0}, {0, 2, 0}, {0, 0, 3}};\n"
                           "  Boolean b[2] = {time > 1, true};\n  Boolean b2[2] = {true, time > 2};\n"
                           "  Real x[3] = {time, 1, 2};\n  Real y[3];\n  Real z[3];\n  Real k1[3] = der(x);\n"
                           "  Real k2[3] = 2 * der(x);\n  Boolean v[2];\n  Boolean w[2];\n  Boolean d7[2] = edge(b);\n"
                           "  Boolean d9[2] = change(b);\nequation\n  der(y) = -A * y + x;\n  z = der(x) + x;\n"
                           "  v = pre(b) and b2;\n  w = pre(b);\nend DP1;\n"),
               std::string("class DP1\n  parameter Real A[1,1] = 1.0;\n  parameter Real A[1,2] = 0.0;\n"
                           "  parameter Real A[1,3] = 0.0;\n  parameter Real A[2,1] = 0.0;\n  parameter Real A[2,2] = 2.0;\n"
                           "  parameter Real A[2,3] = 0.0;\n  parameter Real A[3,1] = 0.0;\n  parameter Real A[3,2] = 0.0;\n"
                           "  parameter Real A[3,3] = 3.0;\n  Boolean b[1];\n  Boolean b[2];\n  Boolean b2[1];\n"
                           "  Boolean b2[2];\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y[1];\n  Real y[2];\n"
                           "  Real y[3];\n  Real z[1];\n  Real z[2];\n  Real z[3];\n  Real k1[1];\n  Real k1[2];\n"
                           "  Real k1[3];\n  Real k2[1];\n  Real k2[2];\n  Real k2[3];\n  Boolean v[1];\n  Boolean v[2];\n"
                           "  Boolean w[1];\n  Boolean w[2];\n  Boolean d7[1];\n  Boolean d7[2];\n  Boolean d9[1];\n"
                           "  Boolean d9[2];\nequation\n  b = {time > 1.0, true};\n  b2 = {true, time > 2.0};\n"
                           "  x = {time, 1.0, 2.0};\n  k1 = der(x);\n"
                           "  k2 = {2.0 * der(x[1]), 2.0 * der(x[2]), 2.0 * der(x[3])};\n"
                           "  d7 = array(edge(b[$i1]) for $i1 in 1:2);\n  d9 = change(b);\n"
                           "  der(y[1]) = x[1] - (A[1,1] * y[1] + A[1,2] * y[2] + A[1,3] * y[3]);\n"
                           "  der(y[2]) = x[2] - (A[2,1] * y[1] + A[2,2] * y[2] + A[2,3] * y[3]);\n"
                           "  der(y[3]) = x[3] - (A[3,1] * y[1] + A[3,2] * y[2] + A[3,3] * y[3]);\n  z[1] = der(x[1]) + x[1];\n"
                           "  z[2] = der(x[2]) + x[2];\n  z[3] = der(x[3]) + x[3];\n  v[1] = pre(b[1]) and b2[1];\n"
                           "  v[2] = pre(b[2]) and b2[2];\n  w[1] = pre(b[1]);\n  w[2] = pre(b[2]);\nend DP1;\n"));
}

YMC_TEST(flatten_boolean_array_expressions) {
  // Boolean 数组的 and / or / not：绑定整体保留（`g3 = b and not b2`），全字面量折成字面量数组，参数数组逐元素摊分、显式等式
  // 逐元素（都不化简 `true or b[1]`）；Boolean 范围整体保留、空范围不出变量；`:` 维由这些绑定推断。期望 = omc 1.25.0 实跑 BL1
  //（2026-10-02）。omc 特殊处理 / 报错的形态 → SKIP（V4u 探针）。
  YMC_CHECK_EQ(flatten_src("model BL1\n  parameter Boolean pb[2] = {true, false};\n  Boolean b[2] = {time > 1, true};\n"
                           "  Boolean b2[2] = {true, time > 2};\n  Boolean a2[:] = not b;\n  Boolean a3[:] = b and b2;\n"
                           "  Boolean a5[:] = {false, false, true, true} or {true, false, true, false};\n"
                           "  Boolean a6[:] = false:true;\n  Boolean c1[:] = true:false;\n  Boolean g3[2] = b and not b2;\n"
                           "  Boolean g5[2] = (b and b2) or b;\n  Boolean g9[2] = {true, false} or b;\n"
                           "  parameter Boolean pc[2] = not pb;\n  parameter Boolean pd[2] = pb and {true, true};\n"
                           "  Boolean v[2];\n  Boolean w[2];\nequation\n  v = {true, false} or b;\n  w = false:true;\nend BL1;\n"),
               std::string("class BL1\n  parameter Boolean pb[1] = true;\n  parameter Boolean pb[2] = false;\n  Boolean b[1];\n"
                           "  Boolean b[2];\n  Boolean b2[1];\n  Boolean b2[2];\n  Boolean a2[1];\n  Boolean a2[2];\n"
                           "  Boolean a3[1];\n  Boolean a3[2];\n  Boolean a5[1];\n  Boolean a5[2];\n  Boolean a5[3];\n"
                           "  Boolean a5[4];\n  Boolean a6[1];\n  Boolean a6[2];\n  Boolean g3[1];\n  Boolean g3[2];\n"
                           "  Boolean g5[1];\n  Boolean g5[2];\n  Boolean g9[1];\n  Boolean g9[2];\n"
                           "  parameter Boolean pc[1] = not pb[1];\n  parameter Boolean pc[2] = not pb[2];\n"
                           "  parameter Boolean pd[1] = pb[1] and true;\n  parameter Boolean pd[2] = pb[2] and true;\n"
                           "  Boolean v[1];\n  Boolean v[2];\n  Boolean w[1];\n  Boolean w[2];\nequation\n"
                           "  b = {time > 1.0, true};\n  b2 = {true, time > 2.0};\n  a2 = not b;\n  a3 = b and b2;\n"
                           "  a5 = {true, false, true, true};\n  a6 = false:true;\n  g3 = b and not b2;\n  g5 = b and b2 or b;\n"
                           "  g9 = {true, false} or b;\n  v[1] = true or b[1];\n  v[2] = false or b[2];\n  w[1] = false;\n"
                           "  w[2] = true;\nend BL1;\n"));
  const char* pre = "model M\n  Boolean b[2] = {time > 1, true};\n  Boolean b2[2] = {true, time > 2};\n  Boolean v[2];\n";
  for (const auto& [tail, why] : std::vector<std::pair<std::string, std::string>>{
           {"equation\n  v = b and (time > 1);\n", "array shape mismatch"},                       // omc：类型错
           {"  Boolean l1[2, 2] = {b, b2} and {b2, b};\n", "array logical of two array literals"},   // omc：`{b and b2, b2 and b}`
           {"equation\n  v = not {b[1], true};\n", "not of literal element in mixed array literal"},   // omc：`v[2] = not true`
           {"  Boolean m1 = {1, 2} < {2, 3};\n", "array relation"}}) {                              // omc：类型错
    bool uns = false;
    const std::string out = flatten_src((std::string(pre) + tail + "end M;\n").c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_array_if_expressions) {
  // 数组 if 表达式：绑定整体保留、各支规整（算术式展开成字面量、字面量逐叶转 Real、向量化调用原样），显式等式 / 参数摊分 /
  // sum 里逐元素；条件折成字面量时直接取一支（另一支形状不同也无妨）。期望 = omc 1.25.0 实跑 IF1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model IF1\n  parameter Integer n = 2;\n  parameter Real q[3] = {1, 2, 3};\n"
                           "  Boolean b[2] = {time > 1, true};\n  Boolean b2[2] = {true, time > 2};\n"
                           "  Real x[3] = {time, 1, 2};\n  Boolean i1[2] = if time > 1 then b else b2;\n"
                           "  Real i3[:] = if time > 1 then x else 2 * x;\n"
                           "  Real i6[3] = if time > 1 then x else if time > 2 then 2 * x else {1, 2, 3};\n"
                           "  Real i7[3] = (if time > 1 then x else 2 * x) + x;\n  Real i8[3] = if b[1] then sin(x) else x;\n"
                           "  Real i9 = sum(if time > 1 then x else 2 * x);\n"
                           "  Real j2[2] = if time > 1 then {1, 2} else {3, 4};\n"
                           "  Real r9[3] = if time > 1 then q else {time, 1, 2};\n  Real r3[3] = if true then x else 2 * x;\n"
                           "  Real r4[2] = if false then {1, 2} elseif false then {1} else {3, 4};\n"
                           "  parameter Real q5[3] = if n == 2 then q else 2 * q;\n  Real y[3];\nequation\n"
                           "  der(y) = if time > 1 then x else -y;\nend IF1;\n"),
               std::string("class IF1\n  parameter Integer n = 2;\n  parameter Real q[1] = 1.0;\n  parameter Real q[2] = 2.0;\n"
                           "  parameter Real q[3] = 3.0;\n  Boolean b[1];\n  Boolean b[2];\n  Boolean b2[1];\n  Boolean b2[2];\n"
                           "  Real x[1];\n  Real x[2];\n  Real x[3];\n  Boolean i1[1];\n  Boolean i1[2];\n  Real i3[1];\n"
                           "  Real i3[2];\n  Real i3[3];\n  Real i6[1];\n  Real i6[2];\n  Real i6[3];\n  Real i7[1];\n"
                           "  Real i7[2];\n  Real i7[3];\n  Real i8[1];\n  Real i8[2];\n  Real i8[3];\n"
                           "  Real i9 = (if time > 1.0 then x[1] else 2.0 * x[1]) + (if time > 1.0 then x[2] else 2.0 * x[2]) + (if time > 1.0 then x[3] else 2.0 * x[3]);\n"
                           "  Real j2[1];\n  Real j2[2];\n  Real r9[1];\n  Real r9[2];\n  Real r9[3];\n  Real r3[1];\n"
                           "  Real r3[2];\n  Real r3[3];\n  Real r4[1];\n  Real r4[2];\n"
                           "  parameter Real q5[1] = if n == 2 then q[1] else 2.0 * q[1];\n"
                           "  parameter Real q5[2] = if n == 2 then q[2] else 2.0 * q[2];\n"
                           "  parameter Real q5[3] = if n == 2 then q[3] else 2.0 * q[3];\n  Real y[1];\n  Real y[2];\n"
                           "  Real y[3];\nequation\n  b = {time > 1.0, true};\n  b2 = {true, time > 2.0};\n"
                           "  x = {time, 1.0, 2.0};\n  i1 = if time > 1.0 then b else b2;\n"
                           "  i3 = if time > 1.0 then x else {2.0 * x[1], 2.0 * x[2], 2.0 * x[3]};\n"
                           "  i6 = if time > 1.0 then x else if time > 2.0 then {2.0 * x[1], 2.0 * x[2], 2.0 * x[3]} else {1.0, 2.0, 3.0};\n"
                           "  i7 = {(if time > 1.0 then x[1] else 2.0 * x[1]) + x[1], (if time > 1.0 then x[2] else 2.0 * x[2]) + x[2], (if time > 1.0 then x[3] else 2.0 * x[3]) + x[3]};\n"
                           "  i8 = if b[1] then array(sin(x[$i1]) for $i1 in 1:3) else x;\n"
                           "  j2 = if time > 1.0 then {1.0, 2.0} else {3.0, 4.0};\n"
                           "  r9 = if time > 1.0 then q else {time, 1.0, 2.0};\n  r3 = x;\n  r4 = {3.0, 4.0};\n"
                           "  der(y[1]) = if time > 1.0 then x[1] else -y[1];\n"
                           "  der(y[2]) = if time > 1.0 then x[2] else -y[2];\n"
                           "  der(y[3]) = if time > 1.0 then x[3] else -y[3];\nend IF1;\n"));
  // omc 报错 / 内部错 / 未做的形态 → SKIP（V4w 探针）。
  const char* pre = "model M\n  parameter Integer n = 2;\n  Real x[3] = {time, 1, 2};\n  Integer k[3] = {1, 2, 3};\n";
  for (const auto& [line, why] : std::vector<std::pair<std::string, std::string>>{
           {"  Real q7[3] = if time > 1 then x else {1, 2};\n", "if-expression branch shape"},               // omc：类型错
           {"  Real r2[3](start = if n == 2 then {1, 2, 3} else {4, 5, 6});\n", "if-expression array attribute"},   // omc：内部错
           {"  Real p1[3] = if time > 1 then x else k;\n", "if-expression array branch"}}) {                 // Integer 引用配 Real：未做
    bool uns = false;
    const std::string out = flatten_src((std::string(pre) + line + "end M;\n").c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_if_expression_structural_branch_selection) {
  // 数组 if 两支维不同：if 是整个绑定 / 等式一侧、条件可按结构参数求值时取选中的那一支，条件里的参数打 final。期望 = omc
  // 1.25.0 实跑 SEL1（2026-10-02）。其余位置 / 形态 → SKIP（V4z 探针：嵌在运算里 omc 类型错、嵌在另一个 if 里 omc 原样留下、
  // 选中的支引用参数时 omc 的求值有怪相、运行时条件 omc 类型错）。
  YMC_CHECK_EQ(flatten_src("model SEL1\n  parameter Integer n = 2;\n  parameter Boolean pt = true;\n"
                           "  parameter Boolean pf = false;\n  Real x[3] = {time, 1, 2};\n"
                           "  Real a1[2] = if pt then {1, 2} else {3, 4, 5};\n  Real a4[:] = if n == 2 then x else {1, 2};\n"
                           "  Real b5[:, :] = if n == 2 then ones(n, 3) else ones(n, n);\n  Real y[3];\nequation\n"
                           "  y = if pf then {1, 2} else x;\nend SEL1;\n"),
               std::string("class SEL1\n  final parameter Integer n = 2;\n  final parameter Boolean pt = true;\n"
                           "  final parameter Boolean pf = false;\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real a1[1];\n"
                           "  Real a1[2];\n  Real a4[1];\n  Real a4[2];\n  Real a4[3];\n  Real b5[1,1];\n  Real b5[1,2];\n"
                           "  Real b5[1,3];\n  Real b5[2,1];\n  Real b5[2,2];\n  Real b5[2,3];\n  Real y[1];\n  Real y[2];\n"
                           "  Real y[3];\nequation\n  x = {time, 1.0, 2.0};\n  a1 = {1.0, 2.0};\n  a4 = x;\n"
                           "  b5 = {{1.0, 1.0, 1.0}, {1.0, 1.0, 1.0}};\n  y[1] = x[1];\n  y[2] = x[2];\n  y[3] = x[3];\n"
                           "end SEL1;\n"));
  const char* pre = "model M\n  parameter Integer n = 2;\n  parameter Boolean pt = true;\n  parameter Boolean pf = false;\n";
  for (const auto& [line, why] : std::vector<std::pair<std::string, std::string>>{
           {"  Real d4[2] = 2 * (if pt then {1, 2} else {3, 4, 5});\n", "if-expression branch shape"},
           {"  Real d1[:] = if pt then (if pf then {1} else {2, 3}) else {4};\n", "if-expression branch shape"},
           {"  Real b2[:] = if n == 3 then {1} else fill(n, 2);\n", "if-expression selected branch references parameter"},
           {"  Real b7[2] = if time > 1 then {1, 2} else {3, 4, 5};\n", "if-expression branch shape"},
           {"  Real b9[2] = if pt then {1, 2} else 3;\n", "if-expression branch shape"}}) {
    bool uns = false;
    const std::string out = flatten_src((std::string(pre) + line + "end M;\n").c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_evaluate_annotation_and_structural_alias) {
  // annotation(Evaluate = true) 的标量参数 = 结构参数（final、求值成字面量、绑定里用到的参数连带 final、引用处代入并化简）；
  // 绑定在源码里就是结构参数裸引用的参数连带成为结构参数（声明绑定 / 实例修饰，传递），表达式绑定 / 类型不同的别名 / 记录
  // 表达式拆出的字段只是被求值。期望 = omc 1.25.0 实跑 EVT1 / EVT2（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model EVT1\n  record R\n    parameter Real x = 1;\n    parameter Real w = x;\n  end R;\n  model M\n"
                           "    parameter Real g = 1 annotation(Evaluate = true);\n    parameter Real h = 1;\n"
                           "    Real y = (g + h) * time;\n  end M;\n  parameter Real p = 2 annotation(Evaluate = true);\n"
                           "  parameter Real a = 1;\n  parameter Real b = 2 * a annotation(Evaluate = true);\n"
                           "  parameter Real q = p;\n  parameter Real r = q;\n  parameter Real s = -p;\n"
                           "  parameter Integer n = 2;\n  parameter Integer m = n;\n  parameter Real t = n;\n"
                           "  parameter R r1(x = p);\n  parameter R r2 = r1;\n  M m1(g = 3, h = p);\n  Real z[n];\n"
                           "  Real x = (p + b + r + s + m + t + r2.x) * time;\nend EVT1;\n"),
               std::string("class EVT1\n  final parameter Real p = 2.0;\n  final parameter Real a = 1.0;\n"
                           "  final parameter Real b = 2.0;\n  final parameter Real q = 2.0;\n  final parameter Real r = 2.0;\n"
                           "  parameter Real s = -2.0;\n  final parameter Integer n = 2;\n  final parameter Integer m = 2;\n"
                           "  parameter Real t = 2.0;\n  final parameter Real r1.x = 2.0;\n  final parameter Real r1.w = 2.0;\n"
                           "  parameter Real r2.x = 2.0;\n  parameter Real r2.w = 2.0;\n  final parameter Real m1.g = 3.0;\n"
                           "  final parameter Real m1.h = 2.0;\n  Real m1.y = 5.0 * time;\n  Real z[1];\n  Real z[2];\n"
                           "  Real x = (6.0 + s + 2.0 + t + r2.x) * time;\nend EVT1;\n"));
  // 绑定依赖 Evaluate = false 的参数：omc 不把它当结构参数。
  YMC_CHECK_EQ(flatten_src("model EVT2\n  parameter Real q = 3 annotation(Evaluate = false);\n"
                           "  parameter Real p = q annotation(Evaluate = true);\n  Real x = p * time;\nend EVT2;\n"),
               std::string("class EVT2\n  parameter Real q = 3.0;\n  parameter Real p = q;\n  Real x = p * time;\nend EVT2;\n"));
  // 无绑定只有 start：start 就是绑定（updateParameterBinding），照常结构求值（omc 1.25.0 实跑，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("model E2\n  parameter Real p(start = 1) annotation(Evaluate = true);\n  Real x = p * time;\nend E2;\n"),
               std::string("class E2\n  final parameter Real p(start = 1.0) = 1.0;\n  Real x = time;\nend E2;\n"));
  // 参数数组同样（加宽 62b；omc 1.25.0 实跑，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("model E7\n  parameter Real x[3] = {1, 2, 3} annotation(Evaluate = true);\n  Real y = x[2] * time;\nend E7;\n"),
               std::string("class E7\n  final parameter Real x[1] = 1.0;\n  final parameter Real x[2] = 2.0;\n"
                           "  final parameter Real x[3] = 3.0;\n  Real y = 2.0 * time;\nend E7;\n"));
  // 未做的形态 → SKIP（EV 探针）：fixed = false（omc 忽略 Evaluate）。
  bool uns = false;
  const std::string out = flatten_src("model E3\n  parameter Real p(fixed = false) annotation(Evaluate = true);\n  Real x = p * time;\n"
                                      "initial equation\n  p = 3;\nend E3;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:Evaluate annotation>"));
}

YMC_TEST(flatten_pre_change_argument_rules) {
  // pre / change 的实参须是变量，when 之外不能是连续变量（NFBuiltinCall.typePreChangeCall :578-588；P1 / P5 / P7 探针 omc 报错）；
  // when 方程体里的连续实参 1.25.0 接受（P4 探针，期望 = 实跑输出，2026-10-02）。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"model P1\n  Real x = time;\n  Real y = pre(x);\nend P1;\n", "pre/change of continuous variable"},
           {"model P5\n  Real x = time;\n  Boolean b = change(x);\nend P5;\n", "pre/change of continuous variable"},
           {"model P7\n  Real x = time;\n  Real y = pre(x + 1);\nend P7;\n", "pre/change argument must be a variable"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
  YMC_CHECK_EQ(flatten_src("model P4\n  Real x = time;\n  discrete Real y;\nequation\n  when time > 1 then\n    y = pre(x);\n"
                           "  end when;\nend P4;\n"),
               std::string("class P4\n  Real x = time;\n  discrete Real y;\nequation\n  when time > 1.0 then\n    y = pre(x);\n"
                           "  end when;\nend P4;\n"));
}

YMC_TEST(flatten_function_sum_product_expansion) {
  // 函数体里 sum / product 的实参维全已知 → 展开成元素链；维未知保留调用；min / max 不展开。期望 = omc 1.25.0 实跑 FS1
  //（2026-10-02）。可展开但不是整体局部数组的实参（`sum(a .* b)` → `a[1] * b[1] + …`）见下面的 FS2。
  YMC_CHECK_EQ(flatten_src("function f1\n  input Real a[3];\n  input Integer b[2, 2];\n  input Real c[:];\n  output Real s;\n"
                           "  output Integer t;\n  output Real u;\n  output Real p;\nalgorithm\n  s := sum(a);\n"
                           "  t := product(b);\n  u := sum(c);\n  p := product(a) + max(a) + min(c);\nend f1;\nmodel FS1\n"
                           "  Real x;\n  Integer y;\n  Real z;\n  Real w;\nequation\n"
                           "  (x, y, z, w) = f1({time, 1, 2}, {{1, 2}, {3, 4}}, {time, time});\nend FS1;\n"),
               std::string("function f1\n  input Real[3] a;\n  input Integer[2, 2] b;\n  input Real[:] c;\n  output Real s;\n"
                           "  output Integer t;\n  output Real u;\n  output Real p;\nalgorithm\n  s := a[1] + a[2] + a[3];\n"
                           "  t := b[1,1] * b[1,2] * b[2,1] * b[2,2];\n  u := sum(c);\n"
                           "  p := a[1] * a[2] * a[3] + max(a) + min(c);\nend f1;\n\nclass FS1\n  Real x;\n  Integer y;\n"
                           "  Real z;\n  Real w;\nequation\n"
                           "  (x, y, z, w) = f1({time, 1.0, 2.0}, {{1, 2}, {3, 4}}, {time, time});\nend FS1;\n"));
  // 实参是化简时已按元素展开的算术式（59z 起做了）。期望 = omc 1.25.0 实跑 FS2（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("function f2\n  input Real a[3];\n  input Real b[3];\n  output Real s;\nalgorithm\n"
                           "  s := sum(a .* b);\nend f2;\nmodel FS2\n  Real x = f2({time, 1, 2}, {1, 2, 3});\nend FS2;\n"),
               std::string("function f2\n  input Real[3] a;\n  input Real[3] b;\n  output Real s;\nalgorithm\n"
                           "  s := a[1] * b[1] + a[2] * b[2] + a[3] * b[3];\nend f2;\n\nclass FS2\n"
                           "  Real x = f2({time, 1.0, 2.0}, {1.0, 2.0, 3.0});\nend FS2;\n"));
}

YMC_TEST(flatten_function_annotations) {
  // 对压平文本无影响的函数注解放行（Inline / smoothOrder / Documentation …，FA 探针 16 种与无注解时逐字节相同）；空 algorithm
  // 段照打一行 `algorithm`。期望 = omc 1.25.0 实跑 FAT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Real x;\n  output Real y;\nalgorithm\n  y := 2 * x;\n"
                           "  annotation(Inline = true, smoothOrder = 1, Documentation(info = \"<html>doc</html>\"));\nend f;\n"
                           "function g\n  input Real x;\n  output Real y = x;\nalgorithm\nend g;\nmodel FAT1\n"
                           "  Real a = f(time);\n  Real b = f(1);\n  Real c = g(time);\nend FAT1;\n"),
               std::string("function f\n  input Real x;\n  output Real y;\nalgorithm\n  y := 2.0 * x;\nend f;\n\nfunction g\n"
                           "  input Real x;\n  output Real y = x;\nalgorithm\nend g;\n\nclass FAT1\n  Real a = f(time);\n"
                           "  Real b = 2.0;\n  Real c = g(time);\nend FAT1;\n"));
  // derivative / inverse 引用的函数连带发射（NFFlatten.flattenFunction :2987-2995）：传递（f1 → f2 → f3）、external 段的注解
  // 同样（e1 → e2）、inverse 的调用里的函数（h → hi）；没被调用的 unused 不打。期望 = omc 1.25.0 实跑 FAT2（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("function f1\n  input Real x;\n  output Real z = 2 * x;\n  annotation(derivative = f2);\nend f1;\n"
                           "function f2\n  input Real x;\n  input Real der_x;\n  output Real z = 2 * der_x;\n"
                           "  annotation(derivative = f3);\nend f2;\nfunction f3\n  input Real x;\n  input Real der_x;\n"
                           "  input Real der_der_x;\n  output Real z = 2 * der_der_x;\nend f3;\nfunction e1\n  input Real x;\n"
                           "  output Real z;\n  external \"C\" z = foo(x) annotation(derivative = e2, Library = \"foo\");\n"
                           "end e1;\nfunction e2\n  input Real x;\n  input Real der_x;\n  output Real z = 2 * der_x;\nend e2;\n"
                           "function h\n  input Real x;\n  input Real y;\n  output Real z = x * y;\n"
                           "  annotation(inverse(y = hi(x, z)));\nend h;\nfunction hi\n  input Real x;\n  input Real z;\n"
                           "  output Real y = z / x;\nend hi;\nfunction unused\n  input Real x;\n  output Real z = x;\n"
                           "  annotation(derivative = f3);\nend unused;\nmodel FAT2\n  Real a = f1(1);\n  Real b = f1(time);\n"
                           "  Real c = e1(time);\n  Real d = h(time, 2);\nend FAT2;\n"),
               std::string("impure function e1\n  input Real x;\n  output Real z;\n\n  external \"C\" z = foo(x);\nend e1;\n\n"
                           "function e2\n  input Real x;\n  input Real der_x;\n  output Real z = 2.0 * der_x;\nend e2;\n\n"
                           "function f1\n  input Real x;\n  output Real z = 2.0 * x;\nend f1;\n\nfunction f2\n  input Real x;\n"
                           "  input Real der_x;\n  output Real z = 2.0 * der_x;\nend f2;\n\nfunction f3\n  input Real x;\n"
                           "  input Real der_x;\n  input Real der_der_x;\n  output Real z = 2.0 * der_der_x;\nend f3;\n\n"
                           "function h\n  input Real x;\n  input Real y;\n  output Real z = x * y;\nend h;\n\nfunction hi\n"
                           "  input Real x;\n  input Real z;\n  output Real y = z / x;\nend hi;\n\nclass FAT2\n  Real a = 2.0;\n"
                           "  Real b = f1(time);\n  Real c = e1(time);\n  Real d = h(time, 2.0);\nend FAT2;\n"));
  // derivative(order = 2)：修饰不进输出，引用的函数照样发射（期望 = omc 1.25.0 实跑 FAS2，2026-10-08；此前未做、按 SKIP 断言）。
  YMC_CHECK_EQ(flatten_src("function f1\n  input Real x;\n  output Real z = 2 * x;\n  annotation(derivative(order = 2) = f2);\n"
                           "end f1;\nfunction f2\n  input Real x;\n  input Real der_x;\n  input Real der_der_x;\n"
                           "  output Real z = 2 * der_der_x;\nend f2;\nmodel FAS2\n  Real b = f1(time);\nend FAS2;\n"),
               std::string("function f1\n  input Real x;\n  output Real z = 2.0 * x;\nend f1;\n\nfunction f2\n  input Real x;\n"
                           "  input Real der_x;\n  input Real der_der_x;\n  output Real z = 2.0 * der_der_x;\nend f2;\n\n"
                           "class FAS2\n  Real b = f1(time);\nend FAS2;\n"));
  // 未做 / omc 报错 → SKIP：__OpenModelica_EarlyInline（omc 把调用内联成 `2.0 * time`，FAS1）、derivative 指向不存在的函数
  //（omc 报错，FAS3）。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"function f\n  input Real x;\n  output Real y = 2 * x;\n  annotation(__OpenModelica_EarlyInline = true);\nend f;\n"
            "model FAS1\n  Real a = f(time);\nend FAS1;\n",
            "function: f"},
           {"function f1\n  input Real x;\n  output Real z = 2 * x;\n  annotation(derivative = nosuch);\nend f1;\n"
            "model FAS3\n  Real b = f1(time);\nend FAS3;\n",
            "function: f1"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_unreferenced_functions_and_substituted_calls) {
  // 被删掉的空数组绑定 / 方程 / 赋值里的调用不留函数块、折成常量的调用同样（omc 的函数收自化简后的模型，
  // NFFlatten.collectFunctions :418-429）。期望 = omc 1.25.0 实跑 FAT3（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Real r;\n  output Real o[0];\nend f;\nfunction k\n  input Real r;\n"
                           "  output Real o;\nalgorithm\n  o := r;\nend k;\nmodel FAT3\n  Real r[0] = f(time);\n  Real s[0];\n"
                           "  Real t[0];\n  Real u = k(1);\nequation\n  s = f(2 * time);\nalgorithm\n  t := f(time);\n"
                           "end FAT3;\n"),
               std::string("class FAT3\n  Real u = 1.0;\nend FAT3;\n"));
  // 结构参数代入到调用实参：记录构造照旧，external "C" 的内建等价调用不求值（`sin(2.0)`）。期望 = omc 1.25.0 实跑 FAT4。
  YMC_CHECK_EQ(flatten_src("record R\n  Real a;\n  Integer b;\nend R;\nfunction g\n  input Real x;\n  output Real y;\n"
                           "  external \"C\" y = sin(x);\nend g;\nmodel FAT4\n  parameter Integer n = 2;\n  Real z[n];\n"
                           "  R r = R(time, n);\n  Real w = g(n);\nend FAT4;\n"),
               std::string("class FAT4\n  final parameter Integer n = 2;\n  Real z[1];\n  Real z[2];\n  Real r.a = time;\n"
                           "  Integer r.b = 2;\n  Real w = sin(2.0);\nend FAT4;\n"));
  // 结构参数代入后实参全成字面量的用户函数调用 omc 会求值（NFSimplifyExp.simplifyCall :206）：`m = 6.0`、g 不再打印。期望 =
  // omc 1.25.0 实跑 FAS4（2026-10-06）。
  YMC_CHECK_EQ(flatten_src("function g\n  input Real x;\n  output Real y = 2 * x;\nend g;\nmodel FAS4\n"
                           "  parameter Integer n = 2;\n  parameter Real m = g(n) + n;\n  Real z[n];\nend FAS4;\n"),
               std::string("class FAS4\n  final parameter Integer n = 2;\n  parameter Real m = 6.0;\n  Real z[1];\n  Real z[2];\n"
                           "end FAS4;\n"));
}

YMC_TEST(flatten_structural_mark_phases) {
  // 结构标记分阶段（v1.25.0 NFStructural.markComponent :311-326 只连带"绑定尚未定型"的参数的依赖；别名连带在别名自己的
  // 绑定定型时判，NFTyping.checkComponentBindingVariability :1150-1159）。期望 = omc 1.25.0 实跑 AOT1–AOT5（2026-10-02）。
  // 别名的别名要目标先定型：m = k 定型时 k 还不是结构参数 → m 只是被求值，不 final。
  YMC_CHECK_EQ(flatten_src("model AOT1\n  parameter Integer m = k;\n  parameter Integer k = n;\n  parameter Integer n = 2;\n"
                           "  Real y[n];\n  Real x = m * time;\nend AOT1;\n"),
               std::string("class AOT1\n  parameter Integer m = 2;\n  final parameter Integer k = 2;\n"
                           "  final parameter Integer n = 2;\n  Real y[1];\n  Real y[2];\n  Real x = /*Real*/(m) * time;\n"
                           "end AOT1;\n"));
  // 绑定定型期的标记（参数下标 `c[i]`）：i 已定型 → 它的依赖 i0 不连带；声明在 v 之前的别名 j 不连带、之后的 l 连带。
  YMC_CHECK_EQ(flatten_src("model AOT2\n  parameter Integer i0 = 1;\n  parameter Integer i = i0;\n  parameter Integer j = i;\n"
                           "  parameter Integer j0 = i0;\n  Real c[2] = {time, 2};\n  Real v = c[i];\n"
                           "  parameter Integer l = i;\n  parameter Integer l0 = i0;\n  Real w = (j + j0 + l + l0) * time;\n"
                           "end AOT2;\n"),
               std::string("class AOT2\n  parameter Integer i0 = 1;\n  final parameter Integer i = 1;\n"
                           "  parameter Integer j = 1;\n  parameter Integer j0 = i0;\n  Real c[1];\n  Real c[2];\n"
                           "  Real v = c[1];\n  final parameter Integer l = 1;\n  parameter Integer l0 = i0;\n"
                           "  Real w = /*Real*/(j + j0 + 1 + l0) * time;\nequation\n  c = {time, 2.0};\nend AOT2;\n"));
  // 同样的下标，但 i 声明在 v 之后（标记时绑定未定型）→ 依赖 i0 连带，i0 的别名 j 也连带。
  YMC_CHECK_EQ(flatten_src("model AOT3\n  Real c[2] = {time, 2};\n  Real v = c[i];\n  parameter Integer i = i0;\n"
                           "  parameter Integer i0 = 1;\n  parameter Integer j = i0;\n  Real w = j * time;\nend AOT3;\n"),
               std::string("class AOT3\n  Real c[1];\n  Real c[2];\n  Real v = c[1];\n  final parameter Integer i = 1;\n"
                           "  final parameter Integer i0 = 1;\n  final parameter Integer j = 1;\n  Real w = time;\nequation\n"
                           "  c = {time, 2.0};\nend AOT3;\n"));
  // 方程段（for 范围）与压平期（if 表达式选支）的标记：不连带依赖（k），也不连带别名（m / pd / pe，哪怕声明在后）。
  YMC_CHECK_EQ(flatten_src("model AOT4\n  parameter Integer k = 2;\n  parameter Integer n = k;\n  parameter Integer m = n;\n"
                           "  parameter Boolean pt = true;\n  parameter Boolean pd = pt;\n  Real x[2];\n"
                           "  Real a1[2] = if pt then {1, 2} else {3, 4, 5};\n  parameter Boolean pe = pt;\n"
                           "  Real w = if pd and pe then m * time else 1;\nequation\n  for i in 1:n loop\n    x[i] = time;\n"
                           "  end for;\nend AOT4;\n"),
               std::string("class AOT4\n  parameter Integer k = 2;\n  final parameter Integer n = 2;\n"
                           "  parameter Integer m = 2;\n  final parameter Boolean pt = true;\n  parameter Boolean pd = true;\n"
                           "  Real x[1];\n  Real x[2];\n  Real a1[1];\n  Real a1[2];\n  parameter Boolean pe = true;\n"
                           "  Real w = if pd and pe then /*Real*/(m) * time else 1.0;\nequation\n  a1 = {1.0, 2.0};\n"
                           "  x[1] = time;\n  x[2] = time;\nend AOT4;\n"));
  // 实例化期 / 组件定型期的标记（绑定里数组构造子的迭代范围、`:` 维数组的绑定、fixed 属性）：别名一律连带。
  YMC_CHECK_EQ(flatten_src("model AOT5\n  parameter Integer n = 2;\n  parameter Integer m = n;\n  parameter Integer p = 2;\n"
                           "  parameter Integer q = p;\n  parameter Boolean b = true;\n  parameter Boolean c = b;\n"
                           "  Real x[2] = {i * time for i in 1:n};\n  Real y[3] = {time, 1, 2};\n  Real s[:] = y[1:p];\n"
                           "  Real z(fixed = b, start = 1);\n  Real w = if c then (m + q) * time else 1;\nequation\n"
                           "  der(z) = 1;\nend AOT5;\n"),
               std::string("class AOT5\n  final parameter Integer n = 2;\n  final parameter Integer m = 2;\n"
                           "  final parameter Integer p = 2;\n  final parameter Integer q = 2;\n"
                           "  final parameter Boolean b = true;\n  final parameter Boolean c = true;\n  Real x[1];\n"
                           "  Real x[2];\n  Real y[1];\n  Real y[2];\n  Real y[3];\n  Real s[1];\n  Real s[2];\n"
                           "  Real z(start = 1.0, fixed = true);\n  Real w = 4.0 * time;\nequation\n"
                           "  x = array(/*Real*/(i) * time for i in 1:2);\n  y = {time, 1.0, 2.0};\n  s = y[1:2];\n"
                           "  der(z) = 1.0;\nend AOT5;\n"));
}

YMC_TEST(flatten_evaluation_frame_and_failed_marks) {
  // 外层写的修饰表达式里的参数按**书写处**实例求值与标记，不是被修饰实例里的同名参数（此前 `m.v = c[2]`、`fixed = false`、
  // m.i 被标 final）。期望 = omc 1.25.0 实跑 AOT6（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model AOT6\n  model M\n    parameter Integer i = 2;\n    parameter Boolean b = false;\n    Real v;\n"
                           "    Real x(start = 1);\n  equation\n    der(x) = 1;\n  end M;\n  parameter Integer i = 1;\n"
                           "  parameter Boolean b = true;\n  Real c[2] = {time, 2};\n  M m(v = c[i], x(fixed = b));\nend AOT6;\n"),
               std::string("class AOT6\n  final parameter Integer i = 1;\n  final parameter Boolean b = true;\n  Real c[1];\n"
                           "  Real c[2];\n  parameter Integer m.i = 2;\n  parameter Boolean m.b = false;\n  Real m.v = c[1];\n"
                           "  Real m.x(start = 1.0, fixed = true);\nequation\n  c = {time, 2.0};\n  der(m.x) = 1.0;\nend AOT6;\n"));
  // 求不出值的求值不留结构标记：if 方程的运行时条件 `time > k` 里的 k 仍是普通参数。期望 = omc 1.25.0 实跑 AOT7。
  YMC_CHECK_EQ(flatten_src("model AOT7\n  parameter Real k = 5;\n  Real x;\nequation\n  if time > k then\n    x = 1;\n  else\n"
                           "    x = 2;\n  end if;\nend AOT7;\n"),
               std::string("class AOT7\n  parameter Real k = 5.0;\n  Real x;\nequation\n  if time > k then\n    x = 1.0;\n"
                           "  else\n    x = 2.0;\n  end if;\nend AOT7;\n"));
  // 1.25.0 对界非字面量的切片的 size 原样留下（`Integer s = size(x[1:n], 1)`，AOS1）→ SKIP；if 两支类型不相容是 omc 类型错
  //（AOS2）→ SKIP。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"model AOS1\n  parameter Integer n = 2;\n  Real x[3] = {time, 1, 2};\n  Integer s = size(x[1:n], 1);\nend AOS1;\n",
            "size of non-literal slice"},
           {"model AOS2\n  Real x = if time > 0 then 1 else \"a\";\nend AOS2;\n", "if-expression branch type mismatch"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_synchronous_operators) {
  // 同步算子（MLS §16）的定型与打印：sample(u) 补 `Clock()`、Clock 构造按首参类型补缺省（Integer → resolution 1、Boolean →
  // startInterval 0.0）且首参不代入 / 不化简、第二实参求值成字面量；计数类实参（backSample 的 n）按结构参数求值——n 在 y6 的
  // 绑定里才标，所以 m 不 final、m2 final；Integer 结果进 Real 语境整体转型。期望 = omc 1.25.0 实跑 SYT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model SYT1\n  parameter Integer n = 2;\n  parameter Integer m = n;\n  parameter Real p = 0.5;\n"
                           "  Real x = time;\n  Real z[2] = {time, 1};\n  Integer k = integer(time);\n"
                           "  Boolean b = time > 0.5;\n  Real y1 = sample(x);\n  Real y2 = sample(x, Clock(2 * p));\n"
                           "  Real y3 = sample(1);\n  Boolean c1 = sample(0, 1);\n  Real y4 = hold(k);\n"
                           "  Real y5[2] = noClock(z);\n  Real y6 = backSample(x, n);\n  parameter Integer m2 = n;\n"
                           "  Real y7 = shiftSample(x, shiftCounter = 1, resolution = 4);\n"
                           "  Real y8 = subSample(x) + superSample(x, 3);\n  Real y9 = interval() + interval(x);\n"
                           "  Boolean c2 = firstTick();\n"
                           "  Real y10 = sample(x, Clock(n)) + sample(x, Clock(b)) + sample(x, Clock(Clock(1, 10), \"ImplicitEuler\"));\n"
                           "  Real y11 = sample(x, subSample(Clock(b, p), 2));\nend SYT1;\n"),
               std::string("class SYT1\n  final parameter Integer n = 2;\n  parameter Integer m = 2;\n"
                           "  parameter Real p = 0.5;\n  Real x = time;\n  Real z[1];\n  Real z[2];\n"
                           "  Integer k = integer(time);\n  Boolean b = time > 0.5;\n  Real y1 = sample(x, Clock());\n"
                           "  Real y2 = sample(x, Clock(2.0 * p));\n  Real y3 = /*Real*/(sample(1, Clock()));\n"
                           "  Boolean c1 = sample(0.0, 1.0);\n  Real y4 = /*Real*/(hold(k));\n  Real y5[1];\n  Real y5[2];\n"
                           "  Real y6 = backSample(x, 2, 1);\n  final parameter Integer m2 = 2;\n"
                           "  Real y7 = shiftSample(x, 1, 4);\n  Real y8 = subSample(x, 0) + superSample(x, 3);\n"
                           "  Real y9 = interval() + interval(x);\n  Boolean c2 = firstTick();\n"
                           "  Real y10 = sample(x, Clock(n, 1)) + sample(x, Clock(b, 0.0)) + sample(x, Clock(Clock(1, 10), \"ImplicitEuler\"));\n"
                           "  Real y11 = sample(x, subSample(Clock(b, 0.5), 2));\nequation\n  z = {time, 1.0};\n"
                           "  y5 = noClock(z);\nend SYT1;\n"));
  // 钟控 when：体是任意方程（含 if 方程、非赋值形的等式）；previous 的数组实参按元素向量化。期望 = omc 1.25.0 实跑 SYT2。
  YMC_CHECK_EQ(flatten_src("model SYT2\n  Real x = time;\n  Real y(start = 1);\n  Real w;\n  Real v[2];\n  Real u1;\n"
                           "  Real u2;\nequation\n  when Clock(0.1) then\n    y = 2 * previous(y);\n    if firstTick() then\n"
                           "      w = 0;\n    else\n      w = previous(w) + interval();\n    end if;\n    u1 + u2 = sample(x);\n"
                           "    u1 - u2 = 0;\n  end when;\n  v = previous(v);\nend SYT2;\n"),
               std::string("class SYT2\n  Real x = time;\n  Real y(start = 1.0);\n  Real w;\n  Real v[1];\n  Real v[2];\n"
                           "  Real u1;\n  Real u2;\nequation\n  when Clock(0.1) then\n    y = 2.0 * previous(y);\n"
                           "    if firstTick() then\n      w = 0.0;\n    else\n      w = previous(w) + interval();\n"
                           "    end if;\n    u1 + u2 = sample(x, Clock());\n    u1 - u2 = 0.0;\n  end when;\n"
                           "  v = array(previous(v[$i1]) for $i1 in 1:2);\nend SYT2;\n"));
  // 未做 / omc 报错 → SKIP：Clock 首参可化简（omc 原样打 `Clock(1.0 / 3.0)`，SYS1）、返回数组的算子嵌在运算里（omc 整体打
  // `hold(z) * 2.0`，SYS2）、previous 的实参不是组件引用（omc 报错，SYS3）、钟控 when 带 elsewhen（omc 报错，SYS4）。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"model SYS1\n  Real x = time;\n  Real y = sample(x, Clock(1 / 3));\nend SYS1;\n",
            "Clock argument not in simplified form"},
           {"model SYS2\n  Real z[2] = {time, 1};\n  Real y[2] = 2 * hold(z);\nend SYS2;\n",
            "array-valued clocked operator inside expression"},
           {"model SYS3\n  Real x = time;\n  Real y;\nequation\n  y = previous(2 * x);\nend SYS3;\n",
            "previous of non-component expression"},
           {"model SYS4\n  Real y;\nequation\n  when Clock(0.1) then\n    y = 1;\n  elsewhen Clock(0.2) then\n"
            "    y = 2;\n  end when;\nend SYS4;\n",
            "clocked when with elsewhen"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_reductions) {
  // 归约式（simplifyReduction，v1.25.0 NFSimplifyExp.mo:652-705）：sum / product 能展开就展开（最后写的迭代变量在最外层）、
  // 范围求不出则保留（多个迭代变量打成嵌套）；min / max 保留成归约式，范围打成求值后的字面量；单迭代变量长度 1 → 体代入、
  // 长度 0 → 缺省值；变率 ≤ 结构参数的求值（m8：n 在 s2 的绑定里已被标结构）。范围里的参数是定型期标的：m 不 final、m2
  // final。期望 = omc 1.25.0 实跑 RDT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model RDT1\n  parameter Integer n = 3;\n  parameter Integer m = n;\n  Real x[3] = {time, 1, 2};\n"
                           "  Real t = time;\n  Real s1 = sum(x[i] * x[j] for i in 1:2, j in 1:3);\n"
                           "  Real s2 = sum(i * 0.5 for i in 1:n);\n  parameter Integer m2 = n;\n"
                           "  Real s3 = sum(x[i] for i in 1:integer(time));\n"
                           "  Real s4 = sum(a * b for a in 1:integer(time), b in 1:4);\n  Real s5 = sum(v * v for v in x);\n"
                           "  Real s6 = sum(i * t for i in 1:0);\n  Real m1 = max(x[i] for i in 1:n);\n"
                           "  Real m3 = max(x[i] * x[j] for i in 1:2, j in 1:3);\n  Real m4 = max(t * v for v in {1.5, 2.5});\n"
                           "  Real m5 = max(x[i] for i in 2:2);\n  Real m6 = min(r * 2 for r in {1.0, 2, 3, 4});\n"
                           "  Real m7 = max(x[i] for i in 1:0);\n  Integer m8 = max(i for i in 1:n);\n"
                           "  Boolean b1 = min(x[i] > 0 for i in 1:3);\n  Boolean b2 = max(b for b in true:false);\nend RDT1;\n"),
               std::string("class RDT1\n  final parameter Integer n = 3;\n  parameter Integer m = 3;\n  Real x[1];\n"
                           "  Real x[2];\n  Real x[3];\n  Real t = time;\n"
                           "  Real s1 = x[1] * x[1] + x[2] * x[1] + x[1] * x[2] + x[2] * x[2] + x[1] * x[3] + x[2] * x[3];\n"
                           "  Real s2 = 3.0;\n  final parameter Integer m2 = 3;\n"
                           "  Real s3 = sum(x[i] for i in 1:integer(time));\n"
                           "  Real s4 = /*Real*/(sum(sum(a * b for a in 1:integer(time)) for b in 1:4));\n"
                           "  Real s5 = x[1] * x[1] + x[2] * x[2] + x[3] * x[3];\n  Real s6 = 0.0;\n"
                           "  Real m1 = max(x[i] for i in 1:3);\n  Real m3 = max(max(x[i] * x[j] for i in 1:2) for j in 1:3);\n"
                           "  Real m4 = max(t * v for v in {1.5, 2.5});\n  Real m5 = x[2];\n  Real m6 = 2.0;\n"
                           "  Real m7 = -8.777798510069901e304;\n  Integer m8 = 3;\n"
                           "  Boolean b1 = min(x[i] > 0.0 for i in 1:3);\n  Boolean b2 = false;\nequation\n"
                           "  x = {time, 1.0, 2.0};\nend RDT1;\n"));
}

YMC_TEST(flatten_min_max_forms) {
  // min / max 的数组实参（整体 / 切片引用原样、算术式展开成字面量、恰一个元素取元素、字面量求值、空数组取类型极值）与
  // Boolean / 枚举的两个标量实参。期望 = omc 1.25.0 实跑 RDT2（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model RDT2\n  type E = enumeration(one, two, three);\n  Real x[3] = {time, 1, 2};\n"
                           "  Integer k[3] = {1, 2, integer(time)};\n  Boolean bb[2] = {time > 1, true};\n"
                           "  E ee[2] = {E.one, E.three};\n  Real t = time;\n  Real a1 = max(x);\n"
                           "  Real a2 = max(x[1:2]) + min({t, 1});\n  Real a3 = max(2 * x);\n  Real a4 = max({t});\n"
                           "  Real a5 = min(fill(1.5, 0));\n  Real a6 = max({{1, 2}, {3, 4}});\n  Integer i1 = max(k);\n"
                           "  Boolean b1 = max(bb);\n  Boolean b2 = min(bb[1], false);\n  Boolean b3 = max(true, false);\n"
                           "  Boolean b4 = min(not bb);\n  E e1 = max(ee);\n  E e2 = min(ee[1], E.two);\n"
                           "  E e3 = max(E.one, E.three);\nend RDT2;\n"),
               std::string("class RDT2\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Integer k[1];\n  Integer k[2];\n"
                           "  Integer k[3];\n  Boolean bb[1];\n  Boolean bb[2];\n  enumeration(one, two, three) ee[1];\n"
                           "  enumeration(one, two, three) ee[2];\n  Real t = time;\n  Real a1 = max(x);\n"
                           "  Real a2 = max(x[1:2]) + min({t, 1.0});\n  Real a3 = max({2.0 * x[1], 2.0 * x[2], 2.0 * x[3]});\n"
                           "  Real a4 = t;\n  Real a5 = 8.777798510069901e304;\n  Real a6 = 4.0;\n  Integer i1 = max(k);\n"
                           "  Boolean b1 = max(bb);\n  Boolean b2 = min(bb[1], false);\n  Boolean b3 = true;\n"
                           "  Boolean b4 = min(not bb);\n  enumeration(one, two, three) e1 = max(ee);\n"
                           "  enumeration(one, two, three) e2 = min(ee[1], E.two);\n"
                           "  enumeration(one, two, three) e3 = E.three;\nequation\n  x = {time, 1.0, 2.0};\n"
                           "  k = {1, 2, integer(time)};\n  bb = {time > 1.0, true};\n  ee = {E.one, E.three};\nend RDT2;\n"));
  // 未做 / 不跟 → SKIP：Integer 的极值随 omc 构建的整数位宽而变（本机 1.25.0 打 -1073741823，RDS1）；`vector(x)` 这类 omc
  // 留调用不展开的实参（RDS2）；记录构造绑定里依赖别的字段的缺省（omc 代入实参式 `t.b = 2.0 * p`，RDS3）；Real 范围（RDS4）。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"model RDS1\n  Integer y = max(zeros(0));\nend RDS1;\n",
            "Integer extreme of empty min/max (platform-dependent in omc)"},
           {"model RDS2\n  Real x[3] = {time, 1, 2};\n  Real y = max(vector(x));\nend RDS2;\n",
            "min/max array argument form"},
           {"record R\n  parameter Real a;\n  parameter Real b = 2 * a;\nend R;\nmodel RDS3\n"
            "  parameter Real p = 4;\n  parameter R t = R(a = p);\nend RDS3;\n",
            "record constructor default depends on other fields"},
           {"model RDS4\n  Real y = sum(i for i in 1.0:3.0);\nend RDS4;\n",
            "reduction range"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_structural_array_parameters) {
  // 内建类型的参数数组在结构语境里被引用（下标、维、条件组件的条件、if 方程的条件）：整个数组求值，标记是组件级的——所有
  // 元素一起 final（idx / n / b / k / m），绑定里的依赖不连带（k0），没进结构语境的不动（u）。期望 = omc 1.25.0 实跑 APT1
  //（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model APT1\n  parameter Integer idx[2] = {2, 1};\n  parameter Integer n[2] = {2, 3};\n"
                           "  parameter Boolean b[2] = {true, false};\n  parameter Integer k0 = 2;\n"
                           "  parameter Integer k[2] = {1, k0};\n  parameter Integer m[2, 2] = {{1, 2}, {3, 1}};\n"
                           "  parameter Integer u[2] = {1, 2};\n  Real x[3] = {time, 1, 2};\n  Real z[n[1]];\n"
                           "  Real y = x[idx[1]] + x[k[2]] + x[m[2, 1]] + u[1];\n  Real w if b[1];\n  Real v;\nequation\n"
                           "  z = {time, 1};\n  w = 1;\n  if b[2] then\n    v = 1;\n  else\n    v = 2;\n  end if;\nend APT1;\n"),
               std::string("class APT1\n  final parameter Integer idx[1] = 2;\n  final parameter Integer idx[2] = 1;\n"
                           "  final parameter Integer n[1] = 2;\n  final parameter Integer n[2] = 3;\n"
                           "  final parameter Boolean b[1] = true;\n  final parameter Boolean b[2] = false;\n"
                           "  parameter Integer k0 = 2;\n  final parameter Integer k[1] = 1;\n"
                           "  final parameter Integer k[2] = 2;\n  final parameter Integer m[1,1] = 1;\n"
                           "  final parameter Integer m[1,2] = 2;\n  final parameter Integer m[2,1] = 3;\n"
                           "  final parameter Integer m[2,2] = 1;\n  parameter Integer u[1] = 1;\n"
                           "  parameter Integer u[2] = 2;\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real z[1];\n"
                           "  Real z[2];\n  Real y = x[2] + x[2] + x[3] + /*Real*/(u[1]);\n  Real w;\n  Real v;\nequation\n"
                           "  x = {time, 1.0, 2.0};\n  z[1] = time;\n  z[2] = 1.0;\n  w = 1.0;\n  v = 2.0;\nend APT1;\n"));
  // 结构参数数组的整体引用：omc 代入整个数组的值（APS2 `y = {1, 2}`；62i 起照做，期望 = omc 1.25.0 实跑，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("model APS2\n  parameter Integer k[2] = {1, 2};\n  Real z[k[2]];\n"
                           "  Integer y[2] = k;\nend APS2;\n"),
               std::string("class APS2\n  final parameter Integer k[1] = 1;\n  final parameter Integer k[2] = 2;\n"
                           "  Real z[1];\n  Real z[2];\n  Integer y[1];\n  Integer y[2];\nequation\n  y = {1, 2};\n"
                           "end APS2;\n"));
}

YMC_TEST(flatten_boolean_absorption_and_model_folding) {
  // and / or 的吸收律（simplifyLogicBinaryAnd / Or，NFSimplifyExp.mo:1178-1242 v1.25.0），标量化出来的不再化简
  //（`v[1] = true or bb[1]`）；方程 assert 条件恒真 → 删（NFSimplifyModel.mo:211-215），语句的保留；结构参数代入后成字面量
  // 的 if 条件选支（方程 / 语句）；体被删空的末尾分支删去。期望 = omc 1.25.0 实跑 APT2（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model APT2\n  parameter Boolean pb = true;\n  parameter Integer n = 2;\n  Real z[n];\n"
                           "  Real g if pb;\n  Boolean bb[2] = {time > 1, true};\n  Boolean y1 = true or bb[1];\n"
                           "  Boolean y2 = bb[1] or false;\n  Boolean y3 = bb[1] and pb;\n"
                           "  Boolean y4 = not (bb[1] and false);\n  Boolean v[2];\n  Real r;\n  Real s;\nequation\n"
                           "  z = {time, 1};\n  g = 1;\n  v = {true, false} or bb;\n  assert(n > 0, \"m\");\n"
                           "  assert(time > 0 and n > 0, \"m2\");\n  if bb[1] and pb then\n    r = 1;\n  else\n    r = 2;\n"
                           "  end if;\n  if bb[1] or pb then\n    assert(true, \"m3\");\n  end if;\n  if bb[1] then\n"
                           "    assert(time > 0, \"m4\");\n  elseif bb[2] then\n    assert(true, \"m5\");\n  end if;\n"
                           "algorithm\n  if bb[2] and not pb then\n    s := 1;\n  elseif bb[1] then\n    s := 3;\n  else\n"
                           "    s := 2;\n  end if;\n  assert(true, \"m6\");\nend APT2;\n"),
               std::string("class APT2\n  final parameter Boolean pb = true;\n  final parameter Integer n = 2;\n  Real z[1];\n"
                           "  Real z[2];\n  Real g;\n  Boolean bb[1];\n  Boolean bb[2];\n  Boolean y1 = true;\n"
                           "  Boolean y2 = bb[1];\n  Boolean y3 = bb[1];\n  Boolean y4 = true;\n  Boolean v[1];\n"
                           "  Boolean v[2];\n  Real r;\n  Real s;\nequation\n  bb = {time > 1.0, true};\n  z[1] = time;\n"
                           "  z[2] = 1.0;\n  g = 1.0;\n  v[1] = true or bb[1];\n  v[2] = false or bb[2];\n"
                           "  assert(time > 0.0, \"m2\");\n  if bb[1] then\n    r = 1.0;\n  else\n    r = 2.0;\n  end if;\n"
                           "  if bb[1] then\n    assert(time > 0.0, \"m4\");\n  end if;\nalgorithm\n  if bb[1] then\n"
                           "    s := 3.0;\n  else\n    s := 2.0;\n  end if;\n  assert(true, \"m6\");\nend APT2;\n"));
  // omc 的标量化把体为空的分支全删掉（scalarizeIfEquation / scalarizeIfStatement，NFScalarize.mo:372-399、461-488 v1.25.0），
  // 其后还有非空分支时条件的语义就变了（APS1：`if b then else assert(…)` 整条消失）——omc 的错，不跟；if 语句全空时 omc 删掉
  // 语句却留下空的 `algorithm`（APS8）——未做。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"model APS1\n  Real y = time;\n  Boolean b = time > 1;\nequation\n  if b then\n  else\n"
            "    assert(y > 0, \"n\");\n  end if;\nend APS1;\n",
            "empty if branch before a non-empty one (omc drops it)"},
           {"model APS8\n  Real y = time;\n  Boolean b = time > 1;\nalgorithm\n  if b then\n  end if;\nend APS8;\n",
            "if statement with only empty branches"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_reduction_range_evaluation) {
  // sum / product 展开前 omc 对各范围做 Ceval.evalExp（foldReduction2，NFExpression.mo:5885 v1.25.0）：参数取值代入、不打结构
  // 标记（y1 / y2 / y4 / y10，k、q 不 final），变量 / 迭代变量的裸引用原样（y9 / y11）；元素是含变量的运算式或转型时求值
  // 失败 → 整个保留，范围打成化简后的形态（y5 / y6 / y8）。min / max 不展开（y3 / y7）。期望 = omc 1.25.0 实跑 APT3
  //（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model APT3\n  parameter Integer k[2] = {1, 2};\n  parameter Real q[2] = {1, 2};\n"
                           "  Integer m = integer(time);\n  Real x[3] = {time, 1, 2};\n  Real t = time;\n"
                           "  Real y1 = sum(x[i] for i in k);\n  Real y2 = sum(v * t for v in q);\n"
                           "  Real y3 = max(x[i] for i in k);\n  Real y4 = sum(v * t for v in {q[1] * 2, t});\n"
                           "  Real y5 = sum(v * t for v in {2 * t, 3});\n  Real y6 = sum(v * t for v in 2 * x);\n"
                           "  Real y7 = max(v * t for v in {m, 2.5});\n"
                           "  Real y8 = sum(v * w for v in {t, 2}, w in {2 * t, 3});\n  Real y9 = sum(x[i] for i in {m, 2});\n"
                           "  Real y10 = product(v * t for v in {q[1], 2 * q[2]});\n"
                           "  Real y11 = max(sum(x[i] for i in {j, 2}) for j in 1:2);\nend APT3;\n"),
               std::string("class APT3\n  parameter Integer k[1] = 1;\n  parameter Integer k[2] = 2;\n"
                           "  parameter Real q[1] = 1.0;\n  parameter Real q[2] = 2.0;\n  Integer m = integer(time);\n"
                           "  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real t = time;\n  Real y1 = x[1] + x[2];\n"
                           "  Real y2 = t + 2.0 * t;\n  Real y3 = max(x[i] for i in k);\n  Real y4 = 2.0 * t + t * t;\n"
                           "  Real y5 = sum(v * t for v in {2.0 * t, 3.0});\n"
                           "  Real y6 = sum(v * t for v in {2.0 * x[1], 2.0 * x[2], 2.0 * x[3]});\n"
                           "  Real y7 = max(v * t for v in {/*Real*/(m), 2.5});\n"
                           "  Real y8 = sum(sum(v * w for v in {t, 2.0}) for w in {2.0 * t, 3.0});\n  Real y9 = x[m] + x[2];\n"
                           "  Real y10 = t * 4.0 * t;\n  Real y11 = max(x[j] + x[2] for j in 1:2);\nequation\n"
                           "  x = {time, 1.0, 2.0};\nend APT3;\n"));
  // 不跟 / 未做 → SKIP：含变量的调用元素（omc 对 `der(t)` 求出 0、`noEvent(t)` / `pre(y)` 取实参，APS3 的 `sin(t)` 是保留）；
  // Real 迭代变量作下标（omc 报错，APS4）；fill 的维 / 归约的范围 / 推导式的范围里有迭代变量——omc 定型时循环还没展开，不求值
  // 也不标结构参数（APS5 `x[:,1:1] = fill(0.0, 3, n + 1 - 1)`、APS6 `sum(x[j] for j in i:n)`，n 都不 final），推导式的直接报
  // 内部错（APS7）。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"model APS3\n  Real t = time;\n  Real y = sum(v * t for v in {sin(t), 2});\nend APS3;\n",
            "reduction range element with call"},
           {"model APS4\n  Real x[3] = {time, 1, 2};\n  Integer m = integer(time);\n"
            "  Real y = sum(x[i] for i in {m, 2.5});\nend APS4;\n",
            "Real subscript"},
           {"model APS5\n  parameter Integer n = 1;\n  Real x[3, 3];\nequation\n  for i in 1:3 loop\n"
            "    x[:, i:i] = zeros(3, n + i - i);\n  end for;\nend APS5;\n",
            "fill dimension depends on iterator"},
           {"model APS6\n  parameter Integer n = 3;\n  Real x[3] = {time, 1, 2};\n"
            "  Real s = max(sum(x[j] for j in i:n) for i in 1:2);\nend APS6;\n",
            "reduction range depends on iterator"},
           {"model APS7\n  Real x[3];\nequation\n  for i in 1:1 loop\n    x = {time * j for j in 1:i + 2};\n  end for;\n"
            "end APS7;\n",
            "comprehension range depends on iterator"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_dims_from_size_of_colon_arrays) {
  // 维里的 size(a, k)，a 的第 k 维写的是 `:`（由绑定推断）：取 a 已发射的形状；size 只看类型，不把 a 标成结构参数（a / A / B
  // 都不 final，n 作了维才 final）。本数组的别的维可以引用自己的 `:` 维（`A[:, size(A, 1)]`）。下标里的 `end`（`b[end]`，
  // b 成结构参数）、类型名的 size（`size(E, 1)`、`size(Boolean, 1)`）。期望 = omc 1.25.0 实跑 DMT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model DMT1\n  type E = enumeration(one, two, three);\n  parameter Real a[:] = {1, 1};\n"
                           "  parameter Real A[:, size(A, 1)] = {{1, 0}, {0, 1}};\n"
                           "  parameter Real B[size(A, 1), :] = {{1}, {1}};\n  parameter Integer b[:] = {3, 2};\n"
                           "  parameter Integer n = size(a, 1) - 1;\n  Real x[size(a, 1)];\n  Real xn[n + 1];\n"
                           "  Real u[size(B, 2)];\n  Real c[b[end]];\n  Integer ints[size(E, 1)];\n"
                           "  Integer nb = size(Boolean, 1);\nend DMT1;\n"),
               std::string("class DMT1\n  parameter Real a[1] = 1.0;\n  parameter Real a[2] = 1.0;\n"
                           "  parameter Real A[1,1] = 1.0;\n  parameter Real A[1,2] = 0.0;\n  parameter Real A[2,1] = 0.0;\n"
                           "  parameter Real A[2,2] = 1.0;\n  parameter Real B[1,1] = 1.0;\n  parameter Real B[2,1] = 1.0;\n"
                           "  final parameter Integer b[1] = 3;\n  final parameter Integer b[2] = 2;\n"
                           "  final parameter Integer n = 1;\n  Real x[1];\n  Real x[2];\n  Real xn[1];\n  Real xn[2];\n"
                           "  Real u[1];\n  Real c[1];\n  Real c[2];\n  Integer ints[1];\n  Integer ints[2];\n"
                           "  Integer ints[3];\n  Integer nb = 2;\nend DMT1;\n"));
  // `:` 维由外面的修饰给（PolynomialEvaluator 的写法），组件是继承来的（声明在基类里，按求值帧的实例找），组件数组的 each
  // 修饰。期望 = omc 1.25.0 实跑 DMT2。
  YMC_CHECK_EQ(flatten_src("model PolyBase\n  parameter Real c[:];\n  Real y[size(c, 1)];\nend PolyBase;\nmodel Poly\n"
                           "  extends PolyBase;\n  parameter Integer n = size(c, 1) - 1;\n  Real xp[n + 1];\nend Poly;\n"
                           "model DMT2\n  Poly p(c = {1, 2, 3});\n  Poly q[2](each c = {1, 2});\nend DMT2;\n"),
               std::string("class DMT2\n  parameter Real p.c[1] = 1.0;\n  parameter Real p.c[2] = 2.0;\n"
                           "  parameter Real p.c[3] = 3.0;\n  Real p.y[1];\n  Real p.y[2];\n  Real p.y[3];\n"
                           "  final parameter Integer p.n = 2;\n  Real p.xp[1];\n  Real p.xp[2];\n  Real p.xp[3];\n"
                           "  parameter Real q[1].c[1] = 1.0;\n  parameter Real q[1].c[2] = 2.0;\n  Real q[1].y[1];\n"
                           "  Real q[1].y[2];\n  final parameter Integer q[1].n = 1;\n  Real q[1].xp[1];\n  Real q[1].xp[2];\n"
                           "  parameter Real q[2].c[1] = 1.0;\n  parameter Real q[2].c[2] = 2.0;\n  Real q[2].y[1];\n"
                           "  Real q[2].y[2];\n  final parameter Integer q[2].n = 1;\n  Real q[2].xp[1];\n  Real q[2].xp[2];\n"
                           "end DMT2;\n"));
  // 被引用的数组声明在引用处之后：`:` 维按它绑定的书写形状取（62h 起，见 colon_dim_size；期望 = omc 1.25.0 实跑，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("model DMS3\n  Real y[size(a, 1)];\n  parameter Real a[:] = {1, 1};\nend DMS3;\n"),
               std::string("class DMS3\n  Real y[1];\n  Real y[2];\n  parameter Real a[1] = 1.0;\n  parameter Real a[2] = 1.0;\n"
                           "end DMS3;\n"));
}

YMC_TEST(flatten_whole_range_slices_and_when_arrays) {
  // 恰好是 `1:dim`（无步长 / 步长 1）的范围切片等价于 `:`（Subscript.simplifySlice，NFSubscript.mo:893-913 v1.25.0）：
  // y1 / y2 / y5 / y7；部分范围、下标数组原样（y3 / y4）；下标数少于维数时 omc 拿末尾的维去比（simplifyList 的 List.lastN），
  // `m[1:2]` 对 3 不相等、原样（y6）。when 方程体里的数组等式逐元素标量化（scalarizeWhenEquation，NFScalarize.mo:401-424）。
  // 期望 = omc 1.25.0 实跑 DMT3（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model DMT3\n  Real x[3] = {time, 1, 2};\n  Real m[2, 3] = {{time, 1, 2}, {3, 4, 5}};\n"
                           "  Real y1[3] = x[1:3];\n  Real y2[3] = x[1:end];\n  Real y3[2] = x[2:3];\n"
                           "  Real y4[3] = x[{1, 2, 3}];\n  Real y5[2, 3] = m[1:2, 1:3];\n  Real y6[2, 3] = m[1:2];\n"
                           "  Real y7 = max(x[1:3]);\n  discrete Real d[3];\n  discrete Real e[2];\nequation\n"
                           "  when time > 1 then\n    d = pre(d) + x[1:3];\n    e = {time, 2};\n  end when;\nend DMT3;\n"),
               std::string("class DMT3\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real m[1,1];\n  Real m[1,2];\n"
                           "  Real m[1,3];\n  Real m[2,1];\n  Real m[2,2];\n  Real m[2,3];\n  Real y1[1];\n  Real y1[2];\n"
                           "  Real y1[3];\n  Real y2[1];\n  Real y2[2];\n  Real y2[3];\n  Real y3[1];\n  Real y3[2];\n"
                           "  Real y4[1];\n  Real y4[2];\n  Real y4[3];\n  Real y5[1,1];\n  Real y5[1,2];\n  Real y5[1,3];\n"
                           "  Real y5[2,1];\n  Real y5[2,2];\n  Real y5[2,3];\n  Real y6[1,1];\n  Real y6[1,2];\n"
                           "  Real y6[1,3];\n  Real y6[2,1];\n  Real y6[2,2];\n  Real y6[2,3];\n  Real y7 = max(x[:]);\n"
                           "  discrete Real d[1];\n  discrete Real d[2];\n  discrete Real d[3];\n  discrete Real e[1];\n"
                           "  discrete Real e[2];\nequation\n  x = {time, 1.0, 2.0};\n"
                           "  m = {{time, 1.0, 2.0}, {3.0, 4.0, 5.0}};\n  y1 = x[:];\n  y2 = x[:];\n  y3 = x[2:3];\n"
                           "  y4 = x[{1, 2, 3}];\n  y5 = m[:,:];\n  y6 = m[1:2];\n  when time > 1.0 then\n"
                           "    d[1] = pre(d[1]) + x[1];\n    d[2] = pre(d[2]) + x[2];\n    d[3] = pre(d[3]) + x[3];\n"
                           "    e[1] = time;\n    e[2] = 2.0;\n  end when;\nend DMT3;\n"));
  // 不跟 / 未做 → SKIP：下标数少于维数的部分切片恰好等于末尾维的大小，omc 把它打成 `:`（DMS1 `y = m2[:]`，含义变了）；when
  // 体里含数组调用的等式整体保留（DMS2 `y = array(x[i] * /*Real*/(i) for i in 1:3)`）。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"model DMS1\n  Real m2[3, 2] = {{time, 1}, {2, 3}, {4, 5}};\n  Real y[2, 2] = m2[1:2];\nend DMS1;\n",
            "partial slice printed as whole by omc"},
           {"model DMS2\n  Real x[3] = {time, 1, 2};\n  discrete Real y[3];\nequation\n  when time > 1 then\n"
            "    y = {x[i] * i for i in 1:3};\n  end when;\nend DMS2;\n",
            "array call in when body"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_mixed_range_types_and_invalid_pow) {
  // 数值范围的配型（typeRange，NFTyping.mo:2371-2412 v1.25.0）：起点-终点先配（Integer 的一方转 Real：`1.0:n`、
  // `/*Real*/(k + 1):n`）；有步长时再拿起点的原类型与步长配、范围类型取这一步的结果——起点、步长都是 Integer 而终点是 Real
  // 时起点已转成 Real、范围类型却是 Integer（`for i in 1.0:2:n`，体里 `/*Real*/(i)`）。期望 = omc 1.25.0 实跑 RGT1
  //（2026-10-02；出自 testsuite simulation/modelica 的 Algorithm.mo）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Integer k;\n  input Real n;\n  output Real y;\nalgorithm\n  y := 1;\n"
                           "  for i in 1:n loop\n    y := y * i;\n  end for;\n  for i in 1:2:n loop\n    y := y * i;\n"
                           "  end for;\n  for i in k + 1:n loop\n    y := y + i;\n  end for;\n  for i in 1:0.5:2 loop\n"
                           "    y := y + i;\n  end for;\nend f;\nmodel RGT1\n  Real t = time;\n  Real y = f(1, t);\n  Real z;\n"
                           "algorithm\n  z := 0;\n  for i in 1:t loop\n    z := z + i;\n  end for;\nend RGT1;\n"),
               std::string("function f\n  input Integer k;\n  input Real n;\n  output Real y;\nalgorithm\n  y := 1.0;\n"
                           "  for i in 1.0:n loop\n    y := y * i;\n  end for;\n  for i in 1.0:2:n loop\n"
                           "    y := y * /*Real*/(i);\n  end for;\n  for i in /*Real*/(k + 1):n loop\n    y := y + i;\n"
                           "  end for;\n  for i in 1.0:0.5:2.0 loop\n    y := y + i;\n  end for;\nend f;\n\nclass RGT1\n"
                           "  Real t = time;\n  Real y = f(1, t);\n  Real z;\nalgorithm\n  z := 0.0;\n  for i in 1.0:t loop\n"
                           "    z := z + i;\n  end for;\nend RGT1;\n"));
  // 负底数配非整数指数：化简不求值、原样留下（Ceval.evalBinaryPow，NFCeval.mo:1095-1111 v1.25.0），ymc 此前折成 NaN。
  // 期望 = omc 1.25.0 实跑 PWT1。
  YMC_CHECK_EQ(flatten_src("model PWT1\n  constant Real c = -1;\n  Real t = time;\n  Real y1 = (-1.0)^0.5 * t;\n"
                           "  Real y2 = (-8.0)^(1/3);\n  Real y3 = (-2.0)^2;\n  Real y4 = c^0.5;\n"
                           "  Real y5[2] = {-1.0, 4.0} .^ 0.5;\n  parameter Real p = (-1)^0.5;\nend PWT1;\n"),
               std::string("class PWT1\n  constant Real c = -1.0;\n  Real t = time;\n  Real y1 = (-1.0) ^ 0.5 * t;\n"
                           "  Real y2 = (-8.0) ^ 0.3333333333333333;\n  Real y3 = 4.0;\n  Real y4 = (-1.0) ^ 0.5;\n"
                           "  Real y5[1];\n  Real y5[2];\n  parameter Real p = (-1.0) ^ 0.5;\nequation\n"
                           "  y5 = {(-1.0) ^ 0.5, 2.0};\nend PWT1;\n"));
  // omc 报错的：constant 绑定里的负底数非整数幂（要求值，INVALID_NEGATIVE_POW，PWS1 / testsuite RealPow2）、0 的负数次幂
  //（PWS2）、extends 的基类名路径上有 replaceable 的类（RPS1 / InvalidReplaceableExtends3）。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"model PWS1\n  constant Real y = (-1.0)^0.5;\nend PWS1;\n", "constant evaluation"},
           {"model PWS2\n  Real y = 0.0^(-1);\nend PWS2;\n", "division by zero in constant"},
           {"model A\n  replaceable model B\n    Real x;\n  end B;\nend A;\nmodel RPS1\n  extends A.B;\nend RPS1;\n",
            "replaceable base class"}}) {   // inst 先拦下：基类名各段须传递地非 replaceable（T-S3-07）
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_subnormal_real_literals) {
  // 次正规数的精度不足 15 位有效数字，最短往返串更短：omc（Ryu）把最小的次正规数打成 `5e-324`，`1.2345678901234e-315` 打成
  // `1.23456789e-315`；正规数照旧（最小正规数 17 位）。期望 = omc 1.25.0 实跑 RLT1（2026-10-02；出自 testsuite
  // openmodelica/parser/RealLiterals1）。
  YMC_CHECK_EQ(flatten_src("model RLT1\n  Real a1 = 1e-310;\n  Real a2 = 1.5e-323;\n  Real a3 = 1e-320;\n"
                           "  Real a4 = 2.2250738585072014e-308;\n  Real a5 = 2.225073858507201e-308;\n"
                           "  Real a6 = 1.2345678901234e-315;\n  Real a7 = -9.8813129168249e-324;\n"
                           "  Real a8 = 4.9406564584124654e-324;\n  Real a9 = 1.7976931348623157e308;\n  Real a10 = 3e-309;\n"
                           "  Real a11 = 1.23e-312 * time;\n  parameter Real p(min = 1e-320) = 2e-322;\nend RLT1;\n"),
               std::string("class RLT1\n  Real a1 = 1e-310;\n  Real a2 = 1.5e-323;\n  Real a3 = 1e-320;\n"
                           "  Real a4 = 2.2250738585072014e-308;\n  Real a5 = 2.225073858507201e-308;\n"
                           "  Real a6 = 1.23456789e-315;\n  Real a7 = -1e-323;\n  Real a8 = 5e-324;\n"
                           "  Real a9 = 1.7976931348623157e308;\n  Real a10 = 3e-309;\n  Real a11 = 1.23e-312 * time;\n"
                           "  parameter Real p(min = 1e-320) = 2e-322;\nend RLT1;\n"));
}

YMC_TEST(flatten_no_return_calls) {
  // 无返回值的调用 `f(args);`：方程（含 for / if / when 体、initial equation）、模型算法语句、函数体语句都留成调用，缺省实参
  // 补齐、注释保留；实参全是常量的纯函数调用被求值掉、整条消失（`f(1.0);`、`f(3.0);`，NFSimplifyModel 的 NORETCALL 支）。
  // 函数解释器认 assert 与调用语句：`ga(2)` 折成 4.0、ga 不发射。期望 = omc 1.25.0 实跑 NRT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Real u;\n  input Real w = 2;\nend f;\nfunction g\n  input Real u;\n"
                           "  output Real v;\nalgorithm\n  f(u);\n  f(1.0);\n  v := u;\nend g;\nfunction ga\n  input Real u;\n"
                           "  output Real v;\nalgorithm\n  assert(u > 0, \"neg\");\n  f(u);\n  v := 2 * u;\nend ga;\n"
                           "model NRT1\n  Real t = time;\n  Real x[2] = {time, 1};\n  Real y = g(t);\n  Real z = ga(2);\n"
                           "  discrete Real d;\nequation\n  f(t);\n  f(1.0);\n  f(t, w = 3);\n  for i in 1:2 loop\n"
                           "    f(x[i]);\n  end for;\n  if t > 1 then\n    f(2 * t);\n  end if;\n  when t > 1 then\n    d = 1;\n"
                           "    f(t);\n  end when;\ninitial equation\n  f(t);\nalgorithm\n  f(t) \"comment\";\n  f(3.0);\n"
                           "end NRT1;\n"),
               std::string("function f\n  input Real u;\n  input Real w = 2.0;\nend f;\n\nfunction g\n  input Real u;\n"
                           "  output Real v;\nalgorithm\n  f(u, 2.0);\n  v := u;\nend g;\n\nclass NRT1\n  Real t = time;\n"
                           "  Real x[1];\n  Real x[2];\n  Real y = g(t);\n  Real z = 4.0;\n  discrete Real d;\n"
                           "initial equation\n  f(t, 2.0);\nequation\n  x = {time, 1.0};\n  f(t, 2.0);\n  f(t, 3.0);\n"
                           "  f(x[1], 2.0);\n  f(x[2], 2.0);\n  if t > 1.0 then\n    f(2.0 * t, 2.0);\n  end if;\n"
                           "  when t > 1.0 then\n    d = 1.0;\n    f(t, 2.0);\n  end when;\nalgorithm\n"
                           "  f(t, 2.0) \"comment\";\nend NRT1;\n"));
  // 常量实参的纯函数调用求值失败（体里的 assert 不成立）时 omc 把调用留下（NRS1 `f(-1.0);`）——ymc 分不清是自己求不出还是
  // omc 也求不出 → SKIP。
  bool uns = false;
  const std::string out = flatten_src("function f\n  input Real u;\nalgorithm\n  assert(u > 0, \"neg\");\nend f;\nmodel NRS1\n"
                                      "equation\n  f(-1.0);\nend NRS1;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:constant no-return call>"));
}

YMC_TEST(flatten_array_expression_call_arguments) {
  // 数组实参里的算术式（维已知）：omc 的化简按元素展开成字面量（`g(2 * x)` → `g({2.0 * x[1], …})`，矩阵、参数数组、等式
  // 右侧同样）；整体 / 切片引用、if 原样（整段切片打成 `x[:]`）。ymc 此前照写 `g(2 * x)`。期望 = omc 1.25.0 实跑 GAT1
  //（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("function g\n  input Real u[3];\n  output Real v;\nalgorithm\n  v := u[1];\nend g;\nfunction gm\n"
                           "  input Real u[:, :];\n  output Real v;\nalgorithm\n  v := u[1, 1];\nend gm;\nmodel GAT1\n"
                           "  Real x[3] = {time, 1, 2};\n  Real m[2, 3] = {{time, 1, 2}, {3, 4, 5}};\n  Real t = time;\n"
                           "  parameter Real p[3] = {1, 2, 3};\n  Real y1 = g(2 * x);\n  Real y2 = g(x + x) + g(-x);\n"
                           "  Real y3 = g(x[1:3]);\n  Real y4 = g({1, 2, 3} * t);\n  Real y5 = gm(2 * m);\n"
                           "  Real y6 = g(2 * p);\n  Real y7 = g(if t > 1 then x else 2 * x);\n  Real y8;\nequation\n"
                           "  y8 = g(x / 2);\nend GAT1;\n"),
               std::string("function g\n  input Real[3] u;\n  output Real v;\nalgorithm\n  v := u[1];\nend g;\n\nfunction gm\n"
                           "  input Real[:, :] u;\n  output Real v;\nalgorithm\n  v := u[1,1];\nend gm;\n\nclass GAT1\n"
                           "  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real m[1,1];\n  Real m[1,2];\n  Real m[1,3];\n"
                           "  Real m[2,1];\n  Real m[2,2];\n  Real m[2,3];\n  Real t = time;\n  parameter Real p[1] = 1.0;\n"
                           "  parameter Real p[2] = 2.0;\n  parameter Real p[3] = 3.0;\n"
                           "  Real y1 = g({2.0 * x[1], 2.0 * x[2], 2.0 * x[3]});\n"
                           "  Real y2 = g({x[1] + x[1], x[2] + x[2], x[3] + x[3]}) + g({-x[1], -x[2], -x[3]});\n"
                           "  Real y3 = g(x[:]);\n  Real y4 = g({t, 2.0 * t, 3.0 * t});\n"
                           "  Real y5 = gm({{2.0 * m[1,1], 2.0 * m[1,2], 2.0 * m[1,3]}, {2.0 * m[2,1], 2.0 * m[2,2], 2.0 * m[2,3]}});\n"
                           "  Real y6 = g({2.0 * p[1], 2.0 * p[2], 2.0 * p[3]});\n"
                           "  Real y7 = g(if t > 1.0 then x else {2.0 * x[1], 2.0 * x[2], 2.0 * x[3]});\n  Real y8;\nequation\n"
                           "  x = {time, 1.0, 2.0};\n  m = {{time, 1.0, 2.0}, {3.0, 4.0, 5.0}};\n"
                           "  y8 = g({x[1] / 2.0, x[2] / 2.0, x[3] / 2.0});\nend GAT1;\n"));
  // `vector(x)` 作实参 omc 留成调用（GAS1 `g(vector(x))`）；实参全是字面量的纯函数调用 ymc 的解释器没求出来（循环超
  // 上限，GAS2）——omc 那边要么求值、要么求值失败照留，分不清 → SKIP。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"function g\n  input Real u[3];\n  output Real v;\nalgorithm\n  v := u[1];\nend g;\nmodel GAS1\n"
            "  Real x[3] = {time, 1, 2};\n  Real y = g(vector(x));\nend GAS1;\n",
            "array builtin kept as call by omc"},
           {"function g\n  input Real u;\n  output Real v;\nalgorithm\n  v := u;\n  while v < 200000 loop\n"
            "    v := v + 1;\n  end while;\nend g;\nmodel GAS2\n  Real y = g(1);\nend GAS2;\n",
            "constant user call not evaluated"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_expression_parenthesization) {
  // 括号规则（Expression.shouldParenthesize :10751-10786 v1.25.0）：优先级相等时右操作数看**它自己**可不可结合——
  // `a < b + c` 不加括号（此前按外层算符判、打成 `a < (b + c)`），`a - (b - c)`、`a / (b / c)`、`a ^ (b ^ c)` 加；
  // 书写出来的双重否定 `not (not p)` → `p`（simplifyLogicUnary）。期望 = omc 1.25.0 实跑 PPT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model PPT1\n  Real a = time;\n  Real b = 2 * time;\n  Real c = 3 * time;\n  Boolean p = time > 1;\n"
                           "  Boolean q = time > 2;\n  Boolean r1 = a < b + c;\n  Boolean r2 = a + b < c - a;\n"
                           "  Boolean r3 = (a < b) == (b < c);\n  Boolean r4 = a * b < c * (a + b);\n"
                           "  Boolean r5 = not (not p);\n  Boolean r6 = not (p and q);\n  Boolean r7 = p and (q or p);\n"
                           "  Boolean r8 = a < (if p then b else c);\n  Real y1 = a - (b - c);\n  Real y2 = a - (b + c);\n"
                           "  Real y3 = a + (b - c);\n  Real y4 = a / (b * c);\n  Real y5 = a * (b / c);\n"
                           "  Real y6 = a / (b / c);\n  Real y7 = a ^ (b ^ c);\n  Real y8 = (a ^ b) ^ c;\n"
                           "  Real y9 = -(a + b);\n  Real y10 = a * (-b);\n  Real y11 = a - (-b);\n  Real y12 = (-a) ^ b;\n"
                           "  Real y13 = a * (b * c);\n  Real y14 = c + (if p then a else b);\n  Real y15 = a - (b - (c - a));\n"
                           "end PPT1;\n"),
               std::string("class PPT1\n  Real a = time;\n  Real b = 2.0 * time;\n  Real c = 3.0 * time;\n"
                           "  Boolean p = time > 1.0;\n  Boolean q = time > 2.0;\n  Boolean r1 = a < b + c;\n"
                           "  Boolean r2 = a + b < c - a;\n  Boolean r3 = a < b == (b < c);\n"
                           "  Boolean r4 = a * b < c * (a + b);\n  Boolean r5 = p;\n  Boolean r6 = not (p and q);\n"
                           "  Boolean r7 = p and (q or p);\n  Boolean r8 = a < (if p then b else c);\n  Real y1 = a - (b - c);\n"
                           "  Real y2 = a - (b + c);\n  Real y3 = a + b - c;\n  Real y4 = a / (b * c);\n  Real y5 = a * b / c;\n"
                           "  Real y6 = a / (b / c);\n  Real y7 = a ^ (b ^ c);\n  Real y8 = (a ^ b) ^ c;\n"
                           "  Real y9 = -(a + b);\n  Real y10 = a * (-b);\n  Real y11 = a + b;\n  Real y12 = (-a) ^ b;\n"
                           "  Real y13 = a * b * c;\n  Real y14 = c + (if p then a else b);\n  Real y15 = a - (b - (c - a));\n"
                           "end PPT1;\n"));
}

YMC_TEST(flatten_function_array_operators) {
  // 函数体里的数组运算。维未知、没能按元素展开的按 DAE 算符类别定形：标量挪到右边（`2 * u` → `u * 2.0`、`a .* u` →
  // `u * a`）、数组 .* / ./ 数组与 .^ 留点号、`a .+ u` → `u .+ a`、`u .- a` → `u .+ (-a)`、标量 .- 数组留点号、不做恒等
  // 化简（`u * 1.0`）、Integer 数组整体转型 `/*Real[:]*/(n)`；不可结合的 `.*` 作右操作数加括号。维已知的按元素展开
  //（`a .- w`、`-w`、`(2 * w) .* w`）。期望 = omc 1.25.0 实跑 FAT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("function h\n  input Real u[:];\n  input Real w[3];\n  input Real a;\n  input Integer n[:];\n"
                           "  input Integer k;\n  output Real o;\nprotected\n  Real v[size(u, 1)];\n  Real v3[3];\n  Real s;\n"
                           "algorithm\n  v := 2 * u;\n  v := a .* u .* u;\n  v := u ./ a;\n  v := a ./ u;\n  v := u .^ 2;\n"
                           "  v := a .+ u;\n  v := u .- a;\n  v := u .- 2;\n  v := k .- u;\n  v := -(2 * u);\n  v := u * 1;\n"
                           "  v := u .* (u .* u);\n  v := n * 2.5;\n  v := n * 2;\n  v3 := a .- w;\n  v3 := -w;\n"
                           "  v3 := (2 * w) .* w;\n  v := (2 * w) .* u;\n  s := sum(u .* (2 * u));\n  s := u * (u .* u);\n"
                           "  o := s;\nend h;\nmodel FAT1\n  Real x[3] = {time, 1, 2};\n"
                           "  Real y = h(x, x, time, {1, 2, 3}, 2);\nend FAT1;\n"),
               std::string("function h\n  input Real[:] u;\n  input Real[3] w;\n  input Real a;\n  input Integer[:] n;\n"
                           "  input Integer k;\n  output Real o;\n  protected Real[size(u, 1)] v;\n  protected Real[3] v3;\n"
                           "  protected Real s;\nalgorithm\n  v := u * 2.0;\n  v := u * a .* u;\n  v := u / a;\n  v := a ./ u;\n"
                           "  v := u .^ 2.0;\n  v := u .+ a;\n  v := u .+ (-a);\n  v := u .+ -2.0;\n  v := /*Real*/(k) .- u;\n"
                           "  v := -u * 2.0;\n  v := u * 1.0;\n  v := u .* (u .* u);\n  v := /*Real[:]*/(n) * 2.5;\n"
                           "  v := /*Real[:]*/(n * 2);\n  v3 := {a - w[1], a - w[2], a - w[3]};\n"
                           "  v3 := {-w[1], -w[2], -w[3]};\n  v3 := {2.0 * w[1] * w[1], 2.0 * w[2] * w[2], 2.0 * w[3] * w[3]};\n"
                           "  v := {2.0 * w[1], 2.0 * w[2], 2.0 * w[3]} .* u;\n  s := sum(u .* u * 2.0);\n  s := u * (u .* u);\n"
                           "  o := s;\nend h;\n\nclass FAT1\n  Real x[1];\n  Real x[2];\n  Real x[3];\n"
                           "  Real y = h(x, x, time, {1, 2, 3}, 2);\nequation\n  x = {time, 1.0, 2.0};\nend FAT1;\n"));
  // 标量与数组之间不带点号的 + / -（omc 报类型错，FAS1）。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"function h\n  input Real u[:];\n  input Real a;\n  output Real v[size(u, 1)];\nalgorithm\n"
            "  v := u + a;\nend h;\nmodel FAS1\n  Real x[3] = {time, 1, 2};\n  Real y[3] = h(x, time);\n"
            "end FAS1;\n",
            "array and scalar operands without elementwise operator"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
  // 已知维的向量积展成积和（同模型语境；59v 时未做、SKIP）。期望 = omc 1.25.0 实跑 FAS2（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("function h\n  input Real w[3];\n  output Real s;\nalgorithm\n  s := w * w;\nend h;\nmodel FAS2\n"
                           "  Real x[3] = {time, 1, 2};\n  Real y = h(x);\nend FAS2;\n"),
               std::string("function h\n  input Real[3] w;\n  output Real s;\nalgorithm\n"
                           "  s := w[1] * w[1] + w[2] * w[2] + w[3] * w[3];\nend h;\n\nclass FAS2\n  Real x[1];\n"
                           "  Real x[2];\n  Real x[3];\n  Real y = h(x);\nequation\n  x = {time, 1.0, 2.0};\nend FAS2;\n"));
}

YMC_TEST(flatten_smooth_semilinear_edge_simplification) {
  // smooth(p, 字面量) → 该字面量（Ceval.evalBuiltinCall 的 smooth 支，NFCeval.mo:1942 v1.25.0）；semiLinear（simplifySemiLinear，
  // NFSimplifyExp.mo:283-303）：x 为零或两个斜率都为零 → 0.0，斜率相同 → `x * m1`（造出来的积不再化简：`a * 1.0`），实参全是
  // 字面量时走 Ceval、求值失败 → 原样；edge 的实参须是变量（括号不算）。期望 = omc 1.25.0 实跑 EST1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model EST1\n  Real a = time;\n  Real b = 2 * time;\n  Real c = 3 * time;\n  parameter Real p = 2;\n"
                           "  Boolean bb = a > 0;\n  Boolean e1 = edge(bb);\n  Boolean e2 = edge((bb));\n"
                           "  Real y1 = smooth(0, 2);\n  Real y2 = smooth(2, 2 * 3 + 1) + smooth(1, a);\n"
                           "  Real y3 = smooth(0, p);\n  Real y4 = semiLinear(a, b, b);\n  Real y5 = semiLinear(a, 1, 1);\n"
                           "  Real y6 = semiLinear(a, 0, 0);\n  Real y7 = semiLinear(0.0, a, b);\n"
                           "  Real y8 = semiLinear(0, 2, 3);\n  Real y9 = semiLinear(a, 2, 3);\n"
                           "  Real y10 = 2 * semiLinear(a, b, b);\n  Real y11 = semiLinear(a + b, c, c) + 1;\n"
                           "  Real y12 = semiLinear(a, p, p);\n  Real y13 = semiLinear(a, 2, 2.0);\n  Real x[2] = {a, b};\n"
                           "  Real z[2] = semiLinear(x, 1, 2);\nend EST1;\n"),
               std::string("class EST1\n  Real a = time;\n  Real b = 2.0 * time;\n  Real c = 3.0 * time;\n"
                           "  parameter Real p = 2.0;\n  Boolean bb = a > 0.0;\n  Boolean e1 = edge(bb);\n"
                           "  Boolean e2 = edge(bb);\n  Real y1 = 2.0;\n  Real y2 = 7.0 + smooth(1, a);\n"
                           "  Real y3 = smooth(0, p);\n  Real y4 = a * b;\n  Real y5 = a * 1.0;\n  Real y6 = 0.0;\n"
                           "  Real y7 = 0.0;\n  Real y8 = semiLinear(0.0, 2.0, 3.0);\n  Real y9 = semiLinear(a, 2.0, 3.0);\n"
                           "  Real y10 = 2.0 * a * b;\n  Real y11 = (a + b) * c + 1.0;\n  Real y12 = a * p;\n"
                           "  Real y13 = a * 2.0;\n  Real x[1];\n  Real x[2];\n  Real z[1];\n  Real z[2];\nequation\n"
                           "  x = {a, b};\n  z = array(semiLinear(x[$i1], 1.0, 2.0) for $i1 in 1:2);\nend EST1;\n"));
  // edge(式子)：omc 报 "First argument to edge … must be a variable"（ESS1）；向量化的 semiLinear 斜率相同时 omc 还化成
  // `array(x[$i1] * c for $i1 in 1:2)`（ESS2，未做）。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"model ESS1\n  Real a = time;\n  Boolean e = edge(a > 0);\nend ESS1;\n", "edge argument must be a variable"},
           {"model ESS2\n  Real c = time;\n  Real x[2] = {time, 1};\n  Real y[2] = semiLinear(x, c, c);\nend ESS2;\n",
            "vectorized semiLinear simplification"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_array_call_and_string_concatenation) {
  // `array(a, b, …)` 是数组构造子的调用写法，同 `{a, b, …}`（MLS §10.4.1；编译期求值与实例化两条路都认）；String + String
  // 的结果是 String，三段以上的拼接照常折叠（此前左操作数被当成 Integer → SKIP）。期望 = omc 1.25.0 实跑 EST2（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model EST2\n  Real a = time;\n  Real b = 2 * time;\n  Real c = 3 * time;\n"
                           "  Real y1[3] = array(a, 1, 2.5);\n  Real y2[2, 2] = array(array(a, b), {c, a});\n"
                           "  Integer n[2] = array(1, 2);\n  constant Integer k[2] = array(2, 3);\n"
                           "  Real y3[k[1]] = array(a, b);\n  Real y4 = sum(array(a, b, c));\n  Real y5[2] = 2 * array(a, b);\n"
                           "  Real y6 = (array(a, b, c))[2];\n  String s1 = \"a\" + \"b\" + \"c\";\n"
                           "  String s2 = String(1) + String(1.5) + String(true);\n  String s3 = \"a\" + String(a) + \"c\";\n"
                           "  parameter String ps = \"x\";\n  String s4 = \"a\" + ps + \"b\";\nend EST2;\n"),
               std::string("class EST2\n  Real a = time;\n  Real b = 2.0 * time;\n  Real c = 3.0 * time;\n  Real y1[1];\n"
                           "  Real y1[2];\n  Real y1[3];\n  Real y2[1,1];\n  Real y2[1,2];\n  Real y2[2,1];\n  Real y2[2,2];\n"
                           "  Integer n[1];\n  Integer n[2];\n  constant Integer k[1] = 2;\n  constant Integer k[2] = 3;\n"
                           "  Real y3[1];\n  Real y3[2];\n  Real y4 = a + b + c;\n  Real y5[1];\n  Real y5[2];\n  Real y6 = b;\n"
                           "  String s1 = \"abc\";\n  String s2 = \"11.5true\";\n"
                           "  String s3 = \"a\" + String(a, 6, 0, true) + \"c\";\n  parameter String ps = \"x\";\n"
                           "  String s4 = \"a\" + ps + \"b\";\nequation\n  y1 = {a, 1.0, 2.5};\n  y2 = {{a, b}, {c, a}};\n"
                           "  n = {1, 2};\n  y3 = {a, b};\n  y5 = {2.0 * a, 2.0 * b};\nend EST2;\n"));
}

YMC_TEST(flatten_synthesized_nodes_keep_arena_references) {
  // flatten 途中往 Inst 的 absyn arena 合成节点（`r.x` 展开成 `{r[1].x, …}`：每个元素一个补了字面量下标的 cref）。arena 一扩容，
  // 上层栈帧持有的 `const Exp&` 就悬垂——修复前这个模型在 ASan 下报 binary() heap-use-after-free。现在构造 Flattener 时
  // 预留、合成只在预留内进行（synth_exp / synth_cref / synth_mod）。期望 = omc 1.25.0 实跑 U1（n = 400，逐字节一致，
  // 2026-10-02；tests/probes/uaf.probes 里是同一份原文），这里按同一形状拼出来。
  const int n = 400;
  const std::string N = std::to_string(n);
  const std::string src = "model U1\n  record R\n    Real x;\n  end R;\n  R r[" + N + "];\n  Real y[" + N +
                          "] = r.x + r.x;\nequation\n  r.x = fill(time, " + N + ");\nend U1;\n";
  auto rx = [](int i) { return "r[" + std::to_string(i) + "].x"; };
  std::string want = "class U1\n";
  for (int i = 1; i <= n; ++i) want += "  Real " + rx(i) + ";\n";
  for (int i = 1; i <= n; ++i) want += "  Real y[" + std::to_string(i) + "];\n";
  want += "equation\n  y = {";
  for (int i = 1; i <= n; ++i) want += (i > 1 ? ", " : "") + rx(i) + " + " + rx(i);
  want += "};\n";
  for (int i = 1; i <= n; ++i) want += "  " + rx(i) + " = time;\n";
  want += "end U1;\n";
  YMC_CHECK_EQ(flatten_src(src.c_str()), want);
}

YMC_TEST(flatten_whole_array_assignment_statements) {
  // 模型算法段的整数组 / 切片赋值：语句不拆（scalarizeStatement 只下到 for / if / when / while 的体里，NFScalarize.mo:443-462
  // v1.25.0），右侧规整成整体保留的形态（whole_array_value，与 Var 数组的绑定方程同一张表）：整取 / 切片引用、if、向量化
  // 调用、Boolean 的 not 原样；算术式逐元素展开（`z * 1` 先展开再逐元素化简 → `{z[1], z[2]}`）；字面量逐叶转型、里面的数组值
  // 元素各自规整；Integer 引用进 Real 目标整体转型 `/*Real[2]*/(m)`；`.^` 两侧都提 Real；带步长的切片照写。
  // 期望 = omc 1.25.0 实跑 WAT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model WAT1\n  Real a = time;\n  Real b = 2 * time;\n  Real z[2] = {a, b};\n"
                           "  Integer m[2] = {1, 2};\n  Boolean j[2] = {a > 0, b > 0};\n  Real y1[2];\n  Real y2[2];\n"
                           "  Real y3[2, 2];\n  Real y4[3];\n  Real s;\n  Integer n[2];\n  Integer c[2];\n  Boolean k[2];\n"
                           "algorithm\n  y1 := {a, 1};\n  y1 := 2 * z;\n  y1 := z * 1;\n  y1 := if a > 0 then z else {1, 2};\n"
                           "  y1 := m;\n  y1 := m .^ 2;\n  y1 := sin(z);\n  y2 := z;\n  y2 := z[2:-1:1];\n  y3 := {z, 2 * z};\n"
                           "  y3 := fill(1, 2, 2);\n  y3[1] := z;\n  y3[:, 2] := m;\n  y4 := 1:3;\n  y4[1:2:3] := {a, b};\n"
                           "  s := z * z;\n  n := 2 * m;\n  k := not j;\n  for i in 1:2 loop\n    y1 := z * i;\n  end for;\n"
                           "  when a > 1 then\n    c := pre(c) + {1, 1};\n  end when;\nend WAT1;\n"),
               std::string("class WAT1\n  Real a = time;\n  Real b = 2.0 * time;\n  Real z[1];\n  Real z[2];\n  Integer m[1];\n"
                           "  Integer m[2];\n  Boolean j[1];\n  Boolean j[2];\n  Real y1[1];\n  Real y1[2];\n  Real y2[1];\n"
                           "  Real y2[2];\n  Real y3[1,1];\n  Real y3[1,2];\n  Real y3[2,1];\n  Real y3[2,2];\n  Real y4[1];\n"
                           "  Real y4[2];\n  Real y4[3];\n  Real s;\n  Integer n[1];\n  Integer n[2];\n  Integer c[1];\n"
                           "  Integer c[2];\n  Boolean k[1];\n  Boolean k[2];\nequation\n  z = {a, b};\n  m = {1, 2};\n"
                           "  j = {a > 0.0, b > 0.0};\nalgorithm\n  y1 := {a, 1.0};\n  y1 := {2.0 * z[1], 2.0 * z[2]};\n"
                           "  y1 := {z[1], z[2]};\n  y1 := if a > 0.0 then z else {1.0, 2.0};\n  y1 := /*Real[2]*/(m);\n"
                           "  y1 := {/*Real*/(m[1]) ^ 2.0, /*Real*/(m[2]) ^ 2.0};\n  y1 := array(sin(z[$i1]) for $i1 in 1:2);\n"
                           "  y2 := z;\n  y2 := z[2:-1:1];\n  y3 := {z, {2.0 * z[1], 2.0 * z[2]}};\n"
                           "  y3 := {{1.0, 1.0}, {1.0, 1.0}};\n  y3[1] := z;\n  y3[:,2] := /*Real[2]*/(m);\n  y4 := 1.0:3.0;\n"
                           "  y4[1:2:3] := {a, b};\n  s := z[1] * z[1] + z[2] * z[2];\n  n := {2 * m[1], 2 * m[2]};\n"
                           "  k := not j;\n  for i in 1:2 loop\n    y1 := {z[1] * /*Real*/(i), z[2] * /*Real*/(i)};\n"
                           "  end for;\n  when a > 1.0 then\n    c := {pre(c[1]) + 1, pre(c[2]) + 1};\n  end when;\nend WAT1;\n"));
  // 形状不符 omc 报类型错（WAS2）。
  bool uns = false;
  YMC_CHECK_EQ(flatten_src("model WAS2\n  Real y[2];\nalgorithm\n  y := {time, 1, 2};\nend WAS2;\n", &uns),
               std::string("<unsupported:algorithm assignment type mismatch>"));
  YMC_CHECK(uns);
}

YMC_TEST(flatten_whole_array_binding_forms) {
  // 绑定里整体保留的数组值（与上面的赋值共用 whole_array_value；此前的潜伏错）：数组运算不在数组层做恒等化简——先展开再
  // 逐元素化简（`z * 1` → `{z[1], z[2]}`、`0 * z` → `{0.0, 0.0}`，此前折成 `z` / 标量 0）；Integer 整取进 Real 数组整体转型
  //（含字面量里的元素、if 的分支）；`.^` 的结果恒为 Real（`sum(m .^ 2)` 不再多套一层转型）；带步长的切片照写的步长 / 终点
  // 打印（`w[1:2:4]`，此前按选中的下标重算成 `1:2:3`）。期望 = omc 1.25.0 实跑 WAT2（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model WAT2\n  Real a = time;\n  Real z[2] = {a, 2 * a};\n  Integer m[2] = {1, 2};\n"
                           "  Real w[4] = {a, a, a, a};\n  Real b1[2] = z * 1;\n  Real b2[2] = 0 * z;\n  Real b3[2] = m;\n"
                           "  Real b4[2] = m .^ 2;\n  Real b5[2, 2] = {m, {a, a}};\n"
                           "  Real b6[2] = if a > 0 then m else {3, 4};\n  Real b7[2] = w[1:2:4];\n  Real b8 = sum(m .^ 2);\n"
                           "end WAT2;\n"),
               std::string("class WAT2\n  Real a = time;\n  Real z[1];\n  Real z[2];\n  Integer m[1];\n  Integer m[2];\n"
                           "  Real w[1];\n  Real w[2];\n  Real w[3];\n  Real w[4];\n  Real b1[1];\n  Real b1[2];\n"
                           "  Real b2[1];\n  Real b2[2];\n  Real b3[1];\n  Real b3[2];\n  Real b4[1];\n  Real b4[2];\n"
                           "  Real b5[1,1];\n  Real b5[1,2];\n  Real b5[2,1];\n  Real b5[2,2];\n  Real b6[1];\n  Real b6[2];\n"
                           "  Real b7[1];\n  Real b7[2];\n  Real b8 = /*Real*/(m[1]) ^ 2.0 + /*Real*/(m[2]) ^ 2.0;\nequation\n"
                           "  z = {a, 2.0 * a};\n  m = {1, 2};\n  w = {a, a, a, a};\n  b1 = {z[1], z[2]};\n  b2 = {0.0, 0.0};\n"
                           "  b3 = /*Real[2]*/(m);\n  b4 = {/*Real*/(m[1]) ^ 2.0, /*Real*/(m[2]) ^ 2.0};\n"
                           "  b5 = {/*Real[2]*/(m), {a, a}};\n  b6 = if a > 0.0 then /*Real[2]*/(m) else {3.0, 4.0};\n"
                           "  b7 = w[1:2:4];\nend WAT2;\n"));
  // 数组 if 的分支是 Boolean 数组的 not / and / or 且等式被逐元素标量化时，omc 打 `(not j)[1]`（WAS1）——不复刻。
  bool uns = false;
  YMC_CHECK_EQ(flatten_src("model WAS1\n  Boolean j[2] = {time > 0, time > 1};\n  Boolean k[2];\nequation\n"
                           "  k = if time > 2 then j else not j;\nend WAS1;\n", &uns),
               std::string("<unsupported:Boolean array operation in scalarized if branch>"));
  YMC_CHECK(uns);
}

YMC_TEST(flatten_when_conditions_and_bodies) {
  // when 条件：任意标量 Boolean 式或条件向量，照化简后的式子打印（WC 探针 c1–c4；此前只收白名单里的几种形态）。
  // when 体：assert / terminate、整个数组 / 元素 / 切片的 reinit（不拆，值规整成整体保留的形态）、数组元素与切片作左侧
  //（各分支解出的变量按元素比）；算法段里的 terminate 语句。期望 = omc 1.25.0 实跑 WCT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model WCT1\n  Real a = time;\n  Real b = 2 * time;\n  Boolean p = a > 1;\n  Boolean q = b > 1;\n"
                           "  Integer k = integer(a);\n  Real x[3](each start = 1);\n  Real y1;\n  Real y2;\n  Real y3;\n"
                           "  Real y4;\n  Real y5;\n  Real z[3];\n  Real w;\nequation\n  der(x) = {-x[1], x[2], 1};\n"
                           "  when p and not q then\n    y1 = a;\n  elsewhen sin(a) > 0 or k > pre(k) then\n    y1 = b;\n"
                           "  end when;\n  when {initial(), a > 1 and b > 2} then\n    y2 = a;\n  end when;\n"
                           "  when terminal() then\n    y3 = a;\n  end when;\n  when not p then\n    y4 = a;\n"
                           "    terminate(\"stop \" + String(a));\n    assert(b > 1 - 1, \"a\" + \"b\");\n  end when;\n"
                           "  when a > 1 then\n    reinit(x, 2 * x);\n    reinit(x[1], 0);\n    reinit(x[2:3], {1, 2});\n"
                           "  end when;\n  when a > 2 then\n    z[1:2] = {a, b};\n    z[3] = 5;\n  elsewhen b > 5 then\n"
                           "    z = {1, 2, 3};\n  end when;\n  when if p then q else not q then\n    y5 = pre(y5) + 1;\n"
                           "  end when;\nalgorithm\n  when change(k) or edge(p) then\n    w := a;\n"
                           "  elsewhen noEvent(a) > 3 then\n    terminate(\"alg\");\n  end when;\nend WCT1;\n"),
               std::string("class WCT1\n  Real a = time;\n  Real b = 2.0 * time;\n  Boolean p = a > 1.0;\n"
                           "  Boolean q = b > 1.0;\n  Integer k = integer(a);\n  Real x[1](start = 1.0);\n"
                           "  Real x[2](start = 1.0);\n  Real x[3](start = 1.0);\n  Real y1;\n  Real y2;\n  Real y3;\n"
                           "  Real y4;\n  Real y5;\n  Real z[1];\n  Real z[2];\n  Real z[3];\n  Real w;\nequation\n"
                           "  der(x[1]) = -x[1];\n  der(x[2]) = x[2];\n  der(x[3]) = 1.0;\n  when p and not q then\n"
                           "    y1 = a;\n  elsewhen sin(a) > 0.0 or k > pre(k) then\n    y1 = b;\n  end when;\n"
                           "  when {initial(), a > 1.0 and b > 2.0} then\n    y2 = a;\n  end when;\n  when terminal() then\n"
                           "    y3 = a;\n  end when;\n  when not p then\n    y4 = a;\n"
                           "    terminate(\"stop \" + String(a, 6, 0, true));\n    assert(b > 0.0, \"a\" + \"b\");\n"
                           "  end when;\n  when a > 1.0 then\n    reinit(x, {2.0 * x[1], 2.0 * x[2], 2.0 * x[3]});\n"
                           "    reinit(x[1], 0.0);\n    reinit(x[2:3], {1.0, 2.0});\n  end when;\n  when a > 2.0 then\n"
                           "    z[1] = a;\n    z[2] = b;\n    z[3] = 5.0;\n  elsewhen b > 5.0 then\n    z[1] = 1.0;\n"
                           "    z[2] = 2.0;\n    z[3] = 3.0;\n  end when;\n  when if p then q else not q then\n"
                           "    y5 = pre(y5) + 1.0;\n  end when;\nalgorithm\n  when change(k) or edge(p) then\n    w := a;\n"
                           "  elsewhen noEvent(a) > 3.0 then\n    terminate(\"alg\");\n  end when;\nend WCT1;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // initial() 不是整个条件 / 条件向量的直接元素：omc 告警（ymc 不产告警）。
           {"model WCS1\n  Real y;\nequation\n  when not initial() then\n    y = time;\n  end when;\nend WCS1;\n", "when condition form"},
           // when 解出的变量是离散的，声明绑定却是连续的：omc 报 "Component a of variability discrete has binding 'time' …"。
           {"model WCS2\n  Real a = time;\n  Real y;\nequation\n  when y > 1 then\n    a = y;\n  end when;\n"
            "end WCS2;\n",
            "when-solved variable with non-constant binding"},
           // 各分支解出的变量按元素比：x[1] 与 x[2] 不是同一组（WhenVariableMismatch2）。
           {"model WCS3\n  Real x[2];\nequation\n  when time > 0 then\n    x[1] = 0;\n  elsewhen time > 1 then\n"
            "    x[2] = 0;\n  end when;\nend WCS3;\n",
            "when branch solved-variable mismatch"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_assert_terminate_messages_and_levels) {
  // 方程里 assert / terminate 的消息 omc 不化简（simplifyEquation 只化简 assert 的条件，NFSimplifyModel.mo:211-215 v1.25.0）：
  // `"a" + "b"`、`String(1 + 2, 0, true)` 原样、常量引用照样内联；语句里的照常化简（"ab"）。显式 level 字面量
  // `AssertionLevel.warning` 照打、`.error` 省略；函数体里的 assert（含形参 / 局部的注释）照发射。
  // 期望 = omc 1.25.0 实跑 WCT2（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model WCT2\n  function f\n    input Real x \"the x\";\n    output Real y \"the y\";\n  protected\n"
                           "    Real t = 2 \"local\";\n  algorithm\n    assert(x > 0, \"neg\" + \"ative\");\n"
                           "    assert(x < 9, \"big\", AssertionLevel.warning);\n    y := x * t;\n  end f;\n  Real a = time;\n"
                           "  Real b = f(a);\n  constant String cs = \"q\";\nequation\n"
                           "  assert(a > 1 - 1, \"x\" + String(1 + 2) + cs);\n"
                           "  assert(b > 0, \"lvl\", AssertionLevel.warning);\n  assert(b > 0, \"err\", AssertionLevel.error);\n"
                           "  terminate(\"a\" + \"b\");\n  if a > 5 then\n    terminate(\"x\" + String(a));\n  end if;\n"
                           "algorithm\n  assert(b > 0, \"a\" + \"b\", AssertionLevel.warning);\n  terminate(\"a\" + \"b\");\n"
                           "end WCT2;\n"),
               std::string("function WCT2.f\n  input Real x \"the x\";\n  output Real y \"the y\";\n"
                           "  protected Real t = 2.0 \"local\";\nalgorithm\n  assert(x > 0.0, \"negative\");\n"
                           "  assert(x < 9.0, \"big\", AssertionLevel.warning);\n  y := x * t;\nend WCT2.f;\n\nclass WCT2\n"
                           "  Real a = time;\n  Real b = WCT2.f(a);\n  constant String cs = \"q\";\nequation\n"
                           "  assert(a > 0.0, \"x\" + String(1 + 2, 0, true) + \"q\");\n"
                           "  assert(b > 0.0, \"lvl\", AssertionLevel.warning);\n  assert(b > 0.0, \"err\");\n"
                           "  terminate(\"a\" + \"b\");\n  if a > 5.0 then\n    terminate(\"x\" + String(a, 6, 0, true));\n"
                           "  end if;\nalgorithm\n  assert(b > 0.0, \"ab\", AssertionLevel.warning);\n  terminate(\"ab\");\n"
                           "end WCT2;\n"));
}

YMC_TEST(flatten_array_constructor_simplification) {
  // 单迭代的数组构造子（simplifyArrayConstructor，NFSimplifyExp.mo:583-637 v1.25.0）：范围只有一个元素 → 展开成 `{体}`
  //（等式里随之逐元素）；体恰是"数组字面量取迭代变量作下标" → 按范围取元素；范围写成数组字面量 / 带步长的照写
  //（此前 `{1, 3}` 打成 `1:3`、`1:2:4` 打成 `1:2:3`）。多个迭代变量的不展开。期望 = omc 1.25.0 实跑 ACT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model ACT1\n  Real a = time;\n  Real x[4] = {a, 2 * a, 3 * a, 4 * a};\n"
                           "  Real c1[1] = {i * a for i in 2:2};\n  Real c2[2] = {i * a for i in 1:2};\n"
                           "  Real c3[2] = {x[i] for i in {1, 3}};\n  Real c4[2] = {x[i] for i in 1:2:4};\n"
                           "  Real c5[3] = {({a, 1, 2})[i] for i in 1:3};\n"
                           "  Real c6[2, 1] = {i * j * a for i in 1:1, j in 1:2};\n  Real c7 = sum(x[i] for i in 3:3);\n"
                           "  Real e1[1];\n  Real e2[2];\n  Real s1[1];\nequation\n  e1 = {x[i] for i in 4:4};\n"
                           "  e2 = {x[i] * 2 for i in {4, 1}};\nalgorithm\n  s1 := {x[i] for i in 2:2};\nend ACT1;\n"),
               std::string("class ACT1\n  Real a = time;\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real x[4];\n"
                           "  Real c1[1];\n  Real c2[1];\n  Real c2[2];\n  Real c3[1];\n  Real c3[2];\n  Real c4[1];\n"
                           "  Real c4[2];\n  Real c5[1];\n  Real c5[2];\n  Real c5[3];\n  Real c6[1,1];\n  Real c6[2,1];\n"
                           "  Real c7 = x[3];\n  Real e1[1];\n  Real e2[1];\n  Real e2[2];\n  Real s1[1];\nequation\n"
                           "  x = {a, 2.0 * a, 3.0 * a, 4.0 * a};\n  c1 = {2.0 * a};\n"
                           "  c2 = array(/*Real*/(i) * a for i in 1:2);\n  c3 = array(x[i] for i in {1, 3});\n"
                           "  c4 = array(x[i] for i in 1:2:4);\n  c5 = {a, 1.0, 2.0};\n"
                           "  c6 = array(array(/*Real*/(i * j) * a for i in 1:1) for j in 1:2);\n  e1[1] = x[4];\n"
                           "  e2 = array(x[i] * 2.0 for i in {4, 1});\nalgorithm\n  s1 := {x[2]};\nend ACT1;\n"));
}

YMC_TEST(flatten_comments_in_functions_and_escapes) {
  // 函数形参 / 局部的字符串注释照打（此前丢了）；别名函数 `function f = g "c"` 用别名自己的注释、没写就没有；注释只解一遍
  // 转义（SCode 里已解过，flatten 此前又解了一遍：`"esc \\n kept"` 打出真的换行）；注释里的换行后补续行缩进——变量
  // 声明 1 格、方程 / 语句跟所在块。期望 = omc 1.25.0 实跑 FCT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model FCT1\n  function g \"g comment\"\n    input Real x \"say \\\"hi\\\"\";\n"
                           "    output Real y \"esc \\\\n kept\";\n  algorithm\n    y := 2 * x \"two\\nlines\";\n  end g;\n"
                           "  function f = g \"alias comment\";\n  function h = g;\n  Real a = time \"var\\ncomment\";\n"
                           "  Real b = f(a) + h(a);\nequation\n  a = a \"eq\\ncomment\";\nend FCT1;\n"),
               std::string("function FCT1.f \"alias comment\"\n  input Real x \"say \\\"hi\\\"\";\n"
                           "  output Real y \"esc \\\\n kept\";\nalgorithm\n  y := 2.0 * x \"two\n  lines\";\nend FCT1.f;\n\n"
                           "function FCT1.h\n  input Real x \"say \\\"hi\\\"\";\n  output Real y \"esc \\\\n kept\";\n"
                           "algorithm\n  y := 2.0 * x \"two\n  lines\";\nend FCT1.h;\n\nclass FCT1\n  Real a = time \"var\n"
                           " comment\";\n  Real b = FCT1.f(a) + FCT1.h(a);\nequation\n  a = a \"eq\n  comment\";\nend FCT1;\n"));
  // 没有类级 annotation 的类注释 omc 存源码原文（translateCommentList 不解转义，AbsynToSCode.mo:1474-1500 v1.25.0），打印时
  // 再转义一遍：`"say \\\"hi\\\""`（此前不复刻、SKIP）。期望 = omc 1.25.0 实跑 WCS4（2026-10-08）。
  // （标识符里的非 ASCII 字符现由词法器拒绝，见 parser_test 的 lexer_quoted_identifier_charset。）
  YMC_CHECK_EQ(flatten_src("model WCS4 \"say \\\"hi\\\"\"\n  Real y = time;\nend WCS4;\n"),
               std::string("class WCS4 \"say \\\\\\\"hi\\\\\\\"\"\n  Real y = time;\nend WCS4;\n"));
}

YMC_TEST(flatten_size_of_array_expressions) {
  // size / ndims 的实参是数组式：形状看穿数组字面量 / 范围（`size({1, 2, 3}, 1)` → 3，此前字面量被当成"不是数组"）；维号
  // 不是常量时 omc 原样留调用、参数不标结构（`size(m, d)`）；求值路（维、constant）按构造子的嵌套层数。
  // 期望 = omc 1.25.0 实跑 SZT1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("model SZT1\n  Real a = time;\n  Real x[3] = {a, 2 * a, 3 * a};\n"
                           "  Real m[2, 3] = {{a, a, a}, {a, a, a}};\n  parameter Integer d = 1;\n"
                           "  Integer k1 = size({1, 2, 3}, 1);\n  Integer k2 = size({{1, 2}, {3, 4}, {5, 6}}, 2);\n"
                           "  Integer k3 = size(1:5, 1);\n  Integer k4 = size(m, d);\n  Integer k5 = size(m, d) + size(m, 2);\n"
                           "  Integer k6[1] = size({1, 2, 3});\n  Integer k7 = ndims(m);\n"
                           "  constant Integer c = size({1, 2}, 1);\n  Real y[size({1, 2, 3}, 1)] = fill(a, 3);\nend SZT1;\n"),
               std::string("class SZT1\n  Real a = time;\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real m[1,1];\n"
                           "  Real m[1,2];\n  Real m[1,3];\n  Real m[2,1];\n  Real m[2,2];\n  Real m[2,3];\n"
                           "  parameter Integer d = 1;\n  Integer k1 = 3;\n  Integer k2 = 2;\n  Integer k3 = 5;\n"
                           "  Integer k4 = size(m, d);\n  Integer k5 = size(m, d) + 3;\n  Integer k6[1];\n  Integer k7 = 2;\n"
                           "  constant Integer c = 2;\n  Real y[1];\n  Real y[2];\n  Real y[3];\nequation\n"
                           "  x = {a, 2.0 * a, 3.0 * a};\n  m = {{a, a, a}, {a, a, a}};\n  k6 = {3};\n  y = {a, a, a};\n"
                           "end SZT1;\n"));
}

YMC_TEST(flatten_function_array_slices) {
  // 函数体里的数组切片（FS 探针 t1–t4）：下标照写、留下一维——`:` 留原维，界是字面量的范围大小已知（`w[2:end]` →
  // `w[2:3]`、恰是整段的打成 `w[:]`），否则未知（`u[1:size(u, 1)]`）；下标向量 `w[{1, 3}]`、`u[idx]`。已知大小的按元素
  // 展开（取负、乘、sum、向量 / 矩阵积、数组 if），未知的按算符定形。Integer 数组进 Real：赋值位置整体转型
  // `/*Real[2]*/(n2)`，作运算数随展开逐个转。`size(q)` 整个留着，被运算 / sum 用到时按维展开。
  // 期望 = omc 1.25.0 实跑 FST1（2026-10-02）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Real u[:];\n  input Real w[3];\n  input Real q[:, :];\n  input Real g[2, 3];\n"
                           "  input Integer k;\n  input Integer n2[2];\n  input Integer idx[:];\n  output Real o;\nprotected\n"
                           "  Real v[size(u, 1)];\n  Real v3[3];\n  Real v2[2];\n  Integer n[2];\n  Integer r;\nalgorithm\n"
                           "  v := u[:];\n  v2 := u[1:2];\n  v2 := w[2:end];\n  v3 := w[1:end];\n  v3 := w[end:-1:1];\n"
                           "  v2 := w[{1, 3}];\n  v := u[idx];\n  v3 := q[k, :];\n  v2 := g[:, k];\n  v3 := -g[2, :];\n"
                           "  v3 := 2 * q[1, :];\n  v := u[1:end];\n  v[1:2] := {1, 2};\n  v3[2:end] := w[1:2];\n  v2 := n2;\n"
                           "  v2 := n2 * 2.5;\n  v2 := if k > 0 then n2 else w[1:2];\n"
                           "  v2 := 2 * (if k > 0 then n2 else w[1:2]);\n  v3 := fill(1, 3) + w;\n  v2 := g * w;\n"
                           "  o := sum(u[1:2]) + sum(q[1, :]) + sum(2 * w[1:2]) + w[1:2] * n2;\n  n := size(q);\n"
                           "  n := size(q) + {1, 1};\n  r := size(2 * u, 1) + ndims(q) + sum(size(q));\n  o := o + r + sum(n);\n"
                           "end f;\n\nmodel FST1\n  Real x[3] = {time, 1, 2};\n  Real m[2, 3] = {{time, 1, 2}, {3, 4, 5}};\n"
                           "  Real y = f(x, x, m, m, 1, {1, 2}, {1, 2, 3});\nend FST1;\n"),
               std::string("function f\n  input Real[:] u;\n  input Real[3] w;\n  input Real[:, :] q;\n  input Real[2, 3] g;\n"
                           "  input Integer k;\n  input Integer[2] n2;\n  input Integer[:] idx;\n  output Real o;\n"
                           "  protected Real[size(u, 1)] v;\n  protected Real[3] v3;\n  protected Real[2] v2;\n"
                           "  protected Integer[2] n;\n  protected Integer r;\nalgorithm\n  v := u[:];\n  v2 := u[1:2];\n"
                           "  v2 := w[2:3];\n  v3 := w[:];\n  v3 := w[3:-1:1];\n  v2 := w[{1, 3}];\n  v := u[idx];\n"
                           "  v3 := q[k,:];\n  v2 := g[:,k];\n  v3 := {-g[2,1], -g[2,2], -g[2,3]};\n  v3 := q[1,:] * 2.0;\n"
                           "  v := u[1:size(u, 1)];\n  v[1:2] := {1.0, 2.0};\n  v3[2:3] := w[1:2];\n  v2 := /*Real[2]*/(n2);\n"
                           "  v2 := {/*Real*/(n2[1]) * 2.5, /*Real*/(n2[2]) * 2.5};\n"
                           "  v2 := if k > 0 then /*Real[2]*/(n2) else w[1:2];\n"
                           "  v2 := {2.0 * (if k > 0 then /*Real*/(n2[1]) else w[1]), 2.0 * (if k > 0 then /*Real*/(n2[2]) else w[2])};\n"
                           "  v3 := {1.0 + w[1], 1.0 + w[2], 1.0 + w[3]};\n"
                           "  v2 := {g[1,1] * w[1] + g[1,2] * w[2] + g[1,3] * w[3], g[2,1] * w[1] + g[2,2] * w[2] + g[2,3] * w[3]};\n"
                           "  o := u[1] + u[2] + sum(q[1,:]) + 2.0 * w[1] + 2.0 * w[2] + w[1] * /*Real*/(n2[1]) + w[2] * /*Real*/(n2[2]);\n"
                           "  n := size(q);\n  n := {size(q, 1) + 1, size(q, 2) + 1};\n"
                           "  r := size(u * 2.0, 1) + 2 + size(q, 1) + size(q, 2);\n"
                           "  o := o + /*Real*/(r) + /*Real*/(n[1] + n[2]);\nend f;\n\nclass FST1\n  Real x[1];\n  Real x[2];\n"
                           "  Real x[3];\n  Real m[1,1];\n  Real m[1,2];\n  Real m[1,3];\n  Real m[2,1];\n  Real m[2,2];\n"
                           "  Real m[2,3];\n  Real y = f(x, x, m, m, 1, {1, 2}, {1, 2, 3});\nequation\n  x = {time, 1.0, 2.0};\n"
                           "  m = {{time, 1.0, 2.0}, {3.0, 4.0, 5.0}};\nend FST1;\n"));
  // 数组字面量的内层维未知时 omc 只认第 1 维（"Invalid index 2 in call to size of {u, u}"，FSS1）。
  bool uns = false;
  YMC_CHECK_EQ(flatten_src("function f\n  input Real u[:];\n  output Integer n;\nalgorithm\n  n := size({u, u}, 2);\nend f;\n\n"
                           "model FSS1\n  Real x[3] = {time, 1, 2};\n  Integer k = f(x);\nend FSS1;\n", &uns),
               std::string("<unsupported:size of array literal with unknown inner dimension>"));
  YMC_CHECK(uns);
}

YMC_TEST(flatten_state_machine_operators) {
  // 状态机算子（MLS §17.1）：1.25.0 的新前端原样留调用——状态实参是类实例的引用（带实例前缀），transition 的缺省实参补齐
  // 成 7 个位置实参（形参表 NFModelicaBuiltin.mo:582-611 v1.25.0），activeState / ticksInState / timeInState 是普通的
  // Boolean / Integer / Real 调用。期望 = omc 1.25.0 实跑 SMT1（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model SMT1\n  block St\n    output Real d;\n  end St;\n  model W\n    St w1;\n    St w2;\n"
                           "    Boolean wc = time > 1;\n  equation\n    initialState(w1);\n"
                           "    transition(w1, w2, wc, priority = 2, immediate = false);\n  end W;\n  St s1;\n  St s2;\n"
                           "  St sa[2];\n  W w;\n  Boolean c = time > 1;\n  Integer i(start = 0);\n"
                           "  Boolean a = activeState(s1) and not activeState(w.w2);\n  Integer t = ticksInState();\n"
                           "  Real tt = timeInState() * 2;\n  Real y;\nequation\n  initialState(s1);\n"
                           "  transition(s1, s2, c and i > 2);\n"
                           "  transition(s2, s1, time > 3, reset = false, synchronize = true);\n"
                           "  transition(from = sa[1], to = sa[2], condition = c);\n  when activeState(s2) then\n    y = time;\n"
                           "  end when;\nalgorithm\n  i := ticksInState();\nend SMT1;\n"),
               std::string("class SMT1\n  Real s1.d;\n  Real s2.d;\n  Real sa[1].d;\n  Real sa[2].d;\n  Real w.w1.d;\n"
                           "  Real w.w2.d;\n  Boolean w.wc = time > 1.0;\n  Boolean c = time > 1.0;\n  Integer i(start = 0);\n"
                           "  Boolean a = activeState(s1) and not activeState(w.w2);\n  Integer t = ticksInState();\n"
                           "  Real tt = timeInState() * 2.0;\n  Real y;\nequation\n  initialState(w.w1);\n"
                           "  transition(w.w1, w.w2, w.wc, false, true, false, 2);\n  initialState(s1);\n"
                           "  transition(s1, s2, c and i > 2, true, true, false, 1);\n"
                           "  transition(s2, s1, time > 3.0, true, false, true, 1);\n"
                           "  transition(sa[1], sa[2], c, true, true, false, 1);\n  when activeState(s2) then\n    y = time;\n"
                           "  end when;\nalgorithm\n  i := ticksInState();\nend SMT1;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // initialState / transition 没有返回值，当表达式用是 omc 的类型错（SMS1）。
           {"model SMS1\n  block St\n  end St;\n  St s1;\n  Real y = initialState(s1);\nend SMS1;\n",
            "state machine operator used as expression"},
           // 状态实参须是类实例；内建类型的变量 omc 报错（SMS2）。
           {"model SMS2\n  Boolean c = time > 1;\nequation\n  initialState(c);\nend SMS2;\n", "state machine operand"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_implicit_iteration_ranges) {
  // 隐式迭代范围 `for i loop` / `sum(x[i] for i)` / `{x[i] for i}`（deduceIterationRange，NFTyping.mo:3735-3881 v1.25.0）：
  // 范围 = `1:末`，末取自体里迭代变量恰作下标的首个用处——模型里是字面量（语句打 `for i in 1:3 loop`、方程展开、保留的
  // 归约 / 推导式打 `for i in 1:3`，同一个类的各实例各推各的），函数里 `:` 维是 `size(u, 1)`、表达式维是维表达式
  //（`Real w[k]` → `1:k`）；内层同名迭代变量的用处不算外层的。期望 = omc 1.25.0 实跑 IRT1 / IRT2（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model IRT1\n  model A\n    parameter Integer n;\n    Real y[n];\n    Real s = sum(y[i] for i);\n"
                           "  equation\n    for i loop\n      y[i] = i * time;\n    end for;\n  end A;\n  Real a = time;\n"
                           "  Real x[3] = {a, 2 * a, 3 * a};\n  Real m[2, 3] = {{a, a, a}, {a, a, a}};\n  Real y[3];\n"
                           "  Real y2[2, 3];\n  Real z[2];\n  Real s = sum(x[i] for i) + max(x[i] for i);\n"
                           "  Real t = sum(m[i, j] for i, j);\n  Real c[3] = {x[i] * 2 for i};\n  Real d[:] = {x[i] for i};\n"
                           "  A a1(n = 2);\n  A a2(n = 3);\nequation\n  for i loop\n    z[i] = sum(m[i, j] for j);\n  end for;\n"
                           "algorithm\n  for i loop\n    y[i] := x[i] * i;\n  end for;\n  for i, j loop\n"
                           "    y2[i, j] := m[i, j];\n  end for;\nend IRT1;\n"),
               std::string("class IRT1\n  Real a = time;\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real m[1,1];\n"
                           "  Real m[1,2];\n  Real m[1,3];\n  Real m[2,1];\n  Real m[2,2];\n  Real m[2,3];\n  Real y[1];\n"
                           "  Real y[2];\n  Real y[3];\n  Real y2[1,1];\n  Real y2[1,2];\n  Real y2[1,3];\n  Real y2[2,1];\n"
                           "  Real y2[2,2];\n  Real y2[2,3];\n  Real z[1];\n  Real z[2];\n"
                           "  Real s = x[1] + x[2] + x[3] + max(x[i] for i in 1:3);\n"
                           "  Real t = m[1,1] + m[2,1] + m[1,2] + m[2,2] + m[1,3] + m[2,3];\n  Real c[1];\n  Real c[2];\n"
                           "  Real c[3];\n  Real d[1];\n  Real d[2];\n  Real d[3];\n  final parameter Integer a1.n = 2;\n"
                           "  Real a1.y[1];\n  Real a1.y[2];\n  Real a1.s = a1.y[1] + a1.y[2];\n"
                           "  final parameter Integer a2.n = 3;\n  Real a2.y[1];\n  Real a2.y[2];\n  Real a2.y[3];\n"
                           "  Real a2.s = a2.y[1] + a2.y[2] + a2.y[3];\nequation\n  x = {a, 2.0 * a, 3.0 * a};\n"
                           "  m = {{a, a, a}, {a, a, a}};\n  c = array(x[i] * 2.0 for i in 1:3);\n"
                           "  d = array(x[i] for i in 1:3);\n  a1.y[1] = time;\n  a1.y[2] = 2.0 * time;\n  a2.y[1] = time;\n"
                           "  a2.y[2] = 2.0 * time;\n  a2.y[3] = 3.0 * time;\n  z[1] = m[1,1] + m[1,2] + m[1,3];\n"
                           "  z[2] = m[2,1] + m[2,2] + m[2,3];\nalgorithm\n  for i in 1:3 loop\n"
                           "    y[i] := x[i] * /*Real*/(i);\n  end for;\n  for i in 1:2 loop\n    for j in 1:3 loop\n"
                           "      y2[i,j] := m[i,j];\n    end for;\n  end for;\nend IRT1;\n"));
  YMC_CHECK_EQ(flatten_src("model IRT2\n  function f\n    input Real u[:];\n    input Integer k;\n"
                           "    output Real v[size(u, 1)];\n  protected\n    Real s;\n    Real w[k];\n"
                           "    Real z[2 * k, size(u, 1)];\n  algorithm\n    s := 0;\n    for i loop\n      v[i] := u[i] * 2;\n"
                           "      for i loop\n        w[i] := 1;\n      end for;\n    end for;\n    for i, j loop\n"
                           "      z[i, j] := u[j];\n      s := s + z[i, j];\n    end for;\n  end f;\n  function g\n"
                           "    input Real u[3];\n    input Real w[3];\n    output Real s;\n  algorithm\n    s := 0;\n"
                           "    for i loop\n      s := s + u[i] * w[i];\n    end for;\n  end g;\n  Real x[3] = {time, 1, 2};\n"
                           "  Real y[3] = f(x, 2);\n  Real r = g(x, y);\nend IRT2;\n"),
               std::string("function IRT2.f\n  input Real[:] u;\n  input Integer k;\n  output Real[size(u, 1)] v;\n"
                           "  protected Real s;\n  protected Real[k] w;\n  protected Real[2 * k, size(u, 1)] z;\nalgorithm\n"
                           "  s := 0.0;\n  for i in 1:size(u, 1) loop\n    v[i] := u[i] * 2.0;\n    for i in 1:k loop\n"
                           "      w[i] := 1.0;\n    end for;\n  end for;\n  for i in 1:2 * k loop\n"
                           "    for j in 1:size(u, 1) loop\n      z[i,j] := u[j];\n      s := s + z[i,j];\n    end for;\n"
                           "  end for;\nend IRT2.f;\n\nfunction IRT2.g\n  input Real[3] u;\n  input Real[3] w;\n"
                           "  output Real s;\nalgorithm\n  s := 0.0;\n  for i in 1:3 loop\n    s := s + u[i] * w[i];\n"
                           "  end for;\nend IRT2.g;\n\nclass IRT2\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y[1];\n"
                           "  Real y[2];\n  Real y[3];\n  Real r = IRT2.g(x, y);\nequation\n  x = {time, 1.0, 2.0};\n"
                           "  y = IRT2.f(x, 2);\nend IRT2;\n"));
  for (const std::string& src : std::vector<std::string>{
           // 各用处的维长不等（IRS1）/ 没有恰作下标的用处（IRS2）：omc 报错。
           "model IRS1\n  Real y[3];\n  Real z[2];\nalgorithm\n  for i loop\n    y[i] := z[i];\n  end for;\n"
           "end IRS1;\n",
           "model IRS2\n  Real y[3];\nalgorithm\n  for i loop\n    y[i + 1] := 1;\n  end for;\nend IRS2;\n",
           // 函数里两个 `:` 维的数组（IRS3）、写法不同的表达式维 `k` 对 `k + 0`（IRS4，omc 比的是未化简的维）：omc 报错。
           "model IRS3\n  function f\n    input Real u[:];\n    input Real w[:];\n    output Real s;\n"
           "  algorithm\n    s := 0;\n    for i loop\n      s := s + u[i] * w[i];\n    end for;\n  end f;\n"
           "  Real x[3] = {time, 1, 2};\n  Real y = f(x, x);\nend IRS3;\n",
           "model IRS4\n  function f\n    input Real u[:];\n    input Integer k;\n    output Real s;\n"
           "  protected\n    Real w[k];\n    Real w2[k + 0];\n  algorithm\n    s := 0;\n    for i loop\n"
           "      w[i] := 1;\n      w2[i] := w[i];\n      s := s + w2[i];\n    end for;\n  end f;\n"
           "  Real x[3] = {time, 1, 2};\n  Real y = f(x, 2);\nend IRS4;\n"}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, std::string("<unsupported:implicit-range for statement>"));
  }
}

YMC_TEST(flatten_symbolic_subscript_simplification) {
  // 留成符号的下标（for 语句 / 保留着的归约、推导式的迭代变量）照常化简：`x[i + 0]`、`x[1 * i]`、`x[i + (2 - 2)]` → `x[i]`，
  // `x[i + 1 - 1]` 不动。期望 = omc 1.25.0 实跑 SST1（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model SST1\n  Real a = time;\n  Real x[3] = {a, 2 * a, 3 * a};\n"
                           "  Real m[2, 3] = {{a, a, a}, {a, a, a}};\n  Real y[3];\n"
                           "  Real s = max(x[1 * i] + x[i - 0] for i in 1:3);\n  Real c[3] = {x[i + 0] for i in 1:3};\n"
                           "algorithm\n  for i in 1:3 loop\n    y[i + 0] := x[i * 1] + x[i + (2 - 2)] + x[i + 1 - 1];\n"
                           "  end for;\n  for i in 1:2 loop\n    for j in 1:3 loop\n      y[j] := m[i + 0, j * 1];\n"
                           "    end for;\n  end for;\nend SST1;\n"),
               std::string("class SST1\n  Real a = time;\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real m[1,1];\n"
                           "  Real m[1,2];\n  Real m[1,3];\n  Real m[2,1];\n  Real m[2,2];\n  Real m[2,3];\n  Real y[1];\n"
                           "  Real y[2];\n  Real y[3];\n  Real s = max(x[i] + x[i] for i in 1:3);\n  Real c[1];\n  Real c[2];\n"
                           "  Real c[3];\nequation\n  x = {a, 2.0 * a, 3.0 * a};\n  m = {{a, a, a}, {a, a, a}};\n"
                           "  c = array(x[i] for i in 1:3);\nalgorithm\n  for i in 1:3 loop\n"
                           "    y[i] := x[i] + x[i] + x[i + 1 - 1];\n  end for;\n  for i in 1:2 loop\n    for j in 1:3 loop\n"
                           "      y[j] := m[i,j];\n    end for;\n  end for;\nend SST1;\n"));
}

YMC_TEST(flatten_dimension_evaluation_through_instances) {
  // 维 / for 范围里经实例路径引用的常量与字面量绑定的参数（`a.j`、`b.p`、`g[2].n`、`r.n`：参数随之成结构参数，数组实例的
  // 各元素一起 final）、子实例数组的 size、ndims、矩阵字面量 `max([n, m])`。期望 = omc 1.25.0 实跑 NDT1（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model NDT1\n  model A\n    constant Integer j = 3;\n    parameter Integer p = 2;\n"
                           "    Real v[2] = {time, 1};\n  end A;\n  model G\n    parameter Integer n;\n  end G;\n  record R\n"
                           "    parameter Integer n = 2;\n  end R;\n  A a(j = 4);\n  A b;\n  G g[2](each n = 2);\n"
                           "  parameter R r(n = 3);\n  parameter Integer n = 2;\n  parameter Integer m = 4;\n  Real x[2, 3];\n"
                           "  Real z1[a.j];\n  Real z2[b.p + 1];\n  Real z3[size(a.v, 1)];\n  Real z4[g[2].n];\n"
                           "  Real z5[r.n];\n  Real z6[max([n, m])];\n  Real z7[ndims(x)];\n  Real q = b.p * time;\nequation\n"
                           "  for i in 1:a.j loop\n    z1[i] = i * time;\n  end for;\nend NDT1;\n"),
               std::string("class NDT1\n  constant Integer a.j = 4;\n  parameter Integer a.p = 2;\n  Real a.v[1];\n"
                           "  Real a.v[2];\n  constant Integer b.j = 3;\n  final parameter Integer b.p = 2;\n  Real b.v[1];\n"
                           "  Real b.v[2];\n  final parameter Integer g[1].n = 2;\n  final parameter Integer g[2].n = 2;\n"
                           "  final parameter Integer r.n = 3;\n  final parameter Integer n = 2;\n"
                           "  final parameter Integer m = 4;\n  Real x[1,1];\n  Real x[1,2];\n  Real x[1,3];\n  Real x[2,1];\n"
                           "  Real x[2,2];\n  Real x[2,3];\n  Real z1[1];\n  Real z1[2];\n  Real z1[3];\n  Real z1[4];\n"
                           "  Real z2[1];\n  Real z2[2];\n  Real z2[3];\n  Real z3[1];\n  Real z3[2];\n  Real z4[1];\n"
                           "  Real z4[2];\n  Real z5[1];\n  Real z5[2];\n  Real z5[3];\n  Real z6[1];\n  Real z6[2];\n"
                           "  Real z6[3];\n  Real z6[4];\n  Real z7[1];\n  Real z7[2];\n  Real q = 2.0 * time;\nequation\n"
                           "  a.v = {time, 1.0};\n  b.v = {time, 1.0};\n  z1[1] = time;\n  z1[2] = 2.0 * time;\n"
                           "  z1[3] = 3.0 * time;\n  z1[4] = 4.0 * time;\nend NDT1;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 经实例路径访问 protected 元素：omc 报错（NDS1）。
           {"model NDS1\n  model F\n  protected\n    constant Integer p = 2;\n  end F;\n  F f;\n  Real z[2];\n"
            "equation\n  for i in 1:f.p loop\n    z[i] = i;\n  end for;\nend NDS1;\n",
            "for equation (non-constant range)"},
           // Evaluate = false 的参数是 NON_STRUCTURAL_PARAMETER（NFInst.mo:3906-3912 v1.25.0）：omc 不求值、维留成
           // `Real[p] z`（不标量化），ymc 未做（NDS2）。
           {"model NDS2\n  parameter Integer p = 2 annotation(Evaluate = false);\n  Real z[p];\nend NDS2;\n", "non-literal array dimension"},
           // ndims(…) 总是参数变率（typeNdimsCall，NFBuiltinCall.mo:487-514）：绑给 constant 是 omc 的类型错（NDS3）。
           {"model NDS3\n  Real x[2, 3];\n  constant Integer n = ndims(x);\nend NDS3;\n", "ndims / size(a) in constant binding"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_comprehension_bindings_multi_iterator) {
  // 多迭代变量的推导式绑定 `{e for i in r1, j in r2}`：最右的迭代变量是最外维（MLS §10.4.2.1），`:` 维取各范围的长度，
  // 参数 / 常量逐元素代入；矩阵字面量常量 `[1, 2; 3, 4]` 的元素作维。期望 = omc 1.25.0 实跑 CBT1（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model CBT1\n  parameter Integer n = 2;\n"
                           "  parameter Integer s[:, :] = {i + j for i in 1:3, j in 1:n};\n"
                           "  parameter Integer t[2, 3, 2] = {i + j + k for i in 1:2, j in 1:3, k in 1:2};\n"
                           "  parameter String u[:, :] = {String(i + j) for i in 1:3, j in 1:2};\n"
                           "  parameter Real p[3] = {1, 2, 3};\n  parameter Real q[:, :] = {p[i] * p[j] for i, j};\n"
                           "  constant Integer M[2, 2] = [1, 2; 3, 4];\n  Real w[M[2, 1]];\nend CBT1;\n"),
               std::string("class CBT1\n  final parameter Integer n = 2;\n  parameter Integer s[1,1] = 2;\n"
                           "  parameter Integer s[1,2] = 3;\n  parameter Integer s[1,3] = 4;\n  parameter Integer s[2,1] = 3;\n"
                           "  parameter Integer s[2,2] = 4;\n  parameter Integer s[2,3] = 5;\n"
                           "  parameter Integer t[1,1,1] = 3;\n  parameter Integer t[1,1,2] = 4;\n"
                           "  parameter Integer t[1,2,1] = 4;\n  parameter Integer t[1,2,2] = 5;\n"
                           "  parameter Integer t[1,3,1] = 5;\n  parameter Integer t[1,3,2] = 6;\n"
                           "  parameter Integer t[2,1,1] = 4;\n  parameter Integer t[2,1,2] = 5;\n"
                           "  parameter Integer t[2,2,1] = 5;\n  parameter Integer t[2,2,2] = 6;\n"
                           "  parameter Integer t[2,3,1] = 6;\n  parameter Integer t[2,3,2] = 7;\n"
                           "  parameter String u[1,1] = \"2\";\n  parameter String u[1,2] = \"3\";\n"
                           "  parameter String u[1,3] = \"4\";\n  parameter String u[2,1] = \"3\";\n"
                           "  parameter String u[2,2] = \"4\";\n  parameter String u[2,3] = \"5\";\n"
                           "  parameter Real p[1] = 1.0;\n  parameter Real p[2] = 2.0;\n  parameter Real p[3] = 3.0;\n"
                           "  parameter Real q[1,1] = p[1] * p[1];\n  parameter Real q[1,2] = p[2] * p[1];\n"
                           "  parameter Real q[1,3] = p[3] * p[1];\n  parameter Real q[2,1] = p[1] * p[2];\n"
                           "  parameter Real q[2,2] = p[2] * p[2];\n  parameter Real q[2,3] = p[3] * p[2];\n"
                           "  parameter Real q[3,1] = p[1] * p[3];\n  parameter Real q[3,2] = p[2] * p[3];\n"
                           "  parameter Real q[3,3] = p[3] * p[3];\n  constant Integer M[1,1] = 1;\n"
                           "  constant Integer M[1,2] = 2;\n  constant Integer M[2,1] = 3;\n  constant Integer M[2,2] = 4;\n"
                           "  Real w[1];\n  Real w[2];\n  Real w[3];\nend CBT1;\n"));
  // 推导式的形状与声明维不符：omc 类型错（CBS1；此前只取前几个元素放行）。
  bool uns = false;
  const std::string out = flatten_src("model CBS1\n  parameter Integer s[2] = {i for i in 1:3};\nend CBS1;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:array binding shape/type mismatch>"));
}

YMC_TEST(flatten_type_dimension_array_references) {
  // 类型维（枚举 / Boolean）数组的引用：元素下标打标签 `y[E.two]`、`b[true]`，整体引用 / 数组运算 / 切片 `y[E.two:E.three]`、
  // `y[{E.three, E.one}]`、`y[end]` 同整数维（压平期按序数走，收尾 relabel_type_dims 换回标签）；constant 的元素取值。
  // 期望 = omc 1.25.0 实跑 ENT1（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model ENT1\n  type E = enumeration(one, two, three);\n  Real a = time;\n  Real y[E] = {a, 1, 2};\n"
                           "  Real b[Boolean] = {a, 1};\n  Real m[E, 2] = {{a, 1}, {2, 3}, {4, 5}};\n"
                           "  parameter Real p[E] = {1, 2, 3};\n  constant Real c[E] = {1, 2, 3};\n"
                           "  Real z1 = y[E.two] + b[true] + p[E.two] + c[E.three];\n  Real z2[E] = 2 * y;\n"
                           "  Real z3 = sum(y) + y[end];\n  Real z4[2] = y[E.two:E.three];\n"
                           "  Real z5[2] = y[{E.three, E.one}];\n  Real z6[2] = m[E.two, :];\n  Real w[E];\n  Real v[3];\n"
                           "equation\n  w = y;\n  v = m[:, 2];\nend ENT1;\n"),
               std::string("class ENT1\n  Real a = time;\n  Real y[E.one];\n  Real y[E.two];\n  Real y[E.three];\n"
                           "  Real b[false];\n  Real b[true];\n  Real m[E.one,1];\n  Real m[E.one,2];\n  Real m[E.two,1];\n"
                           "  Real m[E.two,2];\n  Real m[E.three,1];\n  Real m[E.three,2];\n  parameter Real p[E.one] = 1.0;\n"
                           "  parameter Real p[E.two] = 2.0;\n  parameter Real p[E.three] = 3.0;\n"
                           "  constant Real c[E.one] = 1.0;\n  constant Real c[E.two] = 2.0;\n"
                           "  constant Real c[E.three] = 3.0;\n  Real z1 = y[E.two] + b[true] + p[E.two] + 3.0;\n"
                           "  Real z2[E.one];\n  Real z2[E.two];\n  Real z2[E.three];\n"
                           "  Real z3 = y[E.one] + y[E.two] + y[E.three] + y[E.three];\n  Real z4[1];\n  Real z4[2];\n"
                           "  Real z5[1];\n  Real z5[2];\n  Real z6[1];\n  Real z6[2];\n  Real w[E.one];\n  Real w[E.two];\n"
                           "  Real w[E.three];\n  Real v[1];\n  Real v[2];\n  Real v[3];\nequation\n  y = {a, 1.0, 2.0};\n"
                           "  b = {a, 1.0};\n  m = {{a, 1.0}, {2.0, 3.0}, {4.0, 5.0}};\n"
                           "  z2 = {2.0 * y[E.one], 2.0 * y[E.two], 2.0 * y[E.three]};\n  z4 = y[E.two:E.three];\n"
                           "  z5 = y[{E.three, E.one}];\n  z6 = m[E.two,:];\n  w[E.one] = y[E.one];\n  w[E.two] = y[E.two];\n"
                           "  w[E.three] = y[E.three];\n  v[1] = m[E.one,2];\n  v[2] = m[E.two,2];\n  v[3] = m[E.three,2];\n"
                           "end ENT1;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 整数下标进枚举维：omc 类型错（ENS1）。
           {"model ENS1\n  type E = enumeration(one, two, three);\n  Real y[E] = {time, 1, 2};\n"
            "  Real z = y[2];\nend ENS1;\n", "type-dimension subscript"},
           // `:` 维由带类型维的绑定定（omc：z 也成 `z[E.one]`…），未做（ENS3）。
           {"model ENS3\n  type E = enumeration(one, two, three);\n  Real y[E] = {time, 1, 2};\n"
            "  Real z[:] = y;\nend ENS3;\n", "colon dimension from type-dimension binding"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_for_equations_over_type_ranges) {
  // for 方程在枚举 / Boolean 上迭代：类型名 `for i in E`、标签范围 `E.one:E.two`、标签数组、类型维上的隐式范围；迭代变量
  // 的值是标签（`x[i] = i` → `x[E.one] = E.one`），比较 / Integer(i) 照常折叠。期望 = omc 1.25.0 实跑 ENT2（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model ENT2\n  type E = enumeration(one, two, three);\n  Real a = time;\n  Real y[E];\n"
                           "  Real b[Boolean];\n  E x[E];\n  Integer n[E];\n  Real m[2, E];\n  Real q[E];\n  Real r[E];\n"
                           "equation\n  for i in E loop\n    y[i] = if i == E.two then a else Integer(i);\n    x[i] = i;\n"
                           "  end for;\n  for i in Boolean loop\n    b[i] = if i then a else 0;\n  end for;\n"
                           "  for i in E.one:E.two loop\n    n[i] = Integer(i);\n  end for;\n  n[E.three] = 0;\n"
                           "  for i in 1:2, j in E loop\n    m[i, j] = a * i;\n  end for;\n  for i loop\n    q[i] = a;\n"
                           "  end for;\n  for i in {E.three, E.one} loop\n    r[i] = a;\n  end for;\n  r[E.two] = 0;\n"
                           "end ENT2;\n"),
               std::string("class ENT2\n  Real a = time;\n  Real y[E.one];\n  Real y[E.two];\n  Real y[E.three];\n"
                           "  Real b[false];\n  Real b[true];\n  enumeration(one, two, three) x[E.one];\n"
                           "  enumeration(one, two, three) x[E.two];\n  enumeration(one, two, three) x[E.three];\n"
                           "  Integer n[E.one];\n  Integer n[E.two];\n  Integer n[E.three];\n  Real m[1,E.one];\n"
                           "  Real m[1,E.two];\n  Real m[1,E.three];\n  Real m[2,E.one];\n  Real m[2,E.two];\n"
                           "  Real m[2,E.three];\n  Real q[E.one];\n  Real q[E.two];\n  Real q[E.three];\n  Real r[E.one];\n"
                           "  Real r[E.two];\n  Real r[E.three];\nequation\n  y[E.one] = 1.0;\n  x[E.one] = E.one;\n"
                           "  y[E.two] = a;\n  x[E.two] = E.two;\n  y[E.three] = 3.0;\n  x[E.three] = E.three;\n"
                           "  b[false] = 0.0;\n  b[true] = a;\n  n[E.one] = 1;\n  n[E.two] = 2;\n  n[E.three] = 0;\n"
                           "  m[1,E.one] = a;\n  m[1,E.two] = a;\n  m[1,E.three] = a;\n  m[2,E.one] = a * 2.0;\n"
                           "  m[2,E.two] = a * 2.0;\n  m[2,E.three] = a * 2.0;\n  q[E.one] = a;\n  q[E.two] = a;\n"
                           "  q[E.three] = a;\n  r[E.three] = a;\n  r[E.one] = a;\n  r[E.two] = 0.0;\nend ENT2;\n"));
}

YMC_TEST(flatten_for_statements_over_type_ranges) {
  // for 语句在枚举 / Boolean 上迭代（保留循环）：类型名打成标签数组 `for i in {E.one, E.two, E.three} loop`、范围照写、
  // 隐式范围是首末标签；体里类型维上的符号下标 `y[i]`、`b[not i]`、`k[Integer(i)]`；范围为空的 for 语句整条删去
  //（simplifyStatement，NFSimplifyModel.mo:312-333 v1.25.0，整数范围 `3:1` 同）。期望 = omc 1.25.0 实跑 ENT3（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model ENT3\n  type E = enumeration(one, two, three);\n  Real a = time;\n  Real y[E] = {a, 1, 2};\n"
                           "  Real b[Boolean] = {a, 1};\n  Real z[E];\n  Real s;\n  Real t;\n  Real k[3];\nalgorithm\n"
                           "  s := 0;\n  for i in E loop\n    z[i] := y[i] * 2;\n    k[Integer(i)] := y[i];\n  end for;\n"
                           "  for i in E.two:E.three loop\n    s := s + y[i];\n  end for;\n  for i loop\n    s := s + z[i];\n"
                           "  end for;\n  t := 0;\n  for i in Boolean loop\n    t := t + b[i] + b[not i];\n  end for;\n"
                           "  for i in E.three:E.one loop\n    t := t + 1;\n  end for;\n  for i in 3:1 loop\n    t := t + 1;\n"
                           "  end for;\nend ENT3;\n"),
               std::string("class ENT3\n  Real a = time;\n  Real y[E.one];\n  Real y[E.two];\n  Real y[E.three];\n"
                           "  Real b[false];\n  Real b[true];\n  Real z[E.one];\n  Real z[E.two];\n  Real z[E.three];\n"
                           "  Real s;\n  Real t;\n  Real k[1];\n  Real k[2];\n  Real k[3];\nequation\n  y = {a, 1.0, 2.0};\n"
                           "  b = {a, 1.0};\nalgorithm\n  s := 0.0;\n  for i in {E.one, E.two, E.three} loop\n"
                           "    z[i] := y[i] * 2.0;\n    k[Integer(i)] := y[i];\n  end for;\n  for i in E.two:E.three loop\n"
                           "    s := s + y[i];\n  end for;\n  for i in E.one:E.three loop\n    s := s + z[i];\n  end for;\n"
                           "  t := 0.0;\n  for i in {false, true} loop\n    t := t + b[i] + b[not i];\n  end for;\nend ENT3;\n"));
  // 枚举迭代变量作整数维数组的下标：omc 类型错（ENS2）。
  bool uns = false;
  const std::string out = flatten_src("model ENS2\n  type E = enumeration(one, two, three);\n  Real y[3] = {time, 1, 2};\n  Real z;\n"
                                           "algorithm\n  z := 0;\n  for i in E loop\n    z := z + y[i];\n  end for;\nend ENS2;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:subscript type>"));
}

YMC_TEST(flatten_reductions_and_comprehensions_over_type_ranges) {
  // 归约 / 推导式在枚举 / Boolean 上迭代：sum 展开成 `y[E.one] + …`、max / min 保留 `for i in {E.one, E.two, E.three}` /
  // `E.one:E.two`；推导式绑定的 `:` 维——范围写类型名的是类型维（`z2[E.one]`…），标签范围是整数维。
  // 期望 = omc 1.25.0 实跑 ENT4（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model ENT4\n  type E = enumeration(one, two, three);\n  Real a = time;\n  Real y[E] = {a, 1, 2};\n"
                           "  Real b[Boolean] = {a, 1};\n  parameter Real p[E] = {1, 2, 3};\n  constant Real c[E] = {1, 2, 3};\n"
                           "  Real s1 = sum(y[i] for i in E) + sum(y[i] * Integer(i) for i in E.two:E.three);\n"
                           "  Real s2 = max(y[i] for i in E) + min(y[i] for i in E.one:E.two) + max(y[i] for i);\n"
                           "  Real s3 = sum(c[i] for i in E) * a + sum(b[i] for i in Boolean);\n"
                           "  Real z1[3] = {y[i] * 2 for i in E};\n  Real z2[:] = {y[i] * 2 for i in E};\n"
                           "  Real z3[:] = {y[i] for i in E.one:E.two};\n  Real z4[:] = {b[i] for i in Boolean};\n"
                           "  parameter Real q[:] = {p[i] * 2 for i in E};\n  E x[3] = {i for i in E};\nend ENT4;\n"),
               std::string("class ENT4\n  Real a = time;\n  Real y[E.one];\n  Real y[E.two];\n  Real y[E.three];\n"
                           "  Real b[false];\n  Real b[true];\n  parameter Real p[E.one] = 1.0;\n"
                           "  parameter Real p[E.two] = 2.0;\n  parameter Real p[E.three] = 3.0;\n"
                           "  constant Real c[E.one] = 1.0;\n  constant Real c[E.two] = 2.0;\n"
                           "  constant Real c[E.three] = 3.0;\n"
                           "  Real s1 = y[E.one] + y[E.two] + y[E.three] + y[E.two] * 2.0 + y[E.three] * 3.0;\n"
                           "  Real s2 = max(y[i] for i in {E.one, E.two, E.three}) + min(y[i] for i in E.one:E.two) + max(y[i] for i in E.one:E.three);\n"
                           "  Real s3 = 6.0 * a + b[false] + b[true];\n  Real z1[1];\n  Real z1[2];\n  Real z1[3];\n"
                           "  Real z2[E.one];\n  Real z2[E.two];\n  Real z2[E.three];\n  Real z3[1];\n  Real z3[2];\n"
                           "  Real z4[false];\n  Real z4[true];\n  parameter Real q[E.one] = p[E.one] * 2.0;\n"
                           "  parameter Real q[E.two] = p[E.two] * 2.0;\n  parameter Real q[E.three] = p[E.three] * 2.0;\n"
                           "  enumeration(one, two, three) x[1];\n  enumeration(one, two, three) x[2];\n"
                           "  enumeration(one, two, three) x[3];\nequation\n  y = {a, 1.0, 2.0};\n  b = {a, 1.0};\n"
                           "  z1 = array(y[i] * 2.0 for i in {E.one, E.two, E.three});\n"
                           "  z2 = array(y[i] * 2.0 for i in {E.one, E.two, E.three});\n"
                           "  z3 = array(y[i] for i in E.one:E.two);\n  z4 = array(b[i] for i in {false, true});\n"
                           "  x = array(i for i in {E.one, E.two, E.three});\nend ENT4;\n"));
}

YMC_TEST(flatten_composite_array_element_references) {
  // 复合数组的元素引用：非末段有洞（没给下标 / 下标少于维数 / 切片）→ 整条引用展开成全标量元素的嵌套数组（各段的洞按
  // 段序、维序逐层，末段数组一并展开到标量）：`a.v` → `{{a[1].v[1], a[1].v[2]}, …}`、`a[{3, 1}].x`、`b.q[2].x` →
  // `{b[1].q[2].x, b[2].q[2].x}`；各段都取到元素的照常解析（`b[2].q[1].v` 整体）。等式里随等式逐元素、sum 展开、
  // `der(a.x)` 在绑定里整体保留。期望 = omc 1.25.0 实跑 CAT1（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model CAT1\n  model A\n    Real x;\n    parameter Real k = 1;\n    Real v[2];\n  end A;\n  model B\n"
                           "    A q[2];\n    Real w = time;\n  end B;\n  record R\n    Real s;\n    Real t[2];\n  end R;\n"
                           "  Real tt = time;\n  A a[3];\n  B b[2];\n  parameter R p[2](s = {1, 2}, t = {{1, 2}, {3, 4}});\n"
                           "  Real y1 = sum(a[:].x) + sum(a[1:2].k) + sum(a.v);\n  Real y2[3] = a.x;\n  Real y3[3, 2] = a.v;\n"
                           "  Real y4[3] = a[:].v[2];\n  Real y5[2] = a[{3, 1}].x;\n  Real y6[2, 2] = b.q.x;\n"
                           "  Real y7[2] = b.q[2].x;\n  Real y8 = b[1].q[2].x + b[2].q[1].v[2];\n  Real y9[2] = b[2].q[1].v;\n"
                           "  Real y10[3] = 2 * der(a.x);\n  parameter Real q[2] = p.s;\n  parameter Real u[2, 2] = p.t;\n"
                           "equation\n  der(a.x) = {tt, 1, 2};\n  a[:].v[1] = {tt, 1, 2};\n  a.v[2] = 2 * a.x;\n"
                           "  b.q.x = {{tt, 1}, {2, 3}};\n  b[1].q.v = {{tt, 1}, {2, 3}};\n  b[2].q[:].v = {{4, 5}, {6, 7}};\n"
                           "end CAT1;\n"),
               std::string("class CAT1\n  Real tt = time;\n  Real a[1].x;\n  parameter Real a[1].k = 1.0;\n  Real a[1].v[1];\n"
                           "  Real a[1].v[2];\n  Real a[2].x;\n  parameter Real a[2].k = 1.0;\n  Real a[2].v[1];\n"
                           "  Real a[2].v[2];\n  Real a[3].x;\n  parameter Real a[3].k = 1.0;\n  Real a[3].v[1];\n"
                           "  Real a[3].v[2];\n  Real b[1].q[1].x;\n  parameter Real b[1].q[1].k = 1.0;\n"
                           "  Real b[1].q[1].v[1];\n  Real b[1].q[1].v[2];\n  Real b[1].q[2].x;\n"
                           "  parameter Real b[1].q[2].k = 1.0;\n  Real b[1].q[2].v[1];\n  Real b[1].q[2].v[2];\n"
                           "  Real b[1].w = time;\n  Real b[2].q[1].x;\n  parameter Real b[2].q[1].k = 1.0;\n"
                           "  Real b[2].q[1].v[1];\n  Real b[2].q[1].v[2];\n  Real b[2].q[2].x;\n"
                           "  parameter Real b[2].q[2].k = 1.0;\n  Real b[2].q[2].v[1];\n  Real b[2].q[2].v[2];\n"
                           "  Real b[2].w = time;\n  parameter Real p[1].s = 1.0;\n  parameter Real p[1].t[1] = 1.0;\n"
                           "  parameter Real p[1].t[2] = 2.0;\n  parameter Real p[2].s = 2.0;\n"
                           "  parameter Real p[2].t[1] = 3.0;\n  parameter Real p[2].t[2] = 4.0;\n"
                           "  Real y1 = a[1].x + a[2].x + a[3].x + a[1].k + a[2].k + a[1].v[1] + a[1].v[2] + a[2].v[1] + a[2].v[2] + a[3].v[1] + a[3].v[2];\n"
                           "  Real y2[1];\n  Real y2[2];\n  Real y2[3];\n  Real y3[1,1];\n  Real y3[1,2];\n  Real y3[2,1];\n"
                           "  Real y3[2,2];\n  Real y3[3,1];\n  Real y3[3,2];\n  Real y4[1];\n  Real y4[2];\n  Real y4[3];\n"
                           "  Real y5[1];\n  Real y5[2];\n  Real y6[1,1];\n  Real y6[1,2];\n  Real y6[2,1];\n  Real y6[2,2];\n"
                           "  Real y7[1];\n  Real y7[2];\n  Real y8 = b[1].q[2].x + b[2].q[1].v[2];\n  Real y9[1];\n"
                           "  Real y9[2];\n  Real y10[1];\n  Real y10[2];\n  Real y10[3];\n  parameter Real q[1] = p[1].s;\n"
                           "  parameter Real q[2] = p[2].s;\n  parameter Real u[1,1] = p[1].t[1];\n"
                           "  parameter Real u[1,2] = p[1].t[2];\n  parameter Real u[2,1] = p[2].t[1];\n"
                           "  parameter Real u[2,2] = p[2].t[2];\nequation\n  y2 = {a[1].x, a[2].x, a[3].x};\n"
                           "  y3 = {{a[1].v[1], a[1].v[2]}, {a[2].v[1], a[2].v[2]}, {a[3].v[1], a[3].v[2]}};\n"
                           "  y4 = {a[1].v[2], a[2].v[2], a[3].v[2]};\n  y5 = {a[3].x, a[1].x};\n"
                           "  y6 = {{b[1].q[1].x, b[1].q[2].x}, {b[2].q[1].x, b[2].q[2].x}};\n"
                           "  y7 = {b[1].q[2].x, b[2].q[2].x};\n  y9 = b[2].q[1].v;\n"
                           "  y10 = {2.0 * der(a[1].x), 2.0 * der(a[2].x), 2.0 * der(a[3].x)};\n  der(a[1].x) = tt;\n"
                           "  der(a[2].x) = 1.0;\n  der(a[3].x) = 2.0;\n  a[1].v[1] = tt;\n  a[2].v[1] = 1.0;\n"
                           "  a[3].v[1] = 2.0;\n  a[1].v[2] = 2.0 * a[1].x;\n  a[2].v[2] = 2.0 * a[2].x;\n"
                           "  a[3].v[2] = 2.0 * a[3].x;\n  b[1].q[1].x = tt;\n  b[1].q[2].x = 1.0;\n  b[2].q[1].x = 2.0;\n"
                           "  b[2].q[2].x = 3.0;\n  b[1].q[1].v[1] = tt;\n  b[1].q[1].v[2] = 1.0;\n  b[1].q[2].v[1] = 2.0;\n"
                           "  b[1].q[2].v[2] = 3.0;\n  b[2].q[1].v[1] = 4.0;\n  b[2].q[1].v[2] = 5.0;\n  b[2].q[2].v[1] = 6.0;\n"
                           "  b[2].q[2].v[2] = 7.0;\nend CAT1;\n"));
  // 复合数组段上的符号下标（保留着的迭代变量、运行时整型量）原样留着：`a[i].x`、`a[n + 1].v[2]`、
  // `max(a[i].x for i in 1:3)`；语句里被逐元素运算 / sum 吃掉的展开照常。期望 = omc 1.25.0 实跑 CAT2（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model CAT2\n  model A\n    Real x = time;\n    parameter Real k = 1;\n    Real v[2] = {time, 1};\n"
                           "  end A;\n  A a[3];\n  Integer n = integer(time);\n  Real s;\n  Real y[3];\n"
                           "  Real z1 = a[n].x + a[n + 1].v[2];\n  Real z2[2] = a[n].v;\n"
                           "  Real z3 = max(a[i].x for i in 1:3) + sum(a[i].v[1] for i in 1:3);\n"
                           "  Real z4[3] = {a[i].x * 2 for i in 1:3};\nalgorithm\n  s := sum(a.x) + a.x * a.k;\n"
                           "  y := 2 * a.x;\n  for i in 1:3 loop\n    y[i] := y[i] + a[i].x * a[i].k + sum(a[i].v);\n"
                           "  end for;\n  for i in 1:2 loop\n    s := s + a[i + 1].v[1];\n  end for;\nend CAT2;\n"),
               std::string("class CAT2\n  Real a[1].x = time;\n  parameter Real a[1].k = 1.0;\n  Real a[1].v[1];\n"
                           "  Real a[1].v[2];\n  Real a[2].x = time;\n  parameter Real a[2].k = 1.0;\n  Real a[2].v[1];\n"
                           "  Real a[2].v[2];\n  Real a[3].x = time;\n  parameter Real a[3].k = 1.0;\n  Real a[3].v[1];\n"
                           "  Real a[3].v[2];\n  Integer n = integer(time);\n  Real s;\n  Real y[1];\n  Real y[2];\n"
                           "  Real y[3];\n  Real z1 = a[n].x + a[n + 1].v[2];\n  Real z2[1];\n  Real z2[2];\n"
                           "  Real z3 = max(a[i].x for i in 1:3) + a[1].v[1] + a[2].v[1] + a[3].v[1];\n  Real z4[1];\n"
                           "  Real z4[2];\n  Real z4[3];\nequation\n  a[1].v = {time, 1.0};\n  a[2].v = {time, 1.0};\n"
                           "  a[3].v = {time, 1.0};\n  z2 = a[n].v;\n  z4 = array(a[i].x * 2.0 for i in 1:3);\nalgorithm\n"
                           "  s := a[1].x + a[2].x + a[3].x + a[1].x * a[1].k + a[2].x * a[2].k + a[3].x * a[3].k;\n"
                           "  y := {2.0 * a[1].x, 2.0 * a[2].x, 2.0 * a[3].x};\n  for i in 1:3 loop\n"
                           "    y[i] := y[i] + a[i].x * a[i].k + a[i].v[1] + a[i].v[2];\n  end for;\n  for i in 1:2 loop\n"
                           "    s := s + a[i + 1].v[1];\n  end for;\nend CAT2;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 语句里原样留着的展开数组：omc 打成 `y := array(a[$i1].x for $i1 in 1:3);`，未做（CAS1）。
           {"model CAS1\n  model A\n    Real x = time;\n  end A;\n  A a[3];\n  Real y[3];\nalgorithm\n"
            "  y := a.x;\nend CAS1;\n", "composite array field in algorithm"},
           // 越界的复合数组下标：omc 不查、照打一个不存在的变量 `a[4].x`，不跟（CAS2）。
           {"model CAS2\n  model A\n    Real x = time;\n  end A;\n  A a[3];\n  Real y = a[4].x;\nend CAS2;\n", "composite array element reference"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_array_modifiers_on_component_arrays) {
  // 组件数组上的数组值修饰（Modifier.propagate 逐元素分发）：绑定不是字面量数组时（`2 * p`、`fill(…)`、切片、if 式、
  // 用户函数、推导式、linspace）逐元素取 `(E)[i]`，E 整体定型后取该元素并化简——`a1[2].k = 2.0 * p[2]`、
  // `a3[1].k = p[3]`、`a5[1].k = MDT1.f(p)[1]`、`a6[1].k = p[3]`；Integer 与 Real 混排的字面量在末元素是 Integer 时
  // 照常转型；数组字段的修饰（`v = fill(tt, 3, 2)`）只取前几维；嵌套一层的（`B b[2](w = p[1:2])`）同理。书写的
  // `(2 * p)[2]` 走同一条路。期望 = omc 1.25.0 实跑 MDT1（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model MDT1\n  model A\n    parameter Real k = 1;\n    Real x = k * time;\n"
                           "    Real v[2] = {time, k};\n  end A;\n  model B\n    A q[2];\n    parameter Real w = 1;\n  end B;\n"
                           "  function f\n    input Real u[:];\n    output Real s[size(u, 1)];\n  algorithm\n    s := 2 * u;\n"
                           "  end f;\n  parameter Real p[3] = {1, 2, 3};\n  parameter Boolean c = true;\n  Real tt = time;\n"
                           "  A a1[3](k = 2 * p);\n  A a2[3](k = fill(2, 3), x(start = p));\n  A a3[3](k = p[{3, 1, 2}]);\n"
                           "  A a4[3](k = if c then p else 2 * p);\n  A a5[3](k = f(p));\n"
                           "  A a6[3](k = {p[4 - i] for i in 1:3});\n  A a7[3](k = linspace(1, 2, 3), x = p * tt);\n"
                           "  A a8[3](k = {p[1], 2, 3});\n  A a9[2, 2](k = fill(p[2], 2, 2));\n  A a10[3](v = fill(tt, 3, 2));\n"
                           "  B b[2](w = p[1:2]);\n  Real y1 = (2 * p)[2] * tt;\n  Real y2 = sum({a1[4 - i].x for i in 1:3});\n"
                           "end MDT1;\n"),
               std::string("function MDT1.f\n  input Real[:] u;\n  output Real[size(u, 1)] s;\nalgorithm\n  s := u * 2.0;\n"
                           "end MDT1.f;\n\nclass MDT1\n  parameter Real p[1] = 1.0;\n  parameter Real p[2] = 2.0;\n"
                           "  parameter Real p[3] = 3.0;\n  parameter Boolean c = true;\n  Real tt = time;\n"
                           "  parameter Real a1[1].k = 2.0 * p[1];\n  Real a1[1].x = a1[1].k * time;\n  Real a1[1].v[1];\n"
                           "  Real a1[1].v[2];\n  parameter Real a1[2].k = 2.0 * p[2];\n  Real a1[2].x = a1[2].k * time;\n"
                           "  Real a1[2].v[1];\n  Real a1[2].v[2];\n  parameter Real a1[3].k = 2.0 * p[3];\n"
                           "  Real a1[3].x = a1[3].k * time;\n  Real a1[3].v[1];\n  Real a1[3].v[2];\n"
                           "  parameter Real a2[1].k = 2.0;\n  Real a2[1].x(start = p[1]) = a2[1].k * time;\n"
                           "  Real a2[1].v[1];\n  Real a2[1].v[2];\n  parameter Real a2[2].k = 2.0;\n"
                           "  Real a2[2].x(start = p[2]) = a2[2].k * time;\n  Real a2[2].v[1];\n  Real a2[2].v[2];\n"
                           "  parameter Real a2[3].k = 2.0;\n  Real a2[3].x(start = p[3]) = a2[3].k * time;\n"
                           "  Real a2[3].v[1];\n  Real a2[3].v[2];\n  parameter Real a3[1].k = p[3];\n"
                           "  Real a3[1].x = a3[1].k * time;\n  Real a3[1].v[1];\n  Real a3[1].v[2];\n"
                           "  parameter Real a3[2].k = p[1];\n  Real a3[2].x = a3[2].k * time;\n  Real a3[2].v[1];\n"
                           "  Real a3[2].v[2];\n  parameter Real a3[3].k = p[2];\n  Real a3[3].x = a3[3].k * time;\n"
                           "  Real a3[3].v[1];\n  Real a3[3].v[2];\n  parameter Real a4[1].k = if c then p[1] else 2.0 * p[1];\n"
                           "  Real a4[1].x = a4[1].k * time;\n  Real a4[1].v[1];\n  Real a4[1].v[2];\n"
                           "  parameter Real a4[2].k = if c then p[2] else 2.0 * p[2];\n  Real a4[2].x = a4[2].k * time;\n"
                           "  Real a4[2].v[1];\n  Real a4[2].v[2];\n  parameter Real a4[3].k = if c then p[3] else 2.0 * p[3];\n"
                           "  Real a4[3].x = a4[3].k * time;\n  Real a4[3].v[1];\n  Real a4[3].v[2];\n"
                           "  parameter Real a5[1].k = MDT1.f(p)[1];\n  Real a5[1].x = a5[1].k * time;\n  Real a5[1].v[1];\n"
                           "  Real a5[1].v[2];\n  parameter Real a5[2].k = MDT1.f(p)[2];\n  Real a5[2].x = a5[2].k * time;\n"
                           "  Real a5[2].v[1];\n  Real a5[2].v[2];\n  parameter Real a5[3].k = MDT1.f(p)[3];\n"
                           "  Real a5[3].x = a5[3].k * time;\n  Real a5[3].v[1];\n  Real a5[3].v[2];\n"
                           "  parameter Real a6[1].k = p[3];\n  Real a6[1].x = a6[1].k * time;\n  Real a6[1].v[1];\n"
                           "  Real a6[1].v[2];\n  parameter Real a6[2].k = p[2];\n  Real a6[2].x = a6[2].k * time;\n"
                           "  Real a6[2].v[1];\n  Real a6[2].v[2];\n  parameter Real a6[3].k = p[1];\n"
                           "  Real a6[3].x = a6[3].k * time;\n  Real a6[3].v[1];\n  Real a6[3].v[2];\n"
                           "  parameter Real a7[1].k = 1.0;\n  Real a7[1].x = p[1] * tt;\n  Real a7[1].v[1];\n"
                           "  Real a7[1].v[2];\n  parameter Real a7[2].k = 1.5;\n  Real a7[2].x = p[2] * tt;\n"
                           "  Real a7[2].v[1];\n  Real a7[2].v[2];\n  parameter Real a7[3].k = 2.0;\n"
                           "  Real a7[3].x = p[3] * tt;\n  Real a7[3].v[1];\n  Real a7[3].v[2];\n"
                           "  parameter Real a8[1].k = p[1];\n  Real a8[1].x = a8[1].k * time;\n  Real a8[1].v[1];\n"
                           "  Real a8[1].v[2];\n  parameter Real a8[2].k = 2.0;\n  Real a8[2].x = a8[2].k * time;\n"
                           "  Real a8[2].v[1];\n  Real a8[2].v[2];\n  parameter Real a8[3].k = 3.0;\n"
                           "  Real a8[3].x = a8[3].k * time;\n  Real a8[3].v[1];\n  Real a8[3].v[2];\n"
                           "  parameter Real a9[1,1].k = p[2];\n  Real a9[1,1].x = a9[1,1].k * time;\n  Real a9[1,1].v[1];\n"
                           "  Real a9[1,1].v[2];\n  parameter Real a9[1,2].k = p[2];\n  Real a9[1,2].x = a9[1,2].k * time;\n"
                           "  Real a9[1,2].v[1];\n  Real a9[1,2].v[2];\n  parameter Real a9[2,1].k = p[2];\n"
                           "  Real a9[2,1].x = a9[2,1].k * time;\n  Real a9[2,1].v[1];\n  Real a9[2,1].v[2];\n"
                           "  parameter Real a9[2,2].k = p[2];\n  Real a9[2,2].x = a9[2,2].k * time;\n  Real a9[2,2].v[1];\n"
                           "  Real a9[2,2].v[2];\n  parameter Real a10[1].k = 1.0;\n  Real a10[1].x = a10[1].k * time;\n"
                           "  Real a10[1].v[1];\n  Real a10[1].v[2];\n  parameter Real a10[2].k = 1.0;\n"
                           "  Real a10[2].x = a10[2].k * time;\n  Real a10[2].v[1];\n  Real a10[2].v[2];\n"
                           "  parameter Real a10[3].k = 1.0;\n  Real a10[3].x = a10[3].k * time;\n  Real a10[3].v[1];\n"
                           "  Real a10[3].v[2];\n  parameter Real b[1].q[1].k = 1.0;\n  Real b[1].q[1].x = b[1].q[1].k * time;\n"
                           "  Real b[1].q[1].v[1];\n  Real b[1].q[1].v[2];\n  parameter Real b[1].q[2].k = 1.0;\n"
                           "  Real b[1].q[2].x = b[1].q[2].k * time;\n  Real b[1].q[2].v[1];\n  Real b[1].q[2].v[2];\n"
                           "  parameter Real b[1].w = p[1];\n  parameter Real b[2].q[1].k = 1.0;\n"
                           "  Real b[2].q[1].x = b[2].q[1].k * time;\n  Real b[2].q[1].v[1];\n  Real b[2].q[1].v[2];\n"
                           "  parameter Real b[2].q[2].k = 1.0;\n  Real b[2].q[2].x = b[2].q[2].k * time;\n"
                           "  Real b[2].q[2].v[1];\n  Real b[2].q[2].v[2];\n  parameter Real b[2].w = p[2];\n"
                           "  Real y1 = 2.0 * p[2] * tt;\n  Real y2 = a1[3].x + a1[2].x + a1[1].x;\nequation\n"
                           "  a1[1].v = {time, a1[1].k};\n  a1[2].v = {time, a1[2].k};\n  a1[3].v = {time, a1[3].k};\n"
                           "  a2[1].v = {time, a2[1].k};\n  a2[2].v = {time, a2[2].k};\n  a2[3].v = {time, a2[3].k};\n"
                           "  a3[1].v = {time, a3[1].k};\n  a3[2].v = {time, a3[2].k};\n  a3[3].v = {time, a3[3].k};\n"
                           "  a4[1].v = {time, a4[1].k};\n  a4[2].v = {time, a4[2].k};\n  a4[3].v = {time, a4[3].k};\n"
                           "  a5[1].v = {time, a5[1].k};\n  a5[2].v = {time, a5[2].k};\n  a5[3].v = {time, a5[3].k};\n"
                           "  a6[1].v = {time, a6[1].k};\n  a6[2].v = {time, a6[2].k};\n  a6[3].v = {time, a6[3].k};\n"
                           "  a7[1].v = {time, a7[1].k};\n  a7[2].v = {time, a7[2].k};\n  a7[3].v = {time, a7[3].k};\n"
                           "  a8[1].v = {time, a8[1].k};\n  a8[2].v = {time, a8[2].k};\n  a8[3].v = {time, a8[3].k};\n"
                           "  a9[1,1].v = {time, a9[1,1].k};\n  a9[1,2].v = {time, a9[1,2].k};\n"
                           "  a9[2,1].v = {time, a9[2,1].k};\n  a9[2,2].v = {time, a9[2,2].k};\n  a10[1].v = {tt, tt};\n"
                           "  a10[2].v = {tt, tt};\n  a10[3].v = {tt, tt};\n  b[1].q[1].v = {time, b[1].q[1].k};\n"
                           "  b[1].q[2].v = {time, b[1].q[2].k};\n  b[2].q[1].v = {time, b[2].q[1].k};\n"
                           "  b[2].q[2].v = {time, b[2].q[2].k};\nend MDT1;\n"));
  // 逐元素的参数作维：各元素同维的带绑定数组、参差的参数数组（绑定逐元素摊分）都正常。期望 = omc 1.25.0 实跑 MDT3。
  YMC_CHECK_EQ(flatten_src("model MDT3\n  model X\n    parameter Integer n = 2;\n    Real x[n] = fill(time, n);\n"
                           "    parameter Real q[n] = fill(1.5, n);\n  end X;\n  model Y\n    parameter Integer n = 2;\n"
                           "    parameter Real q[n] = fill(1.5, n);\n    Real s = sum(q) * time;\n  end Y;\n"
                           "  X a[2](n = {3, 3});\n  X b[2](each n = 1);\n  Y c[2](n = {1, 3});\nend MDT3;\n"),
               std::string("class MDT3\n  final parameter Integer a[1].n = 3;\n  Real a[1].x[1];\n  Real a[1].x[2];\n"
                           "  Real a[1].x[3];\n  parameter Real a[1].q[1] = 1.5;\n  parameter Real a[1].q[2] = 1.5;\n"
                           "  parameter Real a[1].q[3] = 1.5;\n  final parameter Integer a[2].n = 3;\n  Real a[2].x[1];\n"
                           "  Real a[2].x[2];\n  Real a[2].x[3];\n  parameter Real a[2].q[1] = 1.5;\n"
                           "  parameter Real a[2].q[2] = 1.5;\n  parameter Real a[2].q[3] = 1.5;\n"
                           "  final parameter Integer b[1].n = 1;\n  Real b[1].x[1];\n  parameter Real b[1].q[1] = 1.5;\n"
                           "  final parameter Integer b[2].n = 1;\n  Real b[2].x[1];\n  parameter Real b[2].q[1] = 1.5;\n"
                           "  final parameter Integer c[1].n = 1;\n  parameter Real c[1].q[1] = 1.5;\n"
                           "  Real c[1].s = c[1].q[1] * time;\n  final parameter Integer c[2].n = 3;\n"
                           "  parameter Real c[2].q[1] = 1.5;\n  parameter Real c[2].q[2] = 1.5;\n"
                           "  parameter Real c[2].q[3] = 1.5;\n  Real c[2].s = (c[2].q[1] + c[2].q[2] + c[2].q[3]) * time;\n"
                           "equation\n  a[1].x = {time, time, time};\n  a[2].x = {time, time, time};\n  b[1].x = {time};\n"
                           "  b[2].x = {time};\nend MDT3;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 混排字面量的末元素是 Real：omc 前面的 Integer 元素不转型（`parameter Real a[1].k = 1;`），不跟（MDS1）。
           {"model MDS1\n  model A\n    parameter Real k = 1;\n  end A;\n  A a[3](k = {1, 2, 3.0});\nend MDS1;\n", "composite array modifier shape/type"},
           // 切片长度与数组维不等：1.25.0 放行并打出 `a[3].k = p[3]`，不跟（MDS2）。
           {"model MDS2\n  model A\n    parameter Real k = 1;\n  end A;\n  parameter Real p[3] = {1, 2, 3};\n"
            "  A a[3](k = p[1:2]);\nend MDS2;\n", "composite array modifier shape/type"},
           // 书写的 `(推导式)[k]`：omc 代入后不再化简（`/*Real*/(2) * time`，MD 探针 m4），不复刻（MDS3）。
           {"model MDS3\n  Real a = ({i * time for i in 1:3})[2];\nend MDS3;\n", "subscripted comprehension"},
           // 参差的带绑定非参数数组：omc 内部错（NFSubscript.toDAEExp failed on unknown subscript），不复刻（MDS4）。
           {"model MDS4\n  model X\n    parameter Integer n = 2;\n    Real x[n] = fill(time, n);\n  end X;\n"
            "  X xs[2](n = {1, 3});\nend MDS4;\n", "ragged component array binding"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_constant_binding_variability) {
  // 归约实参里的参数不取值：`f(sum(p))` 留成 `f(p[1] + p[2])`、`f(max(p))` 原样（此前 eval_reduction 把 allow_param 写死
  // 成 true，整个调用被折成字面量、p 连带 final）；下标位置上的参数是结构参数（`f(c[n])` → 4.0、n final）；已知维的
  // size(a, k) 是 constant 变率，可以绑给 constant。期望 = omc 1.25.0 实跑 MDT2（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model MDT2\n  function f\n    input Real u;\n    output Real y;\n  algorithm\n    y := 2 * u;\n"
                           "  end f;\n  record R\n    Real x;\n  end R;\n  constant Real c[3] = {1, 2, 3};\n"
                           "  parameter Integer n = 2;\n  parameter Real p[2] = {1, 2};\n  parameter Real q1 = f(sum(p));\n"
                           "  parameter Real q2 = f(max(p));\n  parameter R r = R(product(p));\n"
                           "  Real y = f(sum(c)) * time + f(c[n]);\n  constant Integer k1 = size(p, 1);\n"
                           "  constant Integer k2 = size(ones(n, 3), 1);\n  constant Real k3[2] = fill(1.5, size(p, 1));\n"
                           "end MDT2;\n"),
               std::string("function MDT2.f\n  input Real u;\n  output Real y;\nalgorithm\n  y := 2.0 * u;\nend MDT2.f;\n\n"
                           "class MDT2\n  constant Real c[1] = 1.0;\n  constant Real c[2] = 2.0;\n  constant Real c[3] = 3.0;\n"
                           "  final parameter Integer n = 2;\n  parameter Real p[1] = 1.0;\n  parameter Real p[2] = 2.0;\n"
                           "  parameter Real q1 = MDT2.f(p[1] + p[2]);\n  parameter Real q2 = MDT2.f(max(p));\n"
                           "  parameter Real r.x = p[1] * p[2];\n  Real y = 12.0 * time + 4.0;\n  constant Integer k1 = 2;\n"
                           "  constant Integer k2 = 2;\n  constant Real k3[1] = 1.5;\n  constant Real k3[2] = 1.5;\nend MDT2;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // constant 的绑定引用 parameter（只出现在下标 / 范围 / fill 个数这类结构位置也算）：绑定变率高于组件，omc 报错
           //（checkComponentBindingVariability，NFTyping.mo:1121-1160 v1.25.0）。MDS5 标量、MDS6 数组、MDS7 记录。
           {"model MDS5\n  parameter Integer n = 2;\n  constant Real c[3] = {1, 2, 3};\n"
            "  constant Real q = c[n];\nend MDS5;\n", "binding of higher variability"},
           {"model MDS6\n  parameter Integer n = 2;\n  constant Real q[n] = fill(1.5, n);\nend MDS6;\n", "array binding higher variability"},
           {"model MDS7\n  record R\n    Real x;\n  end R;\n  constant Real c[3] = {1, 2, 3};\n"
            "  parameter Integer n = 2;\n  constant R r = R(c[n]);\nend MDS7;\n", "record binding via structural parameter"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_constant_array_subscripts) {
  // constant 数组带下标的引用：omc 把引用换成值、下标不求值地应用上去（Expression.applySubscripts）——求得出的下标（常量、
  // 参数——结构参数、打 final）取元素，切片 / `:` 逐元素应用，运行时下标连同其后的留成 `{…}[n, 1]`；之后"各元素相等"的
  // 字面量数组取符号下标化成该元素（simplifySubscriptedExp：`u[n]` → `1.5`）。Integer 常量数组进 Real 语境时转型留在外面
  //（`/*Real*/({3, 1, 2}[n])`）。来源：本地常量、子实例里的（`a.c[n]`，此前打成对常量的引用）、记录字段、包常量。
  // 期望 = omc 1.25.0 实跑 CXT1（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model CXT1\n  package P\n    constant Real c[3] = {1, 2, 3};\n  end P;\n  model A\n"
                           "    constant Real c[3] = {1, 2, 3};\n  end A;\n  record R\n    Real v[3];\n  end R;\n"
                           "  constant Real x[5] = {1, 2, 3, 4, 5};\n  constant Real m[2, 3] = {{1, 2, 3}, {4, 5, 6}};\n"
                           "  constant Integer k[3] = {3, 1, 2};\n  constant Real u[3] = fill(1.5, 3);\n"
                           "  constant R r(v = {1, 2, 3});\n  parameter Integer p = 2;\n  Integer n = integer(time) + 1;\n"
                           "  A a;\n  Real y1[2] = x[2:3];\n  Real y2[3] = x[{5, 1, 3}];\n  Real y3[2] = m[:, 2];\n"
                           "  Real y4[2] = x[end - 1:end] * time;\n  Real y5 = x[n];\n  Real y6 = m[1, n] + m[n, 1];\n"
                           "  Real y7[2] = m[:, n];\n  Real y8[2] = x[{n, 1}];\n  Real y9 = x[p];\n  Real y10 = k[n];\n"
                           "  Real y11 = x[k[n]];\n  Real y12 = u[n];\n  Real y13 = a.c[n] + r.v[n] + P.c[n];\n"
                           "  Real y14[2] = a.c[2:3];\n  Real s;\nalgorithm\n  s := 0;\n  for i in 1:3 loop\n"
                           "    s := s + x[i] + m[1, i];\n  end for;\nend CXT1;\n"),
               std::string("class CXT1\n  constant Real x[1] = 1.0;\n  constant Real x[2] = 2.0;\n  constant Real x[3] = 3.0;\n"
                           "  constant Real x[4] = 4.0;\n  constant Real x[5] = 5.0;\n  constant Real m[1,1] = 1.0;\n"
                           "  constant Real m[1,2] = 2.0;\n  constant Real m[1,3] = 3.0;\n  constant Real m[2,1] = 4.0;\n"
                           "  constant Real m[2,2] = 5.0;\n  constant Real m[2,3] = 6.0;\n  constant Integer k[1] = 3;\n"
                           "  constant Integer k[2] = 1;\n  constant Integer k[3] = 2;\n  constant Real u[1] = 1.5;\n"
                           "  constant Real u[2] = 1.5;\n  constant Real u[3] = 1.5;\n  constant Real r.v[1] = 1.0;\n"
                           "  constant Real r.v[2] = 2.0;\n  constant Real r.v[3] = 3.0;\n  final parameter Integer p = 2;\n"
                           "  Integer n = integer(time) + 1;\n  constant Real a.c[1] = 1.0;\n  constant Real a.c[2] = 2.0;\n"
                           "  constant Real a.c[3] = 3.0;\n  Real y1[1];\n  Real y1[2];\n  Real y2[1];\n  Real y2[2];\n"
                           "  Real y2[3];\n  Real y3[1];\n  Real y3[2];\n  Real y4[1];\n  Real y4[2];\n"
                           "  Real y5 = {1.0, 2.0, 3.0, 4.0, 5.0}[n];\n"
                           "  Real y6 = {1.0, 2.0, 3.0}[n] + {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}[n, 1];\n  Real y7[1];\n"
                           "  Real y7[2];\n  Real y8[1];\n  Real y8[2];\n  Real y9 = 2.0;\n"
                           "  Real y10 = /*Real*/({3, 1, 2}[n]);\n  Real y11 = {1.0, 2.0, 3.0, 4.0, 5.0}[{3, 1, 2}[n]];\n"
                           "  Real y12 = 1.5;\n  Real y13 = {1.0, 2.0, 3.0}[n] + {1.0, 2.0, 3.0}[n] + {1.0, 2.0, 3.0}[n];\n"
                           "  Real y14[1];\n  Real y14[2];\n  Real s;\nequation\n  y1 = {2.0, 3.0};\n  y2 = {5.0, 1.0, 3.0};\n"
                           "  y3 = {2.0, 5.0};\n  y4 = {4.0 * time, 5.0 * time};\n"
                           "  y7 = {{1.0, 2.0, 3.0}[n], {4.0, 5.0, 6.0}[n]};\n  y8 = {{1.0, 2.0, 3.0, 4.0, 5.0}[n], 1.0};\n"
                           "  y14 = {2.0, 3.0};\nalgorithm\n  s := 0.0;\n  for i in 1:3 loop\n"
                           "    s := s + {1.0, 2.0, 3.0, 4.0, 5.0}[i] + {1.0, 2.0, 3.0}[i];\n  end for;\nend CXT1;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 没取尽各维的符号下标（`m[n]` → 数组值的 `{{…}, {…}}[n]`）：下游还不认（CXS1）。
           {"model CXS1\n  constant Real x[5] = {1, 2, 3, 4, 5};\n"
            "  constant Real m[2, 3] = {{1, 2, 3}, {4, 5, 6}};\n  Integer n = integer(time) + 1;\n"
            "  Real tt = time;\n  Real y[3] = m[n];\nend CXS1;\n", "array-valued subscripted constant"},
           // 求不出的范围下标：omc 留成 `{…}[n:n + 1]`，未做（CXS2）。
           {"model CXS2\n  constant Real x[5] = {1, 2, 3, 4, 5};\n"
            "  constant Real m[2, 3] = {{1, 2, 3}, {4, 5, 6}};\n  Integer n = integer(time) + 1;\n"
            "  Real tt = time;\n  Real y[2] = x[n:n + 1];\nend CXS2;\n", "constant array subscript"},
           // 越界的字面量下标：omc 不查、照打 `{1.0, …}[7]`，不跟（CXS3）。
           {"model CXS3\n  constant Real x[5] = {1, 2, 3, 4, 5};\n"
            "  constant Real m[2, 3] = {{1, 2, 3}, {4, 5, 6}};\n  Integer n = integer(time) + 1;\n"
            "  Real tt = time;\n  Real y = x[7];\nend CXS3;\n", "array subscript out of range"},
           // fixed = false 的参数作下标：omc 把它标成结构参数（打 final）而下标仍是符号，不跟（CXS7）。
           {"model CXS7\n  constant Real x[5] = {1, 2, 3, 4, 5};\n"
            "  constant Real m[2, 3] = {{1, 2, 3}, {4, 5, 6}};\n  Integer n = integer(time) + 1;\n"
            "  Real tt = time;\n  parameter Integer q(fixed = false);\n  Real y = x[q];\ninitial equation\n"
            "  q = 2;\nend CXS7;\n", "parameter in symbolic subscript"},
           // 符号下标后面跟 `:`、整条留在绑定里：omc 内部错（NFSubscript.toDAEExp failed on unknown subscript ':'，2026-10-11
           // 复核），CXS8。64d 起等式里逐元素展开的照做（ks 探针），这里在绑定处就 SKIP。
           {"model CXS8\n  constant Real x[5] = {1, 2, 3, 4, 5};\n"
            "  constant Real m[2, 3] = {{1, 2, 3}, {4, 5, 6}};\n  Integer n = integer(time) + 1;\n"
            "  Real tt = time;\n  Real y[3] = m[n, :];\nend CXS8;\n", "non-literal array binding"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_binding_variability_on_written_expression) {
  // 变率检查做在书写的绑定上，例外照 omc：constant 记录里无前缀的字段也是 constant（`final constant Real y = x / 2.0`）、
  // ndims(v) / size(v, k) 与 v 的变率无关；书写的 `({1.5, 1.5, 1.5})[k]` 同样按"各元素相等"化成元素。
  // 期望 = omc 1.25.0 实跑 CXT2（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model CXT2\n  record R\n    Real x;\n    final constant Real y = x / 2.0;\n  end R;\n"
                           "  constant R r = R(1.0);\n  Real v[2, 3];\n  parameter Integer n = ndims(v);\n"
                           "  parameter Integer m = size(v, 2);\n  Real tt = time;\n"
                           "  Real w = ({1.5, 1.5, 1.5})[integer(tt) + 1];\nend CXT2;\n"),
               std::string("class CXT2\n  constant Real r.x = 1.0;\n  final constant Real r.y = 0.5;\n  Real v[1,1];\n"
                           "  Real v[1,2];\n  Real v[1,3];\n  Real v[2,1];\n  Real v[2,2];\n  Real v[2,3];\n"
                           "  parameter Integer n = 2;\n  parameter Integer m = 3;\n  Real tt = time;\n  Real w = 1.5;\n"
                           "end CXT2;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // parameter 的绑定 / 属性值引用变量，而化简会把它消掉（`0 * tt`、`tt - tt`）：omc 看的是化简前的绑定，报"绑定变率
           // 高于组件"（checkComponentBindingVariability，NFTyping.mo:1121-1160 v1.25.0）。CXS4 标量、CXS5 属性、CXS6 数组。
           {"model CXS4\n  constant Real x[5] = {1, 2, 3, 4, 5};\n"
            "  constant Real m[2, 3] = {{1, 2, 3}, {4, 5, 6}};\n  Integer n = integer(time) + 1;\n"
            "  Real tt = time;\n  parameter Real q = 0 * tt;\nend CXS4;\n", "binding of higher variability"},
           {"model CXS5\n  constant Real x[5] = {1, 2, 3, 4, 5};\n"
            "  constant Real m[2, 3] = {{1, 2, 3}, {4, 5, 6}};\n  Integer n = integer(time) + 1;\n"
            "  Real tt = time;\n  Real y(start = tt - tt) = tt;\nend CXS5;\n", "attribute binding of higher variability"},
           {"model CXS6\n  constant Real x[5] = {1, 2, 3, 4, 5};\n"
            "  constant Real m[2, 3] = {{1, 2, 3}, {4, 5, 6}};\n  Integer n = integer(time) + 1;\n"
            "  Real tt = time;\n  parameter Real q[2] = {tt * 0, 1};\nend CXS6;\n", "array binding higher variability"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_constant_reductions_with_outer_iterators) {
  // 变率 ≤ 结构参数的归约 omc 走"求值"一支；体里有外层保留着的常量变率迭代变量（for 语句 / 数组构造子，范围是常量）时
  // 求不出值，整个归约原样留着（`/*Real*/(sum(i * j for j in 1:3))`、`max(i * j for j in 1:3)`）；体里另有变量的
  //（`sum(i * j * tt …)`）、迭代变量的范围带普通参数 / 运行时量的（`for i in 1:p`、`for i in 1:n`）照常展开。
  // 期望 = omc 1.25.0 实跑 CXT3（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model CXT3\n  constant Real x[3] = {1, 2, 3};\n  parameter Integer p = 2;\n"
                           "  Integer n = integer(time) + 1;\n  Real tt = time;\n  Real y[2];\n"
                           "  Real z[2] = {sum(i * j for j in 1:3) * tt for i in 1:2};\n  Real w;\nalgorithm\n"
                           "  for i in 1:2 loop\n"
                           "    y[i] := sum(i * j for j in 1:3) + sum(x[j] * i for j in 1:3) + sum(i * j * tt for j in 1:3) + max(i * j for j in 1:3);\n"
                           "  end for;\n  for i in 1:p loop\n    w := sum(i * j for j in 1:3);\n  end for;\n"
                           "  for i in 1:n loop\n    w := w + sum(i * j for j in 1:3);\n  end for;\nend CXT3;\n"),
               std::string("class CXT3\n  constant Real x[1] = 1.0;\n  constant Real x[2] = 2.0;\n  constant Real x[3] = 3.0;\n"
                           "  final parameter Integer p = 2;\n  Integer n = integer(time) + 1;\n  Real tt = time;\n"
                           "  Real y[1];\n  Real y[2];\n  Real z[1];\n  Real z[2];\n  Real w;\nequation\n"
                           "  z = array(/*Real*/(sum(i * j for j in 1:3)) * tt for i in 1:2);\nalgorithm\n  for i in 1:2 loop\n"
                           "    y[i] := /*Real*/(sum(i * j for j in 1:3)) + sum({1.0, 2.0, 3.0}[j] * /*Real*/(i) for j in 1:3) + /*Real*/(i) * tt + /*Real*/(i * 2) * tt + /*Real*/(i * 3) * tt + /*Real*/(max(i * j for j in 1:3));\n"
                           "  end for;\n  for i in 1:2 loop\n    w := /*Real*/(i + i * 2 + i * 3);\n  end for;\n"
                           "  for i in 1:n loop\n    w := w + /*Real*/(i + i * 2 + i * 3);\n  end for;\nend CXT3;\n"));
}

YMC_TEST(flatten_connect_subscript_expressions) {
  // connect 实参在采集处解析成元素：下标是表达式（`p[i + 1]`、`p[N + 1]`、`p[end - 1:end]`、`s.ports[end]`）——常量 / 迭代
  // 变量的算式直接求，含参数的按结构参数求值（N final），`end` = 该维大小；各段的维取发射时记下的实际形状（`A a(M = 3)`
  // 里的 `C p[M]`、`S s(nPorts = 2)` 的 `ports[nPorts]`，此前按声明的缺省值求、少连了元素）；零长度的整取不产生方程。
  // 期望 = omc 1.25.0 实跑 CNT1（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model CNT1\n  connector C\n    Real e;\n    flow Real f;\n  end C;\n  model S\n"
                           "    parameter Integer nPorts = 0;\n    C ports[nPorts];\n  end S;\n  model A\n"
                           "    parameter Integer M = 2;\n    C p[M], q[M];\n  equation\n    connect(p, q);\n  end A;\n"
                           "  parameter Integer N = 3;\n  C p[4];\n  C q[2];\n  C r;\n  S s(nPorts = 2);\n  A a(M = 3);\n"
                           "  C z1[0], z2[0];\nequation\n  for i in 1:N - 1 loop\n    connect(p[i], p[i + 1]);\n  end for;\n"
                           "  connect(p[N + 1], r);\n  connect(p[end - 1:end], q);\n  connect(s.ports[end], r);\n"
                           "  connect(z1, z2);\nend CNT1;\n"),
               std::string("class CNT1\n  final parameter Integer N = 3;\n  Real p[1].e;\n  Real p[1].f;\n  Real p[2].e;\n"
                           "  Real p[2].f;\n  Real p[3].e;\n  Real p[3].f;\n  Real p[4].e;\n  Real p[4].f;\n  Real q[1].e;\n"
                           "  Real q[1].f;\n  Real q[2].e;\n  Real q[2].f;\n  Real r.e;\n  Real r.f;\n"
                           "  final parameter Integer s.nPorts = 2;\n  Real s.ports[1].e;\n  Real s.ports[1].f;\n"
                           "  Real s.ports[2].e;\n  Real s.ports[2].f;\n  final parameter Integer a.M = 3;\n  Real a.p[1].e;\n"
                           "  Real a.p[1].f;\n  Real a.p[2].e;\n  Real a.p[2].f;\n  Real a.p[3].e;\n  Real a.p[3].f;\n"
                           "  Real a.q[1].e;\n  Real a.q[1].f;\n  Real a.q[2].e;\n  Real a.q[2].f;\n  Real a.q[3].e;\n"
                           "  Real a.q[3].f;\nequation\n  a.p[1].e = a.q[1].e;\n  -(a.p[1].f + a.q[1].f) = 0.0;\n"
                           "  a.p[2].e = a.q[2].e;\n  -(a.p[2].f + a.q[2].f) = 0.0;\n  a.p[3].e = a.q[3].e;\n"
                           "  -(a.p[3].f + a.q[3].f) = 0.0;\n  p[3].e = q[1].e;\n  p[3].e = p[2].e;\n  p[3].e = p[1].e;\n"
                           "  -(p[3].f + q[1].f + p[2].f + p[1].f) = 0.0;\n  s.ports[2].e = r.e;\n  s.ports[2].e = p[4].e;\n"
                           "  s.ports[2].e = q[2].e;\n  s.ports[2].f - r.f - p[4].f - q[2].f = 0.0;\n  p[1].f = 0.0;\n"
                           "  p[2].f = 0.0;\n  p[3].f = 0.0;\n  p[4].f = 0.0;\n  q[1].f = 0.0;\n  q[2].f = 0.0;\n  r.f = 0.0;\n"
                           "  s.ports[1].f = 0.0;\n  a.p[1].f = 0.0;\n  a.p[2].f = 0.0;\n  a.p[3].f = 0.0;\n  a.q[1].f = 0.0;\n"
                           "  a.q[2].f = 0.0;\n  a.q[3].f = 0.0;\nend CNT1;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 越界的下标：omc 不查，照打一个不存在的 `p[5].e = r.e`，不跟（CNS1）。
           {"model CNS1\n  connector C\n    Real e;\n    flow Real f;\n  end C;\n  parameter Integer N = 3;\n"
            "  C p[4], r;\nequation\n  connect(p[N + 2], r);\nend CNS1;\n", "connect operand shape"},
           // cardinality 的实参带参数下标：omc 的计数表按 cref 字面形态查，`cardinality(p[N])` 恒得 0，不复刻（CNS2）。
           {"model CNS2\n  connector C\n    Real e;\n    flow Real f;\n  end C;\n  parameter Integer N = 3;\n"
            "  C p[3];\n  Real y;\nequation\n  if cardinality(p[N]) == 0 then\n    y = 0;\n  else\n    y = 1;\n"
            "  end if;\n  connect(p[1], p[N]);\nend CNS2;\n", "cardinality argument"},
           // 经父路径整取各实例维不同的连接器数组：omc 内部错（makeConnectors failed to expand connector），CNS3。
           {"model CNS3\n  connector C\n    Real e;\n    flow Real f;\n  end C;\n  model A\n"
            "    parameter Integer M = 2;\n    C p[M];\n  end A;\n  A a[2](M = {2, 3});\n  C q[2];\nequation\n"
            "  connect(a[1].p, q);\nend CNS3;\n", "connect operand shape"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_function_end_from_written_dims) {
  // 函数体下标里的 `end` 取类型里的维：书写成表达式的维照写（`y[size(x, 1)]` 的 `y[3:end]` → `y[3:size(x, 1)]`、
  // `z[size(x, 1) + 1]` 的 `z[end]` → `z[size(x, 1) + 1]`、`w[n]` 的 `w[end]` → `w[n]`；此前一律打成 `size(y, 1)`），
  // `:` 维才是 `size(x, 1)`。期望 = omc 1.25.0 实跑 SLT1（2026-10-03）。
  YMC_CHECK_EQ(flatten_src("model SLT1\n  function f\n    input Real x[:];\n    input Integer n;\n"
                           "    output Real y[size(x, 1)];\n  protected\n    Real z[size(x, 1) + 1];\n    Real w[n];\n"
                           "  algorithm\n    y := x;\n    y[3:end] := x[3:end];\n    y[n:end] := zeros(size(x, 1) - n + 1);\n"
                           "    z[1:end - 1] := x;\n    z[end] := 1;\n    w := x[1:n];\n    w[end] := z[end];\n"
                           "    y[1] := sum(w);\n  end f;\n  Real y[3] = f({time, 2, 3}, 2);\nend SLT1;\n"),
               std::string("function SLT1.f\n  input Real[:] x;\n  input Integer n;\n  output Real[size(x, 1)] y;\n"
                           "  protected Real[size(x, 1) + 1] z;\n  protected Real[n] w;\nalgorithm\n  y := x;\n"
                           "  y[3:size(x, 1)] := x[3:size(x, 1)];\n  y[n:size(x, 1)] := fill(0.0, size(x, 1) - n + 1);\n"
                           "  z[1:size(x, 1) + 1 - 1] := x;\n  z[size(x, 1) + 1] := 1.0;\n  w := x[1:n];\n"
                           "  w[n] := z[size(x, 1) + 1];\n  y[1] := sum(w);\nend SLT1.f;\n\nclass SLT1\n  Real y[1];\n"
                           "  Real y[2];\n  Real y[3];\nequation\n  y = SLT1.f({time, 2.0, 3.0}, 2);\nend SLT1;\n"));
}

YMC_TEST(inst_imports_extends_scope_and_inherited_sections) {
  // inst 侧三张卡（T-S3-05 / 06 / 07）一起端到端：类型名经 import 查（限定 `import P.M`、重命名 `import S = P.Q` 后的
  // `S.N`）；extends 的基类名从扩展类自身查（本类的嵌套类 `Base`）；继承来的方程段按"本类在前、各 extends 逆序在后"
  // 收（`n.y = …` 在前、继承自 NB 的 `n.z = …` 在后，根类自己继承的 `b = …` 排在子组件之后）。
  // 期望 = omc 1.25.0 实跑 IMT1（2026-10-05）。
  YMC_CHECK_EQ(flatten_src("package P\n  model M\n    Real x = time;\n  end M;\n  package Q\n    model NB\n      Real z;\n"
                           "    equation\n      z = 3 * time;\n    end NB;\n    model N\n      extends NB;\n"
                           "      parameter Real k = 1;\n      Real y;\n    equation\n      y = k * time;\n    end N;\n"
                           "  end Q;\nend P;\n\nmodel IMT1\n  import P.M;\n  import S = P.Q;\n  model Base\n    Real b;\n"
                           "  equation\n    b = 2 * time;\n  end Base;\n  extends Base;\n  M m;\n  S.N n(k = 2);\nend IMT1;\n"),
               std::string("class IMT1\n  Real b;\n  Real m.x = time;\n  Real n.z;\n  parameter Real n.k = 2.0;\n  Real n.y;\n"
                           "equation\n  n.y = n.k * time;\n  n.z = 3.0 * time;\n  b = 2.0 * time;\nend IMT1;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 两个 `.*` 都提供 M：用到即冲突（addImportConflict，NFClassTree.mo:1757-1800），omc 报错（IMS1）。
           {"package P\n  model M\n    Real x = time;\n  end M;\nend P;\n\npackage R\n  model M\n"
            "    Real x = 2;\n  end M;\nend R;\n\nmodel IMS1\n  import P.*;\n  import R.*;\n  M m;\nend IMS1;\n", "conflicting imports"},
           // 基类名的各段须传递地非 replaceable（checkReplaceableBaseClass，NFInst.mo:842-883），omc 报错（IMS2）。
           {"model IMS2\n  replaceable model M\n    Real x = time;\n  end M;\n  extends M;\nend IMS2;\n", "replaceable base class"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(inst_short_class_aliasing_composite) {
  // 短类别名到长类（MLS §4.5.1：`model B = A(k = 2)` = 基类 + 修饰）：实例化链底的长类，链上的修饰按最低优先级并进组件
  // 的修饰（组件自己的 `b2(k = 7)` 盖过短类的 `k = 2`；外层短类 `C = B(k = 3)` 盖过内层），值在短类定义处解析（包里的
  // 短类引包常量 `2 * c`）；记录整体绑定 `S s2 = R(b = time)` 用 R 的构造缺省（s2.a = 1.0，不是 S 的 5.0）。
  // 期望 = omc 1.25.0 实跑 SCT1（2026-10-05）。
  YMC_CHECK_EQ(flatten_src("model A\n  parameter Real k = 1;\n  Real x = k * time;\nend A;\nrecord R\n  Real a = 1 \"first\";\n"
                           "  Real b = 2;\nend R;\npackage P\n  constant Real c = 4;\n  model B = A(k = 2 * c);\nend P;\n"
                           "model SCT1\n  model B = A(k = 2);\n  model C = B(k = 3);\n  record S = R(a = 5);\n  B b1;\n"
                           "  B b2(k = 7);\n  C c;\n  P.B pb;\n  S s1;\n  S s2 = R(b = time);\nend SCT1;\n"),
               std::string("class SCT1\n  parameter Real b1.k = 2.0;\n  Real b1.x = b1.k * time;\n  parameter Real b2.k = 7.0;\n"
                           "  Real b2.x = b2.k * time;\n  parameter Real c.k = 3.0;\n  Real c.x = c.k * time;\n"
                           "  parameter Real pb.k = 8.0;\n  Real pb.x = pb.k * time;\n  Real s1.a = 5.0 \"first\";\n"
                           "  Real s1.b = 2.0;\n  Real s2.a = 1.0 \"first\";\n  Real s2.b = time;\nend SCT1;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 链底长类 partial：omc "Component 'b' has partial type 'B'"（SCS1）。
           {"partial model A\n  Real x = time;\nend A;\nmodel SCS1\n  model B = A;\n  B b;\nend SCS1;\n", "partial type"},
           // `class C2 = C`（C 是连接器）：组件种类按短类的受限类型，C2 不是连接器 → omc "c1 is not a valid connector"（SCS2）。
           {"connector C\n  Real e;\n  flow Real f;\nend C;\nmodel SCS2\n  class C2 = C;\n  C2 c1, c2;\n"
            "equation\n  connect(c1, c2);\nend SCS2;\n", "short class restriction mismatch"},
           // 记录字段缺省值进构造函数、在函数上下文里定型 → omc "time is not allowed in a function"（SCS3）。
           {"record R\n  Real a = time;\nend R;\nmodel SCS3\n  R r;\nend SCS3;\n", "time in record field default"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_final_attributes_and_array_derived_types) {
  // 属性修饰上的 final（MSL 的 SI 类型 `type Voltage = Real(final quantity = …, final unit = "V")`）：值照常取、final 不打印；
  // 数组组件的类型链修饰每个元素同值（omc 的 fill）、被外层按元素切分的 `start = {2, 3}` 盖过；extends 里的属性修饰比组件
  // 声明的修饰外一层（`extends A(x(start = 2.0))` 盖过 A 里的 `start = 1.0`）。期望 = omc 1.25.0 实跑 FAT1（2026-10-05）。
  YMC_CHECK_EQ(flatten_src("model A\n  Real x(start = 1.0) = time;\nend A;\nmodel FAT1\n"
                           "  type V = Real(final unit = \"V\", start = 1);\n  V v[2](start = {2, 3}) = {time, time};\n"
                           "  V w(displayUnit = \"kV\") = time;\n  extends A(x(start = 2.0));\nend FAT1;\n"),
               std::string("class FAT1\n  Real v[1](unit = \"V\", start = 2.0);\n  Real v[2](unit = \"V\", start = 3.0);\n"
                           "  Real w(unit = \"V\", displayUnit = \"kV\", start = 1.0) = time;\n  Real x(start = 2.0) = time;\n"
                           "equation\n  v = {time, time};\nend FAT1;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 外层再改 final 的属性：omc "Trying to override final element unit"（FAS1，类型里的 final）。
           {"model FAS1\n  type V = Real(final unit = \"V\");\n  V u(unit = \"A\") = time;\nend FAS1;\n", "final element override"},
           // extends 修饰改基类里 final 的属性：同上（FAS2，scodeinst/BuiltinAttribute8 同形）。
           {"model A\n  Real x(final start = 1.0) = time;\nend A;\nmodel FAS2\n  extends A(x(start = 2.0));\n"
            "end FAS2;\n", "final element override"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_builtin_enums_and_library_connectors) {
  // 内建枚举 StateSelect 当组件类型（顶层合成的枚举类），`stateSelect = s` 里的 s 是结构参数：打 final、属性打求值后的字面量；
  // 包里的短类连接器（`P.RI`）与继承来的连接器（`extends P.SO` 的 y）可以 connect。期望 = omc 1.25.0 实跑 SST1（2026-10-06）。
  YMC_CHECK_EQ(flatten_src("package P\n  connector RI = input Real;\n  connector RO = output Real;\n  partial block SO\n"
                           "    RO y;\n  end SO;\nend P;\nblock Src\n  extends P.SO;\nequation\n  y = time;\nend Src;\n"
                           "model SST1\n  parameter StateSelect s = StateSelect.prefer;\n  Real x(stateSelect = s) = time;\n"
                           "  Src src;\n  P.RI u;\nequation\n  connect(src.y, u);\nend SST1;\n"),
               std::string("class SST1\n"
                           "  final parameter enumeration(never, avoid, default, prefer, always) s = StateSelect.prefer;\n"
                           "  Real x(stateSelect = StateSelect.prefer) = time;\n  Real src.y;\n  input Real u;\nequation\n"
                           "  src.y = u;\n  src.y = time;\nend SST1;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 更深一层基类 extends 修饰里的 stateSelect 参数：预扫没扫到（omc 能过，参数打 final），保守 SKIP（SSS1）。
           {"model A\n  Real x = time;\nend A;\nmodel B\n  parameter StateSelect s = StateSelect.prefer;\n"
            "  extends A(x(stateSelect = s));\nend B;\nmodel C\n  extends B;\nend C;\nmodel SSS1\n  C c;\n"
            "end SSS1;\n", "stateSelect parameter not pre-scanned"},
           // 不存在的字面量：omc "Variable StateSelect.foo not found"（SSS2）。
           {"model SSS2\n  parameter StateSelect s = StateSelect.foo;\nend SSS2;\n", "unknown enumeration literal"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(inst_extends_redeclare_and_dropped_final) {
  // extends 修饰里的组件重声明：新类型 + 重声明自己的修饰 + 原声明的修饰当约束修饰（a.p = o 来自 `replaceable A a(p = o)`），
  // 叶组件的绑定取重声明的、注释取原声明的；连接器里经短类定型的成员，final 修饰给的 final 不打印（omc 的
  // mergeDerivedAttributes）。期望 = omc 1.25.0 实跑 RDT1（2026-10-06）。
  YMC_CHECK_EQ(flatten_src("model A\n  parameter Real p = 1;\nend A;\nmodel B\n  extends A;\n  parameter Real q = 2;\nend B;\n"
                           "model M\n  parameter Real o = 5;\n  replaceable A a(p = o);\n  replaceable Real x = 1 \"orig\";\n"
                           "end M;\ntype TT = Real(final unit = \"K\");\nconnector C\n  TT T;\n  flow Real Q;\nend C;\n"
                           "model RDT1\n  extends M(redeclare B a(q = 3), redeclare Real x = 2 \"new\");\n"
                           "  C c(final T = time);\nend RDT1;\n"),
               std::string("class RDT1\n  parameter Real o = 5.0;\n  parameter Real a.p = o;\n  parameter Real a.q = 3.0;\n"
                           "  Real x = 2.0 \"orig\";\n  Real c.T(unit = \"K\") = time;\n  Real c.Q;\nequation\n  c.Q = 0.0;\n"
                           "end RDT1;\n"));
  // constrainedby：约束修饰取 constrainedby 的修饰（61i 放开；期望 = omc 1.25.0 实跑 RDS2）。
  YMC_CHECK_EQ(flatten_src("model A\n  parameter Real p = 1;\nend A;\nmodel B\n  extends A;\n  parameter Real q = 2;\nend B;\n"
                           "model M\n  replaceable A a constrainedby A(p = 7);\nend M;\nmodel RDS2\n"
                           "  extends M(redeclare B a);\nend RDS2;\n"),
               std::string("class RDS2\n  parameter Real a.p = 7.0;\n  parameter Real a.q = 2.0;\nend RDS2;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 原声明不是 replaceable 而类型不同：omc "Redeclaration with a new type requires 'a' to be replaceable"（RDS1）。
           {"model A\n  parameter Real p = 1;\nend A;\nmodel B\n  extends A;\n  parameter Real q = 2;\nend B;\n"
            "model M\n  A a;\nend M;\nmodel RDS1\n  extends M(redeclare B a);\nend RDS1;\n", "redeclare of non-replaceable component"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_modifier_layers_with_own_scopes) {
  // 修饰各层带自己的作用域：extends 修饰改继承来的复合组件（b.c.z = 2.0 来自 B 的 `extends A(c(z = 2))`，叠在 A 里 `C c(x = q)`
  // 之上；数组 d 的 `x = {5, 6}` 逐元素切分）、实例修饰再叠一层（b.c.y 的 start = p 在根里解析，b.c.x = b.q 在 b 里解析；
  // `d(each z = 8)` 盖过声明的 `z = {3, 4}`）、整体绑定的记录叠属性修饰（b.r.a(start = 7.0) = 3.0）、`final c(…)` 让整棵子树
  // final。期望 = omc 1.25.0 实跑 ECT1（2026-10-06）。
  YMC_CHECK_EQ(flatten_src("model C\n  parameter Real x = 1;\n  parameter Real z = 1;\n  Real y = x * time;\nend C;\nrecord R\n"
                           "  Real a = 1;\n  Real b = 2;\nend R;\nmodel A\n  parameter Real q = 4;\n  C c(x = q);\n"
                           "  C d[2](z = {3, 4});\n  R r = R(3, 4);\nend A;\nmodel B\n"
                           "  extends A(c(z = 2), d(x = {5, 6}), r(a(start = 7)));\nend B;\nmodel ECT1\n"
                           "  parameter Real p = 5;\n  B b(c(y(start = p)), d(each z = 8));\n  extends A(final c(z = 3));\n"
                           "end ECT1;\n"),
               std::string("class ECT1\n  parameter Real p = 5.0;\n  parameter Real b.q = 4.0;\n  parameter Real b.c.x = b.q;\n"
                           "  parameter Real b.c.z = 2.0;\n  Real b.c.y(start = p) = b.c.x * time;\n"
                           "  parameter Real b.d[1].x = 5.0;\n  parameter Real b.d[1].z = 8.0;\n"
                           "  Real b.d[1].y = b.d[1].x * time;\n  parameter Real b.d[2].x = 6.0;\n"
                           "  parameter Real b.d[2].z = 8.0;\n  Real b.d[2].y = b.d[2].x * time;\n"
                           "  Real b.r.a(start = 7.0) = 3.0;\n  Real b.r.b = 4.0;\n  parameter Real q = 4.0;\n"
                           "  final parameter Real c.x = q;\n  final parameter Real c.z = 3.0;\n  final Real c.y = c.x * time;\n"
                           "  parameter Real d[1].x = 1.0;\n  parameter Real d[1].z = 3.0;\n  Real d[1].y = d[1].x * time;\n"
                           "  parameter Real d[2].x = 1.0;\n  parameter Real d[2].z = 4.0;\n  Real d[2].y = d[2].x * time;\n"
                           "  Real r.a = 3.0;\n  Real r.b = 4.0;\nend ECT1;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // extends 修饰改 final 元素：omc "Trying to override final element x"（ECS1，此前被放过）。
           {"model A\n  final parameter Real x = 1;\nend A;\nmodel ECS1\n  extends A(x = 2);\nend ECS1;\n", "final element override"},
           // 内层修饰 `C c(final x = 3)` 被外层实例修饰改：同上（ECS2）。
           {"model C\n  parameter Real x = 1;\nend C;\nmodel A\n  C c(final x = 3);\nend A;\nmodel ECS2\n"
            "  A a(c(x = 2));\nend ECS2;\n", "final element override"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(inst_redeclare_forms_and_parent_evaluate) {
  // extends 修饰里的重声明形态：原声明的 constrainedby 修饰当约束修饰、原声明自己的修饰不用（a1.p = k，不是 9）；维从原声明
  // 补（a2）；条件从原声明来（a3 被去掉，b 成结构参数）；变率前缀从原声明补、注释用原声明的（x）；多层重声明整个替掉内层的、
  // 内层的 constrainedby 修饰留下（a4.q = 6.0）。期望 = omc 1.25.0 实跑 RFT1（2026-10-06）。
  YMC_CHECK_EQ(flatten_src("model A\n  parameter Real p = 1;\nend A;\nmodel B\n  extends A;\n  parameter Real q = 2;\nend B;\n"
                           "model C\n  extends B;\n  parameter Real r = 3;\nend C;\nmodel M\n  parameter Boolean b = false;\n"
                           "  parameter Real k = 4;\n  replaceable A a1(p = 9) constrainedby A(p = k);\n"
                           "  replaceable A a2[2](each p = 5);\n  replaceable A a3 if b;\n"
                           "  replaceable parameter Real x = 1 \"orig\";\n  replaceable A a4;\nend M;\nmodel N\n"
                           "  extends M(redeclare replaceable B a4(q = 7) constrainedby B(q = 6));\nend N;\nmodel RFT1\n"
                           "  extends N(k = 8, redeclare B a1(q = 3), redeclare B a2(each q = 4), redeclare B a3,\n"
                           "    redeclare Real x = 2 \"new\", redeclare C a4(r = 5));\nend RFT1;\n"),
               std::string("class RFT1\n  final parameter Boolean b = false;\n  parameter Real k = 8.0;\n"
                           "  parameter Real a1.p = k;\n  parameter Real a1.q = 3.0;\n  parameter Real a2[1].p = 5.0;\n"
                           "  parameter Real a2[1].q = 4.0;\n  parameter Real a2[2].p = 5.0;\n  parameter Real a2[2].q = 4.0;\n"
                           "  parameter Real x = 2.0 \"orig\";\n  parameter Real a4.p = 1.0;\n  parameter Real a4.q = 6.0;\n"
                           "  parameter Real a4.r = 5.0;\nend RFT1;\n"));
  // 外层组件的 Evaluate = true 传给子树里的参数（自己写了 Evaluate = false 的除外）。期望 = omc 1.25.0 实跑 EVT1。
  YMC_CHECK_EQ(flatten_src("model A\n  parameter Real p = 1;\n  parameter Real q = 2 annotation(Evaluate = false);\n"
                           "  Real y = p * q * time;\nend A;\nmodel B\n  A a;\n  parameter Integer n = 2;\n"
                           "  Real v[n] = fill(time, n);\nend B;\nmodel EVT1\n  B b annotation(Evaluate = true);\n  A c;\n"
                           "end EVT1;\n"),
               std::string("class EVT1\n  final parameter Real b.a.p = 1.0;\n  parameter Real b.a.q = 2.0;\n"
                           "  Real b.a.y = b.a.q * time;\n  final parameter Integer b.n = 2;\n  Real b.v[1];\n  Real b.v[2];\n"
                           "  parameter Real c.p = 1.0;\n  parameter Real c.q = 2.0;\n  Real c.y = c.p * c.q * time;\nequation\n"
                           "  b.v = {time, time};\nend EVT1;\n"));
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 重声明的方向与原声明的冲突：omc "Invalid redeclaration 'output u', original element is declared 'input'"（RFS1）。
           {"model M\n  replaceable input Real u;\nend M;\nmodel RFS1\n  extends M(redeclare output Real u);\n"
            "end RFS1;\n", "redeclare prefix mismatch"},
           // 同一 extends 修饰里重声明 + 普通修饰同一元素：omc "Duplicate modification of element a on extends M"（RFS2）。
           {"model A\n  parameter Real p = 1;\nend A;\nmodel B\n  extends A;\n  parameter Real q = 2;\nend B;\n"
            "model M\n  replaceable A a;\nend M;\nmodel RFS2\n  extends M(redeclare B a, a(p = 9));\nend RFS2;\n", "duplicate modification"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
}

YMC_TEST(flatten_function_variable_attributes) {
  // 函数变量的类型是短类链 / 带属性修饰：属性按内建类型的属性表顺序打（声明修饰的在前、类型链的在后，final 不打）；数组变量
  // 的类型修饰按维铺开（整数维 → 数组字面量，`n` 维 → `fill("V", n)`，`:` 维 → `fill("V", size(u, 1))`），带 each 的声明
  // 修饰照原样标量；属性值可引用别的形参（`start = a`）。期望 = omc 1.25.0 实跑 FVT1（2026-10-06）。
  YMC_CHECK_EQ(flatten_src("package SI\n  type Voltage = Real(final quantity = \"ElectricPotential\", final unit = \"V\");\n"
                           "end SI;\ntype W = SI.Voltage(displayUnit = \"kV\", min = 0);\nfunction f\n  input Integer n;\n"
                           "  input W v[n](each start = 1);\n  input SI.Voltage u[:];\n  input Real a(nominal = 2) = 3;\n"
                           "  output SI.Voltage y[2];\nprotected\n  SI.Voltage t = 2 * a;\nalgorithm\n"
                           "  y := {sum(v), sum(u) + t};\nend f;\nfunction g\n  input Real a;\n  output Real z(start = a);\n"
                           "algorithm\n  z := 2 * a;\nend g;\nmodel FVT1\n  Real y[2] = f(2, {time, 1}, {time});\n"
                           "  Real z = g(time);\nend FVT1;\n"),
               std::string("function f\n  input Integer n;\n"
                           "  input Real[n] v(quantity = fill(\"ElectricPotential\", n), unit = fill(\"V\", n), displayUnit = fill(\"kV\", n), min = fill(0.0, n), start = 1.0);\n"
                           "  input Real[:] u(quantity = fill(\"ElectricPotential\", size(u, 1)), unit = fill(\"V\", size(u, 1)));\n"
                           "  input Real a(nominal = 2.0) = 3.0;\n"
                           "  output Real[2] y(quantity = {\"ElectricPotential\", \"ElectricPotential\"}, unit = {\"V\", \"V\"});\n"
                           "  protected Real t(quantity = \"ElectricPotential\", unit = \"V\") = 2.0 * a;\nalgorithm\n"
                           "  y := {sum(v), sum(u) + t};\nend f;\n\nfunction g\n  input Real a;\n  output Real z(start = a);\n"
                           "algorithm\n  z := 2.0 * a;\nend g;\n\nclass FVT1\n  Real y[1];\n  Real y[2];\n  Real z = g(time);\n"
                           "equation\n  y = f(2, {time, 1.0}, {time}, 3.0);\nend FVT1;\n"));
  // 属性名不对：omc "Modified element foo not found in class Real"（FVS1）。
  bool uns = false;
  YMC_CHECK_EQ(flatten_src("function f\n  input Real v(foo = 1);\n  output Real y;\nalgorithm\n  y := 2 * v;\nend f;\n"
                           "model FVS1\n  Real x = f(time);\nend FVS1;\n", &uns),
               "<unsupported:function: f>");
  YMC_CHECK(uns);
}

YMC_TEST(flatten_function_reductions_and_comprehensions) {
  // 函数体里的归约 / 推导式：范围是运行时的保留 `sum(e for i in 1:size(x, 1))`、`max(…)`；sum 的范围全是字面量 → 展开
  //（`x[1] + x[2]`）；多迭代变量打成嵌套；推导式 `{e for i in r}` → `array(e for i in r)`，Integer 体转 Real 时整体转型带
  // 表达式维（`/*Real[m, 2]*/`，getRangeTypeInt）。期望 = omc 1.25.0 实跑 FRT1（2026-10-06）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Real x[:];\n  input Integer m;\n  output Real y;\n  output Real o[m, 2];\n"
                           "protected\n  Real w[size(x, 1)] = {2 * x[i] for i in 1:size(x, 1)};\nalgorithm\n"
                           "  y := sum(w[i] * i for i in 1:size(x, 1)) + max(x[i] for i in 1:2) + sum(x[i] for i in 1:2) + sum(i * j for i in 1:m, j in 1:2);\n"
                           "  o := {{i * j for j in 1:2} for i in 1:m};\nend f;\nmodel FRT1\n  Real y = f({time, 2}, 2);\n"
                           "end FRT1;\n"),
               std::string("function f\n  input Real[:] x;\n  input Integer m;\n  output Real y;\n  output Real[m, 2] o;\n"
                           "  protected Real[size(x, 1)] w = array(2.0 * x[i] for i in 1:size(x, 1));\nalgorithm\n"
                           "  y := sum(w[i] * /*Real*/(i) for i in 1:size(x, 1)) + max(x[i] for i in 1:2) + x[1] + x[2] + /*Real*/(sum(sum(i * j for i in 1:m) for j in 1:2));\n"
                           "  o := /*Real[m, 2]*/(array(array(i * j for j in 1:2) for i in 1:m));\nend f;\n\nclass FRT1\n"
                           "  Real y = f({time, 2.0}, 2)[1];\nend FRT1;\n"));
  // 多迭代变量的推导式（体是数组）：omc 报类型不符（FRS1）。
  bool uns = false;
  YMC_CHECK_EQ(flatten_src("function f\n  input Integer m;\n  output Real o[m, 2];\nalgorithm\n"
                           "  o := {{i, j} for i in 1:m, j in 1:2};\nend f;\nmodel FRS1\n  Real x[2, 2] = f(2) * time;\n"
                           "end FRS1;\n", &uns), "<unsupported:function reduction form>");
  YMC_CHECK(uns);
}

YMC_TEST(flatten_user_enum_structural_parameters) {
  // 用户枚举参数的结构求值：Evaluate = true 的枚举参数按值打 final、引用代入后 if 方程选支（a 的 InitialState → 初始方程
  // `a.y = a.y_start`，b 的 NoInit → 无）；import 来的枚举类型名规范成全名（`Init.NoInit` → `Types.Init.NoInit`）；非结构的枚举
  // 参数照常保留符号（`Integer(k)`、if 表达式）。期望 = omc 1.25.0 实跑 UET1（2026-10-06）。
  YMC_CHECK_EQ(flatten_src("package Types\n  type Init = enumeration(NoInit, SteadyState, InitialState, InitialOutput);\n"
                           "end Types;\nblock FO\n  import Types.Init;\n"
                           "  parameter Init initType = Init.NoInit annotation(Evaluate = true);\n"
                           "  parameter Real y_start = 0;\n  Real y(start = y_start);\ninitial equation\n"
                           "  if initType == Init.SteadyState then\n    der(y) = 0;\n"
                           "  elseif initType == Init.InitialState or initType == Init.InitialOutput then\n    y = y_start;\n"
                           "  end if;\nequation\n  der(y) = 1 - y;\nend FO;\nmodel UET1\n"
                           "  FO a(initType = Types.Init.InitialState, y_start = 2);\n  FO b;\n"
                           "  parameter Types.Init k = Types.Init.SteadyState;\n  Integer n = Integer(k);\n"
                           "  Real z = if k == Types.Init.NoInit then time else 2 * time;\nend UET1;\n"),
               std::string("class UET1\n"
                           "  final parameter enumeration(NoInit, SteadyState, InitialState, InitialOutput) a.initType = Types.Init.InitialState;\n"
                           "  parameter Real a.y_start = 2.0;\n  Real a.y(start = a.y_start);\n"
                           "  final parameter enumeration(NoInit, SteadyState, InitialState, InitialOutput) b.initType = Types.Init.NoInit;\n"
                           "  parameter Real b.y_start = 0.0;\n  Real b.y(start = b.y_start);\n"
                           "  parameter enumeration(NoInit, SteadyState, InitialState, InitialOutput) k = Types.Init.SteadyState;\n"
                           "  Integer n = Integer(k);\n  Real z = if k == Types.Init.NoInit then time else 2.0 * time;\n"
                           "initial equation\n  a.y = a.y_start;\nequation\n  der(a.y) = 1.0 - a.y;\n  der(b.y) = 1.0 - b.y;\n"
                           "end UET1;\n"));
  // Evaluate = true 而无绑定：omc 报错（UES1）。
  bool uns = false;
  YMC_CHECK_EQ(flatten_src("type E = enumeration(a, b, c);\nmodel UES1\n  parameter E e annotation(Evaluate = true);\n"
                           "  Real x = time;\nend UES1;\n", &uns), "<unsupported:Evaluate annotation>");
  YMC_CHECK(uns);
}

YMC_TEST(inst_extends_builtin_types_and_type_dimensions) {
  // 长类型只 extends 一个类型（`type TypeReal extends Real; end TypeReal;`、`type Length extends Real(unit = "m", …)`）当短类走；
  // 短类带字面量维（`type Axis = TypeReal[3](each final unit = "1")`、`type V3 = Real[3](start = {1, 2, 3})`）拼在组件维后面，
  // 那层类型自己的修饰按类型形状切分；函数变量同样（`input Real[3] a(unit = "1")`、`output Real[3] y(start = {1.0, 2.0, 3.0})`）。
  // 期望 = omc 1.25.0 实跑 XBT1（2026-10-06）。
  YMC_CHECK_EQ(flatten_src("package Icons\n  type TypeReal \"Icon for Real types\"\n    extends Real;\n  end TypeReal;\n"
                           "end Icons;\ntype Length\n  extends Real(unit = \"m\", final quantity = \"Length\");\nend Length;\n"
                           "type Axis = Icons.TypeReal[3](each final unit = \"1\");\ntype V3 = Real[3](start = {1, 2, 3});\n"
                           "function f\n  input Axis a;\n  input Length l;\n  output V3 y;\nalgorithm\n  y := a * l;\nend f;\n"
                           "model XBT1\n  parameter Axis n = {1, 0, 0};\n  Length x(start = 1) = time;\n  V3 v;\n"
                           "  Real w[3] = f(n, 2);\nequation\n  der(v) = -v;\nend XBT1;\n"),
               std::string("function f\n  input Real[3] a(unit = \"1\");\n  input Real l(quantity = \"Length\", unit = \"m\");\n"
                           "  output Real[3] y(start = {1.0, 2.0, 3.0});\nalgorithm\n  y := {a[1] * l, a[2] * l, a[3] * l};\n"
                           "end f;\n\nclass XBT1\n  parameter Real n[1](unit = \"1\") = 1.0;\n"
                           "  parameter Real n[2](unit = \"1\") = 0.0;\n  parameter Real n[3](unit = \"1\") = 0.0;\n"
                           "  Real x(quantity = \"Length\", unit = \"m\", start = 1.0) = time;\n  Real v[1](start = 1.0);\n"
                           "  Real v[2](start = 2.0);\n  Real v[3](start = 3.0);\n  Real w[1];\n  Real w[2];\n  Real w[3];\n"
                           "equation\n  w = f(n, 2.0);\n  der(v[1]) = -v[1];\n  der(v[2]) = -v[2];\n  der(v[3]) = -v[3];\n"
                           "end XBT1;\n"));
  // 短类的维不是字面量（`type V = Real[n]`，n 在包里）：维式搬到组件上会换作用域，未做（XBS1，omc 能过）。
  bool uns = false;
  YMC_CHECK_EQ(flatten_src("package P\n  constant Integer n = 2;\n  type V = Real[n];\nend P;\nmodel XBS1\n"
                           "  P.V x = {time, 1};\nend XBS1;\n", &uns), "<unsupported:derived-type array dimension>");
  YMC_CHECK(uns);
}

YMC_TEST(flatten_structural_parameter_binding_chains) {
  // 结构求值里的多段参数引用顺着绑定链求值：记录字段经修饰 / 整体绑定传下来的值（`m.friction.fp.a` ← `m.fp.a` ← `data.a`），
  // 只给被引用的本身打 final、依赖照旧；维里的同理（`y[fp.n]`）；if 条件里参数的 fixed 检查也顺绑定走（`q = a.p`）。
  // 期望 = omc 1.25.0 实跑（2026-10-06，下同）。
  YMC_CHECK_EQ(flatten_src("record R\n  parameter Real a = 1;\n  parameter Real b = 2 * a;\n  parameter Integer n = 1;\nend R;\n"
                           "model F\n  parameter R fp;\n  Real tau;\n  Real y[fp.n];\nequation\n  if fp.a <= 0 then\n"
                           "    tau = 0;\n  else\n    tau = fp.b * time;\n  end if;\n  y = fill(tau, fp.n);\nend F;\nmodel M\n"
                           "  parameter R fp;\n  F friction(final fp = fp);\nend M;\nmodel A\n  parameter Real p = 1;\nend A;\n"
                           "model PCT1\n  parameter R data(a = 0, n = 2);\n  M m(fp = data);\n  A a(p = 4);\n"
                           "  parameter Real q = a.p;\n  Real z;\nequation\n  if q > 2 then\n    z = 0;\n  else\n    z = time;\n"
                           "  end if;\nend PCT1;\n"),
               std::string("class PCT1\n  parameter Real data.a = 0.0;\n  parameter Real data.b = 2.0 * data.a;\n"
                           "  parameter Integer data.n = 2;\n  parameter Real m.fp.a = data.a;\n"
                           "  parameter Real m.fp.b = data.b;\n  parameter Integer m.fp.n = data.n;\n"
                           "  final parameter Real m.friction.fp.a = 0.0;\n  final parameter Real m.friction.fp.b = m.fp.b;\n"
                           "  final parameter Integer m.friction.fp.n = 2;\n  Real m.friction.tau;\n  Real m.friction.y[1];\n"
                           "  Real m.friction.y[2];\n  parameter Real a.p = 4.0;\n  final parameter Real q = 4.0;\n  Real z;\n"
                           "equation\n  m.friction.tau = 0.0;\n  m.friction.y[1] = m.friction.tau;\n"
                           "  m.friction.y[2] = m.friction.tau;\n  z = 0.0;\nend PCT1;\n"));
  // 被引用的记录参数声明在后（还没发射）：两遍压平，第二遍用第一遍的值。
  YMC_CHECK_EQ(flatten_src("record R\n  parameter Real a = 1;\nend R;\nmodel F\n  parameter R fp;\n  Real tau;\nequation\n"
                           "  if fp.a <= 0 then\n    tau = 0;\n  else\n    tau = time;\n  end if;\nend F;\nmodel PCS1\n"
                           "  F f(fp = data);\n  parameter R data(a = 0);\nend PCS1;\n"),
               std::string("class PCS1\n  final parameter Real f.fp.a = 0.0;\n  Real f.tau;\n  parameter Real data.a = 0.0;\n"
                           "equation\n  f.tau = 0.0;\nend PCS1;\n"));
}

YMC_TEST(flatten_parameter_evaluated_before_declaration) {
  // 方程段的结构求值求到了声明在后的参数：omc 把求得的值存回组件，压平在后的它打字面量（`k = 2`，不是 `j + 1`）。
  YMC_CHECK_EQ(flatten_src("model F\n  parameter Integer n = 1;\n  Real x;\nequation\n  if n > 1 then\n    x = 1;\n  else\n"
                           "    x = 2;\n  end if;\nend F;\nmodel PST1\n  F f(n = k);\n  parameter Integer k = j + 1;\n"
                           "  parameter Integer j = 1;\nend PST1;\n"),
               std::string("class PST1\n  final parameter Integer f.n = 2;\n  Real f.x;\n  parameter Integer k = 2;\n"
                           "  parameter Integer j = 1;\nequation\n  f.x = 1.0;\nend PST1;\n"));
}

YMC_TEST(flatten_if_condition_not_fixed_through_record_binding) {
  // isExpressionNotFixed 顺绑定链：字段修饰绑到 fixed = false 的参数 → 不固定、if 原样留（f）；记录引用整体绑定查被引用的
  // 记录本身——它带 Evaluate = false → 不固定（g），它字段自己 fixed = false 不算（无绑定记录支，h 照折）。
  YMC_CHECK_EQ(flatten_src("record R\n  parameter Real a = 1;\nend R;\nmodel F\n  parameter R fp;\n  Real tau;\nequation\n"
                           "  if fp.a <= 0 then\n    tau = 0;\n  else\n    tau = time;\n  end if;\nend F;\nmodel PNF1\n"
                           "  parameter Real k(fixed = false) = 0;\n  F f(fp(a = k));\n"
                           "  parameter R d(a = 0) annotation(Evaluate = false);\n  F g(fp = d);\n"
                           "  parameter R e(a(fixed = false) = 0);\n  F h(fp = e);\nend PNF1;\n"),
               std::string("class PNF1\n  parameter Real k(fixed = false) = 0.0;\n  parameter Real f.fp.a = k;\n  Real f.tau;\n"
                           "  parameter Real d.a = 0.0;\n  parameter Real g.fp.a = d.a;\n  Real g.tau;\n"
                           "  parameter Real e.a(fixed = false) = 0.0;\n  final parameter Real h.fp.a = 0.0;\n  Real h.tau;\n"
                           "equation\n  if f.fp.a <= 0.0 then\n    f.tau = 0.0;\n  else\n    f.tau = time;\n  end if;\n"
                           "  if g.fp.a <= 0.0 then\n    g.tau = 0.0;\n  else\n    g.tau = time;\n  end if;\n  h.tau = 0.0;\n"
                           "end PNF1;\n"));
}

YMC_TEST(flatten_enum_literal_through_enclosing_package) {
  // 枚举字面量的类型名在外层包里找到（MSL Blocks.Nonlinear.Limiter 的 `Types.LimiterHomotopy.Linear`）：类型路径 = 包的全名
  //（`P.Types.LH.Linear`）；Evaluate = true 的枚举参数作条件时 if 式 / if 方程按值折。期望 = omc 1.25.0 实跑（2026-10-06，下同）。
  YMC_CHECK_EQ(flatten_src("package P\n  package Types\n    type LH = enumeration(NoHomotopy, Linear, UpperLimit);\n"
                           "  end Types;\n  package Nonlinear\n    block Limiter\n      parameter Real uMax = 1;\n"
                           "      parameter Types.LH homotopyType = P.Types.LH.Linear annotation(Evaluate = true);\n"
                           "      Real u = time;\n      Real y;\n      Real s;\n    equation\n"
                           "      s = (if homotopyType == Types.LH.Linear then u else if homotopyType == Types.LH.UpperLimit then uMax else 0);\n"
                           "      if homotopyType == Types.LH.NoHomotopy then\n"
                           "        y = smooth(0, if u > uMax then uMax else u);\n      else\n"
                           "        y = homotopy(actual = smooth(0, if u > uMax then uMax else u), simplified = s);\n"
                           "      end if;\n    end Limiter;\n  end Nonlinear;\nend P;\nmodel LH1\n  P.Nonlinear.Limiter lim;\n"
                           "end LH1;\n"),
               std::string("class LH1\n  parameter Real lim.uMax = 1.0;\n"
                           "  final parameter enumeration(NoHomotopy, Linear, UpperLimit) lim.homotopyType = P.Types.LH.Linear;\n"
                           "  Real lim.u = time;\n  Real lim.y;\n  Real lim.s;\nequation\n  lim.s = lim.u;\n"
                           "  lim.y = homotopy(smooth(0, if lim.u > lim.uMax then lim.uMax else lim.u), lim.s);\nend LH1;\n"));
}

YMC_TEST(flatten_pre_of_when_assigned_variable) {
  // when 外 pre / change 的实参声明为连续、但在 when 里被赋值（隐式离散）：omc 放行，与方程先后、所在实例无关；数组任一元素
  // 赋值即整个组件。
  YMC_CHECK_EQ(flatten_src("model Z\n  Real ySample(start = 0, fixed = true);\n  Real y;\nequation\n  y = pre(ySample);\n"
                           "end Z;\nmodel PD1\n  Z z;\n  Real x[2](each start = 0, each fixed = true);\n  Real w;\n"
                           "  Boolean c;\nequation\n  when sample(0, 0.1) then\n    z.ySample = time;\n    x[1] = time;\n"
                           "  end when;\n  x[2] = 1;\n  w = 2 * pre(x[2]) + 1;\n  c = change(z.ySample);\nend PD1;\n"),
               std::string("class PD1\n  Real z.ySample(start = 0.0, fixed = true);\n  Real z.y;\n"
                           "  Real x[1](start = 0.0, fixed = true);\n  Real x[2](start = 0.0, fixed = true);\n  Real w;\n"
                           "  Boolean c;\nequation\n  z.y = pre(z.ySample);\n  when sample(0.0, 0.1) then\n"
                           "    z.ySample = time;\n    x[1] = time;\n  end when;\n  x[2] = 1.0;\n  w = 2.0 * pre(x[2]) + 1.0;\n"
                           "  c = change(z.ySample);\nend PD1;\n"));
}

YMC_TEST(flatten_composite_reference_through_parameter_sized_array) {
  // 复合引用穿过子实例里参数维的数组（MSL 电机 `vs[m] = plug_sp.pin.v - plug_sn.pin.v`，pin[m] 的 m 是 plug 的参数）：维取那个
  // 子实例的实际维；plug 声明在后（向前引用）→ 两遍压平，第二遍用第一遍记下的维。期望 = omc 1.25.0 实跑（2026-10-06，下同）。
  YMC_CHECK_EQ(flatten_src("connector Pin\n  Real v;\n  flow Real i;\nend Pin;\nconnector Plug\n  parameter Integer m = 3;\n"
                           "  Pin pin[m];\nend Plug;\nmodel M\n  final parameter Integer m = 3 annotation(Evaluate = true);\n"
                           "  output Real vs[m] = p.pin.v - n.pin.v;\n  Real v2 = p.pin[2].v;\n  Plug p(final m = m);\n"
                           "  Plug n(final m = m);\nend M;\nmodel CSF1\n  M mm;\nend CSF1;\n"),
               std::string("class CSF1\n  final parameter Integer mm.m = 3;\n  Real mm.vs[1];\n  Real mm.vs[2];\n"
                           "  Real mm.vs[3];\n  Real mm.v2 = mm.p.pin[2].v;\n  final parameter Integer mm.p.m = 3;\n"
                           "  Real mm.p.pin[1].v;\n  Real mm.p.pin[1].i;\n  Real mm.p.pin[2].v;\n  Real mm.p.pin[2].i;\n"
                           "  Real mm.p.pin[3].v;\n  Real mm.p.pin[3].i;\n  final parameter Integer mm.n.m = 3;\n"
                           "  Real mm.n.pin[1].v;\n  Real mm.n.pin[1].i;\n  Real mm.n.pin[2].v;\n  Real mm.n.pin[2].i;\n"
                           "  Real mm.n.pin[3].v;\n  Real mm.n.pin[3].i;\nequation\n  mm.p.pin[1].i = 0.0;\n"
                           "  mm.p.pin[2].i = 0.0;\n  mm.p.pin[3].i = 0.0;\n  mm.n.pin[1].i = 0.0;\n  mm.n.pin[2].i = 0.0;\n"
                           "  mm.n.pin[3].i = 0.0;\n"
                           "  mm.vs = {mm.p.pin[1].v - mm.n.pin[1].v, mm.p.pin[2].v - mm.n.pin[2].v, mm.p.pin[3].v - mm.n.pin[3].v};\n"
                           "end CSF1;\n"));
}

YMC_TEST(flatten_evaluate_through_forward_record_binding) {
  // Evaluate = true 的参数绑定顺记录整体绑定链到声明在后的记录参数（MSL 电机 `statorCore.m = coreParameters.m`）：第一遍先留着，
  // 第二遍用第一遍的值；记录字段本身成结构参数，链上的依赖不连带。
  YMC_CHECK_EQ(flatten_src("record CP\n  parameter Integer m;\n  parameter Real PRef = 0;\nend CP;\nmodel Core\n"
                           "  parameter CP coreParameters(m = 3);\n"
                           "  final parameter Integer m = coreParameters.m annotation(Evaluate = true);\nend Core;\nmodel EVF1\n"
                           "  Core statorCore(coreParameters = scp);\n  parameter CP scp(final m = 3, PRef = 1);\nend EVF1;\n"),
               std::string("class EVF1\n  final parameter Integer statorCore.coreParameters.m = 3;\n"
                           "  parameter Real statorCore.coreParameters.PRef = scp.PRef;\n"
                           "  final parameter Integer statorCore.m = 3;\n  final parameter Integer scp.m = 3;\n"
                           "  parameter Real scp.PRef = 1.0;\nend EVF1;\n"));
}

YMC_TEST(flatten_transcendental_folding_matches_ucrt) {
  // 编译期求值的超越函数与 omc 1.25.0 逐位一致：omc 用 UCRT 的 libm，gcc（TDM / libmingwex）构建经 base/crt_math 取 ucrtbase.dll
  // 的同名函数（cos(4.188786666666666) libmingwex 差一个 ulp）。期望 = omc 1.25.0 实跑（2026-10-06，下同）。
  YMC_CHECK_EQ(flatten_src("model CRT1\n  parameter Real a = cos(4.188786666666666);\n"
                           "  parameter Real b = sin(0.3) + tan(1.1) + asin(0.3) + acos(0.4) + atan(2.5);\n"
                           "  parameter Real c = sinh(1.3) + cosh(0.7) + tanh(0.9) + exp(2.7) + log(3.3) + log10(47.0);\n"
                           "  parameter Real d = atan2(1.3, -2.2) + 2.7 ^ 1.37;\nend CRT1;\n"),
               std::string("class CRT1\n  parameter Real a = -0.5000030640984336;\n  parameter Real b = 4.914541948335329;\n"
                           "  parameter Real c = 21.41560136440357;\n  parameter Real d = 6.50702022368719;\nend CRT1;\n"));
}

YMC_TEST(flatten_comprehension_with_array_elements) {
  // 元素是数组构造子的推导式（MSL SpacePhasor `{{cos(…), -sin(…)} for k in 1:m}`）：形状 = 迭代维 + 元素维，参数数组逐元素
  // 取对应分量；`2/m * {{… for k in 1:m}, {…}}` 同样逐元素；矩阵乘向量的方程逐行展开。
  YMC_CHECK_EQ(flatten_src("model SP\n  constant Integer m = 3;\n  Real v[m];\n  Real vs[2];\nprotected\n"
                           "  parameter Real TM[2, m] = 2/m*{{cos(+(k - 1)/m*2*3.14159) for k in 1:m}, {+sin(+(k - 1)/m*2*3.14159) for k in 1:m}};\n"
                           "  parameter Real IT[m, 2] = {{cos(-(k - 1)/m*2*3.14159), -sin(-(k - 1)/m*2*3.14159)} for k in 1:m};\n"
                           "equation\n  vs = TM*v;\n  v = {time, 2*time, 3*time};\nend SP;\nmodel CMP1\n  SP s;\nend CMP1;\n"),
               std::string("class CMP1\n  constant Integer s.m = 3;\n  Real s.v[1];\n  Real s.v[2];\n  Real s.v[3];\n"
                           "  Real s.vs[1];\n  Real s.vs[2];\n  protected parameter Real s.TM[1,1] = 0.6666666666666666;\n"
                           "  protected parameter Real s.TM[1,2] = -0.33333231196562396;\n"
                           "  protected parameter Real s.TM[1,3] = -0.3333353760656224;\n"
                           "  protected parameter Real s.TM[2,1] = 0.0;\n"
                           "  protected parameter Real s.TM[2,2] = 0.5773508588753431;\n"
                           "  protected parameter Real s.TM[2,3] = -0.5773490898127704;\n"
                           "  protected parameter Real s.IT[1,1] = 1.0;\n  protected parameter Real s.IT[1,2] = 0.0;\n"
                           "  protected parameter Real s.IT[2,1] = -0.49999846794843594;\n"
                           "  protected parameter Real s.IT[2,2] = 0.8660262883130146;\n"
                           "  protected parameter Real s.IT[3,1] = -0.5000030640984336;\n"
                           "  protected parameter Real s.IT[3,2] = -0.8660236347191557;\nequation\n"
                           "  s.vs[1] = s.TM[1,1] * s.v[1] + s.TM[1,2] * s.v[2] + s.TM[1,3] * s.v[3];\n"
                           "  s.vs[2] = s.TM[2,1] * s.v[1] + s.TM[2,2] * s.v[2] + s.TM[2,3] * s.v[3];\n  s.v[1] = time;\n"
                           "  s.v[2] = 2.0 * time;\n  s.v[3] = 3.0 * time;\nend CMP1;\n"));
}

YMC_TEST(flatten_parameter_bound_to_user_function_call) {
  // 参数绑到用户函数调用、实参是结构参数（MSL ToSpacePhasor `phi[m] = symmetricOrientation(m)`）：压平末尾代入结构参数后实参全成
  // 字面量，按定义求值成字面量（数组逐元素 `so(m)[k]`、标量 `g(m)`），函数不再打印；`2/m*{cos(phi), sin(phi)}` 只折掉 2/m。
  // 期望 = omc 1.25.0 实跑（2026-10-06）。
  YMC_CHECK_EQ(flatten_src("function so\n  input Integer m;\n  output Real o[m];\nalgorithm\n"
                           "  o := {(k - 1)*2*3.14159/m for k in 1:m};\nend so;\nfunction g\n  input Integer m;\n"
                           "  output Real y;\nalgorithm\n  y := 2*m + 0.5;\nend g;\nblock TSP\n"
                           "  parameter Integer m = 3 annotation(Evaluate = true);\n  parameter Real p = g(m);\n"
                           "  Real u[m] = {time, 2*time, 3*time};\n  Real y[2];\nprotected\n  parameter Real phi[m] = so(m);\n"
                           "  parameter Real TM[2, m] = 2/m*{+cos(+phi), +sin(+phi)};\n"
                           "  parameter Real IT[m, 2] = {{+cos(-phi[k]), -sin(-phi[k])} for k in 1:m};\nequation\n  y = TM*u;\n"
                           "end TSP;\nmodel FB1\n  TSP t;\nend FB1;\n"),
               std::string("class FB1\n  final parameter Integer t.m = 3;\n  parameter Real t.p = 6.5;\n  Real t.u[1];\n"
                           "  Real t.u[2];\n  Real t.u[3];\n  Real t.y[1];\n  Real t.y[2];\n"
                           "  protected parameter Real t.phi[1] = 0.0;\n"
                           "  protected parameter Real t.phi[2] = 2.094393333333333;\n"
                           "  protected parameter Real t.phi[3] = 4.188786666666666;\n"
                           "  protected parameter Real t.TM[1,1] = 0.6666666666666666 * cos(t.phi[1]);\n"
                           "  protected parameter Real t.TM[1,2] = 0.6666666666666666 * cos(t.phi[2]);\n"
                           "  protected parameter Real t.TM[1,3] = 0.6666666666666666 * cos(t.phi[3]);\n"
                           "  protected parameter Real t.TM[2,1] = 0.6666666666666666 * sin(t.phi[1]);\n"
                           "  protected parameter Real t.TM[2,2] = 0.6666666666666666 * sin(t.phi[2]);\n"
                           "  protected parameter Real t.TM[2,3] = 0.6666666666666666 * sin(t.phi[3]);\n"
                           "  protected parameter Real t.IT[1,1] = cos(-t.phi[1]);\n"
                           "  protected parameter Real t.IT[1,2] = -sin(-t.phi[1]);\n"
                           "  protected parameter Real t.IT[2,1] = cos(-t.phi[2]);\n"
                           "  protected parameter Real t.IT[2,2] = -sin(-t.phi[2]);\n"
                           "  protected parameter Real t.IT[3,1] = cos(-t.phi[3]);\n"
                           "  protected parameter Real t.IT[3,2] = -sin(-t.phi[3]);\nequation\n"
                           "  t.u = {time, 2.0 * time, 3.0 * time};\n"
                           "  t.y[1] = t.TM[1,1] * t.u[1] + t.TM[1,2] * t.u[2] + t.TM[1,3] * t.u[3];\n"
                           "  t.y[2] = t.TM[2,1] * t.u[1] + t.TM[2,2] * t.u[2] + t.TM[2,3] * t.u[3];\nend FB1;\n"));
}

YMC_TEST(flatten_array_call_inside_parameter_array_binding) {
  // 参数数组绑定里嵌在运算中的数组值用户函数调用（MSL Polyphase.Sources `phase[m] = -symmetricOrientation(m)`）：逐元素取
  // `f(…)[k]`；实参是结构参数的整体求值（调用的输出维里的实参成结构参数，NFCall.evaluateCallTypeDim），取负在数组层（零元素
  // -0.0）；不是结构参数的留 `2.0 * sv(x)[1]` / `-sv(x)[1]`。期望 = omc 1.25.0 实跑（2026-10-06，下同）。
  YMC_CHECK_EQ(flatten_src("function so\n  input Integer m;\n  output Real o[m];\nalgorithm\n"
                           "  o := {(k - 1)*2*3.14159/m for k in 1:m};\nend so;\nfunction sv\n  input Real x;\n"
                           "  output Real o[3];\nalgorithm\n  o := {x, 2*x, 3*x};\nend sv;\nmodel AC1\n"
                           "  parameter Integer m = 3;\n  parameter Real x = 2;\n  parameter Real phase[m] = -so(m);\n"
                           "  parameter Real scaled[m] = x * so(m);\n  parameter Real kept[3] = 2 * sv(x);\n"
                           "  parameter Real neg[3] = -sv(x);\nend AC1;\n"),
               std::string("function sv\n  input Real x;\n  output Real[3] o;\nalgorithm\n  o := {x, 2.0 * x, 3.0 * x};\n"
                           "end sv;\n\nclass AC1\n  final parameter Integer m = 3;\n  parameter Real x = 2.0;\n"
                           "  parameter Real phase[1] = -0.0;\n  parameter Real phase[2] = -2.094393333333333;\n"
                           "  parameter Real phase[3] = -4.188786666666666;\n  parameter Real scaled[1] = 0.0;\n"
                           "  parameter Real scaled[2] = x * 2.094393333333333;\n"
                           "  parameter Real scaled[3] = x * 4.188786666666666;\n  parameter Real kept[1] = 2.0 * sv(x)[1];\n"
                           "  parameter Real kept[2] = 2.0 * sv(x)[2];\n  parameter Real kept[3] = 2.0 * sv(x)[3];\n"
                           "  parameter Real neg[1] = -sv(x)[1];\n  parameter Real neg[2] = -sv(x)[2];\n"
                           "  parameter Real neg[3] = -sv(x)[3];\nend AC1;\n"));
}

YMC_TEST(flatten_negative_zero_literal_negation) {
  // 取负的零：书写的 `-0.0` / `-(0.0)` 是负零字面量；字面量数组整体取负不护零（`-{0.0, 1.0, 2.0}` → -0.0）、全零数组原样；
  // 方程里乘到变量上的零照折成 0.0。
  YMC_CHECK_EQ(flatten_src("model NZ1\n  parameter Real p = -0.0;\n  parameter Real q = -(0.0);\n"
                           "  parameter Real r[3] = -{0.0, 1.0, 2.0};\n  parameter Real s[2] = -{0.0, 0.0};\n  Real y[3];\n"
                           "equation\n  y = -{0.0, 1.0, 2.0} * time;\nend NZ1;\n"),
               std::string("class NZ1\n  parameter Real p = -0.0;\n  parameter Real q = -0.0;\n  parameter Real r[1] = -0.0;\n"
                           "  parameter Real r[2] = -1.0;\n  parameter Real r[3] = -2.0;\n  parameter Real s[1] = 0.0;\n"
                           "  parameter Real s[2] = 0.0;\n  Real y[1];\n  Real y[2];\n  Real y[3];\nequation\n  y[1] = 0.0;\n"
                           "  y[2] = -time;\n  y[3] = -2.0 * time;\nend NZ1;\n"));
}

YMC_TEST(flatten_sub_connector_array_connect) {
  // 连接器里的子连接器数组（MSL Polyphase Plug 的 `Pin pin[m]`）：connect 整个 Plug → 逐元素的势相等 + 流和为零，下标落在 pin
  // 段；维取本实例的修饰值（m = 2，不是类里的缺省 3）。期望 = omc 1.25.0 实跑（2026-10-06，下同）。
  YMC_CHECK_EQ(flatten_src("connector Pin\n  Real v;\n  flow Real i;\nend Pin;\nconnector Plug\n"
                           "  parameter Integer m = 3;\n  Pin pin[m];\nend Plug;\nmodel SP1\n  parameter Integer m = 2;\n"
                           "  Plug p(m = m);\n  Plug n(m = m);\nequation\n  connect(p, n);\nend SP1;\n"),
               std::string("class SP1\n  final parameter Integer m = 2;\n  final parameter Integer p.m = 2;\n"
                           "  Real p.pin[1].v;\n  Real p.pin[1].i;\n  Real p.pin[2].v;\n  Real p.pin[2].i;\n"
                           "  final parameter Integer n.m = 2;\n  Real n.pin[1].v;\n  Real n.pin[1].i;\n  Real n.pin[2].v;\n"
                           "  Real n.pin[2].i;\nequation\n  p.pin[1].v = n.pin[1].v;\n  p.pin[2].v = n.pin[2].v;\n"
                           "  -(p.pin[1].i + n.pin[1].i) = 0.0;\n  -(p.pin[2].i + n.pin[2].i) = 0.0;\n  p.pin[1].i = 0.0;\n"
                           "  p.pin[2].i = 0.0;\n  n.pin[1].i = 0.0;\n  n.pin[2].i = 0.0;\nend SP1;\n"));
}

YMC_TEST(flatten_function_constant_dimension) {
  // 函数形参的维只引用函数自己的常量：打印成字面量维 `Real[3] v`，体里的引用照留（MSL SpacePhasors 的 `v[m]`）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Real v[m];\n  output Real s;\nprotected\n  constant Integer m = 3;\n"
                           "algorithm\n  s := sum(v);\nend f;\nmodel FD1\n  Real x[3] = {time, 2, 3};\n  Real y = f(x);\n"
                           "end FD1;\n"),
               std::string("function f\n  input Real[3] v;\n  output Real s;\n  protected constant Integer m = 3;\n"
                           "algorithm\n  s := v[1] + v[2] + v[3];\nend f;\n\nclass FD1\n  Real x[1];\n  Real x[2];\n"
                           "  Real x[3];\n  Real y = f(x);\nequation\n  x = {time, 2.0, 3.0};\nend FD1;\n"));
}

YMC_TEST(flatten_evaluate_does_not_store_forward_binding) {
  // Evaluate = true 的 core.m 顺记录整体绑定一路求到声明在后的 d.scp.m：值照用（core.m = 3），但 omc 在压平之后才求这个
  // 绑定，d.scp.m 的绑定不存回、照打 `d.m`（同样是向前引用，维的求值在定型期、会存回）。
  YMC_CHECK_EQ(flatten_src("record CP\n  parameter Integer m;\n  parameter Real PRef = 0;\nend CP;\nrecord Data\n"
                           "  parameter Integer m = 3;\n  parameter CP scp(m = m);\nend Data;\nmodel Core\n"
                           "  parameter CP cp(m = 5);\n  parameter Integer m = cp.m annotation(Evaluate = true);\nend Core;\n"
                           "model ST1\n  Core core(cp = d.scp);\n  parameter Data d;\nend ST1;\n"),
               std::string("class ST1\n  final parameter Integer core.cp.m = 3;\n  parameter Real core.cp.PRef = d.scp.PRef;\n"
                           "  final parameter Integer core.m = 3;\n  parameter Integer d.m = 3;\n"
                           "  parameter Integer d.scp.m = d.m;\n  parameter Real d.scp.PRef = 0.0;\nend ST1;\n"));
}

YMC_TEST(flatten_inherited_declaration_resolves_in_base_scope) {
  // 继承来的组件，其声明绑定 / 维 / extends 修饰在写它们的类的词法作用域里查名：派生类自己的同名参数 / 常量看不见（P.B 的
  // `p = q` 是 P.q，`p[n]` 是 P.n；多层继承时 B 的 extends 修饰 `x(start = q)` 在 B 里查）。期望 = omc 1.25.0 实跑（2026-10-06）。
  YMC_CHECK_EQ(flatten_src("package P\n  constant Real q = 1;\n  model B\n    parameter Real p = q;\n  end B;\nend P;\n"
                           "model LX1\n  parameter Real q = 3;\n  extends P.B;\nend LX1;\n"),
               std::string("class LX1\n  parameter Real q = 3.0;\n  parameter Real p = 1.0;\nend LX1;\n"));
  YMC_CHECK_EQ(flatten_src("package P\n  constant Integer n = 2;\n  model B\n    parameter Real p[n] = fill(1, n);\n  end B;\n"
                           "end P;\nmodel LX2\n  constant Integer n = 3;\n  extends P.B;\nend LX2;\n"),
               std::string("class LX2\n  constant Integer n = 3;\n  parameter Real p[1] = 1.0;\n  parameter Real p[2] = 1.0;\n"
                           "end LX2;\n"));
  YMC_CHECK_EQ(flatten_src("package P\n  constant Real q = 1;\n  model A\n    Real x(start = 0);\n  end A;\n  model B\n"
                           "    extends A(x(start = q));\n  end B;\nend P;\nmodel LX3\n  parameter Real q = 3;\n  extends P.B;\n"
                           "end LX3;\n"),
               std::string("class LX3\n  parameter Real q = 3.0;\n  Real x(start = 1.0);\nend LX3;\n"));
}

YMC_TEST(flatten_inherited_declaration_cannot_see_derived_element) {
  // 基类的声明引用只有派生类才有的名字：omc 报 "Variable q not found in scope B"；ymc 查不到 → SKIP（此前在派生类里找到了 q）。
  bool uns = false;
  const std::string out = flatten_src("model B\n  parameter Real p = q;\nend B;\nmodel LX4\n  parameter Real q = 3;\n  extends B;\n"
                                      "end LX4;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:unresolved name>"));
}

YMC_TEST(flatten_evaluate_structural_decision_follows_visit_order) {
  // Evaluate = true 的结构判定在 omc 按声明序访问组件时做：声明在后的 Evaluate = false 还没降级（仍是 PARAMETER）→ 照样
  // 结构求值、连带 final；声明在前的已降成非结构 → 不求值。期望 = omc 1.25.0 实跑（2026-10-07，下同）。
  YMC_CHECK_EQ(flatten_src("model EO1\n  parameter Integer n = k annotation(Evaluate = true);\n  parameter Integer k = j;\n"
                           "  parameter Integer j = 2 annotation(Evaluate = false);\n  Real y = k;\nend EO1;\n"),
               std::string("class EO1\n  final parameter Integer n = 2;\n  final parameter Integer k = 2;\n"
                           "  final parameter Integer j = 2;\n  Real y = 2.0;\nend EO1;\n"));
  YMC_CHECK_EQ(flatten_src("model EO2\n  parameter Integer j = 2 annotation(Evaluate = false);\n"
                           "  parameter Integer n = k annotation(Evaluate = true);\n  parameter Integer k = j;\n  Real y = k;\n"
                           "end EO2;\n"),
               std::string("class EO2\n  parameter Integer j = 2;\n  parameter Integer n = k;\n  parameter Integer k = j;\n"
                           "  Real y = /*Real*/(k);\nend EO2;\n"));
}

YMC_TEST(flatten_evaluate_structural_decision_depth_and_fixed) {
  // isBindingNotFixed 的 maxDepth = 4：三层别名链求值，四层当不固定；带 fixed 属性（哪怕 `fixed = true`）的参数也不固定。
  YMC_CHECK_EQ(flatten_src("model ED1\n  parameter Real p = a annotation(Evaluate = true);\n  parameter Real a = b;\n"
                           "  parameter Real b = c;\n  parameter Real c = d;\n  parameter Real d = 1;\n  Real y = p;\nend ED1;\n"),
               std::string("class ED1\n  parameter Real p = a;\n  parameter Real a = b;\n  parameter Real b = c;\n"
                           "  parameter Real c = d;\n  parameter Real d = 1.0;\n  Real y = p;\nend ED1;\n"));
  YMC_CHECK_EQ(flatten_src("model ED2\n  parameter Real p = a annotation(Evaluate = true);\n  parameter Real a = b;\n"
                           "  parameter Real b = c;\n  parameter Real c = 1;\n  Real y = p;\nend ED2;\n"),
               std::string("class ED2\n  final parameter Real p = 1.0;\n  final parameter Real a = 1.0;\n"
                           "  final parameter Real b = 1.0;\n  final parameter Real c = 1.0;\n  Real y = 1.0;\nend ED2;\n"));
  YMC_CHECK_EQ(flatten_src("model EF1\n  parameter Real q(fixed = true) = 3;\n  parameter Real p = q annotation(Evaluate = true);\n"
                           "  Real y = p;\nend EF1;\n"),
               std::string("class EF1\n  parameter Real q(fixed = true) = 3.0;\n  parameter Real p = q;\n  Real y = p;\nend EF1;\n"));
}

YMC_TEST(flatten_evaluate_parameter_array) {
  // Evaluate = true 的参数数组与标量一样当结构参数：各元素 final + 字面量、引用处代入（数组类型带 `each final unit`，同 MultiBody
  // World 的 n）；绑定依赖 Evaluate = false 的参数 → 不固定、照常打印；整个数组的别名（声明在后）、经子实例的整个数组也求值。
  // 期望值为 omc 1.25.0 对原文实跑（EA 探针 e1-e7）。
  YMC_CHECK_EQ(flatten_src("type Axis = Real[3](each final unit = \"1\");\nmodel EA1\n"
                           "  parameter Axis n = {0, -1, 0} \"dir\" annotation(Evaluate = true);\n  Real y = n[2] * time;\nend EA1;\n"),
               std::string("class EA1\n  final parameter Real n[1](unit = \"1\") = 0.0 \"dir\";\n"
                           "  final parameter Real n[2](unit = \"1\") = -1.0 \"dir\";\n"
                           "  final parameter Real n[3](unit = \"1\") = 0.0 \"dir\";\n  Real y = (-1.0) * time;\nend EA1;\n"));
  YMC_CHECK_EQ(flatten_src("model EA2\n  parameter Real p = 2 annotation(Evaluate = false);\n"
                           "  parameter Real n[2] = {p, 1} annotation(Evaluate = true);\n"
                           "  parameter Real m[2] = a annotation(Evaluate = true);\n  parameter Real a[2] = {3, 4};\nend EA2;\n"),
               std::string("class EA2\n  parameter Real p = 2.0;\n  parameter Real n[1] = p;\n  parameter Real n[2] = 1.0;\n"
                           "  final parameter Real m[1] = 3.0;\n  final parameter Real m[2] = 4.0;\n"
                           "  final parameter Real a[1] = 3.0;\n  final parameter Real a[2] = 4.0;\nend EA2;\n"));
  YMC_CHECK_EQ(flatten_src("model B\n  parameter Real a[2] = {1, 2};\nend B;\nmodel EA3\n  B b;\n"
                           "  parameter Real n[2] = b.a annotation(Evaluate = true);\n  Real y = n[2] * time;\nend EA3;\n"),
               std::string("class EA3\n  final parameter Real b.a[1] = 1.0;\n  final parameter Real b.a[2] = 2.0;\n"
                           "  final parameter Real n[1] = 1.0;\n  final parameter Real n[2] = 2.0;\n  Real y = 2.0 * time;\nend EA3;\n"));
}

YMC_TEST(flatten_condition_string_compare_forward_record_and_rem_inline) {
  // 条件组件的条件里比较字符串（逐字节字典序）；条件经修饰引用声明在后的记录字段（第二遍按第一遍的值求）；rem 照 omc 的
  // EarlyInline 内联成 `x - div(x, y) * y`。期望值为 omc 1.25.0 对原文实跑（CC 探针 c1-c4、RM 探针 r1-r2，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("model A\n  Real x;\nend A;\nmodel T\n  parameter String s(start = \"Y\");\n"
                           "  A a if s <> \"D\";\n  A b if s == \"D\";\nend T;\nmodel CC1\n  T t(s = \"D\");\nend CC1;\n"),
               std::string("class CC1\n  final parameter String t.s(start = \"Y\") = \"D\";\n  Real t.b.x;\nend CC1;\n"));
  YMC_CHECK_EQ(flatten_src("connector H\n  Real T;\n  flow Real Q;\nend H;\nconnector P\n  parameter Boolean u(start = true);\n"
                           "  H h if u;\nend P;\nrecord D\n  parameter Boolean ud = false;\nend D;\nmodel CC2\n"
                           "  P p(u = d.ud);\n  parameter D d;\nend CC2;\n"),
               std::string("class CC2\n  final parameter Boolean p.u(start = true) = false;\n"
                           "  final parameter Boolean d.ud = false;\nend CC2;\n"));
  YMC_CHECK_EQ(flatten_src("model RM1\n  Real y = rem(-time, 2) + rem(time, -3);\n  Integer k = rem(-7, 3);\nend RM1;\n"),
               std::string("class RM1\n  Real y = time - div(time, -3.0) * (-3.0) - (time + div(-time, 2.0) * 2.0);\n"
                           "  Integer k = -1;\nend RM1;\n"));
  // getInstanceName()：根类简单名 + 带下标的实例前缀，多维下标用 ", " 分隔（GI 探针 g1-g2）。
  YMC_CHECK_EQ(flatten_src("model A\n  parameter String s = getInstanceName();\nend A;\nmodel B\n  A a[1, 2];\nend B;\n"
                           "model GI1\n  B b[2];\n  parameter String t = getInstanceName();\nend GI1;\n"),
               std::string("class GI1\n  parameter String b[1].a[1,1].s = \"GI1.b[1].a[1, 1]\";\n"
                           "  parameter String b[1].a[1,2].s = \"GI1.b[1].a[1, 2]\";\n"
                           "  parameter String b[2].a[1,1].s = \"GI1.b[2].a[1, 1]\";\n"
                           "  parameter String b[2].a[1,2].s = \"GI1.b[2].a[1, 2]\";\n  parameter String t = \"GI1\";\nend GI1;\n"));
}

YMC_TEST(flatten_record_ctor_array_fields_and_function_transpose) {
  // 记录构造函数：extends 来的字段就地展开、数组字段打维、短类型字段只打基类型（不打 unit、注释）；模型里没有这个记录的实例
  // 也能构造调用；函数里取数组字段 `r.b[2]`、`r.T`，transpose / identity 在维已知时展开。期望值为 omc 1.25.0 对原文实跑
  //（RC 探针 r1-r3 / t1-t2，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("record Base\n  Real a;\nend Base;\ntype L = Real(unit = \"m\");\nrecord R\n  extends Base;\n"
                           "  L b[2] \"pos\";\nend R;\nfunction f\n  input R r;\n  output Real z;\nalgorithm\n  z := r.a + r.b[2];\n"
                           "end f;\nmodel RC1\n  Real z = f(R(time, {2, 3}));\nend RC1;\n"),
               std::string("function R \"Automatically generated record constructor for R\"\n  input Real a;\n  input Real[2] b;\n"
                           "  output R res;\nend R;\n\nfunction f\n  input R r;\n  output Real z;\nalgorithm\n  z := r.a + r.b[2];\n"
                           "end f;\n\nclass RC1\n  Real z = f(R(time, {2.0, 3.0}));\nend RC1;\n"));
  YMC_CHECK_EQ(flatten_src("record R\n  Real T[2, 2];\nend R;\nfunction f\n  input R r;\n  input Real v[2];\n  output Real y[2];\n"
                           "algorithm\n  y := transpose(r.T) * v + identity(2) * v;\nend f;\nmodel RC2\n"
                           "  R r(T = {{1, 2}, {3, time}});\n  Real y[2] = f(r, {1, 2});\nend RC2;\n"),
               std::string("function R \"Automatically generated record constructor for R\"\n  input Real[2, 2] T;\n  output R res;\n"
                           "end R;\n\nfunction f\n  input R r;\n  input Real[2] v;\n  output Real[2] y;\nalgorithm\n"
                           "  y := {r.T[1,1] * v[1] + r.T[2,1] * v[2] + v[1], r.T[1,2] * v[1] + r.T[2,2] * v[2] + v[2]};\nend f;\n\n"
                           "class RC2\n  Real r.T[1,1];\n  Real r.T[1,2];\n  Real r.T[2,1];\n  Real r.T[2,2];\n  Real y[1];\n"
                           "  Real y[2];\nequation\n  r.T = {{1.0, 2.0}, {3.0, time}};\n  y = f(r, {1.0, 2.0});\nend RC2;\n"));
}

YMC_TEST(flatten_record_valued_constant_calls) {
  // 记录值的常量函数调用：方程里打成构造调用（字段字面量），参数绑定照构造调用摊到字段（数组字段逐元素、短类型字段带属性、
  // 空基类 extends 不碍事）；函数体里 smooth 的数组实参、cross 展开。期望值为 omc 1.25.0 对原文实跑（RV 探针 v1-v3、SM 探针
  // s1、RC 探针 t4，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("record Icon\nend Icon;\nrecord R\n  extends Icon;\n  Real T[2, 2];\n  Real w[2];\nend R;\n"
                           "function nullR\n  output R r;\nalgorithm\n  r := R(T = identity(2), w = zeros(2));\nend nullR;\n"
                           "model RV1\n  R r;\n  parameter R p = nullR();\nequation\n  r = nullR();\nend RV1;\n"),
               std::string("function R \"Automatically generated record constructor for R\"\n  input Real[2, 2] T;\n  input Real[2] w;\n"
                           "  output R res;\nend R;\n\nclass RV1\n  Real r.T[1,1];\n  Real r.T[1,2];\n  Real r.T[2,1];\n"
                           "  Real r.T[2,2];\n  Real r.w[1];\n  Real r.w[2];\n  parameter Real p.T[1,1] = 1.0;\n"
                           "  parameter Real p.T[1,2] = 0.0;\n  parameter Real p.T[2,1] = 0.0;\n  parameter Real p.T[2,2] = 1.0;\n"
                           "  parameter Real p.w[1] = 0.0;\n  parameter Real p.w[2] = 0.0;\nequation\n"
                           "  r = R({{1.0, 0.0}, {0.0, 1.0}}, {0.0, 0.0});\nend RV1;\n"));
  YMC_CHECK_EQ(flatten_src("type L = Real(unit = \"m\");\nrecord R\n  L x;\nend R;\nfunction g\n  output R r;\nalgorithm\n"
                           "  r := R(1);\nend g;\nfunction nz\n  input Real v[3];\n  input Real u[3];\n  output Real c[3];\n"
                           "algorithm\n  c := smooth(0, v) + cross(v, u);\nend nz;\nmodel RV2\n  parameter R r = g();\n"
                           "  Real c[3] = nz({time, 1, 2}, {3, time, 4});\nend RV2;\n"),
               std::string("function nz\n  input Real[3] v;\n  input Real[3] u;\n  output Real[3] c;\nalgorithm\n"
                           "  c := smooth(0, v) + {v[2] * u[3] - v[3] * u[2], v[3] * u[1] - v[1] * u[3], v[1] * u[2] - v[2] * u[1]};\n"
                           "end nz;\n\nclass RV2\n  parameter Real r.x(unit = \"m\") = 1.0;\n  Real c[1];\n  Real c[2];\n  Real c[3];\n"
                           "equation\n  c = nz({time, 1.0, 2.0}, {3.0, time, 4.0});\nend RV2;\n"));
}

YMC_TEST(flatten_array_call_inside_expressions) {
  // 模型里嵌在运算中的数组值调用：所在等式 / Var 数组的绑定整体保留，运算留符号形态——运算数照 omc 的定型提实型（字面量逐叶、
  // 调用整体 CAST）、标量挪右、`.-` 改 `.+ -`、积不展开、不含数组调用的运算数展开成字面量、数组 + / - 的负号挪动；if 分支里的
  // 同样不展开；组件数组上推导式修饰按元素代入迭代变量；vector 的字面量实参压成向量字面量。期望值为 omc 1.25.0 对原文实跑
  //（AX 探针 ut，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Real t;\n  output Real y[3];\nalgorithm\n  y := {t, 2 * t, 3 * t};\nend f;\nmodel AX1\n"
                           "  parameter Real a[3] = {1, 2, 3};\n  parameter Real m[3, 3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};\n"
                           "  Real x[3], y[3], z[3], w[3], v[3], u[3], s;\nequation\n  x = {1, 2, 3} + f(time);\n  y = 2 * f(time);\n"
                           "  z = m * f(time);\n  w = f(time) + a / 2;\n  v = f(time) .- 1;\n  u = -f(time) + a;\n  s = f(time) * a;\n"
                           "end AX1;\n"),
               std::string("function f\n  input Real t;\n  output Real[3] y;\nalgorithm\n  y := {t, 2.0 * t, 3.0 * t};\nend f;\n\n"
                           "class AX1\n  parameter Real a[1] = 1.0;\n  parameter Real a[2] = 2.0;\n  parameter Real a[3] = 3.0;\n"
                           "  parameter Real m[1,1] = 1.0;\n  parameter Real m[1,2] = 0.0;\n  parameter Real m[1,3] = 0.0;\n"
                           "  parameter Real m[2,1] = 0.0;\n  parameter Real m[2,2] = 1.0;\n  parameter Real m[2,3] = 0.0;\n"
                           "  parameter Real m[3,1] = 0.0;\n  parameter Real m[3,2] = 0.0;\n  parameter Real m[3,3] = 1.0;\n  Real x[1];\n"
                           "  Real x[2];\n  Real x[3];\n  Real y[1];\n  Real y[2];\n  Real y[3];\n  Real z[1];\n  Real z[2];\n  Real z[3];\n"
                           "  Real w[1];\n  Real w[2];\n  Real w[3];\n  Real v[1];\n  Real v[2];\n  Real v[3];\n  Real u[1];\n  Real u[2];\n"
                           "  Real u[3];\n  Real s;\nequation\n  x = {1.0, 2.0, 3.0} + f(time);\n  y = f(time) * 2.0;\n  z = m * f(time);\n"
                           "  w = f(time) + {a[1] / 2.0, a[2] / 2.0, a[3] / 2.0};\n  v = f(time) .+ -1.0;\n  u = a - f(time);\n"
                           "  s = f(time) * a;\nend AX1;\n"));
  YMC_CHECK_EQ(flatten_src("function f\n  input Real t;\n  output Real y[3];\nalgorithm\n  y := {t, 2 * t, 3 * t};\nend f;\nfunction h\n"
                           "  input Integer k;\n  output Integer y[3];\nalgorithm\n  y := {k, 2 * k, 3 * k};\nend h;\nmodel AX2\n"
                           "  parameter Real a[3] = {1, 2, 3};\n  Integer k = integer(time);\n  Real x[3] = a + f(time);\n"
                           "  Real y[3] = h(k);\n  Real z[3];\nequation\n  z = if time > 0.5 then 2 * f(time) else a;\nend AX2;\n"),
               std::string("function f\n  input Real t;\n  output Real[3] y;\nalgorithm\n  y := {t, 2.0 * t, 3.0 * t};\nend f;\n\n"
                           "function h\n  input Integer k;\n  output Integer[3] y;\nalgorithm\n  y := {k, 2 * k, 3 * k};\nend h;\n\n"
                           "class AX2\n  parameter Real a[1] = 1.0;\n  parameter Real a[2] = 2.0;\n  parameter Real a[3] = 3.0;\n"
                           "  Integer k = integer(time);\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y[1];\n  Real y[2];\n"
                           "  Real y[3];\n  Real z[1];\n  Real z[2];\n  Real z[3];\nequation\n  x = a + f(time);\n  y = /*Real[3]*/(h(k));\n"
                           "  z = if time > 0.5 then f(time) * 2.0 else a;\nend AX2;\n"));
  YMC_CHECK_EQ(flatten_src("function g\n  input Real t[3];\n  output Real y[3];\nalgorithm\n  y := 2 * t;\nend g;\nmodel S\n"
                           "  input Real r[3] = {0, 0, 0};\nend S;\nmodel AX3\n  Real p[2, 2] = {{1, 2}, {3, 4}} * time;\n"
                           "  Real r0[3] = {1, 2, 3} * time;\n  S s[2](r = {r0 + g(vector([p[i, :]; 0])) for i in 1:2});\nend AX3;\n"),
               std::string("function g\n  input Real[3] t;\n  output Real[3] y;\nalgorithm\n  y := {2.0 * t[1], 2.0 * t[2], 2.0 * t[3]};\n"
                           "end g;\n\nclass AX3\n  Real p[1,1];\n  Real p[1,2];\n  Real p[2,1];\n  Real p[2,2];\n  Real r0[1];\n"
                           "  Real r0[2];\n  Real r0[3];\n  Real s[1].r[1];\n  Real s[1].r[2];\n  Real s[1].r[3];\n  Real s[2].r[1];\n"
                           "  Real s[2].r[2];\n  Real s[2].r[3];\nequation\n  p = {{time, 2.0 * time}, {3.0 * time, 4.0 * time}};\n"
                           "  r0 = {time, 2.0 * time, 3.0 * time};\n  s[1].r = r0 + g({p[1,1], p[1,2], 0.0});\n"
                           "  s[2].r = r0 + g({p[2,1], p[2,2], 0.0});\nend AX3;\n"));
}

YMC_TEST(flatten_component_functions_and_alias_defaults) {
  // 组件里的函数（`w.acc(time)`，outer 取 inner 的路径）：函数名 = 根类名 + 组件实例路径 + 函数名，短类修饰在组件里定型、
  // 调用处补成修饰式（`w.g`），函数定义里的缺省求成值（参数 → 值、变量原样）；模型内函数别名的缺省同理；枚举类型的函数
  // 变量打成字面量表；函数体引用派生类型的包常量内联其值。期望值为 omc 1.25.0 对原文实跑（CF 探针 ut，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("function accBase\n  input Real r;\n  input Real gg = 1;\n  output Real a;\nalgorithm\n  a := gg * r;\n"
                           "end accBase;\nmodel W\n  parameter Real g = 9.81;\n  Real v = time;\n"
                           "  replaceable function acc = accBase(gg = g);\n  replaceable function accv = accBase(gg = v);\nend W;\nmodel B\n"
                           "  outer W w;\n  Real y = w.acc(time);\nend B;\nmodel CU1\n  inner W w(g = 4);\n  B b;\n  W w2;\n"
                           "  Real z = w2.acc(time) + w2.accv(2 * time);\nend CU1;\n"),
               std::string("function CU1.w.acc\n  input Real r;\n  input Real gg = 4.0;\n  output Real a;\nalgorithm\n  a := gg * r;\n"
                           "end CU1.w.acc;\n\nfunction CU1.w2.acc\n  input Real r;\n  input Real gg = 9.81;\n  output Real a;\nalgorithm\n"
                           "  a := gg * r;\nend CU1.w2.acc;\n\nfunction CU1.w2.accv\n  input Real r;\n  input Real gg = w2.v;\n"
                           "  output Real a;\nalgorithm\n  a := gg * r;\nend CU1.w2.accv;\n\nclass CU1\n  parameter Real w.g = 4.0;\n"
                           "  Real w.v = time;\n  Real b.y = CU1.w.acc(time, w.g);\n  parameter Real w2.g = 9.81;\n  Real w2.v = time;\n"
                           "  Real z = CU1.w2.acc(time, w2.g) + CU1.w2.accv(2.0 * time, w2.v);\nend CU1;\n"));
  YMC_CHECK_EQ(flatten_src("function accBase\n  input Real r;\n  input Real gg = 1;\n  output Real a;\nalgorithm\n  a := gg * r;\n"
                           "end accBase;\nfunction accV\n  input Real r;\n  input Real gv[2] = {0, 0};\n  output Real a[2];\nalgorithm\n"
                           "  a := gv * r;\nend accV;\nmodel CU2\n  parameter Real g = 3;\n"
                           "  parameter Real n[2] = {0, -1} annotation(Evaluate = true);\n  function acc = accBase(gg = g);\n"
                           "  function accn = accV(gv = g * n);\n  Real y = acc(time);\n  Real x[2] = accn(time);\nend CU2;\n"),
               std::string("function CU2.acc\n  input Real r;\n  input Real gg = 3.0;\n  output Real a;\nalgorithm\n  a := gg * r;\n"
                           "end CU2.acc;\n\nfunction CU2.accn\n  input Real r;\n  input Real[2] gv = {0.0, -3.0};\n  output Real[2] a;\n"
                           "algorithm\n  a := {gv[1] * r, gv[2] * r};\nend CU2.accn;\n\nclass CU2\n  parameter Real g = 3.0;\n"
                           "  final parameter Real n[1] = 0.0;\n  final parameter Real n[2] = -1.0;\n  Real y = CU2.acc(time, g);\n"
                           "  Real x[1];\n  Real x[2];\nequation\n  x = CU2.accn(time, {0.0, g * (-1.0)});\nend CU2;\n"));
  YMC_CHECK_EQ(flatten_src("package P\n  type GT = enumeration(No, Uni, Point);\nend P;\nfunction h\n  input Real r;\n"
                           "  input P.GT t = P.GT.Uni \"type\";\n  output Real y;\nalgorithm\n  y := if t == P.GT.Uni then r else 0;\n"
                           "end h;\nmodel CU3\n  P.GT e = P.GT.Point;\n  Real y = h(time);\n  Real y2 = h(time, e);\nend CU3;\n"),
               std::string("function h\n  input Real r;\n  input enumeration(No, Uni, Point) t = P.GT.Uni \"type\";\n  output Real y;\n"
                           "algorithm\n  y := if t == P.GT.Uni then r else 0.0;\nend h;\n\nclass CU3\n"
                           "  enumeration(No, Uni, Point) e = P.GT.Point;\n  Real y = h(time, P.GT.Uni);\n  Real y2 = h(time, e);\n"
                           "end CU3;\n"));
  YMC_CHECK_EQ(flatten_src("package K\n  type TC = Real(unit = \"degC\");\n  constant TC T0 = -273.15;\nend K;\nfunction f\n"
                           "  input Real c;\n  output Real k;\nalgorithm\n  k := c - K.T0;\nend f;\nmodel CU4\n  Real y = f(time);\n"
                           "end CU4;\n"),
               std::string("function f\n  input Real c;\n  output Real k;\nalgorithm\n  k := c + 273.15;\nend f;\n\nclass CU4\n"
                           "  Real y = f(time);\nend CU4;\n"));
}

YMC_TEST(flatten_size_in_matrix_marks_structural) {
  // 结构参数的绑定里矩阵构造套 size(a, k)：a 一并成结构参数（各元素 final、引用它的参数绑定折成值，KinematicPTP2 的
  // `nout = max([size(deltaq, 1); size(qd_max, 1)])`）；a 的 `:` 维还没发射时按绑定的书写形状取（CombiTimeTable 的
  // `columns = 2:size(table, 2)`）。期望值为 omc 1.25.0 对原文实跑（SK 探针 ut，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("model K\nparameter Real deltaq[:] = {1};\nparameter Real qd_max[:] = {1};\n"
                           "final parameter Integer nout = max([size(deltaq, 1); size(qd_max, 1)]);\n"
                           "parameter Real p_deltaq[nout] = (if size(deltaq, 1) == 1 then ones(nout) * deltaq[1] else deltaq);\n"
                           "Real y[nout];\nequation\ny = p_deltaq * time;\nend K;\nmodel SK1\nK k(deltaq = {2, 3});\nend SK1;\n"),
               std::string("class SK1\n  final parameter Real k.deltaq[1] = 2.0;\n  final parameter Real k.deltaq[2] = 3.0;\n"
                           "  final parameter Real k.qd_max[1] = 1.0;\n  final parameter Integer k.nout = 2;\n"
                           "  parameter Real k.p_deltaq[1] = 2.0;\n  parameter Real k.p_deltaq[2] = 3.0;\n  Real k.y[1];\n  Real k.y[2];\n"
                           "equation\n  k.y[1] = k.p_deltaq[1] * time;\n  k.y[2] = k.p_deltaq[2] * time;\nend SK1;\n"));
  YMC_CHECK_EQ(flatten_src("model SK2\n  parameter Real table[:, :] = [0, 0, 0; 5, 0, 0];\n"
                           "  parameter Integer columns[:] = 2:size(table, 2);\n  parameter Real offset[:] = {0};\n"
                           "  final parameter Integer nout = max([size(columns, 1); size(offset, 1)]);\n  Real y[nout];\nequation\n"
                           "  y = fill(time, nout);\nend SK2;\n"),
               std::string("class SK2\n  parameter Real table[1,1] = 0.0;\n  parameter Real table[1,2] = 0.0;\n"
                           "  parameter Real table[1,3] = 0.0;\n  parameter Real table[2,1] = 5.0;\n  parameter Real table[2,2] = 0.0;\n"
                           "  parameter Real table[2,3] = 0.0;\n  final parameter Integer columns[1] = 2;\n"
                           "  final parameter Integer columns[2] = 3;\n  final parameter Real offset[1] = 0.0;\n"
                           "  final parameter Integer nout = 2;\n  Real y[1];\n  Real y[2];\nequation\n  y[1] = time;\n  y[2] = time;\n"
                           "end SK2;\n"));
}

YMC_TEST(flatten_structural_array_whole_references) {
  // 压平末尾代入结构参数：打印标记随克隆的节点走（`x = k * f(time)` → `f(time) * 2.0`）；结构参数数组的整体引用代成
  // 整个数组的字面量、调用随即求值，不再含数组调用的等式逐元素、绑定方程展开（`y = g * nrm(n)`、`z = nrm(n) * time`）；
  // 函数体 if 分支里的 zeros / fill 展开；外部对象的向前引用按声明类型求类型名。期望值为 omc 1.25.0 对原文实跑（WR 探针
  // ut，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Real t;\n  output Real y[3];\nalgorithm\n  y := {t, 2 * t, 3 * t};\nend f;\nmodel CI1\n"
                           "  parameter Integer k = 2;\n  Real z[k];\n  Real x[3];\n  Real w[3];\nequation\n  z = fill(time, k);\n"
                           "  x = k * f(time);\n  w = f(time) .- k;\nend CI1;\n"),
               std::string("function f\n  input Real t;\n  output Real[3] y;\nalgorithm\n  y := {t, 2.0 * t, 3.0 * t};\nend f;\n\n"
                           "class CI1\n  final parameter Integer k = 2;\n  Real z[1];\n  Real z[2];\n  Real x[1];\n  Real x[2];\n"
                           "  Real x[3];\n  Real w[1];\n  Real w[2];\n  Real w[3];\nequation\n  z[1] = time;\n  z[2] = time;\n"
                           "  x = f(time) * 2.0;\n  w = f(time) .+ -2.0;\nend CI1;\n"));
  YMC_CHECK_EQ(flatten_src("function nrm\n  input Real v[3];\n  output Real r[3];\nalgorithm\n  r := v / sqrt(v * v);\nend nrm;\nmodel CI2\n"
                           "  parameter Real n[3] = {0, -1, 0} annotation(Evaluate = true);\n  parameter Real g = 9.81;\n  Real y[3];\n"
                           "  Real z[3] = nrm(n) * time;\nequation\n  y = g * nrm(n);\nend CI2;\n"),
               std::string("class CI2\n  final parameter Real n[1] = 0.0;\n  final parameter Real n[2] = -1.0;\n"
                           "  final parameter Real n[3] = 0.0;\n  parameter Real g = 9.81;\n  Real y[1];\n  Real y[2];\n  Real y[3];\n"
                           "  Real z[1];\n  Real z[2];\n  Real z[3];\nequation\n  z = {0.0, (-1.0) * time, 0.0};\n  y[1] = 0.0;\n"
                           "  y[2] = g * (-1.0);\n  y[3] = 0.0;\nend CI2;\n"));
  YMC_CHECK_EQ(flatten_src("function f\n  input Real x[3];\n  input Boolean b;\n  output Real y[3];\n  output Real z[3];\n"
                           "  output Real w[3];\nalgorithm\n  y := if b then x else zeros(3);\n  z := zeros(3);\n"
                           "  w := if b then x else fill(1, 3);\nend f;\nmodel CI3\n  Real y[3] = f({time, 1, 2}, time > 0.5);\nend CI3;\n"),
               std::string("function f\n  input Real[3] x;\n  input Boolean b;\n  output Real[3] y;\n  output Real[3] z;\n"
                           "  output Real[3] w;\nalgorithm\n  y := if b then x else {0.0, 0.0, 0.0};\n  z := {0.0, 0.0, 0.0};\n"
                           "  w := if b then x else {1.0, 1.0, 1.0};\nend f;\n\nclass CI3\n  Real y[1];\n  Real y[2];\n  Real y[3];\n"
                           "equation\n  y = f({time, 1.0, 2.0}, time > 0.5)[1];\nend CI3;\n"));
  YMC_CHECK_EQ(flatten_src("package P\n  class EO\n    extends ExternalObject;\n    function constructor\n      input Integer c[:];\n"
                           "      output EO o;\n      external \"C\" o = initEO(c, size(c, 1));\n    end constructor;\n"
                           "    function destructor\n      input EO o;\n      external \"C\" freeEO(o);\n    end destructor;\n  end EO;\n"
                           "  function getT\n    input EO o;\n    output Real t;\n    external \"C\" t = getT_c(o);\n  end getT;\nend P;\n"
                           "model CI4\n  parameter Integer columns[:] = {2, 3};\n  final parameter Real tmin = P.getT(tid);\nprotected\n"
                           "  parameter P.EO tid = P.EO(columns);\nend CI4;\n"),
               std::string("impure function P.EO.constructor\n  input Integer[:] c;\n  output P.EO o;\n\n"
                           "  external \"C\" o = initEO(c, size(c, 1));\nend P.EO.constructor;\n\nimpure function P.EO.destructor\n"
                           "  input P.EO o;\n\n  external \"C\" freeEO(o);\nend P.EO.destructor;\n\nimpure function P.getT\n"
                           "  input P.EO o;\n  output Real t;\n\n  external \"C\" t = getT_c(o);\nend P.getT;\n\nclass CI4\n"
                           "  parameter Integer columns[1] = 2;\n  parameter Integer columns[2] = 3;\n"
                           "  final parameter Real tmin = P.getT(tid);\n  protected parameter P.EO tid = P.EO.constructor(columns);\n"
                           "end CI4;\n"));
}

YMC_TEST(flatten_enum_attribute_expressions_when_initial_and_initial_algorithm_params) {
  // 枚举属性值是参数引用 / if 式 / 常量：照常定型、保留符号（`start = y0`）；值的枚举类型须相符（别的枚举类型 → omc "Type
  // mismatch in binding" → SKIP）。when 条件向量只要有一个元素是 initial() 本身或不含 initial() 就不告警（checkWhenInitial），
  // 都不是 → omc 告警 → SKIP。参数只在初始算法里可赋（checkAssignment）。期望值为 omc 1.25.0 对原文实跑（EA / WJ / IA 探针
  // ut，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("type E = enumeration(a, b, c);\nmodel EU1\n  parameter E y0 = E.b;\n  constant E c0 = E.c;\n"
                           "  parameter Boolean p = true;\n  discrete E y(start = y0, fixed = true);\n"
                           "  discrete E u(start = if p then E.a else y0, min = E.a, max = c0);\nequation\n"
                           "  when time > 0.5 then\n    y = E.c;\n    u = pre(y);\n  end when;\nend EU1;\n"),
               std::string("class EU1\n  parameter enumeration(a, b, c) y0 = E.b;\n  constant enumeration(a, b, c) c0 = E.c;\n"
                           "  parameter Boolean p = true;\n  discrete enumeration(a, b, c) y(start = y0, fixed = true);\n"
                           "  discrete enumeration(a, b, c) u(min = E.a, max = E.c, start = if p then E.a else y0);\nequation\n"
                           "  when time > 0.5 then\n    y = E.c;\n    u = pre(y);\n  end when;\nend EU1;\n"));
  YMC_CHECK_EQ(flatten_src("type E = enumeration(a, b, c);\ntype F = enumeration(x, y);\nmodel EU2\n  parameter F f0 = F.x;\n"
                           "  discrete E y(start = f0, fixed = true);\nequation\n  when time > 0.5 then\n    y = E.c;\n"
                           "  end when;\nend EU2;\n"),
               std::string("<unsupported:attribute type mismatch>"));
  YMC_CHECK_EQ(flatten_src("model WU1\n  Boolean x = time > 0.3;\n  discrete Real y(start = 0, fixed = true);\n"
                           "  discrete Real z(start = 0, fixed = true);\nequation\n"
                           "  when {initial(), change(x) and not initial()} then\n    y = time;\n  end when;\nalgorithm\n"
                           "  when {x and not initial(), initial()} then\n    z := 2 * time;\n  end when;\nend WU1;\n"),
               std::string("class WU1\n  Boolean x = time > 0.3;\n  discrete Real y(start = 0.0, fixed = true);\n"
                           "  discrete Real z(start = 0.0, fixed = true);\nequation\n"
                           "  when {initial(), change(x) and not initial()} then\n    y = time;\n  end when;\nalgorithm\n"
                           "  when {x and not initial(), initial()} then\n    z := 2.0 * time;\n  end when;\nend WU1;\n"));
  YMC_CHECK_EQ(flatten_src("model WU2\n  Boolean x = time > 0.3;\n  discrete Real y(start = 0, fixed = true);\nequation\n"
                           "  when {not initial(), x and not initial()} then\n    y = time;\n  end when;\nend WU2;\n"),
               std::string("<unsupported:when condition form>"));
  YMC_CHECK_EQ(flatten_src("model IU1\n  parameter Real a = 2;\n  parameter Real k(fixed = false);\n"
                           "  parameter Real m[2](each fixed = false);\n  Real x;\ninitial algorithm\n"
                           "  assert(a > 0, \"a must be positive\");\n  k := a * 2;\n  m := {k, 2 * k};\nequation\n"
                           "  x = k * time + m[2];\nend IU1;\n"),
               std::string("class IU1\n  parameter Real a = 2.0;\n  parameter Real k(fixed = false);\n"
                           "  parameter Real m[1](fixed = false);\n  parameter Real m[2](fixed = false);\n  Real x;\n"
                           "initial algorithm\n  assert(a > 0.0, \"a must be positive\");\n  k := a * 2.0;\n"
                           "  m := {k, 2.0 * k};\nequation\n  x = k * time + m[2];\nend IU1;\n"));
  YMC_CHECK_EQ(flatten_src("model IU2\n  parameter Real a = 2;\n  parameter Real k(fixed = false);\n  Real x;\nalgorithm\n"
                           "  k := a * 2;\nequation\n  x = k * time;\nend IU2;\n"),
               std::string("<unsupported:algorithm assignment to non-continuous variable>"));
}

YMC_TEST(flatten_connector_record_params_fixed_override_array_algorithms_enum_tables) {
  // 连接器里的参数记录成员按字段拆、各字段生成相等断言；外来修饰盖过类里的 `fixed = T0fixed` 时 T0fixed 不标 final；组件数组
  // 各实例各出一段算法（目标 `s[1].x :=`）；枚举维常量表的运行时下标照写（`{2, 3, 4, 1}[b]`）。期望值为 omc 1.25.0 对原文
  // 实跑（CP / FX / AA / CE 探针，2026-10-07）。
  YMC_CHECK_EQ(flatten_src("record Medium\n  parameter Real rho = 1;\n  parameter Real cp = 2;\nend Medium;\n"
                           "connector FlowPort\n  parameter Medium medium;\n  Real p;\n  flow Real m_flow;\nend FlowPort;\n"
                           "model Comp\n  parameter Medium medium = Medium();\n  FlowPort port(final medium = medium);\n"
                           "equation\n  port.m_flow = port.p * medium.rho;\nend Comp;\nmodel CP1\n"
                           "  Comp a(medium = Medium(rho = 3)), b;\nequation\n  connect(a.port, b.port);\nend CP1;\n"),
               std::string("class CP1\n  parameter Real a.medium.rho = 3.0;\n  parameter Real a.medium.cp = 2.0;\n"
                           "  final parameter Real a.port.medium.rho = a.medium.rho;\n"
                           "  final parameter Real a.port.medium.cp = a.medium.cp;\n  Real a.port.p;\n  Real a.port.m_flow;\n"
                           "  parameter Real b.medium.rho = 1.0;\n  parameter Real b.medium.cp = 2.0;\n"
                           "  final parameter Real b.port.medium.rho = b.medium.rho;\n"
                           "  final parameter Real b.port.medium.cp = b.medium.cp;\n  Real b.port.p;\n  Real b.port.m_flow;\n"
                           "equation\n"
                           "  assert(abs(a.port.medium.rho - b.port.medium.rho) <= 0.0, \"Connected constants/parameters must be equal\");\n"
                           "  assert(abs(a.port.medium.cp - b.port.medium.cp) <= 0.0, \"Connected constants/parameters must be equal\");\n"
                           "  a.port.p = b.port.p;\n  b.port.m_flow + a.port.m_flow = 0.0;\n"
                           "  a.port.m_flow = a.port.p * a.medium.rho;\n  b.port.m_flow = b.port.p * b.medium.rho;\nend CP1;\n"));
  YMC_CHECK_EQ(flatten_src("model C\n  parameter Real T0 = 300;\n  parameter Boolean T0fixed = true;\n"
                           "  Real T(start = T0, fixed = T0fixed);\nequation\n  der(T) = -T;\nend C;\nmodel FX1\n  C c1;\n"
                           "  C c2(T(fixed = true));\nend FX1;\n"),
               std::string("class FX1\n  parameter Real c1.T0 = 300.0;\n  final parameter Boolean c1.T0fixed = true;\n"
                           "  Real c1.T(start = c1.T0, fixed = true);\n  parameter Real c2.T0 = 300.0;\n"
                           "  parameter Boolean c2.T0fixed = true;\n  Real c2.T(start = c2.T0, fixed = true);\nequation\n"
                           "  der(c1.T) = -c1.T;\n  der(c2.T) = -c2.T;\nend FX1;\n"));
  YMC_CHECK_EQ(flatten_src("model Sub\n  Real x;\n  Integer k;\n  discrete Real y(start = 0, fixed = true);\nalgorithm\n"
                           "  x := time;\n  k := integer(x * 3);\n  when time > 0.5 then\n    y := pre(y) + 1;\n  end when;\n"
                           "end Sub;\nmodel AA1\n  Sub s[2];\nend AA1;\n"),
               std::string("class AA1\n  Real s[1].x;\n  Integer s[1].k;\n  discrete Real s[1].y(start = 0.0, fixed = true);\n"
                           "  Real s[2].x;\n  Integer s[2].k;\n  discrete Real s[2].y(start = 0.0, fixed = true);\nalgorithm\n"
                           "  s[1].x := time;\n  s[1].k := integer(s[1].x * 3.0);\n  when time > 0.5 then\n"
                           "    s[1].y := pre(s[1].y) + 1.0;\n  end when;\nalgorithm\n  s[2].x := time;\n"
                           "  s[2].k := integer(s[2].x * 3.0);\n  when time > 0.5 then\n    s[2].y := pre(s[2].y) + 1.0;\n"
                           "  end when;\nend AA1;\n"));
  YMC_CHECK_EQ(flatten_src("type L = enumeration(U, X, Z, O);\nmodel CE2\n"
                           "  constant Integer t[L, L] = [1, 2, 3, 4; 2, 3, 4, 1; 3, 4, 1, 2; 4, 1, 2, 3];\n"
                           "  constant Real r[L] = {1, 2, 3, 4};\n  discrete L a(start = L.U, fixed = true);\n"
                           "  discrete L b(start = L.X, fixed = true);\n  Integer k1, k2, k3;\n  Real y;\nalgorithm\n"
                           "  when time > 0.5 then\n    a := L.Z;\n  end when;\n  k1 := t[L.X, b];\n  k2 := t[a, L.X];\n"
                           "equation\n  k3 = t[b, a];\n  y = r[a] * time;\nend CE2;\n"),
               std::string("class CE2\n  constant Integer t[L.U,L.U] = 1;\n  constant Integer t[L.U,L.X] = 2;\n"
                           "  constant Integer t[L.U,L.Z] = 3;\n  constant Integer t[L.U,L.O] = 4;\n"
                           "  constant Integer t[L.X,L.U] = 2;\n  constant Integer t[L.X,L.X] = 3;\n"
                           "  constant Integer t[L.X,L.Z] = 4;\n  constant Integer t[L.X,L.O] = 1;\n"
                           "  constant Integer t[L.Z,L.U] = 3;\n  constant Integer t[L.Z,L.X] = 4;\n"
                           "  constant Integer t[L.Z,L.Z] = 1;\n  constant Integer t[L.Z,L.O] = 2;\n"
                           "  constant Integer t[L.O,L.U] = 4;\n  constant Integer t[L.O,L.X] = 1;\n"
                           "  constant Integer t[L.O,L.Z] = 2;\n  constant Integer t[L.O,L.O] = 3;\n"
                           "  constant Real r[L.U] = 1.0;\n  constant Real r[L.X] = 2.0;\n  constant Real r[L.Z] = 3.0;\n"
                           "  constant Real r[L.O] = 4.0;\n  discrete enumeration(U, X, Z, O) a(start = L.U, fixed = true);\n"
                           "  discrete enumeration(U, X, Z, O) b(start = L.X, fixed = true);\n  Integer k1;\n  Integer k2;\n"
                           "  Integer k3;\n  Real y;\nequation\n"
                           "  k3 = {{1, 2, 3, 4}, {2, 3, 4, 1}, {3, 4, 1, 2}, {4, 1, 2, 3}}[b, a];\n"
                           "  y = {1.0, 2.0, 3.0, 4.0}[a] * time;\nalgorithm\n  when time > 0.5 then\n    a := L.Z;\n"
                           "  end when;\n  k1 := {2, 3, 4, 1}[b];\n"
                           "  k2 := {{1, 2, 3, 4}, {2, 3, 4, 1}, {3, 4, 1, 2}, {4, 1, 2, 3}}[a, L.X];\nend CE2;\n"));
}

YMC_TEST(flatten_enum_tables_runtime_enum_subscripts_and_alias_connectors) {
  // 包里的枚举值真值表（矩阵 / 3 维 `[{{{…}}}]` 写法）按运行时枚举下标取元素：参数下标求值标 final、已知前导下标取元素、
  // 嵌套的表取元素照写；变量 / 参数数组的枚举维上的运行时下标照写；import 别名下的短类连接器数组整体连接；枚举区间常量
  // 数组的 `:` 维与 `Integer(pre(x))`；枚举数组属性 `start = fill(L.U, n)` 逐元素摊分。期望值为 omc 1.25.0 对原文实跑
  //（DG 探针 ut，2026-10-08）。
  YMC_CHECK_EQ(flatten_src("package Pk\n  type L = enumeration(U, X, Z, O);\n  type R = enumeration(U, X, Z);\n"
                           "  type S = enumeration(A, B);\n  package Tables\n    constant R Conv[L] = {R.U, R.X, R.Z, R.X};\n"
                           "    constant L T3[S, R, R] = [{{{L.U, L.X, L.Z}, {L.X, L.X, L.O}, {L.Z, L.O, L.Z}}, {{L.O, L.U, L.X}, {L.Z, L.Z, L.Z}, {L.U, L.U, L.U}}}];\n"
                           "  end Tables;\nend Pk;\nmodel TQ4\n  import T = Pk.Tables;\n  import Pk.L;\n"
                           "  parameter Pk.S strength = Pk.S.B;\n  discrete L a(start = L.U, fixed = true);\n"
                           "  discrete L b(start = L.X, fixed = true);\n  L y;\nalgorithm\n  when time > 0.5 then\n"
                           "    a := L.Z;\n  end when;\n  y := T.T3[strength, T.Conv[a], T.Conv[b]];\nend TQ4;\n"),
               std::string("class TQ4\n  final parameter enumeration(A, B) strength = Pk.S.B;\n"
                           "  discrete enumeration(U, X, Z, O) a(start = Pk.L.U, fixed = true);\n"
                           "  discrete enumeration(U, X, Z, O) b(start = Pk.L.X, fixed = true);\n  enumeration(U, X, Z, O) y;\n"
                           "algorithm\n  when time > 0.5 then\n    a := Pk.L.Z;\n  end when;\n"
                           "  y := {{Pk.L.O, Pk.L.U, Pk.L.X}, {Pk.L.Z, Pk.L.Z, Pk.L.Z}, {Pk.L.U, Pk.L.U, Pk.L.U}}[{Pk.R.U, Pk.R.X, Pk.R.Z, Pk.R.X}[a], {Pk.R.U, Pk.R.X, Pk.R.Z, Pk.R.X}[b]];\n"
                           "end TQ4;\n"));
  YMC_CHECK_EQ(flatten_src("type L = enumeration(U, X, Z, O);\nmodel PR1\n  parameter Integer ResetMap[L] = {1, 4, 3, 2};\n"
                           "  discrete L reset(start = L.U, fixed = true);\n  Integer flag;\n  Real w[L];\n  Real v;\n"
                           "algorithm\n  when time > 0.5 then\n    reset := L.Z;\n  end when;\n  flag := ResetMap[reset];\n"
                           "equation\n  w = {time, 2 * time, 3 * time, 4 * time};\n  v = w[reset] + w[pre(reset)];\nend PR1;\n"),
               std::string("class PR1\n  parameter Integer ResetMap[L.U] = 1;\n  parameter Integer ResetMap[L.X] = 4;\n"
                           "  parameter Integer ResetMap[L.Z] = 3;\n  parameter Integer ResetMap[L.O] = 2;\n"
                           "  discrete enumeration(U, X, Z, O) reset(start = L.U, fixed = true);\n  Integer flag;\n"
                           "  Real w[L.U];\n  Real w[L.X];\n  Real w[L.Z];\n  Real w[L.O];\n  Real v;\nequation\n"
                           "  w[L.U] = time;\n  w[L.X] = 2.0 * time;\n  w[L.Z] = 3.0 * time;\n  w[L.O] = 4.0 * time;\n"
                           "  v = w[reset] + w[pre(reset)];\nalgorithm\n  when time > 0.5 then\n    reset := L.Z;\n  end when;\n"
                           "  flag := ResetMap[reset];\nend PR1;\n"));
  YMC_CHECK_EQ(flatten_src("package P\n  package Interfaces\n    type Logic = enumeration(U, X, Z);\n"
                           "    connector DigitalSignal = Logic;\n    connector DigitalInput = input DigitalSignal;\n"
                           "    connector DigitalOutput = output DigitalSignal;\n    partial block MISO\n      import D = P;\n"
                           "      parameter Integer n = 2;\n      D.Interfaces.DigitalInput x[n];\n"
                           "      D.Interfaces.DigitalOutput y;\n    end MISO;\n  end Interfaces;\n  block And\n"
                           "    extends P.Interfaces.MISO;\n  equation\n    y = x[1];\n  end And;\nend P;\nmodel CS4\n"
                           "  import D = P;\n  extends D.Interfaces.MISO;\n  D.And G1(final n = n);\nequation\n"
                           "  connect(x, G1.x);\n  connect(G1.y, y);\nend CS4;\n"),
               std::string("class CS4\n  final parameter Integer n = 2;\n  input enumeration(U, X, Z) x[1];\n"
                           "  input enumeration(U, X, Z) x[2];\n  output enumeration(U, X, Z) y;\n"
                           "  final parameter Integer G1.n = 2;\n  enumeration(U, X, Z) G1.x[1];\n"
                           "  enumeration(U, X, Z) G1.x[2];\n  enumeration(U, X, Z) G1.y;\nequation\n  x[1] = G1.x[1];\n"
                           "  x[2] = G1.x[2];\n  G1.y = y;\n  G1.y = G1.x[1];\nend CS4;\n"));
  YMC_CHECK_EQ(flatten_src("type L = enumeration(U, X, Z, O);\nmodel ER1\n  constant L LogicValues[:] = L.U:L.O;\n"
                           "  constant L Sub[:] = L.X:L.Z;\n  discrete L x(start = L.U, fixed = true);\n  Real xr;\n  L xd;\n"
                           "equation\n  when time > 0.5 then\n    x = L.Z;\n  end when;\n  xr = Integer(pre(x));\n"
                           "  xd = LogicValues[integer(delay(xr, 0.1))];\nend ER1;\n"),
               std::string("class ER1\n  constant enumeration(U, X, Z, O) LogicValues[1] = L.U;\n"
                           "  constant enumeration(U, X, Z, O) LogicValues[2] = L.X;\n"
                           "  constant enumeration(U, X, Z, O) LogicValues[3] = L.Z;\n"
                           "  constant enumeration(U, X, Z, O) LogicValues[4] = L.O;\n"
                           "  constant enumeration(U, X, Z, O) Sub[1] = L.X;\n  constant enumeration(U, X, Z, O) Sub[2] = L.Z;\n"
                           "  discrete enumeration(U, X, Z, O) x(start = L.U, fixed = true);\n  Real xr;\n"
                           "  enumeration(U, X, Z, O) xd;\nequation\n  when time > 0.5 then\n    x = L.Z;\n  end when;\n"
                           "  xr = /*Real*/(Integer(pre(x)));\n  xd = {L.U, L.X, L.Z, L.O}[integer(delay(xr, 0.1, 0.1))];\n"
                           "end ER1;\n"));
  YMC_CHECK_EQ(flatten_src("type L = enumeration(U, X, Z, O);\nmodel EF1\n  parameter Integer n = 3;\n"
                           "  L s[n](start = fill(L.U, n));\n  discrete L d[2](start = {L.X, L.Z}, each fixed = true);\n"
                           "equation\n  s = fill(L.O, n);\n  when time > 0.5 then\n    d = {L.U, L.U};\n  end when;\nend EF1;\n"),
               std::string("class EF1\n  final parameter Integer n = 3;\n  enumeration(U, X, Z, O) s[1](start = L.U);\n"
                           "  enumeration(U, X, Z, O) s[2](start = L.U);\n  enumeration(U, X, Z, O) s[3](start = L.U);\n"
                           "  discrete enumeration(U, X, Z, O) d[1](start = L.X, fixed = true);\n"
                           "  discrete enumeration(U, X, Z, O) d[2](start = L.Z, fixed = true);\nequation\n  s[1] = L.O;\n"
                           "  s[2] = L.O;\n  s[3] = L.O;\n  when time > 0.5 then\n    d[1] = L.U;\n    d[2] = L.U;\n"
                           "  end when;\nend EF1;\n"));
}

YMC_TEST(flatten_clock_variables_connectors_and_evaluated_solver_bindings) {
  // Clock 类型的变量与短类连接器（`connector ClockInput = input Clock`）；Clock 两实参构造的第二实参求值后存回被引用的参数
  //（沿引用链都打字面量、不 final）；没选中的 if 分支里同样（定型在选支之前），同步算子计数实参照样标结构参数。期望值为
  // omc 1.25.0 对原文实跑的 stdout（CK 探针 ut，2026-10-08）。
  YMC_CHECK_EQ(flatten_src("connector ClockInput = input Clock;\nconnector ClockOutput = output Clock;\nblock PeriodicClock\n"
                           "  ClockOutput y;\nequation\n  y = Clock(0.1);\nend PeriodicClock;\nblock SampleB\n  input Real u;\n"
                           "  output Real y;\n  ClockInput clock;\nequation\n  y = sample(u, clock);\nend SampleB;\nmodel CK1\n"
                           "  PeriodicClock pc;\n  SampleB s(u = time);\nequation\n  connect(pc.y, s.clock);\nend CK1;\n"),
               std::string("class CK1\n  Clock pc.y;\n  Real s.u = time;\n  Real s.y;\n  Clock s.clock;\nequation\n"
                           "  pc.y = s.clock;\n  pc.y = Clock(0.1);\n  s.y = sample(s.u, s.clock);\nend CK1;\n"));
  YMC_CHECK_EQ(flatten_src("model CK5\n  parameter String m = \"ImplicitEuler\";\n  parameter String m2 = m;\n"
                           "  parameter String m3 = m2;\n  Clock c = Clock(Clock(0.1), solverMethod = m3);\nend CK5;\n"),
               std::string("class CK5\n  parameter String m = \"ImplicitEuler\";\n  parameter String m2 = \"ImplicitEuler\";\n"
                           "  parameter String m3 = \"ImplicitEuler\";\n  Clock c = Clock(Clock(0.1), \"ImplicitEuler\");\n"
                           "end CK5;\n"));
  YMC_CHECK_EQ(flatten_src("block EC\n  parameter Boolean useSolver = false;\n"
                           "  parameter String solverMethod = \"ExplicitEuler\";\n  input Boolean u;\n  output Clock y;\n"
                           "equation\n  if useSolver then\n    y = Clock(Clock(u), solverMethod = solverMethod);\n  else\n"
                           "    y = Clock(u);\n  end if;\nend EC;\nmodel CK6\n  parameter String sm = \"ExplicitEuler\";\n"
                           "  EC ec(solverMethod = sm, u = time > 0.5);\nend CK6;\n"),
               std::string("class CK6\n  parameter String sm = \"ExplicitEuler\";\n"
                           "  final parameter Boolean ec.useSolver = false;\n"
                           "  parameter String ec.solverMethod = \"ExplicitEuler\";\n  Boolean ec.u = time > 0.5;\n"
                           "  Clock ec.y;\nequation\n  ec.y = Clock(ec.u, 0.0);\nend CK6;\n"));
  YMC_CHECK_EQ(flatten_src("block SC\n  parameter Boolean sub = false;\n  parameter Integer f = 2;\n  input Clock u;\n"
                           "  output Clock y;\nequation\n  if sub then\n    y = subSample(u, f);\n  else\n    y = u;\n"
                           "  end if;\nend SC;\nmodel CK8\n  SC sc(u = Clock(0.1));\nend CK8;\n"),
               std::string("class CK8\n  final parameter Boolean sc.sub = false;\n  final parameter Integer sc.f = 2;\n"
                           "  Clock sc.u = Clock(0.1);\n  Clock sc.y;\nequation\n  sc.y = sc.u;\nend CK8;\n"));
}

YMC_TEST(flatten_split_if_bindings_and_matrix_comprehensions) {
  // 参数数组的 if 绑定逐元素摊开后不再按 simplifyIf 收拢（applySubscriptIf 取元素不化简：`if b then 1.0 else 1.0`）；逐元素摊开
  // 的绑定里矩阵的推导式列照样展开；外层 if 的条件参数声明在后（向前引用）时第二遍再判固定。期望值为 omc 1.25.0 对原文实跑
  // 的 stdout（CT 探针 ut，2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model CT2\n  parameter Boolean b = true;\n  parameter Real p[2] = if b then {1, 2} else {1, 3};\n"
                           "  parameter Boolean q[2] = if b then {true, false} else {false, false};\n"
                           "  Real x[2] = if b then {time, 2} else {time, 3};\nend CT2;\n"),
               std::string("class CT2\n  parameter Boolean b = true;\n  parameter Real p[1] = if b then 1.0 else 1.0;\n"
                           "  parameter Real p[2] = if b then 2.0 else 3.0;\n"
                           "  parameter Boolean q[1] = if b then true else false;\n"
                           "  parameter Boolean q[2] = if b then false else false;\n  Real x[1];\n  Real x[2];\nequation\n"
                           "  x = if b then {time, 2.0} else {time, 3.0};\nend CT2;\n"));
  YMC_CHECK_EQ(flatten_src("model Tab\n  parameter Real table[:, :] = fill(0.0, 0, 2);\n  Real y = table[1, 2] * time;\n"
                           "end Tab;\nblock BT\n  parameter Real table[:] = {0, 1};\n  parameter Boolean startValue = false;\n"
                           "  Tab combiTimeTable(final table = if n > 0 then if startValue then [table[1], 1.0; table, {mod(i + 1, 2.0) for i in 1:n}] else [table[1], 0.0; table, {mod(i, 2.0) for i in 1:n}] else if startValue then [0.0, 1.0] else [0.0, 0.0]);\n"
                           "protected\n  final parameter Integer n = size(table, 1);\nend BT;\nmodel CT3\n"
                           "  BT bt(table = {0, 1, 3});\nend CT3;\n"),
               std::string("class CT3\n  parameter Real bt.table[1] = 0.0;\n  parameter Real bt.table[2] = 1.0;\n"
                           "  parameter Real bt.table[3] = 3.0;\n  parameter Boolean bt.startValue = false;\n"
                           "  final parameter Real bt.combiTimeTable.table[1,1] = if bt.startValue then bt.table[1] else bt.table[1];\n"
                           "  final parameter Real bt.combiTimeTable.table[1,2] = if bt.startValue then 1.0 else 0.0;\n"
                           "  final parameter Real bt.combiTimeTable.table[2,1] = if bt.startValue then bt.table[1] else bt.table[1];\n"
                           "  final parameter Real bt.combiTimeTable.table[2,2] = if bt.startValue then 0.0 else 1.0;\n"
                           "  final parameter Real bt.combiTimeTable.table[3,1] = if bt.startValue then bt.table[2] else bt.table[2];\n"
                           "  final parameter Real bt.combiTimeTable.table[3,2] = if bt.startValue then 1.0 else 0.0;\n"
                           "  final parameter Real bt.combiTimeTable.table[4,1] = if bt.startValue then bt.table[3] else bt.table[3];\n"
                           "  final parameter Real bt.combiTimeTable.table[4,2] = if bt.startValue then 0.0 else 1.0;\n"
                           "  Real bt.combiTimeTable.y = bt.combiTimeTable.table[1,2] * time;\n"
                           "  protected final parameter Integer bt.n = 3;\nend CT3;\n"));
}

YMC_TEST(flatten_overdetermined_connection_graph_and_function_tuple_assign) {
  // 超定连接图：root / branch 从方程里摘掉，环里压平序最先的连接断开、改出 `<根类>.<lhs 路径>.equalityConstraint(…) = {0.0}`
  //（紧接连接方程），断开连接的超定叶不进连接集；没有根时 isRoot 假、rooted 真。函数体里的元组赋值 `(a, , c) := g(x)`。
  // 期望值为 omc 1.25.0 对原文实跑的 stdout（OC 探针 x / q、FT 探针 t1，2026-10-08）。
  YMC_CHECK_EQ(flatten_src("// Probe (x): overdetermined record connectors declared directly in the root model (no sub-components).\n"
                           "package OCa\n  record Orientation\n    Real T[2];\n    Real w;\n    function equalityConstraint\n"
                           "      input Orientation R1;\n      input Orientation R2;\n      output Real residue[1];\n"
                           "    algorithm\n      residue := {R1.T[1]*R2.T[2] - R1.T[2]*R2.T[1]};\n    end equalityConstraint;\n"
                           "  end Orientation;\n\n  connector Frame\n    Real r;\n    Orientation R;\n    flow Real f;\n"
                           "  end Frame;\n\n  model World\n    Frame frame_b;\n  equation\n    Connections.root(frame_b.R);\n"
                           "    frame_b.r = 0;\n    frame_b.R.T = {1, 0};\n    frame_b.R.w = 0;\n  end World;\n\n  model Joint\n"
                           "    Frame frame_a;\n    Frame frame_b;\n  equation\n    Connections.branch(frame_a.R, frame_b.R);\n"
                           "    frame_b.r = frame_a.r + 1;\n    frame_b.R.T = frame_a.R.T;\n    frame_b.R.w = frame_a.R.w;\n"
                           "    frame_a.f + frame_b.f = 0;\n  end Joint;\n\n  model Body\n    Frame frame_a;\n  equation\n"
                           "    frame_a.f = 1;\n  end Body;\nend OCa;\n\nmodel ProbeX\n  OCa.Frame a, b;\nequation\n"
                           "  Connections.root(a.R);\n  Connections.branch(a.R, b.R);\n  connect(a, b);\nend ProbeX;\n"),
               std::string("function OCa.Orientation \"Automatically generated record constructor for OCa.Orientation\"\n"
                           "  input Real[2] T;\n  input Real w;\n  output Orientation res;\nend OCa.Orientation;\n\n"
                           "function ProbeX.a.R.equalityConstraint\n  input OCa.Orientation R1;\n  input OCa.Orientation R2;\n"
                           "  output Real[1] residue;\nalgorithm\n  residue := {R1.T[1] * R2.T[2] - R1.T[2] * R2.T[1]};\n"
                           "end ProbeX.a.R.equalityConstraint;\n\nclass ProbeX\n  Real a.r;\n  Real a.R.T[1];\n"
                           "  Real a.R.T[2];\n  Real a.R.w;\n  Real a.f;\n  Real b.r;\n  Real b.R.T[1];\n  Real b.R.T[2];\n"
                           "  Real b.R.w;\n  Real b.f;\nequation\n  a.r = b.r;\n  -(a.f + b.f) = 0.0;\n  a.f = 0.0;\n"
                           "  b.f = 0.0;\n  ProbeX.a.R.equalityConstraint(a.R, b.R) = {0.0};\nend ProbeX;\n"));
  YMC_CHECK_EQ(flatten_src("// Probe (q): graph without any (definite or potential) root: isRoot=false, rooted=true (no distances), loop still broken.\n"
                           "package OCa\n  record Orientation\n    Real T[2];\n    Real w;\n    function equalityConstraint\n"
                           "      input Orientation R1;\n      input Orientation R2;\n      output Real residue[1];\n"
                           "    algorithm\n      residue := {R1.T[1]*R2.T[2] - R1.T[2]*R2.T[1]};\n    end equalityConstraint;\n"
                           "  end Orientation;\n\n  connector Frame\n    Real r;\n    Orientation R;\n    flow Real f;\n"
                           "  end Frame;\n\n  model World\n    Frame frame_b;\n  equation\n    Connections.root(frame_b.R);\n"
                           "    frame_b.r = 0;\n    frame_b.R.T = {1, 0};\n    frame_b.R.w = 0;\n  end World;\n\n  model Joint\n"
                           "    Frame frame_a;\n    Frame frame_b;\n  equation\n    Connections.branch(frame_a.R, frame_b.R);\n"
                           "    frame_b.r = frame_a.r + 1;\n    frame_b.R.T = frame_a.R.T;\n    frame_b.R.w = frame_a.R.w;\n"
                           "    frame_a.f + frame_b.f = 0;\n  end Joint;\n\n  model Body\n    Frame frame_a;\n  equation\n"
                           "    frame_a.f = 1;\n  end Body;\n\n  model RJ\n    Frame frame_a;\n    Frame frame_b;\n"
                           "    Boolean ra;\n    Boolean rb;\n    Boolean ia;\n  equation\n"
                           "    Connections.branch(frame_a.R, frame_b.R);\n    ra = Connections.rooted(frame_a.R);\n"
                           "    rb = Connections.rooted(frame_b.R);\n    ia = Connections.isRoot(frame_a.R);\n"
                           "    frame_a.f + frame_b.f = 0;\n  end RJ;\nend OCa;\n\nmodel ProbeQ\n  OCa.RJ j1;\n  OCa.RJ j2;\n"
                           "equation\n  connect(j1.frame_b, j2.frame_a);\n  connect(j2.frame_b, j1.frame_a);\nend ProbeQ;\n"),
               std::string("function OCa.Orientation \"Automatically generated record constructor for OCa.Orientation\"\n"
                           "  input Real[2] T;\n  input Real w;\n  output Orientation res;\nend OCa.Orientation;\n\n"
                           "function ProbeQ.j1.frame_b.R.equalityConstraint\n  input OCa.Orientation R1;\n"
                           "  input OCa.Orientation R2;\n  output Real[1] residue;\nalgorithm\n"
                           "  residue := {R1.T[1] * R2.T[2] - R1.T[2] * R2.T[1]};\nend ProbeQ.j1.frame_b.R.equalityConstraint;\n"
                           "\nclass ProbeQ\n  Real j1.frame_a.r;\n  Real j1.frame_a.R.T[1];\n  Real j1.frame_a.R.T[2];\n"
                           "  Real j1.frame_a.R.w;\n  Real j1.frame_a.f;\n  Real j1.frame_b.r;\n  Real j1.frame_b.R.T[1];\n"
                           "  Real j1.frame_b.R.T[2];\n  Real j1.frame_b.R.w;\n  Real j1.frame_b.f;\n  Boolean j1.ra;\n"
                           "  Boolean j1.rb;\n  Boolean j1.ia;\n  Real j2.frame_a.r;\n  Real j2.frame_a.R.T[1];\n"
                           "  Real j2.frame_a.R.T[2];\n  Real j2.frame_a.R.w;\n  Real j2.frame_a.f;\n  Real j2.frame_b.r;\n"
                           "  Real j2.frame_b.R.T[1];\n  Real j2.frame_b.R.T[2];\n  Real j2.frame_b.R.w;\n  Real j2.frame_b.f;\n"
                           "  Boolean j2.ra;\n  Boolean j2.rb;\n  Boolean j2.ia;\nequation\n  j1.frame_b.r = j2.frame_a.r;\n"
                           "  j2.frame_b.R.T[1] = j1.frame_a.R.T[1];\n  j2.frame_b.R.T[2] = j1.frame_a.R.T[2];\n"
                           "  j2.frame_b.R.w = j1.frame_a.R.w;\n  j2.frame_b.r = j1.frame_a.r;\n"
                           "  j2.frame_a.f + j1.frame_b.f = 0.0;\n  j2.frame_b.f + j1.frame_a.f = 0.0;\n"
                           "  ProbeQ.j1.frame_b.R.equalityConstraint(j1.frame_b.R, j2.frame_a.R) = {0.0};\n  j1.ra = true;\n"
                           "  j1.rb = true;\n  j1.ia = false;\n  j1.frame_a.f + j1.frame_b.f = 0.0;\n  j2.ra = true;\n"
                           "  j2.rb = true;\n  j2.ia = false;\n  j2.frame_a.f + j2.frame_b.f = 0.0;\nend ProbeQ;\n"));
  YMC_CHECK_EQ(flatten_src("function g\n  input Real x;\n  output Real a;\n  output Real b;\n  output Integer c;\nalgorithm\n"
                           "  a := x;\n  b := 2 * x;\n  c := 3;\nend g;\nfunction f\n  input Real x;\n  output Real y;\n"
                           "protected\n  Real a, b;\n  Real v[2];\n  Integer c;\nalgorithm\n  (a, b) := g(x);\n"
                           "  (, b, c) := g(2 * x);\n  (v[1], , c) := g(x);\n  y := a + b + v[1] + c;\nend f;\nmodel FT1\n"
                           "  Real y = f(time);\nend FT1;\n"),
               std::string("function f\n  input Real x;\n  output Real y;\n  protected Real a;\n  protected Real b;\n"
                           "  protected Real[2] v;\n  protected Integer c;\nalgorithm\n  (a, b) := g(x);\n"
                           "  (_, b, c) := g(2.0 * x);\n  (v[1], _, c) := g(x);\n  y := a + b + v[1] + /*Real*/(c);\nend f;\n\n"
                           "function g\n  input Real x;\n  output Real a;\n  output Real b;\n  output Integer c;\nalgorithm\n"
                           "  a := x;\n  b := 2.0 * x;\n  c := 3;\nend g;\n\nclass FT1\n  Real y = f(time);\nend FT1;\n"));
}

YMC_TEST(flatten_parameter_array_bound_to_call_kept_whole) {
  // 参数数组整个绑定是实参求不成字面量的用户函数调用：omc 不标量化（scalarizeVariable 的 isSubscriptedArrayCall，NFScalarize.mo
  // :89-160 v1.25.0），一行 `parameter Real[3] e(unit = "1") = nrm(n)`；带 min / max / nominal 的照逐元素 `mat(v)[1, 2]`
  //（variableHasForcedScalarAttribute :513-525）。Integer 数组引用给 Real 属性（omc 打 `/*Real*/(ni)`）未做 → SKIP。
  // 期望值为 omc 1.25.0 对原文实跑的 stdout（PA 探针 1 / 4 / 7c，2026-10-08）。
  YMC_CHECK_EQ(flatten_src("function nrm\n  input Real v[3];\n  output Real r[3];\nalgorithm\n  r := v / sqrt(v * v);\n"
                           "end nrm;\nmodel PA1\n  parameter Real n[3] = {1, 0, 0};\n"
                           "  parameter Real e[3](each final unit = \"1\") = nrm(n);\n  Real y[3] = e * time;\nend PA1;\n"),
               std::string("function nrm\n  input Real[3] v;\n  output Real[3] r;\nalgorithm\n"
                           "  r := {v[1] / sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3]), v[2] / sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3]), v[3] / sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3])};\n"
                           "end nrm;\n\nclass PA1\n  parameter Real n[1] = 1.0;\n  parameter Real n[2] = 0.0;\n"
                           "  parameter Real n[3] = 0.0;\n  parameter Real[3] e(unit = \"1\") = nrm(n);\n  Real y[1];\n"
                           "  Real y[2];\n  Real y[3];\nequation\n  y = {e[1] * time, e[2] * time, e[3] * time};\nend PA1;\n"));
  YMC_CHECK_EQ(flatten_src("function mat\n  input Real v[2];\n  output Real m[2, 2];\nalgorithm\n"
                           "  m := [v[1], v[2]; -v[2], v[1]];\nend mat;\nmodel PA4\n  parameter Real v[2] = {1, 2};\n"
                           "  parameter Real m[2, 2](each nominal = 2) = mat(v) \"nominal forces scalarization\";\n"
                           "  parameter Real w[2, 2](each unit = \"m\", each fixed = false) = mat(v) \"kept whole\";\n"
                           "  Real y = m[1, 2] * time + w[2, 1];\nend PA4;\n"),
               std::string("function mat\n  input Real[2] v;\n  output Real[2, 2] m;\nalgorithm\n"
                           "  m := {{v[1], v[2]}, {-v[2], v[1]}};\nend mat;\n\nclass PA4\n  parameter Real v[1] = 1.0;\n"
                           "  parameter Real v[2] = 2.0;\n"
                           "  parameter Real m[1,1](nominal = 2.0) = mat(v)[1, 1] \"nominal forces scalarization\";\n"
                           "  parameter Real m[1,2](nominal = 2.0) = mat(v)[1, 2] \"nominal forces scalarization\";\n"
                           "  parameter Real m[2,1](nominal = 2.0) = mat(v)[2, 1] \"nominal forces scalarization\";\n"
                           "  parameter Real m[2,2](nominal = 2.0) = mat(v)[2, 2] \"nominal forces scalarization\";\n"
                           "  parameter Real[2, 2] w(unit = \"m\", fixed = false) = mat(v) \"kept whole\";\n"
                           "  Real y = m[1,2] * time + w[2,1];\nend PA4;\n"));
  YMC_CHECK_EQ(flatten_src("function nrm\n  input Real v[3];\n  output Real r[3];\nalgorithm\n  r := v / sqrt(v * v);\n"
                           "end nrm;\nmodel PA7c\n  parameter Real n[3] = {1, 0, 0};\n  parameter Integer ni[3] = {1, 2, 3};\n"
                           "  parameter Real e[3](start = ni) = nrm(n);\n  Real y = e[2] * time;\nend PA7c;\n"),
               std::string("<unsupported:whole array attribute>"));
}

YMC_TEST(flatten_outer_product_kept_symbolic) {
  // outerProduct 早内联成 `promote(v1, 2) * transpose(promote(v2, 2))`（NFModelicaBuiltin.mo:338-347 v1.25.0），全字面量实参的
  // promote / transpose 求值成字面量矩阵，其余留调用、整个积留符号形态：函数体里、模型的等式 / Var 绑定里；Integer 引用整体转型。
  // 全零运算数只有标量积折零（NFSimplifyExp.mo:974），矩阵积不折。参数数组绑定（omc 逐元素 `e[1] * e[1]`）未做 → SKIP。
  // 期望值为 omc 1.25.0 对原文实跑的 stdout（OP 探针 1 / 4d / 7f / 4b，2026-10-08）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Real a[3];\n  input Real b[3];\n  output Real m[3, 3];\nalgorithm\n"
                           "  m := outerProduct(a, b);\nend f;\nfunction g\n  input Real a[3];\n  output Real m[3, 3];\n"
                           "algorithm\n  m := identity(3) - outerProduct(a, a);\nend g;\nmodel OP1\n"
                           "  Real x[3] = {time, 2, 3};\n  Real m[3, 3] = f(x, {1, 2, 3});\n  Real n[3, 3] = g(x);\n"
                           "  Real p[2, 2] = outerProduct({time, 1}, {2, time});\nend OP1;\n"),
               std::string("function f\n  input Real[3] a;\n  input Real[3] b;\n  output Real[3, 3] m;\nalgorithm\n"
                           "  m := promote(a, 2) * transpose(promote(b, 2));\nend f;\n\nfunction g\n  input Real[3] a;\n"
                           "  output Real[3, 3] m;\nalgorithm\n"
                           "  m := {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}} - promote(a, 2) * transpose(promote(a, 2));\n"
                           "end g;\n\nclass OP1\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real m[1,1];\n  Real m[1,2];\n"
                           "  Real m[1,3];\n  Real m[2,1];\n  Real m[2,2];\n  Real m[2,3];\n  Real m[3,1];\n  Real m[3,2];\n"
                           "  Real m[3,3];\n  Real n[1,1];\n  Real n[1,2];\n  Real n[1,3];\n  Real n[2,1];\n  Real n[2,2];\n"
                           "  Real n[2,3];\n  Real n[3,1];\n  Real n[3,2];\n  Real n[3,3];\n  Real p[1,1];\n  Real p[1,2];\n"
                           "  Real p[2,1];\n  Real p[2,2];\nequation\n  x = {time, 2.0, 3.0};\n  m = f(x, {1.0, 2.0, 3.0});\n"
                           "  n = g(x);\n  p = promote({time, 1.0}, 2) * transpose(promote({2.0, time}, 2));\nend OP1;\n"));
  YMC_CHECK_EQ(flatten_src("model OP4d\n  parameter Integer k[3] = {1, 2, 3};\n  Real T[3, 2] = outerProduct(k, {time, 1});\n"
                           "end OP4d;\n"),
               std::string("class OP4d\n  parameter Integer k[1] = 1;\n  parameter Integer k[2] = 2;\n"
                           "  parameter Integer k[3] = 3;\n  Real T[1,1];\n  Real T[1,2];\n  Real T[2,1];\n  Real T[2,2];\n"
                           "  Real T[3,1];\n  Real T[3,2];\nequation\n"
                           "  T = promote(/*Real[3]*/(k), 2) * transpose(promote({time, 1.0}, 2));\nend OP4d;\n"));
  YMC_CHECK_EQ(flatten_src("function g\n  input Real a[3];\n  output Real r[3];\nalgorithm\n  r := a * 2;\nend g;\nmodel OP7f\n"
                           "  Real x[3] = {time, 2, 3};\n  Real y;\n"
                           "  Real m[2, 3] = {{0, 0, 0}, {0, 0, 0}} * outerProduct(x, x);\nequation\n  y = {0, 0, 0} * g(x);\n"
                           "end OP7f;\n"),
               std::string("class OP7f\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y;\n  Real m[1,1];\n  Real m[1,2];\n"
                           "  Real m[1,3];\n  Real m[2,1];\n  Real m[2,2];\n  Real m[2,3];\nequation\n  x = {time, 2.0, 3.0};\n"
                           "  m = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}} * (promote(x, 2) * transpose(promote(x, 2)));\n  y = 0.0;\n"
                           "end OP7f;\n"));
  YMC_CHECK_EQ(flatten_src("model OP4b\n  parameter Real e[3] = {0, 0, 1};\n  parameter Real P[3, 3] = outerProduct(e, e);\n"
                           "end OP4b;\n"),
               std::string("<unsupported:outerProduct of non-literal>"));
}

YMC_TEST(flatten_derivative_annotation_modifiers) {
  // derivative 注解带修饰 `order = 2` / `noDerivative = k` / `zeroDerivative = k`（getDerivativeAttributes，NFFunctionDerivative.mo
  // :282-338 v1.25.0）：注解不打印，被引用的函数照样发射；名字不是输入 omc 报错（getInputIndex :340-357）→ SKIP。
  // 期望值为 omc 1.25.0 对原文实跑的 stdout（DA 探针 3 / 4，2026-10-08）。
  YMC_CHECK_EQ(flatten_src("function f_der\n  input Real x;\n  input Real k;\n  input Real der_x;\n  output Real dy;\n"
                           "algorithm\n  dy := k * der_x;\nend f_der;\nfunction f_der2\n  input Real x;\n  input Real k;\n"
                           "  input Real der_x;\n  input Real der2_x;\n  output Real d2y;\nalgorithm\n  d2y := k * der2_x;\n"
                           "end f_der2;\nfunction f\n  input Real x;\n  input Real k;\n  output Real y;\nalgorithm\n"
                           "  y := k * x;\n  annotation(derivative(order = 2) = f_der2, derivative = f_der);\nend f;\n"
                           "model DA3\n  Real y = f(time, 2);\nend DA3;\n"),
               std::string("function f\n  input Real x;\n  input Real k;\n  output Real y;\nalgorithm\n  y := k * x;\nend f;\n\n"
                           "function f_der\n  input Real x;\n  input Real k;\n  input Real der_x;\n  output Real dy;\n"
                           "algorithm\n  dy := k * der_x;\nend f_der;\n\nfunction f_der2\n  input Real x;\n  input Real k;\n"
                           "  input Real der_x;\n  input Real der2_x;\n  output Real d2y;\nalgorithm\n  d2y := k * der2_x;\n"
                           "end f_der2;\n\nclass DA3\n  Real y = f(time, 2.0);\nend DA3;\n"));
  YMC_CHECK_EQ(flatten_src("function f_der\n  input Real x;\n  input Real k;\n  input Real der_x;\n  output Real dy;\n"
                           "algorithm\n  dy := k * der_x;\nend f_der;\nfunction f_der2\n  input Real x;\n  input Real k;\n"
                           "  input Real der_x;\n  input Real der2_x;\n  output Real d2y;\nalgorithm\n  d2y := k * der2_x;\n"
                           "end f_der2;\nfunction f\n  input Real x;\n  input Real k;\n  output Real y;\nalgorithm\n"
                           "  y := k * x;\n  annotation(derivative(noDerivative = q) = f_der);\nend f;\nmodel DA4\n"
                           "  Real y = f(time, 2);\nend DA4;\n"),
               std::string("<unsupported:function: f>"));
}

YMC_TEST(flatten_class_comment_escapes_follow_class_annotation) {
  // 类注释：没有类级 annotation 时 omc 存源码原文、打印再转义一遍（`"a\nb"` → `"a\\nb"`）；有 annotation 才解转义（`\n` 成
  // 真换行照打、`\"` 照原样打）——translateCommentList，AbsynToSCode.mo:1474-1500 v1.25.0。期望 = omc 1.25.0 实跑 CC 探针
  // Wk / Wj（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model Wk \"a\\nb\"\n  Real y = time;\nend Wk;\n"),
               std::string("class Wk \"a\\\\nb\"\n  Real y = time;\nend Wk;\n"));
  YMC_CHECK_EQ(flatten_src("model Wj \"a\\nb \\\"j\\\"\"\n  Real y = time;\n  annotation(Icon);\nend Wj;\n"),
               std::string("class Wj \"a\nb \\\"j\\\"\"\n  Real y = time;\nend Wj;\n"));
}

YMC_TEST(flatten_vectorized_call_with_operation_argument) {
  // 向量化调用的实参是不含数组调用的运算式（MultiBody Parts 的 `to_unit1(r - r_shape)`）：omc 化简时先按元素展开成字面量再取
  // `[$i1]`（等式里 `array(to_unit1({…}[$i1]) for $i1 in 1:3)`），参数数组绑定逐元素摊开后 `to_unit1(r[1] - r_shape[1])`。
  // 含数组调用的（omc 打 `(g(time) - r)[$i1]`）未做 → SKIP。期望值为 omc 1.25.0 对原文实跑的 stdout（VU 探针 1 / 5，2026-10-08）。
  YMC_CHECK_EQ(flatten_src("function to_unit1 \"Change the unit of a Real number to unit=\\\"1\\\"\"\n  input Real r;\n"
                           "  output Real result(unit = \"1\");\nalgorithm\n  result := r;\n  annotation(Inline = true);\n"
                           "end to_unit1;\nmodel VU1\n  parameter Real r[3] = {1, 0, 0};\n"
                           "  parameter Real r_shape[3] = {0, 0, 0};\n"
                           "  parameter Real lengthDirection[3](each final unit = \"1\") = to_unit1(r - r_shape);\n"
                           "  Real x[3] = to_unit1(r * time - r_shape);\n  Real y[3] = to_unit1(-r);\nend VU1;\n"),
               std::string("function to_unit1 \"Change the unit of a Real number to unit=\\\"1\\\"\"\n  input Real r;\n"
                           "  output Real result(unit = \"1\");\nalgorithm\n  result := r;\nend to_unit1;\n\nclass VU1\n"
                           "  parameter Real r[1] = 1.0;\n  parameter Real r[2] = 0.0;\n  parameter Real r[3] = 0.0;\n"
                           "  parameter Real r_shape[1] = 0.0;\n  parameter Real r_shape[2] = 0.0;\n"
                           "  parameter Real r_shape[3] = 0.0;\n"
                           "  parameter Real lengthDirection[1](unit = \"1\") = to_unit1(r[1] - r_shape[1]);\n"
                           "  parameter Real lengthDirection[2](unit = \"1\") = to_unit1(r[2] - r_shape[2]);\n"
                           "  parameter Real lengthDirection[3](unit = \"1\") = to_unit1(r[3] - r_shape[3]);\n  Real x[1];\n"
                           "  Real x[2];\n  Real x[3];\n  Real y[1];\n  Real y[2];\n  Real y[3];\nequation\n"
                           "  x = array(to_unit1({r[1] * time - r_shape[1], r[2] * time - r_shape[2], r[3] * time - r_shape[3]}[$i1]) for $i1 in 1:3);\n"
                           "  y = array(to_unit1({-r[1], -r[2], -r[3]}[$i1]) for $i1 in 1:3);\nend VU1;\n"));
  YMC_CHECK_EQ(flatten_src("function g\n  input Real t;\n  output Real v[3];\nalgorithm\n  v := {t, 2 * t, 3 * t};\nend g;\n"
                           "function f\n  input Real x;\n  output Real y;\nalgorithm\n  y := 2 * x;\nend f;\nmodel VU5\n"
                           "  parameter Real r[3] = {1, 0, 0};\n  Real y[3] = f(g(time) - r);\nend VU5;\n"),
               std::string("<unsupported:function: f>"));
}

YMC_TEST(flatten_tuple_equation_array_and_empty_targets) {
  // 元组方程的目标是整个数组的引用（维与输出在调用处求出的维相等）；空数组目标打 `_`、空数组实参打 `{}`（removeEmptyTupleElements /
  // removeEmptyFunctionArguments，NFSimplifyModel.mo:447-488 v1.25.0；函数体同样，simplifyFunction :603-642）。多输出调用的每个
  // 输出都在调用处求维，维式里的参数照样标结构（TE2：只有第二个输出用到 n，k 也成 final）。带属性修饰的空数组整体不出（EA1）。
  // 期望值为 omc 1.25.0 对原文实跑的 stdout（TE 探针 1 / 2 / 3、EA 探针 1，2026-10-08）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Real x;\n  input Integer n;\n  output Real a[2];\n  output Real b[n];\n"
                           "  output Real c[n];\nalgorithm\n  a := {x, 2 * x};\n  b := fill(x, n);\n  c := fill(3 * x, n);\n"
                           "end f;\nfunction g\n  input Real a[2];\n  input Real b[:];\n  input Real c[:];\n  output Real s;\n"
                           "  output Real t[2];\nalgorithm\n  s := sum(a) + sum(b) + sum(c);\n  t := a;\nend g;\nmodel TE1\n"
                           "  parameter Integer n = 0;\n  Real a[2], b[n], c[n], s, t[2];\n  Real a2[2], b2[1], c2[1];\n"
                           "  Real u[2], v[2];\nequation\n  (a, b, c) = f(time, n);\n  (s, t) = g(a, b, c);\n"
                           "  (a2, b2, c2) = f(time, 1);\n  (u, , v) = f(time, 2);\nend TE1;\n"),
               std::string("function f\n  input Real x;\n  input Integer n;\n  output Real[2] a;\n  output Real[n] b;\n"
                           "  output Real[n] c;\nalgorithm\n  a := {x, 2.0 * x};\n  b := fill(x, n);\n  c := fill(3.0 * x, n);\n"
                           "end f;\n\nfunction g\n  input Real[2] a;\n  input Real[:] b;\n  input Real[:] c;\n  output Real s;\n"
                           "  output Real[2] t;\nalgorithm\n  s := a[1] + a[2] + sum(b) + sum(c);\n  t := a;\nend g;\n\n"
                           "class TE1\n  final parameter Integer n = 0;\n  Real a[1];\n  Real a[2];\n  Real s;\n  Real t[1];\n"
                           "  Real t[2];\n  Real a2[1];\n  Real a2[2];\n  Real b2[1];\n  Real c2[1];\n  Real u[1];\n"
                           "  Real u[2];\n  Real v[1];\n  Real v[2];\nequation\n  (a, _, _) = f(time, 0);\n"
                           "  (s, t) = g(a, {}, {});\n  (a2, b2, c2) = f(time, 1);\n  (u, _, v) = f(time, 2);\nend TE1;\n"));
  YMC_CHECK_EQ(flatten_src("function f\n  input Integer n;\n  input Real x;\n  output Real a;\n  output Real b[n];\nalgorithm\n"
                           "  a := n * x;\n  b := fill(x, n);\nend f;\nmodel TE2\n  parameter Integer k = 2;\n"
                           "  Real y = f(k, time);\nend TE2;\n"),
               std::string("function f\n  input Integer n;\n  input Real x;\n  output Real a;\n  output Real[n] b;\nalgorithm\n"
                           "  a := /*Real*/(n) * x;\n  b := fill(x, n);\nend f;\n\nclass TE2\n  final parameter Integer k = 2;\n"
                           "  Real y = f(2, time)[1];\nend TE2;\n"));
  YMC_CHECK_EQ(flatten_src("function h\n  input Real a[:];\n  input Real b[:, 2];\n  output Real r[size(a, 1)];\n"
                           "  output Real s[size(b, 1)];\n  output Real t[size(b, 1)];\nalgorithm\n  r := a;\n  s := b[:, 1];\n"
                           "  t := b[:, 2];\nend h;\nfunction k\n  input Real v[:];\n  output Real s;\nalgorithm\n"
                           "  s := size(v, 1);\nend k;\nfunction g\n  input Integer n;\n  input Real x;\n  output Real cr[n];\n"
                           "protected\n  Real den1[n];\n  Real den2[0, 2];\n  Real c0[0];\n  Real c1[0];\nalgorithm\n"
                           "  for i in 1:n loop\n    den1[i] := i * x;\n  end for;\n  (cr, c0, c1) := h(den1, den2);\nend g;\n"
                           "function g2\n  input Real x;\n  output Real y;\nprotected\n  Real e[0];\nalgorithm\n"
                           "  y := x + k(e) + k({x, 2 * x});\nend g2;\nmodel TE3\n  Real y[2] = g(2, time);\n"
                           "  Real z = g2(time);\nend TE3;\n"),
               std::string("function g\n  input Integer n;\n  input Real x;\n  output Real[n] cr;\n  protected Real[n] den1;\n"
                           "  protected Real[0, 2] den2;\n  protected Real[0] c0;\n  protected Real[0] c1;\nalgorithm\n"
                           "  for i in 1:n loop\n    den1[i] := /*Real*/(i) * x;\n  end for;\n  (cr, _, _) := h(den1, {});\n"
                           "end g;\n\nfunction g2\n  input Real x;\n  output Real y;\n  protected Real[0] e;\nalgorithm\n"
                           "  y := x + k({}) + k({x, 2.0 * x});\nend g2;\n\nfunction h\n  input Real[:] a;\n"
                           "  input Real[:, 2] b;\n  output Real[size(a, 1)] r;\n  output Real[size(b, 1)] s;\n"
                           "  output Real[size(b, 1)] t;\nalgorithm\n  r := a;\n  s := b[:,1];\n  t := b[:,2];\nend h;\n\n"
                           "function k\n  input Real[:] v;\n  output Real s;\nalgorithm\n  s := /*Real*/(size(v, 1));\nend k;\n"
                           "\nclass TE3\n  Real y[1];\n  Real y[2];\n  Real z = g2(time);\nequation\n  y = g(2, time);\n"
                           "end TE3;\n"));
  YMC_CHECK_EQ(flatten_src("model EA1\n  parameter Integer n = 0;\n  parameter Real s[n] = fill(1, n);\n"
                           "  Real x[0](each start = 1);\n  Real y[n](start = s, each fixed = true);\n"
                           "  parameter Real p[n](each min = 0) = s;\n  Real z = time;\nequation\n  der(y) = -y;\nend EA1;\n"),
               std::string("class EA1\n  final parameter Integer n = 0;\n  Real z = time;\nend EA1;\n"));
}

YMC_TEST(flatten_record_parameter_binding_branches) {
  // 参数记录的整体绑定是用户函数调用（NFFlatten.flattenComplexComponent :961-989 v1.25.0；rp62r SPEC R5-R13）：final 的先强制
  // 内联，内联不了（两条语句 BR6、调用处顶层实参是内建调用 `zeros(2)` IA2）→ tryEvalExp 成字面量、不标结构、写回照做（SE1：
  // 声明在后的 p 打求得的 `2.0`）；求不出（p fixed = false）→ 挪成初始方程、字段 final + fixed = false（BR8）；非 final 的挪成
  // 初始方程，数组字段逐元素 fixed = false、按属性序（FD4）；能内联成符号式的未做 → SKIP（BR5）。
  // 期望值为 omc 1.25.0 对原文实跑的 stdout（rp62r 探针，2026-10-08）。
  YMC_CHECK_EQ(flatten_src("package BR6P\n  record R\n    Real x \"field x\";\n    Real y \"field y\";\n  end R;\n"
                           "  function f1 \"single assignment of a record constructor -> inlinable\"\n    input Real a;\n"
                           "    output R r;\n  algorithm\n    r := R(x = a, y = 2 * a);\n  end f1;\n"
                           "  function f2 \"two statements -> not inlinable\"\n    input Real a;\n    output R r;\n  algorithm\n"
                           "    r.x := a;\n    r.y := 2 * a;\n  end f2;\n"
                           "  function f3 \"single statement but a protected local -> not inlinable\"\n    input Real a;\n"
                           "    output R r;\n  protected\n    Real t = 2 * a;\n  algorithm\n    r := R(x = a, y = t);\n"
                           "  end f3;\nend BR6P;\nmodel BR6\n  parameter Real p = 1;\n  final parameter BR6P.R r = BR6P.f2(p);\n"
                           "end BR6;\n"),
               std::string("class BR6\n  parameter Real p = 1.0;\n  final parameter Real r.x = 1.0 \"field x\";\n"
                           "  final parameter Real r.y = 2.0 \"field y\";\nend BR6;\n"));
  YMC_CHECK_EQ(flatten_src("package IA2P\n  record Orientation\n    Real T[2, 2];\n    Real w[2];\n  end Orientation;\n"
                           "  function mk\n    input Real angle;\n    input Real w0[2];\n    output Orientation R;\n"
                           "  algorithm\n    R := Orientation(T = [cos(angle), sin(angle); -sin(angle), cos(angle)], w = w0);\n"
                           "  end mk;\n  function g1\n    input Real a;\n    output Real b;\n  algorithm\n    b := 2 * a;\n"
                           "  end g1;\n  function g2\n    input Real a;\n    output Real b;\n  protected\n    Real t;\n"
                           "  algorithm\n    t := 2 * a;\n    b := t + 1;\n  end g2;\nend IA2P;\nmodel IA2\n"
                           "  parameter Real a0 = 0.1;\n  parameter Real wv[2] = {1, 2};\n"
                           "  final parameter IA2P.Orientation R = IA2P.mk(a0, zeros(2));\nend IA2;\n"),
               std::string("class IA2\n  parameter Real a0 = 0.1;\n  parameter Real wv[1] = 1.0;\n"
                           "  parameter Real wv[2] = 2.0;\n  final parameter Real R.T[1,1] = 0.9950041652780258;\n"
                           "  final parameter Real R.T[1,2] = 0.09983341664682815;\n"
                           "  final parameter Real R.T[2,1] = -0.09983341664682815;\n"
                           "  final parameter Real R.T[2,2] = 0.9950041652780258;\n  final parameter Real R.w[1] = 0.0;\n"
                           "  final parameter Real R.w[2] = 0.0;\nend IA2;\n"));
  YMC_CHECK_EQ(flatten_src("package SE1P\n  record R\n    Real x \"field x\";\n    Real y \"field y\";\n  end R;\n"
                           "  function f1 \"single assignment of a record constructor -> inlinable\"\n    input Real a;\n"
                           "    output R r;\n  algorithm\n    r := R(x = a, y = 2 * a);\n  end f1;\n"
                           "  function f2 \"two statements -> not inlinable\"\n    input Real a;\n    output R r;\n  algorithm\n"
                           "    r.x := a;\n    r.y := 2 * a;\n  end f2;\n"
                           "  function f3 \"single statement but a protected local -> not inlinable\"\n    input Real a;\n"
                           "    output R r;\n  protected\n    Real t = 2 * a;\n  algorithm\n    r := R(x = a, y = t);\n"
                           "  end f3;\nend SE1P;\nmodel SE1\n  parameter Real q = 1;\n  final parameter SE1P.R r = SE1P.f2(p);\n"
                           "  parameter Real p = 2 * q;\nend SE1;\n"),
               std::string("class SE1\n  parameter Real q = 1.0;\n  final parameter Real r.x = 2.0 \"field x\";\n"
                           "  final parameter Real r.y = 4.0 \"field y\";\n  parameter Real p = 2.0;\nend SE1;\n"));
  YMC_CHECK_EQ(flatten_src("package BR8P\n  record R\n    Real x \"field x\";\n    Real y \"field y\";\n  end R;\n"
                           "  function f1 \"single assignment of a record constructor -> inlinable\"\n    input Real a;\n"
                           "    output R r;\n  algorithm\n    r := R(x = a, y = 2 * a);\n  end f1;\n"
                           "  function f2 \"two statements -> not inlinable\"\n    input Real a;\n    output R r;\n  algorithm\n"
                           "    r.x := a;\n    r.y := 2 * a;\n  end f2;\n"
                           "  function f3 \"single statement but a protected local -> not inlinable\"\n    input Real a;\n"
                           "    output R r;\n  protected\n    Real t = 2 * a;\n  algorithm\n    r := R(x = a, y = t);\n"
                           "  end f3;\nend BR8P;\nmodel BR8\n  parameter Real p(fixed = false, start = 1);\n"
                           "  final parameter BR8P.R r = BR8P.f2(p);\ninitial equation\n  p = 1;\nend BR8;\n"),
               std::string("function BR8P.R \"Automatically generated record constructor for BR8P.R\"\n  input Real x;\n"
                           "  input Real y;\n  output R res;\nend BR8P.R;\n\n"
                           "function BR8P.f2 \"two statements -> not inlinable\"\n  input Real a;\n  output BR8P.R r;\n"
                           "algorithm\n  r.x := a;\n  r.y := 2.0 * a;\nend BR8P.f2;\n\nclass BR8\n"
                           "  parameter Real p(start = 1.0, fixed = false);\n"
                           "  final parameter Real r.x(fixed = false) \"field x\";\n"
                           "  final parameter Real r.y(fixed = false) \"field y\";\ninitial equation\n  r = BR8P.f2(p);\n"
                           "  p = 1.0;\nend BR8;\n"));
  YMC_CHECK_EQ(flatten_src("package FD4P\n"
                           "  type AngularVelocity = Real(final quantity = \"AngularVelocity\", final unit = \"rad/s\");\n"
                           "  record RM\n    Real T[2, 2] \"T comment\";\n    AngularVelocity w[2] \"w comment\";\n"
                           "    Real s(min = 0, nominal = 2) = 1 \"s comment\";\n  end RM;\n"
                           "  function h \"array-valued, not inlinable\"\n    input Real a;\n    output Real T[2, 2];\n"
                           "  protected\n    Real c;\n  algorithm\n    c := cos(a);\n    T := [c, 0; 0, c];\n  end h;\n"
                           "  function mkm \"inlinable record function\"\n    input Real a;\n    output RM m;\n  algorithm\n"
                           "    m := RM(T = [cos(a), sin(a); -sin(a), cos(a)], w = {a, 0}, s = a);\n  end mkm;\n"
                           "  function mkm2 \"not inlinable record function\"\n    input Real a;\n    output RM m;\n"
                           "  algorithm\n    m.T := [cos(a), sin(a); -sin(a), cos(a)];\n    m.w := {a, 0};\n    m.s := a;\n"
                           "  end mkm2;\nend FD4P;\nmodel FD4\n  parameter Real p = 0.5;\n"
                           "  parameter FD4P.RM r = FD4P.mkm2(p);\nend FD4;\n"),
               std::string("function FD4P.RM \"Automatically generated record constructor for FD4P.RM\"\n  input Real[2, 2] T;\n"
                           "  input Real[2] w;\n  input Real s = 1.0;\n  output RM res;\nend FD4P.RM;\n\n"
                           "function FD4P.mkm2 \"not inlinable record function\"\n  input Real a;\n  output FD4P.RM m;\n"
                           "algorithm\n  m.T := {{cos(a), sin(a)}, {-sin(a), cos(a)}};\n  m.w := {a, 0.0};\n  m.s := a;\n"
                           "end FD4P.mkm2;\n\nclass FD4\n  parameter Real p = 0.5;\n"
                           "  parameter Real r.T[1,1](fixed = false) \"T comment\";\n"
                           "  parameter Real r.T[1,2](fixed = false) \"T comment\";\n"
                           "  parameter Real r.T[2,1](fixed = false) \"T comment\";\n"
                           "  parameter Real r.T[2,2](fixed = false) \"T comment\";\n"
                           "  parameter Real r.w[1](quantity = \"AngularVelocity\", unit = \"rad/s\", fixed = false) \"w comment\";\n"
                           "  parameter Real r.w[2](quantity = \"AngularVelocity\", unit = \"rad/s\", fixed = false) \"w comment\";\n"
                           "  parameter Real r.s(min = 0.0, fixed = false, nominal = 2.0) \"s comment\";\ninitial equation\n"
                           "  r = FD4P.mkm2(p);\nend FD4;\n"));
  YMC_CHECK_EQ(flatten_src("package BR5P\n  record R\n    Real x \"field x\";\n    Real y \"field y\";\n  end R;\n"
                           "  function f1 \"single assignment of a record constructor -> inlinable\"\n    input Real a;\n"
                           "    output R r;\n  algorithm\n    r := R(x = a, y = 2 * a);\n  end f1;\n"
                           "  function f2 \"two statements -> not inlinable\"\n    input Real a;\n    output R r;\n  algorithm\n"
                           "    r.x := a;\n    r.y := 2 * a;\n  end f2;\n"
                           "  function f3 \"single statement but a protected local -> not inlinable\"\n    input Real a;\n"
                           "    output R r;\n  protected\n    Real t = 2 * a;\n  algorithm\n    r := R(x = a, y = t);\n"
                           "  end f3;\nend BR5P;\nmodel BR5\n  parameter Real p = 1;\n  final parameter BR5P.R r = BR5P.f1(p);\n"
                           "end BR5;\n"),
               // 62y 起强制内联（omc 1.25.0 实跑 = RB 探针 BR5）：字段成符号式，f1 / R 都不打。
               std::string("class BR5\n  parameter Real p = 1.0;\n  final parameter Real r.x = p \"field x\";\n"
                           "  final parameter Real r.y = 2.0 * p \"field y\";\nend BR5;\n"));
}

YMC_TEST(flatten_array_attribute_calls_and_structural_if_branch) {
  // 数组变量的属性值里的数组调用逐元素取 `f(p)[k]`（同参数数组绑定）；数组值的 fixed 整个结构求值后取元素，参数连带标结构
  //（NFInst.mo:3947 v1.25.0）；数组 if 的分支留不住形态时，条件只读已是结构参数的照值选支（代入后 simplifyIf），否则 SKIP。
  // 期望值为 omc 1.25.0 对原文实跑的 stdout（AT 探针 1 / 2、IB 探针 1 / 2，2026-10-08）。
  YMC_CHECK_EQ(flatten_src("function f\n  input Real x;\n  output Real r[3];\nalgorithm\n  r := {x, 2*x, 3*x};\nend f;\n"
                           "model AT1\n  parameter Real p = 1;\n  Real a[3](start = f(p));\n"
                           "  Real b[3](start = 2 * f(p), each fixed = true);\n  Real c[3](start = f(p) + {1, 2, 3});\n"
                           "  parameter Real d[3](start = f(p), each fixed = false);\nequation\n  der(a) = -a;\n  der(b) = -b;\n"
                           "  der(c) = -c;\nend AT1;\n"),
               std::string("function f\n  input Real x;\n  output Real[3] r;\nalgorithm\n  r := {x, 2.0 * x, 3.0 * x};\nend f;\n"
                           "\nclass AT1\n  parameter Real p = 1.0;\n  Real a[1](start = f(p)[1]);\n"
                           "  Real a[2](start = f(p)[2]);\n  Real a[3](start = f(p)[3]);\n"
                           "  Real b[1](start = 2.0 * f(p)[1], fixed = true);\n"
                           "  Real b[2](start = 2.0 * f(p)[2], fixed = true);\n"
                           "  Real b[3](start = 2.0 * f(p)[3], fixed = true);\n  Real c[1](start = f(p)[1] + 1.0);\n"
                           "  Real c[2](start = f(p)[2] + 2.0);\n  Real c[3](start = f(p)[3] + 3.0);\n"
                           "  parameter Real d[1](start = f(p)[1], fixed = false);\n"
                           "  parameter Real d[2](start = f(p)[2], fixed = false);\n"
                           "  parameter Real d[3](start = f(p)[3], fixed = false);\nequation\n  der(a[1]) = -a[1];\n"
                           "  der(a[2]) = -a[2];\n  der(a[3]) = -a[3];\n  der(b[1]) = -b[1];\n  der(b[2]) = -b[2];\n"
                           "  der(b[3]) = -b[3];\n  der(c[1]) = -c[1];\n  der(c[2]) = -c[2];\n  der(c[3]) = -c[3];\nend AT1;\n"));
  YMC_CHECK_EQ(flatten_src("model AT2\n  parameter Boolean bf = false annotation(Evaluate = true);\n"
                           "  parameter Boolean bg = true;\n  parameter Boolean bh = bg;\n"
                           "  Real a[3](start = {1, 2, 3}, fixed = fill(bf, 3));\n"
                           "  Real b[2](start = {1, 2}, fixed = {bg, not bh});\nequation\n  der(a) = -a;\n  der(b) = -b;\n"
                           "end AT2;\n"),
               std::string("class AT2\n  final parameter Boolean bf = false;\n  final parameter Boolean bg = true;\n"
                           "  final parameter Boolean bh = true;\n  Real a[1](start = 1.0, fixed = false);\n"
                           "  Real a[2](start = 2.0, fixed = false);\n  Real a[3](start = 3.0, fixed = false);\n"
                           "  Real b[1](start = 1.0, fixed = true);\n  Real b[2](start = 2.0, fixed = false);\nequation\n"
                           "  der(a[1]) = -a[1];\n  der(a[2]) = -a[2];\n  der(a[3]) = -a[3];\n  der(b[1]) = -b[1];\n"
                           "  der(b[2]) = -b[2];\nend AT2;\n"));
  YMC_CHECK_EQ(flatten_src("function g\n  input Real x;\n  output Real r[2];\nalgorithm\n  r := {x, 2 * x};\nend g;\nmodel IB1\n"
                           "  parameter Integer k = 1 annotation(Evaluate = true);\n  parameter Real p = 3;\n"
                           "  parameter Real a[2] = {1, 2};\n  parameter Real b[2] = if k == 1 then a else g(p);\n"
                           "  parameter Real c[2] = if k == 2 then a else g(p);\n  Real y = b[2] * time + c[1];\nend IB1;\n"),
               std::string("function g\n  input Real x;\n  output Real[2] r;\nalgorithm\n  r := {x, 2.0 * x};\nend g;\n\n"
                           "class IB1\n  final parameter Integer k = 1;\n  parameter Real p = 3.0;\n"
                           "  parameter Real a[1] = 1.0;\n  parameter Real a[2] = 2.0;\n  parameter Real b[1] = a[1];\n"
                           "  parameter Real b[2] = a[2];\n  parameter Real[2] c = g(p);\n  Real y = b[2] * time + c[1];\n"
                           "end IB1;\n"));
  YMC_CHECK_EQ(flatten_src("function g\n  input Real x;\n  output Real r[2];\nalgorithm\n  r := {x, 2 * x};\nend g;\nmodel IB2\n"
                           "  parameter Integer k = 1;\n  parameter Real p = 3;\n  parameter Real a[2] = {1, 2};\n"
                           "  parameter Real b[2] = if k == 1 then a else g(p);\n  Real y = b[2] * time;\nend IB2;\n"),
               std::string("<unsupported:if-expression array branch>"));
}

YMC_TEST(flatten_subinstance_structural_evaluation) {
  // 实例化期（条件组件的条件）求子实例 / 经 outer 的参数：顺绑定链求值，走到的参数一起标结构（markComponent 连带，
  // NFStructural.mo:311-326 v1.25.0；MSL Body 的 `sphere(…) if world.enableAnimation and … and sphereDiameter > 0`）。参数数组的
  // 整体绑定是调用、实参是结构参数数组的照常逐元素求值（MSL Revolute 的 `e = normalizeWithAssert(n)`）。
  // 期望值为 omc 1.25.0 对原文实跑的 stdout（CC 探针 4 / 5、RE 探针 1，2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model A\n  parameter Real q = 2;\n  parameter Real p = 2 * q;\nend A;\nmodel CC4\n  A a;\n"
                           "  Real s = time if a.p > 1;\nend CC4;\n"),
               std::string("class CC4\n  final parameter Real a.q = 2.0;\n  final parameter Real a.p = 4.0;\n  Real s = time;\n"
                           "end CC4;\n"));
  YMC_CHECK_EQ(flatten_src("model W\n  parameter Real nominalLength = 1;\n"
                           "  parameter Real defaultBodyDiameter = nominalLength / 9;\nend W;\nmodel B\n  outer W world;\n"
                           "  parameter Real sphereDiameter = world.defaultBodyDiameter;\n"
                           "  Real s = time if sphereDiameter > 0;\nend B;\nmodel CC5\n  inner W world;\n  B body;\nend CC5;\n"),
               std::string("class CC5\n  final parameter Real world.nominalLength = 1.0;\n"
                           "  final parameter Real world.defaultBodyDiameter = 0.1111111111111111;\n"
                           "  final parameter Real body.sphereDiameter = 0.1111111111111111;\n  Real body.s = time;\nend CC5;\n"));
  YMC_CHECK_EQ(flatten_src("function nrm\n  input Real v[3];\n  output Real r[3];\nalgorithm\n  r := v / sqrt(v * v);\n"
                           "end nrm;\nmodel RE1\n  parameter Real n[3] = {0, 3, 4} annotation(Evaluate = true);\n"
                           "  parameter Real m[3] = {1, 0, 0};\n  parameter Real e[3](each final unit = \"1\") = nrm(n);\n"
                           "  parameter Real f[3] = nrm(m);\n  Real y = e[3] * time + f[1];\nend RE1;\n"),
               std::string("function nrm\n  input Real[3] v;\n  output Real[3] r;\nalgorithm\n"
                           "  r := {v[1] / sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3]), v[2] / sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3]), v[3] / sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3])};\n"
                           "end nrm;\n\nclass RE1\n  final parameter Real n[1] = 0.0;\n  final parameter Real n[2] = 3.0;\n"
                           "  final parameter Real n[3] = 4.0;\n  parameter Real m[1] = 1.0;\n  parameter Real m[2] = 0.0;\n"
                           "  parameter Real m[3] = 0.0;\n  parameter Real e[1](unit = \"1\") = 0.0;\n"
                           "  parameter Real e[2](unit = \"1\") = 0.6;\n  parameter Real e[3](unit = \"1\") = 0.8;\n"
                           "  parameter Real[3] f = nrm(m);\n  Real y = e[3] * time + f[1];\nend RE1;\n"));
}

YMC_TEST(flatten_external_object_variable_and_structors) {
  // 外部对象：组件压平成按类名打印的叶变量，构造 `EO()` 换成 `EO.constructor(1)`（默认实参照补），构造 / 析构随类型发射
  //（外部函数未声明 pure → impure）；当实参传给外部函数。期望 = omc 1.25.0 实跑（2026-10-07，下同）。
  const std::string eo = "class EO\n  extends ExternalObject;\n  function constructor\n    input Integer i = 1;\n"
                         "    output EO o;\n    external \"C\" o = initEO(i);\n  end constructor;\n  function destructor\n"
                         "    input EO o;\n    external \"C\" freeEO(o);\n  end destructor;\nend EO;\n";
  YMC_CHECK_EQ(flatten_src((eo + "function f\n  input EO o;\n  output Real y;\n  external \"C\" y = useEO(o);\nend f;\n"
                                 "model XO1\n  parameter EO e = EO();\n  Real y = f(e);\nend XO1;\n").c_str()),
               std::string("impure function EO.constructor\n  input Integer i = 1;\n  output EO o;\n\n"
                           "  external \"C\" o = initEO(i);\nend EO.constructor;\n\nimpure function EO.destructor\n"
                           "  input EO o;\n\n  external \"C\" freeEO(o);\nend EO.destructor;\n\nimpure function f\n"
                           "  input EO o;\n  output Real y;\n\n  external \"C\" y = useEO(o);\nend f;\n\nclass XO1\n"
                           "  parameter EO e = EO.constructor(1);\n  Real y = f(e);\nend XO1;\n"));
  YMC_CHECK_EQ(flatten_src((eo + "connector C\n  EO eo;\nend C;\nmodel XO2\n  C c1, c2;\nequation\n  connect(c1, c2);\n"
                                 "end XO2;\n").c_str()),
               std::string("impure function EO.constructor\n  input Integer i = 1;\n  output EO o;\n\n"
                           "  external \"C\" o = initEO(i);\nend EO.constructor;\n\nimpure function EO.destructor\n"
                           "  input EO o;\n\n  external \"C\" freeEO(o);\nend EO.destructor;\n\nclass XO2\n  EO c1.eo;\n"
                           "  EO c2.eo;\nequation\n  c1.eo = c2.eo;\nend XO2;\n"));
}

YMC_TEST(flatten_external_object_invalid_element_unsupported) {
  // 外部对象类里只能有 constructor / destructor 两个函数：多一个 `g`，omc 报 "External object EO contains invalid element 'g'"。
  bool uns = false;
  const std::string out = flatten_src("class EO\n  extends ExternalObject;\n  function constructor\n    output EO o;\n"
                                      "    external \"C\" o = initEO();\n  end constructor;\n  function destructor\n"
                                      "    input EO o;\n    external \"C\" freeEO(o);\n  end destructor;\n  function g\n"
                                      "  end g;\nend EO;\nmodel XO3\n  EO e;\nend XO3;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:external object element>"));
}

YMC_TEST(flatten_identical_inherited_elements_kept_once) {
  // 两个基类都有 x：相同（内建类型同、绑定 `1` 与 `1.0` 按数值相等）→ 只留先出现的那个，位置在它的 extends 处。期望 = omc
  // 1.25.0 实跑（2026-10-07，下同）。
  YMC_CHECK_EQ(flatten_src("model A\n  Real x = 1;\n  Real z = x;\nend A;\nmodel B\n  Real x = 1.0;\n  Real w = 2 * x;\n"
                           "end B;\nmodel DU1\n  extends A;\n  extends B;\n  Real y = x;\nend DU1;\n"),
               std::string("class DU1\n  Real x = 1.0;\n  Real z = x;\n  Real w = 2.0 * x;\n  Real y = x;\nend DU1;\n"));
}

YMC_TEST(flatten_nonidentical_inherited_elements_unsupported) {
  // 绑定不同（`x = 1` 与 `x = 2`）：omc 报 "Duplicate elements (due to inherited elements) not identical"。
  bool uns = false;
  const std::string out = flatten_src("model A\n  Real x = 1;\nend A;\nmodel B\n  Real x = 2;\nend B;\nmodel DU2\n  extends A;\n"
                                      "  extends B;\nend DU2;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:duplicate elements not identical>"));
}

YMC_TEST(flatten_subinstance_array_reference_uses_its_own_dims) {
  // 子实例里的数组 `p.s` 的维按它那个实例的修饰定（p.n = 3），不是引用处同名的参数（b.n = 1）；`u[:] = p.s` 的绑定 omc
  // 挪成整体数组方程 `b.u = b.p.s`。期望 = omc 1.25.0 实跑（2026-10-07）。
  YMC_CHECK_EQ(flatten_src("connector P\n  parameter Integer n = 1;\n  Real s[n];\nend P;\nblock B\n  parameter Integer n = 1;\n"
                           "  P p(n = 3);\n  Real u[:] = p.s;\nequation\n  p.s = {1, 2, 3};\nend B;\nmodel WA1\n  B b;\nend WA1;\n"),
               std::string("class WA1\n  parameter Integer b.n = 1;\n  final parameter Integer b.p.n = 3;\n  Real b.p.s[1];\n"
                           "  Real b.p.s[2];\n  Real b.p.s[3];\n  Real b.u[1];\n  Real b.u[2];\n  Real b.u[3];\nequation\n"
                           "  b.u = b.p.s;\n  b.p.s[1] = 1.0;\n  b.p.s[2] = 2.0;\n  b.p.s[3] = 3.0;\nend WA1;\n"));
}

YMC_TEST(flatten_parameter_start_becomes_binding) {
  // 外来修饰的 start 同样当绑定（a.p = 5.0）；当绑定后照常结构求值（维 n = 2，final）；记录字段不做（omc 只告警，r.a 无绑定）。
  // 期望 = omc 1.25.0 实跑（2026-10-07）。
  YMC_CHECK_EQ(flatten_src("model A\n  parameter Real p(start = 2);\nend A;\nrecord R\n  parameter Real a(start = 1);\nend R;\n"
                           "model US1\n  A a(p(start = 5));\n  parameter Integer n(start = 2);\n  Real x[n];\n  parameter R r;\n"
                           "end US1;\n"),
               std::string("class US1\n  parameter Real a.p(start = 5.0) = 5.0;\n  final parameter Integer n(start = 2) = 2;\n"
                           "  Real x[1];\n  Real x[2];\n  parameter Real r.a(start = 1.0);\nend US1;\n"));
}

YMC_TEST(flatten_scalar_builtin_division_by_zero_unsupported) {
  bool uns = false;
  const std::string out = flatten_src("model M\n  Integer x = div(1, 0);\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:division by zero in constant>"));
}

YMC_TEST(flatten_vectorized_structural_binding_keeps_function) {
  // Evaluate 数组参数的绑定是向量化调用 `f(a)`：结构求值逐元素算出字面量（a 连带 final），omc 压平变量的绑定一直是推导式
  //（EvalConstants 只代入里面的结构参数），函数收集在标量化之前 → f 照打（VE8）；同一个参数又被维 `x[integer(p[2])]` 在
  // 定型期求值过 → evalComponentBinding 已把字面量存回，f 不打（VE25）。期望 = omc 1.25.0 实跑（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model VE8\n  function f\n    input Real r;\n    output Real y;\n  algorithm\n    y := 2 * r;\n"
                           "  end f;\n  parameter Real a[3] = {1, 2, 3};\n"
                           "  parameter Real p[3] = f(a) annotation(Evaluate = true);\n  Real x;\nequation\n  der(x) = p[2];\n"
                           "end VE8;\n"),
               std::string("function VE8.f\n  input Real r;\n  output Real y;\nalgorithm\n  y := 2.0 * r;\nend VE8.f;\n\n"
                           "class VE8\n  final parameter Real a[1] = 1.0;\n  final parameter Real a[2] = 2.0;\n"
                           "  final parameter Real a[3] = 3.0;\n  final parameter Real p[1] = 2.0;\n"
                           "  final parameter Real p[2] = 4.0;\n  final parameter Real p[3] = 6.0;\n  Real x;\nequation\n"
                           "  der(x) = 4.0;\nend VE8;\n"));
  YMC_CHECK_EQ(flatten_src("model VE25\n  function f\n    input Real r;\n    output Real y;\n  algorithm\n    y := 2 * r;\n"
                           "  end f;\n  parameter Real a[3] = {1, 2, 3};\n"
                           "  parameter Real p[3] = f(a) annotation(Evaluate = true);\n  Real x[integer(p[2])];\nequation\n"
                           "  for i in 1:4 loop der(x[i]) = p[1]; end for;\nend VE25;\n"),
               std::string("class VE25\n  final parameter Real a[1] = 1.0;\n  final parameter Real a[2] = 2.0;\n"
                           "  final parameter Real a[3] = 3.0;\n  final parameter Real p[1] = 2.0;\n"
                           "  final parameter Real p[2] = 4.0;\n  final parameter Real p[3] = 6.0;\n  Real x[1];\n  Real x[2];\n"
                           "  Real x[3];\n  Real x[4];\nequation\n  der(x[1]) = 2.0;\n  der(x[2]) = 2.0;\n  der(x[3]) = 2.0;\n"
                           "  der(x[4]) = 2.0;\nend VE25;\n"));
}

YMC_TEST(flatten_comprehension_parameter_marking_contexts) {
  // 推导式绑定的参数数组：被别的 Evaluate 参数的绑定读到（EvalConstants 时刻）→ 照样字面量、f 照打（VE23）；只在压平期
  //（if 方程条件）才标成结构参数 → 元素绑定照旧是符号式、只加 final，引用处照样代入值（VE39）。期望 = omc 1.25.0 实跑
  //（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model VE23\n  function f\n    input Real r;\n    output Real y;\n  algorithm\n    y := 2 * r;\n"
                           "  end f;\n  parameter Real a[3] = {1, 2, 3};\n  parameter Real p[3] = f(a);\n"
                           "  parameter Real q = p[2] annotation(Evaluate = true);\n  Real x;\nequation\n  der(x) = q + p[1];\n"
                           "end VE23;\n"),
               std::string("function VE23.f\n  input Real r;\n  output Real y;\nalgorithm\n  y := 2.0 * r;\nend VE23.f;\n\n"
                           "class VE23\n  final parameter Real a[1] = 1.0;\n  final parameter Real a[2] = 2.0;\n"
                           "  final parameter Real a[3] = 3.0;\n  final parameter Real p[1] = 2.0;\n"
                           "  final parameter Real p[2] = 4.0;\n  final parameter Real p[3] = 6.0;\n"
                           "  final parameter Real q = 4.0;\n  Real x;\nequation\n  der(x) = 6.0;\nend VE23;\n"));
  YMC_CHECK_EQ(flatten_src("model VE39\n  function f\n    input Real r;\n    output Real y;\n  algorithm\n    y := 2 * r;\n"
                           "  end f;\n  parameter Real a[3] = {1, 2, 3};\n  parameter Real p[3] = {2 * a[i] for i in 1:3};\n"
                           "  Real x;\nequation\n  if p[2] > 1 then\n    der(x) = p[1];\n  else\n    der(x) = 0;\n  end if;\n"
                           "end VE39;\n"),
               std::string("class VE39\n  parameter Real a[1] = 1.0;\n  parameter Real a[2] = 2.0;\n"
                           "  parameter Real a[3] = 3.0;\n  final parameter Real p[1] = 2.0 * a[1];\n"
                           "  final parameter Real p[2] = 2.0 * a[2];\n  final parameter Real p[3] = 2.0 * a[3];\n  Real x;\n"
                           "equation\n  der(x) = 2.0;\nend VE39;\n"));
}

YMC_TEST(flatten_alias_default_forward_reference) {
  // 函数别名修饰给的缺省 `acc = std(g = g * n, mu = mu)` 引用的 world 声明在调用者之后：第一遍照原式发射、第二遍用第一遍
  // 的值求缺省（MSL 示例的 `inner World world` 多在最后）。期望 = omc 1.25.0 实跑（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model AD1\n  function std\n    input Real r[3];\n    input Real g[3];\n    input Real mu;\n"
                           "    output Real a[3];\n  algorithm\n    a := g + mu * r;\n  end std;\n  model W\n"
                           "    parameter Real g = 9.81;\n    parameter Real n[3] = {0, -1, 0};\n    parameter Real mu = 1;\n"
                           "    function acc = std(g = g * n, mu = mu);\n  end W;\n  model B\n    outer W world;\n"
                           "    Real r[3];\n    Real a[3];\n  equation\n    a = world.acc(r);\n    der(r) = a;\n  end B;\n"
                           "  B b;\n  inner W world;\nend AD1;\n"),
               std::string("function AD1.world.acc\n  input Real[3] r;\n  input Real[3] g = {0.0, -9.81, 0.0};\n"
                           "  input Real mu = 1.0;\n  output Real[3] a;\nalgorithm\n"
                           "  a := {g[1] + mu * r[1], g[2] + mu * r[2], g[3] + mu * r[3]};\nend AD1.world.acc;\n\nclass AD1\n"
                           "  Real b.r[1];\n  Real b.r[2];\n  Real b.r[3];\n  Real b.a[1];\n  Real b.a[2];\n  Real b.a[3];\n"
                           "  parameter Real world.g = 9.81;\n  parameter Real world.n[1] = 0.0;\n"
                           "  parameter Real world.n[2] = -1.0;\n  parameter Real world.n[3] = 0.0;\n"
                           "  parameter Real world.mu = 1.0;\nequation\n"
                           "  b.a = AD1.world.acc(b.r, {world.g * world.n[1], world.g * world.n[2], world.g * world.n[3]}, world.mu);\n"
                           "  der(b.r[1]) = b.a[1];\n  der(b.r[2]) = b.a[2];\n  der(b.r[3]) = b.a[3];\nend AD1;\n"));
}

YMC_TEST(flatten_whole_array_var_lifts_type_attributes) {
  // 不标量化的数组参数（绑定是整个用户函数调用）：类型链派生、不带 each 的属性按元素抬成同形数组，组件上带 each 的打一个值。
  // 期望 = omc 1.25.0 实跑（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model LT2\n"
                           "  type Angle = Real(final quantity = \"Angle\", final unit = \"rad\", displayUnit = \"deg\");\n"
                           "  function f\n    input Real x;\n    output Real y[2, 2];\n  algorithm\n"
                           "    y := {{x, 2 * x}, {3 * x, 4 * x}};\n  end f;\n  parameter Real p = 1;\n"
                           "  parameter Angle phi[2, 2](each displayUnit = \"rad\") = f(p);\n  Real z;\nequation\n"
                           "  der(z) = phi[2, 1];\nend LT2;\n"),
               std::string("function LT2.f\n  input Real x;\n  output Real[2, 2] y;\nalgorithm\n"
                           "  y := {{x, 2.0 * x}, {3.0 * x, 4.0 * x}};\nend LT2.f;\n\nclass LT2\n  parameter Real p = 1.0;\n"
                           "  parameter Real[2, 2] phi(quantity = {{\"Angle\", \"Angle\"}, {\"Angle\", \"Angle\"}}, unit = {{\"rad\", \"rad\"}, {\"rad\", \"rad\"}}, displayUnit = \"rad\") = LT2.f(p);\n"
                           "  Real z;\nequation\n  der(z) = phi[2,1];\nend LT2;\n"));
}

YMC_TEST(flatten_diagonal_kept_as_call) {
  // omc 的化简不展开 diagonal（实参全是字面量的才求值）：整个数组的等式里留调用；参数数组摊分时逐元素取（对角取实参元素、
  // 其余 0.0）。期望 = omc 1.25.0 实跑（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model DG6\n  function f\n    input Real m[3, 3];\n    output Real y;\n  algorithm\n"
                           "    y := m[1, 1] + 2 * m[2, 2] + 3 * m[3, 3];\n  end f;\n  function g\n    input Real m[3, 3];\n"
                           "    input Real k;\n    output Real y[3, 3];\n  algorithm\n    y := k * m;\n  end g;\n"
                           "  parameter Real a[3] = {1, 2, 3};\n  Real z[3, 3] = diagonal(a) * time;\nend DG6;\n"),
               std::string("class DG6\n  parameter Real a[1] = 1.0;\n  parameter Real a[2] = 2.0;\n"
                           "  parameter Real a[3] = 3.0;\n  Real z[1,1];\n  Real z[1,2];\n  Real z[1,3];\n  Real z[2,1];\n"
                           "  Real z[2,2];\n  Real z[2,3];\n  Real z[3,1];\n  Real z[3,2];\n  Real z[3,3];\nequation\n"
                           "  z = diagonal(a) * time;\nend DG6;\n"));
  YMC_CHECK_EQ(flatten_src("model DG9\n  function f\n    input Real m[3, 3];\n    output Real y;\n  algorithm\n"
                           "    y := m[1, 1] + 2 * m[2, 2] + 3 * m[3, 3];\n  end f;\n  parameter Real a[3] = {1, 2, 3};\n"
                           "  parameter Real d[3, 3] = diagonal(a);\nend DG9;\n"),
               std::string("class DG9\n  parameter Real a[1] = 1.0;\n  parameter Real a[2] = 2.0;\n"
                           "  parameter Real a[3] = 3.0;\n  parameter Real d[1,1] = a[1];\n  parameter Real d[1,2] = 0.0;\n"
                           "  parameter Real d[1,3] = 0.0;\n  parameter Real d[2,1] = 0.0;\n  parameter Real d[2,2] = a[2];\n"
                           "  parameter Real d[2,3] = 0.0;\n  parameter Real d[3,1] = 0.0;\n  parameter Real d[3,2] = 0.0;\n"
                           "  parameter Real d[3,3] = a[3];\nend DG9;\n"));
}

YMC_TEST(flatten_zero_array_operand_dropped_after_substitution) {
  // `ta + rot(R, tb) - cross(r, f)`，r 经 Evaluate 连带成结构参数、代入后 cross 成了全零：压平末尾再化简照 simplifyBinarySub
  // 消掉（MSL FixedRotation 的力矩方程）。期望 = omc 1.25.0 实跑（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model ZS2\n  function rot\n    input Real R[3, 3];\n    input Real v[3];\n    output Real y[3];\n"
                           "  algorithm\n    y := R * v;\n  end rot;\n  function to_unit1\n    input Real r;\n"
                           "    output Real result;\n  algorithm\n    result := r;\n  end to_unit1;\n"
                           "  parameter Real r[3] = {0, 0, 0};\n  parameter Real ld[3] = r annotation(Evaluate = true);\n"
                           "  parameter Real R[3, 3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};\n  Real ta[3](each start = 0);\n"
                           "  Real tb[3];\n  Real f[3];\nequation\n  zeros(3) = ta + rot(R, tb) - cross(r, f);\n"
                           "  tb = {time, 0, 0};\n  f = {0, time, 0};\nend ZS2;\n"),
               std::string("function ZS2.rot\n  input Real[3, 3] R;\n  input Real[3] v;\n  output Real[3] y;\nalgorithm\n"
                           "  y := {R[1,1] * v[1] + R[1,2] * v[2] + R[1,3] * v[3], R[2,1] * v[1] + R[2,2] * v[2] + R[2,3] * v[3], R[3,1] * v[1] + R[3,2] * v[2] + R[3,3] * v[3]};\n"
                           "end ZS2.rot;\n\nclass ZS2\n  final parameter Real r[1] = 0.0;\n  final parameter Real r[2] = 0.0;\n"
                           "  final parameter Real r[3] = 0.0;\n  final parameter Real ld[1] = 0.0;\n"
                           "  final parameter Real ld[2] = 0.0;\n  final parameter Real ld[3] = 0.0;\n"
                           "  parameter Real R[1,1] = 1.0;\n  parameter Real R[1,2] = 0.0;\n  parameter Real R[1,3] = 0.0;\n"
                           "  parameter Real R[2,1] = 0.0;\n  parameter Real R[2,2] = 1.0;\n  parameter Real R[2,3] = 0.0;\n"
                           "  parameter Real R[3,1] = 0.0;\n  parameter Real R[3,2] = 0.0;\n  parameter Real R[3,3] = 1.0;\n"
                           "  Real ta[1](start = 0.0);\n  Real ta[2](start = 0.0);\n  Real ta[3](start = 0.0);\n  Real tb[1];\n"
                           "  Real tb[2];\n  Real tb[3];\n  Real f[1];\n  Real f[2];\n  Real f[3];\nequation\n"
                           "  {0.0, 0.0, 0.0} = ta + ZS2.rot(R, tb);\n  tb[1] = time;\n  tb[2] = 0.0;\n  tb[3] = 0.0;\n"
                           "  f[1] = 0.0;\n  f[2] = time;\n  f[3] = 0.0;\nend ZS2;\n"));
}

YMC_TEST(flatten_comprehension_call_folded_on_equal_elements) {
  // Evaluate 数组参数 `p = f(a)`、a = {0, 0, 0}：omc 代入后推导式体里的 `{0.0, 0.0, 0.0}[$i1]` 化成 0.0、f(0.0) 当场求值，
  // f 不再被引用（不打）。期望 = omc 1.25.0 实跑（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model VK3\n  function f\n    input Real r;\n    output Real y;\n  algorithm\n    y := 2 * r;\n"
                           "  end f;\n  parameter Real a[3] = {0, 0, 0};\n"
                           "  parameter Real p[3] = f(a) annotation(Evaluate = true);\n  Real x;\nequation\n  der(x) = p[2];\n"
                           "end VK3;\n"),
               std::string("class VK3\n  final parameter Real a[1] = 0.0;\n  final parameter Real a[2] = 0.0;\n"
                           "  final parameter Real a[3] = 0.0;\n  final parameter Real p[1] = 0.0;\n"
                           "  final parameter Real p[2] = 0.0;\n  final parameter Real p[3] = 0.0;\n  Real x;\nequation\n"
                           "  der(x) = 0.0;\nend VK3;\n"));
}

YMC_TEST(flatten_alias_structural_array_argument_scalarizes) {
  // `Inner i(n = n)`、外层 n 带 Evaluate：i.n 经别名连带成结构参数，`e = nrm(n)` 求值成字面量、逐元素（不是整体数组变量）。
  // 期望 = omc 1.25.0 实跑（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model RA1\n  function nrm\n    input Real v[3];\n    output Real u[3];\n  algorithm\n"
                           "    u := v / sqrt(v * v);\n  end nrm;\n  model Inner\n    parameter Real n[3] = {1, 0, 0};\n"
                           "  protected\n    parameter Real e[3](each final unit = \"1\") = nrm(n);\n  public\n    Real x;\n"
                           "  equation\n    der(x) = e[2];\n  end Inner;\n  model Outer\n"
                           "    parameter Real n[3] = {0, 2, 0} annotation(Evaluate = true);\n    Inner i(n = n);\n"
                           "  end Outer;\n  Outer o;\nend RA1;\n"),
               std::string("class RA1\n  final parameter Real o.n[1] = 0.0;\n  final parameter Real o.n[2] = 2.0;\n"
                           "  final parameter Real o.n[3] = 0.0;\n  final parameter Real o.i.n[1] = 0.0;\n"
                           "  final parameter Real o.i.n[2] = 2.0;\n  final parameter Real o.i.n[3] = 0.0;\n"
                           "  protected parameter Real o.i.e[1](unit = \"1\") = 0.0;\n"
                           "  protected parameter Real o.i.e[2](unit = \"1\") = 1.0;\n"
                           "  protected parameter Real o.i.e[3](unit = \"1\") = 0.0;\n  Real o.i.x;\nequation\n"
                           "  der(o.i.x) = o.i.e[2];\nend RA1;\n"));
}

YMC_TEST(flatten_evaluate_array_call_binding_scalarizes) {
  // Evaluate 数组参数的绑定是用户函数调用、实参（`2 * a`）当时还折不成字面量：它自己已是结构参数，omc 求值成字面量、照常
  // 逐元素（不是整体数组变量）。期望 = omc 1.25.0 实跑（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model SP4\n  function nrm\n    input Real v[3];\n    output Real u[3];\n  algorithm\n"
                           "    u := v / sqrt(v * v);\n  end nrm;\n  parameter Real a[3] = {1, 1, 0};\n"
                           "  parameter Real ey[3] = nrm(2 * a) annotation(Evaluate = true);\n  Real x;\nequation\n"
                           "  der(x) = ey[1];\nend SP4;\n"),
               std::string("class SP4\n  final parameter Real a[1] = 1.0;\n  final parameter Real a[2] = 1.0;\n"
                           "  final parameter Real a[3] = 0.0;\n  final parameter Real ey[1] = 0.7071067811865475;\n"
                           "  final parameter Real ey[2] = 0.7071067811865475;\n  final parameter Real ey[3] = 0.0;\n  Real x;\n"
                           "equation\n  der(x) = 0.7071067811865475;\nend SP4;\n"));
}

YMC_TEST(flatten_condition_forward_reference_chain) {
  // 条件组件的条件经 outer 读到声明在后的 world 参数、其绑定又引用别的参数：第二遍用第一遍的值，依赖一起标结构参数。
  // 期望 = omc 1.25.0 实跑（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model W\n  parameter Real nominalLength = 1;\n"
                           "  parameter Real defaultBodyDiameter = nominalLength / 9;\nend W;\nmodel B\n  outer W world;\n"
                           "  parameter Real sphereDiameter = world.defaultBodyDiameter;\n"
                           "  Real s = time if sphereDiameter > 0;\nend B;\nmodel CF1\n  B body;\n  inner W world;\nend CF1;\n"),
               std::string("class CF1\n  final parameter Real body.sphereDiameter = 0.1111111111111111;\n  Real body.s = time;\n"
                           "  final parameter Real world.nominalLength = 1.0;\n"
                           "  final parameter Real world.defaultBodyDiameter = 0.1111111111111111;\nend CF1;\n"));
}

YMC_TEST(flatten_overridden_state_select_does_not_mark_parameter) {
  // `i1(phi.stateSelect = StateSelect.always, …)` 盖过 Inertia 里的 `phi(stateSelect = stateSelect)`：声明里那个值不用、
  // stateSelect 参数不标 final（只盖一个的 i3 仍 final）。期望 = omc 1.25.0 实跑（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model SS1\n  model Inertia\n    parameter StateSelect stateSelect = StateSelect.default;\n"
                           "    Real phi(stateSelect = stateSelect);\n    Real w(stateSelect = stateSelect);\n  equation\n"
                           "    der(phi) = w;\n    der(w) = 1;\n  end Inertia;\n"
                           "  Inertia i1(phi.stateSelect = StateSelect.always, w.stateSelect = StateSelect.always);\n"
                           "  Inertia i2;\n  Inertia i3(phi.stateSelect = StateSelect.prefer);\nend SS1;\n"),
               std::string("class SS1\n"
                           "  parameter enumeration(never, avoid, default, prefer, always) i1.stateSelect = StateSelect.default;\n"
                           "  Real i1.phi(stateSelect = StateSelect.always);\n  Real i1.w(stateSelect = StateSelect.always);\n"
                           "  final parameter enumeration(never, avoid, default, prefer, always) i2.stateSelect = StateSelect.default;\n"
                           "  Real i2.phi(stateSelect = StateSelect.default);\n  Real i2.w(stateSelect = StateSelect.default);\n"
                           "  final parameter enumeration(never, avoid, default, prefer, always) i3.stateSelect = StateSelect.default;\n"
                           "  Real i3.phi(stateSelect = StateSelect.prefer);\n  Real i3.w(stateSelect = StateSelect.default);\n"
                           "equation\n  der(i1.phi) = i1.w;\n  der(i1.w) = 1.0;\n  der(i2.phi) = i2.w;\n  der(i2.w) = 1.0;\n"
                           "  der(i3.phi) = i3.w;\n  der(i3.w) = 1.0;\nend SS1;\n"));
}

YMC_TEST(flatten_short_class_redeclares_record_fields) {
  // `record SalientResistance = Salient(redeclare SI.Resistance d(start = 2), …)`：短类修饰里的组件重声明施加到实例化出的
  // 记录上——字段的属性先取新类型的（quantity / unit / min），再取重声明 / 原声明的修饰；短类的短类再加修饰照常（RS8）。
  // 期望 = omc 1.25.0 实跑（2026-10-08）。
  YMC_CHECK_EQ(flatten_src("model RS7\n  package SI\n"
                           "    type Inductance = Real(final quantity = \"Inductance\", final unit = \"H\");\n"
                           "    type Resistance = Real(final quantity = \"Resistance\", final unit = \"Ohm\", min = 0);\n"
                           "  end SI;\n  record Salient\n    replaceable Real d \"d-axis\";\n"
                           "    replaceable Real q \"q-axis\";\n  end Salient;\n"
                           "  record SalientInductance = Salient(redeclare SI.Inductance d, redeclare SI.Inductance q) \"Salient inductance\";\n"
                           "  record SalientResistance = Salient(redeclare SI.Resistance d(start = 2), redeclare SI.Resistance q);\n"
                           "  parameter SalientResistance R(d = 1, q = 5);\nend RS7;\n"),
               std::string("class RS7\n"
                           "  parameter Real R.d(quantity = \"Resistance\", unit = \"Ohm\", min = 0.0, start = 2.0) = 1.0 \"d-axis\";\n"
                           "  parameter Real R.q(quantity = \"Resistance\", unit = \"Ohm\", min = 0.0) = 5.0 \"q-axis\";\n"
                           "end RS7;\n"));
  YMC_CHECK_EQ(flatten_src("model RS8\n  package SI\n"
                           "    type Inductance = Real(final quantity = \"Inductance\", final unit = \"H\");\n"
                           "    type Resistance = Real(final quantity = \"Resistance\", final unit = \"Ohm\", min = 0);\n"
                           "  end SI;\n  record Salient\n    replaceable Real d \"d-axis\";\n"
                           "    replaceable Real q \"q-axis\";\n  end Salient;\n"
                           "  record SalientInductance = Salient(redeclare SI.Inductance d, redeclare SI.Inductance q) \"Salient inductance\";\n"
                           "  record SL2 = SalientInductance(d(start = 3));\n  parameter SL2 L(d = 1, q = 2);\nend RS8;\n"),
               std::string("class RS8\n"
                           "  parameter Real L.d(quantity = \"Inductance\", unit = \"H\", start = 3.0) = 1.0 \"d-axis\";\n"
                           "  parameter Real L.q(quantity = \"Inductance\", unit = \"H\") = 2.0 \"q-axis\";\nend RS8;\n"));
}

YMC_TEST(flatten_operator_record_constructor) {
  // operator record（MSL Complex 一类）：`C(…)` 调的是 'constructor' 里的函数（压平名 `C.'constructor'.fromReal`，缺省补齐、
  // 实参提 Real），实参全是常量的求值成记录式 `C(1.0, 2.0)`；引用到这个记录类型就打它整个函数缓存（默认构造函数 + fromReal）。
  // 短类连接器别名 `connector CO = output C` 的组件按连接器（方向、connect 按字段声明序），记录类型是别名本身（只有默认
  // 构造函数 `CO`）；operator record 别名 `P.CV` 的构造 / 缓存按别名的路径。参数绑定照返回记录的函数调用：常量实参的求值成
  // 字段，参数实参的挪成初始方程（fixed = false）；没有 'constructor' 的照普通记录构造（ORU3）。
  // 期望 = omc 1.25.0 实跑（2026-10-08，rp62x SPEC OR1-OR5 / OR13）。
  YMC_CHECK_EQ(flatten_src("operator record C \"complex-like\"\n  Real re \"real part\";\n  Real im \"imaginary part\";\n"
                           "  encapsulated operator 'constructor'\n    function fromReal \"from reals\"\n      import C;\n"
                           "      input Real re \"re in\";\n      input Real im = 0 \"im in\";\n"
                           "      output C result(re = re, im = im) \"result\";\n    algorithm\n"
                           "      annotation(Inline = true);\n    end fromReal;\n  end 'constructor';\nend C;\n"
                           "connector CO = output C \"out\";\nconnector CI = input C \"in\";\nmodel ORU1\n"
                           "  CO y \"y comment\";\n  CI u;\n  C c = C(time, 1);\n  C d;\nequation\n  connect(y, u);\n"
                           "  y = C(2 * time);\n  d = C(1, 2);\nend ORU1;\n"),
               std::string("function C \"Automatically generated record constructor for C\"\n  input Real re;\n"
                           "  input Real im;\n  output C res;\nend C;\n\nfunction C.'constructor'.fromReal \"from reals\"\n"
                           "  input Real re \"re in\";\n  input Real im = 0.0 \"im in\";\n  output C result \"result\";\n"
                           "algorithm\nend C.'constructor'.fromReal;\n\n"
                           "function CO \"Automatically generated record constructor for CO\"\n  input Real re;\n"
                           "  input Real im;\n  output CO res;\nend CO;\n\nclass ORU1\n  output Real y.re \"real part\";\n"
                           "  output Real y.im \"imaginary part\";\n  input Real u.re \"real part\";\n"
                           "  input Real u.im \"imaginary part\";\n  Real c.re \"real part\";\n  Real c.im \"imaginary part\";\n"
                           "  Real d.re \"real part\";\n  Real d.im \"imaginary part\";\nequation\n  y.re = u.re;\n"
                           "  y.im = u.im;\n  c = C.'constructor'.fromReal(time, 1.0);\n"
                           "  y = C.'constructor'.fromReal(2.0 * time, 0.0);\n  d = C(1.0, 2.0);\nend ORU1;\n"));
  YMC_CHECK_EQ(flatten_src("operator record C \"complex-like\"\n  Real re \"real part\";\n  Real im \"imaginary part\";\n"
                           "  encapsulated operator 'constructor'\n    function fromReal \"from reals\"\n      import C;\n"
                           "      input Real re \"re in\";\n      input Real im = 0 \"im in\";\n"
                           "      output C result(re = re, im = im) \"result\";\n    algorithm\n"
                           "      annotation(Inline = true);\n    end fromReal;\n  end 'constructor';\nend C;\nmodel ORU2\n"
                           "  parameter Real r = 2;\n  parameter C p = C(r, 2 * r);\n  parameter C q = C(3);\n"
                           "  constant C k = C(1, 2);\n  Real x = k.im * time;\nend ORU2;\n"),
               std::string("function C \"Automatically generated record constructor for C\"\n  input Real re;\n"
                           "  input Real im;\n  output C res;\nend C;\n\nfunction C.'constructor'.fromReal \"from reals\"\n"
                           "  input Real re \"re in\";\n  input Real im = 0.0 \"im in\";\n  output C result \"result\";\n"
                           "algorithm\nend C.'constructor'.fromReal;\n\nclass ORU2\n  parameter Real r = 2.0;\n"
                           "  parameter Real p.re(fixed = false) \"real part\";\n"
                           "  parameter Real p.im(fixed = false) \"imaginary part\";\n"
                           "  parameter Real q.re = 3.0 \"real part\";\n  parameter Real q.im = 0.0 \"imaginary part\";\n"
                           "  constant Real k.re = 1.0 \"real part\";\n  constant Real k.im = 2.0 \"imaginary part\";\n"
                           "  Real x = 2.0 * time;\ninitial equation\n  p = C.'constructor'.fromReal(r, 2.0 * r);\nend ORU2;\n"));
  YMC_CHECK_EQ(flatten_src("operator record OR \"no constructor\"\n  Real a;\n  Real b = 2;\nend OR;\nmodel ORU3\n  OR r1;\n"
                           "  OR r2;\nequation\n  r1 = OR(time, 1);\n  r2 = OR(a = time);\nend ORU3;\n"),
               std::string("function OR \"Automatically generated record constructor for OR\"\n  input Real a;\n"
                           "  input Real b = 2.0;\n  output OR res;\nend OR;\n\nclass ORU3\n  Real r1.a;\n  Real r1.b = 2.0;\n"
                           "  Real r2.a;\n  Real r2.b = 2.0;\nequation\n  r1 = OR(time, 1.0);\n  r2 = OR(time, 2.0);\n"
                           "end ORU3;\n"));
  YMC_CHECK_EQ(flatten_src("operator record C \"complex-like\"\n  replaceable Real re \"real part\";\n"
                           "  replaceable Real im \"imaginary part\";\n  encapsulated operator 'constructor'\n"
                           "    function fromReal \"from reals\"\n      import C;\n      input Real re \"re in\";\n"
                           "      input Real im = 0 \"im in\";\n      output C result(re = re, im = im) \"result\";\n"
                           "    algorithm\n      annotation(Inline = true);\n    end fromReal;\n  end 'constructor';\nend C;\n"
                           "package P\n  type Voltage = Real(quantity = \"V\", unit = \"V\");\n"
                           "  operator record CV = C(redeclare Voltage re, redeclare Voltage im) \"voltage\";\n"
                           "  record R2 = C;\nend P;\nmodel ORU4\n  P.CV v;\n  P.CV w;\n  P.R2 z;\n  C c;\nequation\n"
                           "  v = P.CV(time);\n  w = c;\n  z.re = time;\n  z.im = 0;\n  c = C(1, time);\nend ORU4;\n"),
               std::string("function C \"Automatically generated record constructor for C\"\n  input Real re;\n"
                           "  input Real im;\n  output C res;\nend C;\n\nfunction C.'constructor'.fromReal \"from reals\"\n"
                           "  input Real re \"re in\";\n  input Real im = 0.0 \"im in\";\n  output C result \"result\";\n"
                           "algorithm\nend C.'constructor'.fromReal;\n\n"
                           "function P.CV \"Automatically generated record constructor for P.CV\"\n  input Real re;\n"
                           "  input Real im;\n  output CV res;\nend P.CV;\n\n"
                           "function P.CV.'constructor'.fromReal \"from reals\"\n  input Real re \"re in\";\n"
                           "  input Real im = 0.0 \"im in\";\n  output C result \"result\";\nalgorithm\n"
                           "end P.CV.'constructor'.fromReal;\n\nclass ORU4\n"
                           "  Real v.re(quantity = \"V\", unit = \"V\") \"real part\";\n"
                           "  Real v.im(quantity = \"V\", unit = \"V\") \"imaginary part\";\n"
                           "  Real w.re(quantity = \"V\", unit = \"V\") \"real part\";\n"
                           "  Real w.im(quantity = \"V\", unit = \"V\") \"imaginary part\";\n  Real z.re \"real part\";\n"
                           "  Real z.im \"imaginary part\";\n  Real c.re \"real part\";\n  Real c.im \"imaginary part\";\n"
                           "equation\n  v = P.CV.'constructor'.fromReal(time, 0.0);\n  w = c;\n  z.re = time;\n  z.im = 0.0;\n"
                           "  c = C.'constructor'.fromReal(1.0, time);\nend ORU4;\n"));
}

YMC_TEST(flatten_operator_record_operators) {
  // 记录运算数的二元 / 一元运算走重载运算符：候选是左运算数类型里的同名运算符函数（`operator function '+'` 本身，或
  // `operator '-'` 里的 negate / subtract），只认精确匹配；没有 → 隐式构造另一侧（`r * b` → `C.'*'.multiply(
  // C.'constructor'.fromReal(r, 0.0), b)`，常量实参的构造求值成 `C(2.0, 0.0)`、Integer 转型）。函数名用运算数类型的路径
  //（别名 `P.CV.'*'.multiply`），结果类型是函数的输出声明类型。`-(-a)` 实例化时就消掉；局部 operator record 常量的整体
  // 引用换成记录式 `C(0.0, 1.0)`。期望 = omc 1.25.0 实跑（2026-10-09，rp62x SPEC OR6-OR9）。
  YMC_CHECK_EQ(flatten_src("operator record C \"complex-like\"\n  replaceable Real re \"real part\";\n"
                           "  replaceable Real im \"imaginary part\";\n  encapsulated operator 'constructor'\n"
                           "    function fromReal \"from reals\"\n      import C;\n      input Real re \"re in\";\n"
                           "      input Real im = 0 \"im in\";\n      output C result(re = re, im = im) \"result\";\n"
                           "    algorithm\n      annotation(Inline = true);\n    end fromReal;\n  end 'constructor';\n"
                           "  encapsulated operator function '+' \"add\"\n    import C;\n    input C c1 \"c1\";\n"
                           "    input C c2 \"c2\";\n    output C c3 \"c3\";\n  algorithm\n"
                           "    c3 := C(c1.re + c2.re, c1.im + c2.im);\n    annotation(Inline = true);\n  end '+';\n"
                           "  encapsulated operator '-' \"minus\"\n    function negate \"neg\"\n      import C;\n"
                           "      input C c1;\n      output C c2;\n    algorithm\n      c2 := C(-c1.re, -c1.im);\n"
                           "    end negate;\n    function subtract \"sub\"\n      import C;\n      input C c1;\n"
                           "      input C c2;\n      output C c3;\n    algorithm\n      c3 := C(c1.re - c2.re, c1.im - c2.im);\n"
                           "    end subtract;\n  end '-';\n  encapsulated operator '*' \"times\"\n"
                           "    function multiply \"mul\"\n      import C;\n      input C c1;\n      input C c2;\n"
                           "      output C c3;\n    algorithm\n"
                           "      c3 := C(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n    end multiply;\n"
                           "  end '*';\n  encapsulated operator function '==' \"eq\"\n    import C;\n    input C c1;\n"
                           "    input C c2;\n    output Boolean result;\n  algorithm\n"
                           "    result := c1.re == c2.re and c1.im == c2.im;\n  end '==';\nend C;\npackage P\n"
                           "  type Voltage = Real(quantity = \"V\", unit = \"V\");\n"
                           "  operator record CV = C(redeclare Voltage re, redeclare Voltage im) \"voltage\";\nend P;\n"
                           "model ORU5\n  C a, b, c, d;\n  Real r = time;\n  Boolean e;\nequation\n  a = C(time, 1);\n"
                           "  b = a + C(2, time);\n  c = -a;\n  d = r * b - a;\n  e = a == b;\nend ORU5;\n"),
               std::string("function C \"Automatically generated record constructor for C\"\n  input Real re;\n"
                           "  input Real im;\n  output C res;\nend C;\n\nfunction C.'*'.multiply \"mul\"\n  input C c1;\n"
                           "  input C c2;\n  output C c3;\nalgorithm\n"
                           "  c3 := C.'constructor'.fromReal(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n"
                           "end C.'*'.multiply;\n\nfunction C.'+' \"add\"\n  input C c1 \"c1\";\n  input C c2 \"c2\";\n"
                           "  output C c3 \"c3\";\nalgorithm\n  c3 := C.'constructor'.fromReal(c1.re + c2.re, c1.im + c2.im);\n"
                           "end C.'+';\n\nfunction C.'-'.negate \"neg\"\n  input C c1;\n  output C c2;\nalgorithm\n"
                           "  c2 := C.'constructor'.fromReal(-c1.re, -c1.im);\nend C.'-'.negate;\n\n"
                           "function C.'-'.subtract \"sub\"\n  input C c1;\n  input C c2;\n  output C c3;\nalgorithm\n"
                           "  c3 := C.'constructor'.fromReal(c1.re - c2.re, c1.im - c2.im);\nend C.'-'.subtract;\n\n"
                           "function C.'==' \"eq\"\n  input C c1;\n  input C c2;\n  output Boolean result;\nalgorithm\n"
                           "  result := c1.re == c2.re and c1.im == c2.im;\nend C.'==';\n\n"
                           "function C.'constructor'.fromReal \"from reals\"\n  input Real re \"re in\";\n"
                           "  input Real im = 0.0 \"im in\";\n  output C result \"result\";\nalgorithm\n"
                           "end C.'constructor'.fromReal;\n\nclass ORU5\n  Real a.re \"real part\";\n"
                           "  Real a.im \"imaginary part\";\n  Real b.re \"real part\";\n  Real b.im \"imaginary part\";\n"
                           "  Real c.re \"real part\";\n  Real c.im \"imaginary part\";\n  Real d.re \"real part\";\n"
                           "  Real d.im \"imaginary part\";\n  Real r = time;\n  Boolean e;\nequation\n"
                           "  a = C.'constructor'.fromReal(time, 1.0);\n  b = C.'+'(a, C.'constructor'.fromReal(2.0, time));\n"
                           "  c = C.'-'.negate(a);\n"
                           "  d = C.'-'.subtract(C.'*'.multiply(C.'constructor'.fromReal(r, 0.0), b), a);\n  e = C.'=='(a, b);\n"
                           "end ORU5;\n"));
  YMC_CHECK_EQ(flatten_src("operator record C \"complex-like\"\n  replaceable Real re \"real part\";\n"
                           "  replaceable Real im \"imaginary part\";\n  encapsulated operator 'constructor'\n"
                           "    function fromReal \"from reals\"\n      import C;\n      input Real re \"re in\";\n"
                           "      input Real im = 0 \"im in\";\n      output C result(re = re, im = im) \"result\";\n"
                           "    algorithm\n      annotation(Inline = true);\n    end fromReal;\n  end 'constructor';\n"
                           "  encapsulated operator function '+' \"add\"\n    import C;\n    input C c1 \"c1\";\n"
                           "    input C c2 \"c2\";\n    output C c3 \"c3\";\n  algorithm\n"
                           "    c3 := C(c1.re + c2.re, c1.im + c2.im);\n    annotation(Inline = true);\n  end '+';\n"
                           "  encapsulated operator '-' \"minus\"\n    function negate \"neg\"\n      import C;\n"
                           "      input C c1;\n      output C c2;\n    algorithm\n      c2 := C(-c1.re, -c1.im);\n"
                           "    end negate;\n    function subtract \"sub\"\n      import C;\n      input C c1;\n"
                           "      input C c2;\n      output C c3;\n    algorithm\n      c3 := C(c1.re - c2.re, c1.im - c2.im);\n"
                           "    end subtract;\n  end '-';\n  encapsulated operator '*' \"times\"\n"
                           "    function multiply \"mul\"\n      import C;\n      input C c1;\n      input C c2;\n"
                           "      output C c3;\n    algorithm\n"
                           "      c3 := C(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n    end multiply;\n"
                           "  end '*';\n  encapsulated operator function '==' \"eq\"\n    import C;\n    input C c1;\n"
                           "    input C c2;\n    output Boolean result;\n  algorithm\n"
                           "    result := c1.re == c2.re and c1.im == c2.im;\n  end '==';\nend C;\npackage P\n"
                           "  type Voltage = Real(quantity = \"V\", unit = \"V\");\n"
                           "  operator record CV = C(redeclare Voltage re, redeclare Voltage im) \"voltage\";\nend P;\n"
                           "model ORU6\n  P.CV v, w;\n  C a;\nequation\n  a = C(time, 2);\n  v = w * a;\n  w = a * 2;\n"
                           "end ORU6;\n"),
               std::string("function C \"Automatically generated record constructor for C\"\n  input Real re;\n"
                           "  input Real im;\n  output C res;\nend C;\n\nfunction C.'*'.multiply \"mul\"\n  input C c1;\n"
                           "  input C c2;\n  output C c3;\nalgorithm\n"
                           "  c3 := C.'constructor'.fromReal(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n"
                           "end C.'*'.multiply;\n\nfunction C.'constructor'.fromReal \"from reals\"\n"
                           "  input Real re \"re in\";\n  input Real im = 0.0 \"im in\";\n  output C result \"result\";\n"
                           "algorithm\nend C.'constructor'.fromReal;\n\n"
                           "function P.CV \"Automatically generated record constructor for P.CV\"\n  input Real re;\n"
                           "  input Real im;\n  output CV res;\nend P.CV;\n\nfunction P.CV.'*'.multiply \"mul\"\n  input C c1;\n"
                           "  input C c2;\n  output C c3;\nalgorithm\n"
                           "  c3 := C.'constructor'.fromReal(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n"
                           "end P.CV.'*'.multiply;\n\nfunction P.CV.'constructor'.fromReal \"from reals\"\n"
                           "  input Real re \"re in\";\n  input Real im = 0.0 \"im in\";\n  output C result \"result\";\n"
                           "algorithm\nend P.CV.'constructor'.fromReal;\n\nclass ORU6\n"
                           "  Real v.re(quantity = \"V\", unit = \"V\") \"real part\";\n"
                           "  Real v.im(quantity = \"V\", unit = \"V\") \"imaginary part\";\n"
                           "  Real w.re(quantity = \"V\", unit = \"V\") \"real part\";\n"
                           "  Real w.im(quantity = \"V\", unit = \"V\") \"imaginary part\";\n  Real a.re \"real part\";\n"
                           "  Real a.im \"imaginary part\";\nequation\n  a = C.'constructor'.fromReal(time, 2.0);\n"
                           "  v = P.CV.'*'.multiply(w, a);\n  w = C.'*'.multiply(a, C(2.0, 0.0));\nend ORU6;\n"));
  YMC_CHECK_EQ(flatten_src("operator record C \"complex-like\"\n  replaceable Real re \"real part\";\n"
                           "  replaceable Real im \"imaginary part\";\n  encapsulated operator 'constructor'\n"
                           "    function fromReal \"from reals\"\n      import C;\n      input Real re \"re in\";\n"
                           "      input Real im = 0 \"im in\";\n      output C result(re = re, im = im) \"result\";\n"
                           "    algorithm\n      annotation(Inline = true);\n    end fromReal;\n  end 'constructor';\n"
                           "  encapsulated operator function '+' \"add\"\n    import C;\n    input C c1 \"c1\";\n"
                           "    input C c2 \"c2\";\n    output C c3 \"c3\";\n  algorithm\n"
                           "    c3 := C(c1.re + c2.re, c1.im + c2.im);\n    annotation(Inline = true);\n  end '+';\n"
                           "  encapsulated operator '-' \"minus\"\n    function negate \"neg\"\n      import C;\n"
                           "      input C c1;\n      output C c2;\n    algorithm\n      c2 := C(-c1.re, -c1.im);\n"
                           "    end negate;\n    function subtract \"sub\"\n      import C;\n      input C c1;\n"
                           "      input C c2;\n      output C c3;\n    algorithm\n      c3 := C(c1.re - c2.re, c1.im - c2.im);\n"
                           "    end subtract;\n  end '-';\n  encapsulated operator '*' \"times\"\n"
                           "    function multiply \"mul\"\n      import C;\n      input C c1;\n      input C c2;\n"
                           "      output C c3;\n    algorithm\n"
                           "      c3 := C(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n    end multiply;\n"
                           "  end '*';\n  encapsulated operator function '==' \"eq\"\n    import C;\n    input C c1;\n"
                           "    input C c2;\n    output Boolean result;\n  algorithm\n"
                           "    result := c1.re == c2.re and c1.im == c2.im;\n  end '==';\nend C;\npackage P\n"
                           "  type Voltage = Real(quantity = \"V\", unit = \"V\");\n"
                           "  operator record CV = C(redeclare Voltage re, redeclare Voltage im) \"voltage\";\nend P;\n"
                           "model ORU7\n  C a, b;\n  Integer n = 3;\nequation\n  a = C(time);\n  b = -(-a) + a * n;\nend ORU7;\n"),
               std::string("function C \"Automatically generated record constructor for C\"\n  input Real re;\n"
                           "  input Real im;\n  output C res;\nend C;\n\nfunction C.'*'.multiply \"mul\"\n  input C c1;\n"
                           "  input C c2;\n  output C c3;\nalgorithm\n"
                           "  c3 := C.'constructor'.fromReal(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n"
                           "end C.'*'.multiply;\n\nfunction C.'+' \"add\"\n  input C c1 \"c1\";\n  input C c2 \"c2\";\n"
                           "  output C c3 \"c3\";\nalgorithm\n  c3 := C.'constructor'.fromReal(c1.re + c2.re, c1.im + c2.im);\n"
                           "end C.'+';\n\nfunction C.'constructor'.fromReal \"from reals\"\n  input Real re \"re in\";\n"
                           "  input Real im = 0.0 \"im in\";\n  output C result \"result\";\nalgorithm\n"
                           "end C.'constructor'.fromReal;\n\nclass ORU7\n  Real a.re \"real part\";\n"
                           "  Real a.im \"imaginary part\";\n  Real b.re \"real part\";\n  Real b.im \"imaginary part\";\n"
                           "  Integer n = 3;\nequation\n  a = C.'constructor'.fromReal(time, 0.0);\n"
                           "  b = C.'+'(a, C.'*'.multiply(a, C.'constructor'.fromReal(/*Real*/(n), 0.0)));\nend ORU7;\n"));
  YMC_CHECK_EQ(flatten_src("operator record C \"complex-like\"\n  replaceable Real re \"real part\";\n"
                           "  replaceable Real im \"imaginary part\";\n  encapsulated operator 'constructor'\n"
                           "    function fromReal \"from reals\"\n      import C;\n      input Real re \"re in\";\n"
                           "      input Real im = 0 \"im in\";\n      output C result(re = re, im = im) \"result\";\n"
                           "    algorithm\n      annotation(Inline = true);\n    end fromReal;\n  end 'constructor';\n"
                           "  encapsulated operator function '+' \"add\"\n    import C;\n    input C c1 \"c1\";\n"
                           "    input C c2 \"c2\";\n    output C c3 \"c3\";\n  algorithm\n"
                           "    c3 := C(c1.re + c2.re, c1.im + c2.im);\n    annotation(Inline = true);\n  end '+';\n"
                           "  encapsulated operator '-' \"minus\"\n    function negate \"neg\"\n      import C;\n"
                           "      input C c1;\n      output C c2;\n    algorithm\n      c2 := C(-c1.re, -c1.im);\n"
                           "    end negate;\n    function subtract \"sub\"\n      import C;\n      input C c1;\n"
                           "      input C c2;\n      output C c3;\n    algorithm\n      c3 := C(c1.re - c2.re, c1.im - c2.im);\n"
                           "    end subtract;\n  end '-';\n  encapsulated operator '*' \"times\"\n"
                           "    function multiply \"mul\"\n      import C;\n      input C c1;\n      input C c2;\n"
                           "      output C c3;\n    algorithm\n"
                           "      c3 := C(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n    end multiply;\n"
                           "  end '*';\n  encapsulated operator function '==' \"eq\"\n    import C;\n    input C c1;\n"
                           "    input C c2;\n    output Boolean result;\n  algorithm\n"
                           "    result := c1.re == c2.re and c1.im == c2.im;\n  end '==';\nend C;\npackage P\n"
                           "  type Voltage = Real(quantity = \"V\", unit = \"V\");\n"
                           "  operator record CV = C(redeclare Voltage re, redeclare Voltage im) \"voltage\";\nend P;\n"
                           "model ORU8\n  constant C j = C(0, 1);\n  C v, w;\n  parameter Real omega = 2;\nequation\n"
                           "  v = -j * omega * w;\n  w = C(time);\nend ORU8;\n"),
               std::string("function C \"Automatically generated record constructor for C\"\n  input Real re;\n"
                           "  input Real im;\n  output C res;\nend C;\n\nfunction C.'*'.multiply \"mul\"\n  input C c1;\n"
                           "  input C c2;\n  output C c3;\nalgorithm\n"
                           "  c3 := C.'constructor'.fromReal(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n"
                           "end C.'*'.multiply;\n\nfunction C.'-'.negate \"neg\"\n  input C c1;\n  output C c2;\nalgorithm\n"
                           "  c2 := C.'constructor'.fromReal(-c1.re, -c1.im);\nend C.'-'.negate;\n\n"
                           "function C.'constructor'.fromReal \"from reals\"\n  input Real re \"re in\";\n"
                           "  input Real im = 0.0 \"im in\";\n  output C result \"result\";\nalgorithm\n"
                           "end C.'constructor'.fromReal;\n\nclass ORU8\n  constant Real j.re = 0.0 \"real part\";\n"
                           "  constant Real j.im = 1.0 \"imaginary part\";\n  Real v.re \"real part\";\n"
                           "  Real v.im \"imaginary part\";\n  Real w.re \"real part\";\n  Real w.im \"imaginary part\";\n"
                           "  parameter Real omega = 2.0;\nequation\n"
                           "  v = C.'-'.negate(C.'*'.multiply(C.'*'.multiply(C(0.0, 1.0), C.'constructor'.fromReal(omega, 0.0)), w));\n"
                           "  w = C.'constructor'.fromReal(time, 0.0);\nend ORU8;\n"));
}

YMC_TEST(flatten_final_record_binding_forced_inline) {
  // final parameter 记录的整体绑定定型后是用户函数调用（'constructor'、重载运算符、返回记录的函数）→ 强制内联成记录式，各字段
  // 的式子化简后当字段绑定：'constructor' 取输出的字段修饰（`re = re`），单语句函数代换输入（记录实参取字段：记录式取元素、
  // 记录引用接成 `p.re`），`k * exp(0.0) * cos(phi) - 0.0 * …` 化简成 `k * cos(phi)`；内联完不留函数块。
  // 期望 = omc 1.25.0 实跑（2026-10-09，rp62x SPEC OR11）。
  YMC_CHECK_EQ(flatten_src("operator record C \"complex-like\"\n  replaceable Real re \"real part\";\n"
                           "  replaceable Real im \"imaginary part\";\n  encapsulated operator 'constructor'\n"
                           "    function fromReal \"from reals\"\n      import C;\n      input Real re \"re in\";\n"
                           "      input Real im = 0 \"im in\";\n      output C result(re = re, im = im) \"result\";\n"
                           "    algorithm\n      annotation(Inline = true);\n    end fromReal;\n  end 'constructor';\n"
                           "  encapsulated operator function '+' \"add\"\n    import C;\n    input C c1 \"c1\";\n"
                           "    input C c2 \"c2\";\n    output C c3 \"c3\";\n  algorithm\n"
                           "    c3 := C(c1.re + c2.re, c1.im + c2.im);\n    annotation(Inline = true);\n  end '+';\n"
                           "  encapsulated operator '-' \"minus\"\n    function negate \"neg\"\n      import C;\n"
                           "      input C c1;\n      output C c2;\n    algorithm\n      c2 := C(-c1.re, -c1.im);\n"
                           "    end negate;\n    function subtract \"sub\"\n      import C;\n      input C c1;\n"
                           "      input C c2;\n      output C c3;\n    algorithm\n      c3 := C(c1.re - c2.re, c1.im - c2.im);\n"
                           "    end subtract;\n  end '-';\n  encapsulated operator '*' \"times\"\n"
                           "    function multiply \"mul\"\n      import C;\n      input C c1;\n      input C c2;\n"
                           "      output C c3;\n    algorithm\n"
                           "      c3 := C(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n    end multiply;\n"
                           "  end '*';\n  encapsulated operator function '==' \"eq\"\n    import C;\n    input C c1;\n"
                           "    input C c2;\n    output Boolean result;\n  algorithm\n"
                           "    result := c1.re == c2.re and c1.im == c2.im;\n  end '==';\nend C;\npackage P\n"
                           "  type Voltage = Real(quantity = \"V\", unit = \"V\");\n"
                           "  operator record CV = C(redeclare Voltage re, redeclare Voltage im) \"voltage\";\nend P;\n"
                           "function cexp \"exp of C\"\n  input C c1;\n  output C c2;\nalgorithm\n"
                           "  c2 := C(exp(c1.re) * cos(c1.im), exp(c1.re) * sin(c1.im));\nend cexp;\nmodel ORU9\n"
                           "  parameter Real a = 1;\n  parameter Real b = 2;\n  parameter Real k = 3;\n"
                           "  parameter Real phi = 0;\n  final parameter C c = C(a, b);\n  final parameter C d = C(a);\n"
                           "  final parameter C n = k * cexp(C(0, phi));\n  parameter C p = C(1, 2);\n"
                           "  final parameter C q = p * p;\n  final parameter C s = p + C(1, 0);\n  final parameter C t = -p;\n"
                           "end ORU9;\n"),
               std::string("class ORU9\n  parameter Real a = 1.0;\n  parameter Real b = 2.0;\n  parameter Real k = 3.0;\n"
                           "  parameter Real phi = 0.0;\n  final parameter Real c.re = a \"real part\";\n"
                           "  final parameter Real c.im = b \"imaginary part\";\n"
                           "  final parameter Real d.re = a \"real part\";\n"
                           "  final parameter Real d.im = 0.0 \"imaginary part\";\n"
                           "  final parameter Real n.re = k * cos(phi) \"real part\";\n"
                           "  final parameter Real n.im = k * sin(phi) \"imaginary part\";\n"
                           "  parameter Real p.re = 1.0 \"real part\";\n  parameter Real p.im = 2.0 \"imaginary part\";\n"
                           "  final parameter Real q.re = p.re * p.re - p.im * p.im \"real part\";\n"
                           "  final parameter Real q.im = p.re * p.im + p.im * p.re \"imaginary part\";\n"
                           "  final parameter Real s.re = p.re + 1.0 \"real part\";\n"
                           "  final parameter Real s.im = p.im \"imaginary part\";\n"
                           "  final parameter Real t.re = -p.re \"real part\";\n"
                           "  final parameter Real t.im = -p.im \"imaginary part\";\nend ORU9;\n"));
}

YMC_TEST(flatten_string_folded_after_structural_substitution) {
  // 结构参数代入后算法段里的 `String(m) + " phases"`（m 因数组维成结构参数）：omc 化简时求值成 "3 phases"（方程里的 assert
  // 消息不化简，见 raw_message）。期望 = omc 1.25.0 实跑（2026-10-09）。
  YMC_CHECK_EQ(flatten_src("model FS2\n  parameter Integer m = 3;\n  Real x[m];\nequation\n  x = fill(time, m);\n"
                           "initial algorithm\n  assert(m > 1, String(m) + \" phases\");\nend FS2;\n"),
               std::string("class FS2\n  final parameter Integer m = 3;\n  Real x[1];\n  Real x[2];\n  Real x[3];\n"
                           "initial algorithm\n  assert(true, \"3 phases\");\nequation\n  x[1] = time;\n  x[2] = time;\n"
                           "  x[3] = time;\nend FS2;\n"));
}

YMC_TEST(flatten_short_class_modifier_on_record_array) {
  // 记录数组的声明类型是带修饰的短类（`record S = R0(a(start = 3), b(min = 0)); S s[2];`）：短类修饰是类型上的，每个元素原样
  // 照收（不按下标切分），优先级最低。期望 = omc 1.25.0 实跑（2026-10-09）。
  YMC_CHECK_EQ(flatten_src("model VA2\n  record R0\n    Real a = 1;\n    Real b = 2;\n  end R0;\n"
                           "  record S = R0(a(start = 3), b(min = 0));\n  S s[2];\nequation\n  s[1].a = time;\n"
                           "  s[2].a = 2 * time;\nend VA2;\n"),
               std::string("class VA2\n  Real s[1].a(start = 3.0) = 1.0;\n  Real s[1].b(min = 0.0) = 2.0;\n"
                           "  Real s[2].a(start = 3.0) = 1.0;\n  Real s[2].b(min = 0.0) = 2.0;\nequation\n  s[1].a = time;\n"
                           "  s[2].a = 2.0 * time;\nend VA2;\n"));
}

YMC_TEST(flatten_overloaded_operator_on_record_arrays) {
  // 记录数组作重载运算符的运算数：+ / - 两侧同形逐元素、标量 * 数组 / 数组 * 标量逐元素（每个元素照标量解析：精确匹配或隐式
  // 构造），结果是元素调用的数组、方程里逐元素标量化。期望 = omc 1.25.0 实跑（2026-10-09，rp62x SPEC OR10）。
  YMC_CHECK_EQ(flatten_src("operator record C \"complex-like\"\n  replaceable Real re \"real part\";\n"
                           "  replaceable Real im \"imaginary part\";\n  encapsulated operator 'constructor'\n"
                           "    function fromReal \"from reals\"\n      import C;\n      input Real re \"re in\";\n"
                           "      input Real im = 0 \"im in\";\n      output C result(re = re, im = im) \"result\";\n"
                           "    algorithm\n      annotation(Inline = true);\n    end fromReal;\n  end 'constructor';\n"
                           "  encapsulated operator function '+' \"add\"\n    import C;\n    input C c1 \"c1\";\n"
                           "    input C c2 \"c2\";\n    output C c3 \"c3\";\n  algorithm\n"
                           "    c3 := C(c1.re + c2.re, c1.im + c2.im);\n    annotation(Inline = true);\n  end '+';\n"
                           "  encapsulated operator '-' \"minus\"\n    function negate \"neg\"\n      import C;\n"
                           "      input C c1;\n      output C c2;\n    algorithm\n      c2 := C(-c1.re, -c1.im);\n"
                           "    end negate;\n    function subtract \"sub\"\n      import C;\n      input C c1;\n"
                           "      input C c2;\n      output C c3;\n    algorithm\n      c3 := C(c1.re - c2.re, c1.im - c2.im);\n"
                           "    end subtract;\n  end '-';\n  encapsulated operator '*' \"times\"\n"
                           "    function multiply \"mul\"\n      import C;\n      input C c1;\n      input C c2;\n"
                           "      output C c3;\n    algorithm\n"
                           "      c3 := C(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n    end multiply;\n"
                           "  end '*';\n  encapsulated operator function '==' \"eq\"\n    import C;\n    input C c1;\n"
                           "    input C c2;\n    output Boolean result;\n  algorithm\n"
                           "    result := c1.re == c2.re and c1.im == c2.im;\n  end '==';\nend C;\npackage P\n"
                           "  type Voltage = Real(quantity = \"V\", unit = \"V\");\n"
                           "  operator record CV = C(redeclare Voltage re, redeclare Voltage im) \"voltage\";\nend P;\n"
                           "model ORU10\n  C z1[2], z2[2], z3[2], z4[2], z5[2];\n  C k;\n  Real r = 2;\nequation\n"
                           "  k = C(time, 1);\n  z1[1] = C(1, time);\n  z1[2] = C(time, 2);\n  z2 = z1;\n  z3 = z1 + z2;\n"
                           "  z4 = k * z1;\n  z5 = z1 * r;\nend ORU10;\n"),
               std::string("function C \"Automatically generated record constructor for C\"\n  input Real re;\n"
                           "  input Real im;\n  output C res;\nend C;\n\nfunction C.'*'.multiply \"mul\"\n  input C c1;\n"
                           "  input C c2;\n  output C c3;\nalgorithm\n"
                           "  c3 := C.'constructor'.fromReal(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n"
                           "end C.'*'.multiply;\n\nfunction C.'+' \"add\"\n  input C c1 \"c1\";\n  input C c2 \"c2\";\n"
                           "  output C c3 \"c3\";\nalgorithm\n  c3 := C.'constructor'.fromReal(c1.re + c2.re, c1.im + c2.im);\n"
                           "end C.'+';\n\nfunction C.'constructor'.fromReal \"from reals\"\n  input Real re \"re in\";\n"
                           "  input Real im = 0.0 \"im in\";\n  output C result \"result\";\nalgorithm\n"
                           "end C.'constructor'.fromReal;\n\nclass ORU10\n  Real z1[1].re \"real part\";\n"
                           "  Real z1[1].im \"imaginary part\";\n  Real z1[2].re \"real part\";\n"
                           "  Real z1[2].im \"imaginary part\";\n  Real z2[1].re \"real part\";\n"
                           "  Real z2[1].im \"imaginary part\";\n  Real z2[2].re \"real part\";\n"
                           "  Real z2[2].im \"imaginary part\";\n  Real z3[1].re \"real part\";\n"
                           "  Real z3[1].im \"imaginary part\";\n  Real z3[2].re \"real part\";\n"
                           "  Real z3[2].im \"imaginary part\";\n  Real z4[1].re \"real part\";\n"
                           "  Real z4[1].im \"imaginary part\";\n  Real z4[2].re \"real part\";\n"
                           "  Real z4[2].im \"imaginary part\";\n  Real z5[1].re \"real part\";\n"
                           "  Real z5[1].im \"imaginary part\";\n  Real z5[2].re \"real part\";\n"
                           "  Real z5[2].im \"imaginary part\";\n  Real k.re \"real part\";\n  Real k.im \"imaginary part\";\n"
                           "  Real r = 2.0;\nequation\n  k = C.'constructor'.fromReal(time, 1.0);\n"
                           "  z1[1] = C.'constructor'.fromReal(1.0, time);\n  z1[2] = C.'constructor'.fromReal(time, 2.0);\n"
                           "  z2[1] = z1[1];\n  z2[2] = z1[2];\n  z3[1] = C.'+'(z1[1], z2[1]);\n  z3[2] = C.'+'(z1[2], z2[2]);\n"
                           "  z4[1] = C.'*'.multiply(k, z1[1]);\n  z4[2] = C.'*'.multiply(k, z1[2]);\n"
                           "  z5[1] = C.'*'.multiply(z1[1], C.'constructor'.fromReal(r, 0.0));\n"
                           "  z5[2] = C.'*'.multiply(z1[2], C.'constructor'.fromReal(r, 0.0));\nend ORU10;\n"));
}

YMC_TEST(flatten_record_array_binding_equations) {
  // 记录数组的整体绑定：推导式元素按结构条件选支（cj 成 final 参数）、fill 逐元素求值；有元素拆不开（`conj(u[2])`）→ 整个
  // 绑定挪成逐元素方程，记录 cref 元素写成字段的记录式 `C(u[1].re, u[1].im)`；向量 * 向量精确匹配到形参是记录数组的
  // scalarProduct（`input C[:] c1`）；`.*` 逐元素。期望 = omc 1.25.0 实跑（2026-10-09，探针 ry-ut）。
  YMC_CHECK_EQ(flatten_src("operator record C \"complex-like\"\n  replaceable Real re \"real part\";\n"
                           "  replaceable Real im \"imaginary part\";\n  encapsulated operator 'constructor'\n"
                           "    function fromReal \"from reals\"\n      import C;\n      input Real re \"re in\";\n"
                           "      input Real im = 0 \"im in\";\n      output C result(re = re, im = im) \"result\";\n"
                           "    algorithm\n      annotation(Inline = true);\n    end fromReal;\n  end 'constructor';\n"
                           "  encapsulated operator function '+' \"add\"\n    import C;\n    input C c1 \"c1\";\n"
                           "    input C c2 \"c2\";\n    output C c3 \"c3\";\n  algorithm\n"
                           "    c3 := C(c1.re + c2.re, c1.im + c2.im);\n    annotation(Inline = true);\n  end '+';\n"
                           "  encapsulated operator '*' \"times\"\n    function multiply \"mul\"\n      import C;\n"
                           "      input C c1;\n      input C c2;\n      output C c3;\n    algorithm\n"
                           "      c3 := C(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n    end multiply;\n"
                           "    function scalarProduct \"dot\"\n      import C;\n      input C c1[:] \"v1\";\n"
                           "      input C c2[size(c1, 1)] \"v2\";\n      output C c3 \"c1*c2\";\n    algorithm\n"
                           "      c3 := C(0);\n      for i in 1:size(c1, 1) loop\n        c3 := c3 + c1[i] * c2[i];\n"
                           "      end for;\n    end scalarProduct;\n  end '*';\nend C;\nfunction conj \"conjugate\"\n"
                           "  input C c1;\n  output C c2;\nalgorithm\n  c2 := C(c1.re, -c1.im);\nend conj;\nmodel RYU1\n"
                           "  parameter Integer n = 2;\n  parameter Boolean cj[n] = {false, true};\n  C u[n];\n"
                           "  C w[n] = {if cj[k] then conj(u[k]) else u[k] for k in 1:n};\n"
                           "  parameter C g[n] = fill(C(2, 1), n);\n  C y = g * w;\n  C v[n] = u .* w;\nequation\n"
                           "  for k in 1:n loop\n    u[k] = C(k * time, 1);\n  end for;\nend RYU1;\n"),
               std::string("function C \"Automatically generated record constructor for C\"\n  input Real re;\n"
                           "  input Real im;\n  output C res;\nend C;\n\nfunction C.'*'.multiply \"mul\"\n  input C c1;\n"
                           "  input C c2;\n  output C c3;\nalgorithm\n"
                           "  c3 := C.'constructor'.fromReal(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n"
                           "end C.'*'.multiply;\n\nfunction C.'*'.scalarProduct \"dot\"\n  input C[:] c1 \"v1\";\n"
                           "  input C[size(c1, 1)] c2 \"v2\";\n  output C c3 \"c1*c2\";\nalgorithm\n  c3 := C(0.0, 0.0);\n"
                           "  for i in 1:size(c1, 1) loop\n    c3 := C.'+'(c3, C.'*'.multiply(c1[i], c2[i]));\n  end for;\n"
                           "end C.'*'.scalarProduct;\n\nfunction C.'+' \"add\"\n  input C c1 \"c1\";\n  input C c2 \"c2\";\n"
                           "  output C c3 \"c3\";\nalgorithm\n  c3 := C.'constructor'.fromReal(c1.re + c2.re, c1.im + c2.im);\n"
                           "end C.'+';\n\nfunction C.'constructor'.fromReal \"from reals\"\n  input Real re \"re in\";\n"
                           "  input Real im = 0.0 \"im in\";\n  output C result \"result\";\nalgorithm\n"
                           "end C.'constructor'.fromReal;\n\nfunction conj \"conjugate\"\n  input C c1;\n  output C c2;\n"
                           "algorithm\n  c2 := C.'constructor'.fromReal(c1.re, -c1.im);\nend conj;\n\nclass RYU1\n"
                           "  final parameter Integer n = 2;\n  final parameter Boolean cj[1] = false;\n"
                           "  final parameter Boolean cj[2] = true;\n  Real u[1].re \"real part\";\n"
                           "  Real u[1].im \"imaginary part\";\n  Real u[2].re \"real part\";\n"
                           "  Real u[2].im \"imaginary part\";\n  Real w[1].re \"real part\";\n"
                           "  Real w[1].im \"imaginary part\";\n  Real w[2].re \"real part\";\n"
                           "  Real w[2].im \"imaginary part\";\n  parameter Real g[1].re = 2.0 \"real part\";\n"
                           "  parameter Real g[1].im = 1.0 \"imaginary part\";\n  parameter Real g[2].re = 2.0 \"real part\";\n"
                           "  parameter Real g[2].im = 1.0 \"imaginary part\";\n  Real y.re \"real part\";\n"
                           "  Real y.im \"imaginary part\";\n  Real v[1].re \"real part\";\n  Real v[1].im \"imaginary part\";\n"
                           "  Real v[2].re \"real part\";\n  Real v[2].im \"imaginary part\";\nequation\n"
                           "  w[1] = C(u[1].re, u[1].im);\n  w[2] = conj(u[2]);\n  y = C.'*'.scalarProduct(g, w);\n"
                           "  v[1] = C.'*'.multiply(u[1], w[1]);\n  v[2] = C.'*'.multiply(u[2], w[2]);\n"
                           "  u[1] = C.'constructor'.fromReal(time, 1.0);\n  u[2] = C.'constructor'.fromReal(2.0 * time, 1.0);\n"
                           "end RYU1;\n"));
}

YMC_TEST(flatten_component_redeclare_in_declaration_modifier) {
  // 组件声明修饰里的成员重声明（只换类型、不引用名字）：记录数组带 each 的每个元素都换（`final` 前缀照打），标量的同样；
  // 整体绑定照常拆字段。期望 = omc 1.25.0 实跑（2026-10-09，探针 ry-ut）。
  YMC_CHECK_EQ(flatten_src("record R \"pair\"\n  replaceable Real re \"first\";\n  replaceable Real im \"second\";\nend R;\n"
                           "model RYU2\n  type Cur = Real(quantity = \"Current\", unit = \"A\");\n"
                           "  R r[2](redeclare each Cur re, redeclare each final Cur im);\n  R s(redeclare Cur re) = R(1, 2);\n"
                           "equation\n  r[1] = R(time, 1);\n  r[2] = R(1, time);\nend RYU2;\n"),
               std::string("function R \"Automatically generated record constructor for R\"\n  input Real re;\n"
                           "  input Real im;\n  output R res;\nend R;\n\nclass RYU2\n"
                           "  Real r[1].re(quantity = \"Current\", unit = \"A\") \"first\";\n"
                           "  final Real r[1].im(quantity = \"Current\", unit = \"A\") \"second\";\n"
                           "  Real r[2].re(quantity = \"Current\", unit = \"A\") \"first\";\n"
                           "  final Real r[2].im(quantity = \"Current\", unit = \"A\") \"second\";\n"
                           "  Real s.re(quantity = \"Current\", unit = \"A\") = 1.0 \"first\";\n  Real s.im = 2.0 \"second\";\n"
                           "equation\n  r[1] = R(time, 1.0);\n  r[2] = R(1.0, time);\nend RYU2;\n"));
}

YMC_TEST(flatten_function_record_array_params) {
  // 函数的记录形参：记录数组元素的字段 `u[k].re`、operator record 短类别名的形参（`input P.CV[:] v`，别名的构造 / 类型函数随之
  // 打、运算符按别名命名 `P.CV.'*'.multiply`）、记录数组实参；函数体里标量记录形参的实参是推导式 → 向量化成
  // `P.re(… v[(1:size(v, 1))[$i1]] …) for $i1 in 1:size(v, 1)`，`sum` 化成归约。期望 = omc 1.25.0 实跑（2026-10-09，探针 rz-ut）。
  YMC_CHECK_EQ(flatten_src("operator record C \"complex-like\"\n  replaceable Real re \"real part\";\n"
                           "  replaceable Real im \"imaginary part\";\n  encapsulated operator 'constructor'\n"
                           "    function fromReal \"from reals\"\n      import C;\n      input Real re \"re in\";\n"
                           "      input Real im = 0 \"im in\";\n      output C result(re = re, im = im) \"result\";\n"
                           "    algorithm\n      annotation(Inline = true);\n    end fromReal;\n  end 'constructor';\n"
                           "  encapsulated operator '*' \"times\"\n    function multiply \"mul\"\n      import C;\n"
                           "      input C c1;\n      input C c2;\n      output C c3;\n    algorithm\n"
                           "      c3 := C(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n    end multiply;\n"
                           "  end '*';\nend C;\npackage P\n  type Voltage = Real(quantity = \"V\", unit = \"V\");\n"
                           "  operator record CV = C(redeclare Voltage re, redeclare Voltage im) \"voltage\";\n"
                           "  function re \"real part\"\n    input C c;\n    output Real r;\n  algorithm\n    r := c.re;\n"
                           "  end re;\n  function power \"sum of real products\"\n    input CV v[:];\n"
                           "    input C i[size(v, 1)];\n    output Real p;\n  algorithm\n"
                           "    p := sum(re({v[k] * i[k] for k in 1:size(v, 1)}));\n  end power;\n  function rms \"rms\"\n"
                           "    input C u[:];\n    output Real y;\n  protected\n    Integer m = size(u, 1);\n  algorithm\n"
                           "    y := sum({sqrt(u[k].re ^ 2 + u[k].im ^ 2) for k in 1:m}) / m;\n  end rms;\nend P;\nmodel RZU1\n"
                           "  P.CV v[2];\n  C i[2];\n  Real p = P.power(v, i);\n  Real r = P.rms(i);\nequation\n"
                           "  v[1] = C(time, 1);\n  v[2] = C(1, time);\n  i[1] = C(2, time);\n  i[2] = C(time, 2);\nend RZU1;\n"),
               std::string("function C \"Automatically generated record constructor for C\"\n  input Real re;\n"
                           "  input Real im;\n  output C res;\nend C;\n\nfunction C.'constructor'.fromReal \"from reals\"\n"
                           "  input Real re \"re in\";\n  input Real im = 0.0 \"im in\";\n  output C result \"result\";\n"
                           "algorithm\nend C.'constructor'.fromReal;\n\n"
                           "function P.CV \"Automatically generated record constructor for P.CV\"\n  input Real re;\n"
                           "  input Real im;\n  output CV res;\nend P.CV;\n\nfunction P.CV.'*'.multiply \"mul\"\n  input C c1;\n"
                           "  input C c2;\n  output C c3;\nalgorithm\n"
                           "  c3 := C.'constructor'.fromReal(c1.re * c2.re - c1.im * c2.im, c1.re * c2.im + c1.im * c2.re);\n"
                           "end P.CV.'*'.multiply;\n\nfunction P.CV.'constructor'.fromReal \"from reals\"\n"
                           "  input Real re \"re in\";\n  input Real im = 0.0 \"im in\";\n  output C result \"result\";\n"
                           "algorithm\nend P.CV.'constructor'.fromReal;\n\nfunction P.power \"sum of real products\"\n"
                           "  input P.CV[:] v;\n  input C[size(v, 1)] i;\n  output Real p;\nalgorithm\n"
                           "  p := sum(P.re(P.CV.'*'.multiply(v[(1:size(v, 1))[$i1]], i[(1:size(v, 1))[$i1]])) for $i1 in 1:size(v, 1));\n"
                           "end P.power;\n\nfunction P.re \"real part\"\n  input C c;\n  output Real r;\nalgorithm\n"
                           "  r := c.re;\nend P.re;\n\nfunction P.rms \"rms\"\n  input C[:] u;\n  output Real y;\n"
                           "  protected Integer m = size(u, 1);\nalgorithm\n"
                           "  y := sum(sqrt(u[k].re ^ 2.0 + u[k].im ^ 2.0) for k in 1:m) / /*Real*/(m);\nend P.rms;\n\n"
                           "class RZU1\n  Real v[1].re(quantity = \"V\", unit = \"V\") \"real part\";\n"
                           "  Real v[1].im(quantity = \"V\", unit = \"V\") \"imaginary part\";\n"
                           "  Real v[2].re(quantity = \"V\", unit = \"V\") \"real part\";\n"
                           "  Real v[2].im(quantity = \"V\", unit = \"V\") \"imaginary part\";\n  Real i[1].re \"real part\";\n"
                           "  Real i[1].im \"imaginary part\";\n  Real i[2].re \"real part\";\n"
                           "  Real i[2].im \"imaginary part\";\n  Real p = P.power(v, i);\n  Real r = P.rms(i);\nequation\n"
                           "  v[1] = C.'constructor'.fromReal(time, 1.0);\n  v[2] = C.'constructor'.fromReal(1.0, time);\n"
                           "  i[1] = C.'constructor'.fromReal(2.0, time);\n  i[2] = C.'constructor'.fromReal(time, 2.0);\n"
                           "end RZU1;\n"));
}

YMC_TEST(flatten_record_array_function_evaluation) {
  // 返回记录数组的函数作 final parameter 记录数组的整体绑定：实参是结构参数 → 求值后逐元素拆字段；函数体里范围作数组运算数
  // （`(0:n - 1)*integer(m/n) + ones(n)`）、输出维里的调用在调用处求值（`ind[nb(m)]`）；Integer 赋给 Real 局部存成 Real
  // （`q := -o` 逐元素取负得 -0.0）。期望 = omc 1.25.0 实跑（2026-10-09，探针 rz-ut）。
  YMC_CHECK_EQ(flatten_src("record R \"pair\"\n  Real re \"first\";\n  Real im \"second\";\nend R;\nfunction nb \"base count\"\n"
                           "  input Integer m;\n  output Integer n;\nalgorithm\n  n := if mod(m, 2) == 0 then 2 else 1;\n"
                           "end nb;\nfunction idx \"indices\"\n  input Integer m;\n  output Integer ind[nb(m)];\nprotected\n"
                           "  Integer n = nb(m);\nalgorithm\n  ind := (0:n - 1)*integer(m/n) + ones(n);\nend idx;\n"
                           "function tm \"matrix\"\n  input Integer m;\n  output R t[m, m];\nprotected\n  Real o[m];\n"
                           "  Real q[m];\nalgorithm\n  o[1] := 0;\n  for k in 2:m loop\n    o[k] := k;\n  end for;\n  q := -o;\n"
                           "  for i in 1:m loop\n    for j in 1:m loop\n      t[i, j] := R(i * o[j], q[i]);\n    end for;\n"
                           "  end for;\nend tm;\nmodel RZU2\n  parameter Integer m = 2 annotation(Evaluate = true);\n"
                           "  final parameter R t[m, m] = tm(m);\n  final parameter Integer ip[:] = idx(m);\nend RZU2;\n"),
               std::string("class RZU2\n  final parameter Integer m = 2;\n  final parameter Real t[1,1].re = 0.0 \"first\";\n"
                           "  final parameter Real t[1,1].im = -0.0 \"second\";\n"
                           "  final parameter Real t[1,2].re = 2.0 \"first\";\n"
                           "  final parameter Real t[1,2].im = -0.0 \"second\";\n"
                           "  final parameter Real t[2,1].re = 0.0 \"first\";\n"
                           "  final parameter Real t[2,1].im = -2.0 \"second\";\n"
                           "  final parameter Real t[2,2].re = 4.0 \"first\";\n"
                           "  final parameter Real t[2,2].im = -2.0 \"second\";\n  final parameter Integer ip[1] = 1;\n"
                           "  final parameter Integer ip[2] = 2;\nend RZU2;\n"));
}

YMC_TEST(flatten_external_string_function_constants) {
  // 外部 C 字符串函数（ModelicaStrings_substring / ModelicaStrings_length）：常量绑定照 omc 的 Ceval（FFI 实调库函数）就地
  // 求值，向前引用的常量（`R1(start = if c1 == "D" …)`）同；endIndex 越过串尾照 omc 取到串尾（`substr(g, 3, 9)` → "01"）；
  // 参数绑定不求（未声明纯度的外部函数是 impure），函数照打 `impure function`；常量作维。期望 = omc 1.25.0 实跑
  // （2026-10-09，探针 xc-ut）。
  YMC_CHECK_EQ(flatten_src("function substr \"substring\"\n  input String string;\n  input Integer startIndex;\n"
                           "  input Integer endIndex;\n  output String result;\n"
                           "external \"C\" result = ModelicaStrings_substring(string, startIndex, endIndex) annotation(Library=\"ModelicaExternalC\");\n"
                           "end substr;\nfunction len \"length\"\n  input String string;\n  output Integer result;\n"
                           "external \"C\" result = ModelicaStrings_length(string) annotation(Library=\"ModelicaExternalC\");\n"
                           "end len;\nmodel XCU1\n  constant String g = \"Dy01\";\n"
                           "  parameter Real R1(start = if c1 == \"D\" then 1 else 3) = 2;\n"
                           "  constant String c1 = substr(g, 1, 1);\n  constant String c2 = substr(g, 3, 9);\n"
                           "  constant Integer n = len(g);\n  parameter String p = substr(g, 2, 2);\n"
                           "  Real x[len(g)] = fill(time, n);\nend XCU1;\n"),
               std::string("impure function substr \"substring\"\n  input String string;\n  input Integer startIndex;\n"
                           "  input Integer endIndex;\n  output String result;\n\n"
                           "  external \"C\" result = ModelicaStrings_substring(string, startIndex, endIndex);\nend substr;\n\n"
                           "class XCU1\n  constant String g = \"Dy01\";\n  parameter Real R1(start = 1.0) = 2.0;\n"
                           "  constant String c1 = \"D\";\n  constant String c2 = \"01\";\n  constant Integer n = 4;\n"
                           "  parameter String p = substr(\"Dy01\", 2, 2);\n  Real x[1];\n  Real x[2];\n  Real x[3];\n"
                           "  Real x[4];\nequation\n  x = {time, time, time, time};\nend XCU1;\n"));
}

YMC_TEST(flatten_record_extends_modifier_defaults) {
  // 记录类 `extends Base(field = …)`：构造函数的输入缺省取 extends 修饰（`input Real rho = 995.6`），多层 extends 外层优先，
  // 属性子修饰不影响构造；构造调用求值（`P.Cold()` → rho = 999.7）、带实参的构造补齐缺省（`P.Water(995.6, time, 3.0)`）。
  // 期望 = omc 1.25.0 实跑（2026-10-09，探针 rq-ut）。
  YMC_CHECK_EQ(flatten_src("package P\n  record Base \"base data\"\n    parameter Real rho = 1 \"density\";\n"
                           "    parameter Real cp = 2;\n    parameter Real x(unit = \"m\") = 3;\n  end Base;\n"
                           "  record Water \"water\"\n    extends Base(rho = 995.6, cp(min = 0) = 4177);\n  end Water;\n"
                           "  record Cold \"cold water\"\n    extends Water(rho = 999.7);\n  end Cold;\n  function f\n"
                           "    input Water b;\n    input Real t;\n    output Real y;\n  algorithm\n    y := b.rho * t + b.cp;\n"
                           "  end f;\nend P;\nmodel RQU1\n  parameter P.Base m = P.Cold();\n  P.Water w;\n"
                           "  Real y = P.f(P.Water(cp = time), time);\n  Real z = P.f(w, time);\nend RQU1;\n"),
               std::string("function P.Water \"Automatically generated record constructor for P.Water\"\n"
                           "  input Real rho = 995.6;\n  input Real cp = 4177.0;\n  input Real x = 3.0;\n  output Water res;\n"
                           "end P.Water;\n\nfunction P.f\n  input P.Water b;\n  input Real t;\n  output Real y;\nalgorithm\n"
                           "  y := b.rho * t + b.cp;\nend P.f;\n\nclass RQU1\n  parameter Real m.rho = 999.7 \"density\";\n"
                           "  parameter Real m.cp = 4177.0;\n  parameter Real m.x(unit = \"m\") = 3.0;\n"
                           "  parameter Real w.rho = 995.6 \"density\";\n  parameter Real w.cp(min = 0.0) = 4177.0;\n"
                           "  parameter Real w.x(unit = \"m\") = 3.0;\n  Real y = P.f(P.Water(995.6, time, 3.0), time);\n"
                           "  Real z = P.f(w, time);\nend RQU1;\n"));
}

YMC_TEST(flatten_function_vectorized_builtins) {
  // 函数体里的元素式内建向量化（MSL SpacePhasors.ToSpacePhasor 复刻）：`cos(+phi)` → `array(cos(phi[$i1]) for $i1 in 1:m)`；
  // 数组字面量体的推导式；标量乘行长未知的字面量不展开、照 toDAE 换位（`{…} * 2.0 / /*Real*/(m)`）；作调用 `g(m)` 输出维实参的
  // parameter 局部打 `final parameter`。期望 = omc 1.25.0 实跑（2026-10-09，探针 tv-ut）。
  YMC_CHECK_EQ(flatten_src("model TVU1\n  function g \"orientation\"\n    input Integer n;\n    output Real o[n];\n  algorithm\n"
                           "    o := {(k - 1) * 2 * 3.141592653589793 / n for k in 1:n};\n  end g;\n"
                           "  function toSP \"space phasor\"\n    input Real x[:];\n    output Real y[2];\n    output Real y0;\n"
                           "  protected\n    parameter Integer m = size(x, 1) annotation(Evaluate = true);\n"
                           "    parameter Real phi[m] = g(m);\n    parameter Real T[2, m] = 2 / m * {+cos(+phi), +sin(+phi)};\n"
                           "    parameter Real IT[m, 2] = {{+cos(-phi[k]), -sin(-phi[k])} for k in 1:m};\n  algorithm\n"
                           "    y := T * x;\n    y0 := 1 / m * sum(x);\n  end toSP;\n"
                           "  Real u[3] = {sin(time), cos(time), time};\n  Real v[2];\n  Real v0;\nequation\n"
                           "  (v, v0) = toSP(u);\nend TVU1;\n"),
               std::string("function TVU1.g \"orientation\"\n  input Integer n;\n  output Real[n] o;\nalgorithm\n"
                           "  o := array(/*Real*/((k - 1) * 2) * 3.141592653589793 / /*Real*/(n) for k in 1:n);\nend TVU1.g;\n\n"
                           "function TVU1.toSP \"space phasor\"\n  input Real[:] x;\n  output Real[2] y;\n  output Real y0;\n"
                           "  protected final parameter Integer m = size(x, 1);\n"
                           "  protected parameter Real[m] phi = TVU1.g(m);\n"
                           "  protected parameter Real[2, m] T = {array(cos(phi[$i1]) for $i1 in 1:m), array(sin(phi[$i1]) for $i1 in 1:m)} * 2.0 / /*Real*/(m);\n"
                           "  protected parameter Real[m, 2] IT = array({cos(-phi[k]), -sin(-phi[k])} for k in 1:m);\n"
                           "algorithm\n  y := T * x;\n  y0 := 1.0 / /*Real*/(m) * sum(x);\nend TVU1.toSP;\n\nclass TVU1\n"
                           "  Real u[1];\n  Real u[2];\n  Real u[3];\n  Real v[1];\n  Real v[2];\n  Real v0;\nequation\n"
                           "  u = {sin(time), cos(time), time};\n  (v, v0) = TVU1.toSP(u);\nend TVU1;\n"));
}

YMC_TEST(flatten_dead_branch_out_of_range_subscript) {
  // 被字面量条件剪掉的 if 分支里的越界下标不算错（omc 的越界检查 VerifyModel 在化简之后）：for 展开后 i = 1 的
  // `inPort[0].occupied`、`x[0]` / `x[-1]` 所在的支被剪掉，活的支照常。期望 = omc 1.25.0 实跑（2026-10-09，探针 db-ut）。
  YMC_CHECK_EQ(flatten_src("model DBU1\n  connector P\n    Boolean occupied;\n    Boolean set;\n  end P;\n"
                           "  parameter Integer nIn = 2;\n  P inPort[nIn];\n  Boolean localActive = time > 0.5;\n"
                           "  Real x[3] = {time, 2 * time, 3 * time};\n  Real y[3];\nequation\n  for i in 1:nIn loop\n"
                           "    inPort[i].occupied = if i == 1 then localActive else inPort[i - 1].occupied or inPort[i - 1].set;\n"
                           "    inPort[i].set = false;\n  end for;\n  for i in 1:3 loop\n"
                           "    y[i] = if i == 1 then 0 elseif i == 2 then x[i - 1] else x[i - 2] + x[i];\n  end for;\n"
                           "end DBU1;\n"),
               std::string("class DBU1\n  final parameter Integer nIn = 2;\n  Boolean inPort[1].occupied;\n"
                           "  Boolean inPort[1].set;\n  Boolean inPort[2].occupied;\n  Boolean inPort[2].set;\n"
                           "  Boolean localActive = time > 0.5;\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y[1];\n"
                           "  Real y[2];\n  Real y[3];\nequation\n  x = {time, 2.0 * time, 3.0 * time};\n"
                           "  inPort[1].occupied = localActive;\n  inPort[1].set = false;\n"
                           "  inPort[2].occupied = inPort[1].occupied or inPort[1].set;\n  inPort[2].set = false;\n"
                           "  y[1] = 0.0;\n  y[2] = x[1];\n  y[3] = x[1] + x[3];\nend DBU1;\n"));
}

YMC_TEST(flatten_pure_function_marked_impure) {
  // 纯函数体里顶层调 impure 函数（`g(x);`）→ omc 把它标成 impure（checkPureCall，NFFunction.mo:1643-1681）：打
  // `impure function`、模型里常量实参的调用不再折叠；嵌在运算里的（h 的 `f(x) + 1`）不标、照常求值（体里的 impure 调用
  // 一并求）。when 体里的元组方程整条保留。期望 = omc 1.25.0 实跑（2026-10-09，探针 ip-ut）。
  YMC_CHECK_EQ(flatten_src("model IPU1\n  impure function g\n    input Real x;\n  algorithm\n    assert(x > 0, \"x\");\n"
                           "  end g;\n  function f \"top-level impure call: marked impure\"\n    input Real x;\n"
                           "    output Real y;\n    output Real z;\n  algorithm\n    g(x);\n    y := x + 1;\n    z := 2 * x;\n"
                           "  end f;\n  function h \"impure call nested in an expression: stays pure\"\n    input Real x;\n"
                           "    output Real y;\n  algorithm\n    y := f(x) + 1;\n  end h;\n  parameter Real p = f(1.0);\n"
                           "  parameter Real q = h(1.0);\n  discrete Real a, b;\nequation\n  when sample(0, 0.1) then\n"
                           "    (a, b) = f(time);\n  end when;\nend IPU1;\n"),
               std::string("impure function IPU1.f \"top-level impure call: marked impure\"\n  input Real x;\n  output Real y;\n"
                           "  output Real z;\nalgorithm\n  IPU1.g(x);\n  y := x + 1.0;\n  z := 2.0 * x;\nend IPU1.f;\n\n"
                           "impure function IPU1.g\n  input Real x;\nalgorithm\n  assert(x > 0.0, \"x\");\nend IPU1.g;\n\n"
                           "class IPU1\n  parameter Real p = IPU1.f(1.0)[1];\n  parameter Real q = 3.0;\n  discrete Real a;\n"
                           "  discrete Real b;\nequation\n  when sample(0.0, 0.1) then\n    (a, b) = IPU1.f(time);\n"
                           "  end when;\nend IPU1;\n"));
}

YMC_TEST(flatten_record_value_foreign_base) {
  // 派生记录的构造调用作记录组件的修饰、基类在别的包里（MSL FluxTubes 材料记录）：值按字段求，基类字段的缺省在基类自己的
  // 作用域里求（只在 Material 包里可见的常量 k、`B = 2 * mu_i`），extends 修饰盖过缺省。期望 = omc 1.25.0 实跑
  //（2026-10-10，探针 fr-ut）。
  YMC_CHECK_EQ(flatten_src("package FRBU1P\n  package Material\n    constant Real k = 2.5;\n    record Base\n"
                           "      parameter Real mu_i = k;\n      parameter Real B = 2 * mu_i;\n    end Base;\n"
                           "    package Steels\n      record Steel\n        extends Material.Base(B = 1.5);\n      end Steel;\n"
                           "      record Iron\n        extends Material.Base;\n      end Iron;\n    end Steels;\n"
                           "  end Material;\n  model Tube\n    parameter Material.Base material = Material.Base();\n"
                           "    Real y = material.mu_i * material.B * time;\n  end Tube;\nend FRBU1P;\nmodel FRBU1\n"
                           "  FRBU1P.Tube t(material = FRBU1P.Material.Steels.Steel());\n"
                           "  FRBU1P.Tube t2(material = FRBU1P.Material.Steels.Iron());\nend FRBU1;\n"),
               std::string("class FRBU1\n  parameter Real t.material.mu_i = 2.5;\n  parameter Real t.material.B = 1.5;\n"
                           "  Real t.y = t.material.mu_i * t.material.B * time;\n  parameter Real t2.material.mu_i = 2.5;\n"
                           "  parameter Real t2.material.B = 5.0;\n  Real t2.y = t2.material.mu_i * t2.material.B * time;\n"
                           "end FRBU1;\n"));
}

YMC_TEST(flatten_constant_record_reference_literal) {
  // 整个常量记录的引用作实参（`constant K C;` 无绑定取字段缺省）→ 记录式 `CRLU1P.K(1.0359431399069999e-10, …)`；Evaluate =
  // true 的 final 记录参数由函数求值成字段字面量，读到的 x0 连带成结构参数（final）。期望 = omc 1.25.0 实跑（2026-10-10，
  // 探针 cr-ut）。
  YMC_CHECK_EQ(flatten_src("package CRLU1P\n  record K\n    constant Real EPS = 11.7 * 8.854214871e-12;\n"
                           "    constant Real CHARGE = 1.6021918e-19;\n  end K;\n  record LP\n    Real a;\n    Real b;\n"
                           "  end LP;\n  function f\n    input Real x;\n    input K k;\n    output Real y;\n  algorithm\n"
                           "    y := x * k.CHARGE + k.EPS;\n  end f;\n  function ren\n    input Real x;\n    output LP r;\n"
                           "  algorithm\n    r.a := x;\n    r.b := 2 * x;\n  end ren;\nend CRLU1P;\nmodel CRLU1\n"
                           "  constant CRLU1P.K C;\n  Real z = CRLU1P.f(time, C);\n  parameter Real x0 = 0.7;\n"
                           "  final parameter CRLU1P.LP p = CRLU1P.ren(x0) annotation(Evaluate = true);\n"
                           "  Real y = p.a * time;\nend CRLU1;\n"),
               std::string("function CRLU1P.K \"Automatically generated record constructor for CRLU1P.K\"\n"
                           "  input Real EPS = 1.0359431399069999e-10;\n  input Real CHARGE = 1.6021918e-19;\n  output K res;\n"
                           "end CRLU1P.K;\n\nfunction CRLU1P.f\n  input Real x;\n  input CRLU1P.K k;\n  output Real y;\n"
                           "algorithm\n  y := x * k.CHARGE + k.EPS;\nend CRLU1P.f;\n\nclass CRLU1\n"
                           "  constant Real C.EPS = 1.0359431399069999e-10;\n  constant Real C.CHARGE = 1.6021918e-19;\n"
                           "  Real z = CRLU1P.f(time, CRLU1P.K(1.0359431399069999e-10, 1.6021918e-19));\n"
                           "  final parameter Real x0 = 0.7;\n  final parameter Real p.a = 0.7;\n"
                           "  final parameter Real p.b = 1.4;\n  Real y = p.a * time;\nend CRLU1;\n"));
}

YMC_TEST(flatten_record_parameter_whole_evaluation) {
  // 整个记录参数作实参的编译期求值：Evaluate = true 的 final 记录 `p = ren(mc)`，ren 体里元组赋值给记录字段、死的 if 语句被删；
  // p 的字段只有带 start 的 `a` 成结构参数（引用处代入），mc 的字段不 final。期望 = omc 1.25.0 实跑（2026-10-10，探针 ws-ut）。
  YMC_CHECK_EQ(flatten_src("package WSU1P\n  record MC\n    parameter Real VTO = -1e40;\n    parameter Real TNOM = -1e40;\n"
                           "  end MC;\n  record LP\n    Real a(start = 0.0);\n    Real b;\n  end LP;\n  function g2\n"
                           "    input Real x;\n    output Real p;\n    output Real q;\n  algorithm\n    p := x;\n"
                           "    q := 2 * x;\n  end g2;\n  function ren\n    input MC ex;\n    output LP r;\n  algorithm\n"
                           "    (r.a, r.b) := g2(ex.VTO);\n    if false then\n      r.b := 0;\n    end if;\n  end ren;\n"
                           "end WSU1P;\nmodel WSU1\n  parameter WSU1P.MC mc(VTO = 0.7);\n"
                           "  final parameter WSU1P.LP p = WSU1P.ren(mc) annotation(Evaluate = true);\n"
                           "  Real y = p.a * time + p.b;\nend WSU1;\n"),
               std::string("class WSU1\n  parameter Real mc.VTO = 0.7;\n  parameter Real mc.TNOM = -1e40;\n"
                           "  final parameter Real p.a(start = 0.0) = 0.7;\n  final parameter Real p.b = 1.4;\n"
                           "  Real y = 0.7 * time + p.b;\nend WSU1;\n"));
}

YMC_TEST(flatten_inherited_class_and_function_lookup) {
  // 继承来的嵌套类作组件类型、继承来的函数 / 常量 / 短类型在派生类里引用（ClassTree.expand：继承元素进派生类的查找表）；函数的
  // 压平名 = 访问它的实例路径 `ILU1.a.f`。期望 = omc 1.25.0 实跑（2026-10-10，探针 il-p1 的 IL1 / IL2 合并）。
  YMC_CHECK_EQ(flatten_src("model ILU1\n  model Base\n    model B\n      Real x = 1;\n    end B;\n    function f\n"
                           "      input Real x;\n      output Real y;\n    algorithm\n      y := 2 * x;\n    end f;\n"
                           "    constant Real k = 3;\n    type T = Real(unit = \"m\");\n  end Base;\n  model A\n"
                           "    extends Base;\n    B b;\n    Real y = f(time) + k;\n    T z = time;\n  end A;\n  A a;\n"
                           "end ILU1;\n"),
               std::string("function ILU1.a.f\n  input Real x;\n  output Real y;\nalgorithm\n  y := 2.0 * x;\n"
                           "end ILU1.a.f;\n\nclass ILU1\n  constant Real a.k = 3.0;\n  Real a.b.x = 1.0;\n"
                           "  Real a.y = ILU1.a.f(time) + 3.0;\n  Real a.z(unit = \"m\") = time;\nend ILU1;\n"));
  // 外层实例上被改过的常量被嵌套类读到（omc 读改过的 3，ymc 的 SCode 词法查找只有声明值 1）→ SKIP（此前静默出 x[1]）。
  const std::string t = flatten_src("model ILU2\n  model A\n    constant Integer n = 1;\n    model B\n"
                                    "      Real x[n] = fill(1, n);\n    end B;\n    B b;\n  end A;\n  A a(n = 3);\nend ILU2;\n");
  YMC_CHECK_EQ(t, std::string("<unsupported:constant of modified enclosing instance>"));
}

YMC_TEST(flatten_short_class_alias_root) {
  // 根类是短类别名：链底的长类照常压平，压平名 / 注释取别名，两层别名的修饰外层在前（final 照打）。期望 = omc 1.25.0 实跑
  //（2026-10-10，探针 sa-ut）。
  YMC_CHECK_EQ(flatten_src("model SAU1A\n  parameter Real p = 1;\n  parameter Real q = 2;\n  Real x;\nequation\n"
                           "  x = p * q * time;\nend SAU1A;\nmodel SAU1B = SAU1A(p = 3);\n"
                           "model SAU1 = SAU1B(final q = 4, x(start = 5)) \"alias root\";\n"),
               std::string("class SAU1 \"alias root\"\n  parameter Real p = 3.0;\n  final parameter Real q = 4.0;\n"
                           "  Real x(start = 5.0);\nequation\n  x = p * q * time;\nend SAU1;\n"));
}

YMC_TEST(flatten_constrainedby_modifier) {
  // 约束子句的修饰排在声明修饰之后（外来 > 声明 > 约束，instElementModifier）；自带约束子句的重声明，新约束修饰在原约束修饰之前。
  // 期望 = omc 1.25.0 实跑（2026-10-10，探针 cb-ut）。
  YMC_CHECK_EQ(flatten_src("model CBU1\n  model A\n    replaceable Real x(start = 3) constrainedby Real(start = 1, min = 0);\n"
                           "    replaceable Real y constrainedby Real(start = 4);\n  end A;\n"
                           "  A a(x(start = 5), redeclare replaceable Real y(min = 1) constrainedby Real(start = 6));\nend CBU1;\n"),
               std::string("class CBU1\n  Real a.x(min = 0.0, start = 5.0);\n  Real a.y(min = 1.0, start = 6.0);\nend CBU1;\n"));
}

YMC_TEST(flatten_element_class_redeclare) {
  // 派生类里的元素级类重声明顶替两层之下基类里的 replaceable 类（继承来的组件 b 的类型查到新类 X(p = 3)，短类修饰在派生类里解析）；
  // 同一基类另有不重声明的实例 a 照用原类。期望 = omc 1.25.0 实跑（2026-10-10，探针 kr-ut）。
  YMC_CHECK_EQ(flatten_src("model KRU1\n  model X\n    parameter Real p = 1;\n    Real z = p;\n  end X;\n  model A\n"
                           "    replaceable model B\n      Real x = 1;\n    end B;\n    B b;\n    Real w = 4;\n  end A;\n"
                           "  model A2\n    extends A;\n  end A2;\n  model C\n    extends A2;\n    redeclare model B = X(p = 3);\n"
                           "  end C;\n  A a;\n  C c;\nend KRU1;\n"),
               std::string("class KRU1\n  Real a.b.x = 1.0;\n  Real a.w = 4.0;\n  parameter Real c.b.p = 3.0;\n"
                           "  Real c.b.z = c.b.p;\n  Real c.w = 4.0;\nend KRU1;\n"));
}

YMC_TEST(flatten_modifier_class_redeclare) {
  // 修饰里的类重声明：extends 修饰（新类修饰引写修饰的类的参数 q）、组件声明修饰、短类修饰，各施加到对应实例的类树。
  // 期望 = omc 1.25.0 实跑（2026-10-10，探针 km-ut）。
  YMC_CHECK_EQ(flatten_src("model KMU1\n  model X\n    parameter Real p = 1;\n    Real z = p;\n  end X;\n  model A\n"
                           "    replaceable model B\n      Real x = 1;\n    end B;\n    B b;\n  end A;\n  model C\n"
                           "    parameter Real q = 6;\n    extends A(redeclare model B = X(p = q));\n  end C;\n"
                           "  model A2 = A(redeclare model B = X(p = 4));\n  C c;\n  A a(redeclare model B = X);\n  A2 a2;\n"
                           "end KMU1;\n"),
               std::string("class KMU1\n  parameter Real c.q = 6.0;\n  parameter Real c.b.p = c.q;\n  Real c.b.z = c.b.p;\n"
                           "  parameter Real a.b.p = 1.0;\n  Real a.b.z = a.b.p;\n  parameter Real a2.b.p = 4.0;\n"
                           "  Real a2.b.z = a2.b.p;\nend KMU1;\n"));
}

YMC_TEST(flatten_class_extends_redeclare) {
  // class extends：新类隐式 extends 经中间基类继承来的原类（原类元素在前），class extends 的修饰 `(x = y)` 施加到原类元素；
  // 基类实例与派生类自己的组件都用新类。期望 = omc 1.25.0 实跑（2026-10-10，探针 kx-ut）。
  YMC_CHECK_EQ(flatten_src("model KXU1\n  model A\n    replaceable model B\n      Real x;\n      Real y;\n    end B;\n"
                           "    B b;\n  end A;\n  model A2\n    extends A;\n  end A2;\n  model C\n    extends A2;\n"
                           "    redeclare model extends B(x = y)\n      Real z = 3;\n    equation\n      y = z;\n    end B;\n"
                           "    B b2;\n  end C;\n  C c;\nend KXU1;\n"),
               std::string("class KXU1\n  Real c.b.x = c.b.y;\n  Real c.b.y;\n  Real c.b.z = 3.0;\n  Real c.b2.x = c.b2.y;\n"
                           "  Real c.b2.y;\n  Real c.b2.z = 3.0;\nequation\n  c.b.y = c.b.z;\n  c.b2.y = c.b2.z;\nend KXU1;\n"));
}

YMC_TEST(flatten_inner_outer_combined) {
  // inner outer：子层 outer 连的是它自己（m.l.y = 2.0 * m.T），本层与从外面经它查名都落到外层 inner（`T + 1` 的 T、`b = m.T`
  // → `b = T`）；constant 两头照常内联（m.b 取外层的 3、m.l.a 取它自己的 6）。期望 = omc 1.25.0 实跑（2026-10-11，探针 iy-ut）。
  YMC_CHECK_EQ(flatten_src("model IYU1\n  model Leaf\n    outer Real T;\n    Real y = 2 * T;\n  end Leaf;\n  model Mid\n"
                           "    inner outer Real T = T + 1;\n    Leaf l;\n  end Mid;\n  inner Real T = time;\n  Mid m;\n"
                           "  Real b = m.T;\nend IYU1;\n"),
               std::string("class IYU1\n  Real T = time;\n  Real m.T = T + 1.0;\n  Real m.l.y = 2.0 * m.T;\n  Real b = T;\n"
                           "end IYU1;\n"));
  YMC_CHECK_EQ(flatten_src("model IYU2\n  model L\n    outer constant Real c;\n    Real a = c;\n  end L;\n  model M\n"
                           "    inner outer constant Real c = 2 * c;\n    Real b = c;\n    L l;\n  end M;\n"
                           "  inner constant Real c = 3;\n  M m;\nend IYU2;\n"),
               std::string("class IYU2\n  constant Real c = 3.0;\n  constant Real m.c = 6.0;\n  Real m.b = 3.0;\n"
                           "  Real m.l.a = 6.0;\nend IYU2;\n"));
}

YMC_TEST(flatten_element_component_redeclare) {
  // 元素级组件重声明：留在本类写它的位置（c 之后），原声明的修饰（start）不带、extends 修饰只带绑定（7）；取自原声明的条件在
  // 原声明处求（P.on = false，派生类外层的 on = true 不算）——元素级的 b.x、修饰里重声明的 c.x 都移除（后者此前静默留着）。
  // 期望 = omc 1.25.0 实跑（2026-10-11，探针 el-ut）。
  YMC_CHECK_EQ(flatten_src("model ELRU1\n  model A\n    Real a = 1;\n    replaceable Real x(start = 1) = 1.0;\n    Real b = 2;\n"
                           "  end A;\n  model B\n    extends A(x = 7);\n    Real c = 3;\n    redeclare Real x(max = 5);\n  end B;\n"
                           "  B b;\nend ELRU1;\n"),
               std::string("class ELRU1\n  Real b.a = 1.0;\n  Real b.b = 2.0;\n  Real b.c = 3.0;\n  Real b.x(max = 5.0) = 7.0;\n"
                           "end ELRU1;\n"));
  YMC_CHECK_EQ(flatten_src("package P\n  constant Boolean on = false;\n  model A\n    replaceable Real x = 1.0 if on;\n"
                           "    Real y = 1;\n  end A;\nend P;\nmodel ELRU2\n  constant Boolean on = true;\n  model B\n"
                           "    extends P.A;\n    redeclare Real x = 2.0;\n  end B;\n  model C\n"
                           "    extends P.A(redeclare Real x = 3.0);\n  end C;\n  B b;\n  C c;\nend ELRU2;\n"),
               std::string("class ELRU2\n  constant Boolean on = true;\n  Real b.y = 1.0;\n  Real c.y = 1.0;\nend ELRU2;\n"));
}

YMC_TEST(flatten_record_binding_evaluation_rules) {
  // 整体绑定的记录：字段自己的 Evaluate = true 只看 start（c2.Idis 不 final、引用处不代入；此前静默错）；下标是 Integer 参数数组
  //（c 成结构参数、`y[1] = table[1,2]`）；if 方程条件里整体绑定的记录的继承来的字段不算固定、照留 if 方程。期望 = omc 1.25.0
  // 实跑（2026-10-11，探针 bx-ut）。
  YMC_CHECK_EQ(flatten_src("model BXU1\n  record R\n    parameter Real Idis = 0 annotation(Evaluate = true);\n"
                           "    parameter Real q = 1;\n  end R;\n  parameter R cd(Idis = 0.001);\n  parameter R c2 = cd;\n"
                           "  Real g = c2.Idis * 2;\nend BXU1;\n"),
               std::string("class BXU1\n  final parameter Real cd.Idis = 0.001;\n  parameter Real cd.q = 1.0;\n"
                           "  parameter Real c2.Idis = 0.001;\n  parameter Real c2.q = cd.q;\n  Real g = c2.Idis * 2.0;\n"
                           "end BXU1;\n"));
  YMC_CHECK_EQ(flatten_src("model BXU2\n  parameter Real table[2, 2] = [0, 1; 1, 2];\n  parameter Integer c[1] = {2};\n"
                           "  Real y[1];\nequation\n  y = table[1, c];\nend BXU2;\n"),
               std::string("class BXU2\n  parameter Real table[1,1] = 0.0;\n  parameter Real table[1,2] = 1.0;\n"
                           "  parameter Real table[2,1] = 1.0;\n  parameter Real table[2,2] = 2.0;\n"
                           "  final parameter Integer c[1] = 2;\n  Real y[1];\nequation\n  y[1] = table[1,2];\nend BXU2;\n"));
  YMC_CHECK_EQ(flatten_src("model BXU3\n  record R0\n    parameter Integer s = 1;\n  end R0;\n  record R1\n    extends R0;\n"
                           "  end R1;\n  model B\n    replaceable parameter R0 cellData;\n    Real y;\n  equation\n"
                           "    if cellData.s == 1 then\n      y = 1;\n    else\n      y = 2;\n    end if;\n  end B;\n"
                           "  model C\n    extends B(redeclare R1 cellData);\n  end C;\n  parameter R1 cd(s = 2);\n"
                           "  C c(cellData = cd);\nend BXU3;\n"),
               std::string("class BXU3\n  parameter Integer cd.s = 2;\n  parameter Integer c.cellData.s = cd.s;\n  Real c.y;\n"
                           "equation\n  if c.cellData.s == 1 then\n    c.y = 1.0;\n  else\n    c.y = 2.0;\n  end if;\n"
                           "end BXU3;\n"));
}

YMC_TEST(flatten_distributed_alias_and_negative_zero) {
  // 组件数组修饰分发的结构参数别名连带：看分发前的原式是不是 cref（`k = r`、`k = r[1:2]` 连带，`k = {s, s}` 不连带），时刻按
  // 共享节点的定型序（目标在后面的绑定里才被标 → 前面的不连带）；负零：书写的 `-0.0` 不护零，Integer / Real 混排的数组取负
  // 得 `-0.0`，Real 目标的 `-{0, 1}` 先转型再取负。期望 = omc 1.25.0 实跑（2026-10-11，探针 dz-ut）。
  YMC_CHECK_EQ(flatten_src("model DZU1\n  block B\n    parameter Real k(start = 1);\n    Real y = k;\n  end B;\n"
                           "  parameter Real r[2] = {1, 2} annotation(Evaluate = true);\n"
                           "  parameter Real s = 3 annotation(Evaluate = true);\n  B a[2](k = r);\n  B c[2](k = {s, s});\n"
                           "  B d[2](k = r[1:2]);\nend DZU1;\n"),
               std::string("class DZU1\n  final parameter Real r[1] = 1.0;\n  final parameter Real r[2] = 2.0;\n"
                           "  final parameter Real s = 3.0;\n  final parameter Real a[1].k(start = 1.0) = 1.0;\n"
                           "  Real a[1].y = 1.0;\n  final parameter Real a[2].k(start = 1.0) = 2.0;\n  Real a[2].y = 2.0;\n"
                           "  parameter Real c[1].k(start = 1.0) = 3.0;\n  Real c[1].y = c[1].k;\n"
                           "  parameter Real c[2].k(start = 1.0) = 3.0;\n  Real c[2].y = c[2].k;\n"
                           "  final parameter Real d[1].k(start = 1.0) = 1.0;\n  Real d[1].y = 1.0;\n"
                           "  final parameter Real d[2].k(start = 1.0) = 2.0;\n  Real d[2].y = 2.0;\nend DZU1;\n"));
  YMC_CHECK_EQ(flatten_src("model DZU2\n  block BI\n    parameter Integer k = 1;\n    Integer y = k;\n  end BI;\n"
                           "  parameter Integer n[2] = {2, 3};\n  BI b[2](k = n);\n  Real z[2] = fill(1.0, n[1]);\n"
                           "  BI e[2](k = n);\nend DZU2;\n"),
               std::string("class DZU2\n  final parameter Integer n[1] = 2;\n  final parameter Integer n[2] = 3;\n"
                           "  parameter Integer b[1].k = 2;\n  Integer b[1].y = b[1].k;\n  parameter Integer b[2].k = 3;\n"
                           "  Integer b[2].y = b[2].k;\n  Real z[1];\n  Real z[2];\n  final parameter Integer e[1].k = 2;\n"
                           "  Integer e[1].y = 2;\n  final parameter Integer e[2].k = 3;\n  Integer e[2].y = 3;\nequation\n"
                           "  z = {1.0, 1.0};\nend DZU2;\n"));
  YMC_CHECK_EQ(flatten_src("model DZU3\n  parameter Real p = 2;\n  parameter Real r[3] = -{0, p, 0} annotation(Evaluate = true);\n"
                           "  parameter Real q[2] = -{0, 1};\n  constant Real c = -0.0;\n  Real v[3] = r;\nend DZU3;\n"),
               std::string("class DZU3\n  final parameter Real p = 2.0;\n  final parameter Real r[1] = -0.0;\n"
                           "  final parameter Real r[2] = -2.0;\n  final parameter Real r[3] = -0.0;\n"
                           "  parameter Real q[1] = -0.0;\n  parameter Real q[2] = -1.0;\n  constant Real c = -0.0;\n"
                           "  Real v[1];\n  Real v[2];\n  Real v[3];\nequation\n  v = {-0.0, -2.0, -0.0};\nend DZU3;\n"));
}

YMC_TEST(flatten_empty_blocks_and_conditional_arrays) {
  // 矩阵构造的空块（TransferFunction 的 `vector([zeros(max(0, na - nb), 1); b])`）；零长切片的方程整条不出；组件数组里的
  // 数组方程照常（含数组调用的整条保留，带下标前缀）；elseif 链里形状不相容的内层 if 由外层选支后按结构参数定；被移除的
  // 条件组件里维用到的参数经修饰绑定连带外面（k final、`j = 3 * k` 代成 3）。期望 = omc 1.25.0 实跑（2026-10-11，探针 mx-ut）。
  YMC_CHECK_EQ(flatten_src("model MXU1\n  parameter Real a[2] = {1, 1};\n  parameter Real b[2] = {-1, 1};\n"
                           "  parameter Integer na = size(a, 1);\n  parameter Integer nb = size(b, 1);\n"
                           "  parameter Real bb[:] = vector([zeros(max(0, na - nb), 1); b]);\n  parameter Real d = bb[1] / a[1];\n"
                           "end MXU1;\n"),
               std::string("class MXU1\n  parameter Real a[1] = 1.0;\n  parameter Real a[2] = 1.0;\n  parameter Real b[1] = -1.0;\n"
                           "  parameter Real b[2] = 1.0;\n  final parameter Integer na = 2;\n  final parameter Integer nb = 2;\n"
                           "  parameter Real bb[1] = b[1];\n  parameter Real bb[2] = b[2];\n  parameter Real d = bb[1] / a[1];\n"
                           "end MXU1;\n"));
  YMC_CHECK_EQ(flatten_src("model MXU2\n  parameter Integer nx = 1;\n  Real x[nx](each start = 1);\ninitial equation\n"
                           "  der(x[2:nx]) = zeros(nx - 1);\nequation\n  der(x[1]) = -x[1];\n  der(x[2:nx]) = x[1:nx - 1];\n"
                           "end MXU2;\n"),
               std::string("class MXU2\n  final parameter Integer nx = 1;\n  Real x[1](start = 1.0);\nequation\n  der(x[1]) = -x[1];\n"
                           "end MXU2;\n"));
  YMC_CHECK_EQ(flatten_src("model MXU3\n  function f\n    input Real u[2];\n    output Real v[2];\n  algorithm\n    v := 2 * u;\n"
                           "  end f;\n  model A\n    Real x[2];\n    Real z[2];\n  equation\n    z = f(x);\n"
                           "    x = {time, 2 * time};\n  end A;\n  A a;\n  A b[2];\nend MXU3;\n"),
               std::string("function MXU3.f\n  input Real[2] u;\n  output Real[2] v;\nalgorithm\n  v := {2.0 * u[1], 2.0 * u[2]};\n"
                           "end MXU3.f;\n\nclass MXU3\n  Real a.x[1];\n  Real a.x[2];\n  Real a.z[1];\n  Real a.z[2];\n"
                           "  Real b[1].x[1];\n  Real b[1].x[2];\n  Real b[1].z[1];\n  Real b[1].z[2];\n  Real b[2].x[1];\n"
                           "  Real b[2].x[2];\n  Real b[2].z[1];\n  Real b[2].z[2];\nequation\n  a.z = MXU3.f(a.x);\n"
                           "  a.x[1] = time;\n  a.x[2] = 2.0 * time;\n  b[1].z = MXU3.f(b[1].x);\n  b[1].x[1] = time;\n"
                           "  b[1].x[2] = 2.0 * time;\n  b[2].z = MXU3.f(b[2].x);\n  b[2].x[1] = time;\n  b[2].x[2] = 2.0 * time;\n"
                           "end MXU3;\n"));
  YMC_CHECK_EQ(flatten_src("package P\n  type K = enumeration(A, B, C);\nend P;\nmodel MXU4\n\n  parameter P.K k = P.K.B;\n"
                           "  parameter Real t[:, :] = if k == P.K.A then Lin elseif k == P.K.B then H else R;\nprotected\n"
                           "  constant Real Lin[:, 2] = [0, 1; 1, 0];\n  constant Real H[:, 2] = [0, 1; 0.5, 0.7; 1, 0];\n"
                           "  constant Real R[:, 2] = [0, 1; 0.2, 0.9; 0.6, 0.4; 1, 0];\nend MXU4;\n"),
               std::string("class MXU4\n  final parameter enumeration(A, B, C) k = P.K.B;\n  parameter Real t[1,1] = 0.0;\n"
                           "  parameter Real t[1,2] = 1.0;\n  parameter Real t[2,1] = 0.5;\n  parameter Real t[2,2] = 0.7;\n"
                           "  parameter Real t[3,1] = 1.0;\n  parameter Real t[3,2] = 0.0;\n"
                           "  protected constant Real Lin[1,1] = 0.0;\n  protected constant Real Lin[1,2] = 1.0;\n"
                           "  protected constant Real Lin[2,1] = 1.0;\n  protected constant Real Lin[2,2] = 0.0;\n"
                           "  protected constant Real H[1,1] = 0.0;\n  protected constant Real H[1,2] = 1.0;\n"
                           "  protected constant Real H[2,1] = 0.5;\n  protected constant Real H[2,2] = 0.7;\n"
                           "  protected constant Real H[3,1] = 1.0;\n  protected constant Real H[3,2] = 0.0;\n"
                           "  protected constant Real R[1,1] = 0.0;\n  protected constant Real R[1,2] = 1.0;\n"
                           "  protected constant Real R[2,1] = 0.2;\n  protected constant Real R[2,2] = 0.9;\n"
                           "  protected constant Real R[3,1] = 0.6;\n  protected constant Real R[3,2] = 0.4;\n"
                           "  protected constant Real R[4,1] = 1.0;\n  protected constant Real R[4,2] = 0.0;\nend MXU4;\n"));
  YMC_CHECK_EQ(flatten_src("model MXU5\n  connector Pl\n    parameter Integer m = 1;\n    Real v[m];\n  end Pl;\n"
                           "  parameter Integer k = 1;\n  parameter Integer j = 3 * k;\n  parameter Boolean use = false;\n"
                           "  Pl p(final m = k) if use;\n  Real y = j;\nend MXU5;\n"),
               std::string("class MXU5\n  final parameter Integer k = 1;\n  parameter Integer j = 3;\n"
                           "  final parameter Boolean use = false;\n  Real y = /*Real*/(j);\nend MXU5;\n"));
}

YMC_TEST(flatten_function_body_vectorization) {
  // 函数体里的向量化调用：切片实参 `u[(1:n)[$i1]]`、范围 `1:n`；二维嵌套 `array(array(… for $i2 in 1:size(m, 2)) for $i1 in
  // 1:size(m, 1))`；when 算法里元组赋值的整体数组目标；函数输入缺省 `fill(0.0, 0)` 折成 `{}`。期望 = omc 1.25.0 实跑
  //（2026-10-11，探针 vb-ut）。
  YMC_CHECK_EQ(flatten_src("model VBU1\n  function g\n    input Real x;\n    output Real y;\n  algorithm\n    y := 2 * x;\n  end g;\n"
                           "  function h\n    input Real x;\n    input Real k;\n    output Real y;\n  algorithm\n    y := k * x;\n"
                           "  end h;\n  function f\n    input Real u[:];\n    input Integer n;\n    output Real a[n];\n  algorithm\n"
                           "    a := g(u[1:n]);\n  end f;\n  Real r[:] = f({time, 2, 3}, 2);\nend VBU1;\n"),
               std::string("function VBU1.f\n  input Real[:] u;\n  input Integer n;\n  output Real[n] a;\nalgorithm\n"
                           "  a := array(VBU1.g(u[(1:n)[$i1]]) for $i1 in 1:n);\nend VBU1.f;\n\nfunction VBU1.g\n  input Real x;\n"
                           "  output Real y;\nalgorithm\n  y := 2.0 * x;\nend VBU1.g;\n\nclass VBU1\n  Real r[1];\n  Real r[2];\n"
                           "equation\n  r = VBU1.f({time, 2.0, 3.0}, 2);\nend VBU1;\n"));
  YMC_CHECK_EQ(flatten_src("model VBU2\n  function g\n    input Real x;\n    output Real y;\n  algorithm\n    y := 2 * x;\n  end g;\n"
                           "  function h\n    input Real x;\n    input Real k;\n    output Real y;\n  algorithm\n    y := k * x;\n"
                           "  end h;\n  function f\n    input Real m[:, :];\n    output Real a[size(m, 1), size(m, 2)];\n  algorithm\n"
                           "    a := g(m);\n  end f;\n  Real r[:, :] = f({{time, 2}, {3, 4}});\nend VBU2;\n"),
               std::string("function VBU2.f\n  input Real[:, :] m;\n  output Real[size(m, 1), size(m, 2)] a;\nalgorithm\n"
                           "  a := array(array(VBU2.g(m[$i1,$i2]) for $i2 in 1:size(m, 2)) for $i1 in 1:size(m, 1));\nend VBU2.f;\n\n"
                           "function VBU2.g\n  input Real x;\n  output Real y;\nalgorithm\n  y := 2.0 * x;\nend VBU2.g;\n\n"
                           "class VBU2\n  Real r[1,1];\n  Real r[1,2];\n  Real r[2,1];\n  Real r[2,2];\nequation\n"
                           "  r = VBU2.f({{time, 2.0}, {3.0, 4.0}});\nend VBU2;\n"));
  YMC_CHECK_EQ(flatten_src("model VBU3\n  function f\n    input Real u[:];\n    input Integer n;\n    output Integer info;\n"
                           "    output Real a[n];\n  algorithm\n    info := 0;\n    a := u[1:n];\n  end f;\n"
                           "  Real y[4](each start = 1, each fixed = true);\n  Real A[2](each start = 0, each fixed = true);\n"
                           "  Integer info(start = 0, fixed = true);\nequation\n  der(y) = zeros(4);\nalgorithm\n"
                           "  when sample(0, 0.1) then\n    (info, A) := f(y, 2);\n  end when;\nend VBU3;\n"),
               std::string("function VBU3.f\n  input Real[:] u;\n  input Integer n;\n  output Integer info;\n  output Real[n] a;\n"
                           "algorithm\n  info := 0;\n  a := u[1:n];\nend VBU3.f;\n\nclass VBU3\n"
                           "  Real y[1](start = 1.0, fixed = true);\n  Real y[2](start = 1.0, fixed = true);\n"
                           "  Real y[3](start = 1.0, fixed = true);\n  Real y[4](start = 1.0, fixed = true);\n"
                           "  Real A[1](start = 0.0, fixed = true);\n  Real A[2](start = 0.0, fixed = true);\n"
                           "  Integer info(start = 0, fixed = true);\nequation\n  der(y[1]) = 0.0;\n  der(y[2]) = 0.0;\n"
                           "  der(y[3]) = 0.0;\n  der(y[4]) = 0.0;\nalgorithm\n  when sample(0.0, 0.1) then\n"
                           "    (info, A) := VBU3.f(y, 2);\n  end when;\nend VBU3;\n"));
  YMC_CHECK_EQ(flatten_src("model VBU4\n  function f\n    input Real u[:];\n    input Real p[:] = fill(0.0, 0);\n    output Real y;\n"
                           "  algorithm\n    y := sum(u) + sum(p);\n  end f;\n  Real y1 = f({time, 2});\nend VBU4;\n"),
               std::string("function VBU4.f\n  input Real[:] u;\n  input Real[:] p = {};\n  output Real y;\nalgorithm\n"
                           "  y := sum(u) + sum(p);\nend VBU4.f;\n\nclass VBU4\n  Real y1 = VBU4.f({time, 2.0}, {});\nend VBU4;\n"));
}

YMC_TEST(flatten_model_vectorization_slices) {
  // 模型语境的向量化调用、实参是范围切片：整条保留 `y = array(g(x[(2:3)[$i1]]) for $i1 in 1:2)`、二维 `m[(1:2)[$i1],$i2]`；
  // 向量化的 previous 不算数组调用、所在等式逐元素展开（`ub[2] = previous(ub[1])`，`(1:2)[1]` 折成 1）。期望 = omc 1.25.0
  // 实跑（2026-10-11，探针 vc-ut）。
  YMC_CHECK_EQ(flatten_src("model VCU1\n  function g\n    input Real x;\n    output Real y;\n  algorithm\n    y := 2 * x;\n  end g;\n"
                           "  Real x[3] = {time, 2 * time, 3 * time};\n  Real y[2];\nequation\n  y = g(x[2:3]);\nend VCU1;\n"),
               std::string("function VCU1.g\n  input Real x;\n  output Real y;\nalgorithm\n  y := 2.0 * x;\nend VCU1.g;\n\n"
                           "class VCU1\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real y[1];\n  Real y[2];\nequation\n"
                           "  x = {time, 2.0 * time, 3.0 * time};\n  y = array(VCU1.g(x[(2:3)[$i1]]) for $i1 in 1:2);\nend VCU1;\n"));
  YMC_CHECK_EQ(flatten_src("model VCU2\n  parameter Integer n = 2;\n  Real u = sample(time, Clock(0.1));\n"
                           "  Real ub[n + 1](each start = 0.0);\nequation\n  ub = cat(1, {u}, previous(ub[1:n]));\nend VCU2;\n"),
               std::string("class VCU2\n  final parameter Integer n = 2;\n  Real u = sample(time, Clock(0.1));\n"
                           "  Real ub[1](start = 0.0);\n  Real ub[2](start = 0.0);\n  Real ub[3](start = 0.0);\nequation\n"
                           "  ub[1] = u;\n  ub[2] = previous(ub[1]);\n  ub[3] = previous(ub[2]);\nend VCU2;\n"));
  YMC_CHECK_EQ(flatten_src("model VCU3\n  function g\n    input Real x;\n    output Real y;\n  algorithm\n    y := 2 * x;\n  end g;\n"
                           "  Real m[3, 2] = {{time, 1}, {2, time}, {3, 4}};\n  Real y[2, 2];\nequation\n  y = g(m[1:2, :]);\n"
                           "end VCU3;\n"),
               std::string("function VCU3.g\n  input Real x;\n  output Real y;\nalgorithm\n  y := 2.0 * x;\nend VCU3.g;\n\n"
                           "class VCU3\n  Real m[1,1];\n  Real m[1,2];\n  Real m[2,1];\n  Real m[2,2];\n  Real m[3,1];\n"
                           "  Real m[3,2];\n  Real y[1,1];\n  Real y[1,2];\n  Real y[2,1];\n  Real y[2,2];\nequation\n"
                           "  m = {{time, 1.0}, {2.0, time}, {3.0, 4.0}};\n"
                           "  y = array(array(VCU3.g(m[(1:2)[$i1],$i2]) for $i2 in 1:2) for $i1 in 1:2);\nend VCU3;\n"));
  YMC_CHECK_EQ(flatten_src("function F\n  input Real a;\n  input Real b;\n  output Real c = a + b;\nend F;\nmodel VCU4\n"
                           "  Real x[3] = {time, 1, 2};\n  Real w[2, 3] = {{time, 1, 2}, {3, 4, 5}};\n  Real y[2] = F(x[2:3], 1);\n"
                           "end VCU4;\n"),
               std::string("function F\n  input Real a;\n  input Real b;\n  output Real c = a + b;\nend F;\n\nclass VCU4\n"
                           "  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real w[1,1];\n  Real w[1,2];\n  Real w[1,3];\n  Real w[2,1];\n"
                           "  Real w[2,2];\n  Real w[2,3];\n  Real y[1];\n  Real y[2];\nequation\n  x = {time, 1.0, 2.0};\n"
                           "  w = {{time, 1.0, 2.0}, {3.0, 4.0, 5.0}};\n  y = array(F(x[(2:3)[$i1]], 1.0) for $i1 in 1:2);\n"
                           "end VCU4;\n"));
}

YMC_TEST(flatten_constant_slice_after_symbolic_subscript) {
  // 常量数组在符号下标后跟 `:`（MSL SVPWM 的 `fire_p = fire[ka + 1, :]`）：所在等式逐元素展开，`:` 换成元素下标
  //（`b[1] = {{true, false}, …}[k + 1, 1]`）；绑定里随运算展开；常量矩阵逐元素照打声明注释。期望 = omc 1.25.0 实跑
  //（2026-10-11，探针 ks-ut）。
  YMC_CHECK_EQ(flatten_src("model KSU1\n  constant Boolean fire[3, 2] = [true, false; false, true; true, true];\n"
                           "  Integer k(start = 0, fixed = true);\n  Boolean b[2];\nequation\n  when sample(0, 0.1) then\n"
                           "    k = mod(pre(k) + 1, 3);\n  end when;\n  b = fire[k + 1, :];\nend KSU1;\n"),
               std::string("class KSU1\n  constant Boolean fire[1,1] = true;\n  constant Boolean fire[1,2] = false;\n"
                           "  constant Boolean fire[2,1] = false;\n  constant Boolean fire[2,2] = true;\n"
                           "  constant Boolean fire[3,1] = true;\n  constant Boolean fire[3,2] = true;\n"
                           "  Integer k(start = 0, fixed = true);\n  Boolean b[1];\n  Boolean b[2];\nequation\n"
                           "  when sample(0.0, 0.1) then\n    k = mod(pre(k) + 1, 3);\n  end when;\n"
                           "  b[1] = {{true, false}, {false, true}, {true, true}}[k + 1, 1];\n"
                           "  b[2] = {{true, false}, {false, true}, {true, true}}[k + 1, 2];\nend KSU1;\n"));
  YMC_CHECK_EQ(flatten_src("model KSU2\n  constant Real T[2, 3] = {{1, 2, 3}, {4, 5, 6}};\n  Integer k(start = 1, fixed = true);\n"
                           "  Real c[3] = T[k, :] * time;\nequation\n  when sample(0, 0.1) then\n    k = 3 - pre(k);\n  end when;\n"
                           "end KSU2;\n"),
               std::string("class KSU2\n  constant Real T[1,1] = 1.0;\n  constant Real T[1,2] = 2.0;\n  constant Real T[1,3] = 3.0;\n"
                           "  constant Real T[2,1] = 4.0;\n  constant Real T[2,2] = 5.0;\n  constant Real T[2,3] = 6.0;\n"
                           "  Integer k(start = 1, fixed = true);\n  Real c[1];\n  Real c[2];\n  Real c[3];\nequation\n"
                           "  c = {{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}[k, 1] * time, {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}[k, 2] * time, {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}[k, 3] * time};\n"
                           "  when sample(0.0, 0.1) then\n    k = 3 - pre(k);\n  end when;\nend KSU2;\n"));
  YMC_CHECK_EQ(flatten_src("model KSU3\n  constant Integer m = 3;\n"
                           "  constant Boolean fire[2, m] = [true, false, false; false, true, true] \"Switching patterns\";\n"
                           "  constant Real r[2, 2] = [1, 2; 3, 4] \"Matrix\";\n  constant Real v[2] = {1, 2} \"Vector\";\n"
                           "  parameter Real p[2, 2] = [1, 2; 3, 4] \"Param matrix\";\n  Real y = time;\nend KSU3;\n"),
               std::string("class KSU3\n  constant Integer m = 3;\n  constant Boolean fire[1,1] = true \"Switching patterns\";\n"
                           "  constant Boolean fire[1,2] = false \"Switching patterns\";\n"
                           "  constant Boolean fire[1,3] = false \"Switching patterns\";\n"
                           "  constant Boolean fire[2,1] = false \"Switching patterns\";\n"
                           "  constant Boolean fire[2,2] = true \"Switching patterns\";\n"
                           "  constant Boolean fire[2,3] = true \"Switching patterns\";\n  constant Real r[1,1] = 1.0 \"Matrix\";\n"
                           "  constant Real r[1,2] = 2.0 \"Matrix\";\n  constant Real r[2,1] = 3.0 \"Matrix\";\n"
                           "  constant Real r[2,2] = 4.0 \"Matrix\";\n  constant Real v[1] = 1.0 \"Vector\";\n"
                           "  constant Real v[2] = 2.0 \"Vector\";\n  parameter Real p[1,1] = 1.0 \"Param matrix\";\n"
                           "  parameter Real p[1,2] = 2.0 \"Param matrix\";\n  parameter Real p[2,1] = 3.0 \"Param matrix\";\n"
                           "  parameter Real p[2,2] = 4.0 \"Param matrix\";\n  Real y = time;\nend KSU3;\n"));
}

YMC_TEST(flatten_lookup_through_short_class) {
  // 进类查名前顺短类链到底（Class.classTree 对 EXPANDED_DERIVED 取基类的树，NFClass.mo:337-350 v1.25.0）：`package B = A;`
  // 里查 M 等于查 A 的。期望 = omc 1.25.0 实跑（2026-10-13，探针 pk-ut）。递归的短类定义（基类名查回自己）omc 报错，ymc SKIP
  //（此前无界递归、栈溢出，testsuite others/RecursiveShort）。
  YMC_CHECK_EQ(flatten_src("package P\n  package A\n    model M\n      Real x = 1;\n    end M;\n  end A;\n  package B = A;\nend P;\n"
                           "model PKU1\n  P.B.M m;\nend PKU1;\n"),
               std::string("class PKU1\n  Real m.x = 1.0;\nend PKU1;\n"));
  bool uns = false;
  const std::string out = flatten_src("model PKR1\n  class Env\n    type Env = Real;\n  end Env;\n  class A\n    type Env = Env.Env;\n"
                                     "    Env e = 1.0;\n  end A;\n  A a;\nend PKR1;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:recursive short class definition>"));
}

YMC_TEST(flatten_lookup_in_partial_class) {
  // partial 类里不许查名（NFLookup.lookupLocalName / lookupCrefInNode 进类前的 isPartial 检查，NFLookup.mo:703-706 / :979-982
  // v1.25.0；派生自 partial 类的短类本身也是 partial，NFInst.mo:1046-1049）：没被重声明的 `replaceable package M = PM` 里的
  // `M.T x`、表达式里的 `P.PM.c`——omc 1.25.0 都报错（2026-10-13 实跑，探针 pk-p2 PKP2 / PKP5），ymc SKIP（此前照常接受）。
  for (const char* src : {"package P\n  partial package PM\n    type T = Real(unit = \"K\");\n  end PM;\n  model V\n"
                          "    replaceable package M = PM;\n    M.T x = 1;\n  end V;\nend P;\nmodel PKP2\n  P.V v;\nend PKP2;\n",
                          "package P\n  partial package PM\n    constant Real c = 2;\n  end PM;\nend P;\nmodel PKP5\n"
                          "  Real x = P.PM.c;\nend PKP5;\n"}) {
    bool uns = false;
    const std::string out = flatten_src(src, &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, std::string("<unsupported:lookup in partial class>"));
  }
}

YMC_TEST(flatten_redeclared_package_as_type_prefix) {
  // 修饰里重声明的包当类型路径前缀（`redeclare package M = P.W` 后的 `M.T x`）：inst 的实例类树里每个实例一份顶替进来的新类
  //（REDECLARED_CLASS，NFInst.redeclareClass :1797-1800 v1.25.0），flatten 在实例层按实例的类树认它（ADR-007 第 2 阶段 65a-2），
  // 两层传递 `redeclare package M = M` 照样。期望 = omc 1.25.0 实跑（2026-10-13，探针 pk-p3 PKN1）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PM\n    type T = Real(unit = \"K\", min = 0);\n  end PM;\n  package W\n"
                           "    extends PM;\n  end W;\n  model V\n    replaceable package M = PM;\n    M.T x = 1;\n  end V;\n"
                           "  model S\n    replaceable package M = PM;\n    V v(redeclare package M = M);\n    M.T y = 2;\n  end S;\n"
                           "end P;\nmodel PKN1\n  P.S s(redeclare package M = P.W);\nend PKN1;\n"),
               std::string("class PKN1\n  Real s.v.x(unit = \"K\", min = 0.0) = 1.0;\n  Real s.y(unit = \"K\", min = 0.0) = 2.0;\n"
                           "end PKN1;\n"));
}

YMC_TEST(flatten_class_extends_target_through_modified_extends) {
  // `extends PartialMedium(nX = 2)` 后的 `redeclare model extends BaseProperties`：OMC 的隐式基类是 extends 实例里的那份
  //（NFInst.redeclareClass :1741 v1.25.0），基类的 `Real X[nX]` 得 2 个（omc 1.25.0 实跑，2026-10-13，探针 pk-p4 PKX3）；ymc 的
  // 目标是共享的基类定义、读到声明值 1——此前静默只出 X[1]，现 SKIP（同按名查经改了常量的 extends 继承来的类）。
  bool uns = false;
  const std::string out = flatten_src("package P\n  partial package PartialMedium\n    constant Integer nX = 1;\n"
                                     "    replaceable partial model BaseProperties\n      Real X[nX];\n    end BaseProperties;\n"
                                     "  end PartialMedium;\n  package Water\n    extends PartialMedium(nX = 2);\n"
                                     "    redeclare model extends BaseProperties\n      Real p = 1;\n    end BaseProperties;\n  end Water;\n"
                                     "end P;\nmodel PKX3\n  P.Water.BaseProperties medium;\nend PKX3;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:inherited class seen through modifier / redeclare>"));
}

YMC_TEST(flatten_package_instance_constants_and_functions) {
  // 重声明的包按包实例查（Inst.instPackage，NFInst.mo:1312-1369 v1.25.0；ADR-007 第 2 阶段 65a-3）：`Medium.nX` 取 extends 修饰
  // 改过的值 2，`Medium.g` 的体里读 nX 也得 2，函数按实例路径命名（NFInstNode.scopePath：`PK3.v.Medium.g`）。期望 = omc 1.25.0
  // 实跑（2026-10-13，探针 pk-p1 PK3）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PartialMedium\n    replaceable partial model BaseProperties\n      Real p;\n"
                           "    end BaseProperties;\n    constant Integer nX = 1;\n"
                           "    type MassFlowRate = Real(quantity = \"MassFlowRate\", unit = \"kg/s\", min = -1e5, max = 1e5);\n"
                           "    replaceable record ThermodynamicState\n      Real T;\n    end ThermodynamicState;\n"
                           "    replaceable function g\n      input Real x;\n      output Real y;\n    algorithm\n      y := x + nX;\n"
                           "    end g;\n  end PartialMedium;\n  package Water\n    extends PartialMedium(nX = 2);\n"
                           "    redeclare model extends BaseProperties\n    equation\n      p = 1;\n    end BaseProperties;\n"
                           "  end Water;\n  model Vessel\n    replaceable package Medium = PartialMedium;\n    Real x[Medium.nX];\n"
                           "    Real y = Medium.g(time);\n  end Vessel;\nend P;\nmodel PK3\n"
                           "  P.Vessel v(redeclare package Medium = P.Water);\nend PK3;\n"),
               std::string("function PK3.v.Medium.g\n  input Real x;\n  output Real y;\nalgorithm\n  y := x + 2.0;\n"
                           "end PK3.v.Medium.g;\n\nclass PK3\n  Real v.x[1];\n  Real v.x[2];\n  Real v.y = PK3.v.Medium.g(time);\n"
                           "end PK3;\n"));
}

YMC_TEST(flatten_class_extends_target_in_package_instance) {
  // 包实例里的 `redeclare model extends BaseProperties`：隐式基类是那个包实例的基类实例里顶替前的原类（NFInst.redeclareClass :1741
  // v1.25.0），基类的 `Real X[nX]` 读 extends 修饰改过的 nX = 2。期望 = omc 1.25.0 实跑（2026-10-13，探针 pk-p4 PKX4）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PartialMedium\n    constant Integer nX = 1;\n"
                           "    replaceable partial model BaseProperties\n      Real X[nX];\n    end BaseProperties;\n"
                           "  end PartialMedium;\n  package Water\n    extends PartialMedium(nX = 2);\n"
                           "    redeclare model extends BaseProperties\n      Real p = 1;\n    end BaseProperties;\n  end Water;\n"
                           "  model Vessel\n    replaceable package Medium = PartialMedium;\n    Medium.BaseProperties medium;\n"
                           "  end Vessel;\nend P;\nmodel PKX4\n  P.Vessel v(redeclare package Medium = P.Water);\nend PKX4;\n"),
               std::string("class PKX4\n  Real v.medium.X[1];\n  Real v.medium.X[2];\n  Real v.medium.p = 1.0;\nend PKX4;\n"));
}

YMC_TEST(flatten_derived_type_modifier_scope) {
  // 短类链修饰在类型定义处求值：包里 `type T = Real(min = Tmin)` 的 Tmin 是包常量 5，不是模型自己的 Tmin = 7（此前一律在组件所在
  // 的类里求、静默打成 7.0）。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p7 PKD1）。
  YMC_CHECK_EQ(flatten_src("package P\n  constant Real Tmin = 5;\n  type T = Real(min = Tmin);\nend P;\nmodel PKD1\n"
                           "  constant Real Tmin = 7;\n  P.T x = time;\nend PKD1;\n"),
               std::string("class PKD1\n  constant Real Tmin = 7.0;\n  Real x(min = 5.0) = time;\nend PKD1;\n"));
}

YMC_TEST(flatten_outer_scan_skips_package_constants) {
  // 查名穿过重声明的包时扫它子树里的 outer（lookup_detaches_outer）：包里只有类和常量，常量的类型里不会有 inner / outer（MLS §4.7），
  // 不下钻——此前去解析 `constant FC fc` 的类型（经 `extends PM(redeclare replaceable record FC = …)` 继承来）撞守卫 SKIP（MSL
  // Fluid / Media 的 `fluidConstants`）。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p8 PKO1）。
  YMC_CHECK_EQ(flatten_src("package P\n  record FC2\n    Real a = 1;\n    Real b = 2;\n  end FC2;\n  partial package PM\n"
                           "    replaceable record FC\n      Real a = 1;\n    end FC;\n    type T = Real(unit = \"K\");\n  end PM;\n"
                           "  partial package PM2\n    extends PM(redeclare replaceable record FC = P.FC2);\n    constant FC fc;\n"
                           "  end PM2;\n  package W\n    extends PM2;\n  end W;\n  model V\n    replaceable package M = PM;\n"
                           "    M.T x = time;\n  end V;\nend P;\nmodel PKO1\n  P.V v(redeclare package M = P.W);\nend PKO1;\n"),
               std::string("class PKO1\n  Real v.x(unit = \"K\") = time;\nend PKO1;\n"));
}

YMC_TEST(flatten_empty_slice_of_constant_array) {
  // 常量数组的空切片（`reference_X[1:nXi]`、nXi = 0）得空数组（typeSubscriptedArray，NFExpression.mo:1420-1443 v1.25.0），
  // 空数组 Xi 的属性照常定型后整个丢掉（MSL Media BaseProperties 的 Xi 起值）。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p9 PKS3）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PM\n    constant Integer nX = 2;\n    constant Integer nXi = nX - 1;\n"
                           "    constant Real reference_X[nX] = fill(1 / nX, nX);\n    replaceable partial model BaseProperties\n"
                           "      Real X[nX](start = reference_X);\n      Real Xi[nXi](start = reference_X[1:nXi]);\n"
                           "    end BaseProperties;\n  end PM;\n  package W\n    extends PM(nX = 1);\n"
                           "    redeclare model extends BaseProperties\n    equation\n      X = {1};\n    end BaseProperties;\n"
                           "  end W;\n  model V\n    replaceable package M = PM;\n    M.BaseProperties medium;\n  end V;\nend P;\n"
                           "model PKS3\n  P.V v(redeclare package M = P.W);\nend PKS3;\n"),
               std::string("class PKS3\n  Real v.medium.X[1](start = 1.0);\nequation\n  v.medium.X[1] = 1.0;\nend PKS3;\n"));
}

YMC_TEST(flatten_colon_dim_of_modified_package_constant) {
  // 包常量的 `:` 维由（被 extends 修饰改过的）绑定推出（typeDimension 的 UNKNOWN 支，NFTyping.mo:686-725 v1.25.0）：
  // `nS = size(substanceNames, 1)` 随 `extends PM(substanceNames = {"a", "b"})` 变成 2；String 数组常量内联成字面量。
  // 期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p9 PKS7）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PM\n    constant String mediumName = \"unusablePartialMedium\";\n"
                           "    constant String substanceNames[:] = {mediumName};\n    constant Boolean reducedX = true;\n"
                           "    constant Boolean fixedX = false;\n    final constant Integer nS = size(substanceNames, 1);\n"
                           "    constant Integer nX = nS;\n"
                           "    constant Integer nXi = if fixedX then 0 else if reducedX then nS - 1 else nS;\n"
                           "    constant Real reference_X[nX] = fill(1 / nX, nX);\n    replaceable partial model BaseProperties\n"
                           "      Real X[nX](start = reference_X);\n      Real Xi[nXi](start = reference_X[1:nXi]);\n"
                           "    end BaseProperties;\n  end PM;\n  package W\n"
                           "    extends PM(mediumName = \"W\", substanceNames = {\"a\", \"b\"});\n"
                           "    redeclare model extends BaseProperties\n    equation\n      X = {1, 2};\n      Xi = {3};\n"
                           "    end BaseProperties;\n  end W;\n  model V\n    replaceable package M = PM;\n"
                           "    M.BaseProperties medium;\n    String s[M.nS] = M.substanceNames;\n  end V;\nend P;\nmodel PKS7\n"
                           "  P.V v(redeclare package M = P.W);\nend PKS7;\n"),
               std::string("class PKS7\n  Real v.medium.X[1](start = 0.5);\n  Real v.medium.X[2](start = 0.5);\n"
                           "  Real v.medium.Xi[1](start = 0.5);\n  String v.s[1];\n  String v.s[2];\nequation\n"
                           "  v.medium.X[1] = 1.0;\n  v.medium.X[2] = 2.0;\n  v.medium.Xi[1] = 3.0;\n  v.s = {\"a\", \"b\"};\n"
                           "end PKS7;\n"));
}

YMC_TEST(flatten_empty_package_constant_array) {
  // 空数组常量（`extraPropertiesNames[:] = fill("", 0)`）按声明类型定型成 `{}`（makeEmptyArray，NFExpression.mo:1077-1083
  // v1.25.0），空数组组件的属性 / 绑定照常核对后丢掉；修饰成非空的照常展开。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p9 PKS10）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PM\n    constant String extraPropertiesNames[:] = fill(\"\", 0);\n"
                           "    final constant Integer nC = size(extraPropertiesNames, 1);\n"
                           "    constant Real C_default[nC] = fill(0, nC);\n  end PM;\n  package W\n    extends PM;\n  end W;\n"
                           "  package W2\n    extends PM(extraPropertiesNames = {\"CO2\"});\n  end W2;\n  model V\n"
                           "    replaceable package M = PM;\n"
                           "    parameter Real C_start[M.nC](quantity = M.extraPropertiesNames) = M.C_default;\n"
                           "    Real C[M.nC](quantity = M.extraPropertiesNames, start = C_start);\n"
                           "    String s[M.nC] = M.extraPropertiesNames;\n  equation\n    der(C) = -C;\n  end V;\nend P;\n"
                           "model PKS10\n  P.V v(redeclare package M = P.W);\n  P.V v2(redeclare package M = P.W2);\nend PKS10;\n"),
               std::string("class PKS10\n  parameter Real v2.C_start[1](quantity = \"CO2\") = 0.0;\n"
                           "  Real v2.C[1](quantity = \"CO2\", start = v2.C_start[1]);\n  String v2.s[1];\nequation\n"
                           "  v2.s = {\"CO2\"};\n  der(v2.C[1]) = -v2.C[1];\nend PKS10;\n"));
}

YMC_TEST(flatten_record_constant_inherited_field) {
  // 记录常量（无绑定，按组件修饰逐字段构造）的继承字段：`record T extends B` 的 `wc[1].molarMass`（MSL Media 的
  // `waterConstants`，TwoPhase.FluidConstants extends Basic.FluidConstants）。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p10 PKM3）。
  YMC_CHECK_EQ(flatten_src("package P\n  record B\n    Real molarMass;\n  end B;\n  record T\n    extends B;\n    Real tc;\n  end T;\n"
                           "  constant T[1] wc(each molarMass = 0.018, each tc = 1);\n  constant T w1(molarMass = 0.02, tc = 2);\n"
                           "end P;\nmodel PKM3\n  Real MM = P.wc[1].molarMass;\n  Real M1 = P.w1.molarMass;\n  Real t = P.wc[1].tc;\n"
                           "end PKM3;\n"),
               std::string("class PKM3\n  Real MM = 0.018;\n  Real M1 = 0.02;\n  Real t = 1.0;\nend PKM3;\n"));
}

YMC_TEST(flatten_string_real_exponent_three_digits) {
  // String(Real) 编译期求值的指数：omc 按 C 的 sprintf 打（NFCeval.mo:2748-2793 v1.25.0），Windows 版 omc 1.25.0 指数至少
  // 3 位（"1e-005"）——平台相关，见 docs/open-questions.md。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p10 PKM4）。
  YMC_CHECK_EQ(flatten_src("model PKM4\n  String s1 = String(1e-5);\n  String s2 = String(1.5e10);\n"
                           "  String s3 = String(123456789.0);\n  String s4 = String(0.0001);\n  String s5 = String(1e6);\n"
                           "  String s6 = String(-2.5e-7, significantDigits = 3);\n  String s7 = String(1e100);\n"
                           "  String s8 = String(1e-5, format = \"g\");\n"
                           "  String s10 = String(611.657e3, minimumLength = 12, leftJustified = false);\nend PKM4;\n"),
               std::string("class PKM4\n  String s1 = \"1e-005\";\n  String s2 = \"1.5e+010\";\n  String s3 = \"1.23457e+008\";\n"
                           "  String s4 = \"0.0001\";\n  String s5 = \"1e+006\";\n  String s6 = \"-2.5e-007\";\n"
                           "  String s7 = \"1e+100\";\n  String s8 = \"1e-005\";\n  String s10 = \"      611657\";\nend PKM4;\n"));
}

YMC_TEST(flatten_state_select_forward_inner_parameter) {
  // stateSelect 的值经 preferredMediumStates → energyDynamics → system.energyDynamics（inner system 声明在后）：第一遍
  // 缺值 → 第二遍带着第一遍的值求，依赖链（system.massDynamics = energyDynamics）一起标结构参数。期望 = omc 1.25.0 实跑
  // （2026-10-14，探针 pk-p10 PKM7）。
  YMC_CHECK_EQ(flatten_src("package P\n  package Types\n"
                           "    type Dynamics = enumeration(DynamicFreeInitial, FixedInitial, SteadyStateInitial, SteadyState);\n"
                           "  end Types;\n  model System\n"
                           "    parameter Types.Dynamics energyDynamics = Types.Dynamics.DynamicFreeInitial;\n"
                           "    parameter Types.Dynamics massDynamics = energyDynamics;\n  end System;\n  partial package PM\n"
                           "    replaceable partial model BaseProperties\n      Real p;\n      Real T;\n"
                           "      parameter Boolean preferredMediumStates = false;\n    end BaseProperties;\n  end PM;\n"
                           "  partial package PS\n    extends PM;\n    redeclare replaceable model extends BaseProperties(\n"
                           "      T(stateSelect = if preferredMediumStates then StateSelect.prefer else StateSelect.default),\n"
                           "      p(stateSelect = if preferredMediumStates then StateSelect.prefer else StateSelect.default))\n"
                           "    equation\n      p = 1;\n      T = time;\n    end BaseProperties;\n  end PS;\n  package W\n"
                           "    extends PS;\n  end W;\n  partial model Vol\n    import P.Types;\n    import P.Types.Dynamics;\n"
                           "    outer System system;\n    replaceable package Medium = PM;\n"
                           "    parameter Types.Dynamics energyDynamics = system.energyDynamics;\n"
                           "    parameter Types.Dynamics massDynamics = system.massDynamics;\n    Medium.BaseProperties medium(\n"
                           "      preferredMediumStates = (if energyDynamics == Dynamics.SteadyState and\n"
                           "                               massDynamics == Dynamics.SteadyState then false else true));\n  end Vol;\n"
                           "  model Tank\n    extends Vol;\n  end Tank;\nend P;\nmodel PKM7\n"
                           "  replaceable package Medium = P.W constrainedby P.PM;\n"
                           "  P.Tank tank1(redeclare package Medium = Medium);\n"
                           "  inner P.System system(energyDynamics = P.Types.Dynamics.FixedInitial);\nend PKM7;\n"),
               std::string("class PKM7\n"
                           "  final parameter enumeration(DynamicFreeInitial, FixedInitial, SteadyStateInitial, SteadyState) tank1.energyDynamics = P.Types.Dynamics.FixedInitial;\n"
                           "  final parameter enumeration(DynamicFreeInitial, FixedInitial, SteadyStateInitial, SteadyState) tank1.massDynamics = P.Types.Dynamics.FixedInitial;\n"
                           "  Real tank1.medium.p(stateSelect = StateSelect.prefer);\n"
                           "  Real tank1.medium.T(stateSelect = StateSelect.prefer);\n"
                           "  final parameter Boolean tank1.medium.preferredMediumStates = true;\n"
                           "  final parameter enumeration(DynamicFreeInitial, FixedInitial, SteadyStateInitial, SteadyState) system.energyDynamics = P.Types.Dynamics.FixedInitial;\n"
                           "  final parameter enumeration(DynamicFreeInitial, FixedInitial, SteadyStateInitial, SteadyState) system.massDynamics = P.Types.Dynamics.FixedInitial;\n"
                           "equation\n  tank1.medium.p = 1.0;\n  tank1.medium.T = time;\nend PKM7;\n"));
}

YMC_TEST(flatten_function_class_extends_record_instance_path) {
  // 经重声明的包调 `redeclare function extends` 定义的函数：签名先收隐式基类（class_extends_target）的成员；记录类型形参与它的
  // 构造函数按实例路径命名（`PKG2.v.M.ThermodynamicState`，NFInstNode.scopePath）。期望 = omc 1.25.0 实跑（2026-10-14，
  // 探针 pk-p11 PKG2）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PM\n    record ThermodynamicState\n      Real T;\n"
                           "    end ThermodynamicState;\n    replaceable partial function specificHeatCapacityCp\n"
                           "      input ThermodynamicState state;\n      output Real cp;\n    end specificHeatCapacityCp;\n  end PM;\n"
                           "  package W\n    extends PM;\n    redeclare function extends specificHeatCapacityCp \"Cp of W\"\n"
                           "    algorithm\n      cp := 4184 + state.T;\n    end specificHeatCapacityCp;\n  end W;\n  model V\n"
                           "    replaceable package M = PM;\n    M.ThermodynamicState s(T = time);\n"
                           "    Real cp = M.specificHeatCapacityCp(s);\n  end V;\nend P;\nmodel PKG2\n"
                           "  P.V v(redeclare package M = P.W);\nend PKG2;\n"),
               std::string("function PKG2.v.M.ThermodynamicState \"Automatically generated record constructor for PKG2.v.M.ThermodynamicState\"\n"
                           "  input Real T;\n  output ThermodynamicState res;\nend PKG2.v.M.ThermodynamicState;\n\n"
                           "function PKG2.v.M.specificHeatCapacityCp \"Cp of W\"\n  input PKG2.v.M.ThermodynamicState state;\n"
                           "  output Real cp;\nalgorithm\n  cp := 4184.0 + state.T;\nend PKG2.v.M.specificHeatCapacityCp;\n\n"
                           "class PKG2\n  Real v.s.T = time;\n  Real v.cp = PKG2.v.M.specificHeatCapacityCp(v.s);\nend PKG2;\n"));
}

YMC_TEST(flatten_function_variable_type_class_modifier) {
  // 函数变量的类型被包的 extends 类修饰改过（`extends PM(AbsolutePressure(min = 611.657, …))`）：类修饰并在类型链最前
  // （`p(min = 611.657, max = 1e8, start = 5e6, nominal = 1e5)`）。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p11 PKG4）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PM\n"
                           "    type AbsolutePressure = Real(min = 0, max = 1e8, nominal = 1e5, start = 1e5);\n"
                           "    replaceable partial function saturationTemperature\n      input AbsolutePressure p;\n"
                           "      output Real T;\n    end saturationTemperature;\n  end PM;\n  package W\n"
                           "    extends PM(AbsolutePressure(min = 611.657, max = 1e8, start = 5e6));\n"
                           "    redeclare function extends saturationTemperature\n    algorithm\n      T := 2 * p;\n"
                           "    end saturationTemperature;\n  end W;\n  model V\n    replaceable package M = PM;\n"
                           "    Real T = M.saturationTemperature(time);\n  end V;\nend P;\nmodel PKG4\n"
                           "  P.V v(redeclare package M = P.W);\nend PKG4;\n"),
               std::string("function PKG4.v.M.saturationTemperature\n"
                           "  input Real p(min = 611.657, max = 1e8, start = 5e6, nominal = 1e5);\n  output Real T;\nalgorithm\n"
                           "  T := 2.0 * p;\nend PKG4.v.M.saturationTemperature;\n\nclass PKG4\n"
                           "  Real v.T = PKG4.v.M.saturationTemperature(time);\nend PKG4;\n"));
}

YMC_TEST(flatten_record_class_extends_chain) {
  // `redeclare record extends ThermodynamicState` 一层层顶替：字段 = 隐式基类的（phase）+ 本体的（p, T）；函数体里的构造调用
  // 按实例路径命名。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p11 PKG6）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PM\n"
                           "    type AbsolutePressure = Real(min = 0, max = 1e8, nominal = 1e5, start = 1e5);\n"
                           "    type Temperature = Real(min = 1, max = 1e4, nominal = 300, start = 288.15);\n"
                           "    replaceable record ThermodynamicState\n    end ThermodynamicState;\n"
                           "    replaceable partial function setState_pT\n      input AbsolutePressure p;\n"
                           "      input Temperature T;\n      output ThermodynamicState state;\n    end setState_pT;\n"
                           "    replaceable partial function temperature\n      input ThermodynamicState state;\n"
                           "      output Temperature T;\n    end temperature;\n  end PM;\n  partial package PT\n    extends PM;\n"
                           "    redeclare replaceable record extends ThermodynamicState\n      Integer phase(min = 0, max = 2);\n"
                           "    end ThermodynamicState;\n  end PT;\n  package W\n"
                           "    extends PT(AbsolutePressure(min = 611.657, start = 5e6), Temperature(start = 500));\n"
                           "    redeclare record extends ThermodynamicState\n      AbsolutePressure p;\n      Temperature T;\n"
                           "    end ThermodynamicState;\n    redeclare function extends setState_pT\n    algorithm\n"
                           "      state := ThermodynamicState(phase = 1, p = p, T = T);\n    end setState_pT;\n"
                           "    redeclare function extends temperature\n    algorithm\n      T := state.T;\n    end temperature;\n"
                           "  end W;\n  model V\n    replaceable package M = PM;\n"
                           "    M.ThermodynamicState s = M.setState_pT(1e5, 300 + time);\n    Real T = M.temperature(s);\n  end V;\n"
                           "end P;\nmodel PKG6\n  P.V v(redeclare package M = P.W);\nend PKG6;\n"),
               std::string("function PKG6.v.M.ThermodynamicState \"Automatically generated record constructor for PKG6.v.M.ThermodynamicState\"\n"
                           "  input Integer phase;\n  input Real p;\n  input Real T;\n  output ThermodynamicState res;\n"
                           "end PKG6.v.M.ThermodynamicState;\n\nfunction PKG6.v.M.setState_pT\n"
                           "  input Real p(min = 611.657, max = 1e8, start = 5e6, nominal = 1e5);\n"
                           "  input Real T(min = 1.0, max = 1e4, start = 500.0, nominal = 300.0);\n"
                           "  output PKG6.v.M.ThermodynamicState state;\nalgorithm\n  state := PKG6.v.M.ThermodynamicState(1, p, T);\n"
                           "end PKG6.v.M.setState_pT;\n\nfunction PKG6.v.M.temperature\n  input PKG6.v.M.ThermodynamicState state;\n"
                           "  output Real T(min = 1.0, max = 1e4, start = 500.0, nominal = 300.0);\nalgorithm\n  T := state.T;\n"
                           "end PKG6.v.M.temperature;\n\nclass PKG6\n  Integer v.s.phase(min = 0, max = 2);\n"
                           "  Real v.s.p(min = 611.657, max = 1e8, start = 5e6, nominal = 1e5);\n"
                           "  Real v.s.T(min = 1.0, max = 1e4, start = 500.0, nominal = 300.0);\n"
                           "  Real v.T = PKG6.v.M.temperature(v.s);\nequation\n  v.s = PKG6.v.M.setState_pT(1e5, 300.0 + time);\n"
                           "end PKG6;\n"));
}

YMC_TEST(flatten_record_class_extends_instance_base) {
  // class extends 记录继承来的字段按实例类树里的隐式基类解析（Instantiator::class_extends_base）：`FixedPhase` 经带修饰的
  // extends（`extends PM(final fixedX = true)`）继承来，定义层查会撞继承守卫。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p12 PKG7）。
  YMC_CHECK_EQ(flatten_src("package P\n  package Types\n    type FixedPhase = Integer(min = 0, max = 2);\n  end Types;\n"
                           "  partial package PM\n    extends Types;\n    constant Boolean fixedX = false;\n"
                           "    replaceable record ThermodynamicState\n    end ThermodynamicState;\n"
                           "    replaceable partial function setState_p\n      input Real p;\n      input FixedPhase phase = 0;\n"
                           "      output ThermodynamicState state;\n    end setState_p;\n  end PM;\n  partial package PP\n"
                           "    extends PM(final fixedX = true);\n  end PP;\n  partial package PT\n    extends PP;\n"
                           "    redeclare replaceable record extends ThermodynamicState\n      FixedPhase phase(min = 0, max = 2);\n"
                           "    end ThermodynamicState;\n  end PT;\n  package W\n    extends PT;\n"
                           "    redeclare record extends ThermodynamicState\n      Real p;\n    end ThermodynamicState;\n"
                           "    redeclare function extends setState_p\n    algorithm\n"
                           "      state := ThermodynamicState(phase = phase, p = p);\n    end setState_p;\n  end W;\n  model V\n"
                           "    replaceable package M = PM;\n    M.ThermodynamicState s = M.setState_p(1e5 + time);\n  end V;\n"
                           "end P;\nmodel PKG7\n  P.V v(redeclare package M = P.W);\nend PKG7;\n"),
               std::string("function PKG7.v.M.ThermodynamicState \"Automatically generated record constructor for PKG7.v.M.ThermodynamicState\"\n"
                           "  input Integer phase;\n  input Real p;\n  output ThermodynamicState res;\n"
                           "end PKG7.v.M.ThermodynamicState;\n\nfunction PKG7.v.M.setState_p\n  input Real p;\n"
                           "  input Integer phase(min = 0, max = 2) = 0;\n  output PKG7.v.M.ThermodynamicState state;\nalgorithm\n"
                           "  state := PKG7.v.M.ThermodynamicState(phase, p);\nend PKG7.v.M.setState_p;\n\nclass PKG7\n"
                           "  Integer v.s.phase(min = 0, max = 2);\n  Real v.s.p;\nequation\n"
                           "  v.s = PKG7.v.M.setState_p(1e5 + time, 0);\nend PKG7;\n"));
}

YMC_TEST(flatten_ce_record_in_constant_evaluation) {
  // 包常量 `h_default = specificEnthalpy_pT(p_default, T_default)` 编译期求值：解释期的 class extends 记录值同样按实例层的
  // 隐式基类收字段。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p12 PKG8）。
  YMC_CHECK_EQ(flatten_src("package P\n  package Types\n    type FixedPhase = Integer(min = 0, max = 2);\n  end Types;\n"
                           "  partial package PM\n    extends Types;\n    constant Boolean fixedX = false;\n"
                           "    constant Real p_default = 1e5;\n    constant Real T_default = 300;\n"
                           "    constant Real h_default = specificEnthalpy_pT(p_default, T_default);\n"
                           "    replaceable record ThermodynamicState\n    end ThermodynamicState;\n"
                           "    replaceable partial function setState_pT\n      input Real p;\n      input Real T;\n"
                           "      input FixedPhase phase = 0;\n      output ThermodynamicState state;\n    end setState_pT;\n"
                           "    replaceable partial function specificEnthalpy\n      input ThermodynamicState state;\n"
                           "      output Real h;\n    end specificEnthalpy;\n    replaceable function specificEnthalpy_pT\n"
                           "      input Real p;\n      input Real T;\n      output Real h;\n    algorithm\n"
                           "      h := specificEnthalpy(setState_pT(p, T));\n    end specificEnthalpy_pT;\n  end PM;\n"
                           "  partial package PT\n    extends PM(final fixedX = true);\n"
                           "    redeclare replaceable record extends ThermodynamicState\n      FixedPhase phase(min = 0, max = 2);\n"
                           "    end ThermodynamicState;\n  end PT;\n  package W\n    extends PT;\n"
                           "    redeclare record extends ThermodynamicState\n      Real T;\n      Real h;\n"
                           "    end ThermodynamicState;\n    redeclare function extends setState_pT\n    algorithm\n"
                           "      state := ThermodynamicState(phase = phase, T = T, h = 4184 * T);\n    end setState_pT;\n"
                           "    redeclare function extends specificEnthalpy\n    algorithm\n      h := state.h;\n"
                           "    end specificEnthalpy;\n  end W;\n  model V\n    replaceable package M = PM;\n"
                           "    parameter Real h_start = M.h_default;\n    Real h(start = h_start);\n  equation\n    der(h) = -h;\n"
                           "  end V;\nend P;\nmodel PKG8\n  P.V v(redeclare package M = P.W);\nend PKG8;\n"),
               std::string("class PKG8\n  parameter Real v.h_start = 1.2552e6;\n  Real v.h(start = v.h_start);\nequation\n"
                           "  der(v.h) = -v.h;\nend PKG8;\n"));
}

YMC_TEST(flatten_bare_annotation_entries) {
  // 注解顶层的空修饰（`annotation(Evaluate, …)`、`annotation(Inline, smoothOrder)`、类型的 `annotation(absoluteValue)`）
  // 保留成 NOMOD（无效 Id）：读注解的地方此前直接取它的修饰 → 断言崩（testsuite Ticket6406）。期望 = omc 1.25.0 实跑
  // （2026-10-14，探针 pk-p12 PKG9）。
  YMC_CHECK_EQ(flatten_src("model PKG9\n  type T = Real annotation(absoluteValue);\n  function f\n    input Real x;\n"
                           "    output Real y;\n  algorithm\n    y := 2 * x;\n    annotation(Inline, smoothOrder);\n  end f;\n"
                           "  parameter Real p = 2 annotation(Evaluate, Dialog(tab = \"General\"));\n  T x = p * time;\n"
                           "  Real y = f(x);\nend PKG9;\n"),
               std::string("function PKG9.f\n  input Real x;\n  output Real y;\nalgorithm\n  y := 2.0 * x;\nend PKG9.f;\n\n"
                           "class PKG9\n  parameter Real p = 2.0;\n  Real x = p * time;\n  Real y = PKG9.f(x);\nend PKG9;\n"));
}

YMC_TEST(flatten_zero_size_composite_and_fill) {
  // 端口数组的整体引用 `ports.C_outflow = fill(C, nPorts)`：C_outflow[nC] 而 nC = 0（零长的洞展开成空数组）、nPorts = 0
  // （深段的维按类实例里的声明求）、fill 的值是空数组——两边都空，不产生方程。期望 = omc 1.25.0 实跑（2026-10-14，探针
  // pk-p13 PKH2）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PM\n    constant Integer nXi = 0;\n    constant Integer nC = 0;\n  end PM;\n"
                           "  package W\n    extends PM;\n  end W;\n  connector FluidPort\n    replaceable package Medium = PM;\n"
                           "    flow Real m_flow;\n    Real p;\n    stream Real h_outflow;\n    stream Real Xi_outflow[Medium.nXi];\n"
                           "    stream Real C_outflow[Medium.nC];\n  end FluidPort;\n  model Source\n"
                           "    replaceable package Medium = PM;\n    parameter Integer nPorts = 0;\n    parameter Real p = 1e5;\n"
                           "    parameter Real h = 1e5;\n    parameter Real X[Medium.nXi] = fill(0, Medium.nXi);\n"
                           "    parameter Real C[Medium.nC] = fill(0, Medium.nC);\n"
                           "    FluidPort ports[nPorts](redeclare each package Medium = Medium);\n  equation\n"
                           "    ports.p = fill(p, nPorts);\n    ports.h_outflow = fill(h, nPorts);\n"
                           "    ports.Xi_outflow = fill(X, nPorts);\n    ports.C_outflow = fill(C, nPorts);\n  end Source;\nend P;\n"
                           "model PKH2\n  P.Source s1(nPorts = 2, redeclare package Medium = P.W);\n"
                           "  P.Source s0(redeclare package Medium = P.W);\nend PKH2;\n"),
               std::string("class PKH2\n  final parameter Integer s1.nPorts = 2;\n  parameter Real s1.p = 1e5;\n"
                           "  parameter Real s1.h = 1e5;\n  Real s1.ports[1].m_flow;\n  Real s1.ports[1].p;\n"
                           "  Real s1.ports[1].h_outflow;\n  Real s1.ports[2].m_flow;\n  Real s1.ports[2].p;\n"
                           "  Real s1.ports[2].h_outflow;\n  final parameter Integer s0.nPorts = 0;\n  parameter Real s0.p = 1e5;\n"
                           "  parameter Real s0.h = 1e5;\nequation\n  s1.ports[1].m_flow = 0.0;\n  s1.ports[2].m_flow = 0.0;\n"
                           "  s1.ports[1].p = s1.p;\n  s1.ports[2].p = s1.p;\n  s1.ports[1].h_outflow = s1.h;\n"
                           "  s1.ports[2].h_outflow = s1.h;\nend PKH2;\n"));
}

YMC_TEST(flatten_connector_empty_members_cardinality) {
  // 连接器的空数组成员（含 stream 的 `Xi_outflow[nXi]` / `C_outflow[nC]`，nXi = nC = 0）没有标量叶，连接与 cardinality 照常
  // （`assert(cardinality(ports[i]) <= 1, …)` 求值为真后整条去掉）。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p13 PKH3）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PM\n    constant Integer nXi = 0;\n    constant Integer nC = 0;\n  end PM;\n"
                           "  package W\n    extends PM;\n  end W;\n  connector FluidPort\n    replaceable package Medium = PM;\n"
                           "    flow Real m_flow;\n    Real p;\n    stream Real h_outflow;\n    stream Real Xi_outflow[Medium.nXi];\n"
                           "    stream Real C_outflow[Medium.nC];\n  end FluidPort;\n  model Source\n"
                           "    replaceable package Medium = PM;\n    parameter Integer nPorts = 0;\n    parameter Real p = 1e5;\n"
                           "    parameter Real h = 1e5;\n    parameter Real X[Medium.nXi] = fill(0, Medium.nXi);\n"
                           "    parameter Real C[Medium.nC] = fill(0, Medium.nC);\n"
                           "    FluidPort ports[nPorts](redeclare each package Medium = Medium);\n  equation\n"
                           "    ports.p = fill(p, nPorts);\n    ports.h_outflow = fill(h, nPorts);\n"
                           "    ports.Xi_outflow = fill(X, nPorts);\n    ports.C_outflow = fill(C, nPorts);\n"
                           "    for i in 1:nPorts loop\n      assert(cardinality(ports[i]) <= 1, \"only one connection per port\");\n"
                           "    end for;\n  end Source;\n  model Sink\n    replaceable package Medium = PM;\n"
                           "    FluidPort port(redeclare package Medium = Medium);\n  equation\n    port.p = 1;\n"
                           "    port.h_outflow = 2;\n    port.Xi_outflow = fill(0, Medium.nXi);\n"
                           "    port.C_outflow = fill(0, Medium.nC);\n  end Sink;\nend P;\nmodel PKH3\n"
                           "  P.Source s1(nPorts = 2, redeclare package Medium = P.W);\n"
                           "  P.Source s0(redeclare package Medium = P.W);\n  P.Sink k(redeclare package Medium = P.W);\nequation\n"
                           "  connect(s1.ports[1], k.port);\nend PKH3;\n"),
               std::string("class PKH3\n  final parameter Integer s1.nPorts = 2;\n  parameter Real s1.p = 1e5;\n"
                           "  parameter Real s1.h = 1e5;\n  Real s1.ports[1].m_flow;\n  Real s1.ports[1].p;\n"
                           "  Real s1.ports[1].h_outflow;\n  Real s1.ports[2].m_flow;\n  Real s1.ports[2].p;\n"
                           "  Real s1.ports[2].h_outflow;\n  final parameter Integer s0.nPorts = 0;\n  parameter Real s0.p = 1e5;\n"
                           "  parameter Real s0.h = 1e5;\n  Real k.port.m_flow;\n  Real k.port.p;\n  Real k.port.h_outflow;\n"
                           "equation\n  s1.ports[1].p = k.port.p;\n  k.port.m_flow + s1.ports[1].m_flow = 0.0;\n"
                           "  s1.ports[2].m_flow = 0.0;\n  s1.ports[1].p = s1.p;\n  s1.ports[2].p = s1.p;\n"
                           "  s1.ports[1].h_outflow = s1.h;\n  s1.ports[2].h_outflow = s1.h;\n  k.port.p = 1.0;\n"
                           "  k.port.h_outflow = 2.0;\nend PKH3;\n"));
}

YMC_TEST(flatten_empty_variable_array_call_kept) {
  // 变量引用展开出的空数组（`anyTrue(c.occupied)`、c[n] 而 n = 0）当实参：调用变率不是常量，omc 照留 `anyTrue({})`、打函数
  // 定义，不求值（testsuite TrueHoldWithResetBefore）。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p13 PKH4）。
  YMC_CHECK_EQ(flatten_src("model PKH4\n  connector C\n    Boolean occupied;\n  end C;\n  function anyTrue\n    input Boolean b[:];\n"
                           "    output Boolean result;\n  algorithm\n    result := false;\n    for i in 1:size(b, 1) loop\n"
                           "      result := result or b[i];\n    end for;\n  end anyTrue;\n  parameter Integer n = 0;\n  C c[n];\n"
                           "  Boolean y = anyTrue(c.occupied) or time > 1;\nend PKH4;\n"),
               std::string("function PKH4.anyTrue\n  input Boolean[:] b;\n  output Boolean result;\nalgorithm\n  result := false;\n"
                           "  for i in 1:size(b, 1) loop\n    result := result or b[i];\n  end for;\nend PKH4.anyTrue;\n\nclass PKH4\n"
                           "  final parameter Integer n = 0;\n  Boolean y = PKH4.anyTrue({}) or time > 1.0;\nend PKH4;\n"));
}

YMC_TEST(flatten_class_extends_composite_array) {
  // 元素类型是 class extends 的复合数组（`Medium.BaseProperties[n] mediums`，`redeclare model extends BaseProperties`）：同标量
  // 复合组件，成员在实例类树里。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p14 PKH6）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PM\n    replaceable partial model BaseProperties\n      Real p;\n"
                           "      Real h;\n      parameter Boolean preferredMediumStates = false;\n    end BaseProperties;\n"
                           "  end PM;\n  package W\n    extends PM;\n"
                           "    redeclare model extends BaseProperties(h(stateSelect = if preferredMediumStates then StateSelect.prefer else StateSelect.default))\n"
                           "    equation\n      p = 2 * h;\n    end BaseProperties;\n  end W;\n  model Pipe\n"
                           "    replaceable package Medium = PM;\n    parameter Integer n = 2;\n"
                           "    Medium.BaseProperties[n] mediums(each preferredMediumStates = true, h(each start = 1));\n  equation\n"
                           "    for i in 1:n loop\n      mediums[i].h = i * time;\n    end for;\n  end Pipe;\nend P;\nmodel PKH6\n"
                           "  P.Pipe pipe(redeclare package Medium = P.W);\nend PKH6;\n"),
               std::string("class PKH6\n  final parameter Integer pipe.n = 2;\n  Real pipe.mediums[1].p;\n"
                           "  Real pipe.mediums[1].h(start = 1.0, stateSelect = StateSelect.prefer);\n"
                           "  final parameter Boolean pipe.mediums[1].preferredMediumStates = true;\n  Real pipe.mediums[2].p;\n"
                           "  Real pipe.mediums[2].h(start = 1.0, stateSelect = StateSelect.prefer);\n"
                           "  final parameter Boolean pipe.mediums[2].preferredMediumStates = true;\nequation\n"
                           "  pipe.mediums[1].p = 2.0 * pipe.mediums[1].h;\n  pipe.mediums[2].p = 2.0 * pipe.mediums[2].h;\n"
                           "  pipe.mediums[1].h = time;\n  pipe.mediums[2].h = 2.0 * time;\nend PKH6;\n"));
}

YMC_TEST(flatten_empty_stream_members_in_equations) {
  // 端口数组上的非 each 类重声明（`ports[nPorts](redeclare package Medium = Medium, …)`）、空数组的字面量切片引用
  // （`ports_mXi_flow[i, :]`）、空 stream 数组成员当 actualStream / inStream 实参、`m_flow * actualStream(h)` 包 smooth(0, …)。
  // 期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p14 PKH7）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PM\n    constant Integer nXi = 0;\n    constant Integer nC = 0;\n  end PM;\n"
                           "  package W\n    extends PM;\n  end W;\n  connector FluidPort\n    replaceable package Medium = PM;\n"
                           "    flow Real m_flow;\n    Real p;\n    stream Real h_outflow;\n    stream Real Xi_outflow[Medium.nXi];\n"
                           "    stream Real C_outflow[Medium.nC];\n  end FluidPort;\n  model Source\n"
                           "    replaceable package Medium = PM;\n    parameter Integer nPorts = 0;\n    parameter Real p = 1e5;\n"
                           "    parameter Real h = 1e5;\n    parameter Real X[Medium.nXi] = fill(0, Medium.nXi);\n"
                           "    parameter Real C[Medium.nC] = fill(0, Medium.nC);\n"
                           "    FluidPort ports[nPorts](redeclare package Medium = Medium, m_flow(each start = 0));\n"
                           "    Real ports_H_flow[nPorts];\n    Real ports_mXi_flow[nPorts, Medium.nXi];\n"
                           "    Real ports_mC_flow[nPorts, Medium.nC];\n  equation\n    ports.p = fill(p, nPorts);\n"
                           "    ports.h_outflow = fill(h, nPorts);\n    ports.Xi_outflow = fill(X, nPorts);\n"
                           "    ports.C_outflow = fill(C, nPorts);\n    for i in 1:nPorts loop\n"
                           "      assert(cardinality(ports[i]) <= 1, \"only one connection per port\");\n"
                           "      ports_H_flow[i] = ports[i].m_flow * actualStream(ports[i].h_outflow);\n"
                           "      ports_mXi_flow[i, :] = ports[i].m_flow * actualStream(ports[i].Xi_outflow);\n"
                           "      ports_mC_flow[i, :] = ports[i].m_flow * inStream(ports[i].C_outflow);\n    end for;\n  end Source;\n"
                           "  model Sink\n    replaceable package Medium = PM;\n"
                           "    FluidPort port(redeclare package Medium = Medium);\n    Real H_flow;\n    Real mXi_flow[Medium.nXi];\n"
                           "  equation\n    H_flow = port.m_flow * actualStream(port.h_outflow);\n"
                           "    mXi_flow = port.m_flow * actualStream(port.Xi_outflow);\n    port.p = 1;\n    port.h_outflow = 2;\n"
                           "    port.Xi_outflow = fill(0, Medium.nXi);\n    port.C_outflow = fill(0, Medium.nC);\n  end Sink;\n"
                           "end P;\nmodel PKH7\n  P.Source s1(nPorts = 2, redeclare package Medium = P.W);\n"
                           "  P.Source s0(redeclare package Medium = P.W);\n  P.Sink k(redeclare package Medium = P.W);\nequation\n"
                           "  connect(s1.ports[1], k.port);\nend PKH7;\n"),
               std::string("class PKH7\n  final parameter Integer s1.nPorts = 2;\n  parameter Real s1.p = 1e5;\n"
                           "  parameter Real s1.h = 1e5;\n  Real s1.ports[1].m_flow(start = 0.0);\n  Real s1.ports[1].p;\n"
                           "  Real s1.ports[1].h_outflow;\n  Real s1.ports[2].m_flow(start = 0.0);\n  Real s1.ports[2].p;\n"
                           "  Real s1.ports[2].h_outflow;\n  Real s1.ports_H_flow[1];\n  Real s1.ports_H_flow[2];\n"
                           "  final parameter Integer s0.nPorts = 0;\n  parameter Real s0.p = 1e5;\n  parameter Real s0.h = 1e5;\n"
                           "  Real k.port.m_flow;\n  Real k.port.p;\n  Real k.port.h_outflow;\n  Real k.H_flow;\nequation\n"
                           "  s1.ports[1].p = k.port.p;\n  k.port.m_flow + s1.ports[1].m_flow = 0.0;\n  s1.ports[2].m_flow = 0.0;\n"
                           "  s1.ports[1].p = s1.p;\n  s1.ports[2].p = s1.p;\n  s1.ports[1].h_outflow = s1.h;\n"
                           "  s1.ports[2].h_outflow = s1.h;\n"
                           "  s1.ports_H_flow[1] = smooth(0, s1.ports[1].m_flow * (if s1.ports[1].m_flow > 0.0 then k.port.h_outflow else s1.ports[1].h_outflow));\n"
                           "  s1.ports_H_flow[2] = smooth(0, s1.ports[2].m_flow * s1.ports[2].h_outflow);\n"
                           "  k.H_flow = smooth(0, k.port.m_flow * (if k.port.m_flow > 0.0 then s1.ports[1].h_outflow else k.port.h_outflow));\n"
                           "  k.port.p = 1.0;\n  k.port.h_outflow = 2.0;\nend PKH7;\n"));
}

YMC_TEST(flatten_actualstream_product_forms) {
  // evaluateActualStreamMul（NFConnectEquations.mo:1014-1038 v1.25.0）：cref 换到前面、cref 是关联 flow 且结果是 if 式才包
  // smooth；flow 方向已知（min = 0）不出 if；别的因子照常求值。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p14 PKH8）。
  YMC_CHECK_EQ(flatten_src("model PKH8\n  connector FP\n    flow Real m_flow;\n    Real p;\n    stream Real h_outflow;\n  end FP;\n"
                           "  connector FPin\n    flow Real m_flow(min = 0);\n    Real p;\n    stream Real h_outflow;\n  end FPin;\n"
                           "  model Comp\n    FP port;\n    Real x = time;\n    Real H1;\n    Real H2;\n    Real H3;\n    Real H4;\n"
                           "  equation\n    port.h_outflow = 3;\n    H1 = actualStream(port.h_outflow) * port.m_flow;\n"
                           "    H2 = x * actualStream(port.h_outflow);\n    H3 = 2 * actualStream(port.h_outflow);\n"
                           "    H4 = -port.m_flow * actualStream(port.h_outflow);\n  end Comp;\n  model CompIn\n    FPin port;\n"
                           "    Real H;\n  equation\n    port.h_outflow = 4;\n    H = port.m_flow * actualStream(port.h_outflow);\n"
                           "  end CompIn;\n  Comp a;\n  Comp b;\n  CompIn c;\n  Comp d;\nequation\n  connect(a.port, b.port);\n"
                           "  connect(c.port, d.port);\n  a.port.p = 1;\n  c.port.p = 1;\n  a.port.m_flow = time;\n"
                           "  c.port.m_flow = time;\nend PKH8;\n"),
               std::string("class PKH8\n  Real a.port.m_flow;\n  Real a.port.p;\n  Real a.port.h_outflow;\n  Real a.x = time;\n"
                           "  Real a.H1;\n  Real a.H2;\n  Real a.H3;\n  Real a.H4;\n  Real b.port.m_flow;\n  Real b.port.p;\n"
                           "  Real b.port.h_outflow;\n  Real b.x = time;\n  Real b.H1;\n  Real b.H2;\n  Real b.H3;\n  Real b.H4;\n"
                           "  Real c.port.m_flow(min = 0.0);\n  Real c.port.p;\n  Real c.port.h_outflow;\n  Real c.H;\n"
                           "  Real d.port.m_flow;\n  Real d.port.p;\n  Real d.port.h_outflow;\n  Real d.x = time;\n  Real d.H1;\n"
                           "  Real d.H2;\n  Real d.H3;\n  Real d.H4;\nequation\n  a.port.p = b.port.p;\n  c.port.p = d.port.p;\n"
                           "  b.port.m_flow + a.port.m_flow = 0.0;\n  d.port.m_flow + c.port.m_flow = 0.0;\n"
                           "  a.port.h_outflow = 3.0;\n"
                           "  a.H1 = smooth(0, a.port.m_flow * (if a.port.m_flow > 0.0 then b.port.h_outflow else a.port.h_outflow));\n"
                           "  a.H2 = a.x * (if a.port.m_flow > 0.0 then b.port.h_outflow else a.port.h_outflow);\n"
                           "  a.H3 = 2.0 * (if a.port.m_flow > 0.0 then b.port.h_outflow else a.port.h_outflow);\n"
                           "  a.H4 = -smooth(0, a.port.m_flow * (if a.port.m_flow > 0.0 then b.port.h_outflow else a.port.h_outflow));\n"
                           "  b.port.h_outflow = 3.0;\n"
                           "  b.H1 = smooth(0, b.port.m_flow * (if b.port.m_flow > 0.0 then a.port.h_outflow else b.port.h_outflow));\n"
                           "  b.H2 = b.x * (if b.port.m_flow > 0.0 then a.port.h_outflow else b.port.h_outflow);\n"
                           "  b.H3 = 2.0 * (if b.port.m_flow > 0.0 then a.port.h_outflow else b.port.h_outflow);\n"
                           "  b.H4 = -smooth(0, b.port.m_flow * (if b.port.m_flow > 0.0 then a.port.h_outflow else b.port.h_outflow));\n"
                           "  c.port.h_outflow = 4.0;\n  c.H = c.port.m_flow * d.port.h_outflow;\n  d.port.h_outflow = 3.0;\n"
                           "  d.H1 = smooth(0, d.port.m_flow * d.port.h_outflow);\n  d.H2 = d.x * d.port.h_outflow;\n"
                           "  d.H3 = 2.0 * d.port.h_outflow;\n  d.H4 = -smooth(0, d.port.m_flow * d.port.h_outflow);\n"
                           "  a.port.p = 1.0;\n  c.port.p = 1.0;\n  a.port.m_flow = time;\n  c.port.m_flow = time;\nend PKH8;\n"));
}

YMC_TEST(flatten_empty_array_index_vector_slice) {
  // 空数组用已展开的下标向量 / 范围取（`C_start[1:Medium.nC]`、nC = 0 → `{}`；`x[1:2, :]`）：结果是空数组，所在方程（含初始
  // 方程）不产生标量方程。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p15 PKH9）。
  YMC_CHECK_EQ(flatten_src("package P\n  partial package PM\n    constant Integer nC = 0;\n"
                           "    constant Real C_nominal[nC] = fill(1, nC);\n  end PM;\n  package W\n    extends PM;\n  end W;\n"
                           "  model Vol\n    replaceable package Medium = PM;\n"
                           "    parameter Real C_start[Medium.nC] = fill(0, Medium.nC);\n    Real m = 1 + time;\n"
                           "    Real mC_scaled[Medium.nC];\n    Real Cs[3, Medium.nC];\n    Real x[2, Medium.nC];\n"
                           "  initial equation\n    mC_scaled = m * C_start[1:Medium.nC] ./ Medium.C_nominal;\n  equation\n"
                           "    der(mC_scaled) = zeros(Medium.nC);\n    Cs = fill(C_start[1:Medium.nC], 3);\n"
                           "    x[1:2, :] = zeros(2, Medium.nC);\n  end Vol;\nend P;\nmodel PKH9\n"
                           "  P.Vol v(redeclare package Medium = P.W);\nend PKH9;\n"),
               std::string("class PKH9\n  Real v.m = 1.0 + time;\nend PKH9;\n"));
}

YMC_TEST(flatten_nested_function_in_instance_function) {
  // 经重声明的包找到的函数里 protected 的嵌套函数：压平名 = 外层函数的压平名 + 名（`PKI1.f.WallFriction.pressureLoss_m_flow.
  // interpolateInRegion2`，MSL Fluid WallFriction.Detailed）。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p16 PKI1）。
  YMC_CHECK_EQ(flatten_src("package P\n  package WF\n    partial package PartialWallFriction\n"
                           "      replaceable partial function pressureLoss_m_flow\n        input Real m_flow;\n"
                           "        output Real dp;\n      end pressureLoss_m_flow;\n    end PartialWallFriction;\n"
                           "    package Detailed\n      extends PartialWallFriction;\n"
                           "      redeclare function extends pressureLoss_m_flow\n      protected\n"
                           "        function interpolateInRegion2\n          input Real x;\n          output Real y;\n"
                           "        algorithm\n          y := 2 * x;\n        end interpolateInRegion2;\n      algorithm\n"
                           "        dp := interpolateInRegion2(m_flow);\n      end pressureLoss_m_flow;\n    end Detailed;\n"
                           "  end WF;\n  model Flow\n    replaceable package WallFriction = WF.PartialWallFriction;\n"
                           "    Real m_flow = time;\n    Real dp = WallFriction.pressureLoss_m_flow(m_flow);\n  end Flow;\nend P;\n"
                           "model PKI1\n  P.Flow f(redeclare package WallFriction = P.WF.Detailed);\nend PKI1;\n"),
               std::string("function PKI1.f.WallFriction.pressureLoss_m_flow\n  input Real m_flow;\n  output Real dp;\nalgorithm\n"
                           "  dp := PKI1.f.WallFriction.pressureLoss_m_flow.interpolateInRegion2(m_flow);\n"
                           "end PKI1.f.WallFriction.pressureLoss_m_flow;\n\n"
                           "function PKI1.f.WallFriction.pressureLoss_m_flow.interpolateInRegion2\n  input Real x;\n  output Real y;\n"
                           "algorithm\n  y := 2.0 * x;\nend PKI1.f.WallFriction.pressureLoss_m_flow.interpolateInRegion2;\n\n"
                           "class PKI1\n  Real f.m_flow = time;\n  Real f.dp = PKI1.f.WallFriction.pressureLoss_m_flow(f.m_flow);\n"
                           "end PKI1;\n"));
}

YMC_TEST(flatten_nested_function_names) {
  // 包里函数 / 模型里函数的嵌套函数：`P.f.g`、`PKI2.m.h.k`（此前模型里的这个静默打成 `P.M.h.k`）。期望 = omc 1.25.0 实跑
  // （2026-10-14，探针 pk-p16 PKI2）。
  YMC_CHECK_EQ(flatten_src("package P\n  function f\n    input Real x;\n    output Real y;\n  protected\n    function g\n"
                           "      input Real a;\n      output Real b;\n    algorithm\n      b := a + 1;\n    end g;\n  algorithm\n"
                           "    y := g(x) + g(2 * x);\n  end f;\n  model M\n    function h\n      input Real x;\n"
                           "      output Real y;\n    protected\n      function k\n        input Real a;\n        output Real b;\n"
                           "      algorithm\n        b := 3 * a;\n      end k;\n    algorithm\n      y := k(x);\n    end h;\n"
                           "    Real z = h(time);\n  end M;\nend P;\nmodel PKI2\n  Real y = P.f(time);\n  P.M m;\nend PKI2;\n"),
               std::string("function P.f\n  input Real x;\n  output Real y;\nalgorithm\n  y := P.f.g(x) + P.f.g(2.0 * x);\nend P.f;\n"
                           "\nfunction P.f.g\n  input Real a;\n  output Real b;\nalgorithm\n  b := a + 1.0;\nend P.f.g;\n\n"
                           "function PKI2.m.h\n  input Real x;\n  output Real y;\nalgorithm\n  y := PKI2.m.h.k(x);\nend PKI2.m.h;\n\n"
                           "function PKI2.m.h.k\n  input Real a;\n  output Real b;\nalgorithm\n  b := 3.0 * a;\nend PKI2.m.h.k;\n\n"
                           "class PKI2\n  Real y = P.f(time);\n  Real m.z = PKI2.m.h(time);\nend PKI2;\n"));
}

YMC_TEST(flatten_inherited_outer_in_condition_forward) {
  // 写在 `extends PartialFlow(redeclare package WallFriction = WF2)` 的类里的 if 条件 `system.use_eps_Re`（system 是继承来的
  // outer、inner 声明在后）：求值器先试枚举字面量时类路径首段查到组件当没找到；向前引用第一遍不落到 SCode 层。期望 = omc
  // 1.25.0 实跑（2026-10-14，探针 pk-p16 PKI6）。
  YMC_CHECK_EQ(flatten_src("package P\n  model System\n    parameter Boolean use_eps_Re = false;\n  end System;\n  package WF1\n"
                           "    constant Boolean dp_is_zero = false;\n  end WF1;\n  package WF2\n"
                           "    constant Boolean dp_is_zero = true;\n  end WF2;\n  partial model PartialFlow\n"
                           "    outer System system;\n    replaceable package WallFriction = WF1;\n"
                           "    Real dp_nominal(start = 1, fixed = false);\n  end PartialFlow;\n  model Detailed\n"
                           "    extends PartialFlow(redeclare package WallFriction = WF2);\n    Real x;\n  initial equation\n"
                           "    if system.use_eps_Re then\n      dp_nominal = 1;\n    else\n      dp_nominal = 2;\n    end if;\n"
                           "  equation\n    der(x) = dp_nominal;\n  end Detailed;\n  model Pipe\n    Detailed fm;\n  end Pipe;\n"
                           "end P;\nmodel PKI6\n  P.Pipe pipe;\n  inner P.System system(use_eps_Re = true);\nend PKI6;\n"),
               std::string("class PKI6\n  Real pipe.fm.dp_nominal(start = 1.0, fixed = false);\n  Real pipe.fm.x;\n"
                           "  final parameter Boolean system.use_eps_Re = true;\ninitial equation\n  pipe.fm.dp_nominal = 1.0;\n"
                           "equation\n  der(pipe.fm.x) = pipe.fm.dp_nominal;\nend PKI6;\n"));
}

YMC_TEST(flatten_inherited_function_default_in_base_scope) {
  // class extends 的函数从基类继承来的输入缺省（`crossArea = pi * diameter ^ 2 / 4`，pi 是基类所在包里 import 的）：在声明它的
  // 函数的作用域里解析（import 不继承，MLS §13.2.1）。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p17 PKI7）。
  YMC_CHECK_EQ(flatten_src("package P\n  package Constants\n    constant Real pi = 3.141592653589793;\n  end Constants;\n"
                           "  package WF\n    partial package PartialWallFriction\n      import P.Constants.pi;\n"
                           "      replaceable partial function pressureLoss_m_flow\n        input Real m_flow;\n"
                           "        input Real diameter;\n        input Real crossArea = pi * diameter ^ 2 / 4;\n"
                           "        output Real dp;\n      end pressureLoss_m_flow;\n    end PartialWallFriction;\n"
                           "    package Detailed\n      extends PartialWallFriction;\n"
                           "      redeclare function extends pressureLoss_m_flow\n      algorithm\n        dp := m_flow / crossArea;\n"
                           "      end pressureLoss_m_flow;\n    end Detailed;\n  end WF;\n  model Flow\n"
                           "    replaceable package WallFriction = WF.PartialWallFriction;\n    Real m_flow = time;\n"
                           "    Real dp = WallFriction.pressureLoss_m_flow(m_flow, 0.1);\n  end Flow;\nend P;\nmodel PKI7\n"
                           "  P.Flow f(redeclare package WallFriction = P.WF.Detailed);\nend PKI7;\n"),
               std::string("function PKI7.f.WallFriction.pressureLoss_m_flow\n  input Real m_flow;\n  input Real diameter;\n"
                           "  input Real crossArea = 3.141592653589793 * diameter ^ 2.0 / 4.0;\n  output Real dp;\nalgorithm\n"
                           "  dp := m_flow / crossArea;\nend PKI7.f.WallFriction.pressureLoss_m_flow;\n\nclass PKI7\n"
                           "  Real f.m_flow = time;\n"
                           "  Real f.dp = PKI7.f.WallFriction.pressureLoss_m_flow(f.m_flow, 0.1, 0.007853981633974483);\nend PKI7;\n"));
}

YMC_TEST(flatten_vectorized_builtin_empty_array) {
  // 向量化内建的实参是零长数组（`abs(m_flows)`、m_flows[n] 而 n = 0）：结果是同形空数组，所在等式不产生方程。期望 = omc 1.25.0
  // 实跑（2026-10-14，探针 pk-p18 PKJ1）。
  YMC_CHECK_EQ(flatten_src("model PKJ1\n  parameter Integer n = 0;\n  Real m_flows[n];\n  Real Res[n];\n  Real x[2];\n  Real z[2];\n"
                           "equation\n  Res = abs(m_flows) * 2;\n  m_flows = fill(time, n);\n  x = {time, 2 * time};\n  z = abs(x);\n"
                           "end PKJ1;\n"),
               std::string("class PKJ1\n  final parameter Integer n = 0;\n  Real x[1];\n  Real x[2];\n  Real z[1];\n  Real z[2];\n"
                           "equation\n  x[1] = time;\n  x[2] = 2.0 * time;\n  z = array(abs(x[$i1]) for $i1 in 1:2);\nend PKJ1;\n"));
}

YMC_TEST(flatten_vectorized_builtin_composite_field) {
  // 向量化内建取复合数组的字段（`abs(ports.m_flow)` → `abs(s2.ports[$i1].m_flow)`，MSL Fluid PartialFlowSource 的 assert）；
  // 只有一个端口时整条 assert 化简掉。期望 = omc 1.25.0 实跑（2026-10-14，探针 pk-p18 PKJ2）。
  YMC_CHECK_EQ(flatten_src("package P\n  connector FP\n    flow Real m_flow;\n    Real p;\n  end FP;\n  model Src\n"
                           "    parameter Integer nPorts = 1;\n    FP ports[nPorts];\n  equation\n"
                           "    assert(abs(sum(abs(ports.m_flow)) - max(abs(ports.m_flow))) <= 1e-60, \"only one connection\");\n"
                           "    for i in 1:nPorts loop\n      ports[i].p = 1;\n    end for;\n  end Src;\nend P;\nmodel PKJ2\n"
                           "  P.Src s1;\n  P.Src s2(nPorts = 2);\nend PKJ2;\n"),
               std::string("class PKJ2\n  final parameter Integer s1.nPorts = 1;\n  Real s1.ports[1].m_flow;\n  Real s1.ports[1].p;\n"
                           "  final parameter Integer s2.nPorts = 2;\n  Real s2.ports[1].m_flow;\n  Real s2.ports[1].p;\n"
                           "  Real s2.ports[2].m_flow;\n  Real s2.ports[2].p;\nequation\n  s1.ports[1].m_flow = 0.0;\n"
                           "  s2.ports[1].m_flow = 0.0;\n  s2.ports[2].m_flow = 0.0;\n  s1.ports[1].p = 1.0;\n"
                           "  assert(abs(abs(s2.ports[1].m_flow) + abs(s2.ports[2].m_flow) - max(abs(s2.ports[$i1].m_flow) for $i1 in 1:2)) <= 1e-60, \"only one connection\");\n"
                           "  s2.ports[1].p = 1.0;\n  s2.ports[2].p = 1.0;\nend PKJ2;\n"));
}

YMC_TEST(flatten_redeclared_function_short_class_modifiers) {
  // 组件修饰里重声明的函数 `redeclare function flowCharacteristic = quadraticFlow(V_flow_nominal = {…}, head_nominal = {…})`：
  // 按实例路径命名 `PKK1.pumps.flowCharacteristic`，短类修饰成输入缺省、调用处补齐。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p19 PKK1）。
  YMC_CHECK_EQ(flatten_src("package P\n  package Chars\n    partial function baseFlow\n      input Real V_flow;\n"
                           "      output Real head;\n    end baseFlow;\n    function quadraticFlow\n      extends baseFlow;\n"
                           "      input Real V_flow_nominal[3];\n      input Real head_nominal[3];\n    algorithm\n"
                           "      head := head_nominal[1] + V_flow * (V_flow_nominal[2] - head_nominal[3]);\n    end quadraticFlow;\n"
                           "  end Chars;\n  partial model PartialPump\n    replaceable function flowCharacteristic = Chars.baseFlow;\n"
                           "    Real V_flow = time;\n    Real head;\n  equation\n    head = flowCharacteristic(V_flow);\n"
                           "  end PartialPump;\n  model Pump\n    extends PartialPump;\n  end Pump;\nend P;\nmodel PKK1\n"
                           "  P.Pump pumps(redeclare function flowCharacteristic = P.Chars.quadraticFlow(V_flow_nominal = {0, 0.25, 0.5}, head_nominal = {100, 60, 0}));\n"
                           "end PKK1;\n"),
               std::string("function PKK1.pumps.flowCharacteristic\n  input Real V_flow;\n"
                           "  input Real[3] V_flow_nominal = {0.0, 0.25, 0.5};\n  input Real[3] head_nominal = {100.0, 60.0, 0.0};\n"
                           "  output Real head;\nalgorithm\n"
                           "  head := head_nominal[1] + V_flow * (V_flow_nominal[2] - head_nominal[3]);\n"
                           "end PKK1.pumps.flowCharacteristic;\n\nclass PKK1\n  Real pumps.V_flow = time;\n  Real pumps.head;\n"
                           "equation\n"
                           "  pumps.head = PKK1.pumps.flowCharacteristic(pumps.V_flow, {0.0, 0.25, 0.5}, {100.0, 60.0, 0.0});\n"
                           "end PKK1;\n"));
}

YMC_TEST(flatten_redeclared_function_extends_modifier_params) {
  // extends 修饰里重声明的函数（MSL Fluid ControlledPump）：修饰引用派生类的参数——函数定义里打求值后的缺省，调用处补
  // `{0.0, pump.q, 1.5 * pump.q}`。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p19 PKK3）。
  YMC_CHECK_EQ(flatten_src("package P\n  package Chars\n    partial function baseFlow\n      input Real V_flow;\n"
                           "      output Real head;\n    end baseFlow;\n    function quadraticFlow\n      extends baseFlow;\n"
                           "      input Real V_flow_nominal[3];\n      input Real head_nominal[3];\n    algorithm\n"
                           "      head := head_nominal[1] + V_flow * (V_flow_nominal[2] - head_nominal[3]);\n    end quadraticFlow;\n"
                           "    function linearFlow\n      extends baseFlow;\n      input Real a = 2;\n    algorithm\n"
                           "      head := a * V_flow;\n    end linearFlow;\n  end Chars;\n  partial model PartialPump\n"
                           "    replaceable function flowCharacteristic = Chars.baseFlow;\n    Real V_flow = time;\n    Real head;\n"
                           "  equation\n    head = flowCharacteristic(V_flow);\n  end PartialPump;\n  model Pump\n"
                           "    extends PartialPump;\n  end Pump;\n  model CPump\n    parameter Real q = 1;\n"
                           "    parameter Real h = 3;\n"
                           "    extends PartialPump(redeclare replaceable function flowCharacteristic = Chars.quadraticFlow(V_flow_nominal = {0, q, 1.5 * q}, head_nominal = {2 * h, h, 0}));\n"
                           "  end CPump;\n  model LPump\n    extends PartialPump;\n    redeclare function flowCharacteristic\n"
                           "      extends Chars.baseFlow;\n    algorithm\n      head := 3 * V_flow;\n    end flowCharacteristic;\n"
                           "  end LPump;\n  model ZPump\n    extends PartialPump;\n    Real y = time * flowCharacteristic(0);\n"
                           "  end ZPump;\n  model M\n    parameter Real k = 0.25;\n"
                           "    P.Pump pumps(redeclare function flowCharacteristic = P.Chars.quadraticFlow(V_flow_nominal = {0, k, 2 * k}, head_nominal = {100, 60, 0}));\n"
                           "  end M;\nend P;\nmodel PKK3\n  P.CPump pump(q = 2);\nend PKK3;\n"),
               std::string("function PKK3.pump.flowCharacteristic\n  input Real V_flow;\n"
                           "  input Real[3] V_flow_nominal = {0.0, 2.0, 3.0};\n  input Real[3] head_nominal = {6.0, 3.0, 0.0};\n"
                           "  output Real head;\nalgorithm\n"
                           "  head := head_nominal[1] + V_flow * (V_flow_nominal[2] - head_nominal[3]);\n"
                           "end PKK3.pump.flowCharacteristic;\n\nclass PKK3\n  parameter Real pump.q = 2.0;\n"
                           "  parameter Real pump.h = 3.0;\n  Real pump.V_flow = time;\n  Real pump.head;\nequation\n"
                           "  pump.head = PKK3.pump.flowCharacteristic(pump.V_flow, {0.0, pump.q, 1.5 * pump.q}, {2.0 * pump.h, pump.h, 0.0});\n"
                           "end PKK3;\n"));
}

YMC_TEST(flatten_redeclared_function_long_element) {
  // 派生类体里长形态的函数重声明 `redeclare function flowCharacteristic … end flowCharacteristic;`，基类的方程调它：
  // `PKK4.pump.flowCharacteristic`。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p19 PKK4）。
  YMC_CHECK_EQ(flatten_src("package P\n  package Chars\n    partial function baseFlow\n      input Real V_flow;\n"
                           "      output Real head;\n    end baseFlow;\n    function quadraticFlow\n      extends baseFlow;\n"
                           "      input Real V_flow_nominal[3];\n      input Real head_nominal[3];\n    algorithm\n"
                           "      head := head_nominal[1] + V_flow * (V_flow_nominal[2] - head_nominal[3]);\n    end quadraticFlow;\n"
                           "    function linearFlow\n      extends baseFlow;\n      input Real a = 2;\n    algorithm\n"
                           "      head := a * V_flow;\n    end linearFlow;\n  end Chars;\n  partial model PartialPump\n"
                           "    replaceable function flowCharacteristic = Chars.baseFlow;\n    Real V_flow = time;\n    Real head;\n"
                           "  equation\n    head = flowCharacteristic(V_flow);\n  end PartialPump;\n  model Pump\n"
                           "    extends PartialPump;\n  end Pump;\n  model CPump\n    parameter Real q = 1;\n"
                           "    parameter Real h = 3;\n"
                           "    extends PartialPump(redeclare replaceable function flowCharacteristic = Chars.quadraticFlow(V_flow_nominal = {0, q, 1.5 * q}, head_nominal = {2 * h, h, 0}));\n"
                           "  end CPump;\n  model LPump\n    extends PartialPump;\n    redeclare function flowCharacteristic\n"
                           "      extends Chars.baseFlow;\n    algorithm\n      head := 3 * V_flow;\n    end flowCharacteristic;\n"
                           "  end LPump;\n  model ZPump\n    extends PartialPump;\n    Real y = time * flowCharacteristic(0);\n"
                           "  end ZPump;\n  model M\n    parameter Real k = 0.25;\n"
                           "    P.Pump pumps(redeclare function flowCharacteristic = P.Chars.quadraticFlow(V_flow_nominal = {0, k, 2 * k}, head_nominal = {100, 60, 0}));\n"
                           "  end M;\nend P;\nmodel PKK4\n  P.LPump pump;\nend PKK4;\n"),
               std::string("function PKK4.pump.flowCharacteristic\n  input Real V_flow;\n  output Real head;\nalgorithm\n"
                           "  head := 3.0 * V_flow;\nend PKK4.pump.flowCharacteristic;\n\nclass PKK4\n  Real pump.V_flow = time;\n"
                           "  Real pump.head;\nequation\n  pump.head = PKK4.pump.flowCharacteristic(pump.V_flow);\nend PKK4;\n"));
}

YMC_TEST(flatten_redeclared_function_constant_call_folded) {
  // 重声明的函数以常量实参调用（`flowCharacteristic(0)`，缺省来自短类修饰）：整个调用求值成 100.0。期望 = omc 1.25.0
  // 实跑（2026-10-15，探针 pk-p19 PKK5）。
  YMC_CHECK_EQ(flatten_src("package P\n  package Chars\n    partial function baseFlow\n      input Real V_flow;\n"
                           "      output Real head;\n    end baseFlow;\n    function quadraticFlow\n      extends baseFlow;\n"
                           "      input Real V_flow_nominal[3];\n      input Real head_nominal[3];\n    algorithm\n"
                           "      head := head_nominal[1] + V_flow * (V_flow_nominal[2] - head_nominal[3]);\n    end quadraticFlow;\n"
                           "    function linearFlow\n      extends baseFlow;\n      input Real a = 2;\n    algorithm\n"
                           "      head := a * V_flow;\n    end linearFlow;\n  end Chars;\n  partial model PartialPump\n"
                           "    replaceable function flowCharacteristic = Chars.baseFlow;\n    Real V_flow = time;\n    Real head;\n"
                           "  equation\n    head = flowCharacteristic(V_flow);\n  end PartialPump;\n  model Pump\n"
                           "    extends PartialPump;\n  end Pump;\n  model CPump\n    parameter Real q = 1;\n"
                           "    parameter Real h = 3;\n"
                           "    extends PartialPump(redeclare replaceable function flowCharacteristic = Chars.quadraticFlow(V_flow_nominal = {0, q, 1.5 * q}, head_nominal = {2 * h, h, 0}));\n"
                           "  end CPump;\n  model LPump\n    extends PartialPump;\n    redeclare function flowCharacteristic\n"
                           "      extends Chars.baseFlow;\n    algorithm\n      head := 3 * V_flow;\n    end flowCharacteristic;\n"
                           "  end LPump;\n  model ZPump\n    extends PartialPump;\n    Real y = time * flowCharacteristic(0);\n"
                           "  end ZPump;\n  model M\n    parameter Real k = 0.25;\n"
                           "    P.Pump pumps(redeclare function flowCharacteristic = P.Chars.quadraticFlow(V_flow_nominal = {0, k, 2 * k}, head_nominal = {100, 60, 0}));\n"
                           "  end M;\nend P;\nmodel PKK5\n"
                           "  P.ZPump pumps(redeclare function flowCharacteristic = P.Chars.quadraticFlow(V_flow_nominal = {0, 0.25, 0.5}, head_nominal = {100, 60, 0}));\n"
                           "end PKK5;\n"),
               std::string("function PKK5.pumps.flowCharacteristic\n  input Real V_flow;\n"
                           "  input Real[3] V_flow_nominal = {0.0, 0.25, 0.5};\n  input Real[3] head_nominal = {100.0, 60.0, 0.0};\n"
                           "  output Real head;\nalgorithm\n"
                           "  head := head_nominal[1] + V_flow * (V_flow_nominal[2] - head_nominal[3]);\n"
                           "end PKK5.pumps.flowCharacteristic;\n\nclass PKK5\n  Real pumps.V_flow = time;\n  Real pumps.head;\n"
                           "  Real pumps.y = time * 100.0;\nequation\n"
                           "  pumps.head = PKK5.pumps.flowCharacteristic(pumps.V_flow, {0.0, 0.25, 0.5}, {100.0, 60.0, 0.0});\n"
                           "end PKK5;\n"));
}

YMC_TEST(flatten_fill_column_in_matrix_construction) {
  // 矩阵构造里的 `ones(3)` / `fill(2.5, 3)` 列逐元素展开（ExpandExp.expandBuiltinFill；MSL Fluid quadraticFlow 的
  // `[ones(3), V_flow_nominal, V_flow_nominal2]`），函数里与模型里都是。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p19 PKK13）。
  YMC_CHECK_EQ(flatten_src("model PKK13\n  function f\n    input Real v[3];\n    output Real y;\n  protected\n"
                           "    Real m[3, 3] = [ones(3), v, fill(2.5, 3)];\n  algorithm\n    y := m[1, 1] + m[3, 2] + m[2, 3];\n"
                           "  end f;\n  Real y[3] = {time, 2 * time, 3};\n  Real z = f(y);\n  Real m2[3, 2] = [ones(3), y];\n"
                           "  Real m3[3, 2] = [fill(2, 3, 1), y];\nend PKK13;\n"),
               std::string("function PKK13.f\n  input Real[3] v;\n  output Real y;\n"
                           "  protected Real[3, 3] m = {{1.0, v[1], 2.5}, {1.0, v[2], 2.5}, {1.0, v[3], 2.5}};\nalgorithm\n"
                           "  y := m[1,1] + m[3,2] + m[2,3];\nend PKK13.f;\n\nclass PKK13\n  Real y[1];\n  Real y[2];\n  Real y[3];\n"
                           "  Real z = PKK13.f(y);\n  Real m2[1,1];\n  Real m2[1,2];\n  Real m2[2,1];\n  Real m2[2,2];\n"
                           "  Real m2[3,1];\n  Real m2[3,2];\n  Real m3[1,1];\n  Real m3[1,2];\n  Real m3[2,1];\n  Real m3[2,2];\n"
                           "  Real m3[3,1];\n  Real m3[3,2];\nequation\n  y = {time, 2.0 * time, 3.0};\n"
                           "  m2 = {{1.0, y[1]}, {1.0, y[2]}, {1.0, y[3]}};\n  m3 = {{2.0, y[1]}, {2.0, y[2]}, {2.0, y[3]}};\n"
                           "end PKK13;\n"));
}

YMC_TEST(flatten_function_dimension_refers_to_itself) {
  // 函数形参的维引用自己（MSL Matrices.solve 的 `input Real A[:, size(A, 1)]`）：照留 `size(A, 1)`。期望 = omc 1.25.0
  // 实跑（2026-10-15，探针 pk-p19 PKK14）。
  YMC_CHECK_EQ(flatten_src("model PKK14\n  function solve2\n    input Real A[:, size(A, 1)];\n    input Real b[size(A, 1)];\n"
                           "    output Real x[size(b, 1)];\n  protected\n    Real Awork[size(A, 1), size(A, 1)] = A;\n  algorithm\n"
                           "    x := b / Awork[1, 1];\n  end solve2;\n  Real b[2] = {time, 1};\n"
                           "  Real x[2] = solve2({{2, 0}, {0, 2}}, b);\nend PKK14;\n"),
               std::string("function PKK14.solve2\n  input Real[:, size(A, 1)] A;\n  input Real[size(A, 1)] b;\n"
                           "  output Real[size(b, 1)] x;\n  protected Real[size(A, 1), size(A, 1)] Awork = A;\nalgorithm\n"
                           "  x := b / Awork[1,1];\nend PKK14.solve2;\n\nclass PKK14\n  Real b[1];\n  Real b[2];\n  Real x[1];\n"
                           "  Real x[2];\nequation\n  b = {time, 1.0};\n  x = PKK14.solve2({{2.0, 0.0}, {0.0, 2.0}}, b);\nend PKK14;\n"));
}

YMC_TEST(flatten_duplicate_outer_lookup_in_base) {
  // 两个基类各声明同一个 `outer System system`，去重后在被丢掉那份所在的基类里查 `system.T_start`：查到留下的那个（连好
  // inner，参数），绑定不是更高变率（ClassTree.replaceDuplicates）。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p19 PKK15）。
  YMC_CHECK_EQ(flatten_src("model PKK15\n  model System\n    parameter Real T_start = 300;\n    parameter Real p_start = 1e5;\n"
                           "  end System;\n  partial model TwoPort\n    outer System system \"System wide properties\";\n"
                           "    Real m_flow = time;\n  end TwoPort;\n  partial model LumpedVolume\n"
                           "    outer System system \"System properties\";\n    parameter Boolean use_T_start = true;\n"
                           "    parameter Real T_start = if use_T_start then system.T_start else 2 * p_start;\n"
                           "    parameter Real p_start = system.p_start;\n    Real T(start = T_start);\n  equation\n"
                           "    der(T) = -T + T_start;\n  end LumpedVolume;\n  model Pump\n    extends TwoPort;\n"
                           "    extends LumpedVolume(final p_start = 2e5);\n  end Pump;\n  inner System system;\n  Pump pump;\n"
                           "end PKK15;\n"),
               std::string("class PKK15\n  parameter Real system.T_start = 300.0;\n  parameter Real system.p_start = 1e5;\n"
                           "  Real pump.m_flow = time;\n  parameter Boolean pump.use_T_start = true;\n"
                           "  parameter Real pump.T_start = if pump.use_T_start then system.T_start else 2.0 * pump.p_start;\n"
                           "  final parameter Real pump.p_start = 2e5;\n  Real pump.T(start = pump.T_start);\nequation\n"
                           "  der(pump.T) = pump.T_start - pump.T;\nend PKK15;\n"));
}

YMC_TEST(flatten_record_constructor_per_package_instance) {
  // 同一个记录定义经不同的包实例各有一个构造函数（`pump.Medium.ThermodynamicState`、`pump.monitoring.Medium.…`、
  // `P16.W.ThermodynamicState`），先引到的不能挡掉后面的。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p19 PKK18）。
  YMC_CHECK_EQ(flatten_src("package P16\n  partial package PM\n    replaceable record ThermodynamicState\n      Real p;\n"
                           "    end ThermodynamicState;\n    replaceable partial function setState\n      input Real p;\n"
                           "      output ThermodynamicState s;\n    end setState;\n  end PM;\n  package W\n    extends PM;\n"
                           "    redeclare record extends ThermodynamicState\n      Real T;\n    end ThermodynamicState;\n"
                           "    redeclare function extends setState\n    algorithm\n      s := ThermodynamicState(p = p, T = 300);\n"
                           "    end setState;\n  end W;\n  model Mon\n    replaceable package Medium = PM;\n"
                           "    input Medium.ThermodynamicState state_in;\n  end Mon;\n  model Pump\n"
                           "    replaceable package Medium = PM;\n"
                           "    Mon monitoring(redeclare final package Medium = Medium, final state_in = Medium.setState(time));\n"
                           "  end Pump;\nend P16;\nmodel PKK18\n  P16.Pump pump0(redeclare package Medium = P16.W);\n"
                           "  P16.Pump pump(redeclare package Medium = P16.W);\n  P16.W.ThermodynamicState s0;\nequation\n"
                           "  s0 = pump0.monitoring.state_in;\nend PKK18;\n"),
               std::string("function P16.W.ThermodynamicState \"Automatically generated record constructor for P16.W.ThermodynamicState\"\n"
                           "  input Real p;\n  input Real T;\n  output ThermodynamicState res;\nend P16.W.ThermodynamicState;\n\n"
                           "function PKK18.pump.Medium.ThermodynamicState \"Automatically generated record constructor for PKK18.pump.Medium.ThermodynamicState\"\n"
                           "  input Real p;\n  input Real T;\n  output ThermodynamicState res;\n"
                           "end PKK18.pump.Medium.ThermodynamicState;\n\nfunction PKK18.pump.Medium.setState\n  input Real p;\n"
                           "  output PKK18.pump.Medium.ThermodynamicState s;\nalgorithm\n"
                           "  s := PKK18.pump.Medium.ThermodynamicState(p, 300.0);\nend PKK18.pump.Medium.setState;\n\n"
                           "function PKK18.pump.monitoring.Medium.ThermodynamicState \"Automatically generated record constructor for PKK18.pump.monitoring.Medium.ThermodynamicState\"\n"
                           "  input Real p;\n  input Real T;\n  output ThermodynamicState res;\n"
                           "end PKK18.pump.monitoring.Medium.ThermodynamicState;\n\n"
                           "function PKK18.pump0.Medium.ThermodynamicState \"Automatically generated record constructor for PKK18.pump0.Medium.ThermodynamicState\"\n"
                           "  input Real p;\n  input Real T;\n  output ThermodynamicState res;\n"
                           "end PKK18.pump0.Medium.ThermodynamicState;\n\nfunction PKK18.pump0.Medium.setState\n  input Real p;\n"
                           "  output PKK18.pump0.Medium.ThermodynamicState s;\nalgorithm\n"
                           "  s := PKK18.pump0.Medium.ThermodynamicState(p, 300.0);\nend PKK18.pump0.Medium.setState;\n\n"
                           "function PKK18.pump0.monitoring.Medium.ThermodynamicState \"Automatically generated record constructor for PKK18.pump0.monitoring.Medium.ThermodynamicState\"\n"
                           "  input Real p;\n  input Real T;\n  output ThermodynamicState res;\n"
                           "end PKK18.pump0.monitoring.Medium.ThermodynamicState;\n\nclass PKK18\n"
                           "  final Real pump0.monitoring.state_in.p;\n  final Real pump0.monitoring.state_in.T;\n"
                           "  final Real pump.monitoring.state_in.p;\n  final Real pump.monitoring.state_in.T;\n  Real s0.p;\n"
                           "  Real s0.T;\nequation\n  pump0.monitoring.state_in = PKK18.pump0.Medium.setState(time);\n"
                           "  pump.monitoring.state_in = PKK18.pump.Medium.setState(time);\n  s0 = pump0.monitoring.state_in;\n"
                           "end PKK18;\n"));
}

YMC_TEST(flatten_record_constructor_alias_medium_class_extends) {
  // `redeclare final package Medium = Medium` 里的 class extends 记录的构造函数：继承来的字段（基类经带重声明的 extends
  // 继承来的类型 FixedPhase）按包实例解析。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p19 PKK20）。
  YMC_CHECK_EQ(flatten_src("package P20\n  partial package PM\n    type FixedPhase = Integer(min = 0, max = 2);\n"
                           "    replaceable record FluidConstants\n      Real mm;\n    end FluidConstants;\n"
                           "    replaceable record ThermodynamicState\n    end ThermodynamicState;\n"
                           "    replaceable partial function setState\n      input Real p;\n      output ThermodynamicState s;\n"
                           "    end setState;\n  end PM;\n  record FC2\n    Real mm;\n    Real tc;\n  end FC2;\n"
                           "  partial package PM2\n    extends PM(redeclare record FluidConstants = FC2);\n"
                           "    redeclare replaceable record extends ThermodynamicState\n      FixedPhase phase(min = 0, max = 2);\n"
                           "    end ThermodynamicState;\n  end PM2;\n  package W\n    extends PM2;\n"
                           "    redeclare record extends ThermodynamicState\n      Real p;\n    end ThermodynamicState;\n"
                           "    redeclare function extends setState\n    algorithm\n      s := ThermodynamicState(phase = 1, p = p);\n"
                           "    end setState;\n  end W;\n  model Mon\n    replaceable package Medium = PM;\n"
                           "    input Medium.ThermodynamicState state_in;\n  end Mon;\n  model Pump\n"
                           "    replaceable package Medium = PM;\n"
                           "    Mon monitoring(redeclare final package Medium = Medium, final state_in = Medium.setState(time));\n"
                           "  end Pump;\nend P20;\nmodel PKK20\n  P20.Pump pump(redeclare package Medium = P20.W);\nend PKK20;\n"),
               std::string("function PKK20.pump.Medium.ThermodynamicState \"Automatically generated record constructor for PKK20.pump.Medium.ThermodynamicState\"\n"
                           "  input Integer phase;\n  input Real p;\n  output ThermodynamicState res;\n"
                           "end PKK20.pump.Medium.ThermodynamicState;\n\nfunction PKK20.pump.Medium.setState\n  input Real p;\n"
                           "  output PKK20.pump.Medium.ThermodynamicState s;\nalgorithm\n"
                           "  s := PKK20.pump.Medium.ThermodynamicState(1, p);\nend PKK20.pump.Medium.setState;\n\n"
                           "function PKK20.pump.monitoring.Medium.ThermodynamicState \"Automatically generated record constructor for PKK20.pump.monitoring.Medium.ThermodynamicState\"\n"
                           "  input Integer phase;\n  input Real p;\n  output ThermodynamicState res;\n"
                           "end PKK20.pump.monitoring.Medium.ThermodynamicState;\n\nclass PKK20\n"
                           "  final Integer pump.monitoring.state_in.phase(min = 0, max = 2);\n"
                           "  final Real pump.monitoring.state_in.p;\nequation\n"
                           "  pump.monitoring.state_in = PKK20.pump.Medium.setState(time);\nend PKK20;\n"));
}

YMC_TEST(flatten_runtime_record_argument_not_evaluated) {
  // 绑定里调函数、实参是经带重声明的 extends 继承来的 input 记录变量（MSL Fluid PumpMonitoringNPSH 的
  // `rho_in = Medium.density(state_in)`）：变量求不出值，照留调用。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p19 PKK22）。
  YMC_CHECK_EQ(flatten_src("package P22\n  partial package PM\n    replaceable record ThermodynamicState\n      Real p;\n"
                           "    end ThermodynamicState;\n    replaceable partial function density\n"
                           "      input ThermodynamicState state;\n      output Real d;\n    end density;\n  end PM;\n"
                           "  partial package PM2\n    extends PM;\n  end PM2;\n  package W\n    extends PM2;\n"
                           "    redeclare function extends density\n    algorithm\n      d := state.p / 2;\n    end density;\n"
                           "  end W;\n  model MonBase\n    replaceable package Medium = PM;\n"
                           "    input Medium.ThermodynamicState state_in;\n  end MonBase;\n  model MonNPSH\n"
                           "    extends MonBase(redeclare replaceable package Medium = PM2);\n"
                           "    Real rho_in = Medium.density(state_in);\n  end MonNPSH;\nend P22;\nmodel PKK22\n"
                           "  P22.MonNPSH m(redeclare package Medium = P22.W, state_in(p = time));\nend PKK22;\n"),
               std::string("function PKK22.m.Medium.ThermodynamicState \"Automatically generated record constructor for PKK22.m.Medium.ThermodynamicState\"\n"
                           "  input Real p;\n  output ThermodynamicState res;\nend PKK22.m.Medium.ThermodynamicState;\n\n"
                           "function PKK22.m.Medium.density\n  input PKK22.m.Medium.ThermodynamicState state;\n  output Real d;\n"
                           "algorithm\n  d := state.p / 2.0;\nend PKK22.m.Medium.density;\n\nclass PKK22\n"
                           "  Real m.state_in.p = time;\n  Real m.rho_in = PKK22.m.Medium.density(m.state_in);\nend PKK22;\n"));
}

YMC_TEST(flatten_empty_comprehension_in_equation) {
  // 单迭代推导式的范围为空（`w[2:n-1] = {x[i] * 2 for i in 2:n-1} * np`、n = 2）：空数组字面量（makeEmptyArray），整条方程
  // 消失（MSL Fluid DynamicPipe 的 Wb_flows）。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p20 PKL1）。
  YMC_CHECK_EQ(flatten_src("model PKL1\n  parameter Integer n = 2;\n  parameter Integer np = 3;\n  Real x[n] = {time, 2 * time};\n"
                           "  Real w[n];\nequation\n  w[2:n - 1] = {x[i] * 2 for i in 2:n - 1} * np;\n  w[1] = x[1];\n  w[n] = x[n];\n"
                           "end PKL1;\n"),
               std::string("class PKL1\n  final parameter Integer n = 2;\n  parameter Integer np = 3;\n  Real x[1];\n  Real x[2];\n"
                           "  Real w[1];\n  Real w[2];\nequation\n  x = {time, 2.0 * time};\n  w[1] = x[1];\n  w[2] = x[2];\n"
                           "end PKL1;\n"));
}

YMC_TEST(flatten_empty_comprehension_reductions) {
  // 空推导式作绑定（`Real y[0]` 不出变量）、sum 折成 0、max 折成 Real 的最小值。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p20 PKL2）。
  YMC_CHECK_EQ(flatten_src("model PKL2\n  parameter Integer n = 0;\n  Real x[3] = {time, 2 * time, 3};\n"
                           "  Real y[n] = {x[i] for i in 1:n};\n  Real s = sum({x[i] for i in 1:n}) + time;\n"
                           "  Real m = max({x[i] for i in 1:n});\nend PKL2;\n"),
               std::string("class PKL2\n  final parameter Integer n = 0;\n  Real x[1];\n  Real x[2];\n  Real x[3];\n  Real s = time;\n"
                           "  Real m = -8.777798510069901e304;\nequation\n  x = {time, 2.0 * time, 3.0};\nend PKL2;\n"));
}

YMC_TEST(flatten_der_of_empty_slice) {
  // 空数组的切片 `der(m[i, :])`（nX = 0，MSL Fluid PartialDistributedVolume 的物质平衡）：方程两边零长、不出方程。期望 =
  // omc 1.25.0 实跑（2026-10-15，探针 pk-p20 PKL4）。
  YMC_CHECK_EQ(flatten_src("model PKL4\n  parameter Integer nX = 0;\n  parameter Integer n = 2;\n  Real m[n, nX];\n  Real f[n, nX];\n"
                           "  Real u[n];\nequation\n  for i in 1:n loop\n    der(m[i, :]) = f[i, :];\n    der(u[i]) = -u[i];\n"
                           "  end for;\n  for i in 1:n loop\n    f[i, :] = zeros(nX);\n  end for;\nend PKL4;\n"),
               std::string("class PKL4\n  final parameter Integer nX = 0;\n  final parameter Integer n = 2;\n  Real u[1];\n"
                           "  Real u[2];\nequation\n  der(u[1]) = -u[1];\n  der(u[2]) = -u[2];\nend PKL4;\n"));
}

YMC_TEST(flatten_der_of_empty_variable_array) {
  // 零长数组变量的 `der(m)` / `der(m[:])`：方程消失，`sum(m)` 折成 0。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p20 PKL5）。
  YMC_CHECK_EQ(flatten_src("model PKL5\n  parameter Integer nX = 0;\n  Real m[nX];\n  Real f[nX];\n  Real u;\nequation\n"
                           "  der(m) = f;\n  der(m[:]) = f[1:nX];\n  der(u) = -u + sum(m);\nend PKL5;\n"),
               std::string("class PKL5\n  final parameter Integer nX = 0;\n  Real u;\nequation\n  der(u) = -u;\nend PKL5;\n"));
}

YMC_TEST(flatten_comprehension_range_in_component_modifier) {
  // 组件修饰里的推导式 `t(table = {{Ptable[j], Itable[j]} for j in 1:size(Ptable, 1)})`：范围在写修饰的实例里求（MSL
  // IMC_withLosses 的 combiTable1Ds）。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p20 PKL6）。
  YMC_CHECK_EQ(flatten_src("model PKL6\n  parameter Real Ptable[:] = {1, 2, 3};\n  parameter Real Itable[:] = {4, 5, 6};\n  model T\n"
                           "    parameter Real table[:, :];\n    Real y = table[1, 1] * time;\n  end T;\n"
                           "  T t(table = {{Ptable[j], Itable[j]} for j in 1:size(Ptable, 1)});\nend PKL6;\n"),
               std::string("class PKL6\n  parameter Real Ptable[1] = 1.0;\n  parameter Real Ptable[2] = 2.0;\n"
                           "  parameter Real Ptable[3] = 3.0;\n  parameter Real Itable[1] = 4.0;\n  parameter Real Itable[2] = 5.0;\n"
                           "  parameter Real Itable[3] = 6.0;\n  parameter Real t.table[1,1] = Ptable[1];\n"
                           "  parameter Real t.table[1,2] = Itable[1];\n  parameter Real t.table[2,1] = Ptable[2];\n"
                           "  parameter Real t.table[2,2] = Itable[2];\n  parameter Real t.table[3,1] = Ptable[3];\n"
                           "  parameter Real t.table[3,2] = Itable[3];\n  Real t.y = t.table[1,1] * time;\nend PKL6;\n"));
}

YMC_TEST(flatten_vectorized_call_derived_scalar_input) {
  // 向量化调用的输入是内建标量的短类派生（`input Rpm rpm`，MSL from_rpm）：omc 求值后照打函数定义（PKL 探针 PKL15，同 V4g），
  // 未做 → 须 SKIP；此前求值器不认这种向量化、静默丢了函数定义。
  bool uns = false;
  const std::string out = flatten_src("package P15\n  type Rpm = Real(final quantity = \"AngularVelocity\", final unit = \"rev/min\");\n"
                                     "  function from_rpm\n    input Rpm rpm;\n    output Real rs;\n  algorithm\n    rs := 0.1 * rpm;\n"
                                     "  end from_rpm;\nend P15;\nmodel PKL15\n  import P15.from_rpm;\n"
                                     "  parameter Real wtable[:] = from_rpm({1500, 1496});\n  Real y = wtable[1] * time;\nend PKL15;\n",
                                      &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:constant user call after structural substitution>"));
}

YMC_TEST(flatten_instream_in_comprehension) {
  // 推导式体里的 inStream 带迭代变量下标（MSL Fluid MultiPort 的 `sum({positiveMax(ports_b[j].m_flow) *
  // inStream(ports_b[j].h_outflow) for j in 1:nPorts_b})`）：先展开再求流算子，打成按连接集代好的和式。期望 = omc 1.25.0
  // 实跑（2026-10-15，探针 pk-p21 PKM1）。
  YMC_CHECK_EQ(flatten_src("package PM1\n  connector FluidPort\n    flow Real m_flow;\n    stream Real h_outflow;\n    Real p;\n"
                           "  end FluidPort;\n  model MultiPort\n    parameter Integer n = 3;\n    FluidPort port_a;\n"
                           "    FluidPort ports_b[n];\n  protected\n    function positiveMax\n      input Real x;\n"
                           "      output Real y;\n    algorithm\n      y := max(x, 1e-10);\n    end positiveMax;\n  equation\n"
                           "    0 = port_a.m_flow + sum(ports_b.m_flow);\n    ports_b.p = fill(port_a.p, n);\n"
                           "    port_a.h_outflow = sum({positiveMax(ports_b[j].m_flow) * inStream(ports_b[j].h_outflow) for j in 1:n}) / sum({positiveMax(ports_b[j].m_flow) for j in 1:n});\n"
                           "    for j in 1:n loop\n      ports_b[j].h_outflow = inStream(port_a.h_outflow);\n    end for;\n"
                           "  end MultiPort;\n  model Source\n    FluidPort port;\n    parameter Real h = 1;\n"
                           "    parameter Real mf = 1;\n  equation\n    port.h_outflow = h;\n    port.m_flow = -mf;\n  end Source;\n"
                           "  model Sink\n    FluidPort port;\n  equation\n    port.h_outflow = 0;\n    port.p = 1;\n  end Sink;\n"
                           "end PM1;\nmodel PKM1\n  PM1.MultiPort mp(n = 3);\n  PM1.Source s1(h = 1), s2(h = 2), s3(h = 3);\n"
                           "  PM1.Sink sink;\nequation\n  connect(s1.port, mp.ports_b[1]);\n  connect(s2.port, mp.ports_b[2]);\n"
                           "  connect(s3.port, mp.ports_b[3]);\n  connect(mp.port_a, sink.port);\nend PKM1;\n"),
               std::string("function PKM1.mp.positiveMax\n  input Real x;\n  output Real y;\nalgorithm\n  y := max(x, 1e-10);\n"
                           "end PKM1.mp.positiveMax;\n\nclass PKM1\n  final parameter Integer mp.n = 3;\n  Real mp.port_a.m_flow;\n"
                           "  Real mp.port_a.h_outflow;\n  Real mp.port_a.p;\n  Real mp.ports_b[1].m_flow;\n"
                           "  Real mp.ports_b[1].h_outflow;\n  Real mp.ports_b[1].p;\n  Real mp.ports_b[2].m_flow;\n"
                           "  Real mp.ports_b[2].h_outflow;\n  Real mp.ports_b[2].p;\n  Real mp.ports_b[3].m_flow;\n"
                           "  Real mp.ports_b[3].h_outflow;\n  Real mp.ports_b[3].p;\n  Real s1.port.m_flow;\n"
                           "  Real s1.port.h_outflow;\n  Real s1.port.p;\n  parameter Real s1.h = 1.0;\n"
                           "  parameter Real s1.mf = 1.0;\n  Real s2.port.m_flow;\n  Real s2.port.h_outflow;\n  Real s2.port.p;\n"
                           "  parameter Real s2.h = 2.0;\n  parameter Real s2.mf = 1.0;\n  Real s3.port.m_flow;\n"
                           "  Real s3.port.h_outflow;\n  Real s3.port.p;\n  parameter Real s3.h = 3.0;\n"
                           "  parameter Real s3.mf = 1.0;\n  Real sink.port.m_flow;\n  Real sink.port.h_outflow;\n"
                           "  Real sink.port.p;\nequation\n  s1.port.p = mp.ports_b[1].p;\n  s2.port.p = mp.ports_b[2].p;\n"
                           "  s3.port.p = mp.ports_b[3].p;\n  mp.port_a.p = sink.port.p;\n"
                           "  sink.port.m_flow + mp.port_a.m_flow = 0.0;\n  s1.port.m_flow + mp.ports_b[1].m_flow = 0.0;\n"
                           "  s2.port.m_flow + mp.ports_b[2].m_flow = 0.0;\n  s3.port.m_flow + mp.ports_b[3].m_flow = 0.0;\n"
                           "  0.0 = mp.port_a.m_flow + mp.ports_b[1].m_flow + mp.ports_b[2].m_flow + mp.ports_b[3].m_flow;\n"
                           "  mp.ports_b[1].p = mp.port_a.p;\n  mp.ports_b[2].p = mp.port_a.p;\n  mp.ports_b[3].p = mp.port_a.p;\n"
                           "  mp.port_a.h_outflow = (PKM1.mp.positiveMax(mp.ports_b[1].m_flow) * s1.port.h_outflow + PKM1.mp.positiveMax(mp.ports_b[2].m_flow) * s2.port.h_outflow + PKM1.mp.positiveMax(mp.ports_b[3].m_flow) * s3.port.h_outflow) / (PKM1.mp.positiveMax(mp.ports_b[1].m_flow) + PKM1.mp.positiveMax(mp.ports_b[2].m_flow) + PKM1.mp.positiveMax(mp.ports_b[3].m_flow));\n"
                           "  mp.ports_b[1].h_outflow = sink.port.h_outflow;\n  mp.ports_b[2].h_outflow = sink.port.h_outflow;\n"
                           "  mp.ports_b[3].h_outflow = sink.port.h_outflow;\n  s1.port.h_outflow = s1.h;\n"
                           "  s1.port.m_flow = -s1.mf;\n  s2.port.h_outflow = s2.h;\n  s2.port.m_flow = -s2.mf;\n"
                           "  s3.port.h_outflow = s3.h;\n  s3.port.m_flow = -s3.mf;\n  sink.port.h_outflow = 0.0;\n"
                           "  sink.port.p = 1.0;\nend PKM1;\n"));
}

YMC_TEST(flatten_actualstream_in_comprehension) {
  // 同上，actualStream。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p21 PKM4）。
  YMC_CHECK_EQ(flatten_src("package PM4\n  connector FluidPort\n    flow Real m_flow;\n    stream Real h_outflow;\n    Real p;\n"
                           "  end FluidPort;\n  model MultiPort\n    parameter Integer n = 3;\n    FluidPort port_a;\n"
                           "    FluidPort ports_b[n];\n  protected\n    function positiveMax\n      input Real x;\n"
                           "      output Real y;\n    algorithm\n      y := max(x, 1e-10);\n    end positiveMax;\n  equation\n"
                           "    0 = port_a.m_flow + sum(ports_b.m_flow);\n    ports_b.p = fill(port_a.p, n);\n"
                           "    port_a.h_outflow = sum({positiveMax(ports_b[j].m_flow) * actualStream(ports_b[j].h_outflow) for j in 1:n}) / sum({positiveMax(ports_b[j].m_flow) for j in 1:n});\n"
                           "    for j in 1:n loop\n      ports_b[j].h_outflow = inStream(port_a.h_outflow);\n    end for;\n"
                           "  end MultiPort;\n  model Source\n    FluidPort port;\n    parameter Real h = 1;\n"
                           "    parameter Real mf = 1;\n  equation\n    port.h_outflow = h;\n    port.m_flow = -mf;\n  end Source;\n"
                           "  model Sink\n    FluidPort port;\n  equation\n    port.h_outflow = 0;\n    port.p = 1;\n  end Sink;\n"
                           "end PM4;\nmodel PKM4\n  PM4.MultiPort mp(n = 3);\n  PM4.Source s1(h = 1), s2(h = 2), s3(h = 3);\n"
                           "  PM4.Sink sink;\nequation\n  connect(s1.port, mp.ports_b[1]);\n  connect(s2.port, mp.ports_b[2]);\n"
                           "  connect(s3.port, mp.ports_b[3]);\n  connect(mp.port_a, sink.port);\nend PKM4;\n"),
               std::string("function PKM4.mp.positiveMax\n  input Real x;\n  output Real y;\nalgorithm\n  y := max(x, 1e-10);\n"
                           "end PKM4.mp.positiveMax;\n\nclass PKM4\n  final parameter Integer mp.n = 3;\n  Real mp.port_a.m_flow;\n"
                           "  Real mp.port_a.h_outflow;\n  Real mp.port_a.p;\n  Real mp.ports_b[1].m_flow;\n"
                           "  Real mp.ports_b[1].h_outflow;\n  Real mp.ports_b[1].p;\n  Real mp.ports_b[2].m_flow;\n"
                           "  Real mp.ports_b[2].h_outflow;\n  Real mp.ports_b[2].p;\n  Real mp.ports_b[3].m_flow;\n"
                           "  Real mp.ports_b[3].h_outflow;\n  Real mp.ports_b[3].p;\n  Real s1.port.m_flow;\n"
                           "  Real s1.port.h_outflow;\n  Real s1.port.p;\n  parameter Real s1.h = 1.0;\n"
                           "  parameter Real s1.mf = 1.0;\n  Real s2.port.m_flow;\n  Real s2.port.h_outflow;\n  Real s2.port.p;\n"
                           "  parameter Real s2.h = 2.0;\n  parameter Real s2.mf = 1.0;\n  Real s3.port.m_flow;\n"
                           "  Real s3.port.h_outflow;\n  Real s3.port.p;\n  parameter Real s3.h = 3.0;\n"
                           "  parameter Real s3.mf = 1.0;\n  Real sink.port.m_flow;\n  Real sink.port.h_outflow;\n"
                           "  Real sink.port.p;\nequation\n  s1.port.p = mp.ports_b[1].p;\n  s2.port.p = mp.ports_b[2].p;\n"
                           "  s3.port.p = mp.ports_b[3].p;\n  mp.port_a.p = sink.port.p;\n"
                           "  sink.port.m_flow + mp.port_a.m_flow = 0.0;\n  s1.port.m_flow + mp.ports_b[1].m_flow = 0.0;\n"
                           "  s2.port.m_flow + mp.ports_b[2].m_flow = 0.0;\n  s3.port.m_flow + mp.ports_b[3].m_flow = 0.0;\n"
                           "  0.0 = mp.port_a.m_flow + mp.ports_b[1].m_flow + mp.ports_b[2].m_flow + mp.ports_b[3].m_flow;\n"
                           "  mp.ports_b[1].p = mp.port_a.p;\n  mp.ports_b[2].p = mp.port_a.p;\n  mp.ports_b[3].p = mp.port_a.p;\n"
                           "  mp.port_a.h_outflow = (PKM4.mp.positiveMax(mp.ports_b[1].m_flow) * (if mp.ports_b[1].m_flow > 0.0 then s1.port.h_outflow else mp.ports_b[1].h_outflow) + PKM4.mp.positiveMax(mp.ports_b[2].m_flow) * (if mp.ports_b[2].m_flow > 0.0 then s2.port.h_outflow else mp.ports_b[2].h_outflow) + PKM4.mp.positiveMax(mp.ports_b[3].m_flow) * (if mp.ports_b[3].m_flow > 0.0 then s3.port.h_outflow else mp.ports_b[3].h_outflow)) / (PKM4.mp.positiveMax(mp.ports_b[1].m_flow) + PKM4.mp.positiveMax(mp.ports_b[2].m_flow) + PKM4.mp.positiveMax(mp.ports_b[3].m_flow));\n"
                           "  mp.ports_b[1].h_outflow = sink.port.h_outflow;\n  mp.ports_b[2].h_outflow = sink.port.h_outflow;\n"
                           "  mp.ports_b[3].h_outflow = sink.port.h_outflow;\n  s1.port.h_outflow = s1.h;\n"
                           "  s1.port.m_flow = -s1.mf;\n  s2.port.h_outflow = s2.h;\n  s2.port.m_flow = -s2.mf;\n"
                           "  s3.port.h_outflow = s3.h;\n  s3.port.m_flow = -s3.mf;\n  sink.port.h_outflow = 0.0;\n"
                           "  sink.port.p = 1.0;\nend PKM4;\n"));
}

YMC_TEST(flatten_structural_array_iterator_subscript_equal) {
  // 结构参数数组以迭代变量下标引用（`{crossAreas[i] * lengths[i] for i in 1:n}`）、各元素相同 → 取那一个（simplifySubscriptedExp）；
  // `lengths / sum(lengths)`、`sum(lengths)` 代入后全字面量 → 求值。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p21 PKM5）。
  YMC_CHECK_EQ(flatten_src("model PKM5\n  parameter Integer n = 2;\n  parameter Real length = 100;\n"
                           "  parameter Real lengths[n] = fill(length / n, n) annotation(Evaluate = true);\n"
                           "  parameter Real dxs[n] = lengths / sum(lengths);\n  Real crossAreas[n] = {time, 2 * time};\n"
                           "  Real fluidVolumes[n] = {crossAreas[i] * lengths[i] for i in 1:n};\n  Real s = sum(lengths) * time;\n"
                           "  Real w[n] = lengths * time;\nend PKM5;\n"),
               std::string("class PKM5\n  final parameter Integer n = 2;\n  final parameter Real length = 100.0;\n"
                           "  final parameter Real lengths[1] = 50.0;\n  final parameter Real lengths[2] = 50.0;\n"
                           "  parameter Real dxs[1] = 0.5;\n  parameter Real dxs[2] = 0.5;\n  Real crossAreas[1];\n"
                           "  Real crossAreas[2];\n  Real fluidVolumes[1];\n  Real fluidVolumes[2];\n  Real s = 100.0 * time;\n"
                           "  Real w[1];\n  Real w[2];\nequation\n  crossAreas = {time, 2.0 * time};\n"
                           "  fluidVolumes = array(crossAreas[i] * 50.0 for i in 1:2);\n  w = {50.0 * time, 50.0 * time};\nend PKM5;\n"));
}

YMC_TEST(flatten_structural_array_iterator_subscript_distinct) {
  // 同上、各元素不同 → 留 `{1.0, 3.0}[i]`。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p21 PKM6）。
  YMC_CHECK_EQ(flatten_src("model PKM6\n  parameter Integer n = 2;\n"
                           "  parameter Real lengths[n] = {1, 3} annotation(Evaluate = true);\n"
                           "  Real crossAreas[n] = {time, 2 * time};\n"
                           "  Real fluidVolumes[n] = {crossAreas[i] * lengths[i] for i in 1:n};\nend PKM6;\n"),
               std::string("class PKM6\n  final parameter Integer n = 2;\n  final parameter Real lengths[1] = 1.0;\n"
                           "  final parameter Real lengths[2] = 3.0;\n  Real crossAreas[1];\n  Real crossAreas[2];\n"
                           "  Real fluidVolumes[1];\n  Real fluidVolumes[2];\nequation\n  crossAreas = {time, 2.0 * time};\n"
                           "  fluidVolumes = array(crossAreas[i] * {1.0, 3.0}[i] for i in 1:2);\nend PKM6;\n"));
}

YMC_TEST(flatten_empty_slice_function_argument) {
  // 非空数组的空切片作调用实参（`f(p, X_start[1:nXi])`、nXi = 0）→ `{}`（removeEmptyFunctionArguments）。期望 = omc 1.25.0
  // 实跑（2026-10-15，探针 pk-p21 PKM8）。
  YMC_CHECK_EQ(flatten_src("model PKM8\n  function f\n    input Real p;\n    input Real X[:];\n    output Real h;\n  algorithm\n"
                           "    h := p + sum(X);\n  end f;\n  parameter Integer nXi = 0;\n  parameter Real p = 2;\n"
                           "  parameter Real X_start[1] = {1.0};\n  parameter Real h_start = f(p, X_start[1:nXi]);\n"
                           "  Real y = h_start * time;\n  Real z = f(time, X_start[1:nXi]);\nend PKM8;\n"),
               std::string("function PKM8.f\n  input Real p;\n  input Real[:] X;\n  output Real h;\nalgorithm\n  h := p + sum(X);\n"
                           "end PKM8.f;\n\nclass PKM8\n  final parameter Integer nXi = 0;\n  parameter Real p = 2.0;\n"
                           "  parameter Real X_start[1] = 1.0;\n  parameter Real h_start = PKM8.f(p, {});\n"
                           "  Real y = h_start * time;\n  Real z = PKM8.f(time, {});\nend PKM8;\n"));
}

YMC_TEST(flatten_reduction_iterator_shadows_function_local) {
  // 函数体里归约的迭代变量 i 遮住同名的函数局部 `Integer i`：字面量范围的 `sum(a[i] * T ^ (i - 1) for i in 1:4)` 展开成和式，
  // 归约外的 `+ i` 仍是局部，范围依赖输入的 `sum(a[i] for i in 1:n)` 留原样。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p22 PKQ1）。
  YMC_CHECK_EQ(flatten_src("model PKQ1\n  function f\n    input Real T;\n    input Integer n;\n    output Real y;\n  protected\n"
                           "    Integer i;\n    Real a[4] = {1, 2, 3, 4};\n  algorithm\n    i := 2;\n"
                           "    y := T ^ 0.5 * sum(a[i] * T ^ (i - 1) for i in 1:4) + i + sum(a[i] for i in 1:n) + i;\n  end f;\n"
                           "  Real y = f(time, 3);\nend PKQ1;\n"),
               std::string("function PKQ1.f\n  input Real T;\n  input Integer n;\n  output Real y;\n  protected Integer i;\n"
                           "  protected Real[4] a = {1.0, 2.0, 3.0, 4.0};\nalgorithm\n  i := 2;\n"
                           "  y := T ^ 0.5 * (a[1] + a[2] * T + a[3] * T ^ 2.0 + a[4] * T ^ 3.0) + /*Real*/(i) + sum(a[i] for i in 1:n) + /*Real*/(i);\n"
                           "end PKQ1.f;\n\nclass PKQ1\n  Real y = PKQ1.f(time, 3);\nend PKQ1;\n"));
}

YMC_TEST(flatten_package_instance_record_constant_fields) {
  // 经重声明的包实例取记录常量的字段（MSL MoistAir 的 `MMX = {steam.MM, dryair.MM}`、`k_mair = steam.MM / dryair.MM`，
  // steam / dryair 是包里的记录常量）→ 折成字面量。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p22 PKN10）。
  YMC_CHECK_EQ(flatten_src("package P10\n  record IconRecord\n  end IconRecord;\n  package Common\n    record DataRecord\n"
                           "      extends IconRecord;\n      String name;\n      Real MM;\n      Real Hf;\n      Real Tlimit;\n"
                           "      Real alow[3];\n      Real blow[2];\n      Real R_s;\n    end DataRecord;\n"
                           "    package SingleGasesData\n      constant Real R_NASA_2002 = 8.3144598;\n"
                           "      constant DataRecord H2O(name = \"H2O\", MM = 0.01801528, Hf = -13423382.8, Tlimit = 1000, alow = {-39479.6, 575.57, 0.93}, blow = {-33039.7, 17.24}, R_s = R_NASA_2002 / H2O.MM);\n"
                           "      constant DataRecord Air(name = \"Air\", MM = 0.0289651159, Hf = -4333.83, Tlimit = 1000, alow = {10099.5, -196.827, 5.00915}, blow = {-176.796, -3.92150}, R_s = R_NASA_2002 / Air.MM);\n"
                           "    end SingleGasesData;\n  end Common;\n  partial package PartialMedium\n    constant Integer nX = 2;\n"
                           "    replaceable partial model BaseProperties\n      Real Xi = 0.01 * time;\n      Real MM;\n"
                           "    end BaseProperties;\n  end PartialMedium;\n  package MoistAir\n    extends PartialMedium;\n"
                           "    constant Integer Water = 1;\n    constant Integer Air = 2;\n"
                           "    constant Real k_mair = steam.MM / dryair.MM;\n"
                           "    constant Common.DataRecord dryair = Common.SingleGasesData.Air;\n"
                           "    constant Common.DataRecord steam = Common.SingleGasesData.H2O;\n"
                           "    constant Real MMX[2] = {steam.MM, dryair.MM};\n    redeclare model extends BaseProperties\n"
                           "    equation\n      MM = 1 / (Xi / MMX[Water] + (1.0 - Xi) / MMX[Air]) * k_mair;\n"
                           "    end BaseProperties;\n  end MoistAir;\n  model Volume\n"
                           "    replaceable package Medium = PartialMedium;\n    Medium.BaseProperties medium;\n  end Volume;\n"
                           "end P10;\nmodel PKN10\n  P10.Volume v(redeclare package Medium = P10.MoistAir);\nend PKN10;\n"),
               std::string("class PKN10\n  Real v.medium.Xi = 0.01 * time;\n  Real v.medium.MM;\nequation\n"
                           "  v.medium.MM = 1.0 / (v.medium.Xi / 0.01801528 + (1.0 - v.medium.Xi) / 0.0289651159) * 0.6219647130774989;\n"
                           "end PKN10;\n"));
}

YMC_TEST(flatten_record_constant_array_field_modifier) {
  // 按修饰逐字段给值、带数组字段的记录常量（`H2O(name = "H2O", MM = 0.018, alow = {…})`）取标量字段 → 0.018。
  // 期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p22 PKY2）。
  YMC_CHECK_EQ(flatten_src("package PY2\n  record IconRecord\n  end IconRecord;\n  record DataRecord\n    String name;\n    Real MM;\n"
                           "    Real alow[3];\n  end DataRecord;\n"
                           "  constant DataRecord H2O(name = \"H2O\", MM = 0.018, alow = {-39479.6, 575.57, 0.93});\nend PY2;\n"
                           "model PKY2\n  Real a = PY2.H2O.MM * time;\nend PKY2;\n"),
               std::string("class PKY2\n  Real a = 0.018 * time;\nend PKY2;\n"));
}

YMC_TEST(flatten_record_constant_field_self_reference) {
  // 记录常量的字段修饰里引用记录自己（MSL SingleGasesData 的 `R_s = R_NASA_2002 / H2O.MM`）：直接取、经包常量 `steam`、
  // 经数组常量 `MMX[1]` 取 MM 都折成 0.01801528。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p22 PKW7）。
  YMC_CHECK_EQ(flatten_src("package PW7\n  record IconRecord\n  end IconRecord;\n  package Common\n    record DataRecord\n"
                           "      extends IconRecord;\n      String name;\n      Real MM;\n      Real Hf;\n      Real Tlimit;\n"
                           "      Real R_s;\n    end DataRecord;\n    package SingleGasesData\n"
                           "      constant Real R_NASA_2002 = 8.3144598;\n"
                           "      constant DataRecord H2O(name = \"H2O\", MM = 0.01801528, Hf = -13423382.8, Tlimit = 1000, R_s = R_NASA_2002 / H2O.MM);\n"
                           "      constant DataRecord Air(name = \"Air\", MM = 0.0289651159, Hf = -4333.83, Tlimit = 1000, R_s = R_NASA_2002 / Air.MM);\n"
                           "    end SingleGasesData;\n  end Common;\n  partial package PartialMedium\n    constant Integer nX = 2;\n"
                           "    replaceable partial model BaseProperties\n      Real Xi = 0.01 * time;\n      Real MM;\n"
                           "    end BaseProperties;\n  end PartialMedium;\n  package MoistAir\n    extends PartialMedium;\n"
                           "    constant Integer Water = 1;\n    constant Integer Air = 2;\n"
                           "    constant Real k_mair = steam.MM / dryair.MM;\n"
                           "    constant Common.DataRecord dryair = Common.SingleGasesData.Air;\n"
                           "    constant Common.DataRecord steam = Common.SingleGasesData.H2O;\n"
                           "    constant Real MMX[2] = {steam.MM, dryair.MM};\n    redeclare model extends BaseProperties\n"
                           "    equation\n      MM = 1 / (Xi / MMX[Water] + (1.0 - Xi) / MMX[Air]) * k_mair;\n"
                           "    end BaseProperties;\n  end MoistAir;\n  model Volume\n"
                           "    replaceable package Medium = PartialMedium;\n    Medium.BaseProperties medium;\n  end Volume;\n"
                           "end PW7;\nmodel PKW7\n  Real a = PW7.Common.SingleGasesData.H2O.MM * time;\n"
                           "  Real b = PW7.MoistAir.steam.MM * time;\n  Real c = PW7.MoistAir.MMX[1] * time;\nend PKW7;\n"),
               std::string("class PKW7\n  Real a = 0.01801528 * time;\n  Real b = 0.01801528 * time;\n  Real c = 0.01801528 * time;\n"
                           "end PKW7;\n"));
}

YMC_TEST(flatten_function_cat_kept) {
  // 函数体里的 `cat(1, X, {1 - sum(X)})` 留调用（不展开），记录构造函数的命名实参改按位置写出。
  // 期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p22 PKN14）。
  YMC_CHECK_EQ(flatten_src("package PN14\n  record State\n    Real p;\n    Real X[2];\n  end State;\n  function f\n    input Real p;\n"
                           "    input Real X[:] = {0.01, 0.99};\n    output State s;\n  algorithm\n"
                           "    s := if size(X, 1) == 2 then State(p = p, X = X) else State(p = p, X = cat(1, X, {1 - sum(X)}));\n"
                           "  end f;\nend PN14;\nmodel PKN14\n  PN14.State z = PN14.f(time, {0.2});\nend PKN14;\n"),
               std::string("function PN14.State \"Automatically generated record constructor for PN14.State\"\n  input Real p;\n"
                           "  input Real[2] X;\n  output State res;\nend PN14.State;\n\nfunction PN14.f\n  input Real p;\n"
                           "  input Real[:] X = {0.01, 0.99};\n  output PN14.State s;\nalgorithm\n"
                           "  s := if size(X, 1) == 2 then PN14.State(p, X) else PN14.State(p, cat(1, X, {1.0 - sum(X)}));\n"
                           "end PN14.f;\n\nclass PKN14\n  Real z.p;\n  Real z.X[1];\n  Real z.X[2];\nequation\n"
                           "  z = PN14.f(time, {0.2});\nend PKN14;\n"));
}

YMC_TEST(flatten_modified_short_class_package) {
  // 带修饰的短类包 `package Medium = PN15.Air(extraPropertiesNames = {"CO2"}, …)`：经它重声明的组件与根上的
  // `Medium.nC` 都看到修饰后的常量（nC = 1）。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p22 PKN15）。
  YMC_CHECK_EQ(flatten_src("package PN15\n  partial package PartialMedium\n"
                           "    constant String extraPropertiesNames[:] = fill(\"\", 0);\n"
                           "    constant Integer nC = size(extraPropertiesNames, 1);\n"
                           "    constant Real C_nominal[nC] = fill(1.0e-6, nC);\n    replaceable partial model BaseProperties\n"
                           "      Real p = time;\n    end BaseProperties;\n  end PartialMedium;\n  package Air\n"
                           "    extends PartialMedium;\n    redeclare model extends BaseProperties\n    end BaseProperties;\n"
                           "  end Air;\n  model Volume\n    replaceable package Medium = PartialMedium;\n"
                           "    Medium.BaseProperties medium;\n    Real C[Medium.nC] = fill(time, Medium.nC);\n"
                           "    parameter Real Cn[Medium.nC] = Medium.C_nominal;\n  end Volume;\nend PN15;\nmodel PKN15\n"
                           "  package Medium = PN15.Air(extraPropertiesNames = {\"CO2\"}, C_nominal = {1.519E-3});\n"
                           "  PN15.Volume v(redeclare package Medium = Medium);\n  Real n = Medium.nC * time;\nend PKN15;\n"),
               std::string("class PKN15\n  Real v.medium.p = time;\n  Real v.C[1];\n  parameter Real v.Cn[1] = 0.001519;\n"
                           "  Real n = time;\nequation\n  v.C = {time};\nend PKN15;\n"));
}

YMC_TEST(flatten_fn_reduction_range_literal_after_typing) {
  // 函数体里归约的范围引用输入、定型后是字面量（`1:size(X, 1)`、`input Real X[2]` → `1:2`）：按字面量范围展开 sum（此前错留成
  // `sum(X[j] / 0.0280134 for j in 1:2)`）。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p23 PKT17）。
  YMC_CHECK_EQ(flatten_src("package P17\n  package Mix\n    constant Real K = 0.0280134;\n    function f\n      input Real X[2];\n"
                           "      output Real MM;\n    algorithm\n      MM := 1 / sum(X[j] / K for j in 1:size(X, 1));\n    end f;\n"
                           "  end Mix;\nend P17;\nmodel PKT17\n  Real x[2] = {0.3, 0.7} * time;\n  Real a = P17.Mix.f(x);\n"
                           "end PKT17;\n"),
               std::string("function P17.Mix.f\n  input Real[2] X;\n  output Real MM;\nalgorithm\n"
                           "  MM := 1.0 / (X[1] / 0.0280134 + X[2] / 0.0280134);\nend P17.Mix.f;\n\nclass PKT17\n  Real x[1];\n"
                           "  Real x[2];\n  Real a = P17.Mix.f(x);\nequation\n  x = {0.3 * time, 0.7 * time};\nend PKT17;\n"));
}

YMC_TEST(flatten_fn_record_constant_field) {
  // 函数体里包常量记录的字段 `dryair.R_s`（dryair = Data.Air）换成值（EvalConstants.evaluateFuncExpTraverser → Ceval.evalCref）。
  // 期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p23 PKT1）。
  YMC_CHECK_EQ(flatten_src("package P1\n  record Rec\n    String name;\n    Real MM;\n    Real a[3];\n    Real R_s;\n  end Rec;\n"
                           "  package Data\n"
                           "    constant Rec H2O(name = \"H2O\", MM = 0.01801528, a = {1.5, 2.5, 3.5}, R_s = 461.5233290850878);\n"
                           "    constant Rec Air(name = \"Air\", MM = 0.0289651159, a = {4.0, 5.0, 6.0}, R_s = 287.0512249529787);\n"
                           "  end Data;\n  package Moist\n    constant Rec steam = Data.H2O;\n    constant Rec dryair = Data.Air;\n"
                           "    function R_gas\n      input Real x;\n      output Real R;\n    algorithm\n"
                           "      R := dryair.R_s * (1 - x) + steam.R_s * x;\n    end R_gas;\n  end Moist;\nend P1;\nmodel PKT1\n"
                           "  Real r = P1.Moist.R_gas(time);\nend PKT1;\n"),
               std::string("function P1.Moist.R_gas\n  input Real x;\n  output Real R;\nalgorithm\n"
                           "  R := 287.0512249529787 * (1.0 - x) + 461.5233290850878 * x;\nend P1.Moist.R_gas;\n\nclass PKT1\n"
                           "  Real r = P1.Moist.R_gas(time);\nend PKT1;\n"));
}

YMC_TEST(flatten_fn_whole_record_constant_argument) {
  // 函数体里整个包常量记录 `steam` 作实参 → 记录式 `P2.Rec("H2O", …)`，构造函数随之发射（记录只出现在函数里，没有实例节点）。
  // 期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p23 PKT2）。
  YMC_CHECK_EQ(flatten_src("package P2\n  record Rec\n    String name;\n    Real MM;\n    Real a[3];\n    Real R_s;\n  end Rec;\n"
                           "  package Data\n"
                           "    constant Rec H2O(name = \"H2O\", MM = 0.01801528, a = {1.5, 2.5, 3.5}, R_s = 461.5233290850878);\n"
                           "  end Data;\n  function h_T\n    input Rec data;\n    input Real T;\n    input Real h_off = 0;\n"
                           "    output Real h;\n  algorithm\n    h := data.R_s * T + data.a[1] + h_off;\n  end h_T;\n  package Moist\n"
                           "    constant Rec steam = Data.H2O;\n    function h_pT\n      input Real T;\n      output Real h;\n"
                           "    algorithm\n      h := h_T(data = steam, T = T, h_off = 46479.819 + 2501014.5);\n    end h_pT;\n"
                           "  end Moist;\nend P2;\nmodel PKT2\n  Real h = P2.Moist.h_pT(time);\nend PKT2;\n"),
               std::string("function P2.Moist.h_pT\n  input Real T;\n  output Real h;\nalgorithm\n"
                           "  h := P2.h_T(P2.Rec(\"H2O\", 0.01801528, {1.5, 2.5, 3.5}, 461.5233290850878), T, 2.547494319e6);\n"
                           "end P2.Moist.h_pT;\n\nfunction P2.Rec \"Automatically generated record constructor for P2.Rec\"\n"
                           "  input String name;\n  input Real MM;\n  input Real[3] a;\n  input Real R_s;\n  output Rec res;\n"
                           "end P2.Rec;\n\nfunction P2.h_T\n  input P2.Rec data;\n  input Real T;\n  input Real h_off = 0.0;\n"
                           "  output Real h;\nalgorithm\n  h := data.R_s * T + data.a[1] + h_off;\nend P2.h_T;\n\nclass PKT2\n"
                           "  Real h = P2.Moist.h_pT(time);\nend PKT2;\n"));
}

YMC_TEST(flatten_fn_record_array_field) {
  // 函数体里记录数组常量的字段 `data.MM`（`Rec[:] data`）逐元素取 → `{0.0280134, 0.0319988}`，与输入的点积按元素展开。
  // 期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p23 PKT5）。
  YMC_CHECK_EQ(flatten_src("package QPKT5\n  record Rec\n    String name;\n    Real MM;\n    Real R_s;\n  end Rec;\n  record FC\n"
                           "    String chemicalFormula;\n    Real Tc;\n    Real mu;\n    Boolean hasDipoleMoment;\n  end FC;\n"
                           "  package Data\n    constant Rec N2(name = \"N2\", MM = 0.0280134, R_s = 296.8033869505308);\n"
                           "    constant Rec O2(name = \"O2\", MM = 0.0319988, R_s = 259.8369938872708);\n  end Data;\n"
                           "  package FData\n"
                           "    constant FC N2(chemicalFormula = \"N2\", Tc = 126.2, mu = 0.0, hasDipoleMoment = true);\n"
                           "    constant FC O2(chemicalFormula = \"O2\", Tc = 154.58, mu = 0.0, hasDipoleMoment = true);\n"
                           "  end FData;\n  function cp_T\n    input Rec data;\n    input Real T;\n    output Real cp;\n  algorithm\n"
                           "    cp := data.R_s * T;\n  end cp_T;\n  package Mix\n    constant Rec[:] data = {Data.N2, Data.O2};\n"
                           "    constant FC[2] fc = {FData.N2, FData.O2};\n    constant Integer nX = 2;\n    function f\n"
                           "      input Real X[nX];\n      output Real MM;\n    algorithm\n      MM := X * data.MM;\n    end f;\n"
                           "  end Mix;\nend QPKT5;\nmodel PKT5\n  Real x[2] = {0.3, 0.7} * time;\n  Real a = QPKT5.Mix.f(x);\n"
                           "end PKT5;\n"),
               std::string("function QPKT5.Mix.f\n  input Real[2] X;\n  output Real MM;\nalgorithm\n"
                           "  MM := X[1] * 0.0280134 + X[2] * 0.0319988;\nend QPKT5.Mix.f;\n\nclass PKT5\n  Real x[1];\n  Real x[2];\n"
                           "  Real a = QPKT5.Mix.f(x);\nequation\n  x = {0.3 * time, 0.7 * time};\nend PKT5;\n"));
}

YMC_TEST(flatten_fn_record_array_element_field_in_reduction) {
  // 归约体里的 `data[j].MM`：先取字段再取符号下标（`{…}[j]`），范围 `1:size(X, 1)` 定型成字面量后展开、下标代入取元素。
  // 期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p23 PKT7）。
  YMC_CHECK_EQ(flatten_src("package QPKT7\n  record Rec\n    String name;\n    Real MM;\n    Real R_s;\n  end Rec;\n  record FC\n"
                           "    String chemicalFormula;\n    Real Tc;\n    Real mu;\n    Boolean hasDipoleMoment;\n  end FC;\n"
                           "  package Data\n    constant Rec N2(name = \"N2\", MM = 0.0280134, R_s = 296.8033869505308);\n"
                           "    constant Rec O2(name = \"O2\", MM = 0.0319988, R_s = 259.8369938872708);\n  end Data;\n"
                           "  package FData\n"
                           "    constant FC N2(chemicalFormula = \"N2\", Tc = 126.2, mu = 0.0, hasDipoleMoment = true);\n"
                           "    constant FC O2(chemicalFormula = \"O2\", Tc = 154.58, mu = 0.0, hasDipoleMoment = true);\n"
                           "  end FData;\n  function cp_T\n    input Rec data;\n    input Real T;\n    output Real cp;\n  algorithm\n"
                           "    cp := data.R_s * T;\n  end cp_T;\n  package Mix\n    constant Rec[:] data = {Data.N2, Data.O2};\n"
                           "    constant FC[2] fc = {FData.N2, FData.O2};\n    constant Integer nX = 2;\n    function f\n"
                           "      input Real X[nX];\n      output Real MM;\n    algorithm\n"
                           "      MM := 1 / sum(X[j] / data[j].MM for j in 1:size(X, 1));\n    end f;\n  end Mix;\nend QPKT7;\n"
                           "model PKT7\n  Real x[2] = {0.3, 0.7} * time;\n  Real a = QPKT7.Mix.f(x);\nend PKT7;\n"),
               std::string("function QPKT7.Mix.f\n  input Real[2] X;\n  output Real MM;\nalgorithm\n"
                           "  MM := 1.0 / (X[1] / 0.0280134 + X[2] / 0.0319988);\nend QPKT7.Mix.f;\n\nclass PKT7\n  Real x[1];\n"
                           "  Real x[2];\n  Real a = QPKT7.Mix.f(x);\nequation\n  x = {0.3 * time, 0.7 * time};\nend PKT7;\n"));
}

YMC_TEST(flatten_fn_inherited_record_field_and_final_locals) {
  // 函数体里只按修饰逐字段给值的记录常量 `Constants` 的字段（含 extends 来的 `Tred`）换成值；声明成 final 的函数局部打 `final`
  // （NFConvertDAE.convertVarAttributes）。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p23 PKT9）。
  YMC_CHECK_EQ(flatten_src("package P9\n  record FundamentalConstants\n    Real R_s;\n    Real MM;\n    Real Tred;\n    Real pred;\n"
                           "  end FundamentalConstants;\n  package Ice\n    package Basic\n      record IceConstants\n"
                           "        extends FundamentalConstants;\n        Real p0;\n      end IceConstants;\n"
                           "      constant IceConstants Constants(R_s = 461.52364, MM = 18.015268E-003, Tred = 273.16, pred = 611.657, p0 = 101325);\n"
                           "      function Gibbs\n        input Real p;\n        input Real T;\n        output Real g;\n"
                           "      protected\n        final constant Real s_0 = -0.332733756492168E+004;\n"
                           "        final Real pi0 = Constants.p0 / Constants.pred;\n        Real theta;\n      algorithm\n"
                           "        g := Constants.R_s;\n        theta := T / Constants.Tred;\n"
                           "        g := g + s_0 * Constants.Tred * theta + 1 / Constants.Tred * p / Constants.pred + pi0;\n"
                           "      end Gibbs;\n    end Basic;\n  end Ice;\nend P9;\nmodel PKT9\n"
                           "  Real g = P9.Ice.Basic.Gibbs(1e5, time);\nend PKT9;\n"),
               std::string("function P9.Ice.Basic.Gibbs\n  input Real p;\n  input Real T;\n  output Real g;\n"
                           "  protected final constant Real s_0 = -3327.33756492168;\n"
                           "  protected final Real pi0 = 165.65656895940043;\n  protected Real theta;\nalgorithm\n  g := 461.52364;\n"
                           "  theta := T / 273.16;\n  g := g + s_0 * 273.16 * theta + 0.0036608581051398447 * p / 611.657 + pi0;\n"
                           "end P9.Ice.Basic.Gibbs;\n\nclass PKT9\n  Real g = P9.Ice.Basic.Gibbs(1e5, time);\nend PKT9;\n"));
}

YMC_TEST(flatten_fn_inherited_member_in_qualified_constant) {
  // 限定常量名经记录类往下、成员是 extends 来的（`Data.data.FPCRIT`，data extends fcrit）。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p23 PKT10）。
  YMC_CHECK_EQ(flatten_src("package P10\n  package Data\n    constant Real R_s = data.R_s;\n    record crit\n"
                           "      constant Real TCRIT = 374.18;\n      constant Real PCRIT = 4056290.0;\n    end crit;\n"
                           "    record fcrit\n      constant Real FTCRIT = 374.209;\n      constant Real FPCRIT = 4059280.0;\n"
                           "    end fcrit;\n    record data\n      constant Real R_s = 81.4888564372;\n"
                           "      constant Real MM = 0.102032;\n      extends crit;\n      constant Real HCRIT = 389653;\n"
                           "      extends fcrit;\n    end data;\n  end Data;\n  package R134a\n    function f\n      input Real p;\n"
                           "      output Real pred;\n    algorithm\n"
                           "      pred := p / Data.data.FPCRIT + p / Data.data.MM + Data.R_s * p + P10.Data.data.TCRIT;\n    end f;\n"
                           "  end R134a;\nend P10;\nmodel PKT10\n  Real p = P10.R134a.f(time);\nend PKT10;\n"),
               std::string("function P10.R134a.f\n  input Real p;\n  output Real pred;\nalgorithm\n"
                           "  pred := p / 4.05928e6 + p / 0.102032 + 81.4888564372 * p + 374.18;\nend P10.R134a.f;\n\nclass PKT10\n"
                           "  Real p = P10.R134a.f(time);\nend PKT10;\n"));
}

YMC_TEST(flatten_fn_constant_array_subscripts) {
  // 函数体里常量数组 / 记录数组带下标：字面量下标取元素（`c[Water]`、`c[end]`），符号下标连同其后的留成 `{…}[i, 2]`、`{…}[n - 1]`，
  // `fc[:].Tc` 整维取后 sum 求值。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p23 PKT16s）。
  YMC_CHECK_EQ(flatten_src("package P16s\n  record FC\n    Real Tc;\n    Real arr[3];\n  end FC;\n  record Rec\n    String name;\n"
                           "    Real MM;\n  end Rec;\n  package Mix\n    constant Integer Water = 2;\n"
                           "    constant FC[2] fc = {FC(Tc = 126.2, arr = {1.0, 2.0, 3.0}), FC(Tc = 154.58, arr = {4.0, 5.0, 6.0})};\n"
                           "    constant Real c[3] = {1.5, 2.5, 3.5};\n"
                           "    constant Rec[2] data = {Rec(name = \"N2\", MM = 0.0280134), Rec(name = \"O2\", MM = 0.0319988)};\n"
                           "    function f1\n      input Real T;\n      input Integer n;\n      output Real y;\n    algorithm\n"
                           "      y := 0;\n      for i in 1:2 loop\n        y := y + fc[i].arr[2] * T + fc[2].arr[i] + data[i].MM;\n"
                           "        for j in 1:3 loop\n          y := y + fc[i].arr[j];\n        end for;\n      end for;\n"
                           "      y := y + sum(fc[:].Tc) * T + c[Water] * T + c[n] + fc[n].Tc + c[end] * T + c[n - 1] * T;\n"
                           "    end f1;\n  end Mix;\nend P16s;\nmodel PKT16s\n  Real y = P16s.Mix.f1(time, 3);\nend PKT16s;\n"),
               std::string("function P16s.Mix.f1\n  input Real T;\n  input Integer n;\n  output Real y;\nalgorithm\n  y := 0.0;\n"
                           "  for i in 1:2 loop\n"
                           "    y := y + {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}[i, 2] * T + {4.0, 5.0, 6.0}[i] + {0.0280134, 0.0319988}[i];\n"
                           "    for j in 1:3 loop\n      y := y + {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}[i, j];\n    end for;\n"
                           "  end for;\n"
                           "  y := y + 280.78000000000003 * T + 2.5 * T + {1.5, 2.5, 3.5}[n] + {126.2, 154.58}[n] + 3.5 * T + {1.5, 2.5, 3.5}[n - 1] * T;\n"
                           "end P16s.Mix.f1;\n\nclass PKT16s\n  Real y = P16s.Mix.f1(time, 3);\nend PKT16s;\n"));
}

YMC_TEST(flatten_partial_application_nested_function) {
  // 函数体里的部分函数应用（ADR-009；MSL MoistAir.T_phX 的 `solveOneNonlinearEquation(function f_nonlinear(p = p, …), …)`）：
  // 被调函数的函数型输入打 `f<function>(#Real u) => #Real f`、体里 `unbox(f(#(u_min)))`，调用处 `function PZ1.T_ph.f_nl(#(p),
  // #(h))`。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p24 PKZ1）。
  YMC_CHECK_EQ(flatten_src("package PZ1\n  partial function PSF \"base\"\n    input Real u \"Independent variable\";\n"
                           "    output Real y \"Dependent variable\";\n  end PSF;\n  function solve \"solve f(u) = 0\"\n"
                           "    input PSF f \"the function\";\n    input Real u_min;\n    input Real u_max;\n"
                           "    input Real tolerance = 1e-13;\n    output Real u;\n  protected\n    Real fa;\n    Real fb;\n"
                           "  algorithm\n    fa := f(u_min);\n    fb := f(u_max);\n"
                           "    u := if abs(fa) < tolerance then u_min else u_max - fb * (u_max - u_min) / (fb - fa);\n  end solve;\n"
                           "  function T_ph \"inverse\"\n    input Real p;\n    input Real h;\n    output Real T;\n  protected\n"
                           "    function f_nl \"residual\"\n      extends PSF;\n      input Real p;\n      input Real h;\n"
                           "    algorithm\n      y := p * u - h;\n    end f_nl;\n  algorithm\n"
                           "    T := solve(function f_nl(p = p, h = h), 190, 647);\n  end T_ph;\nend PZ1;\nmodel PKZ1\n"
                           "  Real T = PZ1.T_ph(time, 2);\nend PKZ1;\n"),
               std::string("function PZ1.T_ph \"inverse\"\n  input Real p;\n  input Real h;\n  output Real T;\nalgorithm\n"
                           "  T := PZ1.solve(function PZ1.T_ph.f_nl(#(p), #(h)), 190.0, 647.0, 1e-13);\nend PZ1.T_ph;\n\n"
                           "function PZ1.T_ph.f_nl \"residual\"\n  input Real u \"Independent variable\";\n  input Real p;\n"
                           "  input Real h;\n  output Real y \"Dependent variable\";\nalgorithm\n  y := p * u - h;\n"
                           "end PZ1.T_ph.f_nl;\n\nfunction PZ1.solve \"solve f(u) = 0\"\n"
                           "  input f<function>(#Real u) => #Real f \"the function\";\n  input Real u_min;\n  input Real u_max;\n"
                           "  input Real tolerance = 1e-13;\n  output Real u;\n  protected Real fa;\n  protected Real fb;\nalgorithm\n"
                           "  fa := unbox(f(#(u_min)));\n  fb := unbox(f(#(u_max)));\n"
                           "  u := if abs(fa) < tolerance then u_min else u_max - fb * (u_max - u_min) / (fb - fa);\nend PZ1.solve;\n"
                           "\nclass PKZ1\n  Real T = PZ1.T_ph(time, 2.0);\nend PKZ1;\n"));
}

YMC_TEST(flatten_partial_application_slot_order) {
  // 部分应用的实参按被调函数的输入槽序（`g(b = b, a = a)` → `function PZ2.g(#(a), #(b))`）；函数类型的路径 = 形参名
  // （`fun<function>`）；经函数型输入的调用在运算里照样拆箱。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p24 PKZ2）。
  YMC_CHECK_EQ(flatten_src("package PZ2\n  partial function PSF\n    input Real u;\n    output Real y;\n  end PSF;\n  function g\n"
                           "    extends PSF;\n    input Real a;\n    input Real b;\n  algorithm\n    y := a * u + b;\n  end g;\n"
                           "  function solve\n    input PSF fun;\n    input Real x;\n    output Real r;\n  algorithm\n"
                           "    r := fun(x) + 2 * fun(x + 1);\n  end solve;\n  function h\n    input Real a;\n    input Real b;\n"
                           "    output Real r;\n  algorithm\n    r := solve(function g(b = b, a = a), 3);\n  end h;\nend PZ2;\n"
                           "model PKZ2\n  Real r = PZ2.h(time, 2);\nend PKZ2;\n"),
               std::string("function PZ2.g\n  input Real u;\n  input Real a;\n  input Real b;\n  output Real y;\nalgorithm\n"
                           "  y := a * u + b;\nend PZ2.g;\n\nfunction PZ2.h\n  input Real a;\n  input Real b;\n  output Real r;\n"
                           "algorithm\n  r := PZ2.solve(function PZ2.g(#(a), #(b)), 3.0);\nend PZ2.h;\n\nfunction PZ2.solve\n"
                           "  input fun<function>(#Real u) => #Real fun;\n  input Real x;\n  output Real r;\nalgorithm\n"
                           "  r := unbox(fun(#(x))) + 2.0 * unbox(fun(#(x + 1.0)));\nend PZ2.solve;\n\nclass PKZ2\n"
                           "  Real r = PZ2.h(time, 2.0);\nend PKZ2;\n"));
}

YMC_TEST(flatten_functional_parameter_type_default) {
  // 函数类型里的数组输入与缺省（`#Real[2] u, #Integer k := #(2)`），调用时缺的实参补缺省的装箱字面量 `f(#(v), #(2))`。
  // 期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p24 PKZ4）。
  YMC_CHECK_EQ(flatten_src("package PZ4\n  partial function PF\n    input Real u[2];\n    input Integer k = 2;\n    output Real y;\n"
                           "  end PF;\n  function g\n    extends PF;\n    input Real c;\n  algorithm\n    y := c * u[1] + k * u[2];\n"
                           "  end g;\n  function apply\n    input PF f;\n    input Real v[2];\n    output Real r;\n  algorithm\n"
                           "    r := f(v);\n  end apply;\n  function h\n    input Real c;\n    output Real r;\n  algorithm\n"
                           "    r := apply(function g(c = c), {c, 2 * c});\n  end h;\nend PZ4;\nmodel PKZ4\n  Real r = PZ4.h(time);\n"
                           "end PKZ4;\n"),
               std::string("function PZ4.apply\n  input f<function>(#Real[2] u, #Integer k := #(2)) => #Real f;\n  input Real[2] v;\n"
                           "  output Real r;\nalgorithm\n  r := unbox(f(#(v), #(2)));\nend PZ4.apply;\n\nfunction PZ4.g\n"
                           "  input Real[2] u;\n  input Integer k = 2;\n  input Real c;\n  output Real y;\nalgorithm\n"
                           "  y := c * u[1] + /*Real*/(k) * u[2];\nend PZ4.g;\n\nfunction PZ4.h\n  input Real c;\n  output Real r;\n"
                           "algorithm\n  r := PZ4.apply(function PZ4.g(#(c)), {c, 2.0 * c});\nend PZ4.h;\n\nclass PKZ4\n"
                           "  Real r = PZ4.h(time);\nend PKZ4;\n"));
}

YMC_TEST(flatten_partial_application_model_context) {
  // 模型绑定里的部分应用：`PZ5.solve(function PZ5.g(#(a)), time)`。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p24 PKZ5）。
  YMC_CHECK_EQ(flatten_src("package PZ5\n  partial function PSF\n    input Real u;\n    output Real y;\n  end PSF;\n  function g\n"
                           "    extends PSF;\n    input Real a;\n  algorithm\n    y := a * u - 1;\n  end g;\n  function solve\n"
                           "    input PSF f;\n    input Real x;\n    output Real r;\n  algorithm\n    r := f(x);\n  end solve;\n"
                           "end PZ5;\nmodel PKZ5\n  parameter Real a = 2;\n  Real r = PZ5.solve(function PZ5.g(a = a), time);\n"
                           "end PKZ5;\n"),
               std::string("function PZ5.g\n  input Real u;\n  input Real a;\n  output Real y;\nalgorithm\n  y := a * u - 1.0;\n"
                           "end PZ5.g;\n\nfunction PZ5.solve\n  input f<function>(#Real u) => #Real f;\n  input Real x;\n"
                           "  output Real r;\nalgorithm\n  r := unbox(f(#(x)));\nend PZ5.solve;\n\nclass PKZ5\n"
                           "  parameter Real a = 2.0;\n  Real r = PZ5.solve(function PZ5.g(#(a)), time);\nend PKZ5;\n"));
}

YMC_TEST(flatten_partial_application_redeclared_package) {
  // 经 `redeclare package Medium = …` 的包实例里的函数（MSL Fluid 的 Medium.T_phX）：嵌套函数按实例路径命名
  // `PKZ6.c.Medium.T_phX.f_nl`。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p24 PKZ6）。
  YMC_CHECK_EQ(flatten_src("package PZ6\n  partial function PSF\n    input Real u;\n    output Real y;\n  end PSF;\n  function solve\n"
                           "    input PSF f;\n    input Real a;\n    input Real b;\n    output Real r;\n  algorithm\n"
                           "    r := (f(a) + f(b)) / 2;\n  end solve;\n  package BaseMedium\n    constant Integer nX = 2;\n"
                           "    replaceable function T_phX\n      input Real p;\n      input Real h;\n      input Real X[:];\n"
                           "      output Real T;\n    end T_phX;\n  end BaseMedium;\n  package Moist\n"
                           "    extends BaseMedium(nX = 2);\n    constant Integer nXi = 1;\n    redeclare function extends T_phX\n"
                           "    protected\n      function f_nl\n        extends PSF;\n        input Real p;\n        input Real h;\n"
                           "        input Real X[:];\n      algorithm\n        y := p * u - h + sum(X);\n      end f_nl;\n"
                           "    algorithm\n      T := solve(function f_nl(p = p, h = h, X = X[1:nXi]), 190, 647);\n    end T_phX;\n"
                           "  end Moist;\n  model Comp\n    replaceable package Medium = BaseMedium;\n"
                           "    Real T = Medium.T_phX(1e5, time, {0.1, 0.9});\n  end Comp;\nend PZ6;\nmodel PKZ6\n"
                           "  PZ6.Comp c(redeclare package Medium = PZ6.Moist);\nend PKZ6;\n"),
               std::string("function PKZ6.c.Medium.T_phX\n  input Real p;\n  input Real h;\n  input Real[:] X;\n  output Real T;\n"
                           "algorithm\n  T := PZ6.solve(function PKZ6.c.Medium.T_phX.f_nl(#(p), #(h), #(X[1:1])), 190.0, 647.0);\n"
                           "end PKZ6.c.Medium.T_phX;\n\nfunction PKZ6.c.Medium.T_phX.f_nl\n  input Real u;\n  input Real p;\n"
                           "  input Real h;\n  input Real[:] X;\n  output Real y;\nalgorithm\n  y := p * u - h + sum(X);\n"
                           "end PKZ6.c.Medium.T_phX.f_nl;\n\nfunction PZ6.solve\n  input f<function>(#Real u) => #Real f;\n"
                           "  input Real a;\n  input Real b;\n  output Real r;\nalgorithm\n"
                           "  r := (unbox(f(#(a))) + unbox(f(#(b)))) / 2.0;\nend PZ6.solve;\n\nclass PKZ6\n"
                           "  Real c.T = PKZ6.c.Medium.T_phX(1e5, time, {0.1, 0.9});\nend PKZ6;\n"));
}

YMC_TEST(flatten_function_name_as_argument) {
  // 函数名当实参 `solve(g, a)` → `PZ8.solve(PZ8.g, a)`；经函数型输入的命名实参调用 `f(u = x)` → `unbox(f(#(x)))`。
  // 期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p24 PKZ8）。
  YMC_CHECK_EQ(flatten_src("package PZ8\n  partial function PSF\n    input Real u;\n    output Real y;\n  end PSF;\n  function g\n"
                           "    extends PSF;\n  algorithm\n    y := 2 * u - 1;\n  end g;\n  function solve\n    input PSF f;\n"
                           "    input Real x;\n    output Real r;\n  algorithm\n    r := f(u = x);\n  end solve;\n  function h\n"
                           "    input Real a;\n    output Real r;\n  algorithm\n    r := solve(g, a);\n  end h;\nend PZ8;\n"
                           "model PKZ8\n  Real r = PZ8.h(time);\nend PKZ8;\n"),
               std::string("function PZ8.g\n  input Real u;\n  output Real y;\nalgorithm\n  y := 2.0 * u - 1.0;\nend PZ8.g;\n\n"
                           "function PZ8.h\n  input Real a;\n  output Real r;\nalgorithm\n  r := PZ8.solve(PZ8.g, a);\nend PZ8.h;\n\n"
                           "function PZ8.solve\n  input f<function>(#Real u) => #Real f;\n  input Real x;\n  output Real r;\n"
                           "algorithm\n  r := unbox(f(#(x)));\nend PZ8.solve;\n\nclass PKZ8\n  Real r = PZ8.h(time);\nend PKZ8;\n"));
}

YMC_TEST(flatten_local_short_package_functions) {
  // 模型里不带修饰的本地短类包 `package Medium = PE1.Gas`（MSL Media 例子）：经它的函数 / 记录在包实例里查、按实例路径命名
  // `PKE1.Medium.setState_pT`（NFFunction.lookupFunction :339-344 v1.25.0）。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p25 PKE1）。
  YMC_CHECK_EQ(flatten_src("package PE1\n  partial package PartialMedium \"base\"\n"
                           "    constant Integer nX = 1 \"Number of mass fractions\";\n"
                           "    constant String mediumName = \"unusable\";\n    record ThermodynamicState \"state\"\n      Real p;\n"
                           "      Real T;\n    end ThermodynamicState;\n"
                           "    replaceable partial function setState_pT \"state from p, T\"\n      input Real p;\n"
                           "      input Real T;\n      output ThermodynamicState state;\n    end setState_pT;\n"
                           "    replaceable partial function specificEnthalpy \"h\"\n      input ThermodynamicState state;\n"
                           "      output Real h;\n    end specificEnthalpy;\n  end PartialMedium;\n  package Gas \"gas\"\n"
                           "    extends PartialMedium(nX = 2, mediumName = \"gas\");\n"
                           "    redeclare function extends setState_pT \"gas state\"\n    algorithm\n"
                           "      state := ThermodynamicState(p = p, T = T);\n    end setState_pT;\n"
                           "    redeclare function extends specificEnthalpy \"gas h\"\n    algorithm\n"
                           "      h := 1000.0 * state.T + nX;\n    end specificEnthalpy;\n  end Gas;\nend PE1;\nmodel PKE1\n"
                           "  package Medium = PE1.Gas;\n  Medium.ThermodynamicState s;\n  Real h;\nequation\n"
                           "  s = Medium.setState_pT(1e5, 300 + time);\n  h = Medium.specificEnthalpy(s);\nend PKE1;\n"),
               std::string("function PKE1.Medium.ThermodynamicState \"Automatically generated record constructor for PKE1.Medium.ThermodynamicState\"\n"
                           "  input Real p;\n  input Real T;\n  output ThermodynamicState res;\nend PKE1.Medium.ThermodynamicState;\n"
                           "\nfunction PKE1.Medium.setState_pT \"gas state\"\n  input Real p;\n  input Real T;\n"
                           "  output PKE1.Medium.ThermodynamicState state;\nalgorithm\n"
                           "  state := PKE1.Medium.ThermodynamicState(p, T);\nend PKE1.Medium.setState_pT;\n\n"
                           "function PKE1.Medium.specificEnthalpy \"gas h\"\n  input PKE1.Medium.ThermodynamicState state;\n"
                           "  output Real h;\nalgorithm\n  h := 1000.0 * state.T + 2.0;\nend PKE1.Medium.specificEnthalpy;\n\n"
                           "class PKE1\n  Real s.p;\n  Real s.T;\n  Real h;\nequation\n"
                           "  s = PKE1.Medium.setState_pT(1e5, 300.0 + time);\n  h = PKE1.Medium.specificEnthalpy(s);\nend PKE1;\n"));
}

YMC_TEST(flatten_local_short_package_constants) {
  // 同上，常量取包实例里的值：经 `extends PartialMedium(nX = 2, …)` 改过的 nX / mediumName / T_default、维里的 Medium.nX。
  // 期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p25 PKE2）。
  YMC_CHECK_EQ(flatten_src("package PE2\n  partial package PartialMedium \"base\"\n"
                           "    constant Integer nX = 1 \"Number of mass fractions\";\n"
                           "    constant String mediumName = \"unusable\";\n    constant Real T_default = 293.15;\n"
                           "    constant Real X_default[nX] = fill(1 / nX, nX);\n"
                           "    type Temperature = Real(min = 1, max = 1e4, unit = \"K\");\n    record ThermodynamicState \"state\"\n"
                           "      Real p;\n      Real T;\n    end ThermodynamicState;\n"
                           "    replaceable partial function setState_pT \"state from p, T\"\n      input Real p;\n"
                           "      input Real T;\n      output ThermodynamicState state;\n    end setState_pT;\n"
                           "    replaceable partial function specificEnthalpy \"h\"\n      input ThermodynamicState state;\n"
                           "      output Real h;\n    end specificEnthalpy;\n  end PartialMedium;\n  package Gas \"gas\"\n"
                           "    extends PartialMedium(nX = 2, mediumName = \"gas\", T_default = 300);\n"
                           "    constant Temperature T_min = 200;\n    redeclare function extends setState_pT \"gas state\"\n"
                           "    algorithm\n      state := ThermodynamicState(p = p, T = T);\n    end setState_pT;\n"
                           "    redeclare function extends specificEnthalpy \"gas h\"\n    algorithm\n"
                           "      h := 1000.0 * state.T + nX;\n    end specificEnthalpy;\n  end Gas;\nend PE2;\nmodel PKE2\n"
                           "  package Medium = PE2.Gas;\n  parameter Integer n = Medium.nX;\n"
                           "  parameter String name = Medium.mediumName;\n  parameter Real T0 = Medium.T_default;\n"
                           "  parameter Real Tmin = Medium.T_min;\n  Real X[Medium.nX] = Medium.X_default;\nend PKE2;\n"),
               std::string("class PKE2\n  parameter Integer n = 2;\n  parameter String name = \"gas\";\n  parameter Real T0 = 300.0;\n"
                           "  parameter Real Tmin = 200.0;\n  Real X[1];\n  Real X[2];\nequation\n  X = {0.5, 0.5};\nend PKE2;\n"));
}

YMC_TEST(flatten_local_short_package_record_constant_fields) {
  // 同上，记录常量的字段（MSL MoistAir 的 `{Medium.dryair.MM, Medium.steam.MM}`）与记录数组常量的元素字段。
  // 期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p25 PKE3）。
  YMC_CHECK_EQ(flatten_src("package PE3\n  record DataRecord\n    String name;\n    Real MM;\n    Real R;\n  end DataRecord;\n"
                           "  package Data\n    constant DataRecord H2O(name = \"H2O\", MM = 0.018, R = 461.5);\n"
                           "    constant DataRecord N2(name = \"N2\", MM = 0.028, R = 296.8);\n  end Data;\n"
                           "  partial package PartialMixture\n    constant DataRecord[:] data;\n"
                           "    constant Integer nS = size(data, 1);\n  end PartialMixture;\n  package Moist\n"
                           "    extends PartialMixture(data = {Data.H2O, Data.N2});\n    constant DataRecord steam = Data.H2O;\n"
                           "    constant DataRecord dryair = Data.N2;\n  end Moist;\nend PE3;\nmodel PKE3\n"
                           "  package Medium = PE3.Moist;\n  parameter Real MMx[2] = {Medium.dryair.MM, Medium.steam.MM};\n"
                           "  parameter Real R1 = Medium.data[1].R;\n  parameter Integer n = Medium.nS;\nend PKE3;\n"),
               std::string("class PKE3\n  parameter Real MMx[1] = 0.028;\n  parameter Real MMx[2] = 0.018;\n"
                           "  parameter Real R1 = 461.5;\n  parameter Integer n = 2;\nend PKE3;\n"));
}

YMC_TEST(flatten_local_short_package_in_component_class) {
  // 组件的类里的本地短类包：两个组件各一份函数 / 构造函数，按组件的实例路径 `PKE5.v1.Medium.f` / `PKE5.v2.Medium.f`。
  // 期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p25 PKE5）。
  YMC_CHECK_EQ(flatten_src("package PE5\n  partial package PartialMedium \"base\"\n"
                           "    constant Integer nX = 1 \"Number of mass fractions\";\n"
                           "    constant String mediumName = \"unusable\";\n    constant Real T_default = 293.15;\n"
                           "    constant Real X_default[nX] = fill(1 / nX, nX);\n"
                           "    type Temperature = Real(min = 1, max = 1e4, unit = \"K\");\n    record ThermodynamicState \"state\"\n"
                           "      Real p;\n      Real T;\n    end ThermodynamicState;\n"
                           "    replaceable partial function setState_pT \"state from p, T\"\n      input Real p;\n"
                           "      input Real T;\n      output ThermodynamicState state;\n    end setState_pT;\n"
                           "    replaceable partial function specificEnthalpy \"h\"\n      input ThermodynamicState state;\n"
                           "      output Real h;\n    end specificEnthalpy;\n  end PartialMedium;\n  package Gas \"gas\"\n"
                           "    extends PartialMedium(nX = 2, mediumName = \"gas\", T_default = 300);\n"
                           "    constant Temperature T_min = 200;\n    redeclare function extends setState_pT \"gas state\"\n"
                           "    algorithm\n      state := ThermodynamicState(p = p, T = T);\n    end setState_pT;\n"
                           "    redeclare function extends specificEnthalpy \"gas h\"\n    algorithm\n"
                           "      h := 1000.0 * state.T + nX;\n    end specificEnthalpy;\n  end Gas;\nend PE5;\nmodel Vol5\n"
                           "  package Medium = PE5.Gas;\n  Medium.ThermodynamicState s = Medium.setState_pT(1e5, 300 + time);\n"
                           "  Real h = Medium.specificEnthalpy(s);\nend Vol5;\nmodel PKE5\n  Vol5 v1;\n  Vol5 v2;\nend PKE5;\n"),
               std::string("function PKE5.v1.Medium.ThermodynamicState \"Automatically generated record constructor for PKE5.v1.Medium.ThermodynamicState\"\n"
                           "  input Real p;\n  input Real T;\n  output ThermodynamicState res;\n"
                           "end PKE5.v1.Medium.ThermodynamicState;\n\nfunction PKE5.v1.Medium.setState_pT \"gas state\"\n"
                           "  input Real p;\n  input Real T;\n  output PKE5.v1.Medium.ThermodynamicState state;\nalgorithm\n"
                           "  state := PKE5.v1.Medium.ThermodynamicState(p, T);\nend PKE5.v1.Medium.setState_pT;\n\n"
                           "function PKE5.v1.Medium.specificEnthalpy \"gas h\"\n  input PKE5.v1.Medium.ThermodynamicState state;\n"
                           "  output Real h;\nalgorithm\n  h := 1000.0 * state.T + 2.0;\nend PKE5.v1.Medium.specificEnthalpy;\n\n"
                           "function PKE5.v2.Medium.ThermodynamicState \"Automatically generated record constructor for PKE5.v2.Medium.ThermodynamicState\"\n"
                           "  input Real p;\n  input Real T;\n  output ThermodynamicState res;\n"
                           "end PKE5.v2.Medium.ThermodynamicState;\n\nfunction PKE5.v2.Medium.setState_pT \"gas state\"\n"
                           "  input Real p;\n  input Real T;\n  output PKE5.v2.Medium.ThermodynamicState state;\nalgorithm\n"
                           "  state := PKE5.v2.Medium.ThermodynamicState(p, T);\nend PKE5.v2.Medium.setState_pT;\n\n"
                           "function PKE5.v2.Medium.specificEnthalpy \"gas h\"\n  input PKE5.v2.Medium.ThermodynamicState state;\n"
                           "  output Real h;\nalgorithm\n  h := 1000.0 * state.T + 2.0;\nend PKE5.v2.Medium.specificEnthalpy;\n\n"
                           "class PKE5\n  Real v1.s.p;\n  Real v1.s.T;\n  Real v1.h = PKE5.v1.Medium.specificEnthalpy(v1.s);\n"
                           "  Real v2.s.p;\n  Real v2.s.T;\n  Real v2.h = PKE5.v2.Medium.specificEnthalpy(v2.s);\nequation\n"
                           "  v1.s = PKE5.v1.Medium.setState_pT(1e5, 300.0 + time);\n"
                           "  v2.s = PKE5.v2.Medium.setState_pT(1e5, 300.0 + time);\nend PKE5;\n"));
}

YMC_TEST(flatten_local_short_package_record_result_without_component) {
  // replaceable 本地短类包；返回记录的调用嵌在调用里、模型里没有这个记录类型的组件（MSL SolveOneNonlinearEquation 例子）：
  // 类型取包实例里的记录类节点，构造函数 `PKE4.Medium.ThermodynamicState`（此前 "record result" SKIP）。
  // 期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p25 PKE4）。
  YMC_CHECK_EQ(flatten_src("package PE4\n  partial package PartialMedium \"base\"\n"
                           "    constant Integer nX = 1 \"Number of mass fractions\";\n"
                           "    constant String mediumName = \"unusable\";\n    constant Real T_default = 293.15;\n"
                           "    constant Real X_default[nX] = fill(1 / nX, nX);\n"
                           "    type Temperature = Real(min = 1, max = 1e4, unit = \"K\");\n    record ThermodynamicState \"state\"\n"
                           "      Real p;\n      Real T;\n    end ThermodynamicState;\n"
                           "    replaceable partial function setState_pT \"state from p, T\"\n      input Real p;\n"
                           "      input Real T;\n      output ThermodynamicState state;\n    end setState_pT;\n"
                           "    replaceable partial function specificEnthalpy \"h\"\n      input ThermodynamicState state;\n"
                           "      output Real h;\n    end specificEnthalpy;\n  end PartialMedium;\n  package Gas \"gas\"\n"
                           "    extends PartialMedium(nX = 2, mediumName = \"gas\", T_default = 300);\n"
                           "    constant Temperature T_min = 200;\n    redeclare function extends setState_pT \"gas state\"\n"
                           "    algorithm\n      state := ThermodynamicState(p = p, T = T);\n    end setState_pT;\n"
                           "    redeclare function extends specificEnthalpy \"gas h\"\n    algorithm\n"
                           "      h := 1000.0 * state.T + nX;\n    end specificEnthalpy;\n  end Gas;\nend PE4;\nmodel PKE4\n"
                           "  replaceable package Medium = PE4.Gas constrainedby PE4.PartialMedium;\n  parameter Real T0 = 300;\n"
                           "  final parameter Real h0 = Medium.specificEnthalpy(Medium.setState_pT(1e5, T0));\n"
                           "  Real h = Medium.specificEnthalpy(Medium.setState_pT(1e5, T0 + time));\nend PKE4;\n"),
               std::string("function PKE4.Medium.ThermodynamicState \"Automatically generated record constructor for PKE4.Medium.ThermodynamicState\"\n"
                           "  input Real p;\n  input Real T;\n  output ThermodynamicState res;\nend PKE4.Medium.ThermodynamicState;\n"
                           "\nfunction PKE4.Medium.setState_pT \"gas state\"\n  input Real p;\n  input Real T;\n"
                           "  output PKE4.Medium.ThermodynamicState state;\nalgorithm\n"
                           "  state := PKE4.Medium.ThermodynamicState(p, T);\nend PKE4.Medium.setState_pT;\n\n"
                           "function PKE4.Medium.specificEnthalpy \"gas h\"\n  input PKE4.Medium.ThermodynamicState state;\n"
                           "  output Real h;\nalgorithm\n  h := 1000.0 * state.T + 2.0;\nend PKE4.Medium.specificEnthalpy;\n\n"
                           "class PKE4\n  parameter Real T0 = 300.0;\n"
                           "  final parameter Real h0 = PKE4.Medium.specificEnthalpy(PKE4.Medium.setState_pT(1e5, T0));\n"
                           "  Real h = PKE4.Medium.specificEnthalpy(PKE4.Medium.setState_pT(1e5, T0 + time));\nend PKE4;\n"));
}

YMC_TEST(flatten_local_short_package_record_ctor_without_component) {
  // 经本地短类包的记录构造调用作实参、模型里没有这个记录类型的组件：构造函数只按实例路径打一个（此前多打定义路径的
  // `PE23.PartialMedium.ThermodynamicState`）。期望 = omc 1.25.0 实跑（2026-10-15，探针 pk-p25 PKE23）。
  YMC_CHECK_EQ(flatten_src("package PE23\n  partial package PartialMedium \"base\"\n"
                           "    constant Integer nX = 1 \"Number of mass fractions\";\n"
                           "    constant String mediumName = \"unusable\";\n    constant Real T_default = 293.15;\n"
                           "    constant Real X_default[nX] = fill(1 / nX, nX);\n"
                           "    type Temperature = Real(min = 1, max = 1e4, unit = \"K\");\n    record ThermodynamicState \"state\"\n"
                           "      Real p;\n      Real T;\n    end ThermodynamicState;\n"
                           "    replaceable partial function setState_pT \"state from p, T\"\n      input Real p;\n"
                           "      input Real T;\n      output ThermodynamicState state;\n    end setState_pT;\n"
                           "    replaceable partial function specificEnthalpy \"h\"\n      input ThermodynamicState state;\n"
                           "      output Real h;\n    end specificEnthalpy;\n  end PartialMedium;\n  package Gas \"gas\"\n"
                           "    extends PartialMedium(nX = 2, mediumName = \"gas\", T_default = 300);\n"
                           "    constant Temperature T_min = 200;\n    redeclare function extends setState_pT \"gas state\"\n"
                           "    algorithm\n      state := ThermodynamicState(p = p, T = T);\n    end setState_pT;\n"
                           "    redeclare function extends specificEnthalpy \"gas h\"\n    algorithm\n"
                           "      h := 1000.0 * state.T + nX;\n    end specificEnthalpy;\n  end Gas;\nend PE23;\nmodel PKE23\n"
                           "  package Medium = PE23.Gas;\n"
                           "  Real h = Medium.specificEnthalpy(Medium.ThermodynamicState(p = 1e5, T = 300 + time));\nend PKE23;\n"),
               std::string("function PKE23.Medium.ThermodynamicState \"Automatically generated record constructor for PKE23.Medium.ThermodynamicState\"\n"
                           "  input Real p;\n  input Real T;\n  output ThermodynamicState res;\nend PKE23.Medium.ThermodynamicState;\n"
                           "\nfunction PKE23.Medium.specificEnthalpy \"gas h\"\n  input PKE23.Medium.ThermodynamicState state;\n"
                           "  output Real h;\nalgorithm\n  h := 1000.0 * state.T + 2.0;\nend PKE23.Medium.specificEnthalpy;\n\n"
                           "class PKE23\n"
                           "  Real h = PKE23.Medium.specificEnthalpy(PKE23.Medium.ThermodynamicState(1e5, 300.0 + time));\n"
                           "end PKE23;\n"));
}

YMC_TEST(flatten_local_short_package_protected_access) {
  // 经本地短类包取 protected 常量：omc 报 "Illegal access of protected element secret"（LookupState.checkProtection，
  // NFLookupState.mo:441-466 v1.25.0）→ SKIP（此前包实例路径不查 protected、静默出值；2026-10-15，探针 pk-p25 PKE13）。
  YMC_CHECK_EQ(flatten_src("package PE13\n  package Gas\n    constant Real a = 1.5;\n  protected\n    constant Real secret = 2.5;\n"
                           "  end Gas;\nend PE13;\nmodel PKE13\n  package Medium = PE13.Gas;\n  parameter Real y = Medium.secret;\n"
                           "end PKE13;\n"),
               std::string("<unsupported:access of protected element>"));
}

YMC_TEST_MAIN
