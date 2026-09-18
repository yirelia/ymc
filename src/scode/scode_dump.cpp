// SCode 文本 dump 实现。对齐 OMC Template/SCodeDumpTpl.tpl（基线 3ceb0c9d06）：
//   dumpElements3 :55-85（元素各一行，PARTS 类前后空行 :95-100）；dumpSectionVisibility :127-135；
//   dumpClass :164-182（头 :184-193，尾 :240-259，类注解在 end 前一行 :247-253）；DERIVED :221-225；
//   ENUMERATION :226-231；PDER :232-234；dumpClassDef :198-214（段顺序）；dumpComponent :273-293
//   （前缀顺序 :622-631，属性顺序 :819-828）；dumpModifier :739-747，子修饰 :784-791，REDECL :772-778；
//   dumpEquation :325-361（assert 的 error 级别省略 :447-454）；dumpStatement :474-516（ALG_IF 总打印 else :531-532）；
//   dumpAnnotationSubModifier :793-817（showAnnotations=false 时剥掉的子修饰白名单 :800-813）；external :887-901。
#include "scode/scode_dump.h"

#include "absyn/unparse.h"
#include "base/strings.h"

namespace ymc {

namespace {

// Config.showAnnotations() 为 false（默认）时省略的注解子修饰（SCodeDumpTpl.tpl:800-813）
bool hidden_annotation(std::string_view name) {
  static const char* const kHidden[] = {"choices", "Documentation", "Dialog", "Diagram", "Icon",
                                        "Line", "Placement", "preferredView", "conversion",
                                        "defaultComponentName", "revisionId", "uses"};
  for (const char* h : kHidden)
    if (name == h) return true;
  return false;
}

class Dumper {
 public:
  explicit Dumper(const SCode& s) : s_(s), a_(s.absyn) {}

  // 单个元素（不带结尾 `;`，同 OMC list(C, exportKind=SCode) 的输出）
  std::string element(Id<SElement> id, const std::string& indent) {
    out_.clear();
    element_line(id, indent);
    if (out_.size() >= 2 && out_.compare(out_.size() - 2, 2, ";\n") == 0) out_.erase(out_.size() - 2, 1);
    return std::move(out_);
  }

  std::string program() {
    out_.clear();
    elements(s_.program, "");
    return std::move(out_);
  }

 private:
  std::string_view name(Symbol s) const { return a_.interner.str(s); }
  void sym(Symbol s) { out_ += name(s); }
  void exp(Id<Exp> e) { out_ += unparse_exp(a_, e); }
  // 注释串：翻译时已解转义，dump 时按 SCodeDumpTpl.tpl:918 System.escapedString(cmt, false) 转义回去
  //（换行不转义）。external 的语言串也走这里（"C"/"FORTRAN 77"/"builtin" 无转义，结果相同）。
  void str(Symbol s) {
    out_ += '"';
    out_ += escape_modelica_string(name(s), /*nl=*/false);
    out_ += '"';
  }
  void path(bool leading_dot, const std::vector<Symbol>& p) {
    if (leading_dot) out_ += '.';
    for (std::size_t i = 0; i < p.size(); ++i) {
      if (i) out_ += '.';
      sym(p[i]);
    }
  }
  void dims(const std::vector<Id<Exp>>& d) {
    if (d.empty()) return;
    out_ += '[';
    for (std::size_t i = 0; i < d.size(); ++i) {
      if (i) out_ += ", ";
      exp(d[i]);
    }
    out_ += ']';
  }

  static bool is_parts_class(const SElement& e) {
    return e.kind == SElementKind::Class &&
           (e.def.kind == SClassDefKind::Parts || e.def.kind == SClassDefKind::ClassExtends);
  }

  // ---- 元素列表：visibility 切换行 + PARTS 类前后空行 ----
  void elements(const std::vector<Id<SElement>>& els, const std::string& indent) {
    SVisibility cur = SVisibility::Public;
    std::string sec_indent = indent.size() >= 2 ? indent.substr(0, indent.size() - 2) : "";
    for (std::size_t i = 0; i < els.size(); ++i) {
      const SElement& e = s_.elems[els[i]];
      // 顺序同 dumpElements3：pre_spacing → 可见性标签 → 元素（探针 2026-09-16：空行在 protected 之前）
      if (i > 0 && (is_parts_class(e) || is_parts_class(s_.elems[els[i - 1]]))) out_ += '\n';
      if (e.prefixes.vis != cur) {
        cur = e.prefixes.vis;
        out_ += sec_indent + (cur == SVisibility::Protected ? "protected\n" : "public\n");
      }
      element_line(els[i], indent);
    }
  }

  void element_line(Id<SElement> id, const std::string& indent) {
    const SElement& e = s_.elems[id];
    out_ += indent;
    switch (e.kind) {
      case SElementKind::Import: import(e); break;
      case SElementKind::Extends: extends(e); break;
      case SElementKind::Component: component(e); break;
      case SElementKind::Class: klass(e, indent); break;
    }
    out_ += ";\n";
  }

  void import(const SElement& e) {
    out_ += "import ";
    if (e.imp.alias.is_valid()) {
      sym(e.imp.alias);
      out_ += " = ";
    }
    path(e.imp.leading_dot, e.imp.path);
    if (e.imp.wildcard) out_ += ".*";
  }

  void extends(const SElement& e) {
    out_ += "extends ";
    path(e.base_leading_dot, e.base_path);
    modifier(e.mod);
    annotation(e.extends_annotation);
  }

  // dumpPrefixes :622-631
  void prefixes(const SPrefixes& p) {
    if (p.is_redeclare) out_ += "redeclare ";
    if (p.is_final) out_ += "final ";
    if (p.io == SInnerOuter::Inner) out_ += "inner ";
    else if (p.io == SInnerOuter::Outer) out_ += "outer ";
    else if (p.io == SInnerOuter::InnerOuter) out_ += "inner outer ";
    if (p.is_replaceable) out_ += "replaceable ";
  }
  void constrainedby(const SPrefixes& p) {
    if (!p.has_cc) return;
    out_ += " constrainedby ";
    path(p.cc.leading_dot, p.cc.path);
    modifier(p.cc.mod);
  }

  // dumpAttributes :819-828：变量性 → 方向 → flow/stream
  void attributes(const SAttributes& at) {
    if (at.var == SVariability::Discrete) out_ += "discrete ";
    else if (at.var == SVariability::Param) out_ += "parameter ";
    else if (at.var == SVariability::Const) out_ += "constant ";
    if (at.dir == SDirection::Input) out_ += "input ";
    else if (at.dir == SDirection::Output) out_ += "output ";
    if (at.ct == SConnectorType::Flow) out_ += "flow ";
    else if (at.ct == SConnectorType::Stream) out_ += "stream ";
  }

  // dumpComponent :273-293
  void component(const SElement& e) {
    prefixes(e.prefixes);
    attributes(e.attr);
    path(e.type.leading_dot, e.type.path);
    out_ += ' ';
    sym(e.name);
    dims(e.attr.dims);
    modifier(e.mod);
    if (e.condition.is_valid()) {
      out_ += " if ";
      exp(e.condition);
    }
    constrainedby(e.prefixes);
    comment(e.cmt);
  }

  const char* restriction(const SElement& e) {
    switch (e.restriction) {
      case SRestriction::Class: return "class";
      case SRestriction::Model: return "model";
      case SRestriction::Record: return "record";
      case SRestriction::OperatorRecord: return "operator record";
      case SRestriction::Block: return "block";
      case SRestriction::Connector: return "connector";
      case SRestriction::ExpandableConnector: return "expandable connector";
      case SRestriction::Operator: return "operator";
      case SRestriction::Type: return "type";
      case SRestriction::Package: return "package";
      case SRestriction::Function:
      case SRestriction::ExternalFunction:  // dumpFunctionRestriction：AbsynDumpTpl.dumpPurity + "function"
        return e.purity == Purity::Impure ? "impure function"
               : e.purity == Purity::Pure ? "pure function"
                                          : "function";
      case SRestriction::OperatorFunction: return "operator function";
    }
    return "class";
  }

  // dumpClass :164-182
  void klass(const SElement& e, const std::string& indent) {
    prefixes(e.prefixes);
    if (e.is_encapsulated) out_ += "encapsulated ";
    if (e.is_partial) out_ += "partial ";
    out_ += restriction(e);
    out_ += ' ';
    if (e.def.kind == SClassDefKind::ClassExtends) out_ += "extends ";
    sym(e.name);
    switch (e.def.kind) {
      case SClassDefKind::Derived:  // :221-225
        out_ += " = ";
        attributes(e.def.derived_attr);
        path(e.def.derived_type.leading_dot, e.def.derived_type.path);
        dims(e.def.derived_attr.dims);
        modifier(e.def.derived_mod);
        comment(e.cmt);
        constrainedby(e.prefixes);
        return;
      case SClassDefKind::Enumeration:  // :226-231
        out_ += " = enumeration(";
        if (e.def.enum_unspecified) {
          out_ += ':';
        } else {
          for (std::size_t i = 0; i < e.def.enum_items.size(); ++i) {
            if (i) out_ += ", ";
            sym(e.def.enum_items[i].literal);
            comment(e.def.enum_items[i].cmt);
          }
        }
        out_ += ')';
        comment(e.cmt);
        constrainedby(e.prefixes);
        return;
      case SClassDefKind::Pder:  // :232-234
        out_ += " = der(";
        path(e.def.pder_leading_dot, e.def.pder_fn);
        for (Symbol v : e.def.pder_vars) {
          out_ += ", ";
          sym(v);
        }
        out_ += ')';
        comment(e.cmt);
        return;
      case SClassDefKind::ClassExtends:
        modifier(e.def.class_extends_mod);
        break;
      case SClassDefKind::Parts:
        break;
    }
    if (e.cmt.str.is_valid()) {
      out_ += ' ';
      str(e.cmt.str);
    }
    bool has_ann = e.cmt.annotation.is_valid() && !hidden_all(e.cmt.annotation);
    const SClassDef& d = e.def;
    bool empty_body = d.elements.empty() && d.equations.empty() && d.initial_equations.empty() &&
                      d.algorithms.empty() && d.initial_algorithms.empty() && !d.external.present;
    if (empty_body) {  // dumpClassFooter 空体分支：`model M [annotation(...);] end M`（探针 2026-09-16）
      if (has_ann) {
        out_ += ' ';
        annotation_bare(e.cmt.annotation);
        out_ += ';';
      }
      out_ += " end ";
      sym(e.name);
      constrainedby(e.prefixes);
      return;
    }
    out_ += '\n';
    std::string body = indent + "  ";
    class_body(d, body);
    if (has_ann) {  // 类注解在 end 前一行，体缩进（探针 2026-09-16）
      out_ += body;
      annotation_bare(e.cmt.annotation);
      out_ += ";\n";
    }
    out_ += indent + "end ";
    sym(e.name);
    constrainedby(e.prefixes);
  }

  // dumpClassDef :198-214：元素 → initial equation → initial algorithm → equation → algorithm → external
  void class_body(const SClassDef& d, const std::string& body) {
    elements(d.elements, body);
    std::string sec = body.size() >= 2 ? body.substr(0, body.size() - 2) : "";
    if (!d.initial_equations.empty()) {
      out_ += sec + "initial equation\n";
      for (Id<SEquation> q : d.initial_equations) equation(q, body);
    }
    if (!d.initial_algorithms.empty()) {
      out_ += sec + "initial algorithm\n";
      for (const SAlgorithm& alg : d.initial_algorithms)
        for (Id<SStatement> st : alg.stmts) statement(st, body);
    }
    if (!d.equations.empty()) {
      out_ += sec + "equation\n";
      for (Id<SEquation> q : d.equations) equation(q, body);
    }
    if (!d.algorithms.empty()) {
      out_ += sec + "algorithm\n";
      for (const SAlgorithm& alg : d.algorithms)
        for (Id<SStatement> st : alg.stmts) statement(st, body);
    }
    if (d.external.present) {  // :887-901
      out_ += body + "external";
      if (d.external.lang.is_valid()) {
        out_ += ' ';
        str(d.external.lang);
      }
      if (d.external.output.is_valid()) {
        out_ += ' ';
        exp(d.external.output);
        out_ += " =";
      }
      if (d.external.func_name.is_valid()) {
        out_ += ' ';
        sym(d.external.func_name);
        out_ += '(';
        for (std::size_t i = 0; i < d.external.args.size(); ++i) {
          if (i) out_ += ", ";
          exp(d.external.args[i]);
        }
        out_ += ')';
      }
      annotation(d.external.annotation);
      out_ += ";\n";
    }
  }

  // ---- 修饰 ----
  // 该子修饰在 dump 里是否产生文本。不产生文本的三种（分隔符也跟着省，Susan 的 separator 跳过空项）：
  //   NAMEMOD(_, NOMOD)——注解顶层保留下来的空子修饰，dumpSubModifier/dumpAnnotationSubModifier 都没有对应 case
  //   （探针 probe-s2/emptymod.mos：annotation(__Dymola_Commands, Evaluate = true) → annotation(Evaluate = true)）；
  //   BREAK_*——模板无 case；注解里名字在隐藏表中的 MOD（:800-813，逐层递归生效）。
  bool printable(const SSubMod& sub, bool in_annotation) const {
    if (!sub.mod.is_valid()) return false;
    const SMod& m = s_.mods[sub.mod];
    if (m.kind == SModKind::BreakComponent || m.kind == SModKind::BreakConnect) return false;
    if (in_annotation && m.kind == SModKind::Mod && hidden_annotation(name(sub.ident))) return false;
    return true;
  }
  // dumpModifier :739-747 / dumpAnnotationModifier :749-757：`(sub, sub) = binding`
  void modifier(Id<SMod> id, bool in_annotation = false) {
    if (!id.is_valid()) return;
    const SMod& m = s_.mods[id];
    if (m.kind != SModKind::Mod) return;
    bool first = true;
    for (const SSubMod& sub : m.subs) {
      if (!printable(sub, in_annotation)) continue;
      out_ += first ? "(" : ", ";
      first = false;
      submod(sub, in_annotation);
    }
    if (!first) out_ += ')';
    if (m.binding.is_valid()) {
      out_ += m.assign ? " := " : " = ";
      exp(m.binding);
    }
  }
  // 子修饰 :784-791；REDECL :772-778。前置条件：printable(sub)
  void submod(const SSubMod& sub, bool in_annotation) {
    const SMod& m = s_.mods[sub.mod];
    if (m.kind == SModKind::Redecl) {
      out_ += "redeclare ";
      if (m.is_each) out_ += "each ";
      if (m.is_final) out_ += "final ";
      const SElement& el = s_.elems[m.element];
      // 元素前缀（redeclare 已打印）
      if (el.prefixes.is_final && !m.is_final) out_ += "final ";
      if (el.prefixes.is_replaceable) out_ += "replaceable ";
      SElement copy = el;
      copy.prefixes.is_redeclare = false;
      copy.prefixes.is_final = false;
      copy.prefixes.is_replaceable = false;
      if (el.kind == SElementKind::Class) klass(copy, "");
      else if (el.kind == SElementKind::Component) component(copy);
      return;
    }
    if (m.is_each) out_ += "each ";
    if (m.is_final) out_ += "final ";
    sym(sub.ident);
    modifier(sub.mod, in_annotation);
    // 修饰的字符串注释（SCode.MOD.comment，SCode.mo:128）在 dumpSubModifier（:784-791）里不打印
    //（探针 probe-s2/extann.mos 的 N.y(start = 1 "c1")）：IR 保留，dump 省略
  }

  // ---- 注解 ----
  bool hidden_all(Id<SMod> ann) const {  // dumpAnnotation：修饰文本为空则整个注解不打印
    for (const SSubMod& sub : s_.mods[ann].subs)
      if (printable(sub, /*in_annotation=*/true)) return false;
    return true;
  }
  // " annotation(...)"；全部子修饰被剥掉时不打印
  void annotation(Id<SMod> ann) {
    if (!ann.is_valid() || hidden_all(ann)) return;
    out_ += ' ';
    annotation_bare(ann);
  }
  void annotation_bare(Id<SMod> ann) {  // 前置条件：!hidden_all(ann)
    out_ += "annotation";
    modifier(ann, /*in_annotation=*/true);
  }
  void comment(const SComment& c) {
    if (c.str.is_valid()) {
      out_ += ' ';
      str(c.str);
    }
    annotation(c.annotation);
  }

  // ---- 方程（dumpEquation :325-361）----
  bool is_error_level(Id<Exp> level) const {  // AssertionLevel.error（限定或非限定）→ 省略第三参
    if (!level.is_valid()) return true;
    const Exp& e = a_.exps[level];
    if (e.kind != ExpKind::CrefExp) return false;
    const Cref& c = a_.crefs[e.cref];
    return c.parts.size() == 2 && name(c.parts[0].name) == "AssertionLevel" &&
           name(c.parts[1].name) == "error";
  }

  void equation(Id<SEquation> id, const std::string& indent) {
    const SEquation& q = s_.eqs[id];
    std::string body = indent + "  ";
    out_ += indent;
    switch (q.kind) {
      case SEqKind::Equals:
        exp(q.a);
        out_ += " = ";
        exp(q.b);
        break;
      case SEqKind::Connect:
        out_ += "connect(";
        exp(q.a);
        out_ += ", ";
        exp(q.b);
        out_ += ')';
        break;
      case SEqKind::Assert:
        out_ += "assert(";
        exp(q.a);
        out_ += ", ";
        exp(q.b);
        if (!is_error_level(q.c)) {
          out_ += ", ";
          exp(q.c);
        }
        out_ += ')';
        break;
      case SEqKind::Terminate:
        out_ += "terminate(";
        exp(q.a);
        out_ += ')';
        break;
      case SEqKind::Reinit:
        out_ += "reinit(";
        exp(q.a);
        out_ += ", ";
        exp(q.b);
        out_ += ')';
        break;
      case SEqKind::NoRetCall:
        exp(q.a);
        break;
      case SEqKind::If:  // :363-401
        for (std::size_t i = 0; i < q.branches.size(); ++i) {
          out_ += (i == 0) ? "if " : indent + "elseif ";
          exp(q.branches[i].cond);
          out_ += " then\n";
          for (Id<SEquation> b : q.branches[i].body) equation(b, body);
        }
        if (!q.else_body.empty()) {
          out_ += indent + "else\n";
          for (Id<SEquation> b : q.else_body) equation(b, body);
        }
        out_ += indent + "end if";
        break;
      case SEqKind::For:  // :415-422
        out_ += "for ";
        sym(q.index);
        if (q.a.is_valid()) {
          out_ += " in ";
          exp(q.a);
        }
        out_ += " loop\n";
        for (Id<SEquation> b : q.body) equation(b, body);
        out_ += indent + "end for";
        break;
      case SEqKind::When:  // :425-445
        for (std::size_t i = 0; i < q.branches.size(); ++i) {
          out_ += (i == 0) ? "when " : indent + "elsewhen ";
          exp(q.branches[i].cond);
          out_ += " then\n";
          for (Id<SEquation> b : q.branches[i].body) equation(b, body);
        }
        out_ += indent + "end when";
        break;
    }
    comment(q.cmt);
    out_ += ";\n";
  }

  // ---- 语句（dumpStatement :474-516）----
  void statement(Id<SStatement> id, const std::string& indent) {
    const SStatement& st = s_.stmts[id];
    std::string body = indent + "  ";
    out_ += indent;
    bool with_comment = true;
    switch (st.kind) {
      case SStmtKind::Assign:
        exp(st.a);
        out_ += " := ";
        exp(st.b);
        break;
      case SStmtKind::Assert:  // ALG_ASSERT/TERMINATE/REINIT 不打印 comment（:486-497）
        out_ += "assert(";
        exp(st.a);
        out_ += ", ";
        exp(st.b);
        if (!is_error_level(st.c)) {
          out_ += ", ";
          exp(st.c);
        }
        out_ += ')';
        with_comment = false;
        break;
      case SStmtKind::Terminate:
        out_ += "terminate(";
        exp(st.a);
        out_ += ')';
        with_comment = false;
        break;
      case SStmtKind::Reinit:
        out_ += "reinit(";
        exp(st.a);
        out_ += ", ";
        exp(st.b);
        out_ += ')';
        with_comment = false;
        break;
      case SStmtKind::NoRetCall:
        exp(st.a);
        break;
      case SStmtKind::Return: out_ += "return"; break;
      case SStmtKind::Break: out_ += "break"; break;
      case SStmtKind::If:  // 总是打印 else（:531-532）
        for (std::size_t i = 0; i < st.branches.size(); ++i) {
          out_ += (i == 0) ? "if " : indent + "elseif ";
          exp(st.branches[i].cond);
          out_ += " then\n";
          for (Id<SStatement> b : st.branches[i].body) statement(b, body);
        }
        out_ += indent + "else\n";
        for (Id<SStatement> b : st.else_body) statement(b, body);
        out_ += indent + "end if";
        break;
      case SStmtKind::For:
        out_ += "for ";
        sym(st.index);
        if (st.a.is_valid()) {
          out_ += " in ";
          exp(st.a);
        }
        out_ += " loop\n";
        for (Id<SStatement> b : st.body) statement(b, body);
        out_ += indent + "end for";
        break;
      case SStmtKind::While:
        out_ += "while ";
        exp(st.a);
        out_ += " loop\n";
        for (Id<SStatement> b : st.body) statement(b, body);
        out_ += indent + "end while";
        break;
      case SStmtKind::When:
        for (std::size_t i = 0; i < st.branches.size(); ++i) {
          out_ += (i == 0) ? "when " : indent + "elsewhen ";
          exp(st.branches[i].cond);
          out_ += " then\n";
          for (Id<SStatement> b : st.branches[i].body) statement(b, body);
        }
        out_ += indent + "end when";
        break;
    }
    if (with_comment) comment(st.cmt);
    out_ += ";\n";
  }

  const SCode& s_;
  const Absyn& a_;
  std::string out_;
};

}  // namespace

std::string dump_scode_element(const SCode& s, Id<SElement> el, const std::string& indent) {
  return Dumper(s).element(el, indent);
}

std::string dump_scode_program(const SCode& s) { return Dumper(s).program(); }

}  // namespace ymc
