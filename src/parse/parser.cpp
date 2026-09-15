// 递归下降解析器实现（方案 §5 S1）。
#include "parse/parser.h"

#include <cstdlib>
#include <string>

namespace ymc {

namespace {
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
}  // namespace

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

// -------------------------------------------------------------- 顶层
void Parser::parse_program() {
  if (at(TokenKind::Kw_within)) {
    advance();
    out_.has_within = true;
    if (!at(TokenKind::Semicolon))
      out_.within_path = parse_name_path(out_.within_leading_dot);
    expect(TokenKind::Semicolon, "within 子句后应有 ';'");
  }
  while (!at(TokenKind::Eof)) {
    std::size_t before = pos_;
    bool is_final = match(TokenKind::Kw_final);
    Id<ClassDef> id = parse_class_definition(is_final);
    if (id.is_valid()) out_.top.push_back(id);
    match(TokenKind::Semicolon);  // 顶层类定义之间的 ';'
    if (pos_ == before && !at(TokenKind::Eof)) advance();  // 进度保证：防死循环
  }
}

static bool set_restriction(Parser*, TokenKind k, Restriction& r) {
  switch (k) {
    case TokenKind::Kw_class: r = Restriction::Class; return true;
    case TokenKind::Kw_model: r = Restriction::Model; return true;
    case TokenKind::Kw_record: r = Restriction::Record; return true;
    case TokenKind::Kw_block: r = Restriction::Block; return true;
    case TokenKind::Kw_connector: r = Restriction::Connector; return true;
    case TokenKind::Kw_type: r = Restriction::Type; return true;
    case TokenKind::Kw_package: r = Restriction::Package; return true;
    case TokenKind::Kw_function: r = Restriction::Function; return true;
    default: return false;
  }
}

Id<ClassDef> Parser::parse_class_definition(bool is_final) {
  ClassDef cls;
  cls.loc = peek().loc;
  cls.is_final = is_final;
  cls.is_encapsulated = match(TokenKind::Kw_encapsulated);
  cls.is_partial = match(TokenKind::Kw_partial);

  // class_prefix：处理 expandable connector / operator record / [pure|impure] [operator] function
  if (match(TokenKind::Kw_expandable)) {
    if (expect(TokenKind::Kw_connector, "expandable 后应为 connector"))
      cls.restriction = Restriction::ExpandableConnector;
  } else if (match(TokenKind::Kw_operator)) {
    if (match(TokenKind::Kw_record)) cls.restriction = Restriction::OperatorRecord;
    else if (match(TokenKind::Kw_function)) cls.restriction = Restriction::Function;
    else cls.restriction = Restriction::Operator;
  } else {
    match(TokenKind::Kw_pure);
    match(TokenKind::Kw_impure);
    match(TokenKind::Kw_operator);
    Restriction r = Restriction::Class;
    if (!set_restriction(this, kind(), r)) {
      error_here("期望类前缀（model/class/record/...）");
      sync_to_semicolon();
      return Id<ClassDef>{};
    }
    advance();
    cls.restriction = r;
  }

  if (!at(TokenKind::Ident)) {
    error_here("类前缀后应为类名");
    sync_to_semicolon();
    return Id<ClassDef>{};
  }
  cls.name = advance().text;

  // 短类定义 `Name = ...` v1 不支持
  if (at(TokenKind::Equal)) {
    diags_.report(DiagLevel::Warning, peek().loc, "短类定义（= ...）S1 v1 尚未支持，已跳过");
    sync_to_semicolon();
    ClassDef stub = cls;
    return out_.add_class(std::move(stub));
  }

  cls.string_comment = parse_string_comment_opt();
  parse_composition(cls);

  expect(TokenKind::Kw_end, "类体后应为 'end'");
  if (at(TokenKind::Ident)) advance();  // end 后的类名（不强制一致，v1）
  return out_.add_class(std::move(cls));
}

void Parser::parse_composition(ClassDef& cls) {
  parse_element_list(cls, /*protected=*/false);
  for (;;) {
    switch (kind()) {
      case TokenKind::Kw_public:
        advance();
        parse_element_list(cls, false);
        break;
      case TokenKind::Kw_protected:
        advance();
        parse_element_list(cls, true);
        break;
      case TokenKind::Kw_equation:
        advance();
        parse_equation_section(cls, false);
        break;
      case TokenKind::Kw_initial:
        advance();
        if (match(TokenKind::Kw_equation)) {
          parse_equation_section(cls, true);
        } else if (match(TokenKind::Kw_algorithm)) {
          diags_.report(DiagLevel::Warning, peek().loc, "initial algorithm 段 S1 v1 尚未支持");
          while (!is_section_boundary(kind())) advance();
        } else {
          error_here("initial 后应为 equation 或 algorithm");
        }
        break;
      case TokenKind::Kw_algorithm:
        advance();
        diags_.report(DiagLevel::Warning, peek().loc, "algorithm 段 S1 v1 尚未支持，已跳过");
        while (!is_section_boundary(kind())) advance();
        break;
      case TokenKind::Kw_external:
        diags_.report(DiagLevel::Warning, peek().loc, "external 段 S1 v1 尚未支持，已跳过");
        advance();  // 先吃掉 external 关键字本身（否则 is_section_boundary 立即为真 → 死循环）
        while (!at(TokenKind::Semicolon) && !at(TokenKind::Kw_end) && !at(TokenKind::Eof))
          advance();
        match(TokenKind::Semicolon);
        break;
      case TokenKind::Kw_annotation:
        skip_annotation_opt();
        match(TokenKind::Semicolon);
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

void Parser::parse_element_list(ClassDef& cls, bool is_protected) {
  while (!is_section_boundary(kind())) {
    std::size_t before = pos_;
    parse_element(cls, is_protected);
    if (pos_ == before) advance();  // 进度保证：防止在无法消费的记号上死循环
  }
}

void Parser::parse_element(ClassDef& cls, bool is_protected) {
  Element el;
  el.loc = peek().loc;
  el.is_protected = is_protected;
  // 前缀（v1：final/inner/outer 记录，redeclare/replaceable 记录但仅告警）
  match(TokenKind::Kw_redeclare);
  el.is_final = match(TokenKind::Kw_final);
  match(TokenKind::Kw_inner);
  match(TokenKind::Kw_outer);
  match(TokenKind::Kw_replaceable);

  if (at(TokenKind::Kw_import)) {
    el.kind = ElementKind::Import;
    el.imp = parse_import();
    expect(TokenKind::Semicolon, "import 子句后应有 ';'");
    cls.elements.push_back(std::move(el));
    return;
  }
  if (at(TokenKind::Kw_extends)) {
    el.kind = ElementKind::Extends;
    el.ext = parse_extends();
    expect(TokenKind::Semicolon, "extends 子句后应有 ';'");
    cls.elements.push_back(std::move(el));
    return;
  }
  // 嵌套类？（class_prefix 打头）
  switch (kind()) {
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
    case TokenKind::Kw_impure: {
      el.kind = ElementKind::ClassDef;
      el.cls = parse_class_definition(el.is_final);
      expect(TokenKind::Semicolon, "嵌套类定义后应有 ';'");
      cls.elements.push_back(std::move(el));
      return;
    }
    default:
      break;
  }
  // 否则：组件子句
  el.kind = ElementKind::Component;
  el.comp = parse_component_clause();
  expect(TokenKind::Semicolon, "组件声明后应有 ';'");
  cls.elements.push_back(std::move(el));
}

Import Parser::parse_import() {
  advance();  // import
  Import imp;
  // import x = A.B.C  |  import A.B.C[.*]
  if (at(TokenKind::Ident) && peek(1).kind == TokenKind::Equal) {
    imp.alias = advance().text;
    advance();  // =
  }
  imp.path = parse_name_path(imp.leading_dot);
  if (match(TokenKind::Dot)) {
    if (match(TokenKind::Star)) imp.wildcard = true;
    else error_here("import ... . 后应为 '*'");
  }
  parse_string_comment_opt();
  skip_annotation_opt();
  return imp;
}

Extends Parser::parse_extends() {
  advance();  // extends
  Extends ext;
  ext.path = parse_name_path(ext.leading_dot);
  if (at(TokenKind::LParen)) ext.mod = parse_class_modification();
  skip_annotation_opt();
  return ext;
}

// -------------------------------------------------------------- 组件 / 类型 / 修饰
bool Parser::parse_type_prefix(TypePrefix& tp) {
  bool any = false;
  for (;;) {
    switch (kind()) {
      case TokenKind::Kw_flow: tp.flow = TypePrefix::Flow::Flow; advance(); any = true; break;
      case TokenKind::Kw_stream: tp.flow = TypePrefix::Flow::Stream; advance(); any = true; break;
      case TokenKind::Kw_discrete:
        tp.var = TypePrefix::Variability::Discrete; advance(); any = true; break;
      case TokenKind::Kw_parameter:
        tp.var = TypePrefix::Variability::Parameter; advance(); any = true; break;
      case TokenKind::Kw_constant:
        tp.var = TypePrefix::Variability::Constant; advance(); any = true; break;
      case TokenKind::Kw_input: tp.caus = TypePrefix::Causality::Input; advance(); any = true; break;
      case TokenKind::Kw_output: tp.caus = TypePrefix::Causality::Output; advance(); any = true; break;
      default:
        return any;
    }
  }
}

ComponentClause Parser::parse_component_clause() {
  ComponentClause cc;
  parse_type_prefix(cc.prefix);
  cc.type_path = parse_name_path(cc.type_leading_dot);
  if (at(TokenKind::LBracket)) cc.type_subscripts = parse_array_subscripts();
  // component_list
  for (;;) {
    ComponentDecl d;
    if (!at(TokenKind::Ident)) {
      error_here("期望组件名");
      break;
    }
    d.name = advance().text;
    if (at(TokenKind::LBracket)) d.subscripts = parse_array_subscripts();
    d.mod = parse_modification_opt();
    // condition attribute `if expr` —— v1 跳过
    if (match(TokenKind::Kw_if)) {
      diags_.report(DiagLevel::Warning, peek().loc, "条件组件（if）S1 v1 尚未支持，条件已忽略");
      (void)parse_expression();
    }
    d.string_comment = parse_string_comment_opt();
    skip_annotation_opt();
    cc.comps.push_back(std::move(d));
    if (!match(TokenKind::Comma)) break;
  }
  return cc;
}

std::vector<Symbol> Parser::parse_name_path(bool& leading_dot) {
  std::vector<Symbol> path;
  leading_dot = match(TokenKind::Dot);
  if (at(TokenKind::Ident) || at(TokenKind::Kw_der) || at(TokenKind::Kw_initial)) {
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
    m.args = out_.mods[inner].args;  // 展开到当前层（class_modification 只有 args）
  }
  if (match(TokenKind::Equal)) {
    m.binding = parse_expression();
  } else if (match(TokenKind::Assign)) {
    m.assign = true;
    m.binding = parse_expression();
  }
  return out_.add_mod(std::move(m));
}

Id<Modification> Parser::parse_class_modification() {
  Modification m;
  expect(TokenKind::LParen, "期望 '('");
  if (!at(TokenKind::RParen)) {
    for (;;) {
      std::size_t before = pos_;
      ModArg arg;
      arg.each_ = match(TokenKind::Kw_each);
      arg.final_ = match(TokenKind::Kw_final);
      if (match(TokenKind::Kw_redeclare) || match(TokenKind::Kw_replaceable)) {
        diags_.report(DiagLevel::Warning, peek().loc, "修饰里的 redeclare/replaceable S1 v1 尚未完整支持");
      }
      bool ld = false;
      arg.path = parse_name_path(ld);
      if (at(TokenKind::LParen)) arg.sub = parse_class_modification();
      if (match(TokenKind::Equal) || match(TokenKind::Assign)) arg.binding = parse_expression();
      arg.string_comment = parse_string_comment_opt();
      m.args.push_back(std::move(arg));
      if (pos_ == before) {  // 进度保证：无法解析的修饰参数
        error_here("无法解析的修饰参数");
        break;
      }
      if (!match(TokenKind::Comma)) break;
    }
  }
  expect(TokenKind::RParen, "修饰应以 ')' 结束");
  return out_.add_mod(std::move(m));
}

// -------------------------------------------------------------- 方程
void Parser::parse_equation_section(ClassDef& cls, bool is_initial) {
  while (!is_section_boundary(kind())) {
    std::size_t before = pos_;
    Equation eq = parse_equation();
    eq.is_initial = is_initial;
    if (eq.lhs.is_valid() || eq.kind == EqKind::Connect) cls.equations.push_back(eq);
    if (pos_ == before) advance();  // 进度保证
  }
}

Equation Parser::parse_equation() {
  Equation eq;
  eq.loc = peek().loc;
  // if/for/when 方程 v1 跳过（深度感知）
  if (at(TokenKind::Kw_if) || at(TokenKind::Kw_for) || at(TokenKind::Kw_when) ||
      at(TokenKind::Kw_while)) {
    diags_.report(DiagLevel::Warning, eq.loc, "if/for/when 方程 S1 v1 尚未支持，已跳过");
    int depth = 0;
    for (;;) {
      TokenKind k = kind();
      if (k == TokenKind::Eof) break;
      if (k == TokenKind::Kw_if || k == TokenKind::Kw_for || k == TokenKind::Kw_when ||
          k == TokenKind::Kw_while) {
        ++depth;
        advance();
      } else if (k == TokenKind::Kw_end) {
        advance();
        if (kind() == TokenKind::Kw_if || kind() == TokenKind::Kw_for ||
            kind() == TokenKind::Kw_when || kind() == TokenKind::Kw_while)
          advance();
        match(TokenKind::Semicolon);
        if (--depth <= 0) break;
      } else if (k == TokenKind::Semicolon && depth == 1) {
        advance();
      } else {
        advance();
      }
    }
    return eq;  // 空 eq，不入表
  }
  if (match(TokenKind::Kw_connect)) {
    eq.kind = EqKind::Connect;
    expect(TokenKind::LParen, "connect 后应为 '('");
    eq.lhs = parse_expression();
    expect(TokenKind::Comma, "connect 两个引脚间应有 ','");
    eq.rhs = parse_expression();
    expect(TokenKind::RParen, "connect 应以 ')' 结束");
    eq.string_comment = parse_string_comment_opt();
    skip_annotation_opt();
    expect(TokenKind::Semicolon, "方程后应有 ';'");
    return eq;
  }
  // simple_expression [ = expression ]
  Id<Exp> lhs = parse_simple_expression();
  if (match(TokenKind::Equal)) {
    eq.kind = EqKind::Equality;
    eq.lhs = lhs;
    eq.rhs = parse_expression();
  } else {
    // 函数调用方程（assert(...)、Connections.branch(...) 等）
    eq.kind = EqKind::FunctionCall;
    eq.lhs = lhs;
  }
  eq.string_comment = parse_string_comment_opt();
  skip_annotation_opt();
  expect(TokenKind::Semicolon, "方程后应有 ';'");
  return eq;
}

// -------------------------------------------------------------- 注释
Symbol Parser::parse_string_comment_opt() {
  if (!at(TokenKind::StringLit)) return Symbol{};
  std::string s(out_.interner.str(advance().text));
  while (match(TokenKind::Plus)) {
    if (at(TokenKind::StringLit)) s += out_.interner.str(advance().text);
    else break;
  }
  return out_.interner.intern(s);
}

void Parser::skip_annotation_opt() {
  if (at(TokenKind::Kw_annotation)) {
    advance();
    if (at(TokenKind::LParen)) (void)parse_class_modification();  // 解析并丢弃
  }
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
  // 一元 + / -
  Id<Exp> a;
  if (at(TokenKind::Plus) || at(TokenKind::Minus)) {
    Op op = at(TokenKind::Plus) ? Op::UPlus : Op::UMinus;
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
    Id<Exp> b = parse_primary();  // ^ 右结合：这里对右操作数再取 primary 足够 v1
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
      std::string s(out_.interner.str(peek().text));
      advance();
      Exp e{}; e.kind = ExpKind::RealLit; e.loc = loc;
      e.rval = std::strtod(s.c_str(), nullptr);
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
      Id<Exp> first = parse_expression();
      if (at(TokenKind::Comma)) {  // 元组 (a, b, ...) —— v1 用 Array 表示
        Exp e{}; e.kind = ExpKind::Array; e.loc = loc; e.args.push_back(first);
        while (match(TokenKind::Comma)) e.args.push_back(parse_expression());
        expect(TokenKind::RParen, "元组应以 ')' 结束");
        return mk(std::move(e));
      }
      expect(TokenKind::RParen, "括号表达式应以 ')' 结束");
      return first;  // 括号本身不入树；unparse 按优先级需要时再补
    }
    case TokenKind::LBrace: {
      advance();
      Exp e{}; e.kind = ExpKind::Array; e.loc = loc;
      if (!at(TokenKind::RBrace)) {
        e.args.push_back(parse_expression());
        while (match(TokenKind::Comma)) e.args.push_back(parse_expression());
      }
      expect(TokenKind::RBrace, "数组构造应以 '}' 结束");
      return mk(std::move(e));
    }
    case TokenKind::LBracket: {
      diags_.report(DiagLevel::Warning, loc, "矩阵 [ ] 表达式 S1 v1 尚未支持，已用空数组占位");
      int depth = 0;
      do {
        if (at(TokenKind::LBracket)) ++depth;
        else if (at(TokenKind::RBracket)) --depth;
        advance();
      } while (depth > 0 && !at(TokenKind::Eof));
      Exp e{}; e.kind = ExpKind::Array; e.loc = loc;
      return mk(std::move(e));
    }
    case TokenKind::Ident:
    case TokenKind::Dot:
    case TokenKind::Kw_der:
    case TokenKind::Kw_initial:
      return parse_cref_or_call();
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
    if (!at(TokenKind::RParen)) e.args = parse_function_args();
    expect(TokenKind::RParen, "函数调用应以 ')' 结束");
    return mk(std::move(e));
  }
  Exp e{}; e.kind = ExpKind::CrefExp; e.loc = loc; e.cref = cref;
  return mk(std::move(e));
}

Id<Cref> Parser::parse_cref() {
  Cref c;
  c.leading_dot = match(TokenKind::Dot);
  for (;;) {
    CrefPart part;
    if (at(TokenKind::Ident) || at(TokenKind::Kw_der) || at(TokenKind::Kw_initial)) {
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

std::vector<Id<Exp>> Parser::parse_function_args() {
  std::vector<Id<Exp>> args;
  for (;;) {
    std::size_t before = pos_;
    // 命名实参 name = expr → 用 Binary(Eq, cref(name), value) 表示（v1 权宜）
    if (at(TokenKind::Ident) && peek(1).kind == TokenKind::Equal) {
      SourceLoc loc = peek().loc;
      Cref c; c.parts.push_back({advance().text, {}});
      Id<Cref> id = out_.add_cref(std::move(c));
      Exp name{}; name.kind = ExpKind::CrefExp; name.loc = loc; name.cref = id;
      advance();  // =
      Exp e{}; e.kind = ExpKind::Binary; e.loc = loc; e.op = Op::Eq;
      e.a = mk(std::move(name)); e.b = parse_expression();
      args.push_back(mk(std::move(e)));
    } else {
      Id<Exp> a = parse_expression();
      if (at(TokenKind::Kw_for)) {  // 归约 expr for i in ... —— v1 忽略 for 部分
        diags_.report(DiagLevel::Warning, peek().loc, "归约表达式（for）S1 v1 尚未支持，已忽略 for 子句");
        int depth = 1;
        advance();
        while (depth > 0 && !at(TokenKind::Eof)) {
          if (at(TokenKind::LParen)) ++depth;
          else if (at(TokenKind::RParen)) { if (--depth == 0) break; }
          advance();
        }
        args.push_back(a);
        return args;
      }
      args.push_back(a);
    }
    if (pos_ == before) {  // 进度保证
      error_here("无法解析的函数实参");
      break;
    }
    if (!match(TokenKind::Comma)) break;
  }
  return args;
}

}  // namespace ymc
