// 压平实现（S3 第一里程碑）。锚点见 flatten.h 头注与各处 R11/R13/R14 引用。
#include "flat/flatten.h"

#include <cmath>
#include <optional>
#include <cstdlib>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "absyn/absyn.h"
#include "base/strings.h"   // unescape_modelica_string

namespace ymc {

namespace {

// inner 声明在作用域里的记录（R11 §5）：outer 组件按名连到最近外层的 inner，用它的压平全名替换引用。
struct InnerVar {
  Symbol name;
  std::vector<Symbol> flat_name;   // inner 的压平全名（prefix + 名）
};

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

// 标量内建函数白名单。求值语义锚点：OMC NFCeval.mo:2074-2129（分派）、2172-3059（evalBuiltin*）；
// 逐字输出锚点：testsuite/flattening/modelica/scodeinst/{FuncBuiltin*,Ceval*}.mo。
enum class BFArg : std::uint8_t { Keep, Real };
enum class BFRes : std::uint8_t { SameArg0, Real, Integer, Boolean, SameArg1, CommonNum, DivType };
struct BuiltinFn {
  const char* name;
  int arity;
  bool foldable;
  BFArg argdom;
  BFRes res;
};

const BuiltinFn* lookup_builtin(std::string_view name) {
  static constexpr BuiltinFn fns[] = {
      {"abs", 1, true, BFArg::Keep, BFRes::SameArg0},
      {"sign", 1, true, BFArg::Real, BFRes::Integer},
      {"sqrt", 1, true, BFArg::Real, BFRes::Real},
      {"sin", 1, true, BFArg::Real, BFRes::Real},
      {"cos", 1, true, BFArg::Real, BFRes::Real},
      {"tan", 1, true, BFArg::Real, BFRes::Real},
      {"asin", 1, true, BFArg::Real, BFRes::Real},
      {"acos", 1, true, BFArg::Real, BFRes::Real},
      {"atan", 1, true, BFArg::Real, BFRes::Real},
      {"atan2", 2, true, BFArg::Real, BFRes::Real},
      {"sinh", 1, true, BFArg::Real, BFRes::Real},
      {"cosh", 1, true, BFArg::Real, BFRes::Real},
      {"tanh", 1, true, BFArg::Real, BFRes::Real},
      {"exp", 1, true, BFArg::Real, BFRes::Real},
      {"log", 1, true, BFArg::Real, BFRes::Real},
      {"log10", 1, true, BFArg::Real, BFRes::Real},
      {"ceil", 1, true, BFArg::Real, BFRes::Real},
      {"floor", 1, true, BFArg::Real, BFRes::Real},
      {"integer", 1, true, BFArg::Keep, BFRes::Integer},
      {"div", 2, true, BFArg::Keep, BFRes::DivType},
      {"mod", 2, true, BFArg::Keep, BFRes::DivType},
      {"rem", 2, true, BFArg::Keep, BFRes::DivType},
      {"Integer", 1, true, BFArg::Keep, BFRes::Integer},
      {"max", 2, true, BFArg::Keep, BFRes::CommonNum},
      {"min", 2, true, BFArg::Keep, BFRes::CommonNum},
      {"der", 1, false, BFArg::Real, BFRes::Real},
      {"pre", 1, false, BFArg::Keep, BFRes::SameArg0},
      {"initial", 0, false, BFArg::Keep, BFRes::Boolean},
      {"terminal", 0, false, BFArg::Keep, BFRes::Boolean},
      {"edge", 1, false, BFArg::Keep, BFRes::Boolean},
      {"change", 1, false, BFArg::Keep, BFRes::Boolean},
      {"sample", 2, false, BFArg::Real, BFRes::Boolean},
      {"smooth", 2, false, BFArg::Keep, BFRes::SameArg1},
      {"semiLinear", 3, false, BFArg::Real, BFRes::Real},
      {"homotopy", 2, false, BFArg::Keep, BFRes::SameArg0},
  };
  for (const BuiltinFn& fn : fns)
    if (name == fn.name) return &fn;
  return nullptr;
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

  using ScodeScope = std::vector<Id<SElement>>;   // SCODE 作用域链：类 SElement，内→外（前置声明，供 eval_dim 等用）

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
    flatten_class(root, {}, /*protected_ctx=*/false, ModCtx{}, {});
    // 未连接 flow 变量的 `name = 0.0` 方程（排在本类方程之后，R13 §6）。到这里的 flow 都属"无 connect"模型
    // （有 connect() 的模型已在 collect_equation 整体 SKIP）→ 全部未连接。
    for (const FlowVar& fv : flow_vars_) {
      Expr lhs;
      lhs.kind = ExprKind::Cref;
      lhs.ty = fv.ty;
      for (Symbol s : fv.name) lhs.cref.parts.push_back({s, {}});
      FlatEquation fe;
      fe.kind = FlatEqKind::Equality;
      fe.a = add(std::move(lhs));
      fe.b = lit_real(0.0);
      fe.loc = fv.loc;
      result_.model.equations.push_back(result_.model.add_eq(std::move(fe)));
    }
    // 重复变量名 → omc 会去重（R11 §3.4-3）；v1 未做，标 unsupported（driver 退 66）。
    for (std::size_t i = 0; i < result_.model.variables.size(); ++i)
      for (std::size_t j = i + 1; j < result_.model.variables.size(); ++j)
        if (result_.model.variables[i].name == result_.model.variables[j].name &&
            result_.model.variables[i].subscripts == result_.model.variables[j].subscripts) {
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
                     const ModCtx& ctx, const std::vector<InnerVar>& outer_inners) {
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
    // 预扫结构参数（ceval3）：某组件的 `fixed = <参数表达式>` 会把用到的参数（及其依赖）求值成 final parameter。
    // 收集这些参数（按 SElement.raw），emit_variable 据此标 final + 把绑定求值成字面量。
    std::unordered_set<std::uint32_t> final_params;
    {
      const ScodeScope fscope = scode_scope_of(inst_node);
      const Symbol fixed_sym = in_.intern("fixed");
      std::vector<std::int64_t> tmp;
      for (NodeId cn : comps) {
        const Id<SElement> fd = I_.comps[I_.nodes[cn].comp].def;
        const SElement& fel = I_.scode.elems[fd];
        // 数组维度里的参数（`Real x[n]` → n）被求值成结构参数 → final（Dim1）。
        for (Id<Exp> d : fel.attr.dims) { tmp.clear(); eval_dim(d, tmp, fscope, &final_params); }
        for (Id<Exp> d : fel.type.dims) { tmp.clear(); eval_dim(d, tmp, fscope, &final_params); }
        // fixed 属性里的参数（ceval3）。
        const Id<SMod> fm = fel.mod;
        if (!fm.is_valid()) continue;
        for (const SSubMod& s : I_.scode.mods[fm].subs)
          if (s.ident == fixed_sym) {
            const Id<Exp> fv = I_.scode.mods[s.mod].binding;
            if (fv.is_valid() && I_.absyn.exps[fv].kind != ExpKind::BoolLit)
              eval_scode(fv, fscope, 0, /*allow_param=*/true, &final_params);
          }
      }
    }
    // inner 作用域（R11 §5）：外层可见 inner + 本层 inner，供本层及子层的 outer 按名连接。
    std::vector<InnerVar> inners = outer_inners;
    for (NodeId cn : comps)
      if (I_.comps[I_.nodes[cn].comp].attributes.is_inner) {
        std::vector<Symbol> fn = prefix;
        fn.push_back(I_.nodes[cn].name);
        inners.push_back({I_.nodes[cn].name, std::move(fn)});
      }
    for (NodeId cn : comps) {
      const CompId cid = I_.nodes[cn].comp;
      const Symbol cname = I_.nodes[cn].name;
      const Visibility vis = I_.nodes[cn].visibility;
      const SElement& cel = I_.scode.elems[I_.comps[cid].def];
      const bool has_dims = !cel.attr.dims.empty() || !cel.type.dims.empty();
      if (cel.condition.is_valid()) set_unsupported("conditional component");
      // outer 组件：不产出变量，按名连到最近外层 inner，记重定向表供 cref 改写（R11 §5）。找不到 inner → SKIP。
      if (I_.comps[cid].attributes.is_outer) {
        // `inner outer` 组合前缀：既声明本层变量又连外层 inner（InnerOuter3），v1 只做纯 outer → SKIP。
        if (I_.comps[cid].attributes.is_inner) { set_unsupported("inner outer combined"); continue; }
        if (has_dims) { set_unsupported("outer array"); continue; }
        const InnerVar* found = nullptr;
        for (auto it = inners.rbegin(); it != inners.rend(); ++it)
          if (it->name == cname) { found = &*it; break; }
        if (!found) { set_unsupported("outer without matching inner"); continue; }
        outer_redirect_[cn.raw] = found->flat_name;
        continue;
      }
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

      // 数组组件（v1 只做叶数组标量化）：复合数组 SKIP，其余走 emit_array。
      if (has_dims) {
        if (I_.comps[cid].ty.kind == TypeKind::Complex) { set_unsupported("composite array"); continue; }
        emit_array(cid, name, is_prot, prefix, inst_node, matched, ctx.scope, ctx.vprefix, comp_final, cel);
        continue;
      }

      if (I_.comps[cid].ty.kind == TypeKind::Complex) {
        // 复合组件的类型必须是长类（PARTS）；短类/枚举/derived 走到这里说明 v1 还没做。
        NodeId ci = I_.comps[cid].class_inst;
        Id<SElement> cdef = I_.nodes[ci].def;
        if (!cdef.is_valid() || I_.scode.elems[cdef].def.kind != SClassDefKind::Parts) {
          set_unsupported("short/enum/derived type");
          continue;
        }
        // 普通连接器当复合组件摊开成员（c.e/c.f）；未连接 flow 变量的 `=0` 方程在 emit_variable 落。
        // expandable connector 要按连接推断成员，v1 未做 → SKIP。含 connect() 的模型在 collect_equation SKIP。
        SRestriction r = I_.scode.elems[cdef].restriction;
        if (r == SRestriction::ExpandableConnector) set_unsupported("expandable connector");
        // `flow C c`：flow/stream 前缀加在连接器**组件**上 → 前缀下沉到每个标量成员（conn9），v1 未做 → SKIP。
        if (I_.comps[cid].attributes.connector != SConnectorType::Potential)
          set_unsupported("flow/stream on composite component");
        // 整体绑定 `A a = A(...)`（外来修饰或组件自身声明给复合/记录组件挂了 binding，如记录构造 `P.Clock(1)`）
        // 需把字段值分配到子变量，v1 未做 → SKIP（Clock6）。
        for (Id<SMod> m : matched)
          if (I_.scode.mods[m].binding.is_valid()) set_unsupported("record/whole binding");
        if (I_.comps[cid].binding.kind == BindingKind::Raw && I_.comps[cid].binding.raw_exp.is_valid())
          set_unsupported("composite/record binding");
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
        flatten_class(ci, name, is_prot, child, inners);
        continue;
      }
      decl_scope_ = cn;   // 组件声明处的词法作用域（继承成员 = 基类作用域，供限定名在原作用域解析，const16）
      emit_variable(cid, name, is_prot, prefix, inst_node, matched, ctx.scope, ctx.vprefix, comp_final, final_params);
      decl_scope_ = Inst::empty_node();
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
    Id<Expr> a = simplify_expr(inst_exp(eq.a, enclosing, prefix));
    Id<Expr> b = simplify_expr(inst_exp(eq.b, enclosing, prefix));
    // 用户数组方程 `x = {…}` 要逐元素定型/强转（数组 typing），v1 只做数组绑定 → SKIP。
    if (result_.model.exprs[a].kind == ExprKind::Array || result_.model.exprs[b].kind == ExprKind::Array) {
      set_unsupported("array equation");
      return;
    }
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
                     const std::vector<Symbol>& mod_prefix, bool force_final,
                     const std::unordered_set<std::uint32_t>& final_params) {
    // 先算绑定/属性（会往 model.exprs add），再取组件字段——不跨 add 持组件引用（arena 纪律）。
    const Type cty = I_.comps[cid].ty;   // 拷贝，跨 add 稳定
    // 结构 final（ceval3）：本参数被某 fixed 用到 → final parameter + 绑定求值成字面量。
    const bool struct_final = final_params.count(I_.comps[cid].def.raw) > 0;
    // 绑定：外来修饰优先（第一个带绑定的子修饰），否则组件自身声明绑定（R15 §3.4 外层实例修饰 > 声明默认）。
    Id<Expr> binding;
    {
      Id<Exp> mraw;
      for (Id<SMod> m : mod_subs)
        if (I_.scode.mods[m].binding.is_valid()) { mraw = I_.scode.mods[m].binding; break; }
      // 枚举类型的绑定值 `E.one` / `P.E.one` 是枚举字面量（打全路径），不走会 SKIP 多段名的通用 cref 解析。
      if (mraw.is_valid()) {
        binding = (cty.kind == TypeKind::Enumeration && is_cref_exp(mraw)) ? enum_lit_value(mraw, mod_scope)
                                                                           : inst_exp(mraw, mod_scope, mod_prefix);
      } else {
        const Binding& b = I_.comps[cid].binding;
        const BindingKind bk = b.kind;
        const Id<Exp> raw = b.raw_exp;
        if (bk == BindingKind::Raw && raw.is_valid())
          binding = (cty.kind == TypeKind::Enumeration && is_cref_exp(raw)) ? enum_lit_value(raw, enclosing)
                                                                            : inst_exp(raw, enclosing, prefix);
      }
    }
    if (binding.is_valid()) {
      binding = simplify_expr(binding);   // 折叠常量子表达式（`1+2`→`3.0`）+ 已在 cref 内联的常量
      if (cty.kind == TypeKind::Real) binding = to_real(binding);   // `= 2` → `= 2.0`
    }
    // 结构 final 参数：绑定求值成字面量（init_x = p1 or p2 → true）。
    if (struct_final) {
      const Id<Exp> raw = I_.comps[cid].binding.raw_exp;
      if (raw.is_valid()) {
        std::optional<CVal> cv = eval_scode(raw, scode_scope_of(enclosing), 0, /*allow_param=*/true, nullptr);
        if (cv) { binding = literal_of(*cv); if (cty.kind == TypeKind::Real) binding = to_real(binding); }
      }
    }
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
    // initial()/terminal() 是 discrete；绑定到 parameter 会被 OMC 拒绝（FuncBuiltinInitial2/Terminal2）。
    if (binding.is_valid() && comp.attributes.variability == SVariability::Param &&
        result_.model.exprs[binding].kind == ExprKind::Call) {
      const std::vector<Symbol>& call_name = result_.model.exprs[binding].call_name;
      if (call_name.size() == 1) {
        const std::string_view fn = in_.str(call_name[0]);
        if (fn == "initial" || fn == "terminal") set_unsupported("binding variability");
      }
    }
    // stream 变量的连接方程（inStream/actualStream）v1 未做 → SKIP。flow 变量正常打印（flow 前缀不打，R13 §3.1），
    // 未连接的 flow → `name = 0.0`（R13 §6-2）在下方压平后落；含 connect() 的模型已在 collect_equation 整体 SKIP。
    if (comp.attributes.connector == SConnectorType::Stream) set_unsupported("stream connector");
    const bool in_conn = enclosing_is_connector(enclosing);
    // 连接器成员带方向（input/output）的 inside/outside 保留规则（顶层连接器保留、深层抹掉，R13 §3.1）v1 未做 → SKIP（Direction1）。
    if (in_conn && comp.attributes.direction != SDirection::Bidir) set_unsupported("connector direction");
    // `flow` 在连接器里生成 `=0`；非连接器里的 flow 前缀 OMC 告警后当普通变量——ymc 不产告警文本，
    // 与期望的 Warning 行对不上 → SKIP（NonConnectorFlow1）。
    if (comp.attributes.connector == SConnectorType::Flow && !in_conn) set_unsupported("flow outside connector");
    const bool is_flow = comp.attributes.connector == SConnectorType::Flow && in_conn;
    FlatVariable v;
    v.name = name;
    v.ty = comp.ty;
    v.is_protected = is_protected;
    v.is_final = comp.attributes.is_final || force_final || struct_final;
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
    // 未连接 flow → `name = 0.0`；`=0` 方程要排在本类方程之后（R13 §6），故先攒起来，run() 末尾统一追加。
    if (is_flow) flow_vars_.push_back({name, cty, comp.loc});
  }

  // 数组叶组件标量化（R13 §3/§4）：字面量维度 → 行主序逐元素变量行（无绑定/属性）；数组绑定 `= {…}` 变成
  // 一条数组方程 `name = {…}`（omc 默认不把绑定摊到每元素，见 Subscript1）。v1 SKIP：非字面量维度、数组属性修饰、
  // 非数组字面量的数组绑定。
  void emit_array(CompId cid, const std::vector<Symbol>& name, bool is_protected,
                  const std::vector<Symbol>& prefix, NodeId enclosing,
                  const std::vector<Id<SMod>>& mod_subs, NodeId mod_scope,
                  const std::vector<Symbol>& mod_prefix, bool force_final, const SElement& cel) {
    // 维度：组件维在前、类型维在后（R13 §3）。字面量 + 参数/常量/size()/算术求值。
    const ScodeScope dscope = scode_scope_of(enclosing);
    std::vector<std::int64_t> dims;
    for (Id<Exp> d : cel.attr.dims)
      if (!eval_dim(d, dims, dscope)) { set_unsupported("non-literal array dimension"); return; }
    for (Id<Exp> d : cel.type.dims)
      if (!eval_dim(d, dims, dscope)) { set_unsupported("non-literal array dimension"); return; }
    if (dims.empty()) { set_unsupported("array dimension"); return; }
    // 属性修饰（逐元素分配 / SPLIT_PROXY，R15 §4）v1 未做。
    if (cel.mod.is_valid() && !I_.scode.mods[cel.mod].subs.empty()) { set_unsupported("array attribute modifier"); return; }
    for (Id<SMod> m : mod_subs)
      if (!I_.scode.mods[m].subs.empty()) { set_unsupported("array attribute modifier"); return; }
    if (!I_.comps[cid].derived_mods.empty()) { set_unsupported("array derived-type modifier"); return; }
    // 绑定：外来修饰优先，否则声明绑定。要求是数组字面量 `{…}`，否则 SKIP。
    Id<Exp> braw;
    for (Id<SMod> m : mod_subs)
      if (I_.scode.mods[m].binding.is_valid()) { braw = I_.scode.mods[m].binding; break; }
    NodeId bscope = mod_scope;
    std::vector<Symbol> bprefix = mod_prefix;
    if (!braw.is_valid()) {
      const Binding& b = I_.comps[cid].binding;
      if (b.kind == BindingKind::Raw && b.raw_exp.is_valid()) { braw = b.raw_exp; bscope = enclosing; bprefix = prefix; }
    }
    if (braw.is_valid() && I_.absyn.exps[braw].kind != ExpKind::Array) {
      set_unsupported("non-literal array binding"); return;
    }

    const Type cty = I_.comps[cid].ty;
    FlatVarKind vk = FlatVarKind::Var;
    switch (I_.comps[cid].attributes.variability) {
      case SVariability::Var: vk = FlatVarKind::Var; break;
      case SVariability::Discrete: vk = FlatVarKind::Discrete; break;
      case SVariability::Param: vk = FlatVarKind::Parameter; break;
      case SVariability::Const: vk = FlatVarKind::Constant; break;
    }
    // 参数/常量数组的绑定 omc 摊到每元素（`x[1]=1.0`…），连续数组才变成数组方程（`x = {…}`）。v1 只做后者。
    if (braw.is_valid() && vk != FlatVarKind::Var) { set_unsupported("parameter/constant array binding"); return; }
    // 数组字面量绑定的形状/类型要与数组匹配（omc 会做类型检查并报错，如 BindingInvalidType3/5）——ymc 无验证 pass，
    // 只放行形状+叶类型都对上的字面量绑定，否则 SKIP。
    if (braw.is_valid() && !array_lit_matches(braw, dims, 0, cty.kind)) {
      set_unsupported("array binding shape/type mismatch"); return;
    }
    const bool is_final = I_.comps[cid].attributes.is_final || force_final;
    FlatDirection dir = FlatDirection::None;
    if (prefix.empty()) {
      if (I_.comps[cid].attributes.direction == SDirection::Input) dir = FlatDirection::Input;
      else if (I_.comps[cid].attributes.direction == SDirection::Output) dir = FlatDirection::Output;
    }

    // 行主序枚举下标元组（1 基），逐元素产出变量行（无绑定/属性）。
    std::vector<std::int64_t> idx(dims.size(), 1);
    for (bool more = true; more;) {
      FlatVariable v;
      v.name = name;
      v.subscripts = idx;
      v.ty = cty;
      v.is_protected = is_protected;
      v.is_final = is_final;
      v.var_kind = vk;
      v.direction = dir;
      v.loc = I_.comps[cid].loc;
      result_.model.variables.push_back(std::move(v));
      // 递增（最后一维最快）。
      std::size_t k = dims.size();
      while (k > 0) {
        --k;
        if (++idx[k] <= dims[k]) break;
        idx[k] = 1;
        if (k == 0) more = false;
      }
    }

    // 数组绑定 → 数组方程 `name = {…}`（元素按叶类型强转）。
    if (braw.is_valid()) {
      Id<Expr> rhs = coerce_deep(simplify_expr(inst_exp(braw, bscope, bprefix)), cty.kind);
      Expr lhs;
      lhs.kind = ExprKind::Cref;
      lhs.ty = cty;
      for (Symbol s : name) lhs.cref.parts.push_back({s, {}});
      FlatEquation fe;
      fe.kind = FlatEqKind::Equality;
      fe.a = add(std::move(lhs));
      fe.b = rhs;
      fe.loc = I_.comps[cid].loc;
      result_.model.equations.push_back(result_.model.add_eq(std::move(fe)));
    }
  }

  // 数组字面量绑定与维度/元素类型是否匹配（形状 + 叶类型）；否则视为类型错误 → SKIP（ymc 无验证 pass）。
  bool array_lit_matches(Id<Exp> b, const std::vector<std::int64_t>& dims, std::size_t di, TypeKind elem) {
    const Exp& e = I_.absyn.exps[b];
    if (di < dims.size()) {
      if (e.kind != ExpKind::Array || static_cast<std::int64_t>(e.args.size()) != dims[di]) return false;
      for (Id<Exp> a : e.args)
        if (!array_lit_matches(a, dims, di + 1, elem)) return false;
      return true;
    }
    switch (e.kind) {   // 叶：标量字面量，类型与元素类型相容
      case ExpKind::IntLit:
      case ExpKind::Unary: return elem == TypeKind::Real || elem == TypeKind::Integer;
      case ExpKind::RealLit: return elem == TypeKind::Real;
      case ExpKind::BoolLit: return elem == TypeKind::Boolean;
      case ExpKind::StringLit: return elem == TypeKind::String;
      default: return false;   // cref/调用等 v1 数组绑定不做
    }
  }

  // 数组维度求值（v1：只认字面量整数；参数/size/: 等 → false）。
  bool eval_dim(Id<Exp> d, std::vector<std::int64_t>& out, const ScodeScope& scope,
                std::unordered_set<std::uint32_t>* used = nullptr) {
    const Exp& e = I_.absyn.exps[d];
    if (e.kind == ExpKind::IntLit && e.ival >= 0) { out.push_back(e.ival); return true; }
    // 参数/常量/size()/算术维度：结构求值成整数（Dim1/DimSize1）；用到的参数记入 used（→ 标 final）。
    std::optional<CVal> cv = eval_scode(d, scope, 0, /*allow_param=*/true, used);
    if (cv && cv->k == CVal::K::Int && cv->i >= 0) { out.push_back(cv->i); return true; }
    return false;
  }

  // 把（可能嵌套的）数组字面量逐叶元素强转到 Real（`{1,2}` → `{1.0,2.0}`）；非 Real 或非数组原样。
  Id<Expr> coerce_deep(Id<Expr> id, TypeKind elem) {
    if (elem != TypeKind::Real) return id;
    if (result_.model.exprs[id].kind != ExprKind::Array) return to_real(id);
    const std::vector<Id<Expr>> args = result_.model.exprs[id].args;   // 拷贝：下面 add 会失效引用
    std::vector<Id<Expr>> out;
    for (Id<Expr> a : args) out.push_back(coerce_deep(a, elem));
    Expr r;
    r.kind = ExprKind::Array;
    r.ty = Type::real();
    r.args = std::move(out);
    return add(std::move(r));
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
        Id<Expr> v = attr_value(a.vk, I_.scode.mods[smid].binding, src.scope, src.vprefix);
        // `fixed` 必是参数/常量 Boolean，omc 求值成字面量（并把参数标 final）：ceval3 的 `fixed = init_x`→`true`。
        // 折不成字面量时对 fixed 做结构参数求值；仍失败 → SKIP（参数依赖变量等）。
        if (an == in_.intern("fixed") && !is_literal(v)) {
          std::optional<CVal> cv = eval_scode(I_.scode.mods[smid].binding, scode_scope_of(src.scope), 0, true, nullptr);
          if (cv) v = literal_of(*cv);
          else { set_unsupported("parameter evaluation in attribute"); return out; }
        }
        out.push_back({an, v});
        break;
      }
    }
    return out;
  }

  Id<Expr> attr_value(AttrVK vk, Id<Exp> val, NodeId enclosing, const std::vector<Symbol>& prefix) {
    switch (vk) {
      case AttrVK::StateSel:
      case AttrVK::EnumComp: return enum_lit_value(val, enclosing);   // `StateSelect.never` / `E.two`
      case AttrVK::Real: return to_real(simplify_expr(inst_exp(val, enclosing, prefix)));   // `min = -100` → `-100.0`
      case AttrVK::Str:
      case AttrVK::Int:
      case AttrVK::Bool: return simplify_expr(inst_exp(val, enclosing, prefix));
    }
    return inst_exp(val, enclosing, prefix);
  }

  // 枚举属性值形如 `T.lit`（StateSelect.never、E.two）：直接按 cref 各段建 EnumLit，dump 打全路径（R13 §4.2）。
  // 不走通用 cref 解析（它会把多段名当跨作用域引用标 unsupported）。
  Id<Expr> enum_lit_value(Id<Exp> val, NodeId scope) {
    const Exp& e = I_.absyn.exps[val];
    if (e.kind != ExpKind::CrefExp) { set_unsupported("enum attribute value"); return lit_int(0); }
    return make_enum_lit(I_.absyn.crefs[e.cref], scope);
  }

  // 从 cref `E.lit` / `P.E.lit` 建 EnumLit：enum_path 打全路径；若能把倒数第二段当枚举类名解析（同/外层作用域的
  // SCODE 元素里找 enumeration 类），补 1 基序数（关系/Integer() 折叠用）；解析不到序数留 0（仍能正确 dump）。
  Id<Expr> make_enum_lit(const Cref& c, NodeId scope) {
    Expr r;
    r.kind = ExprKind::EnumLit;
    r.ty = Type::of(TypeKind::Enumeration);
    for (const CrefPart& p : c.parts) {
      if (!p.subscripts.empty()) { set_unsupported("subscripted enum value"); return lit_int(0); }
      r.enum_path.push_back(p.name);
    }
    if (c.parts.size() >= 2) {
      const SClassDef* ed = find_enum_def(scope, c.parts[c.parts.size() - 2].name);
      if (ed) {
        const Symbol lit = c.parts.back().name;
        for (std::size_t i = 0; i < ed->enum_items.size(); ++i)
          if (ed->enum_items[i].literal == lit) { r.enum_index = static_cast<std::int64_t>(i) + 1; break; }
      }
    }
    return add(std::move(r));
  }

  // 沿词法 parent_scope 链，在各作用域类的 SCODE 元素里找名为 name 的 enumeration 类，返回其 SClassDef；否则 null。
  // （实例的 FlatTree 只存 components 不存嵌套类，所以直接扫 SCODE 元素，不走实例树。）
  const SClassDef* find_enum_def(NodeId scope, Symbol name) {
    for (NodeId s = scope; s.is_valid() && !I_.nodes[s].is_empty(); s = I_.nodes[s].parent_scope) {
      const Id<SElement> d = I_.nodes[s].def;
      if (!d.is_valid()) continue;
      for (Id<SElement> eid : I_.scode.elems[d].def.elements) {
        const SElement& el = I_.scode.elems[eid];
        if (el.kind == SElementKind::Class && el.name == name &&
            el.def.kind == SClassDefKind::Enumeration)
          return &el.def;
      }
    }
    return nullptr;
  }

  // ---- package 限定常量：SCODE 作用域链解析 + 值语义常量求值 ----
  // package 常量往往没被实例化（没走 inst 树），所以直接在 SCODE 上按名解析 + 递归求值，不经 inst_exp/cref。
  struct CVal {
    enum class K { Int, Real, Bool, Str } k = K::Int;
    std::int64_t i = 0;
    double r = 0.0;
    bool b = false;
    Symbol s;
    static CVal I(std::int64_t v) { CVal c; c.k = K::Int; c.i = v; return c; }
    static CVal R(double v) { CVal c; c.k = K::Real; c.r = v; return c; }
    static CVal B(bool v) { CVal c; c.k = K::Bool; c.b = v; return c; }
    static CVal S(Symbol v) { CVal c; c.k = K::Str; c.s = v; return c; }
    double as_r() const { return k == K::Int ? static_cast<double>(i) : r; }
  };

  // enclosing 实例的词法作用域链 → 类 SElement 列表（内→外，<top>/无 def 跳过）。
  ScodeScope scode_scope_of(NodeId enclosing) {
    ScodeScope s;
    for (NodeId n = enclosing; n.is_valid() && !I_.nodes[n].is_empty(); n = I_.nodes[n].parent_scope) {
      const Id<SElement> d = I_.nodes[n].def;
      if (d.is_valid() && I_.scode.elems[d].kind == SElementKind::Class) s.push_back(d);
    }
    return s;
  }

  // 在作用域链（内→外）+ 顶层 program 里找名为 name 的类或组件元素（内层优先 → 支持遮蔽）。
  Id<SElement> find_scode_element(Symbol name, const ScodeScope& scope) {
    auto scan = [&](const std::vector<Id<SElement>>& elems) -> Id<SElement> {
      for (Id<SElement> e : elems) {
        const SElement& el = I_.scode.elems[e];
        if (el.name == name && (el.kind == SElementKind::Class || el.kind == SElementKind::Component)) return e;
      }
      return Id<SElement>{};
    };
    for (Id<SElement> cls : scope) {
      Id<SElement> f = scan(I_.scode.elems[cls].def.elements);
      if (f.is_valid()) return f;
    }
    return scan(I_.scode.program);   // 顶层
  }

  // 解析限定名 parts 到 constant（allow_param 时也接 parameter）组件；out_scope = 该组件所在类的作用域链。前导点 = 从顶层。
  bool resolve_qual_const(const std::vector<Symbol>& parts, bool leading_dot, const ScodeScope& scope,
                          Id<SElement>& out_comp, ScodeScope& out_scope, bool allow_param = false,
                          bool allow_any = false) {
    if (parts.empty()) return false;
    ScodeScope cur = leading_dot ? ScodeScope{} : scope;
    Id<SElement> el = find_scode_element(parts[0], cur);
    if (!el.is_valid()) return false;
    for (std::size_t idx = 1; idx < parts.size(); ++idx) {
      if (I_.scode.elems[el].kind != SElementKind::Class) return false;   // 中间段必须是类
      cur.insert(cur.begin(), el);   // 进入该类（压到作用域链最内）
      Id<SElement> nxt;
      for (Id<SElement> e : I_.scode.elems[el].def.elements) {
        const SElement& c = I_.scode.elems[e];
        if (c.name == parts[idx] && (c.kind == SElementKind::Class || c.kind == SElementKind::Component)) { nxt = e; break; }
      }
      if (!nxt.is_valid()) return false;
      el = nxt;
    }
    if (I_.scode.elems[el].kind != SElementKind::Component) return false;
    if (!allow_any) {
      const SVariability v = I_.scode.elems[el].attr.var;
      if (v != SVariability::Const && !(allow_param && v == SVariability::Param)) return false;
    }
    out_comp = el;
    out_scope = cur;
    return true;
  }

  // 在 SCODE 作用域链上求值常量表达式（字面量 / 限定常量引用 / 算术 / 逻辑 / 关系）。
  // allow_param：也内联 parameter（结构上下文如 `fixed=`）；used 非空时记下用到的 parameter 组件（→ 标 final）。
  std::optional<CVal> eval_scode(Id<Exp> exp, const ScodeScope& scope, int depth,
                                 bool allow_param = false, std::unordered_set<std::uint32_t>* used = nullptr) {
    if (depth > 100) return std::nullopt;
    const Exp& e = I_.absyn.exps[exp];
    switch (e.kind) {
      case ExpKind::IntLit: return CVal::I(e.ival);
      case ExpKind::RealLit: return CVal::R(e.rval);
      case ExpKind::BoolLit: return CVal::B(e.bval);
      case ExpKind::StringLit: return CVal::S(in_.intern(unescape_modelica_string(in_.str(e.sval))));
      case ExpKind::Tuple:
        return e.args.size() == 1 ? eval_scode(e.args[0], scope, depth + 1, allow_param, used) : std::nullopt;
      case ExpKind::CrefExp: {
        const Cref& c = I_.absyn.crefs[e.cref];
        std::vector<Symbol> names;
        for (const CrefPart& p : c.parts) {
          if (!p.subscripts.empty()) return std::nullopt;
          names.push_back(p.name);
        }
        Id<SElement> comp;
        ScodeScope cscope;
        if (!resolve_qual_const(names, c.leading_dot, scope, comp, cscope, allow_param)) return std::nullopt;
        if (used && I_.scode.elems[comp].attr.var == SVariability::Param) used->insert(comp.raw);
        const Id<SMod> m = I_.scode.elems[comp].mod;
        if (!m.is_valid() || !I_.scode.mods[m].binding.is_valid()) return std::nullopt;
        auto v = eval_scode(I_.scode.mods[m].binding, cscope, depth + 1, allow_param, used);
        if (v) v = coerce_cval(*v, I_.scode.elems[comp].type.path);   // 按声明类型强转（Real n=3 → 3.0）
        return v;
      }
      case ExpKind::Unary: {
        auto v = eval_scode(e.a, scope, depth + 1, allow_param, used);
        if (!v) return std::nullopt;
        if (e.op == Op::UPlus) return v;
        if (e.op == Op::UMinus) {
          if (v->k == CVal::K::Int) return CVal::I(-v->i);
          if (v->k == CVal::K::Real) return CVal::R(-v->r);
        }
        if (e.op == Op::Not && v->k == CVal::K::Bool) return CVal::B(!v->b);
        return std::nullopt;
      }
      case ExpKind::Binary: {
        auto a = eval_scode(e.a, scope, depth + 1, allow_param, used);
        auto b = eval_scode(e.b, scope, depth + 1, allow_param, used);
        if (!a || !b) return std::nullopt;
        BinOp aop;
        if (map_binop(e.op, aop)) {   // 算术
          const bool both_int = a->k == CVal::K::Int && b->k == CVal::K::Int && aop != BinOp::Div && aop != BinOp::Pow;
          if (both_int) switch (aop) {
            case BinOp::Add: return CVal::I(a->i + b->i);
            case BinOp::Sub: return CVal::I(a->i - b->i);
            case BinOp::Mul: return CVal::I(a->i * b->i);
            default: break;
          }
          const double x = a->as_r(), y = b->as_r();
          switch (aop) {
            case BinOp::Add: return CVal::R(x + y);
            case BinOp::Sub: return CVal::R(x - y);
            case BinOp::Mul: return CVal::R(x * y);
            case BinOp::Div: return y != 0.0 ? std::optional<CVal>(CVal::R(x / y)) : std::nullopt;
            case BinOp::Pow: return CVal::R(std::pow(x, y));
          }
        }
        LogOp lop;
        if (map_logop(e.op, lop) && a->k == CVal::K::Bool && b->k == CVal::K::Bool)   // 逻辑
          return CVal::B(lop == LogOp::And ? (a->b && b->b) : (a->b || b->b));
        RelOp rop;
        if (map_relop(e.op, rop) && a->k != CVal::K::Str && b->k != CVal::K::Str) {   // 关系（数值/布尔）
          const double x = a->k == CVal::K::Bool ? (a->b ? 1 : 0) : a->as_r();
          const double y = b->k == CVal::K::Bool ? (b->b ? 1 : 0) : b->as_r();
          switch (rop) {
            case RelOp::Lt: return CVal::B(x < y);
            case RelOp::Le: return CVal::B(x <= y);
            case RelOp::Gt: return CVal::B(x > y);
            case RelOp::Ge: return CVal::B(x >= y);
            case RelOp::Eq: return CVal::B(x == y);
            case RelOp::Ne: return CVal::B(x != y);
          }
        }
        return std::nullopt;
      }
      case ExpKind::Call: {
        // size(arr, d)：解析数组组件 arr（任意可变性），求它第 d 维（DimSize1）。
        const Cref& fn = I_.absyn.crefs[e.cref];
        if (fn.parts.size() != 1 || in_.str(fn.parts[0].name) != "size" || e.args.size() != 2) return std::nullopt;
        const Exp& ae = I_.absyn.exps[e.args[0]];
        auto dv = eval_scode(e.args[1], scope, depth + 1, allow_param, used);
        if (ae.kind != ExpKind::CrefExp || !dv || dv->k != CVal::K::Int) return std::nullopt;
        std::vector<Symbol> anames;
        for (const CrefPart& p : I_.absyn.crefs[ae.cref].parts) {
          if (!p.subscripts.empty()) return std::nullopt;
          anames.push_back(p.name);
        }
        Id<SElement> acomp;
        ScodeScope ascope;
        if (!resolve_qual_const(anames, I_.absyn.crefs[ae.cref].leading_dot, scope, acomp, ascope,
                                /*allow_param=*/true, /*allow_any=*/true))
          return std::nullopt;
        std::vector<Id<Exp>> adims = I_.scode.elems[acomp].attr.dims;
        for (Id<Exp> td : I_.scode.elems[acomp].type.dims) adims.push_back(td);
        const std::int64_t di = dv->i;   // 1 基
        if (di < 1 || static_cast<std::size_t>(di) > adims.size()) return std::nullopt;
        return eval_scode(adims[di - 1], ascope, depth + 1, allow_param, used);
      }
      default:
        return std::nullopt;
    }
  }

  // 按组件声明类型强转常量值：Int→Real 加宽 OK；Real→Integer 是收窄（omc 视为类型错，ceval5）→ nullopt（不产值）。
  std::optional<CVal> coerce_cval(const CVal& v, const std::vector<Symbol>& type_path) {
    if (type_path.size() != 1) return v;   // 派生/限定类型不处理
    const std::string_view tn = in_.str(type_path[0]);
    if (tn == "Real" && v.k == CVal::K::Int) return CVal::R(static_cast<double>(v.i));
    if (tn == "Integer" && v.k == CVal::K::Real) return std::nullopt;   // 收窄 → 类型错
    return v;
  }

  Id<Expr> literal_of(const CVal& v) {
    switch (v.k) {
      case CVal::K::Int: return lit_int(v.i);
      case CVal::K::Real: return lit_real(v.r);
      case CVal::K::Bool: return lit_bool(v.b);
      case CVal::K::Str: { Expr e; e.kind = ExprKind::StringLit; e.sval = v.s; e.ty = Type::string(); return add(std::move(e)); }
    }
    return lit_int(0);
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
      case ExpKind::Tuple:
        // 单元素 Tuple = 源码括号分组（S1 保留括号成 1 元 Tuple）；解开即可，dump 再按优先级表重新加括号。
        if (e.args.size() == 1) return inst_exp(e.args[0], enclosing, prefix);
        set_unsupported("tuple");
        return lit_int(0);
      case ExpKind::Array: {
        Expr r;
        r.kind = ExprKind::Array;
        r.ty = Type::real();   // 元素类型粗略；dump 不看，强转在 emit_array/coerce_deep 里做
        std::vector<Id<Expr>> els;
        for (Id<Exp> a : e.args) els.push_back(inst_exp(a, enclosing, prefix));
        r.args = std::move(els);
        return add(std::move(r));
      }
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
      // 叶子是 outer 组件 → 整条引用换成它连接到的 inner 全名（不加前缀）：`a.x`(x outer) → `c.x`（R11 §5）。
      if (ok) {
        auto it = outer_redirect_.find(leaf.raw);
        if (it != outer_redirect_.end()) {
          e.ty = I_.comps[I_.nodes[leaf].comp].ty;
          for (Symbol s : it->second) e.cref.parts.push_back({s, {}});
          return add(std::move(e));
        }
      }
      if (!ok) {
        set_unsupported("unresolved sub-component reference");
        e.ty = Type::real();
      } else {
        // 引用到 constant → omc 内联成值（R14 §3）。求值成功就直接返回字面量；求不出（非常量绑定 / 未支持算子 /
        // 跨作用域常量）→ SKIP。final parameter omc 1.25.0 **不**内联（保留 `y = x`），当普通 cref 处理。
        // 字段先拷贝（eval 会 arena add）。
        const SVariability var = I_.comps[I_.nodes[leaf].comp].attributes.variability;
        const Type lty = I_.comps[I_.nodes[leaf].comp].ty;
        if (var == SVariability::Const) {
          Id<Expr> v = eval_const_value(leaf, enclosing);
          if (v.is_valid()) return v;
          set_unsupported("constant evaluation");
        }
        e.ty = lty;
        // 数组叶引用：整体引用（数组运算/方程）v1 未做；下标引用需字面量且在界内（ymc 无验证 pass）。
        const SElement& lcel = I_.scode.elems[I_.comps[I_.nodes[leaf].comp].def];
        const bool leaf_array = !lcel.attr.dims.empty() || !lcel.type.dims.empty();
        if (!leaf_array && !c.parts.back().subscripts.empty())
          set_unsupported("subscript on non-array");   // `time[2]`、标量下标 → omc 报错，ymc 无验证 → SKIP
        if (leaf_array) {
          const std::vector<Id<Exp>>& subs = c.parts.back().subscripts;
          if (subs.empty()) {
            set_unsupported("whole-array reference");
          } else {
            std::vector<std::int64_t> ldims;
            bool lit = true;
            const ScodeScope lscope = scode_scope_of(enclosing);
            for (Id<Exp> d : lcel.attr.dims) if (!eval_dim(d, ldims, lscope)) lit = false;
            for (Id<Exp> d : lcel.type.dims) if (!eval_dim(d, ldims, lscope)) lit = false;
            if (!lit || subs.size() != ldims.size()) {
              set_unsupported("array subscript");
            } else {
              for (std::size_t k = 0; k < subs.size(); ++k) {
                const Exp& se = I_.absyn.exps[subs[k]];
                if (se.kind != ExpKind::IntLit || se.ival < 1 || se.ival > ldims[k]) {
                  set_unsupported("array subscript out of range");
                  break;
                }
              }
            }
          }
        }
      }
      for (Symbol p : prefix) e.cref.parts.push_back({p, {}});
      e.cref.resolved_node = local.raw;
    } else {
      // 本层没找到：单段名先试**跨作用域常量**（沿词法 parent_scope 找同名 constant，const10 的 `j = i`）。
      if (c.parts.size() == 1 && c.parts[0].subscripts.empty()) {
        NodeId cscope;
        NodeId cc = lookup_const_upscope(enclosing, c.parts[0].name, cscope);
        if (cc.is_valid() && !I_.nodes[cc].is_empty()) {   // empty_node()=raw0 也 is_valid()，必须查 is_empty
          Id<Expr> v = eval_const_value(cc, cscope);
          if (v.is_valid()) return v;
          set_unsupported("constant evaluation");
        }
      }
      // 多段名先试**枚举字面量** `E.lit`：倒数第二段是作用域里的 enumeration 类、末段是它的字面量 → 建 EnumLit
      //（带序数，供关系/Integer() 折叠；如 `E.one < E.two`）。
      if (c.parts.size() >= 2 && c.parts.back().subscripts.empty()) {
        const SClassDef* ed = find_enum_def(enclosing, c.parts[c.parts.size() - 2].name);
        if (ed)
          for (const SEnumItem& it : ed->enum_items)
            if (it.literal == c.parts.back().name) return make_enum_lit(c, enclosing);
      }
      // 多段名先试 **package 限定常量** `P.A.B.c`：SCODE 作用域链解析到 constant + 值语义求值 → 内联字面量。
      if (c.parts.size() > 1) {
        std::vector<Symbol> names;
        bool ok = true;
        for (const CrefPart& p : c.parts) {
          if (!p.subscripts.empty()) { ok = false; break; }
          names.push_back(p.name);
        }
        // 限定名在**声明处**作用域解析（继承成员 = 基类，const16 的 `P.P.i` 要在 P.M 里解析而非派生类）。
        const NodeId qres = decl_scope_.is_valid() && !I_.nodes[decl_scope_].is_empty() ? decl_scope_ : enclosing;
        Id<SElement> qcomp;
        ScodeScope qscope;
        if (ok && resolve_qual_const(names, c.leading_dot, scode_scope_of(qres), qcomp, qscope)) {
          const Id<SMod> m = I_.scode.elems[qcomp].mod;
          if (m.is_valid() && I_.scode.mods[m].binding.is_valid()) {
            std::optional<CVal> v = eval_scode(I_.scode.mods[m].binding, qscope, 0);
            if (v) return literal_of(*v);
          }
        }
      }
      // 单段未解析（time 等内建）v1 当 Real 保留；多段解析不出（package 常量非字面量、redeclare、跨包 extends…）
      // → omc 会内联成值，标 unsupported（driver 退 66）。
      if (c.parts.size() > 1) set_unsupported("qualified/cross-scope reference");
      for (const CrefPart& p : c.parts)
        if (!p.subscripts.empty()) set_unsupported("subscript on non-array");   // `time[2]` → omc 报错
      e.ty = Type::real();
    }
    for (const CrefPart& p : c.parts) {
      std::vector<Id<Expr>> subs;
      for (Id<Exp> s : p.subscripts) subs.push_back(inst_exp(s, enclosing, prefix));
      e.cref.parts.push_back({p.name, std::move(subs)});
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
      // 常量逻辑由 simplify_expr 折叠（`false and false`→`false`）；非常量 `a and b` 保持符号，dump 照打。
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
    // 分支类型统一（MLS §10.6.4）：一支 Real 一支 Integer → 两支都提到 Real
    //（`if c then der(x) else 0` → `else 0.0`，IfExpression17/InnerOuter3）。
    const TypeKind tk = result_.model.exprs[t].ty.kind, fk = result_.model.exprs[f].ty.kind;
    if ((tk == TypeKind::Real && fk == TypeKind::Integer) ||
        (tk == TypeKind::Integer && fk == TypeKind::Real)) {
      t = to_real(t);
      f = to_real(f);
    }
    Expr r; r.kind = ExprKind::IfExp; r.a = c; r.b = t; r.c = f; r.ty = result_.model.exprs[t].ty;
    return add(std::move(r));
  }

  Id<Expr> call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const Cref& fn = I_.absyn.crefs[e.cref];
    const std::string_view builtin_name = fn.parts.size() == 1 ? in_.str(fn.parts[0].name) : std::string_view{};
    const BuiltinFn* bf = nullptr;
    bool supported = true;
    if (fn.parts.size() != 1) {
      set_unsupported("qualified function call");
      supported = false;
    }
    for (Symbol name : e.arg_names)
      if (name.is_valid()) {
        set_unsupported("named function argument");
        supported = false;
        break;
    }
    if (supported) {
      bf = lookup_builtin(builtin_name);
      if (!bf) {
        const std::string why = "function: " + std::string(builtin_name);
        set_unsupported(why.c_str());
        supported = false;
      } else if (bf->arity >= 0 && static_cast<std::size_t>(bf->arity) != e.args.size()) {
        set_unsupported("function arity");
        supported = false;
      }
    }

    std::vector<Id<Expr>> args;
    args.reserve(e.args.size());
    for (Id<Exp> arg : e.args) {
      // 大写 Integer 只接枚举；先建 EnumLit，避免通用 cref 把 E.two 误记成跨作用域引用。
      Id<Expr> a = supported && builtin_name == "Integer" && is_cref_exp(arg)
                       ? enum_lit_value(arg, enclosing)
                       : inst_exp(arg, enclosing, prefix);
      if (result_.model.exprs[a].kind == ExprKind::Array) {
        set_unsupported("array function argument");
        supported = false;
      }
      args.push_back(a);
    }
    if (supported) {
      for (Id<Expr> a : args) {
        const TypeKind k = result_.model.exprs[a].ty.kind;
        const bool numeric = k == TypeKind::Integer || k == TypeKind::Real;
        if ((bf->argdom == BFArg::Real || (bf->foldable && builtin_name != "Integer")) && !numeric) {
          set_unsupported("non-numeric function argument");
          supported = false;
          break;
        }
      }
    }
    if (supported && builtin_name == "smooth") {
      const TypeKind order_ty = result_.model.exprs[args[0]].ty.kind;
      const TypeKind value_ty = result_.model.exprs[args[1]].ty.kind;
      bool parameter_order = result_.model.exprs[args[0]].kind == ExprKind::IntLit;
      if (!parameter_order && result_.model.exprs[args[0]].kind == ExprKind::Cref) {
        const std::uint32_t raw = result_.model.exprs[args[0]].cref.resolved_node;
        if (raw != Id<INode>::kInvalid) {
          const NodeId node = NodeId::from_index(raw);
          if (I_.nodes.contains(node) && !I_.nodes[node].is_empty() && I_.nodes[node].comp.is_valid()) {
            const SVariability var = I_.comps[I_.nodes[node].comp].attributes.variability;
            parameter_order = var == SVariability::Param || var == SVariability::Const;
          }
        }
      }
      if (order_ty != TypeKind::Integer || value_ty != TypeKind::Real || !parameter_order) {
        set_unsupported("smooth argument type/variability");
        supported = false;
      }
    }
    if (supported && bf->argdom == BFArg::Real)
      for (Id<Expr>& a : args) a = to_real(a);
    // `/` 的结果域是 Real；任务卡的 integer(r1/r2) 与 FuncBuiltinInteger.mo 要求在调用内保留该强转。
    if (supported && builtin_name == "integer" && args.size() == 1 &&
        result_.model.exprs[args[0]].kind == ExprKind::Binary &&
        result_.model.exprs[args[0]].bin == BinOp::Div && result_.model.exprs[args[0]].ty.kind == TypeKind::Integer) {
      const Id<Expr> left0 = result_.model.exprs[args[0]].a;
      const Id<Expr> right0 = result_.model.exprs[args[0]].b;
      Id<Expr> left = to_real(left0);
      Id<Expr> right = to_real(right0);
      Expr div; div.kind = ExprKind::Binary; div.bin = BinOp::Div; div.a = left; div.b = right; div.ty = Type::real();
      args[0] = add(std::move(div));
    }
    // 二元数值内建按公共数值类型调用；Encapsulated2/FuncBuiltinMax/Min 的 Result 会把混合实参提升到 Real。
    if (supported && (bf->res == BFRes::CommonNum || bf->res == BFRes::DivType)) {
      bool real = false;
      for (Id<Expr> a : args) real |= result_.model.exprs[a].ty.kind == TypeKind::Real;
      if (real)
        for (Id<Expr>& a : args) a = to_real(a);
    }

    Expr r;
    r.kind = ExprKind::Call;
    for (const CrefPart& p : fn.parts) r.call_name.push_back(p.name);
    r.args = std::move(args);
    if (!supported) {
      r.ty = Type::real();
    } else {
      switch (bf->res) {
        case BFRes::SameArg0: r.ty = result_.model.exprs[r.args[0]].ty; break;
        case BFRes::Real: r.ty = Type::real(); break;
        case BFRes::Integer: r.ty = Type::integer(); break;
        case BFRes::Boolean: r.ty = Type::boolean(); break;
        case BFRes::SameArg1: r.ty = result_.model.exprs[r.args[1]].ty; break;
        case BFRes::CommonNum:
        case BFRes::DivType: {
          bool real = false;
          for (Id<Expr> a : r.args) real |= result_.model.exprs[a].ty.kind == TypeKind::Real;
          r.ty = real ? Type::real() : Type::integer();
          break;
        }
      }
    }
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

  // ---- evalConstants：常量折叠 + 常量/final 参数内联（R14）----
  // omc 对每个绑定/方程/属性都折叠常量子表达式（`1+2`→`3.0`、`x+(1+2)`→`x+3.0`）。这里在类型化 Expr 上做：
  // 整棵/子树是数值/布尔字面量就算出来，常量 cref 已在 cref() 里内联成字面量。v1 只折 Int/Real 算术、Int/Real 关系、
  // 逻辑、Cast、if（常量条件）及白名单标量内建函数；枚举/布尔/字符串关系仍不折。
  bool is_num_lit(const Expr& e) const { return e.kind == ExprKind::IntLit || e.kind == ExprKind::RealLit; }
  double as_real(Id<Expr> id) const {
    const Expr& e = result_.model.exprs[id];
    return e.kind == ExprKind::IntLit ? static_cast<double>(e.ival) : e.rval;
  }

  Id<Expr> simplify_expr(Id<Expr> id) {
    const ExprKind k = result_.model.exprs[id].kind;
    switch (k) {
      case ExprKind::Binary: {
        const Id<Expr> a0 = result_.model.exprs[id].a, b0 = result_.model.exprs[id].b;
        const BinOp op = result_.model.exprs[id].bin;
        const Type ty = result_.model.exprs[id].ty;
        Id<Expr> a = simplify_expr(a0), b = simplify_expr(b0);
        if (is_num_lit(result_.model.exprs[a]) && is_num_lit(result_.model.exprs[b])) {
          const bool both_int = result_.model.exprs[a].kind == ExprKind::IntLit &&
                                result_.model.exprs[b].kind == ExprKind::IntLit &&
                                op != BinOp::Div && op != BinOp::Pow;
          if (both_int) {
            std::int64_t x = result_.model.exprs[a].ival, y = result_.model.exprs[b].ival;
            switch (op) {
              case BinOp::Add: return lit_int(x + y);
              case BinOp::Sub: return lit_int(x - y);
              case BinOp::Mul: return lit_int(x * y);
              default: break;
            }
          }
          double x = as_real(a), y = as_real(b);
          switch (op) {
            case BinOp::Add: return lit_real(x + y);
            case BinOp::Sub: return lit_real(x - y);
            case BinOp::Mul: return lit_real(x * y);
            case BinOp::Div: if (y != 0.0) return lit_real(x / y); break;
            case BinOp::Pow: return lit_real(std::pow(x, y));
          }
        }
        Expr r; r.kind = ExprKind::Binary; r.bin = op; r.a = a; r.b = b; r.ty = ty;
        return add(std::move(r));
      }
      case ExprKind::Unary: {
        const Id<Expr> a0 = result_.model.exprs[id].a;
        const UnOp op = result_.model.exprs[id].un;
        const Type ty = result_.model.exprs[id].ty;
        Id<Expr> a = simplify_expr(a0);
        const Expr& ea = result_.model.exprs[a];
        if (op == UnOp::Minus) {
          if (ea.kind == ExprKind::IntLit) return lit_int(-ea.ival);
          if (ea.kind == ExprKind::RealLit) return lit_real(-ea.rval);
        }
        Expr r; r.kind = ExprKind::Unary; r.un = op; r.a = a; r.ty = ty;
        return add(std::move(r));
      }
      case ExprKind::Relation: {
        const Id<Expr> a0 = result_.model.exprs[id].a, b0 = result_.model.exprs[id].b;
        const RelOp op = result_.model.exprs[id].rel;
        Id<Expr> a = simplify_expr(a0), b = simplify_expr(b0);
        // 关系折叠：< <= > >= == <>，比较键按类型取（数值→double、布尔→0/1(false<true)、
        // 枚举→序数、字符串→字典序）。omc CevalRelation1。
        auto rel = [op](auto x, auto y) -> bool {
          switch (op) {
            case RelOp::Lt: return x < y;
            case RelOp::Le: return x <= y;
            case RelOp::Gt: return x > y;
            case RelOp::Ge: return x >= y;
            case RelOp::Eq: return x == y;
            case RelOp::Ne: return x != y;
          }
          return false;
        };
        const Expr& ea = result_.model.exprs[a];
        const Expr& eb = result_.model.exprs[b];
        if (is_num_lit(ea) && is_num_lit(eb)) return lit_bool(rel(as_real(a), as_real(b)));
        if (ea.kind == ExprKind::BoolLit && eb.kind == ExprKind::BoolLit)
          return lit_bool(rel(ea.bval ? 1 : 0, eb.bval ? 1 : 0));
        if (ea.kind == ExprKind::EnumLit && eb.kind == ExprKind::EnumLit && ea.enum_index && eb.enum_index)
          return lit_bool(rel(ea.enum_index, eb.enum_index));
        if (ea.kind == ExprKind::StringLit && eb.kind == ExprKind::StringLit)
          return lit_bool(rel(in_.str(ea.sval), in_.str(eb.sval)));
        Expr r; r.kind = ExprKind::Relation; r.rel = op; r.a = a; r.b = b; r.ty = Type::boolean();
        return add(std::move(r));
      }
      case ExprKind::Logical: {
        const Id<Expr> a0 = result_.model.exprs[id].a, b0 = result_.model.exprs[id].b;
        const LogOp op = result_.model.exprs[id].log;
        Id<Expr> a = simplify_expr(a0), b = simplify_expr(b0);
        const Expr& ea = result_.model.exprs[a];
        const Expr& eb = result_.model.exprs[b];
        if (ea.kind == ExprKind::BoolLit && eb.kind == ExprKind::BoolLit)
          return lit_bool(op == LogOp::And ? (ea.bval && eb.bval) : (ea.bval || eb.bval));
        Expr r; r.kind = ExprKind::Logical; r.log = op; r.a = a; r.b = b; r.ty = Type::boolean();
        return add(std::move(r));
      }
      case ExprKind::LogicalNot: {
        const Id<Expr> a0 = result_.model.exprs[id].a;
        Id<Expr> a = simplify_expr(a0);
        const Expr& ea = result_.model.exprs[a];
        if (ea.kind == ExprKind::BoolLit) return lit_bool(!ea.bval);
        Expr r; r.kind = ExprKind::LogicalNot; r.a = a; r.ty = Type::boolean();
        return add(std::move(r));
      }
      case ExprKind::Cast: {
        const Id<Expr> a0 = result_.model.exprs[id].a;
        Id<Expr> a = simplify_expr(a0);
        const Expr& ea = result_.model.exprs[a];
        if (ea.kind == ExprKind::IntLit) return lit_real(static_cast<double>(ea.ival));
        if (ea.kind == ExprKind::RealLit) return a;
        Expr r; r.kind = ExprKind::Cast; r.a = a; r.ty = Type::real();
        return add(std::move(r));
      }
      case ExprKind::IfExp: {
        const Id<Expr> c0 = result_.model.exprs[id].a, t0 = result_.model.exprs[id].b,
                       e0 = result_.model.exprs[id].c;
        const Type ty = result_.model.exprs[id].ty;
        Id<Expr> cc = simplify_expr(c0);
        const Expr& ec = result_.model.exprs[cc];
        if (ec.kind == ExprKind::BoolLit) return simplify_expr(ec.bval ? t0 : e0);
        Id<Expr> tt = simplify_expr(t0), ee = simplify_expr(e0);
        Expr r; r.kind = ExprKind::IfExp; r.a = cc; r.b = tt; r.c = ee; r.ty = ty;
        return add(std::move(r));
      }
      case ExprKind::Call: {
        const std::vector<Symbol> call_name = result_.model.exprs[id].call_name;
        const std::vector<Id<Expr>> args0 = result_.model.exprs[id].args;
        const Type ty = result_.model.exprs[id].ty;
        std::vector<Id<Expr>> args;
        args.reserve(args0.size());
        for (Id<Expr> a : args0) args.push_back(simplify_expr(a));

        auto rebuild = [&]() {
          Expr r; r.kind = ExprKind::Call; r.call_name = call_name; r.args = args; r.ty = ty;
          return add(std::move(r));
        };
        if (call_name.size() != 1) return rebuild();
        const std::string_view name = in_.str(call_name[0]);
        const BuiltinFn* bf = lookup_builtin(name);
        if (!bf) return rebuild();
        if (bf->arity >= 0 && static_cast<std::size_t>(bf->arity) != args.size()) return rebuild();
        // ParameterDer.mo：常量/参数的导数为 0；普通变量仍保持 der(x) 符号形式。
        if (name == "der" && args.size() == 1) {
          bool zero = is_num_lit(result_.model.exprs[args[0]]);
          if (!zero && result_.model.exprs[args[0]].kind == ExprKind::Cref) {
            const std::uint32_t raw = result_.model.exprs[args[0]].cref.resolved_node;
            if (raw != Id<INode>::kInvalid) {
              const NodeId node = NodeId::from_index(raw);
              if (I_.nodes.contains(node) && !I_.nodes[node].is_empty() && I_.nodes[node].comp.is_valid()) {
                const SVariability var = I_.comps[I_.nodes[node].comp].attributes.variability;
                zero = var == SVariability::Param || var == SVariability::Const;
              }
            }
          }
          if (zero) return lit_real(0.0);
        }
        if (!bf->foldable) return rebuild();
        if (name == "Integer") {
          const Expr& ea = result_.model.exprs[args[0]];
          if (ea.kind == ExprKind::EnumLit && ea.enum_index) return lit_int(ea.enum_index);
          set_unsupported("enum ordinal");   // 序数没解析出来（跨包枚举等）→ SKIP
          return rebuild();
        }
        for (Id<Expr> a : args)
          if (!is_num_lit(result_.model.exprs[a])) return rebuild();

        const double x = as_real(args[0]);
        if (name == "abs") {
          if (result_.model.exprs[args[0]].kind == ExprKind::IntLit)
            return lit_int(std::llabs(result_.model.exprs[args[0]].ival));
          return lit_real(std::fabs(x));
        }
        if (name == "sign") return lit_int(x > 0.0 ? 1 : x < 0.0 ? -1 : 0);
        if (name == "sqrt") {
          if (x < 0.0) { set_unsupported("constant function domain"); return rebuild(); }
          return lit_real(std::sqrt(x));
        }
        if (name == "sin") return lit_real(std::sin(x));
        if (name == "cos") return lit_real(std::cos(x));
        if (name == "tan") return lit_real(std::tan(x));
        if (name == "asin" || name == "acos") {
          if (x < -1.0 || x > 1.0) { set_unsupported("constant function domain"); return rebuild(); }
          return lit_real(name == "asin" ? std::asin(x) : std::acos(x));
        }
        if (name == "atan") return lit_real(std::atan(x));
        if (name == "sinh") return lit_real(std::sinh(x));
        if (name == "cosh") return lit_real(std::cosh(x));
        if (name == "tanh") return lit_real(std::tanh(x));
        if (name == "exp") return lit_real(std::exp(x));
        if (name == "log" || name == "log10") {
          if (x <= 0.0) { set_unsupported("constant function domain"); return rebuild(); }
          return lit_real(name == "log" ? std::log(x) : std::log10(x));
        }
        if (name == "ceil") return lit_real(std::ceil(x));
        if (name == "floor") return lit_real(std::floor(x));
        if (name == "integer") return lit_int(static_cast<std::int64_t>(std::floor(x)));

        const double y = as_real(args[1]);
        if (name == "atan2") return lit_real(std::atan2(x, y));   // NFCeval.mo:2248-2252：atan2(y, x)
        if ((name == "div" || name == "mod" || name == "rem") && y == 0.0) {
          set_unsupported("division by zero in constant");
          return rebuild();
        }
        const bool real = ty.kind == TypeKind::Real;
        if (name == "div") {
          if (real) return lit_real(std::trunc(x / y));
          return lit_int(result_.model.exprs[args[0]].ival / result_.model.exprs[args[1]].ival);
        }
        if (name == "mod") {
          if (real) return lit_real(x - std::floor(x / y) * y);
          const std::int64_t ix = result_.model.exprs[args[0]].ival;
          const std::int64_t iy = result_.model.exprs[args[1]].ival;
          std::int64_t r = ix % iy;
          if (r != 0 && ((r < 0) != (iy < 0))) r += iy;
          return lit_int(r);
        }
        if (name == "rem") {
          if (real) return lit_real(x - std::trunc(x / y) * y);
          return lit_int(result_.model.exprs[args[0]].ival % result_.model.exprs[args[1]].ival);
        }
        if (name == "max" || name == "min") {
          const bool take_x = name == "max" ? x >= y : x <= y;
          if (real) return lit_real(take_x ? x : y);
          return lit_int(take_x ? result_.model.exprs[args[0]].ival : result_.model.exprs[args[1]].ival);
        }
        return rebuild();
      }
      default:
        return id;   // 叶（cref/time/…）、Array 等原样
    }
  }

  bool is_literal(Id<Expr> id) const {
    ExprKind k = result_.model.exprs[id].kind;
    return k == ExprKind::IntLit || k == ExprKind::RealLit || k == ExprKind::BoolLit ||
           k == ExprKind::StringLit || k == ExprKind::EnumLit;
  }

  // 求一个 constant 组件的值：取它的声明绑定 + 在它的声明作用域里实例化（同/跨作用域常量都会递归内联）+ 折叠。
  // decl_scope = 该常量所在的类（跨作用域时是找到它的那层 parent_scope；同作用域时 = 引用处 enclosing）。
  // 绑定优先取实例化后的 comps.binding（含修饰覆盖）；def-only 组件（未完整实例化，跨作用域常量）从 SCODE 现取。
  // 成功返回字面量 Id，失败（无绑定 / 折不成字面量 / 循环）返回 invalid。
  Id<Expr> eval_const_value(NodeId leaf, NodeId decl_scope) {
    const std::uint32_t key = leaf.raw;
    if (evaluating_.count(key)) return {};   // 循环常量（`x=y; y=x`）
    const CompId cid = I_.nodes[leaf].comp;
    Id<Exp> raw;
    NodeId scope;
    const Binding& bnd = I_.comps[cid].binding;
    if (bnd.kind == BindingKind::Raw && bnd.raw_exp.is_valid()) {
      raw = bnd.raw_exp;
      scope = bnd.scope;
    } else {   // def-only：从 SCODE 取声明绑定，在声明作用域求值
      const Id<SMod> m = I_.scode.elems[I_.comps[cid].def].mod;
      if (m.is_valid()) raw = I_.scode.mods[m].binding;
      scope = decl_scope;
    }
    if (!raw.is_valid()) return {};
    evaluating_.insert(key);
    Id<Expr> e = simplify_expr(inst_exp(raw, scope, {}));   // 常量值与前缀无关
    evaluating_.erase(key);
    return is_literal(e) ? e : Id<Expr>{};
  }

  // 跨作用域常量查找：沿词法 parent_scope 链找同名 constant 组件（从 SCODE 读可变性，def 组件 attributes 未填）。
  // 命中返回组件节点并回填它所在作用域；否则返回 invalid。
  NodeId lookup_const_upscope(NodeId scope, Symbol name, NodeId& found_scope) {
    for (NodeId cur = scope; cur.is_valid() && !I_.nodes[cur].is_empty(); cur = I_.nodes[cur].parent_scope) {
      // 防守：只碰有效 cls + 有效 tree 的 Class 节点（parent_scope 链可能有 <top>/未建树的节点，node_class 会断言）。
      if (I_.nodes[cur].kind != NodeKind::Class || !I_.nodes[cur].cls.is_valid()) continue;
      if (!I_.node_class(cur).tree.is_valid()) continue;
      NodeId comp = find_local(cur, name);   // 未命中返回 empty_node()=raw0（is_valid 仍 true，须查 is_empty）
      if (I_.nodes[comp].is_empty() || !I_.nodes[comp].comp.is_valid()) continue;
      const Id<SElement> def = I_.comps[I_.nodes[comp].comp].def;
      if (def.is_valid() && I_.scode.elems[def].attr.var == SVariability::Const) {
        found_scope = cur;
        return comp;
      }
    }
    return Inst::empty_node();
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

  // 某实例节点的类是否连接器（flow 语义/方向保留只在连接器内成立）。
  bool enclosing_is_connector(NodeId enclosing) {
    if (I_.nodes[enclosing].is_empty()) return false;
    Id<SElement> d = I_.nodes[enclosing].def;
    if (!d.is_valid()) return false;
    SRestriction r = I_.scode.elems[d].restriction;
    return r == SRestriction::Connector || r == SRestriction::ExpandableConnector;
  }

  Inst& I_;
  Interner& in_;
  FlattenResult result_;
  std::unordered_map<std::uint32_t, std::vector<Symbol>> outer_redirect_;   // outer 组件节点 raw → inner 全名
  std::unordered_set<std::uint32_t> evaluating_;   // 正在求值的常量节点（环检测）
  NodeId decl_scope_;   // 当前正在算绑定的组件声明处作用域（限定名在此解析；继承成员 = 基类作用域）
  struct FlowVar { std::vector<Symbol> name; Type ty; SourceLoc loc; };
  std::vector<FlowVar> flow_vars_;   // 未连接 flow 变量（run() 末尾统一补 `=0` 方程）
};

}  // namespace

FlattenResult flatten(Inst& inst, Interner& in, NodeId root) { return Flattener(inst, in).run(root); }

}  // namespace ymc
