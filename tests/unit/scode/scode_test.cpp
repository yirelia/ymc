// S2 SCode 翻译 + dump 测试。期望文本取自 OMC 1.25.0 对同一模型 `list(ProbeR9, exportKind=SCode)` 的实跑
//（研究底稿 R9 的 probe-r9/probe_out.txt），逐段固化 OMC 的归一化规则。
#include "ymc_test.h"

#include "absyn/absyn.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "parse/lexer.h"
#include "parse/parser.h"
#include "scode/absyn_to_scode.h"
#include "scode/scode.h"
#include "scode/scode_dump.h"

#include <string>

using namespace ymc;

namespace {

struct Ctx {
  Interner in;
  Diagnostics d;
  Absyn a{in};
  SCode s{a};
};

// 解析 + 翻译 + dump 整个 program
std::string scode_of(Ctx& c, const char* src) {
  Lexer lx(src, 0, c.in, c.d);
  std::vector<Token> toks = lx.tokenize();
  Parser p(toks, c.a, c.d);
  p.parse_program();
  translate_program(c.s, c.a.top);
  return dump_scode_program(c.s);
}

bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

}  // namespace

// R9 探针模型的核心片段（OMC 输出见 probe_out.txt）
YMC_TEST(scode_components_split_and_normalized) {
  Ctx c;
  std::string t = scode_of(c,
      "package P\n"
      "  import Modelica.Constants.{pi, e2 = e};\n"
      "  model Comp\n"
      "    extends Base(k = 2) annotation(Documentation(info = \"ext\"));\n"
      "    Real a, b(start = 1, fixed = true) \"b comment\";\n"
      "    Real c[2], d[3] annotation(HideResult = true);\n"
      "    Real[2] m2[3];\n"
      "    replaceable model M = Base constrainedby Base(k = 3) \"M comment\";\n"
      "    replaceable Base rb constrainedby Base(k = 4);\n"
      "    Real x if useX;\n"
      "    final parameter Integer n = 3;\n"
      "    inner Real g = 9.81;\n"
      "    Real q(y.start = 5);\n"
      "    final Real fq;\n"
      "    Real sub(start = 1) = 2;\n"
      "  protected\n"
      "    Real z;\n"
      "    discrete Real w;\n"
      "  end Comp;\n"
      "end P;\n");
  YMC_CHECK(!c.d.has_errors());
  YMC_CHECK(has(t, "  import Modelica.Constants.pi;\n  import e2 = Modelica.Constants.e;\n"));   // 组导入拆分
  YMC_CHECK(has(t, "    extends Base(k = 2);\n"));                                              // Documentation 剥掉
  YMC_CHECK(has(t, "    Real a;\n    Real b(start = 1, fixed = true) \"b comment\";\n"));       // 组件列表拆分
  YMC_CHECK(has(t, "    Real c[2];\n    Real d[3] annotation(HideResult = true);\n"));
  YMC_CHECK(has(t, "    Real m2[3, 2];\n"));                                                    // 组件维在前
  YMC_CHECK(has(t, "    replaceable model M = Base constrainedby Base(k = 3);\n"));            // 注释进 ConstrainClass
  YMC_CHECK(has(t, "    replaceable Base rb constrainedby Base(k = 4);\n"));
  YMC_CHECK(has(t, "    Real x if useX;\n"));
  YMC_CHECK(has(t, "    final parameter Integer n = 3;\n"));
  YMC_CHECK(has(t, "    inner Real g = 9.81;\n"));
  YMC_CHECK(has(t, "    Real q(y(start = 5));\n"));                                             // x.y = z → x(y = z)
  YMC_CHECK(has(t, "    final Real fq;\n"));
  YMC_CHECK(has(t, "    Real sub(start = 1) = 2;\n"));
  YMC_CHECK(has(t, "  protected\n    Real z;\n    discrete Real w;\n"));                        // 段标签
}

YMC_TEST(scode_equations_and_statements) {
  Ctx c;
  std::string t = scode_of(c,
      "model Comp\n"
      "  Real a, b, w, x, g;\n"
      "  Real d[3];\n"
      "  Real m2[3, 2];\n"
      "equation\n"
      "  a = sin(time);\n"
      "  assert(a > -2, \"a too small\");\n"
      "  assert(b > -2, \"b too small\", AssertionLevel.warning);\n"
      "  assert(a > -3, \"named level\", level = AssertionLevel.warning);\n"
      "  Modelica.Utilities.Streams.print(\"hi\");\n"
      "  when a > 0.5 then\n"
      "    reinit(g, 0);\n"
      "  elsewhen a < -0.5 then\n"
      "    w = 1;\n"
      "  end when;\n"
      "  if useX then\n"
      "    x = 1;\n"
      "  elseif n > 2 then\n"
      "    x = 2;\n"
      "  else\n"
      "    x = 3;\n"
      "  end if;\n"
      "  for i in 1:2, j in 1:3 loop\n"
      "    m2[i, j] = 0;\n"
      "  end for;\n"
      "  for k2 loop\n"
      "    d[k2] = 0;\n"
      "  end for;\n"
      "  connect(p, rb.p);\n"
      "equation\n"
      "  p.v = 0;\n"
      "initial equation\n"
      "  z = 0;\n"
      "algorithm\n"
      "  (a, b) := two(1.0);\n"
      "  while a < 0 loop\n"
      "    a := a + 1;\n"
      "  end while;\n"
      "  if a > 1 then\n"
      "    z := 1;\n"
      "  elseif a > 0 then\n"
      "    z := 2;\n"
      "  else\n"
      "    z := 3;\n"
      "  end if;\n"
      "  assert(a > 0, \"alg assert\");\n"
      "  terminate(\"done\");\n"
      "  reinit(g, 1);\n"
      "end Comp;\n");
  YMC_CHECK(!c.d.has_errors());
  // 段顺序：initial equation → equation（且第二个 equation 段 `p.v = 0` 反转到最前）→ algorithm
  YMC_CHECK(has(t, "initial equation\n  z = 0;\nequation\n  p.v = 0;\n  a = sin(time);\n"));
  YMC_CHECK(has(t, "  assert(a > -2, \"a too small\");\n"));                                   // error 级别省略
  YMC_CHECK(has(t, "  assert(b > -2, \"b too small\", AssertionLevel.warning);\n"));
  YMC_CHECK(has(t, "  assert(a > -3, \"named level\", AssertionLevel.warning);\n"));           // 具名 level 归一化
  YMC_CHECK(has(t, "  Modelica.Utilities.Streams.print(\"hi\");\n"));                          // NORETCALL
  YMC_CHECK(has(t, "  when a > 0.5 then\n    reinit(g, 0);\n  elsewhen a < -0.5 then\n    w = 1;\n  end when;\n"));
  YMC_CHECK(has(t, "  if useX then\n    x = 1;\n  elseif n > 2 then\n    x = 2;\n  else\n    x = 3;\n  end if;\n"));
  YMC_CHECK(has(t, "  for i in 1:2 loop\n    for j in 1:3 loop\n      m2[i, j] = 0;\n    end for;\n  end for;\n"));  // 嵌套
  YMC_CHECK(has(t, "  for k2 loop\n    d[k2] = 0;\n  end for;\n"));
  YMC_CHECK(has(t, "  connect(p, rb.p);\nalgorithm\n  (a, b) := two(1.0);\n"));
  YMC_CHECK(has(t, "  while a < 0 loop\n    a := a + 1;\n  end while;\n"));
  YMC_CHECK(has(t, "  if a > 1 then\n    z := 1;\n  elseif a > 0 then\n    z := 2;\n  else\n    z := 3;\n  end if;\n"));
  YMC_CHECK(has(t, "  assert(a > 0, \"alg assert\");\n  terminate(\"done\");\n  reinit(g, 1);\n"));
}

YMC_TEST(scode_short_classes_redeclare_external) {
  Ctx c;
  std::string t = scode_of(c,
      "package P\n"
      "  type Angle = Real(unit = \"rad\", displayUnit = \"deg\") \"angle\";\n"
      "  type Color = enumeration(red \"R comment\", green, blue) \"colors\";\n"
      "  model Sys\n"
      "    Comp comp(redeclare model M = Base, a(start = 0), b(start = 3), each c = {1, 2}, final n = 4);\n"
      "    Comp comp2(redeclare replaceable model M = Base(k = 5) constrainedby Base, redeclare final Base rb(k = 6));\n"
      "  end Sys;\n"
      "  model extends Ext(k = 7)\n"
      "    Real q2;\n"
      "  end Ext;\n"
      "  function f\n"
      "    input Real x;\n"
      "    output Real y;\n"
      "    external \"C\" y = foo(x) annotation(Library = \"bar\");\n"
      "  end f;\n"
      "  function df = der(f, x);\n"
      "end P;\n");
  YMC_CHECK(!c.d.has_errors());
  YMC_CHECK(has(t, "  type Angle = Real(unit = \"rad\", displayUnit = \"deg\") \"angle\";\n"));
  YMC_CHECK(has(t, "  type Color = enumeration(red \"R comment\", green, blue) \"colors\";\n"));
  YMC_CHECK(has(t, "    Comp comp(redeclare model M = Base, a(start = 0), b(start = 3), each c = {1, 2}, final n = 4);\n"));
  YMC_CHECK(has(t, "    Comp comp2(redeclare replaceable model M = Base(k = 5) constrainedby Base, redeclare final Base rb(k = 6));\n"));
  YMC_CHECK(has(t, "  model extends Ext(k = 7)\n    Real q2;\n  end Ext;\n"));
  YMC_CHECK(has(t, "    external \"C\" y = foo(x) annotation(Library = \"bar\");\n"));
  YMC_CHECK(has(t, "  function df = der(f, x);\n"));
}

// PARTS 类之间空行、空修饰规范化、顶层 final
YMC_TEST(scode_blank_lines_and_nomod) {
  Ctx c;
  std::string t = scode_of(c,
      "package P\n  import A.B;\n  connector Pin\n    Real v;\n  end Pin;\n  type T = Real;\n"
      "  model M\n    Real x();\n  end M;\nend P;\n");
  YMC_CHECK(has(t, "  import A.B;\n\n  connector Pin\n    Real v;\n  end Pin;\n\n  type T = Real;\n\n  model M\n    Real x;\n  end M;\nend P;\n"));
}

// 空子修饰：普通修饰里丢弃（final/each 除外），注解顶层保留但不打印，嵌套层丢弃；只有注释也算空。
// 期望 = omc 1.25.0 对 probe-s2/emptymod.mos 的实跑（2026-09-17）
YMC_TEST(scode_empty_submods_match_omc_probe) {
  Ctx c;
  std::string t = scode_of(c,
      "package PAnn\n"
      "  model A annotation(__Dymola_Commands, Evaluate = true); end A;\n"
      "  model B\n    Real x annotation(Foo, Bar(baz), Qux(a = 1, b), Zed());\n  end B;\n"
      "  model C\n    Real x(final y, each z, w(), v(u), t \"cmt\") = 1;\n  end C;\n"
      "  model D annotation(Foo); end D;\n"
      "end PAnn;\n");
  YMC_CHECK(!c.d.has_errors());
  YMC_CHECK(has(t, "  model A annotation(Evaluate = true); end A;\n"));
  YMC_CHECK(has(t, "    Real x annotation(Qux(a = 1));\n"));
  YMC_CHECK(has(t, "    Real x(final y, each z) = 1;\n"));
  YMC_CHECK(has(t, "  model D end D;\n"));
}

// 类体为空/类注解/protected 前空行/pure 函数/注释转义：期望文本 = omc 1.25.0 对 probe-s2/layout.mos 的实跑（2026-09-16）
YMC_TEST(scode_layout_matches_omc_probe) {
  Ctx c;
  std::string t = scode_of(c,
      "package PA\n"
      "  model Empty annotation(Evaluate = true); end Empty;\n"
      "  model Full Real x; annotation(experiment(StopTime = 1), Documentation(info = \"d\")); end Full;\n"
      "  model Blank end Blank;\n"
      "  model Hidden annotation(Documentation(info = \"d\")); end Hidden;\n"
      "  type T = Real annotation(Evaluate = true);\n"
      "  pure function f2\n    input Real x;\n    output Real y;\n  algorithm\n    y := x;\n"
      "    annotation(Inline = true);\n  end f2;\n"
      "  impure function f3\n    input Real x;\n    output Real y;\n  algorithm\n    y := x;\n  end f3;\n"
      "  package Q\n    model A end A;\n  protected\n    model B end B;\n    Real z;\n  end Q;\n"
      "  model Cm\n    Real s = 1 \"a \\\"q\\\" b\";\n    parameter String t = \"x\\ny\";\n  end Cm;\n"
      "end PA;\n");
  YMC_CHECK(!c.d.has_errors());
  YMC_CHECK_EQ(t,
      std::string(
          "package PA\n"
          "  model Empty annotation(Evaluate = true); end Empty;\n"
          "\n"
          "  model Full\n"
          "    Real x;\n"
          "    annotation(experiment(StopTime = 1));\n"
          "  end Full;\n"
          "\n"
          "  model Blank end Blank;\n"
          "\n"
          "  model Hidden end Hidden;\n"
          "\n"
          "  type T = Real annotation(Evaluate = true);\n"
          "\n"
          "  pure function f2\n"
          "    input Real x;\n"
          "    output Real y;\n"
          "  algorithm\n"
          "    y := x;\n"
          "    annotation(Inline = true);\n"
          "  end f2;\n"
          "\n"
          "  impure function f3\n"
          "    input Real x;\n"
          "    output Real y;\n"
          "  algorithm\n"
          "    y := x;\n"
          "  end f3;\n"
          "\n"
          "  package Q\n"
          "    model A end A;\n"
          "\n"
          "  protected\n"
          "    model B end B;\n"
          "\n"
          "    Real z;\n"
          "  end Q;\n"
          "\n"
          "  model Cm\n"
          "    Real s = 1 \"a \\\"q\\\" b\";\n"
          "    parameter String t = \"x\\ny\";\n"
          "  end Cm;\n"
          "end PA;\n"));
}

YMC_TEST_MAIN
