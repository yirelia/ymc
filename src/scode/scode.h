// SCode：归一化后的元素表示（方案 §4.2、§5 S2；对应 OMC FrontEnd/SCode.mo，基线 3ceb0c9d06）。
//
// 与 Absyn 的关系（同 OMC）：表达式、cref、类型路径、import 直接复用 S1 的 Absyn 节点（Id<Exp> 等），
// 不复制。归一化只做 OMC 做的那几件事（AbsynToSCode.mo，见 absyn_to_scode.cpp 头注）。
// 表示方式仍是方案 §3.4：每类节点一个 Arena，胖 struct + Kind + switch，Id 互指，不用 variant/继承。
// 名字对齐 SCode.mo 的构造子（注释里给出），便于对照 OMC 的 dump。
// 有意砍掉的 OMC/MetaModelica 专有构造：DEFINEUNIT、R_METARECORD/R_UNIONTYPE、Parallelism、IsField、
// ALG_PARFOR/FAILURE/TRY、EQ_PDE、ConstraintSection、clsattrs、INPUT_OUTPUT 拆分。
#pragma once
#include <cstdint>
#include <vector>

#include "absyn/absyn.h"
#include "base/arena.h"
#include "base/loc.h"
#include "base/symbol.h"

namespace ymc {

struct SElement;
struct SMod;
struct SEquation;
struct SStatement;

enum class SVisibility : std::uint8_t { Public, Protected };                     // SCode.Visibility
enum class SVariability : std::uint8_t { Var, Discrete, Param, Const };           // SCode.Variability
enum class SConnectorType : std::uint8_t { Potential, Flow, Stream };             // SCode.ConnectorType
enum class SDirection : std::uint8_t { Bidir, Input, Output };                    // Absyn.Direction
enum class SInnerOuter : std::uint8_t { None, Inner, Outer, InnerOuter };         // Absyn.InnerOuter
// SCode.Restriction + FunctionRestriction 摊平；purity 单独存
enum class SRestriction : std::uint8_t {
  Class, Model, Record, OperatorRecord, Block, Connector, ExpandableConnector, Operator, Type, Package,
  Function, ExternalFunction, OperatorFunction,
};

// ---- 修饰（SCode.Mod / SubMod）----
// NOMOD 用无效 Id<SMod> 表示（空修饰在翻译时规范化掉，AbsynToSCode.mo:1714）。
enum class SModKind : std::uint8_t { Mod, Redecl, BreakComponent, BreakConnect };
struct SSubMod {           // NAMEMOD(ident, mod)——唯一形态；`x.y = z` 已展成 x(y = z)
  Symbol ident;
  Id<SMod> mod;
};
struct SMod {
  SModKind kind = SModKind::Mod;
  bool is_final = false;
  bool is_each = false;
  std::vector<SSubMod> subs;   // Mod
  Id<Exp> binding;             // Mod：无则 invalid
  bool assign = false;         // ymc 额外保留 `:=`（OMC 的 Absyn.EqMod 不区分）
  Symbol comment;              // Mod：修饰参数后的字符串注释
  SourceLoc loc;
  Id<SElement> element;        // Redecl
  Id<Exp> break_a, break_b;    // BreakConnect
};

struct SComment {              // COMMENT(Option<ANNOTATION(Mod)>, Option<String>)
  Id<SMod> annotation;         // 无则 invalid
  Symbol str;                  // 无则 invalid
};

struct SConstrainClass {       // CONSTRAINCLASS(path, mod, comment)
  bool leading_dot = false;
  std::vector<Symbol> path;
  Id<SMod> mod;
  SComment cmt;
};

struct SPrefixes {             // PREFIXES(visibility, redeclare, final, innerOuter, replaceable(cc))
  SVisibility vis = SVisibility::Public;
  bool is_redeclare = false;
  bool is_final = false;
  SInnerOuter io = SInnerOuter::None;
  bool is_replaceable = false;
  bool has_cc = false;
  SConstrainClass cc;
};

struct SAttributes {           // ATTR(arrayDims, connectorType, variability, direction)
  std::vector<Id<Exp>> dims;
  SConnectorType ct = SConnectorType::Potential;
  SVariability var = SVariability::Var;
  SDirection dir = SDirection::Bidir;
};

struct STypeSpec {             // Absyn.TypeSpec（TPATH + arrayDim）
  bool leading_dot = false;
  std::vector<Symbol> path;
  std::vector<Id<Exp>> dims;
};

// ---- 方程（SCode.Equation）----
enum class SEqKind : std::uint8_t { If, Equals, Connect, For, When, Assert, Terminate, Reinit, NoRetCall };
struct SEqBranch {
  Id<Exp> cond;
  std::vector<Id<SEquation>> body;
};
struct SEquation {
  SEqKind kind = SEqKind::Equals;
  SourceLoc loc;
  Id<Exp> a, b, c;             // Equals: lhs,rhs；Connect: 两个 cref；Assert: cond,msg,level；
                               // Terminate: msg；Reinit: cref,value；NoRetCall: call；For: range（无则 invalid）
  Symbol index;                // For 的迭代变量
  std::vector<SEqBranch> branches;         // If: if/elseif 摊平并列；When: when/elsewhen
  std::vector<Id<SEquation>> body;         // For
  std::vector<Id<SEquation>> else_body;    // If
  SComment cmt;
};

// ---- 语句（SCode.Statement）----
enum class SStmtKind : std::uint8_t {
  Assign, If, For, While, When, Assert, Terminate, Reinit, NoRetCall, Return, Break,
};
struct SStmtBranch {
  Id<Exp> cond;
  std::vector<Id<SStatement>> body;
};
struct SStatement {
  SStmtKind kind = SStmtKind::Assign;
  SourceLoc loc;
  Id<Exp> a, b, c;             // Assign: lhs,rhs；While: cond；Assert/Terminate/Reinit/NoRetCall 同方程；For: range
  Symbol index;                // For
  std::vector<SStmtBranch> branches;       // If: if + elseif（保留列表）；When: when/elsewhen；While: 1 个
  std::vector<Id<SStatement>> body;        // For
  std::vector<Id<SStatement>> else_body;   // If
  SComment cmt;
};
struct SAlgorithm {            // ALGORITHM(statements)
  std::vector<Id<SStatement>> stmts;
};

struct SExternalDecl {         // EXTERNALDECL(funcName, lang, output, args, annotation)
  bool present = false;
  Symbol func_name;            // 无则 invalid
  Symbol lang;                 // 无则 invalid
  Id<Exp> output;              // 无则 invalid
  std::vector<Id<Exp>> args;
  Id<SMod> annotation;
};

// ---- 类体（SCode.ClassDef）----
enum class SClassDefKind : std::uint8_t { Parts, ClassExtends, Derived, Enumeration, Pder };
struct SEnumItem {             // ENUM(literal, comment)
  Symbol literal;
  SComment cmt;
};
struct SClassDef {
  SClassDefKind kind = SClassDefKind::Parts;
  // PARTS / CLASS_EXTENDS
  std::vector<Id<SElement>> elements;
  std::vector<Id<SEquation>> equations;           // normalEquationLst（已按 OMC 的段序）
  std::vector<Id<SEquation>> initial_equations;
  std::vector<SAlgorithm> algorithms;
  std::vector<SAlgorithm> initial_algorithms;
  SExternalDecl external;
  Id<SMod> class_extends_mod;                     // CLASS_EXTENDS.modifications
  // DERIVED(typeSpec, mod, attributes)
  STypeSpec derived_type;
  Id<SMod> derived_mod;
  SAttributes derived_attr;
  // ENUMERATION：空列表 + enum_unspecified = enumeration(:)
  std::vector<SEnumItem> enum_items;
  bool enum_unspecified = false;
  // PDER(functionPath, derivedVariables)
  bool pder_leading_dot = false;
  std::vector<Symbol> pder_fn;
  std::vector<Symbol> pder_vars;
};

// ---- 元素（SCode.Element）----
enum class SElementKind : std::uint8_t { Class, Component, Extends, Import };
struct SElement {
  SElementKind kind = SElementKind::Class;
  SourceLoc loc;
  Symbol name;                 // Class / Component 的名字
  SPrefixes prefixes;          // 全部四种元素都用 prefixes.vis；其余字段 Class/Component 用
  // CLASS
  bool is_encapsulated = false;
  bool is_partial = false;
  SRestriction restriction = SRestriction::Class;
  Purity purity = Purity::None;
  SClassDef def;
  SComment cmt;                // Class / Component 的注释
  // COMPONENT
  SAttributes attr;
  STypeSpec type;              // 不含维度（维度都在 attr.dims：组件维在前、类型维在后）
  Id<SMod> mod;                // 组件级 final 不在这里（在 prefixes）
  Id<Exp> condition;
  // EXTENDS
  bool base_leading_dot = false;
  std::vector<Symbol> base_path;
  Id<SMod> extends_annotation;
  // IMPORT（已拆成单条：组导入 A.{x, y = z} → 两条）
  Import imp;
};

// ---- 容器 ----
struct SCode {
  Absyn& absyn;                // 表达式/cref 所在的树
  Arena<SElement> elems;
  Arena<SMod> mods;
  Arena<SEquation> eqs;
  Arena<SStatement> stmts;
  std::vector<Id<SElement>> program;   // SCode.Program = list<Element>
  Id<Exp> assertion_level_error;       // 共享的 `.AssertionLevel.error`（assert 两参形式的默认 level）

  explicit SCode(Absyn& a) : absyn(a) {}
  Id<SElement> add_elem(SElement e) { return elems.add(std::move(e)); }
  Id<SMod> add_mod(SMod m) { return mods.add(std::move(m)); }
  Id<SEquation> add_eq(SEquation e) { return eqs.add(std::move(e)); }
  Id<SStatement> add_stmt(SStatement s) { return stmts.add(std::move(s)); }
};

}  // namespace ymc
