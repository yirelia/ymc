// pre 模块 FunctionAlias（NBFunctionAlias.main → functionAliasDefault，Modules/2_Pre/NBFunctionAlias.mo:68-337 v1.25.0）：把不能内联的
// 函数调用提成辅助变量 `$FUN_n` 和一条方程 `$FUN_n = 调用`，同一个调用只提一次（NB 里管公共子式的模块）。
//
// 只改标量方程：when、if 方程和算法段整条不动（introduceFunctionAliasEquation :356-370）。变量上的绑定也不动。
#pragma once
#include "bdae/bdae.h"

namespace ymc::bdae {

// interner 要写：`$FUN_n`、`$RES_AUX_n`、属性名 fixed 是新符号。
[[nodiscard]] ModuleResult function_alias(BackendDAE& dae, Interner& in);

}  // namespace ymc::bdae
