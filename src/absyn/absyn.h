// Absyn：忠实源码的抽象语法树（方案 §4.2、§4.3；对应 OMC FrontEnd/Absyn.mo）。S1 v2。
//
// 设计（方案 §3.4）：表达式/cref/修饰/元素/方程/语句/类定义都放进各自的 Arena，用 Id 互指；
// 不用指针、shared_ptr、variant、继承。表达式是"胖 struct + ExpKind + switch"（§4.3）。
// 复合构造（if/for/when 方程与语句、嵌套类、redeclare 修饰）通过 Id 嵌套，值类型里不递归。
//
// v2 覆盖 MLS 3.6 附录 B 的主体：长/短类定义、枚举、class extends、external 段、
// algorithm 段（全部语句）、if/for/when 方程、矩阵/元组/归约表达式、命名实参、
// redeclare/replaceable/constrainedby、条件组件、注解（保留）。
// 尚未覆盖（给诊断）：der 类定义（class X = der(...)）、纯 MetaModelica 语法。
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "base/arena.h"
#include "base/loc.h"
#include "base/symbol.h"

namespace ymc {

struct Exp;
struct Cref;
struct Modification;
struct Element;
struct Equation;
struct Statement;
struct ClassDef;

// ---- 运算符 ----
enum class Op : std::uint8_t {
  Add, Sub, Mul, Div, Pow,               // + - * / ^
  EWAdd, EWSub, EWMul, EWDiv, EWPow,     // .+ .- .* ./ .^
  Lt, Le, Gt, Ge, Eq, Ne,               // < <= > >= == <>
  And, Or,                               // and or
  UPlus, UMinus, Not,                    // 一元 + - not
};

// ---- 组件引用 cref（也用于类型名路径） ----
struct CrefPart {
  Symbol name;
  std::vector<Id<Exp>> subscripts;   // 空 = 无下标
};
struct Cref {
  bool leading_dot = false;          // .Modelica.xxx 的前导点（全局作用域）
  std::vector<CrefPart> parts;
};

// for 迭代器：for i in 1:n（range 无效 = 隐式范围）
struct ForIndex {
  Symbol name;
  Id<Exp> range;
};

// ---- 表达式 ----
enum class ExpKind : std::uint8_t {
  IntLit, RealLit, BoolLit, StringLit,
  CrefExp,        // 组件引用
  Binary, Unary,  // op 作用于 a(,b)
  Call,           // cref(args)；arg_names 与 args 平行，invalid = 位置实参
  Array,          // { args }
  Matrix,         // [ 行; 行 ]：args 的每个元素是一个 Array 节点当"行"用（不打花括号）
  Tuple,          // ( a, b, ... )
  Range,          // a:b 或 a:b:c（b 为步长时 c 为终点）
  IfExp,          // if a then b else c（elseif 嵌进 c）
  Reduction,      // a for iters（出现在函数实参或数组构造里）
  Colon,          // 下标里的 :
  End,            // 下标里的 end
  Break,          // 修饰值 `= break`（MLS 3.6 §7.2）
  Subscript,      // 任意 primary 后的下标：a[args]，如 ({1,2,3})[2]、f(x)[1]
};

struct Exp {
  ExpKind kind = ExpKind::IntLit;
  SourceLoc loc;
  // 字面量
  std::int64_t ival = 0;
  double rval = 0.0;
  bool bval = false;
  Symbol sval;                     // StringLit：原文（转义序列原样，同 OMC Absyn.STRING）；
                                   // RealLit：源码拼写（OMC Absyn.REAL 存 String，Absyn.mo:713-715）
  bool is_elseif = false;          // IfExp：本节点是上级 if 表达式用 `elseif` 写的分支（OMC IFEXP.elseIfBranch；
                                   // 与 `else if` 区分，dump 才能逐字对齐）
  // 结构
  Id<Cref> cref;                   // CrefExp / Call 的函数名
  Op op = Op::Add;                 // Binary / Unary
  Id<Exp> a, b, c;                 // 见各 kind 注释
  std::vector<Id<Exp>> args;       // Call 实参 / Array 元素 / Matrix 行 / Tuple 元素
  std::vector<Symbol> arg_names;   // Call：命名实参名，与 args 平行
  std::vector<ForIndex> iters;     // Reduction
  bool partial_fn = false;         // Call：`function f(a = 1)` 部分函数应用（MLS §12.4.2）
  Id<Cref> call_suffix;            // Call：调用结果的字段访问 f(i).y（OMC 接受）
};

// ---- 修饰 modification ----
struct ModArg {
  bool each_ = false;
  bool final_ = false;
  bool is_redeclare = false;       // redeclare 元素修饰：只用 redecl
  Id<Element> redecl;
  bool is_break = false;           // `break name` / `break connect(a, b)`（MLS 3.6 §7.3.2）
  Id<Exp> break_a, break_b;        // connect 形式的两个引脚；名字形式用 path
  std::vector<Symbol> path;        // 被修饰的名字（点分）
  Id<Modification> sub;            // 嵌套 (...)，无则 invalid
  Id<Exp> binding;                 // = expr / := expr，无则 invalid
  bool assign = false;
  Symbol string_comment;           // 无则 invalid
};
struct Modification {
  std::vector<ModArg> args;        // 来自 ( ... )
  bool assign = false;             // 顶层绑定用 := 而不是 =
  Id<Exp> binding;                 // 顶层 = / := expr，无则 invalid
};

// ---- 元素 ----
enum class Restriction : std::uint8_t {
  Class, Model, Record, Block, Connector, ExpandableConnector,
  Type, Package, Function, Operator, OperatorRecord, OperatorFunction,
};

enum class Purity : std::uint8_t { None, Pure, Impure };  // [pure|impure] function（MLS §12.3）

struct TypePrefix {
  enum class Variability : std::uint8_t { Continuous, Discrete, Parameter, Constant } var =
      Variability::Continuous;
  enum class Causality : std::uint8_t { None, Input, Output } caus = Causality::None;
  enum class Flow : std::uint8_t { None, Flow, Stream } flow = Flow::None;
};

struct ComponentDecl {
  Symbol name;
  std::vector<Id<Exp>> subscripts;   // 组件数组维度 x[3]
  Id<Modification> mod;              // 修饰 + 绑定，无则 invalid
  Id<Exp> condition;                 // 条件组件 `if expr`，无则 invalid
  Symbol string_comment;             // 无则 invalid
  Id<Modification> annotation;       // 无则 invalid
};

struct ComponentClause {
  TypePrefix prefix;
  bool type_leading_dot = false;
  std::vector<Symbol> type_path;     // 类型名（点分）
  std::vector<Id<Exp>> type_subscripts;  // 类型级数组 Real[3] x
  std::vector<ComponentDecl> comps;  // component_list
};

struct Extends {
  bool leading_dot = false;
  std::vector<Symbol> path;
  Id<Modification> mod;
  Id<Modification> annotation;
};

struct Import {
  bool leading_dot = false;
  std::vector<Symbol> path;
  bool wildcard = false;             // import A.B.*
  Symbol alias;                      // import x = A.B（无则 invalid）
  std::vector<Symbol> multi;         // import A.B.{c, d}
  std::vector<Symbol> multi_alias;   // 与 multi 平行：import A.{c, e = d} 的 e（无则 invalid）
  Symbol string_comment;
  Id<Modification> annotation;
};

enum class ElementKind : std::uint8_t { Component, Extends, Import, ClassDef };

struct Element {
  ElementKind kind = ElementKind::Component;
  SourceLoc loc;
  bool is_protected = false;
  bool is_final = false;
  bool is_redeclare = false;
  bool is_replaceable = false;
  bool is_inner = false;
  bool is_outer = false;
  // 各 kind 用自己那一个字段：
  ComponentClause comp;
  Extends ext;
  Import imp;
  Id<ClassDef> cls;                  // 嵌套类
  // replaceable 的约束子句 constrainedby T(mod) "cmt" annotation(...)
  bool has_constrainedby = false;
  bool cb_leading_dot = false;
  std::vector<Symbol> cb_path;
  Id<Modification> cb_mod;
  Symbol cb_comment;
  Id<Modification> cb_annotation;
};

// ---- 方程 ----
enum class EqKind : std::uint8_t { Equality, Connect, FunctionCall, If, For, When };

struct EqBranch {
  Id<Exp> cond;                      // 无效 = else 分支
  std::vector<Id<Equation>> body;
};

struct Equation {
  EqKind kind = EqKind::Equality;
  SourceLoc loc;
  Id<Exp> lhs;   // Equality: 左；Connect: 第一个 cref；FunctionCall: 调用表达式
  Id<Exp> rhs;   // Equality: 右；Connect: 第二个 cref
  std::vector<EqBranch> branches;    // If: if/elseif/else；When: when/elsewhen
  std::vector<ForIndex> iters;       // For
  std::vector<Id<Equation>> body;    // For 体
  Symbol string_comment;
  Id<Modification> annotation;
};

// ---- 语句 ----
enum class StmtKind : std::uint8_t { Assign, Call, If, For, While, When, Break, Return };

struct StmtBranch {
  Id<Exp> cond;                      // 无效 = else 分支
  std::vector<Id<Statement>> body;
};

struct Statement {
  StmtKind kind = StmtKind::Assign;
  SourceLoc loc;
  Id<Exp> lhs;   // Assign: 左（可为 Tuple）；Call: 调用表达式
  Id<Exp> rhs;   // Assign: 右
  std::vector<StmtBranch> branches;  // If: if/elseif/else；When: when/elsewhen；While: 1 个
  std::vector<ForIndex> iters;       // For
  std::vector<Id<Statement>> body;   // For 体
  Symbol string_comment;
  Id<Modification> annotation;
};

// ---- 类定义 ----
struct EnumLiteral {
  Symbol name;
  Symbol string_comment;
  Id<Modification> annotation;
};

struct AlgorithmSection {
  bool is_initial = false;
  std::vector<Id<Statement>> stmts;
};

struct ClassDef {
  SourceLoc loc;
  bool is_final = false;
  bool is_partial = false;
  bool is_encapsulated = false;
  Restriction restriction = Restriction::Class;
  Symbol name;
  Symbol string_comment;             // 类名后的字符串注释
  Id<Modification> annotation;       // 类级 annotation(...)

  Purity purity = Purity::None;

  // 长形式
  bool is_class_extends = false;     // `model extends X(mod) ... end X;`
  Id<Modification> class_extends_mod;
  std::vector<Id<Element>> elements;
  std::vector<Id<Equation>> initial_equations;
  std::vector<Id<Equation>> equations;
  // 各 `equation` / `initial equation` 段在上面向量里的起始下标（一个类可有多段；S2 要按 OMC 的段序处理）
  std::vector<std::size_t> equation_sections;
  std::vector<std::size_t> initial_equation_sections;
  std::vector<AlgorithmSection> algorithms;
  // external 段：external "lang" [lhs =] call annotation(...)
  bool has_external = false;
  Symbol ext_lang;                   // 无则 invalid
  Id<Exp> ext_lhs;                   // 无则 invalid
  Id<Exp> ext_call;                  // 无则 invalid（Call 节点）
  Id<Modification> ext_annotation;

  // 短形式 `Name = [prefix] Type[subs](mod) "cmt"`
  bool is_short = false;
  TypePrefix short_prefix;
  bool short_leading_dot = false;
  std::vector<Symbol> short_type;
  std::vector<Id<Exp>> short_subscripts;
  Id<Modification> short_mod;
  // 枚举 `Name = enumeration(a "c", b)` / `enumeration(:)`
  bool is_enumeration = false;
  bool enum_unspecified = false;
  std::vector<EnumLiteral> enum_lits;
  // 偏导函数 `function df = der(f, x, y)`（MLS §12.7.2）
  bool is_der = false;
  Id<Cref> der_fn;
  std::vector<Symbol> der_vars;
};

// ---- 一个 .mo 文件的解析结果（MLS A.2.1 stored-definition：[within [name] ";"] {class-definition ";"}）----
// 多文件装载（S2）时，每个文件解析成一个 StoredDefinition，再按 within 插进包围类（MLS §13.4.3）。
struct StoredDefinition {
  bool has_within = false;
  bool within_leading_dot = false;
  std::vector<Symbol> within_path;   // 空 = `within;` 顶层
  std::vector<Id<ClassDef>> classes; // 该文件的顶层类定义（按源码顺序）
};

// ---- 整棵树的容器（拥有所有 arena） ----
struct Absyn {
  Interner& interner;
  Arena<Exp> exps;
  Arena<Cref> crefs;
  Arena<Modification> mods;
  Arena<Element> elems;
  Arena<Equation> eqs;
  Arena<Statement> stmts;
  Arena<ClassDef> classes;
  bool has_within = false;
  bool within_leading_dot = false;
  std::vector<Symbol> within_path;
  std::vector<Id<ClassDef>> top;     // stored_definition 顶层类
  // 顶层类 → 解析它的那个文件（装载器交给解析器的路径 / 命令行上的文件，原样）。omc 在 Absyn.Class.info.fileName 里记着，
  // 由它建 URI 映射（SymbolTable.updateUriMapping，SymbolTable.mo:568-599 v1.25.0），OpenModelica.Scripting.uriToFilename 解
  // `modelica://类名/…` 时查（见 base/omc_uri.h、flatten 的 uri_to_filename）。没记的类 → 那样的 URI 求不出（SKIP）。
  std::unordered_map<Symbol, std::string> class_files;

  explicit Absyn(Interner& in) : interner(in) {}

  Id<Exp> add_exp(Exp e) { return exps.add(std::move(e)); }
  Id<Cref> add_cref(Cref c) { return crefs.add(std::move(c)); }
  Id<Modification> add_mod(Modification m) { return mods.add(std::move(m)); }
  Id<Element> add_elem(Element e) { return elems.add(std::move(e)); }
  Id<Equation> add_eq(Equation e) { return eqs.add(std::move(e)); }
  Id<Statement> add_stmt(Statement s) { return stmts.add(std::move(s)); }
  Id<ClassDef> add_class(ClassDef c) { return classes.add(std::move(c)); }
};

}  // namespace ymc
