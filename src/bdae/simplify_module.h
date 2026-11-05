// pre 模块 Simplify（NBackendDAE.simplify，Classes/NBackendDAE.mo:386-444 v1.25.0）。模块表里排两次（:272 "Simplify 1"、:274 "Simplify 2"），
// 两次都是 `simplify(init = false)`，同一个函数、同一组参数；两次的差别只在输入（第二次吃 Alias 之后的方程）。
//
// 做三件事：
//   1. 所有方程（eqData.equations：simulation、初值、removed 都在里面）各化简一遍：式子过 SimplifyExp.simplify，再看方程结构
//      （Equation.simplify NBEquation.mo:1652-1767）：两边相等的标量方程换成 `0 = 0`，when / if 去掉字面量条件的分支、if 并成一条
//      `lhs = if … then … else …`，算法再过一遍 SimplifyModel.simplifyAlgorithm；
//   2. 整条消失的方程（DUMMY）从各桶删掉（EqData.compress）；
//   3. 整条消失的 when 里赋值的变量改成参数（:418-437）。
#pragma once
#include "bdae/bdae.h"

namespace ymc::bdae {

[[nodiscard]] ModuleResult simplify_module(BackendDAE& dae, Interner& in);

}  // namespace ymc::bdae
