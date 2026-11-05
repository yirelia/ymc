// SimplifyExp.simplify（NFSimplifyExp.mo:78-133 v1.25.0）的标量部分。
//
// 这里的旗标是 NB 路径的：NF_SCALARIZE、NF_EXPAND_OPERATIONS、NF_EXPAND_FUNC_ARGS 全关（NFInst.mo:323-334）。
// 输入是 lower 放行的表达式：默认路径的 SimplifyModel 已经化简过一遍（ADR-008 §6），又经过 combineBinaries。所以这一遍真正干活的是 Multary
// 这一支：合并常数、把负项挪到逆项，见 .cpp。
#pragma once
#include <string>

#include "base/symbol.h"
#include "inst/expr.h"

namespace ymc::bdae {

// 不改已有节点，只往 arena 里加新节点（同 combine_binaries，节点可能被共用）。遇到没移植的形状时把原因写进 unsupported 并返回入参，调用方退 66。
// interner 要写：字符串字面量相加折成新串（Ceval.evalBinaryAdd 的 STRING 支）。
[[nodiscard]] Id<Expr> simplify(ExprArena& a, Interner& in, Id<Expr> e, std::string& unsupported);

}  // namespace ymc::bdae
