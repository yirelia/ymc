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
  FlattenResult fr = flatten(inst, in, root);
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

// 非常量下标（x[n]，n 是变量）→ omc 保留数组字面量下标，ymc 无此路径 → 保守 SKIP。
YMC_TEST(flatten_constant_array_variable_index_unsupported) {
  bool uns = false;
  std::string t = flatten_src("model M\n  constant Real x[3] = {1,2,3};\n  Integer n = 2;\n  Real y = x[n];\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
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

YMC_TEST(flatten_nested_class_modifier_unsupported) {
  // 嵌套类修饰 a(B(x=2.0))（B 是类名非组件名）→ 干净 SKIP。
  bool uns = false;
  std::string t = flatten_src(
      "model A\n  model B\n    Real x = 1.0;\n  end B;\n  B b;\nend A;\n"
      "model M\n  A a(B(x = 2.0));\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
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

YMC_TEST(flatten_extends_modifier_on_nested_type_unsupported) {
  // extends 修饰命名嵌套类型（类修饰 mod9）→ 干净 SKIP。
  bool uns = false;
  std::string t = flatten_src(
      "model A\n  type MyReal = Real;\n  MyReal x;\nend A;\n"
      "model B\n  extends A(MyReal(start = 2.0));\nend B;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
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

YMC_TEST(flatten_outer_without_inner_unsupported) {
  // 没有匹配 inner 的 outer → 干净 SKIP（omc 报错，v1 未做诊断）。
  bool uns = false;
  std::string t = flatten_src("model A\n  outer Real x;\n  Real y = x;\nend A;\n"
                              "model M\n  A a;\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
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
  //（UNASSIGNED_FUNCTION_OUTPUT）、纯函数调 impure（ImpureCall1 告警）→ SKIP。期望 = 文件 Result 且与 1.25.0 实跑一致。
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
           "function f\n  input Real x;\n  output Real y;\nend f;\nmodel M\n  Real x1 = f(time);\nend M;\n",
           "impure function f\n  input Real x;\n  output Real y = x;\nend f;\nfunction f2\n  input Real x;\n  output Real y;\n"
           "algorithm\n  y := f(x);\nend f2;\nmodel M\n  Real x = f2(time);\nend M;\n"}) {
    bool uns = false;
    flatten_src(s, &uns);
    YMC_CHECK(uns);
  }
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

YMC_TEST(flatten_unbound_parameter_with_start_unsupported) {
  // 无绑定 parameter 带 start → omc 补默认绑定 + 告警（参数默认值规则），本桶未做 → 干净 SKIP。
  bool uns = false;
  std::string t = flatten_src("model M\n  parameter Real x(start = 1.0);\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
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
  // 枚举字面量名）；Real 结果带指数（1.25.0 是 Windows CRT 的三位指数）→ SKIP。期望 = omc 1.25.0 实跑 S1 / S3（2026-10-02）。
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
  bool uns = false;
  const std::string t = flatten_src("model M\n  String b = String(1e10);\nend M;\n", &uns);
  YMC_CHECK(uns);
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
  // 3581-3590 + flattenIfEquation NFFlatten.mo:1975-2010）；前面有运行时分支的参数条件原样保留。条件参数非字面量绑定
  //（omc 传递标记依赖求值次序）→ SKIP。期望 = omc 1.25.0 实跑 IF1 / IF2 / IF3（2026-10-02）。
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
  bool uns = false;
  const std::string out = flatten_src(
      "model IF3\n  parameter Real c = 2;\n  parameter Boolean b = c > 1;\n  Real x;\nequation\n  if b then\n    x = 1;\n"
      "  else\n    x = 2;\n  end if;\nend IF3;\n",
      &uns);
  YMC_CHECK(uns);   // omc：b final、c 不 final（b 的绑定已被 Ceval 缓存，不再传递标记）
  YMC_CHECK_EQ(out, std::string("<unsupported:if condition folds via parameter>"));
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

YMC_TEST(flatten_scalar_builtin_division_by_zero_unsupported) {
  bool uns = false;
  const std::string out = flatten_src("model M\n  Integer x = div(1, 0);\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:division by zero in constant>"));
}

YMC_TEST_MAIN
