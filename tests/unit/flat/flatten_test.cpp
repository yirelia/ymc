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

YMC_TEST(flatten_comprehension_inside_array_expression_unsupported) {
  // 推导式嵌在数组运算里：omc 1.25.0 整体保留并按数组算符打印（R1 探针 `x = array(/*Real*/(i) * time for i in 1:3) * 2.0`、
  // `y = -array(…)`；R2 探针 `s = array(…) * {1.0, 2.0, 3.0}`），ymc 未做 → 必须 SKIP，不能逐元素展开。参数数组的逐元素摊分
  // 与 sum 的实参照旧展开（期望 = R2 探针，2026-10-02）。
  for (const char* src : {"model R1a\n  Real x[3];\nequation\n  x = 2 * {i * time for i in 1:3};\nend R1a;\n",
                          "model R1b\n  Real y[3] = -{i * time for i in 1:3};\nend R1b;\n",
                          "model R2s\n  Real s = {i * time for i in 1:3} * {1, 2, 3};\nend R2s;\n"}) {
    bool uns = false;
    const std::string out = flatten_src(src, &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, std::string("<unsupported:comprehension inside array expression>"));
  }
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
  // 未做 / 非法的形态必须 SKIP（V4 探针）：嵌在数组运算里（omc `array(…) * 2.0`）、范围切片实参（omc `x[(2:3)[$i1]]`）、向量化
  // 维不一致（omc 内部错）、rem（1.25.0 打成 OpenModelica.Internal.realRem）。
  const char* pre = "function F\n  input Real a;\n  input Real b;\n  output Real c = a + b;\nend F;\nmodel M\n"
                    "  Real x[3] = {time, 1, 2};\n  Real w[2, 3] = {{time, 1, 2}, {3, 4, 5}};\n";
  for (const auto& [line, why] : std::vector<std::pair<std::string, std::string>>{
           {"  Real l[3] = 2 * F(x, 1);\n", "comprehension inside array expression"},
           {"  Real y[2] = F(x[2:3], 1);\n", "function: F"},
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
  // 未做的形态 → SKIP（EV 探针）：无绑定只有 start（omc 取 start 并告警）、fixed = false（omc 忽略 Evaluate）、数组参数。
  for (const char* src : {"model E2\n  parameter Real p(start = 1) annotation(Evaluate = true);\n  Real x = p * time;\nend E2;\n",
                          "model E3\n  parameter Real p(fixed = false) annotation(Evaluate = true);\n  Real x = p * time;\n"
                          "initial equation\n  p = 3;\nend E3;\n",
                          "model E7\n  parameter Real x[3] = {1, 2, 3} annotation(Evaluate = true);\n  Real y = x[2] * time;\nend E7;\n"}) {
    bool uns = false;
    const std::string out = flatten_src(src, &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, std::string("<unsupported:Evaluate annotation>"));
  }
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
  // 未做 / omc 报错 → SKIP：__OpenModelica_EarlyInline（omc 把调用内联成 `2.0 * time`，FAS1）、derivative(order = 2)（FAS2）、
  // derivative 指向不存在的函数（omc 报错，FAS3）。
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           {"function f\n  input Real x;\n  output Real y = 2 * x;\n  annotation(__OpenModelica_EarlyInline = true);\nend f;\n"
            "model FAS1\n  Real a = f(time);\nend FAS1;\n",
            "function: f"},
           {"function f1\n  input Real x;\n  output Real z = 2 * x;\n  annotation(derivative(order = 2) = f2);\nend f1;\n"
            "function f2\n  input Real x;\n  input Real der_x;\n  input Real der_der_x;\n  output Real z = 2 * der_der_x;\nend f2;\n"
            "model FAS2\n  Real b = f1(time);\nend FAS2;\n",
            "function: f1"},
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
  // 代入后实参全成字面量的用户函数调用 omc 会求值（NFSimplifyExp.simplifyCall :206；FAS4 探针 `m = 6.0`、g 不打印），未做 → SKIP。
  bool uns = false;
  const std::string out = flatten_src("function g\n  input Real x;\n  output Real y = 2 * x;\nend g;\nmodel FAS4\n"
                                      "  parameter Integer n = 2;\n  parameter Real m = g(n) + n;\n  Real z[n];\nend FAS4;\n",
                                      &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:constant user call after structural substitution>"));
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
  // 结构参数数组的整体 / 切片引用：omc 代入整个数组的值（`y = {1, 2}`，APS2），ymc 只按元素代入 → SKIP。
  bool uns = false;
  const std::string out = flatten_src("model APS2\n  parameter Integer k[2] = {1, 2};\n  Real z[k[2]];\n"
                                      "  Integer y[2] = k;\nend APS2;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:whole reference to structural parameter array>"));
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
  // 被引用的数组声明在引用处之后（要靠绑定的类型推，未做）→ SKIP。
  bool uns = false;
  const std::string out = flatten_src("model DMS3\n  Real y[size(a, 1)];\n  parameter Real a[:] = {1, 1};\nend DMS3;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:non-literal array dimension>"));
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
            "invalid extends lookup"}}) {
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
  for (const auto& [src, why] : std::vector<std::pair<std::string, std::string>>{
           // 类注释里的转义 omc 再转义一遍（`"say \\\"hi\\\""`），不复刻。
           {"model WCS4 \"say \\\"hi\\\"\"\n  Real y = time;\nend WCS4;\n", "class comment with escapes"},
           // 标识符里的非 ASCII 字符：omc 的词法器报错（ymc 的词法器放行，任务卡 T-S3-09）。
           {"model WCS5\n  Real 'h\xc3\xb6" "he' = time;\nend WCS5;\n", "non-ASCII identifier"}}) {
    bool uns = false;
    const std::string out = flatten_src(src.c_str(), &uns);
    YMC_CHECK(uns);
    YMC_CHECK_EQ(out, "<unsupported:" + why + ">");
  }
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

YMC_TEST(flatten_scalar_builtin_division_by_zero_unsupported) {
  bool uns = false;
  const std::string out = flatten_src("model M\n  Integer x = div(1, 0);\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:division by zero in constant>"));
}

YMC_TEST_MAIN
