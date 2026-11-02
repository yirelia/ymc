// FlatModel → BackendDAE（NBackendDAE.lower，NBackendDAE.mo:228-243 v1.25.0）。
//
// 现在做：标量等式、结果不依赖哈希序的 when（S5a-1b，判据见 lower.cpp when_equation）、assert。其余构造一律退出，不猜：多赋值 when / if /
// algorithm / 顶层无返回值调用 / for / Clock / 外部对象 / 元组 / 记录 / 数组。契约同 flatten：`unsupported` 置位时 dae 不可用，driver 退 66。
#pragma once
#include <string>

#include "bdae/bdae.h"

namespace ymc::bdae {

struct LowerResult {
  BackendDAE dae;
  bool unsupported = false;
  std::string unsupported_reason;
};

// 取走 model 的 ExprArena（Id<Expr> 原样有效）；之后 model 不能再用。interner 要写：`$RES_SIM_n` 等名字是新符号。
[[nodiscard]] LowerResult lower(FlatModel&& model, Interner& in);

}  // namespace ymc::bdae
