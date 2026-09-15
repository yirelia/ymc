// 递归下降解析器（方案 §5 S1）：Token 流 → Absyn。表达式用分层优先级（MLS §B.2.7）。
//
// 用法：
//   Parser p(tokens, absyn, diags);
//   p.parse_program();   // 结果写进 absyn.top
//
// 错误处理走 Diagnostics（不抛异常，方案 §3.4 第 6 条）；出错后同步到 ; 或 end 继续，
// 尽量多报几个错。不支持的构造给出带位置的诊断并跳过（见 absyn.h 头注 v1 范围）。
#pragma once
#include <string_view>
#include <vector>

#include "absyn/absyn.h"
#include "base/diag.h"
#include "parse/token.h"

namespace ymc {

class Parser {
 public:
  Parser(const std::vector<Token>& toks, Absyn& out, Diagnostics& diags)
      : toks_(toks), out_(out), diags_(diags) {}

  void parse_program();

 private:
  // token 游标
  [[nodiscard]] const Token& peek(std::size_t k = 0) const;
  [[nodiscard]] TokenKind kind(std::size_t k = 0) const { return peek(k).kind; }
  [[nodiscard]] bool at(TokenKind k) const { return kind() == k; }
  const Token& advance();
  bool match(TokenKind k);                       // 命中则吃掉并返回 true
  bool expect(TokenKind k, const char* what);    // 不命中则报错
  void error_here(const char* msg);
  void sync_to_semicolon();                       // 错误恢复

  // 顶层
  Id<ClassDef> parse_class_definition(bool is_final);
  void parse_composition(ClassDef& cls);
  void parse_element_list(ClassDef& cls, bool is_protected);
  void parse_element(ClassDef& cls, bool is_protected);
  void parse_equation_section(ClassDef& cls, bool is_initial);
  Equation parse_equation();

  // 声明 / 类型 / 修饰
  ComponentClause parse_component_clause();
  bool parse_type_prefix(TypePrefix& tp);
  std::vector<Symbol> parse_name_path(bool& leading_dot);
  Id<Modification> parse_modification_opt();
  Id<Modification> parse_class_modification();    // 从 '(' 开始
  Import parse_import();
  Extends parse_extends();

  // 注释
  Symbol parse_string_comment_opt();              // 可选 STRING（拼接 + 号连接的多段）
  void skip_annotation_opt();                      // annotation(...) —— v1 解析并丢弃

  // 表达式（分层优先级）
  Id<Exp> parse_expression();
  Id<Exp> parse_simple_expression();               // 含 range a:b:c
  Id<Exp> parse_logical_expression();              // or
  Id<Exp> parse_logical_term();                    // and
  Id<Exp> parse_logical_factor();                  // not
  Id<Exp> parse_relation();                        // < <= > >= == <>
  Id<Exp> parse_arithmetic();                      // + -
  Id<Exp> parse_term();                            // * /
  Id<Exp> parse_factor();                          // ^
  Id<Exp> parse_primary();
  Id<Exp> parse_cref_or_call();
  Id<Cref> parse_cref();
  std::vector<Id<Exp>> parse_array_subscripts();   // 从 '[' 开始
  std::vector<Id<Exp>> parse_function_args();      // 从 '(' 之后到 ')' 之前

  // 造节点的小工具
  Id<Exp> mk(Exp e) { return out_.add_exp(std::move(e)); }

  const std::vector<Token>& toks_;
  Absyn& out_;
  Diagnostics& diags_;
  std::size_t pos_ = 0;
};

}  // namespace ymc
