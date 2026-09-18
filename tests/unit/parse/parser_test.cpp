// S1 词法/语法/unparse 的验收测试（v2）。round-trip 幂等是方案 §5 S1 验收 ②。
#include "ymc_test.h"

#include "absyn/absyn.h"
#include "absyn/unparse.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "parse/lexer.h"
#include "parse/parser.h"

#include <string>

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
  // Class3.mo：首尾类名不同 → 报在类头的名字（1:7）
  YMC_CHECK(first_error("model A\nend B;\n", &msg) == P(1, 7));
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
}

YMC_TEST_MAIN
