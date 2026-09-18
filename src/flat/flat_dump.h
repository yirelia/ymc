// FlatModel → 文本（默认 `ymc X.mo` 的输出）——对齐 OMC DAEDumpTpl.tpl（研究底稿 R13 §2/§3/§6）。
//
// 段序：函数（v1 无）→ `class <全名>` → 变量 → initial equation → initial algorithm → equation → algorithm
// → `end <全名>;`。变量行字段顺序、属性顺序、方程版式见 R13；表达式走 flat/expr_dump。
#pragma once
#include <string>

#include "base/symbol.h"
#include "flat/flat.h"

namespace ymc {

// 打印整个压平模型，末尾一个 `\n`（对齐 `omc X.mo`）。
[[nodiscard]] std::string dump_flat(const FlatModel& m, const Interner& in);

}  // namespace ymc
