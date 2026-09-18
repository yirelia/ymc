// 压平模型 FlatModel（S3 出口 / S5 入口）——对标 OMC NFFrontEnd/NFFlatModel.mo（+ 打印时转 DAE）。
//
// 设计（ADR-006 D1、研究底稿 R13）：压平是一张扁平的变量表 + 方程/算法段；变量名是带前缀的点分标量名
// （`b.x`），类型是塌陷后的内建类型。默认 `ymc X.mo` 的文本对齐 **DAE dump 版式**（R13 §2/§3/§6），
// 不是 Flat/Base Modelica。表达式用类型化 `Expr`（inst/expr.h）。
//
// v1 竖切片（ADR-006 D6）：标量变量 + 普通等式 + 赋值语句。数组/for/when/record 的 kind 先占位。
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "base/arena.h"
#include "base/loc.h"
#include "base/symbol.h"
#include "inst/expr.h"
#include "inst/inode.h"   // SVariability/SDirection/SConnectorType（复用 SCode 枚举）经 CompAttributes 引入

namespace ymc {

// ---- 变量属性（start/unit/…）----
// 按内建类型的 FLAT_TREE 顺序排好（R11 §4.2：压平属性顺序 = 内建组件数组顺序，与书写序无关）；
// flatten 阶段按该顺序产出，dumper 只顺序打印。value 是定型/求值后的类型化表达式。
struct FlatVarAttr {
  Symbol name;        // quantity/unit/displayUnit/min/max/start/fixed/nominal/stateSelect/…
  Id<Expr> value;
};

// ---- 压平变量（R13 §3）----
enum class FlatVarKind : std::uint8_t { Var, Discrete, Parameter, Constant };  // 打印前缀 discrete/parameter/constant
enum class FlatDirection : std::uint8_t { None, Input, Output };

struct FlatVariable {
  std::vector<Symbol> name;              // 点分名（`b.x` → {b, x}）
  std::vector<std::int64_t> subscripts;  // 数组标量化后的下标（`b.x[1,2]` → {1,2}）；空 = 标量。v1 只叶组件是数组
  Type ty;                               // 塌陷后的内建/枚举标量类型
  // 前缀（R13 §3.1 固定顺序：protected final par* const|param|discrete in|out 类型 名字）
  bool is_protected = false;
  bool is_final = false;                 // 结构参数强制 final（R13：final parameter）
  FlatVarKind var_kind = FlatVarKind::Var;
  FlatDirection direction = FlatDirection::None;
  // flow/stream/inner/outer/each：R13 §3.1 明确**不打印**，故不存
  std::vector<FlatVarAttr> attributes;   // 已按类型顺序排好
  Id<Expr> binding;                      // ` = expr`，无则 invalid
  Symbol comment;                        // 保留（R13 §3.6）
  SourceLoc loc;
};

// ---- 压平方程（R13 §6）----
enum class FlatEqKind : std::uint8_t {
  Equality,     // lhs = rhs（connect 已展开成 equality，不单列 Connect）
  If, For, When,
  Assert, Terminate, Reinit, NoRetCall,
};
struct FlatEquation;
struct FlatEqBranch {
  Id<Expr> cond;                         // If/When 分支条件（else 分支 invalid）
  std::vector<Id<FlatEquation>> body;
};
struct FlatEquation {
  FlatEqKind kind = FlatEqKind::Equality;
  SourceLoc loc;
  Id<Expr> a, b, c;                      // Equality: lhs,rhs；Assert: cond,msg,level；Reinit: cref,val；
                                         // Terminate: msg；NoRetCall: call；For: range
  Symbol index;                          // For 迭代变量
  std::vector<FlatEqBranch> branches;    // If（elseif 连写）/ When（elsewhen）
  std::vector<Id<FlatEquation>> body;    // For 体
  std::vector<Id<FlatEquation>> else_body;  // If 的 else
  Symbol comment;                        // 保留（R13 §6-1）
};

// ---- 压平语句（R13 §6-7）----
enum class FlatStmtKind : std::uint8_t {
  Assign, If, For, While, When, Assert, Terminate, Reinit, NoRetCall, Return, Break,
};
struct FlatStatement;
struct FlatStmtBranch {
  Id<Expr> cond;
  std::vector<Id<FlatStatement>> body;
};
struct FlatStatement {
  FlatStmtKind kind = FlatStmtKind::Assign;
  SourceLoc loc;
  Id<Expr> a, b, c;                      // Assign: lhs,rhs；While: cond；其余同方程
  Symbol index;                          // For
  std::vector<FlatStmtBranch> branches;  // If（保留 elseif 列表）/ When / While(1)
  std::vector<Id<FlatStatement>> body;   // For
  std::vector<Id<FlatStatement>> else_body;
  Symbol comment;
};
struct FlatAlgorithm {
  bool is_initial = false;
  std::vector<Id<FlatStatement>> stmts;
};

// ---- 压平模型 ----
// 表达式/方程/语句都存在这里的 arena（Expr 复用 ExprArena；接线时可与 Inst 共享同一 ExprArena）。
struct FlatModel {
  std::vector<Symbol> name;              // 全限定名（点分，R13 §2：class <全名>）
  Symbol comment;                        // 类字符串注释（保留）
  std::vector<FlatVariable> variables;
  std::vector<Id<FlatEquation>> initial_equations;
  std::vector<Id<FlatEquation>> equations;
  std::vector<FlatAlgorithm> initial_algorithms;
  std::vector<FlatAlgorithm> algorithms;

  ExprArena exprs;
  Arena<FlatEquation> eqs;
  Arena<FlatStatement> stmts;

  Id<FlatEquation> add_eq(FlatEquation e) { return eqs.add(std::move(e)); }
  Id<FlatStatement> add_stmt(FlatStatement s) { return stmts.add(std::move(s)); }
};

}  // namespace ymc
