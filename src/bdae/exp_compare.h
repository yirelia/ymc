// Expression.compare（NFExpression.mo:426-748 v1.25.0）。两处用：lower 的 sortForSplit 按它排 if 方程各分支的方程，
// Simplify 的 Expression.isEqual（:417-424，就是 compare == 0）按它判两个式子相等。
//
// 只收 lower 放行的那些节点（expr_ok）加 Multary；别的节点、数组这里都到不了。
#pragma once
#include <vector>

#include "base/symbol.h"
#include "inst/expr.h"

namespace ymc::bdae {

class ExpCompare {
 public:
  ExpCompare(const ExprArena& a, const Interner& in) : a_(a), in_(in) {}

  // 负 / 0 / 正，同 Util.intCompare。
  [[nodiscard]] int operator()(Id<Expr> x, Id<Expr> y) const;

 private:
  [[nodiscard]] int pair_compare(const Expr& p, const Expr& q) const;
  [[nodiscard]] int list_compare(const std::vector<Id<Expr>>& x, const std::vector<Id<Expr>>& y) const;
  [[nodiscard]] int cref_compare(const ExprCref& x, const ExprCref& y) const;
  [[nodiscard]] int path_compare(const std::vector<Symbol>& x, const std::vector<Symbol>& y) const;

  const ExprArena& a_;
  const Interner& in_;
};

}  // namespace ymc::bdae
