// combineBinaries（NFSimplifyExp.mo:1481-1798 v1.25.0）：把嵌套的二元和 / 积合成 Multary。
//
// NB 路径在 NFInst.instClassInProgram 末尾做这一步（:246-250），ymc 的 FlatModel 走的默认路径没有做，lower 时补上。它不是化简，只改树形，
// 但打印和后续模块都看得见：`a + b + c` 变成 `b + a + c`（前缀反序，见 .cpp）。
//
// 两个入口的差别不是实现细节，同一个式子结果不同（census §3 #1、#2）：
//   - combine_binaries：式子整体调一次。方程走这条（Equation.mapExp 直接 func(eq.lhs)，NFEquation.mo:716-722）。
//   - combine_binaries_deep：自底向上每个节点调一次（Expression.map，NFExpression.mo:2651-2818）。绑定和类型属性走这条
//     （Variable.mapExp → Binding.mapExp，NFVariable.mo:490-501、NFBinding.mo:573-586）。
// 两个函数都是纯的：不改已有节点（Expr 节点可能被多处共用），只往 arena 里加新节点。
#pragma once
#include "inst/expr.h"

namespace ymc::bdae {

[[nodiscard]] Id<Expr> combine_binaries(ExprArena& a, Id<Expr> e);
[[nodiscard]] Id<Expr> combine_binaries_deep(ExprArena& a, Id<Expr> e);

}  // namespace ymc::bdae
