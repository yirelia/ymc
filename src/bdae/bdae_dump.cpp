// 版式来源（OMC v1.25.0）：Expression.toString NFExpression.mo:1894-2225；Operator.priority / symbol NFOperator.mo:408-523；
// Variable.toStream NFVariable.mo:527-578；Attributes.toString NFAttributes.mo:619-632；VariableAttributes.toString
// NFBackendExtension.mo:393-423；VariablePointers.toString NBVariable.mo:1575-1609；VarData.toStringVerbose NBVariable.mo:2187-2223；
// Equation.toString NBEquation.mo:968-989；IfEquationBody.toString :2688-2713；WhenEquationBody.toString :3020-3041；
// WhenStatement.toString :3493-3508；EquationPointers.toString :3847-3899；EqData.toString :4416-4440；
// Statement.toStream NFStatement.mo:846-971、toStreamList :973-997；headline StringUtil.mo:52-78。
#include "bdae/bdae_dump.h"

#include <string_view>

#include "base/strings.h"     // escape_modelica_string（System.escapedString，同 DAE dump）
#include "flat/expr_dump.h"   // real_lit（realString）

namespace ymc::bdae {

namespace {

class ExpPrinter {
 public:
  ExpPrinter(const ExprArena& a, const Interner& in) : a_(a), in_(in) {}

  std::string str(Id<Expr> id) {
    const Expr& e = a_[id];
    switch (e.kind) {
      case ExprKind::IntLit: return std::to_string(e.ival);
      case ExprKind::RealLit: return real_lit(e.rval);
      case ExprKind::BoolLit: return e.bval ? "true" : "false";
      case ExprKind::StringLit:
        return "\"" + escape_modelica_string(in_.str(e.sval), /*nl=*/false) + "\"";
      case ExprKind::EnumLit: return dotted(e.enum_path);
      case ExprKind::Cref: {
        std::string s;
        for (const ExprCrefPart& p : e.cref.parts) {
          if (!s.empty()) s += '.';
          s += in_.str(p.name);
        }
        return s;
      }
      case ExprKind::Multary: {
        const std::string op = symbol(e.bin);
        if (e.inv_args.empty()) return multary(e.args, id, op, /*paren=*/false);
        if (e.args.empty())   // 全是逆项：和打 `-`，积打 `1/`
          return (e.bin == BinOp::Add ? "-" : "1/") + multary(e.inv_args, id, op, true);
        return multary(e.args, id, op, true) + symbol(e.bin == BinOp::Add ? BinOp::Sub : BinOp::Div) +
               multary(e.inv_args, id, op, true);
      }
      case ExprKind::Binary:
        return operand(e.a, id, true) + symbol(e.bin) + operand(e.b, id, false);
      case ExprKind::Unary:
        return "-" + operand(e.a, id, false);
      case ExprKind::Logical:
        return operand(e.a, id, true) + (e.log == LogOp::And ? " and " : " or ") + operand(e.b, id, false);
      case ExprKind::LogicalNot:
        return "not " + operand(e.a, id, false);
      case ExprKind::Relation: {
        static constexpr const char* kRel[] = {" < ", " <= ", " > ", " >= ", " == ", " <> "};   // RelOp 序
        return operand(e.a, id, true) + kRel[static_cast<int>(e.rel)] + operand(e.b, id, false);
      }
      case ExprKind::IfExp:
        return "if " + str(e.a) + " then " + str(e.b) + " else " + str(e.c);
      case ExprKind::Cast:   // NF_API 缺省开（Flags.mo:506），只在跑前端时关（CevalScriptBackend.mo:3601、:3611），打印时 cast 不打（:1967-1970）
        return str(e.a);
      case ExprKind::Call:
        return dotted(e.call_name) + "(" + list(e.args) + ")";
      case ExprKind::Array:
        return "{" + list(e.args) + "}";
      case ExprKind::Range:   // :1917-1922：先起点、步长，再终点。Expr 里有步长时 b 是步长，c 是终点
        return operand(e.a, id, false) + ":" + operand(e.b, id, false) + (e.c.is_valid() ? ":" + operand(e.c, id, false) : "");
      case ExprKind::Tuple:
        return "(" + list(e.args) + ")";
      case ExprKind::Wild:    // ComponentRef.WILD
        return "_";
      default:
        YMC_UNREACHABLE("bdae::exp_string: expression kind rejected by lower");
    }
  }

 private:
  std::string dotted(const std::vector<Symbol>& parts) const {
    std::string s;
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (i) s += '.';
      s += in_.str(parts[i]);
    }
    return s;
  }
  std::string list(const std::vector<Id<Expr>>& items) {
    std::string s;
    for (std::size_t i = 0; i < items.size(); ++i) {
      if (i) s += ", ";
      s += str(items[i]);
    }
    return s;
  }
  static const char* symbol(BinOp op) {
    switch (op) {
      case BinOp::Add: return " + ";
      case BinOp::Sub: return " - ";
      case BinOp::Mul: return " * ";
      case BinOp::Div: return " / ";
      case BinOp::Pow: return " ^ ";
    }
    YMC_UNREACHABLE("BinOp");
  }
  static int op_priority(BinOp op, bool lhs) {   // Operator.priority :460-495
    switch (op) {
      case BinOp::Add: return lhs ? 5 : 6;
      case BinOp::Sub: return 5;
      case BinOp::Mul:
      case BinOp::Div: return 2;
      case BinOp::Pow: return 1;
    }
    YMC_UNREACHABLE("BinOp");
  }
  int priority(Id<Expr> id, bool lhs) const {    // Expression.priority :2182-2203
    const Expr& e = a_[id];
    switch (e.kind) {
      case ExprKind::IntLit: return e.ival < 0 ? 4 : 0;
      case ExprKind::RealLit: return e.rval < 0.0 ? 4 : 0;
      case ExprKind::Multary:
      case ExprKind::Binary: return op_priority(e.bin, lhs);
      case ExprKind::Unary: return 4;
      case ExprKind::Logical: return e.log == LogOp::And ? 8 : 9;
      case ExprKind::LogicalNot: return 7;
      case ExprKind::Relation: return 6;
      case ExprKind::Range: return 10;
      case ExprKind::IfExp: return 11;
      case ExprKind::Cast: return priority(e.a, lhs);
      default: return 0;
    }
  }
  // operandString :2092-2121。Multary 既不算可结合也不算不可结合（:2205-2225 只认 BINARY、LBINARY），同优先级时打在右边要加括号，
  // 而 Multary 的各项都按右操作数打（multaryString），所以和里套和打成 `(a + b) + c`、`a < (b + c)`，BINARY 的 `+` 不会这样。
  std::string operand(Id<Expr> child, Id<Expr> parent, bool lhs) {
    const int prio = priority(child, lhs);
    bool paren = false;
    if (prio == 4) {
      paren = true;
    } else {
      const int parent_prio = priority(parent, lhs);
      const Expr& c = a_[child];
      if (prio > parent_prio) paren = true;
      else if (prio == parent_prio)
        paren = lhs ? (c.kind == ExprKind::Binary && c.bin == BinOp::Pow) || c.kind == ExprKind::Logical
                    : !((c.kind == ExprKind::Binary && c.bin == BinOp::Add) || c.kind == ExprKind::Logical);
    }
    return paren ? "(" + str(child) + ")" : str(child);
  }
  // multaryString :2155-2166：逐项都按右操作数打。
  std::string multary(const std::vector<Id<Expr>>& args, Id<Expr> parent, const std::string& op, bool paren) {
    std::string s;
    for (std::size_t i = 0; i < args.size(); ++i) {
      if (i) s += op;
      s += operand(args[i], parent, false);
    }
    return paren && args.size() > 1 ? "(" + s + ")" : s;
  }

  const ExprArena& a_;
  const Interner& in_;
};

std::string headline_1(std::string_view t) {
  const std::string bar(t.size() + 8, '#');
  return bar + "\n\n    " + std::string(t) + "\n\n" + bar + "\n";
}
std::string headline_2(std::string_view t) {
  const std::string bar(t.size() + 4, '=');
  return bar + "\n  " + std::string(t) + "\n" + bar + "\n";
}
std::string headline_4(std::string_view t) {
  return std::string(t) + "\n" + std::string(t.size() + 2, '*') + "\n";
}
// 行首序号：`(i)` 补足 10 列。
std::string row_index(std::size_t i) {
  std::string s = "(" + std::to_string(i) + ")";
  s.resize(10, ' ');
  return s;
}

std::string cref_string(const ExprCref& c, const Interner& in) {
  std::string s;
  for (const ExprCrefPart& p : c.parts) {
    if (!s.empty()) s += '.';
    s += in.str(p.name);
  }
  return s;
}

class Dumper {
 public:
  Dumper(const BackendDAE& d, const Interner& in) : d_(d), in_(in), exp_(d.exprs, in) {}

  std::string run(std::string_view stage) {
    return headline_1("BackendDAE: (" + std::string(stage) + ")") + "\n" + var_data() + "\n" + eq_data();
  }

 private:
  static int size_of(const Var& v) { return v.kind == VarKind::Dummy ? 0 : 1; }

  std::string var_data() {
    const VarData& vd = d_.vars;
    std::string out = headline_2("Variable Data Simulation (scalar unknowns: " + std::to_string(scalar_size(vd.unknowns)) + ")") + "\n";
    out += bucket("State", vd.states);
    out += bucket("Algebraic", vd.algebraics);
    out += bucket("Discrete", vd.discretes);
    out += bucket("Top Level Input", vd.top_level_inputs);
    out += bucket("Parameter", vd.parameters);
    out += bucket("Constant", vd.constants);
    out += bucket("Artificial", vd.artificials);
    return out;
  }
  int scalar_size(const VarSet& s) const {
    int n = 0;
    for (Id<Var> id : s.items) n += size_of(d_.var_arena[id]);
    return n;
  }
  std::string bucket(std::string_view name, const VarSet& s) {
    if (s.items.empty()) return "";   // printEmpty = false
    std::string out = headline_4(std::string(name) + " Variables (" + std::to_string(s.items.size()) + "/" +
                                 std::to_string(scalar_size(s)) + ")");
    for (std::size_t i = 0; i < s.items.size(); ++i) out += row_index(i + 1) + var_line(d_.var_arena[s.items[i]]) + "\n";
    return out + "\n";
  }

  static const char* tag(VarKind k) {
    switch (k) {
      case VarKind::Dummy: return "[DMMY]";
      case VarKind::Time: return "[TIME]";
      case VarKind::Algebraic: return "[ALGB]";
      case VarKind::State: return "[STAT]";
      case VarKind::Discrete: return "[DISC]";
      case VarKind::Parameter: return "[PRMT]";
      case VarKind::Constant: return "[CNST]";
      case VarKind::Iterator: return "[ITER]";
    }
    YMC_UNREACHABLE("VarKind");
  }
  std::string type_string(const Type& t) const {
    switch (t.kind) {
      case TypeKind::Real: return "Real";
      case TypeKind::Integer: return "Integer";
      case TypeKind::Boolean: return "Boolean";
      case TypeKind::String: return "String";
      case TypeKind::Enumeration: {   // Type.toString :970-971：类型全名 + 字面量
        std::string s = "enumeration ";
        for (std::size_t i = 0; i < t.enum_path.size(); ++i) {
          if (i) s += '.';
          s += in_.str(t.enum_path[i]);
        }
        s += '(';
        for (std::size_t i = 0; i < t.enum_literals.size(); ++i) {
          if (i) s += ", ";
          s += in_.str(t.enum_literals[i]);
        }
        return s + ")";
      }
      default: YMC_UNREACHABLE("bdae: variable type rejected by lower");
    }
  }
  std::string var_line(const Var& v) {
    std::string s = tag(v.kind);
    s += " (" + std::to_string(size_of(v)) + ") ";
    if (v.kind == VarKind::Dummy) return s + "$ANY$ ";   // DUMMY_VARIABLE：Type.ANY 打 `$ANY$`，名字是空的 WILD，留下一个空格
    if (v.is_protected) s += "protected ";
    if (v.is_redeclare) s += "redeclare ";
    if (v.is_final) s += "final ";
    if (v.is_inner && v.is_outer) s += "inner outer ";
    else if (v.is_inner) s += "inner ";
    else if (v.is_outer) s += "outer ";
    if (v.connector == SConnectorType::Flow) s += "flow ";
    else if (v.connector == SConnectorType::Stream) s += "stream ";
    switch (v.variability) {   // unparseVariability NFPrefixes.mo:473-486：离散类型上的 discrete 不打
      case FlatVarKind::Var: break;
      case FlatVarKind::Discrete: if (v.ty.kind == TypeKind::Real) s += "discrete "; break;
      case FlatVarKind::Parameter: s += "parameter "; break;
      case FlatVarKind::Constant: s += "constant "; break;
    }
    if (v.direction == FlatDirection::Input) s += "input ";
    else if (v.direction == FlatDirection::Output) s += "output ";
    s += type_string(v.ty) + " " + cref_string(v.name, in_);
    if (v.binding.is_valid()) s += " = " + exp_.str(v.binding);
    const std::string attr = attributes(v);
    return attr.empty() ? s : s + " (" + attr + ")";
  }
  // 固定次序，不看声明序（VariableAttributes.toString :398-414 + attributesToString :1003-1028）。类型上没有的属性 attrs 里本来就没有，
  // 各类型的列表可以合成这一张。
  std::string attributes(const Var& v) {
    std::string s;
    const auto add = [&](const std::string& item) { s += (s.empty() ? "" : ", ") + item; };
    for (const char* name : {"fixed", "start", "min", "max", "nominal"})
      for (const FlatVarAttr& a : v.attrs)
        if (in_.str(a.name) == name) add(std::string(name) + " = " + exp_.str(a.value));
    for (const FlatVarAttr& a : v.attrs)
      if (in_.str(a.name) == "stateSelect") add("StateSelect = " + std::string(in_.str(d_.exprs[a.value].enum_path.back())));
    if (v.tearing_select.is_valid()) add("TearingSelect = " + std::string(in_.str(v.tearing_select)));
    return s;
  }

  std::string eq_data() {
    const EqnData& ed = d_.eqns;
    int sim = 0;
    for (Id<Eqn> id : ed.simulation.items) sim += d_.eqn_arena[id].size;
    std::string out = headline_2("Equation Data Simulation (scalar simulation equations: " + std::to_string(sim) + ")") + "\n";
    out += eq_bucket("Continuous", ed.continuous);
    out += eq_bucket("Discrete", ed.discretes);
    out += eq_bucket("(Exclusively) Initial", ed.initials);
    out += eq_bucket("Removed", ed.removed);
    return out;
  }
  std::string eq_bucket(std::string_view name, const EqnSet& s) {
    if (s.items.empty()) return "";
    int scal = 0;
    for (Id<Eqn> id : s.items) scal += d_.eqn_arena[id].size;
    std::string out = headline_4(std::string(name) + " Equations (" + std::to_string(s.items.size()) + "/" +
                                 std::to_string(scal) + ")");
    for (std::size_t i = 0; i < s.items.size(); ++i) out += eq_text(d_.eqn_arena[s.items[i]], row_index(i + 1)) + "\n";
    return out + "\n";
  }
  std::string eq_text(const Eqn& q, const std::string& row) {
    const std::string size = "(" + std::to_string(q.size) + ")", name = " (" + cref_string(q.name, in_) + ")";
    // 续行也带序号：Equation.toString 把序号列接在 "[----] " 前面当 indent。
    const std::string indent = row + "[----] ";
    switch (q.kind) {
      case EqnKind::Scalar:
        return row + "[SCAL] " + size + " " + exp_.str(q.lhs) + " = " + exp_.str(q.rhs) + name;
      case EqnKind::When: {
        std::string out = row + "[WHEN] " + size + name + "\n";
        for (std::size_t i = 0; i < q.when.size(); ++i) {
          out += indent + (i ? "elsewhen " : "when ") + exp_.str(q.when[i].condition) + " then\n";
          for (const WhenStmt& st : q.when[i].stmts) out += indent + "  " + when_stmt(st) + "\n";
        }
        return out + indent + "end when;";
      }
      case EqnKind::If: {
        std::string out = row + "[-IF-] " + size + name + "\n";
        for (std::size_t i = 0; i < q.if_branches.size(); ++i) {
          const IfBranch& br = q.if_branches[i];
          out += indent + (i ? "else" : "") + (br.condition.is_valid() ? "if " + exp_.str(br.condition) + " then\n" : "\n");
          out += eq_text(d_.eqn_arena[br.eqn], indent + "  ") + "\n";
        }
        return out + indent + "end if;";
      }
      case EqnKind::Algorithm:   // 语句串自己以 ";\n" 结尾，桶那层再加一个换行，所以每条 [ALGO] 后面空一行
        return row + "[ALGO] " + size + name + "\n" + stmt_list(q.alg, indent);
    }
    YMC_UNREACHABLE("EqnKind");
  }
  // toStreamList：多行语句（for / if / when / while）的前后空一行，空行不带缩进。
  std::string stmt_list(const std::vector<Stmt>& stmts, const std::string& indent) {
    std::string out;
    bool first = true, prev_multi = false;
    for (const Stmt& st : stmts) {
      const bool multi = st.kind == StmtKind::For || st.kind == StmtKind::If || st.kind == StmtKind::When || st.kind == StmtKind::While;
      if (!first && (prev_multi || multi)) out += "\n";
      first = false;
      prev_multi = multi;
      out += stmt_text(st, indent) + ";\n";
    }
    return out;
  }
  std::string stmt_text(const Stmt& st, const std::string& indent) {
    switch (st.kind) {
      case StmtKind::Assign: return indent + exp_.str(st.a) + " := " + exp_.str(st.b);
      case StmtKind::For:
        return indent + "for " + std::string(in_.str(st.index)) + " in " + exp_.str(st.a) + " loop\n" + stmt_list(st.body, indent + "  ") + indent + "end for";
      case StmtKind::If: return branches(st, indent, "if ", "elseif ", "end if");
      case StmtKind::When: return branches(st, indent, "when ", "elsewhen ", "end when");
      case StmtKind::While:   // 真是 `then`，不是 `loop`（:949）
        return indent + "while " + exp_.str(st.a) + " then\n" + stmt_list(st.body, indent + "  ") + indent + "end while";
      case StmtKind::Assert: return indent + "assert(" + exp_.str(st.a) + ", " + exp_.str(st.b) + ", " + exp_.str(st.c) + ")";
      case StmtKind::Terminate: return indent + "terminate(" + exp_.str(st.a) + ")";
      case StmtKind::NoRetCall: return indent + exp_.str(st.a);
      case StmtKind::Break: return indent + "#UNKNOWN STATEMENT#";
    }
    YMC_UNREACHABLE("StmtKind");
  }
  std::string branches(const Stmt& st, const std::string& indent, const char* first, const char* next, const char* end) {
    std::string out = indent;
    for (std::size_t i = 0; i < st.branches.size(); ++i) {
      out += (i ? next : first) + exp_.str(st.branches[i].condition) + " then\n" + stmt_list(st.branches[i].body, indent + "  ") + indent;
    }
    return out + end;
  }
  std::string when_stmt(const WhenStmt& st) {
    switch (st.kind) {
      case WhenStmtKind::Assign: return exp_.str(st.a) + " := " + exp_.str(st.b);
      case WhenStmtKind::Reinit: return "reinit(" + exp_.str(st.a) + ", " + exp_.str(st.b) + ")";
      case WhenStmtKind::Assert: return "assert(" + exp_.str(st.a) + ", " + exp_.str(st.b) + ", " + exp_.str(st.c) + ")";
    }
    YMC_UNREACHABLE("WhenStmtKind");
  }

  const BackendDAE& d_;
  const Interner& in_;
  ExpPrinter exp_;
};

}  // namespace

std::string exp_string(const ExprArena& a, const Interner& in, Id<Expr> e) { return ExpPrinter(a, in).str(e); }

std::string dump_stage(const BackendDAE& dae, const Interner& in, std::string_view stage) { return Dumper(dae, in).run(stage); }

}  // namespace ymc::bdae
