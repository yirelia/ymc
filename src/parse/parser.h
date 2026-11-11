// 递归下降解析器（方案 §5 S1，v2）：Token 流 → Absyn。表达式用分层优先级（MLS §B.2.7）。
//
// 用法：
//   Parser p(tokens, absyn, diags);
//   p.parse_program();   // 结果写进 absyn.top
//
// 错误处理走 Diagnostics（不抛异常，方案 §3.4 第 6 条）；出错后同步到 ; 或 end 继续解析（AST 尽量完整），
// 但报哪些错照 omc：见下面 omc_lexer_error_ / omc_aborted_。所有列表循环都有"进度保证"（一轮没消费记号就强制前进），
// 杜绝死循环。
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "absyn/absyn.h"
#include "base/diag.h"
#include "parse/token.h"

namespace ymc {

class Parser {
 public:
  // 词法阶段报过错（Eof 记号带 kLexerErrorMark）就从置上 lexerError 开始，同 omc 共用的那个全局旗。
  Parser(const std::vector<Token>& toks, Absyn& out, Diagnostics& diags)
      : toks_(toks), out_(out), diags_(diags),
        omc_lexer_error_(!toks.empty() && toks.back().kind == TokenKind::Eof && toks.back().text.is_valid()) {}

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
  void error_at(SourceLoc loc, std::string msg);   // ANTLR 失配 / modelicaParserAssert 类语法错的出口
  void error_unconditional(SourceLoc loc, std::string msg);   // 文法动作里直接 c_add_source_message 并置 lexerError 的那几条
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
  void reject_subscripted_modifier(SourceLoc name_start);   // 修饰名后跟 '['：报错并吃掉下标（在 '[' 处调用）
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
  Id<Exp> parse_argument_expression();             // 一个实参：只有这里允许整条是部分函数应用
  Id<Exp> parse_partial_application();             // function f(a = 1)，从 function 开始
  std::vector<Id<Exp>> parse_expression_list();    // expr (, expr)*

  Id<Exp> mk(Exp e) { return out_.add_exp(std::move(e)); }

  const std::vector<Token>& toks_;
  Absyn& out_;
  Diagnostics& diags_;
  std::size_t pos_ = 0;
  bool trace_ = false;
  // omc 报语法错的两种方式（OMC v1.25.0），ymc 用这两个旗照着报，解析本身照常往下走（AST 完整）：
  //  - ANTLR 失配 / 无可行分支、modelicaParserAssert：抛异常。解析器装的是 noRecover（parse.c:320-321），异常一路退出
  //    整份解析，此后的规则动作都不再执行——所以 omc 每份文件至多报一条这类错（error_at；之后置 omc_aborted_，什么都不报）。
  //    若此前已置 lexerError，这条也不报（parse.c:193 handleParseError 见 lexerError 即返回），照样终止。
  //  - 文法动作里直接 c_add_source_message 再置 lexerError、不抛异常的四处：下标修饰（Modelica.g:908-922）、部分函数应用
  //   （:1527-1536）、元组下标（:1802-1806）、实数上溢（:1779-1788）。报这条不看 lexerError，解析继续（error_unconditional）。
  // 实证（s1fix 对抗评审 T01-T10，omc 实跑全部报错逐条比）：`A a(x[);` 后的元组下标照报（T02）、部分函数应用后的缺表达式
  // 不报（T07）、元组下标后的缺表达式不报（T09）。下标修饰之后 omc 是否终止取决于 '[' 后那个记号，见 reject_subscripted_modifier。
  bool omc_lexer_error_ = false;   // = ModelicaParser_lexerError（词法错经 Eof 记号带过来，见构造函数）
  bool omc_aborted_ = false;       // omc 的解析已经因异常退出
};

}  // namespace ymc
