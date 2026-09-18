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
  FlattenResult fr = flatten(inst, in, root);
  if (unsupported) *unsupported = fr.unsupported;
  if (fr.unsupported) return "<unsupported:" + fr.unsupported_reason + ">";
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

YMC_TEST_MAIN
