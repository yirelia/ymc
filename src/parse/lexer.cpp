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
// Q-CHAR（MLS 3.6 §2.3.1；BaseModelica_Lexer.g:434-436 v1.25.0）：字母、数字、下划线 + 这串 ASCII 标点 + 空格 + 双引号。
// 非 ASCII 字节、制表符、换行、反引号都不在内（`'höhe'`、`'a`b'` omc 在词法阶段就报错，PS 探针 l1）。
bool is_qchar(char c) {
  return is_ident_cont(c) || std::string_view("!#%&()*+,-./:;<>=?@[]^{}|~ \"").find(c) != std::string_view::npos;
}

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

// omc 的词法错只报第一条：handleLexerError 只在 lexerError 未置时报、报完置上（parse.c:168-173 v1.25.0）。置上的
// lexerError 也让解析器随后那条抛异常的语法错不报（parse.c:193）——经 Eof 记号的 text 交给 Parser（见 tokenize 末尾）。
// s1fix 对抗评审 U01 等：未闭合的记号报一条后，omc 不再报 EOF 处缺 end 之类。
void Lexer::error_at(SourceLoc loc, std::string msg) {
  if (had_error_) return;
  had_error_ = true;
  diags_.error(omc_syntax_diag_loc(loc), std::move(msg));
}

void Lexer::skip_trivia() {
  for (;;) {
    char c = peek();
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      advance();
    } else if (c == '/' && peek(1) == '/') {
      while (!at_end() && peek() != '\n' && peek() != '\r') advance();
    } else if (c == '/' && peek(1) == '*') {
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
      // 未闭合：ML_COMMENT 的 '*/' 在 EOF 处失配，报在 EOF（见 eof_loc）。
      if (!closed) error_at(eof_loc(), "unterminated block comment");
    } else {
      return;
    }
  }
}

SourceLoc Lexer::loc_from(std::size_t start, std::uint32_t sl, std::uint32_t sc) const {
  return SourceLoc{file_, sl, sc, static_cast<std::uint32_t>(pos_ - start)};
}

// 吃到 EOF 才失败的记号（未闭合的引号名 / 字符串 / 块注释）：omc 报在词法器出错时所在的位置，也就是 EOF，不是记号
// 起点——收尾的 MATCHC 在 EOF 失配，matchc 的 recover 在 EOF 不前进（antlr3inputstream.c:249 到尾即止，
// 3rdParty 520663f5），nextTokenStr 随即报错（antlr3lexer.c:258-264）。s1fix 对抗评审 U01-U06 / U10 / U11 / L1x05 / L1x06：
// `model U03 Real '` 报 1:16（EOF 在第 17 列，第 1 行左移一列），文件以换行结尾时报在下一行第 1 列（L1x05 2:1）。
SourceLoc Lexer::eof_loc() const {
  return SourceLoc{file_, line_, col_, 0};
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
    // EXPONENT : ('e'|'E') ('+'|'-')? (DIGIT)+（BaseModelica_Lexer.g:447-449 v1.25.0）：一个数字都没有是词法错，omc 报
    // "Lexer got '1e' but failed to recognize the rest"，位置在已吃掉的 e / 符号之后那一列（(DIGIT)+ 早退、不 recover，
    // nextTokenStr 先报后 recover，antlr3lexer.c:258-264）。s1fix 对抗评审 LX21 `x = 1e;` 报 2:23、LX25 `1.5e+;` 报 2:26
    //（都是 ';' 那一列；第 1 行再左移一列，LX11 1:22）。
    if (!is_digit(peek())) error_at(SourceLoc{file_, line_, col_, 1}, "incomplete exponent in real literal");
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
  // QIDENT = '\'' (QCHAR | SESCAPE) (QCHAR | SESCAPE)* '\''（BaseModelica_Lexer.g:430-431 v1.25.0）。坏字符报在哪一列，
  // 取决于它卡在哪个元素上：
  //  - 紧跟开引号的必选元素 (QCHAR | SESCAPE)：生成的 mQIDENT 在这里构造"无可行分支"异常后直接 goto 出规则、不 recover
  //   （ANTLR 生成的 Modelica_3_Lexer_BaseModelica_Lexer.c，QIDENT 规则自 v1.25.0 未改），antlr3lexer.c:258-264 的
  //    nextTokenStr 先 reportError 再 recover——报错时输入还停在坏字符上 → 报在坏字符本身那一列。`''` 的第二个引号（Q2 报 2:9）、`'跳…'`（JBouncingBall.mo 5:8）、
  //    `'`b'`、`'\tb'`、`'` 后直接换行（s1fix 探针 s01-s03 / s07）都是这一支。
  //  - 之后的循环 (QCHAR | SESCAPE)*：碰到坏字符就退出循环，接着 MATCHC('\'') 失配；antlr3 运行时的 matchc 先 recover
  //    再返回（antlr3lexer.c:777，3rdParty 520663f5），OMC 的 lexNoRecover 吃掉一个字符（parse.c:58-65）——报错时已越过
  //    坏字符 → 报在它的下一列（`'a`b'` 报 2:11、`'höhe'` 的 ö 首字节在 2:10 报 2:11，PS 探针 Q1 / Q3，s1fix s04 / s05）。
  // 报完照旧吃到收尾的引号（错误恢复），一个词素只报一条（一个汉字是 3 个字节）。
  if (peek() == '\'') error_at(SourceLoc{file_, line_, col_, 1}, "empty quoted identifier");
  bool bad = false;
  bool first = true;   // 正看着紧跟开引号的那个必选元素
  while (!at_end() && peek() != '\'') {
    if (peek() == '\\') {
      // SESCAPE（BaseModelica_Lexer.g:389-405 v1.25.0）：合法转义 \' \" \\ \? \a \b \f \n \r \t \v 连同反斜杠吃两个字符；
      // 别的只吃反斜杠（omc 告警 "Lexer treating \ as \\"），下一个字符照常按 QCHAR 判——`\q` 照收（PS 探针 L14），
      // `'a\`b'` 的反引号照样是坏字符（s1fix 对抗评审 Q03 报 3:12、Q06 `'\`'` 报 3:11，都是反引号的下一列）。
      advance();
      if (!at_end() && std::string_view("'\"\\?abfnrtv").find(peek()) != std::string_view::npos) advance();
      first = false;
      continue;
    }
    const bool ok = is_qchar(peek());
    const SourceLoc at_bad{file_, line_, col_, 1};
    advance();
    if (!ok && !bad) {
      error_at(first ? at_bad : SourceLoc{file_, line_, col_, 1}, "invalid character in quoted identifier");
      bad = true;
    }
    first = false;
  }
  if (peek() == '\'') {
    advance();
  } else {
    error_at(eof_loc(), "unterminated quoted identifier");
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
    error_at(eof_loc(), "unterminated string literal");
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
      toks.push_back(Token{TokenKind::Eof, SourceLoc{file_, sl, 0, 0},
                           had_error_ ? interner_.intern(kLexerErrorMark) : Symbol{}});
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
        error_at(SourceLoc{file_, sl, sc, 1}, std::string("unexpected character '") + c + "'");
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
