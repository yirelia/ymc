// Absyn：忠实源码的抽象语法树（方案 §4.2、§4.3；对应 OMC FrontEnd/Absyn.mo）。
//
// 设计（方案 §3.4）：表达式/cref/修饰/类定义都放进各自的 Arena，用 Id 互指，不用指针、
// 不用 shared_ptr、不用 variant、不用继承。表达式是"胖 struct + ExpKind + switch"（§4.3）。
// 元素/方程/组件声明是值语义 struct，存在其父 ClassDef 的 vector 里（无 new/delete）。
//
// v1 覆盖的子集：长形式类定义（model/class/record/block/connector/type/package/function 等）、
// extends、import、带前缀/数组/修饰/绑定/注释的组件声明、equation 段（等式 + connect + 函数调用
// 方程）、完整表达式文法。**尚不支持**（会给出带位置的诊断并跳过）：algorithm 段、if/for/when
// 方程、短类定义、枚举、矩阵 [ ]、归约表达式、redeclare/replaceable 元素。
#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

#include "base/arena.h"
#include "base/loc.h"
#include "base/symbol.h"

namespace ymc {

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
  std::vector<Id<struct Exp>> subscripts;   // 空 = 无下标
};
struct Cref {
  bool leading_dot = false;                 // .Modelica.xxx 的前导点（全局作用域）
  std::vector<CrefPart> parts;
};

// ---- 表达式 ----
enum class ExpKind : std::uint8_t {
  IntLit, RealLit, BoolLit, StringLit,
  CrefExp,        // 组件引用
  Binary, Unary,  // op 作用于 a(,b)
  Call,           // cref(args)；命名实参用 Binary(Eq, cref(name), value) 表示（v1 权宜）
  Array,          // { args }
  Range,          // a:b 或 a:b:c（b 为步长时 c 为终点）
  IfExp,          // if a then b else c（elseif 嵌进 c）
  Colon,          // 下标里的 :
  End,            // 下标里的 end
};

struct Exp {
  ExpKind kind = ExpKind::IntLit;
  SourceLoc loc;
  // 字面量
  std::int64_t ival = 0;
  double rval = 0.0;
  bool bval = false;
  Symbol sval;                 // StringLit 内容
  // 结构
  Id<Cref> cref;               // CrefExp / Call 的函数名
  Op op = Op::Add;             // Binary / Unary
  Id<Exp> a, b, c;             // 见各 kind 注释
  std::vector<Id<Exp>> args;   // Call 实参 / Array 元素
};

// ---- 修饰 modification ----
struct ModArg {
  bool each_ = false;
  bool final_ = false;
  std::vector<Symbol> path;    // 被修饰的名字（点分）
  Id<struct Modification> sub; // 嵌套 (...)，无则 invalid
  Id<Exp> binding;             // = expr，无则 invalid
  Symbol string_comment;       // 无则 invalid
};
struct Modification {
  std::vector<ModArg> args;    // 来自 ( ... )
  bool assign = false;         // 顶层绑定用 := 而不是 =
  Id<Exp> binding;             // 顶层 = / := expr，无则 invalid
};

// ---- 元素 ----
enum class Restriction : std::uint8_t {
  Class, Model, Record, Block, Connector, ExpandableConnector,
  Type, Package, Function, Operator, OperatorRecord,
};

struct TypePrefix {
  enum class Variability : std::uint8_t { Continuous, Discrete, Parameter, Constant } var =
      Variability::Continuous;
  enum class Causality : std::uint8_t { None, Input, Output } caus = Causality::None;
  enum class Flow : std::uint8_t { None, Flow, Stream } flow = Flow::None;
  enum class Scope : std::uint8_t { None, Inner, Outer, InnerOuter } scope = Scope::None;
};

struct ComponentDecl {
  Symbol name;
  std::vector<Id<Exp>> subscripts;   // 组件数组维度 x[3]
  Id<Modification> mod;              // 修饰 + 绑定，无则 invalid
  Symbol string_comment;             // 无则 invalid
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
  Id<Modification> mod;              // 无则 invalid
};

struct Import {
  bool leading_dot = false;
  std::vector<Symbol> path;
  bool wildcard = false;             // import A.B.*
  Symbol alias;                      // import x = A.B（无则 invalid）
};

enum class ElementKind : std::uint8_t { Component, Extends, Import, ClassDef };

struct Element {
  ElementKind kind = ElementKind::Component;
  SourceLoc loc;
  bool is_protected = false;
  bool is_final = false;
  // 各 kind 用自己那一个字段：
  ComponentClause comp;
  Extends ext;
  Import imp;
  Id<struct ClassDef> cls;           // 嵌套类
};

// ---- 方程 ----
enum class EqKind : std::uint8_t { Equality, Connect, FunctionCall };

struct Equation {
  EqKind kind = EqKind::Equality;
  SourceLoc loc;
  Id<Exp> lhs;   // Equality: 左；Connect: 第一个 cref 表达式；FunctionCall: 调用表达式
  Id<Exp> rhs;   // Equality: 右；Connect: 第二个 cref 表达式
  Symbol string_comment;
  bool is_initial = false;
};

// ---- 类定义 ----
struct ClassDef {
  SourceLoc loc;
  bool is_final = false;
  bool is_partial = false;
  bool is_encapsulated = false;
  Restriction restriction = Restriction::Class;
  Symbol name;
  Symbol string_comment;             // 类名后的字符串注释
  std::vector<Element> elements;
  std::vector<Equation> equations;   // 含 initial（用 Equation.is_initial 区分）
};

// ---- 整棵树的容器（拥有所有 arena） ----
struct Absyn {
  Interner& interner;
  Arena<Exp> exps;
  Arena<Cref> crefs;
  Arena<Modification> mods;
  Arena<ClassDef> classes;
  bool has_within = false;
  bool within_leading_dot = false;
  std::vector<Symbol> within_path;
  std::vector<Id<ClassDef>> top;     // stored_definition 顶层类

  explicit Absyn(Interner& in) : interner(in) {}

  Id<Exp> add_exp(Exp e) { return exps.add(std::move(e)); }
  Id<Cref> add_cref(Cref c) { return crefs.add(std::move(c)); }
  Id<Modification> add_mod(Modification m) { return mods.add(std::move(m)); }
  Id<ClassDef> add_class(ClassDef c) { return classes.add(std::move(c)); }
};

}  // namespace ymc
