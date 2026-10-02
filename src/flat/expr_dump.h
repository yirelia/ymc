// 类型化表达式 → 文本（S3 压平打印）——对齐 OMC Template/ExpressionDumpTpl.tpl + Expression.shouldParenthesize。
//
// 与 S1/S2 的 unparse 不同（ADR-006 D5、研究底稿 R13 §4）：unparse 保留源码括号（单元素 Tuple）；这里的
// `Expr` 是无源码括号的类型化树，靠 `Expression.mo:10757-10946` 的**优先级表**决定加不加括号。
// 运算符间距：`+ -` 两侧空格、`* / ^` 无空格、一元负紧贴、`not` 后一空格；CAST 打 `/*Real*/(exp)`；
// Real 复用 `real_literal_text`（S2 已按 ryu_to_hr 实现）。
#pragma once
#include <cstdint>
#include <string>
#include <unordered_set>

#include "base/arena.h"
#include "base/symbol.h"
#include "inst/expr.h"

namespace ymc {

// 打印一个类型化表达式（顶层上下文，不加外围括号）。interner 用于取名字/字符串内容。
// ew / product：打点号的数组二元运算节点、向量 / 矩阵积节点（见 FlatModel::ew_binaries / product_binaries；可空）。
[[nodiscard]] std::string dump_expr(const ExprArena& a, const Interner& in, Id<Expr> e,
                                    const std::unordered_set<std::uint32_t>* ew = nullptr,
                                    const std::unordered_set<std::uint32_t>* product = nullptr);

}  // namespace ymc
