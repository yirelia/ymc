// Absyn 反解析（unparse）——方案 §5 S1 产出；对应 OMC FrontEnd/Dump.mo。
//
// 把 Absyn 打回 Modelica 源码文本。表达式按运算符优先级补最少括号，使
// parse→unparse→parse→unparse 幂等（S1 验收 ② 的 round-trip）。
#pragma once
#include <string>

#include "absyn/absyn.h"

namespace ymc {

[[nodiscard]] std::string unparse(const Absyn& a);

// 单个表达式 / 字符串字面量（带引号与转义）的文本；S2 的 SCode dump 复用同一优先级/括号规则。
[[nodiscard]] std::string unparse_exp(const Absyn& a, Id<Exp> e);
// Real 字面量文本（与 OMC realString 相同的最短往返表示；规则见 unparse.cpp real_text）。
[[nodiscard]] std::string real_literal_text(double v);
[[nodiscard]] std::string unparse_string_lit(const Absyn& a, Symbol s);

}  // namespace ymc
