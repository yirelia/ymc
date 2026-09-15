// Absyn 反解析实现（方案 §5 S1）。
#include "absyn/unparse.h"

#include <cstdio>

namespace ymc {

namespace {

const char* restriction_kw(Restriction r) {
  switch (r) {
    case Restriction::Class: return "class";
    case Restriction::Model: return "model";
    case Restriction::Record: return "record";
    case Restriction::Block: return "block";
    case Restriction::Connector: return "connector";
    case Restriction::ExpandableConnector: return "expandable connector";
    case Restriction::Type: return "type";
    case Restriction::Package: return "package";
    case Restriction::Function: return "function";
    case Restriction::Operator: return "operator";
    case Restriction::OperatorRecord: return "operator record";
  }
  return "class";
}

const char* op_sym(Op op) {
  switch (op) {
    case Op::Add: return " + ";
    case Op::Sub: return " - ";
    case Op::Mul: return " * ";
    case Op::Div: return " / ";
    case Op::Pow: return " ^ ";
    case Op::EWAdd: return " .+ ";
    case Op::EWSub: return " .- ";
    case Op::EWMul: return " .* ";
    case Op::EWDiv: return " ./ ";
    case Op::EWPow: return " .^ ";
    case Op::Lt: return " < ";
    case Op::Le: return " <= ";
    case Op::Gt: return " > ";
    case Op::Ge: return " >= ";
    case Op::Eq: return " == ";
    case Op::Ne: return " <> ";
    case Op::And: return " and ";
    case Op::Or: return " or ";
    case Op::UPlus: return "+";
    case Op::UMinus: return "-";
    case Op::Not: return "not ";
  }
  return "?";
}

// 二元运算符的优先级（数字越大越紧）。
int binop_prec(Op op) {
  switch (op) {
    case Op::Or: return 2;
    case Op::And: return 3;
    case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: case Op::Eq: case Op::Ne: return 5;
    case Op::Add: case Op::Sub: case Op::EWAdd: case Op::EWSub: return 6;
    case Op::Mul: case Op::Div: case Op::EWMul: case Op::EWDiv: return 7;
    case Op::Pow: case Op::EWPow: return 8;
    default: return 6;
  }
}

class Unparser {
 public:
  explicit Unparser(const Absyn& a) : a_(a) {}

  std::string run() {
    if (a_.has_within) {
      out_ += "within";
      if (!a_.within_path.empty()) {
        out_ += ' ';
        path(a_.within_leading_dot, a_.within_path);
      }
      out_ += ";\n";
    }
    for (Id<ClassDef> id : a_.top) {
      class_def(id, "");
      out_ += ";\n";
    }
    return std::move(out_);
  }

 private:
  // ---- 名字/文本 ----
  void sym(Symbol s) { out_ += a_.interner.str(s); }
  void path(bool leading_dot, const std::vector<Symbol>& p) {
    if (leading_dot) out_ += '.';
    for (std::size_t i = 0; i < p.size(); ++i) {
      if (i) out_ += '.';
      sym(p[i]);
    }
  }
  void string_lit(Symbol s) {
    out_ += '"';
    for (char c : a_.interner.str(s)) {
      switch (c) {
        case '"': out_ += "\\\""; break;
        case '\\': out_ += "\\\\"; break;
        case '\n': out_ += "\\n"; break;
        case '\t': out_ += "\\t"; break;
        case '\r': out_ += "\\r"; break;
        default: out_ += c; break;
      }
    }
    out_ += '"';
  }

  // ---- 表达式 ----
  int prec_of(const Exp& e) {
    switch (e.kind) {
      case ExpKind::IfExp: return 0;
      case ExpKind::Range: return 1;
      case ExpKind::Binary: return binop_prec(e.op);
      case ExpKind::Unary:
        return e.op == Op::Not ? 4 : 6;  // not / 一元 +-
      default: return 100;               // 字面量、cref、call、array
    }
  }

  void exp(Id<Exp> id, int min_prec) {
    const Exp& e = a_.exps[id];
    int p = prec_of(e);
    bool paren = p < min_prec;
    if (paren) out_ += '(';
    emit_exp(e);
    if (paren) out_ += ')';
  }

  void emit_exp(const Exp& e) {
    switch (e.kind) {
      case ExpKind::IntLit: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(e.ival));
        out_ += buf;
        break;
      }
      case ExpKind::RealLit: {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", e.rval);
        // 保证反解析回来仍是 Real（含小数点或指数）：如 "1" → "1.0"。
        bool has_dot_or_exp = false;
        for (const char* q = buf; *q; ++q)
          if (*q == '.' || *q == 'e' || *q == 'E' || *q == 'n' || *q == 'i') has_dot_or_exp = true;
        out_ += buf;
        if (!has_dot_or_exp) out_ += ".0";
        break;
      }
      case ExpKind::BoolLit: out_ += e.bval ? "true" : "false"; break;
      case ExpKind::StringLit: string_lit(e.sval); break;
      case ExpKind::CrefExp: cref(e.cref); break;
      case ExpKind::End: out_ += "end"; break;
      case ExpKind::Colon: out_ += ':'; break;
      case ExpKind::Binary: {
        int p = binop_prec(e.op);
        bool right_assoc = (e.op == Op::Pow || e.op == Op::EWPow);
        exp(e.a, right_assoc ? p + 1 : p);
        out_ += op_sym(e.op);
        exp(e.b, right_assoc ? p : p + 1);
        break;
      }
      case ExpKind::Unary:
        out_ += op_sym(e.op);
        exp(e.a, e.op == Op::Not ? 5 : 7);
        break;
      case ExpKind::Range:
        exp(e.a, 2);
        out_ += ':';
        exp(e.b, 2);
        if (e.c.is_valid()) {
          out_ += ':';
          exp(e.c, 2);
        }
        break;
      case ExpKind::IfExp:
        out_ += "if ";
        exp(e.a, 0);
        out_ += " then ";
        exp(e.b, 0);
        out_ += " else ";
        exp(e.c, 0);
        break;
      case ExpKind::Call:
        cref(e.cref);
        out_ += '(';
        args(e.args);
        out_ += ')';
        break;
      case ExpKind::Array:
        out_ += '{';
        args(e.args);
        out_ += '}';
        break;
    }
  }

  void args(const std::vector<Id<Exp>>& v) {
    for (std::size_t i = 0; i < v.size(); ++i) {
      if (i) out_ += ", ";
      exp(v[i], 0);
    }
  }

  void cref(Id<Cref> id) {
    const Cref& c = a_.crefs[id];
    if (c.leading_dot) out_ += '.';
    for (std::size_t i = 0; i < c.parts.size(); ++i) {
      if (i) out_ += '.';
      sym(c.parts[i].name);
      if (!c.parts[i].subscripts.empty()) subscripts(c.parts[i].subscripts);
    }
  }

  void subscripts(const std::vector<Id<Exp>>& subs) {
    out_ += '[';
    args(subs);
    out_ += ']';
  }

  // ---- 修饰 ----
  void modification(Id<Modification> id) {
    if (!id.is_valid()) return;
    const Modification& m = a_.mods[id];
    if (!m.args.empty()) {
      out_ += '(';
      for (std::size_t i = 0; i < m.args.size(); ++i) {
        if (i) out_ += ", ";
        const ModArg& arg = m.args[i];
        if (arg.each_) out_ += "each ";
        if (arg.final_) out_ += "final ";
        path(false, arg.path);
        if (arg.sub.is_valid()) modification(arg.sub);
        if (arg.binding.is_valid()) {
          out_ += " = ";
          exp(arg.binding, 0);
        }
        if (arg.string_comment.is_valid()) {
          out_ += ' ';
          string_lit(arg.string_comment);
        }
      }
      out_ += ')';
    }
    if (m.binding.is_valid()) {
      out_ += m.assign ? " := " : " = ";
      exp(m.binding, 0);
    }
  }

  // ---- 元素 / 类 ----
  void type_prefix(const TypePrefix& tp) {
    if (tp.flow == TypePrefix::Flow::Flow) out_ += "flow ";
    else if (tp.flow == TypePrefix::Flow::Stream) out_ += "stream ";
    if (tp.var == TypePrefix::Variability::Discrete) out_ += "discrete ";
    else if (tp.var == TypePrefix::Variability::Parameter) out_ += "parameter ";
    else if (tp.var == TypePrefix::Variability::Constant) out_ += "constant ";
    if (tp.caus == TypePrefix::Causality::Input) out_ += "input ";
    else if (tp.caus == TypePrefix::Causality::Output) out_ += "output ";
  }

  void component_clause(const ComponentClause& cc, const std::string& indent) {
    out_ += indent;
    type_prefix(cc.prefix);
    path(cc.type_leading_dot, cc.type_path);
    if (!cc.type_subscripts.empty()) subscripts(cc.type_subscripts);
    out_ += ' ';
    for (std::size_t i = 0; i < cc.comps.size(); ++i) {
      if (i) out_ += ", ";
      const ComponentDecl& d = cc.comps[i];
      sym(d.name);
      if (!d.subscripts.empty()) subscripts(d.subscripts);
      modification(d.mod);
      if (d.string_comment.is_valid()) {
        out_ += ' ';
        string_lit(d.string_comment);
      }
    }
    out_ += ";\n";
  }

  void element(const Element& el, const std::string& indent) {
    switch (el.kind) {
      case ElementKind::Component:
        component_clause(el.comp, indent);
        break;
      case ElementKind::Extends:
        out_ += indent + "extends ";
        path(el.ext.leading_dot, el.ext.path);
        modification(el.ext.mod);
        out_ += ";\n";
        break;
      case ElementKind::Import:
        out_ += indent + "import ";
        if (el.imp.alias.is_valid()) {
          sym(el.imp.alias);
          out_ += " = ";
        }
        path(el.imp.leading_dot, el.imp.path);
        if (el.imp.wildcard) out_ += ".*";
        out_ += ";\n";
        break;
      case ElementKind::ClassDef:
        class_def(el.cls, indent);
        out_ += ";\n";
        break;
    }
  }

  void class_def(Id<ClassDef> id, const std::string& indent) {
    const ClassDef& c = a_.classes[id];
    out_ += indent;
    if (c.is_final) out_ += "final ";
    if (c.is_encapsulated) out_ += "encapsulated ";
    if (c.is_partial) out_ += "partial ";
    out_ += restriction_kw(c.restriction);
    out_ += ' ';
    sym(c.name);
    if (c.string_comment.is_valid()) {
      out_ += ' ';
      string_lit(c.string_comment);
    }
    out_ += '\n';
    std::string body_indent = indent + "  ";
    // 元素：先 public（无关键字），再 protected
    for (const Element& el : c.elements)
      if (!el.is_protected) element(el, body_indent);
    bool any_prot = false;
    for (const Element& el : c.elements)
      if (el.is_protected) any_prot = true;
    if (any_prot) {
      out_ += indent + "protected\n";
      for (const Element& el : c.elements)
        if (el.is_protected) element(el, body_indent);
    }
    // 方程：initial equation 与 equation 分别成段
    bool any_init = false, any_eq = false;
    for (const Equation& eq : c.equations) {
      if (eq.is_initial) any_init = true;
      else any_eq = true;
    }
    if (any_init) {
      out_ += indent + "initial equation\n";
      for (const Equation& eq : c.equations)
        if (eq.is_initial) equation(eq, body_indent);
    }
    if (any_eq) {
      out_ += indent + "equation\n";
      for (const Equation& eq : c.equations)
        if (!eq.is_initial) equation(eq, body_indent);
    }
    out_ += indent + "end ";
    sym(c.name);
  }

  void equation(const Equation& eq, const std::string& indent) {
    out_ += indent;
    switch (eq.kind) {
      case EqKind::Equality:
        exp(eq.lhs, 0);
        out_ += " = ";
        exp(eq.rhs, 0);
        break;
      case EqKind::Connect:
        out_ += "connect(";
        exp(eq.lhs, 0);
        out_ += ", ";
        exp(eq.rhs, 0);
        out_ += ")";
        break;
      case EqKind::FunctionCall:
        exp(eq.lhs, 0);
        break;
    }
    if (eq.string_comment.is_valid()) {
      out_ += ' ';
      string_lit(eq.string_comment);
    }
    out_ += ";\n";
  }

  const Absyn& a_;
  std::string out_;
};

}  // namespace

std::string unparse(const Absyn& a) { return Unparser(a).run(); }

}  // namespace ymc
