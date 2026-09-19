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

YMC_TEST(flatten_whole_array_reference_unsupported) {
  // 数组整体引用（数组运算/方程）v1 未做 → SKIP。
  bool uns = false;
  flatten_src("model M\n  Real x[3];\n  Real y[3];\nequation\n  y = x;\nend M;\n", &uns);
  YMC_CHECK(uns);
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

YMC_TEST(flatten_constant_record_unsupported) {
  // `constant R r` 变率前缀要下沉成员 + 常量字段引用求值（CevalRecord3/5），本桶未做 → 干净 SKIP。
  bool uns = false;
  std::string t = flatten_src(
      "record R\n  Real x;\nend R;\nmodel M\n  constant R r = R(1.0);\nend M;\n", &uns);
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

YMC_TEST(flatten_multiple_connect_unsupported) {
  // 多条 connect 要连接集合并（union-find，元素序是 OMC 内部产物）→ v1 未做，干净 SKIP。
  bool uns = false;
  std::string t = flatten_src(
      "connector C\n  Real e;\n  flow Real f;\nend C;\n"
      "model M\n  C c1, c2, c3;\nequation\n  connect(c1, c2);\n  connect(c2, c3);\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
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

YMC_TEST(flatten_scalar_builtin_division_by_zero_unsupported) {
  bool uns = false;
  const std::string out = flatten_src("model M\n  Integer x = div(1, 0);\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK_EQ(out, std::string("<unsupported:division by zero in constant>"));
}

YMC_TEST_MAIN
