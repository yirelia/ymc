// pre 模块 Early Inline（NBInline.main，Modules/2_Pre/NBInline.mo:76-106 v1.25.0；NBackendDAE.mo:256、:271 给它的内联类型是
// NORM / BUILTIN_EARLY / EARLY / DEFAULT，init = false）。两件事：
//   1. 体恰是一条赋值的用户函数，调用换成代过实参的右边（NBReplacements.replaceFunctions :268-341）；
//   2. 不论有没有函数，simulation 和初值里的 if 方程全部重造一遍：换新名字、挪到桶尾（inlineRecordsTuplesArrays NBInline.mo:264-289）。
#pragma once
#include "bdae/bdae.h"

namespace ymc::bdae {

// interner 要写：重造的 if 方程起新名字 `$RES_SIM_n`。
[[nodiscard]] ModuleResult early_inline(BackendDAE& dae, Interner& in);

}  // namespace ymc::bdae
