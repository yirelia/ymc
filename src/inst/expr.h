// 类型化表达式（S3 实例化后 / 压平 / 后端共用）——对标 OMC NFFrontEnd/NFExpression.mo。
//
// 与 S1 的 `Absyn::Exp` 的区别（ADR-006 D1）：Absyn::Exp 忠实源码（保留括号当单元素 Tuple、Real 存拼写、
// cref 是未解析的名字）；本 `Expr` 是 **instExpressions + typing + simplify 之后**的树——每个节点挂 `Type`、
// cref 已解析到实例、字面量已折叠、`end` 已换成 `size(a,1)`、整型→实型插了 `Cast` 节点（R13 §4）。
// 两套 IR 并存、不互转：Absyn 服务 S1 的 round-trip，Expr 服务 S3 起的语义处理与压平打印。
//
// 表示（方案 §3.4）：arena + `Id<Expr>`、胖 struct + `ExprKind` + switch（`-Werror=switch` 穷尽）。
// v1 竖切片（ADR-006 D6）只用标量：字面量 / cref / 二元 / 一元 / 关系 / 逻辑 / if 表达式 / cast /
// 少量内建调用（der/pre/abs…）。Array/Range/Tuple/Reduction/Subscript 的 kind 先占位。
#pragma once
#include <cstdint>
#include <vector>

#include "base/arena.h"
#include "base/loc.h"
#include "base/symbol.h"
#include "inst/type.h"

namespace ymc {

// 标量二元/一元运算（v1）。数组逐元素变体（`.+ .* ./ .^`，R13 §4.1）等数组竖切片再加。
enum class BinOp : std::uint8_t { Add, Sub, Mul, Div, Pow };
enum class RelOp : std::uint8_t { Lt, Le, Gt, Ge, Eq, Ne };
enum class LogOp : std::uint8_t { And, Or };
enum class UnOp : std::uint8_t { Minus };   // 一元负；`not` 单独走 LogicalNot（R13：not 后有空格）

// ---- 解析后的组件引用 ----
// 对标 NFComponentRef：一串 (组件, 下标)。v1 是标量点分路径（如 b.x）；resolved_node 回链到实例节点
// （inode.h 落地前用不透明 u32；接线时改成 Id<INode>）。压平打印器只需 parts 的点分名 + 下标。
struct ExprCrefPart {
  Symbol name;
  std::vector<Id<Expr>> subscripts;   // 空 = 标量；v1 恒空
};
struct ExprCref {
  std::vector<ExprCrefPart> parts;
  std::uint32_t resolved_node = 0xFFFFFFFFu;   // Id<INode>.raw，待 inode.h 后强类型化
};

enum class ExprKind : std::uint8_t {
  // 字面量
  IntLit, RealLit, BoolLit, StringLit, EnumLit,
  // 引用
  Cref,
  // 运算
  Binary,       // op(BinOp) a, b
  Unary,        // op(UnOp) a
  Relation,     // rel(RelOp) a, b
  Logical,      // log(LogOp) a, b
  LogicalNot,   // not a
  IfExp,        // if a then b else c（elseif 嵌进 c，打印成 else if，R13 §4.1）
  Cast,         // /*Type*/(a)：整型→实型等隐式转换（R13 §4.1）
  Call,         // cref 命名的调用（内建/用户函数）；args 已是位置参数、补齐缺省（R13 §4.3）
  // —— v1 不用，占位 ——
  Array,        // {a, b, …}
  Range,        // a:b / a:step:b
  Tuple,        // (a, b)：多输出 / 元组赋值；空位用 wild
  Subscript,    // ASUB：对表达式取下标 exp[i,j]
  TupleElement, // TSUB：取元组第 k 个输出 f(x)[1]
  Size,         // size(a, d)
  Reduction,    // sum(e for i in r) / array(e for …)
  Wild,         // 元组通配 _
  End,          // 下标里的 end（typing 前；typing 后被换成 size）
};

struct Expr {
  ExprKind kind = ExprKind::IntLit;
  Type ty;                     // 定型后回填（R14：typeExp 返回 (exp, type, variability)，variability 不常驻）
  SourceLoc loc;

  // 字面量
  std::int64_t ival = 0;       // IntLit
  double rval = 0.0;           // RealLit（打印复用 real_literal_text，S2 已实现）
  bool bval = false;           // BoolLit
  Symbol sval;                 // StringLit：解转义后的值（打印时再 escapedString）
  std::vector<Symbol> enum_path;  // EnumLit：全限定字面量 Pkg.E.lit（R13：打印全路径）

  // 引用 / 运算
  ExprCref cref;               // Cref
  BinOp bin = BinOp::Add;      // Binary
  RelOp rel = RelOp::Lt;       // Relation
  LogOp log = LogOp::And;      // Logical
  UnOp un = UnOp::Minus;       // Unary
  Id<Expr> a, b, c;            // 见各 kind 注释（Binary/Relation/Logical: a,b；Unary/Not/Cast: a；IfExp: a,b,c）

  // 调用
  std::vector<Symbol> call_name;   // Call：函数全限定名（点分；打印去前导点）
  std::vector<Id<Expr>> args;      // Call / Array / Tuple 元素
};

// 表达式变率（R14 §2；不常驻 Expr 节点，定型时算、只存到需要检查处：组件、绑定）。
enum class Variability : std::uint8_t { Constant, Parameter, StructuralParameter, Discrete, Continuous };

// 表达式竞技场容器（挂在 inst 层的容器上；这里先给独立定义，接线时并入 Inst 容器）。
struct ExprArena {
  Arena<Expr> exprs;
  Id<Expr> add(Expr e) { return exprs.add(std::move(e)); }
  [[nodiscard]] Expr& operator[](Id<Expr> id) { return exprs[id]; }
  [[nodiscard]] const Expr& operator[](Id<Expr> id) const { return exprs[id]; }
};

}  // namespace ymc
