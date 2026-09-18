// Absyn 反解析实现（方案 §5 S1，v2）。
#include "absyn/unparse.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ymc {

namespace {

// Real 字面量文本，对齐 OMC 的 realString（SimulationRuntime/c/meta/realString.c:76-84 → Ryu 最短往返十进制；
// 排版规则在 3rdParty/ryu/ryu/om_format.c 的 ryu_to_hr(real_output=1)）：指数在 [-3,5] 且（exp<=0 或
// exp-ndec<=3）用小数形式、整数值补 ".0"；否则 `d.ddde±x`。为什么要对齐：S2 起 dump 要与 OMC 逐行比对，
// `%.17g` 会把 9.81 打成 9.8100000000000005。GCC 10 没有浮点 std::to_chars，最短数字串用
// %.14e/%.15e/%.16e 逐级试 strtod 往返代替（≤15 位时与 Ryu 相同：15 位十进制网格比 double 的 ulp 粗）。
std::string real_text(double v) {
  if (std::isnan(v)) return "NaN";
  if (std::isinf(v)) return v < 0 ? "-Inf" : "Inf";
  char sci[40];
  for (int prec = 14; prec <= 16; ++prec) {
    std::snprintf(sci, sizeof sci, "%.*e", prec, v);
    if (prec == 16 || std::strtod(sci, nullptr) == v) break;
  }
  const char* p = sci;  // [-]d.ddddde±xx
  bool neg = *p == '-';
  if (neg) ++p;
  std::string digits;  // 有效数字，不含小数点
  for (; *p && *p != 'e'; ++p)
    if (*p != '.') digits += *p;
  int exp = std::atoi(p + 1);
  while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
  int ndec = static_cast<int>(digits.size()) - 1;
  std::string out = neg ? "-" : "";
  if (exp > 5 || exp < -3 || (exp > 0 && exp - ndec > 3)) {
    out += digits[0];
    if (ndec > 0) {
      out += '.';
      out += digits.substr(1);
    }
    out += 'e';
    out += std::to_string(exp);
    return out;
  }
  if (exp >= 0) {
    out += digits.substr(0, static_cast<std::size_t>(std::min(ndec, exp)) + 1);
    if (exp > ndec) out.append(static_cast<std::size_t>(exp - ndec), '0');
    else if (exp < ndec) {
      out += '.';
      out += digits.substr(static_cast<std::size_t>(exp) + 1);
    }
  } else {
    out += "0.";
    out.append(static_cast<std::size_t>(-exp - 1), '0');
    out += digits;
  }
  if (exp >= ndec) out += ".0";
  return out;
}

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
    case Restriction::OperatorFunction: return "operator function";
  }
  return "class";
}

// 运算符文本与间距逐条对齐 OMC Template/AbsynDumpTpl.tpl:1049-1075 dumpOperator：
// + - .+ .- 与关系/逻辑运算符两侧留空格，* / ^ .* ./ .^ 不留；一元 + - 紧贴，not 后一个空格。
const char* op_sym(Op op) {
  switch (op) {
    case Op::Add: return " + ";
    case Op::Sub: return " - ";
    case Op::Mul: return "*";
    case Op::Div: return "/";
    case Op::Pow: return "^";
    case Op::EWAdd: return " .+ ";
    case Op::EWSub: return " .- ";
    case Op::EWMul: return ".*";
    case Op::EWDiv: return "./";
    case Op::EWPow: return ".^";
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

class Unparser {
 public:
  explicit Unparser(const Absyn& a) : a_(a) {}

  // 供 S2 的 SCode dump 复用：单个表达式 / 字符串字面量的文本
  std::string exp_text(Id<Exp> e) {
    out_.clear();
    exp(e);
    return std::move(out_);
  }
  std::string string_text(Symbol s) {
    out_.clear();
    string_lit(s);
    return std::move(out_);
  }

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
  void string_lit(Symbol s) {  // 原文（转义序列原样），同 OMC AbsynDumpTpl.tpl dumpExp STRING
    out_ += '"';
    sym(s);
    out_ += '"';
  }
  // 字符串注释 + 注解
  void comment(Symbol str, Id<Modification> ann) {
    if (str.is_valid()) {
      out_ += ' ';
      string_lit(str);
    }
    annotation(ann);
  }
  void annotation(Id<Modification> ann) {
    if (!ann.is_valid()) return;
    out_ += " annotation";
    mod_args(ann);
  }

  // ---- 表达式 ----
  // 不按优先级补括号：源码里的括号已解析成单元素 Tuple（parser.cpp parse_primary），与 OMC 相同
  //（Modelica.g:1856-1861 TUPLE({e})；AbsynDumpTpl.tpl:857-864 dumpOperand 的加括号逻辑被注释掉）。
  // 这样 unparse 逐字等于 OMC 的 Absyn dump，代价是手工构造的 Absyn 树自己负责括号。
  void exp(Id<Exp> id) {
    const Exp& e = a_.exps[id];
    switch (e.kind) {
      case ExpKind::IntLit: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(e.ival));
        out_ += buf;
        break;
      }
      case ExpKind::RealLit:  // 源码拼写优先（OMC Absyn.REAL 存 String）；手工构造的节点才用 realString 规则
        if (e.sval.is_valid()) sym(e.sval);
        else out_ += real_text(e.rval);
        break;
      case ExpKind::BoolLit: out_ += e.bval ? "true" : "false"; break;
      case ExpKind::StringLit: string_lit(e.sval); break;
      case ExpKind::CrefExp: cref(e.cref); break;
      case ExpKind::End: out_ += "end"; break;
      case ExpKind::Colon: out_ += ':'; break;
      case ExpKind::Binary:
        exp(e.a);
        out_ += op_sym(e.op);
        exp(e.b);
        break;
      case ExpKind::Unary:
        out_ += op_sym(e.op);
        exp(e.a);
        break;
      case ExpKind::Range:
        exp(e.a);
        out_ += ':';
        exp(e.b);
        if (e.c.is_valid()) {
          out_ += ':';
          exp(e.c);
        }
        break;
      case ExpKind::IfExp: {  // elseif 链平铺（AbsynDumpTpl.tpl dumpIfExp/dumpElseIfExp）；`else if` 是嵌套节点
        out_ += "if ";
        exp(e.a);
        out_ += " then ";
        exp(e.b);
        const Exp* cur = &e;
        while (a_.exps[cur->c].kind == ExpKind::IfExp && a_.exps[cur->c].is_elseif) {
          cur = &a_.exps[cur->c];
          out_ += " elseif ";
          exp(cur->a);
          out_ += " then ";
          exp(cur->b);
        }
        out_ += " else ";
        exp(cur->c);
        break;
      }
      case ExpKind::Call:
        if (e.partial_fn) out_ += "function ";
        cref(e.cref);
        out_ += '(';
        call_args(e);
        out_ += ')';
        if (e.call_suffix.is_valid()) {  // f(i).y
          out_ += '.';
          cref(e.call_suffix);
        }
        break;
      case ExpKind::Break: out_ += "break"; break;
      case ExpKind::Subscript:  // OMC SUBSCRIPTED_EXP 总打印 (e)[subs]（AbsynDumpTpl.tpl:890-891）
        out_ += '(';
        exp(e.a);
        out_ += ')';
        subscripts(e.args);
        break;
      case ExpKind::Array:
        out_ += '{';
        args(e.args);
        out_ += '}';
        break;
      case ExpKind::Matrix:
        out_ += '[';
        for (std::size_t i = 0; i < e.args.size(); ++i) {
          if (i) out_ += "; ";
          args(a_.exps[e.args[i]].args);  // 行是 Array 节点，不打花括号
        }
        out_ += ']';
        break;
      case ExpKind::Tuple:
        out_ += '(';
        for (std::size_t i = 0; i < e.args.size(); ++i) {
          if (i) out_ += ", ";
          const Exp& el = a_.exps[e.args[i]];
          if (el.kind == ExpKind::Tuple && el.args.empty()) continue;  // 空位
          exp(e.args[i]);
        }
        out_ += ')';
        break;
      case ExpKind::Reduction:
        exp(e.a);
        out_ += " for ";
        for_indices(e.iters);
        break;
    }
  }

  void args(const std::vector<Id<Exp>>& v) {
    for (std::size_t i = 0; i < v.size(); ++i) {
      if (i) out_ += ", ";
      exp(v[i]);
    }
  }

  void call_args(const Exp& e) {
    for (std::size_t i = 0; i < e.args.size(); ++i) {
      if (i) out_ += ", ";
      if (i < e.arg_names.size() && e.arg_names[i].is_valid()) {
        sym(e.arg_names[i]);
        out_ += " = ";
      }
      exp(e.args[i]);
    }
  }

  void for_indices(const std::vector<ForIndex>& iters) {
    for (std::size_t i = 0; i < iters.size(); ++i) {
      if (i) out_ += ", ";
      sym(iters[i].name);
      if (iters[i].range.is_valid()) {
        out_ += " in ";
        exp(iters[i].range);
      }
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
  // "( arg, arg )"，总是带括号（annotation 用）
  void mod_args(Id<Modification> id) {
    const Modification& m = a_.mods[id];
    out_ += '(';
    for (std::size_t i = 0; i < m.args.size(); ++i) {
      if (i) out_ += ", ";
      mod_arg(m.args[i]);
    }
    out_ += ')';
  }
  // 修饰：有 args 才打括号，再打顶层绑定
  void modification(Id<Modification> id) {
    if (!id.is_valid()) return;
    const Modification& m = a_.mods[id];
    if (!m.args.empty()) mod_args(id);
    if (m.binding.is_valid()) {
      out_ += m.assign ? " := " : " = ";
      exp(m.binding);
    }
  }
  void mod_arg(const ModArg& arg) {
    if (arg.is_break) {
      out_ += "break ";
      if (arg.break_a.is_valid()) {
        out_ += "connect(";
        exp(arg.break_a);
        out_ += ", ";
        exp(arg.break_b);
        out_ += ')';
      } else {
        path(false, arg.path);
      }
      return;
    }
    if (arg.is_redeclare) {
      const Element& el = a_.elems[arg.redecl];
      if (el.is_redeclare) out_ += "redeclare ";
      if (arg.each_) out_ += "each ";
      if (arg.final_) out_ += "final ";
      if (el.is_replaceable) out_ += "replaceable ";
      element_body(el, "");
      return;
    }
    if (arg.each_) out_ += "each ";
    if (arg.final_) out_ += "final ";
    path(false, arg.path);
    if (arg.sub.is_valid()) modification(arg.sub);
    if (arg.binding.is_valid()) {
      out_ += arg.assign ? " := " : " = ";
      exp(arg.binding);
    }
    if (arg.string_comment.is_valid()) {
      out_ += ' ';
      string_lit(arg.string_comment);
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

  void component_clause(const ComponentClause& cc) {
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
      if (d.condition.is_valid()) {
        out_ += " if ";
        exp(d.condition);
      }
      comment(d.string_comment, d.annotation);
    }
  }

  // 元素主体（不含前缀、缩进与 ';'）；indent 用于嵌套长类的类体
  void element_body(const Element& el, const std::string& indent) {
    switch (el.kind) {
      case ElementKind::Component:
        component_clause(el.comp);
        break;
      case ElementKind::Extends:
        out_ += "extends ";
        path(el.ext.leading_dot, el.ext.path);
        modification(el.ext.mod);
        annotation(el.ext.annotation);
        break;
      case ElementKind::Import:
        out_ += "import ";
        if (el.imp.alias.is_valid()) {
          sym(el.imp.alias);
          out_ += " = ";
        }
        path(el.imp.leading_dot, el.imp.path);
        if (el.imp.wildcard) out_ += ".*";
        if (!el.imp.multi.empty()) {
          out_ += ".{";
          for (std::size_t i = 0; i < el.imp.multi.size(); ++i) {
            if (i) out_ += ", ";
            if (i < el.imp.multi_alias.size() && el.imp.multi_alias[i].is_valid()) {
              sym(el.imp.multi_alias[i]);
              out_ += " = ";
            }
            sym(el.imp.multi[i]);
          }
          out_ += '}';
        }
        comment(el.imp.string_comment, el.imp.annotation);
        break;
      case ElementKind::ClassDef:
        class_def(el.cls, indent);
        break;
    }
    if (el.has_constrainedby) {
      out_ += " constrainedby ";
      path(el.cb_leading_dot, el.cb_path);
      modification(el.cb_mod);
      comment(el.cb_comment, el.cb_annotation);
    }
  }

  void element(Id<Element> id, const std::string& indent) {
    const Element& el = a_.elems[id];
    out_ += indent;
    if (el.is_redeclare) out_ += "redeclare ";
    if (el.is_final) out_ += "final ";
    if (el.is_inner) out_ += "inner ";
    if (el.is_outer) out_ += "outer ";
    if (el.is_replaceable) out_ += "replaceable ";
    element_body(el, indent);
    out_ += ";\n";
  }

  // 类头从当前光标处开始打（调用方已打缩进/前缀）；不打末尾换行。
  void class_def(Id<ClassDef> id, const std::string& indent) {
    const ClassDef& c = a_.classes[id];
    if (c.is_final) out_ += "final ";
    if (c.is_encapsulated) out_ += "encapsulated ";
    if (c.is_partial) out_ += "partial ";
    if (c.purity == Purity::Pure) out_ += "pure ";
    else if (c.purity == Purity::Impure) out_ += "impure ";
    out_ += restriction_kw(c.restriction);
    out_ += ' ';
    if (c.is_class_extends) out_ += "extends ";
    sym(c.name);

    if (c.is_short) {
      out_ += " = ";
      if (c.is_der) {  // function df = der(f, x, y)
        out_ += "der(";
        cref(c.der_fn);
        for (Symbol v : c.der_vars) {
          out_ += ", ";
          sym(v);
        }
        out_ += ')';
        comment(c.string_comment, c.annotation);
        return;
      }
      if (c.is_enumeration) {
        out_ += "enumeration(";
        if (c.enum_unspecified) {
          out_ += ':';
        } else {
          for (std::size_t i = 0; i < c.enum_lits.size(); ++i) {
            if (i) out_ += ", ";
            sym(c.enum_lits[i].name);
            comment(c.enum_lits[i].string_comment, c.enum_lits[i].annotation);
          }
        }
        out_ += ')';
      } else {
        type_prefix(c.short_prefix);
        path(c.short_leading_dot, c.short_type);
        if (!c.short_subscripts.empty()) subscripts(c.short_subscripts);
        modification(c.short_mod);
      }
      comment(c.string_comment, c.annotation);
      return;
    }

    if (c.is_class_extends) modification(c.class_extends_mod);
    if (c.string_comment.is_valid()) {
      out_ += ' ';
      string_lit(c.string_comment);
    }
    out_ += '\n';
    std::string body = indent + "  ";

    // 元素：先 public（无关键字），再 protected
    for (Id<Element> e : c.elements)
      if (!a_.elems[e].is_protected) element(e, body);
    bool any_prot = false;
    for (Id<Element> e : c.elements)
      if (a_.elems[e].is_protected) any_prot = true;
    if (any_prot) {
      out_ += indent + "protected\n";
      for (Id<Element> e : c.elements)
        if (a_.elems[e].is_protected) element(e, body);
    }
    if (!c.initial_equations.empty()) {
      out_ += indent + "initial equation\n";
      equation_list(c.initial_equations, body);
    }
    if (!c.equations.empty()) {
      out_ += indent + "equation\n";
      equation_list(c.equations, body);
    }
    for (const AlgorithmSection& s : c.algorithms) {
      out_ += indent + (s.is_initial ? "initial algorithm\n" : "algorithm\n");
      statement_list(s.stmts, body);
    }
    if (c.has_external) {
      out_ += body + "external";
      if (c.ext_lang.is_valid()) {
        out_ += ' ';
        string_lit(c.ext_lang);
      }
      if (c.ext_lhs.is_valid()) {
        out_ += ' ';
        exp(c.ext_lhs);
        out_ += " =";
      }
      if (c.ext_call.is_valid()) {
        out_ += ' ';
        exp(c.ext_call);
      }
      annotation(c.ext_annotation);
      out_ += ";\n";
    }
    if (c.annotation.is_valid()) {
      out_ += body;
      out_ += "annotation";
      mod_args(c.annotation);
      out_ += ";\n";
    }
    out_ += indent + "end ";
    sym(c.name);
  }

  // ---- 方程 ----
  void equation_list(const std::vector<Id<Equation>>& v, const std::string& indent) {
    for (Id<Equation> id : v) equation(id, indent);
  }

  void equation(Id<Equation> id, const std::string& indent) {
    const Equation& eq = a_.eqs[id];
    std::string body = indent + "  ";
    out_ += indent;
    switch (eq.kind) {
      case EqKind::Equality:
        exp(eq.lhs);
        out_ += " = ";
        exp(eq.rhs);
        break;
      case EqKind::Connect:
        out_ += "connect(";
        exp(eq.lhs);
        out_ += ", ";
        exp(eq.rhs);
        out_ += ')';
        break;
      case EqKind::FunctionCall:
        exp(eq.lhs);
        break;
      case EqKind::If:
        for (std::size_t i = 0; i < eq.branches.size(); ++i) {
          const EqBranch& b = eq.branches[i];
          if (i == 0) {
            out_ += "if ";
          } else if (b.cond.is_valid()) {
            out_ += indent + "elseif ";
          } else {
            out_ += indent + "else\n";
            equation_list(b.body, body);
            continue;
          }
          exp(b.cond);
          out_ += " then\n";
          equation_list(b.body, body);
        }
        out_ += indent + "end if";
        break;
      case EqKind::For:
        out_ += "for ";
        for_indices(eq.iters);
        out_ += " loop\n";
        equation_list(eq.body, body);
        out_ += indent + "end for";
        break;
      case EqKind::When:
        for (std::size_t i = 0; i < eq.branches.size(); ++i) {
          const EqBranch& b = eq.branches[i];
          out_ += (i == 0) ? "when " : indent + "elsewhen ";
          exp(b.cond);
          out_ += " then\n";
          equation_list(b.body, body);
        }
        out_ += indent + "end when";
        break;
    }
    comment(eq.string_comment, eq.annotation);
    out_ += ";\n";
  }

  // ---- 语句 ----
  void statement_list(const std::vector<Id<Statement>>& v, const std::string& indent) {
    for (Id<Statement> id : v) statement(id, indent);
  }

  void statement(Id<Statement> id, const std::string& indent) {
    const Statement& st = a_.stmts[id];
    std::string body = indent + "  ";
    out_ += indent;
    switch (st.kind) {
      case StmtKind::Assign:
        exp(st.lhs);
        out_ += " := ";
        exp(st.rhs);
        break;
      case StmtKind::Call:
        exp(st.lhs);
        break;
      case StmtKind::Break: out_ += "break"; break;
      case StmtKind::Return: out_ += "return"; break;
      case StmtKind::If:
        for (std::size_t i = 0; i < st.branches.size(); ++i) {
          const StmtBranch& b = st.branches[i];
          if (i == 0) {
            out_ += "if ";
          } else if (b.cond.is_valid()) {
            out_ += indent + "elseif ";
          } else {
            out_ += indent + "else\n";
            statement_list(b.body, body);
            continue;
          }
          exp(b.cond);
          out_ += " then\n";
          statement_list(b.body, body);
        }
        out_ += indent + "end if";
        break;
      case StmtKind::For:
        out_ += "for ";
        for_indices(st.iters);
        out_ += " loop\n";
        statement_list(st.body, body);
        out_ += indent + "end for";
        break;
      case StmtKind::While:
        out_ += "while ";
        if (!st.branches.empty()) {
          exp(st.branches[0].cond);
          out_ += " loop\n";
          statement_list(st.branches[0].body, body);
        }
        out_ += indent + "end while";
        break;
      case StmtKind::When:
        for (std::size_t i = 0; i < st.branches.size(); ++i) {
          const StmtBranch& b = st.branches[i];
          out_ += (i == 0) ? "when " : indent + "elsewhen ";
          exp(b.cond);
          out_ += " then\n";
          statement_list(b.body, body);
        }
        out_ += indent + "end when";
        break;
    }
    comment(st.string_comment, st.annotation);
    out_ += ";\n";
  }

  const Absyn& a_;
  std::string out_;
};

}  // namespace

std::string unparse(const Absyn& a) { return Unparser(a).run(); }

std::string unparse_exp(const Absyn& a, Id<Exp> e) { return Unparser(a).exp_text(e); }

std::string unparse_string_lit(const Absyn& a, Symbol s) { return Unparser(a).string_text(s); }

std::string real_literal_text(double v) { return real_text(v); }

}  // namespace ymc
