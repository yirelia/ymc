// SCode 文本 dump（方案 §2.1 每种 IR 一种文本格式）。
//
// 格式对齐 OMC `list(X, exportKind=OpenModelica.Scripting.ExportKind.SCode)` 的输出
// （Template/SCodeDumpTpl.tpl，行号见 scode_dump.cpp），使 tools/scode_dump_compare.py 能逐行 diff：
// 可再解析的 Modelica 文本；PARTS 体按 元素 → initial equation → initial algorithm → equation → algorithm →
// external 打印；visibility 用独立的 `protected`/`public` 行；默认剥掉图形/文档类注解（showAnnotations=false）。
#pragma once
#include <string>

#include "scode/scode.h"

namespace ymc {

// 打印一个 SCode 元素（通常是类），不带结尾 `;`（同 OMC list(C, exportKind=SCode)），末尾换行。indent 为起始缩进。
[[nodiscard]] std::string dump_scode_element(const SCode& s, Id<SElement> el, const std::string& indent = "");

// 打印整个 program（SCodeDump.programStr）。
[[nodiscard]] std::string dump_scode_program(const SCode& s);

}  // namespace ymc
