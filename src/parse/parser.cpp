// 递归下降解析器实现（方案 §5 S1，v2）。
#include "parse/parser.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace ymc {

namespace {

// 类体里"段"的边界：遇到这些记号，元素/方程/语句列表停止。
bool is_section_boundary(TokenKind k) {
  switch (k) {
    case TokenKind::Kw_public:
    case TokenKind::Kw_protected:
    case TokenKind::Kw_equation:
    case TokenKind::Kw_algorithm:
    case TokenKind::Kw_initial:
    case TokenKind::Kw_external:
    case TokenKind::Kw_annotation:
    case TokenKind::Kw_end:
    case TokenKind::Eof:
      return true;
    default:
      return false;
  }
}

// 方程/语句列表的终止：段边界 + 复合构造的分支关键字。
bool is_list_end(TokenKind k) {
  return is_section_boundary(k) || k == TokenKind::Kw_elseif || k == TokenKind::Kw_else ||
         k == TokenKind::Kw_elsewhen;
}

// 类定义的起始记号（含前缀）。
bool is_class_prefix_start(TokenKind k) {
  switch (k) {
    case TokenKind::Kw_class:
    case TokenKind::Kw_model:
    case TokenKind::Kw_record:
    case TokenKind::Kw_block:
    case TokenKind::Kw_connector:
    case TokenKind::Kw_type:
    case TokenKind::Kw_package:
    case TokenKind::Kw_function:
    case TokenKind::Kw_operator:
    case TokenKind::Kw_expandable:
    case TokenKind::Kw_encapsulated:
    case TokenKind::Kw_partial:
    case TokenKind::Kw_pure:
    case TokenKind::Kw_impure:
      return true;
    default:
      return false;
  }
}

}  // namespace

// -------------------------------------------------------------- 游标
const Token& Parser::peek(std::size_t k) const {
  std::size_t i = pos_ + k;
  return i < toks_.size() ? toks_[i] : toks_.back();  // back() 是 Eof
}

const Token& Parser::advance() {
  const Token& t = peek();
  if (pos_ < toks_.size() - 1) ++pos_;
  return t;
}

bool Parser::match(TokenKind k) {
  if (at(k)) {
    advance();
    return true;
  }
  return false;
}

void Parser::error_here(const char* msg) {
  diags_.error(peek().loc, std::string(msg) + "，实际遇到 '" + token_kind_name(kind()) + "'");
}

void Parser::warn_here(const char* msg) {
  diags_.report(DiagLevel::Warning, peek().loc, msg);
}

bool Parser::expect(TokenKind k, const char* what) {
  if (at(k)) {
    advance();
    return true;
  }
  error_here(what);
  return false;
}

void Parser::sync_to_semicolon() {
  while (!at(TokenKind::Eof) && !at(TokenKind::Semicolon)) advance();
  match(TokenKind::Semicolon);
}

void Parser::trace(const char* rule) const {
  if (trace_)
    std::fprintf(stderr, "[parse] %s @%u:%u\n", rule, peek().loc.line, peek().loc.col);
}

bool Parser::at_name() const {
  TokenKind k = kind();
  // der / initial / pure 是保留字但可当函数名（MLS §B.2.7 primary）；assert 不是保留字，走 Ident
  return k == TokenKind::Ident || k == TokenKind::Kw_der || k == TokenKind::Kw_initial ||
         k == TokenKind::Kw_pure;
}

// -------------------------------------------------------------- 顶层
// MLS A.2.1：stored-definition : [ within [ name ] ";" ] { [ final ] class-definition ";" }
StoredDefinition Parser::parse_stored_definition() {
  StoredDefinition sd;
  if (at(TokenKind::Kw_within)) {
    advance();
    sd.has_within = true;
    if (!at(TokenKind::Semicolon))
      sd.within_path = parse_name_path(sd.within_leading_dot);
    expect(TokenKind::Semicolon, "within 子句后应有 ';'");
  }
  while (!at(TokenKind::Eof)) {
    std::size_t before = pos_;
    bool is_final = match(TokenKind::Kw_final);
    Id<ClassDef> id = parse_class_definition(is_final);
    if (id.is_valid()) sd.classes.push_back(id);
    // 每个顶层类后必须有 ';'（缺失时 OMC 在 EOF 报错）
    if (id.is_valid()) expect(TokenKind::Semicolon, "顶层类定义后应有 ';'");
    else match(TokenKind::Semicolon);
    if (pos_ == before && !at(TokenKind::Eof)) advance();  // 进度保证
  }
  return sd;
}

void Parser::parse_program() {
  StoredDefinition sd = parse_stored_definition();
  out_.has_within = sd.has_within;
  out_.within_leading_dot = sd.within_leading_dot;
  out_.within_path = sd.within_path;
  for (Id<ClassDef> id : sd.classes) out_.top.push_back(id);
}

// -------------------------------------------------------------- 类
bool Parser::parse_class_prefix(ClassDef& cls) {
  if (match(TokenKind::Kw_expandable)) {
    if (expect(TokenKind::Kw_connector, "expandable 后应为 connector"))
      cls.restriction = Restriction::ExpandableConnector;
    return true;
  }
  if (match(TokenKind::Kw_operator)) {
    if (match(TokenKind::Kw_record)) cls.restriction = Restriction::OperatorRecord;
    else if (match(TokenKind::Kw_function)) cls.restriction = Restriction::OperatorFunction;
    else cls.restriction = Restriction::Operator;
    return true;
  }
  if (match(TokenKind::Kw_pure)) cls.purity = Purity::Pure;
  else if (match(TokenKind::Kw_impure)) cls.purity = Purity::Impure;
  if (match(TokenKind::Kw_operator)) {  // pure operator function
    if (match(TokenKind::Kw_function)) cls.restriction = Restriction::OperatorFunction;
    else cls.restriction = Restriction::Operator;
    return true;
  }
  switch (kind()) {
    case TokenKind::Kw_class: cls.restriction = Restriction::Class; break;
    case TokenKind::Kw_model: cls.restriction = Restriction::Model; break;
    case TokenKind::Kw_record: cls.restriction = Restriction::Record; break;
    case TokenKind::Kw_block: cls.restriction = Restriction::Block; break;
    case TokenKind::Kw_connector: cls.restriction = Restriction::Connector; break;
    case TokenKind::Kw_type: cls.restriction = Restriction::Type; break;
    case TokenKind::Kw_package: cls.restriction = Restriction::Package; break;
    case TokenKind::Kw_function: cls.restriction = Restriction::Function; break;
    default: return false;
  }
  advance();
  return true;
}

Id<ClassDef> Parser::parse_class_definition(bool is_final) {
  trace("class_definition");
  ClassDef cls;
  cls.loc = peek().loc;
  cls.is_final = is_final;
  cls.is_encapsulated = match(TokenKind::Kw_encapsulated);
  cls.is_partial = match(TokenKind::Kw_partial);
  if (!parse_class_prefix(cls)) {
    error_here("期望类前缀（model/class/record/...）");
    sync_to_semicolon();
    return Id<ClassDef>{};
  }

  // `model extends X(mod) "cmt" ... end X;`
  if (match(TokenKind::Kw_extends)) {
    cls.is_class_extends = true;
    if (!at(TokenKind::Ident)) {
      error_here("class extends 后应为类名");
      sync_to_semicolon();
      return Id<ClassDef>{};
    }
    SourceLoc name_loc = peek().loc;
    cls.name = advance().text;
    if (at(TokenKind::LParen)) cls.class_extends_mod = parse_class_modification();
    cls.string_comment = parse_string_comment_opt();
    parse_composition(cls);
    parse_class_end(cls.name, name_loc);
    return out_.add_class(std::move(cls));
  }

  if (!at(TokenKind::Ident)) {
    error_here("类前缀后应为类名");
    sync_to_semicolon();
    return Id<ClassDef>{};
  }
  SourceLoc name_loc = peek().loc;
  cls.name = advance().text;

  if (match(TokenKind::Equal)) {  // 短类定义
    parse_short_class_body(cls);
    return out_.add_class(std::move(cls));
  }

  cls.string_comment = parse_string_comment_opt();
  parse_composition(cls);
  parse_class_end(cls.name, name_loc);
  return out_.add_class(std::move(cls));
}

// `end IDENT`：缺 IDENT 在 end 处报（OMC: No viable alternative near token: end）；
// 首尾名字不同在类头的名字处报（OMC Modelica.g：The identifier at start and end are different）。
void Parser::parse_class_end(Symbol name, SourceLoc name_loc) {
  SourceLoc end_loc = peek().loc;
  if (!expect(TokenKind::Kw_end, "类体后应为 'end'")) return;
  if (!at(TokenKind::Ident)) {
    diags_.error(end_loc, "end 后应为类名");
    return;
  }
  Symbol end_name = advance().text;
  if (!(end_name == name))
    diags_.error(name_loc, "The identifier at start and end are different");
}

void Parser::parse_short_class_body(ClassDef& cls) {
  cls.is_short = true;
  if (match(TokenKind::Kw_enumeration)) {
    cls.is_enumeration = true;
    expect(TokenKind::LParen, "enumeration 后应为 '('");
    if (match(TokenKind::Colon)) {
      cls.enum_unspecified = true;
    } else if (!at(TokenKind::RParen)) {
      for (;;) {
        std::size_t before = pos_;
        EnumLiteral lit;
        if (!at(TokenKind::Ident)) {
          error_here("期望枚举项名");
          break;
        }
        lit.name = advance().text;
        parse_comment(lit.string_comment, lit.annotation);
        cls.enum_lits.push_back(lit);
        if (pos_ == before) break;
        if (!match(TokenKind::Comma)) break;
      }
    }
    expect(TokenKind::RParen, "枚举应以 ')' 结束");
    parse_comment(cls.string_comment, cls.annotation);
    return;
  }
  if (match(TokenKind::Kw_der)) {  // function df = der(f, x, y)
    cls.is_der = true;
    expect(TokenKind::LParen, "der 后应为 '('");
    cls.der_fn = parse_cref();
    while (match(TokenKind::Comma)) {
      if (!at(TokenKind::Ident)) {
        error_here("der(...) 里期望变量名");
        break;
      }
      cls.der_vars.push_back(advance().text);
    }
    expect(TokenKind::RParen, "der(...) 应以 ')' 结束");
    parse_comment(cls.string_comment, cls.annotation);
    return;
  }
  parse_type_prefix(cls.short_prefix);  // base_prefix：input/output（宽松接受全部前缀）
  cls.short_type = parse_name_path(cls.short_leading_dot);
  if (at(TokenKind::LBracket)) cls.short_subscripts = parse_array_subscripts();
  if (at(TokenKind::LParen)) cls.short_mod = parse_class_modification();
  parse_comment(cls.string_comment, cls.annotation);
}

void Parser::parse_composition(ClassDef& cls) {
  // 记住当前所在的段：类级 annotation 可以夹在任何段中间（testsuite 常见），处理完要回到该段继续。
  enum class Sec { Pub, Prot, Eq, InitEq, Alg } sec = Sec::Pub;
  auto run_section = [&]() {
    switch (sec) {
      case Sec::Pub: parse_element_list(cls, false); break;
      case Sec::Prot: parse_element_list(cls, true); break;
      case Sec::Eq: parse_equation_list(cls.equations); break;
      case Sec::InitEq: parse_equation_list(cls.initial_equations); break;
      case Sec::Alg: parse_statement_list(cls.algorithms.back().stmts); break;
    }
  };
  run_section();
  for (;;) {
    switch (kind()) {
      case TokenKind::Kw_public:
        advance();
        sec = Sec::Pub;
        run_section();
        break;
      case TokenKind::Kw_protected:
        advance();
        sec = Sec::Prot;
        run_section();
        break;
      case TokenKind::Kw_equation:
        advance();
        cls.equation_sections.push_back(cls.equations.size());
        sec = Sec::Eq;
        run_section();
        break;
      case TokenKind::Kw_initial:
        advance();
        if (match(TokenKind::Kw_equation)) {
          cls.initial_equation_sections.push_back(cls.initial_equations.size());
          sec = Sec::InitEq;
          run_section();
        } else if (match(TokenKind::Kw_algorithm)) {
          AlgorithmSection s;
          s.is_initial = true;
          cls.algorithms.push_back(std::move(s));
          sec = Sec::Alg;
          run_section();
        } else {
          error_here("initial 后应为 equation 或 algorithm");
        }
        break;
      case TokenKind::Kw_algorithm:
        advance();
        cls.algorithms.push_back(AlgorithmSection{});
        sec = Sec::Alg;
        run_section();
        break;
      case TokenKind::Kw_external:
        parse_external(cls);
        break;
      case TokenKind::Kw_annotation:
        merge_class_annotation(cls, parse_annotation_opt());
        match(TokenKind::Semicolon);
        run_section();  // 注解后回到当前段继续
        break;
      case TokenKind::Kw_end:
      case TokenKind::Eof:
        return;
      default:
        error_here("类体里出现意外记号");
        sync_to_semicolon();
        if (at(TokenKind::Eof)) return;
        break;
    }
  }
}

void Parser::parse_external(ClassDef& cls) {
  advance();  // external
  cls.has_external = true;
  if (at(TokenKind::StringLit)) cls.ext_lang = advance().text;
  if (at_name() || at(TokenKind::Dot)) {
    Id<Exp> e = parse_cref_or_call();
    if (match(TokenKind::Equal)) {
      cls.ext_lhs = e;
      cls.ext_call = parse_cref_or_call();
    } else {
      cls.ext_call = e;
    }
  }
  cls.ext_annotation = parse_annotation_opt();
  expect(TokenKind::Semicolon, "external 子句后应有 ';'");
}

void Parser::parse_element_list(ClassDef& cls, bool is_protected) {
  for (;;) {
    // 类级 annotation 可以夹在元素中间（testsuite 里常见，OMC 文法允许），不终止元素列表
    if (at(TokenKind::Kw_annotation)) {
      merge_class_annotation(cls, parse_annotation_opt());
      match(TokenKind::Semicolon);
      continue;
    }
    if (is_section_boundary(kind())) return;
    std::size_t before = pos_;
    parse_element(cls, is_protected);
    if (pos_ == before) advance();  // 进度保证
  }
}

void Parser::parse_element(ClassDef& cls, bool is_protected) {
  trace("element");
  Element el;
  el.loc = peek().loc;
  el.is_protected = is_protected;
  el.is_redeclare = match(TokenKind::Kw_redeclare);
  el.is_final = match(TokenKind::Kw_final);
  el.is_inner = match(TokenKind::Kw_inner);
  el.is_outer = match(TokenKind::Kw_outer);
  el.is_replaceable = match(TokenKind::Kw_replaceable);

  if (at(TokenKind::Kw_import)) {
    el.kind = ElementKind::Import;
    el.imp = parse_import();
    expect(TokenKind::Semicolon, "import 子句后应有 ';'");
    cls.elements.push_back(out_.add_elem(std::move(el)));
    return;
  }
  if (at(TokenKind::Kw_extends)) {
    el.kind = ElementKind::Extends;
    el.ext = parse_extends();
    expect(TokenKind::Semicolon, "extends 子句后应有 ';'");
    cls.elements.push_back(out_.add_elem(std::move(el)));
    return;
  }
  if (is_class_prefix_start(kind())) {
    el.kind = ElementKind::ClassDef;
    el.cls = parse_class_definition(/*is_final=*/false);  // final 记在 Element 上，避免重复
    parse_constraining_clause_opt(el);
    expect(TokenKind::Semicolon, "类定义后应有 ';'");
    cls.elements.push_back(out_.add_elem(std::move(el)));
    return;
  }
  el.kind = ElementKind::Component;
  el.comp = parse_component_clause();
  parse_constraining_clause_opt(el);
  expect(TokenKind::Semicolon, "组件声明后应有 ';'");
  cls.elements.push_back(out_.add_elem(std::move(el)));
}

void Parser::parse_constraining_clause_opt(Element& el) {
  // Modelica 2.x 用 `extends` 写约束子句（如 `replaceable type T = R extends TwoPin;`），
  // OMC 兼容之；此处（元素末尾、';' 之前）`extends` 不可能是别的东西。unparse 统一成 constrainedby。
  if (!match(TokenKind::Kw_constrainedby) && !match(TokenKind::Kw_extends)) return;
  el.has_constrainedby = true;
  el.cb_path = parse_name_path(el.cb_leading_dot);
  if (at(TokenKind::LParen)) el.cb_mod = parse_class_modification();
  parse_comment(el.cb_comment, el.cb_annotation);
}

Import Parser::parse_import() {
  advance();  // import
  Import imp;
  if (at(TokenKind::Ident) && peek(1).kind == TokenKind::Equal) {
    imp.alias = advance().text;
    advance();  // =
  }
  // import 的 name 不允许前导 '.'（MLS §13.2.1 name = IDENT {"." IDENT}；OMC 在 '.' 处报错）
  if (at(TokenKind::Dot)) error_here("import 的名字不能以 '.' 开头");
  imp.path = parse_name_path(imp.leading_dot);
  if (match(TokenKind::DotStar)) {  // 词法最大匹配把 `.*` 切成一个记号
    imp.wildcard = true;
  } else if (at(TokenKind::Dot)) {
    advance();
    if (match(TokenKind::Star)) {
      imp.wildcard = true;
    } else if (match(TokenKind::LBrace)) {
      for (;;) {
        if (!at(TokenKind::Ident)) {
          error_here("import {...} 里期望名字");
          break;
        }
        Symbol alias;
        if (peek(1).kind == TokenKind::Equal) {  // import A.{e = d}
          alias = advance().text;
          advance();  // =
          if (!at(TokenKind::Ident)) {
            error_here("import {...} 里 '=' 后期望名字");
            break;
          }
        }
        imp.multi.push_back(advance().text);
        imp.multi_alias.push_back(alias);
        if (!match(TokenKind::Comma)) break;
      }
      expect(TokenKind::RBrace, "import {...} 应以 '}' 结束");
    } else {
      error_here("import 路径后的 '.' 应接 '*' 或 '{'");
    }
  }
  parse_comment(imp.string_comment, imp.annotation);
  return imp;
}

Extends Parser::parse_extends() {
  advance();  // extends
  Extends ext;
  ext.path = parse_name_path(ext.leading_dot);
  if (at(TokenKind::LParen)) ext.mod = parse_class_modification();
  ext.annotation = parse_annotation_opt();
  return ext;
}

// -------------------------------------------------------------- 组件 / 类型 / 修饰 / 注释
// type_prefix = [flow|stream] [discrete|parameter|constant] [input|output]（MLS §B.2.4，顺序与各至多一个）。
// 严格按此顺序：多余/乱序的前缀留给后面的 parse_name_path 在该记号处报错（对齐 OMC 的位置，
// 如 Declaration1.mo `discrete constant Real x` 报在 constant）。
bool Parser::parse_type_prefix(TypePrefix& tp) {
  bool any = false;
  if (at(TokenKind::Kw_flow)) { tp.flow = TypePrefix::Flow::Flow; advance(); any = true; }
  else if (at(TokenKind::Kw_stream)) { tp.flow = TypePrefix::Flow::Stream; advance(); any = true; }
  if (at(TokenKind::Kw_discrete)) { tp.var = TypePrefix::Variability::Discrete; advance(); any = true; }
  else if (at(TokenKind::Kw_parameter)) { tp.var = TypePrefix::Variability::Parameter; advance(); any = true; }
  else if (at(TokenKind::Kw_constant)) { tp.var = TypePrefix::Variability::Constant; advance(); any = true; }
  if (at(TokenKind::Kw_input)) { tp.caus = TypePrefix::Causality::Input; advance(); any = true; }
  else if (at(TokenKind::Kw_output)) { tp.caus = TypePrefix::Causality::Output; advance(); any = true; }
  return any;
}

ComponentClause Parser::parse_component_clause(bool single_decl) {
  ComponentClause cc;
  SourceLoc start = peek().loc;
  bool had_prefix = parse_type_prefix(cc.prefix);
  cc.type_path = parse_name_path(cc.type_leading_dot);
  if (at(TokenKind::LBracket)) cc.type_subscripts = parse_array_subscripts();
  bool first = true;
  for (;;) {
    std::size_t before = pos_;
    ComponentDecl d;
    if (!at(TokenKind::Ident)) {
      // OMC（ANTLR 回溯）的位置约定：元素以类型名（IDENT）开头且第一个组件名缺失时，整条元素
      // 都"无可行分支"，报在元素起点（Identifier.mo / ParseError2.mo）；带前缀或后续组件缺名则报在
      // 出错记号处（ConstructParameters1.mo / ParseError1.mo）。
      if (first && !had_prefix) {
        diags_.error(start, std::string("无法识别的元素，实际遇到 '") + token_kind_name(kind()) + "'");
      } else {
        error_here("期望组件名");
      }
      break;
    }
    first = false;
    d.name = advance().text;
    if (at(TokenKind::LBracket)) d.subscripts = parse_array_subscripts();
    d.mod = parse_modification_opt();
    if (match(TokenKind::Kw_if)) d.condition = parse_expression();
    parse_comment(d.string_comment, d.annotation);
    cc.comps.push_back(std::move(d));
    if (pos_ == before) break;
    // 修饰里的 redeclare 用 component_clause1（单个声明），逗号属于外层实参列表
    if (single_decl) break;
    if (!match(TokenKind::Comma)) break;
  }
  return cc;
}

// 类型名 / 修饰名 / import 名：只接受 IDENT（der/initial/assert/pure 只能在表达式里当函数名，
// 见 parse_cref；`x(initial = 2)` 在 OMC 里是语法错误，SimpleIntegrator4.mo）。
std::vector<Symbol> Parser::parse_name_path(bool& leading_dot) {
  std::vector<Symbol> path;
  leading_dot = match(TokenKind::Dot);
  if (at(TokenKind::Ident)) {
    path.push_back(advance().text);
  } else {
    error_here("期望名字");
    return path;
  }
  while (at(TokenKind::Dot) && peek(1).kind == TokenKind::Ident) {
    advance();  // .
    path.push_back(advance().text);
  }
  return path;
}

Id<Modification> Parser::parse_modification_opt() {
  if (!at(TokenKind::LParen) && !at(TokenKind::Equal) && !at(TokenKind::Assign))
    return Id<Modification>{};
  Modification m;
  if (at(TokenKind::LParen)) {
    Id<Modification> inner = parse_class_modification();
    m.args = out_.mods[inner].args;  // class_modification 只有 args，合并到当前层
  }
  if (match(TokenKind::Equal)) {
    m.binding = parse_modification_expression();
  } else if (match(TokenKind::Assign)) {
    m.assign = true;
    m.binding = parse_modification_expression();
  }
  return out_.add_mod(std::move(m));
}

// modification_expression = expression | break（MLS 3.6 §7.2）
Id<Exp> Parser::parse_modification_expression() {
  if (at(TokenKind::Kw_break)) {
    Exp e{}; e.kind = ExpKind::Break; e.loc = advance().loc;
    return mk(std::move(e));
  }
  return parse_expression();
}

void Parser::parse_redeclare_in_modification(ModArg& arg) {
  // element_redeclaration = redeclare [each] [final] (short_class_def | component_clause1 | element_replaceable)
  // element_modification_or_replaceable = [each] [final] (element_modification | element_replaceable)
  Element el;
  el.loc = peek().loc;
  el.is_redeclare = match(TokenKind::Kw_redeclare);
  arg.each_ = match(TokenKind::Kw_each);
  arg.final_ = match(TokenKind::Kw_final);
  el.is_final = arg.final_;
  el.is_replaceable = match(TokenKind::Kw_replaceable);
  arg.is_redeclare = true;
  if (is_class_prefix_start(kind())) {
    el.kind = ElementKind::ClassDef;
    el.cls = parse_class_definition(/*is_final=*/false);  // final 记在 Element 上
  } else {
    el.kind = ElementKind::Component;
    el.comp = parse_component_clause(/*single_decl=*/true);
  }
  parse_constraining_clause_opt(el);
  arg.redecl = out_.add_elem(std::move(el));
}

Id<Modification> Parser::parse_class_modification() {
  trace("class_modification");
  Modification m;
  expect(TokenKind::LParen, "期望 '('");
  if (!at(TokenKind::RParen)) {
    for (;;) {
      std::size_t before = pos_;
      ModArg arg;
      // 先看是不是 redeclare/replaceable 元素修饰（each/final 可能在前）
      std::size_t k = 0;
      if (kind(k) == TokenKind::Kw_redeclare) ++k;
      if (kind(k) == TokenKind::Kw_each) ++k;
      if (kind(k) == TokenKind::Kw_final) ++k;
      bool is_redecl = (kind() == TokenKind::Kw_redeclare) || kind(k) == TokenKind::Kw_replaceable;
      if (is_redecl) {
        parse_redeclare_in_modification(arg);
      } else if (match(TokenKind::Kw_break)) {  // break name | break connect(a, b)
        arg.is_break = true;
        if (match(TokenKind::Kw_connect)) {
          expect(TokenKind::LParen, "break connect 后应为 '('");
          arg.break_a = parse_expression();
          expect(TokenKind::Comma, "break connect 两个引脚间应有 ','");
          arg.break_b = parse_expression();
          expect(TokenKind::RParen, "break connect 应以 ')' 结束");
        } else {
          bool ld = false;
          arg.path = parse_name_path(ld);
        }
      } else {
        arg.each_ = match(TokenKind::Kw_each);
        arg.final_ = match(TokenKind::Kw_final);
        bool ld = false;
        arg.path = parse_name_path(ld);
        if (at(TokenKind::LParen)) arg.sub = parse_class_modification();
        if (match(TokenKind::Equal)) {
          arg.binding = parse_modification_expression();
        } else if (match(TokenKind::Assign)) {
          arg.assign = true;
          arg.binding = parse_modification_expression();
        }
        arg.string_comment = parse_string_comment_opt();
      }
      m.args.push_back(std::move(arg));
      if (pos_ == before) {  // 进度保证
        error_here("无法解析的修饰参数");
        break;
      }
      if (!match(TokenKind::Comma)) break;
    }
  }
  expect(TokenKind::RParen, "修饰应以 ')' 结束");
  return out_.add_mod(std::move(m));
}

Symbol Parser::parse_string_comment_opt() {
  if (!at(TokenKind::StringLit)) return Symbol{};
  std::string s(out_.interner.str(advance().text));
  while (at(TokenKind::Plus) && peek(1).kind == TokenKind::StringLit) {
    advance();  // +
    s += out_.interner.str(advance().text);
  }
  return out_.interner.intern(s);
}

Id<Modification> Parser::parse_annotation_opt() {
  if (!at(TokenKind::Kw_annotation)) return Id<Modification>{};
  advance();
  if (!at(TokenKind::LParen)) {
    error_here("annotation 后应为 '('");
    return Id<Modification>{};
  }
  return parse_class_modification();
}

// 类级 annotation 可以出现多次（元素间、段间、末尾）；OMC 用 AbsynUtil.mergeAnnotationsList 合并成一条
// （AbsynToSCode.mo:1498-1503）。这里按出现顺序把参数追加到同一个 Modification 里。
void Parser::merge_class_annotation(ClassDef& cls, Id<Modification> ann) {
  if (!ann.is_valid()) return;
  if (!cls.annotation.is_valid()) {
    cls.annotation = ann;
    return;
  }
  std::vector<ModArg> more = out_.mods[ann].args;  // 先拷贝，避免引用跨越 arena 访问
  for (ModArg& a : more) out_.mods[cls.annotation].args.push_back(std::move(a));
}

void Parser::parse_comment(Symbol& str, Id<Modification>& ann) {
  str = parse_string_comment_opt();
  ann = parse_annotation_opt();
}

// -------------------------------------------------------------- 方程
void Parser::parse_equation_list(std::vector<Id<Equation>>& out) {
  while (!is_list_end(kind())) {
    std::size_t before = pos_;
    Id<Equation> e = parse_equation();
    if (e.is_valid()) out.push_back(e);
    if (pos_ == before) advance();  // 进度保证
  }
}

Id<Equation> Parser::parse_equation() {
  trace("equation");
  Equation eq;
  eq.loc = peek().loc;
  switch (kind()) {
    case TokenKind::Kw_if: {
      advance();
      eq.kind = EqKind::If;
      EqBranch b;
      b.cond = parse_expression();
      expect(TokenKind::Kw_then, "if 条件后应为 then");
      parse_equation_list(b.body);
      eq.branches.push_back(std::move(b));
      while (match(TokenKind::Kw_elseif)) {
        EqBranch bi;
        bi.cond = parse_expression();
        expect(TokenKind::Kw_then, "elseif 条件后应为 then");
        parse_equation_list(bi.body);
        eq.branches.push_back(std::move(bi));
      }
      if (match(TokenKind::Kw_else)) {
        EqBranch be;
        parse_equation_list(be.body);
        eq.branches.push_back(std::move(be));
      }
      {
        SourceLoc end_loc = peek().loc;  // OMC 把"缺 end if"报在 end 记号上（IfElseIf.mo 11:1）
        expect(TokenKind::Kw_end, "if 方程应以 end if 结束");
        if (!match(TokenKind::Kw_if)) {
          bool nested_else_if = !eq.branches.empty() && !eq.branches.back().cond.is_valid() &&
                                !eq.branches.back().body.empty() &&
                                out_.eqs[eq.branches.back().body.back()].kind == EqKind::If;
          diags_.error(end_loc, nested_else_if
                                    ? "Expected 'end if'; did you use a nested 'else if' instead of 'elseif'?"
                                    : "Expected 'end if'");
        }
      }
      break;
    }
    case TokenKind::Kw_for: {
      advance();
      eq.kind = EqKind::For;
      eq.iters = parse_for_indices();
      expect(TokenKind::Kw_loop, "for 迭代器后应为 loop");
      parse_equation_list(eq.body);
      expect(TokenKind::Kw_end, "for 方程应以 end for 结束");
      expect(TokenKind::Kw_for, "end 后应为 for");
      break;
    }
    case TokenKind::Kw_when: {
      advance();
      eq.kind = EqKind::When;
      EqBranch b;
      b.cond = parse_expression();
      expect(TokenKind::Kw_then, "when 条件后应为 then");
      parse_equation_list(b.body);
      eq.branches.push_back(std::move(b));
      while (match(TokenKind::Kw_elsewhen)) {
        EqBranch bi;
        bi.cond = parse_expression();
        expect(TokenKind::Kw_then, "elsewhen 条件后应为 then");
        parse_equation_list(bi.body);
        eq.branches.push_back(std::move(bi));
      }
      expect(TokenKind::Kw_end, "when 方程应以 end when 结束");
      expect(TokenKind::Kw_when, "end 后应为 when");
      break;
    }
    case TokenKind::Kw_connect: {
      advance();
      eq.kind = EqKind::Connect;
      expect(TokenKind::LParen, "connect 后应为 '('");
      eq.lhs = parse_expression();
      expect(TokenKind::Comma, "connect 两个引脚间应有 ','");
      eq.rhs = parse_expression();
      expect(TokenKind::RParen, "connect 应以 ')' 结束");
      break;
    }
    default: {
      Id<Exp> lhs = parse_simple_expression();
      if (match(TokenKind::Equal)) {
        eq.kind = EqKind::Equality;
        eq.lhs = lhs;
        eq.rhs = parse_expression();
      } else if (at(TokenKind::Assign)) {  // OMC Modelica.g 的语法层检查，位置在 ':='
        error_here("Equations can not contain assignments (':='), use equality ('=') instead");
        advance();
        eq.kind = EqKind::Equality;  // 按等式恢复，继续解析右侧
        eq.lhs = lhs;
        eq.rhs = parse_expression();
      } else {
        eq.kind = EqKind::FunctionCall;  // assert(...)、Connections.branch(...) 等
        eq.lhs = lhs;
        if (out_.exps[lhs].kind != ExpKind::Call)  // ParseError3.mo：`assert;`
          diags_.error(eq.loc, "A singleton expression in an equation section is required to be a function call");
      }
      break;
    }
  }
  parse_comment(eq.string_comment, eq.annotation);
  expect(TokenKind::Semicolon, "方程后应有 ';'");
  return out_.add_eq(std::move(eq));
}

// -------------------------------------------------------------- 语句
void Parser::parse_statement_list(std::vector<Id<Statement>>& out) {
  while (!is_list_end(kind())) {
    std::size_t before = pos_;
    Id<Statement> s = parse_statement();
    if (s.is_valid()) out.push_back(s);
    if (pos_ == before) advance();  // 进度保证
  }
}

std::vector<ForIndex> Parser::parse_for_indices() {
  std::vector<ForIndex> iters;
  for (;;) {
    ForIndex fi;
    if (!at(TokenKind::Ident)) {
      error_here("for 迭代器期望名字");
      break;
    }
    fi.name = advance().text;
    if (match(TokenKind::Kw_in)) fi.range = parse_expression();
    iters.push_back(fi);
    if (!match(TokenKind::Comma)) break;
  }
  return iters;
}

Id<Statement> Parser::parse_statement() {
  trace("statement");
  Statement st;
  st.loc = peek().loc;
  switch (kind()) {
    case TokenKind::Kw_if: {
      advance();
      st.kind = StmtKind::If;
      StmtBranch b;
      b.cond = parse_expression();
      expect(TokenKind::Kw_then, "if 条件后应为 then");
      parse_statement_list(b.body);
      st.branches.push_back(std::move(b));
      while (match(TokenKind::Kw_elseif)) {
        StmtBranch bi;
        bi.cond = parse_expression();
        expect(TokenKind::Kw_then, "elseif 条件后应为 then");
        parse_statement_list(bi.body);
        st.branches.push_back(std::move(bi));
      }
      if (match(TokenKind::Kw_else)) {
        StmtBranch be;
        parse_statement_list(be.body);
        st.branches.push_back(std::move(be));
      }
      {
        SourceLoc end_loc = peek().loc;
        expect(TokenKind::Kw_end, "if 语句应以 end if 结束");
        if (!match(TokenKind::Kw_if)) {
          bool nested_else_if = !st.branches.empty() && !st.branches.back().cond.is_valid() &&
                                !st.branches.back().body.empty() &&
                                out_.stmts[st.branches.back().body.back()].kind == StmtKind::If;
          diags_.error(end_loc, nested_else_if
                                    ? "Expected 'end if'; did you use a nested 'else if' instead of 'elseif'?"
                                    : "Expected 'end if'");
        }
      }
      break;
    }
    case TokenKind::Kw_for: {
      advance();
      st.kind = StmtKind::For;
      st.iters = parse_for_indices();
      expect(TokenKind::Kw_loop, "for 迭代器后应为 loop");
      parse_statement_list(st.body);
      expect(TokenKind::Kw_end, "for 语句应以 end for 结束");
      expect(TokenKind::Kw_for, "end 后应为 for");
      break;
    }
    case TokenKind::Kw_while: {
      advance();
      st.kind = StmtKind::While;
      StmtBranch b;
      b.cond = parse_expression();
      expect(TokenKind::Kw_loop, "while 条件后应为 loop");
      parse_statement_list(b.body);
      st.branches.push_back(std::move(b));
      expect(TokenKind::Kw_end, "while 语句应以 end while 结束");
      expect(TokenKind::Kw_while, "end 后应为 while");
      break;
    }
    case TokenKind::Kw_when: {
      advance();
      st.kind = StmtKind::When;
      StmtBranch b;
      b.cond = parse_expression();
      expect(TokenKind::Kw_then, "when 条件后应为 then");
      parse_statement_list(b.body);
      st.branches.push_back(std::move(b));
      while (match(TokenKind::Kw_elsewhen)) {
        StmtBranch bi;
        bi.cond = parse_expression();
        expect(TokenKind::Kw_then, "elsewhen 条件后应为 then");
        parse_statement_list(bi.body);
        st.branches.push_back(std::move(bi));
      }
      expect(TokenKind::Kw_end, "when 语句应以 end when 结束");
      expect(TokenKind::Kw_when, "end 后应为 when");
      break;
    }
    case TokenKind::Kw_break:
      advance();
      st.kind = StmtKind::Break;
      break;
    case TokenKind::Kw_return:
      advance();
      st.kind = StmtKind::Return;
      break;
    case TokenKind::LParen: {  // ( output_expression_list ) := function_call
      st.kind = StmtKind::Assign;
      st.lhs = parse_primary();
      expect(TokenKind::Assign, "元组左值后应为 ':='");
      st.rhs = parse_expression();
      if (out_.exps[st.rhs].kind != ExpKind::Call)  // Assign3.mo：`(x,y,z) := res;`，报在语句起点
        diags_.error(st.loc, "Modelica assignment statements are either on the form "
                             "'component_reference := expression' or '( output_expression_list ) := function_call'");
      break;
    }
    default: {
      if (!at_name() && !at(TokenKind::Dot)) {  // Assign2.mo：`{x,y,z} := ...`，报在语句起点
        error_here("Modelica assignment statements are either on the form "
                   "'component_reference := expression' or '( output_expression_list ) := function_call'");
        return Id<Statement>{};
      }
      Id<Exp> e = parse_cref_or_call();
      if (match(TokenKind::Assign)) {
        st.kind = StmtKind::Assign;
        st.lhs = e;
        st.rhs = parse_expression();
      } else {
        st.kind = StmtKind::Call;
        st.lhs = e;
        if (at(TokenKind::Equal))  // Assign1.mo：算法段里写了等式，OMC 报在 '='
          error_here("Algorithms can not contain equations ('='), use assignments (':=') instead");
        else if (out_.exps[e].kind != ExpKind::Call)
          error_here("语句应为赋值或函数调用");
      }
      break;
    }
  }
  parse_comment(st.string_comment, st.annotation);
  expect(TokenKind::Semicolon, "语句后应有 ';'");
  return out_.add_stmt(std::move(st));
}

// -------------------------------------------------------------- 表达式
Id<Exp> Parser::parse_expression() {
  if (!at(TokenKind::Kw_if)) return parse_simple_expression();
  // if c1 then t1 (elseif ci then ti)* else e  → 右嵌套 IfExp
  std::vector<std::pair<Id<Exp>, Id<Exp>>> clauses;
  SourceLoc loc = peek().loc;
  advance();  // if
  Id<Exp> c = parse_expression();
  expect(TokenKind::Kw_then, "if 条件后应为 then");
  Id<Exp> t = parse_expression();
  clauses.emplace_back(c, t);
  while (match(TokenKind::Kw_elseif)) {
    Id<Exp> ci = parse_expression();
    expect(TokenKind::Kw_then, "elseif 条件后应为 then");
    Id<Exp> ti = parse_expression();
    clauses.emplace_back(ci, ti);
  }
  expect(TokenKind::Kw_else, "if 表达式应有 else");
  Id<Exp> acc = parse_expression();
  for (auto it = clauses.rbegin(); it != clauses.rend(); ++it) {
    Exp e;
    e.kind = ExpKind::IfExp;
    e.loc = loc;
    e.a = it->first;
    e.b = it->second;
    e.c = acc;
    e.is_elseif = (it + 1 != clauses.rend());  // 除第一个 if 外都是 elseif 分支（OMC IFEXP.elseIfBranch）
    acc = mk(std::move(e));
  }
  return acc;
}

Id<Exp> Parser::parse_simple_expression() {
  Id<Exp> a = parse_logical_expression();
  if (at(TokenKind::Colon)) {
    Exp r;
    r.kind = ExpKind::Range;
    r.loc = peek().loc;
    advance();
    r.a = a;
    r.b = parse_logical_expression();
    if (match(TokenKind::Colon)) r.c = parse_logical_expression();  // a:step:b
    return mk(std::move(r));
  }
  return a;
}

Id<Exp> Parser::parse_logical_expression() {
  Id<Exp> a = parse_logical_term();
  while (at(TokenKind::Kw_or)) {
    SourceLoc loc = advance().loc;
    Id<Exp> b = parse_logical_term();
    Exp e{}; e.kind = ExpKind::Binary; e.loc = loc; e.op = Op::Or; e.a = a; e.b = b;
    a = mk(std::move(e));
  }
  return a;
}

Id<Exp> Parser::parse_logical_term() {
  Id<Exp> a = parse_logical_factor();
  while (at(TokenKind::Kw_and)) {
    SourceLoc loc = advance().loc;
    Id<Exp> b = parse_logical_factor();
    Exp e{}; e.kind = ExpKind::Binary; e.loc = loc; e.op = Op::And; e.a = a; e.b = b;
    a = mk(std::move(e));
  }
  return a;
}

Id<Exp> Parser::parse_logical_factor() {
  if (at(TokenKind::Kw_not)) {
    SourceLoc loc = advance().loc;
    Id<Exp> a = parse_relation();
    Exp e{}; e.kind = ExpKind::Unary; e.loc = loc; e.op = Op::Not; e.a = a;
    return mk(std::move(e));
  }
  return parse_relation();
}

Id<Exp> Parser::parse_relation() {
  Id<Exp> a = parse_arithmetic();
  Op op;
  switch (kind()) {
    case TokenKind::Less: op = Op::Lt; break;
    case TokenKind::LessEq: op = Op::Le; break;
    case TokenKind::Greater: op = Op::Gt; break;
    case TokenKind::GreaterEq: op = Op::Ge; break;
    case TokenKind::EqEq: op = Op::Eq; break;
    case TokenKind::NotEq: op = Op::Ne; break;
    default: return a;
  }
  SourceLoc loc = advance().loc;
  Id<Exp> b = parse_arithmetic();
  Exp e{}; e.kind = ExpKind::Binary; e.loc = loc; e.op = op; e.a = a; e.b = b;
  return mk(std::move(e));
}

Id<Exp> Parser::parse_arithmetic() {
  Id<Exp> a;
  if (at(TokenKind::Plus) || at(TokenKind::Minus) || at(TokenKind::DotPlus) ||
      at(TokenKind::DotMinus)) {
    Op op = (at(TokenKind::Plus) || at(TokenKind::DotPlus)) ? Op::UPlus : Op::UMinus;
    SourceLoc loc = advance().loc;
    Id<Exp> t = parse_term();
    Exp e{}; e.kind = ExpKind::Unary; e.loc = loc; e.op = op; e.a = t;
    a = mk(std::move(e));
  } else {
    a = parse_term();
  }
  for (;;) {
    Op op;
    switch (kind()) {
      case TokenKind::Plus: op = Op::Add; break;
      case TokenKind::Minus: op = Op::Sub; break;
      case TokenKind::DotPlus: op = Op::EWAdd; break;
      case TokenKind::DotMinus: op = Op::EWSub; break;
      default: return a;
    }
    SourceLoc loc = advance().loc;
    Id<Exp> b = parse_term();
    Exp e{}; e.kind = ExpKind::Binary; e.loc = loc; e.op = op; e.a = a; e.b = b;
    a = mk(std::move(e));
  }
}

Id<Exp> Parser::parse_term() {
  Id<Exp> a = parse_factor();
  for (;;) {
    Op op;
    switch (kind()) {
      case TokenKind::Star: op = Op::Mul; break;
      case TokenKind::Slash: op = Op::Div; break;
      case TokenKind::DotStar: op = Op::EWMul; break;
      case TokenKind::DotSlash: op = Op::EWDiv; break;
      default: return a;
    }
    SourceLoc loc = advance().loc;
    Id<Exp> b = parse_factor();
    Exp e{}; e.kind = ExpKind::Binary; e.loc = loc; e.op = op; e.a = a; e.b = b;
    a = mk(std::move(e));
  }
}

Id<Exp> Parser::parse_factor() {
  Id<Exp> a = parse_primary();
  if (at(TokenKind::Caret) || at(TokenKind::DotCaret)) {
    Op op = at(TokenKind::Caret) ? Op::Pow : Op::EWPow;
    SourceLoc loc = advance().loc;
    // MLS: factor = primary [ (^ | .^) primary ]，右操作数是 primary（可带一元负号需加括号）
    Id<Exp> b = parse_primary();
    Exp e{}; e.kind = ExpKind::Binary; e.loc = loc; e.op = op; e.a = a; e.b = b;
    a = mk(std::move(e));
  }
  return a;
}

Id<Exp> Parser::parse_primary() {
  SourceLoc loc = peek().loc;
  switch (kind()) {
    case TokenKind::IntLit: {
      std::string s(out_.interner.str(peek().text));
      advance();
      Exp e{}; e.kind = ExpKind::IntLit; e.loc = loc;
      e.ival = std::strtoll(s.c_str(), nullptr, 10);
      return mk(std::move(e));
    }
    case TokenKind::RealLit: {
      Symbol text = peek().text;
      std::string s(out_.interner.str(text));
      advance();
      Exp e{}; e.kind = ExpKind::RealLit; e.loc = loc;
      e.rval = std::strtod(s.c_str(), nullptr);
      e.sval = text;  // 源码拼写（OMC Absyn.REAL 存 String，Absyn.mo:713-715：unparse 不改用户写法）
      return mk(std::move(e));
    }
    case TokenKind::StringLit: {
      Symbol s = advance().text;
      Exp e{}; e.kind = ExpKind::StringLit; e.loc = loc; e.sval = s;
      return mk(std::move(e));
    }
    case TokenKind::Kw_true:
    case TokenKind::Kw_false: {
      bool v = at(TokenKind::Kw_true);
      advance();
      Exp e{}; e.kind = ExpKind::BoolLit; e.loc = loc; e.bval = v;
      return mk(std::move(e));
    }
    case TokenKind::Kw_end: {
      advance();
      Exp e{}; e.kind = ExpKind::End; e.loc = loc;
      return mk(std::move(e));
    }
    case TokenKind::LParen: {
      advance();
      if (at(TokenKind::RParen)) {  // 空元组 ()
        advance();
        Exp e{}; e.kind = ExpKind::Tuple; e.loc = loc;
        return parse_postfix(mk(std::move(e)));
      }
      // 元组元素：空位（, 或 ) 之前没有表达式）用空 Tuple 节点占位，如 (, x) := f()
      auto element = [&]() -> Id<Exp> {
        if (at(TokenKind::Comma) || at(TokenKind::RParen)) {
          Exp hole{}; hole.kind = ExpKind::Tuple; hole.loc = peek().loc;
          return mk(std::move(hole));
        }
        return parse_expression();
      };
      Id<Exp> first = element();
      if (at(TokenKind::Comma)) {  // 元组 (a, b, ...)
        Exp e{}; e.kind = ExpKind::Tuple; e.loc = loc; e.args.push_back(first);
        while (match(TokenKind::Comma)) e.args.push_back(element());
        expect(TokenKind::RParen, "元组应以 ')' 结束");
        return parse_postfix(mk(std::move(e)));
      }
      expect(TokenKind::RParen, "括号表达式应以 ')' 结束");
      // (e)[subs] → Subscript(e)（OMC SUBSCRIPTED_EXP，Modelica.g:1851）；否则括号入树为单元素元组
      //（OMC Modelica.g:1856-1861 把 (e) 解析成 TUPLE({e})，其 dump 不按优先级补括号，只打印源码里的）
      if (at(TokenKind::LBracket)) return parse_postfix(first);
      Exp e{}; e.kind = ExpKind::Tuple; e.loc = loc; e.args.push_back(first);
      return mk(std::move(e));
    }
    case TokenKind::LBrace: {
      advance();
      Exp e{}; e.kind = ExpKind::Array; e.loc = loc;
      if (at(TokenKind::RBrace)) {  // OMC 在语法层拒绝 `{}`（EmptyArray.mo），位置在 '{'
        diags_.error(loc, "Empty array constructors are not valid in Modelica.");
      } else {
        std::vector<Symbol> names;
        parse_function_args(e.args, names);
      }
      expect(TokenKind::RBrace, "数组构造应以 '}' 结束");
      return parse_postfix(mk(std::move(e)));
    }
    case TokenKind::LBracket: {
      advance();
      Exp m{}; m.kind = ExpKind::Matrix; m.loc = loc;
      if (!at(TokenKind::RBracket)) {
        for (;;) {
          std::size_t before = pos_;
          Exp row{}; row.kind = ExpKind::Array; row.loc = peek().loc;
          row.args = parse_expression_list();
          m.args.push_back(mk(std::move(row)));
          if (pos_ == before) break;
          if (!match(TokenKind::Semicolon)) break;
        }
      }
      expect(TokenKind::RBracket, "矩阵应以 ']' 结束");
      return parse_postfix(mk(std::move(m)));
    }
    case TokenKind::Ident:
    case TokenKind::Dot:
    case TokenKind::Kw_der:
    case TokenKind::Kw_initial:
    case TokenKind::Kw_pure:
      return parse_cref_or_call();
    case TokenKind::Kw_function: {  // 部分函数应用 function f(a = 1)（MLS §12.4.2）
      advance();
      Id<Exp> e = parse_cref_or_call();
      if (out_.exps[e].kind != ExpKind::Call) error_here("function 后应为函数调用");
      out_.exps[e].partial_fn = true;
      return e;
    }
    default:
      error_here("期望表达式");
      advance();
      Exp e{}; e.kind = ExpKind::IntLit; e.loc = loc; e.ival = 0;
      return mk(std::move(e));
  }
}

Id<Exp> Parser::parse_cref_or_call() {
  SourceLoc loc = peek().loc;
  Id<Cref> cref = parse_cref();
  if (at(TokenKind::LParen)) {
    advance();
    Exp e{}; e.kind = ExpKind::Call; e.loc = loc; e.cref = cref;
    if (!at(TokenKind::RParen)) parse_function_args(e.args, e.arg_names);
    expect(TokenKind::RParen, "函数调用应以 ')' 结束");
    if (at(TokenKind::Dot) && peek(1).kind == TokenKind::Ident) {  // f(i).y[...]（OMC 接受）
      advance();  // .
      e.call_suffix = parse_cref();
    }
    return parse_postfix(mk(std::move(e)));
  }
  Exp e{}; e.kind = ExpKind::CrefExp; e.loc = loc; e.cref = cref;
  return mk(std::move(e));  // cref 自己的下标已在 parse_cref 里
}

// primary 后面的下标链：({1,2,3})[2]、f(x)[1]、[1,2;3,4][1,2]
Id<Exp> Parser::parse_postfix(Id<Exp> base) {
  while (at(TokenKind::LBracket)) {
    Exp s{}; s.kind = ExpKind::Subscript; s.loc = peek().loc; s.a = base;
    s.args = parse_array_subscripts();
    base = mk(std::move(s));
  }
  return base;
}

Id<Cref> Parser::parse_cref() {
  Cref c;
  c.leading_dot = match(TokenKind::Dot);
  for (;;) {
    CrefPart part;
    if (at_name()) {
      part.name = advance().text;
    } else {
      error_here("期望组件名");
      break;
    }
    if (at(TokenKind::LBracket)) part.subscripts = parse_array_subscripts();
    c.parts.push_back(std::move(part));
    if (at(TokenKind::Dot) && peek(1).kind == TokenKind::Ident) {
      advance();  // .
    } else {
      break;
    }
  }
  return out_.add_cref(std::move(c));
}

std::vector<Id<Exp>> Parser::parse_array_subscripts() {
  std::vector<Id<Exp>> subs;
  expect(TokenKind::LBracket, "期望 '['");
  if (!at(TokenKind::RBracket)) {
    for (;;) {
      if (at(TokenKind::Colon)) {
        SourceLoc loc = advance().loc;
        Exp e{}; e.kind = ExpKind::Colon; e.loc = loc;
        subs.push_back(mk(std::move(e)));
      } else {
        subs.push_back(parse_expression());
      }
      if (!match(TokenKind::Comma)) break;
    }
  }
  expect(TokenKind::RBracket, "下标应以 ']' 结束");
  return subs;
}

void Parser::parse_function_args(std::vector<Id<Exp>>& args, std::vector<Symbol>& names) {
  for (;;) {
    std::size_t before = pos_;
    if (at(TokenKind::Ident) && peek(1).kind == TokenKind::Equal) {  // 命名实参
      names.push_back(advance().text);
      advance();  // =
      args.push_back(parse_expression());
    } else {
      Id<Exp> a = parse_expression();
      if (at(TokenKind::Kw_for)) {  // 归约：expr for iters —— 是唯一实参
        Exp r{}; r.kind = ExpKind::Reduction; r.loc = peek().loc; r.a = a;
        advance();  // for
        r.iters = parse_for_indices();
        args.push_back(mk(std::move(r)));
        names.push_back(Symbol{});
        return;
      }
      args.push_back(a);
      names.push_back(Symbol{});
    }
    if (pos_ == before) {  // 进度保证
      error_here("无法解析的函数实参");
      break;
    }
    if (!match(TokenKind::Comma)) break;
  }
}

std::vector<Id<Exp>> Parser::parse_expression_list() {
  std::vector<Id<Exp>> v;
  for (;;) {
    std::size_t before = pos_;
    v.push_back(parse_expression());
    if (pos_ == before) break;
    if (!match(TokenKind::Comma)) break;
  }
  return v;
}

}  // namespace ymc
