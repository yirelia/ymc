// 压平实现（S3 第一里程碑）。锚点见 flatten.h 头注与各处 R11/R13/R14 引用。
#include "flat/flatten.h"

#include <algorithm>
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
      {"noEvent", 1, false, BFArg::Keep, BFRes::SameArg0},
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
  SVariability var_floor = SVariability::Var;   // 复合组件下沉的变率下限（OMC variabilityMin，NFAttributes.mo:236）
  // 方向保留：所有前缀组件都是连接器或 input 组件时才保留 input/output（Variable.removeNonTopLevelDirection，
  // NFVariable.mo:466-493）；根为 true（顶层变量保留）。
  bool keep_dir = true;
  // 处在连接器内部（含连接器里的记录字段）：flow 成员在此生成 `=0` / 进连接集；连接器外的 flow 前缀 → SKIP。
  bool in_conn = false;
  // 连接器里 `flow R f` 记录成员：flow 前缀下沉到全部标量字段（ConnectorType.merge，NFAttributes.mo:229-234）。
  bool flow_inherited = false;
  // potentially present 链（expandable 连接器）：expandable 类的直属成员置 POTENTIALLY_PRESENT（NFAttributes.mo:411-417），
  // 子孙经 ConnectorType.merge 按位或继承（:229/234），expandable 组件自身清掉（updateComponentConnectorType :493-494）。
  // 存链上各组件节点 raw；叶变量 present ⟺ 链上任一节点被 markComponentPresent（NFExpandableConnectors.mo:334-355）。
  std::vector<std::uint32_t> pp_chain;
};

// OMC Prefixes.variabilityMin（取更受约束者）：ymc 枚举 Var<Discrete<Param<Const 升序=更常量，故取序数较大者。
inline SVariability var_min(SVariability a, SVariability b) {
  return static_cast<std::uint8_t>(a) >= static_cast<std::uint8_t>(b) ? a : b;
}

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
    // 连接集展开（DisjointSets 序：potential 相等、flow 求和、未连接 flow `=0`），整体插到模型自身方程之前
    //（NFFlatten.mo:2497）。顶层（outside）连接器的 flow 既进求和集（OUTSIDE 面）又留 INSIDE 单元素集 → 同时有
    // `-(c1.f + c2.f) = 0` 与 `c1.f = 0`（Connector.isEqual 含 face，NFConnector.mo:143-148）。
    process_connects();
    // 连接解析后才能求 inStream/actualStream 与 cardinality（evaluateConnectionOperators，NFFlatten.mo:2565-2576），
    // 随后按 SimplifyModel 折叠。
    evaluate_stream_ops();
    apply_structural_params();   // 连接算子求值中结构化的参数：final + 属性折叠（NFEvalConstants 效果，受限）
    evaluate_cardinality();
    // 某变量名同时出现在 removed_（另一同名声明条件为假被移除）：redeclare 一个 replaceable-conditional 成员的
    // 语义（保留原条件 / 禁止带条件 redeclare）v1 未做 → SKIP（RedeclareElementCondition1/2/3）。
    if (!removed_.empty())
      for (const FlatVariable& v : result_.model.variables)
        if (removed_.count(path_key(v.name))) { set_unsupported("conditional component redeclared"); break; }
    // 重复变量名 → omc 会去重（R11 §3.4-3）；v1 未做，标 unsupported（driver 退 66）。
    for (std::size_t i = 0; i < result_.model.variables.size(); ++i)
      for (std::size_t j = i + 1; j < result_.model.variables.size(); ++j)
        if (result_.model.variables[i].name == result_.model.variables[j].name &&
            name_subs_eq(result_.model.variables[i].name_subs, result_.model.variables[j].name_subs) &&
            subs_eq(result_.model.variables[i].subscripts, result_.model.variables[j].subscripts)) {
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
    const SElement& def = I_.scode.elems[I_.nodes[inst_node].def];
    if (def.def.external.present) set_unsupported("external function");
    for (Id<SElement> eid : def.def.elements) {
      SElementKind k = I_.scode.elems[eid].kind;
      if (k == SElementKind::Import) set_unsupported("import");   // v1 未做 import 解析
    }
    // 本类是 expandable 连接器：其直属成员 potentially present（checkDeclaredComponentAttributes，NFAttributes.mo:411-417）。
    const bool exp_class = def.restriction == SRestriction::ExpandableConnector;
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
      if (conflicting_direction(cel, inst_node)) { set_unsupported("conflicting direction prefix"); continue; }
      // expandable 直属成员禁 flow/stream 前缀（assertNotFlowStream，NFAttributes.mo:411-413；omc 报错，ExpandableConnectorFlow1）。
      if (exp_class && cel.attr.ct != SConnectorType::Potential) { set_unsupported("flow/stream in expandable connector"); continue; }
      // 本组件 potentially present（直属 expandable 或祖先 pp）时的链（含自身）；expandable 组件自身不 pp（见 ModCtx::pp_chain）。
      std::vector<std::uint32_t> pp_self;
      if (exp_class || !ctx.pp_chain.empty()) { pp_self = ctx.pp_chain; pp_self.push_back(cn.raw); }
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
          const bool r_exp = r == SRestriction::ExpandableConnector;   // expandable 数组：逐元素同标量 expandable
          // 普通连接器数组 `C c[3]`：与其它复合数组同路——逐元素 `c[i]` 递归摊开成员（标量成员 → 变量，
          // flow 成员 → flow_vars_ 带下标）。连接方程在 process_connects 按元素下标解析（ForConnect1）。
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
            achild.keep_dir = ctx.keep_dir && (r == SRestriction::Connector || r_exp ||
                                               I_.comps[cid].attributes.direction == SDirection::Input);
            achild.in_conn = ctx.in_conn || r == SRestriction::Connector || r_exp;
            achild.flow_inherited = ctx.flow_inherited;   // 复合数组上的 flow 前缀仍在上方守卫处 SKIP
            // 数组各元素共用同一组件节点（present 标记按节点，YmcExpNode1 探针）；expandable 组件的成员重起链。
            if (!r_exp) achild.pp_chain = pp_self;
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
        cur_pp_chain_ = pp_self;
        emit_array(cid, name, is_prot, prefix, psubs, inst_node, matched, ctx.scope, ctx.vprefix, comp_final, cel, ctx);
        cur_pp_chain_.clear();
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
        // expandable 连接器同样摊开已声明成员（均 potentially present），连接后由 elaborate_expandable 增广/删除。
        SRestriction r = I_.scode.elems[cdef].restriction;
        const bool r_exp = r == SRestriction::ExpandableConnector;
        // flow/stream 前缀加在复合**组件**上：连接器里的 `flow R f`（记录）→ 前缀下沉到全部标量字段（ConnectorType.merge，
        // NFAttributes.mo:229-234；YmcRecConn 探针）；`flow C c`（连接器，conn9：omc 另有不平衡告警行）与 stream 仍 SKIP。
        const bool flow_rec = ctx.in_conn && r == SRestriction::Record &&
                              I_.comps[cid].attributes.connector == SConnectorType::Flow;
        if (I_.comps[cid].attributes.connector != SConnectorType::Potential && !flow_rec)
          set_unsupported("flow/stream on composite component");
        // 记录常量折叠：`constant/parameter R r = f(…)`（函数返回记录）→ 直接发射字段（继承 r 变率 + 折叠值），
        // 绕开下面的"变率复合" SKIP（CevalFuncRecord1/2）。
        if (r == SRestriction::Record && pp_self.empty()) {   // expandable 里的记录折叠字段不带 pp 链 → 不走此路
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
        // `constant/parameter R r`：变率前缀下沉到每个成员（Half A），常量记录字段引用 `r.x` 折叠（Half B）。
        // 只对**纯数据记录**放行（排除 final 字段/数组字段/嵌套记录/extends 等会 FAIL 的形态，见 is_plain_data_record）。
        if (I_.comps[cid].attributes.variability != SVariability::Var && !is_plain_data_record(ci, cdef))
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
        child.var_floor = var_min(ctx.var_floor, I_.comps[cid].attributes.variability);   // 变率下沉（Half A）
        // expandable 组件也算连接器（InstNode.isConnector = CONNECTOR_MASK 含 EXPANDABLE，NFPrefixes.mo:59-60/167-175）：
        // 其成员保留方向（expconn4 `input Real ec.ri`、ExpandableConnector7 `output Real ec1.ec.x`）。
        child.keep_dir = ctx.keep_dir && (r == SRestriction::Connector || r_exp ||
                                          I_.comps[cid].attributes.direction == SDirection::Input);
        child.in_conn = ctx.in_conn || r == SRestriction::Connector || r_exp;
        child.flow_inherited = ctx.flow_inherited || flow_rec;
        if (!r_exp) child.pp_chain = pp_self;   // expandable 组件的成员重起链（其直属成员由 exp_class 置 pp）
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
      cur_pp_chain_ = pp_self;
      emit_variable(cid, name, is_prot, prefix, psubs, inst_node, matched, ctx.scope, ctx.vprefix, comp_final, final_params, ctx.var_floor, ctx);
      cur_pp_chain_.clear();
      decl_scope_ = Inst::empty_node();
    }
    // 本类方程（子组件方程已在上面递归时加入 → 符合 R11 §3.4-6"子组件先、本类后"）。
    // 段序：initial equation 先于 equation（R13 §2）；enclosing = 本实例，cref 前缀 = prefix。
    for (Id<SEquation> eid : def.def.initial_equations) collect_equation(eid, inst_node, prefix, psubs, /*initial=*/true);
    for (Id<SEquation> eid : def.def.equations) collect_equation(eid, inst_node, prefix, psubs, /*initial=*/false);
    // 算法段：翻译成 flat 语句（子组件段已在上面递归先入 → 子先本类后，与方程同序 R11 §3.4-6）。
    for (const SAlgorithm& alg : def.def.initial_algorithms) collect_algorithm(alg, inst_node, prefix, psubs, /*initial=*/true);
    for (const SAlgorithm& alg : def.def.algorithms) collect_algorithm(alg, inst_node, prefix, psubs, /*initial=*/false);
  }

  // 翻译一个算法段 → FlatAlgorithm（v1：只做直线标量赋值，见 collect_statement）。段内任一语句未做 → 整模型 SKIP。
  void collect_algorithm(const SAlgorithm& alg, NodeId enclosing, const std::vector<Symbol>& prefix,
                         const PSubs& psubs, bool is_initial) {
    if (result_.unsupported) return;
    cur_psubs_ = psubs;   // cref 前缀改写（与 collect_equation 一致：本段引用带 `a[i]` 段下标）
    FlatAlgorithm fa;
    fa.is_initial = is_initial;
    for (Id<SStatement> sid : alg.stmts) {
      Id<FlatStatement> fs = collect_statement(sid, enclosing, prefix);
      if (result_.unsupported) return;   // 有语句未做 → 放弃整模型（保 FAIL=0，不产半截算法段）
      fa.stmts.push_back(fs);
    }
    if (fa.stmts.empty()) return;   // 空段不产关键字（dumper flat_dump.cpp:129 也跳空段）
    if (is_initial) result_.model.initial_algorithms.push_back(std::move(fa));
    else result_.model.algorithms.push_back(std::move(fa));
  }

  // 翻译一条语句（v1：只做标量赋值 `cref := expr`，目标须是已发射的连续变量；其余 kind/目标 → SKIP 保 FAIL=0）。
  Id<FlatStatement> collect_statement(Id<SStatement> sid, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const SStatement& st = I_.scode.stmts[sid];
    if (st.kind == SStmtKind::For) return collect_for_statement(st, enclosing, prefix);
    if (st.kind != SStmtKind::Assign) { set_unsupported("algorithm statement"); return {}; }
    // LHS 解析成 flat cref。常量目标已被 cref() 内联成字面量 → 非 Cref → 落到下面的目标校验 SKIP。
    Id<Expr> lhs = simplify_expr(inst_exp(st.a, enclosing, prefix));
    if (result_.unsupported) return {};
    std::vector<Symbol> lname;
    bool array_elem = false;
    {
      const Expr& le = result_.model.exprs[lhs];
      if (le.kind != ExprKind::Cref) { set_unsupported("algorithm assignment target"); return {}; }
      for (std::size_t i = 0; i < le.cref.parts.size(); ++i) {
        lname.push_back(le.cref.parts[i].name);
        if (!le.cref.parts[i].subscripts.empty()) {
          if (i + 1 != le.cref.parts.size()) { set_unsupported("algorithm array-element assignment"); return {}; }  // 非末段下标暂缓
          array_elem = true;   // 末段带下标 = 数组元素赋值（for 体内 `x[i] :=`）
        }
      }
    }
    // 目标须是已发射的连续变量（Var）：标量取 find_scalar_flat_var；数组元素取 find_array_flat_var（标量化成 x[1..n]）。
    const FlatVariable* lv = array_elem ? find_array_flat_var(lname) : find_scalar_flat_var(lname);
    if (!lv || lv->var_kind != FlatVarKind::Var) { set_unsupported("algorithm assignment to non-continuous variable"); return {}; }
    const TypeKind ta = lv->ty.kind;   // 先取（下面 inst_exp/to_real 会往 arena add，勿跨调用持引用）
    Id<Expr> rhs = simplify_expr(inst_exp(st.b, enclosing, prefix));
    if (result_.unsupported) return {};
    TypeKind tb;
    {
      const Expr& re = result_.model.exprs[rhs];
      if (re.kind == ExprKind::Array || re.kind == ExprKind::Tuple) { set_unsupported("algorithm array/tuple rhs"); return {}; }
      tb = re.ty.kind;
    }
    // 整型→实型强制（Comment1 `x:=1`→`x:=1.0`）；其余类型不符 → SKIP（StatementInvalidType1 `Real:=String`）。
    if (ta == TypeKind::Real && tb == TypeKind::Integer) rhs = to_real(rhs);
    else if (ta != tb) { set_unsupported("algorithm assignment type mismatch"); return {}; }
    FlatStatement fs;
    fs.kind = FlatStmtKind::Assign;
    fs.a = lhs;
    fs.b = rhs;
    fs.loc = st.loc;
    if (st.cmt.str.is_valid()) fs.comment = in_.intern(unescape_modelica_string(in_.str(st.cmt.str)));
    return result_.model.add_stmt(std::move(fs));
  }

  // 在已发射变量里按点分名找标量变量（无末段下标、无逐段下标）。供算法赋值目标的可变性校验。
  const FlatVariable* find_scalar_flat_var(const std::vector<Symbol>& nm) {
    for (const FlatVariable& v : result_.model.variables) {
      if (!v.subscripts.empty() || v.name != nm) continue;
      bool clean = true;
      for (const std::vector<Id<Expr>>& s : v.name_subs) if (!s.empty()) { clean = false; break; }
      if (clean) return &v;
    }
    return nullptr;
  }

  // for-语句（KEPT loop，对标 NFFlatten.flattenStatement:2308——不展开）：范围类型化 + 迭代变量注册为符号 + 体翻译。
  Id<FlatStatement> collect_for_statement(const SStatement& st, NodeId enclosing, const std::vector<Symbol>& prefix) {
    if (!st.a.is_valid()) { set_unsupported("implicit-range for statement"); return {}; }   // 隐式范围 `for i loop` 暂缓
    Id<Expr> range = simplify_expr(inst_exp(st.a, enclosing, prefix));
    if (result_.unsupported) return {};
    TypeKind ek;   // 迭代变量类型 = 范围元素类型（数组范围取首元素类型；Range 的 ty 已是元素类型）
    {
      const Expr& re = result_.model.exprs[range];
      ek = (re.kind == ExprKind::Array)
               ? (re.args.empty() ? TypeKind::Integer : result_.model.exprs[re.args[0]].ty.kind)
               : re.ty.kind;
    }
    const std::uint32_t id = st.index.id;   // 注册迭代变量（符号，不内联）；save/restore 支持嵌套
    const bool had = stmt_iters_.count(id) > 0;
    const TypeKind prev = had ? stmt_iters_[id] : TypeKind::Integer;
    stmt_iters_[id] = (ek == TypeKind::Real) ? TypeKind::Real : TypeKind::Integer;
    FlatStatement fs;
    fs.kind = FlatStmtKind::For;
    fs.index = st.index;
    fs.a = range;
    fs.loc = st.loc;
    bool ok = true;
    for (Id<SStatement> bid : st.body) {
      Id<FlatStatement> bs = collect_statement(bid, enclosing, prefix);
      if (result_.unsupported) { ok = false; break; }
      fs.body.push_back(bs);
    }
    if (had) stmt_iters_[id] = prev; else stmt_iters_.erase(id);   // 恢复
    if (!ok) return {};
    return result_.model.add_stmt(std::move(fs));
  }

  // Absyn 下标 s 是否为当前 for-语句的符号迭代变量（单段无下标 cref、名在 stmt_iters_）。
  bool is_stmt_iter_subscript(Id<Exp> s) {
    if (stmt_iters_.empty()) return false;
    const Exp& e = I_.absyn.exps[s];
    if (e.kind != ExpKind::CrefExp) return false;
    const Cref& c = I_.absyn.crefs[e.cref];
    return c.parts.size() == 1 && c.parts[0].subscripts.empty() && stmt_iters_.count(c.parts[0].name.id) > 0;
  }

  // 在已发射变量里按点分名找标量化数组变量（name={x}、有末段下标 x[1..n]）。供 for 体 `x[i]:=` 目标校验。
  const FlatVariable* find_array_flat_var(const std::vector<Symbol>& nm) {
    for (const FlatVariable& v : result_.model.variables)
      if (v.name == nm && !v.subscripts.empty()) return &v;
    return nullptr;
  }

  // 翻译一条方程（v1：Equals + 攒 connect 待 run() 展开；其余 kind 标 unsupported）。
  void collect_equation(Id<SEquation> eid, NodeId enclosing, const std::vector<Symbol>& prefix, const PSubs& psubs,
                        bool is_initial) {
    cur_psubs_ = psubs;   // cref 前缀改写用（本类方程里的引用带 `a[i]` 段下标）
    const SEquation& eq = I_.scode.eqs[eid];
    if (eq.kind == SEqKind::Connect) {
      // connect 攒起来在 run() 末尾按连接集展开（potential 相等 + flow 求和）。initial 段里的 connect 罕见 → SKIP。
      if (is_initial) { set_unsupported("connect in initial equation"); return; }
      if (in_if_) { set_unsupported("connect in if equation"); return; }   // 分支体里的 connect 未做
      // for 循环体里的 connect（`connect(c1[i],c2[i])`）：下标含迭代变量 i，但解析推迟到 run() 末尾时
      // loop_vars_ 已清空 → 采集当前 loop_vars_ 快照，process_connects 解析该 connect 前还原（ForConnect1）。
      connects_.push_back({eq.a, eq.b, enclosing, prefix, eq.loc, loop_vars_, psubs});
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
      card_allowed_ = true;   // 条件里允许 cardinality（CardinalityInvalidContext*：其它位置 omc 报错）
      fe.a = simplify_expr(inst_exp(eq.a, enclosing, prefix));
      card_allowed_ = false;
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
    if (eq.kind == SEqKind::If) {
      // if 方程：常量条件求值折叠（选中真分支 / 丢弃假分支），否则发射 flat if。参数条件 omc 会把参数标 final——
      // ymc 的 if 路径不标 final，故只折**纯字面量/迭代变量**条件（used 为空），参数条件 → SKIP。
      std::vector<FlatEqBranch> out_branches;
      for (const SEqBranch& br : eq.branches) {   // if + elseif（保序）
        card_allowed_ = true;   // 条件里允许 cardinality（CardinalityInvalidContext*）
        Id<Expr> ce = simplify_expr(inst_exp(br.cond, enclosing, prefix));
        card_allowed_ = false;
        if (result_.unsupported) return;
        if (result_.model.exprs[ce].ty.kind != TypeKind::Boolean) { set_unsupported("if condition type"); return; }
        std::unordered_set<std::uint32_t> used;
        std::optional<CVal> cv = eval_scode(br.cond, scode_scope_of(enclosing), 0, /*allow_param=*/true, &used);
        if (cv && cv->k == CVal::K::Bool) {
          if (!used.empty()) { set_unsupported("if condition folds via parameter"); return; }
          if (cv->b) {   // 字面量真：选中该分支，丢弃其余
            if (out_branches.empty()) {   // 无前置分支 → 直接内联体（无 if 外壳）
              for (Id<SEquation> beid : br.body) collect_equation(beid, enclosing, prefix, psubs, is_initial);
            } else {   // 有前置分支 → 该分支成为 else
              std::vector<Id<FlatEquation>> else_ids = collect_if_body(br.body, enclosing, prefix, psubs, is_initial);
              if (result_.unsupported) return;
              emit_if(std::move(out_branches), std::move(else_ids), eq.loc, is_initial);
            }
            return;
          }
          continue;   // 字面量假 → 丢弃该分支
        }
        if (!cond_is_runtime(ce)) { set_unsupported("if condition not evaluable"); return; }
        std::vector<Id<FlatEquation>> body_ids = collect_if_body(br.body, enclosing, prefix, psubs, is_initial);
        if (result_.unsupported) return;
        out_branches.push_back(FlatEqBranch{ce, std::move(body_ids)});
      }
      if (out_branches.empty()) {   // 所有 if/elseif 都假（或空）→ else 恒成立
        for (Id<SEquation> beid : eq.else_body) collect_equation(beid, enclosing, prefix, psubs, is_initial);
        return;
      }
      std::vector<Id<FlatEquation>> else_ids = collect_if_body(eq.else_body, enclosing, prefix, psubs, is_initial);
      if (result_.unsupported) return;
      emit_if(std::move(out_branches), std::move(else_ids), eq.loc, is_initial);
      return;
    }
    if (eq.kind != SEqKind::Equals) {
      set_unsupported("non-equality equation (reinit…)");
      return;
    }
    Id<Expr> a = inst_exp(eq.a, enclosing, prefix);
    Id<Expr> b = inst_exp(eq.b, enclosing, prefix);
    if (result_.unsupported) return;
    // 数组方程：两侧同形数组 → 行优先逐元素标量化（NFScalarize.scalarizeEquation）；否则走标量路。
    if (result_.model.exprs[a].ty.is_array() || result_.model.exprs[b].ty.is_array()) {
      collect_array_equation(a, b, eq.loc, eq.cmt, is_initial);
      return;
    }
    a = simplify_expr(a);
    b = simplify_expr(b);
    // 用户数组字面量方程 `x = {…}`（标量 x）等，逐元素定型未做 → SKIP。
    if (result_.model.exprs[a].kind == ExprKind::Array || result_.model.exprs[b].kind == ExprKind::Array) {
      set_unsupported("array equation");
      return;
    }
    emit_scalar_equality(a, b, eq.loc, eq.cmt, is_initial);
  }

  // 发射一条标量等式（含两侧整型↔实型强制，R14：eq1 `x = 2`→`x = 2.0`）。数组方程逐元素也调它。
  void emit_scalar_equality(Id<Expr> a, Id<Expr> b, SourceLoc loc, const SComment& cmt, bool is_initial) {
    const TypeKind ta = result_.model.exprs[a].ty.kind, tb = result_.model.exprs[b].ty.kind;
    if (ta == TypeKind::Real && tb == TypeKind::Integer) b = to_real(b);
    else if (ta == TypeKind::Integer && tb == TypeKind::Real) a = to_real(a);
    FlatEquation fe;
    fe.kind = FlatEqKind::Equality;
    fe.a = a;
    fe.b = b;
    fe.loc = loc;
    if (cmt.str.is_valid()) fe.comment = in_.intern(unescape_modelica_string(in_.str(cmt.str)));
    Id<FlatEquation> id = result_.model.add_eq(std::move(fe));
    if (is_initial) result_.model.initial_equations.push_back(id);
    else result_.model.equations.push_back(id);
  }

  static bool same_dims(const std::vector<Dimension>& x, const std::vector<Dimension>& y) {
    if (x.size() != y.size()) return false;
    for (std::size_t i = 0; i < x.size(); ++i)
      if (x[i].kind != DimKind::Integer || y[i].kind != DimKind::Integer || x[i].size != y[i].size) return false;
    return true;
  }

  // 数组方程 `lhs = rhs`（两侧同形数组）→ 行优先枚举 + 逐元素标量化 + 逐元素标量等式（对标 NFScalarize.scalarizeEquation）。
  void collect_array_equation(Id<Expr> a, Id<Expr> b, SourceLoc loc, const SComment& cmt, bool is_initial) {
    const Type aty = result_.model.exprs[a].ty, bty = result_.model.exprs[b].ty;   // 拷贝，跨 add 稳定
    // 一侧数组一侧标量 = omc 类型错；形状不一致同理 → SKIP（保 FAIL=0）。
    if (!aty.is_array() || !bty.is_array() || !same_dims(aty.dims, bty.dims)) {
      set_unsupported("array equation shape mismatch");
      return;
    }
    // 复合数组前缀（enclosing 带 `a[i]` 段下标）与叶数组标量化的组合本增量暂缓（eq2/eq3）→ SKIP。
    for (const std::vector<std::int64_t>& s : cur_psubs_)
      if (!s.empty()) { set_unsupported("array equation under array prefix"); return; }
    std::vector<std::int64_t> dims;
    for (const Dimension& d : aty.dims) dims.push_back(d.size);
    std::vector<std::int64_t> idx(dims.size(), 1);
    bool more = !dims.empty();
    while (more) {
      Id<Expr> ai = simplify_expr(scalarize(a, idx));
      Id<Expr> bi = simplify_expr(scalarize(b, idx));
      if (result_.unsupported) return;
      emit_scalar_equality(ai, bi, loc, cmt, is_initial);
      std::size_t k = dims.size();   // 行优先递增（最后一维最快）
      while (k > 0) { --k; if (++idx[k] <= dims[k]) break; idx[k] = 1; if (k == 0) more = false; }
    }
  }

  // 把类型化的数组表达式 e 在多下标 idx 处标量化成一个标量 Expr（标量子表达式=广播操作数/字面量，原样返回）。
  Id<Expr> scalarize(Id<Expr> e, const std::vector<std::int64_t>& idx) {
    const Expr x = result_.model.exprs[e];   // 值拷贝：后续 add realloc 不失效
    if (x.ty.is_scalar()) return e;
    switch (x.kind) {
      case ExprKind::Cref: {
        Expr c = x;
        c.ty = x.ty.element_type();
        std::vector<Id<Expr>> subs;
        subs.reserve(idx.size());
        for (std::int64_t v : idx) subs.push_back(lit_int(v));
        c.cref.parts.back().subscripts = std::move(subs);   // 末段补下标（Phase 2 保证整体引用末段无下标）
        return add(std::move(c));
      }
      case ExprKind::Binary: {
        Id<Expr> sa = scalarize(x.a, idx), sb = scalarize(x.b, idx);
        const bool real = x.bin == BinOp::Div || result_.model.exprs[sa].ty.kind == TypeKind::Real ||
                          result_.model.exprs[sb].ty.kind == TypeKind::Real;
        if (real) { sa = to_real(sa); sb = to_real(sb); }
        Expr r; r.kind = ExprKind::Binary; r.bin = x.bin; r.a = sa; r.b = sb;
        r.ty = real ? Type::real() : Type::integer();
        return add(std::move(r));
      }
      case ExprKind::Unary: {
        Id<Expr> sa = scalarize(x.a, idx);
        Expr r; r.kind = ExprKind::Unary; r.un = x.un; r.a = sa; r.ty = result_.model.exprs[sa].ty;
        return add(std::move(r));
      }
      case ExprKind::LogicalNot: {
        Id<Expr> sa = scalarize(x.a, idx);
        Expr r; r.kind = ExprKind::LogicalNot; r.a = sa; r.ty = Type::boolean();
        return add(std::move(r));
      }
      case ExprKind::Logical: {
        Id<Expr> sa = scalarize(x.a, idx), sb = scalarize(x.b, idx);
        Expr r; r.kind = ExprKind::Logical; r.log = x.log; r.a = sa; r.b = sb; r.ty = Type::boolean();
        return add(std::move(r));
      }
      default:
        set_unsupported("array expression scalarization");
        return lit_int(0);
    }
  }

  // 数组二元算术（逐元素/标量广播；结果维取自数组操作数）。矩阵乘/除/幂（非点号、两侧皆数组）非逐元素 → SKIP。
  Id<Expr> array_binary(Op orig, BinOp op, Id<Expr> a, Id<Expr> b) {
    const Type ta = result_.model.exprs[a].ty, tb = result_.model.exprs[b].ty;
    const bool aarr = ta.is_array(), barr = tb.is_array();
    const TypeKind ea = ta.element_type().kind, eb = tb.element_type().kind;
    auto numeric = [](TypeKind k) { return k == TypeKind::Integer || k == TypeKind::Real; };
    // 非内建数值元素（记录数组 + 运算符重载 OperatorOverloadArrayOp*）→ 需重载算子展开，未做 → SKIP。
    if (!numeric(ea) || !numeric(eb)) { set_unsupported("non-numeric array operand"); return lit_int(0); }
    // 非逐元素 / 暂缓：矩阵乘（* 两侧皆数组）、矩阵幂（^ 含数组，OperationPow2/3）、除法（/ ./ 需操作数提实型，Tier C 暂缓）。
    if (orig == Op::Pow) { set_unsupported("array matrix power"); return lit_int(0); }
    if ((orig == Op::Mul || orig == Op::Div) && aarr && barr) {   // 矩阵乘 / 数组÷数组（非点号）非逐元素 → SKIP
      set_unsupported("non-elementwise array operator");
      return lit_int(0);
    }
    std::vector<Dimension> ds;
    if (aarr && barr) {
      if (!same_dims(ta.dims, tb.dims)) { set_unsupported("array shape mismatch"); return lit_int(0); }
      ds = ta.dims;
    } else {
      ds = aarr ? ta.dims : tb.dims;
    }
    const bool real = op == BinOp::Div || ea == TypeKind::Real || eb == TypeKind::Real;   // `/` `./` 恒实除
    Expr r;
    r.kind = ExprKind::Binary;
    r.bin = op;
    r.a = a;
    r.b = b;
    r.ty = Type::array_of(real ? Type::real() : Type::integer(), std::move(ds));
    return add(std::move(r));
  }

  // 数组逻辑（and/or 逐元素/广播，结果 Boolean 数组）。
  Id<Expr> array_logical(LogOp lop, Id<Expr> a, Id<Expr> b) {
    const Type ta = result_.model.exprs[a].ty, tb = result_.model.exprs[b].ty;
    if (ta.element_type().kind != TypeKind::Boolean || tb.element_type().kind != TypeKind::Boolean) {
      set_unsupported("non-boolean array logical");
      return lit_int(0);
    }
    std::vector<Dimension> ds;
    if (ta.is_array() && tb.is_array()) {
      if (!same_dims(ta.dims, tb.dims)) { set_unsupported("array shape mismatch"); return lit_int(0); }
      ds = ta.dims;
    } else {
      ds = ta.is_array() ? ta.dims : tb.dims;
    }
    Expr r;
    r.kind = ExprKind::Logical;
    r.log = lop;
    r.a = a;
    r.b = b;
    r.ty = Type::array_of(Type::boolean(), std::move(ds));
    return add(std::move(r));
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

  // 收集 if 分支体成独立的 flat 方程序列：体方程照常 collect_equation（追加到主方程表），再把新增尾段搬出。
  // in_if_ 期内禁 connect（分支体里的 connect 未做）；save/restore 支持嵌套 if。
  std::vector<Id<FlatEquation>> collect_if_body(const std::vector<Id<SEquation>>& body, NodeId enclosing,
                                                const std::vector<Symbol>& prefix, const PSubs& psubs,
                                                bool is_initial) {
    std::vector<Id<FlatEquation>>& sink = is_initial ? result_.model.initial_equations : result_.model.equations;
    const std::size_t start = sink.size();
    const bool saved = in_if_;
    in_if_ = true;
    for (Id<SEquation> beid : body) {
      collect_equation(beid, enclosing, prefix, psubs, is_initial);
      if (result_.unsupported) break;
    }
    in_if_ = saved;
    std::vector<Id<FlatEquation>> out(sink.begin() + start, sink.end());
    sink.erase(sink.begin() + start, sink.end());
    return out;
  }

  // 条件是否为运行时（可作为 flat if 保留）：引用 time（未解析）或连续/离散变量，或残留调用。参数/常量 → false（SKIP）。
  bool cond_is_runtime(Id<Expr> id) {
    const Expr& e = result_.model.exprs[id];
    switch (e.kind) {
      case ExprKind::Cref: {
        const std::uint32_t raw = e.cref.resolved_node;
        if (raw == Id<INode>::kInvalid) return true;   // time / 内建 → 运行时
        const NodeId n = NodeId::from_index(raw);
        if (I_.nodes.contains(n) && !I_.nodes[n].is_empty() && I_.nodes[n].comp.is_valid()) {
          const SVariability v = I_.comps[I_.nodes[n].comp].attributes.variability;
          return v == SVariability::Var || v == SVariability::Discrete;
        }
        return true;
      }
      case ExprKind::Relation:
      case ExprKind::Logical:
      case ExprKind::Binary:
        return cond_is_runtime(e.a) || cond_is_runtime(e.b);
      case ExprKind::Unary:
      case ExprKind::LogicalNot:
      case ExprKind::Cast:
        return cond_is_runtime(e.a);
      case ExprKind::IfExp:
        return cond_is_runtime(e.a) || cond_is_runtime(e.b) || cond_is_runtime(e.c);
      case ExprKind::Call: return true;
      default: return false;
    }
  }

  // ---- FlatVariable 子下标（类型化 Expr）转换 + 结构比较（对标 NFSubscript：下标是表达式）----
  // 数组标量化下标只可能是 IntLit/BoolLit/EnumLit；去重比较按值（Id 恒等不够，独立构造的 IntLit 1 Id 不同）。
  std::vector<Id<Expr>> idx_to_exprs(const std::vector<std::int64_t>& idx) {
    std::vector<Id<Expr>> out;
    out.reserve(idx.size());
    for (std::int64_t v : idx) out.push_back(lit_int(v));
    return out;
  }
  // 行主序推进下标元组（1 基，最后一维最快）；走完返回 false。dims 空（标量）→ 立即 false（do-while 恰跑一次）。
  static bool next_index(std::vector<std::int64_t>& idx, const std::vector<std::int64_t>& dims) {
    std::size_t k = idx.size();
    while (k > 0) {
      --k;
      if (++idx[k] <= dims[k]) return true;
      idx[k] = 1;
    }
    return false;
  }
  std::vector<std::vector<Id<Expr>>> psubs_to_exprs(const PSubs& ps) {
    std::vector<std::vector<Id<Expr>>> out;
    out.reserve(ps.size());
    for (const std::vector<std::int64_t>& seg : ps) out.push_back(idx_to_exprs(seg));
    return out;
  }
  bool sub_expr_eq(Id<Expr> a, Id<Expr> b) {
    if (a == b) return true;
    const Expr& ea = result_.model.exprs[a];
    const Expr& eb = result_.model.exprs[b];
    if (ea.kind != eb.kind) return false;
    switch (ea.kind) {
      case ExprKind::IntLit: return ea.ival == eb.ival;
      case ExprKind::BoolLit: return ea.bval == eb.bval;
      case ExprKind::EnumLit: return ea.enum_path == eb.enum_path;
      default: return false;
    }
  }
  // Expression.isEqual（结构相等）：同 kind、同值 / 同运算、子式递归相等（simplifyIf 的"两支相等"用）。
  bool expr_equal(Id<Expr> a, Id<Expr> b) {
    if (a == b) return true;
    if (!a.is_valid() || !b.is_valid()) return false;
    const Expr& x = result_.model.exprs[a];
    const Expr& y = result_.model.exprs[b];
    if (x.kind != y.kind) return false;
    auto list_eq = [&](const std::vector<Id<Expr>>& p, const std::vector<Id<Expr>>& q) {
      if (p.size() != q.size()) return false;
      for (std::size_t i = 0; i < p.size(); ++i) if (!expr_equal(p[i], q[i])) return false;
      return true;
    };
    switch (x.kind) {
      case ExprKind::IntLit: return x.ival == y.ival;
      case ExprKind::RealLit: return x.rval == y.rval;
      case ExprKind::BoolLit: return x.bval == y.bval;
      case ExprKind::StringLit: return x.sval == y.sval;
      case ExprKind::EnumLit: return x.enum_path == y.enum_path;
      case ExprKind::Cref:
        if (x.cref.parts.size() != y.cref.parts.size()) return false;
        for (std::size_t i = 0; i < x.cref.parts.size(); ++i)
          if (x.cref.parts[i].name != y.cref.parts[i].name || !list_eq(x.cref.parts[i].subscripts, y.cref.parts[i].subscripts))
            return false;
        return true;
      case ExprKind::Binary: return x.bin == y.bin && expr_equal(x.a, y.a) && expr_equal(x.b, y.b);
      case ExprKind::Relation: return x.rel == y.rel && expr_equal(x.a, y.a) && expr_equal(x.b, y.b);
      case ExprKind::Logical: return x.log == y.log && expr_equal(x.a, y.a) && expr_equal(x.b, y.b);
      case ExprKind::Unary: return x.un == y.un && expr_equal(x.a, y.a);
      case ExprKind::LogicalNot:
      case ExprKind::Cast: return expr_equal(x.a, y.a);
      case ExprKind::IfExp: return expr_equal(x.a, y.a) && expr_equal(x.b, y.b) && expr_equal(x.c, y.c);
      case ExprKind::Call: return x.call_name == y.call_name && list_eq(x.args, y.args);
      case ExprKind::Array: return list_eq(x.args, y.args);
      default: return false;
    }
  }
  bool subs_eq(const std::vector<Id<Expr>>& a, const std::vector<Id<Expr>>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t k = 0; k < a.size(); ++k) if (!sub_expr_eq(a[k], b[k])) return false;
    return true;
  }
  bool name_subs_eq(const std::vector<std::vector<Id<Expr>>>& a, const std::vector<std::vector<Id<Expr>>>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t k = 0; k < a.size(); ++k) if (!subs_eq(a[k], b[k])) return false;
    return true;
  }

  void emit_if(std::vector<FlatEqBranch> branches, std::vector<Id<FlatEquation>> else_body,
               SourceLoc loc, bool is_initial) {
    FlatEquation fe;
    fe.kind = FlatEqKind::If;
    fe.branches = std::move(branches);
    fe.else_body = std::move(else_body);
    fe.loc = loc;
    Id<FlatEquation> id = result_.model.add_eq(std::move(fe));
    if (is_initial) result_.model.initial_equations.push_back(id);
    else result_.model.equations.push_back(id);
  }

  // mod_subs：命中本叶组件名的外来子修饰（优先级序），其 .binding 覆盖声明绑定、.subs 覆盖声明属性；
  // mod_scope/mod_prefix：这些修饰值的解析作用域（书写处）。force_final：上层 final 传播。
  void emit_variable(CompId cid, const std::vector<Symbol>& name, bool is_protected,
                     const std::vector<Symbol>& prefix, const PSubs& psubs, NodeId enclosing,
                     const std::vector<Id<SMod>>& mod_subs, NodeId mod_scope,
                     const std::vector<Symbol>& mod_prefix, bool force_final,
                     const std::unordered_set<std::uint32_t>& final_params,
                     SVariability var_floor, const ModCtx& ctx) {
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
    // stream 变量：须在连接器内部，且该连接器恰有一个 flow（associatedFlowCref，NFConnectEquations.mo:1106-1126；
    // 否则 omc 报错 InvalidConnectorType3/4/6）。stream 前缀不打印（R13 §3.1）；连接方程 / inStream 在 process_connects 后求。
    if (comp.attributes.connector == SConnectorType::Stream && !(ctx.in_conn && single_flow_name(enclosing)))
      set_unsupported("stream connector");
    const bool in_conn = ctx.in_conn;   // 连接器内部（含连接器里的记录字段）
    // `flow` 在连接器里生成 `=0`；非连接器里的 flow 前缀 OMC 告警后当普通变量——ymc 不产告警文本，
    // 与期望的 Warning 行对不上 → SKIP（NonConnectorFlow1）。
    if (comp.attributes.connector == SConnectorType::Flow && !in_conn) set_unsupported("flow outside connector");
    // 连接器里 `flow R f` 的字段继承 flow（ConnectorType.merge，NFAttributes.mo:229-234）。
    const bool is_flow = in_conn && (comp.attributes.connector == SConnectorType::Flow || ctx.flow_inherited);
    FlatVariable v;
    v.name = name;
    std::vector<std::vector<Id<Expr>>> nsubs = psubs_to_exprs(psubs);
    nsubs.push_back({});   // 叶段（标量）无逐段下标；与 name 平行
    v.name_subs = nsubs;
    v.ty = comp.ty;
    v.is_protected = is_protected;
    v.is_final = comp.attributes.is_final || force_final || struct_final;
    switch (var_min(var_floor, comp.attributes.variability)) {   // 复合组件变率下沉（Half A）
      case SVariability::Var: v.var_kind = FlatVarKind::Var; break;
      case SVariability::Discrete: v.var_kind = FlatVarKind::Discrete; break;
      case SVariability::Param: v.var_kind = FlatVarKind::Parameter; break;
      case SVariability::Const: v.var_kind = FlatVarKind::Constant; break;
    }
    // 方向保留：所有前缀组件都是连接器或 input 组件（顶层变量、顶层连接器成员、顶层 input 组件成员）才打印
    // input/output，否则抹掉（Variable.removeNonTopLevelDirection，NFVariable.mo:466-493；Direction1）。
    if (ctx.keep_dir) {
      if (comp.attributes.direction == SDirection::Input) v.direction = FlatDirection::Input;
      else if (comp.attributes.direction == SDirection::Output) v.direction = FlatDirection::Output;
    }
    // 无绑定的 parameter（NFFlatten.verifyBinding :812-875；非 relaxed 模式才查，:742-746，普通翻译即非 relaxed，
    // NFInst.mo:167-168）。fixed 缺省 true：fixed 时有 start → omc 以 start 作默认绑定并告警（未做 → SKIP）；无 start →
    // UNBOUND_PARAMETER_ERROR 报错（UnboundParameter1 / InvalidVariability1/2 / InvalidComplexConnectorType1/2）→ SKIP；
    // fixed = false → 合法照印（YmcUnfixed1 探针）。fixed 非字面量（omc 先 tryEvalExp）未做 → SKIP。
    // 注：源码 HEAD 另有 constant 无绑定报错（NO_CONSTANT_BINDING :844-848），1.25.0 实跑不报、照印 `constant Integer n;`
    // （YmcUnbConst1 探针）→ 按 1.25.0 不查。
    if (v.var_kind == FlatVarKind::Parameter && !binding.is_valid()) {
      const Symbol start = in_.intern("start"), fixed = in_.intern("fixed");
      bool has_start = false, is_fixed = true, fixed_lit = true;
      for (const FlatVarAttr& a : attrs) {
        if (a.name == start) has_start = true;
        if (a.name == fixed) {
          const Expr& fx = result_.model.exprs[a.value];
          if (fx.kind == ExprKind::BoolLit) is_fixed = fx.bval; else fixed_lit = false;
        }
      }
      if (!fixed_lit) set_unsupported("unbound parameter with non-literal fixed");
      else if (is_fixed) set_unsupported(has_start ? "unbound parameter default from start" : "unbound parameter");
    }
    // constant 组件的绑定 omc 一定求值成字面量（R14 §3）；绑定若还是表达式，说明要 evalConstants，v1 没做。
    if (v.var_kind == FlatVarKind::Constant && binding.is_valid()) {
      ExprKind bk = result_.model.exprs[binding].kind;
      bool is_lit = bk == ExprKind::IntLit || bk == ExprKind::RealLit || bk == ExprKind::BoolLit ||
                    bk == ExprKind::StringLit || bk == ExprKind::EnumLit;
      if (!is_lit) set_unsupported("constant evaluation");
    }
    // Boolean 变量绑定到（未折叠的）noEvent(…)：noEvent 抑制事件 → 连续，绑到离散 Boolean 是错误（omc 报错，NoEvent1）→ SKIP。
    // 普通关系 `b = x > 1` 会产生事件→离散，合法，不在此列。
    if (binding.is_valid() && cty.kind == TypeKind::Boolean &&
        result_.model.exprs[binding].kind == ExprKind::Call) {
      const std::vector<Symbol>& cn = result_.model.exprs[binding].call_name;
      if (cn.size() == 1 && in_.str(cn[0]) == "noEvent") set_unsupported("noEvent bound to discrete variable");
    }
    v.binding = binding;
    v.attributes = std::move(attrs);
    v.loc = comp.loc;
    // 组件声明注释（R13 §3.6）：`Real x "c";`。存原文（去转义），dumper 打印时再转义（与方程/语句注释同法）。
    if (I_.scode.elems[comp.def].cmt.str.is_valid())
      v.comment = in_.intern(unescape_modelica_string(in_.str(I_.scode.elems[comp.def].cmt.str)));
    result_.model.variables.push_back(std::move(v));
    const std::size_t vidx = result_.model.variables.size() - 1;
    if (!cur_pp_chain_.empty()) var_pp_.emplace(vidx, cur_pp_chain_);
    // 未连接 flow → `name = 0.0`；`=0` 方程要排在本类方程之后（R13 §6），故先攒起来，run() 末尾统一追加。
    // nsubs 带元素下标（连接器数组 `c[i].f`）；与 name 平行，标量连接器时全空 → 打印无下标（旧行为）。
    if (is_flow) flow_vars_.push_back({name, nsubs, cty, comp.loc, vidx, false});
  }

  // 数组叶组件标量化（R13 §3/§4）：字面量维度 → 行主序逐元素变量行（无绑定/属性）；数组绑定 `= {…}` 变成
  // 一条数组方程 `name = {…}`（omc 默认不把绑定摊到每元素，见 Subscript1）。v1 SKIP：非字面量维度、数组属性修饰、
  // 非数组字面量的数组绑定。
  void emit_array(CompId cid, const std::vector<Symbol>& name, bool is_protected,
                  const std::vector<Symbol>& prefix, const PSubs& psubs, NodeId enclosing,
                  const std::vector<Id<SMod>>& mod_subs, NodeId mod_scope,
                  const std::vector<Symbol>& mod_prefix, bool force_final, const SElement& cel,
                  const ModCtx& ctx) {
    cur_psubs_ = psubs;   // 数组绑定/摊分里 cref 前缀改写用
    // 维度：组件维在前、类型维在后（R13 §3）。字面量 + 参数/常量/size()/算术求值。
    const ScodeScope dscope = scode_scope_of(enclosing);
    // 维度表达式：组件维在前、类型维在后（R13 §3）。先收集，绑定就绪后再解析（`:` 维靠绑定形状推断）。
    std::vector<Id<Exp>> dim_exps;
    for (Id<Exp> d : cel.attr.dims) dim_exps.push_back(d);
    for (Id<Exp> d : cel.type.dims) dim_exps.push_back(d);
    if (dim_exps.empty()) { set_unsupported("array dimension"); return; }
    // 属性修饰（逐元素分配 / SPLIT_PROXY，R15 §4）v1 未做。
    if (cel.mod.is_valid() && !I_.scode.mods[cel.mod].subs.empty()) { set_unsupported("array attribute modifier"); return; }
    for (Id<SMod> m : mod_subs)
      if (!I_.scode.mods[m].subs.empty()) { set_unsupported("array attribute modifier"); return; }
    if (!I_.comps[cid].derived_mods.empty()) { set_unsupported("array derived-type modifier"); return; }
    // 绑定：外来修饰优先，否则声明绑定。要求是数组构造子 `{…}` / `array(…)`，否则 SKIP。
    Id<Exp> braw;
    for (Id<SMod> m : mod_subs)
      if (I_.scode.mods[m].binding.is_valid()) { braw = I_.scode.mods[m].binding; break; }
    NodeId bscope = mod_scope;
    std::vector<Symbol> bprefix = mod_prefix;
    if (!braw.is_valid()) {
      const Binding& b = I_.comps[cid].binding;
      if (b.kind == BindingKind::Raw && b.raw_exp.is_valid()) { braw = b.raw_exp; bscope = enclosing; bprefix = prefix; }
    }
    const bool nlbind = braw.is_valid() && !is_array_ctor(braw);   // 非构造子绑定（= y / = 2*y / …），下面类型化摊分
    // 顶层 input/output 数组带绑定：omc 丢方向+绑定（标量不丢，探针实测），该 quirk 未复刻 → SKIP（TopLevelInputs2）。
    if (braw.is_valid() && prefix.empty() &&
        (I_.comps[cid].attributes.direction == SDirection::Input ||
         I_.comps[cid].attributes.direction == SDirection::Output)) {
      set_unsupported("top-level directional array with binding"); return;
    }
    // 解析各维成整数：字面量/参数/size()/算术走 eval_dim；`:` 维从绑定构造子形状推断（CevalArray*）。
    std::vector<std::int64_t> dims;
    std::vector<std::vector<Id<Expr>>> dim_labels;   // 每维下标标签：整数维 IntLit 1..n；类型维 false/true 或 E.lit
    for (std::size_t di = 0; di < dim_exps.size(); ++di) {
      std::vector<Id<Expr>> labels;
      if (auto tl = classify_type_dim(dim_exps[di], enclosing)) {
        labels = std::move(*tl);   // 类型名维（Boolean / 枚举，NFDimension.fromExp）
      } else if (result_.unsupported) {
        return;
      } else if (I_.absyn.exps[dim_exps[di]].kind == ExpKind::Colon) {
        std::optional<std::int64_t> n = binding_dim_size(braw, di);
        if (!n) { set_unsupported("non-literal array dimension"); return; }
        for (std::int64_t i = 1; i <= *n; ++i) labels.push_back(lit_int(i));
      } else {
        std::vector<std::int64_t> one;
        if (!eval_dim(dim_exps[di], one, dscope) || one.size() != 1) {
          set_unsupported("non-literal array dimension"); return;
        }
        for (std::int64_t i = 1; i <= one[0]; ++i) labels.push_back(lit_int(i));
      }
      dims.push_back(static_cast<std::int64_t>(labels.size()));
      dim_labels.push_back(std::move(labels));
    }
    // 空数组（某维为 0）：omc 整体丢弃（无变量行）——本增量未做该语义 → SKIP（EmptyArray*/矩阵含 0 维）。
    for (std::int64_t n : dims) if (n == 0) { set_unsupported("empty array"); return; }

    const Type cty = I_.comps[cid].ty;
    // 连接器里的数组成员（`flow Real f[2]`）：与 emit_variable 同一套守卫（stream / 成员方向 / 连接器外 flow），
    // flow 元素攒进 flow_vars_（未连接 → `f[k] = 0.0`，collectFlows 按变量声明序，NFConnections.mo:139-166）。
    if (I_.comps[cid].attributes.connector == SConnectorType::Stream) { set_unsupported("stream connector"); return; }
    const bool in_conn = ctx.in_conn;   // 连接器内部（含连接器里的记录字段）
    if (I_.comps[cid].attributes.connector == SConnectorType::Flow && !in_conn) { set_unsupported("flow outside connector"); return; }
    const bool is_flow = in_conn && (I_.comps[cid].attributes.connector == SConnectorType::Flow || ctx.flow_inherited);
    FlatVarKind vk = FlatVarKind::Var;
    switch (I_.comps[cid].attributes.variability) {
      case SVariability::Var: vk = FlatVarKind::Var; break;
      case SVariability::Discrete: vk = FlatVarKind::Discrete; break;
      case SVariability::Param: vk = FlatVarKind::Parameter; break;
      case SVariability::Const: vk = FlatVarKind::Constant; break;
    }
    // 无绑定的 parameter 数组：verifyBinding（NFFlatten.mo:812-875）报错（数组无属性修饰可达此处 → fixed 缺省 true、无 start；
    // YmcUnbArr1 探针）→ SKIP。
    if (vk == FlatVarKind::Parameter && !braw.is_valid()) { set_unsupported("unbound parameter"); return; }
    // 参数/常量数组绑定 omc 摊到每元素（`x[1]=1.0`…），连续（Var）数组才变成数组方程（`x = {…}`，Subscript1）。
    // v1 摊分只做"字面量元素"（下降 Array 到标量）；元素含 cref 数组等 → 逐元素时命中会 SKIP。
    const bool spread = braw.is_valid() && vk != FlatVarKind::Var;
    // 数组字面量绑定的形状/类型要与数组匹配（omc 会做类型检查并报错，如 BindingInvalidType3/5）——ymc 无验证 pass，
    // 只放行形状+叶类型都对上的字面量绑定，否则 SKIP。
    if (braw.is_valid() && !nlbind && !array_lit_matches(braw, dims, 0, cty.kind)) {
      set_unsupported("array binding shape/type mismatch"); return;
    }
    // 非构造子绑定（数组值 `= y` / `= 2*y`）：类型化一次 + 形状/变率守卫（对标 NFFlatten:700 / NFScalarize）。
    Id<Expr> rhsE;
    std::vector<Dimension> xd;
    if (nlbind) {
      for (std::int64_t n : dims) { Dimension d; d.kind = DimKind::Integer; d.size = n; xd.push_back(d); }
      rhsE = simplify_expr(inst_exp(braw, bscope, bprefix));
      if (result_.unsupported) return;   // inst_exp 已拒绝切片/常量数组/限定名等
      const Type rty = result_.model.exprs[rhsE].ty;
      if (!rty.is_array() || !same_dims(rty.dims, xd)) { set_unsupported("non-literal array binding shape"); return; }
      if (vk == FlatVarKind::Discrete) { set_unsupported("discrete array binding"); return; }   // omc 报错（需 when）
      if (vk != FlatVarKind::Var && cond_is_runtime(rhsE)) {   // param/const 绑更高变率 = omc 报错（B3/C3）
        set_unsupported("array binding higher variability"); return;
      }
    }
    const bool is_final = I_.comps[cid].attributes.is_final || force_final;
    // 方向保留：所有前缀组件都是连接器或 input 组件（Variable.removeNonTopLevelDirection，NFVariable.mo:466-493）。
    FlatDirection dir = FlatDirection::None;
    if (ctx.keep_dir) {
      if (I_.comps[cid].attributes.direction == SDirection::Input) dir = FlatDirection::Input;
      else if (I_.comps[cid].attributes.direction == SDirection::Output) dir = FlatDirection::Output;
    }

    // 行主序枚举下标元组（1 基），逐元素产出变量行；param/const 数组把绑定摊到每元素。
    std::vector<std::int64_t> idx(dims.size(), 1);
    for (bool more = true; more;) {
      FlatVariable v;
      v.name = name;
      v.name_subs = psubs_to_exprs(psubs);
      v.name_subs.push_back({});   // 叶数组的下标走 v.subscripts（末段），逐段 name_subs 末段留空
      {   // 末段下标：整数维取 IntLit，类型维取 false/true·E.lit（行主序 idx → 该维标签）
        std::vector<Id<Expr>> subs;
        subs.reserve(idx.size());
        for (std::size_t k = 0; k < idx.size(); ++k) subs.push_back(dim_labels[k][idx[k] - 1]);
        v.subscripts = std::move(subs);
      }
      v.ty = cty;
      v.is_protected = is_protected;
      v.is_final = is_final;
      v.var_kind = vk;
      v.direction = dir;
      v.loc = I_.comps[cid].loc;
      if (spread) {
        Id<Expr> eb;
        if (nlbind) {
          eb = simplify_expr(scalarize(rhsE, idx));   // 逐元素：y[i] / 2.0*y[i]（B1/B2/B4/B5/B8）
          if (result_.unsupported) return;
        } else {
          // 该下标处的字面量元素（下降 Array 到标量）；命中 cref 数组等 → 本增量 SKIP。
          Id<Exp> el = array_scalar_elem(braw, idx, 0);
          if (!el.is_valid()) { set_unsupported("array binding element spread"); return; }
          eb = simplify_expr(inst_exp(el, bscope, bprefix));
        }
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
      // flow 元素：逐段下标 + 末段元素下标放 subs 末槽（flow=0 发射器逐段打印；conn_key 取连接器 key 时去掉末槽）。
      std::vector<std::vector<Id<Expr>>> fsubs;
      if (is_flow) { fsubs = v.name_subs; fsubs.back() = v.subscripts; }
      result_.model.variables.push_back(std::move(v));
      const std::size_t vidx = result_.model.variables.size() - 1;
      if (!cur_pp_chain_.empty()) var_pp_.emplace(vidx, cur_pp_chain_);
      if (is_flow) flow_vars_.push_back({name, std::move(fsubs), cty, I_.comps[cid].loc, vidx, false});
      // 递增（最后一维最快）。
      std::size_t k = dims.size();
      while (k > 0) {
        --k;
        if (++idx[k] <= dims[k]) break;
        idx[k] = 1;
        if (k == 0) more = false;
      }
    }

    // Var 数组绑定 → 单条数组方程（param/const 已在上面摊成逐元素绑定）。
    // 构造子：`name = {…}`；非构造子：整取同型 cref → `name = y`（保持整取，NFScalarize ARRAY_EQUALITY 不标量化）；
    // 逐元素算子 → 展开成 `{op(y[1]),…}`（NFExpandExp）；整取 Int→Real（数组 cast）/Call/Range → SKIP。
    if (braw.is_valid() && !spread) {
      Id<Expr> rhs;
      if (nlbind) {
        const Expr& re = result_.model.exprs[rhsE];
        const ExprKind rk = re.kind;
        const bool whole_cref = rk == ExprKind::Cref && (re.cref.parts.empty() || re.cref.parts.back().subscripts.empty());
        const TypeKind relem = re.ty.element_type().kind;
        if (whole_cref && relem == cty.kind) {
          rhs = rhsE;   // x = y（同元素类型，A1/A5/A9/I1）
        } else if (rk == ExprKind::Binary || rk == ExprKind::Unary ||
                   rk == ExprKind::Logical || rk == ExprKind::LogicalNot) {
          std::vector<Id<Expr>> elems;   // 逐元素展开 {op(y[1]), …}（A2/A3/A8/G3/G4）
          std::vector<std::int64_t> eidx(dims.size(), 1);
          for (bool more = true; more;) {
            Id<Expr> se = simplify_expr(scalarize(rhsE, eidx));
            if (result_.unsupported) return;
            if (cty.kind == TypeKind::Real) se = to_real(se);
            elems.push_back(se);
            std::size_t k = dims.size();
            while (k > 0) { --k; if (++eidx[k] <= dims[k]) break; eidx[k] = 1; if (k == 0) more = false; }
          }
          Expr arr;
          arr.kind = ExprKind::Array;
          arr.ty = Type::array_of(cty, xd);
          arr.args = std::move(elems);
          rhs = add(std::move(arr));
        } else {
          set_unsupported("non-literal array binding"); return;   // 整取 Int→Real(A6)、Call、Range → SKIP
        }
      } else {
        rhs = coerce_deep(simplify_expr(inst_exp(braw, bscope, bprefix)), cty.kind);
      }
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
    if (is_array_ctor(e)) {   // `{…}` 或 `array(…)`
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
      if (!is_array_ctor(b) || static_cast<std::int64_t>(e.args.size()) != dims[di]) return false;
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

  // 数组构造子：`{…}`（ExpKind::Array）或 `array(…)`（MLS §10.4.1.1 等价，元素同在 .args）。
  bool is_array_ctor(Id<Exp> e) {
    const Exp& x = I_.absyn.exps[e];
    if (x.kind == ExpKind::Array) return true;
    if (x.kind == ExpKind::Call) {
      const Cref& c = I_.absyn.crefs[x.cref];
      return c.parts.size() == 1 && c.parts[0].subscripts.empty() && in_.str(c.parts[0].name) == "array";
    }
    return false;
  }

  // 绑定构造子在第 di 维的元素个数（外层下降 di 层取该层 arg 数，假定矩形）；供 `:` 维从绑定推断形状。
  std::optional<std::int64_t> binding_dim_size(Id<Exp> b, std::size_t di) {
    if (!b.is_valid()) return std::nullopt;
    Id<Exp> cur = b;
    for (std::size_t k = 0; k < di; ++k) {
      if (!is_array_ctor(cur) || I_.absyn.exps[cur].args.empty()) return std::nullopt;
      cur = I_.absyn.exps[cur].args[0];
    }
    if (!is_array_ctor(cur)) return std::nullopt;
    return static_cast<std::int64_t>(I_.absyn.exps[cur].args.size());
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
  // 按简单名沿词法作用域链找类元素（任意 kind；走法同 find_enum_def），最后再查顶层 program（顶层声明的连接器短类，
  // Cardinality3）。继承/导入来的类不在链上 → nullptr（调用方 SKIP）。
  const SElement* find_class_elem(NodeId scope, Symbol name) {
    for (NodeId s = scope; s.is_valid() && !I_.nodes[s].is_empty(); s = I_.nodes[s].parent_scope) {
      const Id<SElement> d = I_.nodes[s].def;
      if (!d.is_valid()) continue;
      for (Id<SElement> eid : I_.scode.elems[d].def.elements) {
        const SElement& el = I_.scode.elems[eid];
        if (el.kind == SElementKind::Class && el.name == name) return &el;
      }
    }
    for (Id<SElement> eid : I_.scode.program) {
      const SElement& el = I_.scode.elems[eid];
      if (el.kind == SElementKind::Class && el.name == name) return &el;
    }
    return nullptr;
  }
  // 组件自身的方向前缀与短类链上的方向前缀冲突（`connector C = input Real; output C c3;`）：omc 报错
  //（Prefixes.mergeDirection，NFPrefixes.mo:588-604；派生链 allowSame=true，NFAttributes.mo:274 → 相同可、不同报错；conn8）。
  // 正确归宿是 inst.cpp 短类塌陷的属性合并（:436-438 只做"首个非默认者胜"）——用户接手时移过去；此处先按同规则 SKIP。
  // 限定类型名 / 找不到类 → 不查（下一跳在组件声明作用域链上找，近似）。
  bool conflicting_direction(const SElement& cel, NodeId scope) {
    if (cel.type.leading_dot || cel.type.path.size() != 1) return false;
    SDirection acc = cel.attr.dir;
    const SElement* tel = find_class_elem(scope, cel.type.path[0]);
    for (int guard = 0; tel && tel->def.kind == SClassDefKind::Derived && guard < 100; ++guard) {
      const SDirection d = tel->def.derived_attr.dir;
      if (d != SDirection::Bidir) {
        if (acc == SDirection::Bidir) acc = d;
        else if (acc != d) return true;
      }
      if (tel->def.derived_type.leading_dot || tel->def.derived_type.path.size() != 1) break;
      tel = find_class_elem(scope, tel->def.derived_type.path[0]);
    }
    return false;
  }

  // 类型名维度（`x[Boolean]` / `x[E]`，对标 NFDimension.fromExp:111 + NFTypeCheck.checkDimensionType）：
  // 返回该维的下标标签（Boolean→[false,true]；本地枚举→[E.lit…]，路径用书写名 E，与 omc 对本地类型打裸名一致）。
  // 非类型名维（整数/参数/`:`）→ nullopt（调用方走 eval_dim）。限定/别名/builtin 枚举维暂缓：nullopt（→ eval_dim 失败 SKIP）。
  std::optional<std::vector<Id<Expr>>> classify_type_dim(Id<Exp> d, NodeId enclosing) {
    const Exp& e = I_.absyn.exps[d];
    if (e.kind != ExpKind::CrefExp) return std::nullopt;
    const Cref& c = I_.absyn.crefs[e.cref];
    if (c.parts.size() != 1 || !c.parts[0].subscripts.empty()) return std::nullopt;   // 多段（Pk.E）/带下标暂缓
    const Symbol nm = c.parts[0].name;
    if (in_.str(nm) == "Boolean") {
      std::vector<Id<Expr>> labels;
      for (bool bv : {false, true}) {
        Expr r;
        r.kind = ExprKind::BoolLit;
        r.bval = bv;
        r.ty = Type::boolean();
        labels.push_back(add(std::move(r)));
      }
      return labels;
    }
    // 枚举字面量：本地 `type E=enumeration(…)` 或 builtin StateSelect（MLS §3.8.3，固定 5 值）。
    std::vector<Symbol> lits;
    if (in_.str(nm) == "StateSelect") {
      for (const char* s : {"never", "avoid", "default", "prefer", "always"}) lits.push_back(in_.intern(s));
    } else if (const SClassDef* ed = find_enum_def(enclosing, nm)) {
      for (const auto& it : ed->enum_items) lits.push_back(it.literal);
    } else {
      return std::nullopt;   // 不是类型名（普通整数维参数等）→ 走 eval_dim
    }
    std::vector<Id<Expr>> labels;
    for (std::size_t i = 0; i < lits.size(); ++i) {
      Expr r;
      r.kind = ExprKind::EnumLit;
      r.ty = Type::of(TypeKind::Enumeration);
      r.enum_path.push_back(nm);        // 书写名（本地类型打裸名 E；builtin 打 StateSelect）
      r.enum_path.push_back(lits[i]);
      r.enum_index = static_cast<std::int64_t>(i) + 1;
      labels.push_back(add(std::move(r)));
    }
    return labels;
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
  // flat 表达式是否引用 names 里的某个名字（首段）——供函数局部依赖拓扑排序。
  bool flat_exp_refs(Id<Expr> e, const std::unordered_set<std::uint32_t>& names) {
    const Expr& x = result_.model.exprs[e];
    switch (x.kind) {
      case ExprKind::Cref: {
        if (!x.cref.parts.empty() && names.count(x.cref.parts[0].name.id)) return true;
        for (const ExprCrefPart& p : x.cref.parts) for (Id<Expr> s : p.subscripts) if (flat_exp_refs(s, names)) return true;
        return false;
      }
      case ExprKind::Binary: case ExprKind::Relation: case ExprKind::Logical:
        return flat_exp_refs(x.a, names) || flat_exp_refs(x.b, names);
      case ExprKind::Unary: case ExprKind::LogicalNot: case ExprKind::Cast:
        return flat_exp_refs(x.a, names);
      case ExprKind::IfExp:
        return flat_exp_refs(x.a, names) || flat_exp_refs(x.b, names) || flat_exp_refs(x.c, names);
      case ExprKind::Call: case ExprKind::Array: case ExprKind::Tuple: case ExprKind::Range:
        if (x.a.is_valid() && flat_exp_refs(x.a, names)) return true;
        if (x.b.is_valid() && flat_exp_refs(x.b, names)) return true;
        if (x.c.is_valid() && flat_exp_refs(x.c, names)) return true;
        for (Id<Expr> arg : x.args) if (flat_exp_refs(arg, names)) return true;
        return false;
      default: return false;
    }
  }

  // 算法段是否有把 out 当赋值 LHS 的语句（输出经算法赋值判定，否则未赋值→omc 告警→SKIP）。
  bool algorithm_assigns(const SAlgorithm& alg, Symbol out) {
    for (Id<SStatement> sid : alg.stmts) {
      const SStatement& st = I_.scode.stmts[sid];
      if (st.kind != SStmtKind::Assign) continue;
      const Exp& lhs = I_.absyn.exps[st.a];
      if (lhs.kind != ExpKind::CrefExp) continue;
      const Cref& lc = I_.absyn.crefs[lhs.cref];
      if (lc.parts.size() == 1 && lc.parts[0].name == out) return true;
    }
    return false;
  }

  // 函数体语句翻译（首批：标量赋值 `局部/输出 := expr`）。目标须是 func_params_ 里的名字。
  Id<FlatStatement> collect_func_statement(Id<SStatement> sid, NodeId enclosing) {
    const SStatement& st = I_.scode.stmts[sid];
    if (st.kind != SStmtKind::Assign) { set_unsupported("function statement"); return {}; }
    Id<Expr> lhs = simplify_expr(inst_exp(st.a, enclosing, {}));
    if (result_.unsupported) return {};
    TypeKind ta;
    {
      const Expr& le = result_.model.exprs[lhs];
      if (le.kind != ExprKind::Cref || le.cref.parts.size() != 1 || !le.cref.parts[0].subscripts.empty()) {
        set_unsupported("function assignment target"); return {};
      }
      auto it = func_params_.find(le.cref.parts[0].name.id);
      if (it == func_params_.end()) { set_unsupported("function assignment target"); return {}; }
      ta = it->second.kind;
    }
    Id<Expr> rhs = simplify_expr(inst_exp(st.b, enclosing, {}));
    if (result_.unsupported) return {};
    const TypeKind tb = result_.model.exprs[rhs].ty.kind;
    if (ta == TypeKind::Real && tb == TypeKind::Integer) rhs = to_real(rhs);
    else if (ta != tb) { set_unsupported("function assignment type mismatch"); return {}; }
    FlatStatement fs;
    fs.kind = FlatStmtKind::Assign;
    fs.a = lhs;
    fs.b = rhs;
    fs.loc = st.loc;
    return result_.model.add_stmt(std::move(fs));
  }

  // 内建标量类型名 → TypeKind（函数形参/局部类型解析；非标量内建 → false）。
  bool builtin_scalar_type_kind(const std::vector<Symbol>& path, TypeKind& tk) {
    if (path.size() != 1) return false;
    const std::string_view n = in_.str(path[0]);
    if (n == "Real") { tk = TypeKind::Real; return true; }
    if (n == "Integer") { tk = TypeKind::Integer; return true; }
    if (n == "Boolean") { tk = TypeKind::Boolean; return true; }
    if (n == "String") { tk = TypeKind::String; return true; }
    return false;
  }

  // 发射用户函数为 FlatFunction（首批：纯标量 in/out 形参、绑定定义输出、无 protected 局部、无算法段）。
  // 去重按限定名（已发射返回 true）。不可发射（数组/记录/多输出无所谓/局部/算法/external/impure/注解/非标量）→ false（调用方保 SKIP）。
  bool emit_function(Id<SElement> fe, const std::vector<Symbol>& qname, NodeId enclosing) {
    for (const FlatFunction& f : result_.model.functions) if (f.name == qname) return true;   // 去重
    const SElement& fel = I_.scode.elems[fe];
    if (fel.is_partial || fel.def.external.present || fel.purity == Purity::Impure ||
        fel.cmt.annotation.is_valid()) return false;   // partial/external/impure/带注解 → SKIP
    if (!fel.def.equations.empty() || !fel.def.initial_algorithms.empty() ||
        !fel.def.initial_equations.empty() || fel.def.algorithms.size() > 1) return false;   // 无方程/初始段/多算法段
    if (emitting_funcs_.count(fe.raw)) return false;   // 递归（自/互调）→ SKIP
    std::vector<Id<SElement>> inputs, outputs, locals;   // in / out / protected 局部（声明序）
    for (Id<SElement> eid : fel.def.elements) {
      const SElement& c = I_.scode.elems[eid];
      if (c.kind != SElementKind::Component) return false;   // extends/嵌套类 → SKIP
      if (c.attr.dir == SDirection::Input) inputs.push_back(eid);
      else if (c.attr.dir == SDirection::Output) outputs.push_back(eid);
      else if (c.prefixes.vis == SVisibility::Protected) locals.push_back(eid);
      else return false;   // 公开非 io 变量 → 无效函数
    }
    if (outputs.size() != 1) return false;   // 多输出（元组）/无输出 → SKIP
    const bool has_alg = !fel.def.algorithms.empty();
    { const Id<SMod> om = I_.scode.elems[outputs[0]].mod;   // 输出须被赋值：绑定 或 算法 LHS，否则 omc 告警 → SKIP
      const bool ob = om.is_valid() && I_.scode.mods[om].binding.is_valid();
      if (!ob && !(has_alg && algorithm_assigns(fel.def.algorithms[0], I_.scode.elems[outputs[0]].name))) return false;
    }
    auto tk_of = [&](Id<SElement> eid, TypeKind& tk) {   // 标量内建类型
      const SElement& c = I_.scode.elems[eid];
      return c.attr.dims.empty() && c.type.dims.empty() && builtin_scalar_type_kind(c.type.path, tk);
    };
    auto saved = func_params_;   // func_params_ 每函数独立（clear+restore）
    func_params_.clear();
    auto reg = [&](const std::vector<Id<SElement>>& v) {
      for (Id<SElement> eid : v) { TypeKind tk; if (!tk_of(eid, tk)) return false; func_params_[I_.scode.elems[eid].name.id] = Type::of(tk); }
      return true;
    };
    if (!reg(inputs) || !reg(outputs) || !reg(locals)) { func_params_ = saved; return false; }
    emitting_funcs_.insert(fe.raw);
    bool ok = true;
    std::vector<Id<Expr>> lbind(locals.size());   // 局部绑定先类型化（供拓扑排序 + 发射）
    for (std::size_t i = 0; i < locals.size() && ok; ++i) {
      const Id<SMod> m = I_.scode.elems[locals[i]].mod;
      if (m.is_valid() && I_.scode.mods[m].binding.is_valid()) {
        lbind[i] = simplify_expr(inst_exp(I_.scode.mods[m].binding, enclosing, {}));
        if (result_.unsupported) ok = false;
        else { TypeKind tk; tk_of(locals[i], tk); if (tk == TypeKind::Real) lbind[i] = to_real(lbind[i]); }
      }
    }
    std::vector<std::size_t> lorder;   // 局部拓扑序：引用的其它局部先出（x1=x2,x2=x → x2,x1）
    if (ok) {
      std::vector<bool> done(locals.size(), false);
      for (std::size_t k = 0; k < locals.size() && ok; ++k) {
        bool progress = false;
        for (std::size_t i = 0; i < locals.size(); ++i) {
          if (done[i]) continue;
          bool ready = true;
          if (lbind[i].is_valid())
            for (std::size_t j = 0; j < locals.size(); ++j)
              if (!done[j] && j != i && flat_exp_refs(lbind[i], {I_.scode.elems[locals[j]].name.id})) { ready = false; break; }
          if (ready) { done[i] = true; lorder.push_back(i); progress = true; }
        }
        if (!progress) { ok = false; }   // 循环依赖 → SKIP
      }
    }
    FlatFunction ff;
    ff.name = qname;
    if (fel.cmt.str.is_valid()) ff.comment = fel.cmt.str;
    auto emit_io = [&](const std::vector<Id<SElement>>& v, FlatDirection dir) {   // in/out：变率剥离
      for (Id<SElement> eid : v) {
        if (!ok) return;
        const SElement& c = I_.scode.elems[eid];
        TypeKind tk; tk_of(eid, tk);
        FlatFuncParam p;
        p.name = c.name; p.ty = Type::of(tk); p.direction = dir; p.var_kind = FlatVarKind::Var;
        const Id<SMod> m = c.mod;
        if (m.is_valid() && I_.scode.mods[m].binding.is_valid()) {
          Id<Expr> b = simplify_expr(inst_exp(I_.scode.mods[m].binding, enclosing, {}));
          if (result_.unsupported) { ok = false; return; }
          if (tk == TypeKind::Real) b = to_real(b);
          p.binding = b;
        }
        ff.params.push_back(std::move(p));
      }
    };
    if (ok) emit_io(inputs, FlatDirection::Input);
    if (ok) emit_io(outputs, FlatDirection::Output);
    if (ok)
      for (std::size_t idx : lorder) {   // protected 局部（首批变率均按 Var 打，不加前缀）
        const SElement& c = I_.scode.elems[locals[idx]];
        TypeKind tk; tk_of(locals[idx], tk);
        FlatFuncParam p;
        p.name = c.name; p.ty = Type::of(tk); p.direction = FlatDirection::None; p.is_protected = true;
        p.var_kind = FlatVarKind::Var;
        p.binding = lbind[idx];
        ff.params.push_back(std::move(p));
      }
    if (ok && has_alg)
      for (Id<SStatement> sid : fel.def.algorithms[0].stmts) {
        Id<FlatStatement> s = collect_func_statement(sid, enclosing);
        if (result_.unsupported) { ok = false; break; }
        ff.body.push_back(s);
      }
    emitting_funcs_.erase(fe.raw);
    func_params_ = saved;
    if (!ok) return false;
    result_.model.functions.push_back(std::move(ff));
    return true;
  }

  // 纯标量用户函数调用 → 发射函数 + 建符号 Call（限定名 + 实参）。不可发射/多段名/数组实参 → invalid（调用方 SKIP）。
  Id<Expr> try_emit_user_call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const Cref& fn = I_.absyn.crefs[e.cref];
    if (fn.parts.size() != 1 || !fn.parts[0].subscripts.empty()) return {};
    for (Symbol n : e.arg_names) if (n.is_valid()) return {};   // 首批：不做命名实参
    const ScodeScope sc = scode_scope_of(enclosing);
    const Id<SElement> fe = find_scode_element(fn.parts[0].name, sc);
    if (!fe.is_valid() || I_.scode.elems[fe].kind != SElementKind::Class ||
        I_.scode.elems[fe].restriction != SRestriction::Function ||
        I_.scode.elems[fe].def.kind != SClassDefKind::Parts)
      return {};
    bool top_level = false;   // 首批：只发射顶层函数（裸名）；模型内嵌套函数需限定名 → SKIP
    for (Id<SElement> eid : I_.scode.program) if (eid.raw == fe.raw) { top_level = true; break; }
    if (!top_level) return {};
    std::size_t ninputs = 0;   // 首批：实参数须匹配输入数（不做省略默认展开，FuncMissingDefault1 等 → SKIP）
    for (Id<SElement> eid : I_.scode.elems[fe].def.elements)
      if (I_.scode.elems[eid].kind == SElementKind::Component &&
          I_.scode.elems[eid].attr.dir == SDirection::Input) ++ninputs;
    if (e.args.size() != ninputs) return {};
    const std::vector<Symbol> qname = {fn.parts[0].name};   // 首批：顶层函数裸名
    std::vector<Id<Expr>> args;   // 实参先定型（要求非数组；首批不做省略默认展开）
    for (Id<Exp> a : e.args) {
      Id<Expr> ae = inst_exp(a, enclosing, prefix);
      if (result_.unsupported || result_.model.exprs[ae].ty.is_array()) return {};
      args.push_back(ae);
    }
    if (!emit_function(fe, qname, enclosing)) return {};
    Expr r;
    r.kind = ExprKind::Call;
    r.call_name = qname;
    r.ty = Type::real();   // 输出类型（标量）；dump 不看
    r.args = std::move(args);
    return add(std::move(r));
  }

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
      v.name_subs = psubs_to_exprs(psubs);
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
      case ExpKind::Range: {   // a:b / a:step:b（Absyn 与 flat 同布局：a=起点, b=步长或终点, c=终点）
        Expr r;
        r.kind = ExprKind::Range;
        r.a = inst_exp(e.a, enclosing, prefix);
        r.b = inst_exp(e.b, enclosing, prefix);
        if (e.c.is_valid()) r.c = inst_exp(e.c, enclosing, prefix);
        auto ik = [&](Id<Expr> x) { return result_.model.exprs[x].ty.kind; };   // 迭代变量类型 = 范围元素类型
        const bool allint = ik(r.a) == TypeKind::Integer && ik(r.b) == TypeKind::Integer &&
                            (!r.c.is_valid() || ik(r.c) == TypeKind::Integer);
        r.ty = allint ? Type::integer() : Type::real();
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
    // for-语句迭代变量（KEPT loop）：符号引用，元素类型，不内联/不加前缀（对标 flattenStatement 不替换迭代变量）。
    if (!stmt_iters_.empty() && c.parts.size() == 1 && c.parts[0].subscripts.empty()) {
      auto it = stmt_iters_.find(c.parts[0].name.id);
      if (it != stmt_iters_.end()) {
        Expr r;
        r.kind = ExprKind::Cref;
        r.ty = Type::of(it->second);
        r.cref.parts.push_back({c.parts[0].name, {}});
        return add(std::move(r));
      }
    }
    // 函数体发射期：形参/局部引用 → 符号裸名 cref（元素类型取自 func_params_）。
    if (!func_params_.empty() && c.parts.size() == 1 && c.parts[0].subscripts.empty()) {
      auto it = func_params_.find(c.parts[0].name.id);
      if (it != func_params_.end()) {
        Expr r;
        r.kind = ExprKind::Cref;
        r.ty = it->second;
        r.cref.parts.push_back({c.parts[0].name, {}});
        return add(std::move(r));
      }
    }
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
        // 变率取**根**组件（`r1.x` 的 r1）：常量记录/数组的成员引用要按根的 constant 折叠，非叶字段自身变率（NFCeval evalCref 门）。
        const SVariability var = I_.comps[I_.nodes[local].comp].attributes.variability;
        const Type lty = I_.comps[I_.nodes[leaf].comp].ty;
        if (var == SVariability::Const) {
          if (c.parts.size() > 1) {
            // constant 记录成员 `r1.x` → 复用已发射叶子的字面量绑定（Half B，NFCeval recordElement）。
            Id<Expr> v = eval_const_record_member(prefix, c);
            if (v.is_valid()) return v;
            set_unsupported("constant evaluation");
          } else {
            // constant 数组的下标引用 `x[2]` → 绑定求值成数组值，再逐层按（常量）下标取标量（NFCeval subscriptBinding）。
            const SElement& clc = I_.scode.elems[I_.comps[I_.nodes[leaf].comp].def];
            const bool c_array = !clc.attr.dims.empty() || !clc.type.dims.empty();
            Id<Expr> v = (c_array && !c.parts.back().subscripts.empty())
                             ? eval_const_array_element(leaf, enclosing, c.parts.back().subscripts)
                             : eval_const_value(leaf, enclosing);
            if (v.is_valid()) return v;
            set_unsupported("constant evaluation");
          }
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
            // 整体数组引用 `x`（x: T[dims]）：提升成 ARRAY 类型（元素 lty），供上层逐元素标量化。维须字面量。
            std::vector<std::int64_t> ldims;
            bool alit = true;
            const ScodeScope lscope = scode_scope_of(enclosing);
            for (Id<Exp> d : lcel.attr.dims) if (!eval_dim(d, ldims, lscope)) alit = false;
            for (Id<Exp> d : lcel.type.dims) if (!eval_dim(d, ldims, lscope)) alit = false;
            if (!alit) {
              set_unsupported("whole-array reference");
            } else {
              std::vector<Dimension> ads;
              ads.reserve(ldims.size());
              for (std::int64_t n : ldims) { Dimension dd; dd.kind = DimKind::Integer; dd.size = n; ads.push_back(dd); }
              e.ty = Type::array_of(lty, std::move(ads));
            }
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
                if (is_stmt_iter_subscript(subs[k])) continue;   // for-语句符号迭代变量下标：omc 不静态越界检查
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
      // 函数体里的未解析裸名（非形参/局部/常量）= time 或模型变量 → 函数不能引用（omc 报错，Time3）→ SKIP。
      if (!func_params_.empty()) set_unsupported("function body references non-local");
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
    if (result_.model.exprs[a].ty.is_array() || result_.model.exprs[b].ty.is_array())
      return array_binary(e.op, op, a, b);   // 数组：逐元素/广播，维度传播，逐元素标量化在 collect_array_equation
    // Modelica `/` 恒实除：整型操作数也提实型（`i/j` → `/*Real*/(i) / /*Real*/(j)`，MLS §10.6.1）。
    bool real = op == BinOp::Div || result_.model.exprs[a].ty.kind == TypeKind::Real ||
                result_.model.exprs[b].ty.kind == TypeKind::Real;
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
    const bool arr = result_.model.exprs[a].ty.is_array() || result_.model.exprs[b].ty.is_array();
    RelOp rop;
    LogOp lop;
    if (map_relop(e.op, rop)) {
      if (arr) { set_unsupported("array relation"); return lit_int(0); }   // 元素级 Boolean 数组关系本增量暂缓
      bool real = result_.model.exprs[a].ty.kind == TypeKind::Real || result_.model.exprs[b].ty.kind == TypeKind::Real;
      if (real) { a = to_real(a); b = to_real(b); }
      Expr r; r.kind = ExprKind::Relation; r.rel = rop; r.a = a; r.b = b; r.ty = Type::boolean();
      return add(std::move(r));
    }
    if (map_logop(e.op, lop)) {
      if (arr) return array_logical(lop, a, b);   // and/or 数组逐元素/广播
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
      const Type tya = result_.model.exprs[a].ty;
      Expr r; r.kind = ExprKind::LogicalNot; r.a = a;
      r.ty = tya.is_array() ? Type::array_of(Type::boolean(), tya.dims) : Type::boolean();
      return add(std::move(r));
    }
    // UMinus（数组元素级：结果类型 = 操作数类型，含数组维）
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
      // 折不成常量：纯标量用户函数 → 发射函数定义 + 保留符号调用（首批，S3.e）。不可发射 → 落到下面 SKIP。
      if (Id<Expr> uc = try_emit_user_call(e, enclosing, prefix); uc.is_valid()) return uc;
    }
    const std::string_view builtin_name = fn.parts.size() == 1 ? in_.str(fn.parts[0].name) : std::string_view{};
    if (builtin_name == "cardinality") return cardinality_call(e, enclosing, prefix);
    if (builtin_name == "inStream" || builtin_name == "actualStream") return stream_op_call(e, enclosing, prefix, builtin_name);
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
        // simplifyIf（NFSimplifyExp.mo:1330-1337）：两支相等 → 该支；两支是（不等的）布尔字面量 → cond / not cond
        //（logicNegate：not 的 not 抵消，否则包一层 not，NFExpression.mo:5334-5344）。
        if (expr_equal(tt, ee)) return tt;
        if (result_.model.exprs[tt].kind == ExprKind::BoolLit && result_.model.exprs[ee].kind == ExprKind::BoolLit) {
          if (result_.model.exprs[tt].bval) return cc;
          if (result_.model.exprs[cc].kind == ExprKind::LogicalNot) return result_.model.exprs[cc].a;
          Expr n; n.kind = ExprKind::LogicalNot; n.a = cc; n.ty = Type::boolean();
          return add(std::move(n));
        }
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
        // noEvent(const) → 脱壳折叠成字面量；符号参数保留 `noEvent(...)`（CevalNoEvent1）。
        if (name == "noEvent" && args.size() == 1) return is_literal(args[0]) ? args[0] : rebuild();
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

  // 纯数据记录：Record 长类、成员全是标量 Var 非 final 非复合的 Component。放行 constant/parameter 复合组件的变率下沉，
  // 排除会 FAIL 的形态（final 字段 RecordBinding6/7、数组字段、嵌套记录、extends/嵌套类/操作符构造子）。
  bool is_plain_data_record(NodeId ci, Id<SElement> cdef) {
    const SElement& cd = I_.scode.elems[cdef];
    if (cd.restriction != SRestriction::Record || cd.def.kind != SClassDefKind::Parts) return false;
    for (Id<SElement> e : cd.def.elements)
      if (I_.scode.elems[e].kind != SElementKind::Component) return false;   // 无 extends/嵌套类/操作符构造子
    for (NodeId fn : I_.trees[I_.node_class(ci).tree].components) {
      const CompId fc = I_.nodes[fn].comp;
      const SElement& f = I_.scode.elems[I_.comps[fc].def];
      if (!f.attr.dims.empty() || !f.type.dims.empty()) return false;                 // 无数组字段
      if (I_.comps[fc].attributes.variability != SVariability::Var) return false;     // 字段须 Var（排除 final constant 字段）
      if (I_.comps[fc].attributes.is_final) return false;                             // 排除 final 字段
      if (I_.comps[fc].ty.kind == TypeKind::Complex) return false;                    // 无嵌套记录
    }
    return true;
  }

  // constant 记录成员 `r1.x` 折叠（Half B，Option A）：复用已发射的叶子字面量（r1.x 在 r1 声明时已发射，带常量字面量绑定）。
  // 依赖声明序（r1 先于引用者）；前向引用 / 带下标 / 非字面量 → invalid → 调用方 set_unsupported SKIP。
  Id<Expr> eval_const_record_member(const std::vector<Symbol>& prefix, const Cref& c) {
    std::vector<Symbol> full = prefix;
    for (const CrefPart& p : c.parts) {
      if (!p.subscripts.empty()) return {};   // 记录数组 / 数组字段暂缓
      full.push_back(p.name);
    }
    const FlatVariable* lv = find_scalar_flat_var(full);
    if (lv && lv->var_kind == FlatVarKind::Constant && lv->binding.is_valid() && is_literal(lv->binding))
      return lv->binding;
    return {};
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

  // constant 数组的下标元素求值：绑定 → CVal::Array，逐层按（常量）下标取（行主序，NFCeval applySubscripts）。
  // 任何失败（绑定非数组字面量 / 下标非常量 / 越界 / 部分下标留数组·记录）→ invalid，调用方 set_unsupported SKIP。
  Id<Expr> eval_const_array_element(NodeId leaf, NodeId decl_scope, const std::vector<Id<Exp>>& subs) {
    const CompId cid = I_.nodes[leaf].comp;
    Id<Exp> raw;
    NodeId scope;
    const Binding& bnd = I_.comps[cid].binding;
    if (bnd.kind == BindingKind::Raw && bnd.raw_exp.is_valid()) { raw = bnd.raw_exp; scope = bnd.scope; }
    else {   // def-only：从 SCODE 取声明绑定，在声明作用域求值
      const Id<SMod> m = I_.scode.elems[I_.comps[cid].def].mod;
      if (m.is_valid()) raw = I_.scode.mods[m].binding;
      scope = decl_scope;
    }
    if (!raw.is_valid()) return {};
    std::optional<CVal> av = eval_scode(raw, scode_scope_of(scope), 0, /*allow_param=*/false);
    if (!av || av->k != CVal::K::Array) return {};   // 绑定非数组字面量（构造子/函数/切片）→ SKIP
    const CVal* cur = &*av;
    for (Id<Exp> s : subs) {
      std::optional<CVal> iv = eval_scode(s, scode_scope_of(decl_scope), 0, /*allow_param=*/false);
      if (!iv || iv->k != CVal::K::Int) return {};    // 非常量下标（x[n]，n 是变量）→ SKIP
      if (cur->k != CVal::K::Array || !cur->arr) return {};
      const std::int64_t idx = iv->i;
      if (idx < 1 || static_cast<std::size_t>(idx) > cur->arr->size()) return {};   // 越界 → SKIP（保守，omc 保留下标）
      cur = &(*cur->arr)[idx - 1];
    }
    if (cur->k == CVal::K::Array || cur->k == CVal::K::Record) return {};   // 部分下标留数组/记录段 → SKIP
    std::optional<CVal> cv = coerce_cval(*cur, I_.scode.elems[I_.comps[cid].def].type.path);
    if (!cv || cv->k == CVal::K::Array || cv->k == CVal::K::Record) return {};
    return literal_of(*cv);
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
  // 连接器叶成员：path = 相对连接器的成员路径（嵌套连接器多段 `c1.e`）；ty = 元素类型；dims 空 = 标量成员，
  // 非空 = 数组成员（连接方程逐元素摊，Connection.scalarize）。
  // stream 叶：is_stream + flow_path（同连接器层唯一 flow 的相对路径，associatedFlowCref，NFConnectEquations.mo:1106-1126）。
  struct ConnMember {
    std::vector<Symbol> path; bool is_flow; Type ty; std::vector<std::int64_t> dims;
    bool is_stream = false;
    std::vector<Symbol> flow_path;
  };
  // connect 端的种类（sortConnections，NFExpandableConnectors.mo:126-171）：Expandable = expandable 连接器组件本身（cty
  // EXPANDABLE）；Declared = 已声明的 potentially present 成员；Virtual = expandable 里未声明的名字（虚元素，NAME 节点）。
  enum class EndKind : std::uint8_t { Normal, Expandable, Declared, Virtual };
  struct ConnInfo {
    std::vector<Symbol> path;                        // 连接器点分路径（`m.c` → {m,c}）
    std::vector<std::vector<Id<Expr>>> subs;         // 与 path 平行的逐段下标（数组连接器 `c[i]`）；标量段空
    bool outside;
    std::vector<ConnMember> members;                 // Expandable / Virtual 端在 elaborate_expandable 里补
    EndKind kind = EndKind::Normal;
    NodeId node;              // 末段组件节点（Virtual 无）
    NodeId parent;            // Declared / Virtual：父段组件节点（须是 expandable）
    bool scalar_ref = true;   // 各段各维都由单个下标给出（虚元素取类型的另一端须是标量元素）
  };
  struct Resolved { ConnInfo a, b; SourceLoc loc; };
  // 连接集节点（Util/DisjointSets.mo 的元素）= 标量叶 × 面。
  struct SetNode {
    Id<Expr> cref; bool outside; bool is_flow; Type ty; SourceLoc loc;
    bool is_stream;
    std::string name_key;   // 叶 key（不含面）：removeStreamSetElement 按名比较（NFConnectEquations.mo:1088-1104）
    Id<Expr> flow_cref;     // stream 叶：关联 flow 的 cref
    std::string flow_key;   // stream 叶：关联 flow 变量的 key（查 min/max/nominal）
  };
  // 连接器身份 key（含数组下标）：无下标时与 path_key 逐字节相同（标量连接器向后兼容）。
  std::string conn_key(const std::vector<Symbol>& path, const std::vector<std::vector<Id<Expr>>>& subs) {
    std::string k;
    for (std::size_t i = 0; i < path.size(); ++i) {
      k += std::to_string(path[i].id);
      if (i < subs.size())
        for (Id<Expr> s : subs[i]) {
          const Expr& se = result_.model.exprs[s];
          k.push_back('[');
          if (se.kind == ExprKind::IntLit) k += std::to_string(se.ival);
          else k += "#" + std::to_string(s.raw);   // 非整型下标（不入本桶目标）——Id 保唯一
          k.push_back(']');
        }
      k.push_back('.');
    }
    return k;
  }
  std::string conn_key(const ConnInfo& c) { return conn_key(c.path, c.subs); }

  // 连接器叶成员表（对标 Connector.split → splitImpl/splitImpl2，NFConnector.mo:328-393）：每层 potential 在前、flow 在后，
  // 组内**按名升序**（makeConnectorType 遍历 ClassTree.enumerateComponents = 按名 LookupTree 折叠，NFTyping.mo:339-354 /
  // NFClassTree.mo:1418-1444；cons/reverse 链净效果为升序，YmcOrder.Flat/Nested + ConnectDiffOrder2 探针实测）；
  // 子连接器（potential 位）就地递归展开（splitImpl :342-348）。数组成员保留维度（:363），发射时再逐元素摊。
  // stream / 记录成员（:354-360 未做）/ 子连接器数组 / `flow` 前缀子连接器（conn9）/ 非 Var 变率 / 非字面量维 → false（SKIP）。
  bool connector_leaves(NodeId conn, const std::vector<Symbol>& mpath, std::vector<ConnMember>& out) {
    const std::vector<NodeId> mnodes = I_.trees[I_.node_class(conn).tree].components;   // 拷贝（不跨 add）
    std::vector<NodeId> pots, flows, streams;   // makeConnectorType 三组（NFTyping.mo:339-354）
    for (NodeId mn : mnodes) {
      const SConnectorType ct = I_.comps[I_.nodes[mn].comp].attributes.connector;
      (ct == SConnectorType::Stream ? streams : ct == SConnectorType::Flow ? flows : pots).push_back(mn);
    }
    auto by_name = [&](NodeId a, NodeId b) { return in_.str(I_.nodes[a].name) < in_.str(I_.nodes[b].name); };
    std::stable_sort(pots.begin(), pots.end(), by_name);
    std::stable_sort(flows.begin(), flows.end(), by_name);
    std::stable_sort(streams.begin(), streams.end(), by_name);
    const ScodeScope mscope = scode_scope_of(conn);
    auto leaf = [&](NodeId mn, bool is_flow) -> bool {
      const CompId mc = I_.nodes[mn].comp;
      const SElement& mcel = I_.scode.elems[I_.comps[mc].def];
      // parameter/constant potential 成员：连接生成相等断言（generatePotentialEquations :215-235，发射时按集首变率判）；
      // 非连续的 flow 未做 → SKIP。
      if (is_flow && I_.comps[mc].attributes.variability != SVariability::Var) return false;
      std::vector<std::int64_t> mdims;
      for (Id<Exp> d : mcel.attr.dims) if (!eval_dim(d, mdims, mscope)) return false;   // 非字面量维 → SKIP
      for (Id<Exp> d : mcel.type.dims) if (!eval_dim(d, mdims, mscope)) return false;
      for (std::int64_t n : mdims) if (n <= 0) return false;   // 空数组成员（omc 整体丢弃）未做 → SKIP
      std::vector<Symbol> p = mpath;
      p.push_back(I_.nodes[mn].name);
      out.push_back({std::move(p), is_flow, I_.comps[mc].ty, std::move(mdims), false, {}});
      return true;
    };
    for (NodeId mn : pots) {
      const CompId mc = I_.nodes[mn].comp;
      if (I_.comps[mc].ty.kind != TypeKind::Complex) { if (!leaf(mn, false)) return false; continue; }
      // 复合成员：子连接器就地递归（splitImpl :342-348）；记录成员按字段声明序展开（:354-360）。本身须标量、Var。
      NodeId sub = I_.comps[mc].class_inst;
      if (!sub.is_valid() || I_.nodes[sub].is_empty() || !I_.nodes[sub].def.is_valid()) return false;
      const SRestriction sr = I_.scode.elems[I_.nodes[sub].def].restriction;
      const SElement& mcel = I_.scode.elems[I_.comps[mc].def];
      if (!mcel.attr.dims.empty() || !mcel.type.dims.empty()) return false;
      if (I_.comps[mc].attributes.variability != SVariability::Var) return false;
      std::vector<Symbol> p = mpath;
      p.push_back(I_.nodes[mn].name);
      if (sr == SRestriction::Connector) { if (!connector_leaves(sub, p, out)) return false; }
      else if (sr == SRestriction::Record) { if (!record_leaves(sub, p, false, out)) return false; }
      else return false;
    }
    for (NodeId mn : flows) {
      const CompId mc = I_.nodes[mn].comp;
      if (I_.comps[mc].ty.kind != TypeKind::Complex) { if (!leaf(mn, true)) return false; continue; }
      // `flow R f`：记录字段全部继承 flow（ConnectorType.merge，NFAttributes.mo:229-234）；`flow C c` 子连接器未做（conn9）。
      NodeId sub = I_.comps[mc].class_inst;
      if (!sub.is_valid() || I_.nodes[sub].is_empty() || !I_.nodes[sub].def.is_valid() ||
          I_.scode.elems[I_.nodes[sub].def].restriction != SRestriction::Record) return false;
      const SElement& mcel = I_.scode.elems[I_.comps[mc].def];
      if (!mcel.attr.dims.empty() || !mcel.type.dims.empty()) return false;
      if (I_.comps[mc].attributes.variability != SVariability::Var) return false;
      std::vector<Symbol> p = mpath;
      p.push_back(I_.nodes[mn].name);
      if (!record_leaves(sub, p, true, out)) return false;
    }
    // stream 成员（splitImpl :346：flows 之后，按名升序）：关联同层**唯一**标量 flow（associatedFlowCref :1106-1126；
    // 否则 omc 报错 "must have exactly one flow variable"）。v1：标量 stream、不向外层连接器外溯。
    if (!streams.empty()) {
      if (flows.size() != 1) return false;
      const CompId fc = I_.nodes[flows[0]].comp;
      const SElement& fcel = I_.scode.elems[I_.comps[fc].def];
      if (I_.comps[fc].ty.kind == TypeKind::Complex || !fcel.attr.dims.empty() || !fcel.type.dims.empty()) return false;
      std::vector<Symbol> fpath = mpath;
      fpath.push_back(I_.nodes[flows[0]].name);
      for (NodeId mn : streams) {
        const CompId mc = I_.nodes[mn].comp;
        const SElement& mcel = I_.scode.elems[I_.comps[mc].def];
        if (I_.comps[mc].ty.kind == TypeKind::Complex || !mcel.attr.dims.empty() || !mcel.type.dims.empty()) return false;
        if (I_.comps[mc].attributes.variability != SVariability::Var) return false;
        std::vector<Symbol> p = mpath;
        p.push_back(I_.nodes[mn].name);
        out.push_back({std::move(p), false, I_.comps[mc].ty, {}, true, fpath});
      }
    }
    return true;
  }
  // 记录成员的字段叶（splitImpl 记录分支，NFConnector.mo:354-360：ClassTree.getComponents = 组件数组 = **声明序**，
  // 不像连接器那样按名排序；YmcRecConn 探针 r2,r1）。is_flow 由记录成员的前缀决定并下沉到全部字段；嵌套记录递归。
  bool record_leaves(NodeId rec, const std::vector<Symbol>& mpath, bool is_flow, std::vector<ConnMember>& out) {
    const std::vector<NodeId> fnodes = I_.trees[I_.node_class(rec).tree].components;   // 拷贝（不跨 add）
    const ScodeScope fscope = scode_scope_of(rec);
    for (NodeId fn : fnodes) {
      const CompId fc = I_.nodes[fn].comp;
      const SElement& fcel = I_.scode.elems[I_.comps[fc].def];
      if (I_.comps[fc].attributes.connector != SConnectorType::Potential) return false;   // 字段自带 flow/stream → 未做
      if (I_.comps[fc].attributes.variability != SVariability::Var) return false;
      std::vector<Symbol> p = mpath;
      p.push_back(I_.nodes[fn].name);
      if (I_.comps[fc].ty.kind == TypeKind::Complex) {
        NodeId sub = I_.comps[fc].class_inst;
        if (!sub.is_valid() || I_.nodes[sub].is_empty() || !I_.nodes[sub].def.is_valid() ||
            I_.scode.elems[I_.nodes[sub].def].restriction != SRestriction::Record) return false;
        if (!fcel.attr.dims.empty() || !fcel.type.dims.empty()) return false;
        if (!record_leaves(sub, p, is_flow, out)) return false;
        continue;
      }
      std::vector<std::int64_t> fdims;
      for (Id<Exp> d : fcel.attr.dims) if (!eval_dim(d, fdims, fscope)) return false;
      for (Id<Exp> d : fcel.type.dims) if (!eval_dim(d, fdims, fscope)) return false;
      for (std::int64_t n : fdims) if (n <= 0) return false;
      out.push_back({std::move(p), is_flow, I_.comps[fc].ty, std::move(fdims), false, {}});
    }
    return true;
  }
  // 连接器成员（含嵌套前缀）是否为条件为假被移除的组件：逐段前缀查 removed_（键不含下标，数组连接器各元素共享）。
  bool member_removed(const std::vector<Symbol>& cpath, const std::vector<Symbol>& mpath) {
    std::vector<Symbol> p = cpath;
    for (Symbol s : mpath) { p.push_back(s); if (removed_.count(path_key(p))) return true; }
    return false;
  }
  // 标量叶身份 key = 连接器路径（逐段下标）+ 成员路径 + 末段元素下标；编码与 conn_key(name, subs) 相同，
  // 故 flow 变量可用 conn_key(fv.name, fv.subs) 直接查同一张表（元素下标放 subs 末槽）。
  std::string leaf_key(const ConnInfo& c, const ConnMember& m, const std::vector<std::int64_t>& idx) {
    std::string k = conn_key(c.path, c.subs);
    for (std::size_t i = 0; i < m.path.size(); ++i) {
      k += std::to_string(m.path[i].id);
      if (i + 1 == m.path.size())
        for (std::int64_t v : idx) { k.push_back('['); k += std::to_string(v); k.push_back(']'); }
      k.push_back('.');
    }
    return k;
  }

  // 复合组件的类实例节点；非复合 / 无效 → 无效 NodeId。
  NodeId class_of(NodeId comp) {
    const Component& cp = I_.comps[I_.nodes[comp].comp];
    if (cp.ty.kind != TypeKind::Complex) return {};
    const NodeId ci = cp.class_inst;
    return (ci.is_valid() && !I_.nodes[ci].is_empty() && I_.nodes[ci].def.is_valid()) ? ci : NodeId{};
  }
  bool is_expandable_comp(NodeId comp) {
    const NodeId cls = class_of(comp);
    return cls.is_valid() && I_.scode.elems[I_.nodes[cls].def].restriction == SRestriction::ExpandableConnector;
  }
  // 短类连接器 `connector InReal = input Real`：class_inst 已塌到内建（inst.cpp 短类塌陷），故按声明类型名在声明类作用域
  // 找元素：须是 restriction=connector 的短类（`type T = input Real` 不是连接器，omc 报错 ConnectNonConnector*）。
  // 限定名 / 继承来的类型暂缓 → false。
  bool short_class_connector(NodeId comp, NodeId decl) {
    const SElement& cel = I_.scode.elems[I_.comps[I_.nodes[comp].comp].def];
    if (cel.type.leading_dot || cel.type.path.size() != 1) return false;
    const SElement* tel = find_class_elem(decl, cel.type.path[0]);
    return tel && tel->restriction == SRestriction::Connector && tel->def.kind == SClassDefKind::Derived;
  }
  // InstNode.isConnector（NFInstNode.mo:507-516）：cty 含 CONNECTOR / EXPANDABLE / POTENTIALLY_PRESENT 任一位
  // （NFPrefixes.mo:59-60/167-175）= connector / expandable 类组件、短类连接器、或 potentially present 成员。
  bool is_connector_comp(NodeId comp, NodeId decl, bool pp) {
    if (pp) return true;
    if (I_.comps[I_.nodes[comp].comp].ty.kind == TypeKind::Complex) {
      const NodeId cls = class_of(comp);
      if (!cls.is_valid()) return false;
      const SRestriction r = I_.scode.elems[I_.nodes[cls].def].restriction;
      return r == SRestriction::Connector || r == SRestriction::ExpandableConnector;
    }
    return short_class_connector(comp, decl);
  }

  // 把 connect 实参（cref）解析成连接器**元素列表**（对标 makeConnectors，NFConnections.mo:209-235）：
  // 每段下标按 ExpandExp.expandCref / Subscript.expandList 展开（整数、范围/切片、缺省维取全部），各段各维行主序
  // 求积（首段最外），每个元素：flat 路径 + 逐段下标 + inside/outside + 叶成员表（见 ConnMember；短类连接器
  // `connector InReal = input Real` 本身即唯一标量叶）+ 端种类（ConnInfo::kind）。Expandable / Virtual 端的成员表留空
  // （增广后才定，elaborate_expandable）。stream 短类、非连接器、非常量维/下标、越界 → nullopt（调用方 SKIP）。
  std::optional<std::vector<ConnInfo>> resolve_connector(Id<Exp> ce, NodeId enclosing) {
    if (I_.absyn.exps[ce].kind != ExpKind::CrefExp) return std::nullopt;
    const Cref& c = I_.absyn.crefs[I_.absyn.exps[ce].cref];
    if (c.leading_dot || c.parts.empty()) return std::nullopt;
    // 合法连接器引用形态（NFTyping.checkConnector/checkConnectorForm :3170-3231）：除首段外每段都须是连接器
    // （`c1.c2…cn` 或 `m.c`；`b.a.c` 中 a 非连接器 → omc 报错 ConnectInvalidForm1），末段本身须是连接器。
    // expandable 里未声明的末段名 = 虚元素（NAME 节点 isConnector 恒真）；虚元素再往下一层 omc 内部错误
    //（ExpandableConnectorNonDecl3）→ SKIP。逐段记组件节点与声明类（该段维度表达式在声明类作用域求值）。
    struct Seg { NodeId comp; NodeId decl; };
    std::vector<Seg> segs;
    std::vector<Symbol> path;
    NodeId cur = find_local(enclosing, c.parts[0].name);
    if (!cur.is_valid() || I_.nodes[cur].is_empty()) return std::nullopt;
    segs.push_back({cur, enclosing});
    path.push_back(c.parts[0].name);
    // 面（crefFace，NFConnector.mo:312-326）：单段、或首段是连接器 → OUTSIDE；否则 INSIDE。
    const bool first_conn = is_connector_comp(cur, enclosing, false);
    if (c.parts.size() == 1 && !first_conn) return std::nullopt;   // 单段非连接器（ConnectNonConnector*）
    const bool outside = c.parts.size() == 1 || first_conn;
    bool pp = false;            // 当前段 potentially present（ModCtx::pp_chain 同一规则）
    bool is_virtual = false;
    for (std::size_t i = 1; i < c.parts.size(); ++i) {
      const NodeId cls = class_of(cur);
      const bool cls_exp = is_expandable_comp(cur);
      const NodeId nxt = cls.is_valid() ? find_local(cls, c.parts[i].name) : NodeId{};
      if (!nxt.is_valid() || I_.nodes[nxt].is_empty()) {
        if (!cls_exp || i + 1 != c.parts.size() || !c.parts[i].subscripts.empty()) return std::nullopt;
        is_virtual = true;   // 虚元素（标量；带下标的虚数组元素属增量 B）
        path.push_back(c.parts[i].name);
        break;
      }
      pp = (cls_exp || pp) && !is_expandable_comp(nxt);
      if (!is_connector_comp(nxt, cls, pp)) return std::nullopt;
      cur = nxt;
      segs.push_back({cur, cls});
      path.push_back(c.parts[i].name);
    }
    // 端种类 + 叶成员表（split 序：每层 potential 升序 → flow 升序，子连接器就地展开；见 connector_leaves）。
    EndKind kind = EndKind::Normal;
    NodeId parent;
    std::vector<ConnMember> members;
    if (is_virtual) {
      kind = EndKind::Virtual;   // cur = 父 expandable；成员 = 另一端的类型（makeVirtualConnector，增广时补）
      parent = cur;
    } else if (is_expandable_comp(cur)) {
      kind = EndKind::Expandable;   // 成员 = 增广后的类型（updateExpandableConnector :490-500，增广时补）
    } else {
      const CompId ccid = I_.nodes[cur].comp;
      if (pp) {
        kind = EndKind::Declared;
        parent = segs[segs.size() - 2].comp;   // pp 只能在第二段起成立
        const SElement& sel = I_.scode.elems[I_.comps[ccid].def];
        if (!sel.attr.dims.empty() || !sel.type.dims.empty()) return std::nullopt;   // pp 数组成员属增量 B
      }
      if (I_.comps[ccid].ty.kind == TypeKind::Complex) {
        const NodeId conn = class_of(cur);
        if (!conn.is_valid() || I_.scode.elems[I_.nodes[conn].def].restriction != SRestriction::Connector) return std::nullopt;
        if (!connector_leaves(conn, {}, members)) return std::nullopt;
      } else {
        // 短类连接器（组件本身即唯一标量叶，splitImpl 非 COMPLEX 分支，NFConnector.mo:366），或 expandable 里声明的
        // 内建标量成员（`Real x`，isConnector 靠 POTENTIALLY_PRESENT 位）。
        if (!pp && !short_class_connector(cur, segs.back().decl)) return std::nullopt;
        if (I_.comps[ccid].attributes.connector == SConnectorType::Stream) return std::nullopt;
        const bool fl = I_.comps[ccid].attributes.connector == SConnectorType::Flow;
        // `connector C = parameter Real`：parameter/constant 短类连接器照常连，发射时出断言（ConstantConnector1/2）；flow 须连续。
        if (fl && I_.comps[ccid].attributes.variability != SVariability::Var) return std::nullopt;
        members.push_back({{}, fl, I_.comps[ccid].ty, {}, false, {}});
      }
    }
    // 元素列表（对标 ExpandExp.expandCref：每段下标按 Subscript.expandList 展开——给定下标：整数/迭代变量取值、
    // 范围/数组按其自身顺序（YmcSlice.Desc/Step 探针）、缺省的后续维取全部（NFSubscript.mo:1081-1103，YmcSlice.Partial）；
    // 各段各维行主序求积，首段最外（expandCref3/4，NFExpandExp.mo:208-255；ConnectArrays3 宿主段在先）。
    // 下标越界 = omc 报错 → SKIP；非常量下标（ConnectArraysInvalidSubscript1）→ SKIP。
    std::vector<std::vector<std::vector<std::int64_t>>> seg_lists(segs.size());   // [段][维] → 下标值列表
    bool scalar_ref = true;   // 各维都由单个整数下标给出（无缺省维、无范围/切片）
    for (std::size_t si = 0; si < segs.size(); ++si) {
      const SElement& sel = I_.scode.elems[I_.comps[I_.nodes[segs[si].comp].comp].def];
      std::vector<std::int64_t> dims;
      const ScodeScope dscope = scode_scope_of(segs[si].decl);
      for (Id<Exp> d : sel.attr.dims) if (!eval_dim(d, dims, dscope)) return std::nullopt;
      for (Id<Exp> d : sel.type.dims) if (!eval_dim(d, dims, dscope)) return std::nullopt;
      const std::vector<Id<Exp>>& ssubs = c.parts[si].subscripts;
      if (ssubs.size() > dims.size()) return std::nullopt;   // 下标多于维数（标量带下标）→ SKIP
      for (std::size_t k = 0; k < dims.size(); ++k) {
        if (dims[k] <= 0) return std::nullopt;
        std::vector<std::int64_t> vals;
        if (k < ssubs.size()) {
          if (std::optional<std::int64_t> iv = subscript_int(ssubs[k])) vals.push_back(*iv);
          else if (std::optional<std::vector<std::int64_t>> rv = eval_range(ssubs[k], dscope, 0)) vals = std::move(*rv);
          else return std::nullopt;
          const ExpKind sk = I_.absyn.exps[ssubs[k]].kind;
          if (sk == ExpKind::Range || sk == ExpKind::Array || sk == ExpKind::Colon) scalar_ref = false;
        } else {
          for (std::int64_t v = 1; v <= dims[k]; ++v) vals.push_back(v);
          scalar_ref = false;
        }
        for (std::int64_t v : vals) if (v < 1 || v > dims[k]) return std::nullopt;   // 越界 → SKIP
        seg_lists[si].push_back(std::move(vals));
      }
    }
    // 行主序求积：把各段各维摊成一串"维"，pos 记每维在其取值列表里的位置（1 基，与 next_index 一致）。
    std::vector<const std::vector<std::int64_t>*> flat;
    for (const auto& sl : seg_lists) for (const auto& vals : sl) flat.push_back(&vals);
    std::vector<std::int64_t> sizes, pos(flat.size(), 1);
    for (const auto* v : flat) sizes.push_back(static_cast<std::int64_t>(v->size()));
    std::vector<ConnInfo> out;
    do {
      ConnInfo info;
      info.path = path;
      info.subs.resize(path.size());
      std::size_t fi = 0;
      for (std::size_t si = 0; si < segs.size(); ++si)
        for (std::size_t k = 0; k < seg_lists[si].size(); ++k, ++fi)
          info.subs[si].push_back(lit_int((*flat[fi])[static_cast<std::size_t>(pos[fi] - 1)]));
      info.outside = outside;
      info.members = members;
      info.kind = kind;
      if (!is_virtual) info.node = cur;
      info.parent = parent;
      info.scalar_ref = scalar_ref;
      out.push_back(std::move(info));
    } while (next_index(pos, sizes));
    return out;
  }

  // 连接器成员 cref：`path[subs].m1.m2…[msubs]`（mpath = 成员相对路径，嵌套连接器多段；msubs = 数组成员的元素下标，标量空）。
  Id<Expr> member_cref(const std::vector<Symbol>& path, const std::vector<std::vector<Id<Expr>>>& subs,
                       const std::vector<Symbol>& mpath, const std::vector<Id<Expr>>& msubs, Type ty) {
    Expr e;
    e.kind = ExprKind::Cref;
    e.ty = ty;
    for (std::size_t i = 0; i < path.size(); ++i)
      e.cref.parts.push_back({path[i], i < subs.size() ? subs[i] : std::vector<Id<Expr>>{}});
    for (std::size_t i = 0; i < mpath.size(); ++i)
      e.cref.parts.push_back({mpath[i], i + 1 == mpath.size() ? msubs : std::vector<Id<Expr>>{}});
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

  // connect 实参是否引用了被移除（条件为假）的组件——按 书写处前缀 ++ cref 逐段前缀查 removed_（键是全路径）。
  bool references_removed(Id<Exp> ce, const std::vector<Symbol>& prefix) {
    if (I_.absyn.exps[ce].kind != ExpKind::CrefExp) return false;
    const Cref& c = I_.absyn.crefs[I_.absyn.exps[ce].cref];
    std::vector<Symbol> p = prefix;
    for (const CrefPart& part : c.parts) {
      p.push_back(part.name);
      if (removed_.count(path_key(p))) return true;
    }
    return false;
  }

  // ---- DisjointSets（Util/DisjointSets.mo）：link <0 = 根（值 = −秩），>0 = 父下标+1 ----
  static std::size_t ds_root(const std::vector<std::int64_t>& link, std::size_t i) {
    while (link[i] > 0) i = static_cast<std::size_t>(link[i] - 1);
    return i;
  }
  // union（:256-287）：秩存负数；注释与代码相反，按**代码**——rank1 > rank2 把 set2 挂到 set1 下。
  static void ds_union(std::vector<std::int64_t>& link, std::size_t s1, std::size_t s2) {
    if (s1 == s2) return;
    const std::int64_t r1 = link[s1], r2 = link[s2];
    if (r1 > r2) link[s2] = static_cast<std::int64_t>(s1) + 1;
    else if (r1 < r2) link[s1] = static_cast<std::int64_t>(s2) + 1;
    else { link[s1] -= 1; link[s2] = static_cast<std::int64_t>(s1) + 1; }
  }

  // ---- expandable 连接器（MLS §9.1.3；omc NFExpandableConnectors.elaborate :71-122 + NFFlatten.mo:2462-2468）----
  // exp_set 是 UnorderedSet<Connector>（按末段名 stringHashDjb2 散列，:316/510-515），toList 次序 = 桶序（Util/UnorderedSet.mo
  // add :112-126 / addKey :834-861 / rehash :539-563 / toList :254-264）。stringHashDjb2 = djb2 在 C `long` 上累乘后 labs
  // （meta_modelica_builtin.c:119-157）；安装版 omc 1.25.0 是 Windows 构建，long 为 32 位（LLP64）→ 32 位回绕
  // （YmcExpHash2 探针：speed/torque/temp 的实测序只与 32 位一致；YmcExpHash1：a/b/c 实测 b,c,a = 桶序而非插入序）。
  static std::int64_t djb2_long32(std::string_view s) {
    std::uint32_t h = 5381;
    for (unsigned char ch : s) h = h * 33u + ch;
    const std::int64_t v = static_cast<std::int32_t>(h);
    return v < 0 ? -v : v;   // ponytail: labs(LONG_MIN) 在 C 里仍为负（UB），此处得 2^31；概率 2^-32，遇到再对齐
  }
  static std::size_t next_prime(std::size_t n) {   // Util.nextPrime（Util.mo:1225-1270）：≥ n 的最小素数（只试奇数）
    if (n <= 2) return 2;
    if (n % 2 == 0) ++n;
    for (;; n += 2) {
      bool prime = true;
      for (std::size_t i = 3; i <= n / i; i += 2)
        if (n % i == 0) { prime = false; break; }
      if (prime) return n;
    }
  }
  // createVirtualVariables（:431-455）的叶：复合类型按 Type.complexComponents（组件声明序）递归，标量叶逐个前插。
  struct VLeaf { std::vector<Symbol> mpath; bool is_flow; SourceLoc loc; };
  // v1 只做标量 Real 叶；数组 / stream / 非 Real 叶 → false（调用方 SKIP，数组属增量 B）。
  // elem：comp 以标量元素被引用（`y[1]`，调用方已按 scalar_ref 保证），其自身维度不算数组叶。
  bool virtual_leaves(NodeId comp, const std::vector<Symbol>& mpath, bool flow_in, std::vector<VLeaf>& out,
                      bool elem = false) {
    const CompId cc = I_.nodes[comp].comp;
    const SElement& cel = I_.scode.elems[I_.comps[cc].def];
    if (!elem && (!cel.attr.dims.empty() || !cel.type.dims.empty())) return false;
    const SConnectorType ct = I_.comps[cc].attributes.connector;
    if (ct == SConnectorType::Stream) return false;
    const bool fl = flow_in || ct == SConnectorType::Flow;
    const TypeKind tk = I_.comps[cc].ty.element_type().kind;   // 数组组件的 ty 是 Array（elem 引用取元素类型）
    if (tk != TypeKind::Complex) {
      if (tk != TypeKind::Real) return false;
      out.push_back({mpath, fl, I_.comps[cc].loc});
      return true;
    }
    const NodeId cls = class_of(comp);
    if (!cls.is_valid()) return false;
    for (NodeId mn : I_.trees[I_.node_class(cls).tree].components) {
      if (I_.nodes[mn].kind != NodeKind::Component) continue;
      std::vector<Symbol> p = mpath;
      p.push_back(I_.nodes[mn].name);
      if (!virtual_leaves(mn, p, fl, out)) return false;
    }
    return true;
  }
  // 增广后 expandable 连接器的叶成员（Connection.split 对增广类型：makeConnectorType(isExpandable=false) 全在 potentials、
  // 按名升序，splitImpl2 跳过仍 potentially present 的成员，NFConnector.mo:383-392）。未增广过的（不在任何 expandable 集里的
  // 嵌套 expandable）类型仍是 EXPANDABLE_CONNECTOR → splitImpl 记录分支，按组件声明序（:355-360）。
  bool expandable_leaves(NodeId exp, const std::vector<Symbol>& mpath, std::vector<ConnMember>& out) {
    const NodeId cls = class_of(exp);
    if (!cls.is_valid()) return false;
    std::vector<AugMember> mem;
    if (auto it = aug_.find(exp.raw); it != aug_.end()) mem = it->second;
    else
      for (NodeId mn : I_.trees[I_.node_class(cls).tree].components)
        if (I_.nodes[mn].kind == NodeKind::Component) mem.push_back({I_.nodes[mn].name, mn, {}});
    for (const AugMember& m : mem) {
      std::vector<Symbol> p = mpath;
      p.push_back(m.name);
      if (!m.decl.is_valid()) {   // 虚成员：类型来源端的叶表，挂到本成员路径下
        for (ConnMember x : m.vmembers) {
          x.path.insert(x.path.begin(), p.begin(), p.end());
          if (x.is_stream) x.flow_path.insert(x.flow_path.begin(), p.begin(), p.end());
          out.push_back(std::move(x));
        }
        continue;
      }
      const NodeId mn = m.decl;
      const bool sub_exp = is_expandable_comp(mn);
      if (!sub_exp && !marked_.count(mn.raw)) continue;   // 仍 potentially present → 不拆
      const CompId mc = I_.nodes[mn].comp;
      const SElement& mcel = I_.scode.elems[I_.comps[mc].def];
      if (!mcel.attr.dims.empty() || !mcel.type.dims.empty()) return false;   // 数组成员属增量 B
      if (I_.comps[mc].attributes.variability != SVariability::Var) return false;
      if (sub_exp) { if (!expandable_leaves(mn, p, out)) return false; continue; }
      if (I_.comps[mc].ty.kind == TypeKind::Complex) {
        const NodeId sub = class_of(mn);
        if (!sub.is_valid()) return false;
        const SRestriction sr = I_.scode.elems[I_.nodes[sub].def].restriction;
        if (sr == SRestriction::Connector) { if (!connector_leaves(sub, p, out)) return false; }
        else if (sr == SRestriction::Record) { if (!record_leaves(sub, p, false, out)) return false; }
        else return false;
        continue;
      }
      if (I_.comps[mc].attributes.connector != SConnectorType::Potential) return false;
      out.push_back({std::move(p), false, I_.comps[mc].ty, {}, false, {}});
    }
    return true;
  }
  // expandable 关联图的元素（ConnectionSets 节点，面一律 OUTSIDE：addConnectionToSets :272-282）。
  struct XNode {
    std::vector<Symbol> path; std::vector<std::vector<Id<Expr>>> subs;
    bool expandable;          // cty EXPANDABLE：expandable 端、未声明端的父（:266 强标 EXPANDABLE）、嵌套 expandable
    NodeId node;              // expandable：组件节点；Declared：pp 成员节点；Virtual：无效
    const ConnInfo* end;      // 未声明端本身
    const ConnInfo* partner;  // Virtual：另一端（makeVirtualConnector 取其类型，:284-307）
  };
  // elaborate（:71-122）：分类 → 关联图 → 逐集增广（已声明者 markComponentPresent、未声明者建虚变量）→ 补 expandable /
  // 虚元素端的成员表 → 连接表重排 → 虚变量前插 + 删仍 potentially present 的变量（NFFlatten.mo:2464）+ 重建 flow 表。
  void elaborate_expandable(std::vector<Resolved>& rs) {
    // 1. 分类（sortConnections :126-171），各类内保持自然序（E/U 各前插两次、N 前插后反转）。
    std::vector<std::size_t> E, U, N;
    auto undeclared = [](EndKind k) { return k == EndKind::Declared || k == EndKind::Virtual; };
    for (std::size_t i = 0; i < rs.size(); ++i) {
      const bool ea = rs[i].a.kind == EndKind::Expandable, eb = rs[i].b.kind == EndKind::Expandable;
      const bool ua = undeclared(rs[i].a.kind), ub = undeclared(rs[i].b.kind);
      if (ea || eb) {
        // EXPANDABLE_NON_EXPANDABLE_CONNECTION（InvalidExpandableConnector1）→ omc 报错
        if (!(ea && eb)) { set_unsupported("expandable connected to non-expandable"); return; }
        E.push_back(i);
      } else if (ua || ub) {
        // UNDECLARED_CONNECTION（ExpandableConnectorNonDecl1 / expconn6 / expconn7）→ omc 报错
        if (ua && ub) { set_unsupported("undeclared connected to undeclared"); return; }
        U.push_back(i);
      } else {
        N.push_back(i);
      }
    }
    std::vector<FlatVariable> virt;   // 虚变量（创建序；每个都前插进变量表 → 最终序为其逆）
    std::vector<bool> virt_flow;
    if (!E.empty() || !U.empty()) {
      // 2. 关联图（独立 DisjointSets，全 OUTSIDE）：先 expandable 连接（+ 同名嵌套 expandable，:173-237），再未声明端 ↔
      //    其父 expandable（:239-282）；各按自然序插入。
      std::vector<XNode> xs;
      std::vector<std::int64_t> xlink;
      std::unordered_map<std::string, std::size_t> xindex;
      auto xfind = [&](XNode n) {
        const std::string k = conn_key(n.path, n.subs);
        if (auto it = xindex.find(k); it != xindex.end()) return it->second;
        xs.push_back(std::move(n));
        xlink.push_back(-1);
        xindex.emplace(k, xs.size() - 1);
        return xs.size() - 1;
      };
      auto xmerge = [&](XNode a, XNode b) {
        const std::size_t i = xfind(std::move(a));
        const std::size_t j = xfind(std::move(b));
        ds_union(xlink, ds_root(xlink, i), ds_root(xlink, j));
      };
      auto by_name = [&](NodeId a, NodeId b) { return in_.str(I_.nodes[a].name) < in_.str(I_.nodes[b].name); };
      auto nested = [&](NodeId n) {   // 类型里的嵌套 expandable 成员（makeConnectorType 的 exps，按名升序）
        std::vector<NodeId> r;
        if (const NodeId cls = class_of(n); cls.is_valid())
          for (NodeId mn : I_.trees[I_.node_class(cls).tree].components)
            if (I_.nodes[mn].kind == NodeKind::Component && is_expandable_comp(mn)) r.push_back(mn);
        std::stable_sort(r.begin(), r.end(), by_name);
        return r;
      };
      // getExpandableConnectorsInConnector 前插成降序、配对再前插回升序 → 同名嵌套对按名升序、逐对深度优先并入。
      auto add_exp = [&](auto& self, XNode a, XNode b) -> void {
        const NodeId na = a.node, nb = b.node;
        const std::vector<Symbol> pa = a.path, pb = b.path;
        const std::vector<std::vector<Id<Expr>>> sa = a.subs, sb = b.subs;
        xmerge(std::move(a), std::move(b));
        const std::vector<NodeId> la = nested(na), lb = nested(nb);
        for (NodeId m1 : la)
          for (NodeId m2 : lb) {
            if (I_.nodes[m1].name != I_.nodes[m2].name) continue;
            for (NodeId m : {m1, m2}) {
              const SElement& sel = I_.scode.elems[I_.comps[I_.nodes[m].comp].def];
              if (!sel.attr.dims.empty() || !sel.type.dims.empty()) { set_unsupported("nested expandable connector array"); return; }
            }
            XNode x1{pa, sa, true, m1, nullptr, nullptr}, x2{pb, sb, true, m2, nullptr, nullptr};
            x1.path.push_back(I_.nodes[m1].name);
            x1.subs.resize(x1.path.size());
            x2.path.push_back(I_.nodes[m2].name);
            x2.subs.resize(x2.path.size());
            self(self, std::move(x1), std::move(x2));
            if (result_.unsupported) return;
            break;
          }
      };
      for (std::size_t i : E) {
        const ConnInfo& a = rs[i].a;
        const ConnInfo& b = rs[i].b;
        add_exp(add_exp, XNode{a.path, a.subs, true, a.node, nullptr, nullptr},
                XNode{b.path, b.subs, true, b.node, nullptr, nullptr});
        if (result_.unsupported) return;
      }
      for (std::size_t i : U) {
        const bool a_und = undeclared(rs[i].a.kind);
        const ConnInfo& c = a_und ? rs[i].a : rs[i].b;
        const ConnInfo& other = a_und ? rs[i].b : rs[i].a;
        // 父须是 expandable 组件（经 pp 连接器再下探的 `ec.c.e`：omc 把父强标 EXPANDABLE 硬增广，未做 → SKIP）。
        if (!is_expandable_comp(c.parent)) { set_unsupported("undeclared connector parent not expandable"); return; }
        // 虚元素取另一端的类型：须是标量元素引用（整体数组 / 切片 → 数组型虚元素，属增量 B）。
        if (c.kind == EndKind::Virtual && !other.scalar_ref) { set_unsupported("array-typed virtual connector"); return; }
        XNode pn{c.path, c.subs, true, c.parent, nullptr, nullptr};
        pn.path.pop_back();
        pn.subs.pop_back();
        xmerge(XNode{c.path, c.subs, false, c.node, &c, c.kind == EndKind::Virtual ? &other : nullptr}, std::move(pn));
      }
      // 3. 提取（extractSets :295-342）：根按节点序编号，集合内元素 = 插入序。
      std::vector<std::size_t> xset_of(xs.size(), 0);
      std::vector<std::vector<std::size_t>> xsets;
      for (std::size_t i = 0; i < xs.size(); ++i)
        if (xlink[i] < 0) { xset_of[i] = xsets.size(); xsets.emplace_back(); }
      for (std::size_t i = 0; i < xs.size(); ++i) xsets[xset_of[ds_root(xlink, i)]].push_back(i);
      // 4. 逐集增广（elaborateExpandableSet :309-332 / augmentExpandableConnector :357-429）。
      const Symbol vcomment = in_.intern("virtual variable in expandable connector");
      auto bucket_of = [&](std::size_t q, std::size_t nb) {
        return static_cast<std::size_t>(djb2_long32(in_.str(xs[q].path.back())) % static_cast<std::int64_t>(nb));
      };
      for (const std::vector<std::size_t>& set : xsets) {
        std::vector<std::size_t> exp_conns;                   // expandable 成员，前插 → 逆序
        std::vector<std::vector<std::size_t>> buckets(13);    // exp_set：按末段名去重（isNodeNameEqual），桶内表头在前
        std::size_t esize = 0;
        for (std::size_t k : set) {
          if (xs[k].expandable) { exp_conns.insert(exp_conns.begin(), k); continue; }
          if (xs[k].end->kind == EndKind::Declared) marked_.insert(xs[k].node.raw);   // markComponentPresent（:323）
          bool dup = false;
          for (std::size_t q : buckets[bucket_of(k, buckets.size())])
            if (xs[q].path.back() == xs[k].path.back()) { dup = true; break; }
          if (dup) continue;
          if (esize > buckets.size()) {   // loadFactor > 1 → rehash 到 nextPrime(2·size)，按旧桶序逐个前插
            std::vector<std::vector<std::size_t>> nbk(next_prime(esize * 2));
            for (const std::vector<std::size_t>& b : buckets)
              for (std::size_t q : b) {
                std::vector<std::size_t>& dst = nbk[bucket_of(q, nbk.size())];
                dst.insert(dst.begin(), q);
              }
            buckets = std::move(nbk);
          }
          std::vector<std::size_t>& dst = buckets[bucket_of(k, buckets.size())];
          dst.insert(dst.begin(), k);
          ++esize;
        }
        std::vector<std::size_t> lst;   // toList：逐桶逐元素前插
        for (const std::vector<std::size_t>& b : buckets)
          for (std::size_t q : b) lst.insert(lst.begin(), q);
        for (std::size_t ek : exp_conns) {
          const XNode& ec = xs[ek];
          const NodeId cls = class_of(ec.node);
          if (!cls.is_valid()) { set_unsupported("expandable connector class"); return; }
          std::vector<AugMember> am;
          for (NodeId mn : I_.trees[I_.node_class(cls).tree].components)
            if (I_.nodes[mn].kind == NodeKind::Component) am.push_back({I_.nodes[mn].name, mn, {}});
          for (std::size_t uk : lst) {
            const XNode& u = xs[uk];
            const Symbol nm = u.path.back();
            const NodeId found = find_local(cls, nm);
            if (found.is_valid() && !I_.nodes[found].is_empty()) { marked_.insert(found.raw); continue; }
            // 虚成员：类型来源 = 虚元素端的另一端，或（别的 expandable 里）已声明的同名成员本身。
            const ConnInfo& src = u.end->kind == EndKind::Virtual ? *u.partner : *u.end;
            std::vector<VLeaf> leaves;
            if (!src.node.is_valid() || !virtual_leaves(src.node, {}, false, leaves, /*elem=*/true)) {
              set_unsupported("virtual connector type"); return;
            }
            am.push_back({nm, NodeId{}, src.members});
            for (const VLeaf& lf : leaves) {
              FlatVariable v;
              v.name = ec.path;
              v.name.push_back(nm);
              v.name.insert(v.name.end(), lf.mpath.begin(), lf.mpath.end());
              v.name_subs = ec.subs;
              v.name_subs.resize(v.name.size());
              v.ty = Type::real();
              v.comment = vcomment;
              v.loc = lf.loc;
              virt.push_back(std::move(v));
              virt_flow.push_back(lf.is_flow);
            }
          }
          std::stable_sort(am.begin(), am.end(),
                           [&](const AugMember& x, const AugMember& y) { return in_.str(x.name) < in_.str(y.name); });
          // 节点被数组元素共用：componentApply 覆盖类型（:428），**最后一次增广生效**——别的元素的 E–E 拆分也按它，
          // 可能引用不存在的虚变量，omc 照样输出（YmcExpArr3 探针：`ec[2].x = ec2[2].x`，ec[2].x 未声明也未建）。
          aug_[ec.node.raw] = std::move(am);
        }
      }
      // 5. 成员表：虚元素端 = 另一端的叶表（makeVirtualConnector 的类型）；expandable 端 = 增广后类型的叶表。
      for (std::size_t i : U) {
        if (rs[i].a.kind == EndKind::Virtual) rs[i].a.members = rs[i].b.members;
        if (rs[i].b.kind == EndKind::Virtual) rs[i].b.members = rs[i].a.members;
      }
      for (std::size_t i : E)
        for (ConnInfo* ci : {&rs[i].a, &rs[i].b})
          if (!expandable_leaves(ci->node, {}, ci->members)) { set_unsupported("expandable connector member"); return; }
      // 6. 连接表重排（:113-115）：表序 [expandable…, 未声明…, 普通…] → 自然序（合并按其逆）= 普通、未声明、expandable。
      std::vector<Resolved> ordered;
      ordered.reserve(rs.size());
      for (const std::vector<std::size_t>* grp : {&N, &U, &E})
        for (std::size_t i : *grp) ordered.push_back(std::move(rs[i]));
      rs = std::move(ordered);
    }
    // 7. 变量表：虚变量逐个前插（最终序 = 创建序之逆），再删仍 potentially present 的变量（Variable.isPresent，
    //    NFVariable.mo:300-303；NFFlatten.mo:2464）；flow 表按新变量表重建（collectFlows 在其后，NFFlatten.mo:2466-2468）。
    if (virt.empty() && var_pp_.empty()) return;
    constexpr std::size_t kGone = static_cast<std::size_t>(-1);
    std::vector<FlatVariable>& vars = result_.model.variables;
    std::vector<FlatVariable> nv;
    std::vector<FlowVar> nf;
    for (std::size_t k = virt.size(); k-- > 0;) {
      if (virt_flow[k]) nf.push_back({virt[k].name, virt[k].name_subs, virt[k].ty, virt[k].loc, nv.size(), true});
      nv.push_back(std::move(virt[k]));
    }
    std::vector<std::size_t> remap(vars.size(), kGone);
    for (std::size_t i = 0; i < vars.size(); ++i) {
      if (auto pit = var_pp_.find(i); pit != var_pp_.end() &&
          std::none_of(pit->second.begin(), pit->second.end(), [&](std::uint32_t r) { return marked_.count(r) > 0; }))
        continue;
      remap[i] = nv.size();
      nv.push_back(std::move(vars[i]));
    }
    for (FlowVar& f : flow_vars_)
      if (remap[f.var_idx] != kGone) { f.var_idx = remap[f.var_idx]; nf.push_back(std::move(f)); }
    vars = std::move(nv);
    flow_vars_ = std::move(nf);
    var_pp_.clear();
  }

  // 连接集展开（对标 NFFlatten.resolveConnections :2433 → ConnectionSets.fromConnections + DisjointSets +
  // ConnectEquations.generateEquations；顺序规则与 14 个探针见 docs/research/connect-sets-order.md）。
  // 没有 connect 也要跑：全部 flow 变量作为 INSIDE 单元素集产出 `f = 0.0`。结果整体插到模型自身方程之前（NFFlatten.mo:2497）。
  void process_connects() {
    // 引用了被移除组件的 connect 整条丢弃（条件组件 false，Condition5）；其余才算"活" connect。
    std::vector<ConnectStmt> live;
    for (const ConnectStmt& cs : connects_)
      if (!references_removed(cs.a, cs.prefix) && !references_removed(cs.b, cs.prefix)) live.push_back(cs);
    // 逐条解析（for-connect 的下标 `c[i]` 靠采集时快照的 loop_vars_ 还原；解析后立即清空避免污染）。
    // 每个实参解析成元素列表（整体数组连接已展开），左右按位配对成逐元素 connect
    // （对标 makeConnections，NFConnections.mo:200-206；左右元素数不等 = omc 报错 → SKIP）。
    // 子模型内的 connect：面已按**书写类**本地判定（crefFace 用 firstNonScope，前缀段是 SCOPE 来源，
    // NFComponentRef.mo:429-440），这里再把书写处前缀（含复合数组元素下标）接到路径前面。
    std::vector<Resolved> rs;
    for (const ConnectStmt& cs : live) {
      loop_vars_ = cs.loopvars;
      std::optional<std::vector<ConnInfo>> A = resolve_connector(cs.a, cs.enclosing);
      std::optional<std::vector<ConnInfo>> B = resolve_connector(cs.b, cs.enclosing);
      loop_vars_.clear();
      if (!A || !B) { set_unsupported("connect operand shape"); return; }
      if (A->size() != B->size()) { set_unsupported("connector array size mismatch"); return; }
      const std::vector<std::vector<Id<Expr>>> pre_subs = psubs_to_exprs(cs.psubs);
      auto prefixed = [&](ConnInfo& ci) {
        if (cs.prefix.empty()) return;
        std::vector<Symbol> p = cs.prefix;
        p.insert(p.end(), ci.path.begin(), ci.path.end());
        std::vector<std::vector<Id<Expr>>> s = pre_subs;
        s.resize(cs.prefix.size());
        s.insert(s.end(), ci.subs.begin(), ci.subs.end());
        ci.path = std::move(p);
        ci.subs = std::move(s);
      };
      for (std::size_t i = 0; i < A->size(); ++i) {
        prefixed((*A)[i]);
        prefixed((*B)[i]);
        rs.push_back({std::move((*A)[i]), std::move((*B)[i]), cs.loc});
      }
    }
    // cardinality 计数表（CardinalityTable.fromConnections，NFCardinalityTable.mo:54-92）：在 collectConnections 之后、
    // split/scalarize 之前，按**连接器元素**（数组已展开、成员未拆）统计每个 cref 在连接里出现的次数；键 = 元素 cref 串。
    for (const Resolved& r : rs) {
      ++card_table_[conn_key(r.a.path, r.a.subs)];
      ++card_table_[conn_key(r.b.path, r.b.subs)];
    }
    // expandable 连接器增广 + 删未连接的 potentially present 变量（NFFlatten.mo:2462-2464），在 collectFlows 之前。
    elaborate_expandable(rs);
    if (result_.unsupported) return;
    for (const Resolved& r : rs)
      if (r.a.members.size() != r.b.members.size()) { set_unsupported("connector member mismatch"); return; }
    // 标量叶配对（对标 Connection.split/scalarize 后的逐元素 CONNECTION），自然序：配对序 → 叶成员表序（每层 potential
    // 升序在前、flow 升序在后、子连接器就地展开，见 connector_leaves）→ 数组成员行主序（Connection.scalarize →
    // Connector.scalarize → scalarizeAll，NFConnection.mo:76-98）。任一侧成员为条件假被移除 → 整对丢弃（NFConnection.mo:68）。
    struct LeafPair { const Resolved* r; const ConnMember* m; std::vector<std::int64_t> idx; };
    std::vector<LeafPair> pairs;
    for (const Resolved& r : rs)
      for (const ConnMember& m : r.a.members) {
        const ConnMember* bm = nullptr;
        for (const ConnMember& x : r.b.members) if (x.path == m.path) { bm = &x; break; }
        if (!bm || bm->is_flow != m.is_flow || bm->is_stream != m.is_stream || bm->dims != m.dims) {
          set_unsupported("connector member mismatch"); return;
        }
        if (member_removed(r.a.path, m.path) || member_removed(r.b.path, m.path)) continue;
        std::vector<std::int64_t> idx(m.dims.size(), 1);
        do { pairs.push_back({&r, &m, idx}); } while (next_index(idx, m.dims));
      }

    // ---- 并查集（Util/DisjointSets.mo）：节点 = 标量叶 × 面（Connector.isEqual 含 face，NFConnector.mo:143-148）----
    // 存成成员（cs_*）：stream 算子 inStream/actualStream 要在方程生成后按集合求值。
    std::vector<SetNode>& nodes = cs_nodes_;                 // 插入序（UnorderedMap 的 keys Vector，addEntry :861）
    std::vector<std::int64_t>& link = cs_link_;              // <0 根（值 = −秩）；>0 父节点下标+1
    std::unordered_map<std::string, std::size_t>& index = cs_index_;   // 叶 key + 面 → 节点下标
    auto find_node = [&](const std::string& key, SetNode n) -> std::size_t {   // DisjointSets.find：缺则新增（:207-230）
      auto it = index.find(key);
      if (it != index.end()) return it->second;
      nodes.push_back(std::move(n));
      link.push_back(-1);
      index.emplace(key, nodes.size() - 1);
      return nodes.size() - 1;
    };
    auto root_of = [&](std::size_t i) { return cs_root(i); };
    auto unite = [&](std::size_t s1, std::size_t s2) { ds_union(link, s1, s2); };
    // 1. 先插全部 flow 叶（INSIDE 面）：collectFlows 按变量声明序 cons → 反序（NFConnections.mo:139-166；
    //    ConnectionSets.fromConnections :78 先 fold flows）。增广出的虚 flow 另加 OUTSIDE 面（:158-163，后 cons →
    //    表里排在 INSIDE 前；ExpandableConnectorComplex1 / expconn1 的 `bus.a.i = 0.0` 次序据此）。
    for (auto it = flow_vars_.rbegin(); it != flow_vars_.rend(); ++it) {
      Expr lhs;
      lhs.kind = ExprKind::Cref;
      lhs.ty = it->ty;
      for (std::size_t k = 0; k < it->name.size(); ++k)
        lhs.cref.parts.push_back({it->name[k], k < it->subs.size() ? it->subs[k] : std::vector<Id<Expr>>{}});
      const std::string nk = conn_key(it->name, it->subs);
      const Id<Expr> fc = add(std::move(lhs));
      if (it->augmented) find_node(nk + "#o", {fc, true, true, it->ty, it->loc, false, nk, {}, {}});
      find_node(nk + "#i", {fc, false, true, it->ty, it->loc, false, nk, {}, {}});
    }
    // 2. 合并（fromConnections :82 fold connections → merge）：合并序 = 语句反序 → 元素反序 → 叶反序 → 成员元素反序
    //    （collectConnections/makeConnections 的 cons + List.mapFlat 块内反转的净效果，List.mo:2308-2341）= 自然序整体反转；
    //    每对先 lhs 后 rhs（merge :202-203）。
    for (auto it = pairs.rbegin(); it != pairs.rend(); ++it) {
      const LeafPair& p = *it;
      const std::vector<Id<Expr>> msubs = idx_to_exprs(p.idx);
      auto endpoint = [&](const ConnInfo& c) -> std::size_t {
        const std::string nk = leaf_key(c, *p.m, p.idx);
        SetNode n{member_cref(c.path, c.subs, p.m->path, msubs, p.m->ty), c.outside, p.m->is_flow, p.m->ty, p.r->loc,
                  false, nk, {}, {}};
        if (p.m->is_stream) {   // 关联 flow：同一连接器元素的唯一 flow（associatedFlowCref :1106-1126）
          n.is_stream = true;
          n.flow_cref = member_cref(c.path, c.subs, p.m->flow_path, {}, Type::real());
          n.flow_key = conn_key(c.path, c.subs);
          for (Symbol s : p.m->flow_path) { n.flow_key += std::to_string(s.id); n.flow_key.push_back('.'); }
        }
        return find_node(nk + (c.outside ? "#o" : "#i"), std::move(n));
      };
      const std::size_t a = endpoint(p.r->a);
      const std::size_t b = endpoint(p.r->b);
      unite(root_of(a), root_of(b));
    }
    // 3. 提取（extractSets :295-342）：根按节点下标序编号；集合内元素 = 插入序。
    std::vector<std::vector<std::size_t>>& sets = cs_sets_;
    std::vector<std::size_t>& set_of = cs_set_of_;
    set_of.assign(nodes.size(), 0);
    for (std::size_t i = 0; i < nodes.size(); ++i)
      if (link[i] < 0) { set_of[i] = sets.size(); sets.emplace_back(); }
    for (std::size_t i = 0; i < nodes.size(); ++i) sets[set_of[root_of(i)]].push_back(i);
    // 4. 生成方程，集合号**反序**（generateEquations 逐集前插，NFConnectEquations.mo:126）。
    std::vector<Id<FlatEquation>> conn_eqs;
    for (auto sit = sets.rbegin(); sit != sets.rend(); ++sit) {
      const std::vector<std::size_t>& s = *sit;
      const SetNode& head = nodes[s[0]];
      if (head.is_stream) { emit_stream_set(s, conn_eqs); continue; }   // generateStreamEquations（:423-474）
      if (!head.is_flow) {
        // potential（generatePotentialEquations :202-236）：集首变率 > parameter → `head = 其余`；parameter / constant →
        // 逐个相等断言（makeEqualityAssert，见 emit_equality_assert）。变率只看集首（YmcCCMix1 探针：parameter 集首连
        // 连续变量也出断言）。
        const FlatVariable* hv = set_var(head.name_key);
        const bool as_assert = hv && (hv->var_kind == FlatVarKind::Parameter || hv->var_kind == FlatVarKind::Constant);
        for (std::size_t k = 1; k < s.size(); ++k) {
          if (as_assert) { emit_equality_assert(head, nodes[s[k]], conn_eqs); continue; }
          FlatEquation fe;
          fe.kind = FlatEqKind::Equality;
          fe.loc = head.loc;
          fe.a = head.cref;
          fe.b = nodes[s[k]].cref;
          conn_eqs.push_back(result_.model.add_eq(std::move(fe)));
        }
        continue;
      }
      // flow：单元素（未连接 inside）直接 `f = 0.0`（generateFlowEquations :386-387，不取负）；否则 makeFlowExp 求和
      //（inside 取正、outside 取负，:406-421）。元素序恒为 [inside…, outside…]（inside 预插入在前），取负折叠后打印
      // `in1 + … - out1 - …`，全 outside 时 `-(out1 + …)`（InOutOut/InInOutOut/Star4 探针）。
      FlatEquation fe;
      fe.kind = FlatEqKind::Equality;
      fe.loc = head.loc;
      fe.b = lit_real(0.0);
      if (s.size() == 1) {
        fe.a = head.cref;
      } else {
        Id<Expr> ins, outs;
        for (std::size_t k : s) {
          Id<Expr>& acc = nodes[k].outside ? outs : ins;
          acc = acc.is_valid() ? bin_expr(BinOp::Add, acc, nodes[k].cref) : nodes[k].cref;
        }
        if (!ins.is_valid()) {
          fe.a = neg_expr(outs);
        } else {
          fe.a = ins;
          for (std::size_t k : s) if (nodes[k].outside) fe.a = bin_expr(BinOp::Sub, fe.a, nodes[k].cref);
        }
      }
      conn_eqs.push_back(result_.model.add_eq(std::move(fe)));
    }
    // 5. 连接方程整体置于模型自身方程之前（NFFlatten.mo:2497 `listAppend(conn_eql, flatModel.equations)`；EqPlace 探针）。
    result_.model.equations.insert(result_.model.equations.begin(), conn_eqs.begin(), conn_eqs.end());
  }

  // 连接集节点对应的变量（name_key 与 var_key 同一编码）；找不到（如增广"最后一次生效"引用的未建虚变量）→ nullptr。
  const FlatVariable* set_var(const std::string& key) {
    ensure_var_index();
    auto it = var_index_.find(key);
    return it == var_index_.end() ? nullptr : &result_.model.variables[it->second];
  }
  // makeEqualityAssert（NFConnectEquations.mo:290-341）：Real → `abs(a - b) <= 0.0`（Modelica 禁 Real 的 ==），其它类型 →
  // `a == b`；消息 EQ_ASSERT_STR（:82-83），默认 error 级。常量由 EvalConstants 折成值（代入字面量绑定再化简），
  // `assert(true, …)` 被 SimplifyModel 删掉（YmcCCEq1 / YmcCCConst3 探针；ConstantConnector2 → `assert(false, …)`）。
  // 连接已逐元素标量化（Connections.scalarize 默认全摊），故无数组 for 包裹。
  void emit_equality_assert(const SetNode& x, const SetNode& y, std::vector<Id<FlatEquation>>& out) {
    auto val = [&](const SetNode& n) {
      const FlatVariable* v = set_var(n.name_key);
      return (v && v->var_kind == FlatVarKind::Constant && v->binding.is_valid() && is_lit(v->binding)) ? v->binding : n.cref;
    };
    const Id<Expr> a = val(x), b = val(y);
    Expr rel;
    rel.kind = ExprKind::Relation;
    rel.ty = Type::boolean();
    if (x.ty.element_type().kind == TypeKind::Real) {
      rel.rel = RelOp::Le;
      rel.a = call_expr("abs", {bin_expr(BinOp::Sub, a, b)}, Type::real());
      rel.b = lit_real(0.0);
    } else {
      rel.rel = RelOp::Eq;
      rel.a = a;
      rel.b = b;
    }
    const Id<Expr> cond = simplify_expr(add(std::move(rel)));
    const Expr& ce = result_.model.exprs[cond];
    if (ce.kind == ExprKind::BoolLit && ce.bval) return;
    FlatEquation fe;
    fe.kind = FlatEqKind::Assert;
    fe.loc = x.loc;
    fe.a = cond;
    fe.b = lit_string(in_.intern("Connected constants/parameters must be equal"));
    out.push_back(result_.model.add_eq(std::move(fe)));
  }

  // ---- stream 连接器（MLS §15；omc NFConnectEquations.mo :423-1142）----
  std::size_t cs_root(std::size_t i) const { return ds_root(cs_link_, i); }
  Id<Expr> call_expr(const char* name, std::vector<Id<Expr>> args, Type ty) {
    Expr r;
    r.kind = ExprKind::Call;
    r.call_name = {in_.intern(name)};
    r.args = std::move(args);
    r.ty = ty;
    return add(std::move(r));
  }
  // 连接器类恰有一个 flow 成员 → 其名（stream 成员的关联 flow，associatedFlowCref :1106-1126）；否则 nullopt。
  std::optional<Symbol> single_flow_name(NodeId cls) {
    if (!cls.is_valid() || I_.nodes[cls].is_empty() || !I_.nodes[cls].def.is_valid()) return std::nullopt;
    if (I_.scode.elems[I_.nodes[cls].def].restriction != SRestriction::Connector) return std::nullopt;
    std::optional<Symbol> f;
    int n = 0;
    for (NodeId mn : I_.trees[I_.node_class(cls).tree].components)
      if (I_.comps[I_.nodes[mn].comp].attributes.connector == SConnectorType::Flow) { ++n; f = I_.nodes[mn].name; }
    return n == 1 ? f : std::nullopt;
  }
  // inStream / actualStream（MLS §15.2-15.3）：实参须是 stream 变量 cref（否则 omc 报错 InStreamNoStream1 /
  // *NonCref1）、下标须为字面量（参数下标要结构化 final 副作用 → 增量 B；变量下标 omc 报错 *Variability1）。
  // 连接集尚未建好 → 保持符号 Call（args = {stream cref, 关联 flow cref}），连接解析后按集合求值。
  Id<Expr> stream_op_call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix, std::string_view name) {
    auto bad = [&](const char* why) { set_unsupported(why); return lit_real(0.0); };
    if (e.args.size() != 1 || !is_cref_exp(e.args[0])) return bad("stream operator argument");
    const Cref& c = I_.absyn.crefs[I_.absyn.exps[e.args[0]].cref];
    if (c.leading_dot || c.parts.size() < 2) return bad("stream operator argument");
    // 逐段解析 + 下标求值（ComponentRef.evaluateSubscripts，NFConnectEquations.mo:994；连接器下标须参数变率，
    // NFTyping.mo:3191-3200）：整数 / 迭代变量字面量；本地参数 `c[n]` → 取值并记为结构参数（打印 final，
    // ActualStreamVariability2）；变量下标（omc 报错 *Variability1）等其余 → SKIP。每段须逐维选出标量元素。
    std::vector<Symbol> path;
    std::vector<std::vector<Id<Expr>>> subs;
    NodeId cur = find_local(enclosing, c.parts[0].name);
    NodeId decl = enclosing;
    for (std::size_t i = 0; i < c.parts.size(); ++i) {
      if (i > 0) {
        const NodeId cls = I_.comps[I_.nodes[cur].comp].class_inst;
        decl = cls;
        cur = (cls.is_valid() && !I_.nodes[cls].is_empty()) ? find_local(cls, c.parts[i].name) : Inst::empty_node();
      }
      if (!cur.is_valid() || I_.nodes[cur].is_empty() || !I_.nodes[cur].comp.is_valid()) return bad("stream operator argument");
      const SElement& sel = I_.scode.elems[I_.comps[I_.nodes[cur].comp].def];
      if (c.parts[i].subscripts.size() != sel.attr.dims.size() + sel.type.dims.size()) return bad("stream operator subscript");
      std::vector<Id<Expr>> seg;
      for (Id<Exp> sub : c.parts[i].subscripts) {
        std::optional<std::int64_t> iv = subscript_int(sub);
        if (!iv) iv = structural_subscript(sub, enclosing, prefix);
        if (!iv) return bad("stream operator subscript");
        seg.push_back(lit_int(*iv));
      }
      path.push_back(c.parts[i].name);
      subs.push_back(std::move(seg));
    }
    if (I_.comps[I_.nodes[cur].comp].attributes.connector != SConnectorType::Stream) return bad("stream operator argument");
    const std::optional<Symbol> fname = single_flow_name(decl);
    if (!fname) return bad("stream connector");
    if (!prefix.empty()) {   // 子模型内：与 connect 相同的前缀化（书写处前缀 + 复合数组元素下标）
      std::vector<Symbol> p = prefix;
      p.insert(p.end(), path.begin(), path.end());
      std::vector<std::vector<Id<Expr>>> ps = psubs_to_exprs(cur_psubs_);
      ps.resize(prefix.size());
      ps.insert(ps.end(), subs.begin(), subs.end());
      path = std::move(p);
      subs = std::move(ps);
    }
    const Id<Expr> s = member_cref(path, subs, {}, {}, Type::real());
    std::vector<Symbol> fpath = path;
    fpath.back() = *fname;
    std::vector<std::vector<Id<Expr>>> fsubs = subs;
    fsubs.back().clear();
    const Id<Expr> f = member_cref(fpath, fsubs, {}, {}, Type::real());
    uses_stream_ops_ = true;
    return call_expr(name == "inStream" ? "inStream" : "actualStream", {s, f}, Type::real());
  }
  // 变量身份 key（与 conn_key / leaf_key 同一编码；末段下标 = FlatVariable.subscripts）。
  std::string var_key(const FlatVariable& v) {
    std::vector<std::vector<Id<Expr>>> subs = v.name_subs;
    subs.resize(v.name.size());
    if (!subs.empty()) subs.back() = v.subscripts;
    return conn_key(v.name, subs);
  }
  void ensure_var_index() {
    if (!var_index_.empty()) return;
    for (std::size_t i = 0; i < result_.model.variables.size(); ++i) var_index_.emplace(var_key(result_.model.variables[i]), i);
  }
  bool is_lit(Id<Expr> x) {
    const ExprKind k = result_.model.exprs[x].kind;
    return k == ExprKind::IntLit || k == ExprKind::RealLit || k == ExprKind::BoolLit || k == ExprKind::StringLit ||
           k == ExprKind::EnumLit;
  }
  // 变量某属性（lookupVarAttr :1128-）：是否存在、原式、若是数值字面量则其值。
  struct AttrVal { bool present = false; std::optional<double> lit; Id<Expr> expr; };
  AttrVal var_attr_info(const std::string& vkey, const char* attr) {
    ensure_var_index();
    AttrVal r;
    auto it = var_index_.find(vkey);
    if (it == var_index_.end()) return r;
    const Symbol an = in_.intern(attr);
    for (const FlatVarAttr& a : result_.model.variables[it->second].attributes) {
      if (a.name != an) continue;
      r.present = true;
      r.expr = a.value;
      const Expr& ev = result_.model.exprs[a.value];
      if (ev.kind == ExprKind::RealLit) r.lit = ev.rval;
      else if (ev.kind == ExprKind::IntLit) r.lit = static_cast<double>(ev.ival);
      return r;
    }
    return r;
  }
  // 非破坏性代入：cref（key 命中 vals）→ 对应字面量，其余节点按需复制。
  Id<Expr> clone_subst(Id<Expr> id, const std::unordered_map<std::string, Id<Expr>>& vals) {
    if (!id.is_valid()) return id;
    Expr c = result_.model.exprs[id];
    if (c.kind == ExprKind::Cref) {
      auto it = vals.find(expr_cref_key(c.cref));
      return it != vals.end() ? it->second : id;
    }
    bool changed = false;
    for (Id<Expr>& k : c.args) { const Id<Expr> n = clone_subst(k, vals); changed = changed || !(n == k); k = n; }
    for (Id<Expr>* k : {&c.a, &c.b, &c.c}) {
      if (!k->is_valid()) continue;
      const Id<Expr> n = clone_subst(*k, vals);
      changed = changed || !(n == *k);
      *k = n;
    }
    return changed ? add(std::move(c)) : id;
  }
  // 收集表达式里的参数/常量 cref 及其字面量绑定。遇到更高变率（变量 / time / 找不到）→ ok=false（不可结构化求值）；
  // 参数绑定非字面量 → ymc 无法复刻 Ceval 递归 → SKIP；fixed=false 的参数不结构化（Structural.isExpressionNotFixed）。
  void collect_param_crefs(Id<Expr> id, std::unordered_map<std::string, Id<Expr>>& vals,
                           std::vector<std::string>& params, bool& ok) {
    if (!id.is_valid() || !ok) return;
    const Expr e = result_.model.exprs[id];
    if (e.kind == ExprKind::Cref) {
      const std::string k = expr_cref_key(e.cref);
      auto it = var_index_.find(k);
      if (it == var_index_.end()) { ok = false; return; }
      const FlatVariable& v = result_.model.variables[it->second];
      if (v.var_kind != FlatVarKind::Parameter && v.var_kind != FlatVarKind::Constant) { ok = false; return; }
      if (!v.binding.is_valid() || !is_lit(v.binding)) { set_unsupported("structural parameter binding"); ok = false; return; }
      const Symbol fixed = in_.intern("fixed");
      for (const FlatVarAttr& a : v.attributes)
        if (a.name == fixed && result_.model.exprs[a.value].kind == ExprKind::BoolLit && !result_.model.exprs[a.value].bval) {
          ok = false; return;
        }
      vals.emplace(k, v.binding);
      if (v.var_kind == FlatVarKind::Parameter) params.push_back(k);
      return;
    }
    for (Id<Expr> k : e.args) collect_param_crefs(k, vals, params, ok);
    collect_param_crefs(e.a, vals, params, ok);
    collect_param_crefs(e.b, vals, params, ok);
    collect_param_crefs(e.c, vals, params, ok);
  }
  // isNoFlow（:938-976）：属性字面量直接判；参数变率 → Structural.markExp（记为结构参数）+ Ceval 后判；
  // 更高变率 → pred(非字面量) = 假。求值不改属性原式（折叠在 apply_structural_params 统一做，保 evaluateFlowDirection
  // 看到的仍是未折叠原式，ActualStreamMinMax2）。
  std::optional<double> noflow_attr(const std::string& vkey, const char* attr) {
    const AttrVal av = var_attr_info(vkey, attr);
    if (!av.present) return std::nullopt;
    if (av.lit) return av.lit;
    std::unordered_map<std::string, Id<Expr>> vals;
    std::vector<std::string> params;
    bool ok = true;
    collect_param_crefs(av.expr, vals, params, ok);
    if (!ok) return std::nullopt;
    const Id<Expr> y = simplify_expr(clone_subst(av.expr, vals));
    const Expr& ey = result_.model.exprs[y];
    if (ey.kind != ExprKind::RealLit && ey.kind != ExprKind::IntLit) { set_unsupported("structural attribute evaluation"); return std::nullopt; }
    for (const std::string& p : params) record_structural(p);
    return ey.kind == ExprKind::RealLit ? ey.rval : static_cast<double>(ey.ival);
  }
  // isNoFlowInside / isNoFlowOutside（:918-936）：inside 的 flow min ≥ 0、outside 的 flow max ≤ 0 → 无流。
  bool no_flow_inside(const SetNode& n) { const std::optional<double> v = noflow_attr(n.flow_key, "min"); return v && *v >= 0; }
  bool no_flow_outside(const SetNode& n) { const std::optional<double> v = noflow_attr(n.flow_key, "max"); return v && *v <= 0; }
  // 结构参数（被连接算子求值结构化，Structural.markExp）：打印 final；EvalConstants 效果只做"变量属性里引用它们"
  // 一处（折成字面量），其余位置（绑定 / 方程 / 算法 / 折不成字面量）引用 → SKIP（全局代入未做）。
  void record_structural(const std::string& k) {
    if (std::find(structural_keys_.begin(), structural_keys_.end(), k) == structural_keys_.end()) structural_keys_.push_back(k);
  }
  bool refs_keys(Id<Expr> id, const std::unordered_map<std::string, Id<Expr>>& vals) {
    if (!id.is_valid()) return false;
    const Expr& e = result_.model.exprs[id];
    if (e.kind == ExprKind::Cref) return vals.count(expr_cref_key(e.cref)) > 0;
    for (Id<Expr> k : e.args) if (refs_keys(k, vals)) return true;
    return refs_keys(e.a, vals) || refs_keys(e.b, vals) || refs_keys(e.c, vals);
  }
  bool eq_refs_keys(Id<FlatEquation> id, const std::unordered_map<std::string, Id<Expr>>& vals) {
    const FlatEquation& fe = result_.model.eqs[id];
    if (refs_keys(fe.a, vals) || refs_keys(fe.b, vals) || refs_keys(fe.c, vals)) return true;
    for (const FlatEqBranch& br : fe.branches) {
      if (refs_keys(br.cond, vals)) return true;
      for (Id<FlatEquation> b : br.body) if (eq_refs_keys(b, vals)) return true;
    }
    for (Id<FlatEquation> b : fe.body) if (eq_refs_keys(b, vals)) return true;
    for (Id<FlatEquation> b : fe.else_body) if (eq_refs_keys(b, vals)) return true;
    return false;
  }
  void apply_structural_params() {
    if (structural_keys_.empty() || result_.unsupported) return;
    ensure_var_index();
    std::unordered_map<std::string, Id<Expr>> vals;
    for (const std::string& k : structural_keys_) {
      auto it = var_index_.find(k);
      if (it == var_index_.end()) { set_unsupported("structural parameter"); return; }
      FlatVariable& v = result_.model.variables[it->second];
      if (!v.binding.is_valid() || !is_lit(v.binding)) { set_unsupported("structural parameter binding"); return; }
      v.is_final = true;
      vals.emplace(k, v.binding);
    }
    for (FlatVariable& v : result_.model.variables) {
      for (FlatVarAttr& a : v.attributes) {
        if (!refs_keys(a.value, vals)) continue;
        a.value = simplify_expr(clone_subst(a.value, vals));
        if (!is_lit(a.value)) { set_unsupported("structural parameter in attribute"); return; }
      }
      if (v.binding.is_valid() && refs_keys(v.binding, vals)) { set_unsupported("structural parameter referenced"); return; }
    }
    for (const std::vector<Id<FlatEquation>>* eqs : {&result_.model.equations, &result_.model.initial_equations})
      for (Id<FlatEquation> id : *eqs)
        if (eq_refs_keys(id, vals)) { set_unsupported("structural parameter referenced"); return; }
    if (!result_.model.algorithms.empty() || !result_.model.initial_algorithms.empty()) {
      set_unsupported("structural parameter with algorithms");   // 算法段引用检查未做 → 保守 SKIP
    }
  }
  // 下标是本地参数/常量 cref（`c[n]`）：取值（eval_scode）；参数记为结构参数（打印 final，ActualStreamVariability2）。
  std::optional<std::int64_t> structural_subscript(Id<Exp> s, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const Exp& ex = I_.absyn.exps[s];
    if (ex.kind != ExpKind::CrefExp) return std::nullopt;
    const Cref& c = I_.absyn.crefs[ex.cref];
    if (c.leading_dot || c.parts.size() != 1 || !c.parts[0].subscripts.empty()) return std::nullopt;
    const NodeId n = find_local(enclosing, c.parts[0].name);
    if (!n.is_valid() || I_.nodes[n].is_empty() || !I_.nodes[n].comp.is_valid()) return std::nullopt;
    const SVariability var = I_.comps[I_.nodes[n].comp].attributes.variability;
    if (var != SVariability::Param && var != SVariability::Const) return std::nullopt;
    std::optional<CVal> cv = eval_scode(s, scode_scope_of(enclosing), 0, /*allow_param=*/true, nullptr);
    if (!cv || cv->k != CVal::K::Int) return std::nullopt;
    if (var == SVariability::Param) {
      std::vector<Symbol> p = prefix;
      p.push_back(c.parts[0].name);
      std::vector<std::vector<Id<Expr>>> ps = psubs_to_exprs(cur_psubs_);
      ps.resize(prefix.size());
      ps.push_back({});
      record_structural(conn_key(p, ps));
    }
    return cv->i;
  }
  // streamSumEquationExp（:499-551）：
  //   inStreamDiv((Σ_out max(f,eps)*inStream(s) + Σ_in max(-f,eps)*s) / (Σ_out max(f,eps) + Σ_in max(-f,eps)), fallback)；
  // sumMap 后项前插（:553-573）；eps = FLOW_THRESHOLD(1e-7) × flow 的 nominal（makePositiveMaxCall :673-711；
  // InStreamNominalThreshold 实跑：1e-7 / 1e-10 / 1.9999999999999998e-4）。两边都空 → fallback。
  Id<Expr> stream_sum(const std::vector<std::size_t>& outs, const std::vector<std::size_t>& ins, Id<Expr> fallback, int depth) {
    if (outs.empty() && ins.empty()) return fallback;
    auto pmax = [&](Id<Expr> flow, const SetNode& n) {
      // 阈值 = FLOW_THRESHOLD × nominal 原式（makePositiveMaxCall :688-696；字面量由化简折成一个数）。
      Id<Expr> thr = lit_real(1e-7);
      const AttrVal nom = var_attr_info(n.flow_key, "nominal");
      if (nom.present) thr = simplify_expr(bin_expr(BinOp::Mul, thr, nom.expr));
      return call_expr("$OMC$PositiveMax", {flow, thr}, Type::real());
    };
    auto sum = [&](const std::vector<std::size_t>& v, auto term) {
      Id<Expr> acc = term(cs_nodes_[v[0]]);
      for (std::size_t i = 1; i < v.size(); ++i) acc = bin_expr(BinOp::Add, term(cs_nodes_[v[i]]), acc);
      return acc;
    };
    auto o1 = [&](const SetNode& n) {
      const Id<Expr> m = pmax(n.flow_cref, n);
      return bin_expr(BinOp::Mul, m, eval_instream(n.name_key, n.cref, depth + 1));
    };
    auto i1 = [&](const SetNode& n) { const Id<Expr> m = pmax(neg_expr(n.flow_cref), n); return bin_expr(BinOp::Mul, m, n.cref); };
    auto o2 = [&](const SetNode& n) { return pmax(n.flow_cref, n); };
    auto i2 = [&](const SetNode& n) { return pmax(neg_expr(n.flow_cref), n); };
    Id<Expr> num, den;
    if (outs.empty()) { num = sum(ins, i1); den = sum(ins, i2); }
    else if (ins.empty()) { num = sum(outs, o1); den = sum(outs, o2); }
    else {
      const Id<Expr> no = sum(outs, o1), ni = sum(ins, i1);
      num = bin_expr(BinOp::Add, no, ni);
      const Id<Expr> dO = sum(outs, o2), dI = sum(ins, i2);
      den = bin_expr(BinOp::Add, dO, dI);
    }
    return call_expr("$OMC$inStreamDiv", {bin_expr(BinOp::Div, num, den), fallback}, Type::real());
  }
  // evaluateInStream（:814-841）+ generateInStreamExp（:843-900）：按 **INSIDE 面**查所在集合（不在任何集合 → 本身）；
  // isNoFlowMinMax 精简（查询者本身按名保留）；{本身} → 本身；两 inside → 对方；一内一外 → 对方的 inStream；
  // 其余 → streamSumEquationExp（fallback = 本身）。
  Id<Expr> eval_instream(const std::string& name_key, Id<Expr> cref, int depth) {
    if (depth > 64) { set_unsupported("inStream recursion"); return cref; }
    auto it = cs_index_.find(name_key + "#i");
    if (it == cs_index_.end()) return cref;
    const std::vector<std::size_t> set = cs_sets_[cs_set_of_[cs_root(it->second)]];
    std::vector<std::size_t> red;
    for (std::size_t k : set) {
      const SetNode& n = cs_nodes_[k];
      if (n.name_key == name_key) { red.push_back(k); continue; }
      if (n.outside ? no_flow_outside(n) : no_flow_inside(n)) continue;
      red.push_back(k);
    }
    if (result_.unsupported) return cref;
    if (red.size() == 1 && !cs_nodes_[red[0]].outside) return cref;
    if (red.size() == 2) {
      const std::size_t o = cs_nodes_[red[0]].name_key == name_key ? red[1] : red[0];
      const bool in0 = !cs_nodes_[red[0]].outside, in1 = !cs_nodes_[red[1]].outside;
      if (in0 && in1) return cs_nodes_[o].cref;
      if (in0 != in1) return eval_instream(cs_nodes_[o].name_key, cs_nodes_[o].cref, depth + 1);
    }
    std::vector<std::size_t> outs, ins;
    for (std::size_t k : red) {
      if (cs_nodes_[k].outside) outs.push_back(k);
      else if (cs_nodes_[k].name_key != name_key) ins.push_back(k);
    }
    return stream_sum(outs, ins, cref, depth);
  }
  // evaluateActualStream（:978-1016）+ evaluateFlowDirection（:1044-1076）：由 flow 的字面量 min/max 定向——
  // 正 → inStream(s)；负 → s；不定 → `if f > 0.0 then inStream(s) else s`。
  Id<Expr> eval_actualstream(Id<Expr> s, Id<Expr> f) {
    const std::string sk = expr_cref_key(result_.model.exprs[s].cref);
    const std::string fk = expr_cref_key(result_.model.exprs[f].cref);
    // 只认 Real 字面量（:1057-1075）；属性在但非字面量（参数表达式，尚未被 EvalConstants 折）→ 不定。
    const AttrVal mn = var_attr_info(fk, "min"), mx = var_attr_info(fk, "max");
    int dir = 0;
    if ((mn.present && !mn.lit) || (mx.present && !mx.lit)) dir = 0;
    else if (mn.present && !mx.present) dir = *mn.lit >= 0 ? 1 : 0;
    else if (!mn.present && mx.present) dir = *mx.lit <= 0 ? -1 : 0;
    else if (mn.present && mx.present)
      dir = (*mn.lit >= 0 && *mx.lit >= *mn.lit) ? 1 : (*mx.lit <= 0 && *mn.lit <= *mx.lit) ? -1 : 0;
    if (dir == 1) return eval_instream(sk, s, 0);
    if (dir == -1) return s;
    const Id<Expr> in = eval_instream(sk, s, 0);
    Expr cond;
    cond.kind = ExprKind::Relation;
    cond.rel = RelOp::Gt;
    cond.a = f;
    cond.b = lit_real(0.0);
    cond.ty = Type::boolean();
    const Id<Expr> ce = add(std::move(cond));
    Expr r;
    r.kind = ExprKind::IfExp;
    r.a = ce;
    r.b = in;
    r.c = s;
    r.ty = Type::real();
    return add(std::move(r));
  }
  // generateStreamEquations（:423-474）+ streamEquationGeneral（:476-497）：inside 先去掉无流者；
  // ({_},{}) / ({_,_},{}) → 无；({},{o1,o2}) → o1 = inStream(o2), o2 = inStream(o1)；({i},{o}) → i = o；
  // 其余 → 每个 outside 一条 `o = inStreamDiv(…, 0)`（cons → 反序；带注释 " equation generated from stream connection"）。
  void emit_stream_set(const std::vector<std::size_t>& s, std::vector<Id<FlatEquation>>& out) {
    std::vector<std::size_t> outs, ins;
    for (std::size_t k : s) {
      if (cs_nodes_[k].outside) outs.push_back(k);
      else if (!no_flow_inside(cs_nodes_[k])) ins.push_back(k);
    }
    const SourceLoc loc = cs_nodes_[s[0]].loc;
    auto eq = [&](Id<Expr> a, Id<Expr> b, Symbol cmt) {
      FlatEquation fe;
      fe.kind = FlatEqKind::Equality;
      fe.a = a;
      fe.b = b;
      fe.comment = cmt;
      fe.loc = loc;
      return result_.model.add_eq(std::move(fe));
    };
    if (outs.empty() && (ins.size() == 1 || ins.size() == 2)) return;
    if (ins.empty() && outs.size() == 2) {
      const std::string k0 = cs_nodes_[outs[0]].name_key, k1 = cs_nodes_[outs[1]].name_key;
      const Id<Expr> c0 = cs_nodes_[outs[0]].cref, c1 = cs_nodes_[outs[1]].cref;
      out.push_back(eq(c0, eval_instream(k1, c1, 0), Symbol{}));
      out.push_back(eq(c1, eval_instream(k0, c0, 0), Symbol{}));
      return;
    }
    if (ins.size() == 1 && outs.size() == 1) {
      out.push_back(eq(cs_nodes_[ins[0]].cref, cs_nodes_[outs[0]].cref, Symbol{}));
      return;
    }
    std::vector<std::size_t> red_out;
    for (std::size_t k : outs) if (!no_flow_outside(cs_nodes_[k])) red_out.push_back(k);
    const Symbol cmt = in_.intern(" equation generated from stream connection");
    std::vector<Id<FlatEquation>> gen;
    for (std::size_t e : outs) {
      std::vector<std::size_t> others;
      for (std::size_t k : red_out) if (cs_nodes_[k].name_key != cs_nodes_[e].name_key) others.push_back(k);
      const Id<Expr> lhs = cs_nodes_[e].cref;
      const Id<Expr> rhs = stream_sum(others, ins, lit_int(0), 0);
      gen.push_back(eq(lhs, rhs, cmt));
    }
    out.insert(out.end(), gen.rbegin(), gen.rend());
  }
  bool is_stream_call(Id<Expr> id, const char* which) {
    if (!id.is_valid()) return false;
    const Expr& e = result_.model.exprs[id];
    return e.kind == ExprKind::Call && e.call_name.size() == 1 && in_.str(e.call_name[0]) == which;
  }
  // 就地把 inStream/actualStream 调用改写成求值结果（evaluateOperators :130-191）；返回是否有替换。
  bool subst_stream(Id<Expr> id) {
    const Expr e = result_.model.exprs[id];   // 拷贝：求值会 add，不跨 add 持引用
    if (e.kind == ExprKind::Call && e.args.size() == 2 && (is_stream_call(id, "inStream") || is_stream_call(id, "actualStream"))) {
      const bool ins = is_stream_call(id, "inStream");
      const Id<Expr> r = ins ? eval_instream(expr_cref_key(result_.model.exprs[e.args[0]].cref), e.args[0], 0)
                             : eval_actualstream(e.args[0], e.args[1]);
      const Expr rv = result_.model.exprs[r];
      result_.model.exprs[id] = rv;
      return true;
    }
    // `flow * actualStream(stream)` 要包 smooth(0, …)（evaluateActualStreamMul :1018-1042）——未做 → SKIP。
    if (e.kind == ExprKind::Binary && e.bin == BinOp::Mul &&
        (is_stream_call(e.a, "actualStream") || is_stream_call(e.b, "actualStream"))) {
      set_unsupported("flow * actualStream (smooth wrap)");
      return false;
    }
    bool changed = false;
    for (Id<Expr> k : e.args) changed = subst_stream(k) || changed;
    for (Id<Expr> k : {e.a, e.b, e.c}) if (k.is_valid()) changed = subst_stream(k) || changed;
    return changed;
  }
  void subst_stream_eq(Id<FlatEquation> id) {
    auto sub = [&](Id<Expr>& x) { if (x.is_valid() && subst_stream(x)) x = simplify_expr(x); };
    FlatEquation& fe = result_.model.eqs[id];   // 本趟不新增方程节点，引用稳定
    sub(fe.a);
    sub(fe.b);
    sub(fe.c);
    for (FlatEqBranch& br : fe.branches) {
      sub(br.cond);
      for (Id<FlatEquation> b : br.body) subst_stream_eq(b);
    }
    for (Id<FlatEquation> b : fe.body) subst_stream_eq(b);
    for (Id<FlatEquation> b : fe.else_body) subst_stream_eq(b);
  }
  bool expr_has_stream_op(Id<Expr> id) {
    if (!id.is_valid()) return false;
    if (is_stream_call(id, "inStream") || is_stream_call(id, "actualStream")) return true;
    const Expr& e = result_.model.exprs[id];
    for (Id<Expr> k : e.args) if (expr_has_stream_op(k)) return true;
    return expr_has_stream_op(e.a) || expr_has_stream_op(e.b) || expr_has_stream_op(e.c);
  }
  bool stmt_has_stream_op(Id<FlatStatement> id) {
    const FlatStatement& st = result_.model.stmts[id];
    if (expr_has_stream_op(st.a) || expr_has_stream_op(st.b) || expr_has_stream_op(st.c)) return true;
    for (const FlatStmtBranch& br : st.branches) {
      if (expr_has_stream_op(br.cond)) return true;
      for (Id<FlatStatement> b : br.body) if (stmt_has_stream_op(b)) return true;
    }
    for (Id<FlatStatement> b : st.body) if (stmt_has_stream_op(b)) return true;
    for (Id<FlatStatement> b : st.else_body) if (stmt_has_stream_op(b)) return true;
    return false;
  }
  // evaluateConnectionOperators（NFFlatten.mo:2565-2576）：绑定、方程、初始方程里的 stream 算子按集合求值；
  // 算法段 omc 不求值（:2575 TODO）→ 残留调用 ymc 无法照印 → SKIP。
  void evaluate_stream_ops() {
    if (!uses_stream_ops_ || result_.unsupported) return;
    for (FlatVariable& v : result_.model.variables)
      if (v.binding.is_valid() && subst_stream(v.binding)) v.binding = simplify_expr(v.binding);
    for (Id<FlatEquation> id : result_.model.equations) subst_stream_eq(id);
    for (Id<FlatEquation> id : result_.model.initial_equations) subst_stream_eq(id);
    for (const std::vector<FlatAlgorithm>* algs : {&result_.model.algorithms, &result_.model.initial_algorithms})
      for (const FlatAlgorithm& a : *algs)
        for (Id<FlatStatement> st : a.stmts)
          if (stmt_has_stream_op(st)) { set_unsupported("stream operator in algorithm"); return; }
  }

  // ---- cardinality（MLS §3.7.2.3；omc NFCardinalityTable + NFConnectEquations.evaluateOperators :156-157）----
  // 实参须是**标量连接器** cref（类名 / 数组连接器 → omc 报错，CardinalityInvalidArg1/2），且只能出现在 if 方程/assert 的
  // 条件里（CardinalityInvalidContext1/2）。求值时连接尚未解析 → 保持符号调用，实参 = 前缀化后的连接器 cref（与 connect
  // 同样的书写处前缀 + 复合数组元素下标）；process_connects 后按元素级出现次数替换成 Integer 字面量再折叠 if/assert。
  Id<Expr> cardinality_call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    if (!card_allowed_) { set_unsupported("cardinality outside if/assert condition"); return lit_int(0); }
    if (e.args.size() != 1 || !is_cref_exp(e.args[0])) { set_unsupported("cardinality argument"); return lit_int(0); }
    std::optional<std::vector<ConnInfo>> ci = resolve_connector(e.args[0], enclosing);
    if (!ci || ci->size() != 1) { set_unsupported("cardinality argument"); return lit_int(0); }
    ConnInfo c = std::move((*ci)[0]);
    if (!prefix.empty()) {
      std::vector<Symbol> p = prefix;
      p.insert(p.end(), c.path.begin(), c.path.end());
      std::vector<std::vector<Id<Expr>>> s = psubs_to_exprs(cur_psubs_);
      s.resize(prefix.size());
      s.insert(s.end(), c.subs.begin(), c.subs.end());
      c.path = std::move(p);
      c.subs = std::move(s);
    }
    Expr r;
    r.kind = ExprKind::Call;
    r.call_name = {I_.absyn.crefs[e.cref].parts[0].name};
    r.ty = Type::integer();
    r.args = {member_cref(c.path, c.subs, {}, {}, Type::real())};
    uses_cardinality_ = true;
    return add(std::move(r));
  }
  // 类型化 cref 的身份 key：与 conn_key(path, subs) 同一编码（段 id + 各下标 [整数] + '.'），供 cardinality 实参查表。
  std::string expr_cref_key(const ExprCref& c) {
    std::string k;
    for (const ExprCrefPart& p : c.parts) {
      k += std::to_string(p.name.id);
      for (Id<Expr> s : p.subscripts) {
        const Expr& se = result_.model.exprs[s];
        k.push_back('[');
        if (se.kind == ExprKind::IntLit) k += std::to_string(se.ival);
        else k += "#" + std::to_string(s.raw);
        k.push_back(']');
      }
      k.push_back('.');
    }
    return k;
  }
  // 就地把 cardinality(cref) 改写成 Integer 字面量（evaluateCardinality :94-103，未出现 → 0）；返回是否有替换。
  bool subst_cardinality(Id<Expr> id) {
    Expr& e = result_.model.exprs[id];
    if (e.kind == ExprKind::Call && e.call_name.size() == 1 && in_.str(e.call_name[0]) == "cardinality" &&
        e.args.size() == 1 && result_.model.exprs[e.args[0]].kind == ExprKind::Cref) {
      auto it = card_table_.find(expr_cref_key(result_.model.exprs[e.args[0]].cref));
      const std::int64_t n = it == card_table_.end() ? 0 : it->second;
      e.kind = ExprKind::IntLit;
      e.ival = n;
      e.ty = Type::integer();
      e.args.clear();
      e.call_name.clear();
      return true;
    }
    bool changed = false;
    const std::vector<Id<Expr>> kids = e.args;
    for (Id<Expr> k : kids) changed = subst_cardinality(k) || changed;
    for (Id<Expr> k : {e.a, e.b, e.c}) if (k.is_valid()) changed = subst_cardinality(k) || changed;
    return changed;
  }
  // 替换后按 NFSimplifyModel 折叠：assert 条件恒真 → 删（:215-219）；if 分支真 → 首个保留分支则内联体、否则成为 else 并
  // 截断，假 → 丢弃，其余保留（simplifyIfEqBranches :510-573）。只重新化简发生过替换的表达式，其余输出保持不变。
  void evaluate_cardinality() {
    if (!uses_cardinality_) return;
    for (FlatVariable& v : result_.model.variables)
      if (v.binding.is_valid() && subst_cardinality(v.binding)) v.binding = simplify_expr(v.binding);
    result_.model.equations = fold_card_eqs(result_.model.equations);
    result_.model.initial_equations = fold_card_eqs(result_.model.initial_equations);
  }
  std::vector<Id<FlatEquation>> fold_card_eqs(const std::vector<Id<FlatEquation>>& eqs) {
    std::vector<Id<FlatEquation>> out;
    for (Id<FlatEquation> id : eqs) fold_card_eq(id, out);
    return out;
  }
  void fold_card_eq(Id<FlatEquation> id, std::vector<Id<FlatEquation>>& out) {
    auto subst = [&](Id<Expr>& x) { if (x.is_valid() && subst_cardinality(x)) x = simplify_expr(x); };
    FlatEquation& fe = result_.model.eqs[id];   // 本趟不新增方程节点，引用稳定
    switch (fe.kind) {
      case FlatEqKind::Assert: {
        subst(fe.a);
        const Expr& c = result_.model.exprs[fe.a];
        if (c.kind == ExprKind::BoolLit && c.bval) return;   // assert(true, …) → 删除
        out.push_back(id);
        return;
      }
      case FlatEqKind::If: {
        std::vector<FlatEqBranch> kept;
        for (FlatEqBranch& br : fe.branches) {
          subst(br.cond);
          std::vector<Id<FlatEquation>> body = fold_card_eqs(br.body);
          const Expr& c = result_.model.exprs[br.cond];
          if (c.kind == ExprKind::BoolLit) {
            if (!c.bval) continue;                                                   // 假 → 丢弃
            if (kept.empty()) { out.insert(out.end(), body.begin(), body.end()); return; }   // 真且无前置 → 内联体
            fe.branches = std::move(kept);                                           // 真 → 成为 else，截断其余
            fe.else_body = std::move(body);
            out.push_back(id);
            return;
          }
          kept.push_back({br.cond, std::move(body)});
        }
        std::vector<Id<FlatEquation>> els = fold_card_eqs(fe.else_body);
        if (kept.empty()) { out.insert(out.end(), els.begin(), els.end()); return; }   // 分支全假 → 只剩 else 体
        fe.branches = std::move(kept);
        fe.else_body = std::move(els);
        out.push_back(id);
        return;
      }
      case FlatEqKind::When:
        for (FlatEqBranch& br : fe.branches) { subst(br.cond); br.body = fold_card_eqs(br.body); }
        out.push_back(id);
        return;
      case FlatEqKind::For:
        subst(fe.a); subst(fe.b); subst(fe.c);
        fe.body = fold_card_eqs(fe.body);
        out.push_back(id);
        return;
      default:
        subst(fe.a); subst(fe.b); subst(fe.c);
        out.push_back(id);
        return;
    }
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
  std::unordered_map<std::uint32_t, TypeKind> stmt_iters_;      // for **语句**（KEPT loop）的迭代变量→元素类型（符号，不内联）
  std::unordered_map<std::uint32_t, Type> func_params_;         // 函数体发射期：形参/局部名→类型（cref 符号解析成裸名）
  std::unordered_set<std::uint32_t> emitting_funcs_;            // 正在发射的函数（检测递归/互调，首批 SKIP）
  bool in_if_ = false;   // 正在收集 if 分支体（connect 不能进分支体 → SKIP）
  struct FlowVar {
    std::vector<Symbol> name; std::vector<std::vector<Id<Expr>>> subs; Type ty; SourceLoc loc;
    std::size_t var_idx;   // model.variables 下标（elaborate_expandable 删 pp 变量后按它重建本表）
    bool augmented;        // 增广进 expandable 的虚 flow：另加 OUTSIDE 面（collectFlows，NFConnections.mo:158-163）
  };
  std::vector<FlowVar> flow_vars_;   // flow 变量（run() 末尾按 outside/未连接补 `=0` 方程）
  // expandable 连接器（elaborate_expandable）。
  std::vector<std::uint32_t> cur_pp_chain_;   // 正在发射的叶组件的 pp 链（flatten_class 设、emit_* 读）
  std::unordered_map<std::size_t, std::vector<std::uint32_t>> var_pp_;   // pp 变量下标 → pp 链
  std::unordered_set<std::uint32_t> marked_;  // markComponentPresent 过的组件节点 raw
  // 增广成员：decl 有效 = 已声明成员；否则虚成员，vmembers = 其类型的叶表（取自类型来源端，makeVirtualConnector）。
  struct AugMember { Symbol name; NodeId decl; std::vector<ConnMember> vmembers; };
  // expandable 组件节点 → 最后一次增广后的成员表（按名升序，makeConnectorType；节点被数组元素共用，:417-428）。
  std::unordered_map<std::uint32_t, std::vector<AugMember>> aug_;
  // cardinality（NFCardinalityTable）：连接器元素 cref key → 在连接里出现的次数；调用先留符号，连接解析后替换并折叠。
  std::unordered_map<std::string, std::int64_t> card_table_;
  bool uses_cardinality_ = false;
  // 连接集（process_connects 建；stream 算子在方程生成后按集合求值）。节点 = 标量叶 × 面（SetNode 见 ConnInfo 旁）。
  std::vector<SetNode> cs_nodes_;
  std::vector<std::int64_t> cs_link_;
  std::unordered_map<std::string, std::size_t> cs_index_;
  std::vector<std::vector<std::size_t>> cs_sets_;
  std::vector<std::size_t> cs_set_of_;
  bool uses_stream_ops_ = false;
  std::unordered_map<std::string, std::size_t> var_index_;   // 变量 key → model.variables 下标（flow 属性查询）
  std::vector<std::string> structural_keys_;   // 连接算子求值中被结构化的参数（变量 key；Structural.markExp）
  bool card_allowed_ = false;   // 只在 if 方程 / assert 的条件表达式里为 true（其它上下文 omc 报错 → SKIP）
  struct ConnectStmt {
    Id<Exp> a, b; NodeId enclosing; std::vector<Symbol> prefix; SourceLoc loc;
    std::unordered_map<std::uint32_t, std::int64_t> loopvars;   // 采集时的 for 迭代变量快照（连接器下标 `c[i]` 求值）
    PSubs psubs;                                                 // 书写处前缀的逐段下标（子模型数组元素 `a[i]` 内的 connect）
  };
  std::vector<ConnectStmt> connects_;            // 攒下的 connect()，run() 末尾展开
  std::unordered_set<std::string> removed_;             // 条件为假被移除的组件路径 key（引用它的 connect 整条删）
  std::unordered_set<std::uint32_t> removed_nodes_;     // 被移除组件的节点 raw（方程/绑定里引用它 → omc 报错 → SKIP）
};

}  // namespace

FlattenResult flatten(Inst& inst, Interner& in, NodeId root) { return Flattener(inst, in).run(root); }

}  // namespace ymc
