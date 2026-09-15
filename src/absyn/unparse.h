// Absyn 反解析（unparse）——方案 §5 S1 产出；对应 OMC FrontEnd/Dump.mo。
//
// 把 Absyn 打回 Modelica 源码文本。表达式按运算符优先级补最少括号，使
// parse→unparse→parse→unparse 幂等（S1 验收 ② 的 round-trip）。
#pragma once
#include <string>

#include "absyn/absyn.h"

namespace ymc {

[[nodiscard]] std::string unparse(const Absyn& a);

}  // namespace ymc
