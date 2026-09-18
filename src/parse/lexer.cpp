// 词法器实现（方案 §5 S1）。
#include "parse/lexer.h"

#include <string>
#include <unordered_map>

namespace ymc {

namespace {

// '$' 是 OMC 兼容扩展：testsuite 与 OMC 内部名用 $overload / $cpuTime / $DER 等
// （OMC 词法 IDENT2 单列了 '$cpuTime'，语法层再认 $overload；这里统一放宽到标识符字符）。
bool is_ident_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}
bool is_ident_cont(char c) {
  return is_ident_start(c) || (c >= '0' && c <= '9');
}
bool is_digit(char c) { return c >= '0' && c <= '9'; }

const std::unordered_map<std::string_view, TokenKind>& keyword_table() {
  static const std::unordered_map<std::string_view, TokenKind> kw = {
      {"algorithm", TokenKind::Kw_algorithm}, {"and", TokenKind::Kw_and},
      {"annotation", TokenKind::Kw_annotation},
      {"block", TokenKind::Kw_block}, {"break", TokenKind::Kw_break},
      {"class", TokenKind::Kw_class}, {"connect", TokenKind::Kw_connect},
      {"connector", TokenKind::Kw_connector}, {"constant", TokenKind::Kw_constant},
      {"constrainedby", TokenKind::Kw_constrainedby}, {"der", TokenKind::Kw_der},
      {"discrete", TokenKind::Kw_discrete}, {"each", TokenKind::Kw_each},
      {"else", TokenKind::Kw_else}, {"elseif", TokenKind::Kw_elseif},
      {"elsewhen", TokenKind::Kw_elsewhen}, {"encapsulated", TokenKind::Kw_encapsulated},
      {"end", TokenKind::Kw_end}, {"enumeration", TokenKind::Kw_enumeration},
      {"equation", TokenKind::Kw_equation}, {"expandable", TokenKind::Kw_expandable},
      {"extends", TokenKind::Kw_extends}, {"external", TokenKind::Kw_external},
      {"false", TokenKind::Kw_false}, {"final", TokenKind::Kw_final},
      {"flow", TokenKind::Kw_flow}, {"for", TokenKind::Kw_for},
      {"function", TokenKind::Kw_function}, {"if", TokenKind::Kw_if},
      {"import", TokenKind::Kw_import}, {"impure", TokenKind::Kw_impure},
      {"in", TokenKind::Kw_in}, {"initial", TokenKind::Kw_initial},
      {"inner", TokenKind::Kw_inner}, {"input", TokenKind::Kw_input},
      {"loop", TokenKind::Kw_loop}, {"model", TokenKind::Kw_model},
      {"not", TokenKind::Kw_not}, {"operator", TokenKind::Kw_operator},
      {"or", TokenKind::Kw_or}, {"outer", TokenKind::Kw_outer},
      {"output", TokenKind::Kw_output}, {"package", TokenKind::Kw_package},
      {"parameter", TokenKind::Kw_parameter}, {"partial", TokenKind::Kw_partial},
      {"protected", TokenKind::Kw_protected}, {"public", TokenKind::Kw_public},
      {"pure", TokenKind::Kw_pure}, {"record", TokenKind::Kw_record},
      {"redeclare", TokenKind::Kw_redeclare}, {"replaceable", TokenKind::Kw_replaceable},
      {"return", TokenKind::Kw_return}, {"stream", TokenKind::Kw_stream},
      {"then", TokenKind::Kw_then}, {"true", TokenKind::Kw_true},
      {"type", TokenKind::Kw_type}, {"when", TokenKind::Kw_when},
      {"while", TokenKind::Kw_while}, {"within", TokenKind::Kw_within},
  };
  return kw;
}

}  // namespace

TokenKind keyword_kind(std::string_view text) noexcept {
  auto it = keyword_table().find(text);
  return it == keyword_table().end() ? TokenKind::Ident : it->second;
}

char Lexer::advance() noexcept {
  char c = src_[pos_++];
  if (c == '\n') {
    ++line_;
    col_ = 1;
  } else if (c == '\r') {
    // \r 或 \r\n：把 \r 当行结束，若后跟 \n 由下一次 advance 再算一次会重复计行，
    // 故这里对 \r\n 特殊处理：遇到 \r 增行、重置列；若下一个是 \n 顺带吃掉。
    ++line_;
    col_ = 1;
    if (pos_ < src_.size() && src_[pos_] == '\n') ++pos_;
  } else {
    ++col_;
  }
  return c;
}

void Lexer::skip_trivia() {
  for (;;) {
    char c = peek();
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      advance();
    } else if (c == '/' && peek(1) == '/') {
      while (!at_end() && peek() != '\n' && peek() != '\r') advance();
    } else if (c == '/' && peek(1) == '*') {
      SourceLoc start{file_, line_, col_, 2};
      advance();
      advance();  // 吃 /*
      bool closed = false;
      while (!at_end()) {
        if (peek() == '*' && peek(1) == '/') {
          advance();
          advance();
          closed = true;
          break;
        }
        advance();
      }
      if (!closed) diags_.error(start, "unterminated block comment");
    } else {
      return;
    }
  }
}

SourceLoc Lexer::loc_from(std::size_t start, std::uint32_t sl, std::uint32_t sc) const {
  return SourceLoc{file_, sl, sc, static_cast<std::uint32_t>(pos_ - start)};
}

Token Lexer::lex_number(std::size_t start, std::uint32_t sl, std::uint32_t sc) {
  bool is_real = false;
  while (is_digit(peek())) advance();
  if (peek() == '.') {
    is_real = true;
    advance();
    while (is_digit(peek())) advance();
  }
  if (peek() == 'e' || peek() == 'E') {
    is_real = true;
    advance();
    if (peek() == '+' || peek() == '-') advance();
    while (is_digit(peek())) advance();
  }
  std::string_view lex = src_.substr(start, pos_ - start);
  return Token{is_real ? TokenKind::RealLit : TokenKind::IntLit,
               loc_from(start, sl, sc), interner_.intern(lex)};
}

Token Lexer::lex_ident(std::size_t start, std::uint32_t sl, std::uint32_t sc) {
  while (is_ident_cont(peek())) advance();
  std::string_view lex = src_.substr(start, pos_ - start);
  TokenKind kind = keyword_kind(lex);
  if (!pure_impure_keywords_ && (kind == TokenKind::Kw_pure || kind == TokenKind::Kw_impure))
    kind = TokenKind::Ident;  // --std < 3.3
  return Token{kind, loc_from(start, sl, sc), interner_.intern(lex)};
}

Token Lexer::lex_qident(std::size_t start, std::uint32_t sl, std::uint32_t sc) {
  advance();  // 开头的 '
  while (!at_end() && peek() != '\'') {
    if (peek() == '\\') advance();  // 转义：跳过反斜杠，下面再吃一个
    if (!at_end()) advance();
  }
  if (peek() == '\'') {
    advance();
  } else {
    diags_.error(loc_from(start, sl, sc), "unterminated quoted identifier");
  }
  // Q-IDENT 词素连同两侧引号一起 intern（unparse 原样输出 'x'）。
  std::string_view lex = src_.substr(start, pos_ - start);
  return Token{TokenKind::Ident, loc_from(start, sl, sc), interner_.intern(lex)};
}

Token Lexer::lex_string(std::size_t start, std::uint32_t sl, std::uint32_t sc) {
  advance();  // 开头的 "
  // 原文保留转义序列（同 OMC：Absyn.STRING 存 BaseModelica_Lexer.g:338 STRING_GUTS 的原文，只做编码转换）。
  // 为什么：Absyn/SCode 的 dump 要与 OMC 逐字一致；解转义是语义动作，放在需要的地方（base/strings.h）。
  std::string content;
  while (!at_end() && peek() != '"') {
    char c = advance();
    content.push_back(c);
    if (c == '\\' && !at_end()) content.push_back(advance());  // 转义对：\" 不结束字符串
  }
  if (peek() == '"') {
    advance();
  } else {
    diags_.error(loc_from(start, sl, sc), "unterminated string literal");
  }
  return Token{TokenKind::StringLit, loc_from(start, sl, sc), interner_.intern(content)};
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> toks;
  // 跳过 UTF-8 BOM（EF BB BF）：MLS §13.4 要求 .mo 为 UTF-8，Windows 工具常带 BOM；不算列
  if (src_.size() >= 3 && static_cast<unsigned char>(src_[0]) == 0xEF &&
      static_cast<unsigned char>(src_[1]) == 0xBB && static_cast<unsigned char>(src_[2]) == 0xBF)
    pos_ = 3;
  for (;;) {
    skip_trivia();
    std::size_t start = pos_;
    std::uint32_t sl = line_, sc = col_;
    if (at_end()) {
      // EOF 的列记 0：对齐 OMC 报 "<EOF>" 时的 `line:0`（如 MissingSemicolon.mo 期望 20:0）
      (void)sc;
      toks.push_back(Token{TokenKind::Eof, SourceLoc{file_, sl, 0, 0}, Symbol{}});
      return toks;
    }
    char c = peek();

    // 数字；OMC 的词法器也接受无整数部分的实数（如 .13E2），跟它对齐（Modelica_3_Lexer.g）
    if (is_digit(c) || (c == '.' && is_digit(peek(1)))) {
      toks.push_back(lex_number(start, sl, sc));
      continue;
    }
    if (is_ident_start(c)) {
      toks.push_back(lex_ident(start, sl, sc));
      continue;
    }
    if (c == '\'') {
      toks.push_back(lex_qident(start, sl, sc));
      continue;
    }
    if (c == '"') {
      toks.push_back(lex_string(start, sl, sc));
      continue;
    }

    // 运算符 / 标点（最大匹配）。
    auto emit = [&](TokenKind k, int n) {
      for (int i = 0; i < n; ++i) advance();
      toks.push_back(Token{k, loc_from(start, sl, sc), Symbol{}});
    };
    char c1 = peek(1);
    switch (c) {
      case '(': emit(TokenKind::LParen, 1); break;
      case ')': emit(TokenKind::RParen, 1); break;
      case '[': emit(TokenKind::LBracket, 1); break;
      case ']': emit(TokenKind::RBracket, 1); break;
      case '{': emit(TokenKind::LBrace, 1); break;
      case '}': emit(TokenKind::RBrace, 1); break;
      case ';': emit(TokenKind::Semicolon, 1); break;
      case ',': emit(TokenKind::Comma, 1); break;
      case ':': emit(c1 == '=' ? TokenKind::Assign : TokenKind::Colon, c1 == '=' ? 2 : 1); break;
      case '+': emit(TokenKind::Plus, 1); break;
      case '-': emit(TokenKind::Minus, 1); break;
      case '*': emit(TokenKind::Star, 1); break;
      case '/': emit(TokenKind::Slash, 1); break;
      case '^': emit(TokenKind::Caret, 1); break;
      case '=': emit(c1 == '=' ? TokenKind::EqEq : TokenKind::Equal, c1 == '=' ? 2 : 1); break;
      case '<':
        if (c1 == '=') emit(TokenKind::LessEq, 2);
        else if (c1 == '>') emit(TokenKind::NotEq, 2);
        else emit(TokenKind::Less, 1);
        break;
      case '>': emit(c1 == '=' ? TokenKind::GreaterEq : TokenKind::Greater, c1 == '=' ? 2 : 1); break;
      case '.':
        switch (c1) {
          case '+': emit(TokenKind::DotPlus, 2); break;
          case '-': emit(TokenKind::DotMinus, 2); break;
          case '*': emit(TokenKind::DotStar, 2); break;
          case '/': emit(TokenKind::DotSlash, 2); break;
          case '^': emit(TokenKind::DotCaret, 2); break;
          default: emit(TokenKind::Dot, 1); break;
        }
        break;
      default:
        diags_.error(SourceLoc{file_, sl, sc, 1},
                     std::string("unexpected character '") + c + "'");
        advance();
        break;
    }
  }
}

const char* token_kind_name(TokenKind k) noexcept {
  switch (k) {
    case TokenKind::Eof: return "eof";
    case TokenKind::Ident: return "ident";
    case TokenKind::IntLit: return "int";
    case TokenKind::RealLit: return "real";
    case TokenKind::StringLit: return "string";
    case TokenKind::LParen: return "(";
    case TokenKind::RParen: return ")";
    case TokenKind::LBracket: return "[";
    case TokenKind::RBracket: return "]";
    case TokenKind::LBrace: return "{";
    case TokenKind::RBrace: return "}";
    case TokenKind::Semicolon: return ";";
    case TokenKind::Comma: return ",";
    case TokenKind::Colon: return ":";
    case TokenKind::Dot: return ".";
    case TokenKind::Assign: return ":=";
    case TokenKind::Equal: return "=";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::Caret: return "^";
    case TokenKind::DotPlus: return ".+";
    case TokenKind::DotMinus: return ".-";
    case TokenKind::DotStar: return ".*";
    case TokenKind::DotSlash: return "./";
    case TokenKind::DotCaret: return ".^";
    case TokenKind::Less: return "<";
    case TokenKind::LessEq: return "<=";
    case TokenKind::Greater: return ">";
    case TokenKind::GreaterEq: return ">=";
    case TokenKind::EqEq: return "==";
    case TokenKind::NotEq: return "<>";
    default: return is_keyword(k) ? "keyword" : "?";
  }
}

}  // namespace ymc
