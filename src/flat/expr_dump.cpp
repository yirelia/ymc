// 类型化表达式打印实现。括号规则逐条照 OMC Expression.shouldParenthesize/priority
//（Expression.mo:10757-10946，研究底稿 R13 §4.2 的优先级表 + 结合性规则）。
#include "flat/expr_dump.h"

#include "absyn/unparse.h"   // real_literal_text
#include "base/strings.h"    // escape_modelica_string

namespace ymc {

namespace {

class ExprDumper {
 public:
  ExprDumper(const ExprArena& a, const Interner& in) : a_(a), in_(in) {}

  std::string run(Id<Expr> e) {
    out_.clear();
    exp(e);
    return std::move(out_);
  }

 private:
  // 优先级（R13 §4.2 表；数值越小绑得越紧）。lhs = 该表达式作为左操作数/外层运算的左侧时取左列，否则右列。
  // 负 Real 字面量按一元负（4）；负 Integer 字面量按 0（→ `k * -1` 不加括号，R13）。
  int priority(const Expr& e, bool lhs) const {
    switch (e.kind) {
      case ExprKind::Binary:
        switch (e.bin) {
          case BinOp::Pow: return 1;
          case BinOp::Mul: return lhs ? 2 : 3;
          case BinOp::Div: return 2;
          case BinOp::Add: return lhs ? 5 : 6;
          case BinOp::Sub: return 5;
        }
        return 0;
      case ExprKind::Unary: return 4;              // 一元负
      case ExprKind::RealLit: return e.rval < 0 ? 4 : 0;
      case ExprKind::Relation: return 6;
      case ExprKind::LogicalNot: return 7;
      case ExprKind::Logical: return e.log == LogOp::And ? 8 : 9;
      case ExprKind::Range: return 10;
      case ExprKind::IfExp: return 11;
      default: return 0;                            // cref/call/字面量/cast/array…
    }
  }
  // 幂运算不可结合（相等优先级时左操作数要加括号）。
  static bool is_pow(const Expr& e) { return e.kind == ExprKind::Binary && e.bin == BinOp::Pow; }
  // 可结合运算（相等优先级时右操作数不加括号）：标量 + * 与逻辑 and/or（R13 §4.2）。
  static bool is_associative(const Expr& op) {
    if (op.kind == ExprKind::Logical) return true;
    return op.kind == ExprKind::Binary && (op.bin == BinOp::Add || op.bin == BinOp::Mul);
  }

  // 是否给操作数加括号（R13 §4.2）。operand = 子表达式，op = 外层运算，lhs = 子在左侧。
  bool should_paren(const Expr& operand, const Expr& op, bool lhs) const {
    if (operand.kind == ExprKind::Unary) return true;   // 一元负操作数一律括号
    int po = priority(operand, lhs);
    int pe = priority(op, lhs);
    if (po > pe) return true;
    if (po == pe) return lhs ? is_pow(operand) : !is_associative(op);
    return false;
  }

  void operand(Id<Expr> child, const Expr& op, bool lhs) {
    const Expr& c = a_[child];
    bool paren = should_paren(c, op, lhs);
    if (paren) out_ += '(';
    exp(child);
    if (paren) out_ += ')';
  }

  static const char* binop(BinOp b) {
    switch (b) {
      // OMC ExpressionDump.mo:112-116 binopSymbol1：全部二元算符两侧带空格。
      case BinOp::Add: return " + ";
      case BinOp::Sub: return " - ";
      case BinOp::Mul: return " * ";
      case BinOp::Div: return " / ";
      case BinOp::Pow: return " ^ ";
    }
    return "?";
  }
  static const char* relop(RelOp r) {
    switch (r) {
      case RelOp::Lt: return " < ";
      case RelOp::Le: return " <= ";
      case RelOp::Gt: return " > ";
      case RelOp::Ge: return " >= ";
      case RelOp::Eq: return " == ";
      case RelOp::Ne: return " <> ";
    }
    return "?";
  }
  // CAST 的类型名：Real/Integer/String 用类型名，Boolean 在 cast 里叫 Bool（R13 §4.1）。
  static const char* cast_type_name(TypeKind k) {
    switch (k) {
      case TypeKind::Real: return "Real";
      case TypeKind::Integer: return "Integer";
      case TypeKind::Boolean: return "Bool";
      case TypeKind::String: return "String";
      default: return "Real";
    }
  }

  void dotted(const std::vector<Symbol>& parts) {
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (i) out_ += '.';
      out_ += in_.str(parts[i]);
    }
  }

  void exp(Id<Expr> id) {
    const Expr& e = a_[id];
    switch (e.kind) {
      case ExprKind::IntLit: out_ += std::to_string(e.ival); break;
      case ExprKind::RealLit: out_ += real_literal_text(e.rval); break;
      case ExprKind::BoolLit: out_ += e.bval ? "true" : "false"; break;
      case ExprKind::StringLit:
        out_ += '"';
        out_ += escape_modelica_string(in_.str(e.sval), /*nl=*/false);
        out_ += '"';
        break;
      case ExprKind::EnumLit: dotted(e.enum_path); break;
      case ExprKind::Cref:
        for (std::size_t i = 0; i < e.cref.parts.size(); ++i) {
          if (i) out_ += '.';
          out_ += in_.str(e.cref.parts[i].name);
          const auto& subs = e.cref.parts[i].subscripts;
          if (!subs.empty()) {   // `x[2]`、`m[1,2]`：`,` 无空格（R13 §4.2 dumpSubscripts）
            out_ += '[';
            for (std::size_t k = 0; k < subs.size(); ++k) {
              if (k) out_ += ',';
              // 下标位置的 Wild = 整维切片 `:`（Subscript.WHOLE；flatten 的 slice_subscript 产出）。
              if (a_[subs[k]].kind == ExprKind::Wild) out_ += ':';
              else exp(subs[k]);
            }
            out_ += ']';
          }
        }
        break;
      case ExprKind::Binary:
        operand(e.a, e, true);
        out_ += binop(e.bin);
        operand(e.b, e, false);
        break;
      case ExprKind::Unary:
        out_ += '-';
        operand(e.a, e, false);
        break;
      case ExprKind::Relation:
        operand(e.a, e, true);
        out_ += relop(e.rel);
        operand(e.b, e, false);
        break;
      case ExprKind::Logical:
        operand(e.a, e, true);
        out_ += e.log == LogOp::And ? " and " : " or ";
        operand(e.b, e, false);
        break;
      case ExprKind::LogicalNot:
        out_ += "not ";
        operand(e.a, e, false);
        break;
      case ExprKind::IfExp:
        out_ += "if ";
        exp(e.a);
        out_ += " then ";
        exp(e.b);
        out_ += " else ";
        exp(e.c);   // elseif 是嵌进 c 的另一个 IfExp，会自然打成 "else if …"（R13 §4.1）
        break;
      case ExprKind::Cast:
        out_ += "/*";
        out_ += cast_type_name(e.ty.kind);
        out_ += "*/(";
        exp(e.a);
        out_ += ')';
        break;
      case ExprKind::Call:
        dotted(e.call_name);
        out_ += '(';
        for (std::size_t i = 0; i < e.args.size(); ++i) {
          if (i) out_ += ", ";
          exp(e.args[i]);
        }
        out_ += ')';
        break;
      // —— v1 不产出，先给最简打印，数组/元组等竖切片再精修 ——
      case ExprKind::Array:
        out_ += '{';
        for (std::size_t i = 0; i < e.args.size(); ++i) {
          if (i) out_ += ", ";
          exp(e.args[i]);
        }
        out_ += '}';
        break;
      case ExprKind::Range:
        operand(e.a, e, false);
        out_ += ':';
        if (e.c.is_valid()) { operand(e.b, e, false); out_ += ':'; operand(e.c, e, false); }
        else operand(e.b, e, false);
        break;
      case ExprKind::Tuple:
        out_ += '(';
        for (std::size_t i = 0; i < e.args.size(); ++i) {
          if (i) out_ += ", ";
          exp(e.args[i]);
        }
        out_ += ')';
        break;
      case ExprKind::Wild: out_ += '_'; break;
      case ExprKind::End: out_ += "end"; break;
      case ExprKind::Size:
        out_ += "size(";
        for (std::size_t i = 0; i < e.args.size(); ++i) {
          if (i) out_ += ", ";
          exp(e.args[i]);
        }
        out_ += ')';
        break;
      case ExprKind::TupleElement: {
        // TSUB（ExpressionDumpTpl :131-136）：`<exp>[<ix>]`，exp 非字面量/cref/调用/数组/元组/cast/size/归约时加括号
        //（parenthesizeSubExp :194-210）。a = 元组表达式，ival = 1 基输出序号（TupleOperation1 `-f(time)[1]`）。
        const ExprKind k = a_[e.a].kind;
        const bool bare = k == ExprKind::IntLit || k == ExprKind::RealLit || k == ExprKind::StringLit ||
                          k == ExprKind::BoolLit || k == ExprKind::EnumLit || k == ExprKind::Cref ||
                          k == ExprKind::Call || k == ExprKind::Array || k == ExprKind::Tuple ||
                          k == ExprKind::Cast || k == ExprKind::Size || k == ExprKind::Reduction;
        if (!bare) out_ += '(';
        exp(e.a);
        if (!bare) out_ += ')';
        out_ += '[';
        out_ += std::to_string(e.ival);
        out_ += ']';
        break;
      }
      case ExprKind::Subscript:
      case ExprKind::Reduction:
        // 数组/归约竖切片再实现；先占位为可辨识的形态，避免静默产出错文本。
        out_ += "/*UNIMPL_EXPR*/";
        break;
    }
  }

  const ExprArena& a_;
  const Interner& in_;
  std::string out_;
};

}  // namespace

std::string dump_expr(const ExprArena& a, const Interner& in, Id<Expr> e) {
  return ExprDumper(a, in).run(e);
}

}  // namespace ymc
