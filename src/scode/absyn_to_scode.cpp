// Absyn → SCode 翻译实现。逐条对应 OMC AbsynToSCode.mo（基线 3ceb0c9d06，行号见各处注释）。
//
// OMC 做的归一化（研究底稿 R9 结论 2）：
//   1. 组件列表 `Real a, b;` 拆成两个 COMPONENT（:1194-1228）；组件维在前、类型维在后（:1205）
//   2. public/protected 折进每个元素的 Prefixes.visibility（:600-612）
//   3. classParts 分成 元素/方程/初始方程/算法/初始算法/external 桶（:448-456）；
//      **多个 equation 段在 SCode 中段序反转**（:635-637，后面的段排前面；算法段不反转 :691）——
//      方案 §6.2 要求复现 OMC 的顺序，这里照做，不"修正"
//   4. `x.y = z` 展成 `x(y = z)`（:1790-1794）；空修饰规范化为 NOMOD（:1714）；空子修饰丢弃，注解里保留（:1737,:985）
//   5. 组导入 `A.{x, y = z}` 拆成单条 IMPORT（:1265-1291）
//   6. 方程/语句里的 assert/terminate/reinit 调用提升为专用节点，其余调用是 NORETCALL（:1595-1619, :844-865）
//   另：EQ_IF 把 elseif 摊平成并列分支（:1554-1562），ALG_IF 保留 elseif（:798-804）；多迭代器 for 展成嵌套、
//   第一个迭代器最外层（:1587-1590, :811-814）；组件级 final 进 Prefixes 不进 Mod（:1201,:1208）；
//   constrainedby → Replaceable 的 ConstrainClass（:1211,:1357-1364）；类的多条 annotation 已在 S1 解析时合并。
#include "scode/absyn_to_scode.h"

#include "base/strings.h"

namespace ymc {

namespace {

class Translator {
 public:
  explicit Translator(SCode& out) : s_(out), a_(out.absyn) {}

  Id<SElement> klass(Id<ClassDef> cid, const SPrefixes& prefixes);

 private:
  // ---- 小工具 ----
  std::string_view name(Symbol s) const { return a_.interner.str(s); }

  // translateComment（:1466-1476）/ translateCommentWithLineInfoChanges（:1494-1503）：注释串解转义
  //（System.unescapedString）；表达式里的字符串字面量不动。
  SComment comment(Symbol str, Id<Modification> ann) {
    SComment c;
    if (str.is_valid()) c.str = a_.interner.intern(unescape_modelica_string(name(str)));
    c.annotation = annotation(ann);
    return c;
  }

  // translateAnnotation（:970-991）：keepEmpty = true（注解里的空修饰有意义，如 Dialog()）
  Id<SMod> annotation(Id<Modification> ann) {
    if (!ann.is_valid()) return Id<SMod>{};
    return mod(ann, /*final=*/false, /*each=*/false, SourceLoc{}, /*keep_empty=*/true);
  }

  // translateMod（:1686-1717）：({}, NONE, NOT_FINAL, NOT_EACH) → NOMOD（无效 Id）
  Id<SMod> mod(Id<Modification> m, bool is_final, bool is_each, SourceLoc loc, bool keep_empty = false) {
    std::vector<SSubMod> subs;
    Id<Exp> binding;
    bool assign = false;
    if (m.is_valid()) {
      const Modification& am = a_.mods[m];
      subs = args(am.args, keep_empty);
      binding = am.binding;
      assign = am.assign;
    }
    if (subs.empty() && !binding.is_valid() && !is_final && !is_each) return Id<SMod>{};
    SMod sm;
    sm.kind = SModKind::Mod;
    sm.is_final = is_final;
    sm.is_each = is_each;
    sm.subs = std::move(subs);
    sm.binding = binding;
    sm.assign = assign;
    sm.loc = loc;
    return s_.add_mod(std::move(sm));
  }

  // translateArgs（:1719-1771）
  std::vector<SSubMod> args(const std::vector<ModArg>& in, bool keep_empty) {
    std::vector<SSubMod> out;
    for (const ModArg& arg : in) {
      if (arg.is_redeclare) {  // REDECLARATION → NAMEMOD(name, REDECL(final, each, element))（:1746-1754）
        std::vector<Id<SElement>> els;
        element(arg.redecl, SVisibility::Public, els);
        if (els.size() != 1) continue;  // OMC 要求恰好一个元素
        SMod rm;
        rm.kind = SModKind::Redecl;
        rm.is_final = arg.final_;
        rm.is_each = arg.each_;
        rm.element = els[0];
        SSubMod sub;
        sub.ident = s_.elems[els[0]].name;
        sub.mod = s_.add_mod(std::move(rm));
        out.push_back(std::move(sub));
        continue;
      }
      if (arg.is_break) {  // :1760-1765
        SMod bm;
        SSubMod sub;
        if (arg.break_a.is_valid()) {
          bm.kind = SModKind::BreakConnect;
          bm.break_a = arg.break_a;
          bm.break_b = arg.break_b;
          sub.ident = a_.interner.intern("");
        } else {
          bm.kind = SModKind::BreakComponent;
          sub.ident = arg.path.empty() ? a_.interner.intern("") : arg.path[0];
        }
        sub.mod = s_.add_mod(std::move(bm));
        out.push_back(std::move(sub));
        continue;
      }
      // MODIFICATION（:1731-1741）：内层 translateMod 不带 keepEmpty（默认 false）——只有注解的**顶层**保留空子修饰。
      // "空" = 无子修饰、无绑定、非 final、非 each（translateMod :1712-1715；只有字符串注释也算空）。
      // 空：keep_empty 时保留为 NAMEMOD(ident, NOMOD)（无效 Id），否则整条丢弃。探针 probe-s2/emptymod.mos：
      //   x(final y, each z, w(), v(u), t "cmt") → x(final y, each z)；annotation(Foo, Bar(baz), Qux(a = 1, b)) → 只剩 Qux(a = 1)
      std::vector<SSubMod> inner_subs;
      if (arg.sub.is_valid()) inner_subs = args(a_.mods[arg.sub].args, /*keep_empty=*/false);
      Id<SMod> smod;  // 无效 = NOMOD
      if (!inner_subs.empty() || arg.binding.is_valid() || arg.final_ || arg.each_) {
        SMod sm;
        sm.kind = SModKind::Mod;
        sm.is_final = arg.final_;
        sm.is_each = arg.each_;
        sm.subs = std::move(inner_subs);
        sm.binding = arg.binding;
        sm.assign = arg.assign;
        sm.comment = arg.string_comment;
        smod = s_.add_mod(std::move(sm));
      } else if (!keep_empty) {
        continue;
      }
      if (arg.path.empty()) continue;
      out.push_back(sub_path(arg.path, 0, smod));
    }
    return out;
  }

  // translateSub（:1773-1796）：QUALIFIED(i, path) → NAMEMOD(i, MOD({}, {sub(path)}, NONE))
  SSubMod sub_path(const std::vector<Symbol>& path, std::size_t i, Id<SMod> leaf) {
    SSubMod sub;
    sub.ident = path[i];
    if (i + 1 == path.size()) {
      sub.mod = leaf;
      return sub;
    }
    SMod wrap;
    wrap.kind = SModKind::Mod;
    wrap.subs.push_back(sub_path(path, i + 1, leaf));
    sub.mod = s_.add_mod(std::move(wrap));
    return sub;
  }

  SPrefixes prefixes_of(const Element& el, SVisibility vis) {
    SPrefixes p;
    p.vis = vis;
    p.is_redeclare = el.is_redeclare;
    p.is_final = el.is_final;
    p.io = el.is_inner && el.is_outer ? SInnerOuter::InnerOuter
           : el.is_inner              ? SInnerOuter::Inner
           : el.is_outer              ? SInnerOuter::Outer
                                      : SInnerOuter::None;
    p.is_replaceable = el.is_replaceable;
    if (el.has_constrainedby) {  // translateConstrainClass（:1344-1368）
      p.has_cc = true;
      p.cc.leading_dot = el.cb_leading_dot;
      p.cc.path = el.cb_path;
      p.cc.mod = mod(el.cb_mod, false, false, el.loc);
      p.cc.cmt = comment(el.cb_comment, el.cb_annotation);
    }
    return p;
  }

  static SVariability variability(TypePrefix::Variability v) {
    switch (v) {
      case TypePrefix::Variability::Continuous: return SVariability::Var;
      case TypePrefix::Variability::Discrete: return SVariability::Discrete;
      case TypePrefix::Variability::Parameter: return SVariability::Param;
      case TypePrefix::Variability::Constant: return SVariability::Const;
    }
    return SVariability::Var;
  }
  static SDirection direction(TypePrefix::Causality c) {
    switch (c) {
      case TypePrefix::Causality::None: return SDirection::Bidir;
      case TypePrefix::Causality::Input: return SDirection::Input;
      case TypePrefix::Causality::Output: return SDirection::Output;
    }
    return SDirection::Bidir;
  }
  static SConnectorType connector_type(TypePrefix::Flow f) {
    switch (f) {
      case TypePrefix::Flow::None: return SConnectorType::Potential;
      case TypePrefix::Flow::Flow: return SConnectorType::Flow;
      case TypePrefix::Flow::Stream: return SConnectorType::Stream;
    }
    return SConnectorType::Potential;
  }

  // translateRestriction（:269-324）：function 有 external 段 → FR_EXTERNAL_FUNCTION（:285-288）
  static SRestriction restriction(const ClassDef& c) {
    switch (c.restriction) {
      case Restriction::Class: return SRestriction::Class;
      case Restriction::Model: return SRestriction::Model;
      case Restriction::Record: return SRestriction::Record;
      case Restriction::OperatorRecord: return SRestriction::OperatorRecord;
      case Restriction::Block: return SRestriction::Block;
      case Restriction::Connector: return SRestriction::Connector;
      case Restriction::ExpandableConnector: return SRestriction::ExpandableConnector;
      case Restriction::Operator: return SRestriction::Operator;
      case Restriction::Type: return SRestriction::Type;
      case Restriction::Package: return SRestriction::Package;
      case Restriction::Function:
        return c.has_external ? SRestriction::ExternalFunction : SRestriction::Function;
      case Restriction::OperatorFunction: return SRestriction::OperatorFunction;
    }
    return SRestriction::Class;
  }

  // ---- 元素 ----
  // translateElement / translateElementspec（:1006-1243）；一个 Absyn 元素可产出多个 SCode 元素
  void element(Id<Element> eid, SVisibility vis, std::vector<Id<SElement>>& out) {
    const Element& el = a_.elems[eid];
    switch (el.kind) {
      case ElementKind::Import: {  // translateImports（:1245-1291）
        if (el.imp.multi.empty()) {
          SElement se;
          se.kind = SElementKind::Import;
          se.loc = el.loc;
          se.prefixes.vis = vis;
          se.imp = el.imp;
          out.push_back(s_.add_elem(std::move(se)));
        } else {
          for (std::size_t i = 0; i < el.imp.multi.size(); ++i) {
            SElement se;
            se.kind = SElementKind::Import;
            se.loc = el.loc;
            se.prefixes.vis = vis;
            se.imp.leading_dot = el.imp.leading_dot;
            se.imp.path = el.imp.path;
            se.imp.path.push_back(el.imp.multi[i]);                      // QUAL_IMPORT(prefix.name)
            if (i < el.imp.multi_alias.size() && el.imp.multi_alias[i].is_valid())
              se.imp.alias = el.imp.multi_alias[i];                        // NAMED_IMPORT(rename, prefix.name)
            out.push_back(s_.add_elem(std::move(se)));
          }
        }
        return;
      }
      case ElementKind::Extends: {  // :1173-1186
        SElement se;
        se.kind = SElementKind::Extends;
        se.loc = el.loc;
        se.prefixes.vis = vis;
        se.base_leading_dot = el.ext.leading_dot;
        se.base_path = el.ext.path;
        se.mod = mod(el.ext.mod, false, false, el.loc);
        se.extends_annotation = annotation(el.ext.annotation);
        out.push_back(s_.add_elem(std::move(se)));
        return;
      }
      case ElementKind::ClassDef: {  // :1154-1171
        out.push_back(klass(el.cls, prefixes_of(el, vis)));
        return;
      }
      case ElementKind::Component: {  // :1190-1230
        const ComponentClause& cc = el.comp;
        SPrefixes pre = prefixes_of(el, vis);
        for (const ComponentDecl& d : cc.comps) {
          SElement se;
          se.kind = SElementKind::Component;
          se.loc = el.loc;  // 元素级 info（同一声明的多个组件共享，:1207）
          se.name = d.name;
          se.prefixes = pre;
          se.attr.dims = d.subscripts;  // 组件维在前、类型维在后（:1205）
          for (Id<Exp> dim : cc.type_subscripts) se.attr.dims.push_back(dim);
          se.attr.ct = connector_type(cc.prefix.flow);
          se.attr.var = variability(cc.prefix.var);
          se.attr.dir = direction(cc.prefix.caus);
          se.type.leading_dot = cc.type_leading_dot;
          se.type.path = cc.type_path;
          se.mod = mod(d.mod, /*final=*/false, /*each=*/false, el.loc);  // 组件 final 不进 Mod（:1201）
          se.cmt = comment(d.string_comment, d.annotation);
          se.condition = d.condition;
          out.push_back(s_.add_elem(std::move(se)));
        }
        return;
      }
    }
  }

  // ---- 方程 ----
  // 单标识符调用 name(args) 且无命名实参时返回 args；否则 npos
  bool simple_call(Id<Exp> e, std::string_view fn, std::size_t& npos, bool& has_named) const {
    const Exp& x = a_.exps[e];
    if (x.kind != ExpKind::Call || x.partial_fn || x.call_suffix.is_valid()) return false;
    const Cref& c = a_.crefs[x.cref];
    if (c.leading_dot || c.parts.size() != 1 || !c.parts[0].subscripts.empty()) return false;
    if (name(c.parts[0].name) != fn) return false;
    npos = 0;
    has_named = false;
    for (std::size_t i = 0; i < x.args.size(); ++i) {
      if (i < x.arg_names.size() && x.arg_names[i].is_valid()) has_named = true;
      else ++npos;
    }
    return true;
  }
  // assert 的三种形态（:1595-1608）：2 个位置实参 / 3 个 / 2 个 + 具名 level
  bool assert_args(Id<Exp> e, Id<Exp>& cond, Id<Exp>& msg, Id<Exp>& level) const {
    std::size_t npos;
    bool named;
    if (!simple_call(e, "assert", npos, named)) return false;
    const Exp& x = a_.exps[e];
    if (!named && npos == 2) {
      cond = x.args[0];
      msg = x.args[1];
      level = s_.assertion_level_error;
      return true;
    }
    if (!named && npos == 3) {
      cond = x.args[0];
      msg = x.args[1];
      level = x.args[2];
      return true;
    }
    if (named && x.args.size() == 3 && npos == 2 && x.arg_names.size() == 3 &&
        x.arg_names[2].is_valid() && name(x.arg_names[2]) == "level") {
      cond = x.args[0];
      msg = x.args[1];
      level = x.args[2];
      return true;
    }
    return false;
  }
  bool unary_call(Id<Exp> e, std::string_view fn, Id<Exp>& arg0) const {
    std::size_t npos;
    bool named;
    if (!simple_call(e, fn, npos, named) || named || npos != 1) return false;
    arg0 = a_.exps[e].args[0];
    return true;
  }
  bool binary_call(Id<Exp> e, std::string_view fn, Id<Exp>& arg0, Id<Exp>& arg1) const {
    std::size_t npos;
    bool named;
    if (!simple_call(e, fn, npos, named) || named || npos != 2) return false;
    arg0 = a_.exps[e].args[0];
    arg1 = a_.exps[e].args[1];
    return true;
  }

  // translateEquation（:1534-1629）
  Id<SEquation> equation(Id<Equation> eid) {
    const Equation& eq = a_.eqs[eid];
    SEquation se;
    se.loc = eq.loc;
    se.cmt = comment(eq.string_comment, eq.annotation);
    switch (eq.kind) {
      case EqKind::Equality:
        se.kind = SEqKind::Equals;
        se.a = eq.lhs;
        se.b = eq.rhs;
        break;
      case EqKind::Connect:
        se.kind = SEqKind::Connect;
        se.a = eq.lhs;
        se.b = eq.rhs;
        break;
      case EqKind::FunctionCall: {
        Id<Exp> x, y, z;
        if (assert_args(eq.lhs, x, y, z)) {
          se.kind = SEqKind::Assert;
          se.a = x; se.b = y; se.c = z;
        } else if (unary_call(eq.lhs, "terminate", x)) {
          se.kind = SEqKind::Terminate;
          se.a = x;
        } else if (binary_call(eq.lhs, "reinit", x, y)) {
          se.kind = SEqKind::Reinit;
          se.a = x; se.b = y;
        } else {
          se.kind = SEqKind::NoRetCall;  // 含参数个数不对的 assert 等（:1621-1626）
          se.a = eq.lhs;
        }
        break;
      }
      case EqKind::If: {  // EQ_IF：elseif 摊平（:1554-1562）
        se.kind = SEqKind::If;
        for (const EqBranch& b : eq.branches) {
          if (b.cond.is_valid()) {
            SEqBranch sb;
            sb.cond = b.cond;
            sb.body = equations(b.body);
            se.branches.push_back(std::move(sb));
          } else {
            se.else_body = equations(b.body);
          }
        }
        break;
      }
      case EqKind::When: {  // :1564-1571
        se.kind = SEqKind::When;
        for (const EqBranch& b : eq.branches) {
          SEqBranch sb;
          sb.cond = b.cond;
          sb.body = equations(b.body);
          se.branches.push_back(std::move(sb));
        }
        break;
      }
      case EqKind::For: {  // 多迭代器 → 嵌套，第一个最外层（:1582-1592）
        std::vector<Id<SEquation>> inner = equations(eq.body);
        for (std::size_t i = eq.iters.size(); i-- > 1;) {
          SEquation nest;
          nest.kind = SEqKind::For;
          nest.loc = eq.loc;
          nest.index = eq.iters[i].name;
          nest.a = eq.iters[i].range;
          nest.body = std::move(inner);
          inner.clear();
          inner.push_back(s_.add_eq(std::move(nest)));
        }
        se.kind = SEqKind::For;
        se.index = eq.iters.empty() ? Symbol{} : eq.iters[0].name;
        se.a = eq.iters.empty() ? Id<Exp>{} : eq.iters[0].range;
        se.body = std::move(inner);
        break;
      }
    }
    return s_.add_eq(std::move(se));
  }

  std::vector<Id<SEquation>> equations(const std::vector<Id<Equation>>& in) {
    std::vector<Id<SEquation>> out;
    out.reserve(in.size());
    for (Id<Equation> e : in) out.push_back(equation(e));
    return out;
  }

  // 多段方程：段序反转（translateClassdefEquations :635-637），段内顺序不变
  std::vector<Id<SEquation>> equation_sections(const std::vector<Id<Equation>>& all,
                                               const std::vector<std::size_t>& starts) {
    if (starts.size() <= 1) return equations(all);
    std::vector<Id<SEquation>> out;
    for (std::size_t si = starts.size(); si-- > 0;) {
      std::size_t b = starts[si];
      std::size_t e = si + 1 < starts.size() ? starts[si + 1] : all.size();
      for (std::size_t i = b; i < e; ++i) out.push_back(equation(all[i]));
    }
    return out;
  }

  // ---- 语句（translateClassdefAlgorithmItem :772-891）----
  Id<SStatement> statement(Id<Statement> sid) {
    const Statement& st = a_.stmts[sid];
    SStatement ss;
    ss.loc = st.loc;
    ss.cmt = comment(st.string_comment, st.annotation);
    switch (st.kind) {
      case StmtKind::Assign:
        ss.kind = SStmtKind::Assign;
        ss.a = st.lhs;
        ss.b = st.rhs;
        break;
      case StmtKind::Call: {
        Id<Exp> x, y, z;
        if (assert_args(st.lhs, x, y, z)) {
          ss.kind = SStmtKind::Assert;
          ss.a = x; ss.b = y; ss.c = z;
        } else if (unary_call(st.lhs, "terminate", x)) {
          ss.kind = SStmtKind::Terminate;
          ss.a = x;
        } else if (binary_call(st.lhs, "reinit", x, y)) {
          ss.kind = SStmtKind::Reinit;
          ss.a = x; ss.b = y;
        } else {
          ss.kind = SStmtKind::NoRetCall;
          ss.a = st.lhs;
        }
        break;
      }
      case StmtKind::If: {  // ALG_IF 保留 elseif 列表（:798-804）
        ss.kind = SStmtKind::If;
        for (const StmtBranch& b : st.branches) {
          if (b.cond.is_valid()) {
            SStmtBranch sb;
            sb.cond = b.cond;
            sb.body = statements(b.body);
            ss.branches.push_back(std::move(sb));
          } else {
            ss.else_body = statements(b.body);
          }
        }
        break;
      }
      case StmtKind::While: {
        ss.kind = SStmtKind::While;
        if (!st.branches.empty()) {
          ss.a = st.branches[0].cond;
          ss.body = statements(st.branches[0].body);
        }
        break;
      }
      case StmtKind::When: {
        ss.kind = SStmtKind::When;
        for (const StmtBranch& b : st.branches) {
          SStmtBranch sb;
          sb.cond = b.cond;
          sb.body = statements(b.body);
          ss.branches.push_back(std::move(sb));
        }
        break;
      }
      case StmtKind::For: {
        std::vector<Id<SStatement>> inner = statements(st.body);
        for (std::size_t i = st.iters.size(); i-- > 1;) {
          SStatement nest;
          nest.kind = SStmtKind::For;
          nest.loc = st.loc;
          nest.index = st.iters[i].name;
          nest.a = st.iters[i].range;
          nest.body = std::move(inner);
          inner.clear();
          inner.push_back(s_.add_stmt(std::move(nest)));
        }
        ss.kind = SStmtKind::For;
        ss.index = st.iters.empty() ? Symbol{} : st.iters[0].name;
        ss.a = st.iters.empty() ? Id<Exp>{} : st.iters[0].range;
        ss.body = std::move(inner);
        break;
      }
      case StmtKind::Break: ss.kind = SStmtKind::Break; break;
      case StmtKind::Return: ss.kind = SStmtKind::Return; break;
    }
    return s_.add_stmt(std::move(ss));
  }

  std::vector<Id<SStatement>> statements(const std::vector<Id<Statement>>& in) {
    std::vector<Id<SStatement>> out;
    out.reserve(in.size());
    for (Id<Statement> st : in) out.push_back(statement(st));
    return out;
  }

  // ---- 类体（translateClassdef :390-512）----
  // class_ann：已翻译的类注解（external 段的注解合并要用；由 klass() 翻译一次后传入）
  SClassDef class_def(const ClassDef& c, Id<SMod> class_ann) {
    SClassDef d;
    if (c.is_short && c.is_enumeration) {  // :461-474, translateEnumlist :563-585
      d.kind = SClassDefKind::Enumeration;
      d.enum_unspecified = c.enum_unspecified;
      for (const EnumLiteral& lit : c.enum_lits) {
        SEnumItem it;
        it.literal = lit.name;
        it.cmt = comment(lit.string_comment, lit.annotation);
        d.enum_items.push_back(it);
      }
      return d;
    }
    if (c.is_short && c.is_der) {  // :499-504
      d.kind = SClassDefKind::Pder;
      const Cref& fn = a_.crefs[c.der_fn];
      d.pder_leading_dot = fn.leading_dot;
      for (const CrefPart& p : fn.parts) d.pder_fn.push_back(p.name);
      d.pder_vars = c.der_vars;
      return d;
    }
    if (c.is_short) {  // DERIVED（:429-437）：typeSpec 无维度，维度进 attributes
      d.kind = SClassDefKind::Derived;
      d.derived_type.leading_dot = c.short_leading_dot;
      d.derived_type.path = c.short_type;
      d.derived_attr.dims = c.short_subscripts;
      d.derived_attr.ct = connector_type(c.short_prefix.flow);
      d.derived_attr.var = variability(c.short_prefix.var);
      d.derived_attr.dir = direction(c.short_prefix.caus);
      d.derived_mod = mod(c.short_mod, false, false, c.loc);
      return d;
    }
    // PARTS / CLASS_EXTENDS（:439-459, :483-497）
    d.kind = c.is_class_extends ? SClassDefKind::ClassExtends : SClassDefKind::Parts;
    if (c.is_class_extends) d.class_extends_mod = mod(c.class_extends_mod, false, false, c.loc);
    for (Id<Element> e : c.elements)
      element(e, a_.elems[e].is_protected ? SVisibility::Protected : SVisibility::Public, d.elements);
    d.equations = equation_sections(c.equations, c.equation_sections);
    d.initial_equations = equation_sections(c.initial_equations, c.initial_equation_sections);
    for (const AlgorithmSection& alg : c.algorithms) {  // 算法段保持源码顺序（:691）
      SAlgorithm sa;
      sa.stmts = statements(alg.stmts);
      if (alg.is_initial) d.initial_algorithms.push_back(std::move(sa));
      else d.algorithms.push_back(std::move(sa));
    }
    if (c.has_external) {  // translateClassdefExternaldecls（:908-937）
      d.external.present = true;
      d.external.lang = c.ext_lang;
      d.external.output = c.ext_lhs;
      if (c.ext_call.is_valid()) {
        const Exp& call = a_.exps[c.ext_call];
        if (call.kind == ExpKind::Call) {
          const Cref& fn = a_.crefs[call.cref];
          if (!fn.parts.empty()) d.external.func_name = fn.parts.back().name;
          d.external.args = call.args;
        }
      }
      // translateAlternativeExternalAnnotation：oracle（omc 1.25.0，tag v1.25.0 AbsynToSCode.mo:508-531）
      // 无条件把类注解并进 external 段注解（SCodeUtil.mergeSCodeOptAnn：外层子修饰在前）；探针
      // probe-s2/extann.mos 六种组合全部印证。基线 HEAD 3ceb0c9d06（:514-561）已改成"仅当 external
      // 无注解时按白名单 Library/Include/LibraryDirectory/SourceDirectory/License 复制"——按方案以安装版为准。
      Id<SMod> ext_ann = annotation(c.ext_annotation);
      if (!ext_ann.is_valid()) {
        d.external.annotation = class_ann;  // 共享同一节点（dump 只读；后续阶段若要改写需先拷贝）
      } else if (!class_ann.is_valid()) {
        d.external.annotation = ext_ann;
      } else {
        SMod merged = s_.mods[ext_ann];
        const std::vector<SSubMod>& more = s_.mods[class_ann].subs;
        merged.subs.insert(merged.subs.end(), more.begin(), more.end());
        if (!merged.binding.is_valid()) merged.binding = s_.mods[class_ann].binding;
        d.external.annotation = s_.add_mod(std::move(merged));
      }
    }
    return d;
  }

  SCode& s_;
  Absyn& a_;
};

Id<SElement> Translator::klass(Id<ClassDef> cid, const SPrefixes& prefixes) {
  const ClassDef& c = a_.classes[cid];
  SElement se;
  se.kind = SElementKind::Class;
  se.loc = c.loc;
  se.name = c.name;
  se.prefixes = prefixes;
  se.is_encapsulated = c.is_encapsulated;
  se.is_partial = c.is_partial;
  se.restriction = restriction(c);
  se.purity = c.purity;
  Id<SMod> class_ann = annotation(c.annotation);
  se.def = class_def(c, class_ann);
  se.cmt.annotation = class_ann;
  // 类的字符串注释：长类（PARTS / CLASS_EXTENDS）走 translateCommentList（AbsynToSCode.mo:1474-1500 v1.25.0）——没有类级
  // annotation 时（`case ({},_)`）原文不解转义，打印时再转义一遍成双重转义（`"say \"hi\""` → `"say \\\"hi\\\""`、`"a\nb"` →
  // `"a\\nb"`，CC 探针 We / Wk）；有 annotation（空的 `annotation()`、夹在方程段里的也算，Wf / Wl）才 System.unescapedString
  //（Wg / Wj：`\n` 解成真换行、照原样打）。短类 / 枚举 / der 走 translateComment（:1454-1472），恒解转义。
  if (c.string_comment.is_valid()) {
    const bool long_form = !c.is_short && !c.is_enumeration && !c.is_der;
    se.cmt.str = long_form && !c.annotation.is_valid()
                     ? c.string_comment
                     : a_.interner.intern(unescape_modelica_string(name(c.string_comment)));
  }
  return s_.add_elem(std::move(se));
}

}  // namespace

Id<SElement> translate_class(SCode& out, Id<ClassDef> cls, const SPrefixes& prefixes) {
  if (!out.assertion_level_error.is_valid()) {  // 共享的 `.AssertionLevel.error`（AbsynToSCode.mo:69-70）
    Cref c;
    c.leading_dot = true;
    c.parts.push_back({out.absyn.interner.intern("AssertionLevel"), {}});
    c.parts.push_back({out.absyn.interner.intern("error"), {}});
    Exp e;
    e.kind = ExpKind::CrefExp;
    e.cref = out.absyn.add_cref(std::move(c));
    out.assertion_level_error = out.absyn.add_exp(std::move(e));
  }
  Translator t(out);
  return t.klass(cls, prefixes);
}

void translate_program(SCode& out, const std::vector<Id<ClassDef>>& top) {
  for (Id<ClassDef> cid : top) {
    SPrefixes p;  // 顶层：PUBLIC、非 redeclare、final 取类自身、无 inner/outer、非 replaceable（:154-159）
    p.is_final = out.absyn.classes[cid].is_final;
    out.program.push_back(translate_class(out, cid, p));
  }
}

}  // namespace ymc
