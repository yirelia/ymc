// 压平实现（S3 第一里程碑）。锚点见 flatten.h 头注与各处 R11/R13/R14 引用。
#include "flat/flatten.h"

#include <cmath>
#include <optional>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
  using PSubs = std::vector<std::vector<std::int64_t>>;   // 与 prefix 平行的逐段下标（复合数组 `a[1].x`；空段=无下标）

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
    flatten_class(root, {}, {}, /*protected_ctx=*/false, ModCtx{}, {});
    // connect 连接集展开（potential 相等 + flow 求和），填 connected_inside_（已连接的 inside 连接器）。
    process_connects();
    // flow 边界/未连接方程（排在连接集方程之后，R13 §6）：outside（顶层）连接器的 flow 恒 =0；
    // inside 连接器仅在**未连接**时 =0（已连接 inside 的 flow 已进求和 → 不再单列）。
    for (const FlowVar& fv : flow_vars_) {
      const bool outside = fv.name.size() == 2;   // 顶层连接器：路径 = {连接器, 成员}
      if (!outside) {
        std::vector<Symbol> cpath(fv.name.begin(), fv.name.end() - 1);
        if (connected_inside_.count(path_key(cpath))) continue;   // 已连接 inside → 不单列 =0
      }
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
    // 某变量名同时出现在 removed_（另一同名声明条件为假被移除）：redeclare 一个 replaceable-conditional 成员的
    // 语义（保留原条件 / 禁止带条件 redeclare）v1 未做 → SKIP（RedeclareElementCondition1/2/3）。
    if (!removed_.empty())
      for (const FlatVariable& v : result_.model.variables)
        if (removed_.count(path_key(v.name))) { set_unsupported("conditional component redeclared"); break; }
    // 重复变量名 → omc 会去重（R11 §3.4-3）；v1 未做，标 unsupported（driver 退 66）。
    for (std::size_t i = 0; i < result_.model.variables.size(); ++i)
      for (std::size_t j = i + 1; j < result_.model.variables.size(); ++j)
        if (result_.model.variables[i].name == result_.model.variables[j].name &&
            result_.model.variables[i].name_subs == result_.model.variables[j].name_subs &&
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
  void flatten_class(NodeId inst_node, const std::vector<Symbol>& prefix, const PSubs& psubs,
                     bool protected_ctx, const ModCtx& ctx, const std::vector<InnerVar>& outer_inners) {
    // 已判 unsupported 就早停：模型必 SKIP，继续摊分（尤其大复合数组）只是白烧时间/内存（ArrayConnect3 cells[1000,100]）。
    if (result_.unsupported) return;
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
        // 组件条件里的参数（`x if b` → b 是结构参数 → final，Condition3）；在组件声明处作用域求值（继承=基类）。
        if (fel.condition.is_valid()) {
          const NodeId ps = I_.nodes[cn].parent_scope;
          const NodeId cs = (ps.is_valid() && !I_.nodes[ps].is_empty()) ? ps : inst_node;
          eval_scode(fel.condition, scode_scope_of(cs), 0, /*allow_param=*/true, &final_params);
        }
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
      // 条件组件 `x if cond`：cond 须求值成常量 Bool（MLS §4.4.5）。false → 移除组件（不产变量/不递归）、
      // 记路径供 connect 丢弃；true → 正常展开（条件抹掉）。求不出常量 Bool → SKIP。
      if (cel.condition.is_valid()) {
        // 在组件**声明处**作用域求值（继承成员 = 基类，条件里的兄弟参数在原类可见，Condition3）。
        const NodeId ps = I_.nodes[cn].parent_scope;
        const NodeId cscope = (ps.is_valid() && !I_.nodes[ps].is_empty()) ? ps : inst_node;
        std::optional<CVal> cv = eval_scode(cel.condition, scode_scope_of(cscope), 0, /*allow_param=*/true, nullptr);
        if (!cv || cv->k != CVal::K::Bool) { set_unsupported("conditional component condition"); continue; }
        if (!cv->b) {
          std::vector<Symbol> rn = prefix;
          rn.push_back(cname);
          removed_.insert(path_key(rn));
          removed_nodes_.insert(cn.raw);
          continue;
        }
      }
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

      // 数组组件：叶数组走 emit_array；复合数组 `A a[2]` 逐元素以 `a[i]` 前缀递归展开（2a 只做无修饰/无整体绑定）。
      if (has_dims) {
        if (I_.comps[cid].ty.kind == TypeKind::Complex) {
          NodeId ci = I_.comps[cid].class_inst;
          Id<SElement> cdef = I_.nodes[ci].def;
          if (!cdef.is_valid() || I_.scode.elems[cdef].def.kind != SClassDefKind::Parts) {
            set_unsupported("composite array (non-parts type)"); continue;
          }
          SRestriction r = I_.scode.elems[cdef].restriction;
          if (r == SRestriction::Connector || r == SRestriction::ExpandableConnector) {
            set_unsupported("connector array"); continue;   // 数组 connect（另一 sub-bucket）
          }
          if (I_.comps[cid].attributes.variability != SVariability::Var) { set_unsupported("variability on composite array"); continue; }
          if (I_.comps[cid].attributes.connector != SConnectorType::Potential) { set_unsupported("flow/stream on composite array"); continue; }
          if (I_.comps[cid].binding.kind == BindingKind::Raw && I_.comps[cid].binding.raw_exp.is_valid()) {
            set_unsupported("composite array whole binding"); continue;
          }
          // 数组修饰分发（phase 2b）：只处理组件自身声明修饰 cel.mod（`a[3](x={…})`）；外来 matched/派生修饰的数组
          // 分发留后续 → SKIP。
          bool ext_mod = false;
          for (Id<SMod> m : matched)
            if (!I_.scode.mods[m].subs.empty() || I_.scode.mods[m].binding.is_valid()) ext_mod = true;
          if (ext_mod || !I_.comps[cid].derived_mods.empty()) { set_unsupported("composite array external modifier"); continue; }
          const bool own_mod = cel.mod.is_valid() && !I_.scode.mods[cel.mod].subs.empty();
          std::vector<std::int64_t> adims;
          const ScodeScope adscope = scode_scope_of(inst_node);
          bool okdim = true;
          for (Id<Exp> d : cel.attr.dims) if (!eval_dim(d, adims, adscope)) okdim = false;
          for (Id<Exp> d : cel.type.dims) if (!eval_dim(d, adims, adscope)) okdim = false;
          if (!okdim || adims.empty()) { set_unsupported("non-literal array dimension"); continue; }
          // 元素数上限：大复合数组 omc 常不标量化（-nfScalarize），ymc 摊分会爆量 → 超阈值 SKIP。
          std::int64_t total = 1;
          for (std::int64_t d : adims) total *= (d > 0 ? d : 0);
          if (total > 1024) { set_unsupported("composite array too large"); continue; }
          std::vector<std::int64_t> aidx(adims.size(), 1);
          for (bool more = true; more;) {
            PSubs cps = psubs;
            cps.push_back(aidx);   // 该复合数组段 `a` 带下标 [i…]
            ModCtx achild;
            achild.final_all = comp_final;
            if (own_mod) {
              // 该元素的合成修饰：每个子修饰绑定按 aidx 下降取标量（`x={1,2,3}`→`x=vi`；each 保持原值）。
              Id<SMod> em = distribute_mod(cel.mod, aidx);
              if (!em.is_valid()) { set_unsupported("composite array modifier distribution"); break; }
              achild.mods = {em};
              achild.scope = inst_node;
              achild.vprefix = prefix;
            }
            flatten_class(ci, name, cps, is_prot, achild, inners);
            std::size_t k = adims.size();
            while (k > 0) { --k; if (++aidx[k] <= adims[k]) break; aidx[k] = 1; if (k == 0) more = false; }
          }
          continue;
        }
        emit_array(cid, name, is_prot, prefix, psubs, inst_node, matched, ctx.scope, ctx.vprefix, comp_final, cel);
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
        // 记录常量折叠：`constant/parameter R r = f(…)`（函数返回记录）→ 直接发射字段（继承 r 变率 + 折叠值），
        // 绕开下面的"变率复合" SKIP（CevalFuncRecord1/2）。
        if (r == SRestriction::Record) {
          Id<Exp> wb;
          NodeId ws = inst_node;
          for (Id<SMod> m : matched)
            if (I_.scode.mods[m].binding.is_valid()) { wb = I_.scode.mods[m].binding; ws = ctx.scope; break; }
          if (!wb.is_valid() && I_.comps[cid].binding.kind == BindingKind::Raw && I_.comps[cid].binding.raw_exp.is_valid())
            wb = I_.comps[cid].binding.raw_exp;
          if (wb.is_valid() && I_.absyn.exps[wb].kind == ExpKind::Call) {
            std::optional<CVal> rv = eval_user_call(I_.absyn.exps[wb], scode_scope_of(ws), 0);
            if (rv && rv->k == CVal::K::Record) {
              if (emit_record_fields(cid, name, prefix, psubs, ci, *rv, is_prot, comp_final)) continue;
              set_unsupported("record function fold field"); continue;
            }
          }
        }
        // `constant/parameter R r`：变率前缀要下沉到每个成员，且常量记录字段引用 `r.x` 要求值（CevalRecord3/5）——
        // 两者本桶都未做 → SKIP（否则字段丢 constant 前缀、引用不折叠）。
        if (I_.comps[cid].attributes.variability != SVariability::Var)
          set_unsupported("variability on composite component");
        // 整体绑定 `A a = A(...)`（外来修饰或组件自身声明）。记录构造 `R r = R(4,5)` 翻成类修饰 `R r(x=4,y=5)`
        // 走既有修饰机制摊到字段（RecordBinding1）；其余整体绑定（记录构造以外的复合绑定，Clock6）仍 SKIP。
        Id<Exp> whole_raw;
        NodeId whole_scope = inst_node;
        std::vector<Symbol> whole_vpfx = prefix;
        for (Id<SMod> m : matched)
          if (I_.scode.mods[m].binding.is_valid()) {
            whole_raw = I_.scode.mods[m].binding; whole_scope = ctx.scope; whole_vpfx = ctx.vprefix; break;
          }
        if (!whole_raw.is_valid() && I_.comps[cid].binding.kind == BindingKind::Raw &&
            I_.comps[cid].binding.raw_exp.is_valid())
          whole_raw = I_.comps[cid].binding.raw_exp;
        Id<SMod> ctor_mod;
        if (r == SRestriction::Record && whole_raw.is_valid())
          ctor_mod = build_record_ctor_mod(whole_raw, ci, cdef);
        if (whole_raw.is_valid() && !ctor_mod.is_valid()) set_unsupported("composite/record binding");
        // 组子层修饰上下文（R15 §3.3 优先级：外层实例修饰 > 组件自身声明修饰）。两者同时非空要合并两套作用域，
        // 本桶未做 → 标 unsupported（easy slice 不出现）。
        const bool own = cel.mod.is_valid() && !I_.scode.mods[cel.mod].subs.empty();
        bool matched_field = false;
        for (Id<SMod> m : matched)
          if (!I_.scode.mods[m].subs.empty()) { matched_field = true; break; }
        ModCtx child;
        child.final_all = comp_final;
        if (ctor_mod.is_valid()) {
          // 构造绑定与另有字段修饰叠加（`R r(z=9) = R(4,5)`）要合并两套 → 本桶未做 SKIP。
          if (own || matched_field) set_unsupported("record ctor + field modifier");
          else { child.mods = {ctor_mod}; child.scope = whole_scope; child.vprefix = whole_vpfx; }
        } else if (!matched.empty() && own) {
          set_unsupported("stacked modifier scopes");
        } else if (!matched.empty()) {
          child.mods = matched; child.scope = ctx.scope; child.vprefix = ctx.vprefix;
        } else if (own) {
          child.mods = {cel.mod}; child.scope = inst_node; child.vprefix = prefix;
        }
        PSubs cps = psubs;
        cps.push_back({});   // 标量复合段无下标
        flatten_class(ci, name, cps, is_prot, child, inners);
        continue;
      }
      decl_scope_ = cn;   // 组件声明处的词法作用域（继承成员 = 基类作用域，供限定名在原作用域解析，const16）
      emit_variable(cid, name, is_prot, prefix, psubs, inst_node, matched, ctx.scope, ctx.vprefix, comp_final, final_params);
      decl_scope_ = Inst::empty_node();
    }
    // 本类方程（子组件方程已在上面递归时加入 → 符合 R11 §3.4-6"子组件先、本类后"）。
    // 段序：initial equation 先于 equation（R13 §2）；enclosing = 本实例，cref 前缀 = prefix。
    for (Id<SEquation> eid : def.def.initial_equations) collect_equation(eid, inst_node, prefix, psubs, /*initial=*/true);
    for (Id<SEquation> eid : def.def.equations) collect_equation(eid, inst_node, prefix, psubs, /*initial=*/false);
  }

  // 翻译一条方程（v1：Equals + 攒 connect 待 run() 展开；其余 kind 标 unsupported）。
  void collect_equation(Id<SEquation> eid, NodeId enclosing, const std::vector<Symbol>& prefix, const PSubs& psubs,
                        bool is_initial) {
    cur_psubs_ = psubs;   // cref 前缀改写用（本类方程里的引用带 `a[i]` 段下标）
    const SEquation& eq = I_.scode.eqs[eid];
    if (eq.kind == SEqKind::Connect) {
      // connect 攒起来在 run() 末尾按连接集展开（potential 相等 + flow 求和）。initial 段里的 connect 罕见 → SKIP。
      if (is_initial) { set_unsupported("connect in initial equation"); return; }
      connects_.push_back({eq.a, eq.b, enclosing, prefix, eq.loc});
      return;
    }
    if (eq.kind == SEqKind::For) {
      // `for i in RANGE loop … end for`：常量范围 → 逐值展开体方程（loop_vars_ 供 cref 解析迭代变量与下标）。
      if (!eq.a.is_valid() || !eq.index.is_valid()) { set_unsupported("for equation (implicit range)"); return; }
      // 迭代变量遮蔽外层同名迭代变量：omc 会告警，ymc 不产告警文本 → 与期望的 Warning 行对不上 → SKIP（ForEquationShadow1）。
      if (loop_vars_.count(eq.index.id)) { set_unsupported("shadowed for iterator"); return; }
      std::optional<std::vector<std::int64_t>> seq = eval_range(eq.a, scode_scope_of(enclosing), 0);
      if (!seq) { set_unsupported("for equation (non-constant range)"); return; }
      for (std::int64_t v : *seq) {
        auto prev = loop_vars_.find(eq.index.id);
        const bool had = prev != loop_vars_.end();
        const std::int64_t old = had ? prev->second : 0;
        loop_vars_[eq.index.id] = v;
        for (Id<SEquation> beid : eq.body) collect_equation(beid, enclosing, prefix, psubs, is_initial);
        if (had) loop_vars_[eq.index.id] = old; else loop_vars_.erase(eq.index.id);
      }
      return;
    }
    if (eq.kind == SEqKind::Assert) {
      // assert(cond, msg[, level])：默认 level = AssertionLevel.error 省略（R13 §6-6，flat_dump 决定）。
      FlatEquation fe;
      fe.kind = FlatEqKind::Assert;
      fe.a = simplify_expr(inst_exp(eq.a, enclosing, prefix));
      fe.b = simplify_expr(inst_exp(eq.b, enclosing, prefix));
      // 参数类型校验（omc 报错，AssertInvalid1/2/3）：cond=Boolean、msg=String、显式 level=枚举。
      if (result_.model.exprs[fe.a].ty.kind != TypeKind::Boolean) { set_unsupported("assert condition type"); return; }
      if (result_.model.exprs[fe.b].ty.kind != TypeKind::String) { set_unsupported("assert message type"); return; }
      if (eq.c.is_valid() && eq.c.raw != I_.scode.assertion_level_error.raw) {
        fe.c = simplify_expr(inst_exp(eq.c, enclosing, prefix));   // 显式非默认 level
        if (result_.model.exprs[fe.c].ty.kind != TypeKind::Enumeration) { set_unsupported("assert level type"); return; }
      }
      fe.loc = eq.loc;
      Id<FlatEquation> id = result_.model.add_eq(std::move(fe));
      if (is_initial) result_.model.initial_equations.push_back(id);
      else result_.model.equations.push_back(id);
      return;
    }
    if (eq.kind == SEqKind::Terminate) {
      // terminate(msg)：msg 须 String（omc 报错 → SKIP，TerminateInvalid1）。
      FlatEquation fe;
      fe.kind = FlatEqKind::Terminate;
      fe.a = simplify_expr(inst_exp(eq.a, enclosing, prefix));
      if (result_.model.exprs[fe.a].ty.kind != TypeKind::String) { set_unsupported("terminate message type"); return; }
      fe.loc = eq.loc;
      Id<FlatEquation> id = result_.model.add_eq(std::move(fe));
      if (is_initial) result_.model.initial_equations.push_back(id);
      else result_.model.equations.push_back(id);
      return;
    }
    if (eq.kind == SEqKind::When) {
      // when/elsewhen 方程：每分支条件（标量 Boolean）+ 体（标量方程 / reinit）。所有分支须解同一组 LHS 变量
      // （When7/WhenVariableMismatch）。initial 段不许 when（WhenInitial1）。数组条件/体、pre/noEvent 条件、嵌套 when 等 SKIP。
      if (is_initial) { set_unsupported("when in initial equation"); return; }
      FlatEquation fe;
      fe.kind = FlatEqKind::When;
      fe.loc = eq.loc;
      std::vector<std::set<std::string>> solved;
      for (const SEqBranch& br : eq.branches) {
        Id<Expr> cond = simplify_expr(inst_exp(br.cond, enclosing, prefix));
        if (!when_cond_ok(cond)) { set_unsupported("when condition form"); return; }
        FlatEqBranch fb;
        fb.cond = cond;
        std::set<std::string> lhs;
        if (!collect_when_body(br.body, enclosing, prefix, fb.body, lhs)) return;   // 内部已 set_unsupported
        solved.push_back(std::move(lhs));
        fe.branches.push_back(std::move(fb));
      }
      for (std::size_t i = 1; i < solved.size(); ++i)
        if (solved[i] != solved[0]) { set_unsupported("when branch solved-variable mismatch"); return; }
      result_.model.equations.push_back(result_.model.add_eq(std::move(fe)));
      return;
    }
    if (eq.kind != SEqKind::Equals) {
      set_unsupported("non-equality equation (if/reinit…)");
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

  // when 条件白名单（v1 只做标量 Boolean 的可判别形态）：关系（操作数非调用，排除 pre/noEvent）、Boolean 变量、initial()。
  // 数组条件、Clock、not initial()、pre(x)>… 等 → false（调用方 SKIP）。
  bool when_cond_ok(Id<Expr> c) {
    const Expr& e = result_.model.exprs[c];
    if (e.ty.kind != TypeKind::Boolean) return false;
    switch (e.kind) {
      case ExprKind::Relation: {
        auto simple = [&](Id<Expr> x) {
          const ExprKind k = result_.model.exprs[x].kind;
          return k == ExprKind::Cref || k == ExprKind::IntLit || k == ExprKind::RealLit ||
                 k == ExprKind::Binary || k == ExprKind::Unary;
        };
        return simple(e.a) && simple(e.b);
      }
      case ExprKind::Cref: return true;
      case ExprKind::Call:
        return e.call_name.size() == 1 && in_.str(e.call_name[0]) == "initial" && e.args.empty();
      default: return false;
    }
  }

  // 收集 when 分支体（标量方程 / reinit）；填 out + 该分支解的 LHS 变量名集合 solved。任何超范围体项 → set_unsupported+false。
  bool collect_when_body(const std::vector<Id<SEquation>>& body, NodeId enclosing,
                         const std::vector<Symbol>& prefix, std::vector<Id<FlatEquation>>& out,
                         std::set<std::string>& solved) {
    for (Id<SEquation> beid : body) {
      const SEquation& b = I_.scode.eqs[beid];
      if (b.kind == SEqKind::Equals) {
        Id<Expr> a = simplify_expr(inst_exp(b.a, enclosing, prefix));
        Id<Expr> rhs = simplify_expr(inst_exp(b.b, enclosing, prefix));
        if (result_.model.exprs[a].kind != ExprKind::Cref) { set_unsupported("when-eq LHS not cref"); return false; }
        if (result_.model.exprs[a].kind == ExprKind::Array || result_.model.exprs[rhs].kind == ExprKind::Array) {
          set_unsupported("array in when body"); return false;
        }
        const ExprCref& cr = result_.model.exprs[a].cref;
        if (cr.parts.empty() || !cr.parts.back().subscripts.empty()) { set_unsupported("array when-eq"); return false; }
        const TypeKind ta = result_.model.exprs[a].ty.kind, tb = result_.model.exprs[rhs].ty.kind;
        if (ta == TypeKind::Real && tb == TypeKind::Integer) rhs = to_real(rhs);
        else if (ta == TypeKind::Integer && tb == TypeKind::Real) a = to_real(a);
        FlatEquation fe;
        fe.kind = FlatEqKind::Equality;
        fe.a = a;
        fe.b = rhs;
        fe.loc = b.loc;
        out.push_back(result_.model.add_eq(std::move(fe)));
        std::string nm;
        for (const ExprCrefPart& p : result_.model.exprs[fe.a].cref.parts) { if (!nm.empty()) nm += '.'; nm += in_.str(p.name); }
        solved.insert(nm);
      } else if (b.kind == SEqKind::Reinit) {
        Id<Expr> tgt = simplify_expr(inst_exp(b.a, enclosing, prefix));
        Id<Expr> val = simplify_expr(inst_exp(b.b, enclosing, prefix));
        if (result_.model.exprs[tgt].kind != ExprKind::Cref) { set_unsupported("reinit arg0 not cref"); return false; }
        const ExprCref& tc = result_.model.exprs[tgt].cref;
        if (result_.model.exprs[tgt].ty.kind != TypeKind::Real || tc.parts.empty() ||
            !tc.parts.back().subscripts.empty() || result_.model.exprs[val].kind == ExprKind::Array) {
          set_unsupported("reinit non-scalar-Real"); return false;
        }
        // reinit 目标须连续时间变量（Var）。ReinitInvalid1/2/3：discrete/param/const → SKIP。
        bool cont = false;
        const std::uint32_t raw = tc.resolved_node;
        if (raw != Id<INode>::kInvalid) {
          const NodeId node = NodeId::from_index(raw);
          if (I_.nodes.contains(node) && !I_.nodes[node].is_empty() && I_.nodes[node].comp.is_valid())
            cont = I_.comps[I_.nodes[node].comp].attributes.variability == SVariability::Var;
        }
        if (!cont) { set_unsupported("reinit target not continuous"); return false; }
        val = to_real(val);
        FlatEquation fe;
        fe.kind = FlatEqKind::Reinit;
        fe.a = tgt;
        fe.b = val;
        fe.loc = b.loc;
        out.push_back(result_.model.add_eq(std::move(fe)));   // reinit 不计入 solved（When7 靠此判失配）
      } else {
        set_unsupported("unsupported when-body equation");     // 嵌套 when / if / for / connect → SKIP
        return false;
      }
    }
    return true;
  }

  // mod_subs：命中本叶组件名的外来子修饰（优先级序），其 .binding 覆盖声明绑定、.subs 覆盖声明属性；
  // mod_scope/mod_prefix：这些修饰值的解析作用域（书写处）。force_final：上层 final 传播。
  void emit_variable(CompId cid, const std::vector<Symbol>& name, bool is_protected,
                     const std::vector<Symbol>& prefix, const PSubs& psubs, NodeId enclosing,
                     const std::vector<Id<SMod>>& mod_subs, NodeId mod_scope,
                     const std::vector<Symbol>& mod_prefix, bool force_final,
                     const std::unordered_set<std::uint32_t>& final_params) {
    cur_psubs_ = psubs;   // 绑定里 cref 前缀改写用（复合数组元素 `a[i].y = a[i].x`）
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
      // 数值组件绑定成 String/Bool 是类型错误（omc 报错，ConditionInvalidBinding1）——ymc 无绑定 typing → SKIP。
      const TypeKind bk = result_.model.exprs[binding].ty.kind;
      if ((cty.kind == TypeKind::Real || cty.kind == TypeKind::Integer) &&
          (bk == TypeKind::String || bk == TypeKind::Boolean))
        set_unsupported("binding type mismatch");
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
    v.name_subs = psubs;
    v.name_subs.push_back({});   // 叶段（标量）无逐段下标；与 name 平行
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
                  const std::vector<Symbol>& prefix, const PSubs& psubs, NodeId enclosing,
                  const std::vector<Id<SMod>>& mod_subs, NodeId mod_scope,
                  const std::vector<Symbol>& mod_prefix, bool force_final, const SElement& cel) {
    cur_psubs_ = psubs;   // 数组绑定/摊分里 cref 前缀改写用
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
    // 参数/常量数组绑定 omc 摊到每元素（`x[1]=1.0`…），连续（Var）数组才变成数组方程（`x = {…}`，Subscript1）。
    // v1 摊分只做"字面量元素"（下降 Array 到标量）；元素含 cref 数组等 → 逐元素时命中会 SKIP。
    const bool spread = braw.is_valid() && vk != FlatVarKind::Var;
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

    // 行主序枚举下标元组（1 基），逐元素产出变量行；param/const 数组把绑定摊到每元素。
    std::vector<std::int64_t> idx(dims.size(), 1);
    for (bool more = true; more;) {
      FlatVariable v;
      v.name = name;
      v.name_subs = psubs;
      v.name_subs.push_back({});   // 叶数组的下标走 v.subscripts（末段），逐段 name_subs 末段留空
      v.subscripts = idx;
      v.ty = cty;
      v.is_protected = is_protected;
      v.is_final = is_final;
      v.var_kind = vk;
      v.direction = dir;
      v.loc = I_.comps[cid].loc;
      if (spread) {
        // 该下标处的字面量元素（下降 Array 到标量）；命中 cref 数组等 → 本增量 SKIP。
        Id<Exp> el = array_scalar_elem(braw, idx, 0);
        if (!el.is_valid()) { set_unsupported("array binding element spread"); return; }
        Id<Expr> eb = simplify_expr(inst_exp(el, bscope, bprefix));
        if (cty.kind == TypeKind::Real) eb = to_real(eb);
        // constant 元素必须求值成字面量（同标量 constant 规则，R14 §3）。
        if (vk == FlatVarKind::Constant) {
          const ExprKind bk = result_.model.exprs[eb].kind;
          const bool is_lit = bk == ExprKind::IntLit || bk == ExprKind::RealLit || bk == ExprKind::BoolLit ||
                              bk == ExprKind::StringLit || bk == ExprKind::EnumLit;
          if (!is_lit) { set_unsupported("constant evaluation"); return; }
        }
        v.binding = eb;
      }
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

    // Var 数组绑定 → 单条数组方程 `name = {…}`（param/const 已在上面摊成逐元素绑定）。
    if (braw.is_valid() && !spread) {
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

  // 数组字面量绑定在多下标 idx 处的标量元素（Absyn）：逐维下降 Array（1 基）。命中非 Array 但仍有剩余下标
  //（cref 数组元素 `{x, …}` 等）→ 返回 invalid，调用方 SKIP（本增量只做纯字面量下降）。
  Id<Exp> array_scalar_elem(Id<Exp> e, const std::vector<std::int64_t>& idx, std::size_t start) {
    if (start == idx.size()) return e;
    const ExpKind k = I_.absyn.exps[e].kind;
    if (k == ExpKind::Array) {
      const std::int64_t i = idx[start];
      const std::size_t n = I_.absyn.exps[e].args.size();
      if (i < 1 || static_cast<std::size_t>(i) > n) return {};
      const Id<Exp> next = I_.absyn.exps[e].args[i - 1];
      return array_scalar_elem(next, idx, start + 1);
    }
    if (k == ExpKind::CrefExp) {
      // cref 数组元素（`{x, …}` 里的 x）：把剩余下标补到 cref 末段 → `x[i,j]`（要求末段原本无下标）。
      Cref nc = I_.absyn.crefs[I_.absyn.exps[e].cref];   // 拷贝（下面会往 absyn arena add）
      if (nc.parts.empty() || !nc.parts.back().subscripts.empty()) return {};
      std::vector<Id<Exp>> subs;
      for (std::size_t j = start; j < idx.size(); ++j) {
        Exp lit;
        lit.kind = ExpKind::IntLit;
        lit.ival = idx[j];
        subs.push_back(I_.absyn.add_exp(std::move(lit)));
      }
      nc.parts.back().subscripts = std::move(subs);
      Exp ce;
      ce.kind = ExpKind::CrefExp;
      ce.cref = I_.absyn.add_cref(std::move(nc));
      return I_.absyn.add_exp(std::move(ce));
    }
    return {};   // 剩余下标落在标量表达式 → 本增量 SKIP
  }

  // 数组字面量绑定与维度/元素类型是否匹配（形状 + 叶类型）；否则视为类型错误 → SKIP（ymc 无验证 pass）。
  bool array_lit_matches(Id<Exp> b, const std::vector<std::int64_t>& dims, std::size_t di, TypeKind elem) {
    const Exp& e = I_.absyn.exps[b];
    if (di < dims.size()) {
      // cref 数组元素（`{x, …}` 的 x）填一整段维度：静态形状无法只看 Absyn 校验，交给 inst/摊分（BindingArray8）。
      if (e.kind == ExpKind::CrefExp) return true;
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
    enum class K { Int, Real, Bool, Str, Record, Array } k = K::Int;
    std::int64_t i = 0;
    double r = 0.0;
    bool b = false;
    Symbol s;
    // Record：字段名→值（保序）。用 shared_ptr 保持 CVal 拷贝廉价；set_field 写时复制避免别名污染。
    std::shared_ptr<std::vector<std::pair<Symbol, CVal>>> rec;
    std::shared_ptr<std::vector<CVal>> arr;   // Array：元素（可嵌套）
    static CVal I(std::int64_t v) { CVal c; c.k = K::Int; c.i = v; return c; }
    static CVal R(double v) { CVal c; c.k = K::Real; c.r = v; return c; }
    static CVal B(bool v) { CVal c; c.k = K::Bool; c.b = v; return c; }
    static CVal S(Symbol v) { CVal c; c.k = K::Str; c.s = v; return c; }
    static CVal Rec() { CVal c; c.k = K::Record; c.rec = std::make_shared<std::vector<std::pair<Symbol, CVal>>>(); return c; }
    static CVal Arr(std::vector<CVal> v) { CVal c; c.k = K::Array; c.arr = std::make_shared<std::vector<CVal>>(std::move(v)); return c; }
    double as_r() const { return k == K::Int ? static_cast<double>(i) : r; }
    void set_field(Symbol name, CVal v) {
      if (!rec) rec = std::make_shared<std::vector<std::pair<Symbol, CVal>>>();
      else if (rec.use_count() > 1) rec = std::make_shared<std::vector<std::pair<Symbol, CVal>>>(*rec);   // COW
      for (auto& f : *rec) if (f.first == name) { f.second = std::move(v); return; }
      rec->push_back({name, std::move(v)});
    }
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

  // 单段类型名解析到记录类（restriction Record）；否则 invalid。限定记录类型暂不做。
  Id<SElement> resolve_record_class(const std::vector<Symbol>& path, const ScodeScope& scope) {
    if (path.size() != 1) return {};
    Id<SElement> e = find_scode_element(path[0], scope);
    if (e.is_valid() && I_.scode.elems[e].kind == SElementKind::Class &&
        I_.scode.elems[e].restriction == SRestriction::Record)
      return e;
    return {};
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
      case ExpKind::Array: {   // 数组字面量 → CVal::Array（元素逐个求值，可嵌套）
        std::vector<CVal> els;
        els.reserve(e.args.size());
        for (Id<Exp> a : e.args) {
          std::optional<CVal> v = eval_scode(a, scope, depth + 1, allow_param, used);
          if (!v) return std::nullopt;
          els.push_back(std::move(*v));
        }
        return CVal::Arr(std::move(els));
      }
      case ExpKind::CrefExp: {
        const Cref& c = I_.absyn.crefs[e.cref];
        // 用户函数解释期：单名命中局部环境（输入/局部/输出）→ 直接返回其值。
        if (locals_ && c.parts.size() == 1 && c.parts[0].subscripts.empty()) {
          auto it = locals_->find(c.parts[0].name.id);
          if (it != locals_->end()) return it->second;
        }
        // for 方程迭代变量（嵌套 for 的依赖范围 `i+1:i+3`，ForEquation7）。
        if (!loop_vars_.empty() && c.parts.size() == 1 && c.parts[0].subscripts.empty()) {
          auto it = loop_vars_.find(c.parts[0].name.id);
          if (it != loop_vars_.end()) return CVal::I(it->second);
        }
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
        if (fn.parts.size() != 1 || in_.str(fn.parts[0].name) != "size" || e.args.size() != 2) {
          if (auto rv = eval_reduction(e, scope, depth)) return rv;   // sum/product/min/max(常量数组)
          return eval_user_call(e, scope, depth);   // 非 size → 试用户函数常量折叠
        }
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

  // 用户函数常量折叠（v1）：解析到 function 类，绑定实参（位置/命名/默认）到输入，解释函数体（输出绑定 + 直线赋值算法），
  // 返回**单**输出的常量值。任何超范围（多输出、外部函数、数组/记录、非直线控制流、非常量实参）→ nullopt（调用方 SKIP）。
  std::optional<CVal> eval_user_call(const Exp& call, const ScodeScope& scope, int depth) {
    if (depth > 100) return std::nullopt;
    const Cref& fn = I_.absyn.crefs[call.cref];
    if (fn.parts.size() != 1 || !fn.parts[0].subscripts.empty()) return std::nullopt;
    const Id<SElement> fe = find_scode_element(fn.parts[0].name, scope);
    if (!fe.is_valid()) return std::nullopt;
    if (I_.scode.elems[fe].kind != SElementKind::Class ||
        I_.scode.elems[fe].restriction != SRestriction::Function ||
        I_.scode.elems[fe].def.kind != SClassDefKind::Parts || I_.scode.elems[fe].def.external.present)
      return std::nullopt;
    if (I_.scode.elems[fe].purity == Purity::Impure) return std::nullopt;   // 不纯函数不能常量折叠（须发射，ImpureCall1）
    if (I_.scode.elems[fe].cmt.annotation.is_valid()) return std::nullopt;  // 带函数注解（inverse/derivative…）→ 保守 SKIP（FunctionInverseInvalid1/2 告警）
    // 输入/输出（声明序）；非 input/output 的公开变量是错误（须 protected，FunctionNonInputOutputParameter）→ SKIP。
    std::vector<Id<SElement>> inputs, outputs;
    for (Id<SElement> eid : I_.scode.elems[fe].def.elements) {
      const SElement& c = I_.scode.elems[eid];
      if (c.kind != SElementKind::Component) continue;
      if (c.attr.dir == SDirection::Input) inputs.push_back(eid);
      else if (c.attr.dir == SDirection::Output) outputs.push_back(eid);
      else if (c.prefixes.vis != SVisibility::Protected) return std::nullopt;   // 公开非 io 变量 → 无效函数
    }
    if (outputs.size() != 1) return std::nullopt;   // 多输出（元组）/无输出 → SKIP
    // 实参匹配输入（位置 + 命名）。
    std::vector<Id<Exp>> bound(inputs.size());   // 每输入的实参 Exp（invalid = 用默认）
    std::size_t pos = 0;
    for (std::size_t i = 0; i < call.args.size(); ++i) {
      if (i < call.arg_names.size() && call.arg_names[i].is_valid()) {
        std::size_t j = 0;
        for (; j < inputs.size(); ++j)
          if (I_.scode.elems[inputs[j]].name == call.arg_names[i]) break;
        if (j >= inputs.size() || bound[j].is_valid()) return std::nullopt;
        bound[j] = call.args[i];
      } else {
        if (pos >= inputs.size()) return std::nullopt;
        bound[pos++] = call.args[i];
      }
    }
    // 先在**调用者**上下文求所有显式实参（用当前 locals_，支持嵌套 f(g(x))）。
    std::unordered_map<std::uint32_t, CVal> env;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      if (!bound[i].is_valid()) continue;
      std::optional<CVal> v = eval_scode(bound[i], scope, depth + 1, /*allow_param=*/true);
      if (!v) return std::nullopt;
      v = coerce_cval(*v, I_.scode.elems[inputs[i]].type.path);
      if (!v) return std::nullopt;
      env[I_.scode.elems[inputs[i]].name.id] = *v;
    }
    // 切到函数局部环境（含已绑定输入）；函数 scope = 函数类 + 调用者作用域。
    ScodeScope fscope = scope;
    fscope.insert(fscope.begin(), fe);
    std::unordered_set<std::uint32_t> inset;
    for (Id<SElement> ii : inputs) inset.insert(I_.scode.elems[ii].name.id);
    auto* saved = locals_;
    auto* saved_in = input_names_;
    locals_ = &env;
    input_names_ = &inset;
    auto fail = [&]() -> std::optional<CVal> { locals_ = saved; input_names_ = saved_in; return std::nullopt; };
    // 缺省输入（声明序，可引用前面输入）。
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      if (bound[i].is_valid()) continue;
      const Id<SMod> m = I_.scode.elems[inputs[i]].mod;
      if (!m.is_valid() || !I_.scode.mods[m].binding.is_valid()) return fail();   // 无实参无默认
      std::optional<CVal> v = eval_scode(I_.scode.mods[m].binding, fscope, depth + 1, true);
      if (!v) return fail();
      v = coerce_cval(*v, I_.scode.elems[inputs[i]].type.path);
      if (!v) return fail();
      env[I_.scode.elems[inputs[i]].name.id] = *v;
    }
    // 输出 init：记录输出（`output R r(x=x,y=y)`）→ 空 Record + 字段修饰；标量输出（`output Real y = expr`）→ 求绑定。
    const SElement& oel = I_.scode.elems[outputs[0]];
    const Id<SElement> orec = resolve_record_class(oel.type.path, fscope);
    if (orec.is_valid()) {
      CVal rv = CVal::Rec();
      if (oel.mod.is_valid())
        for (const SSubMod& sub : I_.scode.mods[oel.mod].subs) {
          const SMod& sm = I_.scode.mods[sub.mod];
          if (!sm.binding.is_valid() || !sm.subs.empty()) return fail();   // 属性/嵌套修饰 → SKIP
          std::optional<CVal> v = eval_scode(sm.binding, fscope, depth + 1, true);
          if (!v) return fail();
          rv.set_field(sub.ident, *v);
        }
      env[oel.name.id] = std::move(rv);
    } else if (oel.mod.is_valid() && I_.scode.mods[oel.mod].binding.is_valid()) {
      std::optional<CVal> v = eval_scode(I_.scode.mods[oel.mod].binding, fscope, depth + 1, true);
      if (v) env[oel.name.id] = *v;
    }
    // 算法段：直线赋值 + if/for/while/return（exec_body 返回控制信号）。
    for (const SAlgorithm& alg : I_.scode.elems[fe].def.algorithms) {
      const Flow f = exec_body(alg.stmts, fscope, depth + 1);
      if (f == Flow::Fail || f == Flow::Break) return fail();   // 顶层 break（不在循环内）是错误（Break2）→ SKIP
      if (f == Flow::Return) break;   // return 提前结束函数
    }
    // 读单输出。
    auto it = env.find(oel.name.id);
    std::optional<CVal> result;
    if (it != env.end()) result = coerce_cval(it->second, oel.type.path);
    locals_ = saved;
    input_names_ = saved_in;
    return result;
  }

  // 函数体执行的控制信号。
  enum class Flow { Normal, Return, Break, Fail };

  // 顺序执行语句列表，遇 Return/Break/Fail 提前返回该信号。
  Flow exec_body(const std::vector<Id<SStatement>>& body, const ScodeScope& fscope, int depth) {
    if (depth > 100) return Flow::Fail;
    for (Id<SStatement> sid : body) {
      const Flow f = exec_stmt(sid, fscope, depth);
      if (f != Flow::Normal) return f;
    }
    return Flow::Normal;
  }

  // 常量 range `a:b` / `a:step:b` 或整数数组 `{1,2,3}` → 整数序列（供 for 展开）；非常量/非整数/过大 → nullopt。
  std::optional<std::vector<std::int64_t>> eval_range(Id<Exp> rexp, const ScodeScope& fscope, int depth) {
    if (!rexp.is_valid()) return std::nullopt;
    const Exp& e = I_.absyn.exps[rexp];
    if (e.kind == ExpKind::Array) {   // `for i in {1,2,3,…}`
      std::vector<std::int64_t> out;
      for (Id<Exp> el : e.args) {
        std::optional<CVal> cv = eval_scode(el, fscope, depth + 1, true);
        if (!cv || cv->k != CVal::K::Int) return std::nullopt;
        out.push_back(cv->i);
      }
      return out;
    }
    if (e.kind != ExpKind::Range || !e.a.is_valid() || !e.b.is_valid()) return std::nullopt;
    const bool has_step = e.c.is_valid();   // a:step:b（c 为终点）；否则 a:b
    auto sv = eval_scode(e.a, fscope, depth + 1, true);
    auto ev = eval_scode(has_step ? e.c : e.b, fscope, depth + 1, true);
    std::optional<CVal> stv = has_step ? eval_scode(e.b, fscope, depth + 1, true) : std::optional<CVal>(CVal::I(1));
    if (!sv || !ev || !stv || sv->k != CVal::K::Int || ev->k != CVal::K::Int || stv->k != CVal::K::Int) return std::nullopt;
    const std::int64_t a = sv->i, b = ev->i, step = stv->i;
    if (step == 0) return std::nullopt;
    std::vector<std::int64_t> out;
    for (std::int64_t i = a; (step > 0 ? i <= b : i >= b); i += step) {
      if (out.size() > 100000) return std::nullopt;   // omc evalLoopLimit=100000：超限放弃折叠
      out.push_back(i);
    }
    return out;
  }

  // 单语句解释：赋值 / if-elseif-else / for / while / return / break。其余 → Fail（调用方 SKIP）。
  Flow exec_stmt(Id<SStatement> sid, const ScodeScope& fscope, int depth) {
    const SStatement& st = I_.scode.stmts[sid];
    switch (st.kind) {
      case SStmtKind::Assign: {
        const Exp& lhs = I_.absyn.exps[st.a];
        if (lhs.kind != ExpKind::CrefExp) return Flow::Fail;
        const Cref& lc = I_.absyn.crefs[lhs.cref];
        if (lc.parts.empty() || (lc.parts.size() != 1 && lc.parts.size() != 2)) return Flow::Fail;   // 标量或记录字段 `r.x`
        for (const CrefPart& p : lc.parts) if (!p.subscripts.empty()) return Flow::Fail;   // 数组赋值未做
        if (input_names_ && input_names_->count(lc.parts[0].name.id)) return Flow::Fail;   // 给输入赋值 = 错误（AssignInput1）
        std::optional<CVal> v = eval_scode(st.b, fscope, depth + 1, true);
        if (!v) return Flow::Fail;
        if (lc.parts.size() == 1) {
          (*locals_)[lc.parts[0].name.id] = *v;
        } else {   // 记录字段赋值 `r.x := …`
          CVal& rec = (*locals_)[lc.parts[0].name.id];
          if (rec.k != CVal::K::Record) rec = CVal::Rec();
          rec.set_field(lc.parts[1].name, *v);
        }
        return Flow::Normal;
      }
      case SStmtKind::If: {
        for (const SStmtBranch& br : st.branches) {   // if + elseif（保留列表）
          std::optional<CVal> c = eval_scode(br.cond, fscope, depth + 1, true);
          if (!c || c->k != CVal::K::Bool) return Flow::Fail;
          if (c->b) return exec_body(br.body, fscope, depth + 1);
        }
        return exec_body(st.else_body, fscope, depth + 1);   // else（可空）
      }
      case SStmtKind::For: {
        if (!st.a.is_valid()) return Flow::Fail;   // 隐式范围 `for i loop`（无 in range）v1 未做 → SKIP（ImplicitRangeAlg2）
        auto seq = eval_range(st.a, fscope, depth);
        if (!seq) return Flow::Fail;
        for (std::int64_t iv : *seq) {
          (*locals_)[st.index.id] = CVal::I(iv);
          const Flow f = exec_body(st.body, fscope, depth + 1);
          if (f == Flow::Break) break;
          if (f == Flow::Return || f == Flow::Fail) return f;
        }
        return Flow::Normal;
      }
      case SStmtKind::While: {
        if (!st.a.is_valid()) return Flow::Fail;   // While: cond=st.a, body=st.body
        for (std::int64_t guard = 0; guard < 100000; ++guard) {   // omc evalLoopLimit=100000：超限放弃折叠
          std::optional<CVal> c = eval_scode(st.a, fscope, depth + 1, true);
          if (!c || c->k != CVal::K::Bool) return Flow::Fail;
          if (!c->b) return Flow::Normal;
          const Flow f = exec_body(st.body, fscope, depth + 1);
          if (f == Flow::Break) return Flow::Normal;
          if (f == Flow::Return || f == Flow::Fail) return f;
        }
        return Flow::Fail;   // 超迭代上限 → 放弃折叠
      }
      case SStmtKind::Return: return Flow::Return;
      case SStmtKind::Break: return Flow::Break;
      default: return Flow::Fail;   // when/assert/terminate/reinit/noretcall → 未做
    }
  }

  // 记录常量折叠的字段直接发射：`constant R r = f(…)` 折成记录值 rv → 逐字段 `r.field = 值`（继承 r 的变率/final）。
  // 字段须标量内建（非数组/嵌套记录），且折叠值含该字段并能按字段类型强转，否则 false（调用方 SKIP）。
  bool emit_record_fields(CompId cid, const std::vector<Symbol>& name, const std::vector<Symbol>& prefix,
                          const PSubs& psubs, NodeId reci, const CVal& rv, bool is_protected, bool force_final) {
    (void)prefix;
    FlatVarKind vk = FlatVarKind::Var;
    switch (I_.comps[cid].attributes.variability) {
      case SVariability::Var: vk = FlatVarKind::Var; break;
      case SVariability::Discrete: vk = FlatVarKind::Discrete; break;
      case SVariability::Param: vk = FlatVarKind::Parameter; break;
      case SVariability::Const: vk = FlatVarKind::Constant; break;
    }
    const bool fin = I_.comps[cid].attributes.is_final || force_final;
    const std::vector<NodeId> fields = I_.trees[I_.node_class(reci).tree].components;   // 拷贝（不跨 add 持引用）
    for (NodeId fn : fields) {
      const CompId fc = I_.nodes[fn].comp;
      const SElement& fcel = I_.scode.elems[I_.comps[fc].def];
      if (!fcel.attr.dims.empty() || !fcel.type.dims.empty()) return false;   // 数组字段 → SKIP
      if (I_.comps[fc].ty.kind == TypeKind::Complex) return false;            // 嵌套记录字段 → SKIP
      const CVal* fv = nullptr;
      if (rv.rec)
        for (const auto& f : *rv.rec) if (f.first == I_.nodes[fn].name) { fv = &f.second; break; }
      if (!fv) return false;
      std::optional<CVal> cv = coerce_cval(*fv, fcel.type.path);
      if (!cv || cv->k == CVal::K::Record) return false;
      const Type fty = I_.comps[fc].ty;
      const SourceLoc floc = I_.comps[fc].loc;
      FlatVariable v;
      v.name = name;
      v.name.push_back(I_.nodes[fn].name);
      v.name_subs = psubs;
      v.name_subs.resize(v.name.size());   // 逐段无下标
      v.ty = fty;
      v.is_protected = is_protected;
      v.is_final = fin;
      v.var_kind = vk;
      v.binding = literal_of(*cv);
      v.loc = floc;
      result_.model.variables.push_back(std::move(v));
    }
    return true;
  }

  Id<Expr> literal_of(const CVal& v) {
    switch (v.k) {
      case CVal::K::Int: return lit_int(v.i);
      case CVal::K::Real: return lit_real(v.r);
      case CVal::K::Bool: return lit_bool(v.b);
      case CVal::K::Str: { Expr e; e.kind = ExprKind::StringLit; e.sval = v.s; e.ty = Type::string(); return add(std::move(e)); }
      case CVal::K::Record: break;   // 记录值不应到这里（emit_record_fields 已按字段拆开）
      case CVal::K::Array: break;    // 数组值不应到这里（标量上下文才调 literal_of）
    }
    return lit_int(0);
  }

  // 摊平嵌套数组 CVal 成标量元素序列（规约用）。
  static void flatten_cval_array(const CVal& v, std::vector<CVal>& out) {
    if (v.k == CVal::K::Array) { if (v.arr) for (const CVal& e : *v.arr) flatten_cval_array(e, out); }
    else out.push_back(v);
  }

  // 常量数组规约 sum/product/min/max（单数组实参）→ 标量常量。min(a,b) 等标量形态不在此（返回 nullopt）。
  std::optional<CVal> eval_reduction(const Exp& call, const ScodeScope& scope, int depth) {
    const Cref& fn = I_.absyn.crefs[call.cref];
    if (fn.parts.size() != 1 || !fn.parts[0].subscripts.empty() || call.args.size() != 1) return std::nullopt;
    const std::string_view nm = in_.str(fn.parts[0].name);
    const bool is_sum = nm == "sum", is_prod = nm == "product", is_min = nm == "min", is_max = nm == "max";
    if (!is_sum && !is_prod && !is_min && !is_max) return std::nullopt;
    std::optional<CVal> av = eval_scode(call.args[0], scope, depth + 1, true);
    if (!av || av->k != CVal::K::Array) return std::nullopt;
    std::vector<CVal> flat;
    flatten_cval_array(*av, flat);
    if (flat.empty()) return std::nullopt;
    bool all_int = true;
    for (const CVal& c : flat) {
      if (c.k != CVal::K::Int && c.k != CVal::K::Real) return std::nullopt;   // 只规约数值
      if (c.k != CVal::K::Int) all_int = false;
    }
    if (is_sum || is_prod) {
      if (all_int) {
        std::int64_t acc = is_sum ? 0 : 1;
        for (const CVal& c : flat) acc = is_sum ? acc + c.i : acc * c.i;
        return CVal::I(acc);
      }
      double acc = is_sum ? 0.0 : 1.0;
      for (const CVal& c : flat) acc = is_sum ? acc + c.as_r() : acc * c.as_r();
      return CVal::R(acc);
    }
    CVal best = flat[0];   // min/max：保留胜出元素的类型
    for (const CVal& c : flat)
      if (is_min ? (c.as_r() < best.as_r()) : (c.as_r() > best.as_r())) best = c;
    return best;
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

  // 下标常量值：整型字面量，或 for 方程迭代变量的当前值（ForEquation1 的 `x[i]`）；否则 nullopt。
  std::optional<std::int64_t> subscript_int(Id<Exp> s) {
    const Exp& e = I_.absyn.exps[s];
    if (e.kind == ExpKind::IntLit) return e.ival;
    if (e.kind == ExpKind::CrefExp) {
      const Cref& c = I_.absyn.crefs[e.cref];
      if (c.parts.size() == 1 && c.parts[0].subscripts.empty()) {
        auto it = loop_vars_.find(c.parts[0].name.id);
        if (it != loop_vars_.end()) return it->second;
      }
    }
    return std::nullopt;
  }

  // cref：解析首段到 enclosing 的本地组件；命中 → 前缀改写 + 取其类型；否则（time 等内建）原样、类型 Real。
  Id<Expr> cref(Id<Cref> ac, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const Cref& c = I_.absyn.crefs[ac];
    // for 方程迭代变量作为值：`… = i` → 内联成整数字面量（ForEquation1 的 RHS）。
    if (!loop_vars_.empty() && c.parts.size() == 1 && c.parts[0].subscripts.empty()) {
      auto it = loop_vars_.find(c.parts[0].name.id);
      if (it != loop_vars_.end()) return lit_int(it->second);
    }
    Expr e;
    e.kind = ExprKind::Cref;
    NodeId local = c.parts.empty() ? Inst::empty_node() : find_local(enclosing, c.parts[0].name);
    // 首段命中条件为假被移除的组件 → 在方程/绑定里引用它是错误（omc 报错，ConditionInvalidContext1/2/4）→ SKIP。
    if (local.is_valid() && !I_.nodes[local].is_empty() && removed_nodes_.count(local.raw)) {
      set_unsupported("reference to removed conditional component");
      e.ty = Type::real();
      return add(std::move(e));
    }
    // 非末段带下标（复合数组元素引用 `a[3].x`）：ymc 未做复合数组下标越界/求值校验 → 保守 SKIP（ArrayBounds2 越界）。
    if (local.is_valid() && !I_.nodes[local].is_empty())
      for (std::size_t i = 0; i + 1 < c.parts.size(); ++i)
        if (!c.parts[i].subscripts.empty()) {
          set_unsupported("composite array element reference");
          e.ty = Type::real();
          return add(std::move(e));
        }
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
                std::optional<std::int64_t> iv = subscript_int(subs[k]);
                if (!iv || *iv < 1 || *iv > ldims[k]) {
                  set_unsupported("array subscript out of range");
                  break;
                }
              }
            }
          }
        }
      }
      // 前缀改写：逐段带上 cur_psubs_ 的下标（复合数组 `a[i].x`）；无则空下标（普通嵌套）。
      for (std::size_t i = 0; i < prefix.size(); ++i) {
        std::vector<Id<Expr>> psub;
        if (i < cur_psubs_.size())
          for (std::int64_t s : cur_psubs_[i]) psub.push_back(lit_int(s));
        e.cref.parts.push_back({prefix[i], std::move(psub)});
      }
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
    // 用户函数常量折叠优先（支持命名/默认实参）：单名解析到 function 类且实参可求常量 → 折成字面量（CevalFunc1/FuncSimple）。
    if (fn.parts.size() == 1)
    {
      const ScodeScope sc = scode_scope_of(enclosing);
      if (auto rv = eval_reduction(e, sc, 0)) return literal_of(*rv);   // sum/product/min/max(常量数组) → 字面量
      if (auto uv = eval_user_call(e, sc, 0)) return literal_of(*uv);
    }
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
          // der 实参须 Real：非 Real 原实参会被插 Cast（如 der(Integer)）→ omc 报错（DerInvalid2）→ SKIP。
          if (result_.model.exprs[args[0]].kind == ExprKind::Cast) set_unsupported("der of non-Real argument");
          bool zero = is_num_lit(result_.model.exprs[args[0]]);
          if (!zero && result_.model.exprs[args[0]].kind == ExprKind::Cref) {
            const std::uint32_t raw = result_.model.exprs[args[0]].cref.resolved_node;
            if (raw != Id<INode>::kInvalid) {
              const NodeId node = NodeId::from_index(raw);
              if (I_.nodes.contains(node) && !I_.nodes[node].is_empty() && I_.nodes[node].comp.is_valid()) {
                const SVariability var = I_.comps[I_.nodes[node].comp].attributes.variability;
                zero = var == SVariability::Param || var == SVariability::Const;
                // der 作用于离散变量是错误（omc 报错，FuncBuiltinDerInvalid1/2）→ SKIP。
                if (var == SVariability::Discrete) set_unsupported("der of discrete variable");
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

  // 记录构造整体绑定 `R r = R(a, b, x=c)` = 类修饰 `R r(字段=实参…)`：把构造实参按声明序（位置实参）
  // 或名字（命名实参）匹配到字段，合成一个类修饰 SMod（缺省实参不进 subs → 该字段保留声明默认，RecordBinding1）。
  // 返回 invalid = 不适用/未支持（非构造调用、名字对不上、记录有 extends…），调用方据此仍走整体绑定 SKIP。
  Id<SMod> build_record_ctor_mod(Id<Exp> callraw, NodeId ci, Id<SElement> cdef) {
    const Exp& call = I_.absyn.exps[callraw];
    if (call.kind != ExpKind::Call) return {};
    if (call.partial_fn || call.call_suffix.is_valid()) return {};
    // 构造函数名末段必须 == 记录类名（否则是返回记录的用户函数等 → SKIP）。
    const Cref& fn = I_.absyn.crefs[call.cref];
    if (fn.parts.empty() || !fn.parts.back().subscripts.empty()) return {};
    if (fn.parts.back().name != I_.scode.elems[cdef].name) return {};
    // 记录含 extends（继承字段的构造序/约束）本桶未验证 → SKIP（RecordExtends）。
    for (Id<SElement> eid : I_.scode.elems[cdef].def.elements)
      if (I_.scode.elems[eid].kind == SElementKind::Extends) return {};
    const std::vector<NodeId>& fields = I_.trees[I_.node_class(ci).tree].components;
    std::vector<SSubMod> subs;
    std::size_t pos = 0;
    for (std::size_t i = 0; i < call.args.size(); ++i) {
      Symbol fld;
      if (i < call.arg_names.size() && call.arg_names[i].is_valid()) {
        fld = call.arg_names[i];                       // 命名实参 R(x=..)
      } else {                                          // 位置实参 → 第 pos 个字段（声明序）
        if (pos >= fields.size()) return {};            // 实参多于字段 → 越界，交给上层 SKIP
        fld = I_.nodes[fields[pos++]].name;
      }
      SMod fm;
      fm.kind = SModKind::Mod;
      fm.binding = call.args[i];
      fm.loc = call.loc;
      subs.push_back({fld, I_.scode.add_mod(std::move(fm))});
    }
    SMod outer;
    outer.kind = SModKind::Mod;
    outer.subs = std::move(subs);
    outer.loc = call.loc;
    return I_.scode.add_mod(std::move(outer));
  }

  // 复合数组第 aidx 个元素的合成修饰：把每个子修饰的绑定按 aidx 逐维下降取标量（`x={1,2,3}` 的元素 i → `x=vi`）；
  // each 子修饰保持原绑定（对每元素相同）。子修饰带嵌套 subs（属性修饰 `x(start=…)`）或无绑定 → 返回 invalid（本 phase 未做）。
  Id<SMod> distribute_mod(Id<SMod> m, const std::vector<std::int64_t>& aidx) {
    const std::vector<SSubMod> subs = I_.scode.mods[m].subs;   // 拷贝（下面 add_mod 会 realloc mods arena）
    const SourceLoc mloc = I_.scode.mods[m].loc;
    std::vector<SSubMod> out;
    for (const SSubMod& s : subs) {
      const SMod sub = I_.scode.mods[s.mod];   // 拷贝
      if (!sub.subs.empty() || !sub.binding.is_valid()) return {};
      Id<Exp> bexp = sub.is_each ? sub.binding : array_scalar_elem(sub.binding, aidx, 0);
      if (!bexp.is_valid()) return {};
      SMod nsub;
      nsub.kind = SModKind::Mod;
      nsub.binding = bexp;
      nsub.is_final = sub.is_final;
      nsub.loc = sub.loc;
      out.push_back({s.ident, I_.scode.add_mod(std::move(nsub))});
    }
    SMod outer;
    outer.kind = SModKind::Mod;
    outer.subs = std::move(out);
    outer.loc = mloc;
    return I_.scode.add_mod(std::move(outer));
  }

  // 某实例节点的类是否连接器（flow 语义/方向保留只在连接器内成立）。
  bool enclosing_is_connector(NodeId enclosing) {
    if (I_.nodes[enclosing].is_empty()) return false;
    Id<SElement> d = I_.nodes[enclosing].def;
    if (!d.is_valid()) return false;
    SRestriction r = I_.scode.elems[d].restriction;
    return r == SRestriction::Connector || r == SRestriction::ExpandableConnector;
  }

  // ---- connect 连接集展开（v1：恰好一条 connect、根层、两个标量连接器）----
  static std::string path_key(const std::vector<Symbol>& p) {
    std::string k;
    for (Symbol s : p) { k += std::to_string(s.id); k.push_back('.'); }
    return k;
  }
  struct ConnMember { Symbol name; bool is_flow; Type ty; };
  struct ConnInfo { std::vector<Symbol> path; bool outside; std::vector<ConnMember> members; };

  // 把 connect 实参（cref）解析成连接器：flat 路径 + inside/outside（顶层连接器=outside）+ 标量成员表。
  // 任何超范围形态（数组连接器/成员、嵌套连接器成员、stream、expandable、非连接器、下标）→ nullopt（调用方 SKIP）。
  std::optional<ConnInfo> resolve_connector(Id<Exp> ce, NodeId enclosing) {
    if (I_.absyn.exps[ce].kind != ExpKind::CrefExp) return std::nullopt;
    const Cref& c = I_.absyn.crefs[I_.absyn.exps[ce].cref];
    if (c.leading_dot || c.parts.empty() || !c.parts[0].subscripts.empty()) return std::nullopt;
    // 合法连接器引用形态（MLS §9.1 / omc）：`c`（顶层，outside）或 `m.c`（m 非连接器组件，inside）。
    // 深层 `b.a.c`（omc 直接报错）与嵌套 `c1.c2` v1 未做 → SKIP，保持 outside=(depth==1) 的判定成立。
    if (c.parts.size() > 2) return std::nullopt;
    NodeId cur = find_local(enclosing, c.parts[0].name);
    if (!cur.is_valid() || I_.nodes[cur].is_empty()) return std::nullopt;
    if (c.parts.size() == 2) {
      NodeId fcls = I_.comps[I_.nodes[cur].comp].class_inst;
      const bool first_is_conn = fcls.is_valid() && !I_.nodes[fcls].is_empty() &&
                                 I_.nodes[fcls].def.is_valid() &&
                                 I_.scode.elems[I_.nodes[fcls].def].restriction == SRestriction::Connector;
      if (first_is_conn) return std::nullopt;   // `c1.c2` 嵌套连接（omc 视为 outside）v1 未做
    }
    std::vector<Symbol> path{c.parts[0].name};
    for (std::size_t i = 1; i < c.parts.size(); ++i) {
      if (!c.parts[i].subscripts.empty()) return std::nullopt;
      NodeId cls = I_.comps[I_.nodes[cur].comp].class_inst;
      NodeId nxt = cls.is_valid() ? find_local(cls, c.parts[i].name) : Inst::empty_node();
      if (!nxt.is_valid() || I_.nodes[nxt].is_empty()) return std::nullopt;
      cur = nxt;
      path.push_back(c.parts[i].name);
    }
    // cur = 连接器组件；其类必须是连接器，且组件本身标量（非数组连接器）。
    const SElement& curcel = I_.scode.elems[I_.comps[I_.nodes[cur].comp].def];
    if (!curcel.attr.dims.empty() || !curcel.type.dims.empty()) return std::nullopt;
    NodeId conn = I_.comps[I_.nodes[cur].comp].class_inst;
    if (!conn.is_valid() || I_.nodes[conn].is_empty()) return std::nullopt;
    Id<SElement> cdef = I_.nodes[conn].def;
    if (!cdef.is_valid() || I_.scode.elems[cdef].restriction != SRestriction::Connector) return std::nullopt;
    ConnInfo info;
    info.path = path;
    info.outside = path.size() == 1;
    const std::vector<NodeId> members = I_.trees[I_.node_class(conn).tree].components;   // 拷贝（不跨 add）
    for (NodeId mn : members) {
      const CompId mc = I_.nodes[mn].comp;
      const SElement& mcel = I_.scode.elems[I_.comps[mc].def];
      if (!mcel.attr.dims.empty() || !mcel.type.dims.empty()) return std::nullopt;    // 数组成员 → SKIP
      if (I_.comps[mc].ty.kind == TypeKind::Complex) return std::nullopt;             // 嵌套连接器成员 → SKIP
      if (I_.comps[mc].attributes.connector == SConnectorType::Stream) return std::nullopt;  // stream → SKIP
      // parameter/constant 连接器成员：连接生成的是相等断言 assert(abs(a-b)<=0,…) 而非方程，且变率不匹配是错误
      // （ConstantConnector3 / InvalidComplexConnectorType*）——v1 未做 → SKIP。
      if (I_.comps[mc].attributes.variability != SVariability::Var) return std::nullopt;
      const bool is_flow = I_.comps[mc].attributes.connector == SConnectorType::Flow;
      info.members.push_back({I_.nodes[mn].name, is_flow, I_.comps[mc].ty});
    }
    return info;
  }

  Id<Expr> member_cref(const std::vector<Symbol>& path, Symbol member, Type ty) {
    Expr e;
    e.kind = ExprKind::Cref;
    e.ty = ty;
    for (Symbol s : path) e.cref.parts.push_back({s, {}});
    e.cref.parts.push_back({member, {}});
    return add(std::move(e));
  }
  Id<Expr> neg_expr(Id<Expr> x) {
    const Type t = result_.model.exprs[x].ty;
    Expr e;
    e.kind = ExprKind::Unary;
    e.un = UnOp::Minus;
    e.a = x;
    e.ty = t;
    return add(std::move(e));
  }
  Id<Expr> bin_expr(BinOp op, Id<Expr> a, Id<Expr> b) {
    const Type t = result_.model.exprs[a].ty;
    Expr e;
    e.kind = ExprKind::Binary;
    e.bin = op;
    e.a = a;
    e.b = b;
    e.ty = t;
    return add(std::move(e));
  }
  // flow 求和左端（= 0）：inside 取正、outside 取负；both-inside 项序反转（B,A）、否则正序（A,B）——
  // 复刻 OMC DisjointSets seed 反序/merge 正序的可见效果（Connect2/Connect5/BothInside 实测）。
  Id<Expr> flow_sum(const ConnInfo& A, const ConnInfo& B, Symbol m, Type ty) {
    Id<Expr> ca = member_cref(A.path, m, ty);
    Id<Expr> cb = member_cref(B.path, m, ty);
    if (A.outside && B.outside) return neg_expr(bin_expr(BinOp::Add, ca, cb));   // -(A.f + B.f)
    if (!A.outside && !B.outside) return bin_expr(BinOp::Add, cb, ca);           // B.f + A.f（反序）
    Id<Expr> pos = A.outside ? cb : ca;                                          // inside 为正
    Id<Expr> negt = A.outside ? ca : cb;                                         // outside 为负
    return bin_expr(BinOp::Sub, pos, negt);                                      // pos - neg
  }

  // connect 实参是否引用了被移除（条件为假）的组件——按 cref 前缀逐段查 removed_。
  bool references_removed(Id<Exp> ce) {
    if (I_.absyn.exps[ce].kind != ExpKind::CrefExp) return false;
    const Cref& c = I_.absyn.crefs[I_.absyn.exps[ce].cref];
    std::vector<Symbol> p;
    for (const CrefPart& part : c.parts) {
      p.push_back(part.name);
      if (removed_.count(path_key(p))) return true;
    }
    return false;
  }

  void process_connects() {
    if (connects_.empty()) return;
    // 引用了被移除组件的 connect 整条丢弃（条件组件 false，Condition5）；其余才算"活" connect。
    std::vector<ConnectStmt> live;
    for (const ConnectStmt& cs : connects_)
      if (!references_removed(cs.a) && !references_removed(cs.b)) live.push_back(cs);
    if (live.empty()) return;
    for (const ConnectStmt& cs : live)
      if (!cs.prefix.empty()) { set_unsupported("connect in submodel"); return; }   // 前缀化+重算 inside/outside 未做
    if (live.size() > 1) { set_unsupported("multiple connect (needs connection sets)"); return; }
    const ConnectStmt& cs = live[0];
    std::optional<ConnInfo> A = resolve_connector(cs.a, cs.enclosing);
    std::optional<ConnInfo> B = resolve_connector(cs.b, cs.enclosing);
    if (!A || !B) { set_unsupported("connect operand shape"); return; }
    if (A->members.size() != B->members.size()) { set_unsupported("connector member mismatch"); return; }
    // 遍历 A 的成员（声明序）：potential 攒相等、flow 攒求和；potential 全部在前、flow 在后（实测顺序）。
    std::vector<Id<FlatEquation>> pots, flows;
    for (const ConnMember& m : A->members) {
      const ConnMember* bm = nullptr;
      for (const ConnMember& x : B->members) if (x.name == m.name) { bm = &x; break; }
      if (!bm || bm->is_flow != m.is_flow) { set_unsupported("connector member mismatch"); return; }
      FlatEquation fe;
      fe.kind = FlatEqKind::Equality;
      fe.loc = cs.loc;
      if (m.is_flow) {
        fe.a = flow_sum(*A, *B, m.name, m.ty);
        fe.b = lit_real(0.0);
        flows.push_back(result_.model.add_eq(std::move(fe)));
      } else {
        fe.a = member_cref(A->path, m.name, m.ty);
        fe.b = member_cref(B->path, m.name, m.ty);
        pots.push_back(result_.model.add_eq(std::move(fe)));
      }
    }
    for (Id<FlatEquation> id : pots) result_.model.equations.push_back(id);
    for (Id<FlatEquation> id : flows) result_.model.equations.push_back(id);
    if (!A->outside) connected_inside_.insert(path_key(A->path));
    if (!B->outside) connected_inside_.insert(path_key(B->path));
  }

  Inst& I_;
  Interner& in_;
  FlattenResult result_;
  std::unordered_map<std::uint32_t, std::vector<Symbol>> outer_redirect_;   // outer 组件节点 raw → inner 全名
  std::unordered_set<std::uint32_t> evaluating_;   // 正在求值的常量节点（环检测）
  NodeId decl_scope_;   // 当前正在算绑定的组件声明处作用域（限定名在此解析；继承成员 = 基类作用域）
  PSubs cur_psubs_;     // 当前 prefix 的逐段下标（复合数组 `a[i]`）；cref 前缀改写读它。emit_*/collect_equation 入口设置
  std::unordered_map<std::uint32_t, CVal>* locals_ = nullptr;   // 用户函数解释期的局部环境（输入/局部/输出名→值）
  std::unordered_set<std::uint32_t>* input_names_ = nullptr;    // 当前函数的输入名（禁止对其赋值，AssignInput1）
  std::unordered_map<std::uint32_t, std::int64_t> loop_vars_;   // for 方程展开期的迭代变量→值（cref/下标解析）
  struct FlowVar { std::vector<Symbol> name; Type ty; SourceLoc loc; };
  std::vector<FlowVar> flow_vars_;   // flow 变量（run() 末尾按 outside/未连接补 `=0` 方程）
  struct ConnectStmt { Id<Exp> a, b; NodeId enclosing; std::vector<Symbol> prefix; SourceLoc loc; };
  std::vector<ConnectStmt> connects_;            // 攒下的 connect()，run() 末尾展开
  std::unordered_set<std::string> connected_inside_;   // 已连接的 inside 连接器路径 key（其 flow 不再单列 =0）
  std::unordered_set<std::string> removed_;             // 条件为假被移除的组件路径 key（引用它的 connect 整条删）
  std::unordered_set<std::uint32_t> removed_nodes_;     // 被移除组件的节点 raw（方程/绑定里引用它 → omc 报错 → SKIP）
};

}  // namespace

FlattenResult flatten(Inst& inst, Interner& in, NodeId root) { return Flattener(inst, in).run(root); }

}  // namespace ymc
