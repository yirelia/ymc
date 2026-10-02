// 压平：实例树 → FlatModel（S3）——对标 OMC NFFrontEnd/NFFlatten.mo（+ instExpressions 的表达式实例化）。
//
// 本文件合并两趟职责（R11 §1、§3.4）：
//   - **instExpressions**（`inst_exp`）：把组件绑定的 Absyn 表达式转成类型化 `Expr`，解析 cref、做标量定型 +
//     整型→实型字面量折叠。
//   - **flatten**（`flatten_class`）：带前缀深度优先遍历 FlatTree，叶组件产出 `FlatVariable`（点分名 = 前缀 + 组件名），
//     复合组件递归（前缀加一段），cref 按前缀改写。
//
// v1 第一里程碑（ADR-006 D6）：标量变量 + 绑定（字面量/cref/二元/一元/if/内建调用）+ 单继承 + 复合组件。
// **未做**：方程/算法段、数组、函数、属性修饰、短类——碰到就置 `unsupported`，driver 据此退出 66（记 SKIP）。
#pragma once
#include "base/symbol.h"
#include "flat/flat.h"
#include "inst/inst.h"

namespace ymc {

struct FlattenResult {
  FlatModel model;
  bool unsupported = false;          // 碰到 v1 未实现的构造（方程/数组/…）
  std::string unsupported_reason;    // 供 driver 打 `ymc: not implemented: <reason>`
  std::string unsupported_detail;    // 可选的细目（用户函数调用卡在哪一步），driver 接在后面打 ` [<detail>]`
};

// 压平一个已实例化的根节点（instantiate_root 的返回值）。
[[nodiscard]] FlattenResult flatten(Inst& inst, Interner& in, NodeId root);

}  // namespace ymc
