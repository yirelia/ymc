// 词法记号（token）——方案 §5 S1、MLS 3.6 附录 B。
//
// 一个 Token = 种类 + 源码位置 + 词素文本（intern 成 Symbol）。数值字面量的值在解析阶段
// 从文本转换（v1 够用即止）。关键字用独立的 TokenKind（配 -Werror=switch 得到穷尽检查）。
#pragma once
#include <cstdint>

#include "base/loc.h"
#include "base/symbol.h"

namespace ymc {

enum class TokenKind : std::uint8_t {
  Eof,
  Ident,        // 普通标识符或 Q-IDENT（'...'）；词素含引号，intern 后原样保存
  IntLit,       // 整数字面量
  RealLit,      // 实数字面量
  StringLit,    // 字符串字面量；text = 去引号并处理转义后的内容

  // 标点 / 运算符
  LParen, RParen, LBracket, RBracket, LBrace, RBrace,
  Semicolon, Comma, Colon, Dot,
  Assign,       // :=
  Equal,        // =
  Plus, Minus, Star, Slash, Caret,
  DotPlus, DotMinus, DotStar, DotSlash, DotCaret,   // 逐元素算子 .+ .- .* ./ .^
  Less, LessEq, Greater, GreaterEq, EqEq, NotEq,    // < <= > >= == <>

  // 关键字（MLS 附录 B.2.7 保留字）
  Kw_algorithm, Kw_and, Kw_annotation, Kw_assert, Kw_block, Kw_break, Kw_class,
  Kw_connect, Kw_connector, Kw_constant, Kw_constrainedby, Kw_der, Kw_discrete,
  Kw_each, Kw_else, Kw_elseif, Kw_elsewhen, Kw_encapsulated, Kw_end, Kw_enumeration,
  Kw_equation, Kw_expandable, Kw_extends, Kw_external, Kw_false, Kw_final, Kw_flow,
  Kw_for, Kw_function, Kw_if, Kw_import, Kw_impure, Kw_in, Kw_initial, Kw_inner,
  Kw_input, Kw_loop, Kw_model, Kw_not, Kw_operator, Kw_or, Kw_outer, Kw_output,
  Kw_package, Kw_parameter, Kw_partial, Kw_protected, Kw_public, Kw_pure, Kw_record,
  Kw_redeclare, Kw_replaceable, Kw_return, Kw_stream, Kw_then, Kw_true, Kw_type,
  Kw_when, Kw_while, Kw_within,
};

struct Token {
  TokenKind kind = TokenKind::Eof;
  SourceLoc loc;
  Symbol text;   // 词素（idents/keywords 原文；string 为解码后内容；数字为原文）
};

// 判断一个 TokenKind 是否是关键字（关键字也可当普通标识符用的场合极少，v1 不处理）。
[[nodiscard]] inline bool is_keyword(TokenKind k) noexcept {
  return k >= TokenKind::Kw_algorithm && k <= TokenKind::Kw_within;
}

// 关键字文本 → TokenKind；非关键字返回 TokenKind::Ident。定义在 lexer.cpp。
[[nodiscard]] TokenKind keyword_kind(std::string_view text) noexcept;

// TokenKind 的可读名字（诊断/dump 用）。定义在 lexer.cpp。
[[nodiscard]] const char* token_kind_name(TokenKind k) noexcept;

}  // namespace ymc
