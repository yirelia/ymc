// combineBinariesExp 的移植。OMC 把两件事写在一个函数里（:1546-1747）：optOperator = SOME(op) 时往现成的 MULTARY 里收项，
// NONE 时从头处理一个式子。这里拆成 collect 和 combine 两个函数，case 编号照原文注释。
//
// 表达式形状的来历都在"头插"上：addArgument（:1773-1798）往列表头加，case 3.0 又先收右操作数、后收左操作数。所以
// ((a + b) + c) + d 收成 c, b, a, d：最右的项先进、留在尾巴上，其余的按遍历序反过来。再调一次，`3.1` 把 Multary 拆开重收一遍，
// 列表整个反一次。这就是深映射在调用实参里逐层翻转的原因：sqrt(a * b) 的 a * b 被 Expression.map 收一次、被外层 CALL 的
// `call.arguments := list(combineBinariesExp(arg))`（:1673-1676）再收一次，于是 b * a。
#include "bdae/combine_binaries.h"

#include <algorithm>
#include <vector>

namespace ymc::bdae {

namespace {

// Operator.invert（NFOperator.mo:105-136）的标量部分；Pow 不进这里（isSoftCommutative 为假）。
BinOp invert(BinOp op) {
  switch (op) {
    case BinOp::Add: return BinOp::Sub;
    case BinOp::Sub: return BinOp::Add;
    case BinOp::Mul: return BinOp::Div;
    case BinOp::Div: return BinOp::Mul;
    case BinOp::Pow: break;
  }
  YMC_UNREACHABLE("Pow has no inverse");
}

// 收项途中的 MULTARY。OMC 头插，这里尾插，收完（finish）再整个反过来，省得反复挪动。
struct Pending {
  BinOp op;
  Type ty;
  std::vector<Id<Expr>> args, inv;
};

class Combiner {
 public:
  explicit Combiner(ExprArena& a) : a_(a) {}

  // combineBinariesExp(exp)：optOperator = NONE，result = EMPTY。
  Id<Expr> combine(Id<Expr> id) {
    const Expr& e = a_[id];
    switch (e.kind) {
      case ExprKind::Binary:
        if (is_commutative(e)) {             // case 3.0
          Pending m{e.bin, e.ty, {}, {}};
          collect(e.b, e.bin, m, false);
          collect(e.a, e.bin, m, false);
          return finish(m);
        }
        if (e.bin == BinOp::Sub || e.bin == BinOp::Div) {   // case 4.0，isSoftCommutative（NFOperator.mo:907-925）
          Pending m{invert(e.bin), e.ty, {}, {}};
          collect(e.a, m.op, m, false);
          collect(e.b, m.op, m, true);
          return finish(m);
        }
        return id;                           // 幂、String 的 `+`：没有哪个 case 接，落到 else，子式也不下探（:1745）
      case ExprKind::Multary: {              // case 3.1
        Pending m{e.bin, e.ty, {}, {}};
        for (Id<Expr> arg : e.args) collect(arg, e.bin, m, false);
        for (Id<Expr> arg : e.inv_args) collect(arg, e.bin, m, true);
        return finish(m);
      }
      case ExprKind::Unary:                  // :1685-1687
      case ExprKind::LogicalNot:             // :1696-1698
      case ExprKind::Cast:                   // :1711-1713
        return with_children(id, combine(e.a), {}, {}, nullptr);
      case ExprKind::Logical:                // :1691-1694
      case ExprKind::Relation: {             // :1700-1703
        const Id<Expr> x = combine(e.a);
        return with_children(id, x, combine(e.b), {}, nullptr);
      }
      case ExprKind::IfExp: {                // :1705-1709
        const Id<Expr> x = combine(e.a), y = combine(e.b);
        return with_children(id, x, y, combine(e.c), nullptr);
      }
      case ExprKind::Range: {                // :1657-1663。Expr 的 Range 是 a:b 或 a:步长(b):终点(c)，这里不分
        const Id<Expr> x = combine(e.a), y = combine(e.b);
        return with_children(id, x, y, e.c.is_valid() ? combine(e.c) : Id<Expr>{}, nullptr);
      }
      case ExprKind::Call:                   // :1673-1676
      case ExprKind::Array:                  // :1649-1655（literal 的数组里没有可并的式子，不用判）。现在只有 when 条件和 for 的范围走到这里
      case ExprKind::Tuple: {                // :1665-1667，元组赋值的左边
        std::vector<Id<Expr>> args;
        for (Id<Expr> arg : e.args) args.push_back(combine(arg));
        return with_children(id, {}, {}, {}, &args);
      }
      // ponytail: Cref 的下标、记录……（:1644-1742）都走不到：bdae::lower 在这之前 SKIP 掉了。放开数组时照原文补。
      default:
        return id;
    }
  }

  // Expression.map(exp, combineBinaries)：先子后父，每个节点都收一次（:2651-2818）。
  Id<Expr> deep(Id<Expr> id) {
    const Expr& e = a_[id];
    Id<Expr> out = id;
    switch (e.kind) {
      case ExprKind::Binary:
      case ExprKind::Logical:
      case ExprKind::Relation: {
        const Id<Expr> x = deep(e.a);
        out = with_children(id, x, deep(e.b), {}, nullptr);
        break;
      }
      case ExprKind::Unary:
      case ExprKind::LogicalNot:
      case ExprKind::Cast:
        out = with_children(id, deep(e.a), {}, {}, nullptr);
        break;
      case ExprKind::IfExp: {
        const Id<Expr> x = deep(e.a), y = deep(e.b);
        out = with_children(id, x, y, deep(e.c), nullptr);
        break;
      }
      case ExprKind::Call: {                 // Call.mapExp（NFCall.mo:1710 起）只走实参
        std::vector<Id<Expr>> args;
        for (Id<Expr> arg : e.args) args.push_back(deep(arg));
        out = with_children(id, {}, {}, {}, &args);
        break;
      }
      default:
        break;                               // 字面量、Cref：没有子式
    }
    return combine(out);
  }

 private:
  // combineBinariesExp(exp, SOME(op), result, inverse)。result 恒为收到一半的 MULTARY：只有 case 3.x / 4.0 会传 SOME。
  void collect(Id<Expr> id, BinOp op, Pending& r, bool inverse) {
    const Expr& e = a_[id];
    if (e.kind == ExprKind::Binary && e.bin == op) {                    // case 1.0
      collect(e.a, op, r, inverse);
      collect(e.b, op, r, inverse);
    } else if (e.kind == ExprKind::Multary && e.bin == op) {            // case 1.1
      for (Id<Expr> arg : e.args) collect(arg, op, r, inverse);
      for (Id<Expr> arg : e.inv_args) collect(arg, op, r, !inverse);
    } else if (e.kind == ExprKind::Binary && combineable(op, e.bin)) {  // case 2.0
      collect(e.a, op, r, inverse);
      collect(e.b, op, r, !inverse);
    } else {                                                            // 不能并：另起一个式子，整个当一项（:1745、:1614、:1625、:1633）
      // case 2.1 走不到：Multary 的运算只会是 Add / Mul（case 4.0 先取逆），能并的已被 1.1 接走。
      (inverse ? r.inv : r.args).push_back(combine(id));
    }
  }

  // Operator.isCombineable（NFOperator.mo:956-970）的标量部分：同一运算，或都是加减。乘和除的 MathClassification 不同，不并
  //（所以 a * (b / c) 里的 b / c 另成一项，和 + - 不对称）。
  static bool combineable(BinOp a, BinOp b) {
    const auto dash = [](BinOp x) { return x == BinOp::Add || x == BinOp::Sub; };
    return a == b || (dash(a) && dash(b));
  }

  // Operator.isCommutative（NFOperator.mo:879-905）：先看类型，String 的 `+` 是拼接，不可交换。
  static bool is_commutative(const Expr& e) {
    const TypeKind k = e.ty.element_type().kind;
    if (k != TypeKind::Integer && k != TypeKind::Real && k != TypeKind::Boolean) return false;
    return e.bin == BinOp::Add || e.bin == BinOp::Mul;
  }

  Id<Expr> finish(Pending& m) {
    std::reverse(m.args.begin(), m.args.end());
    std::reverse(m.inv.begin(), m.inv.end());
    Expr out;
    out.kind = ExprKind::Multary;
    out.ty = m.ty;
    out.bin = m.op;
    out.args = std::move(m.args);
    out.inv_args = std::move(m.inv);
    return a_.add(std::move(out));
  }

  // 换掉子式后的新节点；子式都没变就沿用原节点。原节点绝不改：同一 Id 可能被别处引用。
  Id<Expr> with_children(Id<Expr> id, Id<Expr> a, Id<Expr> b, Id<Expr> c, const std::vector<Id<Expr>>* args) {
    const Expr& e = a_[id];
    if (a == e.a && b == e.b && c == e.c && (!args || *args == e.args)) return id;
    Expr out = e;
    out.a = a;
    out.b = b;
    out.c = c;
    if (args) out.args = *args;
    return a_.add(std::move(out));
  }

  ExprArena& a_;
};

}  // namespace

Id<Expr> combine_binaries(ExprArena& a, Id<Expr> e) { return Combiner(a).combine(e); }
Id<Expr> combine_binaries_deep(ExprArena& a, Id<Expr> e) { return Combiner(a).deep(e); }

}  // namespace ymc::bdae
