// S1 词法/语法/unparse 的验收测试。round-trip 幂等是方案 §5 S1 验收 ②。
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

// 解析：无错误，顶层 1 个类，名字为 M，2 个方程
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
  YMC_CHECK_EQ(c.elements.size(), std::size_t{3});  // k, x, y
}

// round-trip 幂等：parse→unparse→parse→unparse 两次输出一致
YMC_TEST(roundtrip_idempotent) {
  bool err = false;
  std::string u1 = once(kModel, &err);
  YMC_CHECK(!err);
  std::string u2 = once(u1, &err);
  YMC_CHECK(!err);
  YMC_CHECK_EQ(u1, u2);
}

// 运算符优先级：补最少括号且幂等
YMC_TEST(precedence_parens) {
  std::string u1 = once("model E Real y; equation y = (a + b) * c - d / e; end E;");
  std::string u2 = once(u1);
  YMC_CHECK_EQ(u1, u2);
  YMC_CHECK(u1.find("(a + b) * c") != std::string::npos);
  // a + b * c 不应加括号
  std::string v1 = once("model E Real y; equation y = a + b * c; end E;");
  YMC_CHECK(v1.find("a + b * c") != std::string::npos);
}

// connect、extends、import、包与嵌套类
YMC_TEST(roundtrip_more_constructs) {
  const char* src =
      "package P\n"
      "  import Modelica.SIunits.Voltage;\n"
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
  bool err = false;
  std::string u1 = once(src, &err);
  YMC_CHECK(!err);
  std::string u2 = once(u1, &err);
  YMC_CHECK(!err);
  YMC_CHECK_EQ(u1, u2);
  YMC_CHECK(u1.find("connect(p, p)") != std::string::npos);
  YMC_CHECK(u1.find("flow Real i") != std::string::npos);
}

// if 表达式、数组、范围、函数调用
YMC_TEST(roundtrip_expressions) {
  const char* src =
      "model X\n"
      "  Real y;\n"
      "  Real a[3] = {1.0, 2.0, 3.0};\n"
      "equation\n"
      "  y = if x > 0 then sin(x) else -x;\n"
      "end X;\n";
  bool err = false;
  std::string u1 = once(src, &err);
  YMC_CHECK(!err);
  std::string u2 = once(u1, &err);
  YMC_CHECK_EQ(u1, u2);
  YMC_CHECK(u1.find("if x > 0 then sin(x) else -x") != std::string::npos);
}

YMC_TEST_MAIN
