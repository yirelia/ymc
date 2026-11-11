// pre 模块 Alias（NBAlias.main → aliasDefault，Modules/2_Pre/NBAlias.mo:113-284 v1.25.0），在 Simplify 1 之后、Simplify 2 之前（NBackendDAE.mo:273）。
//
// 做五件事（aliasDefault 的 STEPS，:210-216）：
//   1. 在 simulation 方程里找"简单方程"（两边都只有常数、变量的加减和常数倍，最多两个未知量，:458-722），删掉它们，按共用的变量连成
//      别名集合；一个集合可以带一条"常量绑定"（只含一个未知量的简单方程）；
//   2. 没有常量绑定的集合挑一个变量留下（chooseVariableToKeep :891-926），其余的要被它表示；
//   3. 每个集合因果化（Causalize.simple：匹配 + Tarjan），逐个解出替换式（Replacements.simple NBReplacements.mo:99-141）；
//   4. 替换式代进剩下的方程和变量绑定（Replacements.applySimple :143-185），出现在 pre() 里的变量不换、补一条方程（checkReplacements :286-316）；
//   5. 被换掉的变量分三类：替换式是常量 / 参数 → 改成参数；是另一变量（或它的负 / not）或 time → 进 Alias 桶；别的 → 不打印，绑定方程进 removed。
#pragma once
#include "bdae/bdae.h"

namespace ymc::bdae {

[[nodiscard]] ModuleResult alias(BackendDAE& dae, Interner& in);

}  // namespace ymc::bdae
