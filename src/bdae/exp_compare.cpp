#include "bdae/exp_compare.h"

#include <string_view>

namespace ymc::bdae {

namespace {

// Util.intCompare / realCompare / boolCompare（Util/Util.mo:757、:786、:794）。
template <class T>
int three_way(T a, T b) { return a == b ? 0 : (a > b ? 1 : -1); }
// stringCompare（SimulationRuntime/c/util/modelica_string.c:359-369）：strcmp 归一到 -1 / 0 / 1。
int str_compare(std::string_view a, std::string_view b) { return three_way(a.compare(b), 0); }

// valueConstructor：record 在 uniontype 里的序（NFExpression.mo:83-256）。只比大小，用不到的 record 不占名字，序号照原文跳开。
int rank(ExprKind k) {
  switch (k) {
    case ExprKind::IntLit: return 0;
    case ExprKind::RealLit: return 1;
    case ExprKind::StringLit: return 2;
    case ExprKind::BoolLit: return 3;
    case ExprKind::EnumLit: return 4;
    case ExprKind::Cref: return 6;
    case ExprKind::Call: return 13;
    case ExprKind::Binary: return 16;
    case ExprKind::Unary: return 17;
    case ExprKind::Logical: return 18;
    case ExprKind::LogicalNot: return 19;
    case ExprKind::Relation: return 20;
    case ExprKind::Multary: return 21;
    case ExprKind::IfExp: return 22;
    case ExprKind::Cast: return 23;
    default: YMC_UNREACHABLE("ExpCompare: expression kind rejected by lower");
  }
}
int op_rank(BinOp op) { return static_cast<int>(op); }   // ADD..POW（NFOperator.mo:45-49），同 BinOp 序

}  // namespace

int ExpCompare::operator()(Id<Expr> x, Id<Expr> y) const {
  if (x == y) return 0;                  // referenceEq
  const Expr& p = a_[x];
  const Expr& q = a_[y];
  if (const int c = three_way(rank(p.kind), rank(q.kind))) return c;
  switch (p.kind) {
    case ExprKind::IntLit: return three_way(p.ival, q.ival);
    case ExprKind::RealLit: return three_way(p.rval, q.rval);
    case ExprKind::StringLit: return str_compare(in_.str(p.sval), in_.str(q.sval));
    case ExprKind::BoolLit: return three_way(p.bval, q.bval);
    case ExprKind::EnumLit: {            // 先比类型名（字面量全名去掉末段），再比序数
      const std::vector<Symbol> t1(p.enum_path.begin(), p.enum_path.end() - 1), t2(q.enum_path.begin(), q.enum_path.end() - 1);
      if (const int c = path_compare(t1, t2)) return c;
      return three_way(p.enum_index, q.enum_index);
    }
    case ExprKind::Cref: return cref_compare(p.cref, q.cref);
    case ExprKind::Call:                 // Call.compare NFCall.mo:541-555；迭代量只有推导式有，这里没有
      if (const int c = path_compare(p.call_name, q.call_name)) return c;
      return list_compare(p.args, q.args);
    case ExprKind::Multary:
      if (const int c = three_way(op_rank(p.bin), op_rank(q.bin))) return c;
      if (const int c = list_compare(p.args, q.args)) return c;
      return list_compare(p.inv_args, q.inv_args);
    case ExprKind::Binary:
      if (const int c = three_way(op_rank(p.bin), op_rank(q.bin))) return c;
      return pair_compare(p, q);
    case ExprKind::Unary:
      return (*this)(p.a, q.a);          // 只有 UMINUS，运算符相同
    case ExprKind::Logical:
      if (const int c = three_way(static_cast<int>(p.log), static_cast<int>(q.log))) return c;   // AND(27) < OR(28)，同 LogOp 序
      return pair_compare(p, q);
    case ExprKind::LogicalNot:
      return (*this)(p.a, q.a);
    case ExprKind::Relation:
      if (const int c = three_way(static_cast<int>(p.rel), static_cast<int>(q.rel))) return c;   // LESS..NEQUAL（NFOperator.mo:80-85），同 RelOp 序
      return pair_compare(p, q);
    case ExprKind::IfExp:
      if (const int c = pair_compare(p, q)) return c;
      return (*this)(p.c, q.c);
    case ExprKind::Cast:
      return (*this)(p.a, q.a);
    default:
      YMC_UNREACHABLE("ExpCompare: expression kind rejected by lower");
  }
}

int ExpCompare::pair_compare(const Expr& p, const Expr& q) const {
  if (const int c = (*this)(p.a, q.a)) return c;
  return (*this)(p.b, q.b);
}
// List.compare（List.mo:307-340）：先比长度。
int ExpCompare::list_compare(const std::vector<Id<Expr>>& x, const std::vector<Id<Expr>>& y) const {
  if (x.size() != y.size()) return x.size() < y.size() ? -1 : 1;
  for (std::size_t i = 0; i < x.size(); ++i)
    if (const int c = (*this)(x[i], y[i])) return c;
  return 0;
}
// ComponentRef.compare（NFComponentRef.mo:1100-1125）：从末段往前比，短的小。下标只有数组有，lower 挡住了。
int ExpCompare::cref_compare(const ExprCref& x, const ExprCref& y) const {
  std::size_t i = x.parts.size(), j = y.parts.size();
  while (i > 0 && j > 0)
    if (const int c = str_compare(in_.str(x.parts[--i].name), in_.str(y.parts[--j].name))) return c;
  return i == 0 && j == 0 ? 0 : (j == 0 ? 1 : -1);
}
// AbsynUtil.pathCompare（FrontEnd/AbsynUtil.mo:1266-1288）：限定名比标识符大，不看内容。
int ExpCompare::path_compare(const std::vector<Symbol>& x, const std::vector<Symbol>& y) const {
  for (std::size_t i = 0;; ++i) {
    const bool qx = i + 1 < x.size(), qy = i + 1 < y.size();
    if (!qx && !qy) return str_compare(in_.str(x[i]), in_.str(y[i]));
    if (!qx || !qy) return qx ? 1 : -1;
    if (const int c = str_compare(in_.str(x[i]), in_.str(y[i]))) return c;
  }
}

}  // namespace ymc::bdae
