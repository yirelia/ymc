// Modelica 字符串的转义/解转义（对齐 OMC，S2 起 dump 要与 OMC 逐行比对）。
//
// 词法器把字符串原文（转义序列原样）存进 Absyn（同 OMC：Absyn.STRING 存 BaseModelica_Lexer.g:338 的
// STRING_GUTS 原文）；只有 SCode 的注释串被解转义（AbsynToSCode.mo:1474/1496/1502 System.unescapedString），
// SCode dump 再把注释串转义回去（SCodeDumpTpl.tpl:918 System.escapedString(cmt, false)）。
#pragma once
#include <string>
#include <string_view>

namespace ymc {

// System.unescapedString（Compiler/runtime/systemimpl.c:1761-1800）：识别 \' \" \? \\ \a \b \f \n \r \t \v；
// 其它 `\x` 原样保留反斜杠。
inline std::string unescape_modelica_string(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '\\' && i + 1 < s.size()) {
      switch (s[i + 1]) {
        case '\'': c = '\''; ++i; break;
        case '"': c = '"'; ++i; break;
        case '?': c = '?'; ++i; break;
        case '\\': c = '\\'; ++i; break;
        case 'a': c = '\a'; ++i; break;
        case 'b': c = '\b'; ++i; break;
        case 'f': c = '\f'; ++i; break;
        case 'n': c = '\n'; ++i; break;
        case 'r': c = '\r'; ++i; break;
        case 't': c = '\t'; ++i; break;
        case 'v': c = '\v'; ++i; break;
        default: break;
      }
    }
    out.push_back(c);
  }
  return out;
}

// omc__escapedString（SimulationRuntime/c/util/modelica_string.c:272-300）：转义 " \ \a \b \f \v；
// nl 为真时还转义 \r \n（SCode dump 传 false：换行原样输出）。
inline std::string escape_modelica_string(std::string_view s, bool nl) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\a': out += "\\a"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\v': out += "\\v"; break;
      case '\r': if (nl) out += "\\r"; else out += c; break;
      case '\n': if (nl) out += "\\n"; else out += c; break;
      default: out += c; break;
    }
  }
  return out;
}

}  // namespace ymc
