// FlatModel 文本打印实现。版式逐条照 OMC Template/DAEDumpTpl.tpl（研究底稿 R13 §2/§3/§6）。
#include "flat/flat_dump.h"

#include "base/strings.h"    // escape_modelica_string
#include "flat/expr_dump.h"

namespace ymc {

namespace {

class FlatDumper {
 public:
  FlatDumper(const FlatModel& m, const Interner& in) : m_(m), in_(in) {}

  std::string run() {
    out_.clear();
    // 被调用户函数在 class 之前发射（DAEDump appendStream：函数块各接空行、再 class）。
    for (const FlatFunction& fn : m_.functions) {
      function_block(fn);
      out_ += '\n';
    }
    // 头（R13 §2）：class <全名>[ "注释"]
    out_ += "class ";
    dotted(m_.name);
    if (m_.comment.is_valid()) {
      out_ += " \"";
      out_ += escape_modelica_string(in_.str(m_.comment), /*nl=*/false);
      out_ += '"';
    }
    out_ += '\n';

    for (const FlatVariable& v : m_.variables) variable(v);

    section_eqs("initial equation", m_.initial_equations);
    section_algs("initial algorithm", m_.initial_algorithms, /*initial=*/true);
    section_eqs("equation", m_.equations);
    section_algs("algorithm", m_.algorithms, /*initial=*/false);

    out_ += "end ";
    dotted(m_.name);
    out_ += ";\n";
    return std::move(out_);
  }

 private:
  void dotted(const std::vector<Symbol>& parts) {
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (i) out_ += '.';
      out_ += in_.str(parts[i]);
    }
  }
  void expr(Id<Expr> e) { out_ += dump_expr(m_.exprs, in_, e); }

  // ---- 函数块（DAEDump dumpFunction）：function <名> / 形参逐行（维在类型上）/ [algorithm 语句] / end <名>; ----
  void function_block(const FlatFunction& fn) {
    out_ += "function ";
    dotted(fn.name);
    if (fn.comment.is_valid()) {
      out_ += " \"";
      out_ += escape_modelica_string(in_.str(fn.comment), /*nl=*/false);
      out_ += '"';
    }
    out_ += '\n';
    for (const FlatFuncParam& p : fn.params) func_param(p);
    if (!fn.body.empty()) {
      out_ += "algorithm\n";
      for (Id<FlatStatement> s : fn.body) statement(s, "  ");
    }
    out_ += "end ";
    dotted(fn.name);
    out_ += ";\n";
  }

  void func_param(const FlatFuncParam& p) {
    out_ += "  ";
    if (p.is_protected) out_ += "protected ";
    switch (p.var_kind) {   // 局部保留 kind；in/out 已在发射期归一 Var（不打前缀）
      case FlatVarKind::Discrete: out_ += "discrete "; break;
      case FlatVarKind::Parameter: out_ += "parameter "; break;
      case FlatVarKind::Constant: out_ += "constant "; break;
      case FlatVarKind::Var: break;
    }
    if (p.direction == FlatDirection::Input) out_ += "input ";
    else if (p.direction == FlatDirection::Output) out_ += "output ";
    type_name(p.ty);   // 维在类型上（`Real[3] u`）——本增量标量，暂无 dims
    out_ += ' ';
    out_ += in_.str(p.name);
    if (p.binding.is_valid()) { out_ += " = "; expr(p.binding); }
    out_ += ";\n";
  }

  // ---- 变量行（R13 §3.1 固定前缀顺序）----
  void variable(const FlatVariable& v) {
    out_ += "  ";                                  // 无前缀变量缩进 2 格
    if (v.is_protected) out_ += "protected ";
    if (v.is_final) out_ += "final ";
    switch (v.var_kind) {
      case FlatVarKind::Discrete: out_ += "discrete "; break;
      case FlatVarKind::Parameter: out_ += "parameter "; break;
      case FlatVarKind::Constant: out_ += "constant "; break;
      case FlatVarKind::Var: break;
    }
    if (v.direction == FlatDirection::Input) out_ += "input ";
    else if (v.direction == FlatDirection::Output) out_ += "output ";
    type_name(v.ty);
    out_ += ' ';
    // 逐段名 + 该段下标（复合数组 `a[1].x`）；末段后再补叶数组下标 subscripts（`b.x[1,2]`）。R13 §4.2：`,` 无空格。
    auto emit_subs = [&](const std::vector<Id<Expr>>& s) {
      if (s.empty()) return;
      out_ += '[';
      for (std::size_t i = 0; i < s.size(); ++i) { if (i) out_ += ','; out_ += dump_expr(m_.exprs, in_, s[i]); }
      out_ += ']';
    };
    for (std::size_t i = 0; i < v.name.size(); ++i) {
      if (i) out_ += '.';
      out_ += in_.str(v.name[i]);
      if (i < v.name_subs.size()) emit_subs(v.name_subs[i]);
    }
    emit_subs(v.subscripts);
    attributes(v.attributes);
    if (v.binding.is_valid()) {
      out_ += " = ";
      expr(v.binding);
    }
    if (v.comment.is_valid()) {
      out_ += " \"";
      out_ += escape_modelica_string(in_.str(v.comment), /*nl=*/false);
      out_ += '"';
    }
    out_ += ";\n";
  }

  void type_name(const Type& t) {
    switch (t.kind) {
      case TypeKind::Real: out_ += "Real"; break;
      case TypeKind::Integer: out_ += "Integer"; break;
      case TypeKind::Boolean: out_ += "Boolean"; break;
      case TypeKind::String: out_ += "String"; break;
      case TypeKind::Enumeration:
        out_ += "enumeration(";
        for (std::size_t i = 0; i < t.enum_literals.size(); ++i) {
          if (i) out_ += ", ";
          out_ += in_.str(t.enum_literals[i]);
        }
        out_ += ')';
        break;
      default: out_ += "Real"; break;   // v1 只到标量；其它类型竖切片再补
    }
  }

  void attributes(const std::vector<FlatVarAttr>& attrs) {
    if (attrs.empty()) return;
    out_ += '(';
    for (std::size_t i = 0; i < attrs.size(); ++i) {
      if (i) out_ += ", ";
      out_ += in_.str(attrs[i].name);
      out_ += " = ";
      expr(attrs[i].value);
    }
    out_ += ')';
  }

  // ---- 段 ----
  void section_eqs(const char* kw, const std::vector<Id<FlatEquation>>& eqs) {
    if (eqs.empty()) return;
    out_ += kw;
    out_ += '\n';
    for (Id<FlatEquation> e : eqs) equation(e, "  ");
  }
  void section_algs(const char* kw, const std::vector<FlatAlgorithm>& algs, bool /*initial*/) {
    for (const FlatAlgorithm& alg : algs) {
      if (alg.stmts.empty()) continue;
      out_ += kw;                         // 算法段各打一次关键字、彼此无空行（R13 §2-5）
      out_ += '\n';
      for (Id<FlatStatement> s : alg.stmts) statement(s, "  ");
    }
  }

  // AssertionLevel.error 时 assert 省第三参（R13 §6-6）。
  bool is_error_level(Id<Expr> level) const {
    if (!level.is_valid()) return true;
    const Expr& e = m_.exprs[level];
    if (e.kind != ExprKind::EnumLit) return false;
    const std::vector<Symbol>& p = e.enum_path;
    return p.size() >= 2 && in_.str(p[p.size() - 2]) == "AssertionLevel" &&
           in_.str(p.back()) == "error";
  }

  void comment(Symbol c) {
    if (!c.is_valid()) return;
    out_ += " \"";
    out_ += escape_modelica_string(in_.str(c), /*nl=*/false);
    out_ += '"';
  }

  void equation(Id<FlatEquation> id, const std::string& indent) {
    const FlatEquation& q = m_.eqs[id];
    std::string body = indent + "  ";
    out_ += indent;
    switch (q.kind) {
      case FlatEqKind::Equality:
        expr(q.a);
        out_ += " = ";
        expr(q.b);
        break;
      case FlatEqKind::Assert:
        out_ += "assert(";
        expr(q.a);
        out_ += ", ";
        expr(q.b);
        if (!is_error_level(q.c)) { out_ += ", "; expr(q.c); }
        out_ += ')';
        break;
      case FlatEqKind::Terminate:
        out_ += "terminate(";
        expr(q.a);
        out_ += ')';
        break;
      case FlatEqKind::Reinit:
        out_ += "reinit(";
        expr(q.a);
        out_ += ", ";
        expr(q.b);
        out_ += ')';
        break;
      case FlatEqKind::NoRetCall:
        expr(q.a);
        break;
      case FlatEqKind::If:
        for (std::size_t i = 0; i < q.branches.size(); ++i) {
          out_ += (i == 0) ? "if " : indent + "elseif ";
          expr(q.branches[i].cond);
          out_ += " then\n";
          for (Id<FlatEquation> b : q.branches[i].body) equation(b, body);
        }
        if (!q.else_body.empty()) {
          out_ += indent + "else\n";
          for (Id<FlatEquation> b : q.else_body) equation(b, body);
        }
        out_ += indent + "end if";
        break;
      case FlatEqKind::For:
        out_ += "for ";
        out_ += in_.str(q.index);
        if (q.a.is_valid()) { out_ += " in "; expr(q.a); }
        out_ += " loop\n";
        for (Id<FlatEquation> b : q.body) equation(b, body);
        out_ += indent + "end for";
        break;
      case FlatEqKind::When:
        for (std::size_t i = 0; i < q.branches.size(); ++i) {
          out_ += (i == 0) ? "when " : indent + "elsewhen ";
          expr(q.branches[i].cond);
          out_ += " then\n";
          for (Id<FlatEquation> b : q.branches[i].body) equation(b, body);
        }
        out_ += indent + "end when";
        break;
    }
    comment(q.comment);
    out_ += ";\n";
  }

  void statement(Id<FlatStatement> id, const std::string& indent) {
    const FlatStatement& s = m_.stmts[id];
    std::string body = indent + "  ";
    out_ += indent;
    switch (s.kind) {
      case FlatStmtKind::Assign:
        expr(s.a);
        out_ += " := ";
        expr(s.b);
        break;
      case FlatStmtKind::Assert:
        out_ += "assert(";
        expr(s.a);
        out_ += ", ";
        expr(s.b);
        if (!is_error_level(s.c)) { out_ += ", "; expr(s.c); }
        out_ += ')';
        break;
      case FlatStmtKind::Terminate:
        out_ += "terminate(";
        expr(s.a);
        out_ += ')';
        break;
      case FlatStmtKind::Reinit:
        out_ += "reinit(";
        expr(s.a);
        out_ += ", ";
        expr(s.b);
        out_ += ')';
        break;
      case FlatStmtKind::NoRetCall:
        expr(s.a);
        break;
      case FlatStmtKind::Return: out_ += "return"; break;
      case FlatStmtKind::Break: out_ += "break"; break;
      case FlatStmtKind::If:
        for (std::size_t i = 0; i < s.branches.size(); ++i) {
          out_ += (i == 0) ? "if " : indent + "elseif ";
          expr(s.branches[i].cond);
          out_ += " then\n";
          for (Id<FlatStatement> b : s.branches[i].body) statement(b, body);
        }
        if (!s.else_body.empty()) {
          out_ += indent + "else\n";
          for (Id<FlatStatement> b : s.else_body) statement(b, body);
        }
        out_ += indent + "end if";
        break;
      case FlatStmtKind::For:
        out_ += "for ";
        out_ += in_.str(s.index);
        if (s.a.is_valid()) { out_ += " in "; expr(s.a); }
        out_ += " loop\n";
        for (Id<FlatStatement> b : s.body) statement(b, body);
        out_ += indent + "end for";
        break;
      case FlatStmtKind::While:
        out_ += "while ";
        expr(s.a);
        out_ += " loop\n";
        for (Id<FlatStatement> b : s.body) statement(b, body);
        out_ += indent + "end while";
        break;
      case FlatStmtKind::When:
        for (std::size_t i = 0; i < s.branches.size(); ++i) {
          out_ += (i == 0) ? "when " : indent + "elsewhen ";
          expr(s.branches[i].cond);
          out_ += " then\n";
          for (Id<FlatStatement> b : s.branches[i].body) statement(b, body);
        }
        out_ += indent + "end when";
        break;
    }
    comment(s.comment);
    out_ += ";\n";
  }

  const FlatModel& m_;
  const Interner& in_;
  std::string out_;
};

}  // namespace

std::string dump_flat(const FlatModel& m, const Interner& in) { return FlatDumper(m, in).run(); }

}  // namespace ymc
