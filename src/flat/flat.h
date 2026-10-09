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
#include <unordered_set>
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
  std::vector<Id<Expr>> subscripts;      // **末段**数组标量化下标（`b.x[1,2]` → {1,2}）；子下标是类型化 Expr（IntLit/BoolLit/EnumLit，对标 NFSubscript）；空 = 末段标量
  // 复合数组标量化：非末段的逐段下标（`a[1].x` → 段 `a` 带 {1}）。与 name 平行、空=该段无下标；
  // 末段下标仍用上面的 subscripts。全为空（默认）时行为与旧模型一致。
  std::vector<std::vector<Id<Expr>>> name_subs;
  Type ty;                               // 塌陷后的内建/枚举标量类型
  std::vector<Symbol> type_path;         // 外部对象类型：按类名打印（`ExtObj eo1`）；空 = 内建类型（ty）
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

// ---- 压平函数（S3.e：发射被调用户函数；DAEDump 函数块）----
// 形参/局部：维在**类型**上（与类体变量维在名上相反）；in/out 变率剥离成 Var（不打前缀），protected 局部保留 kind。
struct FlatFuncParam {
  Symbol name;
  Type ty;                                       // 元素类型 + dims（维在类型上打印：Integer 维打数、Unknown 打 `:`、Exp 打表达式）
  FlatDirection direction = FlatDirection::None;  // Input/Output；None = protected 局部
  bool is_protected = false;
  FlatVarKind var_kind = FlatVarKind::Var;        // 局部保留；in/out 归一 Var
  bool is_final = false;                         // 成了结构参数的 parameter 局部（打 `final parameter`）
  Id<Expr> binding;                              // 默认值 / 输出定义 / 局部绑定；无则 invalid
  std::vector<Symbol> type_path;                 // 记录类型：按类型名打印（`input R r`）；空 = 内建类型（ty）
  std::uint32_t record_def = 0xFFFFFFFFu;        // 记录类型的 SCode 类元素（Id<SElement>.raw），调用处实参匹配用；不打印
  Symbol comment;                                // 字符串注释（`input Real x "the x";`，dumpVar 照打）
  std::vector<FlatVarAttr> attributes;           // 属性（`input Real v(unit = "V")`，dumpVar 照打，名字后、绑定前；final 不打）
};
struct FlatFunction {
  std::vector<Symbol> name;                      // 压平名（点分：顶层 f；包 P.f；模型内嵌按实例路径 M.a.f）
  std::vector<FlatFuncParam> params;             // dump 序：inputs, outputs, protected 局部（拓扑序）
  std::vector<Id<FlatStatement>> body;           // algorithm 语句
  bool has_algorithm = false;                    // 源码有 algorithm 段（空段 omc 也打一行 `algorithm`，EA1 探针）
  std::vector<std::vector<Symbol>> linked;       // derivative / inverse 注解连带的函数（压平名）：本函数留则它们也留
  bool is_impure = false;                        // 打 `impure function`（DAEDumpTpl dumpFunction :115）
  bool is_record_ctor = false;                   // 自动生成的记录构造函数（不打印这个标记；区分"可被 omc 求值的用户函数"用）
  // external 段（FUNCTION_EXT，DAEDumpTpl dumpExternalDecl :166-178）：体后空一行 +
  // `  external "<lang>"[ <out> =][ <name>(<args>)];`
  bool is_external = false;
  Symbol ext_lang;                               // 语言串（不含引号）
  Symbol ext_name;                               // 外部调用名；invalid = 无调用
  Id<Expr> ext_output;                           // 输出引用；invalid = 无
  std::vector<Id<Expr>> ext_args;
  Symbol comment;
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
  std::vector<FlatFunction> functions;   // 被模型调用的用户函数（发射序；dump 时按压平名字节序排，class 之前打印）

  ExprArena exprs;
  Arena<FlatEquation> eqs;
  Arena<FlatStatement> stmts;
  // 函数体里没被展开的数组二元运算节点（Expr 的 raw id）按 DAE 算符类别分两类，打印时点号 / 括号规则不同（ExprDumper）：
  std::unordered_set<std::uint32_t> ew_binaries;        // 打点号的：MUL_ARR `.*`、DIV_ARR / DIV_SCALAR_ARRAY `./`、POW_* `.^`、
                                                        // ADD_ARRAY_SCALAR `.+`、SUB_SCALAR_ARRAY `.-`
  std::unordered_set<std::uint32_t> product_binaries;   // 向量 / 矩阵积（MUL_SCALAR_PRODUCT / MUL_MATRIX_PRODUCT，不可结合）
  // 打印时两个运算数换位的（标量在前的 MUL_SCALAR_ARRAY / ADD_SCALAR_ARRAY：NFOperator.toDAE 换成 *_ARRAY_SCALAR）。节点里仍是
  // 定型时的次序——外面再展开（sum 的实参等）按原次序取元素，换位只是打印形态。
  std::unordered_set<std::uint32_t> swapped_binaries;

  Id<FlatEquation> add_eq(FlatEquation e) { return eqs.add(std::move(e)); }
  Id<FlatStatement> add_stmt(FlatStatement s) { return stmts.add(std::move(s)); }
};

}  // namespace ymc
