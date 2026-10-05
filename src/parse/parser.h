// 递归下降解析器（方案 §5 S1，v2）：Token 流 → Absyn。表达式用分层优先级（MLS §B.2.7）。
//
// 用法：
//   Parser p(tokens, absyn, diags);
//   p.parse_program();   // 结果写进 absyn.top
//
// 错误处理走 Diagnostics（不抛异常，方案 §3.4 第 6 条）；出错后同步到 ; 或 end 继续，
// 尽量多报几个错。所有列表循环都有"进度保证"（一轮没消费记号就强制前进），杜绝死循环。
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

  // 单文件模式：解析后把 within/顶层类写进 Absyn（unparse/driver 用）。
  void parse_program();
  // 多文件模式（S2 装载器用）：只返回本文件的 stored-definition，不碰 Absyn::top/within。
  [[nodiscard]] StoredDefinition parse_stored_definition();
  // --log=parse：主要规则入口打印 "[parse] <rule> @line:col"（方案 §5 S1 调试手段）
  void set_trace(bool on) { trace_ = on; }

 private:
  void trace(const char* rule) const;
  // token 游标
  [[nodiscard]] const Token& peek(std::size_t k = 0) const;
  [[nodiscard]] TokenKind kind(std::size_t k = 0) const { return peek(k).kind; }
  [[nodiscard]] bool at(TokenKind k) const { return kind() == k; }
  const Token& advance();
  bool match(TokenKind k);
  bool expect(TokenKind k, const char* what);
  void error_here(const char* msg);
  void warn_here(const char* msg);
  void sync_to_semicolon();
  [[nodiscard]] bool at_name() const;              // Ident / der / initial / assert 可当名字

  // 类
  Id<ClassDef> parse_class_definition(bool is_final);
  void parse_class_end(Symbol name, SourceLoc name_loc);   // end IDENT + 首尾一致检查
  bool parse_class_prefix(ClassDef& cls);
  void parse_short_class_body(ClassDef& cls);      // 已吃掉 '='
  void parse_composition(ClassDef& cls);
  void parse_external(ClassDef& cls);
  void parse_element_list(ClassDef& cls, bool is_protected);
  void parse_element(ClassDef& cls, bool is_protected);
  void parse_constraining_clause_opt(Element& el);

  // 声明 / 类型 / 修饰 / 注释
  ComponentClause parse_component_clause(bool single_decl = false);  // single：component_clause1
  bool parse_type_prefix(TypePrefix& tp);
  std::vector<Symbol> parse_name_path(bool& leading_dot);
  Id<Modification> parse_modification_opt();
  Id<Modification> parse_class_modification();     // 从 '(' 开始
  void parse_redeclare_in_modification(ModArg& arg);   // 从 redeclare/each/final/replaceable 处开始
  Import parse_import();
  Extends parse_extends();
  Symbol parse_string_comment_opt();
  Id<Modification> parse_annotation_opt();
  void merge_class_annotation(ClassDef& cls, Id<Modification> ann);
  void parse_comment(Symbol& str, Id<Modification>& ann);

  // 方程
  void parse_equation_list(std::vector<Id<Equation>>& out);
  Id<Equation> parse_equation();
  // 语句
  void parse_statement_list(std::vector<Id<Statement>>& out);
  Id<Statement> parse_statement();
  std::vector<ForIndex> parse_for_indices();

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
  Id<Exp> parse_postfix(Id<Exp> base);             // 括号式 (e) 后的 [subs]（只此一处合法）
  Id<Exp> reject_tuple_subscript(Id<Exp> tup, SourceLoc lparen);   // 元组后跟下标 → 报错
  Id<Exp> parse_modification_expression();         // expression | break
  Id<Cref> parse_cref();
  std::vector<Id<Exp>> parse_array_subscripts();   // 从 '[' 开始
  void parse_function_args(std::vector<Id<Exp>>& args, std::vector<Symbol>& names);
  std::vector<Id<Exp>> parse_expression_list();    // expr (, expr)*

  Id<Exp> mk(Exp e) { return out_.add_exp(std::move(e)); }

  const std::vector<Token>& toks_;
  Absyn& out_;
  Diagnostics& diags_;
  std::size_t pos_ = 0;
  bool trace_ = false;
};

}  // namespace ymc
