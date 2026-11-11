// S1 词法/语法/unparse 的验收测试（v2）。round-trip 幂等是方案 §5 S1 验收 ②。
#include "ymc_test.h"

#include "absyn/absyn.h"
#include "absyn/unparse.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "parse/lexer.h"
#include "parse/parser.h"

#include <string>
#include <utility>
#include <vector>

using namespace ymc;

// 解析一段源码并反解析，返回 unparse 文本；*err 报告是否有语法错误。
static std::string once(const std::string& src, bool* err = nullptr) {
  Interner in;
  Diagnostics d;
  Lexer lx(src, 0, in, d);
  std::vector<Token> toks = lx.tokenize();
  Absyn a(in);
  Parser p(toks, a, d);
  p.parse_program();
  if (err) *err = d.has_errors();
  return unparse(a);
}

// 两次 unparse 一致且无错误
static void check_roundtrip(const char* src) {
  bool err = false;
  std::string u1 = once(src, &err);
  YMC_CHECK(!err);
  std::string u2 = once(u1, &err);
  YMC_CHECK(!err);
  YMC_CHECK_EQ(u1, u2);
}

static const char* kModel =
    "model M \"doc\"\n"
    "  parameter Real k = 2.0 \"gain\";\n"
    "  Real x(start = 1.0, fixed = true);\n"
    "  Real y;\n"
    "equation\n"
    "  der(x) = -k * x + 1.0;\n"
    "  y = x ^ 2 + k;\n"
    "end M;\n";

// 词法：能切出 token，末尾是 Eof，无词法错误
YMC_TEST(lexer_basic) {
  Interner in;
  Diagnostics d;
  Lexer lx("model M Real x; equation x = 1.0; end M;", 0, in, d);
  std::vector<Token> toks = lx.tokenize();
  YMC_CHECK(!d.has_errors());
  YMC_CHECK(toks.size() > 5);
  YMC_CHECK(toks.back().kind == TokenKind::Eof);
  YMC_CHECK(toks[0].kind == TokenKind::Kw_model);
}

// 词法：UTF-8 BOM 被跳过且不计列（MLS §13.4 要求 UTF-8；Windows 工具常带 BOM）
YMC_TEST(lexer_skips_utf8_bom) {
  Interner in;
  Diagnostics d;
  Lexer lx("\xEF\xBB\xBFmodel M\nend M;\n", 0, in, d);
  std::vector<Token> toks = lx.tokenize();
  YMC_CHECK(!d.has_errors());
  YMC_CHECK(toks[0].kind == TokenKind::Kw_model);
  YMC_CHECK_EQ(toks[0].loc.line, 1u);
  YMC_CHECK_EQ(toks[0].loc.col, 1u);
  bool err = false;
  std::string u = once("\xEF\xBB\xBFmodel M\nend M;\n", &err);
  YMC_CHECK(!err);
  YMC_CHECK(u.find("model M") == 0);
}

// 解析：无错误，顶层 1 个类，名字为 M，2 个方程，3 个元素
YMC_TEST(parse_structure) {
  Interner in;
  Diagnostics d;
  Lexer lx(kModel, 0, in, d);
  std::vector<Token> toks = lx.tokenize();
  Absyn a(in);
  Parser p(toks, a, d);
  p.parse_program();
  YMC_CHECK(!d.has_errors());
  YMC_CHECK_EQ(a.top.size(), std::size_t{1});
  const ClassDef& c = a.classes[a.top[0]];
  YMC_CHECK_EQ(in.str(c.name), std::string_view("M"));
  YMC_CHECK(c.restriction == Restriction::Model);
  YMC_CHECK_EQ(c.equations.size(), std::size_t{2});
  YMC_CHECK_EQ(c.elements.size(), std::size_t{3});
}

YMC_TEST(roundtrip_idempotent) { check_roundtrip(kModel); }

// Real 字面量文本 = OMC realString（期望值取自 OMC 3rdParty/ryu/ryu/om_format.c 的 test_real 向量）
YMC_TEST(real_literal_text_matches_omc_ryu_to_hr) {
  YMC_CHECK_EQ(real_literal_text(8e5), std::string("8e5"));
  YMC_CHECK_EQ(real_literal_text(8e4), std::string("8e4"));
  YMC_CHECK_EQ(real_literal_text(8e3), std::string("8000.0"));
  YMC_CHECK_EQ(real_literal_text(8e0), std::string("8.0"));
  YMC_CHECK_EQ(real_literal_text(8e-3), std::string("0.008"));
  YMC_CHECK_EQ(real_literal_text(8e-4), std::string("8e-4"));
  YMC_CHECK_EQ(real_literal_text(8.13e5), std::string("813000.0"));
  YMC_CHECK_EQ(real_literal_text(8.13e6), std::string("8.13e6"));
  YMC_CHECK_EQ(real_literal_text(8.13e1), std::string("81.3"));
  YMC_CHECK_EQ(real_literal_text(8.13e-3), std::string("0.00813"));
  YMC_CHECK_EQ(real_literal_text(8.1234567e5), std::string("812345.67"));
  YMC_CHECK_EQ(real_literal_text(8.1234567e-2), std::string("0.081234567"));
  YMC_CHECK_EQ(real_literal_text(-1.2e1), std::string("-12.0"));
  YMC_CHECK_EQ(real_literal_text(-4.56e8), std::string("-4.56e8"));
  YMC_CHECK_EQ(real_literal_text(1e-60), std::string("1e-60"));
  YMC_CHECK_EQ(real_literal_text(1e80), std::string("1e80"));
  YMC_CHECK_EQ(real_literal_text(9.499999999999999e2), std::string("949.9999999999999"));
  YMC_CHECK_EQ(real_literal_text(1.0000000000000022e0), std::string("1.0000000000000022"));
  YMC_CHECK_EQ(real_literal_text(9.81), std::string("9.81"));
  YMC_CHECK_EQ(real_literal_text(0.0), std::string("0.0"));
  YMC_CHECK_EQ(real_literal_text(0.1), std::string("0.1"));
}

// 表达式文本逐字对齐 OMC 的 Absyn dump（AbsynDumpTpl.tpl）：源码括号保留（单元素元组，Modelica.g:1856-1861），
// 不按优先级增删括号；* / ^ 不留空格、+ - 留空格（:1049-1075）；Real 字面量与字符串保留源码拼写；
// elseif 平铺、`else if` 保持嵌套；(e)[i] 形式保留。期望值来自 omc 1.25.0 对同一源码的 list()（2026-09-16 探针）。
YMC_TEST(expression_text_matches_omc_absyn_dump) {
  std::string u1 = once("model E Real y; equation y = (a + b) * c - d / e; end E;");
  YMC_CHECK_EQ(u1, once(u1));
  YMC_CHECK(u1.find("y = (a + b)*c - d/e;") != std::string::npos);
  std::string v1 = once("model E Real y; equation y = -a + (b * c) ^ 2 .* x; end E;");
  YMC_CHECK(v1.find("y = -a + (b*c)^2.*x;") != std::string::npos);
  std::string w1 = once("model E Real y; equation y = if a then 1e3 elseif b then 0.347E-6 else (if c then 1 else 2); end E;");
  YMC_CHECK(w1.find("y = if a then 1e3 elseif b then 0.347E-6 else (if c then 1 else 2);") != std::string::npos);
  std::string x1 = once("model E Real y; equation y = if a then 1 else if b then 2 else 3; end E;");
  YMC_CHECK(x1.find("y = if a then 1 else if b then 2 else 3;") != std::string::npos);
  std::string s1 = once("model E String s = \"a\\\"b\\n\" \"c\\'d\"; equation y = ({1, 2})[1] + 1; z = not b and c; end E;");
  YMC_CHECK(s1.find("String s = \"a\\\"b\\n\" \"c\\'d\";") != std::string::npos);
  YMC_CHECK(s1.find("y = ({1, 2})[1] + 1;") != std::string::npos);
  YMC_CHECK(s1.find("z = not b and c;") != std::string::npos);
}

// connect、extends、import、包与嵌套类
YMC_TEST(roundtrip_more_constructs) {
  const char* src =
      "package P\n"
      "  import Modelica.SIunits.Voltage;\n"
      "  import SI = Modelica.SIunits;\n"
      "  import Modelica.Math.{sin, cos};\n"
      "  connector Pin\n"
      "    Real v;\n"
      "    flow Real i;\n"
      "  end Pin;\n"
      "  model C\n"
      "    extends P.Base(n = 3);\n"
      "    Pin p;\n"
      "  equation\n"
      "    connect(p, p);\n"
      "  end C;\n"
      "end P;\n";
  check_roundtrip(src);
  std::string u = once(src);
  YMC_CHECK(u.find("connect(p, p)") != std::string::npos);
  YMC_CHECK(u.find("import Modelica.Math.{sin, cos}") != std::string::npos);
}

// if 表达式、数组、范围、函数调用、命名实参
YMC_TEST(roundtrip_expressions) {
  const char* src =
      "model X\n"
      "  Real y;\n"
      "  Real a[3] = {1.0, 2.0, 3.0};\n"
      "  Real s = sum(a[i] for i in 1:3);\n"
      "  Real b[2] = {i * 2 for i in 1:2};\n"
      "equation\n"
      "  y = if x > 0 then sin(x) elseif x < -1 then 0 else -x;\n"
      "  y = f(a, k = 2, m = {1, 2});\n"
      "end X;\n";
  check_roundtrip(src);
  std::string u = once(src);
  YMC_CHECK(u.find("if x > 0 then sin(x) elseif x < -1 then 0 else -x") != std::string::npos);
  YMC_CHECK(u.find("f(a, k = 2, m = {1, 2})") != std::string::npos);
  YMC_CHECK(u.find("sum(a[i] for i in 1:3)") != std::string::npos);
}

// algorithm 段：赋值、元组赋值、if/for/while/when 语句、break/return、函数调用语句
YMC_TEST(roundtrip_algorithm) {
  const char* src =
      "function F\n"
      "  input Real x;\n"
      "  output Real y;\n"
      "protected\n"
      "  Integer n;\n"
      "algorithm\n"
      "  y := 0;\n"
      "  (y, n) := g(x);\n"
      "  for i in 1:3, j loop\n"
      "    y := y + i;\n"
      "  end for;\n"
      "  while y > 10 loop\n"
      "    y := y / 2;\n"
      "    if y < 1 then\n"
      "      break;\n"
      "    elseif y < 2 then\n"
      "      return;\n"
      "    else\n"
      "      assert(y > 0, \"neg\");\n"
      "    end if;\n"
      "  end while;\n"
      "  when x > 1 then\n"
      "    y := 1;\n"
      "  elsewhen x < 0 then\n"
      "    y := 2;\n"
      "  end when;\n"
      "end F;\n";
  check_roundtrip(src);
  std::string u = once(src);
  YMC_CHECK(u.find("(y, n) := g(x)") != std::string::npos);
  YMC_CHECK(u.find("end while") != std::string::npos);
  YMC_CHECK(u.find("elsewhen x < 0 then") != std::string::npos);
}

// if/for/when 方程
YMC_TEST(roundtrip_compound_equations) {
  const char* src =
      "model Q\n"
      "  Real x[3];\n"
      "  Boolean b;\n"
      "initial equation\n"
      "  x = zeros(3);\n"
      "equation\n"
      "  for i in 1:3 loop\n"
      "    der(x[i]) = -x[i];\n"
      "  end for;\n"
      "  if b then\n"
      "    y = 1;\n"
      "  elseif not b then\n"
      "    y = 2;\n"
      "  else\n"
      "    y = 3;\n"
      "  end if;\n"
      "  when b then\n"
      "    reinit(x[1], 0);\n"
      "  elsewhen not b then\n"
      "    z = pre(z) + 1;\n"
      "  end when;\n"
      "end Q;\n";
  check_roundtrip(src);
  std::string u = once(src);
  YMC_CHECK(u.find("initial equation") != std::string::npos);
  YMC_CHECK(u.find("elseif not b then") != std::string::npos);
  YMC_CHECK(u.find("end when") != std::string::npos);
}

// 矩阵、元组、枚举、短类定义、条件组件
YMC_TEST(roundtrip_matrix_enum_short) {
  const char* src =
      "package K\n"
      "  type Color = enumeration(Red \"r\", Green, Blue);\n"
      "  type Any = enumeration(:);\n"
      "  type Len = Real(unit = \"m\", min = 0) \"length\";\n"
      "  connector RealInput = input Real;\n"
      "  model M\n"
      "    parameter Boolean useX = true;\n"
      "    Real x if useX;\n"
      "    Real A[2, 2] = [1, 2; 3, 4];\n"
      "    Real e = A[end, 1];\n"
      "    Color c = Color.Red;\n"
      "  end M;\n"
      "end K;\n";
  check_roundtrip(src);
  std::string u = once(src);
  YMC_CHECK(u.find("enumeration(Red \"r\", Green, Blue)") != std::string::npos);
  YMC_CHECK(u.find("enumeration(:)") != std::string::npos);
  YMC_CHECK(u.find("[1, 2; 3, 4]") != std::string::npos);
  YMC_CHECK(u.find("Real x if useX") != std::string::npos);
  YMC_CHECK(u.find("connector RealInput = input Real") != std::string::npos);
}

// redeclare / replaceable / constrainedby、注解保留、class extends、external
YMC_TEST(roundtrip_redeclare_annotation_external) {
  const char* src =
      "package R\n"
      "  replaceable package Medium = Modelica.Media.Air constrainedby PartialMedium \"m\";\n"
      "  model Sys\n"
      "    extends Base(redeclare package M = N, redeclare each final Real x = 1);\n"
      "    replaceable Real y constrainedby Real(min = 0);\n"
      "    Real z annotation(Evaluate = true);\n"
      "    annotation(Icon(graphics = {Text(extent = {{-100, -100}, {100, 100}})}));\n"
      "  end Sys;\n"
      "  model extends Sys(x = 2)\n"
      "    Real w;\n"
      "  end Sys;\n"
      "  function f\n"
      "    input Real x;\n"
      "    output Real y;\n"
      "    external \"C\" y = foo(x) annotation(Library = \"m\");\n"
      "  end f;\n"
      "end R;\n";
  check_roundtrip(src);
  std::string u = once(src);
  YMC_CHECK(u.find("replaceable package Medium = Modelica.Media.Air constrainedby PartialMedium \"m\"") !=
            std::string::npos);
  YMC_CHECK(u.find("redeclare each final Real x = 1") != std::string::npos);
  YMC_CHECK(u.find("annotation(Evaluate = true)") != std::string::npos);
  YMC_CHECK(u.find("model extends Sys(x = 2)") != std::string::npos);
  YMC_CHECK(u.find("external \"C\" y = foo(x) annotation(Library = \"m\")") != std::string::npos);
}

// 约束子句只跟在 replaceable 后面：omc 1.25.0 实跑（探针 pk-p49a A68CC6 / CC22-CC24）前四条 Failed to parse file，后两条照收。
YMC_TEST(constrainedby_needs_replaceable) {
  bool err = false;
  once("model C M m(redeclare model T = X constrainedby X(x = 8)); end C;", &err);
  YMC_CHECK(err);
  once("model C extends N; redeclare model T = A constrainedby A; end C;", &err);
  YMC_CHECK(err);
  once("model C Real x constrainedby Real; end C;", &err);
  YMC_CHECK(err);
  once("model C model T = A constrainedby A; end C;", &err);
  YMC_CHECK(err);
  once("model C M m(redeclare replaceable model T = X constrainedby X(x = 8)); end C;", &err);
  YMC_CHECK(!err);
  once("model C replaceable model T = A extends A; T t; end C;", &err);
  YMC_CHECK(!err);
}

// ---- S1 验收 ③：语法错误的位置与 OMC 对齐（位置一致即可，措辞不要求）----
// 返回第一条 Error 的 (line, col)；无错误返回 (0, 0)。
static std::pair<unsigned, unsigned> first_error(const char* src, std::string* msg = nullptr) {
  Interner in;
  Diagnostics d;
  Lexer lx(src, 0, in, d);
  std::vector<Token> toks = lx.tokenize();
  Absyn a(in);
  Parser p(toks, a, d);
  p.parse_program();
  for (const Diagnostic& diag : d.items()) {
    if (diag.level == DiagLevel::Error) {
      if (msg) *msg = diag.msg;
      return {diag.loc.line, diag.loc.col};
    }
  }
  return {0u, 0u};
}

YMC_TEST(error_positions_match_omc) {
  using P = std::pair<unsigned, unsigned>;
  std::string msg;
  // Class3.mo：首尾类名不同 → 报在类头的名字。原件在第 8 行报 8:7；这串写在第 1 行，omc 1.25.0 实跑它本身报 1:6
  //（s1fix 探针 v01）——第 1 行的语法错整体左移一列，见 token.h omc_syntax_diag_loc。
  YMC_CHECK(first_error("model A\nend B;\n", &msg) == P(1, 6));
  YMC_CHECK(msg.find("start and end") != std::string::npos);
  // Class4.mo：`end;` 缺名字 → 报在 end（2:1）
  YMC_CHECK(first_error("model A\nend;\n") == P(2, 1));
  // Assign4.mo：方程段里的 := → 报在 :=（4:5）
  YMC_CHECK(first_error("model M\n  Real x;\nequation\n  x := 1;\nend M;\n") == P(4, 5));
  // Assign1.mo：算法段里的 = → 报在 =（3:5）
  YMC_CHECK(first_error("function F\nalgorithm\n  x = 3;\nend F;\n") == P(3, 5));
  // Assign3.mo：元组赋值右侧不是函数调用 → 报在语句起点（3:3）
  YMC_CHECK(first_error("function F\nalgorithm\n  (x, y) := r;\nend F;\n") == P(3, 3));
  // EmptyArray.mo：{} → 报在 {（2:15）
  YMC_CHECK(first_error("model M\n  Real r[:] = {};\nend M;\n") == P(2, 15));
  // ImportFullyQualified.mo：import 名以 . 开头 → 报在 .（2:10）
  YMC_CHECK(first_error("model M\n  import .P;\nend M;\n") == P(2, 10));
  // Declaration1.mo：两个变量性前缀 → 报在第二个（2:12）
  YMC_CHECK(first_error("model M\n  discrete constant Real x;\nend M;\n") == P(2, 12));
  // ParseError2.mo / Identifier.mo：无前缀元素、第一个组件名缺失 → 报在元素起点（2:3）
  YMC_CHECK(first_error("model M\n  Real 1x;\nend M;\n") == P(2, 3));
  YMC_CHECK(first_error("model M\n  Real model;\nend M;\n") == P(2, 3));
  // ConstructParameters1.mo：带前缀 → 报在出错记号（2:18）
  YMC_CHECK(first_error("model M\n  parameter Real (p3, p4) = f(p1);\nend M;\n") == P(2, 18));
  // SimpleIntegrator4.mo：修饰名用了关键字 initial → 报在 initial（2:10）
  YMC_CHECK(first_error("model M\n  Real x(initial = 2);\nend M;\n") == P(2, 10));
  // ParseError3.mo：方程段里的单独表达式不是函数调用 → 报在表达式起点（3:3）
  YMC_CHECK(first_error("model M\nequation\n  assert;\nend M;\n") == P(3, 3));
  // IfElseIf.mo：嵌套 else if 未闭合 → 报在类的 end（9:1），带提示
  YMC_CHECK(first_error("model M\n  Real x;\nequation\n  if b then\n    x = 1;\n  else if c then\n"
                        "    x = 2;\n  end if;\nend M;\n", &msg) == P(9, 1));
  YMC_CHECK(msg.find("nested 'else if'") != std::string::npos);
  // MissingSemicolon.mo：顶层类缺 ';' → 在 EOF 报错（列 0）
  YMC_CHECK(first_error("model M\n  Real x;\nend M\n").second == 0u);
  // 正常文件无错误
  YMC_CHECK(first_error("model M\n  Real x;\nend M;\n") == P(0, 0));
  // ArrayModification1.mo（12:7）/ ArrayModification2.mo（13:13）：修饰名后跟下标 → 报在修饰名上（Modelica.g:908-922
  // v1.25.0）。源码 = 两个 testsuite 文件到 end 为止的原文；期望值 = 文件的 Result，且 omc 1.25.0 实跑一致
  //（loadFile 两个原文件，2026-11-05）。
  const char* kArrMod1 =
      "// name:     ArrayModification1\n// keywords: array, modification\n// status:   incorrect\n//\n"
      "// Subscripted modifiers are not allowed.\n//\n\nclass ArrayModification1\n  class A\n    Real x[3];\n"
      "  end A;\n  A a(x[2] = 1.0);\n  annotation(__OpenModelica_commandLineOptions=\"-d=-newInst\");\n"
      "end ArrayModification1;\n";
  YMC_CHECK(first_error(kArrMod1, &msg) == P(12, 7));
  YMC_CHECK(msg.find("Subscripting modifiers is not allowed") != std::string::npos);
  const char* kArrMod2 =
      "// name:     ArrayModification2\n// keywords: array, modification\n// status:   incorrect\n//\n"
      "// Subscripted modifiers are not allowed.\n//\n\nclass ArrayModification2\n  class A\n    Real x[3];\n"
      "  end A;\n\n  extends A(x[2] = 1.0);\n  annotation(__OpenModelica_commandLineOptions=\"-d=-newInst\");\n"
      "end ArrayModification2;\n";
  YMC_CHECK(first_error(kArrMod2) == P(13, 13));
}

// 下标修饰的兄弟形态（Modelica.g:908-922 v1.25.0 element_modification：name_path2 后紧跟 LBRACK 即报，位置从修饰名首记号
// 到 '[' 后记号的 0 起列；报完置 lexerError，整份解析就此终止、不再报别的）。期望值全部来自 omc 1.25.0 实跑：
// `omc pNN.mo`（s1fix 探针 p01-p35，2026-11-05），探针文件 = kSubModHead + 被测行 + "end M;\n"，与下面的字符串逐字相同。
static const char* kSubModHead =
    "model M\n  model A\n    Real x[3]; Real y;\n  end A;\n  model B\n    replaceable A a;\n  end B;\n";

static std::vector<Diagnostic> errors_of(const std::string& src) {
  Interner in;
  Diagnostics d;
  Lexer lx(src, 0, in, d);
  std::vector<Token> toks = lx.tokenize();
  Absyn a(in);
  Parser p(toks, a, d);
  p.parse_program();
  std::vector<Diagnostic> out;
  for (const Diagnostic& diag : d.items())
    if (diag.level == DiagLevel::Error) out.push_back(diag);
  return out;
}

YMC_TEST(subscripted_modifier_matches_omc) {
  struct Case {
    const char* line;     // 第 8 行
    unsigned col, end;    // omc 报 [8:col-8:end]
  };
  const Case cases[] = {
      {"  A a(x[2] = 1.0);\n", 7, 8},                                // p01 组件修饰
      {"  A a(each x[2] = 1.0);\n", 12, 13},                         // p02 each 不算进起点
      {"  A a(final x[2] = 1.0);\n", 13, 14},                        // p03 final 同上
      {"  A a(each final x[2] = 1.0);\n", 18, 19},                   // p04
      {"  B b(a(x[1] = 1));\n", 9, 10},                              // p05 嵌套修饰
      {"  extends A(x[2] = 1.0);\n", 13, 14},                        // p06 extends 修饰
      {"  model A2 = A(x[1] = 1.0);\n", 16, 17},                     // p07 短类修饰
      {"  B b(redeclare A a(x[1] = 1.0));\n", 21, 22},               // p08 redeclare 组件里
      {"  B b(redeclare model C = A(x[1] = 1.0));\n", 29, 30},       // p09 redeclare 短类里
      {"  annotation(x[1] = 1);\n", 14, 15},                         // p10 类注解
      {"  Real z annotation(x[1] = 1);\n", 21, 22},                  // p11 组件注解
      {"  A a(x[1, 2] = 1.0);\n", 7, 8},                             // p12 多维
      {"  B b(a.x[1] = 1.0);\n", 7, 10},                             // p13 带点名：起点在首段
      {"  A a(x[:] = {1, 2, 3});\n", 7, 8},                          // p14 冒号下标
      {"  A a(x  [  2] = 1.0);\n", 7, 12},                           // p15 空白：终点跟 '[' 后记号走
      {"  A a(x[2](start = 1));\n", 7, 8},                           // p17 下标后再跟子修饰
      {"  A a(y = 1, x[2] = 1.0);\n", 14, 15},                       // p18 第二个实参
      {"  B b(a[1].x = 1.0);\n", 7, 8},                              // p21 首段带下标
      {"  replaceable A a constrainedby A(x[1] = 1.0);\n", 35, 36},  // p22 约束子句
      {"  B b(replaceable A a(x[1] = 1.0));\n", 23, 24},             // p23 修饰里的 replaceable
      {"  A a(x[2]);\n", 7, 8},                                      // p24 无绑定
      {"  A a('x'[2] = 1.0);\n", 7, 10},                             // p25 带引号名
      {"  annotation(Icon(graphics[1] = 2));\n", 19, 27},            // p28 注解里嵌套
      {"  B b(redeclare each A a(x[1] = 1.0));\n", 26, 27},          // p29 redeclare each
      {"  A a(x[2] = 1.0 \"c\");\n", 7, 8},                          // p31 带字符串注释
      {"  A a(x[1] = 1.0, x[2] = 2.0);\n", 7, 8},                    // p27 两个：omc 只报第一个
      {"  A a(x[2] = 1.0);\n  Real z = ;\n", 7, 8},                  // p19 后面的语法错不报
      {"  A a(x[2] = 1.0);\n  Real z = (1, 2)[1];\n", 7, 8},         // p33 后面的元组下标也不报
  };
  for (const Case& c : cases) {
    std::vector<Diagnostic> errs = errors_of(std::string(kSubModHead) + c.line + "end M;\n");
    YMC_CHECK_EQ(errs.size(), std::size_t{1});   // 失败即抛出，下面可以放心取 errs[0]
    YMC_CHECK_EQ(errs[0].loc.line, 8u);
    YMC_CHECK_EQ(errs[0].loc.col, c.col);
    YMC_CHECK_EQ(errs[0].loc.col + errs[0].loc.len, c.end);
    YMC_CHECK(errs[0].msg.find("Subscripting modifiers is not allowed. Apply the modification on the whole "
                               "identifier using an array-expression or an each-modifier.") != std::string::npos);
  }
  using P = std::pair<unsigned, unsigned>;
  const std::string head(kSubModHead);
  // p16：'[' 后换行，omc 报 8:7-9:6——ymc 的诊断终点只能在同一行，起点一致
  YMC_CHECK(first_error((head + "  A a(x[\n      2] = 1.0);\nend M;\n").c_str()) == P(8, 7));
  // p20：前面先有语法错，omc 报的是那一条（8:12）
  YMC_CHECK(first_error((head + "  Real z = ;\n  A a(x[2] = 1.0);\nend M;\n").c_str()) == P(8, 12));
  // p32：元组下标在前（不终止解析），两条都报：8:12 元组、9:7 下标修饰
  {
    std::vector<Diagnostic> errs = errors_of(head + "  Real z = (1, 2)[1];\n  A a(x[2] = 1.0);\nend M;\n");
    YMC_CHECK_EQ(errs.size(), std::size_t{2});
    YMC_CHECK(P(errs[0].loc.line, errs[0].loc.col) == P(8, 12));
    YMC_CHECK(P(errs[1].loc.line, errs[1].loc.col) == P(9, 7));
  }
  // p34：class extends 的修饰（10:31）；p35：顶层短类（4:13）
  YMC_CHECK(first_error("model M\n  model A\n    Real x[3];\n  end A;\n  model P\n    replaceable model C = A;\n"
                        "  end P;\n  model Q\n    extends P;\n    redeclare model extends C(x[1] = 1.0)\n    end C;\n"
                        "  end Q;\nend M;\n") == P(10, 31));
  YMC_CHECK(first_error("model A\n  Real x[3];\nend A;\nmodel M = A(x[2] = 1.0);\n") == P(4, 13));
}

// 任务卡 T-S3-08：下标只能跟在括号式后面（Modelica.g:1800-1806 v1.25.0 只有 `LPAR … RPAR array_subscripts?` 这一支带
// 下标，且只一段）。位置 = omc 1.25.0 实跑（PS 探针 ut，2026-10-05）。
YMC_TEST(subscript_only_after_parenthesized_expression) {
  using P = std::pair<unsigned, unsigned>;
  std::string msg;
  // `{…}[i]`、`f(x)[i]`、`[…][i]`、`(e)[i][j]` 的第二段：omc 在那个 `[` 处报 "Missing token: SEMICOLON"
  //（E1 2:18、E2 3:16、E3 2:24、E5 2:25）。
  YMC_CHECK(first_error("model E1\n  Real y = {1, 2}[2];\nend E1;\n") == P(2, 18));
  YMC_CHECK(first_error("model E2\n  function f input Real u; output Real y[2]; algorithm y := {u, u}; end f;\n"
                        "  Real y = f(1)[1];\nend E2;\n") == P(3, 16));
  YMC_CHECK(first_error("model E3\n  Real y = [1, 2; 3, 4][1, 2];\nend E3;\n") == P(2, 24));
  YMC_CHECK(first_error("model E5\n  Real y = ({{1, 2}})[1][2];\nend E5;\n") == P(2, 25));
  // 元组后跟下标：omc 报 "Tuple expression can not be subscripted."，从 '(' 起（E4 2:12）。
  YMC_CHECK(first_error("model E4\n  Real y = (1, 2)[1];\nend E4;\n", &msg) == P(2, 12));
  YMC_CHECK(msg.find("Tuple expression can not be subscripted") != std::string::npos);
  // 括号式后的一段下标合法；`f(i).y[1]` 的下标属于 cref，照收。
  YMC_CHECK(first_error("model M\n  Real y = ({1, 2})[2];\n  Real z = (fill(1, 2))[1];\n  Real w = f(1).y[1];\nend M;\n") ==
            P(0, 0));
}

// 任务卡 T-S3-09：带引号标识符的字符集（Q-CHAR，BaseModelica_Lexer.g:430-436 v1.25.0；MLS 3.6 §2.3.1 只有 ASCII）。
// 位置 = omc 1.25.0 实跑（PS 探针 ut，2026-10-05）：坏字符报在它的下一列，`''` 报在第二个引号。
YMC_TEST(lexer_quoted_identifier_charset) {
  using P = std::pair<unsigned, unsigned>;
  YMC_CHECK(first_error("model Q1\n  Real 'h\xc3\xb6" "he' = 1;\nend Q1;\n") == P(2, 11));   // 非 ASCII
  YMC_CHECK(first_error("model Q2\n  Real '' = 1;\nend Q2;\n") == P(2, 9));                 // 空
  YMC_CHECK(first_error("model Q3\n  Real 'a`b' = 1;\nend Q3;\n") == P(2, 11));             // 反引号
  YMC_CHECK(first_error("model Q4\n  Real 'a\tb' = 1;\nend Q4;\n") == P(2, 11));            // 制表符
  // 合法：空格和 ASCII 标点；非法转义 `\q` omc 只告警、照收（M5）；字符串、注释里可以有非 ASCII。
  YMC_CHECK(first_error("model M5\n  Real 'a b!#' = 1 \"h\xc3\xb6" "he\"; // h\xc3\xb6" "he\n  Real 'a\\qb' = 2;\nend M5;\n") ==
            P(0, 0));
  // 坏字符紧跟开引号（QIDENT 的必选元素）→ 报在坏字符本身，不是下一列。omc 1.25.0 实跑（s1fix 探针 s01-s07 / t01-t08，
  // 2026-11-05；JBouncingBall.mo 5:8 同理）。
  YMC_CHECK(first_error("model Q\n  Real '\xe8\xb7\xb3' = 1;\nend Q;\n") == P(2, 9));      // s01 非 ASCII
  YMC_CHECK(first_error("model Q\n  Real '`b' = 1;\nend Q;\n") == P(2, 9));               // s02 反引号
  YMC_CHECK(first_error("model Q\n  Real '\tb' = 1;\nend Q;\n") == P(2, 9));              // s03 制表符
  YMC_CHECK(first_error("model Q\n  Real '\nb' = 1;\nend Q;\n") == P(2, 9));              // s07 换行：仍在第 2 行
  YMC_CHECK(first_error("model Q\n  Real 'a\xe8\xb7\xb3' = 1;\nend Q;\n") == P(2, 11));   // s04 第二个字符起：下一列
  YMC_CHECK(first_error("model Q\n  Real '\\\\`' = 1;\nend Q;\n") == P(2, 12));           // s05 转义算第一个元素
  YMC_CHECK(first_error("// c\nmodel '\xe8\xb7\xb3'\nend '\xe8\xb7\xb3';\n") == P(2, 8));  // t01
  // 第 1 行再整体左移一列（token.h omc_syntax_diag_loc）
  YMC_CHECK(first_error("model '\xe8\xb7\xb3'\nend '\xe8\xb7\xb3';\n") == P(1, 7));         // s06
  YMC_CHECK(first_error("model Q Real '\xe8\xb7\xb3' = 1; end Q;\n") == P(1, 14));          // t02
  YMC_CHECK(first_error("model Q Real 'a`' = 1; end Q;\n") == P(1, 16));                    // t03
  YMC_CHECK(first_error("model Q Real '' = 1; end Q;\n") == P(1, 14));                      // t08
}

// 第 1 行的语法层诊断整体左移一列（antlr3inputstream.c:222 列号从 -1 起；语义位置 omc 在 PARSER_INFO 补过，
// 语法错没补）。期望值 = omc 1.25.0 实跑这些字符串本身（s1fix 探针 u01-u08 / t06 / t07 / v03，2026-11-05）。
YMC_TEST(omc_line1_syntax_column_shift) {
  using P = std::pair<unsigned, unsigned>;
  YMC_CHECK(first_error("model Q Real x = ; end Q;\n") == P(1, 17));                       // u01：';' 在第 18 列
  YMC_CHECK(first_error("model Q\nReal x = ; end Q;\n") == P(2, 10));                      // u02：第 2 行不移
  YMC_CHECK(first_error("model Q end R;\n") == P(1, 6));                                   // u06
  YMC_CHECK(first_error("model Q Real x; equation x := 1; end Q;\n") == P(1, 27));         // u07
  YMC_CHECK(first_error("modl Q end Q;\n") == P(1, 0));                                    // u08：行首记号报成第 0 列
  YMC_CHECK(first_error("model Q Real x = 1 ? 2; end Q;\n") == P(1, 19));                  // t06：词法错同样
  YMC_CHECK(first_error("model Q\n Real x = 1 ? 2; end Q;\n") == P(2, 13));                // t07
  YMC_CHECK(first_error("model M Real r[:] = {}; end M;\n") == P(1, 20));                  // v03
  // u03：下标修饰在第 1 行报 1:38-1:39（终点跟着一起移）
  std::vector<Diagnostic> errs = errors_of("model Q model A Real x[2]; end A; A a(x[2] = 1); end Q;\n");
  YMC_CHECK_EQ(errs.size(), std::size_t{1});
  YMC_CHECK(P(errs[0].loc.line, errs[0].loc.col) == P(1, 38));
  YMC_CHECK_EQ(errs[0].loc.col + errs[0].loc.len, 39u);
}

// 部分函数应用只许整条出现在函数实参 / 数组构造的元素里（Modelica.g:1527-1536、1983、2007、2019 v1.25.0），
// 修饰里 redeclare / replaceable 的组件类型不许带维（Modelica.g:956-958、768）。期望值 = omc 1.25.0 实跑
//（s1fix 探针 q01-q16 / r01-r08，探针文件 = kPfHead + 被测行 + "end M;\n"，逐字相同；InvalidPartialFunction.mo 5:18、
// RedeclareComponentInvalidDims1.mo 14:24 是 testsuite 里的同类）。
static const char* kPfHead =
    "model M\n  function f\n    input Real x; output Real y;\n  algorithm\n    y := x;\n  end f;\n"
    "  model A\n    Real x[3];\n  end A;\n  model B\n    replaceable A a;\n  end B;\n";

YMC_TEST(partial_application_and_redeclare_dims_match_omc) {
  using P = std::pair<unsigned, unsigned>;
  const std::string head(kPfHead);
  auto fe = [&](const char* body) { return first_error((head + body + "end M;\n").c_str()); };
  YMC_CHECK(fe("  Real z = function f(x = 1);\n") == P(13, 12));                       // q01 绑定
  YMC_CHECK(fe("  Real z(start = function f(x = 1));\n") == P(13, 18));                // q02 修饰值
  YMC_CHECK(fe("  Real z = g(1 + function f(x = 1));\n") == P(13, 18));                // q06 实参里嵌着
  YMC_CHECK(fe("  Real z;\nequation\n  z = function f(x = 1);\n") == P(15, 7));        // q07 方程
  YMC_CHECK(fe("  Real z[1, 1] = [function f(x = 1)];\n") == P(13, 19));               // q08 矩阵
  YMC_CHECK(fe("  Real z = function f;\n") == P(13, 22));                              // q11 不成形：报里面
  YMC_CHECK(fe("  Real z = if true then function f(x = 1) else 2;\n") == P(13, 25));   // q12 if 分支
  YMC_CHECK(fe("  Real z = (function f(x = 1));\n") == P(13, 13));                     // q14 括号
  YMC_CHECK(fe("  Real z;\nalgorithm\n  z := function f(x = 1);\n") == P(15, 8));      // q15 赋值
  // 照收：位置实参、命名实参、数组构造元素、归约、语句里的实参（omc 解析通过，后面的错是语义的）
  YMC_CHECK(fe("  Real z = g(function f(x = 1));\n") == P(0, 0));                      // q03
  YMC_CHECK(fe("  Real z = g(h = function f(x = 1));\n") == P(0, 0));                  // q04
  YMC_CHECK(fe("  Real z[1] = {function f(x = 1)};\n") == P(0, 0));                    // q05
  YMC_CHECK(fe("  Real z = g(function f(x = 1) for i in 1:2);\n") == P(0, 0));         // q13
  YMC_CHECK(fe("  Real z;\nalgorithm\n  z := g(function f(x = 1));\n") == P(0, 0));    // q16
  // q10：部分函数应用的错不终止 omc 的解析，后面的下标修饰照报，两条
  std::vector<Diagnostic> errs = errors_of(head + "  Real z = function f(x = 1);\n  A a(x[1] = 1);\nend M;\n");
  YMC_CHECK_EQ(errs.size(), std::size_t{2});
  YMC_CHECK(P(errs[0].loc.line, errs[0].loc.col) == P(13, 12));
  YMC_CHECK(P(errs[1].loc.line, errs[1].loc.col) == P(14, 7));
  // redeclare / replaceable 修饰里类型带维 → 报在 '['
  YMC_CHECK(fe("  B b(redeclare A[2] a);\n") == P(13, 18));                            // r01
  YMC_CHECK(fe("  B b(replaceable A[2] a);\n") == P(13, 20));                          // r02
  YMC_CHECK(fe("  B b(redeclare final A[2] a);\n") == P(13, 24));                      // r03
  YMC_CHECK(fe("  B b(redeclare each A[2] a);\n") == P(13, 23));                       // r04
  YMC_CHECK(fe("  B b(redeclare parameter A[2] a);\n") == P(13, 28));                  // r07
  YMC_CHECK(fe("  B b(redeclare M.A[2] a);\n") == P(13, 20));                          // r08
  YMC_CHECK(fe("  B b(redeclare A a[2]);\n") == P(0, 0));                              // r05 维在组件名后
  YMC_CHECK(fe("  replaceable A[2] a2;\n") == P(0, 0));                                // r06 类体里可以
}

// 对抗评审第二轮（s1fix）：每一行的期望值 = omc 1.25.0 实跑这串源码（`omc <名>.mo`，2026-11-10）报的全部语法错的起点，
// 按报出的先后；空表示 omc 语法层通过（之后只有语义错或干脆通过）。大写名是评审 cases/ 里的原文，小写名是紧凑版。
// 断言行由草稿区 mk_tests.py 从 omc 的输出机械生成（不是照 ymc 抄的）。覆盖：未闭合引号名 / 字符串 / 块注释报在 EOF
// （U* / L1x* / o*）、指数缺数字（LX* / x05-x08）、引号名里的非法转义（Q03 / Q06 / y*）、实数上溢与下溢（Q11 / T10 /
// t12 / x01 / x02）、外部调用实参与函数名（e*，Modelica.g:484-489）、部分函数应用只收命名实参（p*）、修饰名前导点
// （S19 / m*）、omc 的 lexerError / 异常终止决定第二条起报不报（t*，见 parser.h omc_lexer_error_ / omc_aborted_）。
static std::string err_positions(const char* name, const std::string& src) {
  std::string s = std::string(name) + ":";
  for (const Diagnostic& d : errors_of(src)) s += " " + std::to_string(d.loc.line) + ":" + std::to_string(d.loc.col);
  return s;
}

YMC_TEST(adversarial_review_round2_matches_omc) {
  YMC_CHECK_EQ(err_positions("U01", "model U01 Real 'abc"), std::string("U01: 1:19"));
  YMC_CHECK_EQ(err_positions("U02", "\nmodel U02 Real 'abc"), std::string("U02: 2:20"));
  YMC_CHECK_EQ(err_positions("U03", "model U03 Real '"), std::string("U03: 1:16"));
  YMC_CHECK_EQ(err_positions("U04", "model U04 String s = \""), std::string("U04: 1:22"));
  YMC_CHECK_EQ(err_positions("U05", "model U05 Real x; /*"), std::string("U05: 1:20"));
  YMC_CHECK_EQ(err_positions("U06", "model U06 String s = \"abc"), std::string("U06: 1:25"));
  YMC_CHECK_EQ(err_positions("U10", "\nmodel U10 String s = \""), std::string("U10: 2:23"));
  YMC_CHECK_EQ(err_positions("U11", "\nmodel U11 Real '"), std::string("U11: 2:17"));
  YMC_CHECK_EQ(err_positions("L1x05", "model L1x05 String s = \"abc; end L1x05;\n"), std::string("L1x05: 2:1"));
  YMC_CHECK_EQ(err_positions("L1x06", "model L1x06 Real x = 1; /* abc end L1x06;\n"), std::string("L1x06: 2:1"));
  YMC_CHECK_EQ(err_positions("LX11", "model LX11 Real x = 1e; end LX11;\n"), std::string("LX11: 1:22"));
  YMC_CHECK_EQ(err_positions("LX15", "model LX15 Real x = 1.5e+; end LX15;\n"), std::string("LX15: 1:25"));
  YMC_CHECK_EQ(err_positions("LX21", "\nmodel LX21 Real x = 1e; end LX21;\n"), std::string("LX21: 2:23"));
  YMC_CHECK_EQ(err_positions("LX25", "\nmodel LX25 Real x = 1.5e+; end LX25;\n"), std::string("LX25: 2:26"));
  YMC_CHECK_EQ(err_positions("Q03", "\nmodel Q03\n  Real 'a\\`b' = 1;\nend Q03;\n"), std::string("Q03: 3:12"));
  YMC_CHECK_EQ(err_positions("Q06", "\nmodel Q06\n  Real '\\`' = 1;\nend Q06;\n"), std::string("Q06: 3:11"));
  YMC_CHECK_EQ(err_positions("Q11", "\nmodel Q11\n  Real x = 1e999;\nend Q11;\n"), std::string("Q11: 3:12"));
  YMC_CHECK_EQ(err_positions("T10", "\nmodel T10\n  Real r = 1e999;\n  Real q = ;\nend T10;\n"), std::string("T10: 3:12"));
  YMC_CHECK_EQ(err_positions("S19", "\nmodel A\n  Real x[2];\n  Real y;\nend A;\nmodel S19\n  A a(.y = 1);\nend S19;\n"), std::string("S19: 7:7"));
  YMC_CHECK_EQ(err_positions("e01", "model M\n  function e\n    input Real x;\n    output Real y;\n  external \"C\" y = sin(function f0());\n  end e;\n  function f0 input Real x; output Real y = 2 * x; end f0;\n  Real y = e(time);\nend M;\n"), std::string("e01: 5:24"));
  YMC_CHECK_EQ(err_positions("e02", "model M\n  function e\n    input Real x;\n    output Real y;\n  external \"C\" y = sin(x = x);\n  end e;\n  function f0 input Real x; output Real y = 2 * x; end f0;\n  Real y = e(time);\nend M;\n"), std::string("e02: 5:26"));
  YMC_CHECK_EQ(err_positions("e03", "model M\n  function e\n    input Real x;\n    output Real y;\n  external \"C\" y = Mod.sin(x);\n  end e;\n  function f0 input Real x; output Real y = 2 * x; end f0;\n  Real y = e(time);\nend M;\n"), std::string("e03: 5:23"));
  YMC_CHECK_EQ(err_positions("e04", "model M\n  function e\n    input Real x;\n    output Real y;\n  external \"C\" y = sin(x for i in 1:2);\n  end e;\n  function f0 input Real x; output Real y = 2 * x; end f0;\n  Real y = e(time);\nend M;\n"), std::string("e04: 5:26"));
  YMC_CHECK_EQ(err_positions("e05", "model M\n  function e\n    input Real x;\n    output Real y;\n  external \"C\" y = sin(x);\n  end e;\n  function f0 input Real x; output Real y = 2 * x; end f0;\n  Real y = e(time);\nend M;\n"), std::string("e05:"));
  YMC_CHECK_EQ(err_positions("e06", "model M\n  function e\n    input Real x;\n    output Real y;\n  external \"C\" Mod.sin(x);\n  end e;\n  function f0 input Real x; output Real y = 2 * x; end f0;\n  Real y = e(time);\nend M;\n"), std::string("e06: 5:23"));
  YMC_CHECK_EQ(err_positions("e07", "model M\n  function e\n    input Real x;\n    output Real y;\n  external \"C\" der(x);\n  end e;\n  function f0 input Real x; output Real y = 2 * x; end f0;\n  Real y = e(time);\nend M;\n"), std::string("e07: 5:16"));
  YMC_CHECK_EQ(err_positions("e08", "model M\n  function e\n    input Real x;\n    output Real y;\n  external \"C\" foo();\n  end e;\n  function f0 input Real x; output Real y = 2 * x; end f0;\n  Real y = e(time);\nend M;\n"), std::string("e08:"));
  YMC_CHECK_EQ(err_positions("e09", "model M\n  function e\n    input Real x;\n    output Real y;\n  external \"C\" y sin(x);\n  end e;\n  function f0 input Real x; output Real y = 2 * x; end f0;\n  Real y = e(time);\nend M;\n"), std::string("e09: 5:18"));
  YMC_CHECK_EQ(err_positions("p07", "model M\n  function f input Real a; output Real y = a; end f;\n  Real y = g(function f(2), time);\nend M;\n"), std::string("p07: 3:25"));
  YMC_CHECK_EQ(err_positions("p12", "model M\n  function f input Real a; output Real y = a; end f;\n  Real y = g(function f(a = 2, 3), time);\nend M;\n"), std::string("p12: 3:32"));
  YMC_CHECK_EQ(err_positions("p14", "model M\n  function f input Real a; output Real y = a; end f;\n  Real y = g(function f(a = 2).y, time);\nend M;\n"), std::string("p14: 3:31"));
  YMC_CHECK_EQ(err_positions("p15", "model M\n  function f input Real a; output Real y = a; end f;\n  Real y = g(function f(a = function f(a = 1)), time);\nend M;\n"), std::string("p15:"));
  YMC_CHECK_EQ(err_positions("p16", "model M\n  function f input Real a; output Real y = a; end f;\n  Real y = g(function f(a), time);\nend M;\n"), std::string("p16: 3:26"));
  YMC_CHECK_EQ(err_positions("p17", "model M\n  function f input Real a; output Real y = a; end f;\n  Real y = g(function f(a = 1, b), time);\nend M;\n"), std::string("p17: 3:33"));
  YMC_CHECK_EQ(err_positions("m01", "model M\n  model A Real x[2]; Real y; end A;\n  A a(each .y = 1);\nend M;\n"), std::string("m01: 3:12"));
  YMC_CHECK_EQ(err_positions("m02", "model M\n  Real y annotation(.x = 1);\nend M;\n"), std::string("m02: 2:21"));
  YMC_CHECK_EQ(err_positions("t01", "model M\n  model A Real x[2]; Real y; end A;\n  function f0 input Real x; output Real y = 2 * x; end f0;\n  A a(x[, y = 1);\n  Real p = g(1 + function f0(), 1);\n  Real q = function f0();\nend M;\n"), std::string("t01: 4:7"));
  YMC_CHECK_EQ(err_positions("t02", "model M\n  model A Real x[2]; Real y; end A;\n  A a(x[);\n  Real r;\nequation\n  (r, r)[1] = 1;\nend M;\n"), std::string("t02: 3:7 6:3"));
  YMC_CHECK_EQ(err_positions("t03", "model M\n  model A Real x[2]; Real y; end A;\n  A a(x[, y = 1);\n  Real r = {};\nend M;\n"), std::string("t03: 3:7"));
  YMC_CHECK_EQ(err_positions("t04", "model M\n  model A Real x[2]; Real y; end A;\n  function f0 input Real x; output Real y = 2 * x; end f0;\n  A a(x[);\n  Real p = function f0();\nend M;\n"), std::string("t04: 4:7 5:12"));
  YMC_CHECK_EQ(err_positions("t05", "model M\n  model A Real x[2]; Real y; end A;\n  A a(x[, y[1] = 2);\nend M;\n"), std::string("t05: 3:7 3:11"));
  YMC_CHECK_EQ(err_positions("t06", "model M\n  model A Real x[2]; Real y; end A;\n  A a(x[);\n  Real z = 1e999;\nend M;\n"), std::string("t06: 3:7 4:12"));
  YMC_CHECK_EQ(err_positions("t07", "model M\n  function f0 input Real x; output Real y = 2 * x; end f0;\n  Real p = function f0();\n  Real q = ;\nend M;\n"), std::string("t07: 3:12"));
  YMC_CHECK_EQ(err_positions("t08", "model M\n  Real r;\n  Real q = ;\nequation\n  (r, r)[1] = 1;\nend M;\n"), std::string("t08: 3:12"));
  YMC_CHECK_EQ(err_positions("t09", "model M\n  Real r;\nequation\n  (r, r)[1] = 1;\n  r = ;\nend M;\n"), std::string("t09: 4:3"));
  YMC_CHECK_EQ(err_positions("t11", "model M\n  Real z = (1, 2)[1];\n  Real w = (3, 4)[2];\nend M;\n"), std::string("t11: 2:12 3:12"));
  YMC_CHECK_EQ(err_positions("t12", "model M\n  Real z = 1e999;\n  Real w = 2e999;\nend M;\n"), std::string("t12: 2:12 3:12"));
  YMC_CHECK_EQ(err_positions("x01", "model M\n  Real x = 1e-999;\nend M;\n"), std::string("x01:"));
  YMC_CHECK_EQ(err_positions("x02", "model M\n  Real x = 1e-310;\nend M;\n"), std::string("x02:"));
  YMC_CHECK_EQ(err_positions("x05", "model M\n  Real x = 2E;\nend M;\n"), std::string("x05: 2:14"));
  YMC_CHECK_EQ(err_positions("x06", "model M\n  Real x = 2.e-;\nend M;\n"), std::string("x06: 2:16"));
  YMC_CHECK_EQ(err_positions("x08", "model M\n  Real x = .5e;\nend M;\n"), std::string("x08: 2:15"));
  YMC_CHECK_EQ(err_positions("y01", "model M\n  Real 'a\\nb' = 1;\nend M;\n"), std::string("y01:"));
  YMC_CHECK_EQ(err_positions("y03", "model M\n  Real 'a\\qb' = 1;\nend M;\n"), std::string("y03:"));
  YMC_CHECK_EQ(err_positions("y05", "model M\n  Real 'a\\\tb' = 1;\nend M;\n"), std::string("y05: 2:12"));
  YMC_CHECK_EQ(err_positions("y06", "model M\n  Real 'a\\"), std::string("y06: 2:11"));
  YMC_CHECK_EQ(err_positions("o01", "model M\n  String s = \"abc\ndef"), std::string("o01: 3:4"));
  YMC_CHECK_EQ(err_positions("o02", "model M\n  Real x; /* abc\n def"), std::string("o02: 3:5"));
  YMC_CHECK_EQ(err_positions("o03", "model M\n  Real 'ab\nc"), std::string("o03: 3:1"));
}

YMC_TEST_MAIN
