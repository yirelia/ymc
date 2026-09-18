// 压平实现（S3 第一里程碑）。锚点见 flatten.h 头注与各处 R11/R13/R14 引用。
#include "flat/flatten.h"

#include "absyn/absyn.h"
#include "base/strings.h"   // unescape_modelica_string

namespace ymc {

namespace {

// 内建属性表（R13 §4.2：压平属性顺序 = 内建组件数组顺序，与书写序无关）。
// vk 决定值的处理：Str/Int/Bool 直译字面量；Real 需整型→实型强转；StateSel/EnumComp 是枚举字面量（`T.lit`）。
// 表内没有的名字（Real 的 unbounded/startOrigin、String 的 fixed）= 合法但 omc 丢弃（见 collect_attributes）。
enum class AttrVK : std::uint8_t { Str, Real, Int, Bool, StateSel, EnumComp };
struct AttrSpec { const char* name; AttrVK vk; };

const std::vector<AttrSpec>& attr_table(TypeKind k) {
  static const std::vector<AttrSpec> real_{
      {"quantity", AttrVK::Str}, {"unit", AttrVK::Str}, {"displayUnit", AttrVK::Str},
      {"min", AttrVK::Real}, {"max", AttrVK::Real}, {"start", AttrVK::Real}, {"fixed", AttrVK::Bool},
      {"nominal", AttrVK::Real}, {"stateSelect", AttrVK::StateSel}};
  static const std::vector<AttrSpec> int_{
      {"quantity", AttrVK::Str}, {"min", AttrVK::Int}, {"max", AttrVK::Int},
      {"start", AttrVK::Int}, {"fixed", AttrVK::Bool}};
  static const std::vector<AttrSpec> bool_{
      {"quantity", AttrVK::Str}, {"start", AttrVK::Bool}, {"fixed", AttrVK::Bool}};
  static const std::vector<AttrSpec> string_{{"quantity", AttrVK::Str}, {"start", AttrVK::Str}};
  static const std::vector<AttrSpec> enum_{
      {"quantity", AttrVK::Str}, {"min", AttrVK::EnumComp}, {"max", AttrVK::EnumComp},
      {"start", AttrVK::EnumComp}, {"fixed", AttrVK::Bool}};
  static const std::vector<AttrSpec> none_{};
  switch (k) {
    case TypeKind::Real: return real_;
    case TypeKind::Integer: return int_;
    case TypeKind::Boolean: return bool_;
    case TypeKind::String: return string_;
    case TypeKind::Enumeration: return enum_;
    default: return none_;
  }
}

// 修饰传播上下文（R15 §3.3：没有全局修饰环境，每层 instClassDef 只把 mod 按名拆成子修饰传给下一层）。
// flatten 的 DFS 本身就是这个逐层下降，这里把"适用于本层各子元素的修饰"顺着 DFS 带下去。
//   mods    ：本层子元素可能命中的修饰节点（其 subs 按子元素名匹配；多个 = 重名/多来源并集，如 mod4 的两个 a(...)）。
//   scope   ：修饰里表达式 cref 的解析作用域（= 修饰书写处的实例；R15 §1 binding 自带作用域）。
//   vprefix ：修饰里表达式解析后要加的前缀（= 书写处的前缀）。
//   final_all：上层 final 传播（`final A a(...)` → a 整棵子树 final）。
struct ModCtx {
  std::vector<Id<SMod>> mods;
  NodeId scope;
  std::vector<Symbol> vprefix;
  bool final_all = false;
};

// 单个属性/绑定的来源：一个修饰节点 + 它的解析作用域。按优先级排列（外层实例修饰在前）。
struct ModSource {
  Id<SMod> mod;
  NodeId scope;
  std::vector<Symbol> vprefix;
};

class Flattener {
 public:
  Flattener(Inst& inst, Interner& in) : I_(inst), in_(in) {}

  FlattenResult run(NodeId root) {
    // 全名（R13 §2）：v1 根类是顶层类，全名 = 类名。
    result_.model.name = {I_.nodes[root].name};
    const SElement& el = I_.scode.elems[I_.nodes[root].def];
    result_.model.comment = el.cmt.str;
    if (el.def.kind != SClassDefKind::Parts) set_unsupported("short/derived/enum root class");
    // __OpenModelica_commandLineOptions（-d=evaluateAllParameters / mergeComponents …）会重写整个
    // 展平结果，是 OMC 专属变换，v1 不做 → 标 unsupported（driver 退 66，记 SKIP 不是 FAIL）。
    if (el.cmt.annotation.is_valid())
      for (const SSubMod& s : I_.scode.mods[el.cmt.annotation].subs)
        if (s.ident == in_.intern("__OpenModelica_commandLineOptions"))
          set_unsupported("__OpenModelica_commandLineOptions annotation");
    flatten_class(root, {}, /*protected_ctx=*/false, ModCtx{});
    // 重复变量名 → omc 会去重（R11 §3.4-3）；v1 未做，标 unsupported（driver 退 66）。
    for (std::size_t i = 0; i < result_.model.variables.size(); ++i)
      for (std::size_t j = i + 1; j < result_.model.variables.size(); ++j)
        if (result_.model.variables[i].name == result_.model.variables[j].name) {
          set_unsupported("duplicate variable");
          i = result_.model.variables.size();
          break;
        }
    return std::move(result_);
  }

 private:
  // ---- flatten：带前缀 DFS（R11 §3.4）----
  // protected_ctx：外层组件是 protected 时，其所有子变量也 protected（R13 §3；ComponentAttributes1）。
  void flatten_class(NodeId inst_node, const std::vector<Symbol>& prefix, bool protected_ctx,
                     const ModCtx& ctx) {
    // 算法段 v1 未做（方程段的 Equals 已支持，见文末 collect_equations）。
    const SElement& def = I_.scode.elems[I_.nodes[inst_node].def];
    if (!def.def.algorithms.empty() || !def.def.initial_algorithms.empty())
      set_unsupported("algorithms");
    if (def.def.external.present) set_unsupported("external function");
    for (Id<SElement> eid : def.def.elements) {
      SElementKind k = I_.scode.elems[eid].kind;
      if (k == SElementKind::Import) set_unsupported("import");   // v1 未做 import 解析
    }
    ClassTreeId tree = I_.node_class(inst_node).tree;
    const std::vector<NodeId> comps = I_.trees[tree].components;  // 拷一份（下面会往 arena add）
    // 校验：外来修饰的每个子名都必须命中本类某个组件——否则是嵌套类修饰 `a(B(...))`（ClassMod1）或写错名，
    // 本桶未做 → 标 unsupported（保 FAIL=0）。
    check_modifier_names(comps, ctx);
    for (NodeId cn : comps) {
      const CompId cid = I_.nodes[cn].comp;
      const Symbol cname = I_.nodes[cn].name;
      const Visibility vis = I_.nodes[cn].visibility;
      const SElement& cel = I_.scode.elems[I_.comps[cid].def];
      if (!cel.attr.dims.empty() || !cel.type.dims.empty()) set_unsupported("array");
      if (cel.condition.is_valid()) set_unsupported("conditional component");
      if (I_.comps[cid].attributes.is_inner || I_.comps[cid].attributes.is_outer)
        set_unsupported("inner/outer");   // v1 还没做 inner/outer 链接（R11 §5）
      bool is_prot = protected_ctx || vis == Visibility::Protected;
      std::vector<Symbol> name = prefix;
      name.push_back(cname);

      // 从外来修饰里取命中本组件名的子修饰（重名并集：mod4 的两个 a(...)）；判 final、判不支持形态。
      std::vector<Id<SMod>> matched;
      bool sub_final = false;
      for (Id<SMod> m : ctx.mods)
        for (const SSubMod& s : I_.scode.mods[m].subs)
          if (s.ident == cname) {
            const SMod& sm = I_.scode.mods[s.mod];
            if (sm.kind != SModKind::Mod) set_unsupported("redeclare/break modifier");
            if (sm.is_final) sub_final = true;
            matched.push_back(s.mod);
          }
      const bool comp_final = ctx.final_all || sub_final || I_.comps[cid].attributes.is_final;

      if (I_.comps[cid].ty.kind == TypeKind::Complex) {
        // 复合组件的类型必须是长类（PARTS）；短类/枚举/derived 走到这里说明 v1 还没做。
        NodeId ci = I_.comps[cid].class_inst;
        Id<SElement> cdef = I_.nodes[ci].def;
        if (!cdef.is_valid() || I_.scode.elems[cdef].def.kind != SClassDefKind::Parts) {
          set_unsupported("short/enum/derived type");
          continue;
        }
        // 连接器（含 expandable）要 connect 展开 + 方向/flow 规则（R13 §3/§6）——v1 未做，整个标 unsupported。
        SRestriction r = I_.scode.elems[cdef].restriction;
        if (r == SRestriction::Connector || r == SRestriction::ExpandableConnector)
          set_unsupported("connector type");
        // 整体绑定 `A a = A(...)`（外来修饰给复合组件挂了 binding）v1 未做。
        for (Id<SMod> m : matched)
          if (I_.scode.mods[m].binding.is_valid()) set_unsupported("record/whole binding");
        // 组子层修饰上下文（R15 §3.3 优先级：外层实例修饰 > 组件自身声明修饰）。两者同时非空要合并两套作用域，
        // 本桶未做 → 标 unsupported（easy slice 不出现）。
        const bool own = cel.mod.is_valid() && !I_.scode.mods[cel.mod].subs.empty();
        ModCtx child;
        child.final_all = comp_final;
        if (!matched.empty() && own) {
          set_unsupported("stacked modifier scopes");
        } else if (!matched.empty()) {
          child.mods = matched; child.scope = ctx.scope; child.vprefix = ctx.vprefix;
        } else if (own) {
          child.mods = {cel.mod}; child.scope = inst_node; child.vprefix = prefix;
        }
        flatten_class(ci, name, is_prot, child);
        continue;
      }
      emit_variable(cid, name, is_prot, prefix, inst_node, matched, ctx.scope, ctx.vprefix, comp_final);
    }
    // 本类方程（子组件方程已在上面递归时加入 → 符合 R11 §3.4-6"子组件先、本类后"）。
    // 段序：initial equation 先于 equation（R13 §2）；enclosing = 本实例，cref 前缀 = prefix。
    for (Id<SEquation> eid : def.def.initial_equations) collect_equation(eid, inst_node, prefix, /*initial=*/true);
    for (Id<SEquation> eid : def.def.equations) collect_equation(eid, inst_node, prefix, /*initial=*/false);
  }

  // 翻译一条方程（v1 只做 Equals；其余 kind 标 unsupported）。
  void collect_equation(Id<SEquation> eid, NodeId enclosing, const std::vector<Symbol>& prefix, bool is_initial) {
    const SEquation& eq = I_.scode.eqs[eid];
    if (eq.kind != SEqKind::Equals) {
      set_unsupported("non-equality equation (connect/if/for/when/assert…)");
      return;
    }
    Id<Expr> a = inst_exp(eq.a, enclosing, prefix);
    Id<Expr> b = inst_exp(eq.b, enclosing, prefix);
    // 等式两侧整型↔实型强制（R14）：eq1 的 `x = 2`（x:Real）→ `x = 2.0`。
    TypeKind ta = result_.model.exprs[a].ty.kind, tb = result_.model.exprs[b].ty.kind;
    if (ta == TypeKind::Real && tb == TypeKind::Integer) b = to_real(b);
    else if (ta == TypeKind::Integer && tb == TypeKind::Real) a = to_real(a);
    FlatEquation fe;
    fe.kind = FlatEqKind::Equality;
    fe.a = a;
    fe.b = b;
    fe.loc = eq.loc;
    if (eq.cmt.str.is_valid()) fe.comment = in_.intern(unescape_modelica_string(in_.str(eq.cmt.str)));
    Id<FlatEquation> id = result_.model.add_eq(std::move(fe));
    if (is_initial) result_.model.initial_equations.push_back(id);
    else result_.model.equations.push_back(id);
  }

  // mod_subs：命中本叶组件名的外来子修饰（优先级序），其 .binding 覆盖声明绑定、.subs 覆盖声明属性；
  // mod_scope/mod_prefix：这些修饰值的解析作用域（书写处）。force_final：上层 final 传播。
  void emit_variable(CompId cid, const std::vector<Symbol>& name, bool is_protected,
                     const std::vector<Symbol>& prefix, NodeId enclosing,
                     const std::vector<Id<SMod>>& mod_subs, NodeId mod_scope,
                     const std::vector<Symbol>& mod_prefix, bool force_final) {
    // 先算绑定/属性（会往 model.exprs add），再取组件字段——不跨 add 持组件引用（arena 纪律）。
    const Type cty = I_.comps[cid].ty;   // 拷贝，跨 add 稳定
    // 绑定：外来修饰优先（第一个带绑定的子修饰），否则组件自身声明绑定（R15 §3.4 外层实例修饰 > 声明默认）。
    Id<Expr> binding;
    {
      Id<Exp> mraw;
      for (Id<SMod> m : mod_subs)
        if (I_.scode.mods[m].binding.is_valid()) { mraw = I_.scode.mods[m].binding; break; }
      // 枚举类型的绑定值 `E.one` / `P.E.one` 是枚举字面量（打全路径），不走会 SKIP 多段名的通用 cref 解析。
      if (mraw.is_valid()) {
        binding = (cty.kind == TypeKind::Enumeration && is_cref_exp(mraw)) ? enum_lit_value(mraw)
                                                                           : inst_exp(mraw, mod_scope, mod_prefix);
      } else {
        const Binding& b = I_.comps[cid].binding;
        const BindingKind bk = b.kind;
        const Id<Exp> raw = b.raw_exp;
        if (bk == BindingKind::Raw && raw.is_valid())
          binding = (cty.kind == TypeKind::Enumeration && is_cref_exp(raw)) ? enum_lit_value(raw)
                                                                            : inst_exp(raw, enclosing, prefix);
      }
    }
    if (binding.is_valid() && cty.kind == TypeKind::Real) binding = to_real(binding);   // `= 2` → `= 2.0`
    // 属性来源（优先级序 R15 §3.4）：外来子修饰 > 组件自身声明修饰 > 短类链派生修饰（外层派生在前）。
    std::vector<ModSource> asrc;
    for (Id<SMod> m : mod_subs) asrc.push_back({m, mod_scope, mod_prefix});
    {
      const Id<SMod> dm = I_.scode.elems[I_.comps[cid].def].mod;
      if (dm.is_valid()) asrc.push_back({dm, enclosing, prefix});
    }
    for (Id<SMod> dm : I_.comps[cid].derived_mods) asrc.push_back({dm, enclosing, prefix});
    std::vector<FlatVarAttr> attrs = collect_attributes(cid, asrc);
    const Component& comp = I_.comps[cid];
    // flow/stream 变量需要连接方程（未连接顶层 flow → `x = 0.0`，R13 §6-2）——v1 未做 connect，标 unsupported。
    if (comp.attributes.connector != SConnectorType::Potential) set_unsupported("connector (flow/stream)");
    FlatVariable v;
    v.name = name;
    v.ty = comp.ty;
    v.is_protected = is_protected;
    v.is_final = comp.attributes.is_final || force_final;
    switch (comp.attributes.variability) {
      case SVariability::Var: v.var_kind = FlatVarKind::Var; break;
      case SVariability::Discrete: v.var_kind = FlatVarKind::Discrete; break;
      case SVariability::Param: v.var_kind = FlatVarKind::Parameter; break;
      case SVariability::Const: v.var_kind = FlatVarKind::Constant; break;
    }
    // 顶层 input/output 才打印方向（R13 §3.1；非顶层在 removeNonTopLevelDirections 抹掉，R11 §1 步骤 20）。
    if (prefix.empty()) {
      if (comp.attributes.direction == SDirection::Input) v.direction = FlatDirection::Input;
      else if (comp.attributes.direction == SDirection::Output) v.direction = FlatDirection::Output;
    }
    // 无绑定 parameter 若带 start：omc 把 start 当默认值补成绑定（`x(start=1.0) = 1.0`）并告警（fixed=true 初始化）。
    // 这是参数默认值规则（evalConstants 邻域），本桶只做属性打印 → 标 unsupported（保 FAIL=0）。
    if (v.var_kind == FlatVarKind::Parameter && !binding.is_valid()) {
      const Symbol start = in_.intern("start");
      for (const FlatVarAttr& a : attrs)
        if (a.name == start) { set_unsupported("unbound parameter default from start"); break; }
    }
    // constant 组件的绑定 omc 一定求值成字面量（R14 §3）；绑定若还是表达式，说明要 evalConstants，v1 没做。
    if (v.var_kind == FlatVarKind::Constant && binding.is_valid()) {
      ExprKind bk = result_.model.exprs[binding].kind;
      bool is_lit = bk == ExprKind::IntLit || bk == ExprKind::RealLit || bk == ExprKind::BoolLit ||
                    bk == ExprKind::StringLit || bk == ExprKind::EnumLit;
      if (!is_lit) set_unsupported("constant evaluation");
    }
    v.binding = binding;
    v.attributes = std::move(attrs);
    v.loc = comp.loc;
    result_.model.variables.push_back(std::move(v));
  }

  // 校验外来修饰的每个子名都命中本类某组件——否则是嵌套类修饰 `a(B(...))`（ClassMod1）或写错名 → unsupported。
  void check_modifier_names(const std::vector<NodeId>& comps, const ModCtx& ctx) {
    for (Id<SMod> m : ctx.mods)
      for (const SSubMod& s : I_.scode.mods[m].subs) {
        bool found = false;
        for (NodeId cn : comps)
          if (I_.nodes[cn].kind == NodeKind::Component && I_.nodes[cn].name == s.ident) { found = true; break; }
        if (!found) set_unsupported("modifier names non-component (nested class / typo)");
      }
  }

  // 收集标量叶组件的内建属性（R13 §4.2）。sources 按优先级排（外来修饰在前、组件声明修饰在后），
  // 只看各来源的 **子修饰**（.subs）——顶层 .binding 是叶绑定另行处理。按类型属性表顺序产出，每属性取最高优先来源。
  // 遇到 v1 范围外的修饰形态（嵌套子修饰/final/redeclare/break、或类型无此属性名）→ 标 unsupported。
  std::vector<FlatVarAttr> collect_attributes(CompId cid, const std::vector<ModSource>& sources) {
    std::vector<FlatVarAttr> out;
    const Type ty = I_.comps[cid].ty;
    const std::vector<AttrSpec>& table = attr_table(ty.kind);
    auto droppable = [&](Symbol name) -> bool {
      if (name == in_.intern("unbounded") || name == in_.intern("startOrigin")) return true;
      return ty.kind == TypeKind::String && name == in_.intern("fixed");
    };
    // 校验各来源的属性子（each 在标量属性上无害，如 Each4 的 `each fixed=true` → 允许）。
    for (const ModSource& src : sources)
      for (const SSubMod& s : I_.scode.mods[src.mod].subs) {
        const SMod& sm = I_.scode.mods[s.mod];
        bool simple = sm.kind == SModKind::Mod && !sm.is_final && sm.subs.empty() && sm.binding.is_valid();
        bool named = droppable(s.ident);
        for (const AttrSpec& a : table)
          if (s.ident == in_.intern(a.name)) named = true;
        if (!simple || !named) { set_unsupported("attribute modifier"); return out; }
      }
    // 按表序产出：每个属性取第一个提供它的来源（优先级）。
    for (const AttrSpec& a : table) {
      const Symbol an = in_.intern(a.name);
      for (const ModSource& src : sources) {
        Id<SMod> smid;
        for (const SSubMod& s : I_.scode.mods[src.mod].subs)
          if (s.ident == an) { smid = s.mod; break; }
        if (!smid.is_valid()) continue;
        out.push_back({an, attr_value(a.vk, I_.scode.mods[smid].binding, src.scope, src.vprefix)});
        break;
      }
    }
    return out;
  }

  Id<Expr> attr_value(AttrVK vk, Id<Exp> val, NodeId enclosing, const std::vector<Symbol>& prefix) {
    switch (vk) {
      case AttrVK::StateSel:
      case AttrVK::EnumComp: return enum_lit_value(val);   // `StateSelect.never` / `E.two`
      case AttrVK::Real: return to_real(inst_exp(val, enclosing, prefix));   // `min = -100` → `-100.0`
      case AttrVK::Str:
      case AttrVK::Int:
      case AttrVK::Bool: return inst_exp(val, enclosing, prefix);
    }
    return inst_exp(val, enclosing, prefix);
  }

  // 枚举属性值形如 `T.lit`（StateSelect.never、E.two）：直接按 cref 各段建 EnumLit，dump 打全路径（R13 §4.2）。
  // 不走通用 cref 解析（它会把多段名当跨作用域引用标 unsupported）。
  Id<Expr> enum_lit_value(Id<Exp> val) {
    const Exp& e = I_.absyn.exps[val];
    if (e.kind != ExpKind::CrefExp) { set_unsupported("enum attribute value"); return lit_int(0); }
    const Cref& c = I_.absyn.crefs[e.cref];
    Expr r;
    r.kind = ExprKind::EnumLit;
    r.ty = Type::of(TypeKind::Enumeration);
    for (const CrefPart& p : c.parts) {
      if (!p.subscripts.empty()) { set_unsupported("subscripted enum value"); return lit_int(0); }
      r.enum_path.push_back(p.name);
    }
    return add(std::move(r));
  }

  // ---- instExpressions：Absyn Exp → 类型化 Expr（在 model.exprs）----
  // 解析 cref（对 enclosing 的 FlatTree 组件），本地 cref 前缀改写；标量定型 + 整型→实型字面量折叠。
  Id<Expr> inst_exp(Id<Exp> ae, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const Exp& e = I_.absyn.exps[ae];
    switch (e.kind) {
      case ExpKind::IntLit: return lit_int(e.ival);
      case ExpKind::RealLit: return lit_real(e.rval);
      case ExpKind::BoolLit: return lit_bool(e.bval);
      case ExpKind::StringLit: return lit_string(e.sval);
      case ExpKind::CrefExp: return cref(e.cref, enclosing, prefix);
      case ExpKind::Binary: return binary(e, enclosing, prefix);
      case ExpKind::Unary: return unary(e, enclosing, prefix);
      case ExpKind::IfExp: return if_exp(e, enclosing, prefix);
      case ExpKind::Call: return call(e, enclosing, prefix);
      default:
        set_unsupported("expression kind");
        return lit_int(0);   // 占位，driver 会因 unsupported 退 66
    }
  }

  bool is_cref_exp(Id<Exp> ae) const { return I_.absyn.exps[ae].kind == ExpKind::CrefExp; }

  Id<Expr> lit_int(std::int64_t v) { Expr e; e.kind = ExprKind::IntLit; e.ival = v; e.ty = Type::integer(); return add(std::move(e)); }
  Id<Expr> lit_real(double v) { Expr e; e.kind = ExprKind::RealLit; e.rval = v; e.ty = Type::real(); return add(std::move(e)); }
  Id<Expr> lit_bool(bool v) { Expr e; e.kind = ExprKind::BoolLit; e.bval = v; e.ty = Type::boolean(); return add(std::move(e)); }
  Id<Expr> lit_string(Symbol raw) {
    // Absyn 存源码原文（转义序列原样，S1 决定）；类型化 Expr 存实际值，dump 时再 escape。
    Expr e; e.kind = ExprKind::StringLit; e.ty = Type::string();
    e.sval = in_.intern(unescape_modelica_string(in_.str(raw)));
    return add(std::move(e));
  }

  // cref：解析首段到 enclosing 的本地组件；命中 → 前缀改写 + 取其类型；否则（time 等内建）原样、类型 Real。
  Id<Expr> cref(Id<Cref> ac, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const Cref& c = I_.absyn.crefs[ac];
    Expr e;
    e.kind = ExprKind::Cref;
    NodeId local = c.parts.empty() ? Inst::empty_node() : find_local(enclosing, c.parts[0].name);
    if (local.is_valid() && !I_.nodes[local].is_empty()) {
      // 多段 `a.x`：逐段深入复合组件解析到叶子，取**叶子**的类型/可变性（否则会用复合类型误加 Cast）。
      NodeId leaf = local, cur = local;
      bool ok = true;
      for (std::size_t i = 1; i < c.parts.size(); ++i) {
        NodeId cls = I_.comps[I_.nodes[cur].comp].class_inst;
        NodeId nxt = cls.is_valid() ? find_local(cls, c.parts[i].name) : Inst::empty_node();
        if (!nxt.is_valid() || I_.nodes[nxt].is_empty()) { ok = false; break; }
        cur = nxt;
        leaf = nxt;
      }
      if (!ok) {
        set_unsupported("unresolved sub-component reference");
        e.ty = Type::real();
      } else {
        const Component& lc = I_.comps[I_.nodes[leaf].comp];
        // 引用到 constant 或 final parameter → omc 内联成值（R14 §3）；v1 未做 evalConstants，标 unsupported。
        if (lc.attributes.variability == SVariability::Const ||
            (lc.attributes.variability == SVariability::Param && lc.attributes.is_final))
          set_unsupported("constant evaluation");
        e.ty = lc.ty;
      }
      for (Symbol p : prefix) e.cref.parts.push_back({p, {}});
      e.cref.resolved_node = local.raw;
    } else {
      // 单段未解析（time 等内建）v1 当 Real 保留；多段未解析（跨包常量 A.B.c、package 常量）我们既不解析也不
      // 求值，omc 会把它内联成值——标 unsupported（driver 退 66）。
      if (c.parts.size() > 1) set_unsupported("qualified/cross-scope reference");
      e.ty = Type::real();
    }
    for (const CrefPart& p : c.parts) {
      if (!p.subscripts.empty()) set_unsupported("subscripted cref");
      e.cref.parts.push_back({p.name, {}});
    }
    return add(std::move(e));
  }

  // 在 enclosing 实例的 FlatTree 组件里按名字找（v1：直接线性找，规模小）。
  NodeId find_local(NodeId enclosing, Symbol name) {
    if (I_.nodes[enclosing].kind != NodeKind::Class) return Inst::empty_node();
    ClassTreeId t = I_.node_class(enclosing).tree;
    for (NodeId cn : I_.trees[t].components)
      if (I_.nodes[cn].kind == NodeKind::Component && I_.nodes[cn].name == name) return cn;
    return Inst::empty_node();
  }

  Id<Expr> binary(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    BinOp op;
    bool is_arith = map_binop(e.op, op);
    if (!is_arith) {  // 关系 / 逻辑
      return rel_or_logic(e, enclosing, prefix);
    }
    Id<Expr> a = inst_exp(e.a, enclosing, prefix);
    Id<Expr> b = inst_exp(e.b, enclosing, prefix);
    bool real = result_.model.exprs[a].ty.kind == TypeKind::Real || result_.model.exprs[b].ty.kind == TypeKind::Real;
    if (real) {  // 整型操作数提升到实型：字面量直接折叠，非字面量插 Cast（R13 §4.3 / R14 §1）
      a = to_real(a);
      b = to_real(b);
    }
    Expr r;
    r.kind = ExprKind::Binary;
    r.bin = op;
    r.a = a;
    r.b = b;
    r.ty = real ? Type::real() : Type::integer();
    return add(std::move(r));
  }

  Id<Expr> rel_or_logic(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    Id<Expr> a = inst_exp(e.a, enclosing, prefix);
    Id<Expr> b = inst_exp(e.b, enclosing, prefix);
    RelOp rop;
    LogOp lop;
    if (map_relop(e.op, rop)) {
      bool real = result_.model.exprs[a].ty.kind == TypeKind::Real || result_.model.exprs[b].ty.kind == TypeKind::Real;
      if (real) { a = to_real(a); b = to_real(b); }
      Expr r; r.kind = ExprKind::Relation; r.rel = rop; r.a = a; r.b = b; r.ty = Type::boolean();
      return add(std::move(r));
    }
    if (map_logop(e.op, lop)) {
      // omc 会折叠常量关系/逻辑（`false and false`→`false`，R14 §4）——v1 未做 simplify，标 unsupported。
      set_unsupported("relational/logical simplification");
      Expr r; r.kind = ExprKind::Logical; r.log = lop; r.a = a; r.b = b; r.ty = Type::boolean();
      return add(std::move(r));
    }
    set_unsupported("binary op");
    return lit_int(0);
  }

  Id<Expr> unary(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    if (e.op == Op::UPlus) return inst_exp(e.a, enclosing, prefix);   // +a → a（R13 §4.3）
    Id<Expr> a = inst_exp(e.a, enclosing, prefix);
    if (e.op == Op::Not) {
      Expr r; r.kind = ExprKind::LogicalNot; r.a = a; r.ty = Type::boolean();
      return add(std::move(r));
    }
    // UMinus
    Expr r; r.kind = ExprKind::Unary; r.un = UnOp::Minus; r.a = a; r.ty = result_.model.exprs[a].ty;
    return add(std::move(r));
  }

  Id<Expr> if_exp(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    Id<Expr> c = inst_exp(e.a, enclosing, prefix);
    Id<Expr> t = inst_exp(e.b, enclosing, prefix);
    Id<Expr> f = inst_exp(e.c, enclosing, prefix);
    Expr r; r.kind = ExprKind::IfExp; r.a = c; r.b = t; r.c = f; r.ty = result_.model.exprs[t].ty;
    return add(std::move(r));
  }

  Id<Expr> call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    // v1 未做函数求值/内联：omc 会把内建数学函数在常量实参上折叠（abs/sin/…，R14 §4），用户函数会打印
    // 函数定义块。两者都没做，标 unsupported（driver 退 66）。仍构造 Call 节点占位，避免半途空指针。
    set_unsupported("function call / ceval");
    const Cref& fn = I_.absyn.crefs[e.cref];
    Expr r;
    r.kind = ExprKind::Call;
    for (const CrefPart& p : fn.parts) r.call_name.push_back(p.name);
    for (Id<Exp> arg : e.args) r.args.push_back(inst_exp(arg, enclosing, prefix));
    r.ty = r.args.empty() ? Type::real() : result_.model.exprs[r.args[0]].ty;
    return add(std::move(r));
  }

  // 把表达式提升到 Real：字面量折叠（IntLit→RealLit），否则插 Cast。
  Id<Expr> to_real(Id<Expr> e) {
    const Expr& x = result_.model.exprs[e];
    if (x.ty.kind == TypeKind::Real) return e;
    if (x.kind == ExprKind::IntLit) return lit_real(static_cast<double>(x.ival));
    // 折叠 `-<intlit>`（如属性 `min = -100` → `-100.0`）：omc 前端会把整型常量按目标实型求值成实字面量。
    if (x.kind == ExprKind::Unary && x.un == UnOp::Minus) {
      const Expr& inner = result_.model.exprs[x.a];
      if (inner.kind == ExprKind::IntLit) { double v = -static_cast<double>(inner.ival); return lit_real(v); }
    }
    Expr c; c.kind = ExprKind::Cast; c.a = e; c.ty = Type::real();
    return add(std::move(c));
  }

  static bool map_binop(Op op, BinOp& out) {
    switch (op) {
      case Op::Add: case Op::EWAdd: out = BinOp::Add; return true;
      case Op::Sub: case Op::EWSub: out = BinOp::Sub; return true;
      case Op::Mul: case Op::EWMul: out = BinOp::Mul; return true;
      case Op::Div: case Op::EWDiv: out = BinOp::Div; return true;
      case Op::Pow: case Op::EWPow: out = BinOp::Pow; return true;
      default: return false;
    }
  }
  static bool map_relop(Op op, RelOp& out) {
    switch (op) {
      case Op::Lt: out = RelOp::Lt; return true;
      case Op::Le: out = RelOp::Le; return true;
      case Op::Gt: out = RelOp::Gt; return true;
      case Op::Ge: out = RelOp::Ge; return true;
      case Op::Eq: out = RelOp::Eq; return true;
      case Op::Ne: out = RelOp::Ne; return true;
      default: return false;
    }
  }
  static bool map_logop(Op op, LogOp& out) {
    switch (op) {
      case Op::And: out = LogOp::And; return true;
      case Op::Or: out = LogOp::Or; return true;
      default: return false;
    }
  }

  Id<Expr> add(Expr e) { return result_.model.exprs.add(std::move(e)); }
  void set_unsupported(const char* why) {
    if (!result_.unsupported) { result_.unsupported = true; result_.unsupported_reason = why; }
  }

  Inst& I_;
  Interner& in_;
  FlattenResult result_;
};

}  // namespace

FlattenResult flatten(Inst& inst, Interner& in, NodeId root) { return Flattener(inst, in).run(root); }

}  // namespace ymc
