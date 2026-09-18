// 词法器（方案 §5 S1）：手写状态机，把源码切成 Token 流。
//
// 用法：
//   Lexer lex(source, file_id, interner, diags);
//   std::vector<Token> toks = lex.tokenize();   // 末尾含一个 Eof
//
// 处理：空白、行注释 //、块注释 /* */（MLS 不允许嵌套）、标识符、Q-IDENT '...'、
// 整数/实数、字符串（含转义）、全部运算符与标点（最大匹配）。位置从 1 起。
#pragma once
#include <string_view>
#include <vector>

#include "base/diag.h"
#include "base/symbol.h"
#include "parse/token.h"

namespace ymc {

class Lexer {
 public:
  // pure_impure_keywords：pure/impure 是 MLS 3.3 引入的保留字；--std=3.2 及更早时它们是普通标识符
  //（OMC 用同一开关，见 testsuite/openmodelica/parser/PureImpure3.mo）。
  Lexer(std::string_view src, std::uint32_t file, Interner& interner, Diagnostics& diags,
        bool pure_impure_keywords = true)
      : src_(src), file_(file), interner_(interner), diags_(diags),
        pure_impure_keywords_(pure_impure_keywords) {}

  [[nodiscard]] std::vector<Token> tokenize();

 private:
  [[nodiscard]] bool at_end() const noexcept { return pos_ >= src_.size(); }
  [[nodiscard]] char peek(std::size_t k = 0) const noexcept {
    return (pos_ + k < src_.size()) ? src_[pos_ + k] : '\0';
  }
  char advance() noexcept;
  void skip_trivia();                       // 空白 + 注释
  [[nodiscard]] SourceLoc loc_from(std::size_t start, std::uint32_t sl, std::uint32_t sc) const;

  Token lex_number(std::size_t start, std::uint32_t sl, std::uint32_t sc);
  Token lex_ident(std::size_t start, std::uint32_t sl, std::uint32_t sc);
  Token lex_qident(std::size_t start, std::uint32_t sl, std::uint32_t sc);
  Token lex_string(std::size_t start, std::uint32_t sl, std::uint32_t sc);

  std::string_view src_;
  std::uint32_t file_;
  Interner& interner_;
  Diagnostics& diags_;
  std::size_t pos_ = 0;
  std::uint32_t line_ = 1;
  std::uint32_t col_ = 1;
  bool pure_impure_keywords_ = true;
};

}  // namespace ymc
