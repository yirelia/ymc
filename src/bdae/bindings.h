// pre 模块 Bindings（NBBindings.main，Modules/2_Pre/NBBindings.mo:50-143 v1.25.0）：给带绑定的未知量造方程 `x = 绑定`。
//
// lower 把绑定留在变量上（census §3 #6），这里才下沉成方程。变量上的绑定不清掉，打印不变，只有 EqData 多了方程。
#pragma once
#include "bdae/bdae.h"

namespace ymc::bdae {

// interner 要写：`$RES_BND_n` 是新符号。
[[nodiscard]] ModuleResult bindings(BackendDAE& dae, Interner& in);

}  // namespace ymc::bdae
