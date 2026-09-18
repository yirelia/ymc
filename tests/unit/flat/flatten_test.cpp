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

YMC_TEST(flatten_unbound_parameter_with_start_unsupported) {
  // 无绑定 parameter 带 start → omc 补默认绑定 + 告警（参数默认值规则），本桶未做 → 干净 SKIP。
  bool uns = false;
  std::string t = flatten_src("model M\n  parameter Real x(start = 1.0);\nend M;\n", &uns);
  YMC_CHECK(uns);
  YMC_CHECK(t.rfind("<unsupported:", 0) == 0);
}

YMC_TEST_MAIN
