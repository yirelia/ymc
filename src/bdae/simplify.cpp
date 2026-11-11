// 函数名、case 顺序照 NFSimplifyExp.mo（v1.25.0），字面量运算照 NFCeval.mo。走不到的支不写，留 stop：
//   - 数值的 BINARY：绑定走深映射 combineBinaries，+ - * / 全成了 Multary。浅映射（Early Inline 的函数体）不进 `^` 的操作数，那里留着
//     BINARY（`(x + 1) ^ 2`），照 simplifyBinaryAdd / Sub / Mul / Div（:980-1088）移植了。两边都是字面量的只做了 `^`。
//   - 数组、记录、Range、Size、推导式、元组：lower 的 expr_ok 挡住了。只有方程里的几个位置放行（when 条件顶层的数组、算法 for 的范围、
//     元组赋值的左边），Simplify 模块会走到，那里收了 ARRAY / RANGE 两支，元组照原文不走。
#include "bdae/simplify.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

#include "bdae/exp_compare.h"

namespace ymc::bdae {

namespace {

constexpr double kMaxExactInt = 9007199254740992.0;   // 2^53

class Simplifier {
 public:
  Simplifier(ExprArena& a, Interner& in, std::string& stop) : a_(a), in_(in), compare_(a, in), stop_(stop) {}

  Id<Expr> simplify(Id<Expr> id) {
    if (!stop_.empty()) return id;
    switch (a_[id].kind) {
      case ExprKind::IntLit:
      case ExprKind::RealLit:
      case ExprKind::BoolLit:
      case ExprKind::StringLit:
      case ExprKind::EnumLit:
      case ExprKind::Cref:               // :85-90 只重算下标和类型，标量上不变
        return id;
      case ExprKind::Call: return call(id);
      case ExprKind::Multary: return multary(id);
      case ExprKind::Binary: return binary(id);
      case ExprKind::Unary: return unary(id);
      case ExprKind::Logical: return logical(id);
      case ExprKind::LogicalNot: return logical_not(id);
      case ExprKind::Relation: return relation(id);
      case ExprKind::IfExp: return if_exp(id);
      case ExprKind::Cast: return cast(id);
      // 只在方程里有（Simplify 模块，when 条件顶层的 `{a, b}`、算法 for 的范围、元组赋值的左边）；绑定和内联的函数体里 lower 挡了。
      case ExprKind::Array: return array(id);
      case ExprKind::Range: return range(id);
      case ExprKind::Tuple:              // :124 else 支，元素不走
      case ExprKind::Wild:
        return id;
      default: return stop("expression kind", id);
    }
  }

 private:
  Id<Expr> stop(std::string_view why, Id<Expr> id) {
    if (stop_.empty()) stop_ = std::string("simplify: ") + std::string(why);
    return id;
  }

  // ---- 表达式工具（NFExpression.mo）。这里只有标量。

  Id<Expr> real(double v) {
    Expr e;
    e.kind = ExprKind::RealLit;
    e.ty = Type::real();
    e.rval = v;
    return a_.add(std::move(e));
  }
  Id<Expr> integer(std::int64_t v) {
    Expr e;
    e.kind = ExprKind::IntLit;
    e.ty = Type::integer();
    e.ival = v;
    return a_.add(std::move(e));
  }
  Id<Expr> boolean(bool v) {
    Expr e;
    e.kind = ExprKind::BoolLit;
    e.ty = Type::boolean();
    e.bval = v;
    return a_.add(std::move(e));
  }
  // isLiteral :4650-4667
  bool is_literal(Id<Expr> id) const {
    switch (a_[id].kind) {
      case ExprKind::IntLit:
      case ExprKind::RealLit:
      case ExprKind::BoolLit:
      case ExprKind::StringLit:
      case ExprKind::EnumLit:
        return true;
      default:
        return false;
    }
  }
  bool all_literal(const std::vector<Id<Expr>>& ids) const {
    return std::all_of(ids.begin(), ids.end(), [&](Id<Expr> x) { return is_literal(x); });
  }
  // isZero :4504-4516
  bool is_zero(Id<Expr> id) const {
    const Expr& e = a_[id];
    switch (e.kind) {
      case ExprKind::IntLit: return e.ival == 0;
      case ExprKind::RealLit: return e.rval == 0.0;
      case ExprKind::Cast:
      case ExprKind::Unary: return is_zero(e.a);
      default: return false;
    }
  }
  // isOne / isMinusOne :4523-4547：UNARY 里互相调。
  bool is_one(Id<Expr> id, int sign = 1) const {
    const Expr& e = a_[id];
    switch (e.kind) {
      case ExprKind::IntLit: return e.ival == sign;
      case ExprKind::RealLit: return e.rval == sign;
      case ExprKind::Cast: return is_one(e.a, sign);
      case ExprKind::Unary: return is_one(e.a, -sign);
      default: return false;
    }
  }
  // isConstNumber :4712-4723
  bool is_const_number(Id<Expr> id) const {
    const Expr& e = a_[id];
    switch (e.kind) {
      case ExprKind::IntLit:
      case ExprKind::RealLit: return true;
      case ExprKind::Cast:
      case ExprKind::Unary: return is_const_number(e.a);
      default: return false;
    }
  }
  // isNegated :5022-5033
  bool is_negated(Id<Expr> id) const {
    const Expr& e = a_[id];
    switch (e.kind) {
      case ExprKind::IntLit: return e.ival < 0;
      case ExprKind::RealLit: return e.rval < 0;
      case ExprKind::Cast: return is_negated(e.a);
      case ExprKind::Unary: return true;
      default: return false;
    }
  }
  // negate :5035-5046
  Id<Expr> negate(Id<Expr> id) {
    const Expr& e = a_[id];
    switch (e.kind) {
      case ExprKind::IntLit: return integer(-e.ival);
      case ExprKind::RealLit: return real(-e.rval);
      case ExprKind::Cast: {
        Expr out = e;
        out.a = negate(e.a);
        return a_.add(std::move(out));
      }
      case ExprKind::Unary: return e.a;
      default: {
        Expr out;
        out.kind = ExprKind::Unary;
        out.ty = e.ty;
        out.a = id;
        return a_.add(std::move(out));
      }
    }
  }
  // logicNegate :5048-5058
  Id<Expr> logic_negate(Id<Expr> id) {
    const Expr& e = a_[id];
    if (e.kind == ExprKind::BoolLit) return boolean(!e.bval);
    if (e.kind == ExprKind::LogicalNot) return e.a;
    Expr out;
    out.kind = ExprKind::LogicalNot;
    out.ty = e.ty;
    out.a = id;
    return a_.add(std::move(out));
  }
  // makeZero / makeOne :4836-4883
  Id<Expr> make_number(const Type& ty, int v, Id<Expr> at) {
    if (ty.kind == TypeKind::Real) return real(v);
    if (ty.kind == TypeKind::Integer) return integer(v);
    return stop("zero or one of a non-numeric type", at);
  }
  // makeNaN :4899-4906：不是字面量，是 `0.0 / 0.0` 这个式子。
  Id<Expr> make_nan(const Type& ty, Id<Expr> at) {
    Expr e;
    e.kind = ExprKind::Binary;
    e.ty = ty;
    e.bin = BinOp::Div;
    e.a = e.b = make_number(ty, 0, at);
    return a_.add(std::move(e));
  }
  bool is_equal(Id<Expr> x, Id<Expr> y) const { return compare_(x, y) == 0; }

  // ---- simplifyCall :166-219

  // 实参走一遍化简，后面分三路（:176-204）：
  //   - 内建、纯、实参全是字面量：Ceval.evalCall；用户函数同样（NF_EVAL_CONST_ARG_FUNCS 缺省开，Flags.mo:502）。
  //   - 其余内建：simplifyBuiltinCall。
  //   - 其余：留着调用。
  // 默认路径的 SimplifyModel 对同一个调用走过同一段代码。那一遍折得掉的这里已经没了，留下的实参全是字面量只可能是 evalCall 失败（try / else 留调用），
  // 这一遍同样失败，所以不用算。要算的只有实参是这一遍才变成字面量的情形：没移植 Ceval，SKIP。
  //   simplifyBuiltinCall 里，delay、der、homotopy、min、max 走的判断第一遍也走过：延迟时间照样求值（:559-560）、变率不变（:576）、
  //   replaceHomotopy 缺省 none（Flags.mo:872-873），两个实参的 min / max 直接留调用（:325）。$OMC$PositiveMax 看流量变量的 min / max 属性（:340-343），
  //   $OMC$inStreamDiv 看 0 / 0（:361），第一遍同样判过，实参还是那个 cref 和阈值（探针 LbnXS04-LbnXS07）。cat、fill、sum 这些只吃数组，lower 挡了。
  //   semiLinear 看实参形状，第一遍不等的 m1、m2 这一遍可能相等（常数并完），照 simplifySemiLinear 改写。
  Id<Expr> call(Id<Expr> id) {
    const Expr& e = a_[id];
    std::vector<Id<Expr>> args;
    for (Id<Expr> arg : e.args) args.push_back(simplify(arg));
    if (!stop_.empty()) return id;
    if (all_literal(args) && !all_literal(e.args)) return stop("call whose arguments became literals", id);
    const std::string_view name = e.call_name.size() == 1 ? in_.str(e.call_name[0]) : std::string_view{};
    // simplifyPositiveMax / simplifyInStreamDiv :330-370 看实参的形状（isNonPositive、isGreaterOrEqual 只认字面量、cref 的 min / max、一元负号，isNaN 认
    // `0 / 0`）。实参和第一遍一样时判得也一样，留着调用；这一遍把实参化简成了别的样子（方程里 combineBinaries 之后常数并完），判断可能变，没移植，SKIP。
    if (name == "$OMC$PositiveMax" || name == "$OMC$inStreamDiv") {
      for (std::size_t i = 0; i < args.size(); ++i)
        if (!is_equal(args[i], e.args[i])) return stop("stream helper call whose arguments changed", id);
    }
    // 实参全是字面量时走的是 Ceval.evalCall（:191-196），不进 simplifyBuiltinCall；第一遍留下来的就是求值失败的，原样留着
    //（实跑 BdaR202：`semiLinear(0.0, 1.0, 2.0)` 不变成 0.0）。
    if (name == "semiLinear" && args.size() == 3 && !all_literal(args)) {
      // simplifySemiLinear :283-303：x 或两个斜率是 0 → makeZero(typeOf(x))；两个斜率相等 → BINARY(x, *, m1)，这个 BINARY 不再化简
      //（实跑 Ls1C_semi1：`semiLinear(time, p * 2.0 * 3.0, 6.0 * p)` 化简后两个斜率都是 `6.0 * p`，变成 `time * (6.0 * p)`）。
      const Type ty = a_[args[0]].ty;
      if (is_zero(args[0]) || (is_zero(args[1]) && is_zero(args[2]))) return make_number(ty, 0, id);
      if (is_equal(args[1], args[2])) {
        Expr out;
        out.kind = ExprKind::Binary;
        out.ty = ty;
        out.bin = BinOp::Mul;
        out.a = args[0];
        out.b = args[1];
        return a_.add(std::move(out));
      }
    }
    return args == e.args ? id : with_args(id, std::move(args));
  }
  Id<Expr> with_args(Id<Expr> id, std::vector<Id<Expr>> args) {
    Expr out = a_[id];
    out.args = std::move(args);
    return a_.add(std::move(out));
  }

  // ---- simplifyMultary :783-872

  Id<Expr> multary(Id<Expr> id) {
    const Expr& e = a_[id];
    if (e.args.empty() && e.inv_args.empty()) return make_number(e.ty, e.bin == BinOp::Add ? 0 : 1, id);
    if (e.args.size() == 1 && e.inv_args.empty()) return simplify(e.args[0]);
    const bool add = e.bin == BinOp::Add;    // 运算只有 Add / Mul：case 4.0 取过逆（combine_binaries.cpp）
    std::vector<Id<Expr>> arguments, inv_arguments;
    for (Id<Expr> arg : e.args) arguments.push_back(simplify(arg));
    for (Id<Expr> arg : e.inv_args) inv_arguments.push_back(simplify(arg));
    if (!stop_.empty()) return id;
    const bool negative = signs(arguments, inv_arguments, add);

    std::vector<Id<Expr>> const_args, inv_const_args;
    split_const(arguments, const_args);
    split_const(inv_arguments, inv_const_args);
    const Id<Expr> new_const = combine_constants(const_args, inv_const_args, add, e.ty, id);
    if (!stop_.empty()) return id;

    const bool use_const = add ? !is_zero(new_const) : !is_one(new_const);
    Id<Expr> result;
    if (arguments.empty() && inv_arguments.empty()) {
      result = new_const;                                           // const + {} - {} = const
    } else if (arguments.size() == 1 && inv_arguments.empty() && !use_const) {
      result = arguments[0];                                        // 0 + {cr} - {} = cr
    } else if (add && arguments.empty() && inv_arguments.size() == 1 && !use_const) {
      result = negate(inv_arguments[0]);                            // 0 + {} - {cr} = -cr
    } else if (!add && is_zero(new_const)) {
      result = new_const;                                           // 0 * {...} / {...} = 0
    } else {
      Expr out;
      out.kind = ExprKind::Multary;
      out.ty = e.ty;
      out.bin = e.bin;
      if (use_const) out.args.push_back(new_const);
      out.args.insert(out.args.end(), arguments.begin(), arguments.end());
      out.inv_args = std::move(inv_arguments);
      result = a_.add(std::move(out));
    }
    return negative ? negate(result) : result;
  }

  // simplifyMultarySigns :874-927。和：负项挪到另一个表（两个表都是表头一侧放另一边挪来的）。积：负号拿掉，返回是否奇数个。
  bool signs(std::vector<Id<Expr>>& args, std::vector<Id<Expr>>& inv, bool add) {
    bool negative = false;
    std::vector<Id<Expr>> new_args, new_inv;
    if (add) {
      // 原文倒着头插：new_inv = inv 里没变号的（原序）+ args 里变号的（原序），new_args 对称。
      for (Id<Expr> x : inv) if (is_negated(x)) new_args.push_back(negate(x));
      for (Id<Expr> x : args) if (!is_negated(x)) new_args.push_back(x);
      for (Id<Expr> x : inv) if (!is_negated(x)) new_inv.push_back(x);
      for (Id<Expr> x : args) if (is_negated(x)) new_inv.push_back(negate(x));
    } else {
      const auto strip = [&](const std::vector<Id<Expr>>& in, std::vector<Id<Expr>>& out) {
        for (Id<Expr> x : in) {
          const bool neg = is_negated(x);
          negative = neg != negative;
          out.push_back(neg ? negate(x) : x);
        }
      };
      strip(args, new_args);
      strip(inv, new_inv);
    }
    args = std::move(new_args);
    inv = std::move(new_inv);
    return negative;
  }

  // List.splitOnTrue（List.mo:1179-1201）：两边都保序。
  void split_const(std::vector<Id<Expr>>& all, std::vector<Id<Expr>>& consts) const {
    const auto mid = std::stable_partition(all.begin(), all.end(), [&](Id<Expr> x) { return is_const_number(x); });
    consts.assign(all.begin(), mid);
    all.erase(all.begin(), mid);
  }

  // combineConstantNumbers :1408-1468。Integer 也在 Real 上算，再截断。
  Id<Expr> combine_constants(const std::vector<Id<Expr>>& consts, const std::vector<Id<Expr>>& inv, bool add, const Type& ty, Id<Expr> at) {
    double result = add ? 0.0 : 1.0;
    for (Id<Expr> x : consts) {
      const double v = number(x);
      result = add ? result + v : result * v;
    }
    if (!add && result == 0.0) {
      for (Id<Expr> x : inv) if (is_zero(x)) return make_nan(ty, at);
      return make_number(ty, 0, at);
    }
    for (Id<Expr> x : inv) {
      // 非零常数除以 0：omc 在这里失败，Bindings 模块整个失败，没有这一段（实跑 LbnW038、LbnW039、LbnW043）。
      if (!add && is_zero(x)) return stop("constant division by zero", at);
      const double v = number(x);
      result = add ? result - v : result / v;
    }
    if (ty.kind == TypeKind::Real) return real(result);
    if (ty.kind != TypeKind::Integer) return stop("constants of a non-numeric type", at);
    if (std::abs(result) >= kMaxExactInt) return stop("integer constant out of the exact Real range", at);
    return integer(static_cast<std::int64_t>(result));   // realInt 截断
  }
  // getConstantValue :1470-1479 = realValue(Ceval.evalExp(exp))。化简过的实参里 UNARY 包字面量的都折掉了（simplifyUnaryOp），CAST 包 Integer 字面量
  // 的也折掉了；剩下的是 CAST(Real, Real 字面量)：simplifyCast 只折 (REAL, INTEGER)（:1322-1344），别的原样包着。Alias 对 CAST(Real, x) 求导就造出
  // 这种（`CAST(1.0 - z)` 的导数是 CAST(Real, -1.0)，实跑探针 LwaTCast2），Ceval 把它求成那个 Real。CAST 到别的类型没核过。
  double number(Id<Expr> id) {
    const Expr& e = a_[id];
    if (e.kind == ExprKind::IntLit) return static_cast<double>(e.ival);
    if (e.kind == ExprKind::RealLit) return e.rval;
    if (e.kind == ExprKind::Cast && e.ty.kind == TypeKind::Real) return number(e.a);
    if (e.kind == ExprKind::Unary) return -number(e.a);
    stop("constant number that is not a literal", id);
    return 0.0;
  }

  // ---- simplifyBinary :929-944 → simplifyBinaryOp :946-978（标量支）

  Id<Expr> binary(Id<Expr> id) {
    const Expr& e = a_[id];
    const Id<Expr> x = simplify(e.a), y = simplify(e.b);
    if (!stop_.empty()) return id;
    if (is_literal(x) && is_literal(y)) return eval_binary(e, x, y, id);
    switch (e.bin) {
      case BinOp::Add: return binary_add(id, x, e.bin, y);
      case BinOp::Sub: return binary_sub(id, x, e.bin, y);
      case BinOp::Mul: return binary_mul(id, x, y, false);
      case BinOp::Div: return binary_div(id, x, y);
      case BinOp::Pow:                       // simplifyBinaryPow :1090-1103
        if (is_zero(y)) return make_number(e.ty, 1, id);
        if (is_one(y)) return x;
        return make_binary(id, x, e.bin, y);
    }
    YMC_UNREACHABLE("BinOp");
  }
  // BINARY(exp1, op, exp2)：运算的类型就是 at 那个节点的。没变就沿用原节点（节点可能被共用，不改）。
  Id<Expr> make_binary(Id<Expr> at, Id<Expr> x, BinOp op, Id<Expr> y) {
    const Expr& e = a_[at];
    if (e.kind == ExprKind::Binary && e.bin == op && e.a == x && e.b == y) return at;
    Expr out = e;
    out.kind = ExprKind::Binary;
    out.bin = op;
    out.a = x;
    out.b = y;
    return a_.add(std::move(out));
  }
  static BinOp invert(BinOp op) { return op == BinOp::Add ? BinOp::Sub : BinOp::Add; }   // Operator.invert，只用在加减上
  // simplifyBinaryAdd :980-1006。String 的 `+`：isZero、isNegated 都假，落到末支。
  Id<Expr> binary_add(Id<Expr> at, Id<Expr> x, BinOp op, Id<Expr> y) {
    if (is_zero(x)) return y;
    if (is_zero(y)) return x;
    if (is_negated(x)) {
      if (is_negated(y)) return negate(make_binary(at, negate(x), op, negate(y)));   // (-e1) + (-e2) = -(e1 + e2)
      return binary_sub(at, y, invert(op), negate(x));                               // (-e1) + e2 = e2 - e1
    }
    if (is_negated(y)) return binary_sub(at, x, invert(op), negate(y));             // e1 + (-e2) = e1 - e2
    return make_binary(at, x, op, y);
  }
  // simplifyBinarySub :1008-1037。
  Id<Expr> binary_sub(Id<Expr> at, Id<Expr> x, BinOp op, Id<Expr> y) {
    if (is_zero(x)) return negate(y);
    if (is_zero(y)) return x;
    if (is_equal(x, y)) return make_number(a_[at].ty, 0, at);                     // makeZero(Operator.typeOf(op))
    if (is_negated(x)) {
      if (is_negated(y)) return make_binary(at, negate(y), op, negate(x));          // (-e1) - (-e2) = e2 - e1
      return negate(binary_add(at, negate(x), invert(op), y));                      // (-e1) - e2 = -(e1 + e2)
    }
    if (is_negated(y)) return binary_add(at, x, invert(op), negate(y));             // e1 - (-e2) = e1 + e2
    return make_binary(at, x, op, y);
  }
  // simplifyBinaryMul :1039-1061：只认直接的字面量 0 / 1（不看 CAST、UNARY），左边不是就换过来再看一次。
  Id<Expr> binary_mul(Id<Expr> at, Id<Expr> x, Id<Expr> y, bool switched) {
    const Expr& p = a_[x];
    if ((p.kind == ExprKind::IntLit && p.ival == 0) || (p.kind == ExprKind::RealLit && p.rval == 0.0)) return x;
    if ((p.kind == ExprKind::IntLit && p.ival == 1) || (p.kind == ExprKind::RealLit && p.rval == 1.0)) return y;
    return switched ? make_binary(at, y, BinOp::Mul, x) : binary_mul(at, y, x, true);
  }
  // simplifyBinaryDiv :1063-1088。原文的符号判断两次都看 exp1（`match (isNegated(exp1), isNegated(exp1))`），照抄：只走 (true, true) 和
  // (false, false) 两支，分子带负号时分母也被取负。
  Id<Expr> binary_div(Id<Expr> at, Id<Expr> x, Id<Expr> y) {
    if (is_one(y)) return x;
    if (is_one(y, -1)) return negate(x);
    if (is_zero(x)) {
      // isNonZero = isPositive or isNegative（NFExpression.mo:4518-4585）：cref 要查变量的 min / max 属性，没接。
      bool nonzero = false;
      if (!sign_known(y, nonzero)) return stop("0 / reference", at);
      if (nonzero) return x;
    }
    if (is_negated(x)) return make_binary(at, negate(x), BinOp::Div, negate(y));
    return make_binary(at, x, BinOp::Div, y);
  }
  // isPositive / isNegative 不查属性的部分：字面量（经 CAST、UNARY）。碰到别的返回 false（那两个函数在 else 支也是假，只有 CREF 不是）。
  bool sign_known(Id<Expr> id, bool& nonzero) const {
    const Expr& e = a_[id];
    switch (e.kind) {
      case ExprKind::IntLit: nonzero = e.ival != 0; return true;
      case ExprKind::RealLit: nonzero = e.rval != 0.0; return true;
      case ExprKind::Cast:
      case ExprKind::Unary: return sign_known(e.a, nonzero);
      case ExprKind::Cref: return false;
      default: nonzero = false; return true;
    }
  }
  Id<Expr> with_operands(Id<Expr> id, Id<Expr> x, Id<Expr> y) {
    Expr out = a_[id];
    out.a = x;
    out.b = y;
    return a_.add(std::move(out));
  }
  // Ceval.evalBinaryOp NFCeval.mo:903-1125 的标量支：默认路径折过的这里一般不会再来，能来的是这一遍才变成字面量的操作数（Early Inline 代进字面量
  // 实参，`x ^ (z / z)`），和默认路径不化简的位置（方程里 assert 的消息、Bindings 造的 NaN）。evalBinaryAdd / Sub / Mul :965-1056、Div :1058-1093、
  // Pow :1095-1129。Integer 照原文是 Integer 运算，结果超出 2^53 的没核过，SKIP。
  // 两边都是字面量时 simplifyBinaryOp 直接调 Ceval，没有 try（:954-955）：Ceval 接不住的组合（Integer 的 / 和 ^、Integer 混 Real、String 的 - * /）
  // 让整个模块失败，omc 没有这一段，这里 SKIP 正合适。接得住的：
  //   - evalBinaryDiv 的第一支 `(_, REAL(0.0))`（:1065-1075）：无 EvalTarget 时原样留 BINARY(e1, /, 0.0)，被除数是什么都行（实跑 LbnW040：
  //     Bindings 造的 NaN `0.0 / 0.0` 到 Simplify 1 不变）；
  //   - evalBinaryAdd 的 STRING 支（:977-978）：拼接（实跑 Wa1：assert 消息 `"a" + "b"` 前端不化简，这里成 "ab"）。
  Id<Expr> eval_binary(const Expr& e, Id<Expr> x, Id<Expr> y, Id<Expr> at) {
    const Expr& p = a_[x];
    const Expr& q = a_[y];
    if (e.bin == BinOp::Div && q.kind == ExprKind::RealLit && q.rval == 0.0) return make_binary(at, x, BinOp::Div, y);
    if (e.bin == BinOp::Add && p.kind == ExprKind::StringLit && q.kind == ExprKind::StringLit) {
      Expr out;
      out.kind = ExprKind::StringLit;
      out.ty = Type::string();
      out.sval = in_.intern(std::string(in_.str(p.sval)) + std::string(in_.str(q.sval)));
      return a_.add(std::move(out));
    }
    if (p.kind == ExprKind::RealLit && q.kind == ExprKind::RealLit) {
      switch (e.bin) {
        case BinOp::Add: return real(p.rval + q.rval);
        case BinOp::Sub: return real(p.rval - q.rval);
        case BinOp::Mul: return real(p.rval * q.rval);
        case BinOp::Div: return real(p.rval / q.rval);
        case BinOp::Pow:
          if (p.rval < 0 && std::trunc(q.rval) != q.rval) return with_operands(at, x, y);   // :1103 负底非整指数，留着
          return real(std::pow(p.rval, q.rval));
      }
    }
    if (p.kind == ExprKind::IntLit && q.kind == ExprKind::IntLit && e.bin != BinOp::Div && e.bin != BinOp::Pow) {
      // 结果落在 2^53 以内才算：这个范围里没有溢出，和 Real 上算的也一致（同 combine_constants）。
      const double v = e.bin == BinOp::Add   ? static_cast<double>(p.ival) + static_cast<double>(q.ival)
                       : e.bin == BinOp::Sub ? static_cast<double>(p.ival) - static_cast<double>(q.ival)
                                             : static_cast<double>(p.ival) * static_cast<double>(q.ival);
      if (std::abs(v) >= kMaxExactInt) return stop("integer constant out of the exact Real range", at);
      return integer(e.bin == BinOp::Add ? p.ival + q.ival : e.bin == BinOp::Sub ? p.ival - q.ival : p.ival * q.ival);
    }
    return stop("literal binary expression", at);
  }

  // ---- simplifyUnary :1116-1160

  Id<Expr> unary(Id<Expr> id) {
    const Expr& e = a_[id];
    if (a_[e.a].kind == ExprKind::Unary) return simplify(a_[e.a].a);
    const Id<Expr> se = simplify(e.a);
    if (!stop_.empty()) return id;
    if (is_literal(se)) {                    // Ceval.evalUnaryOp :1364-1402
      if (is_zero(se)) return se;
      const ExprKind k = a_[se].kind;
      return k == ExprKind::IntLit || k == ExprKind::RealLit ? negate(se) : stop("negated non-numeric literal", id);
    }
    return a_[se].kind == ExprKind::Unary ? a_[se].a : negate(se);   // simplifyUnarySign(se, true)：se 化简过，里面不会再套 UNARY
  }

  // ---- simplifyLogicBinary :1162-1242、simplifyLogicUnary :1244-1266

  Id<Expr> logical(Id<Expr> id) {
    const Expr& e = a_[id];
    const Id<Expr> x = simplify(e.a), y = simplify(e.b);
    if (!stop_.empty()) return id;
    const Expr& p = a_[x];
    const Expr& q = a_[y];
    // (ARRAY, ARRAY) 支逐元素算（:1199-1205）：when 拆分拼出来的 `not {a, b} and {c, d}` 才会有，没核过。
    if (p.kind == ExprKind::Array && q.kind == ExprKind::Array) return stop("logical operation on two arrays", id);
    const bool absorbing = e.log == LogOp::Or;      // and：false 吸收、true 消去；or 反过来
    if (p.kind == ExprKind::BoolLit) return p.bval == absorbing ? x : y;
    if (q.kind == ExprKind::BoolLit) return q.bval == absorbing ? y : x;
    return x == e.a && y == e.b ? id : with_operands(id, x, y);
  }
  Id<Expr> logical_not(Id<Expr> id) {
    const Expr& e = a_[id];
    if (a_[e.a].kind == ExprKind::LogicalNot) return simplify(a_[e.a].a);
    const Id<Expr> se = simplify(e.a);
    if (!stop_.empty()) return id;
    if (a_[se].kind == ExprKind::BoolLit) return boolean(!a_[se].bval);   // Ceval.evalLogicUnaryNot :1520-1535
    if (is_literal(se)) return stop("not of a non-Boolean literal", id);
    // isLiteral 的 ARRAY 支（NFExpression.mo:4660）：元素全是字面量的数组也算，evalLogicUnaryNot 逐元素取反（:1528）。when 拆分把前一支的
    // 条件取反拼进后一支（`not {false, false} and x > 9.0`），实跑 LwbC089l 成 `{true, true} and x > 9.0`。
    if (a_[se].kind == ExprKind::Array && all_literal(a_[se].args)) {
      std::vector<Id<Expr>> elems;
      for (Id<Expr> x : a_[se].args) {
        if (a_[x].kind != ExprKind::BoolLit) return stop("not of a non-Boolean literal array", id);
        elems.push_back(boolean(!a_[x].bval));
      }
      return with_args(se, std::move(elems));
    }
    if (se == e.a) return id;
    Expr out = e;
    out.a = se;
    return a_.add(std::move(out));
  }

  // ---- simplifyRelation :1268-1284，字面量 Ceval.evalRelationOp NFCeval.mo:1537-1777

  template <class T>
  static bool holds(RelOp op, const T& a, const T& b) {
    switch (op) {
      case RelOp::Lt: return a < b;
      case RelOp::Le: return a <= b;
      case RelOp::Gt: return a > b;
      case RelOp::Ge: return a >= b;
      case RelOp::Eq: return a == b;
      case RelOp::Ne: return a != b;
    }
    YMC_UNREACHABLE("RelOp");
  }
  Id<Expr> relation(Id<Expr> id) {
    const Expr& e = a_[id];
    const Id<Expr> x = simplify(e.a), y = simplify(e.b);
    if (!stop_.empty()) return id;
    if (!is_literal(x) || !is_literal(y)) return x == e.a && y == e.b ? id : with_operands(id, x, y);
    const Expr& p = a_[x];
    const Expr& q = a_[y];
    if (p.kind != q.kind) return stop("relation on literals of different kinds", id);
    switch (p.kind) {
      case ExprKind::IntLit: return boolean(holds(e.rel, p.ival, q.ival));
      case ExprKind::RealLit: return boolean(holds(e.rel, p.rval, q.rval));
      case ExprKind::BoolLit: return boolean(holds(e.rel, p.bval, q.bval));
      case ExprKind::StringLit: return boolean(holds(e.rel, in_.str(p.sval), in_.str(q.sval)));   // stringCompare 按字节比
      case ExprKind::EnumLit: return boolean(holds(e.rel, p.enum_index, q.enum_index));
      default: return stop("relation on literals", id);
    }
  }

  // ---- simplifyIf :1286-1320

  Id<Expr> if_exp(Id<Expr> id) {
    const Expr& e = a_[id];
    const Id<Expr> cond = simplify(e.a);
    if (!stop_.empty()) return id;
    if (a_[cond].kind == ExprKind::BoolLit) return simplify(a_[cond].bval ? e.b : e.c);
    const Id<Expr> tb = simplify(e.b), fb = simplify(e.c);
    if (!stop_.empty()) return id;
    if (is_equal(tb, fb)) return tb;         // if cond then x else x => x
    if (a_[tb].kind == ExprKind::BoolLit && a_[fb].kind == ExprKind::BoolLit)
      return a_[tb].bval ? cond : logic_negate(cond);
    if (cond == e.a && tb == e.b && fb == e.c) return id;
    Expr out = e;
    out.a = cond;
    out.b = tb;
    out.c = fb;
    return a_.add(std::move(out));
  }

  // ARRAY 支 :92-97：非 literal 的数组逐元素化简（literal 的元素都是字面量，化简也不变，不用分）。Type.simplify 只动维的式子，标量元素的数组不变。
  Id<Expr> array(Id<Expr> id) {
    std::vector<Id<Expr>> elems;
    for (Id<Expr> x : a_[id].args) elems.push_back(simplify(x));
    if (!stop_.empty() || elems == a_[id].args) return id;
    return with_args(id, std::move(elems));
  }
  // simplifyRange :135-164：起点、步长、终点各化简；变了就重新定型（类型不打印）。Expr 的 Range：a 起点，有步长时 b 步长、c 终点，否则 b 终点。
  Id<Expr> range(Id<Expr> id) {
    const Expr& e = a_[id];
    const Id<Expr> x = simplify(e.a), y = simplify(e.b), z = e.c.is_valid() ? simplify(e.c) : e.c;
    if (!stop_.empty()) return id;
    if (x == e.a && y == e.b && z == e.c) return id;
    Expr out = e;
    out.a = x;
    out.b = y;
    out.c = z;
    return a_.add(std::move(out));
  }

  // simplifyCast :1322-1344
  Id<Expr> cast(Id<Expr> id) {
    const Expr& e = a_[id];
    const Id<Expr> x = simplify(e.a);
    if (!stop_.empty()) return id;
    if (e.ty.kind == TypeKind::Real && a_[x].kind == ExprKind::IntLit) return real(static_cast<double>(a_[x].ival));
    if (x == e.a) return id;
    Expr out = e;
    out.a = x;
    return a_.add(std::move(out));
  }

  ExprArena& a_;
  Interner& in_;
  ExpCompare compare_;
  std::string& stop_;
};

}  // namespace

Id<Expr> simplify(ExprArena& a, Interner& in, Id<Expr> e, std::string& unsupported) {
  return Simplifier(a, in, unsupported).simplify(e);
}

}  // namespace ymc::bdae
