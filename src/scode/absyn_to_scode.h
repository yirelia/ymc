// Absyn → SCode 翻译（方案 §5 S2；对应 OMC FrontEnd/AbsynToSCode.mo）。
#pragma once
#include <vector>

#include "absyn/absyn.h"
#include "scode/scode.h"

namespace ymc {

// 翻译一组顶层类（AbsynToSCode.translateAbsyn2SCode :73）；结果追加到 out.program。
void translate_program(SCode& out, const std::vector<Id<ClassDef>>& top);

// 翻译单个类为 SCode.CLASS 元素；prefixes 由调用方给（顶层类：PUBLIC、非 redeclare、final 取类自身、
// 无 inner/outer、非 replaceable——AbsynToSCode.mo:154-159）。
Id<SElement> translate_class(SCode& out, Id<ClassDef> cls, const SPrefixes& prefixes);

}  // namespace ymc
