// 压平打印器测试：手搭 FlatModel / 类型化 Expr，逐字对照 OMC DAE-dump 版式（研究底稿 R13）。
// 括号用例（expr_paren_matches_omc_priority_table）逐条对应 R13 §4.2 的优先级表 + 结合性规则。
#include "ymc_test.h"

#include <string>

#include "base/symbol.h"
#include "flat/expr_dump.h"
#include "flat/flat.h"
#include "flat/flat_dump.h"
#include "inst/expr.h"

using namespace ymc;

namespace {

// —— 表达式搭建小工具（在给定 ExprArena 里造节点）——
struct EB {
  ExprArena& a;
  Interner& in;
  Id<Expr> intlit(std::int64_t v) { Expr e; e.kind = ExprKind::IntLit; e.ival = v; e.ty = Type::integer(); return a.add(std::move(e)); }
  Id<Expr> reallit(double v) { Expr e; e.kind = ExprKind::RealLit; e.rval = v; e.ty = Type::real(); return a.add(std::move(e)); }
  Id<Expr> boollit(bool v) { Expr e; e.kind = ExprKind::BoolLit; e.bval = v; e.ty = Type::boolean(); return a.add(std::move(e)); }
  Id<Expr> cref(const char* name) {
    Expr e; e.kind = ExprKind::Cref; e.ty = Type::real();
    ExprCrefPart p; p.name = in.intern(name); e.cref.parts.push_back(std::move(p));
    return a.add(std::move(e));
  }
  Id<Expr> bin(BinOp op, Id<Expr> x, Id<Expr> y) { Expr e; e.kind = ExprKind::Binary; e.bin = op; e.a = x; e.b = y; e.ty = Type::real(); return a.add(std::move(e)); }
  Id<Expr> neg(Id<Expr> x) { Expr e; e.kind = ExprKind::Unary; e.un = UnOp::Minus; e.a = x; e.ty = Type::real(); return a.add(std::move(e)); }
  Id<Expr> rel(RelOp op, Id<Expr> x, Id<Expr> y) { Expr e; e.kind = ExprKind::Relation; e.rel = op; e.a = x; e.b = y; e.ty = Type::boolean(); return a.add(std::move(e)); }
  Id<Expr> logic(LogOp op, Id<Expr> x, Id<Expr> y) { Expr e; e.kind = ExprKind::Logical; e.log = op; e.a = x; e.b = y; e.ty = Type::boolean(); return a.add(std::move(e)); }
  Id<Expr> lnot(Id<Expr> x) { Expr e; e.kind = ExprKind::LogicalNot; e.a = x; e.ty = Type::boolean(); return a.add(std::move(e)); }
  Id<Expr> ifexp(Id<Expr> c, Id<Expr> t, Id<Expr> f) { Expr e; e.kind = ExprKind::IfExp; e.a = c; e.b = t; e.c = f; e.ty = Type::real(); return a.add(std::move(e)); }
  Id<Expr> cast(TypeKind k, Id<Expr> x) { Expr e; e.kind = ExprKind::Cast; e.a = x; e.ty = Type::of(k); return a.add(std::move(e)); }
};

std::string D(ExprArena& a, Interner& in, Id<Expr> e) { return dump_expr(a, in, e); }

}  // namespace

// R13 §4.1 运算符间距 + §4.2 优先级/结合性括号规则，逐条对拍（期望值取自 R13 实测）。
YMC_TEST(expr_paren_matches_omc_priority_table) {
  Interner in; ExprArena a; EB b{a, in};
  Id<Expr> A = b.cref("a"), B = b.cref("b"), C = b.cref("c");
  // 间距：全部二元算符两侧空格（OMC ExpressionDump.mo:112-116）
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Add, A, B)), std::string("a + b"));
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Mul, A, B)), std::string("a * b"));
  // a + b * c（乘法右操作数不加括号）
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Add, A, b.bin(BinOp::Mul, B, C))), std::string("a + b * c"));
  // (a + b) * c（加法作左操作数要括号）
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Mul, b.bin(BinOp::Add, A, B), C)), std::string("(a + b) * c"));
  // a*(b/c) → a * b / c（除法右操作数优先级 2 < 乘法右 3）
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Mul, A, b.bin(BinOp::Div, B, C))), std::string("a * b / c"));
  // a / (b * c) 保留（乘法右 3 > 除法右 2）
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Div, A, b.bin(BinOp::Mul, B, C))), std::string("a / (b * c)"));
  // (a ^ b) ^ c 与 a ^ (b ^ c) 两边都保留
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Pow, b.bin(BinOp::Pow, A, B), C)), std::string("(a ^ b) ^ c"));
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Pow, A, b.bin(BinOp::Pow, B, C))), std::string("a ^ (b ^ c)"));
  // 一元负：(-a) ^ 2.0、a * (-b)、-(a*b) → -a * b、-(a+b) 保留
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Pow, b.neg(A), b.reallit(2.0))), std::string("(-a) ^ 2.0"));
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Mul, A, b.neg(B))), std::string("a * (-b)"));
  YMC_CHECK_EQ(D(a, in, b.neg(b.bin(BinOp::Mul, A, B))), std::string("-a * b"));
  YMC_CHECK_EQ(D(a, in, b.neg(b.bin(BinOp::Add, A, B))), std::string("-(a + b)"));
  // 负 Real 字面量作右操作数加括号；负 Integer 字面量不加（R13 §4.2）
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Mul, A, b.reallit(-2.0))), std::string("a * (-2.0)"));
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Mul, A, b.intlit(-1))), std::string("a * -1"));
  // 逻辑/关系：not a < b、p and q、not (p and q)、p and q and r
  Id<Expr> P = b.cref("p"), Q = b.cref("q"), R = b.cref("r");
  YMC_CHECK_EQ(D(a, in, b.lnot(b.rel(RelOp::Lt, A, B))), std::string("not a < b"));
  YMC_CHECK_EQ(D(a, in, b.lnot(b.logic(LogOp::And, P, Q))), std::string("not (p and q)"));
  YMC_CHECK_EQ(D(a, in, b.logic(LogOp::And, b.logic(LogOp::And, P, Q), R)), std::string("p and q and r"));
  // if 表达式 + 作操作数加括号
  YMC_CHECK_EQ(D(a, in, b.ifexp(P, A, B)), std::string("if p then a else b"));
  YMC_CHECK_EQ(D(a, in, b.bin(BinOp::Add, b.ifexp(P, A, B), C)), std::string("(if p then a else b) + c"));
  // CAST：/*Real*/(i + 1)（内层整体一层括号）
  YMC_CHECK_EQ(D(a, in, b.cast(TypeKind::Real, b.bin(BinOp::Add, b.cref("i"), b.intlit(1)))),
               std::string("/*Real*/(i + 1)"));
}

// R13 §2/§3：类头尾、变量行前缀顺序、属性、绑定、注释。
YMC_TEST(flat_variable_line_format) {
  Interner in;
  FlatModel m;
  m.name = {in.intern("M")};
  EB b{m.exprs, in};
  // Real x;
  { FlatVariable v; v.name = {in.intern("x")}; v.ty = Type::real(); m.variables.push_back(std::move(v)); }
  // Real y = a + b "comment";
  { FlatVariable v; v.name = {in.intern("y")}; v.ty = Type::real();
    v.binding = b.bin(BinOp::Add, b.cref("a"), b.cref("b")); v.comment = in.intern("comment");
    m.variables.push_back(std::move(v)); }
  // parameter Real p(start = 1.0, fixed = true) = 2.0;
  { FlatVariable v; v.name = {in.intern("p")}; v.ty = Type::real(); v.var_kind = FlatVarKind::Parameter;
    v.attributes.push_back({in.intern("start"), b.reallit(1.0)});
    v.attributes.push_back({in.intern("fixed"), b.boollit(true)});
    v.binding = b.reallit(2.0);
    m.variables.push_back(std::move(v)); }
  // protected final parameter Boolean fb = true;
  { FlatVariable v; v.name = {in.intern("fb")}; v.ty = Type::boolean(); v.is_protected = true;
    v.is_final = true; v.var_kind = FlatVarKind::Parameter; v.binding = b.boollit(true);
    m.variables.push_back(std::move(v)); }

  std::string t = dump_flat(m, in);
  YMC_CHECK_EQ(t, std::string(
      "class M\n"
      "  Real x;\n"
      "  Real y = a + b \"comment\";\n"
      "  parameter Real p(start = 1.0, fixed = true) = 2.0;\n"
      "  protected final parameter Boolean fb = true;\n"
      "end M;\n"));
}

// R13 §2 段序 + §6 方程版式：initial equation 在 equation 前，普通等式，assert error 省第三参。
YMC_TEST(flat_sections_and_equations) {
  Interner in;
  FlatModel m;
  m.name = {in.intern("Sys")};
  EB b{m.exprs, in};
  { FlatVariable v; v.name = {in.intern("x")}; v.ty = Type::real(); m.variables.push_back(std::move(v)); }
  { FlatVariable v; v.name = {in.intern("z")}; v.ty = Type::real(); m.variables.push_back(std::move(v)); }
  // initial equation  z = 0.0;
  { FlatEquation e; e.kind = FlatEqKind::Equality; e.a = b.cref("z"); e.b = b.reallit(0.0);
    m.initial_equations.push_back(m.add_eq(std::move(e))); }
  // equation  x = z + 1.0;  assert(x > 0.0, "msg");
  { FlatEquation e; e.kind = FlatEqKind::Equality; e.a = b.cref("x");
    e.b = b.bin(BinOp::Add, b.cref("z"), b.reallit(1.0)); m.equations.push_back(m.add_eq(std::move(e))); }
  { FlatEquation e; e.kind = FlatEqKind::Assert; e.a = b.rel(RelOp::Gt, b.cref("x"), b.reallit(0.0));
    Expr s; s.kind = ExprKind::StringLit; s.sval = in.intern("msg"); s.ty = Type::string();
    e.b = m.exprs.add(std::move(s));
    // level = AssertionLevel.error → 省略
    Expr lvl; lvl.kind = ExprKind::EnumLit; lvl.enum_path = {in.intern("AssertionLevel"), in.intern("error")};
    e.c = m.exprs.add(std::move(lvl));
    m.equations.push_back(m.add_eq(std::move(e))); }

  std::string t = dump_flat(m, in);
  YMC_CHECK_EQ(t, std::string(
      "class Sys\n"
      "  Real x;\n"
      "  Real z;\n"
      "initial equation\n"
      "  z = 0.0;\n"
      "equation\n"
      "  x = z + 1.0;\n"
      "  assert(x > 0.0, \"msg\");\n"
      "end Sys;\n"));
}

YMC_TEST_MAIN
