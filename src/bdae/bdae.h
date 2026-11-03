// BackendDAE（S5a，ADR-008 D1 + §6）——对标 OMC NBackEnd/Classes/NBackendDAE.mo 的 BackendDAE.MAIN（:107-124 v1.25.0）。
//
// 形状：一处所有、桶是视图。Var / Eqn 各住一个 Arena，VarData / EqnData 的每个桶是 `std::vector<Id<…>>`，对应 OMC 里多个
// VariablePointers 共享同一个 `Pointer<Variable>`：一个变量可以同时在 unknowns 和 algebraics 里，改种类只改 arena 里那一格。
// 表达式从 FlatModel 整个搬过来（ExprArena 的所有权移交），Id<Expr> 原样有效，不用深拷贝。
//
// 这里只有 S5a-1a / 1b / 1c（lower：[SCAL] [WHEN] [-IF-] [ALGO]）用到的那部分：D1 列的其余种类、桶、字段等用到它们的桶再加，
// 靠 -Werror=switch 保证不漏。
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "base/arena.h"
#include "base/symbol.h"
#include "flat/flat.h"
#include "inst/expr.h"

namespace ymc::bdae {

// 变量种类（NFBackendExtension.mo VariableKind :179-236；打印标签 :238-274）。Dummy 是 `$ANY$`（FRONTEND_DUMMY → [DMMY]）；
// Iterator 是算法里 for 的迭代量（collectIterators NBackendDAE.mo:1393-1427，进 Artificial 桶）。
enum class VarKind : std::uint8_t { Dummy, Time, Algebraic, State, Discrete, Parameter, Constant, Iterator };

struct Var {
  ExprCref name;
  Type ty;
  VarKind kind = VarKind::Algebraic;
  // 声明前缀（Variable.toStream 打印：visibility + Attributes.toString，NFVariable.mo:536-540、NFAttributes.mo:619-632）。
  bool is_protected = false, is_final = false, is_redeclare = false, is_inner = false, is_outer = false;
  SConnectorType connector = SConnectorType::Potential;
  FlatVarKind variability = FlatVarKind::Var;
  FlatDirection direction = FlatDirection::None;
  std::vector<FlatVarAttr> attrs;   // 全部类型属性（打印只挑 fixed / start / min / max / nominal / stateSelect，VariableAttributes.toString :393-423）
  Symbol tearing_select;            // TearingSelect 成员名；无 = invalid
  Id<Expr> binding;                 // lower 时不下沉（Bindings 模块才造方程，NBBindings.mo:68-81）
};

struct VarSet { std::vector<Id<Var>> items; };   // VariablePointers（NBVariable.mo:1568-1573）的视图部分

// VAR_DATA_SIM（NBVariable.mo:2053-2084）的子集。unknowns 是 algebraics + discretes，打印时只用它算 `scalar unknowns`。
struct VarData {
  VarSet unknowns, states, algebraics, discretes, top_level_inputs, parameters, constants, artificials;
};

enum class EqnTime : std::uint8_t { Continuous, Discrete, Empty };      // EquationKind（NBEquation.mo:3790）的子集
enum class EqnKind : std::uint8_t { Scalar, When, If, Algorithm };      // Equation（NBEquation.mo:899-966）的子集

// WhenStatement（NBEquation.mo:3459-3491）。没有 TERMINATE、NORETCALL：WhenEquationBody.split 把它们丢了（见 lower.cpp when_equation）。
enum class WhenStmtKind : std::uint8_t { Assign, Reinit, Assert };
struct WhenStmt {
  WhenStmtKind kind = WhenStmtKind::Assign;
  Id<Expr> a, b, c;                 // Assign：lhs、rhs；Reinit：状态、值；Assert：条件、消息、级别
};
// WHEN_EQUATION_BODY（:3000-3004）。else_when 链铺平：第一支是 when，其余是 elsewhen。
struct WhenBranch {
  Id<Expr> condition;
  std::vector<WhenStmt> stmts;
};

// Statement（NFStatement.mo:54-135）的子集。Break 单列是因为打印：Statement.toStream 没有 BREAK 支（:961-963 写成了两个 RETURN），打 `#UNKNOWN STATEMENT#`。
enum class StmtKind : std::uint8_t { Assign, For, If, When, While, Assert, Terminate, NoRetCall, Break };
struct Stmt;
struct StmtBranch {
  Id<Expr> condition;
  std::vector<Stmt> body;
};
struct Stmt {
  StmtKind kind = StmtKind::Assign;
  Id<Expr> a, b, c;                 // Assign：lhs、rhs；For：范围；While：条件；Assert：条件、消息、级别；Terminate：消息；NoRetCall：调用
  Symbol index;                     // For 的迭代量
  std::vector<StmtBranch> branches; // If / When。If 的 else 是条件为字面量 true 的末分支（NF 的表示，打印成 `elseif true then`）
  std::vector<Stmt> body;           // For / While
};

struct Eqn;
// IF_EQUATION_BODY（NBEquation.mo:2611-2615）铺平：一个 Eqn 是 split 之后的一条，所以每支恰好一条方程。condition 无效 = Expression.END（`else`）。
struct IfBranch {
  Id<Expr> condition;
  Id<Eqn> eqn;
};

// exclusively_initial 由所在的桶表达：initials 里的就是。
struct Eqn {
  EqnKind kind = EqnKind::Scalar;
  ExprCref name;                    // $RES_SIM_n（EquationAttributes.residualVar）
  Id<Expr> lhs, rhs;                // Scalar
  std::vector<WhenBranch> when;     // When
  std::vector<IfBranch> if_branches;   // If
  std::vector<Stmt> alg;            // Algorithm
  int size = 1;
  EqnTime time = EqnTime::Continuous;
};

struct EqnSet { std::vector<Id<Eqn>> items; };

// EQ_DATA_SIM（NBEquation.mo:4303-4313）的子集。simulation = continuous + discretes，打印时只用它算 `scalar simulation equations`；removed 不算。
struct EqnData {
  int unique_index = 0;             // 下一个可用的 $RES_… 序号（Bindings 等模块接着往上编，NBBindings.mo:68-81）
  EqnSet simulation, continuous, discretes, initials, removed;
};

struct BackendDAE {
  VarData vars;
  EqnData eqns;
  Arena<Var> var_arena;
  Arena<Eqn> eqn_arena;
  ExprArena exprs;
};

// 一个 pre 模块的结果。模块原地改 BackendDAE（Module.wrapper：BackendDAE → BackendDAE，NBModule.mo），契约同 LowerResult：unsupported 置位时 dae 不可用，driver 退 66。
struct ModuleResult {
  bool unsupported = false;
  std::string unsupported_reason;
};

}  // namespace ymc::bdae
