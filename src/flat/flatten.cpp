// 压平实现（S3 第一里程碑）。锚点见 flatten.h 头注与各处 R11/R13/R14 引用。
#include "flat/flatten.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <deque>
#include <functional>
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
#include "base/strings.h"   // unescape_modelica_string（字符串字面量；注释在 SCode 里已解过转义，直接用）

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
      // homotopy：`input Real actual; input Real simplified; output Real`（NFModelicaBuiltin.mo:295-298，无特殊定型）→ 实参提 Real
      //（NA2 探针 `homotopy(simplified = 1, actual = x)` → `homotopy(x, 1.0)`）。
      {"homotopy", 2, false, BFArg::Real, BFRes::Real},
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
  // 外层组件合并后的方向（mergeComponentAttributes :212-250：子组件方向 = mergeDirection(外层, 自身)，外层无方向取自身、
  // 自身无方向取外层、两边都有 → INVALID_TYPE_PREFIX 报错；YmcDir1 探针 `input R ri` → `input Real ri.x`）。
  SDirection dir_inh = SDirection::Bidir;
  // 外层记录的整体绑定拆不开、已挪成（初始）方程：本层字段一律无绑定（outerBinding = EMPTY_BINDING，NFFlatten.mo:1004），
  // parameter 字段补 fixed = false（flattenSimpleComponent :730-735）；嵌套记录字段同样（其自身的整体绑定也丢）。
  bool unbound_fields = false;
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
  Flattener(Inst& inst, Interner& in) : I_(inst), in_(in) {
    // 合成节点的预留（见 synth）：上限在这里算——成员按声明序初始化，I_ 声明在类尾，默认成员初始化式里还用不了。
    I_.absyn.exps.reserve(exp_limit_ = I_.absyn.exps.size() + kSynthCap);
    I_.absyn.crefs.reserve(cref_limit_ = I_.absyn.crefs.size() + kSynthCap);
    I_.scode.mods.reserve(mod_limit_ = I_.scode.mods.size() + kSynthCap);
  }

  using ScodeScope = std::vector<Id<SElement>>;   // SCODE 作用域链：类 SElement，内→外（前置声明，供 eval_dim 等用）
  using PSubs = std::vector<std::vector<std::int64_t>>;   // 与 prefix 平行的逐段下标（复合数组 `a[1].x`；空段=无下标）

  FlattenResult run(NodeId root) {
    root_ = root;
    synth_mod_floor_ = I_.scode.mods.size();
    // 全名（R13 §2）：v1 根类是顶层类，全名 = 类名。
    result_.model.name = {I_.nodes[root].name};
    const SElement& el = I_.scode.elems[I_.nodes[root].def];
    result_.model.comment = class_comment(el.cmt.str);
    if (el.def.kind != SClassDefKind::Parts) set_unsupported("short/derived/enum root class");
    // 根类不可是 function / package（NFInst.checkInstanceRestriction :4186-4205，INST_INVALID_RESTRICTION）。
    if (el.restriction == SRestriction::Package || el.restriction == SRestriction::Function ||
        el.restriction == SRestriction::ExternalFunction || el.restriction == SRestriction::OperatorFunction)
      set_unsupported("root class is package/function");
    // 顶层作用域的类树：同名元素 → DOUBLE_DECLARATION_OF_ELEMENTS（NFClassTree.addLocalElementConflict :1653-1676）；
    // 与 NF 顶层内建类型同名（Clock2 的 `model Clock`，NFModelicaBuiltin.mo）同样冲突。
    if (has_duplicate_names(I_.scode.program)) set_unsupported("duplicate element declaration");
    for (Id<SElement> e : I_.scode.program) {
      const std::string_view n = in_.str(I_.scode.elems[e].name);
      if (n == "Clock" || n == "StateSelect" || n == "Uncertainty" || n == "AssertionLevel" || n == "Connections")
        set_unsupported("class name collides with builtin");
    }
    // 实例化 partial 根类（NFInst.checkPartialClass，PartialClass1）→ omc 报错。
    if (el.is_partial) set_unsupported("partial root class");
    // __OpenModelica_commandLineOptions（-d=evaluateAllParameters / mergeComponents …）会重写整个
    // 展平结果，是 OMC 专属变换，v1 不做 → 标 unsupported（driver 退 66，记 SKIP 不是 FAIL）。
    if (el.cmt.annotation.is_valid())
      for (const SSubMod& s : I_.scode.mods[el.cmt.annotation].subs)
        if (s.ident == in_.intern("__OpenModelica_commandLineOptions"))
          set_unsupported("__OpenModelica_commandLineOptions annotation");
    flatten_class(root, {}, {}, /*protected_ctx=*/false, ModCtx{}, {});
    // 标识符里的非 ASCII 字符：MLS §2.3.1 的 Q-CHAR 只有 ASCII，omc 的词法器报错（`'跳ねるボール'`：japaneselanguage/
    // JBouncingBall；LX 探针 `Real 'höhe'`）。ymc 的词法器放行了（lexer 归用户，任务卡 T-S3-09）→ 这里先挡根类名与变量名。
    {
      auto non_ascii = [&](Symbol sy) {
        for (unsigned char ch : in_.str(sy)) if (ch >= 0x80) return true;
        return false;
      };
      bool bad = non_ascii(el.name);
      for (const FlatVariable& v : result_.model.variables)
        for (Symbol sy : v.name) bad = bad || non_ascii(sy);
      if (bad) set_unsupported("non-ASCII identifier");
    }
    // 连接集展开（DisjointSets 序：potential 相等、flow 求和、未连接 flow `=0`），整体插到模型自身方程之前
    //（NFFlatten.mo:2497）。顶层（outside）连接器的 flow 既进求和集（OUTSIDE 面）又留 INSIDE 单元素集 → 同时有
    // `-(c1.f + c2.f) = 0` 与 `c1.f = 0`（Connector.isEqual 含 face，NFConnector.mo:143-148）。
    process_connects();
    // 连接解析后才能求 inStream/actualStream 与 cardinality（evaluateConnectionOperators，NFFlatten.mo:2565-2576），
    // 随后按 SimplifyModel 折叠。
    evaluate_stream_ops();
    apply_structural_params();   // 连接算子求值中结构化的参数：final + 属性折叠（NFEvalConstants 效果，受限）
    simplify_model();            // cardinality 求值 + 方程 / 语句的收尾折叠
    // 某变量名同时出现在 removed_（另一同名声明条件为假被移除）：redeclare 一个 replaceable-conditional 成员的
    // 语义（保留原条件 / 禁止带条件 redeclare）v1 未做 → SKIP（RedeclareElementCondition1/2/3）。
    if (!removed_.empty())
      for (const FlatVariable& v : result_.model.variables) {
        std::vector<std::vector<Id<Expr>>> ns = v.name_subs;   // 组件路径（逐段下标，不含叶数组下标）
        ns.resize(v.name.size());
        if (removed_.count(conn_key(v.name, ns))) { set_unsupported("conditional component redeclared"); break; }
      }
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
    check_discrete_reals();
    prune_functions();
    relabel_type_dims();
    return std::move(result_);
  }

  // 类型维数组的元素下标换回标签（见 sub_ordinal）：按已发射变量建"组（名字链 + 逐段下标，叶段留空）→ 各维的标签表"，
  // 再把全部 Cref 节点里落在类型维上的整数下标换成对应的标签（`y[2]` → `y[E.two]`、`b[1]` → `b[false]`）。
  void relabel_type_dims() {
    std::unordered_map<std::string, std::vector<std::vector<Id<Expr>>>> groups;
    for (const FlatVariable& v : result_.model.variables) {
      bool any = false;
      for (Id<Expr> sx : v.subscripts) any = any || result_.model.exprs[sx].kind != ExprKind::IntLit;
      if (!any) continue;
      std::vector<std::vector<Id<Expr>>> ns = v.name_subs;
      ns.resize(v.name.size());
      ns.back().clear();
      std::vector<std::vector<Id<Expr>>>& g = groups[conn_key(v.name, ns)];
      if (g.size() < v.subscripts.size()) g.resize(v.subscripts.size());
      for (std::size_t k = 0; k < v.subscripts.size(); ++k) {
        if (result_.model.exprs[v.subscripts[k]].kind == ExprKind::IntLit) continue;
        const std::optional<std::int64_t> o = sub_ordinal(v.subscripts[k]);
        if (!o || *o < 1) continue;
        if (g[k].size() < static_cast<std::size_t>(*o)) g[k].resize(static_cast<std::size_t>(*o));
        g[k][static_cast<std::size_t>(*o) - 1] = v.subscripts[k];
      }
    }
    if (groups.empty()) return;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(result_.model.exprs.exprs.size()); ++i) {
      Expr& e = result_.model.exprs[Id<Expr>::from_index(i)];
      if (e.kind != ExprKind::Cref || e.cref.parts.empty() || e.cref.parts.back().subscripts.empty() ||
          e.cref.resolved_node == kFnLocal)
        continue;
      std::vector<Symbol> names;
      std::vector<std::vector<Id<Expr>>> ns;
      for (const ExprCrefPart& cp : e.cref.parts) {
        names.push_back(cp.name);
        ns.push_back(cp.subscripts);
      }
      ns.back().clear();
      const auto it = groups.find(conn_key(names, ns));
      if (it == groups.end()) continue;
      std::vector<Id<Expr>>& ls = e.cref.parts.back().subscripts;
      for (std::size_t k = 0; k < ls.size() && k < it->second.size(); ++k) {
        const std::vector<Id<Expr>>& labels = it->second[k];
        if (labels.empty()) continue;
        auto label = [&](Id<Expr> x) {   // 整数字面量 → 标签；别的原样
          const Expr& xe = result_.model.exprs[x];
          if (xe.kind != ExprKind::IntLit || xe.ival < 1 || static_cast<std::size_t>(xe.ival) > labels.size() ||
              !labels[static_cast<std::size_t>(xe.ival) - 1].is_valid())
            return x;
          return labels[static_cast<std::size_t>(xe.ival) - 1];
        };
        Expr& sx = result_.model.exprs[ls[k]];
        if (sx.kind == ExprKind::IntLit) {
          ls[k] = label(ls[k]);
        } else if (sx.kind == ExprKind::Range && !sx.c.is_valid()) {   // 切片 `y[E.one:E.two]`
          sx.a = label(sx.a);
          sx.b = label(sx.b);
        } else if (sx.kind == ExprKind::Array) {                        // 下标数组 `y[{E.three, E.one}]`
          for (Id<Expr>& a : sx.args) a = label(a);
        }
      }
    }
  }

  // NFVerifyModel.checkDiscreteReal :345-390：discrete Real 变量必须出现在某个 when 体（含其中嵌套 if / for）里等式或
  // 赋值的左侧，否则 DISCRETE_REAL_UNDEFINED 报错（Discrete2 / DiscreteDeclRecord / DiscreteDeclType）。按去下标的路径比。
  void check_discrete_reals() {
    if (result_.unsupported) return;
    bool any = false;
    for (const FlatVariable& v : result_.model.variables)
      if (v.var_kind == FlatVarKind::Discrete && v.ty.element_type().kind == TypeKind::Real) any = true;
    if (!any) return;
    std::unordered_set<std::string> defined;
    auto lhs = [&](Id<Expr> e) {
      if (!e.is_valid()) return;
      const Expr& x = result_.model.exprs[e];
      if (x.kind != ExprKind::Cref) return;
      std::vector<Symbol> nm;
      for (const ExprCrefPart& cp : x.cref.parts) nm.push_back(cp.name);
      defined.insert(path_key(nm));
    };
    auto eqn = [&](auto& self, Id<FlatEquation> id, bool in_when) -> void {
      const FlatEquation& q = result_.model.eqs[id];
      if (q.kind == FlatEqKind::Equality && in_when) lhs(q.a);
      const bool w = in_when || q.kind == FlatEqKind::When;
      for (const FlatEqBranch& br : q.branches) for (Id<FlatEquation> b : br.body) self(self, b, w);
      for (Id<FlatEquation> b : q.body) self(self, b, w);
      for (Id<FlatEquation> b : q.else_body) self(self, b, w);
    };
    auto stmt = [&](auto& self, Id<FlatStatement> id, bool in_when) -> void {
      const FlatStatement& st = result_.model.stmts[id];
      if (st.kind == FlatStmtKind::Assign && in_when) lhs(st.a);
      const bool w = in_when || st.kind == FlatStmtKind::When;
      for (const FlatStmtBranch& br : st.branches) for (Id<FlatStatement> b : br.body) self(self, b, w);
      for (Id<FlatStatement> b : st.body) self(self, b, w);
      for (Id<FlatStatement> b : st.else_body) self(self, b, w);
    };
    for (Id<FlatEquation> id : result_.model.equations) eqn(eqn, id, false);
    for (const FlatAlgorithm& a : result_.model.algorithms) for (Id<FlatStatement> id : a.stmts) stmt(stmt, id, false);
    for (const FlatVariable& v : result_.model.variables)
      if (v.var_kind == FlatVarKind::Discrete && v.ty.element_type().kind == TypeKind::Real && !defined.count(path_key(v.name))) {
        set_unsupported("discrete Real not assigned in when");
        return;
      }
  }

  // 多个 extends 时，某个 extends 的名字（首段）恰是另一基类里的局部类名：继承后再查会找到那个类（NFInst.instExtends
  // "Found other base class for extends X after instantiating extends"，InvalidInheritedExtends1/2）→ SKIP。
  bool extends_shadowed(const SElement& def) {
    std::vector<const SElement*> exts;
    for (Id<SElement> e : def.def.elements)
      if (I_.scode.elems[e].kind == SElementKind::Extends) exts.push_back(&I_.scode.elems[e]);
    if (exts.size() < 2) return false;
    for (const SElement* a : exts) {
      if (a->base_path.empty()) continue;
      for (const SElement* b : exts) {
        if (a == b || b->base_path.empty()) continue;
        ScodeScope ds;
        const Id<SElement> bc = find_scode_element_at(b->base_path[0], {}, ds);
        if (!bc.is_valid() || b->base_path.size() != 1 || I_.scode.elems[bc].kind != SElementKind::Class) continue;
        for (Id<SElement> m : I_.scode.elems[bc].def.elements)
          if (I_.scode.elems[m].kind == SElementKind::Class && I_.scode.elems[m].name == a->base_path[0]) return true;
      }
    }
    return false;
  }
  bool bad_enum_type(const SElement& c, const ScodeScope& scope) {
    if (c.type.path.size() != 1) return false;
    ScodeScope ds;
    Id<SElement> e = find_scode_element_at(c.type.path[0], c.type.leading_dot ? ScodeScope{} : scope, ds);
    for (int guard = 0; e.is_valid() && guard < 20; ++guard) {
      const SElement& el = I_.scode.elems[e];
      if (el.kind != SElementKind::Class) return false;
      if (el.def.kind == SClassDefKind::Enumeration) {
        std::unordered_set<std::uint32_t> seen;
        for (const SEnumItem& it : el.def.enum_items) {
          const std::string_view n = in_.str(it.literal);
          if (!seen.insert(it.literal.id).second || n == "quantity" || n == "min" || n == "max" || n == "start" || n == "fixed")
            return true;
        }
        return false;
      }
      if (el.def.kind != SClassDefKind::Derived || el.def.derived_type.path.size() != 1) return false;
      ScodeScope nds;
      e = find_scode_element_at(el.def.derived_type.path[0], ds, nds);
      ds = std::move(nds);
    }
    return false;
  }
  // extends 路径：中间段经过 partial 类（PartialLookup3）；或基类里有与 extends 名同名的局部类（继承后再查会找到它，
  // ExtendInherited1 "Found other base class for extends A after instantiating extends"）；或路径上有 replaceable 的类
  //（基类名须传递地非 replaceable，MLS §7.1.4；1.25.0 报 "Class 'B' in 'extends A.<B>' is replaceable …"，
  // InvalidReplaceableExtends3 / 4——归 inst 的查找，任务卡 T-S3-07，这里先挡住）→ SKIP。
  bool bad_extends_path(const SElement& x, const ScodeScope& scope) {
    if (x.base_path.empty()) return false;
    ScodeScope ds;
    Id<SElement> e = find_scode_element_at(x.base_path[0], x.base_leading_dot ? ScodeScope{} : scope, ds);
    if (e.is_valid() && I_.scode.elems[e].kind == SElementKind::Class && I_.scode.elems[e].prefixes.is_replaceable) return true;
    for (std::size_t k = 0; e.is_valid() && k + 1 < x.base_path.size(); ++k) {
      const SElement& el = I_.scode.elems[e];
      if (el.kind != SElementKind::Class) return false;
      if (el.is_partial) return true;
      Id<SElement> nxt;
      for (Id<SElement> m : el.def.elements)
        if (I_.scode.elems[m].name == x.base_path[k + 1] && I_.scode.elems[m].kind == SElementKind::Class) { nxt = m; break; }
      e = nxt;
      if (e.is_valid() && I_.scode.elems[e].prefixes.is_replaceable) return true;
    }
    if (e.is_valid() && I_.scode.elems[e].kind == SElementKind::Class)
      for (Id<SElement> m : I_.scode.elems[e].def.elements)
        if (I_.scode.elems[m].kind == SElementKind::Class && I_.scode.elems[m].name == x.base_path[0]) return true;
    return false;
  }
  // 组件类型路径的合法性：中间段经过 partial 类（PartialLookup1/2，"name lookup is not allowed in partial classes"）或含
  // 非常量组件 / 方程的 package（PackageIllegal / DependsMutual：查找时实例化包 → NFRestriction.assertOnlyConstantComponents），
  // 或类型本身是 partial 类（Partial1 "Component has partial type"）→ SKIP。scope = 组件声明处的作用域链。
  bool bad_type_path(const SElement& c, const ScodeScope& scope) {
    if (c.type.path.empty()) return false;
    ScodeScope ds;
    Id<SElement> e = find_scode_element_at(c.type.path[0], c.type.leading_dot ? ScodeScope{} : scope, ds);
    if (!e.is_valid()) {   // SCODE 查找不到（encapsulated 边界，Encapsulated1）而又不是内建类型 → SKIP
      const std::string_view n = in_.str(c.type.path[0]);
      return !(c.type.path.size() == 1 && (n == "Real" || n == "Integer" || n == "Boolean" || n == "String" ||
                                           n == "StateSelect" || n == "AssertionLevel" || n == "Clock"));
    }
    for (std::size_t k = 0; e.is_valid(); ++k) {
      const SElement& el = I_.scode.elems[e];
      if (el.kind != SElementKind::Class) return false;
      const bool last = k + 1 == c.type.path.size();
      if (el.is_partial && !c.prefixes.is_replaceable) return true;
      if (last) {
        // 组件类型是 package / function（inst6 在 1.25.0 能过但较新 OMC 报错——只对 package 判，function 型组件另有语义）、
        // 无基类型的长 type 类（TypeMissingBaseType1）、未指定的枚举 `enumeration(:)`（EnumUnspecified1）→ SKIP。
        if (el.restriction == SRestriction::Type && el.def.kind == SClassDefKind::Parts) {
          bool has_ext = false;
          for (Id<SElement> m : el.def.elements) if (I_.scode.elems[m].kind == SElementKind::Extends) has_ext = true;
          if (!has_ext) return true;
        }
        if (el.def.kind == SClassDefKind::Enumeration && el.def.enum_unspecified) return true;
        return false;
      }
      if (el.restriction == SRestriction::Package) {
        if (!el.def.equations.empty() || !el.def.initial_equations.empty() || !el.def.algorithms.empty()) return true;
        for (Id<SElement> m : el.def.elements)
          if (I_.scode.elems[m].kind == SElementKind::Component && I_.scode.elems[m].attr.var != SVariability::Const) return true;
      }
      Id<SElement> nxt;
      for (Id<SElement> m : el.def.elements)
        if (I_.scode.elems[m].name == c.type.path[k + 1] && I_.scode.elems[m].kind == SElementKind::Class) { nxt = m; break; }
      e = nxt;
    }
    return false;
  }
  // 一组元素里有同名的类 / 组件（NFClassTree.addLocalElementConflict）。
  bool has_duplicate_names(const std::vector<Id<SElement>>& elems) {
    std::unordered_set<std::uint32_t> names;
    for (Id<SElement> e : elems) {
      const SElement& el = I_.scode.elems[e];
      if (el.kind != SElementKind::Class && el.kind != SElementKind::Component) continue;
      if (!names.insert(el.name.id).second) return true;
    }
    return false;
  }
  // 同一修饰里同名子修饰：至多一个带绑定时合并（子修饰递归同查），两个都带绑定 → DUPLICATE_MODIFICATIONS
  //（NFModifier.mergeLocal :715-756；DuplicateMod1/2/3/6、Modification8 `b(a.x = 1.0, a(x = 2.0))`、DuplicateBaseTypeMod）。
  bool mod_has_duplicates(const std::vector<Id<SMod>>& group) {
    std::vector<std::pair<std::uint32_t, std::vector<Id<SMod>>>> by_name;
    for (Id<SMod> m : group) {
      if (!m.is_valid()) continue;
      for (const SSubMod& sm : I_.scode.mods[m].subs) {
        auto it = std::find_if(by_name.begin(), by_name.end(), [&](const auto& g) { return g.first == sm.ident.id; });
        if (it == by_name.end()) by_name.push_back({sm.ident.id, {sm.mod}});
        else it->second.push_back(sm.mod);
      }
    }
    for (const auto& g : by_name) {
      int bindings = 0;
      for (Id<SMod> m : g.second) if (m.is_valid() && I_.scode.mods[m].binding.is_valid()) ++bindings;
      if (bindings > 1) return true;
      if (g.second.size() > 1 ? mod_has_duplicates(g.second) : mod_has_duplicates({g.second[0]})) return true;
    }
    return false;
  }
  // 本类的结构性校验（与 OMC 报错对齐，命中即 SKIP）：局部同名元素、受限类的段 / 可见性（NFRestriction.checkClass
  // :404-497）、记录 / 连接器成员前缀（NFAttributes.checkDeclaredComponentAttributes :402-429）、组件 / extends 修饰里的
  // 重复修饰、元素级 redeclare（ymc 未做：Illegal redeclare 判定与替换语义）、组件类型名命中本类组件（Lookup8
  // `Cytosol Cytosol`：类型查找先找到组件 → "Expected X to be a class"）。
  void check_class_rules(const SElement& def, const ScodeScope& scope) {
    if (has_duplicate_names(def.def.elements)) { set_unsupported("duplicate element declaration"); return; }
    const SRestriction r = def.restriction;
    const bool has_eq = !def.def.equations.empty() || !def.def.initial_equations.empty();
    bool has_alg = false;
    for (const SAlgorithm& a : def.def.algorithms) if (!a.stmts.empty()) has_alg = true;
    for (const SAlgorithm& a : def.def.initial_algorithms) if (!a.stmts.empty()) has_alg = true;
    bool has_prot = false;
    for (Id<SElement> e : def.def.elements)
      if (I_.scode.elems[e].prefixes.vis == SVisibility::Protected) has_prot = true;
    const bool rec_like = r == SRestriction::Record || r == SRestriction::OperatorRecord || r == SRestriction::Type;
    const bool conn = r == SRestriction::Connector || r == SRestriction::ExpandableConnector;
    if ((rec_like || conn) && (has_prot || has_eq || has_alg)) { set_unsupported("restricted class sections"); return; }
    if ((r == SRestriction::Package || r == SRestriction::Operator) && (has_eq || has_alg)) {
      set_unsupported("restricted class sections"); return;
    }
    if (r == SRestriction::Type)   // 长类型定义只能 extends 内建类型（"Type X does not extend a basic type"，Type1）
      for (Id<SElement> e : def.def.elements)
        if (I_.scode.elems[e].kind == SElementKind::Component) { set_unsupported("type with components"); return; }
    if (extends_shadowed(def)) { set_unsupported("extends of inherited class name"); return; }
    for (Id<SElement> e : def.def.elements) {
      const SElement& c = I_.scode.elems[e];
      if (c.prefixes.is_redeclare && c.kind != SElementKind::Extends) { set_unsupported("element redeclare"); return; }
      if (c.kind == SElementKind::Extends) {
        if (c.mod.is_valid() && mod_has_duplicates({c.mod})) { set_unsupported("duplicate modification"); return; }
        if (bad_extends_path(c, scope)) { set_unsupported("invalid extends lookup"); return; }
        continue;
      }
      if (c.kind != SElementKind::Component) continue;
      if (c.mod.is_valid() && mod_has_duplicates({c.mod})) { set_unsupported("duplicate modification"); return; }
      // outer 元素不可带修饰（InnerOuterInvalidMod1-3：OUTER_ELEMENT_MOD）。
      if (c.prefixes.io == SInnerOuter::Outer && c.mod.is_valid()) { set_unsupported("modifier on outer element"); return; }
      // constrainedby（含 2.x 的 `extends A(...)` 写法）的修饰要施加到元素上（ConstrainingClass1 / redeclare5），未做 → SKIP。
      if (c.prefixes.has_cc) { set_unsupported("constrainedby clause"); return; }
      // constant 带 fixed = false（NonFixedConstant1："Constant must be fixed"）。
      if (c.attr.var == SVariability::Const && c.mod.is_valid())
        for (const SSubMod& sm : I_.scode.mods[c.mod].subs)
          if (in_.str(sm.ident) == "fixed" && I_.scode.mods[sm.mod].binding.is_valid()) {
            const Exp& fx = I_.absyn.exps[I_.scode.mods[sm.mod].binding];
            if (fx.kind == ExpKind::BoolLit && !fx.bval) { set_unsupported("constant with fixed = false"); return; }
          }
      const bool io = c.prefixes.io != SInnerOuter::None;
      if ((r == SRestriction::Record || r == SRestriction::OperatorRecord) &&
          (c.attr.dir != SDirection::Bidir || io || c.attr.ct != SConnectorType::Potential)) {
        set_unsupported("invalid component prefix in record"); return;
      }
      if (conn && io) { set_unsupported("inner/outer in connector"); return; }
      if (bad_type_path(c, scope)) { set_unsupported("invalid component type lookup"); return; }
      // 枚举类型的字面量重名，或与枚举类型属性（quantity/min/max/start/fixed）同名 → DOUBLE_DECLARATION（EnumDuplicateLiteral /
      // EnumInvalidLiteral）。
      if (bad_enum_type(c, scope)) { set_unsupported("invalid enumeration literal"); return; }
      if (!c.type.leading_dot && !c.type.path.empty())
        for (Id<SElement> o : def.def.elements)
          if (I_.scode.elems[o].kind == SElementKind::Component && I_.scode.elems[o].name == c.type.path[0]) {
            set_unsupported("type name refers to a component"); return;
          }
    }
  }

  // 只留最终模型用到的函数（NFFlatten.collectFunctions :418-429 在 EvalConstants 之后从变量 / 方程 / 算法收集调用，
  // flattenFunction 再递归收函数体里的调用）：类型化时发射、随后被求值掉的调用（结构参数 `m = f(n) + n` → 3，ceval4）
  // 不留函数块。
  void prune_functions() {
    collect_record_ctors();
    if (result_.unsupported) return;
    std::vector<FlatFunction>& fns = result_.model.functions;
    if (fns.empty()) return;
    std::vector<bool> keep(fns.size(), false);
    std::vector<std::size_t> work;
    auto mark_call = [&](const std::vector<Symbol>& name) {
      for (std::size_t i = 0; i < fns.size(); ++i)
        if (!keep[i] && fns[i].name == name) { keep[i] = true; work.push_back(i); }
    };
    for (const std::vector<Symbol>& rn : model_record_ctors_) mark_call(rn);
    auto expr = [&](auto& self, Id<Expr> id) -> void {
      if (!id.is_valid()) return;
      const Expr& x = result_.model.exprs[id];
      if (x.kind == ExprKind::Call) mark_call(x.call_name);
      for (const ExprCrefPart& cp : x.cref.parts) for (Id<Expr> sub : cp.subscripts) self(self, sub);
      self(self, x.a);
      self(self, x.b);
      self(self, x.c);
      for (Id<Expr> a : x.args) self(self, a);
    };
    auto walk_expr = [&](Id<Expr> id) { expr(expr, id); };
    auto eqn = [&](auto& self, Id<FlatEquation> id) -> void {
      const FlatEquation& q = result_.model.eqs[id];
      walk_expr(q.a); walk_expr(q.b); walk_expr(q.c);
      for (const FlatEqBranch& br : q.branches) { walk_expr(br.cond); for (Id<FlatEquation> b : br.body) self(self, b); }
      for (Id<FlatEquation> b : q.body) self(self, b);
      for (Id<FlatEquation> b : q.else_body) self(self, b);
    };
    auto stmt = [&](auto& self, Id<FlatStatement> id) -> void {
      const FlatStatement& st = result_.model.stmts[id];
      walk_expr(st.a); walk_expr(st.b); walk_expr(st.c);
      for (const FlatStmtBranch& br : st.branches) { walk_expr(br.cond); for (Id<FlatStatement> b : br.body) self(self, b); }
      for (Id<FlatStatement> b : st.body) self(self, b);
      for (Id<FlatStatement> b : st.else_body) self(self, b);
    };
    for (const FlatVariable& v : result_.model.variables) {
      walk_expr(v.binding);
      for (const FlatVarAttr& at : v.attributes) walk_expr(at.value);
    }
    for (Id<FlatEquation> q : result_.model.equations) eqn(eqn, q);
    for (Id<FlatEquation> q : result_.model.initial_equations) eqn(eqn, q);
    for (const FlatAlgorithm& a : result_.model.algorithms) for (Id<FlatStatement> st : a.stmts) stmt(stmt, st);
    for (const FlatAlgorithm& a : result_.model.initial_algorithms) for (Id<FlatStatement> st : a.stmts) stmt(stmt, st);
    while (!work.empty()) {
      const std::size_t i = work.back();
      work.pop_back();
      for (const FlatFuncParam& fp : fns[i].params) {
        if (fp.record_def != Id<SElement>::kInvalid) mark_call(fp.type_path);   // collectTypeFuncs：记录构造函数
        walk_expr(fp.binding);
        for (const Dimension& d : fp.ty.dims) if (d.kind == DimKind::Exp) walk_expr(d.exp);
      }
      for (Id<FlatStatement> st : fns[i].body) stmt(stmt, st);
      for (Id<Expr> a : fns[i].ext_args) walk_expr(a);
      // derivative / inverse 注解连带的函数随本函数保留（NFFlatten.mo:2987-2995）。拷贝：mark_call 不改 fns 的大小，但别跨调用持引用。
      for (const std::vector<Symbol> ln : std::vector<std::vector<Symbol>>(fns[i].linked)) mark_call(ln);
    }
    std::vector<FlatFunction> kept;
    for (std::size_t i = 0; i < fns.size(); ++i) if (keep[i]) kept.push_back(std::move(fns[i]));
    fns = std::move(kept);
  }

 private:
  // ---- flatten：带前缀 DFS（R11 §3.4）----
  // protected_ctx：外层组件是 protected 时，其所有子变量也 protected（R13 §3；ComponentAttributes1）。
  void flatten_class(NodeId inst_node, const std::vector<Symbol>& prefix, const PSubs& psubs,
                     bool protected_ctx, const ModCtx& ctx, const std::vector<InnerVar>& outer_inners) {
    // 已判 unsupported 就早停：模型必 SKIP，继续摊分（尤其大复合数组）只是白烧时间/内存（ArrayConnect3 cells[1000,100]）。
    if (result_.unsupported) return;
    // 实例帧：本类压平期间 eval_scode 的单段名按本实例的局部参数 / 常量（修饰感知）求值（见 frame_value）。
    FramePush frame_push(*this, inst_node, prefix, psubs, ctx);
    const SElement& def = I_.scode.elems[I_.nodes[inst_node].def];
    if (def.def.external.present) set_unsupported("external function");
    check_class_rules(def, scode_scope_of(inst_node));
    if (result_.unsupported) return;
    // import（表达式里的名字经 find_scode_element_at → import_lookup 解析；类型名 / 基类名的 import 在 inst，任务卡
    // T-S3-06）：`.*` 的包在 initImports 时立即解析，找不到 → omc 报错（R12 §5 探针 p19a）→ SKIP；import 名被本地元素遮蔽：
    // 1.25.0 静默、testsuite 期望 LOOKUP_SHADOWING 告警（ImportShadowing1 在 FAILINGTESTFILES）→ SKIP 不入分歧。
    for (Id<SElement> eid : def.def.elements) {
      const SElement& im = I_.scode.elems[eid];
      if (im.kind != SElementKind::Import) continue;
      if (im.imp.wildcard) {
        ScodeScope ds;
        const Id<SElement> pkg = resolve_top_path(im.imp.path, ds);
        if (!pkg.is_valid() || I_.scode.elems[pkg].kind != SElementKind::Class) { set_unsupported("unresolved import"); break; }
        continue;
      }
      std::vector<Symbol> names;
      if (!im.imp.multi.empty()) {
        for (std::size_t k = 0; k < im.imp.multi.size(); ++k)
          names.push_back(k < im.imp.multi_alias.size() && im.imp.multi_alias[k].is_valid() ? im.imp.multi_alias[k]
                                                                                              : im.imp.multi[k]);
      } else {
        names.push_back(im.imp.alias.is_valid() ? im.imp.alias : (im.imp.path.empty() ? Symbol{} : im.imp.path.back()));
      }
      for (Id<SElement> o : def.def.elements) {
        const SElement& oe = I_.scode.elems[o];
        if ((oe.kind == SElementKind::Class || oe.kind == SElementKind::Component) &&
            std::find(names.begin(), names.end(), oe.name) != names.end())
          set_unsupported("import shadowed by local element");
      }
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
      const PhaseScope early(*this, MarkPhase::Early);   // 维 / 条件 / fixed：实例化期的标记（见 MarkPhase）
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
    const PhaseScope comp_phase(*this, MarkPhase::Binding);   // 组件：绑定定型期；循环之后的方程 / 算法段：Late
    for (NodeId cn : comps) {
      const CompId cid = I_.nodes[cn].comp;
      const Symbol cname = I_.nodes[cn].name;
      const Visibility vis = I_.nodes[cn].visibility;
      const SElement& cel = I_.scode.elems[I_.comps[cid].def];
      const bool has_dims = !cel.attr.dims.empty() || !cel.type.dims.empty();
      if (conflicting_direction(cel, inst_node)) { set_unsupported("conflicting direction prefix"); continue; }
      const SDirection own_dir = I_.comps[cid].attributes.direction;
      if (ctx.dir_inh != SDirection::Bidir && own_dir != SDirection::Bidir) {   // mergeDirection 冲突（NFPrefixes :588-604）
        set_unsupported("conflicting direction prefix"); continue;
      }
      const SDirection mdir = merged_dir(own_dir, ctx);
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
        std::optional<CVal> cv;
        {
          const PhaseScope early(*this, MarkPhase::Early);
          cv = eval_scode(cel.condition, scode_scope_of(cscope), 0, /*allow_param=*/true, nullptr);
        }
        if (!cv || cv->k != CVal::K::Bool) { set_unsupported("conditional component condition"); continue; }
        if (!cv->b) {
          // 键带逐段下标（复合数组各元素的条件各自求值，Condition8 / ConnectArrayCond2：b[2].a 被移除、b[1].a 保留）。
          std::vector<Symbol> rn = prefix;
          rn.push_back(cname);
          PSubs rs = psubs;
          rs.resize(prefix.size());
          rs.push_back({});
          removed_.insert(int_key(rn, rs));
          continue;
        }
      }
      // outer 组件：不产出变量，按名连到最近外层 inner，记重定向表供 cref 改写（R11 §5）。找不到 inner → SKIP。
      if (I_.comps[cid].attributes.is_outer) {
        // 外来修饰改 outer 元素（InnerOuterInvalidMod2/3：OUTER_ELEMENT_MOD）→ omc 报错 → SKIP。
        for (Id<SMod> m : ctx.mods)
          for (const SSubMod& sm : I_.scode.mods[m].subs)
            if (sm.ident == cname) set_unsupported("modifier on outer element");
        if (result_.unsupported) continue;
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
      // 外来修饰改 final 元素（NFModifier.checkFinalOverride :697-713，FinalMod1 / ModifyConstant6）、改 protected 元素
      //（PROTECTED_MODIFICATION，ProtectedAccess / Protected1）→ omc 报错 → SKIP。
      if (!matched.empty() && (I_.comps[cid].attributes.is_final || vis == Visibility::Protected)) {
        set_unsupported(vis == Visibility::Protected ? "modification of protected element" : "final element override");
        continue;
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
          // 记录数组的 parameter / constant 前缀同标量记录下沉到各元素的字段（variabilityMin）；其余复合数组带变率前缀未做。
          if (I_.comps[cid].attributes.variability != SVariability::Var && !is_plain_data_record(ci, cdef)) {
            set_unsupported("variability on composite array");
            continue;
          }
          if (I_.comps[cid].attributes.connector != SConnectorType::Potential) { set_unsupported("flow/stream on composite array"); continue; }
          // 整体绑定（外来修饰优先，否则组件自身声明）：记录数组 `R ra[2] = {r1, R(3, 4)}` / `= rs` 逐元素取第 i 个元素
          //（flattenArray → getRecordBindings 的 nthRecordElement，NFFlatten.mo:1016-1021、:902-905；splitRecordCref 先把
          // 数组 cref 展开成元素 cref，:1038-1059），元素再按标量记录绑定拆字段（build_record_ctor_mod）。非记录 → SKIP。
          Id<Exp> wraw;
          NodeId wscope = inst_node;
          std::vector<Symbol> wvpfx = prefix;
          for (Id<SMod> m : matched)
            if (I_.scode.mods[m].binding.is_valid()) { wraw = I_.scode.mods[m].binding; wscope = ctx.scope; wvpfx = ctx.vprefix; break; }
          if (!wraw.is_valid() && I_.comps[cid].binding.kind == BindingKind::Raw && I_.comps[cid].binding.raw_exp.is_valid())
            wraw = I_.comps[cid].binding.raw_exp;
          if (wraw.is_valid() && r != SRestriction::Record) { set_unsupported("composite array whole binding"); continue; }
          // 数组修饰分发（Modifier.propagate，NFModifier.mo:444-513）：组件自身声明修饰 cel.mod（`a[3](x={…})`，作用域 = 本
          // 实例）或外来修饰（`b(a(x={…}))`，作用域 = 修饰所在实例，BindingArray3/6）逐元素切分。派生修饰 / 两套同时有
          //（异作用域）→ SKIP。有整体绑定时字段修饰先去掉绑定（字段绑定由整体绑定给，同标量记录，RM 探针），剩下的属性须与
          // 整体绑定同作用域。
          std::vector<Id<SMod>> ext_mods;
          for (Id<SMod> m : matched)
            if (!I_.scode.mods[m].subs.empty()) ext_mods.push_back(m);
          if (!I_.comps[cid].derived_mods.empty()) { set_unsupported("composite array external modifier"); continue; }
          const bool own_mod = cel.mod.is_valid() && !I_.scode.mods[cel.mod].subs.empty();
          if (own_mod && !ext_mods.empty()) { set_unsupported("composite array stacked modifier scopes"); continue; }
          std::vector<Id<SMod>> dmods = own_mod ? std::vector<Id<SMod>>{cel.mod} : ext_mods;
          const NodeId dscope = own_mod ? inst_node : ctx.scope;
          const std::vector<Symbol> dvpfx = own_mod ? prefix : ctx.vprefix;
          if (wraw.is_valid()) {
            std::vector<Id<SMod>> kept;
            for (Id<SMod> m : dmods) {
              bool ok = true;
              const Id<SMod> f = strip_field_bindings(m, ok);
              if (!ok) set_unsupported("record binding + final/each field binding");
              else if (f.is_valid() && (dscope != wscope || dvpfx != wvpfx)) set_unsupported("record binding + field modifier scopes");
              else if (f.is_valid()) kept.push_back(f);
            }
            if (result_.unsupported) continue;
            dmods = std::move(kept);
          }
          std::vector<std::int64_t> adims;
          const ScodeScope adscope = scode_scope_of(inst_node);
          bool okdim = true;
          {
            // `:` 维按整体绑定字面量的形状推（deduceDimensionFromExp，NFTyping.mo:781-826；RecordBinding5 `R1 t[:] = {r1}`）。
            std::optional<std::vector<std::int64_t>> bd;
            if (wraw.is_valid()) bd = lit_dims(wraw);
            std::size_t k = 0;
            for (const std::vector<Id<Exp>>* ds : {&cel.attr.dims, &cel.type.dims})
              for (Id<Exp> d : *ds) {
                if (I_.absyn.exps[d].kind == ExpKind::Colon) {
                  if (bd && k < bd->size()) adims.push_back((*bd)[k]); else okdim = false;
                } else if (!eval_dim(d, adims, adscope)) {
                  okdim = false;
                }
                ++k;
              }
          }
          if (!okdim || adims.empty()) { set_unsupported("non-literal array dimension"); continue; }
          // 元素数上限：大复合数组 omc 常不标量化（-nfScalarize），ymc 摊分会爆量 → 超阈值 SKIP。
          std::int64_t total = 1;
          for (std::int64_t d : adims) total *= (d > 0 ? d : 0);
          if (total > 1024) { set_unsupported("composite array too large"); continue; }
          if (total == 0) continue;   // 零长度复合数组：没有元素（ZeroDimUnconnectedFlow1/2）
          bool shape_ok = true;
          for (Id<SMod> m : dmods)
            if (!split_mod_ok(m, ci, TypeKind::Unknown, adims, dscope, dvpfx)) shape_ok = false;
          if (!shape_ok) {
            if (!result_.unsupported) set_unsupported("composite array modifier shape/type");
            continue;
          }
          std::vector<std::int64_t> aidx(adims.size(), 1);
          for (bool more = true; more;) {
            PSubs cps = psubs;
            cps.push_back(aidx);   // 该复合数组段 `a` 带下标 [i…]
            ModCtx achild;
            achild.final_all = comp_final;
            achild.var_floor = var_min(ctx.var_floor, I_.comps[cid].attributes.variability);   // 变率下沉（记录数组）
            achild.keep_dir = ctx.keep_dir && (r == SRestriction::Connector || r_exp || mdir == SDirection::Input);
            achild.dir_inh = mdir;
            achild.in_conn = ctx.in_conn || r == SRestriction::Connector || r_exp;
            achild.flow_inherited = ctx.flow_inherited;   // 复合数组上的 flow 前缀仍在上方守卫处 SKIP
            // 数组各元素共用同一组件节点（present 标记按节点，YmcExpNode1 探针）；expandable 组件的成员重起链。
            if (!r_exp) achild.pp_chain = pp_self;
            if (wraw.is_valid()) {
              // 该元素的整体绑定 = 绑定式的第 aidx 个元素（字面量下降 / 数组 cref 补下标），再拆成字段修饰（排首位：字段绑定取它）。
              const Id<Exp> er = array_scalar_elem(wraw, aidx, 0);
              const bool rec_const = var_min(ctx.var_floor, I_.comps[cid].attributes.variability) == SVariability::Const;
              const Id<SMod> cm = er.is_valid() ? build_record_ctor_mod(er, ci, cdef, wscope, rec_const) : Id<SMod>{};
              if (!cm.is_valid()) { set_unsupported("composite array whole binding"); break; }
              achild.mods.push_back(cm);
              achild.scope = wscope;
              achild.vprefix = wvpfx;
            }
            if (!dmods.empty()) {
              // 该元素的合成修饰：每个子修饰绑定按 aidx 下降取标量（`x={1,2,3}`→`x=vi`；each 保持原值）。
              for (Id<SMod> m : dmods) {
                const Id<SMod> em = distribute_mod(m, aidx);
                if (!em.is_valid()) { set_unsupported("composite array modifier distribution"); break; }
                achild.mods.push_back(em);
              }
              if (result_.unsupported) break;
              achild.scope = dscope;
              achild.vprefix = dvpfx;
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
        // 组件自身声明 / 外来修饰里是否有字段修饰（`R r(x(start = 1)) = …`）。
        const bool own = cel.mod.is_valid() && !I_.scode.mods[cel.mod].subs.empty();
        bool matched_field = false;
        for (Id<SMod> m : matched)
          if (!I_.scode.mods[m].subs.empty()) { matched_field = true; break; }
        // 记录常量折叠：`constant/parameter R r = f(…)`（函数返回记录）→ 直接发射字段（继承 r 变率 + 折叠值），
        // 绕开下面的"变率复合" SKIP（CevalFuncRecord1/2）。带字段修饰时字段属性要合并（emit_record_fields 不看修饰，
        // RM1 `R d(y(min = 0)) = R(4, 5)` 曾丢 min）→ 不走此路（构造调用走下面的合并路，其余 SKIP）。
        if (r == SRestriction::Record && pp_self.empty() && !own && !matched_field) {   // expandable 里的记录折叠字段不带 pp 链 → 不走此路
          Id<Exp> wb;
          NodeId ws = inst_node;
          for (Id<SMod> m : matched)
            if (I_.scode.mods[m].binding.is_valid()) { wb = I_.scode.mods[m].binding; ws = ctx.scope; break; }
          if (!wb.is_valid() && I_.comps[cid].binding.kind == BindingKind::Raw && I_.comps[cid].binding.raw_exp.is_valid())
            wb = I_.comps[cid].binding.raw_exp;
          if (wb.is_valid() && I_.absyn.exps[wb].kind == ExpKind::Call) {
            // 只折常量实参的调用：参数实参的记录函数绑定 omc 不求值（ConstructParameters2 → initial equation + fixed=false，
            // 未做 → 落到下面 SKIP）。
            std::optional<CVal> rv = eval_user_call(I_.absyn.exps[wb], scode_scope_of(ws), 0, /*allow_param=*/false);
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
        if (ctx.unbound_fields) whole_raw = {};   // 上层记录的整体绑定已挪成方程：本层（嵌套记录）的整体绑定一并丢
        if (whole_raw.is_valid() && r == SRestriction::Record) {
          cur_psubs_ = psubs;
          whole_raw = fold_record_if(whole_raw, whole_scope, whole_vpfx);
          if (result_.unsupported) continue;
        }
        Id<SMod> ctor_mod;
        if (r == SRestriction::Record && whole_raw.is_valid())
          ctor_mod = build_record_ctor_mod(whole_raw, ci, cdef, whole_scope,
                                           var_min(ctx.var_floor, I_.comps[cid].attributes.variability) == SVariability::Const);
        // 拆不开的记录整体绑定（返回记录的函数调用等）：挪成（初始）方程 `r = f(x)`，字段无绑定（RF1 探针）。
        bool moved_binding = false;
        if (whole_raw.is_valid() && !ctor_mod.is_valid()) {
          if (r == SRestriction::Record &&
              record_binding_equation(cid, name, psubs, whole_raw, whole_scope, whole_vpfx, ctx, comp_final))
            moved_binding = true;
          else if (!result_.unsupported)
            set_unsupported("composite/record binding");
        }
        // 组子层修饰上下文（R15 §3.3 优先级：外层实例修饰 > 组件自身声明修饰）。两者同时非空要合并两套作用域，
        // 本桶未做 → 标 unsupported（easy slice 不出现）。
        ModCtx child;
        child.final_all = comp_final;
        child.var_floor = var_min(ctx.var_floor, I_.comps[cid].attributes.variability);   // 变率下沉（Half A）
        // expandable 组件也算连接器（InstNode.isConnector = CONNECTOR_MASK 含 EXPANDABLE，NFPrefixes.mo:59-60/167-175）：
        // 其成员保留方向（expconn4 `input Real ec.ri`、ExpandableConnector7 `output Real ec1.ec.x`）。
        child.keep_dir = ctx.keep_dir && (r == SRestriction::Connector || r_exp || mdir == SDirection::Input);
        child.dir_inh = mdir;
        child.in_conn = ctx.in_conn || r == SRestriction::Connector || r_exp;
        child.flow_inherited = ctx.flow_inherited || flow_rec;
        child.unbound_fields = ctx.unbound_fields || moved_binding;
        if (!r_exp) child.pp_chain = pp_self;   // expandable 组件的成员重起链（其直属成员由 exp_class 置 pp）
        if (ctor_mod.is_valid()) {
          // 整体绑定 + 字段修饰（RM1-RM4 探针）：字段绑定一律来自整体绑定（getRecordBindings 的 outerBinding 盖过字段修饰的
          // 绑定，RM2 `R c(x = 5) = a` → `c.x = a.x`；构造缺省实参也盖，RM4 `R d(y = 3) = R(x = 4)` → `d.y = 2.0`），字段的
          // 属性 / 嵌套修饰照常合并（RM1 `b.x(start = 3.0) = a.x`）。字段修饰去掉绑定后：空 → 丢；与整体绑定同作用域 → 并入；
          // 异作用域（两套作用域并存）→ SKIP。
          std::vector<Id<SMod>> mods = {ctor_mod};
          auto take = [&](Id<SMod> m, NodeId sc, const std::vector<Symbol>& vp) {
            bool ok = true;
            const Id<SMod> f = strip_field_bindings(m, ok);
            if (!ok) set_unsupported("record binding + final/each field binding");
            else if (f.is_valid() && (sc != whole_scope || vp != whole_vpfx)) set_unsupported("record binding + field modifier scopes");
            else if (f.is_valid()) mods.push_back(f);
          };
          if (own) take(cel.mod, inst_node, prefix);
          for (Id<SMod> m : matched)
            if (!I_.scode.mods[m].subs.empty()) take(m, ctx.scope, ctx.vprefix);
          child.mods = std::move(mods); child.scope = whole_scope; child.vprefix = whole_vpfx;
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
    const PhaseScope late(*this, MarkPhase::Late);
    // 本类方程（子组件方程已在上面递归时加入 → 符合 R11 §3.4-6"子组件先、本类后"）。
    // 段序：initial equation 先于 equation（R13 §2）；enclosing = 本实例，cref 前缀 = prefix。
    for (Id<SEquation> eid : def.def.initial_equations) collect_equation(eid, inst_node, prefix, psubs, /*initial=*/true);
    for (Id<SEquation> eid : def.def.equations) collect_equation(eid, inst_node, prefix, psubs, /*initial=*/false);
    // 算法段：翻译成 flat 语句（子组件段已在上面递归先入 → 子先本类后，与方程同序 R11 §3.4-6）。
    for (const SAlgorithm& alg : def.def.initial_algorithms) collect_algorithm(alg, inst_node, prefix, psubs, /*initial=*/true);
    for (const SAlgorithm& alg : def.def.algorithms) collect_algorithm(alg, inst_node, prefix, psubs, /*initial=*/false);
    collect_inherited_sections(inst_node, prefix, psubs);
  }

  // 继承来的方程 / 算法段（R11 §3.4-6、NFInst.instExpressions 用 Sections.join(new, accumulated) 头插：本类段在前，
  // 各 extends 的段按 extends **逆序**在后，每个 extends 内再递归同规则；NFSections.mo:192-209）。名字在基类实例里解析
  // （find_local / 词法作用域 = 基类），前缀与本实例相同。基类实例由 inst 按 extends 序记在实例类树的 exts 里（任务卡
  // T-S3-05；未记录时为空 → 无操作）。
  void collect_inherited_sections(NodeId inst, const std::vector<Symbol>& prefix, const PSubs& psubs) {
    if (result_.unsupported) return;
    const ClassTreeId t = I_.node_class(inst).tree;
    if (!t.is_valid()) return;
    const std::vector<NodeId> exts = I_.trees[t].exts;   // 拷贝（下面会往 arena add）
    for (auto it = exts.rbegin(); it != exts.rend() && !result_.unsupported; ++it) {
      const NodeId b = *it;
      if (!b.is_valid() || I_.nodes[b].is_empty() || !I_.nodes[b].def.is_valid()) continue;
      const SClassDef& bd = I_.scode.elems[I_.nodes[b].def].def;
      for (Id<SEquation> eid : bd.initial_equations) collect_equation(eid, b, prefix, psubs, /*initial=*/true);
      for (Id<SEquation> eid : bd.equations) collect_equation(eid, b, prefix, psubs, /*initial=*/false);
      for (const SAlgorithm& alg : bd.initial_algorithms) collect_algorithm(alg, b, prefix, psubs, /*initial=*/true);
      for (const SAlgorithm& alg : bd.algorithms) collect_algorithm(alg, b, prefix, psubs, /*initial=*/false);
      collect_inherited_sections(b, prefix, psubs);
    }
  }

  // 翻译一个算法段 → FlatAlgorithm（v1：只做直线标量赋值，见 collect_statement）。段内任一语句未做 → 整模型 SKIP。
  void collect_algorithm(const SAlgorithm& alg, NodeId enclosing, const std::vector<Symbol>& prefix,
                         const PSubs& psubs, bool is_initial) {
    if (result_.unsupported) return;
    cur_psubs_ = psubs;   // cref 前缀改写（与 collect_equation 一致：本段引用带 `a[i]` 段下标）
    FlatAlgorithm fa;
    fa.is_initial = is_initial;
    alg_initial_ = is_initial;
    for (Id<SStatement> sid : alg.stmts)
      if (!append_statement(sid, enclosing, prefix, fa.stmts)) return;   // 有语句未做 → 放弃整模型（保 FAIL=0）
    alg_initial_ = false;
    if (fa.stmts.empty()) return;   // 空段不产关键字（dumper flat_dump.cpp:129 也跳空段）
    if (is_initial) result_.model.initial_algorithms.push_back(std::move(fa));
    else result_.model.algorithms.push_back(std::move(fa));
  }

  // 追加一条算法语句的压平结果（0..n 条，NFSimplifyModel.simplifyStatement :304-382）：if 语句化简常量条件（可能整条消失
  // 或只剩某分支体），体为空的 for 删去；其余语句一条。失败 → unsupported，返回 false。
  bool append_statement(Id<SStatement> sid, NodeId enclosing, const std::vector<Symbol>& prefix,
                        std::vector<Id<FlatStatement>>& out) {
    const SStatement& st = I_.scode.stmts[sid];   // SCode 只读（flatten 期不增），引用稳定
    if (st.kind == SStmtKind::If) return append_if_statement(st, enclosing, prefix, out);
    if (st.kind == SStmtKind::Assign && is_tuple_target(st.a)) {   // `(a, b) := f(…)`：常量 → 逐元素赋值；否则元组赋值
      std::vector<std::pair<Id<Expr>, Id<Expr>>> pairs;
      if (!tuple_target(st.a, st.b, enclosing, prefix, pairs)) return false;
      for (const auto& [l, r] : pairs) {
        if (!tuple_assign_target_ok(l)) return false;
        FlatStatement fs;
        fs.kind = FlatStmtKind::Assign;
        fs.a = l;
        fs.b = r;
        fs.loc = st.loc;
        if (st.cmt.str.is_valid()) fs.comment = st.cmt.str;
        out.push_back(result_.model.add_stmt(std::move(fs)));
      }
      return true;
    }
    const Id<FlatStatement> s = collect_statement(sid, enclosing, prefix);
    if (result_.unsupported) return false;
    if (!s.is_valid()) return true;   // 删去的语句（空数组赋值）
    // 语句里留着的"复合数组取字段"的展开数组：语句不标量化，omc 打成 `y := array(a[$i1].x for $i1 in 1:3);`（CA 探针 c1；
    // 此前打成 `{a[1].x, a[2].x, a[3].x}`）——未做 → SKIP。被 sum 之类吃掉的（`y := sum(a.x)`）不受影响。
    if (stmt_has_xfield(s)) { set_unsupported("composite array field in algorithm"); return false; }
    if (result_.model.stmts[s].kind == FlatStmtKind::For && result_.model.stmts[s].body.empty()) return true;
    // 范围的类型是空数组的 for 语句整条删去（simplifyStatement 的 FOR 支：Dimension.isZero，NFSimplifyModel.mo:315-333
    // v1.25.0；EN 探针 t3：`for i in 3:1 loop`、`for i in E.three:E.one loop`；此前照打）。
    if (result_.model.stmts[s].kind == FlatStmtKind::For && range_known_empty(result_.model.stmts[s].a)) return true;
    out.push_back(s);
    return true;
  }
  // 数组字面量的元素全是"复合数组取字段"展开出来的元素 cref（可隔一层转型 / 嵌套）：原样留着的展开数组。逐元素运算过的
  //（`2 * a.x` → `{2.0 * a[1].x, …}`）元素不再是那些 cref，不算。
  bool is_xfield_array(Id<Expr> id) {
    const Expr& e = result_.model.exprs[id];
    if (e.kind != ExprKind::Array || e.args.empty()) return false;
    for (Id<Expr> a : e.args) {
      const Expr& x = result_.model.exprs[a];
      const bool elem = xfield_elems_.count(a.raw) || (x.kind == ExprKind::Cast && x.a.is_valid() && xfield_elems_.count(x.a.raw));
      if (!elem && !is_xfield_array(a)) return false;
    }
    return true;
  }
  bool expr_has_xfield(Id<Expr> id) {
    if (!id.is_valid()) return false;
    if (is_xfield_array(id)) return true;
    const Expr& e = result_.model.exprs[id];
    for (Id<Expr> a : e.args) if (expr_has_xfield(a)) return true;
    if (e.kind == ExprKind::Cref)
      for (const ExprCrefPart& cp : e.cref.parts)
        for (Id<Expr> sx : cp.subscripts) if (expr_has_xfield(sx)) return true;
    return expr_has_xfield(e.a) || expr_has_xfield(e.b) || expr_has_xfield(e.c);
  }
  bool stmt_has_xfield(Id<FlatStatement> sid) {
    if (xfield_elems_.empty()) return false;
    const FlatStatement& st = result_.model.stmts[sid];
    if (expr_has_xfield(st.a) || expr_has_xfield(st.b) || expr_has_xfield(st.c)) return true;
    for (const FlatStmtBranch& br : st.branches) {
      if (expr_has_xfield(br.cond)) return true;
      for (Id<FlatStatement> b : br.body) if (stmt_has_xfield(b)) return true;
    }
    for (Id<FlatStatement> b : st.body) if (stmt_has_xfield(b)) return true;
    for (Id<FlatStatement> b : st.else_body) if (stmt_has_xfield(b)) return true;
    return false;
  }
  // for 范围已知是空的：字面量范围（整数 / Real / Boolean / 枚举，含步长）取不到值，或空的数组字面量。
  bool range_known_empty(Id<Expr> range) {
    const Expr& r = result_.model.exprs[range];
    if (r.kind == ExprKind::Array) return r.args.empty();
    if (r.kind != ExprKind::Range) return false;
    auto num = [&](Id<Expr> x) -> std::optional<double> {
      const Expr& e = result_.model.exprs[x];
      if (e.kind == ExprKind::IntLit) return static_cast<double>(e.ival);
      if (e.kind == ExprKind::RealLit) return e.rval;
      if (const std::optional<std::int64_t> o = sub_ordinal(x)) return static_cast<double>(*o);   // Boolean / 枚举按序数
      return std::nullopt;
    };
    const std::optional<double> a = num(r.a), b = num(r.b);
    if (!a || !b) return false;
    if (!r.c.is_valid()) return *a > *b;
    // a:步长:终点（Expr 的 Range：b = 步长、c = 终点）
    const std::optional<double> stop = num(r.c);
    if (!stop || *b == 0) return false;
    return *b > 0 ? *a > *stop : *a < *stop;
  }
  // 元组赋值的目标（标量 cref 或元组里的各非空位）须是可赋值变量：已发射的 Var（when 体内另可 discrete），同标量赋值规则。
  bool tuple_assign_target_ok(Id<Expr> l) {
    const Expr le = result_.model.exprs[l];   // 拷贝
    if (le.kind == ExprKind::Tuple) {
      for (Id<Expr> x : le.args)
        if (result_.model.exprs[x].kind != ExprKind::Wild && !tuple_assign_target_ok(x)) return false;
      return true;
    }
    std::vector<Symbol> nm;
    for (const auto& p : le.cref.parts) {
      if (!p.subscripts.empty()) { set_unsupported("tuple target"); return false; }
      nm.push_back(p.name);
    }
    const FlatVariable* lv = find_scalar_flat_var(nm);
    if (!lv || !(lv->var_kind == FlatVarKind::Var || (alg_in_when_ > 0 && lv->var_kind == FlatVarKind::Discrete))) {
      set_unsupported("algorithm assignment to non-continuous variable");
      return false;
    }
    return true;
  }
  // if 语句（simplifyIfStmtBranches，NFSimplifyModel.mo:575-616；else 按条件 true 的末分支处理）：条件化简后为 false 的
  // 分支删去；为 true 的分支若在最前 → 去掉 if、只留其体，否则成为 else 并丢弃其后分支；没有分支留下 → 整条消失
  //（AlgorithmElseOpt）。被删分支的体照样翻译（未做的语句仍 SKIP）。
  bool append_if_statement(const SStatement& st, NodeId enclosing, const std::vector<Symbol>& prefix,
                           std::vector<Id<FlatStatement>>& out) {
    FlatStatement fs;
    fs.kind = FlatStmtKind::If;
    fs.loc = st.loc;
    if (st.cmt.str.is_valid()) fs.comment = st.cmt.str;
    auto body = [&](const std::vector<Id<SStatement>>& in, std::vector<Id<FlatStatement>>& b) -> bool {
      ++alg_depth_;
      bool ok = true;
      for (Id<SStatement> x : in)
        if (!append_statement(x, enclosing, prefix, b)) { ok = false; break; }
      --alg_depth_;
      return ok;
    };
    bool closed = false;   // 遇到条件 true 的分支（其后全丢）
    for (const SStmtBranch& br : st.branches) {
      const Id<Expr> c = simplify_expr(inst_exp(br.cond, enclosing, prefix));
      if (result_.unsupported) return false;
      if (result_.model.exprs[c].ty.kind != TypeKind::Boolean) { set_unsupported("algorithm condition type"); return false; }
      std::vector<Id<FlatStatement>> b;
      if (!body(br.body, b)) return false;
      const Expr& ce = result_.model.exprs[c];
      if (ce.kind == ExprKind::BoolLit && !ce.bval) continue;
      if (ce.kind == ExprKind::BoolLit && ce.bval) {
        if (fs.branches.empty()) { out.insert(out.end(), b.begin(), b.end()); return true; }
        fs.else_body = std::move(b);
        closed = true;
        break;
      }
      fs.branches.push_back({c, std::move(b)});
    }
    if (!closed) {   // 原 else = 条件 true 的末分支
      std::vector<Id<FlatStatement>> b;
      if (!body(st.else_body, b)) return false;
      if (fs.branches.empty()) { out.insert(out.end(), b.begin(), b.end()); return true; }
      fs.else_body = std::move(b);
    }
    out.push_back(result_.model.add_stmt(std::move(fs)));
    return true;
  }

  // 方程里 assert / terminate 的消息实参：omc 只化简 assert 的条件，消息原样留着（simplifyEquation 的 ASSERT 支只动
  // eq.condition、TERMINATE 落到 else，NFSimplifyModel.mo:211-215、:234 v1.25.0；WC 探针 c6：`terminate("a" + "b")`、
  // `assert(b > 0.0, "x" + String(1, 0, true))`、常量引用照样内联成 `"q" + "b"`）。语句里的则照常化简（"ab"）。
  // 定型时不折叠 String(…)（raw_msg_）；消息里有别的调用（定型期就求值 / 展开的内建、用户函数）形态未核实 → SKIP。
  Id<Expr> raw_message(Id<Exp> m, NodeId enclosing, const std::vector<Symbol>& prefix) {
    raw_msg_ = true;
    const Id<Expr> x = inst_exp(m, enclosing, prefix);
    raw_msg_ = false;
    if (!result_.unsupported && has_call_other_than_string(x)) set_unsupported("call in assert/terminate message");
    return x;
  }
  bool has_call_other_than_string(Id<Expr> id) {
    if (!id.is_valid()) return false;
    const Expr& e = result_.model.exprs[id];
    if (e.kind == ExprKind::Call && !(e.call_name.size() == 1 && in_.str(e.call_name[0]) == "String")) return true;
    for (Id<Expr> a : e.args) if (has_call_other_than_string(a)) return true;
    return has_call_other_than_string(e.a) || has_call_other_than_string(e.b) || has_call_other_than_string(e.c);
  }
  bool raw_msg_ = false;

  // 翻译一条语句（v1：只做标量赋值 `cref := expr`，目标须是已发射的连续变量；其余 kind/目标 → SKIP 保 FAIL=0）。
  Id<FlatStatement> collect_statement(Id<SStatement> sid, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const SStatement& st = I_.scode.stmts[sid];
    if (st.kind == SStmtKind::For) return collect_for_statement(st, enclosing, prefix);
    if (st.kind != SStmtKind::Assign) return collect_control_statement(st, enclosing, prefix);
    // LHS 解析成 flat cref。常量目标已被 cref() 内联成字面量 → 非 Cref → 落到下面的目标校验 SKIP。
    Id<Expr> lhs = simplify_expr(inst_exp(st.a, enclosing, prefix));
    if (result_.unsupported) return {};
    // 空数组赋值（目标某维为 0）：整条删去（NFSimplifyModel.simplifyAssignment :413；EmptyArrayAlgorithm `r2 := r1`），返回无效
    // id。目标须是本层的连续变量（when 体内另可 discrete）、右侧同形同类。
    if (const std::vector<std::int64_t> ls = expr_shape(lhs); std::find(ls.begin(), ls.end(), 0) != ls.end()) {
      // 目标引用已被 cref() 换成空数组字面量 → 按源 cref（单段、无下标的本地名）查组件变率。
      bool target_ok = false;
      if (const Exp& ax = I_.absyn.exps[st.a]; ax.kind == ExpKind::CrefExp) {
        const Cref& ac = I_.absyn.crefs[ax.cref];
        if (ac.parts.size() == 1 && ac.parts[0].subscripts.empty()) {
          const NodeId n = find_local(enclosing, ac.parts[0].name);
          if (n.is_valid() && !I_.nodes[n].is_empty() && I_.nodes[n].comp.is_valid()) {
            const SVariability v = I_.comps[I_.nodes[n].comp].attributes.variability;
            target_ok = v == SVariability::Var || (alg_in_when_ > 0 && v == SVariability::Discrete);
          }
        }
      }
      if (!target_ok) { set_unsupported("algorithm assignment to non-continuous variable"); return {}; }
      const TypeKind lk = expr_elem_kind(lhs);
      array_call_ok_ = I_.absyn.exps[st.b].kind == ExpKind::Call;   // 右侧整个是数组调用（ArrayAssignEmpty `r := f(time)`）
      const Id<Expr> r = simplify_expr(inst_exp(st.b, enclosing, prefix));
      array_call_ok_ = false;
      if (result_.unsupported) return {};
      if (expr_shape(r) != ls || !elem_kinds_compat(lk, expr_elem_kind(r)))
        set_unsupported("algorithm assignment type mismatch");
      return {};   // 被删赋值里登记过的函数由 prune_functions 收走
    }
    std::vector<Symbol> lname;
    bool array_elem = false;
    {
      const Expr& le = result_.model.exprs[lhs];
      if (le.kind != ExprKind::Cref) { set_unsupported("algorithm assignment target"); return {}; }
      for (std::size_t i = 0; i < le.cref.parts.size(); ++i) {
        lname.push_back(le.cref.parts[i].name);
        if (!le.cref.parts[i].subscripts.empty()) {
          if (i + 1 != le.cref.parts.size()) { set_unsupported("algorithm array-element assignment"); return {}; }  // 非末段下标暂缓
          array_elem = true;   // 末段带下标 = 数组元素 / 切片赋值（for 体内 `x[i] :=`、`y[1:2] :=`）
        }
      }
    }
    // 整数组赋值 `y := <数组式>`（目标是整体引用、各维已知）：语句不拆，右侧规整成整体保留的形态（whole_array_value；
    // WA 探针 w1：`y := {2.0 * z[1], 2.0 * z[2]}`、`y := z`、`y := if a > 0.0 then z else {1.0, 2.0}`、`y := 1.0:3.0`）。
    // 目标是切片（`y[1:2] := z`、`y[1, :] := z`、`y[1] := z` 取矩阵的一行）同理，左侧照写。
    const std::vector<std::int64_t> ls = expr_shape(lhs);
    const bool whole = !ls.empty();
    // 目标须是已发射的变量（Var；when 语句体里另可是 discrete）：标量取 find_scalar_flat_var；数组（元素 / 整体）取
    // find_array_flat_var（标量化成 x[1..n]）。参数 / 常量目标 → omc 报错 → SKIP。
    const FlatVariable* lv = array_elem || whole ? find_array_flat_var(lname) : find_scalar_flat_var(lname);
    if (!lv || !(lv->var_kind == FlatVarKind::Var || (alg_in_when_ > 0 && lv->var_kind == FlatVarKind::Discrete))) {
      set_unsupported("algorithm assignment to non-continuous variable");
      return {};
    }
    // when 语句里赋值的变量是离散的：声明绑定是连续的 → omc 报错（同 when 方程，WC 探针 c5 `Real y = time; … when … y := 2`）。
    if (alg_in_when_ > 0 && when_target_runtime_bound(lname)) {
      set_unsupported("when-solved variable with non-constant binding");
      return {};
    }
    const Type lty = lv->ty;   // 先取（下面 inst_exp/to_real 会往 arena add，勿跨调用持引用）
    const TypeKind ta = lty.kind;
    array_call_ok_ = whole && I_.absyn.exps[st.b].kind == ExpKind::Call;   // 右侧整个是数组调用（`x := f(a, b)`）
    if_top_ = whole && I_.absyn.exps[st.b].kind == ExpKind::IfExp;
    Id<Expr> rhs = simplify_expr(inst_exp(st.b, enclosing, prefix));
    array_call_ok_ = false;
    if_top_ = false;
    if (result_.unsupported) return {};
    if (whole) {
      // 同形（数组调用的输出维可以未知）、元素同类或 Integer → Real；字面量逐叶核对类型（同数组绑定）。
      const ExprKind rk = result_.model.exprs[rhs].kind;
      const bool arr_call = rk == ExprKind::Call || rk == ExprKind::TupleElement;
      const std::vector<std::int64_t> rs = expr_shape(rhs);
      bool ok = rs.size() == ls.size() && elem_kinds_compat(ta, expr_elem_kind(rhs)) &&
                (rk != ExprKind::Array || array_leaves_ok(rhs, ls.size(), ta));
      for (std::size_t k = 0; ok && k < ls.size(); ++k) ok = ls[k] >= 1 && (rs[k] == ls[k] || (arr_call && rs[k] < 0));
      if (!ok) { set_unsupported("algorithm assignment type mismatch"); return {}; }
      rhs = whole_array_value(rhs, ls, lty);
      if (!rhs.is_valid()) return {};
    } else {
      TypeKind tb;
      {
        const Expr& re = result_.model.exprs[rhs];
        if (re.kind == ExprKind::Array || re.kind == ExprKind::Tuple) { set_unsupported("algorithm array/tuple rhs"); return {}; }
        tb = re.ty.kind;
      }
      // 整型→实型强制（Comment1 `x:=1`→`x:=1.0`）；其余类型不符 → SKIP（StatementInvalidType1 `Real:=String`）。
      if (ta == TypeKind::Real && tb == TypeKind::Integer) rhs = to_real(rhs);
      else if (ta != tb) { set_unsupported("algorithm assignment type mismatch"); return {}; }
    }
    FlatStatement fs;
    fs.kind = FlatStmtKind::Assign;
    fs.a = lhs;
    fs.b = rhs;
    fs.loc = st.loc;
    if (st.cmt.str.is_valid()) fs.comment = st.cmt.str;
    return result_.model.add_stmt(std::move(fs));
  }

  // 组件合并后的方向（见 ModCtx::dir_inh；冲突已在 flatten_class 里判 SKIP）。
  static SDirection merged_dir(SDirection own, const ModCtx& ctx) {
    return own == SDirection::Bidir ? ctx.dir_inh : own;
  }
  // 组件注解里有 `Evaluate = true`（字面量）。
  bool evaluate_true(Id<SElement> def) { return evaluate_annotation(def) == std::optional<bool>(true); }
  // `annotation(Evaluate = true/false)` 的字面量值；没有 / 非字面量 → nullopt。
  std::optional<bool> evaluate_annotation(Id<SElement> def) {
    if (!def.is_valid()) return std::nullopt;
    const Id<SMod> ann = I_.scode.elems[def].cmt.annotation;
    if (!ann.is_valid()) return std::nullopt;
    for (const SSubMod& sm : I_.scode.mods[ann].subs)
      if (in_.str(sm.ident) == "Evaluate") {
        const Id<Exp> b = I_.scode.mods[sm.mod].binding;
        if (b.is_valid() && I_.absyn.exps[b].kind == ExpKind::BoolLit) return I_.absyn.exps[b].bval;
        return std::nullopt;
      }
    return std::nullopt;
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
    Id<Exp> ra = st.a;
    if (!ra.is_valid()) {
      // 隐式范围 `for i loop`（见 implicit_range）：打成 `for i in 1:3 loop`（IR 探针、ImplicitRangeAlg1、AlgorithmFor5）。
      IterUses u;
      iter_uses(st.body, st.index, u);
      ra = implicit_range(u, enclosing, prefix);
      if (!ra.is_valid()) { set_unsupported("implicit-range for statement"); return {}; }
    }
    // Boolean / 枚举上的范围：迭代变量取该类型的值（IR 探针 e1：`for i in E loop` 打成 `for i in {E.one, E.two, E.three} loop`、
    // `for i in E.one:E.three loop` 照写、隐式范围同后者）。
    const std::optional<IterVals> lv = label_iter_values(ra, enclosing, prefix);
    if (result_.unsupported) return {};
    Id<Expr> range = lv ? lv->kept : simplify_expr(inst_exp(ra, enclosing, prefix));
    if (result_.unsupported) return {};
    // 参数变率的范围是结构的（typeRange 对 ≤ PARAMETER 的范围调 Structural.markExp，NFTyping.mo:2455-2460）：参数打 final、
    // 范围代入值（SumVectorForIf `for i in 1:n` → `1:5`）。
    if (!lv && absyn_refs_param(ra, enclosing)) {
      const Exp re = I_.absyn.exps[ra];   // 拷贝
      const ScodeScope sc = scode_scope_of(enclosing);
      auto ival = [&](Id<Exp> x) -> Id<Expr> {
        std::optional<CVal> v = eval_scode(x, sc, 0, /*allow_param=*/true, nullptr);
        return v && v->k == CVal::K::Int ? lit_int(v->i) : Id<Expr>{};
      };
      if (re.kind != ExpKind::Range) { set_unsupported("parameter for-statement range"); return {}; }
      Expr r;
      r.kind = ExprKind::Range;
      r.ty = Type::integer();
      r.a = ival(re.a);
      r.b = ival(re.b);
      if (re.c.is_valid()) r.c = ival(re.c);
      if (!r.a.is_valid() || !r.b.is_valid() || (re.c.is_valid() && !r.c.is_valid())) {
        set_unsupported("parameter for-statement range");
        return {};
      }
      range = add(std::move(r));
    }
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
    const Id<Expr> prev_label = stmt_iter_labels_.count(id) ? stmt_iter_labels_[id] : Id<Expr>{};
    stmt_iters_[id] = lv ? label_type(lv->type_label).kind : (ek == TypeKind::Real) ? TypeKind::Real : TypeKind::Integer;
    if (lv) stmt_iter_labels_[id] = lv->type_label; else stmt_iter_labels_.erase(id);
    FlatStatement fs;
    fs.kind = FlatStmtKind::For;
    fs.index = st.index;
    fs.a = range;
    fs.loc = st.loc;
    bool ok = true;
    ++alg_loops_;
    ++alg_depth_;
    for (Id<SStatement> bid : st.body)
      if (!append_statement(bid, enclosing, prefix, fs.body)) { ok = false; break; }
    --alg_depth_;
    --alg_loops_;
    if (had) stmt_iters_[id] = prev; else stmt_iters_.erase(id);   // 恢复
    if (prev_label.is_valid()) stmt_iter_labels_[id] = prev_label; else stmt_iter_labels_.erase(id);
    if (!ok) return {};
    return result_.model.add_stmt(std::move(fs));
  }

  // 模型算法段的控制语句（NFFlatten.flattenStatement：结构照搬、只改写 cref；Algorithm1 / Break / WhenStatement1 /
  // StaticAssertSuccess 探针）：if / while / when / break（须在循环里，BREAK_OUTSIDE_LOOP）/ assert。when 语句不许嵌套、
  // 不许在 if / 循环里（MLS §11.2.7）、不许在 initial algorithm；reinit 在算法段 omc 报错（需 allowNonStandardModelica）；
  // 其余（return / 调用语句 / terminate / 元组赋值）未做 → SKIP。
  Id<FlatStatement> collect_control_statement(const SStatement& st, NodeId enclosing, const std::vector<Symbol>& prefix) {
    FlatStatement fs;
    fs.loc = st.loc;
    if (st.cmt.str.is_valid()) fs.comment = st.cmt.str;
    auto cond = [&](Id<Exp> c) -> Id<Expr> {
      const Id<Expr> x = simplify_expr(inst_exp(c, enclosing, prefix));
      if (!result_.unsupported && result_.model.exprs[x].ty.kind != TypeKind::Boolean) set_unsupported("algorithm condition type");
      return x;
    };
    auto body = [&](const std::vector<Id<SStatement>>& in, std::vector<Id<FlatStatement>>& out) -> bool {
      ++alg_depth_;
      bool ok = true;
      for (Id<SStatement> b : in)
        if (!append_statement(b, enclosing, prefix, out)) { ok = false; break; }
      --alg_depth_;
      return ok;
    };
    switch (st.kind) {   // if 由 append_if_statement 处理（化简常量条件）
      case SStmtKind::While: {
        fs.kind = FlatStmtKind::While;
        fs.a = cond(st.a);
        if (result_.unsupported) return {};
        ++alg_loops_;
        const bool ok = body(st.body, fs.body);
        --alg_loops_;
        if (!ok) return {};
        break;
      }
      case SStmtKind::Break:
        if (alg_loops_ == 0) { set_unsupported("break outside loop"); return {}; }
        fs.kind = FlatStmtKind::Break;
        break;
      case SStmtKind::When:
        if (alg_depth_ > 0 || alg_in_when_ > 0 || alg_initial_) { set_unsupported("when statement placement"); return {}; }
        fs.kind = FlatStmtKind::When;
        for (const SStmtBranch& br : st.branches) {
          FlatStmtBranch fb;
          fb.cond = simplify_expr(inst_exp(br.cond, enclosing, prefix));   // 条件向量的粗类型不过 cond() 的 Boolean 检查
          if (result_.unsupported) return {};
          fb.cond = expand_when_cond(fb.cond);
          if (!when_cond_ok(fb.cond)) { set_unsupported("when condition form"); return {}; }
          ++alg_in_when_;
          const bool ok = body(br.body, fb.body);
          --alg_in_when_;
          if (!ok) return {};
          fs.branches.push_back(std::move(fb));
        }
        break;
      case SStmtKind::Assert:
        fs.kind = FlatStmtKind::Assert;
        fs.a = cond(st.a);
        if (result_.unsupported) return {};
        fs.b = simplify_expr(inst_exp(st.b, enclosing, prefix));
        if (result_.unsupported) return {};
        if (result_.model.exprs[fs.b].ty.kind != TypeKind::String) { set_unsupported("assert message type"); return {}; }
        if (st.c.is_valid() && st.c.raw != I_.scode.assertion_level_error.raw) {   // 显式非默认 level
          fs.c = simplify_expr(inst_exp(st.c, enclosing, prefix));
          if (result_.model.exprs[fs.c].ty.kind != TypeKind::Enumeration) { set_unsupported("assert level type"); return {}; }
          if (result_.model.exprs[fs.c].kind != ExprKind::EnumLit) { set_unsupported("assert level not a literal"); return {}; }
        }
        break;
      case SStmtKind::NoRetCall:
        fs.kind = FlatStmtKind::NoRetCall;
        fs.a = noret_call(st.a, enclosing, prefix);
        if (!fs.a.is_valid()) return {};   // 删去的语句（调用方按 invalid 处理）/ unsupported
        break;
      case SStmtKind::Terminate:   // terminate(msg)：同方程（WC 探针 c5：算法段、if / when 语句体里都照打）
        fs.kind = FlatStmtKind::Terminate;
        fs.a = simplify_expr(inst_exp(st.a, enclosing, prefix));
        if (result_.unsupported) return {};
        if (result_.model.exprs[fs.a].ty.kind != TypeKind::String) { set_unsupported("terminate message type"); return {}; }
        break;
      default:
        set_unsupported("algorithm statement");
        return {};
    }
    return result_.model.add_stmt(std::move(fs));
  }

  // Absyn 下标 s 是否引用当前 for-语句的符号迭代变量（`i`、`i - 1`、`p[i]`）：循环保留时 omc 不展开、下标保持符号，
  // 不做静态越界检查（AlgorithmFor1 `a[i - 1]`、NestedSubscriptFor `x[p[i]]`）。
  bool is_stmt_iter_subscript(Id<Exp> s) { return absyn_refs_iter(s, stmt_iters_); }
  // Absyn 表达式是否引用 iters 里的迭代变量（stmt_iters_ = 保留着的符号迭代变量，loop_vars_ = 展开中的取值）。
  template <class Iters>
  bool absyn_refs_iter(Id<Exp> s, const Iters& iters) {
    if (iters.empty() || !s.is_valid()) return false;
    const Exp& e = I_.absyn.exps[s];
    if (e.kind == ExpKind::CrefExp) {
      const Cref& c = I_.absyn.crefs[e.cref];
      if (!c.leading_dot && c.parts.size() == 1 && iters.count(c.parts[0].name.id) > 0) return true;
      for (const CrefPart& p : c.parts)
        for (Id<Exp> x : p.subscripts)
          if (absyn_refs_iter(x, iters)) return true;
      return false;
    }
    if (absyn_refs_iter(e.a, iters) || absyn_refs_iter(e.b, iters) || absyn_refs_iter(e.c, iters)) return true;
    for (Id<Exp> a : e.args)
      if (absyn_refs_iter(a, iters)) return true;
    for (const ForIndex& it : e.iters)
      if (absyn_refs_iter(it.range, iters)) return true;
    return false;
  }
  // 切片下标（`:`、`a:b`、`a:s:b`、`{i, j}`）在大小为 dim 的维上选中的下标表（1 基、须在界内、非空）；边界 / 元素里的
  // `end` = dim，其余按常量求值，含参数的按结构参数求值（同标量下标）。求不出 / 越界 → nullopt。
  // written：带步长的范围按书写求出的 {步长, 终点}（打印照写，终点不一定取得到：`w[1:2:4]`）。
  std::optional<std::vector<std::int64_t>> slice_indices(Id<Exp> s, std::int64_t dim, const ScodeScope& scope,
                                                         NodeId enclosing, std::pair<std::int64_t, std::int64_t>* written = nullptr) {
    const Exp e = I_.absyn.exps[s];   // 拷贝：求值期间不持 absyn 引用
    auto bound = [&](Id<Exp> b) -> std::optional<std::int64_t> {
      if (I_.absyn.exps[b].kind == ExpKind::End) return dim;
      if (std::optional<std::int64_t> v = subscript_int(b, &scope)) return v;
      if (absyn_refs_param(b, enclosing))
        if (std::optional<CVal> pv = eval_scode(b, scope, 0, /*allow_param=*/true, nullptr); pv && pv->k == CVal::K::Int)
          return pv->i;
      return std::nullopt;
    };
    std::vector<std::int64_t> out;
    if (e.kind == ExpKind::Colon) {
      for (std::int64_t i = 1; i <= dim; ++i) out.push_back(i);
    } else if (e.kind == ExpKind::Range) {   // Absyn：a = 起点，b = 步长或终点，c = 终点（有步长时）
      const std::optional<std::int64_t> a = bound(e.a), b = bound(e.b);
      const std::optional<std::int64_t> c = e.c.is_valid() ? bound(e.c) : std::nullopt;
      if (!a || !b || (e.c.is_valid() && !c)) return std::nullopt;
      const std::int64_t step = e.c.is_valid() ? *b : 1, stop = e.c.is_valid() ? *c : *b;
      if (step == 0) return std::nullopt;
      if (written) *written = {step, stop};
      for (std::int64_t i = *a; step > 0 ? i <= stop : i >= stop; i += step) out.push_back(i);
    } else {
      for (Id<Exp> x : e.args) {
        const std::optional<std::int64_t> v = bound(x);
        if (!v) return std::nullopt;
        out.push_back(*v);
      }
    }
    if (out.empty()) return std::nullopt;   // 空切片 = 零长数组，未做
    for (std::int64_t i : out)
      if (i < 1 || i > dim) return std::nullopt;
    return out;
  }
  // 切片下标的打印形：`:` → Wild（下标位置打印成 `:`，对标 Subscript.WHOLE）；范围 → 求值后的 `a:b` / `a:s:b`；下标数组 →
  // 整数字面量数组。标量化（scalarize）按同一形式把第 i 个元素映射回原下标。
  // 恰好是 `1:dim`（无步长或步长 1）的范围等价于 `:`（Subscript.simplifySlice，NFSubscript.mo:893-913 v1.25.0；DM 探针 E1
  // `y = x[1:3]` → `y = x[:]`）；下标数组 `{1, 2, 3}` 不收。
  // omc 比的那一维 cmp_dim：下标数少于维数时取的是**末尾**那几维（simplifyList 的 List.lastN，NFSubscript.mo:929）——
  // `Real m[2, 3]` 的 `m[1:2]` 拿 3 去比、原样留下（s1 探针）；反过来部分切片恰好等于末尾维的大小时 omc 把它打成 `:`
  //（`Real m2[3, 2]` 的 `m2[1:2]` → `m2[:]`，改了含义）——omc 的错，不跟 → SKIP。
  Id<Expr> slice_subscript(ExpKind kind, const std::vector<std::int64_t>& sel, bool has_step, std::int64_t dim,
                           std::int64_t cmp_dim, std::pair<std::int64_t, std::int64_t> written) {
    Expr r;
    r.ty = Type::integer();
    const bool whole = kind == ExpKind::Range && cmp_dim >= 1 && static_cast<std::int64_t>(sel.size()) == cmp_dim &&
                       sel.front() == 1 && sel.back() == cmp_dim;
    if (whole && cmp_dim != dim) set_unsupported("partial slice printed as whole by omc");
    if (kind == ExpKind::Colon || whole) {
      r.kind = ExprKind::Wild;
    } else if (kind == ExpKind::Range) {
      r.kind = ExprKind::Range;
      r.a = lit_int(sel.front());
      if (has_step) {   // 步长、终点照写（ArraySlice `w3 = w[1:2:4]`；此前按选中的下标重算成 `1:2:3`）
        r.b = lit_int(written.first);
        r.c = lit_int(written.second);
      } else {
        r.b = lit_int(sel.back());
      }
    } else {
      r.kind = ExprKind::Array;
      for (std::int64_t i : sel) r.args.push_back(lit_int(i));
    }
    return add(std::move(r));
  }
  // Absyn 表达式是否引用 enclosing 里的 parameter 组件（首段按本地组件解析；迭代变量 / 常量 / 外层名不算）。
  bool absyn_refs_param(Id<Exp> s, NodeId enclosing) {
    if (!s.is_valid()) return false;
    const Exp& e = I_.absyn.exps[s];
    if (e.kind == ExpKind::CrefExp) {
      const Cref& c = I_.absyn.crefs[e.cref];
      if (!c.leading_dot && !c.parts.empty() && !loop_vars_.count(c.parts[0].name.id) &&
          !stmt_iters_.count(c.parts[0].name.id) && absyn_cref_var(c, enclosing) == std::optional<SVariability>(SVariability::Param))
        return true;
      for (const CrefPart& p : c.parts)
        for (Id<Exp> x : p.subscripts)
          if (absyn_refs_param(x, enclosing)) return true;
      return false;
    }
    if (absyn_refs_param(e.a, enclosing) || absyn_refs_param(e.b, enclosing) || absyn_refs_param(e.c, enclosing)) return true;
    for (Id<Exp> a : e.args)
      if (absyn_refs_param(a, enclosing)) return true;
    return false;
  }

  // 组件绑定里数组构造子（`{e for i in r}` / `array(e for i in r)`，嵌在哪一层都算）的迭代范围在实例化期就标结构
  //（Structural.markExpSize，NFInst.mo:3927 + NFStructural.mo:328-351，v1.25.0；AO 探针 a2：`Real x[2] = {i * time for i in
  // 1:n}` 前后的别名都 final，归约 `sum(x[i] for i in 1:n)` 的范围不在此列）：发射变量前先把这些范围按 Early 求一次值，只为
  // 打标记（求不出的不管，后面定型时自会 SKIP）。scope = 绑定的书写处实例。
  void mark_ctor_ranges(Id<Exp> id, NodeId scope) {
    if (!id.is_valid()) return;
    const FrameAt frame_at(*this, scope);
    const Exp e = I_.absyn.exps[id];   // 拷贝：求值期间不持 absyn 引用
    if (e.kind == ExpKind::Array || (e.kind == ExpKind::Call && is_array_ctor(id)))
      for (Id<Exp> a : e.args)
        if (I_.absyn.exps[a].kind == ExpKind::Reduction) {
          const std::vector<ForIndex> iters = I_.absyn.exps[a].iters;   // 拷贝
          const PhaseScope early(*this, MarkPhase::Early);
          for (const ForIndex& it : iters)
            if (it.range.is_valid() && absyn_refs_param(it.range, scope))
              (void)eval_scode(it.range, scode_scope_of(scope), 0, /*allow_param=*/true, nullptr);
        }
    if (e.kind == ExpKind::CrefExp) {
      const Cref c = I_.absyn.crefs[e.cref];   // 拷贝
      for (const CrefPart& p : c.parts)
        for (Id<Exp> s : p.subscripts) mark_ctor_ranges(s, scope);
      return;
    }
    mark_ctor_ranges(e.a, scope);
    mark_ctor_ranges(e.b, scope);
    mark_ctor_ranges(e.c, scope);
    for (Id<Exp> a : e.args) mark_ctor_ranges(a, scope);
    for (const ForIndex& it : e.iters) mark_ctor_ranges(it.range, scope);
  }

  // 在已发射变量里按点分名找标量化数组变量（name={x}、有末段下标 x[1..n]）。供 for 体 `x[i]:=` 目标校验。
  const FlatVariable* find_array_flat_var(const std::vector<Symbol>& nm) {
    for (const FlatVariable& v : result_.model.variables)
      if (v.name == nm && !v.subscripts.empty()) return &v;
    return nullptr;
  }

  // 翻译一条方程（v1：Equals + 攒 connect 待 run() 展开；其余 kind 标 unsupported）。
  // 元组目标 `(a, b, …)`：≥ 2 个元素的 Tuple（单元素 Tuple 只是括号；空位 = 空 Tuple 节点）。
  bool is_tuple_target(Id<Exp> lhs) const {
    return lhs.is_valid() && I_.absyn.exps[lhs].kind == ExpKind::Tuple && I_.absyn.exps[lhs].args.size() >= 2;
  }
  // 元组赋值 / 元组方程 `(a, , c) := f(…)`、`(a, b) = f(…)`（NFSimplifyModel.simplifyTupleElement :430-464）：实参全常量 →
  // 用户函数求值后按元素拆成 (目标, 值) 对，空位跳过（T1 探针 `(p, q) = f(1.0)` → `p = 2.0; q = 3.0;`、`(, u) := f(2.0)` →
  // `u := 6.0;`）；否则单对 (元组, 调用)，空位打 `_`（T2 探针 `(w, _, k) = f(time)`）。目标须是标量 cref，类型与对应输出
  // 相容（常量值 Integer → Real 提升；符号形须同类型）。其余 → SKIP。
  bool tuple_target(Id<Exp> ltuple, Id<Exp> rhs, NodeId enclosing, const std::vector<Symbol>& prefix,
                    std::vector<std::pair<Id<Expr>, Id<Expr>>>& out) {
    const std::vector<Id<Exp>> elems = I_.absyn.exps[ltuple].args;   // 拷贝
    if (I_.absyn.exps[rhs].kind != ExpKind::Call) { set_unsupported("tuple assignment rhs"); return false; }
    const std::size_t n = elems.size();
    std::vector<Id<Expr>> lhs(n);
    for (std::size_t i = 0; i < n; ++i) {
      const Exp& x = I_.absyn.exps[elems[i]];
      if (x.kind == ExpKind::Tuple && x.args.empty()) continue;   // 空位
      lhs[i] = simplify_expr(inst_exp(elems[i], enclosing, prefix));
      if (result_.unsupported) return false;
      if (result_.model.exprs[lhs[i]].kind != ExprKind::Cref || !expr_shape(lhs[i]).empty()) {
        set_unsupported("tuple target");
        return false;
      }
    }
    std::vector<CVal> outs;
    const Exp call = I_.absyn.exps[rhs];   // 拷贝
    if (eval_user_call(call, scode_scope_of(enclosing), 0, /*allow_param=*/false, &outs) && outs.size() >= n) {
      for (std::size_t i = 0; i < n; ++i) {
        if (!lhs[i].is_valid()) continue;
        if (is_arr(outs[i]) || outs[i].k == CVal::K::Record) { set_unsupported("tuple element value"); return false; }
        Id<Expr> v = literal_of(outs[i]);
        const TypeKind lk = result_.model.exprs[lhs[i]].ty.kind, vk = result_.model.exprs[v].ty.kind;
        if (lk == TypeKind::Real && vk == TypeKind::Integer) v = to_real(v);
        else if (lk != vk) { set_unsupported("tuple element type mismatch"); return false; }
        out.push_back({lhs[i], v});
      }
      return true;
    }
    Id<Expr> c = inst_exp(rhs, enclosing, prefix);   // 多输出调用在表达式里包成 TupleElement(调用, 1)
    if (result_.unsupported) return false;
    if (result_.model.exprs[c].kind == ExprKind::TupleElement) c = result_.model.exprs[c].a;
    if (result_.model.exprs[c].kind != ExprKind::Call) { set_unsupported("tuple call"); return false; }
    std::vector<TypeKind> okinds;   // 被调函数的输出元素类型（按声明序）
    {
      const std::vector<Symbol> cname = result_.model.exprs[c].call_name;
      for (const FlatFunction& f : result_.model.functions)
        if (f.name == cname) {
          for (const FlatFuncParam& p : f.params)
            if (p.direction == FlatDirection::Output) okinds.push_back(p.ty.is_array() ? TypeKind::Unknown : p.ty.kind);
          break;
        }
    }
    if (okinds.size() < n) { set_unsupported("tuple arity"); return false; }
    Expr t;
    t.kind = ExprKind::Tuple;
    t.ty = Type::real();
    for (std::size_t i = 0; i < n; ++i) {
      if (!lhs[i].is_valid()) {
        Expr w;
        w.kind = ExprKind::Wild;
        t.args.push_back(add(std::move(w)));
        continue;
      }
      if (result_.model.exprs[lhs[i]].ty.kind != okinds[i]) { set_unsupported("tuple element type mismatch"); return false; }
      t.args.push_back(lhs[i]);
    }
    out.push_back({add(std::move(t)), c});
    return true;
  }

  void collect_equation(Id<SEquation> eid, NodeId enclosing, const std::vector<Symbol>& prefix, const PSubs& psubs,
                        bool is_initial) {
    cur_psubs_ = psubs;   // cref 前缀改写用（本类方程里的引用带 `a[i]` 段下标）
    const SEquation& eq = I_.scode.eqs[eid];
    if (eq.kind == SEqKind::Connect) {
      // connect 攒起来在 run() 末尾按连接集展开（potential 相等 + flow 求和）。initial 段里的 connect 罕见 → SKIP。
      if (is_initial) { set_unsupported("connect in initial equation"); return; }
      if (in_if_) { set_unsupported("connect in if equation"); return; }   // 分支体里的 connect 未做
      // 带字符串注释的 connect：生成的连接方程带合并后的源注释（ElementSource.mergeSources，ConnectTheSameConnector 两条
      // 方程都带 "bummer!"），合并规则未核实 → SKIP。
      if (eq.cmt.str.is_valid()) { set_unsupported("connect with comment"); return; }
      // Boolean / 枚举范围的 for 里的 connect：连接推迟到末尾解析，只带着迭代变量的序数（下标的类型核对不到）→ 未做。
      if (!loop_labels_.empty()) { set_unsupported("connect in type-range for equation"); return; }
      // for 循环体里的 connect（`connect(c1[i],c2[i])`）：下标含迭代变量 i，但解析推迟到 run() 末尾时
      // loop_vars_ 已清空 → 采集当前 loop_vars_ 快照，process_connects 解析该 connect 前还原（ForConnect1）。
      connects_.push_back({eq.a, eq.b, enclosing, prefix, eq.loc, loop_vars_, psubs});
      return;
    }
    if (eq.kind == SEqKind::For) {
      // `for i in RANGE loop … end for`：常量范围 → 逐值展开体方程（loop_vars_ 供 cref 解析迭代变量与下标）。
      if (!eq.index.is_valid()) { set_unsupported("for equation (implicit range)"); return; }
      // 迭代变量遮蔽外层同名迭代变量：omc 会告警，ymc 不产告警文本 → 与期望的 Warning 行对不上 → SKIP（ForEquationShadow1）。
      if (loop_vars_.count(eq.index.id)) { set_unsupported("shadowed for iterator"); return; }
      Id<Exp> range = eq.a;
      if (!range.is_valid()) {
        // 隐式范围 `for i loop`（见 implicit_range）：体里 i 作下标处的各 (数组, 维) 维长须相同，范围 = 1:该长
        //（ImplicitRangeEq1 `x[i] = i`）；一处都没有 / 长度不一 → omc 报错，维不可求 → 未做 → SKIP。
        IterUses u;
        iter_uses(eq.body, eq.index, u);
        range = implicit_range(u, enclosing, prefix);
        if (!range.is_valid()) { set_unsupported("for equation (implicit range)"); return; }
      }
      const std::optional<IterVals> vals = iter_values(range, enclosing, prefix);
      if (!vals) { set_unsupported("for equation (non-constant range)"); return; }
      for (std::size_t j = 0; j < vals->ords.size(); ++j) {   // 同名外层迭代变量已在上面 SKIP：退出时擦掉即可
        loop_vars_[eq.index.id] = vals->ords[j];
        if (vals->type_label.is_valid()) loop_labels_[eq.index.id] = vals->labels[j];
        for (Id<SEquation> beid : eq.body) collect_equation(beid, enclosing, prefix, psubs, is_initial);
        loop_vars_.erase(eq.index.id);
        loop_labels_.erase(eq.index.id);
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
      fe.b = raw_message(eq.b, enclosing, prefix);
      if (result_.unsupported) return;
      // 参数类型校验（omc 报错，AssertInvalid1/2/3）：cond=Boolean、msg=String、显式 level=枚举。
      if (result_.model.exprs[fe.a].ty.kind != TypeKind::Boolean) { set_unsupported("assert condition type"); return; }
      if (result_.model.exprs[fe.b].ty.kind != TypeKind::String) { set_unsupported("assert message type"); return; }
      if (eq.c.is_valid() && eq.c.raw != I_.scode.assertion_level_error.raw) {
        fe.c = simplify_expr(inst_exp(eq.c, enclosing, prefix));   // 显式非默认 level
        if (result_.model.exprs[fe.c].ty.kind != TypeKind::Enumeration) { set_unsupported("assert level type"); return; }
        // level 须是参数表达式（omc："Function argument level=… has variability discrete which is not a parameter
        // expression"，WC 探针 c7 `if p then AssertionLevel.warning else AssertionLevel.error`），而且 omc 把它当结构表达式
        // 求值（`if pb then … else …` → 字面量、pb 打 final）——只收化简后已是字面量的，其余 SKIP。
        if (result_.model.exprs[fe.c].kind != ExprKind::EnumLit) { set_unsupported("assert level not a literal"); return; }
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
      fe.a = raw_message(eq.a, enclosing, prefix);
      if (result_.unsupported) return;
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
        if (result_.unsupported) return;
        // 钟控 when（条件是钟，MLS §16.6）：体是任意方程（WhenClocked1 `x + y = 0.0`、嵌套 if、元组方程），同 if 分支体照常
        // 收；带 elsewhen 是 omc 报错（SY 探针 s1）。
        if (result_.model.exprs[cond].ty.kind == TypeKind::Clock) {
          if (eq.branches.size() != 1) { set_unsupported("clocked when with elsewhen"); return; }
          FlatEqBranch fb;
          fb.cond = cond;
          fb.body = collect_if_body(br.body, enclosing, prefix, psubs, /*is_initial=*/false);
          if (result_.unsupported) return;
          fe.branches.push_back(std::move(fb));
          result_.model.equations.push_back(result_.model.add_eq(std::move(fe)));
          return;
        }
        cond = expand_when_cond(cond);
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
      // if 方程：常量条件求值折叠（选中真分支 / 丢弃假分支），否则发射 flat if。参数条件（此前各支都已折掉 = 全部
      // ≤ parameter）是结构条件（typeIfEquation，NFTyping.mo:3581-3590）：条件里的参数标结构（final + 值代入，
      // Structural.markExp）后求值选支（flattenIfEquation，NFFlatten.mo:1975-2010；IfConnect1/2）。前面有运行时分支、
      // 参数带 Evaluate=false（NON_STRUCTURAL，omc 保留 if，EvaluateFalse1）、参数非字面量绑定（传递标记未做）→ SKIP。
      std::vector<FlatEqBranch> out_branches;
      for (const SEqBranch& br : eq.branches) {   // if + elseif（保序）
        card_allowed_ = true;   // 条件里允许 cardinality（CardinalityInvalidContext*）
        Id<Expr> ce = simplify_expr(inst_exp(br.cond, enclosing, prefix));
        card_allowed_ = false;
        if (result_.unsupported) return;
        if (result_.model.exprs[ce].ty.kind != TypeKind::Boolean) { set_unsupported("if condition type"); return; }
        std::unordered_set<std::uint32_t> used;
        // 帧求值读到的参数会被记为结构参数（frame_value；方程段里不连带其绑定里的参数，见 MarkPhase）；非结构条件要撤销。
        const StructSnap snap = struct_snap();
        std::optional<CVal> cv = eval_scode(br.cond, scode_scope_of(enclosing), 0, /*allow_param=*/true, &used);
        // 定型化简后已是字面量的条件（枚举迭代变量的比较 `i == E.one`：值求值路没有枚举值）照字面量走。
        if (!cv && used.empty() && result_.model.exprs[ce].kind == ExprKind::BoolLit) cv = CVal::B(result_.model.exprs[ce].bval);
        // 非结构的参数条件（前面有运行时分支 IF2 / Evaluate=false 的 EvaluateFalse1 / 参数 fixed=false）：omc 原样保留为
        // 分支条件，不求值。
        bool keep_param = false;
        if (cv && cv->k == CVal::K::Bool && !used.empty()) {
          bool structural = out_branches.empty();
          for (std::uint32_t d : used)
            if (evaluate_annotation(Id<SElement>::from_index(d)) == std::optional<bool>(false)) structural = false;
          if (structural) {
            const std::optional<bool> fx = cond_params_fixed(ce);
            if (!fx) { set_unsupported("if condition folds via parameter"); return; }
            structural = *fx;
          }
          if (!structural) {
            keep_param = true;
            struct_restore(snap);
          }
        }
        // Evaluate = false 的参数不求值（frame_value 直接失败，但记进了 used）：非结构条件，原样保留（EvaluateFalse1）。
        if (!cv)
          for (std::uint32_t d : used)
            if (evaluate_annotation(Id<SElement>::from_index(d)) == std::optional<bool>(false)) {
              keep_param = true;
              struct_restore(snap);
              break;
            }
        // 求不出值、但引用了 fixed = false 的参数（无绑定、初始方程里定值）：isExpressionNotFixed → 非结构条件，omc 原样保留
        //（IfEquation8 `if b == true then`）。
        if ((!cv || cv->k != CVal::K::Bool) && !cond_is_runtime(ce) && cond_has_unfixed_param(ce)) {
          keep_param = true;
          struct_restore(snap);
        }
        if (cv && cv->k == CVal::K::Bool && !keep_param) {
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
        if (!keep_param && !cond_is_runtime(ce)) { set_unsupported("if condition not evaluable"); return; }
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
    if (eq.kind == SEqKind::NoRetCall) {
      const Id<Expr> c = noret_call(eq.a, enclosing, prefix);
      if (!c.is_valid()) return;   // 删去 / unsupported
      FlatEquation fe;
      fe.kind = FlatEqKind::NoRetCall;
      fe.a = c;
      fe.loc = eq.loc;
      if (eq.cmt.str.is_valid()) fe.comment = eq.cmt.str;
      const Id<FlatEquation> id = result_.model.add_eq(std::move(fe));
      if (is_initial) result_.model.initial_equations.push_back(id);
      else result_.model.equations.push_back(id);
      return;
    }
    if (eq.kind != SEqKind::Equals) {
      set_unsupported("non-equality equation (reinit…)");
      return;
    }
    if (is_tuple_target(eq.a)) {   // `(a, b) = f(…)`：常量 → 逐元素方程；否则保留元组方程
      std::vector<std::pair<Id<Expr>, Id<Expr>>> pairs;
      if (!tuple_target(eq.a, eq.b, enclosing, prefix, pairs)) return;
      for (const auto& [l, r] : pairs) {
        if (result_.model.exprs[l].kind != ExprKind::Tuple) { emit_scalar_equality(l, r, eq.loc, eq.cmt, is_initial); continue; }
        FlatEquation fe;
        fe.kind = FlatEqKind::Equality;
        fe.a = l;
        fe.b = r;
        fe.loc = eq.loc;
        if (eq.cmt.str.is_valid()) fe.comment = eq.cmt.str;
        const Id<FlatEquation> id = result_.model.add_eq(std::move(fe));
        if (is_initial) result_.model.initial_equations.push_back(id);
        else result_.model.equations.push_back(id);
      }
      return;
    }
    array_call_ok_ = I_.absyn.exps[eq.a].kind == ExpKind::Call;   // 等式一侧整个是调用（见 array_call_ok_）
    if_top_ = I_.absyn.exps[eq.a].kind == ExpKind::IfExp;         // 等式一侧整个是 if 表达式（见 if_top_）
    Id<Expr> a = inst_exp(eq.a, enclosing, prefix);
    array_call_ok_ = I_.absyn.exps[eq.b].kind == ExpKind::Call;
    if_top_ = I_.absyn.exps[eq.b].kind == ExpKind::IfExp;
    Id<Expr> b = inst_exp(eq.b, enclosing, prefix);
    array_call_ok_ = false;
    if_top_ = false;
    if (result_.unsupported) return;
    // 空数组方程（某维为 0）：整条删去（NFSimplifyModel.simplifyEqualityEquation :254；EmptyArray1 `x = ones(size(x, 1))`、
    // ABCDsystem `der(x) = A*x + B*u`）。两侧须同形同类（否则 omc 类型错）。被删式子里登记过的函数由 prune_functions 收走。
    if (const std::vector<std::int64_t> sa = expr_shape(a); std::find(sa.begin(), sa.end(), 0) != sa.end()) {
      const TypeKind ka = expr_elem_kind(a), kb = expr_elem_kind(b);
      if (sa != expr_shape(b) || !(elem_kinds_compat(ka, kb) || elem_kinds_compat(kb, ka)))
        set_unsupported("array equation shape mismatch");
      return;
    }
    // 数组方程：含数组调用 → 整体保留（NFScalarize.scalarizeEquation :343-346，ImplicitTrailingSubscript1
    // `b = testFunc(a)`）；否则两侧同形数组 → 行优先逐元素标量化；否则走标量路。
    if (!expr_shape(a).empty() || !expr_shape(b).empty()) {   // 形状看穿字面量的粗类型（`m = {{x, 1}, {2, y}}`）
      if (is_array_call(a) || is_array_call(b)) emit_array_call_equation(a, b, eq.loc, eq.cmt, is_initial);
      else collect_array_equation(a, b, eq.loc, eq.cmt, is_initial);
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
    // 两侧类型不相容（String = Real、Boolean = Real…，Function7）→ omc "Type mismatch in equation" → SKIP。
    auto num = [](TypeKind k) { return k == TypeKind::Integer || k == TypeKind::Real; };
    if (ta != tb && !(num(ta) && num(tb)) && ta != TypeKind::Unknown && tb != TypeKind::Unknown) {
      set_unsupported("equation type mismatch");
      return;
    }
    if (ta == TypeKind::Real && tb == TypeKind::Integer) b = to_real(b);
    else if (ta == TypeKind::Integer && tb == TypeKind::Real) a = to_real(a);
    // 两侧是不同记录类：右侧按左侧类型匹配（NFTyping.mo:3517 matchExpressions(e2, ty2, e1, ty1) → matchComplexTypes）。
    // 同名同序同型 = PLUG_COMPATIBLE 原样（RO5 探针 `r1 = r4`）；字段顺序 / 类型要转 = typeCastRecord 成 `R1(r2.x, r2.y)`
    //（RO4、RecordOrder2；记录构造表达式未做）→ SKIP；字段对不上 = omc 类型错 → SKIP。
    if (ta == TypeKind::Complex && tb == TypeKind::Complex &&
        record_match(result_.model.exprs[b].ty, result_.model.exprs[a].ty) > RecMatch::Plug) {
      set_unsupported("record equation type cast");
      return;
    }
    FlatEquation fe;
    fe.kind = FlatEqKind::Equality;
    fe.a = a;
    fe.b = b;
    fe.loc = loc;
    if (cmt.str.is_valid()) fe.comment = cmt.str;
    Id<FlatEquation> id = result_.model.add_eq(std::move(fe));
    if (is_initial) result_.model.initial_equations.push_back(id);
    else result_.model.equations.push_back(id);
  }

  // matchComplexTypes（NFTypeCheck.mo:1835-2004）的分类：actual 记录类与 expected 同类 → Same；字段数相同、expected 的每个
  // 字段在 actual 里同位同名同型 → Plug（不转）；同名但错位 / 字段类型不同（要 typeCastRecord 或逐字段 cast）→ Cast；
  // 字段数不同 / 缺名 → Incompatible。嵌套记录字段只认同一记录类（不同类按 Cast 保守处理）。
  enum class RecMatch : std::uint8_t { Same, Plug, Cast, Incompatible };
  RecMatch record_match(const Type& actual, const Type& expected) {
    auto cls_of = [&](const Type& t) -> NodeId {
      const std::uint32_t cn = t.element_type().complex_node;
      if (cn == Id<INode>::kInvalid) return {};
      const NodeId n = NodeId::from_index(cn);
      return I_.nodes.contains(n) && !I_.nodes[n].is_empty() && I_.nodes[n].def.is_valid() ? n : NodeId{};
    };
    const NodeId an = cls_of(actual), en = cls_of(expected);
    if (!an.is_valid() || !en.is_valid()) return RecMatch::Incompatible;
    if (I_.nodes[an].def == I_.nodes[en].def) return RecMatch::Same;
    const std::vector<NodeId>& ac = I_.trees[I_.node_class(an).tree].components;
    const std::vector<NodeId>& ec = I_.trees[I_.node_class(en).tree].components;
    if (ac.size() != ec.size()) return RecMatch::Incompatible;
    RecMatch m = RecMatch::Plug;
    for (std::size_t i = 0; i < ec.size(); ++i) {
      const Symbol name = I_.nodes[ec[i]].name;
      std::size_t idx = i;
      if (I_.nodes[ac[i]].name != name) {
        idx = ac.size();
        for (std::size_t k = 0; k < ac.size(); ++k)
          if (I_.nodes[ac[k]].name == name) { idx = k; break; }
        if (idx == ac.size()) return RecMatch::Incompatible;
        m = RecMatch::Cast;
      }
      const Type& at = I_.comps[I_.nodes[ac[idx]].comp].ty;
      const Type& et = I_.comps[I_.nodes[ec[i]].comp].ty;
      if (at.kind != et.kind || !same_dims(at.dims, et.dims)) { m = RecMatch::Cast; continue; }
      if (at.kind == TypeKind::Complex && record_match(at, et) != RecMatch::Same) m = RecMatch::Cast;
    }
    return m;
  }

  // 同秩，且两边都是整数维处相等（未知 / 表达式维与任何维相容：函数形参 `:`、调用处求不出的输出维）。
  static bool compat_dims(const std::vector<Dimension>& x, const std::vector<Dimension>& y) {
    if (x.size() != y.size()) return false;
    for (std::size_t i = 0; i < x.size(); ++i)
      if (x[i].kind == DimKind::Integer && y[i].kind == DimKind::Integer && x[i].size != y[i].size) return false;
    return true;
  }
  // 值的元素类型可配目标：同类，或 Integer 值配 Real 目标（matchTypes 的 Integer→Real 提升）。
  static bool elem_kinds_compat(TypeKind target, TypeKind value) {
    return target == value || (target == TypeKind::Real && value == TypeKind::Integer);
  }
  static bool same_dims(const std::vector<Dimension>& x, const std::vector<Dimension>& y) {
    if (x.size() != y.size()) return false;
    for (std::size_t i = 0; i < x.size(); ++i)
      if (x[i].kind != DimKind::Integer || y[i].kind != DimKind::Integer || x[i].size != y[i].size) return false;
    return true;
  }

  // 返回数组的调用（或其元组元素）——Expression.hasArrayCall 的顶层形（嵌在运算里的数组调用在 try_emit_user_call 已拒）。
  // 整个 Integer 数组表达式转 Real 数组：Cast（ty = Real + 原维），打印 `/*Real[n]*/(…)`。
  Id<Expr> array_real_cast(Id<Expr> x) {
    Expr c;
    c.kind = ExprKind::Cast;
    c.a = x;
    c.ty = Type::array_of(Type::real(), result_.model.exprs[x].ty.dims);
    return add(std::move(c));
  }
  bool is_array_call(Id<Expr> id) {
    const Expr& x = result_.model.exprs[id];
    // 推导式 `array(e for i in r)` 同数组构造调用：等式整体保留（AC3 探针 `x = array(/*Real*/(i) * time for i in 1:3)`）；
    // 整体转型包着的也算（`/*Real[3]*/(array(…))`）。
    if (x.kind == ExprKind::Cast && x.ty.is_array()) return is_array_call(x.a);
    if (non_vectorizeable_call(x)) return false;
    return (x.kind == ExprKind::Call || x.kind == ExprKind::TupleElement || x.kind == ExprKind::Reduction) && x.ty.is_array();
  }
  // der / pre / previous：Call.isVectorizeable 为 false（NFCall.mo:1300-1318）——它们的数组调用不让等式整体保留，所在等式
  // 照常逐元素标量化（`der(y) = x` → `der(y[1]) = x[1]`、`v = pre(b)` → `v[1] = pre(b[1])`，V4s 探针）。
  bool non_vectorizeable_call(const Expr& x) {
    if (x.kind != ExprKind::Call || x.call_name.size() != 1) return false;
    const std::string_view n = in_.str(x.call_name[0]);
    return n == "der" || n == "pre" || n == "previous";
  }
  // 含数组调用的等式整体发射 `lhs = rhs`：同秩、已知维相等、元素类型相同（Integer↔Real 要整体强转，未做 → SKIP）。
  void emit_array_call_equation(Id<Expr> a, Id<Expr> b, SourceLoc loc, const SComment& cmt, bool is_initial) {
    // Integer 推导式对 Real 一侧：整体转型 `/*Real[3]*/(array(i for i in 1:3))`（AC3 探针；其它数组调用的整体转型未核实）。
    auto real_side = [&](Id<Expr>& x, Id<Expr> other) {
      if (result_.model.exprs[x].kind == ExprKind::Reduction && result_.model.exprs[x].ty.element_type().kind == TypeKind::Integer &&
          result_.model.exprs[other].ty.element_type().kind == TypeKind::Real)
        x = array_real_cast(x);
    };
    real_side(a, b);
    real_side(b, a);
    const Type aty = result_.model.exprs[a].ty, bty = result_.model.exprs[b].ty;
    if (!aty.is_array() || !bty.is_array() || !compat_dims(aty.dims, bty.dims) ||
        aty.element_type().kind != bty.element_type().kind) {
      set_unsupported("array call equation shape/type");
      return;
    }
    // 空数组侧（另一侧维未知，上游的空方程删除没接住）：未核实 → SKIP。
    for (const Type* t : {&aty, &bty})
      for (const Dimension& d : t->dims)
        if (d.kind == DimKind::Integer && d.size == 0) { set_unsupported("empty array call equation"); return; }
    for (const std::vector<std::int64_t>& s : cur_psubs_)
      if (!s.empty()) { set_unsupported("array equation under array prefix"); return; }
    FlatEquation fe;
    fe.kind = FlatEqKind::Equality;
    fe.a = simplify_expr(a);
    fe.b = simplify_expr(b);
    fe.loc = loc;
    if (cmt.str.is_valid()) fe.comment = cmt.str;
    const Id<FlatEquation> id = result_.model.add_eq(std::move(fe));
    if (is_initial) result_.model.initial_equations.push_back(id);
    else result_.model.equations.push_back(id);
  }

  // 数组方程 `lhs = rhs`（两侧同形数组）→ 行优先枚举 + 逐元素标量化 + 逐元素标量等式（对标 NFScalarize.scalarizeEquation）。
  void collect_array_equation(Id<Expr> a, Id<Expr> b, SourceLoc loc, const SComment& cmt, bool is_initial) {
    // 一侧数组一侧标量 = omc 类型错；形状不一致 / 未知同理 → SKIP（保 FAIL=0）。形状看穿字面量的粗类型。
    const std::vector<std::int64_t> dims = expr_shape(a);
    if (dims.empty() || dims != expr_shape(b) || std::find(dims.begin(), dims.end(), -1) != dims.end()) {
      set_unsupported("array equation shape mismatch");
      return;
    }
    // 复合数组前缀（enclosing 带 `a[i]` 段下标）与叶数组标量化的组合本增量暂缓（eq2/eq3）→ SKIP。
    for (const std::vector<std::int64_t>& s : cur_psubs_)
      if (!s.empty()) { set_unsupported("array equation under array prefix"); return; }
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
    if (x.kind == ExprKind::Array && !idx.empty()) {   // 字面量（粗类型，不能凭 ty 判标量）：取首下标处元素，余下标继续
      if (idx[0] < 1 || static_cast<std::size_t>(idx[0]) > x.args.size()) {
        set_unsupported("array literal element out of range");
        return lit_int(0);
      }
      return scalarize(x.args[static_cast<std::size_t>(idx[0]) - 1], std::vector<std::int64_t>(idx.begin() + 1, idx.end()));
    }
    if (expr_shape(e).empty()) return e;   // 标量（广播操作数）；形状看穿字面量 / 一元的粗类型（`-{1, 2}`）
    if (x.kind == ExprKind::Reduction && x.call_name.size() == 2) {
      // 推导式的第 idx[0] 个元素：迭代变量代入范围的该值后化简，余下下标交给体（嵌套推导式）。AC3 探针
      // `sum({x[i] * i for i in 1:3})` → `x[1] + x[2] * 2.0 + x[3] * 3.0`。
      // 只在核实过的位置展开（red_scalarize_ok_：sum / product 的实参、参数数组绑定的逐元素摊分）。其余位置 omc 把含数组调用
      // 的式子整体保留并按数组算符打印（Expression.hasArrayCall = contains，NFExpression.mo:5440-5471；R1 / R2 探针
      // `x = array(…) * 2.0`、`y = -array(…)`、`s = array(…) * {1.0, 2.0, 3.0}`），未做 → SKIP。
      if (!red_scalarize_ok_) { set_unsupported("comprehension inside array expression"); return lit_int(0); }
      const std::optional<RangeLit> l = range_lit(result_.model.exprs[x.b]);
      if (!l || !l->integer || l->boolean || idx.empty() || idx[0] < 1 || idx[0] > l->n) {
        set_unsupported("comprehension scalarization");
        return lit_int(0);
      }
      Expr ic;
      ic.kind = ExprKind::Cref;
      ic.cref.parts.push_back({x.call_name[1], {}});
      const std::unordered_map<std::string, Id<Expr>> vals{
          {expr_cref_key(ic.cref), lit_int(static_cast<std::int64_t>(l->start + static_cast<double>(idx[0] - 1) * l->step))}};
      const Id<Expr> el = simplify_expr(clone_subst(x.a, vals));
      // 代入后实参全成字面量的调用 omc 会求值（V4c 探针 `sum(F({1, 2, time}, 2))` 的前两项 → `3.0 + 4.0 + …`）；内建的已被
      // simplify 折掉，用户函数的调用处求值未接进来 → SKIP。
      if (has_const_call(el)) { set_unsupported("constant call after comprehension substitution"); return lit_int(0); }
      return scalarize(el, std::vector<std::int64_t>(idx.begin() + 1, idx.end()));
    }
    if (x.kind == ExprKind::Range) {   // 字面量范围的第 i 个元素 = 起 + (i - 1)·步（RG2 探针 `q = 1:3` → `q[2] = 2.0`）
      const std::optional<RangeLit> l = range_lit(x);
      if (!l || idx.size() != 1 || idx[0] < 1 || idx[0] > l->n) { set_unsupported("range scalarization"); return lit_int(0); }
      const double v = l->start + static_cast<double>(idx[0] - 1) * l->step;
      if (l->boolean) return lit_bool(v != 0);   // `v = false:true` → `v[1] = false; v[2] = true`（V4r 探针）
      return l->integer ? lit_int(static_cast<std::int64_t>(v)) : lit_real(v);
    }
    switch (x.kind) {
      case ExprKind::Cref: {
        Expr c = x;
        c.ty = x.ty.element_type();
        std::vector<Id<Expr>> subs;
        const std::vector<Id<Expr>> orig = x.cref.parts.back().subscripts;
        if (orig.empty()) {   // 整体引用：末段补下标
          for (std::int64_t v : idx) subs.push_back(lit_int(v));
        } else {
          // 切片引用（slice_subscript 的三种形）：按出现序消费 idx——`:` → i、范围 → 起点 + (i - 1)·步长、下标数组 → 第 i
          // 个；标量下标原样。
          std::size_t used = 0;
          for (Id<Expr> s : orig) {
            const Expr se = result_.model.exprs[s];
            const bool slice = se.kind == ExprKind::Wild || se.kind == ExprKind::Range || se.kind == ExprKind::Array;
            if (!slice) { subs.push_back(s); continue; }
            if (used >= idx.size()) { set_unsupported("slice scalarization"); return lit_int(0); }
            const std::int64_t i = idx[used++];
            if (se.kind == ExprKind::Wild) {
              subs.push_back(lit_int(i));
            } else if (se.kind == ExprKind::Range) {
              const std::int64_t start = result_.model.exprs[se.a].ival;
              const std::int64_t step = se.c.is_valid() ? result_.model.exprs[se.b].ival : 1;
              subs.push_back(lit_int(start + (i - 1) * step));
            } else {
              if (i < 1 || static_cast<std::size_t>(i) > se.args.size()) { set_unsupported("slice scalarization"); return lit_int(0); }
              subs.push_back(se.args[static_cast<std::size_t>(i) - 1]);
            }
          }
          // 下标少于维数（`x[i]` 引 2 维 x = `x[i, :]`）：余下的 idx 补成末尾下标。
          while (used < idx.size()) subs.push_back(lit_int(idx[used++]));
        }
        c.cref.parts.back().subscripts = std::move(subs);
        return add(std::move(c));
      }
      case ExprKind::Binary: {
        Id<Expr> sa = scalarize(x.a, idx), sb = scalarize(x.b, idx);
        // `/`、`^` 的结果恒为 Real、两侧都提 Real（同标量 binary()；WA 探针 w5 `y := m .^ 2` →
        // `{/*Real*/(m[1]) ^ 2.0, …}`，此前幂漏了、打成 `/*Real*/(m[1] ^ 2)`）。
        const bool real = x.bin == BinOp::Div || x.bin == BinOp::Pow || result_.model.exprs[sa].ty.kind == TypeKind::Real ||
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
        // 混合数组字面量的字面量叶：omc 逐元素出 `not true`、不化简（V4u 探针 `v = not {b[1], true}` → `v[2] = not true`、
        // `h4[2] = not true`），ymc 的 simplify 会折成 `false`，未做 → SKIP。
        if (result_.model.exprs[x.a].kind == ExprKind::Array && result_.model.exprs[sa].kind == ExprKind::BoolLit) {
          set_unsupported("not of literal element in mixed array literal");
          return lit_int(0);
        }
        Expr r; r.kind = ExprKind::LogicalNot; r.a = sa; r.ty = Type::boolean();
        return add(std::move(r));
      }
      case ExprKind::Logical: {
        Id<Expr> sa = scalarize(x.a, idx), sb = scalarize(x.b, idx);
        Expr r; r.kind = ExprKind::Logical; r.log = x.log; r.a = sa; r.b = sb; r.ty = Type::boolean();
        const Id<Expr> n = add(std::move(r));
        scalarized_logicals_.insert(n.raw);   // omc 先化简后标量化：`v = {true, false} or b` → `v[1] = true or b[1]`
        return n;
      }
      case ExprKind::IfExp: {
        // 数组 if（array_if_exp 建的）：两支取该元素、条件不变（`y[1] = if time > 1.0 then x[1] else 2.0 * x[1]`，V4r 探针）。
        if (!x.ty.is_array()) { set_unsupported("array expression scalarization"); return lit_int(0); }
        // 分支是 Boolean 数组的 and / or / not：omc 取元素时不往里推下标，打成 `(not j)[1]`（WA 探针 w5），不复刻 → SKIP。
        for (Id<Expr> br : {x.b, x.c})
          if (const ExprKind bk = result_.model.exprs[br].kind; bk == ExprKind::Logical || bk == ExprKind::LogicalNot) {
            set_unsupported("Boolean array operation in scalarized if branch");
            return lit_int(0);
          }
        Id<Expr> sb = scalarize(x.b, idx), sc = scalarize(x.c, idx);
        if (x.ty.element_type().kind == TypeKind::Real) { sb = to_real(sb); sc = to_real(sc); }
        Expr r;
        r.kind = ExprKind::IfExp;
        r.a = x.a;
        r.b = sb;
        r.c = sc;
        r.ty = x.ty.element_type();
        return add(std::move(r));
      }
      case ExprKind::Call:
        // der / pre 的数组调用逐元素（ExpressionIterator 把实参取到元素：`der(y)` → `der(y[i])`、`pre(b)` → `pre(b[i])`）。
        // 其余数组调用不逐元素（等式整体保留，上游已分流）。
        if (non_vectorizeable_call(x) && x.args.size() == 1) {
          Expr c = x;
          c.args[0] = scalarize(x.args[0], idx);
          c.ty = x.ty.element_type();
          return add(std::move(c));
        }
        set_unsupported("array expression scalarization");
        return lit_int(0);
      default:
        set_unsupported("array expression scalarization");
        return lit_int(0);
    }
  }

  // 数组二元算术（逐元素/标量广播；结果维取自数组操作数）。矩阵乘/除/幂（非点号、两侧皆数组）非逐元素 → SKIP。
  // 运算数的类型：数组字面量按其形状 / 叶类型给数组类型（inst_exp 给字面量的是粗类型——当标量会把 `p + {1, 1, 1}`
  // 误当数组 + 标量广播；单测 flatten_array_literal_operand_elementwise）。
  Type op_type(Id<Expr> id) {
    const Expr& x = result_.model.exprs[id];
    if (x.kind != ExprKind::Array) return x.ty;
    const TypeKind ek = expr_elem_kind(id);
    std::vector<Dimension> ds;
    for (std::int64_t n : expr_shape(id)) {
      Dimension d;
      if (n >= 0) { d.kind = DimKind::Integer; d.size = n; } else { d.kind = DimKind::Unknown; }
      ds.push_back(d);
    }
    return Type::array_of(Type::of(ek), std::move(ds));
  }
  // 函数语境里把 Integer 运算数提到 Real（matchExpressions）：标量插 Cast / 折字面量、数组字面量逐叶转、fill 调用转填充值
  //（NFCall.typeCast :1361-1367）、维未知的数组整体转型；维已知的 Integer 数组未做 → invalid。
  Id<Expr> fn_real_operand(Id<Expr> x) {
    const Type t = op_type(x);
    const Expr xe = result_.model.exprs[x];
    // 数组字面量逐叶转：混合字面量 `{x, 2}` 的元素类型已是 Real，里面的 Integer 叶仍要转成 `2.0`（ES 探针 e6
    // `array(x, 2) .* n` → `{x * /*Real*/(n[1]), 2.0 * /*Real*/(n[2])}`）。
    if (xe.kind == ExprKind::Array && (t.element_type().kind == TypeKind::Real || t.element_type().kind == TypeKind::Integer))
      return coerce_deep(x, TypeKind::Real);
    if (t.element_type().kind == TypeKind::Real) return x;
    if (t.element_type().kind != TypeKind::Integer) return {};
    if (!t.is_array()) return to_real(x);
    // 数组 if：两支各自转（`if k > 0 then /*Real[2]*/(n2) else {/*Real*/(2 * n2[1]), …}`，FS 探针 t4）。
    if (xe.kind == ExprKind::IfExp) {
      Expr r = xe;
      r.b = fn_real_operand(xe.b);
      r.c = fn_real_operand(xe.c);
      if (!r.b.is_valid() || !r.c.is_valid()) return {};
      r.ty = Type::array_of(Type::real(), t.dims);
      return add(std::move(r));
    }
    if (xe.kind == ExprKind::Call && xe.call_name.size() == 1 && in_.str(xe.call_name[0]) == "fill" && !xe.args.empty()) {
      Expr c = xe;
      c.args[0] = to_real(xe.args[0]);
      c.ty = Type::array_of(Type::real(), xe.ty.dims);
      return add(std::move(c));
    }
    // Integer 数组整体转型（FA 探针 f2 `n * 2.5` → `/*Real[:]*/(n) * 2.5`；FS 探针 t2：赋值位置 `v2 := /*Real[2]*/(n2)`、
    // `/*Real[2]*/(n[1:2])`）。维已知的作运算数时随运算按元素展开、逐个转（fn_expand 的 Cast 支：
    // `{/*Real*/(n[1]) * 2.5, …}`）——只收引用（整体 / 切片）；别的已知维 Integer 数组式未核实。
    for (const Dimension& d : t.dims)
      if (d.kind != DimKind::Integer) return array_real_cast(x);
    return xe.kind == ExprKind::Cref ? array_real_cast(x) : Id<Expr>{};
  }

  Id<Expr> array_binary(Op orig, BinOp op, Id<Expr> a, Id<Expr> b) {
    const Type ta = op_type(a), tb = op_type(b);
    const bool aarr = ta.is_array(), barr = tb.is_array();
    const TypeKind ea = ta.element_type().kind, eb = tb.element_type().kind;
    auto numeric = [](TypeKind k) { return k == TypeKind::Integer || k == TypeKind::Real; };
    // 非内建数值元素（记录数组 + 运算符重载 OperatorOverloadArrayOp*）→ 需重载算子展开，未做 → SKIP。
    if (!numeric(ea) || !numeric(eb)) { set_unsupported("non-numeric array operand"); return lit_int(0); }
    // 非逐元素 / 暂缓：矩阵乘（* 两侧皆数组）、矩阵幂（^ 含数组，OperationPow2/3）、除法（/ ./ 需操作数提实型，Tier C 暂缓）。
    if (orig == Op::Pow) { set_unsupported("array matrix power"); return lit_int(0); }
    // 函数语境：符号形态（不标量化）。Integer 运算数提实型；`*` 两侧皆数组 = 向量 / 矩阵积（NFTypeCheck.checkBinaryOperationMul
    // :1019-1093，未知 / 表达式维与任何维相等，Dimension.isEqual :294-307；ImplicitRangeExp3 `x * y` → Real）；逐元素运算
    // 同秩相容维。
    if (fn_scope_) {
      // 标量与数组之间不带点号的 + / -、标量 / 数组：omc 报类型错（FA 探针 f3 `u + a`、`a / u`）。同一个数组自己减自己
      //（`u - u`，维未知）：omc 报错。
      if (((orig == Op::Add || orig == Op::Sub) && aarr != barr) || (orig == Op::Div && !aarr && barr)) {
        set_unsupported("array and scalar operands without elementwise operator");
        return lit_int(0);
      }
      if ((orig == Op::Sub || orig == Op::EWSub) && aarr && barr && result_.model.exprs[a].kind == ExprKind::Cref &&
          result_.model.exprs[b].kind == ExprKind::Cref &&
          expr_cref_key(result_.model.exprs[a].cref) == expr_cref_key(result_.model.exprs[b].cref)) {
        set_unsupported("array minus itself in function body");
        return lit_int(0);
      }
      const bool freal = op == BinOp::Div || op == BinOp::Pow || ea == TypeKind::Real || eb == TypeKind::Real;
      if (freal) {
        a = fn_real_operand(a);
        b = fn_real_operand(b);
        if (!a.is_valid() || !b.is_valid()) { set_unsupported("function array operand cast"); return lit_int(0); }
      }
      const Type relem = freal ? Type::real() : Type::integer();
      bool product = false;   // 向量 / 矩阵积（非逐元素，ExpandExp 另有展开规则）
      auto eq = [](const Dimension& x, const Dimension& y) {
        return x.kind != DimKind::Integer || y.kind != DimKind::Integer || x.size == y.size;
      };
      std::vector<Dimension> rd;
      bool scalar_res = false;
      if (orig == Op::Mul && aarr && barr) {
        const std::vector<Dimension>& d1 = ta.dims;
        const std::vector<Dimension>& d2 = tb.dims;
        bool ok = false;
        if (d1.size() == 1 && d2.size() == 1) { ok = eq(d1[0], d2[0]); scalar_res = true; }
        else if (d1.size() == 1 && d2.size() == 2) { ok = eq(d1[0], d2[0]); rd = {d2[1]}; }
        else if (d1.size() == 2 && d2.size() == 1) { ok = eq(d1[1], d2[0]); rd = {d1[0]}; }
        else if (d1.size() == 2 && d2.size() == 2) { ok = eq(d1[1], d2[0]); rd = {d1[0], d2[1]}; }
        if (!ok) { set_unsupported("array product shape"); return lit_int(0); }
        product = true;
      } else if (orig == Op::Div && aarr && barr) {
        set_unsupported("non-elementwise array operator");
        return lit_int(0);
      } else if (aarr && barr) {
        if (!compat_dims(ta.dims, tb.dims)) { set_unsupported("array shape mismatch"); return lit_int(0); }
        rd = ta.dims;
        for (std::size_t k = 0; k < rd.size(); ++k) if (rd[k].kind != DimKind::Integer) rd[k] = tb.dims[k];
      } else {
        rd = aarr ? ta.dims : tb.dims;
      }
      Expr r;
      r.kind = ExprKind::Binary;
      r.bin = op;
      r.a = a;
      r.b = b;
      r.ty = scalar_res ? relem : Type::array_of(relem, std::move(rd));
      const Id<Expr> rid = add(std::move(r));
      if (product) result_.model.product_binaries.insert(rid.raw);
      else fn_array_ops_[rid.raw] = orig;   // 打印形态（点号 / 操作数换位）留到化简时定：能按元素展开的不用换（fn_array_form）
      return rid;
    }
    if (orig == Op::Mul && aarr && barr) return model_array_product(a, b);   // 向量 / 矩阵积：展开成积和
    if (orig == Op::Div && aarr && barr) {   // 数组÷数组（非点号）非逐元素 → SKIP
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
    // `/` `./` 恒实除，`.^` 的结果也恒为 Real（w5 `sum(m .^ 2)`：此前数组类型留成 Integer，外面多套一层转型）。
    const bool real = op == BinOp::Div || op == BinOp::Pow || ea == TypeKind::Real || eb == TypeKind::Real;
    Expr r;
    r.kind = ExprKind::Binary;
    r.bin = op;
    r.a = a;
    r.b = b;
    r.ty = Type::array_of(real ? Type::real() : Type::integer(), std::move(ds));
    return add(std::move(r));
  }

  // 数组逻辑（and/or 逐元素/广播，结果 Boolean 数组）。
  // 叶全是 Boolean 字面量的数组字面量。
  bool all_bool_literal(Id<Expr> id) {
    const Expr& x = result_.model.exprs[id];
    if (x.kind == ExprKind::BoolLit) return true;
    if (x.kind != ExprKind::Array || x.args.empty()) return false;
    for (Id<Expr> a : x.args) if (!all_bool_literal(a)) return false;
    return true;
  }
  // 全字面量 Boolean 数组上的 not / and / or 逐元素求值成字面量数组（omc 对字面量数组整体求值：V4u 探针
  // `a8 = {false, true}`、`a5 = {true, false, true, true}`、`m6 = {false, true} or b` 的内层）。b 无效 = not。
  Id<Expr> fold_bool_arrays(const LogOp* op, Id<Expr> a, Id<Expr> b) {
    const Expr xa = result_.model.exprs[a];   // 拷贝
    if (xa.kind == ExprKind::BoolLit) {
      if (!op) return lit_bool(!xa.bval);
      const bool y = result_.model.exprs[b].bval;
      return lit_bool(*op == LogOp::And ? (xa.bval && y) : (xa.bval || y));
    }
    Expr r = xa;
    for (std::size_t i = 0; i < xa.args.size(); ++i)
      r.args[i] = fold_bool_arrays(op, xa.args[i], op ? result_.model.exprs[b].args[i] : Id<Expr>{});
    return add(std::move(r));
  }

  Id<Expr> array_logical(LogOp lop, Id<Expr> a, Id<Expr> b) {
    const Type ta = op_type(a), tb = op_type(b);   // 数组字面量按形状 / 叶类型取类型（`{true, false} or b`）
    if (ta.element_type().kind != TypeKind::Boolean || tb.element_type().kind != TypeKind::Boolean) {
      set_unsupported("non-boolean array logical");
      return lit_int(0);
    }
    // 两侧须是同形数组：标量与数组的 and / or omc 报类型错（V4u 探针 `v = b and (time > 1)`）。
    if (!ta.is_array() || !tb.is_array() || !same_dims(ta.dims, tb.dims)) { set_unsupported("array shape mismatch"); return lit_int(0); }
    // 两侧都是数组字面量：全字面量 → 求值；否则 omc 在首维逐元素合并并化简（`{b, b2} and {b2, b}` → `{b and b2, b2 and b}`、
    // `{x[1] > 1, false} or {true, x[2] < 1}` → `{true, x[2] < 1.0}`），未做 → SKIP。
    if (result_.model.exprs[a].kind == ExprKind::Array && result_.model.exprs[b].kind == ExprKind::Array) {
      if (all_bool_literal(a) && all_bool_literal(b)) return fold_bool_arrays(&lop, a, b);
      set_unsupported("array logical of two array literals");
      return lit_int(0);
    }
    std::vector<Dimension> ds = ta.dims;
    Expr r;
    r.kind = ExprKind::Logical;
    r.log = lop;
    r.a = a;
    r.b = b;
    r.ty = Type::array_of(Type::boolean(), std::move(ds));
    return add(std::move(r));
  }

  // when 条件：标量 Boolean 式，或由它们组成的条件向量 `{c1, c2, …}`——omc 照化简后的式子打印，不挑形态（WC 探针 c1–c4：
  // 关系 / and / or / not / if、内建与用户函数调用作操作数、字面量 `when true`；W1 探针：`sample(0.0, h)`、`change(k)`）。
  // 挡两类：① initial() 不是整个条件、也不是条件向量的直接元素 → omc 告警 "initial() may only be used as a when condition"
  //（`not initial()`、`initial() or a > 1`；ymc 不产告警）；② noEvent 包着 Boolean 式（引用除外）→ 不是离散时间表达式，omc
  // 报 "When-condition … is not a discrete-time expression"（`noEvent(a > 1)`；`noEvent(p)`、`noEvent(a) > 1` 可以）。
  // Clock 条件在调用方另走。数组 cref 条件先经 expand_when_cond 展开。
  bool when_cond_ok(Id<Expr> c) {
    const Expr& e = result_.model.exprs[c];
    auto elem = [&](Id<Expr> x) {
      const Expr& xe = result_.model.exprs[x];
      if (xe.ty.kind != TypeKind::Boolean || xe.ty.is_array() || xe.kind == ExprKind::Array) return false;
      return is_initial_call(xe) || when_operand_ok(x);
    };
    if (e.kind != ExprKind::Array) return elem(c);
    if (e.args.empty()) return false;
    for (Id<Expr> x : e.args)
      if (!elem(x)) return false;
    return true;
  }
  bool is_initial_call(const Expr& e) const {
    return e.kind == ExprKind::Call && e.call_name.size() == 1 && e.args.empty() && in_.str(e.call_name[0]) == "initial";
  }
  bool when_operand_ok(Id<Expr> id) {
    if (!id.is_valid()) return true;
    const Expr& e = result_.model.exprs[id];
    if (is_initial_call(e)) return false;
    if (e.kind == ExprKind::Call && e.call_name.size() == 1 && e.args.size() == 1 && in_.str(e.call_name[0]) == "noEvent") {
      const Expr& a = result_.model.exprs[e.args[0]];
      if (a.ty.kind == TypeKind::Boolean && a.kind != ExprKind::Cref) return false;
    }
    for (Id<Expr> a : e.args) if (!when_operand_ok(a)) return false;
    return when_operand_ok(e.a) && when_operand_ok(e.b) && when_operand_ok(e.c);
  }
  // Boolean 数组 cref 作 when 条件 → 按元素展开成条件向量（omc 打 `{b[1], b[2], b[3]}`，When2 / W1 探针）。其余原样。
  Id<Expr> expand_when_cond(Id<Expr> c) {
    const std::vector<std::int64_t> sh = expr_shape(c);
    if (sh.size() != 1 || sh[0] < 1 || result_.model.exprs[c].kind != ExprKind::Cref) return c;
    Dimension d;
    d.kind = DimKind::Integer;
    d.size = sh[0];
    Expr arr;
    arr.kind = ExprKind::Array;
    arr.ty = Type::array_of(Type::boolean(), {d});
    for (std::int64_t i = 1; i <= sh[0]; ++i) arr.args.push_back(scalarize(c, {i}));
    return add(std::move(arr));
  }

  // when 里解出 / 赋值的变量的声明绑定是否引用了变量或 time（标量看变量的绑定，数组看 emit_array 记下的绑定方程）。
  bool when_target_runtime_bound(const std::vector<Symbol>& lname) {
    if (const FlatVariable* v = find_scalar_flat_var(lname); v && v->binding.is_valid() && refs_runtime_var(v->binding)) return true;
    std::string nm;
    for (Symbol sy : lname) { if (!nm.empty()) nm += '.'; nm += in_.str(sy); }
    return runtime_bound_arrays_.count(nm) > 0;
  }
  std::unordered_set<std::string> runtime_bound_arrays_;   // 绑定方程右侧引用了变量 / time 的 Var 数组（点分名）

  // 收集 when 分支体（标量方程 / reinit）；填 out + 该分支解的 LHS 变量名集合 solved。任何超范围体项 → set_unsupported+false。
  bool collect_when_body(const std::vector<Id<SEquation>>& body, NodeId enclosing,
                         const std::vector<Symbol>& prefix, std::vector<Id<FlatEquation>>& out,
                         std::set<std::string>& solved) {
    struct WhenScope {   // when 方程体里（pre / change 的连续实参只在 when 里合法，见 builtin_call）
      int& n;
      explicit WhenScope(int& c) : n(c) { ++n; }
      ~WhenScope() { --n; }
      WhenScope(const WhenScope&) = delete;
      WhenScope& operator=(const WhenScope&) = delete;
    } when_scope(eq_in_when_);
    // when 方程解出的变量是离散的（MLS §3.8.4）：它的声明绑定若是连续的，omc 报 "Component a of variability discrete has
    // binding 'time' of higher variability continuous"（WC 探针 c5 `Real a = time; … when … then a = y;`）。绑定引用了
    // 变量 / time 即拒绝（离散绑定也一并挡，保守）；数组变量的绑定是方程，不查。
    // solved 按标量元素记（带下标的键）：`x[1] = 0` 与 elsewhen 里的 `x[2] = 0` 不是同一组变量（WhenVariableMismatch2：
    // "The same variables must be solved in elsewhen clause as in the when clause"）。
    auto solve = [&](Id<Expr> lhs) -> bool {
      std::vector<Symbol> lname;
      for (const ExprCrefPart& cp : result_.model.exprs[lhs].cref.parts) lname.push_back(cp.name);
      if (when_target_runtime_bound(lname)) {
        set_unsupported("when-solved variable with non-constant binding");
        return false;
      }
      solved.insert(expr_cref_key(result_.model.exprs[lhs].cref));
      return true;
    };
    for (Id<SEquation> beid : body) {
      const SEquation& b = I_.scode.eqs[beid];
      if (b.kind == SEqKind::Equals) {
        Id<Expr> a = inst_exp(b.a, enclosing, prefix);
        Id<Expr> rhs = inst_exp(b.b, enclosing, prefix);
        if (result_.unsupported) return false;
        // 数组等式：同模型方程逐元素标量化（scalarizeWhenEquation 对体做 scalarizeEquations，NFScalarize.mo:401-424 v1.25.0；
        // DM 探针 s2 `when t > 1 then y = x; end when` → `y[1] = x[1]; …`）。左侧须是引用（整体 / 切片）；含数组调用的等式
        // 在 when 体里的形态未核实 → SKIP。
        if (result_.model.exprs[a].kind == ExprKind::Cref && (!expr_shape(a).empty() || !expr_shape(rhs).empty())) {
          if (is_array_call(rhs)) { set_unsupported("array call in when body"); return false; }
          std::vector<Id<FlatEquation>>& sink = result_.model.equations;
          const std::size_t start = sink.size();
          collect_array_equation(a, rhs, b.loc, b.cmt, /*is_initial=*/false);
          if (result_.unsupported) return false;
          for (std::size_t k = start; k < sink.size(); ++k)   // 逐元素等式的左侧 = 解出的元素
            if (!solve(result_.model.eqs[sink[k]].a)) return false;
          out.insert(out.end(), sink.begin() + static_cast<std::ptrdiff_t>(start), sink.end());
          sink.erase(sink.begin() + static_cast<std::ptrdiff_t>(start), sink.end());
          continue;
        }
        a = simplify_expr(a);
        rhs = simplify_expr(rhs);
        if (result_.model.exprs[a].kind != ExprKind::Cref) { set_unsupported("when-eq LHS not cref"); return false; }
        if (result_.model.exprs[a].kind == ExprKind::Array || result_.model.exprs[rhs].kind == ExprKind::Array) {
          set_unsupported("array in when body"); return false;
        }
        // 左侧可以是数组元素（`x[3] = 5`、for 方程展开出的 `x[i] = …`；WC 探针 c5、When8 / WhenWithArray）。
        const TypeKind ta = result_.model.exprs[a].ty.kind, tb = result_.model.exprs[rhs].ty.kind;
        if (ta == TypeKind::Real && tb == TypeKind::Integer) rhs = to_real(rhs);
        else if (ta == TypeKind::Integer && tb == TypeKind::Real) a = to_real(a);
        FlatEquation fe;
        fe.kind = FlatEqKind::Equality;
        fe.a = a;
        fe.b = rhs;
        fe.loc = b.loc;
        const Id<Expr> lhs = fe.a;
        out.push_back(result_.model.add_eq(std::move(fe)));
        if (!solve(lhs)) return false;
      } else if (b.kind == SEqKind::Reinit) {
        Id<Expr> tgt = simplify_expr(inst_exp(b.a, enclosing, prefix));
        Id<Expr> val = simplify_expr(inst_exp(b.b, enclosing, prefix));
        if (result_.unsupported) return false;
        if (result_.model.exprs[tgt].kind != ExprKind::Cref) { set_unsupported("reinit arg0 not cref"); return false; }
        const ExprCref tc = result_.model.exprs[tgt].cref;   // 拷贝（下面的 expr_shape / whole_array_value 会往 arena 加）
        // 目标：Real 的标量 / 数组元素 / 整个数组 / 切片（不拆，WC 探针 c5：`reinit(x, {0.0, 0.0, 0.0})`、
        // `reinit(x[1:2], {0.0, 0.0})`、`reinit(x[1], 0.0)`）；数组值规整成整体保留的形态（whole_array_value）。
        const std::vector<std::int64_t> tsh = expr_shape(tgt);
        if (result_.model.exprs[tgt].ty.element_type().kind != TypeKind::Real || tc.parts.empty() || tsh != expr_shape(val) ||
            std::find_if(tsh.begin(), tsh.end(), [](std::int64_t n) { return n < 1; }) != tsh.end() ||
            !elem_kinds_compat(TypeKind::Real, expr_elem_kind(val))) {
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
        val = tsh.empty() ? to_real(val) : whole_array_value(val, tsh, Type::real());
        if (!val.is_valid()) return false;
        FlatEquation fe;
        fe.kind = FlatEqKind::Reinit;
        fe.a = tgt;
        fe.b = val;
        fe.loc = b.loc;
        out.push_back(result_.model.add_eq(std::move(fe)));   // reinit 不计入 solved（When7 靠此判失配）
      } else if (b.kind == SEqKind::NoRetCall) {
        const Id<Expr> c = noret_call(b.a, enclosing, prefix);
        if (result_.unsupported) return false;
        if (c.is_valid()) {   // invalid = 求值掉了，整条删去（when 本身留着，N2 探针）
          FlatEquation fe;
          fe.kind = FlatEqKind::NoRetCall;
          fe.a = c;
          fe.loc = b.loc;
          out.push_back(result_.model.add_eq(std::move(fe)));
        }
      } else if (b.kind == SEqKind::Assert || b.kind == SEqKind::Terminate) {
        // assert / terminate 照模型方程收（WC 探针 c5：`when a > 1.0 then terminate("stop"); end when;`），再从主方程表搬进体里。
        std::vector<Id<FlatEquation>>& sink = result_.model.equations;
        const std::size_t start = sink.size();
        collect_equation(beid, enclosing, prefix, {}, /*is_initial=*/false);
        if (result_.unsupported) return false;
        out.insert(out.end(), sink.begin() + static_cast<std::ptrdiff_t>(start), sink.end());
        sink.erase(sink.begin() + static_cast<std::ptrdiff_t>(start), sink.end());
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

  // 表达式是否引用连续 / 离散变量或 time（属性绑定的变率检查）。
  // floor_scope / floor：引用处所在实例（如 `constant R r` 的记录实例）的变率下限——引用本实例自己的字段时按
  // variabilityMin 下沉后的变率判（RecordBinding7 `final constant Real y = x / 2.0` 的 x 声明是 Real，实际 constant）。
  // 类实例节点（组件的 class_inst 可下钻）。
  bool is_class_inst(NodeId cls) {
    return cls.is_valid() && !I_.nodes[cls].is_empty() && I_.nodes[cls].kind == NodeKind::Class && I_.nodes[cls].cls.is_valid() &&
           I_.node_class(cls).tree.is_valid();
  }
  bool is_comp_node(NodeId n) {
    return n.is_valid() && I_.nodes.contains(n) && !I_.nodes[n].is_empty() && I_.nodes[n].comp.is_valid();
  }
  // 类型化 cref 的有效变率：首个本地段（resolved_node = n，变率 v0）沿后续各段下沉（var_min：类实例 `a` 自身是 Var、它的参数
  // `a.p` 是参数；`parameter R r` 的 `r.x` 是参数——同 cref_is_continuous）。n 在 parts 里的位置（前缀长度）这里不知道：从右
  // 往左找同名、且后续各段都走得通的那一段；走不通 → 保守按首段（v0）。ND 探针 d1 / d2：`parameter Integer k = a.p` 此前判成
  // 更高变率的绑定、`y[a.p]` 的下标被当成运行时变量原样留着。
  SVariability cref_path_var(const ExprCref& cref, NodeId n, SVariability v0) {
    for (std::size_t i0 = cref.parts.size(); i0-- > 0;) {
      if (cref.parts[i0].name != I_.nodes[n].name) continue;
      SVariability w = v0;
      NodeId cur = n;
      bool ok = true;
      for (std::size_t i = i0 + 1; ok && i < cref.parts.size(); ++i) {
        const NodeId cls = I_.comps[I_.nodes[cur].comp].class_inst;
        ok = is_class_inst(cls);
        if (!ok) break;
        cur = find_local(cls, cref.parts[i].name);
        ok = is_comp_node(cur);
        if (ok) w = var_min(w, I_.comps[I_.nodes[cur].comp].attributes.variability);
      }
      if (ok) return w;
    }
    return v0;
  }
  // 书写的 cref 所指组件的有效变率：首段是 enclosing 的本地组件，沿后续各段下沉；首段不是本地组件 → nullopt，后续段走不通
  // → 走到哪算哪。
  std::optional<SVariability> absyn_cref_var(const Cref& c, NodeId enclosing) {
    if (c.leading_dot || c.parts.empty()) return std::nullopt;
    NodeId cur = find_local(enclosing, c.parts[0].name);
    if (!is_comp_node(cur)) return std::nullopt;
    SVariability v = I_.comps[I_.nodes[cur].comp].attributes.variability;
    for (std::size_t i = 1; i < c.parts.size(); ++i) {
      const NodeId cls = I_.comps[I_.nodes[cur].comp].class_inst;
      if (!is_class_inst(cls)) break;
      const NodeId nx = find_local(cls, c.parts[i].name);
      if (!is_comp_node(nx)) break;
      cur = nx;
      v = var_min(v, I_.comps[I_.nodes[cur].comp].attributes.variability);
    }
    return v;
  }
  bool refs_runtime_var(Id<Expr> id, NodeId floor_scope = {}, SVariability floor = SVariability::Var) {
    if (!id.is_valid()) return false;
    const Expr& e = result_.model.exprs[id];
    if (e.kind == ExprKind::Cref) {
      const std::uint32_t raw = e.cref.resolved_node;
      if (raw == Id<INode>::kInvalid) {
        return e.cref.parts.size() == 1 && in_.str(e.cref.parts[0].name) == "time";
      }
      if (raw == kFnLocal) return false;
      const NodeId n = NodeId::from_index(raw);
      if (I_.nodes.contains(n) && !I_.nodes[n].is_empty() && I_.nodes[n].comp.is_valid()) {
        SVariability v = I_.comps[I_.nodes[n].comp].attributes.variability;
        // 首段所指组件就是引用处实例的本地组件（flat cref 已带前缀，按节点自身名在该实例里找回同一节点判）。
        if (floor != SVariability::Var && floor_scope.is_valid() && find_local(floor_scope, I_.nodes[n].name) == n)
          v = var_min(floor, v);
        v = cref_path_var(e.cref, n, v);
        if (v == SVariability::Var || v == SVariability::Discrete) return true;
      }
      for (const ExprCrefPart& cp : e.cref.parts)
        for (Id<Expr> sx : cp.subscripts) if (refs_runtime_var(sx, floor_scope, floor)) return true;
      return false;
    }
    for (Id<Expr> a : e.args) if (refs_runtime_var(a, floor_scope, floor)) return true;
    return refs_runtime_var(e.a, floor_scope, floor) || refs_runtime_var(e.b, floor_scope, floor) ||
           refs_runtime_var(e.c, floor_scope, floor);
  }

  // 类型化 cref 所指变量是否连续：Real，且沿本地各段（前缀之后）逐段下沉的变率仍是 Var（`parameter R r` 的 `r.x` 是参数）；
  // `time` 连续；函数局部 / 迭代变量不是。段解析不到（outer 改写等）→ 保守当连续。
  bool cref_is_continuous(const Expr& x, std::size_t nprefix) {
    if (x.ty.element_type().kind != TypeKind::Real) return false;
    const std::uint32_t raw = x.cref.resolved_node;
    if (raw == kFnLocal) return false;
    if (raw == Id<INode>::kInvalid) return x.cref.parts.size() == 1 && in_.str(x.cref.parts[0].name) == "time";
    NodeId cur = NodeId::from_index(raw);
    SVariability v = SVariability::Var;
    for (std::size_t i = nprefix; i < x.cref.parts.size(); ++i) {
      if (i > nprefix) {
        const NodeId cls = I_.comps[I_.nodes[cur].comp].class_inst;
        if (!cls.is_valid() || I_.nodes[cls].is_empty() || I_.nodes[cls].kind != NodeKind::Class || !I_.nodes[cls].cls.is_valid() ||
            !I_.node_class(cls).tree.is_valid())
          return true;
        cur = find_local(cls, x.cref.parts[i].name);
      }
      if (!cur.is_valid() || !I_.nodes.contains(cur) || I_.nodes[cur].is_empty() || !I_.nodes[cur].comp.is_valid()) return true;
      v = var_min(v, I_.comps[I_.nodes[cur].comp].attributes.variability);
    }
    return v == SVariability::Var;
  }

  // 是否引用 parameter 变量（同 refs_runtime_var 按 cref 首段所指组件的声明变率判）。
  bool refs_param_var(Id<Expr> id) {
    if (!id.is_valid()) return false;
    const Expr& e = result_.model.exprs[id];
    if (e.kind == ExprKind::Cref) {
      const std::uint32_t raw = e.cref.resolved_node;
      if (raw != Id<INode>::kInvalid && raw != kFnLocal) {
        const NodeId n = NodeId::from_index(raw);
        if (I_.nodes.contains(n) && !I_.nodes[n].is_empty() && I_.nodes[n].comp.is_valid() &&
            cref_path_var(e.cref, n, I_.comps[I_.nodes[n].comp].attributes.variability) == SVariability::Param)
          return true;
      }
      for (const ExprCrefPart& cp : e.cref.parts) for (Id<Expr> sx : cp.subscripts) if (refs_param_var(sx)) return true;
      return false;
    }
    for (Id<Expr> a : e.args) if (refs_param_var(a)) return true;
    return refs_param_var(e.a) || refs_param_var(e.b) || refs_param_var(e.c);
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
          const SVariability v = cref_path_var(e.cref, n, I_.comps[I_.nodes[n].comp].attributes.variability);
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
    const PhaseScope bind_phase(*this, MarkPhase::Binding);
    ++bind_clock_;        // 本变量的绑定 / 属性定型序号（见 MarkPhase）
    // `annotation(Evaluate = true)` 的参数：omc 当结构参数（EV0–EV14 探针：`final parameter Real p = 2.0`、绑定求值成字面量、
    // 绑定里用到的参数连带 final、引用处代入并化简 `Real x = 2.0 * time`）——走下面 struct_final 的既有通路。求不出值（外部函数
    // 求值失败 omc 报错，CevalBinding10 账本；无绑定只有 start 的 omc 取 start 并告警，EV2）、带 fixed 属性（fixed = false 时
    // omc 忽略 Evaluate，EV3）→ SKIP。
    const bool eval_true = I_.comps[cid].attributes.variability == SVariability::Param && evaluate_true(I_.comps[cid].def);
    // 先算绑定/属性（会往 model.exprs add），再取组件字段——不跨 add 持组件引用（arena 纪律）。
    const Type cty = I_.comps[cid].ty;   // 拷贝，跨 add 稳定
    // 结构 final（ceval3）：本参数被某 fixed 用到 → final parameter + 绑定求值成字面量。
    PSubs vsubs = psubs;
    vsubs.resize(prefix.size());
    vsubs.push_back({});
    const std::string vkey = int_key(name, vsubs);
    typed_keys_.insert(vkey);
    const bool struct_early = eval_true || final_params.count(I_.comps[cid].def.raw) > 0;   // 实例化期就是结构参数
    const bool struct_other = final_params.count(I_.comps[cid].def.raw) > 0 || structural_set_.count(vkey) > 0;
    bool struct_final = struct_other || eval_true;
    {   // 绑定在源码里的形态（结构参数的别名连带用，见 apply_structural_params）：裸 cref（含括号）才是候选。
      Id<Exp> braw0;
      NodeId bscope0 = mod_scope;
      int origin = 0;   // 0 书写的（声明绑定 / 实例修饰）、1 记录表达式绑定拆出的字段修饰、2 其它合成修饰（组件数组的修饰分发）
      for (Id<SMod> m : mod_subs)
        if (I_.scode.mods[m].binding.is_valid()) {
          braw0 = I_.scode.mods[m].binding;
          origin = m.raw < synth_mod_floor_ ? 0 : derived_mods_.count(m.raw) ? 1 : 2;
          break;
        }
      if (!braw0.is_valid() && I_.comps[cid].binding.kind == BindingKind::Raw) {
        braw0 = I_.comps[cid].binding.raw_exp;
        bscope0 = enclosing;
      }
      mark_ctor_ranges(braw0, bscope0);
      // ndims(…) / 单实参 size(…) 是参数变率（见 absyn_param_size_call）：绑给 constant 是更高变率的绑定，omc 报错（ND 探针
      // v1：`constant Integer n = ndims(x)`，此前折成字面量放行）；Evaluate = true 的参数绑它时，实参里的非参数量让它不成
      // 结构参数（isBindingNotFixed 的 CALL 支，NFStructural.mo:210-219：`parameter Integer n = ndims(x) annotation(Evaluate
      // = true)` 不 final），未细分 → SKIP。
      if (absyn_param_size_call(braw0)) {
        if (var_min(var_floor, I_.comps[cid].attributes.variability) == SVariability::Const)
          set_unsupported("ndims / size(a) in constant binding");
        else if (eval_true)
          set_unsupported("Evaluate annotation");
      }
      while (braw0.is_valid() && I_.absyn.exps[braw0].kind == ExpKind::Tuple && I_.absyn.exps[braw0].args.size() == 1)
        braw0 = I_.absyn.exps[braw0].args[0];
      if (braw0.is_valid() && I_.absyn.exps[braw0].kind == ExpKind::CrefExp) {
        if (origin == 0) alias_keys_.emplace(vkey, bind_clock_);
        else if (origin == 2) alias_unknown_keys_.insert(vkey);
      }
    }
    // 绑定：外来修饰优先（第一个带绑定的子修饰），否则组件自身声明绑定（R15 §3.4 外层实例修饰 > 声明默认）。
    Id<Expr> binding;
    {
      Id<Exp> mraw;
      for (Id<SMod> m : mod_subs)
        if (I_.scode.mods[m].binding.is_valid()) { mraw = I_.scode.mods[m].binding; break; }
      // constant 标量：先按值求值（NFCeval；cref 取已发射常量的最终值 → 修饰感知，CevalBinding2；含数组运算如标量积
      // `{1, 2, 3} * {4, 5, 6}`，CevalVectorProduct1）。求不出（枚举 / 内建数学函数 / if 式…）→ 走下面的类型化路径。
      const TypeKind ck = cty.kind;
      if (var_min(var_floor, I_.comps[cid].attributes.variability) == SVariability::Const &&
          (ck == TypeKind::Real || ck == TypeKind::Integer || ck == TypeKind::Boolean || ck == TypeKind::String)) {
        Id<Exp> craw = mraw;
        NodeId cscope = mod_scope;
        const std::vector<Symbol>* cpfx = &mod_prefix;
        if (!craw.is_valid() && I_.comps[cid].binding.kind == BindingKind::Raw && I_.comps[cid].binding.raw_exp.is_valid()) {
          craw = I_.comps[cid].binding.raw_exp;
          cscope = enclosing;
          cpfx = &prefix;
        }
        if (craw.is_valid()) {
          std::optional<CVal> cv;
          {
            const FrameAt frame_at(*this, cscope);
            CevalCtx guard(*this, *cpfx, psubs);
            cv = eval_scode(craw, scode_scope_of(cscope), 0);
          }
          if (cv)
            if (std::optional<CVal> ce = coerce_to_kind(*cv, ck)) binding = literal_of(*ce);
        }
      }
      // 枚举类型的绑定值 `E.one` / `P.E.one` 是枚举字面量（打全路径），不走会 SKIP 多段名的通用 cref 解析。
      if (binding.is_valid()) {
        // 已按值求得
      } else if (mraw.is_valid()) {
        binding = (cty.kind == TypeKind::Enumeration && is_cref_exp(mraw)) ? enum_lit_value(mraw, mod_scope, mod_prefix)
                                                                           : inst_exp(mraw, mod_scope, mod_prefix);
      } else {
        const Binding& b = I_.comps[cid].binding;
        const BindingKind bk = b.kind;
        const Id<Exp> raw = b.raw_exp;
        if (bk == BindingKind::Raw && raw.is_valid())
          binding = (cty.kind == TypeKind::Enumeration && is_cref_exp(raw)) ? enum_lit_value(raw, enclosing, prefix)
                                                                            : inst_exp(raw, enclosing, prefix);
      }
    }
    if (ctx.unbound_fields) binding = {};   // 外层记录整体绑定已挪成方程：字段无绑定（见 ModCtx::unbound_fields）
    if (binding.is_valid()) {
      binding = simplify_expr(binding);   // 折叠常量子表达式（`1+2`→`3.0`）+ 已在 cref 内联的常量
      // 数值组件绑定成 String/Bool 是类型错误（omc 报错，ConditionInvalidBinding1）——ymc 无绑定 typing → SKIP。
      const TypeKind bk = result_.model.exprs[binding].ty.kind;
      if ((cty.kind == TypeKind::Real || cty.kind == TypeKind::Integer) &&
          (bk == TypeKind::String || bk == TypeKind::Boolean))
        set_unsupported("binding type mismatch");
      // parameter / constant 绑定引用连续 / 离散变量或 time → omc HIGHER_VARIABILITY_BINDING（DisturbedResistance1
      // `extends Resistor(R = 1.0 + 0.1*sin(time))`）。
      {
        const SVariability cv = var_min(var_floor, I_.comps[cid].attributes.variability);
        if ((cv == SVariability::Param || cv == SVariability::Const) && refs_runtime_var(binding, enclosing, var_floor))
          set_unsupported("binding of higher variability");
      }
      // 标量组件绑数组（BindingInvalidType2 `Real x = {1, 2, 3}`）→ omc 维度不符报错。
      if (!cty.is_array() && (result_.model.exprs[binding].ty.is_array() || result_.model.exprs[binding].kind == ExprKind::Array))
        set_unsupported("binding type mismatch");
      // Real 绑定给 Integer 变量：TypeCheck 不做 Real→Integer（IntDiv `4000 / 100`、IntPow、Real2Integer1）→ omc 报错。
      if (cty.kind == TypeKind::Integer && bk == TypeKind::Real) set_unsupported("binding type mismatch");
      // 枚举绑定须是本枚举类型的字面量（Enum12 `E1 e = E2.B` → omc "Type mismatch in binding"）：按字面量路径的类型段与
      // 字面量名核对（类型别名写法会被保守 SKIP）。
      if (cty.kind == TypeKind::Enumeration && result_.model.exprs[binding].kind == ExprKind::EnumLit) {
        const std::vector<Symbol> ep = result_.model.exprs[binding].enum_path;
        const bool lit_ok = ep.size() >= 2 && !cty.enum_path.empty() && ep[ep.size() - 2] == cty.enum_path.back() &&
                            std::find(cty.enum_literals.begin(), cty.enum_literals.end(), ep.back()) != cty.enum_literals.end();
        if (!lit_ok) set_unsupported("enumeration binding type mismatch");
      }
      if (cty.kind == TypeKind::Real) binding = to_real(binding);   // `= 2` → `= 2.0`
    }
    // 结构 final 参数：绑定求值成字面量（init_x = p1 or p2 → true）。
    if (struct_final) {
      // 值取修饰感知求值的结果（frame_value 记在 const_vals_）；没有则按本实例帧现求（有效绑定 = 外来修饰 > 声明）。
      // Evaluate / 维 / 条件 / fixed 带来的是实例化期的标记（依赖一路连带）；其余（此前某个绑定定型时标的）只是取值——本
      // 参数已定型，依赖不再连带。
      const PhaseScope self_phase(*this, struct_early ? MarkPhase::Early : MarkPhase::Binding);
      std::optional<CVal> cv;
      const StructSnap snap = struct_snap();
      std::unordered_set<std::uint32_t> used;
      if (auto it = const_vals_.find(vkey); it != const_vals_.end()) cv = it->second;
      if (!cv) {
        Cref self;
        self.parts.push_back(CrefPart{name.back(), {}});
        const FrameHit fh = frame_value(ceval_frame_, self, /*allow_param=*/true, 0, &used);
        if (fh.kind == FrameHit::Value) cv = fh.v;
      }
      // 只因 Evaluate = true 才结构化、但绑定用到 Evaluate = false 的参数：omc 不把它当结构参数（EV8 探针 `q = 3
      // annotation(Evaluate = false); p = q annotation(Evaluate = true)` → `parameter Real p = q`）→ 撤销求值带来的标记。
      if (eval_true && !struct_other)
        for (std::uint32_t d : used)
          if (evaluate_annotation(Id<SElement>::from_index(d)) == std::optional<bool>(false)) struct_final = false;
      if (!struct_final) {
        struct_restore(snap);
      } else {
        if (cv) { binding = literal_of(*cv); if (cty.kind == TypeKind::Real) binding = to_real(binding); }
        else if (eval_true) set_unsupported("Evaluate annotation");
        record_structural(vkey);
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
    if (eval_true && struct_final)   // fixed = false 时 omc 忽略 Evaluate（EV3 探针）；带 fixed 属性的组合未逐一核实 → SKIP
      for (const FlatVarAttr& a : attrs)
        if (in_.str(a.name) == "fixed") set_unsupported("Evaluate annotation");
    // 外层记录整体绑定已挪成初始方程：parameter 字段 fixed = false（Binding.setAttr：有则替换、无则追加，NFBinding.mo:821-836）。
    if (ctx.unbound_fields && var_min(var_floor, I_.comps[cid].attributes.variability) == SVariability::Param) {
      const Symbol fixed = in_.intern("fixed");
      bool replaced = false;
      for (FlatVarAttr& a : attrs)
        if (a.name == fixed) { a.value = lit_bool(false); replaced = true; }
      if (!replaced) attrs.push_back({fixed, lit_bool(false)});
    }
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
      const SDirection d = merged_dir(comp.attributes.direction, ctx);
      if (d == SDirection::Input) v.direction = FlatDirection::Input;
      else if (d == SDirection::Output) v.direction = FlatDirection::Output;
    }
    // 无绑定的 parameter（NFFlatten.verifyBinding :812-875；非 relaxed 模式才查，:742-746，普通翻译即非 relaxed，
    // NFInst.mo:167-168）。fixed 缺省 true：fixed 时有 start → omc 以 start 作默认绑定并告警（未做 → SKIP）；无 start →
    // UNBOUND_PARAMETER_ERROR 报错（UnboundParameter1 / InvalidVariability1/2 / InvalidComplexConnectorType1/2）→ SKIP；
    // fixed = false → 合法照印（YmcUnfixed1 探针）。fixed 非字面量（omc 先 tryEvalExp）未做 → SKIP。
    // 注：源码 HEAD 另有 constant 无绑定报错（NO_CONSTANT_BINDING :844-848），1.25.0 实跑不报、照印 `constant Integer n;`
    // （YmcUnbConst1 探针）→ 按 1.25.0 不查。
    if (v.var_kind == FlatVarKind::Parameter && !binding.is_valid()) unbound_param_ok(attrs);
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
      v.comment = I_.scode.elems[comp.def].cmt.str;
    result_.model.variables.push_back(std::move(v));
    const std::size_t vidx = result_.model.variables.size() - 1;
    if (!cur_pp_chain_.empty()) var_pp_.emplace(vidx, cur_pp_chain_);
    record_const(result_.model.variables[vidx]);
    // 未连接 flow → `name = 0.0`；`=0` 方程要排在本类方程之后（R13 §6），故先攒起来，run() 末尾统一追加。
    // nsubs 带元素下标（连接器数组 `c[i].f`）；与 name 平行，标量连接器时全空 → 打印无下标（旧行为）。
    if (is_flow) flow_vars_.push_back({name, nsubs, cty, comp.loc, vidx, false});
  }

  // 无绑定 parameter（标量 / 数组元素）按其属性判合法性（规则见 emit_variable 的注释）；不合法 → unsupported，返回 false。
  bool unbound_param_ok(const std::vector<FlatVarAttr>& attrs) {
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
    return fixed_lit && !is_fixed;
  }

  // 最高优先、带值、非 each 的 start 子修饰的值（`:` 维推断用）；没有 → invalid。
  Id<Exp> start_value(const std::vector<ModSource>& sources) {
    const Symbol start = in_.intern("start");
    for (const ModSource& s : sources)
      for (const SSubMod& sub : I_.scode.mods[s.mod].subs)
        if (sub.ident == start && I_.scode.mods[sub.mod].binding.is_valid())
          return I_.scode.mods[sub.mod].is_each ? Id<Exp>{} : I_.scode.mods[sub.mod].binding;
    return {};
  }

  // constant 数组按值发射（NFFlatten 对 constant 绑定 Ceval 后逐元素摊分）：绑定在书写处上下文求值（cref 先查已发射
  // 常量的最终值，修饰感知），形状须与声明维相符（`:` 维取值的形状），元素按声明类型强转（Int→Real）。有维为 0 → omc
  // 整体不出变量（CevalSub1 的 `i6[:] = zeros(0) - zeros(0)`）。成功发射返回 true；求不出 / 不符返回 false（调用方落回原路径）。
  bool emit_const_array(CompId cid, const std::vector<Symbol>& name, bool is_protected, const PSubs& psubs, Id<Exp> braw,
                        NodeId bscope, const std::vector<Symbol>& bprefix, const std::vector<Id<Exp>>& dim_exps,
                        const ScodeScope& dscope, bool is_final, const ModCtx& ctx) {
    std::optional<CVal> val;
    {
      const FrameAt frame_at(*this, bscope);
      CevalCtx guard(*this, bprefix, psubs);
      val = eval_scode(braw, scode_scope_of(bscope), 0);
    }
    if (!val || !is_arr(*val)) return false;
    const std::vector<std::int64_t> shape = cval_shape(*val);
    if (shape.size() != dim_exps.size()) return false;
    for (std::size_t k = 0; k < dim_exps.size(); ++k) {
      if (I_.absyn.exps[dim_exps[k]].kind == ExpKind::Colon) continue;
      std::vector<std::int64_t> one;
      if (!eval_dim(dim_exps[k], one, dscope) || one.size() != 1 || one[0] != shape[k]) return false;
    }
    const Type cty = I_.comps[cid].ty;
    std::vector<CVal> flat;
    flatten_cval_array(*val, flat);
    for (CVal& e : flat) {
      std::optional<CVal> ce = coerce_to_kind(e, cty.element_type().kind);
      if (!ce) return false;
      e = std::move(*ce);
    }
    if (std::find(shape.begin(), shape.end(), 0) != shape.end()) {   // 空数组：不出变量，只记形状
      empty_shapes_[int_key(name, psubs)] = shape;
      return true;
    }
    FlatDirection dir = FlatDirection::None;
    if (ctx.keep_dir) {
      const SDirection d = merged_dir(I_.comps[cid].attributes.direction, ctx);
      if (d == SDirection::Input) dir = FlatDirection::Input;
      else if (d == SDirection::Output) dir = FlatDirection::Output;
    }
    std::vector<std::int64_t> idx(shape.size(), 1);
    std::size_t fi = 0;
    do {
      FlatVariable v;
      v.name = name;
      v.name_subs = psubs_to_exprs(psubs);
      v.name_subs.push_back({});
      for (std::int64_t x : idx) v.subscripts.push_back(lit_int(x));
      v.ty = cty;
      v.is_protected = is_protected;
      v.is_final = is_final;
      v.var_kind = FlatVarKind::Constant;
      v.direction = dir;
      v.loc = I_.comps[cid].loc;
      v.binding = literal_of(flat[fi++]);
      result_.model.variables.push_back(std::move(v));
      if (!cur_pp_chain_.empty()) var_pp_.emplace(result_.model.variables.size() - 1, cur_pp_chain_);
      record_const(result_.model.variables.back());
    } while (next_index(idx, shape));
    PSubs bsubs = psubs;
    bsubs.resize(name.size());
    std::size_t pos = 0;
    const_vals_[int_key(name, bsubs)] = cval_nest(flat, shape, 0, pos);
    return true;
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
    // 本数组的绑定 / 属性定型序号（见 MarkPhase）。带 `:` 维的数组，绑定在组件定型期就为推断维而定型（NFTyping.typeDimension
    // :719-720，v1.25.0）→ 那里的标记算 Early（AO 探针 a2：`Real s[:] = x[1:n]` 前后的别名都 final，`Real s[2] = …` 只后面的）。
    bool colon_dim = false;
    for (Id<Exp> d : cel.attr.dims) colon_dim = colon_dim || I_.absyn.exps[d].kind == ExpKind::Colon;
    for (Id<Exp> d : cel.type.dims) colon_dim = colon_dim || I_.absyn.exps[d].kind == ExpKind::Colon;
    const PhaseScope bind_phase(*this, colon_dim ? MarkPhase::Early : MarkPhase::Binding);
    ++bind_clock_;
    {
      PSubs ws = psubs;
      ws.resize(name.size());   // 末段（叶）无下标 = 整体键
      typed_keys_.insert(int_key(name, ws));
    }
    // 外层记录整体绑定挪成方程后的数组字段（逐元素 fixed = false 等）未核实 → SKIP。
    if (ctx.unbound_fields) { set_unsupported("array field of record with equation binding"); return; }
    // `annotation(Evaluate = true)` 的参数数组：同标量（emit_variable），omc 求值成 final 字面量，未做 → SKIP（CevalFuncArray6）。
    if (I_.comps[cid].attributes.variability == SVariability::Param && evaluate_true(I_.comps[cid].def)) {
      set_unsupported("Evaluate annotation");
      return;
    }
    // 维度：组件维在前、类型维在后（R13 §3）。字面量 + 参数/常量/size()/算术求值。
    const ScodeScope dscope = scode_scope_of(enclosing);
    // 维度表达式：组件维在前、类型维在后（R13 §3）。先收集，绑定就绪后再解析（`:` 维靠绑定形状推断）。
    std::vector<Id<Exp>> dim_exps;
    for (Id<Exp> d : cel.attr.dims) dim_exps.push_back(d);
    for (Id<Exp> d : cel.type.dims) dim_exps.push_back(d);
    if (dim_exps.empty()) { set_unsupported("array dimension"); return; }
    // 属性来源（优先级序同 emit_variable）：外来子修饰 > 组件自身声明修饰。短类链派生修饰（类修饰 origin ≠ parent 时要补
    // fill 维，expandProxySubscripts :1616-1643）未做 → SKIP。
    if (!I_.comps[cid].derived_mods.empty()) { set_unsupported("array derived-type modifier"); return; }
    std::vector<ModSource> asrc;
    for (Id<SMod> m : mod_subs) asrc.push_back({m, mod_scope, mod_prefix});
    if (cel.mod.is_valid()) asrc.push_back({cel.mod, enclosing, prefix});
    // 绑定：外来修饰优先，否则声明绑定。要求是数组构造子 `{…}` / `array(…)`，否则 SKIP。
    Id<Exp> braw;
    bool braw_written = true;   // 绑定是源码里书写的（非 flatten 合成的修饰）：结构参数的别名连带用
    for (Id<SMod> m : mod_subs)
      if (I_.scode.mods[m].binding.is_valid()) {
        braw = I_.scode.mods[m].binding;
        braw_written = m.raw < synth_mod_floor_;
        break;
      }
    NodeId bscope = mod_scope;
    std::vector<Symbol> bprefix = mod_prefix;
    if (!braw.is_valid()) {
      const Binding& b = I_.comps[cid].binding;
      if (b.kind == BindingKind::Raw && b.raw_exp.is_valid()) { braw = b.raw_exp; bscope = enclosing; bprefix = prefix; }
    }
    mark_ctor_ranges(braw, bscope);
    // constant 数组绑 ndims(…) / 单实参 size(…)：更高变率的绑定，omc 报错（同标量，见 emit_variable）。
    if (absyn_param_size_call(braw) && var_min(ctx.var_floor, I_.comps[cid].attributes.variability) == SVariability::Const) {
      set_unsupported("ndims / size(a) in constant binding");
      return;
    }
    // 非构造子绑定（= y / = 2*y / …，含推导式 `{e for i in …}`），下面按值（constant）或类型化摊分。
    const bool comprehension = braw.is_valid() && I_.absyn.exps[braw].kind == ExpKind::Array &&
                               I_.absyn.exps[braw].args.size() == 1 &&
                               I_.absyn.exps[I_.absyn.exps[braw].args[0]].kind == ExpKind::Reduction;
    const bool nlbind = braw.is_valid() && (!is_array_ctor(braw) || comprehension);
    // `:` 维由绑定定、而绑定的维是类型维（`Real z[:] = y`，y: Real[E] → z 也成 `z[E.one]`…，EN 探针 t3）：只做了推导式里
    // 写类型名的范围（见下面的维推断），其余——引用带类型维的数组的绑定、在这种数组上迭代的推导式范围——未做 → SKIP。
    {
      bool has_colon = false;
      for (Id<Exp> d : dim_exps) has_colon = has_colon || I_.absyn.exps[d].kind == ExpKind::Colon;
      if (has_colon && braw.is_valid()) {
        bool typed_dim = false;
        if (!comprehension) {
          typed_dim = absyn_refs_type_dim(braw, bscope, bprefix);
        } else {
          const std::vector<ForIndex> its = I_.absyn.exps[I_.absyn.exps[braw].args[0]].iters;   // 拷贝
          for (const ForIndex& it : its)
            if (it.range.is_valid() && !classify_type_dim(it.range, bscope, bprefix) && absyn_refs_type_dim(it.range, bscope, bprefix))
              typed_dim = true;
        }
        if (result_.unsupported) return;
        if (typed_dim) { set_unsupported("colon dimension from type-dimension binding"); return; }
      }
    }
    // 顶层 input/output 数组带绑定：omc 丢方向+绑定（标量不丢，探针实测），该 quirk 未复刻 → SKIP（TopLevelInputs2）。
    if (braw.is_valid() && prefix.empty() && merged_dir(I_.comps[cid].attributes.direction, ctx) != SDirection::Bidir) {
      set_unsupported("top-level directional array with binding"); return;
    }
    // constant 数组的非构造子绑定（`{1,2,3} + {3,4,5}`、`m1 * v1`、`1.0:3.0`、`zeros(4, 3)`…）：按值求值（NFCeval）→
    // 维取值的形状（`:` 维由此定）、逐元素字面量；求不出 → 落回下面的类型化路径（多半 SKIP）。
    if (nlbind && var_min(ctx.var_floor, I_.comps[cid].attributes.variability) == SVariability::Const) {
      if (emit_const_array(cid, name, is_protected, psubs, braw, bscope, bprefix, dim_exps, dscope,
                           I_.comps[cid].attributes.is_final || force_final, ctx))
        return;
      if (result_.unsupported) return;
    }
    // 非构造子绑定的类型化值：`:` 维推断时提前求，下面的摊分 / 数组方程复用（只 inst 一次）。
    Id<Expr> rhsE;
    auto typed_rhs = [&]() -> Id<Expr> {
      if (!rhsE.is_valid()) {
        array_call_ok_ = I_.absyn.exps[braw].kind == ExpKind::Call;   // 整个绑定是调用（见 array_call_ok_）
        if_top_ = I_.absyn.exps[braw].kind == ExpKind::IfExp;         // 整个绑定是 if 表达式（见 if_top_）
        rhsE = simplify_expr(inst_exp(braw, bscope, bprefix));
        array_call_ok_ = false;
        if_top_ = false;
      }
      return rhsE;
    };
    // 解析各维成整数：字面量/参数/size()/算术走 eval_dim；`:` 维从绑定构造子形状推断（CevalArray*）。
    // 两趟：先定 `:` 维并登记到 pending_shapes_，再定其余的——别的维可以用 size() 引用本数组的 `:` 维
    //（`parameter Real A[:, size(A, 1)] = {{1, 0}, {0, 1}}`，Modelica.Blocks.Continuous.StateSpace 的写法；DM 探针 D8）。
    std::vector<std::vector<Id<Expr>>> dim_labels(dim_exps.size());   // 每维下标标签：整数维 IntLit 1..n；类型维 false/true 或 E.lit
    const std::string self_key = int_key(name, psubs);
    struct PendingShape {
      Flattener& f; const std::string& key;
      PendingShape(Flattener& fl, const std::string& k, std::size_t n) : f(fl), key(k) { f.pending_shapes_[key].assign(n, -1); }
      ~PendingShape() { f.pending_shapes_.erase(key); }
    } pending(*this, self_key, dim_exps.size());
    for (const bool colon_round : {true, false})
    for (std::size_t di = 0; di < dim_exps.size(); ++di) {
      if ((I_.absyn.exps[dim_exps[di]].kind == ExpKind::Colon) != colon_round) continue;
      std::vector<Id<Expr>> labels;
      if (auto tl = classify_type_dim(dim_exps[di], enclosing, prefix)) {
        labels = std::move(*tl);   // 类型名维（Boolean / 枚举，NFDimension.fromExp）
      } else if (result_.unsupported) {
        return;
      } else if (I_.absyn.exps[dim_exps[di]].kind == ExpKind::Colon) {
        // 无绑定时改用 start 属性的值推断（NFTyping.typeDimension :705-711；DimUnknown7/10/11）。
        std::optional<std::int64_t> n = binding_dim_size(braw.is_valid() ? braw : start_value(asrc), di);
        // 非字面量绑定：取类型化绑定的第 di 维（deduceDimensionFromExp，NFTyping.mo:781-826）；该维未知时 omc 再对绑定求值，
        // 未做 → SKIP。
        // 推导式 `{e for i in r, …}`：各维 = 对应范围的长度（comprehension_shape；迭代变量个数须等于维数）；范围写的是
        // 类型名的那一维是类型维，下标标签取该类型的。
        if (!n && comprehension) {
          std::vector<std::vector<Id<Expr>>> cl;
          if (const std::optional<std::vector<std::int64_t>> csh = comprehension_shape(braw, bscope, bprefix, &cl);
              csh && csh->size() == dim_exps.size()) {
            n = (*csh)[di];
            if (!cl[di].empty()) labels = cl[di];
          }
        }
        if (!n && nlbind && !comprehension) {
          const std::vector<std::int64_t> sh = expr_shape(typed_rhs());
          if (result_.unsupported) return;
          if (sh.size() == dim_exps.size() && sh[di] >= 0) n = sh[di];
        }
        if (!n) { set_unsupported("non-literal array dimension"); return; }
        if (labels.empty())
          for (std::int64_t i = 1; i <= *n; ++i) labels.push_back(lit_int(i));
      } else {
        std::vector<std::int64_t> one;
        if (!eval_dim(dim_exps[di], one, dscope) || one.size() != 1) {
          set_unsupported("non-literal array dimension"); return;
        }
        for (std::int64_t i = 1; i <= one[0]; ++i) labels.push_back(lit_int(i));
      }
      if (colon_round) pending_shapes_[self_key][di] = static_cast<std::int64_t>(labels.size());
      dim_labels[di] = std::move(labels);
    }
    std::vector<std::int64_t> dims;
    for (const std::vector<Id<Expr>>& l : dim_labels) dims.push_back(static_cast<std::int64_t>(l.size()));
    const Type cty = I_.comps[cid].ty;
    // 空数组（某维为 0）：标量化成零个变量、类型为空数组的绑定方程被删（NFSimplifyModel.simplifyEqualityEquation :254）→
    // 整体不出行（EmptyArrayAlgorithm、Array13 / 14）。绑定须与声明同形同类（否则 omc 类型错）；带属性修饰、字面量绑定未核实
    // → SKIP。被删绑定里登记过的函数由 prune_functions 收走（omc 的函数收自化简后的模型）。
    if (std::find(dims.begin(), dims.end(), 0) != dims.end()) {
      for (const ModSource& s : asrc)
        if (!I_.scode.mods[s.mod].subs.empty()) { set_unsupported("empty array attribute"); return; }
      if (braw.is_valid()) {
        if (!nlbind) { set_unsupported("empty array literal binding"); return; }
        const std::vector<std::int64_t> bsh = expr_shape(typed_rhs());
        if (result_.unsupported) return;
        if (bsh != dims || !elem_kinds_compat(cty.element_type().kind, expr_elem_kind(rhsE))) {
          set_unsupported("non-literal array binding shape");
          return;
        }
      }
      empty_shapes_[int_key(name, psubs)] = dims;
      return;
    }
    // 连接器里的数组成员（`flow Real f[2]`）：与 emit_variable 同一套守卫（stream / 成员方向 / 连接器外 flow），
    // flow 元素攒进 flow_vars_（未连接 → `f[k] = 0.0`，collectFlows 按变量声明序，NFConnections.mo:139-166）。
    if (I_.comps[cid].attributes.connector == SConnectorType::Stream) { set_unsupported("stream connector"); return; }
    const bool in_conn = ctx.in_conn;   // 连接器内部（含连接器里的记录字段）
    if (I_.comps[cid].attributes.connector == SConnectorType::Flow && !in_conn) { set_unsupported("flow outside connector"); return; }
    const bool is_flow = in_conn && (I_.comps[cid].attributes.connector == SConnectorType::Flow || ctx.flow_inherited);
    FlatVarKind vk = FlatVarKind::Var;
    switch (var_min(ctx.var_floor, I_.comps[cid].attributes.variability)) {   // 复合组件变率下沉（记录里的数组字段）
      case SVariability::Var: vk = FlatVarKind::Var; break;
      case SVariability::Discrete: vk = FlatVarKind::Discrete; break;
      case SVariability::Param: vk = FlatVarKind::Parameter; break;
      case SVariability::Const: vk = FlatVarKind::Constant; break;
    }
    std::vector<AttrPlan> aplan;
    {
      const PhaseScope attr_phase(*this, MarkPhase::Binding);   // 属性总在绑定定型期（`:` 维只让绑定提前）
      aplan = plan_attributes(cid, asrc, dims);
    }
    if (result_.unsupported) return;
    // 参数/常量数组绑定 omc 摊到每元素（`x[1]=1.0`…），连续（Var）数组才变成数组方程（`x = {…}`，Subscript1）。
    // v1 摊分只做"字面量元素"（下降 Array 到标量）；元素含 cref 数组等 → 逐元素时命中会 SKIP。
    const bool spread = braw.is_valid() && vk != FlatVarKind::Var;
    // 数组字面量绑定的形状/类型要与数组匹配（omc 会做类型检查并报错，如 BindingInvalidType3/5）——ymc 无验证 pass，
    // 只放行形状+叶类型都对上的字面量绑定，否则 SKIP。
    if (braw.is_valid() && !nlbind && !array_lit_matches(braw, dims, 0, cty.kind)) {
      set_unsupported("array binding shape/type mismatch"); return;
    }
    // 参数 / 常量数组的推导式绑定 `{e for i in r}`：omc 逐元素取 `{…}[i]` 后代入迭代变量化简（applyIndexSubscriptArrayConstructor
    // + replaceIterator，NFExpression.mo:1932-1952；C1 探针 `p = {i^2 for i in 1:n}` → `p[2] = 4.0`）。下面逐元素做；连续数组
    // 的推导式绑定 omc 保留 `array(… for …)`（未做，走 typed 路 SKIP）。
    const bool comp_spread = comprehension && spread;
    if (comp_spread) {
      // 推导式的形状须与声明维一致（omc 类型错；ND 探针 c2：`parameter Integer s[2] = {i for i in 1:3}` 此前放行）。
      const std::optional<std::vector<std::int64_t>> csh = comprehension_shape(braw, bscope, bprefix);
      if (result_.unsupported) return;
      if (!csh) { set_unsupported("comprehension range"); return; }
      if (*csh != dims) { set_unsupported("array binding shape/type mismatch"); return; }
    }
    // 非构造子绑定（数组值 `= y` / `= 2*y`）：类型化一次 + 形状/变率守卫（对标 NFFlatten:700 / NFScalarize）。
    if (nlbind && !comp_spread) {
      typed_rhs();
      if (result_.unsupported) return;   // inst_exp 已拒绝切片/常量数组/限定名等
      // 数组调用的类型可带未知维（调用处求不出的输出维），与任何维相容；其余须同形（形状看穿字面量 / 一元的粗类型）。
      const ExprKind rk0 = result_.model.exprs[rhsE].kind;
      const bool arr_call = rk0 == ExprKind::Call || rk0 == ExprKind::TupleElement || rk0 == ExprKind::Array;
      const std::vector<std::int64_t> rsh = expr_shape(rhsE);
      bool shape_ok = rsh.size() == dims.size();
      for (std::size_t k = 0; shape_ok && k < dims.size(); ++k)
        if (rsh[k] != dims[k] && !(arr_call && rsh[k] < 0)) shape_ok = false;
      if (!shape_ok) { set_unsupported("non-literal array binding shape"); return; }
      if (vk == FlatVarKind::Discrete) { set_unsupported("discrete array binding"); return; }   // omc 报错（需 when）
      if (vk != FlatVarKind::Var && cond_is_runtime(rhsE)) {   // param/const 绑更高变率 = omc 报错（B3/C3）
        set_unsupported("array binding higher variability"); return;
      }
    }
    const bool is_final = I_.comps[cid].attributes.is_final || force_final;
    // 方向保留：所有前缀组件都是连接器或 input 组件（Variable.removeNonTopLevelDirection，NFVariable.mo:466-493）。
    FlatDirection dir = FlatDirection::None;
    if (ctx.keep_dir) {
      const SDirection d = merged_dir(I_.comps[cid].attributes.direction, ctx);
      if (d == SDirection::Input) dir = FlatDirection::Input;
      else if (d == SDirection::Output) dir = FlatDirection::Output;
    }

    // 行主序枚举下标元组（1 基），逐元素产出变量行；param/const 数组把绑定摊到每元素。
    // constant 元素的值另攒成整体数组记入 const_vals_（整取引用 `m1 * v1` 用）；有非 CVal 字面量（枚举）则不记。
    std::vector<CVal> const_flat;
    bool const_all = vk == FlatVarKind::Constant;
    // 参数数组的绑定在源码里是裸 cref（`parameter Boolean[3] b = surBou.isFloor`）：各元素是结构参数别名连带的候选，且整个数组
    // 一起连带（CevalRecordArray10：目标元素都是结构参数 → 三个元素都 final）；见 apply_structural_params。
    bool alias_arr = false;
    if (spread && vk == FlatVarKind::Parameter && braw_written) {
      Id<Exp> b0 = braw;
      while (I_.absyn.exps[b0].kind == ExpKind::Tuple && I_.absyn.exps[b0].args.size() == 1) b0 = I_.absyn.exps[b0].args[0];
      alias_arr = I_.absyn.exps[b0].kind == ExpKind::CrefExp;
    }
    if (alias_arr) alias_groups_.emplace_back();
    std::vector<std::int64_t> idx(dims.size(), 1);
    for (bool more = true; more;) {
      FlatVariable v;
      v.name = name;
      v.name_subs = psubs_to_exprs(psubs);
      v.name_subs.push_back({});   // 叶数组的下标走 v.subscripts（末段），逐段 name_subs 末段留空
      // 组件声明注释逐元素照打（同标量，WillowForest：`parameter Real e[1] = 0.73 "Radiation efficiency fraction";`）。
      if (cel.cmt.str.is_valid()) v.comment = cel.cmt.str;
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
      {
        const PhaseScope attr_phase(*this, MarkPhase::Binding);
        v.attributes = attributes_at(aplan, idx);
      }
      if (result_.unsupported) return;
      // 无绑定的 parameter 元素（verifyBinding，同标量规则；YmcUnbArr1 探针、BuiltinAttribute21 `each fixed = false`）。
      if (vk == FlatVarKind::Parameter && !braw.is_valid() && !unbound_param_ok(v.attributes)) return;
      if (spread) {
        Id<Expr> eb;
        if (comp_spread) {
          eb = comprehension_elem(braw, idx, bscope, bprefix);
          if (result_.unsupported) return;
          if (vk == FlatVarKind::Discrete || refs_runtime_var(eb)) { set_unsupported("array binding higher variability"); return; }
        } else if (nlbind) {
          // 逐元素：y[i] / 2.0*y[i]（B1/B2/B4/B5/B8）；含推导式的也逐元素代入（R2 探针 `q = 2 * linspace(1, 2, 3)` →
          // `q[2] = 3.0`、`p = {i * 1.5 for i in 1:3} + q` → `p[1] = 1.5 + q[1]`）。
          const RedScalarize rs(red_scalarize_ok_);
          eb = simplify_expr(scalarize(rhsE, idx));
          if (result_.unsupported) return;
        } else {
          // 该下标处的字面量元素（下降 Array 到标量）；命中 cref 数组等 → 本增量 SKIP。
          Id<Exp> el = array_scalar_elem(braw, idx, 0);
          if (!el.is_valid()) { set_unsupported("array binding element spread"); return; }
          eb = simplify_expr(inst_exp(el, bscope, bprefix));
          if (result_.unsupported) return;
          if (!leaf_type_ok(eb, cty.kind)) { set_unsupported("array binding shape/type mismatch"); return; }
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
      if (alias_arr) {
        const std::string ek = var_key(result_.model.variables[vidx]);
        alias_keys_.emplace(ek, bind_clock_);
        alias_groups_.back().push_back(ek);
      }
      if (!cur_pp_chain_.empty()) var_pp_.emplace(vidx, cur_pp_chain_);
      if (is_flow) flow_vars_.push_back({name, std::move(fsubs), cty, I_.comps[cid].loc, vidx, false});
      if (vk == FlatVarKind::Parameter) record_const(result_.model.variables[vidx]);
      if (const_all) {
        const FlatVariable& cvv = result_.model.variables[vidx];
        std::optional<CVal> ev = cvv.binding.is_valid() ? cval_of(cvv.binding) : std::nullopt;
        if (ev) { record_const(cvv); const_flat.push_back(std::move(*ev)); } else { const_all = false; }
      }
      // 递增（最后一维最快）。
      std::size_t k = dims.size();
      while (k > 0) {
        --k;
        if (++idx[k] <= dims[k]) break;
        idx[k] = 1;
        if (k == 0) more = false;
      }
    }

    if (const_all && !const_flat.empty()) {
      PSubs bsubs = psubs;
      bsubs.resize(name.size());   // 末段（叶）无下标 = 整体键
      std::size_t pos = 0;
      const_vals_[int_key(name, bsubs)] = cval_nest(const_flat, dims, 0, pos);
    }
    // Var 数组绑定 → 单条数组方程（param/const 已在上面摊成逐元素绑定）。
    // 构造子：`name = {…}`；非构造子：整取同型 cref → `name = y`（保持整取，NFScalarize ARRAY_EQUALITY 不标量化）；
    // 逐元素算子 → 展开成 `{op(y[1]),…}`（NFExpandExp）；整取 Int→Real（数组 cast）/Call/Range → SKIP。
    if (braw.is_valid() && !spread) {
      Id<Expr> rhs;
      if (nlbind) {
        rhs = whole_array_value(rhsE, dims, cty);
        if (!rhs.is_valid()) return;
      } else {
        rhs = simplify_expr(inst_exp(braw, bscope, bprefix));
        if (result_.unsupported) return;
        if (!array_leaves_ok(rhs, dims.size(), cty.kind)) { set_unsupported("array binding shape/type mismatch"); return; }
        rhs = whole_array_value(rhs, dims, cty);   // 字面量里的数组值元素各自规整（`{m, m}` → `{/*Real[2]*/(m), …}`，w5）
        if (!rhs.is_valid()) return;
      }
      Expr lhs;
      lhs.kind = ExprKind::Cref;
      lhs.ty = cty;
      // 左侧带逐段下标（复合数组元素 `a[1].x = {…}`，CA1 探针；曾丢下标错出 `a.x = …`）。
      const std::vector<std::vector<Id<Expr>>> lsubs = psubs_to_exprs(psubs);
      for (std::size_t k = 0; k < name.size(); ++k)
        lhs.cref.parts.push_back({name[k], k < lsubs.size() ? lsubs[k] : std::vector<Id<Expr>>{}});
      FlatEquation fe;
      fe.kind = FlatEqKind::Equality;
      fe.a = add(std::move(lhs));
      fe.b = rhs;
      fe.loc = I_.comps[cid].loc;
      result_.model.equations.push_back(result_.model.add_eq(std::move(fe)));
      if (refs_runtime_var(rhs)) {   // 供 when_target_runtime_bound
        std::string nm;
        for (Symbol sy : name) { if (!nm.empty()) nm += '.'; nm += in_.str(sy); }
        runtime_bound_arrays_.insert(std::move(nm));
      }
    }
  }

  // 整体保留的数组值规整成 omc 的打印形态（NFSimplifyExp 化简 + NFExpandExp 展开后的样子）——Var 数组的绑定方程 `x = …`
  // 与模型算法段的整数组赋值 `x := …` 共用（赋值语句不标量化：scalarizeStatement 只下到 for / if / when / while 的体里，
  // NFScalarize.mo:443-462 v1.25.0）。rhsE = 已定型、化简过的右侧，dims = 目标各维，cty = 目标元素类型。
  // 不能保留 / 未核实的形态 → set_unsupported、返回 invalid。
  Id<Expr> whole_array_value(Id<Expr> rhsE, const std::vector<std::int64_t>& dims, const Type cty) {
    Id<Expr> rhs;
    std::vector<Dimension> xd;
    for (std::int64_t n : dims) { Dimension d; d.kind = DimKind::Integer; d.size = n; xd.push_back(d); }
    const Expr re = result_.model.exprs[rhsE];   // 拷贝（下面会往 arena 加）
    const ExprKind rk = re.kind;
    const TypeKind relem = re.ty.element_type().kind;
    if (rk == ExprKind::Cref && relem == cty.kind) {
      rhs = rhsE;   // x = y、y = x[2:3]、w = z[1,:]（整取 / 切片同元素类型保持整体，A1/A5/A9/I1、SL1 探针）
    } else if (rk == ExprKind::Cref && relem == TypeKind::Integer && cty.kind == TypeKind::Real) {
      rhs = array_real_cast(rhsE);   // Integer 整取 / 切片进 Real 数组：整体转型 `/*Real[2]*/(n)`、`/*Real[2]*/(n[1:2])`（WA 探针 w2）
    } else if ((rk == ExprKind::Call || rk == ExprKind::TupleElement) && relem == cty.kind) {
      rhs = rhsE;   // x = f(n)：含数组调用的等式整体保留（NFScalarize :343-346；IfExpression11 / FuncUnknownDim3）
    } else if (rk == ExprKind::Reduction) {
      // 推导式绑定保留 `array(e for i in r)`；Integer 元素对 Real 数组整体转型（AC2 探针 `x = /*Real[3]*/(array(i for …))`）。
      if (relem == cty.kind) rhs = rhsE;
      else if (relem == TypeKind::Integer && cty.kind == TypeKind::Real) rhs = array_real_cast(rhsE);
      else { set_unsupported("comprehension binding type"); return {}; }
    } else if (rk == ExprKind::Array) {
      // 字面量：数组值的元素各自规整（WA 探针 w3 `y = {z, 2 * z}` → `{z, {2.0 * z[1], 2.0 * z[2]}}`：整取留着、算术式
      // 展开），再逐叶转型（常量数组调用求值成的字面量，FuncUnknownDim2）。
      Expr lit = re;
      if (dims.size() > 1) {
        const std::vector<std::int64_t> sub(dims.begin() + 1, dims.end());
        for (Id<Expr>& el : lit.args)
          if (result_.model.exprs[el].kind != ExprKind::Array || sub.size() > 1) {
            el = whole_array_value(el, sub, cty);
            if (!el.is_valid()) return {};
          }
      }
      rhs = coerce_deep(add(std::move(lit)), cty.kind);
    } else if (rk == ExprKind::Range && range_lit(re)) {
      // 字面量范围整体保留（`x = 1:5`）；Real 数组的范围起 / 步 / 止都转 Real（RG2 探针 `r = 1.0:3.0`、`s = 1.0:2.0:8.0`）。
      Expr r = re;
      if (relem == TypeKind::Boolean && cty.kind != TypeKind::Boolean) {
        set_unsupported("non-literal array binding"); return {};   // Boolean 范围绑给非 Boolean 数组：omc 类型错
      } else if (cty.kind == TypeKind::Real) {
        r.a = to_real(r.a);
        r.b = to_real(r.b);
        if (r.c.is_valid()) r.c = to_real(r.c);
        r.ty = Type::real();
      } else if (relem != cty.kind) {
        set_unsupported("non-literal array binding"); return {};
      }
      rhs = add(std::move(r));
    } else if (rk == ExprKind::IfExp && re.ty.is_array()) {
      // 数组 if 绑定整体保留（V4r 探针 `i2 = if time > 1.0 then x else {1.0, 2.0, 3.0}`）；Integer 元素配 Real 数组：字面量支
      // 逐叶转（`j2 = if time > 1.0 then {1.0, 2.0} else {3.0, 4.0}`），其余支的整体转型形态未核实 → SKIP。
      if (relem == cty.kind) {
        rhs = rhsE;
      } else if (relem == TypeKind::Integer && cty.kind == TypeKind::Real) {
        std::function<Id<Expr>(Id<Expr>)> conv = [&](Id<Expr> x) -> Id<Expr> {
          const Expr xe = result_.model.exprs[x];   // 拷贝
          if (xe.kind == ExprKind::Array) return coerce_deep(x, TypeKind::Real);
          if (xe.kind == ExprKind::Cref) return array_real_cast(x);   // `if a > 0.0 then /*Real[2]*/(n) else {3.0, 4.0}`（w2）
          if (xe.kind != ExprKind::IfExp) return {};
          Expr r = xe;
          r.b = conv(xe.b);
          r.c = conv(xe.c);
          if (!r.b.is_valid() || !r.c.is_valid()) return {};
          r.ty = Type::array_of(Type::real(), xe.ty.dims);
          return add(std::move(r));
        };
        rhs = conv(rhsE);
        if (!rhs.is_valid()) { set_unsupported("if-expression binding type"); return {}; }
      } else {
        set_unsupported("if-expression binding type"); return {};
      }
    } else if ((rk == ExprKind::Logical || rk == ExprKind::LogicalNot) && relem == cty.kind) {
      // Boolean 数组的 and / or / not：omc 不展开，整体保留（V4u 探针 `g1 = b and b2`、`g3 = b and not b2`、
      // `g9 = {true, false} or b`、`m3 = not {b[1], true}`）。全字面量的已在定型时折成字面量数组（走上面的 Array 支）。
      rhs = rhsE;
    } else if (rk == ExprKind::Binary || rk == ExprKind::Unary) {
      std::vector<Id<Expr>> elems;   // 逐元素展开 {op(y[1]), …}（A2/A3/A8/G3/G4）
      std::vector<std::int64_t> eidx(dims.size(), 1);
      for (bool more = true; more;) {
        Id<Expr> se = simplify_expr(scalarize(rhsE, eidx));
        if (result_.unsupported) return {};
        if (cty.kind == TypeKind::Real) se = to_real(se);
        elems.push_back(se);
        std::size_t k = dims.size();
        while (k > 0) { --k; if (++eidx[k] <= dims[k]) break; eidx[k] = 1; if (k == 0) more = false; }
      }
      // 行主序平铺 → 按维嵌套（多维 `{{2.0, 3.0}, {5.0, 6.0}}`，ArrayAddSub1）。
      std::size_t pos = 0;
      auto nest = [&](auto& self, std::size_t k) -> Id<Expr> {
        if (k == dims.size()) return elems[pos++];
        Expr arr;
        arr.kind = ExprKind::Array;
        arr.ty = Type::array_of(cty, std::vector<Dimension>(xd.begin() + static_cast<std::ptrdiff_t>(k), xd.end()));
        for (std::int64_t i = 0; i < dims[k]; ++i) arr.args.push_back(self(self, k + 1));
        return add(std::move(arr));
      };
      rhs = nest(nest, 0);
    } else {
      set_unsupported("non-literal array binding"); return {};   // 整取 Int→Real(A6)、Call、Range → SKIP
    }
    return rhs;
  }

  // 数组字面量绑定在多下标 idx 处的标量元素（Absyn）：逐维下降 Array（1 基）。命中非 Array 但仍有剩余下标
  //（cref 数组元素 `{x, …}` 等）→ 返回 invalid，调用方 SKIP（本增量只做纯字面量下降）。
  Id<Exp> array_scalar_elem(Id<Exp> e, const std::vector<std::int64_t>& idx, std::size_t start) {
    if (start == idx.size()) return e;
    const ExpKind k = I_.absyn.exps[e].kind;
    // `{…}`、`array(…)`，或矩阵 `[…; …]`（args 是行 Array 节点：先取行、再在行里取列；元素是构造子的拼接形由调用方
    // 先经 lit_dims 拒掉）。
    if (is_array_ctor(e) || k == ExpKind::Matrix) {
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
        subs.push_back(synth_exp(std::move(lit)));
      }
      nc.parts.back().subscripts = std::move(subs);
      Exp ce;
      ce.kind = ExpKind::CrefExp;
      ce.cref = synth_cref(std::move(nc));
      return synth_exp(std::move(ce));
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
      // 表达式叶（`{time, 2, 3}`、`{x[1], f(y)}`）：Absyn 看不出类型，定型后由 leaf_type_ok / array_leaves_ok 再核。
      case ExpKind::CrefExp: case ExpKind::Binary: case ExpKind::Call: case ExpKind::IfExp: case ExpKind::Subscript:
      case ExpKind::Tuple:
        return true;
      default: return false;
    }
  }
  // 数组绑定叶（已定型）与元素类型相容：Real ← Integer / Real；其余同类（Integer / Boolean / String / 枚举）；叶是数组 →
  // false（类型不符 omc 报错 → 调用方 SKIP）。
  bool leaf_type_ok(Id<Expr> x, TypeKind elem) {
    if (!expr_shape(x).empty()) return false;
    const TypeKind k = result_.model.exprs[x].ty.kind;
    return elem == TypeKind::Real ? (k == TypeKind::Real || k == TypeKind::Integer) : k == elem;
  }
  // 定型后的数组字面量绑定逐叶核类型（深 depth 层的叶）；某层不是数组字面量（cref 数组填整段维，BindingArray8）→ 不在此核。
  bool array_leaves_ok(Id<Expr> x, std::size_t depth, TypeKind elem) {
    if (depth == 0) return leaf_type_ok(x, elem);
    if (result_.model.exprs[x].kind != ExprKind::Array) return true;
    const std::vector<Id<Expr>> args = result_.model.exprs[x].args;   // 拷贝（expr_shape 可能往 arena 加）
    for (Id<Expr> a : args)
      if (!array_leaves_ok(a, depth - 1, elem)) return false;
    return true;
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

  // 绑定字面量（构造子 / 矩阵）第 di 维的大小；供 `:` 维从绑定推断形状（NFTyping.typeDimension :692-730）。
  std::optional<std::int64_t> binding_dim_size(Id<Exp> b, std::size_t di) {
    if (!b.is_valid()) return std::nullopt;
    const std::optional<std::vector<std::int64_t>> d = lit_dims(b);
    if (!d || di >= d->size()) return std::nullopt;
    return (*d)[di];
  }

  // 数组维度求值（v1：只认字面量整数；参数/size/: 等 → false）。
  bool eval_dim(Id<Exp> d, std::vector<std::int64_t>& out, const ScodeScope& scope,
                std::unordered_set<std::uint32_t>* used = nullptr) {
    const Exp& e = I_.absyn.exps[d];
    if (e.kind == ExpKind::IntLit && e.ival >= 0) { out.push_back(e.ival); return true; }
    // 参数/常量/size()/算术维度：结构求值成整数（Dim1/DimSize1）；用到的参数记入 used（→ 标 final）。维里的参数在实例化期
    // 就标（Structural.markDimension，NFInst.mo:3922，v1.25.0）。
    const PhaseScope early(*this, MarkPhase::Early);
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

  // 收集标量叶组件的内建属性（R13 §4.2）：数组叶组件的特例（零维，每个属性值整取）。
  std::vector<FlatVarAttr> collect_attributes(CompId cid, const std::vector<ModSource>& sources) {
    const std::vector<AttrPlan> plans = plan_attributes(cid, sources, {});
    if (result_.unsupported) return {};
    return attributes_at(plans, {});
  }

  // 一个内建属性的取值计划。Whole = 每元素同值（标量组件 / each，value 已定）；Literal = 字面量数组按元素下标下降（raw）；
  // Typed = 类型化的整体数组值（value）按元素标量化。
  struct AttrPlan {
    enum class Mode : std::uint8_t { Whole, Literal, Typed };
    const AttrSpec* spec;
    Symbol name;
    Mode mode;
    Id<Exp> raw;
    NodeId scope;
    std::vector<Symbol> vprefix;
    Id<Expr> value;
  };

  // 内建属性的取值计划（R13 §4.2）。sources 按优先级排（外来修饰在前、组件声明修饰在后），只看各来源的 **子修饰**（.subs）
  // ——顶层 .binding 是叶绑定另行处理；按类型属性表顺序，每属性取最高优先、带值的来源。dims 非空 = 数组叶组件：非 each
  // 属性值按元素切分（Modifier.propagate 给非 each 子修饰追加 SPLIT_PROXY，NFModifier.mo:444-513；压平时换成元素下标再
  // applySubscripts，NFFlatten.mo:1625-1683），值须恰是该形状的数组（标量 → MODIFIER_NON_ARRAY_TYPE_ERROR，Each3；维不符 →
  // VARIABLE_BINDING_DIMS_MISMATCH，BuiltinAttribute24/25/26）；each 属性每元素同值、须是标量。
  // v1 范围外的修饰形态（嵌套子修饰 / final / redeclare / break、或类型无此属性名）→ 标 unsupported。
  std::vector<AttrPlan> plan_attributes(CompId cid, const std::vector<ModSource>& sources,
                                        const std::vector<std::int64_t>& dims) {
    std::vector<AttrPlan> out;
    const Type ty = I_.comps[cid].ty;
    const std::vector<AttrSpec>& table = attr_table(ty.kind);
    auto droppable = [&](Symbol name) -> bool {
      if (name == in_.intern("unbounded") || name == in_.intern("startOrigin")) return true;
      return ty.kind == TypeKind::String && name == in_.intern("fixed");
    };
    // 校验各来源的属性子。无值无子修饰的 `each start` / `final start` 等于没写（typeTypeAttribute 换成 NOMOD，
    // NFTyping.mo:1281-1286；BuiltinAttribute20）。
    for (const ModSource& src : sources)
      for (const SSubMod& s : I_.scode.mods[src.mod].subs) {
        const SMod& sm = I_.scode.mods[s.mod];
        if (sm.kind == SModKind::Mod && sm.subs.empty() && !sm.binding.is_valid()) continue;
        bool simple = sm.kind == SModKind::Mod && !sm.is_final && sm.subs.empty();
        bool named = droppable(s.ident);
        for (const AttrSpec& a : table)
          if (s.ident == in_.intern(a.name)) named = true;
        if (!simple || !named) { set_unsupported("attribute modifier"); return out; }
      }
    for (const AttrSpec& a : table) {
      const Symbol an = in_.intern(a.name);
      const ModSource* src = nullptr;
      Id<SMod> smid;
      bool each_unbound = false;   // 更高优先来源写了无值的 `each a`：合并后 each 取外层（NFModifier.merge :406-413）
      for (const ModSource& s : sources) {
        for (const SSubMod& sub : I_.scode.mods[s.mod].subs) {
          if (sub.ident != an) continue;
          if (I_.scode.mods[sub.mod].binding.is_valid()) { smid = sub.mod; break; }
          if (I_.scode.mods[sub.mod].is_each) each_unbound = true;
        }
        if (smid.is_valid()) { src = &s; break; }
      }
      if (!smid.is_valid()) continue;
      const SMod sm = I_.scode.mods[smid];   // 拷贝（下面 inst 可能扩 arena）
      if (!dims.empty() && each_unbound && !sm.is_each) { set_unsupported("each without value over attribute"); return out; }
      AttrPlan p{&a, an, AttrPlan::Mode::Whole, sm.binding, src->scope, src->vprefix, {}};
      const bool matrix = I_.absyn.exps[sm.binding].kind == ExpKind::Matrix;
      if (dims.empty() || sm.is_each) {
        Id<Expr> v = attr_value(a.vk, sm.binding, src->scope, src->vprefix);
        if (!finish_attr(a, an, v, sm.binding, src->scope)) return out;
        p.value = v;
      } else if ((is_array_ctor(sm.binding) && !is_comprehension(sm.binding)) || matrix) {
        const std::optional<std::vector<std::int64_t>> ld = lit_dims(sm.binding);
        if (!ld || *ld != dims) { set_unsupported("array attribute shape mismatch"); return out; }
        const bool numeric = a.vk == AttrVK::Real || a.vk == AttrVK::Int;   // 强转分歧只在数值目标上
        if (!matrix && numeric && !split_leaves_uniform(sm.binding, src->scope, src->vprefix)) {
          set_unsupported("mixed-type split modifier literal"); return out;
        }
        p.mode = AttrPlan::Mode::Literal;
      } else {
        if (a.vk == AttrVK::StateSel || a.vk == AttrVK::EnumComp) { set_unsupported("array enumeration attribute"); return out; }
        const Id<Expr> t = simplify_expr(inst_exp(sm.binding, src->scope, src->vprefix));
        if (result_.unsupported) return out;
        if (expr_shape(t) != dims) { set_unsupported("array attribute shape mismatch"); return out; }
        p.mode = AttrPlan::Mode::Typed;
        p.value = t;
      }
      out.push_back(std::move(p));
    }
    return out;
  }

  // 按计划取元素 idx（1 基；标量组件为空）处的各属性值。
  std::vector<FlatVarAttr> attributes_at(const std::vector<AttrPlan>& plans, const std::vector<std::int64_t>& idx) {
    std::vector<FlatVarAttr> out;
    for (const AttrPlan& p : plans) {
      Id<Expr> v = p.value;
      if (p.mode == AttrPlan::Mode::Literal) {
        const Id<Exp> el = array_scalar_elem(p.raw, idx, 0);
        if (!el.is_valid()) { set_unsupported("array attribute element"); return out; }
        v = attr_value(p.spec->vk, el, p.scope, p.vprefix);
        if (!finish_attr(*p.spec, p.name, v, el, p.scope)) return out;
      } else if (p.mode == AttrPlan::Mode::Typed) {
        // 属性的数组值逐元素摊分；推导式值同样逐元素代入（S16 探针 `Real t[3](start = {i for i in 1:3})` → `t[2](start = 2.0)`）。
        // 数组 if 表达式作属性值：1.25.0 是内部错 "NFScalarize.scalarizeVariable failed"（W26 探针
        // `Real r2[3](start = if n == 2 then {1, 2, 3} else {4, 5, 6})`）→ 不复刻、也不出结果，SKIP。
        if (result_.model.exprs[p.value].kind == ExprKind::IfExp) { set_unsupported("if-expression array attribute"); return out; }
        const RedScalarize rs(red_scalarize_ok_);
        v = simplify_expr(scalarize(p.value, idx));
        if (result_.unsupported) return out;
        if (p.spec->vk == AttrVK::Real) v = to_real(v);
        if (!finish_attr(*p.spec, p.name, v, {}, p.scope)) return out;
      }
      out.push_back({p.name, v});
    }
    return out;
  }

  // 属性值的检查与归一（标量 / 逐元素共用）；raw = 该值的 Absyn（无则 invalid，fixed 只能是字面量）。失败 → unsupported。
  bool finish_attr(const AttrSpec& a, Symbol an, Id<Expr>& v, Id<Exp> raw, NodeId scope) {
    // 数组值给标量属性（`Real y(start = {1, 2})`）→ omc 维度不符报错。
    if (result_.model.exprs[v].ty.is_array() || result_.model.exprs[v].kind == ExprKind::Array) {
      set_unsupported("attribute type mismatch"); return false;
    }
    {   // 属性值类型须与属性类型相容（BuiltinAttribute5 `start = "fish"`、BuiltinAttribute6 `quantity = 1`）。
      Id<Expr> vin = v;   // 透过 attr_value 插的 Cast 看原值类型（`start = "fish"` 会被包成 /*Real*/("fish")）
      while (result_.model.exprs[vin].kind == ExprKind::Cast) vin = result_.model.exprs[vin].a;
      const TypeKind vk = result_.model.exprs[vin].ty.kind;
      auto num = [](TypeKind k) { return k == TypeKind::Integer || k == TypeKind::Real; };
      bool bad = false;
      switch (a.vk) {
        case AttrVK::Str: bad = vk != TypeKind::String; break;
        case AttrVK::Real: bad = !num(vk) && vk != TypeKind::Unknown; break;
        case AttrVK::Int: bad = vk != TypeKind::Integer && vk != TypeKind::Unknown; break;
        case AttrVK::Bool: bad = vk != TypeKind::Boolean && vk != TypeKind::Unknown; break;
        case AttrVK::StateSel: case AttrVK::EnumComp: break;
      }
      if (bad) { set_unsupported("attribute type mismatch"); return false; }
    }
    // `fixed` 必是参数/常量 Boolean，omc 求值成字面量（并把参数标 final）：ceval3 的 `fixed = init_x`→`true`。
    // 折不成字面量时对 fixed 做结构参数求值；仍失败 → SKIP（参数依赖变量等）。
    if (an == in_.intern("fixed") && !is_literal(v)) {
      std::optional<CVal> cv;
      const PhaseScope early(*this, MarkPhase::Early);   // fixed 里的参数在实例化期就标（NFInst.mo:3947，v1.25.0）
      const FrameAt frame_at(*this, scope);
      if (raw.is_valid()) cv = eval_scode(raw, scode_scope_of(scope), 0, true, nullptr);
      if (cv) v = literal_of(*cv);
      else { set_unsupported("parameter evaluation in attribute"); return false; }
    }
    // 内建属性都是参数变率（类型属性组件），绑定引用连续 / 离散变量 → omc HIGHER_VARIABILITY_BINDING（ScopeDeclaration2
    // `Real a(min = a)`）→ SKIP。
    if (refs_runtime_var(v)) { set_unsupported("attribute binding of higher variability"); return false; }
    return true;
  }

  // 数组推导式 `{e for i in …}`（Array 节点只有一个 Reduction 实参）。
  bool is_comprehension(Id<Exp> e) {
    const Exp& x = I_.absyn.exps[e];
    return x.kind == ExpKind::Array && x.args.size() == 1 && I_.absyn.exps[x.args[0]].kind == ExpKind::Reduction;
  }

  // 数组字面量（`{…}` / `array(…)` / 矩阵 `[…; …]`）的形状；兄弟须同形（矩形）。叶（非构造子）按标量计——cref 等的实际
  // 形状由调用方另查。矩阵元素须非构造子（`[{1, 2}; {3, 4}]` 是拼接，形状另算）。空 / 不规则 / 推导式 → nullopt。
  std::optional<std::vector<std::int64_t>> lit_dims(Id<Exp> e) {
    const Exp& x = I_.absyn.exps[e];
    if (x.kind == ExpKind::Matrix) {
      if (x.args.empty()) return std::nullopt;
      std::size_t cols = 0;
      for (std::size_t r = 0; r < x.args.size(); ++r) {
        const std::vector<Id<Exp>>& els = I_.absyn.exps[x.args[r]].args;
        if (els.empty() || (r > 0 && els.size() != cols)) return std::nullopt;
        cols = els.size();
        for (Id<Exp> el : els)
          if (is_array_ctor(el) || I_.absyn.exps[el].kind == ExpKind::Matrix) return std::nullopt;
      }
      return std::vector<std::int64_t>{static_cast<std::int64_t>(x.args.size()), static_cast<std::int64_t>(cols)};
    }
    if (!is_array_ctor(e)) return std::vector<std::int64_t>{};
    if (x.args.empty() || is_comprehension(e)) return std::nullopt;
    std::optional<std::vector<std::int64_t>> sub;
    for (Id<Exp> a : x.args) {
      std::optional<std::vector<std::int64_t>> d = lit_dims(a);
      if (!d || (sub && *sub != *d)) return std::nullopt;
      sub = std::move(d);
    }
    std::vector<std::int64_t> r{static_cast<std::int64_t>(x.args.size())};
    r.insert(r.end(), sub->begin(), sub->end());
    return r;
  }

  // 被切分的字面量数组修饰值：各叶须是同一类型的标量。omc 对切分绑定逐元素各自类型化、数组类型取**末元素**的类型
  //（typeSubscriptedExp2，NFTyping.mo:1655-1688），叶类型混杂时不统一强转——`start = {1, 2.0}` 打出 `start = 1`、
  // `p = {sin(1), 2}` 打出 `/*Real*/(0.84…)`（分歧账本「切分修饰的混型字面量」）。不复刻 → 混杂即 SKIP。
  bool split_leaves_uniform(Id<Exp> root, NodeId scope, const std::vector<Symbol>& prefix) {
    std::optional<TypeKind> k;
    return split_leaves_uniform(root, scope, prefix, k);
  }
  bool split_leaves_uniform(Id<Exp> e, NodeId scope, const std::vector<Symbol>& prefix, std::optional<TypeKind>& k) {
    if (is_array_ctor(e) || I_.absyn.exps[e].kind == ExpKind::Matrix) {
      const std::vector<Id<Exp>> args = I_.absyn.exps[e].args;   // 拷贝：inst 可能扩 absyn arena
      for (Id<Exp> a : args)
        if (!split_leaves_uniform(a, scope, prefix, k)) return false;
      return true;
    }
    const Id<Expr> t = inst_exp(e, scope, prefix);
    if (result_.unsupported) return false;
    const Expr& te = result_.model.exprs[t];
    if (te.ty.is_array() || te.kind == ExprKind::Array) return false;
    if (!k) k = te.ty.kind;
    return *k == te.ty.kind;
  }

  Id<Expr> attr_value(AttrVK vk, Id<Exp> val, NodeId enclosing, const std::vector<Symbol>& prefix) {
    switch (vk) {
      case AttrVK::StateSel:
      case AttrVK::EnumComp: return enum_lit_value(val, enclosing, prefix);   // `StateSelect.never` / `E.two`
      case AttrVK::Real: return to_real(simplify_expr(inst_exp(val, enclosing, prefix)));   // `min = -100` → `-100.0`
      case AttrVK::Str:
      case AttrVK::Int:
      case AttrVK::Bool: return simplify_expr(inst_exp(val, enclosing, prefix));
    }
    return inst_exp(val, enclosing, prefix);
  }

  // 枚举属性值形如 `T.lit`（StateSelect.never、E.two）：直接按 cref 各段建 EnumLit（R13 §4.2）。
  // 不走通用 cref 解析（它会把多段名当跨作用域引用标 unsupported）。prefix = scope 实例的组件路径。
  Id<Expr> enum_lit_value(Id<Exp> val, NodeId scope, const std::vector<Symbol>& prefix) {
    const Exp& e = I_.absyn.exps[val];
    if (e.kind != ExpKind::CrefExp) { set_unsupported("enum attribute value"); return lit_int(0); }
    return make_enum_lit(I_.absyn.crefs[e.cref], scope, prefix);
  }

  // 从 cref `E.lit` / `P.E.lit` 建 EnumLit：类型路径按 enum_type_path 规范化（`m.F.v`、`Pk.E.a`），补 1 基序数（关系 /
  // Integer() 折叠用；内建枚举无定义 → 留 0，打印不受影响）。类型解析不到 / 字面量不在枚举里 → SKIP。
  // 内建枚举 AssertionLevel = enumeration(warning, error)（NFBuiltin.mo:349-356 v1.25.0）的字面量：assert 的显式 level
  //（`assert(c, "m", AssertionLevel.warning)`）。不是它 → invalid。
  Id<Expr> assertion_level_lit(const Cref& c) {
    if (c.parts.size() != 2 || !c.parts[0].subscripts.empty() || !c.parts[1].subscripts.empty() ||
        in_.str(c.parts[0].name) != "AssertionLevel")
      return {};
    const std::string_view lit = in_.str(c.parts[1].name);
    if (lit != "warning" && lit != "error") return {};
    Expr r;
    r.kind = ExprKind::EnumLit;
    r.ty = Type::of(TypeKind::Enumeration);
    r.enum_path = {c.parts[0].name, c.parts[1].name};
    r.enum_index = lit == "warning" ? 1 : 2;
    return add(std::move(r));
  }
  Id<Expr> make_enum_lit(const Cref& c, NodeId scope, const std::vector<Symbol>& prefix) {
    for (const CrefPart& p : c.parts)
      if (!p.subscripts.empty()) { set_unsupported("subscripted enum value"); return lit_int(0); }
    // 不带类型前缀的字面量名（`start = two`）按普通名字查找（Enum10 → omc "Variable two not found"）→ 未做，SKIP。
    if (c.parts.size() < 2) { set_unsupported("unqualified enum literal"); return lit_int(0); }
    std::vector<Symbol> tparts;
    for (std::size_t i = 0; i + 1 < c.parts.size(); ++i) tparts.push_back(c.parts[i].name);
    const SClassDef* ed = nullptr;
    const std::optional<std::vector<Symbol>> tp = enum_type_path(tparts, scope, prefix, ed);
    if (!tp) { set_unsupported("enumeration type path"); return lit_int(0); }
    Expr r;
    r.kind = ExprKind::EnumLit;
    r.ty = Type::of(TypeKind::Enumeration);
    r.enum_path = *tp;
    r.enum_path.push_back(c.parts.back().name);
    if (ed) {
      const Symbol lit = c.parts.back().name;
      for (std::size_t i = 0; i < ed->enum_items.size(); ++i)
        if (ed->enum_items[i].literal == lit) { r.enum_index = static_cast<std::int64_t>(i) + 1; break; }
      if (r.enum_index == 0) { set_unsupported("unknown enumeration literal"); return lit_int(0); }
    }
    return add(std::move(r));
  }

  // 枚举类型的打印路径 = 枚举类的 InstNode.scopePath（makeEnumerationType，NFInst.mo:627-638；相对式 scopePath：根类不计、
  // 组件里声明的类带组件实例路径、包按类名逐段，NFInstNode.mo:1311-1381）。P10 / Pk 探针：根类里的 `F` → `F.u`，组件 m
  // 的类里的 `F` → `m.F.u`，包 Pk 里的 `E` → `Pk.E.a`，顶层 `E0` → `E0.a0`。type_parts = 书写的类型名各段；首段沿词法
  // 作用域链找：当前实例类（scope）→ 前缀 prefix；根类 → 空；顶层 program → 空（顶层类路径即书写名）；内建枚举 → 书写名。
  // 其余（更外层的非根实例、继承来的类、import 别名、短类别名）未核实 → nullopt。def 出参 = 枚举定义（内建为 null）。
  std::optional<std::vector<Symbol>> enum_type_path(const std::vector<Symbol>& type_parts, NodeId scope,
                                                    const std::vector<Symbol>& prefix, const SClassDef*& def) {
    def = nullptr;
    if (type_parts.empty()) return std::nullopt;
    auto class_in = [&](const std::vector<Id<SElement>>& elems, Symbol name) -> const SElement* {
      for (Id<SElement> eid : elems) {
        const SElement& x = I_.scode.elems[eid];
        if (x.kind == SElementKind::Class && x.name == name) return &x;
      }
      return nullptr;
    };
    const SElement* el = nullptr;
    std::vector<Symbol> path;
    if (fn_scope_ && (!scope.is_valid() || I_.nodes[scope].is_empty())) {
      // 函数体：按函数定义处的 SCODE 作用域链（内→外）找；路径 = 根类以内、到声明它的类为止的各类名（外→内）。
      const ScodeScope& sc = *fn_scope_;
      const std::uint32_t root_def = I_.nodes[root_].def.raw;
      for (std::size_t k = 0; k < sc.size(); ++k) {
        if (!(el = class_in(I_.scode.elems[sc[k]].def.elements, type_parts[0]))) continue;
        std::size_t r = sc.size();
        for (std::size_t j = k; j < sc.size(); ++j)
          if (sc[j].raw == root_def) { r = j; break; }
        for (std::size_t j = r; j > k; --j) path.push_back(I_.scode.elems[sc[j - 1]].name);
        break;
      }
    }
    for (NodeId s = scope; !el && s.is_valid() && !I_.nodes[s].is_empty(); s = I_.nodes[s].parent_scope) {
      const Id<SElement> d = I_.nodes[s].def;
      if (!d.is_valid() || !(el = class_in(I_.scode.elems[d].def.elements, type_parts[0]))) continue;
      // 实例节点是定义节点的克隆、parent_scope 指词法外层的**定义**节点（inst.cpp instantiate），故根类按 def 认。
      if (s.raw == scope.raw) path = prefix;
      else if (I_.nodes[s].def.raw != I_.nodes[root_].def.raw) return std::nullopt;   // 更外层的非根类：路径未核实
      break;
    }
    if (!el) el = class_in(I_.scode.program, type_parts[0]);
    if (!el) {
      const std::string_view n = in_.str(type_parts[0]);
      if (type_parts.size() == 1 && (n == "StateSelect" || n == "AssertionLevel" || n == "Uncertainty")) return type_parts;
      return std::nullopt;
    }
    for (std::size_t i = 1; i < type_parts.size(); ++i)
      if (!(el = class_in(el->def.elements, type_parts[i]))) return std::nullopt;
    if (el->def.kind != SClassDefKind::Enumeration) return std::nullopt;
    def = &el->def;
    path.insert(path.end(), type_parts.begin(), type_parts.end());
    return path;
  }
  // 按简单名沿词法作用域链找类元素（任意 kind），最后再查顶层 program（顶层声明的连接器短类，Cardinality3）。
  // 继承/导入来的类不在链上 → nullptr（调用方 SKIP）。
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
  std::optional<std::vector<Id<Expr>>> classify_type_dim(Id<Exp> d, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const Exp& e = I_.absyn.exps[d];
    if (e.kind != ExpKind::CrefExp) return std::nullopt;
    const Cref& c = I_.absyn.crefs[e.cref];
    std::vector<Symbol> tparts;
    for (const CrefPart& p : c.parts) {
      if (!p.subscripts.empty()) return std::nullopt;
      tparts.push_back(p.name);
    }
    return type_labels(tparts, enclosing, prefix);
  }
  // 类型维数组（`Real y[E]`、`y[Boolean]`）的元素下标在压平期一律按**序数**走（IntLit：false 1、true 2，枚举字面量 = 序号），
  // 收尾由 relabel_type_dims 统一换回标签——下标的生成点（标量化、切片、展开）都不用知道维的种类。
  // 下标 / 取值的序数：整数字面量 → 值；Boolean / 枚举字面量 → 序数；其余（符号下标）→ nullopt。
  std::optional<std::int64_t> sub_ordinal(Id<Expr> sx) const {
    const Expr& e = result_.model.exprs[sx];
    if (e.kind == ExprKind::IntLit) return e.ival;
    if (e.kind == ExprKind::BoolLit) return e.bval ? 2 : 1;
    if (e.kind == ExprKind::EnumLit && e.enum_index > 0) return e.enum_index;
    return std::nullopt;
  }
  // 两个标签属同一个类型（都是 Boolean，或同一个枚举类型）。
  bool same_label_type(Id<Expr> a, Id<Expr> b) const {
    const Expr& x = result_.model.exprs[a];
    const Expr& y = result_.model.exprs[b];
    if (x.kind != y.kind) return false;
    if (x.kind == ExprKind::BoolLit) return true;
    return x.kind == ExprKind::EnumLit && !x.enum_path.empty() && x.enum_path.size() == y.enum_path.size() &&
           std::equal(x.enum_path.begin(), x.enum_path.end() - 1, y.enum_path.begin());
  }
  // 类型化的值 se 是标签表 labels 那个类型的字面量 → 它的序数；不是字面量 / 类型不符 → nullopt。
  std::optional<std::int64_t> label_ordinal(Id<Expr> se, const std::vector<Id<Expr>>& labels) const {
    if (labels.empty() || !same_label_type(se, labels[0])) return std::nullopt;
    const std::optional<std::int64_t> o = sub_ordinal(se);
    if (!o || *o < 1 || static_cast<std::size_t>(*o) > labels.size()) return std::nullopt;
    return o;
  }
  // 书写式 x 是 Boolean / 枚举的字面量（`true`、`E.two`、`P.E.two`，可带括号）→ 它的序数，all = 该类型的全部标签。
  // 不是 → nullopt（不置 unsupported）。
  std::optional<std::int64_t> absyn_label(Id<Exp> x, NodeId enclosing, const std::vector<Symbol>& prefix,
                                          std::vector<Id<Expr>>& all) {
    while (I_.absyn.exps[x].kind == ExpKind::Tuple && I_.absyn.exps[x].args.size() == 1) x = I_.absyn.exps[x].args[0];
    const Exp& e = I_.absyn.exps[x];
    if (e.kind == ExpKind::BoolLit) {
      all = {lit_bool(false), lit_bool(true)};
      return e.bval ? 2 : 1;
    }
    if (e.kind != ExpKind::CrefExp) return std::nullopt;
    const Cref c = I_.absyn.crefs[e.cref];   // 拷贝
    if (c.parts.size() < 2) return std::nullopt;
    std::vector<Symbol> tparts;
    for (const CrefPart& cp : c.parts) {
      if (!cp.subscripts.empty()) return std::nullopt;
      tparts.push_back(cp.name);
    }
    const Symbol lit = tparts.back();
    tparts.pop_back();
    std::optional<std::vector<Id<Expr>>> tl = type_labels(tparts, enclosing, prefix);
    if (!tl) return std::nullopt;
    for (std::size_t i = 0; i < tl->size(); ++i) {
      const Expr& le = result_.model.exprs[(*tl)[i]];
      if (le.kind == ExprKind::EnumLit && le.enum_path.back() == lit) {
        all = std::move(*tl);
        return static_cast<std::int64_t>(i) + 1;
      }
    }
    return std::nullopt;
  }
  // for 方程 / 归约的范围取值。类型名（`for i in E`、`for b in Boolean`）→ 全部标签；标签范围 `E.a:E.c`、`false:true`
  // 与标签数组 `{E.one, E.three}` → 对应的标签；其余按整数范围（eval_range）。labels 非空 = 迭代变量是该类型的值（与 ords
  // 平行）。求不出 → nullopt。EN 探针 t1：for 方程展开成 `y[E.one] = …`、`x[i] = i` → `x[E.one] = E.one`。
  struct IterVals {
    std::vector<std::int64_t> ords;
    std::vector<Id<Expr>> labels;
    bool type_name = false;   // 范围写的是类型名（`for i in E`）：由它定的数组维是类型维（标签范围 / 数组是整数维）
    Id<Expr> type_label;   // 标签范围：该类型的任一标签（类型核对用；空范围也有）；整数范围 → invalid
    Id<Expr> kept;         // 标签范围的保留形态（for 语句 / 保留的归约、推导式里打印的范围式）
  };
  // 标签（Boolean / 枚举）的元素类型。
  Type label_type(Id<Expr> label) const {
    return result_.model.exprs[label].kind == ExprKind::BoolLit ? Type::boolean() : Type::of(TypeKind::Enumeration);
  }
  // 范围是不是 Boolean / 枚举上的：类型名、标签范围、标签数组。是 → 取值 + 保留形态（EN 探针 t1 / IR 探针 e1：类型名打成
  // 标签数组 `{E.one, E.two, E.three}`、`{false, true}`，范围照写 `E.one:E.three`）；不是（或两界 / 各元素类型不一）→ nullopt。
  std::optional<IterVals> label_iter_values(Id<Exp> range, NodeId enclosing, const std::vector<Symbol>& prefix) {
    IterVals r;
    Id<Exp> rg = range;
    while (I_.absyn.exps[rg].kind == ExpKind::Tuple && I_.absyn.exps[rg].args.size() == 1) rg = I_.absyn.exps[rg].args[0];
    const Exp rx = I_.absyn.exps[rg];   // 拷贝
    auto as_array = [&]() {
      Dimension d;
      d.kind = DimKind::Integer;
      d.size = static_cast<std::int64_t>(r.labels.size());
      Expr arr;
      arr.kind = ExprKind::Array;
      arr.ty = Type::array_of(label_type(r.type_label), {d});
      arr.args = r.labels;
      r.kept = add(std::move(arr));
    };
    if (rx.kind == ExpKind::CrefExp) {
      std::optional<std::vector<Id<Expr>>> tl = classify_type_dim(rg, enclosing, prefix);
      if (!tl || tl->empty()) return std::nullopt;
      r.labels = std::move(*tl);
      for (std::size_t i = 1; i <= r.labels.size(); ++i) r.ords.push_back(static_cast<std::int64_t>(i));
      r.type_label = r.labels[0];
      r.type_name = true;
      as_array();
      return r;
    }
    if (rx.kind == ExpKind::Range && !rx.c.is_valid()) {
      std::vector<Id<Expr>> all, all2;
      const std::optional<std::int64_t> lo = absyn_label(rx.a, enclosing, prefix, all);
      if (!lo) return std::nullopt;
      const std::optional<std::int64_t> hi = absyn_label(rx.b, enclosing, prefix, all2);
      if (!hi || !same_label_type(all[0], all2[0])) return std::nullopt;   // 两界类型不同：omc 类型错
      for (std::int64_t v = *lo; v <= *hi; ++v) {
        r.ords.push_back(v);
        r.labels.push_back(all[static_cast<std::size_t>(v) - 1]);
      }
      r.type_label = all[0];
      Expr rg2;
      rg2.kind = ExprKind::Range;
      rg2.ty = label_type(r.type_label);
      rg2.a = all[static_cast<std::size_t>(*lo) - 1];
      rg2.b = all[static_cast<std::size_t>(*hi) - 1];
      r.kept = add(std::move(rg2));
      return r;
    }
    if (rx.kind == ExpKind::Array && !rx.args.empty() && I_.absyn.exps[rx.args[0]].kind != ExpKind::Reduction) {
      std::vector<Id<Expr>> all;
      if (!absyn_label(rx.args[0], enclosing, prefix, all)) return std::nullopt;
      for (Id<Exp> a : rx.args) {
        std::vector<Id<Expr>> all2;
        const std::optional<std::int64_t> v = absyn_label(a, enclosing, prefix, all2);
        if (!v || !same_label_type(all[0], all2[0])) return std::nullopt;
        r.ords.push_back(*v);
        r.labels.push_back(all[static_cast<std::size_t>(*v) - 1]);
      }
      r.type_label = all[0];
      as_array();
      return r;
    }
    return std::nullopt;
  }
  std::optional<IterVals> iter_values(Id<Exp> range, NodeId enclosing, const std::vector<Symbol>& prefix) {
    if (std::optional<IterVals> lv = label_iter_values(range, enclosing, prefix)) return lv;
    if (result_.unsupported) return std::nullopt;
    IterVals r;
    std::optional<std::vector<std::int64_t>> seq = eval_range(range, scode_scope_of(enclosing), 0);
    if (!seq) return std::nullopt;
    r.ords = std::move(*seq);
    return r;
  }
  // 下标恰是保留着的迭代变量（for 语句 / 归约 / 推导式）、且它取 Boolean / 枚举值 → 该类型的一个标签；否则 invalid。
  Id<Expr> stmt_iter_label(Id<Exp> sx) const {
    const Exp& x = I_.absyn.exps[sx];
    if (x.kind != ExpKind::CrefExp) return {};
    const Cref& c = I_.absyn.crefs[x.cref];
    if (c.leading_dot || c.parts.size() != 1 || !c.parts[0].subscripts.empty() || !stmt_iters_.count(c.parts[0].name.id)) return {};
    const auto it = stmt_iter_labels_.find(c.parts[0].name.id);
    return it != stmt_iter_labels_.end() ? it->second : Id<Expr>{};
  }
  // 类型名（`Boolean`、枚举类型的路径）→ 它的各个取值的标签；不是类型名 → nullopt。
  std::optional<std::vector<Id<Expr>>> type_labels(const std::vector<Symbol>& tparts, NodeId enclosing,
                                                   const std::vector<Symbol>& prefix) {
    const Symbol nm = tparts.back();
    if (tparts.size() == 1 && in_.str(nm) == "Boolean") {
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
    // 枚举类型维：本地 / 包 / 顶层 `type E = enumeration(…)`（下标打规范化路径，enum_type_path），或 builtin StateSelect
    //（MLS §3.8.3，固定 5 值）。
    std::vector<Symbol> lits;
    std::vector<Symbol> tpath;
    const SClassDef* ed = nullptr;
    if (tparts.size() == 1 && in_.str(nm) == "StateSelect") {
      for (const char* s : {"never", "avoid", "default", "prefer", "always"}) lits.push_back(in_.intern(s));
      tpath = tparts;
    } else if (std::optional<std::vector<Symbol>> tp = enum_type_path(tparts, enclosing, prefix, ed); tp && ed) {
      for (const auto& it : ed->enum_items) lits.push_back(it.literal);
      tpath = std::move(*tp);
    } else {
      return std::nullopt;   // 不是类型名（普通整数维参数等）→ 走 eval_dim
    }
    std::vector<Id<Expr>> labels;
    for (std::size_t i = 0; i < lits.size(); ++i) {
      Expr r;
      r.kind = ExprKind::EnumLit;
      r.ty = Type::of(TypeKind::Enumeration);
      r.enum_path = tpath;
      r.enum_path.push_back(lits[i]);
      r.enum_index = static_cast<std::int64_t>(i) + 1;
      labels.push_back(add(std::move(r)));
    }
    return labels;
  }

  // ---- package 限定常量：SCODE 作用域链解析 + 值语义常量求值 ----
  // package 常量往往没被实例化（没走 inst 树），所以直接在 SCODE 上按名解析 + 递归求值，不经 inst_exp/cref。
  struct CVal {
    // Undef：函数里未赋值的数组输出 / 局部元素（NFEvalFunction.buildBinding :342-365 用 EMPTY 占位）；读到即求值失败。
    enum class K { Int, Real, Bool, Str, Record, Array, Undef } k = K::Int;
    std::int64_t i = 0;
    double r = 0.0;
    bool b = false;
    Symbol s;
    // Record：字段名→值（保序）。用 shared_ptr 保持 CVal 拷贝廉价；set_field 写时复制避免别名污染。
    std::shared_ptr<std::vector<std::pair<Symbol, CVal>>> rec;
    std::shared_ptr<std::vector<CVal>> arr;   // Array：元素（可嵌套）
    std::vector<std::int64_t> eshape;         // 空数组的完整形状（`ones(0, 3)` → {0, 3}）；空 = {0}。非空数组不用
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

  // enclosing 实例的词法作用域链 → 类 SElement 列表（内→外，<top>/无 def 跳过）。函数体发射期（enclosing 为空节点）：
  // 名字按函数定义处作用域解析（fn_scope_）。
  ScodeScope scode_scope_of(NodeId enclosing) {
    if (fn_scope_ && (!enclosing.is_valid() || I_.nodes[enclosing].is_empty())) return *fn_scope_;
    ScodeScope s;
    for (NodeId n = enclosing; n.is_valid() && !I_.nodes[n].is_empty(); n = I_.nodes[n].parent_scope) {
      const Id<SElement> d = I_.nodes[n].def;
      if (d.is_valid() && I_.scode.elems[d].kind == SElementKind::Class) s.push_back(d);
    }
    return s;
  }

  // 在作用域链（内→外）+ 顶层 program 里找名为 name 的类或组件元素（内层优先 → 支持遮蔽）；每层本地元素之后查该层的
  // import（import_lookup）。
  Id<SElement> find_scode_element(Symbol name, const ScodeScope& scope) {
    ScodeScope unused;
    return find_scode_element_at(name, scope, unused);
  }

  // 顶层类路径 `A.B.c`（import 目标永远从顶层查，NFLookup.lookupImport :276-287）：首段在 program 里找类，其后逐段下降（中间段
  // 须是类，末段可是类或组件）。def_scope = 末段的定义作用域链（内→外）。找不到 → invalid。
  Id<SElement> resolve_top_path(const std::vector<Symbol>& path, ScodeScope& def_scope) {
    def_scope.clear();
    if (path.empty()) return {};
    Id<SElement> e;
    for (Id<SElement> x : I_.scode.program)
      if (I_.scode.elems[x].name == path[0] && I_.scode.elems[x].kind == SElementKind::Class) { e = x; break; }
    for (std::size_t k = 1; k < path.size() && e.is_valid(); ++k) {
      if (I_.scode.elems[e].kind != SElementKind::Class) return {};
      Id<SElement> nxt;
      for (Id<SElement> c : I_.scode.elems[e].def.elements) {
        const SElement& ce = I_.scode.elems[c];
        if (ce.name == path[k] && (ce.kind == SElementKind::Class || ce.kind == SElementKind::Component)) { nxt = c; break; }
      }
      if (!nxt.is_valid()) return {};
      def_scope.insert(def_scope.begin(), e);
      e = nxt;
    }
    return e;
  }
  // 类 cls 自己的 import 按名 name 解析（MLS §13.2；NFImport / NFClassTree，R12 §5）：限定 / 重命名 / 组导入优先于 `.*`
  //（addImportConflict，NFClassTree.mo:1757-1800）；同一档里两个同名项 = 冲突，用到才报错 → SKIP；名字命中但目标解析不到
  //（含内建函数别名 `import sinx = sin`）→ omc LOOKUP_IMPORT_ERROR / 未做 → SKIP。def_scope = 目标的定义作用域链。
  // import 不被继承（只看 cls 自己的元素）。
  Id<SElement> import_lookup(Symbol name, Id<SElement> cls, ScodeScope& def_scope) {
    Id<SElement> found;
    ScodeScope fds;
    // 同一档里第二个同名项即冲突——不论目标是否相同、解析得到与否（addImportConflict 的 (UNRESOLVED, UNRESOLVED) /
    // (RESOLVED, RESOLVED)，NFClassTree.mo:1757-1800；用到才报错）。
    int matches = 0;
    bool unresolved = false;
    auto take = [&](Id<SElement> t, ScodeScope ds) {
      found = t;
      fds = std::move(ds);
    };
    for (Id<SElement> eid : I_.scode.elems[cls].def.elements) {   // 限定 / 重命名 / 组导入
      const SElement& el = I_.scode.elems[eid];
      if (el.kind != SElementKind::Import || el.imp.wildcard) continue;
      std::vector<std::vector<Symbol>> targets;
      if (!el.imp.multi.empty()) {
        for (std::size_t k = 0; k < el.imp.multi.size(); ++k) {
          const bool aliased = k < el.imp.multi_alias.size() && el.imp.multi_alias[k].is_valid();
          if ((aliased ? el.imp.multi_alias[k] : el.imp.multi[k]) != name) continue;
          std::vector<Symbol> p = el.imp.path;
          p.push_back(el.imp.multi[k]);
          targets.push_back(std::move(p));
        }
      } else if (el.imp.alias.is_valid() ? el.imp.alias == name : (!el.imp.path.empty() && el.imp.path.back() == name)) {
        targets.push_back(el.imp.path);
      }
      for (const std::vector<Symbol>& p : targets) {
        ++matches;
        ScodeScope ds;
        const Id<SElement> t = resolve_top_path(p, ds);
        if (t.is_valid()) take(t, std::move(ds));
        else unresolved = true;
      }
    }
    if (matches == 0) {
      for (Id<SElement> eid : I_.scode.elems[cls].def.elements) {   // `.*`：包里的全部类与组件（不滤 protected，R12 p40）
        const SElement& el = I_.scode.elems[eid];
        if (el.kind != SElementKind::Import || !el.imp.wildcard) continue;
        ScodeScope ds;
        const Id<SElement> pkg = resolve_top_path(el.imp.path, ds);
        if (!pkg.is_valid() || I_.scode.elems[pkg].kind != SElementKind::Class) continue;   // flatten_class 已拒
        for (Id<SElement> c : I_.scode.elems[pkg].def.elements) {
          const SElement& ce = I_.scode.elems[c];
          if (ce.name != name || (ce.kind != SElementKind::Class && ce.kind != SElementKind::Component)) continue;
          ScodeScope cds = ds;
          cds.insert(cds.begin(), pkg);
          ++matches;
          take(c, std::move(cds));
          break;
        }
      }
    }
    if (matches >= 2) { set_unsupported("conflicting imports"); return {}; }
    if (unresolved) { set_unsupported("unresolved import"); return {}; }
    if (found.is_valid()) def_scope = std::move(fds);
    return found;
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
    // 首段命中处的定义作用域链（经 import 命中 = 目标包的链）：被引常量的绑定在其声明处的词法作用域里求值。
    ScodeScope cur;
    Id<SElement> el = find_scode_element_at(parts[0], leading_dot ? ScodeScope{} : scope, cur);
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

  // 限定名常量路径的值（NFCeval.evalCref 沿路径取值）：`P.cr.r`、`P.cr[1].r1.r`、`.P.x[2]`。前几段按类下降，遇到的第一个
  // 组件须是 constant（allow_param 时也接 parameter），取其值（const_comp_value），余下各段按下标 / 记录字段取（cval_path，
  // 下标在书写处作用域求值）。resolve_qual_const 只接"中间段全是类、无下标"，本函数补其余形态（RecordConstant1-3、
  // PackageConstant3/4）。不适用 → nullopt。
  std::optional<CVal> eval_qual_path(const Cref& c, const ScodeScope& scope, int depth, bool allow_param,
                                     std::unordered_set<std::uint32_t>* used) {
    if (c.parts.empty() || depth > 100) return std::nullopt;
    ScodeScope cur;
    Id<SElement> el = find_scode_element_at(c.parts[0].name, c.leading_dot ? ScodeScope{} : scope, cur);
    std::size_t k = 0;
    while (el.is_valid() && I_.scode.elems[el].kind == SElementKind::Class) {
      if (!c.parts[k].subscripts.empty() || ++k >= c.parts.size()) return std::nullopt;
      cur.insert(cur.begin(), el);
      Id<SElement> nxt;
      for (Id<SElement> e : I_.scode.elems[el].def.elements) {
        const SElement& ce = I_.scode.elems[e];
        if (ce.name == c.parts[k].name && (ce.kind == SElementKind::Class || ce.kind == SElementKind::Component)) { nxt = e; break; }
      }
      el = nxt;
    }
    if (!el.is_valid() || I_.scode.elems[el].kind != SElementKind::Component) return std::nullopt;
    const SVariability v = I_.scode.elems[el].attr.var;
    if (v != SVariability::Const && !(allow_param && v == SVariability::Param)) return std::nullopt;
    if (used && v == SVariability::Param) used->insert(el.raw);
    const std::optional<CVal> root = const_comp_value(el, cur, depth);
    if (!root) return std::nullopt;
    Cref rest;
    rest.parts.assign(c.parts.begin() + static_cast<std::ptrdiff_t>(k), c.parts.end());
    return cval_path(*root, rest, scope, depth);
  }
  // 包 / 类里 constant 组件的值（被引常量的声明在其自身作用域求值：书写处前缀 / 实例帧不适用）：有绑定 → 求值并按声明
  // 类型强转；无绑定的记录 / 记录数组 → 组件修饰 + 记录字段缺省逐字段构造（evalComponentBinding 对无绑定记录按字段绑定
  // 组记录；非 each 的数组修饰逐元素取第 i 个，each 原样）。其余 → nullopt。
  std::optional<CVal> const_comp_value(Id<SElement> el, const ScodeScope& cscope, int depth) {
    const SElement ce = I_.scode.elems[el];   // 拷贝（求值可能扩 arena）
    const std::vector<Symbol>* saved = ceval_prefix_;
    const int saved_frame = ceval_frame_;
    ceval_prefix_ = nullptr;
    ceval_frame_ = -1;
    std::optional<CVal> out;
    if (ce.mod.is_valid() && I_.scode.mods[ce.mod].binding.is_valid()) {
      out = eval_scode(I_.scode.mods[ce.mod].binding, cscope, depth + 1);
      if (out) out = coerce_cval(*out, ce.type.path);
    } else {
      ScodeScope rds;
      const Id<SElement> rec = resolve_class_path(ce.type.path, ce.type.leading_dot, cscope, rds);
      if (rec.is_valid() && I_.scode.elems[rec].kind == SElementKind::Class &&
          I_.scode.elems[rec].restriction == SRestriction::Record && I_.scode.elems[rec].def.kind == SClassDefKind::Parts) {
        std::vector<SSubMod> subs;
        if (ce.mod.is_valid()) subs = I_.scode.mods[ce.mod].subs;
        std::vector<std::int64_t> dims;
        bool dims_ok = true;
        for (const std::vector<Id<Exp>>* ds : {&ce.attr.dims, &ce.type.dims})
          for (Id<Exp> d : *ds) if (!eval_dim(d, dims, cscope)) dims_ok = false;
        if (dims_ok && dims.empty()) {
          out = record_value(rec, rds, subs, cscope, depth);
        } else if (dims_ok && dims.size() == 1 && dims[0] >= 1) {
          std::vector<CVal> els;
          for (std::int64_t i = 1; i <= dims[0] && out.has_value() == false; ++i) {
            std::vector<SSubMod> esubs;   // 该元素的修饰：each 原样，非 each 的绑定取第 i 个元素
            bool ok = true;
            for (const SSubMod& s : subs) {
              SMod sm = I_.scode.mods[s.mod];
              if (!sm.is_each && sm.binding.is_valid()) {
                sm.binding = array_scalar_elem(sm.binding, {i}, 0);
                if (!sm.binding.is_valid()) { ok = false; break; }
              } else if (!sm.is_each && !sm.subs.empty()) {
                ok = false; break;   // 非 each 的嵌套修饰逐元素切分未做
              }
              esubs.push_back({s.ident, synth_mod(std::move(sm))});
            }
            std::optional<CVal> ev = ok ? record_value(rec, rds, esubs, cscope, depth) : std::nullopt;
            if (!ev) break;
            els.push_back(std::move(*ev));
          }
          if (static_cast<std::int64_t>(els.size()) == dims[0]) out = CVal::Arr(std::move(els));
        }
      }
    }
    ceval_prefix_ = saved;
    ceval_frame_ = saved_frame;
    return out;
  }
  // 记录值：逐字段——外来修饰（subs，在 mscope 求值）优先：带绑定取绑定、只带嵌套修饰的记录字段递归；否则字段声明绑定
  // （在记录作用域求值）；否则记录字段按其缺省递归构造。字段数组 / extends / 求不出 → nullopt。rds = 记录类的定义作用域链。
  std::optional<CVal> record_value(Id<SElement> rec, const ScodeScope& rds, const std::vector<SSubMod>& subs,
                                   const ScodeScope& mscope, int depth) {
    if (depth > 50) return std::nullopt;
    ScodeScope fscope = rds;
    fscope.insert(fscope.begin(), rec);
    CVal r = CVal::Rec();
    const std::vector<Id<SElement>> fields = I_.scode.elems[rec].def.elements;   // 拷贝
    for (Id<SElement> f : fields) {
      const SElement fe = I_.scode.elems[f];
      if (fe.kind == SElementKind::Extends) return std::nullopt;
      if (fe.kind != SElementKind::Component) continue;
      if (!fe.attr.dims.empty() || !fe.type.dims.empty()) return std::nullopt;
      const SSubMod* hit = nullptr;
      for (const SSubMod& s : subs) if (s.ident == fe.name) { hit = &s; break; }
      std::optional<CVal> fv;
      if (hit && I_.scode.mods[hit->mod].binding.is_valid()) {
        fv = eval_scode(I_.scode.mods[hit->mod].binding, mscope, depth + 1);
      } else if (fe.mod.is_valid() && I_.scode.mods[fe.mod].binding.is_valid() && !hit) {
        fv = eval_scode(I_.scode.mods[fe.mod].binding, fscope, depth + 1);
      } else {
        ScodeScope frds;
        const Id<SElement> frec = resolve_class_path(fe.type.path, fe.type.leading_dot, fscope, frds);
        if (!frec.is_valid() || I_.scode.elems[frec].kind != SElementKind::Class ||
            I_.scode.elems[frec].restriction != SRestriction::Record || I_.scode.elems[frec].def.kind != SClassDefKind::Parts)
          return std::nullopt;
        std::vector<SSubMod> nsubs;
        if (hit) nsubs = I_.scode.mods[hit->mod].subs;
        fv = record_value(frec, frds, nsubs, hit ? mscope : fscope, depth + 1);
      }
      if (!fv) return std::nullopt;
      fv = coerce_cval(*fv, fe.type.path);
      if (!fv) return std::nullopt;
      r.set_field(fe.name, std::move(*fv));
    }
    return r;
  }

  // 在 SCODE 作用域链上求值常量表达式（字面量 / 限定常量引用 / 算术 / 逻辑 / 关系）。
  // allow_param：也内联 parameter（结构上下文如 `fixed=`）；used 非空时记下用到的 parameter 组件（→ 标 final）。
  // 顶层的带参数求值是一笔"事务"：求不出值就把途中打的结构标记撤掉。omc 的 Structural.markExp 只在式子确实被当结构式用的
  // 地方调用；ymc 的标记是求值的副作用，求值失败后调用方落回非结构路径，标记不该留下（ProbeTimeParam：if 方程的运行时条件
  // `time > k` 求不出值，此前 k 仍被标成 final、代入成 `time > 5.0`，omc 原样）。快照在首次改动标记状态时才拍（txn_touch）。
  std::optional<CVal> eval_scode(Id<Exp> exp, const ScodeScope& scope, int depth,
                                 bool allow_param = false, std::unordered_set<std::uint32_t>* used = nullptr) {
    if (depth != 0 || !allow_param || ceval_frame_ < 0) return eval_scode_impl(exp, scope, depth, allow_param, used);
    MarkTxn txn;
    txn.outer = txn_;
    txn_ = &txn;
    std::optional<CVal> v = eval_scode_impl(exp, scope, depth, allow_param, used);
    txn_ = txn.outer;
    if (!v && txn.snap) struct_restore(std::move(*txn.snap));
    return v;
  }
  std::optional<CVal> eval_scode_impl(Id<Exp> exp, const ScodeScope& scope, int depth, bool allow_param,
                                      std::unordered_set<std::uint32_t>* used) {
    if (depth > 100) return std::nullopt;
    const Exp& e = I_.absyn.exps[exp];
    // `array(…)` 是数组构造子的调用写法（MLS §10.4.1；元素 / 推导式同在 .args）：按 `{…}` 求值。
    switch (is_array_ctor(exp) ? ExpKind::Array : e.kind) {
      case ExpKind::IntLit: return CVal::I(e.ival);
      case ExpKind::RealLit: return CVal::R(e.rval);
      case ExpKind::BoolLit: return CVal::B(e.bval);
      case ExpKind::StringLit: return CVal::S(in_.intern(unescape_modelica_string(in_.str(e.sval))));
      case ExpKind::End: return eval_end_ ? std::optional<CVal>(CVal::I(*eval_end_)) : std::nullopt;
      case ExpKind::Tuple:
        return e.args.size() == 1 ? eval_scode(e.args[0], scope, depth + 1, allow_param, used) : std::nullopt;
      case ExpKind::Array: {   // 数组字面量 → CVal::Array（元素逐个求值，可嵌套）
        // 数组推导式 `{e for i in r, j in s}`（解析成单个 Reduction 实参）：MLS §10.4.2.1 等价于 {{e for i in r} for j in s}
        // ——最右的迭代器是最外维（evalArrayConstructor2 :3292-3330 按迭代表逐层求，CevalArrayConstructor2 实证）。
        if (e.args.size() == 1 && I_.absyn.exps[e.args[0]].kind == ExpKind::Reduction) {
          const Exp& red = I_.absyn.exps[e.args[0]];
          auto level = [&](auto& self, std::size_t k) -> std::optional<CVal> {   // k = 尚未绑定的迭代器个数
            if (k == 0) return eval_scode(red.a, scope, depth + 1, allow_param, used);
            const ForIndex& it = red.iters[k - 1];
            if (!it.range.is_valid()) return std::nullopt;   // 隐式范围未做
            std::optional<CVal> rv = eval_scode(it.range, scope, depth + 1, allow_param, used);
            if (!rv || !is_arr(*rv)) return std::nullopt;
            std::vector<CVal> out;
            for (const CVal& x : *rv->arr) {
              citers_.push_back({it.name.id, x});
              std::optional<CVal> ev = self(self, k - 1);
              citers_.pop_back();
              if (!ev) return std::nullopt;
              out.push_back(std::move(*ev));
            }
            return out.empty() ? empty_arr({0}) : CVal::Arr(std::move(out));
          };
          return level(level, red.iters.size());
        }
        std::vector<CVal> els;
        els.reserve(e.args.size());
        for (Id<Exp> a : e.args) {
          std::optional<CVal> v = eval_scode(a, scope, depth + 1, allow_param, used);
          if (!v) return std::nullopt;
          els.push_back(std::move(*v));
        }
        return CVal::Arr(std::move(els));
      }
      case ExpKind::Matrix: {
        // 矩阵构造 `[a, b; c, d]` 的值（同 matrix_exp：元素提升到 2 维——标量 1×1、向量 n×1、矩阵不变，行内按列、行间按行
        // 拼接；Integer 与 Real 混排取 Real）。ND 探针 d1：`Real y[max([n, m])]`、`constant Integer M[2, 2] = [1, 2; 3, 4]`。
        std::vector<std::vector<CVal>> all;   // 行 × 列
        for (Id<Exp> rid : e.args) {
          std::vector<std::vector<CVal>> block;
          for (Id<Exp> a : I_.absyn.exps[rid].args) {
            std::optional<CVal> v = eval_scode(a, scope, depth + 1, allow_param, used);
            if (!v) return std::nullopt;
            const std::vector<std::int64_t> sh = cval_shape(*v);
            std::vector<std::vector<CVal>> g;
            if (sh.empty()) {
              g.push_back({*v});
            } else if (sh.size() == 1 && sh[0] > 0) {
              for (const CVal& x : *v->arr) g.push_back({x});
            } else if (sh.size() == 2 && sh[0] > 0 && sh[1] > 0) {
              for (const CVal& r : *v->arr) g.push_back(*r.arr);
            } else {
              return std::nullopt;
            }
            if (block.empty()) { block = std::move(g); continue; }
            if (g.size() != block.size()) return std::nullopt;
            for (std::size_t i = 0; i < g.size(); ++i) block[i].insert(block[i].end(), g[i].begin(), g[i].end());
          }
          if (block.empty() || (!all.empty() && block[0].size() != all[0].size())) return std::nullopt;
          for (std::vector<CVal>& r : block) all.push_back(std::move(r));
        }
        if (all.empty()) return std::nullopt;
        bool any_real = false;
        const CVal::K k0 = all[0][0].k;
        for (const std::vector<CVal>& r : all)
          for (const CVal& x : r) {
            const bool num = x.k == CVal::K::Int || x.k == CVal::K::Real;
            if (!(num && (k0 == CVal::K::Int || k0 == CVal::K::Real)) && (x.k != k0 || (k0 != CVal::K::Bool && k0 != CVal::K::Str)))
              return std::nullopt;
            any_real = any_real || x.k == CVal::K::Real;
          }
        std::vector<CVal> rows;
        for (std::vector<CVal>& r : all) {
          if (any_real)
            for (CVal& x : r) if (x.k == CVal::K::Int) x = CVal::R(static_cast<double>(x.i));
          rows.push_back(CVal::Arr(std::move(r)));
        }
        return CVal::Arr(std::move(rows));
      }
      case ExpKind::CrefExp: {
        const Cref& c = I_.absyn.crefs[e.cref];
        // 数组推导式的迭代变量（内层优先）。
        if (!citers_.empty() && c.parts.size() == 1 && c.parts[0].subscripts.empty())
          for (auto it = citers_.rbegin(); it != citers_.rend(); ++it)
            if (it->first == c.parts[0].name.id) return it->second;
        // 用户函数解释期：单名命中局部环境（输入/局部/输出）→ 取其值；带下标 → 取元素 / 切片（cval_index）。
        if (locals_ && c.parts.size() == 1) {
          auto it = locals_->find(c.parts[0].name.id);
          if (it != locals_->end()) {
            if (c.parts[0].subscripts.empty()) return it->second;
            const CVal whole = it->second;   // 拷贝：下标求值可能改动 locals_（不会，但不跨调用持迭代器）
            return cval_index(whole, c.parts[0].subscripts, 0, scope, depth);
          }
        }
        // 局部记录的字段 `r.x` / `r2.r1.x`（可带逐段下标）。
        if (locals_ && c.parts.size() > 1) {
          auto it = locals_->find(c.parts[0].name.id);
          if (it != locals_->end()) {
            const CVal root = it->second;
            return cval_path(root, c, scope, depth);
          }
        }
        // for 方程迭代变量（嵌套 for 的依赖范围 `i+1:i+3`，ForEquation7）。
        if (!loop_vars_.empty() && c.parts.size() == 1 && c.parts[0].subscripts.empty()) {
          auto it = loop_vars_.find(c.parts[0].name.id);
          if (it != loop_vars_.end()) {
            // Boolean 范围上的迭代变量取 Boolean 值；枚举值在值求值路没有表示 → 求不出（调用方走定型路 / SKIP）。
            if (auto lb = loop_labels_.find(it->first); lb != loop_labels_.end()) {
              const Expr& le = result_.model.exprs[lb->second];
              return le.kind == ExprKind::BoolLit ? std::optional<CVal>(CVal::B(le.bval)) : std::nullopt;
            }
            return CVal::I(it->second);
          }
        }
        // 模型语境：本实例的局部参数 / 常量按修饰后的绑定求值（NF 的组件绑定 = 修饰合并后，Structural.markExp 把求值
        // 用到的参数标为结构参数）。
        if (ceval_frame_ >= 0 && !locals_ && !c.leading_dot) {
          // 多段名：子实例 / 记录参数里已发射的常量、字面量绑定的参数（`Real z[a.j]`、`a.p`、`r.n`、`c[2].j`，ND 探针 d1；
          // 参数随之成结构参数）。frame_value 不接多段名（参数 → Fail），所以先查；没有显式的书写处上下文（维 / for 范围
          // 的求值）时按当前帧的前缀。
          if (c.parts.size() > 1) {
            const Frame& F = frames_[static_cast<std::size_t>(ceval_frame_)];
            bool prot = false;
            const NodeId leaf = inst_path_leaf(F.inst, c, prot);
            if (leaf.is_valid()) {
              // 首段之后有 protected 段：omc 报错（访问受保护元素；ND 探针 p1：`constant Integer k = f.p`、`1:f.p`、`y[f.p]`
              // 此前都按已发射的值放行）。Evaluate = false 的参数不求（见 frame_value）。
              if (prot || evaluate_annotation(I_.comps[I_.nodes[leaf].comp].def) == std::optional<bool>(false)) return std::nullopt;
              if (std::optional<CVal> kv = const_lookup_val(ceval_prefix_ ? *ceval_prefix_ : F.prefix,
                                                            ceval_prefix_ ? ceval_psubs_ : F.psubs, c, scope, depth, allow_param))
                return kv;
            }
          }
          const FrameHit fh = frame_value(ceval_frame_, c, allow_param, depth, used, &scope);
          if (fh.kind == FrameHit::Value) return fh.v;
          if (fh.kind == FrameHit::Fail) return std::nullopt;
        }
        // 书写处实例前缀上已发射的常量（修饰后的最终值，const_vals_）优先：`A a(x = 1)` 里 `z = x + y` 的 x 取 1。
        if (ceval_prefix_ && !c.leading_dot)
          if (std::optional<CVal> kv = const_lookup_val(*ceval_prefix_, ceval_psubs_, c, scope, depth)) return kv;
        std::vector<Symbol> names;
        bool has_subs = false;
        for (const CrefPart& p : c.parts) {
          if (!p.subscripts.empty()) has_subs = true;
          names.push_back(p.name);
        }
        // 带下标 / 中间段是记录常量（`P.cr.r`、`P.cr[1].x`）：沿路径取值（eval_qual_path）。
        if (has_subs) return eval_qual_path(c, scope, depth, allow_param, used);
        Id<SElement> comp;
        ScodeScope cscope;
        if (!resolve_qual_const(names, c.leading_dot, scope, comp, cscope, allow_param))
          return c.parts.size() > 1 ? eval_qual_path(c, scope, depth, allow_param, used) : std::nullopt;
        if (used && I_.scode.elems[comp].attr.var == SVariability::Param) used->insert(comp.raw);
        const Id<SMod> m = I_.scode.elems[comp].mod;
        // 无绑定：记录常量由修饰 + 字段缺省构造（PackageConstant4 `r2[1] = {r1}`，r1 只有 `(x = 1)`）；其余 → nullopt。
        if (!m.is_valid() || !I_.scode.mods[m].binding.is_valid()) return const_comp_value(comp, cscope, depth);
        // 被引常量的声明绑定在其自身作用域求值：书写处前缀 / 实例帧不再适用。
        const std::vector<Symbol>* saved = ceval_prefix_;
        const int saved_frame = ceval_frame_;
        ceval_prefix_ = nullptr;
        ceval_frame_ = -1;
        auto v = eval_scode(I_.scode.mods[m].binding, cscope, depth + 1, allow_param, used);
        ceval_prefix_ = saved;
        ceval_frame_ = saved_frame;
        if (v) v = coerce_cval(*v, I_.scode.elems[comp].type.path);   // 按声明类型强转（Real n=3 → 3.0）
        return v;
      }
      case ExpKind::Unary: {
        auto v = eval_scode(e.a, scope, depth + 1, allow_param, used);
        if (!v) return std::nullopt;
        if (e.op == Op::UPlus) return v;
        if (e.op == Op::UMinus) return cval_is_zero(*v) ? v : cval_negate(*v);   // evalUnaryOp :1548-1549
        if (e.op == Op::Not && v->k == CVal::K::Bool) return CVal::B(!v->b);
        return std::nullopt;
      }
      case ExpKind::IfExp: {   // 只求被选分支（elseif 嵌在 c 里）
        auto cnd = eval_scode(e.a, scope, depth + 1, allow_param, used);
        if (!cnd || cnd->k != CVal::K::Bool) return std::nullopt;
        return eval_scode(cnd->b ? e.b : e.c, scope, depth + 1, allow_param, used);
      }
      case ExpKind::Range: {   // a:b / a:step:b（Absyn：a=起点, b=步长或终点, c=终点）
        auto a = eval_scode(e.a, scope, depth + 1, allow_param, used);
        auto b = eval_scode(e.b, scope, depth + 1, allow_param, used);
        if (!a || !b) return std::nullopt;
        if (!e.c.is_valid()) return cval_range(*a, std::nullopt, *b);
        auto z = eval_scode(e.c, scope, depth + 1, allow_param, used);
        return z ? cval_range(*a, b, *z) : std::nullopt;
      }
      case ExpKind::Binary: {
        auto a = eval_scode(e.a, scope, depth + 1, allow_param, used);
        auto b = eval_scode(e.b, scope, depth + 1, allow_param, used);
        if (!a || !b) return std::nullopt;
        BinOp aop;
        if (map_binop(e.op, aop)) return cval_binary(e.op, *a, *b);   // 算术（含数组，见 cval_binary）
        if (is_arr(*a) || is_arr(*b)) return std::nullopt;             // 数组逻辑 / 关系未做
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
        if (e.call_suffix.is_valid()) return std::nullopt;   // `f(i).y` 未做
        // size(arr, d)：解析数组组件 arr（任意可变性），求它第 d 维（DimSize1）。
        const Cref& fn = I_.absyn.crefs[e.cref];
        // size(arr)：各维大小组成的 Integer 向量（evalBuiltinSize，NFCeval；Scalar 的 `scalar(size(rs))`）——函数局部按当前值
        // 形状，组件按声明维求值。
        // ndims(arr)：声明的维数（不求各维的值；TestNDims `Real[ndims(a)] b`）。
        const bool is_ndims = fn.parts.size() == 1 && in_.str(fn.parts[0].name) == "ndims";
        if (fn.parts.size() == 1 && (is_ndims || in_.str(fn.parts[0].name) == "size") && e.args.size() == 1 &&
            !find_scode_element(fn.parts[0].name, scope).is_valid()) {
          const Exp& ae = I_.absyn.exps[e.args[0]];
          if (ae.kind != ExpKind::CrefExp) return std::nullopt;
          const Cref& ac = I_.absyn.crefs[ae.cref];
          std::vector<CVal> out;
          auto done = [&]() -> std::optional<CVal> {
            if (is_ndims) return CVal::I(static_cast<std::int64_t>(out.size()));
            return out.empty() ? std::nullopt : std::optional<CVal>(CVal::Arr(std::move(out)));
          };
          if (locals_ && ac.parts.size() == 1 && ac.parts[0].subscripts.empty()) {
            auto it = locals_->find(ac.parts[0].name.id);
            if (it != locals_->end()) {
              for (std::int64_t n : cval_shape(it->second)) out.push_back(CVal::I(n));
              return done();
            }
          }
          if (const std::optional<std::vector<std::int64_t>> sh = sub_instance_shape(ac)) {
            if (sh->empty()) return std::nullopt;
            for (std::int64_t n : *sh) out.push_back(CVal::I(n));
            return done();
          }
          std::vector<Symbol> anames;
          for (const CrefPart& p : ac.parts) {
            if (!p.subscripts.empty()) return std::nullopt;
            anames.push_back(p.name);
          }
          Id<SElement> acomp;
          ScodeScope ascope;
          if (!resolve_qual_const(anames, ac.leading_dot, scope, acomp, ascope, /*allow_param=*/true, /*allow_any=*/true)) {
            if (!frame_local_comp(anames, acomp)) return std::nullopt;
            ascope = scope;
          }
          std::vector<Id<Exp>> adims = I_.scode.elems[acomp].attr.dims;
          for (Id<Exp> td : I_.scode.elems[acomp].type.dims) adims.push_back(td);
          if (is_ndims) return CVal::I(static_cast<std::int64_t>(adims.size()));
          for (std::size_t k = 0; k < adims.size(); ++k) {
            std::optional<CVal> dv = colon_dim_size(anames, acomp, adims, k);
            if (!dv) dv = eval_scode(adims[k], ascope, depth + 1, allow_param, used);
            if (!dv || dv->k != CVal::K::Int) return std::nullopt;
            out.push_back(*dv);
          }
          return out.empty() ? std::nullopt : std::optional<CVal>(CVal::Arr(std::move(out)));
        }
        if (fn.parts.size() != 1 || in_.str(fn.parts[0].name) != "size" || e.args.size() != 2) {
          if (auto av = eval_array_builtin(e, scope, depth, allow_param, used)) return av;   // zeros/ones/fill/identity
          if (auto rv = eval_reduction(e, scope, depth)) return rv;   // sum/product/min/max(常量数组)
          // 标量内建数学函数（abs/sqrt/sin/…/max(a, b)…，无同名用户类）：实参求值后复用类型化路径的折叠。
          if (fn.parts.size() == 1 && fn.parts[0].subscripts.empty() && lookup_builtin(in_.str(fn.parts[0].name)) &&
              !find_scode_element(fn.parts[0].name, scope).is_valid()) {
            bool named = false;
            for (Symbol an : e.arg_names) named = named || an.is_valid();
            std::vector<CVal> bargs;
            for (Id<Exp> a : e.args) {
              if (named) break;
              std::optional<CVal> v = eval_scode(a, scope, depth + 1, allow_param, used);
              if (!v) return std::nullopt;
              bargs.push_back(std::move(*v));
            }
            if (!named) return eval_scalar_builtin(in_.str(fn.parts[0].name), bargs);
          }
          return eval_user_call(e, scope, depth, allow_param);   // 非 size → 试用户函数常量折叠
        }
        const Exp& ae = I_.absyn.exps[e.args[0]];
        auto dv = eval_scode(e.args[1], scope, depth + 1, allow_param, used);
        // size(<数组构造子 / 矩阵>, d)：按书写的嵌套层数（lit_dims；元素是数组引用的那一层求不出 → nullopt）。供维 / 常量
        //（SZ 探针 `Real y[size({1, 2, 3}, 1)]`、`constant Integer c = size({1, 2}, 1)`）。
        if (dv && dv->k == CVal::K::Int && dv->i >= 1 && (is_array_ctor(e.args[0]) || ae.kind == ExpKind::Matrix))
          if (const std::optional<std::int64_t> n = binding_dim_size(e.args[0], static_cast<std::size_t>(dv->i) - 1))
            return CVal::I(*n);
        if (ae.kind != ExpKind::CrefExp || !dv || dv->k != CVal::K::Int) return std::nullopt;
        // 函数解释期的局部数组（输入 `A[:]` 取实参形状，PolynomialEvaluator1 `size(A, 1)`）：按当前值的形状。
        if (locals_ && I_.absyn.crefs[ae.cref].parts.size() == 1 && I_.absyn.crefs[ae.cref].parts[0].subscripts.empty()) {
          auto it = locals_->find(I_.absyn.crefs[ae.cref].parts[0].name.id);
          if (it != locals_->end()) {
            const std::vector<std::int64_t> sh = cval_shape(it->second);
            if (dv->i < 1 || static_cast<std::size_t>(dv->i) > sh.size()) return std::nullopt;
            return CVal::I(sh[static_cast<std::size_t>(dv->i) - 1]);
          }
        }
        if (const std::optional<std::vector<std::int64_t>> sh = sub_instance_shape(I_.absyn.crefs[ae.cref])) {
          if (dv->i < 1 || static_cast<std::size_t>(dv->i) > sh->size()) return std::nullopt;
          return CVal::I((*sh)[static_cast<std::size_t>(dv->i) - 1]);
        }
        std::vector<Symbol> anames;
        for (const CrefPart& p : I_.absyn.crefs[ae.cref].parts) {
          if (!p.subscripts.empty()) return std::nullopt;
          anames.push_back(p.name);
        }
        // size(T, 1)，T 是类型名（Boolean / 枚举）：其取值个数（DM 探针 `Integer y[size(E, 1)]` → 3 个元素、
        // `size(Boolean, 1)` → 2）。
        if (dv->i == 1)
          if (std::optional<CVal> tn = type_dim_size(e.args[0])) return tn;
        Id<SElement> acomp;
        ScodeScope ascope;
        if (!resolve_qual_const(anames, I_.absyn.crefs[ae.cref].leading_dot, scope, acomp, ascope,
                                /*allow_param=*/true, /*allow_any=*/true)) {
          if (!frame_local_comp(anames, acomp)) return std::nullopt;
          ascope = scope;
        }
        std::vector<Id<Exp>> adims = I_.scode.elems[acomp].attr.dims;
        for (Id<Exp> td : I_.scode.elems[acomp].type.dims) adims.push_back(td);
        const std::int64_t di = dv->i;   // 1 基
        if (di < 1 || static_cast<std::size_t>(di) > adims.size()) return std::nullopt;
        if (std::optional<CVal> cd = colon_dim_size(anames, acomp, adims, static_cast<std::size_t>(di) - 1)) return cd;
        if (std::optional<CVal> tn = type_dim_size(adims[di - 1])) return tn;   // 维写的是类型名（`Real e[E]`）
        return eval_scode(adims[di - 1], ascope, depth + 1, allow_param, used);
      }
      default:
        return std::nullopt;
    }
  }

  // size() 的实参是当前求值帧里的本地数组、第 k 维写的是 `:`（由绑定推断）：取已发射元素的形状——emit_array 已按推出的维
  // 逐元素发射，所以被引用的数组须先于引用处发射（前向 / 自引用 → nullopt，调用方 SKIP）。size 只看类型，不把数组标成
  // 结构参数（Structural.markExp 的 SIZE 支，NFStructural.mo:270-282 v1.25.0；DM 探针 D1 `Real x[size(a, 1) - 1]` → a 不
  // final）。不是这种情形 → nullopt。
  std::optional<CVal> colon_dim_size(const std::vector<Symbol>& anames, Id<SElement> acomp, const std::vector<Id<Exp>>& adims,
                                     std::size_t k) {
    if (I_.absyn.exps[adims[k]].kind != ExpKind::Colon || anames.size() != 1 || ceval_frame_ < 0 || locals_) return std::nullopt;
    const Frame& F = frames_[static_cast<std::size_t>(ceval_frame_)];
    const NodeId ln = find_local(F.inst, anames[0]);
    if (!ln.is_valid() || I_.nodes[ln].is_empty() || !I_.nodes[ln].comp.is_valid() || !(I_.comps[I_.nodes[ln].comp].def == acomp))
      return std::nullopt;
    std::vector<Symbol> names = F.prefix;
    names.push_back(anames[0]);
    PSubs ps = F.psubs;
    ps.resize(F.prefix.size());
    if (auto it = pending_shapes_.find(int_key(names, ps)); it != pending_shapes_.end())   // 正在发射的本数组
      return k < it->second.size() && it->second[k] >= 0 ? std::optional<CVal>(CVal::I(it->second[k])) : std::nullopt;
    const std::optional<std::vector<std::int64_t>> sh = emitted_shape(names, ps);
    if (!sh || sh->size() != adims.size()) return std::nullopt;
    return CVal::I((*sh)[k]);
  }

  // 多段名沿实例树逐段找组件（首段是 inst 的本地组件，下标不看）：走通 → 末段的组件节点，任一段找不到 → invalid。
  // prot = 首段之后有 protected 的段。
  NodeId inst_path_leaf(NodeId inst, const Cref& c, bool& prot) {
    NodeId cur = find_local(inst, c.parts[0].name);
    if (!is_comp_node(cur)) return {};
    for (std::size_t i = 1; i < c.parts.size(); ++i) {
      const NodeId cls = I_.comps[I_.nodes[cur].comp].class_inst;
      if (!is_class_inst(cls)) return {};
      cur = find_local(cls, c.parts[i].name);
      if (!is_comp_node(cur)) return {};
      prot = prot || I_.nodes[cur].visibility == Visibility::Protected;
    }
    return cur;
  }
  // size() / ndims() 的实参是当前求值帧里子实例的数组（`a.v`、`bb.x`：首段是本地的类实例组件）：它的维受实例修饰影响
  //（`B bb(n = 3)` 的 `Real x[n]`），按 SCode 声明求会漏掉修饰 → 取已发射元素的形状（被引用的数组须先于引用处发射；
  // 没发射 / 带下标 → 空形状，调用方求不出 → SKIP）。不是这种情形 → nullopt。
  std::optional<std::vector<std::int64_t>> sub_instance_shape(const Cref& ac) {
    if (ac.parts.size() < 2 || ac.leading_dot || ceval_frame_ < 0 || locals_) return std::nullopt;
    const Frame& F = frames_[static_cast<std::size_t>(ceval_frame_)];
    const NodeId ln = find_local(F.inst, ac.parts[0].name);
    if (!ln.is_valid() || I_.nodes[ln].is_empty() || !I_.nodes[ln].comp.is_valid() ||
        I_.comps[I_.nodes[ln].comp].ty.kind != TypeKind::Complex)
      return std::nullopt;
    std::vector<Symbol> names = F.prefix;
    PSubs ps = F.psubs;
    ps.resize(F.prefix.size());
    for (std::size_t i = 0; i < ac.parts.size(); ++i) {
      if (!ac.parts[i].subscripts.empty()) return std::vector<std::int64_t>{};
      names.push_back(ac.parts[i].name);
      if (i + 1 < ac.parts.size()) ps.push_back({});
    }
    const std::optional<std::vector<std::int64_t>> sh = emitted_shape(names, ps);
    return sh ? *sh : std::vector<std::int64_t>{};
  }
  // size() 的实参按 SCode 作用域链查不到（继承来的组件声明在基类里，而维是在派生实例的作用域里求的，DM 探针 X4
  // `model A … Real y[size(c, 1)]; end A; model X4 extends A; end X4;`）：按当前求值帧的实例找本地组件。它的维照旧在当前
  // 作用域里求——维里的本地名走帧查找，基类词法作用域里才看得见的名字求不出（nullopt → SKIP）。
  bool frame_local_comp(const std::vector<Symbol>& anames, Id<SElement>& acomp) {
    if (anames.size() != 1 || ceval_frame_ < 0 || locals_) return false;
    const NodeId ln = find_local(frames_[static_cast<std::size_t>(ceval_frame_)].inst, anames[0]);
    if (!ln.is_valid() || I_.nodes[ln].is_empty() || !I_.nodes[ln].comp.is_valid()) return false;
    acomp = I_.comps[I_.nodes[ln].comp].def;
    return acomp.is_valid();
  }
  // Absyn 表达式是类型名（Boolean / 枚举，同 classify_type_dim）→ 该类型的取值个数；按当前求值帧的实例解析。
  std::optional<CVal> type_dim_size(Id<Exp> d) {
    if (ceval_frame_ < 0 || locals_ || I_.absyn.exps[d].kind != ExpKind::CrefExp) return std::nullopt;
    const Frame& F = frames_[static_cast<std::size_t>(ceval_frame_)];
    const std::optional<std::vector<Id<Expr>>> labels = classify_type_dim(d, F.inst, F.prefix);
    return labels ? std::optional<CVal>(CVal::I(static_cast<std::int64_t>(labels->size()))) : std::nullopt;
  }

  // ---- 数组值运算（NFCeval.evalBinary*/evalUnaryMinus/evalRange*/evalBuiltinFill…，行号见各函数）----
  static bool is_arr(const CVal& v) { return v.k == CVal::K::Array; }
  // 数组值的形状（矩形；空数组靠 eshape 记完整形状）。标量 → {}。
  static std::vector<std::int64_t> cval_shape(const CVal& v) {
    std::vector<std::int64_t> s;
    for (const CVal* c = &v; c->k == CVal::K::Array; c = &c->arr->front()) {
      if (!c->arr || c->arr->empty()) {
        if (c->eshape.empty()) s.push_back(0);
        else s.insert(s.end(), c->eshape.begin(), c->eshape.end());
        break;
      }
      s.push_back(static_cast<std::int64_t>(c->arr->size()));
    }
    return s;
  }
  static CVal empty_arr(std::vector<std::int64_t> shape) {
    CVal r = CVal::Arr({});
    r.eshape = std::move(shape);
    return r;
  }
  // fill(v, n1, n2, …)（Expression.fillArgs；zeros/ones = fill(0/1, …)，evalBuiltinZeros :3102 / Ones :2745 / Fill :2431）。
  static CVal cval_fill(const CVal& v, const std::vector<std::int64_t>& dims, std::size_t k = 0) {
    if (k == dims.size()) return v;
    if (dims[k] == 0) return empty_arr(std::vector<std::int64_t>(dims.begin() + static_cast<std::ptrdiff_t>(k), dims.end()));
    std::vector<CVal> els(static_cast<std::size_t>(dims[k]), cval_fill(v, dims, k + 1));
    return CVal::Arr(std::move(els));
  }
  // 标量算术（evalBinaryAdd/Sub/Mul/Div/Pow 的标量分支）：Int∘Int 保 Int（除/幂除外，typing 已把 `/` `^` 操作数提实型）；
  // String `+` 拼接（:1028-1029）；除零、负底非整指数（INVALID_NEGATIVE_POW :1281-1289）→ omc 报错 → nullopt。
  std::optional<CVal> cval_scalar_arith(BinOp op, const CVal& a, const CVal& b) {
    if (a.k == CVal::K::Str || b.k == CVal::K::Str) {
      if (op != BinOp::Add || a.k != CVal::K::Str || b.k != CVal::K::Str) return std::nullopt;
      return CVal::S(in_.intern(std::string(in_.str(a.s)) + std::string(in_.str(b.s))));
    }
    if ((a.k != CVal::K::Int && a.k != CVal::K::Real) || (b.k != CVal::K::Int && b.k != CVal::K::Real)) return std::nullopt;
    if (a.k == CVal::K::Int && b.k == CVal::K::Int && op != BinOp::Div && op != BinOp::Pow) switch (op) {
      case BinOp::Add: return CVal::I(a.i + b.i);
      case BinOp::Sub: return CVal::I(a.i - b.i);
      case BinOp::Mul: return CVal::I(a.i * b.i);
      default: break;
    }
    const double x = a.as_r(), y = b.as_r();
    switch (op) {
      case BinOp::Add: return CVal::R(x + y);
      case BinOp::Sub: return CVal::R(x - y);
      case BinOp::Mul: return CVal::R(x * y);
      case BinOp::Div: return y != 0.0 ? std::optional<CVal>(CVal::R(x / y)) : std::nullopt;
      case BinOp::Pow: return (x < 0 && std::floor(y) != y) ? std::nullopt : std::optional<CVal>(CVal::R(std::pow(x, y)));
    }
    return std::nullopt;
  }
  // 逐元素（同形数组两两，或一侧标量广播：ARRAY/ARRAY 分支 + evalBinaryScalarArray/ArrayScalar :1309-1351）。
  template <class F>
  std::optional<CVal> cval_zip(const CVal& a, const CVal& b, F&& f) {
    if (!is_arr(a) && !is_arr(b)) return f(a, b);
    if (is_arr(a) && is_arr(b) && cval_shape(a) != cval_shape(b)) return std::nullopt;   // 形状不符 = omc 类型错
    const CVal& lead = is_arr(a) ? a : b;
    if (lead.arr->empty()) return empty_arr(cval_shape(lead));
    std::vector<CVal> out;
    out.reserve(lead.arr->size());
    for (std::size_t i = 0; i < lead.arr->size(); ++i) {
      std::optional<CVal> x = cval_zip(is_arr(a) ? (*a.arr)[i] : a, is_arr(b) ? (*b.arr)[i] : b, f);
      if (!x) return std::nullopt;
      out.push_back(std::move(*x));
    }
    return CVal::Arr(std::move(out));
  }
  // 标量积（evalBinaryScalarProduct :1405-1437）：从元素类型的零起逐项加积；空向量 → 0。
  std::optional<CVal> cval_dot(const CVal& a, const CVal& b) {
    if (!is_arr(a) || !is_arr(b) || a.arr->size() != b.arr->size()) return std::nullopt;
    CVal acc = (!a.arr->empty() && a.arr->front().k == CVal::K::Real) ? CVal::R(0.0) : CVal::I(0);
    for (std::size_t i = 0; i < a.arr->size(); ++i) {
      std::optional<CVal> p = cval_scalar_arith(BinOp::Mul, (*a.arr)[i], (*b.arr)[i]);
      if (!p) return std::nullopt;
      std::optional<CVal> s = cval_scalar_arith(BinOp::Add, acc, *p);
      if (!s) return std::nullopt;
      acc = std::move(*s);
    }
    return acc;
  }
  // 转置（Expression.transposeArray）：交换前两维（m×p×… → p×m×…，维数 ≥ 2），空维经 eshape 保留。
  std::optional<CVal> cval_transpose(const CVal& m) {
    const std::vector<std::int64_t> s = cval_shape(m);
    if (s.size() < 2) return std::nullopt;
    const std::vector<std::int64_t> rest(s.begin() + 2, s.end());
    if (s[1] == 0) {
      std::vector<std::int64_t> es{0, s[0]};
      es.insert(es.end(), rest.begin(), rest.end());
      return empty_arr(std::move(es));
    }
    std::vector<CVal> rows;
    for (std::int64_t j = 0; j < s[1]; ++j) {
      if (s[0] == 0) {
        std::vector<std::int64_t> es{0};
        es.insert(es.end(), rest.begin(), rest.end());
        rows.push_back(empty_arr(std::move(es)));
        continue;
      }
      std::vector<CVal> row;
      for (std::int64_t i = 0; i < s[0]; ++i) row.push_back((*(*m.arr)[static_cast<std::size_t>(i)].arr)[static_cast<std::size_t>(j)]);
      rows.push_back(CVal::Arr(std::move(row)));
    }
    return CVal::Arr(std::move(rows));
  }
  // `*` 两侧皆数组（TypeCheck 按维数选算子）：向量·向量 = 标量积；矩阵·向量 / 向量·矩阵（evalBinaryMulMatrixVector /
  // MulVectorMatrix :1353-1403）；矩阵·矩阵（evalBinaryMatrixProduct :1439-1484：右阵转置后行对行作标量积，右阵无列 →
  // n×0 零阵）。
  std::optional<CVal> cval_matmul(const CVal& a, const CVal& b) {
    const std::vector<std::int64_t> sa = cval_shape(a), sb = cval_shape(b);
    if (sa.size() == 1 && sb.size() == 1) return cval_dot(a, b);
    if (sa.size() == 2 && sb.size() == 1) {
      if (sa[1] != sb[0]) return std::nullopt;
      std::vector<CVal> out;
      for (const CVal& row : *a.arr) {
        std::optional<CVal> d = cval_dot(row, b);
        if (!d) return std::nullopt;
        out.push_back(std::move(*d));
      }
      return out.empty() ? empty_arr({0}) : CVal::Arr(std::move(out));
    }
    if (sa.size() == 1 && sb.size() == 2) {
      if (sa[0] != sb[0]) return std::nullopt;
      std::optional<CVal> bt = cval_transpose(b);
      if (!bt) return std::nullopt;
      if (bt->arr->empty()) return empty_arr({0});
      std::vector<CVal> out;
      for (const CVal& col : *bt->arr) {
        std::optional<CVal> d = cval_dot(a, col);
        if (!d) return std::nullopt;
        out.push_back(std::move(*d));
      }
      return CVal::Arr(std::move(out));
    }
    if (sa.size() == 2 && sb.size() == 2) {
      if (sa[1] != sb[0]) return std::nullopt;
      std::optional<CVal> bt = cval_transpose(b);
      if (!bt) return std::nullopt;
      const std::int64_t n = sa[0], p = sb[1];
      if (p == 0) return cval_fill(CVal::I(0), {n, 0});   // makeZero(n×0)
      if (n == 0) return empty_arr({0, p});
      std::vector<CVal> rows;
      for (const CVal& row : *a.arr) {
        std::vector<CVal> r;
        for (const CVal& col : *bt->arr) {
          std::optional<CVal> d = cval_dot(row, col);
          if (!d) return std::nullopt;
          r.push_back(std::move(*d));
        }
        rows.push_back(CVal::Arr(std::move(r)));
      }
      return CVal::Arr(std::move(rows));
    }
    return std::nullopt;
  }
  // 矩阵整数幂（evalBinaryPowMatrix :1486-1511 / PowMatrix2 :1513-1540）：^0 = Real 单位阵；^1 = 自身；偶数 = 半幂自乘；
  // 奇数 = A·A^(n-1)。负指数 → nullopt。
  std::optional<CVal> cval_matpow(const CVal& m, const CVal& e) {
    const std::vector<std::int64_t> s = cval_shape(m);
    if (e.k != CVal::K::Int || s.size() != 2 || s[0] != s[1] || e.i < 0) return std::nullopt;
    if (e.i == 0) {
      std::vector<CVal> rows;
      for (std::int64_t i = 0; i < s[0]; ++i) {
        std::vector<CVal> r;
        for (std::int64_t j = 0; j < s[0]; ++j) r.push_back(CVal::R(i == j ? 1.0 : 0.0));
        rows.push_back(CVal::Arr(std::move(r)));
      }
      return rows.empty() ? empty_arr({0, 0}) : CVal::Arr(std::move(rows));
    }
    auto pow2 = [&](auto& self, std::int64_t n) -> std::optional<CVal> {
      if (n == 1) return m;
      if (n == 2) return cval_matmul(m, m);
      if (n % 2 == 0) {
        std::optional<CVal> h = self(self, n / 2);
        return h ? cval_matmul(*h, *h) : std::nullopt;
      }
      std::optional<CVal> h = self(self, n - 1);
      return h ? cval_matmul(m, *h) : std::nullopt;
    };
    return pow2(pow2, e.i);
  }
  // 二元算术分派（evalBinaryOp_dispatch :971-1008，算子已由 TypeCheck 按操作数维数选定）：`+ -` 须同形（标量混数组只许
  // `.+ .-`）；`*` 标量·数组广播、两侧皆数组走矩阵积；`/` 只许 数组/标量；`^` 标量或 方阵^整数；点号算子逐元素 / 广播。
  std::optional<CVal> cval_binary(Op op, const CVal& a, const CVal& b) {
    BinOp bop;
    if (!map_binop(op, bop)) return std::nullopt;
    const bool ew = op == Op::EWAdd || op == Op::EWSub || op == Op::EWMul || op == Op::EWDiv || op == Op::EWPow;
    auto scal = [&](const CVal& x, const CVal& y) { return cval_scalar_arith(bop, x, y); };
    const bool aa = is_arr(a), ba = is_arr(b);
    if (!aa && !ba) return scal(a, b);
    switch (bop) {
      case BinOp::Add:
      case BinOp::Sub:
        if (!ew && aa != ba) return std::nullopt;
        return cval_zip(a, b, scal);
      case BinOp::Mul:
        if (ew || !aa || !ba) return cval_zip(a, b, scal);
        return cval_matmul(a, b);
      case BinOp::Div:
        if (!ew && !(aa && !ba)) return std::nullopt;
        return cval_zip(a, b, scal);
      case BinOp::Pow:
        if (ew) return cval_zip(a, b, scal);
        return (aa && !ba) ? cval_matpow(a, b) : std::nullopt;
    }
    return std::nullopt;
  }
  // 一元取负（evalUnaryOp :1542-1557 / evalUnaryMinus :1559-1580）：值为零（含全零数组）原样返回，否则逐元素取负。
  static bool cval_is_zero(const CVal& v) {
    if (v.k == CVal::K::Int) return v.i == 0;
    if (v.k == CVal::K::Real) return v.r == 0.0;
    if (v.k == CVal::K::Array) {
      for (const CVal& e : *v.arr) if (!cval_is_zero(e)) return false;
      return true;
    }
    return false;
  }
  static std::optional<CVal> cval_negate(const CVal& v) {
    if (v.k == CVal::K::Int) return CVal::I(-v.i);
    if (v.k == CVal::K::Real) return CVal::R(-v.r);
    if (v.k != CVal::K::Array) return std::nullopt;
    if (v.arr->empty()) return v;
    std::vector<CVal> out;
    for (const CVal& e : *v.arr) {
      std::optional<CVal> n = cval_negate(e);
      if (!n) return std::nullopt;
      out.push_back(std::move(*n));
    }
    return CVal::Arr(std::move(out));
  }
  // 范围（evalRangeExp :841-913）：Integer（含步长，步长 0 → nullopt）/ Real（evalRangeReal :915-938：个数 =
  // floor((stop-start)/step + 5e-15) + 1（Util.realRangeSize），首尾按原值、中间 start + i·step）/ Boolean。
  std::optional<CVal> cval_range(const CVal& start, const std::optional<CVal>& step, const CVal& stop) {
    auto num = [](const CVal& v) { return v.k == CVal::K::Int || v.k == CVal::K::Real; };
    std::vector<CVal> els;
    if (start.k == CVal::K::Bool && stop.k == CVal::K::Bool && !step) {
      for (int b = start.b ? 1 : 0; b <= (stop.b ? 1 : 0); ++b) els.push_back(CVal::B(b == 1));
    } else if (start.k == CVal::K::Int && stop.k == CVal::K::Int && (!step || step->k == CVal::K::Int)) {
      const std::int64_t st = step ? step->i : 1;
      if (st == 0) return std::nullopt;
      for (std::int64_t i = start.i; st > 0 ? i <= stop.i : i >= stop.i; i += st) els.push_back(CVal::I(i));
    } else if (num(start) && num(stop) && (!step || num(*step))) {
      const double a = start.as_r(), s = step ? step->as_r() : 1.0, z = stop.as_r();
      if (s == 0.0) return std::nullopt;
      const std::int64_t n = std::max<std::int64_t>(static_cast<std::int64_t>(std::floor((z - a) / s + 5e-15)) + 1, 0);
      if (n >= 1) els.push_back(CVal::R(a));
      for (std::int64_t i = 1; i + 1 < n; ++i) els.push_back(CVal::R(a + static_cast<double>(i) * s));
      if (n >= 2) els.push_back(CVal::R(z));
    } else {
      return std::nullopt;
    }
    return els.empty() ? empty_arr({0}) : CVal::Arr(std::move(els));
  }

  // 数组类内建的常量求值。同名用户类遮蔽内建 → 不接（nullopt）；实参类型/个数不符 = omc 报错 → nullopt。
  //   zeros/ones/fill/identity（evalBuiltinZeros :3102 / Ones :2745 / Fill :2431 / Identity :2458）：zeros/ones 为 Integer
  //     0/1，fill(v, n…) 复制 v，identity(n) 为 Integer 单位阵；维数须为非负整数常量。
  //   transpose（:3069-3088，维数 ≥ 2，换前两维）、vector（:3090-3100，全部标量元素摊成向量）、diagonal（:2340-2377，零取
  //     元素类型）、scalar（:2846-2853，逐层取唯一元素）、der（:2333-2338，常量的导数 = 同形 Real 0）、skew（:2888-2913）。
  //   cross / linspace 是 Modelica 层内建（NFModelicaBuiltin.mo:353-362 / :305-316），按其函数体求值：输入 Real（Int 提升），
  //     cross = {x2·y3−x3·y2, x3·y1−x1·y3, x1·y2−x2·y1}，linspace = {x1 + (x2−x1)·(i−1)/(n−1) for i in 1:n}。
  std::optional<CVal> eval_array_builtin(const Exp& e, const ScodeScope& scope, int depth, bool allow_param,
                                         std::unordered_set<std::uint32_t>* used) {
    const Cref& fn = I_.absyn.crefs[e.cref];
    if (fn.parts.size() != 1 || !fn.parts[0].subscripts.empty()) return std::nullopt;
    const std::string_view nm = in_.str(fn.parts[0].name);
    static constexpr std::string_view kNames[] = {"zeros", "ones", "fill", "identity", "transpose", "vector", "diagonal",
                                                  "scalar", "der", "skew", "cross", "linspace"};
    if (std::find(std::begin(kNames), std::end(kNames), nm) == std::end(kNames)) return std::nullopt;
    for (Symbol an : e.arg_names) if (an.is_valid()) return std::nullopt;
    if (find_scode_element(fn.parts[0].name, scope).is_valid()) return std::nullopt;
    std::vector<CVal> args;
    for (Id<Exp> a : e.args) {
      std::optional<CVal> v = eval_scode(a, scope, depth + 1, allow_param, used);
      if (!v) return std::nullopt;
      args.push_back(std::move(*v));
    }
    auto real_vec3 = [&](const CVal& v) -> std::optional<std::vector<double>> {   // Real[3] 输入（Int 提升）
      if (!is_arr(v) || v.arr->size() != 3) return std::nullopt;
      std::vector<double> r;
      for (const CVal& x : *v.arr) {
        if (x.k != CVal::K::Int && x.k != CVal::K::Real) return std::nullopt;
        r.push_back(x.as_r());
      }
      return r;
    };
    if (nm == "transpose") return args.size() == 1 ? cval_transpose(args[0]) : std::nullopt;
    if (nm == "vector") {
      if (args.size() != 1) return std::nullopt;
      if (!is_arr(args[0])) return CVal::Arr({args[0]});
      std::vector<CVal> flat;
      flatten_cval_array(args[0], flat);
      return flat.empty() ? empty_arr({0}) : CVal::Arr(std::move(flat));
    }
    if (nm == "diagonal") {
      if (args.size() != 1 || !is_arr(args[0]) || cval_shape(args[0]).size() != 1) return std::nullopt;
      const std::vector<CVal>& d = *args[0].arr;
      if (d.empty()) return args[0];
      const CVal zero = d.front().k == CVal::K::Real ? CVal::R(0.0) : CVal::I(0);
      std::vector<CVal> rows;
      for (std::size_t i = 0; i < d.size(); ++i) {
        std::vector<CVal> r(d.size(), zero);
        r[i] = d[i];
        rows.push_back(CVal::Arr(std::move(r)));
      }
      return CVal::Arr(std::move(rows));
    }
    if (nm == "scalar") {
      if (args.size() != 1) return std::nullopt;
      CVal cur = args[0];
      while (is_arr(cur)) {
        if (cur.arr->size() != 1) return std::nullopt;
        CVal next = cur.arr->front();
        cur = std::move(next);
      }
      return cur;
    }
    if (nm == "der") {
      if (args.size() != 1) return std::nullopt;
      return is_arr(args[0]) ? cval_fill(CVal::R(0.0), cval_shape(args[0])) : CVal::R(0.0);
    }
    if (nm == "skew") {
      std::optional<std::vector<double>> x = args.size() == 1 ? real_vec3(args[0]) : std::nullopt;
      if (!x) return std::nullopt;
      const double x1 = (*x)[0], x2 = (*x)[1], x3 = (*x)[2];
      return CVal::Arr({CVal::Arr({CVal::R(0.0), CVal::R(-x3), CVal::R(x2)}),
                        CVal::Arr({CVal::R(x3), CVal::R(0.0), CVal::R(-x1)}),
                        CVal::Arr({CVal::R(-x2), CVal::R(x1), CVal::R(0.0)})});
    }
    if (nm == "cross") {
      std::optional<std::vector<double>> x = args.size() == 2 ? real_vec3(args[0]) : std::nullopt;
      std::optional<std::vector<double>> y = args.size() == 2 ? real_vec3(args[1]) : std::nullopt;
      if (!x || !y) return std::nullopt;
      const std::vector<double>& a = *x;
      const std::vector<double>& b = *y;
      return CVal::Arr({CVal::R(a[1] * b[2] - a[2] * b[1]), CVal::R(a[2] * b[0] - a[0] * b[2]),
                        CVal::R(a[0] * b[1] - a[1] * b[0])});
    }
    if (nm == "linspace") {
      if (args.size() != 3 || args[2].k != CVal::K::Int) return std::nullopt;
      if ((args[0].k != CVal::K::Int && args[0].k != CVal::K::Real) || (args[1].k != CVal::K::Int && args[1].k != CVal::K::Real))
        return std::nullopt;
      const double x1 = args[0].as_r(), x2 = args[1].as_r();
      const std::int64_t n = args[2].i;
      if (n < 0) return std::nullopt;
      if (n == 0) return empty_arr({0});
      if (n == 1) return std::nullopt;   // (i−1)/(n−1) 除零 → omc 报错
      std::vector<CVal> v;
      for (std::int64_t i = 1; i <= n; ++i)
        v.push_back(CVal::R(x1 + (x2 - x1) * static_cast<double>(i - 1) / static_cast<double>(n - 1)));
      return CVal::Arr(std::move(v));
    }
    // zeros / ones / fill / identity
    const std::size_t first_dim = nm == "fill" ? 1 : 0;
    if (args.size() < first_dim + 1 || (nm == "identity" && args.size() != 1)) return std::nullopt;
    std::vector<std::int64_t> dims;
    for (std::size_t k = first_dim; k < args.size(); ++k) {
      if (args[k].k != CVal::K::Int || args[k].i < 0) return std::nullopt;
      dims.push_back(args[k].i);
    }
    if (nm == "identity") {
      const std::int64_t n = dims[0];
      if (n == 0) return empty_arr({0, 0});
      std::vector<CVal> rows;
      for (std::int64_t i = 0; i < n; ++i) {
        std::vector<CVal> r;
        for (std::int64_t j = 0; j < n; ++j) r.push_back(CVal::I(i == j ? 1 : 0));
        rows.push_back(CVal::Arr(std::move(r)));
      }
      return CVal::Arr(std::move(rows));
    }
    const CVal fillv = nm == "zeros" ? CVal::I(0) : nm == "ones" ? CVal::I(1) : args[0];
    return cval_fill(fillv, dims);
  }

  // 按组件声明类型强转常量值：Int→Real 加宽 OK；Real→Integer 是收窄（omc 视为类型错，ceval5）→ nullopt（不产值）。
  // 数组逐元素强转（未赋值占位 Undef 原样）。
  std::optional<CVal> coerce_cval(const CVal& v, const std::vector<Symbol>& type_path) {
    if (type_path.size() != 1) return v;   // 派生/限定类型不处理
    if (is_arr(v)) {
      if (v.arr->empty()) return v;
      std::vector<CVal> out;
      for (const CVal& e : *v.arr) {
        std::optional<CVal> c = coerce_cval(e, type_path);
        if (!c) return std::nullopt;
        out.push_back(std::move(*c));
      }
      return CVal::Arr(std::move(out));
    }
    const std::string_view tn = in_.str(type_path[0]);
    if (v.k == CVal::K::Undef) return v;
    if (tn == "Real" && v.k == CVal::K::Int) return CVal::R(static_cast<double>(v.i));
    // 内建类型配不上的值是类型错（Real→Integer 收窄、String 给 Real 形参 …；N2 探针 `f("a")`，f 的输入是 Real → omc 报错）。
    if (tn == "Real") return v.k == CVal::K::Real ? std::optional<CVal>(v) : std::nullopt;
    if (tn == "Integer") return v.k == CVal::K::Int ? std::optional<CVal>(v) : std::nullopt;
    if (tn == "Boolean") return v.k == CVal::K::Bool ? std::optional<CVal>(v) : std::nullopt;
    if (tn == "String") return v.k == CVal::K::Str ? std::optional<CVal>(v) : std::nullopt;
    return v;
  }
  // 下标值：整数 → {i}；`:` → 1..n；范围 / 整数数组 → 其元素。越界 / 非整数 → nullopt。
  std::optional<std::vector<std::int64_t>> sub_indices(Id<Exp> s, std::int64_t n, const ScodeScope& scope, int depth) {
    std::vector<std::int64_t> out;
    if (I_.absyn.exps[s].kind == ExpKind::Colon) {
      for (std::int64_t i = 1; i <= n; ++i) out.push_back(i);
      return out;
    }
    const std::optional<std::int64_t> saved_end = eval_end_;
    eval_end_ = n;   // 下标里的 `end` = 所在维的大小（DM 探针 D4 `Real c[b[end]]`）
    std::optional<CVal> sv = eval_scode(s, scope, depth + 1, true);
    eval_end_ = saved_end;
    if (!sv) return std::nullopt;
    std::vector<CVal> flat;
    if (is_arr(*sv)) flatten_cval_array(*sv, flat); else flat.push_back(*sv);
    for (const CVal& c : flat) {
      if (c.k != CVal::K::Int || c.i < 1 || c.i > n) return std::nullopt;
      out.push_back(c.i);
    }
    return out;
  }
  // 按下标取值（NFCeval evalSubscriptedExp / applySubscripts）：整数下标降一维，`:` / 范围保留该维。未赋值占位 → nullopt。
  std::optional<CVal> cval_index(const CVal& v, const std::vector<Id<Exp>>& subs, std::size_t k, const ScodeScope& scope,
                                 int depth) {
    if (k == subs.size()) return v.k == CVal::K::Undef ? std::nullopt : std::optional<CVal>(v);
    if (!is_arr(v)) return std::nullopt;
    std::optional<std::vector<std::int64_t>> idx = sub_indices(subs[k], static_cast<std::int64_t>(v.arr->size()), scope, depth);
    if (!idx) return std::nullopt;
    const Exp& se = I_.absyn.exps[subs[k]];
    const bool scalar_sub = se.kind != ExpKind::Colon && se.kind != ExpKind::Range && se.kind != ExpKind::Array;
    if (scalar_sub) return cval_index((*v.arr)[static_cast<std::size_t>(idx->front() - 1)], subs, k + 1, scope, depth);
    std::vector<CVal> out;
    for (std::int64_t i : *idx) {
      std::optional<CVal> x = cval_index((*v.arr)[static_cast<std::size_t>(i - 1)], subs, k + 1, scope, depth);
      if (!x) return std::nullopt;
      out.push_back(std::move(*x));
    }
    return out.empty() ? empty_arr({0}) : CVal::Arr(std::move(out));
  }
  std::optional<std::int64_t> eval_end_;   // 编译期求值中下标里 `end` 的值（sub_indices 置）
  std::unordered_map<std::string, std::vector<std::int64_t>> pending_shapes_;   // 正在发射的数组 → 已定的 `:` 维（其余 -1）
  // omc 的函数求值把 cref 实参的数组对象原样交给输入（不复制），函数里 `y := x; y[i] := …` 原地改写的就是调用者的值——
  // 连被引用的 constant 也被改掉打印出来（FunctionBubblesort：constant a 打印成排好序的；分歧账本）。ymc 写时复制、语义
  // 正确但与 1.25.0 不一致 → 下标写入命中这类共享存储即 SKIP（分歧账本）。监视表 = cref 实参的各层数组存储。
  std::vector<const std::vector<CVal>*> alias_watch_;
  int alias_eval_depth_ = 0;
  void watch_arrays(const CVal& v) {
    if (!is_arr(v)) return;
    alias_watch_.push_back(v.arr.get());
    for (const CVal& e : *v.arr) watch_arrays(e);
  }
  // 按下标写入（函数里 `x[i] := v`、`x[i, j] := v`、切片 `table[:, 1] := vec`）：逐层写时复制，切片须与右值同长。
  bool cval_assign(CVal& target, const std::vector<Id<Exp>>& subs, std::size_t k, const CVal& val, const ScodeScope& scope,
                   int depth) {
    if (k == subs.size()) { target = val; return true; }
    if (!is_arr(target)) return false;
    if (std::find(alias_watch_.begin(), alias_watch_.end(), target.arr.get()) != alias_watch_.end())
      set_unsupported("omc aliases input array in function evaluation");
    if (target.arr.use_count() > 1) target.arr = std::make_shared<std::vector<CVal>>(*target.arr);   // COW
    std::optional<std::vector<std::int64_t>> idx =
        sub_indices(subs[k], static_cast<std::int64_t>(target.arr->size()), scope, depth);
    if (!idx) return false;
    const Exp& se = I_.absyn.exps[subs[k]];
    const bool scalar_sub = se.kind != ExpKind::Colon && se.kind != ExpKind::Range && se.kind != ExpKind::Array;
    if (scalar_sub) return cval_assign((*target.arr)[static_cast<std::size_t>(idx->front() - 1)], subs, k + 1, val, scope, depth);
    if (!is_arr(val) || val.arr->size() != idx->size()) return false;
    for (std::size_t j = 0; j < idx->size(); ++j)
      if (!cval_assign((*target.arr)[static_cast<std::size_t>((*idx)[j] - 1)], subs, k + 1, (*val.arr)[j], scope, depth))
        return false;
    return true;
  }
  // 标量内建数学函数的值求值：复用类型化路径 simplify_expr 的折叠（同一套语义与定义域守卫，lookup_builtin 表），实参按
  // argdom 提实型；单数组实参的一元函数逐元素向量化（typing 对标量函数的数组实参做 vectorize，如 `acos(1 .- v)`）。
  std::optional<CVal> eval_scalar_builtin(std::string_view name, const std::vector<CVal>& args) {
    const BuiltinFn* bf = lookup_builtin(name);
    if (!bf || !bf->foldable || name == "Integer" || bf->arity < 0 || static_cast<std::size_t>(bf->arity) != args.size())
      return std::nullopt;
    if (args.size() == 1 && is_arr(args[0])) {
      if (args[0].arr->empty()) return args[0];
      std::vector<CVal> out;
      for (const CVal& e : *args[0].arr) {
        std::optional<CVal> r = eval_scalar_builtin(name, {e});
        if (!r) return std::nullopt;
        out.push_back(std::move(*r));
      }
      return CVal::Arr(std::move(out));
    }
    bool any_real = false;
    for (const CVal& a : args) {
      if (a.k != CVal::K::Int && a.k != CVal::K::Real) return std::nullopt;
      any_real = any_real || a.k == CVal::K::Real;
    }
    Expr c;
    c.kind = ExprKind::Call;
    c.call_name = {in_.intern(name)};
    for (const CVal& a : args) {
      Id<Expr> x = literal_of(a);
      c.args.push_back(bf->argdom == BFArg::Real ? to_real(x) : x);
    }
    const bool real_res = bf->res == BFRes::Real || ((bf->res == BFRes::SameArg0 || bf->res == BFRes::CommonNum ||
                                                       bf->res == BFRes::DivType) && (any_real || bf->argdom == BFArg::Real));
    c.ty = bf->res == BFRes::Integer ? Type::integer() : real_res ? Type::real() : Type::integer();
    const Id<Expr> r = simplify_expr(add(std::move(c)));
    return cval_of(r);
  }

  // ---- 实例帧：修饰感知的参数 / 常量求值（结构参数）----
  // NF 的组件绑定是修饰合并后的绑定（`Foo foo(enabled = b)` 里 foo.enabled 的绑定是 `b`），条件 / 维度 / fixed / for 范围等
  // 编译期求值用到的参数经 Structural.markExp 变成结构参数（NFInst.updateImplicitVariability :3955-3992、NFFlatten
  // :1068 / :2106），EvalConstants 再把它们的引用处一律代入值（NFEvalConstants.evaluateExpTraverser :196-230）。ymc 在压平
  // DFS 里维护祖先帧：单段名命中本实例的局部参数 / 常量时，按"外来修饰绑定（在修饰书写处的帧求值）> 声明绑定"求值，
  // 记入 const_vals_，参数记为结构参数（apply_structural_params 统一打 final、代入引用）。YmcCond1/2 探针。
  struct FramePush {
    Flattener& f;
    int saved;
    FramePush(Flattener& fl, NodeId inst, const std::vector<Symbol>& prefix, const PSubs& psubs, const ModCtx& ctx)
        : f(fl), saved(fl.ceval_frame_) {
      f.frames_.push_back(Frame{inst, prefix, psubs, &ctx});
      f.ceval_frame_ = static_cast<int>(f.frames_.size()) - 1;
    }
    ~FramePush() { f.frames_.pop_back(); f.ceval_frame_ = saved; }
    FramePush(const FramePush&) = delete;
    FramePush& operator=(const FramePush&) = delete;
  };
  struct FrameHit {
    enum Kind { NotLocal, Value, Fail } kind = NotLocal;
    CVal v;
  };
  // 实例节点所在的帧（基类实例 = 其派生实例的帧：继承成员的绑定作用域是基类实例，instance_parent 指回派生实例）。
  int frame_of(NodeId n) {
    for (int guard = 0; guard < 64 && n.is_valid() && !I_.nodes[n].is_empty(); ++guard) {
      for (int i = static_cast<int>(frames_.size()) - 1; i >= 0; --i)
        if (frames_[static_cast<std::size_t>(i)].inst.raw == n.raw) return i;
      n = I_.nodes[n].instance_parent;
    }
    return -1;
  }
  // cscope：c 的书写处作用域（求 c 自带的下标用；只有数组参数的下标引用需要）。
  FrameHit frame_value(int fi, const Cref& c, bool allow_param, int depth,
                       std::unordered_set<std::uint32_t>* used, const ScodeScope* cscope = nullptr) {
    FrameHit r;
    if (fi < 0 || depth > 100) return r;
    const NodeId inst = frames_[static_cast<std::size_t>(fi)].inst;
    const NodeId ln = find_local(inst, c.parts[0].name);
    if (!ln.is_valid() || I_.nodes[ln].is_empty() || !I_.nodes[ln].comp.is_valid()) return r;
    const CompId cid = I_.nodes[ln].comp;
    // 有效变率 = 声明变率按本帧实例的下限下沉（`constant R r` 里的 `Real x` 是 constant，variabilityMin；RecordBinding7）。
    const ModCtx* fctx = frames_[static_cast<std::size_t>(fi)].ctx;
    const SVariability var = var_min(fctx ? fctx->var_floor : SVariability::Var, I_.comps[cid].attributes.variability);
    const SElement& cel = I_.scode.elems[I_.comps[cid].def];
    // `Evaluate = false` 的参数是 NON_STRUCTURAL_PARAMETER（updateImplicitVariability，NFInst.mo:3906-3912 v1.25.0），Ceval
    // 不求它（evalCref 的守卫，NFCeval.mo:311-314）：维留成 `Real[p] z`、下标留 `y[p]`，for 方程范围 / 条件组件 / fixed
    // 报错（ND 探针 p1；此前照常求值 → `Real z[1]; Real z[2]` 与 `final parameter`）。
    if (var == SVariability::Param && evaluate_annotation(I_.comps[cid].def) == std::optional<bool>(false)) {
      if (used) used->insert(I_.comps[cid].def.raw);   // 调用方据此认出"非结构参数"（if 方程原样保留条件）
      r.kind = FrameHit::Fail;
      return r;
    }
    const bool arr = !cel.attr.dims.empty() || !cel.type.dims.empty();
    const TypeKind tk = I_.comps[cid].ty.kind;
    const bool scalar_builtin = tk == TypeKind::Integer || tk == TypeKind::Real || tk == TypeKind::Boolean ||
                                tk == TypeKind::String;
    // 内建类型的参数数组（整体 `k` / 带下标 `k[i]`、`k[1:2]`）：求整个数组的值再取下标；标记是组件级的——所有元素一起成为
    // 结构参数（AP 探针：`parameter Integer idx[2] = {2, 1}; Real y = x[idx[1]]` → idx[1]、idx[2] 都 final）。
    const bool arr_param = arr && scalar_builtin && var == SVariability::Param && allow_param && c.parts.size() == 1 &&
                           (c.parts[0].subscripts.empty() || cscope);
    // 多段 / 带下标 / 数组 / 记录 / 枚举常量：交回原有路径（const_vals_ 按已发射值）；同形态的参数在编译期求值语境
    // 未做 → Fail（不落到 SCode 声明默认值——那会忽略修饰）。
    if (!arr_param && (c.parts.size() != 1 || !c.parts[0].subscripts.empty() || arr || !scalar_builtin)) {
      if (var == SVariability::Param && allow_param) r.kind = FrameHit::Fail;
      return r;
    }
    if (var == SVariability::Var || var == SVariability::Discrete) { r.kind = FrameHit::Fail; return r; }
    if (var == SVariability::Param && !allow_param) { r.kind = FrameHit::Fail; return r; }
    if (cel.condition.is_valid()) { r.kind = FrameHit::Fail; return r; }   // 引用条件组件（存在性未定，CyclicBindingConditional）
    if (used && var == SVariability::Param) used->insert(I_.comps[cid].def.raw);
    const Frame& F = frames_[static_cast<std::size_t>(fi)];
    std::vector<Symbol> names = F.prefix;
    names.push_back(c.parts[0].name);
    PSubs isubs = F.psubs;
    isubs.resize(F.prefix.size());
    isubs.push_back({});
    const std::string key = int_key(names, isubs);
    // 结构标记（Structural.markExp / markComponent，v1.25.0 NFStructural.mo:246-326）：被求值式直接引用的参数打标记；它
    // 绑定里的参数（依赖）只在该绑定**尚未定型**时连带（markComponent 只进 Binding.untypedExp，:322-325）——Early 时全都
    // 未定型 → 一路连带；Binding 时看该参数发射了没有（AO 探针 a3：`n = k; s = x[n]` → 只 n final，n 声明在 s 之后则 k 也
    // final）；Late 时全都已定型 → 不连带（`for i in 1:n` → 只 n final）。dep_mark_ = 本层是否还在连带链上。
    const bool marked = dep_mark_ && var == SVariability::Param;
    const int mt = mark_time();
    const bool deps = marked && (mark_phase_ == MarkPhase::Early ||
                                 (mark_phase_ == MarkPhase::Binding && !typed_keys_.count(key)));
    auto hit = [&](const CVal& val) {   // 求到值才标记（求不出的不标，调用方多半走符号路径）
      if (!arr_param) {
        if (marked) record_structural(key);
        r.kind = FrameHit::Value;
        r.v = val;
        return r;
      }
      // 数组：先取下标（下标式在书写处求值，里面的参数照常标记），取得出再给全部元素打标记、记下各元素的值
      //（apply_structural_params 按元素 key 取字面量）。
      std::optional<CVal> ev = val;
      if (!c.parts[0].subscripts.empty()) ev = cval_index(val, c.parts[0].subscripts, 0, *cscope, depth);
      if (!ev) { r.kind = FrameHit::Fail; return r; }
      if (marked) {
        const std::vector<std::int64_t> sh = cval_shape(val);
        std::vector<std::int64_t> ix(sh.size(), 1);
        bool more = !sh.empty();
        for (std::int64_t n : sh) more = more && n >= 1;
        while (more) {
          const CVal* e = &val;
          for (std::int64_t i : ix) e = &(*e->arr)[static_cast<std::size_t>(i - 1)];
          PSubs es = isubs;
          es.back() = ix;
          const std::string ek = int_key(names, es);
          record_structural(ek);
          const_vals_[ek] = *e;
          std::size_t k = sh.size();
          while (k > 0) { --k; if (++ix[k] <= sh[k]) break; ix[k] = 1; if (k == 0) more = false; }
        }
      }
      r.kind = FrameHit::Value;
      r.v = std::move(*ev);
      return r;
    };
    // 值有缓存。依赖还没按不晚于现在的时刻连带过 → 仍走一遍绑定补标记（走不通就用缓存值）。
    std::optional<CVal> cached;
    if (auto it = const_vals_.find(key); it != const_vals_.end()) {
      cached = it->second;
      const auto dt = deps_time_.find(key);
      if (!deps || (dt != deps_time_.end() && dt->second <= mt)) return hit(*cached);
    }
    auto fail = [&]() {
      if (cached) return hit(*cached);
      r.kind = FrameHit::Fail;
      return r;
    };
    if (frame_busy_.count(key)) return fail();
    // 有效绑定：外来修饰（在修饰书写处的帧 / 作用域求值）> 声明绑定（继承成员 = 基类作用域）。
    Id<Exp> raw;
    NodeId scope_node;
    int efi = fi;
    for (Id<SMod> m : F.ctx->mods)
      for (const SSubMod& sm : I_.scode.mods[m].subs)
        if (!raw.is_valid() && sm.ident == c.parts[0].name && I_.scode.mods[sm.mod].binding.is_valid()) {
          raw = I_.scode.mods[sm.mod].binding;
          scope_node = F.ctx->scope;
          efi = frame_of(F.ctx->scope);
        }
    if (!raw.is_valid()) {
      const Binding& b = I_.comps[cid].binding;
      if (b.kind == BindingKind::Raw && b.raw_exp.is_valid()) {
        raw = b.raw_exp;
        scope_node = b.scope.is_valid() && !I_.nodes[b.scope].is_empty() ? b.scope : inst;
      }
    }
    if (!raw.is_valid() || efi < 0) return fail();   // 无绑定参数 / 修饰书写处不在帧栈上
    frame_busy_.insert(key);
    std::optional<CVal> v;
    {
      const int saved = ceval_frame_;
      const bool saved_dep = dep_mark_;
      ceval_frame_ = efi;
      dep_mark_ = deps;
      CevalCtx guard(*this, frames_[static_cast<std::size_t>(efi)].prefix, frames_[static_cast<std::size_t>(efi)].psubs);
      v = eval_scode(raw, scode_scope_of(scope_node), depth + 1, /*allow_param=*/true, used);
      ceval_frame_ = saved;
      dep_mark_ = saved_dep;
    }
    frame_busy_.erase(key);
    if (v && arr_param) {   // 数组：须是数组值，各叶按元素类型强转
      auto conv = [&](auto& self, const CVal& x) -> std::optional<CVal> {
        if (!is_arr(x)) return coerce_to_kind(x, tk);
        if (!x.arr || x.arr->empty()) return std::nullopt;
        std::vector<CVal> out;
        for (const CVal& e : *x.arr) {
          std::optional<CVal> ce = self(self, e);
          if (!ce) return std::nullopt;
          out.push_back(std::move(*ce));
        }
        return CVal::Arr(std::move(out));
      };
      v = is_arr(*v) ? conv(conv, *v) : std::nullopt;
    } else if (v) {
      v = coerce_to_kind(*v, tk);
    }
    if (!v) return fail();
    const_vals_[key] = *v;
    if (deps) {
      const auto dt = deps_time_.find(key);
      if (dt == deps_time_.end()) { txn_touch(); deps_time_.emplace(key, mt); }
      else if (mt < dt->second) { txn_touch(); dt->second = mt; }
    }
    return hit(*v);
  }

  // 函数引用（resolve_fn）与签名（collect_fn_sig）——前置定义，供下面的发射段在声明里用。
  struct FnRecord { Id<SElement> rec; ScodeScope rscope; };   // 函数里记录类型的形参 / 局部 → 其记录类（+ 作用域链）
  // 类（模型 / 函数）的字符串注释。omc 打印时把源码原文（带着转义序列）再转义一遍：`"say \"hi\""` 打成
  // `"say \\\"hi\\\""`、`"a\nb"` 打成 `"a\\nb"`（FC 探针 f1 / f2；变量、方程、语句的注释只转义一遍）。SCode 里存的是解过
  // 转义的文本，原文还原不出来 → 含引号 / 反斜杠（只可能来自转义）的不复刻，SKIP。换行 / 制表符当作源码里直接写的
  //（多行注释很常见：ListMultilineComment、ASSC，omc 照打）；真写成 `\n` 转义的类注释会打错（账本）。
  Symbol class_comment(Symbol c) {
    if (c.is_valid())
      for (char ch : in_.str(c))
        if (ch == '"' || ch == '\\') { set_unsupported("class comment with escapes"); break; }
    return c;
  }
  struct FnRef {
    Id<SElement> fe;
    Id<SElement> named;   // 调用里写的那个类（别名链的头；不是别名时 = fe）
    ScodeScope fscope;
    std::vector<Id<SMod>> alias_mods;
    bool local_to_caller = false;
  };
  struct FnSig {
    std::vector<Id<SElement>> inputs, outputs, locals;
    std::vector<Id<SElement>> all;          // 类树组件序（extends 就地展开后），makeDefaultExternalCall 按它出实参
    const SAlgorithm* alg = nullptr;
    const SExternalDecl* ext = nullptr;     // external 段（整条链至多一个；与算法段互斥）
    Id<SElement> ext_owner;                 // external 段所在类：隐式调用名 = 其类名（instExternalDecl :3227，FunctionSections5）
    bool external = false;
  };

  // external 函数按内建处理（NFFunction.makeAttributes :2584-2593 → SCodeUtil.isBuiltinFunction :2525-2560）：
  //   external "builtin"（调用名缺省 = 类名）；
  //   external "C" 显式 `out = name(in1, …)`，name 属已知数学函数、out 是唯一输出、实参恰为输入按序；
  //   external "C" 无调用名、类名属已知数学函数。
  // 命中 → 调用打内建名（Function.nameConsiderBuiltin :766-778）、不发射函数（NFFunction.new :301 COLLECTED）。常量实参
  // 只对 "builtin" 语言求值：external "C" 的仍算外部函数（Class.isExternalFunction :694-707 只把 "builtin" 除外），
  // SimplifyExp.simplifyCall :182 对外部调用不求值（Function12 `f = Modelica.Math.atan2` → `atan2(...)`、LogCall1
  // `mylog(100)` → `log(100.0)` 不折叠）。ext_c 回报是否 external "C"。
  std::optional<std::string> builtin_equiv(const FnRef& fr, bool* ext_c = nullptr) {
    const SElement& el = I_.scode.elems[fr.fe];
    if (el.restriction != SRestriction::ExternalFunction || !el.def.external.present) return std::nullopt;
    const SExternalDecl& ed = el.def.external;
    const std::string_view lang = ed.lang.is_valid() ? in_.str(ed.lang) : std::string_view("C");
    if (ext_c) *ext_c = lang != "builtin";
    if (lang == "builtin") return std::string(in_.str(ed.func_name.is_valid() ? ed.func_name : el.name));
    if (lang != "C") return std::nullopt;
    static const char* const kKnown[] = {"sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh",
                                         "exp", "log", "log10", "sqrt"};
    auto known = [&](Symbol n) {
      for (const char* k : kKnown) if (in_.str(n) == k) return true;
      return false;
    };
    if (!ed.func_name.is_valid()) return known(el.name) ? std::optional<std::string>(std::string(in_.str(el.name))) : std::nullopt;
    if (!known(ed.func_name) || !ed.output.is_valid()) return std::nullopt;
    FnSig sig;
    std::unordered_set<std::uint32_t> seen;
    if (!collect_fn_sig(fr.fe, fr.fscope, sig, seen, 0) || sig.outputs.size() != 1 || sig.inputs.size() != ed.args.size())
      return std::nullopt;
    auto ident = [&](Id<Exp> x, Symbol want) {
      const Exp& ex = I_.absyn.exps[x];
      if (ex.kind != ExpKind::CrefExp) return false;
      const Cref& c = I_.absyn.crefs[ex.cref];
      return !c.leading_dot && c.parts.size() == 1 && c.parts[0].subscripts.empty() && c.parts[0].name == want;
    };
    if (!ident(ed.output, I_.scode.elems[sig.outputs[0]].name)) return std::nullopt;
    for (std::size_t i = 0; i < ed.args.size(); ++i)
      if (!ident(ed.args[i], I_.scode.elems[sig.inputs[i]].name)) return std::nullopt;
    return std::string(in_.str(ed.func_name));
  }

  // ================= 用户函数发射（加宽(55)-B）=================
  // 依据：NFFunction.lookupFunction :323-350（压平名 = 找到处的作用域链 + 调用名）、fillArgs :1084-1141 / collectArgs
  // :1202-1234 / fillDefaultSlot2 :1261-1303 / evaluateSlotCref :1329-1366（缺省实参 = 输入绑定，其中引用别的输入槽处
  // 代换成该槽实参并按槽类型强转）、sortLocals :2818-2840（局部按依赖拓扑序，Graph.topologicalSort :89-176 的 FIFO Kahn）、
  // checkUseBeforeAssign :2961-3160（用前未赋值 / 输出未赋值告警）、makeAttributes :2626-2633（3.3 起未声明纯度的 external
  // 函数 impure，外部调用名为 ModelicaError 的除外）；NFTyping.typeExp CALL :1424-1433（多输出调用不单独在等式一侧 →
  // TUPLE_ELEMENT 取首输出）、typeSize :2514-2584（已知维求成字面量）、typeFunctionSections / typeExternalArg /
  // makeDefaultExternalCall :2793-2981；NFConvertDAE.convertFunction :1081-1136（inputs, outputs, locals 序）；
  // DAEDumpTpl.dumpFunction :102-135。
  static constexpr std::uint32_t kFnLocal = 0xFFFFFFFEu;   // flat cref.resolved_node 标记：函数形参 / 局部（非模型变量）

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

  // 函数体发射语境（InstContext FUNCTION）：进入时存下并清空模型语境的状态（迭代变量、声明作用域、常量前缀、推导式迭代
  // 变量、解释器环境），退出时恢复。fn_scope_ 非空 = 正在发射函数体。
  struct FnCtxGuard {
    Flattener& f;
    std::unordered_map<std::uint32_t, Type> params;
    std::unordered_set<std::uint32_t> inputs;
    const ScodeScope* scope;
    Id<SElement> home;
    bool impure;
    Id<Expr> end;
    int loops_depth;
    NodeId decl;
    std::unordered_map<std::uint32_t, TypeKind> iters;
    std::unordered_map<std::uint32_t, std::int64_t> loops;
    const std::vector<Symbol>* cpfx;
    PSubs cpsubs;
    std::vector<std::pair<std::uint32_t, CVal>> citers;
    std::unordered_map<std::uint32_t, CVal>* locals;
    std::unordered_set<std::uint32_t>* in_names;
    std::unordered_map<std::uint32_t, FnRecord> records;
    std::unordered_map<std::uint32_t, std::vector<Id<Exp>>> dim_exps;
    std::unordered_map<std::uint32_t, Id<Expr>> iter_labels, loop_labels;
    FnCtxGuard(Flattener& fl, const ScodeScope* sc, Id<SElement> h, bool imp)
        : f(fl), params(std::move(fl.func_params_)), inputs(std::move(fl.fn_inputs_)), scope(fl.fn_scope_),
          home(fl.fn_home_), impure(fl.fn_impure_), end(fl.fn_end_), loops_depth(fl.fn_loops_), decl(fl.decl_scope_),
          iters(std::move(fl.stmt_iters_)), loops(std::move(fl.loop_vars_)), cpfx(fl.ceval_prefix_),
          cpsubs(std::move(fl.ceval_psubs_)), citers(std::move(fl.citers_)), locals(fl.locals_), in_names(fl.input_names_),
          records(std::move(fl.fn_records_)), dim_exps(std::move(fl.fn_dim_exps_)),
          iter_labels(std::move(fl.stmt_iter_labels_)), loop_labels(std::move(fl.loop_labels_)) {
      f.func_params_.clear(); f.fn_inputs_.clear(); f.stmt_iters_.clear(); f.loop_vars_.clear(); f.fn_records_.clear();
      f.fn_dim_exps_.clear(); f.stmt_iter_labels_.clear(); f.loop_labels_.clear();
      f.ceval_psubs_.clear(); f.citers_.clear();
      f.fn_scope_ = sc; f.fn_home_ = h; f.fn_impure_ = imp; f.fn_end_ = Id<Expr>{}; f.fn_loops_ = 0;
      f.decl_scope_ = Inst::empty_node(); f.ceval_prefix_ = nullptr; f.locals_ = nullptr; f.input_names_ = nullptr;
    }
    ~FnCtxGuard() {
      f.func_params_ = std::move(params); f.fn_inputs_ = std::move(inputs); f.fn_scope_ = scope; f.fn_home_ = home;
      f.fn_impure_ = impure; f.fn_end_ = end; f.fn_loops_ = loops_depth; f.decl_scope_ = decl;
      f.stmt_iters_ = std::move(iters);
      f.loop_vars_ = std::move(loops); f.ceval_prefix_ = cpfx; f.ceval_psubs_ = std::move(cpsubs);
      f.citers_ = std::move(citers); f.locals_ = locals; f.input_names_ = in_names; f.fn_records_ = std::move(records);
      f.fn_dim_exps_ = std::move(dim_exps);
      f.stmt_iter_labels_ = std::move(iter_labels); f.loop_labels_ = std::move(loop_labels);
    }
    FnCtxGuard(const FnCtxGuard&) = delete;
    FnCtxGuard& operator=(const FnCtxGuard&) = delete;
  };

  // 函数的压平名（NFFunction.lookupFunction :323-350 + InstNode.scopeList :1212-1265：找到调用名首段处的作用域链（含根类）
  // + 调用名）。
  //   首段在顶层找到 / 前导点 → 调用名本身（YmcFnName1：`Zf`、`P.g`）；
  //   在调用实例链上某实例的类里找到（沿 instance_parent 上溯，基类实例与派生实例同层）→ 根名 + 该层前缀 + 调用名
  //   （YmcFnName1/2：`R.a.h`、继承来的 `R.a2.h`、`R.b.a.h`；根类 → `R.fr`、FuncDefaultArg6 `FuncDefaultArg6.g`）；
  //   在根类里找到（函数体语境：函数体外层是根实例）→ 根名 + 调用名；
  //   在其余外层类（包）里找到 → 该类的类路径 + 调用名（YmcFnName3 `P2.g2`、YmcFnEmit1 函数体里的 `P.b`）。
  //   包嵌在被实例化的类里、根类内部更深的词法层、与正在发射的函数同出一个实例类 → nullopt（实例路径未逐层核实，SKIP）。
  // home：名字来自实例链（非根）时置为该实例的类，供发射其函数体时判断同类调用。
  std::optional<std::vector<Symbol>> fn_flat_name(const Cref& fn, const ScodeScope& scope, NodeId enclosing,
                                                  const std::vector<Symbol>& prefix, Id<SElement>& home) {
    home = Id<SElement>{};
    std::vector<Symbol> parts;
    for (const CrefPart& p : fn.parts) parts.push_back(p.name);
    if (parts.empty()) return std::nullopt;
    ScodeScope ds;
    const Id<SElement> first = find_scode_element_at(parts[0], fn.leading_dot ? ScodeScope{} : scope, ds);
    if (!first.is_valid()) return std::nullopt;
    parts[0] = I_.scode.elems[first].name;   // 重命名 import `import g = P.f` 的 g → 真名 f
    if (ds.empty()) return parts;
    auto qualified = [&](std::vector<Symbol> head) {
      head.insert(head.end(), parts.begin(), parts.end());
      return head;
    };
    const std::uint32_t root_def = I_.nodes[root_].def.raw;
    std::vector<std::pair<std::uint32_t, std::size_t>> chain;   // (实例的类 def raw, 该层前缀长)
    std::size_t plen = prefix.size();
    for (NodeId n = enclosing; n.is_valid() && !I_.nodes[n].is_empty(); n = I_.nodes[n].instance_parent) {
      const INode& nd = I_.nodes[n];
      if (nd.kind == NodeKind::Class) chain.push_back({nd.def.raw, plen});
      else if (nd.kind == NodeKind::Component && plen > 0) --plen;
      else break;
    }
    for (const auto& c : chain)
      if (c.first == ds[0].raw) {
        if (c.first != root_def) home = ds[0];
        std::vector<Symbol> head = result_.model.name;
        head.insert(head.end(), prefix.begin(), prefix.begin() + static_cast<std::ptrdiff_t>(c.second));
        return qualified(std::move(head));
      }
    if (ds[0].raw == root_def) return qualified(result_.model.name);
    if (fn_home_.is_valid() && ds[0].raw == fn_home_.raw) return std::nullopt;
    for (Id<SElement> d : ds) {
      if (d.raw == root_def) return std::nullopt;
      for (const auto& c : chain) if (c.first == d.raw) return std::nullopt;
    }
    std::vector<Symbol> head;
    for (auto it = ds.rbegin(); it != ds.rend(); ++it) head.push_back(I_.scode.elems[*it].name);
    return qualified(std::move(head));
  }

  // 表达式形状：数组字面量逐层数元素；if 表达式两支维不等处为未知（函数里允许，IfExpression11）；其余取类型维（非整数维 =
  // -1）。标量 → {}。
  // 字面量范围（起 / 步 / 止都是数值字面量）：元素数按 getRangeTypeInt（max(intDiv(stop - start, step) + 1, 0)）/
  // getRangeTypeReal（Util.realRangeSize = floor((stop - start) / step + 5e-15) + 1，≥ 0），NFTypeCheck.mo:2758-2833；
  // Util.mo:1324-1333。步长为 0 / 非字面量 → nullopt。第 i 个元素 = 起 + (i - 1)·步（NFRangeIterator）。
  struct RangeLit { bool integer = true; bool boolean = false; double start = 0, step = 1, stop = 0; std::int64_t n = 0; };
  std::optional<RangeLit> range_lit(const Expr& r) {
    if (r.kind != ExprKind::Range) return std::nullopt;
    // Boolean 范围 `false:true`（无步长；false < true，NFTypeCheck.getRangeTypeBool :2864-2908；V4p 探针 `a6 = false:true`、
    // `true:false` 为空）。
    if (!r.c.is_valid() && result_.model.exprs[r.a].kind == ExprKind::BoolLit && result_.model.exprs[r.b].kind == ExprKind::BoolLit) {
      RangeLit l;
      l.boolean = true;
      l.start = result_.model.exprs[r.a].bval ? 1 : 0;
      l.stop = result_.model.exprs[r.b].bval ? 1 : 0;
      l.n = std::max<std::int64_t>(static_cast<std::int64_t>(l.stop - l.start) + 1, 0);
      return l;
    }
    auto num = [&](Id<Expr> x, double& v, bool& is_int) {
      const Expr& e = result_.model.exprs[x];
      if (e.kind == ExprKind::IntLit) { v = static_cast<double>(e.ival); return true; }
      if (e.kind == ExprKind::RealLit) { v = e.rval; is_int = false; return true; }
      return false;
    };
    RangeLit l;
    const Id<Expr> stop_id = r.c.is_valid() ? r.c : r.b;
    if (!num(r.a, l.start, l.integer) || !num(stop_id, l.stop, l.integer)) return std::nullopt;
    if (r.c.is_valid() && !num(r.b, l.step, l.integer)) return std::nullopt;
    if (l.step == 0) return std::nullopt;
    if (l.integer) {
      const auto s = static_cast<std::int64_t>(l.start), t = static_cast<std::int64_t>(l.stop), st = static_cast<std::int64_t>(l.step);
      l.n = std::max<std::int64_t>((t - s) / st + 1, 0);   // intDiv 向零截断
    } else {
      l.n = std::max<std::int64_t>(static_cast<std::int64_t>(std::floor((l.stop - l.start) / l.step + 5e-15)) + 1, 0);
    }
    return l;
  }

  std::vector<std::int64_t> expr_shape(Id<Expr> id) {
    const Expr& e = result_.model.exprs[id];
    if (e.kind == ExprKind::Range) {   // Range 的 ty 是元素类型（迭代变量用），形状另算
      const std::optional<RangeLit> l = range_lit(e);
      return {l ? l->n : -1};
    }
    if (e.kind == ExprKind::Array) {
      // 空数组字面量（`fill(0.0, 0, 2)` 等内建展开产出，类型带全维）：形状取类型维，否则秩丢失。
      if (e.args.empty() && e.ty.is_array()) {
        std::vector<std::int64_t> s;
        for (const Dimension& d : e.ty.dims) s.push_back(d.kind == DimKind::Integer ? d.size : -1);
        return s;
      }
      std::vector<std::int64_t> s{static_cast<std::int64_t>(e.args.size())};
      if (!e.args.empty()) {
        const std::vector<std::int64_t> in = expr_shape(e.args[0]);
        s.insert(s.end(), in.begin(), in.end());
      }
      return s;
    }
    if (e.kind == ExprKind::Unary) return expr_shape(e.a);   // `-{1, 2}`：一元运算的类型随操作数（字面量是粗类型）
    if (e.kind == ExprKind::IfExp) {
      std::vector<std::int64_t> t = expr_shape(e.b);
      const std::vector<std::int64_t> f = expr_shape(e.c);
      if (t.size() != f.size()) { set_unsupported("if-expression branch rank mismatch"); return t; }
      for (std::size_t k = 0; k < t.size(); ++k) if (t[k] != f[k]) t[k] = -1;
      return t;
    }
    std::vector<std::int64_t> s;
    for (const Dimension& d : e.ty.dims) s.push_back(d.kind == DimKind::Integer ? d.size : -1);
    return s;
  }
  // 元素类型：数组字面量 / if 表达式取各叶里"最宽"的数值类型（有 Real 即 Real），其余取类型的元素类型。
  TypeKind expr_elem_kind(Id<Expr> id) {
    const Expr& e = result_.model.exprs[id];
    auto widest = [&](const std::vector<Id<Expr>>& xs) {
      TypeKind k = TypeKind::Unknown;
      for (Id<Expr> x : xs) {
        const TypeKind xk = expr_elem_kind(x);
        if (k == TypeKind::Unknown || (k == TypeKind::Integer && xk == TypeKind::Real)) k = xk;
      }
      return k == TypeKind::Unknown ? TypeKind::Real : k;
    };
    if (e.kind == ExprKind::Array) return e.args.empty() && e.ty.is_array() ? e.ty.element_type().kind : widest(e.args);
    if (e.kind == ExprKind::IfExp) return widest({e.b, e.c});
    return e.ty.element_type().kind;
  }
  // 按目标（形参 / 赋值目标）类型匹配（TypeCheck.matchTypes）：同秩；目标整数维须与已知实参维相等（`:` / 表达式维不查）；
  // 元素同类型或 Integer→Real（标量插 Cast / 字面量折叠，数组字面量逐叶转）。数组非字面量的 Integer→Real（omc 打
  // `/*Real[n]*/(x)`）未做、秩不同（向量化调用）→ invalid。
  // target 按值：调用方可能传 arena 里某表达式的类型，这里会 add（arena 纪律）。
  Id<Expr> coerce_to_param(Id<Expr> a, const Type target) {
    const std::vector<std::int64_t> ash = expr_shape(a);
    if (ash.size() != target.dims.size()) return {};
    for (std::size_t k = 0; k < ash.size(); ++k)
      if (target.dims[k].kind == DimKind::Integer && ash[k] >= 0 && ash[k] != target.dims[k].size) return {};
    const TypeKind tk = target.element_type().kind, ak = expr_elem_kind(a);
    // Real 目标的数组字面量逐叶转（混合 Integer / Real 叶也要转，FuncUnknownDim3 `{time, 1}` → `{time, 1.0}`）。
    if (tk == TypeKind::Real && result_.model.exprs[a].kind == ExprKind::Array && (ak == TypeKind::Real || ak == TypeKind::Integer))
      return coerce_deep(a, TypeKind::Real);
    if (tk == ak) return a;
    if (tk != TypeKind::Real || ak != TypeKind::Integer) return {};
    if (ash.empty()) return to_real(a);
    if (fn_scope_) return fn_real_operand(a);   // fill 调用转填充值；其余 Integer 数组 → invalid
    return {};
  }
  // 常量值 → 类型化字面量：标量走 literal_of（先按 elem 强转），数组逐层建 Array（带数组类型）；记录 / Undef / 空数组 → invalid。
  Id<Expr> cval_expr(const CVal& v, TypeKind elem) {
    if (v.k == CVal::K::Array) {
      if (!v.arr || v.arr->empty()) return {};
      std::vector<Id<Expr>> els;
      for (const CVal& x : *v.arr) {
        const Id<Expr> xe = cval_expr(x, elem);
        if (!xe.is_valid()) return {};
        els.push_back(xe);
      }
      Dimension d;
      d.kind = DimKind::Integer;
      d.size = static_cast<std::int64_t>(els.size());
      Expr r;
      r.kind = ExprKind::Array;
      r.ty = Type::array_of(result_.model.exprs[els[0]].ty, {d});
      r.args = std::move(els);
      return add(std::move(r));
    }
    std::optional<CVal> c = coerce_to_kind(v, elem);
    if (!c || c->k == CVal::K::Record || c->k == CVal::K::Undef || c->k == CVal::K::Array) return {};
    return literal_of(*c);
  }

  // 函数体里的表达式是否引用函数形参 / 局部 / for 迭代变量（Absyn 层按首段名扫）。引用了就不做常量求值：这些量在函数里
  // 不是常量（YmcFnEmit1 探针：局部常量 `c` 保留引用、包常量 `K` 内联成 4.0）。
  bool exp_refs_fn_locals(const Exp& x) {
    if (x.kind == ExpKind::CrefExp) {
      const Cref& c = I_.absyn.crefs[x.cref];
      if (!c.leading_dot && !c.parts.empty() &&
          (func_params_.count(c.parts[0].name.id) || stmt_iters_.count(c.parts[0].name.id)))
        return true;
      for (const CrefPart& p : c.parts)
        for (Id<Exp> s : p.subscripts) if (exp_refs_fn_locals(s)) return true;
      return false;
    }
    if (exp_refs_fn_locals(x.a) || exp_refs_fn_locals(x.b) || exp_refs_fn_locals(x.c)) return true;
    for (Id<Exp> a : x.args) if (exp_refs_fn_locals(a)) return true;
    for (const ForIndex& it : x.iters) if (exp_refs_fn_locals(it.range)) return true;
    return false;
  }
  bool exp_refs_fn_locals(Id<Exp> id) { return id.is_valid() && exp_refs_fn_locals(I_.absyn.exps[id]); }

  // 函数语境的绑定 / 维 / 右端：整式不引用函数局部量且能常量求值 → 字面量（NFEvalConstants；FunctionCompOrder3 `ones(3)` →
  // `{1.0, 1.0, 1.0}`）；否则类型化（函数语境 cref，simplify 折字面量子式）。结果按目标类型匹配（coerce_to_param），
  // 不符 → invalid（调用方视为 omc 类型错误，SKIP）。
  Id<Expr> fn_exp(Id<Exp> raw, const Type target) {
    Id<Expr> e;
    if (!exp_refs_fn_locals(raw))
      if (std::optional<CVal> v = eval_scode(raw, *fn_scope_, 0, /*allow_param=*/false))
        e = cval_expr(*v, target.element_type().kind);
    if (!e.is_valid()) {
      e = simplify_expr(inst_exp(raw, Inst::empty_node(), {}));
      if (result_.unsupported) return {};
    }
    return coerce_to_param(e, target);
  }

  // 维 k 的大小表达式：整数维 → 字面量；`:` / 表达式维 → `size(x, k)`（typeSize :2562-2575）。
  Id<Expr> dim_size_expr(Id<Expr> arr, const Type ty, std::size_t k) {
    if (ty.dims[k].kind == DimKind::Integer) return lit_int(ty.dims[k].size);
    Expr s;
    s.kind = ExprKind::Size;
    s.args = {arr, lit_int(static_cast<std::int64_t>(k) + 1)};
    s.ty = Type::integer();
    return add(std::move(s));
  }
  // 函数形参 / 局部的 cref（kFnLocal 裸名）。标量下标去一维；下标里的 `end` = 所在维大小（End3 `x[end]` → `x[size(x, 1)]`，
  // 已知维 → 字面量）。切片下标照写、留下一维（FS 探针 t1）：`:` 留原维；范围 `a:b` / `a:s:b` 的界都是字面量 → 已知大小，
  // 否则未知（`u[1:k]`、`u[1:size(u, 1)]`）；恰是 `1:该维大小` 的打成 `:`（`w[1:3]`、`w[1:end]` → `w[:]`，同模型里的
  // simplifySlice）；下标数组 `{1, 3}` 留其元素个数。
  Id<Expr> fn_local_cref(const CrefPart& part, const Type ty) {
    Expr whole;
    whole.kind = ExprKind::Cref;
    whole.cref.resolved_node = kFnLocal;
    whole.ty = ty;
    whole.cref.parts.push_back({part.name, {}});
    const Id<Expr> wid = add(std::move(whole));
    if (part.subscripts.empty()) return wid;
    if (part.subscripts.size() > ty.dims.size()) { set_unsupported("subscript on non-array"); return lit_int(0); }
    std::vector<Id<Expr>> subs;
    std::vector<Dimension> rest;   // 结果的维：切片留下的（按出现序）+ 没给下标的尾维
    auto wild = [&] {
      Expr w;
      w.kind = ExprKind::Wild;
      w.ty = Type::integer();
      return add(std::move(w));
    };
    const std::size_t nsub = part.subscripts.size();
    for (std::size_t k = 0; k < nsub; ++k) {
      if (I_.absyn.exps[part.subscripts[k]].kind == ExpKind::Colon) {
        subs.push_back(wild());
        rest.push_back(ty.dims[k]);
        continue;
      }
      const Id<Expr> saved = fn_end_;
      fn_end_ = dim_size_expr(wid, ty, k);
      const Id<Expr> s = simplify_expr(inst_exp(part.subscripts[k], Inst::empty_node(), {}));
      fn_end_ = saved;
      if (result_.unsupported) return lit_int(0);
      const Expr se = result_.model.exprs[s];   // 拷贝
      if (se.kind == ExprKind::Range) {
        if (se.ty.kind != TypeKind::Integer) { set_unsupported("function subscript type"); return lit_int(0); }
        Dimension d;
        d.kind = DimKind::Unknown;
        const std::optional<RangeLit> l = range_lit(se);
        if (l) {
          if (l->n < 1) { set_unsupported("empty function array slice"); return lit_int(0); }
          d.kind = DimKind::Integer;
          d.size = l->n;
        }
        // 整段范围 → `:`。下标少于维数时 omc 拿来比的是**尾部**对应的维（List.lastN，同模型里的 slice_subscript）：
        // 与真实的维不一致的不复刻。
        const Dimension& cmp = ty.dims[ty.dims.size() - nsub + k];
        const bool full = l && l->start == 1 && l->step == 1 && cmp.kind == DimKind::Integer && l->n == cmp.size;
        if (full && !(ty.dims[k].kind == DimKind::Integer && ty.dims[k].size == cmp.size)) {
          set_unsupported("partial slice printed as whole by omc");
          return lit_int(0);
        }
        subs.push_back(full ? wild() : s);
        rest.push_back(d);
        continue;
      }
      if (se.kind == ExprKind::Array) {   // 下标数组：元素须是 Integer 标量
        for (Id<Expr> x : se.args)
          if (result_.model.exprs[x].ty.kind != TypeKind::Integer || result_.model.exprs[x].kind == ExprKind::Array) {
            set_unsupported("function subscript type");
            return lit_int(0);
          }
        if (se.args.empty()) { set_unsupported("empty function array slice"); return lit_int(0); }
        Dimension d;
        d.kind = DimKind::Integer;
        d.size = static_cast<std::int64_t>(se.args.size());
        subs.push_back(s);
        rest.push_back(d);
        continue;
      }
      // 下标是 Integer 向量（变量 / 切片，`w[idx]`、`u[n2]`）：照写，留下它的那一维（FS 探针 t2）。
      if (se.kind == ExprKind::Cref && se.ty.is_array() && se.ty.dims.size() == 1 &&
          se.ty.element_type().kind == TypeKind::Integer) {
        subs.push_back(s);
        rest.push_back(se.ty.dims[0]);
        continue;
      }
      if (se.ty.kind != TypeKind::Integer || se.ty.is_array()) { set_unsupported("function subscript type"); return lit_int(0); }
      subs.push_back(s);
    }
    Expr r;
    r.kind = ExprKind::Cref;
    r.cref.resolved_node = kFnLocal;
    rest.insert(rest.end(), ty.dims.begin() + static_cast<std::ptrdiff_t>(nsub), ty.dims.end());
    r.ty = rest.empty() ? ty.element_type() : Type::array_of(ty.element_type(), rest);
    r.cref.parts.push_back({part.name, std::move(subs)});
    return add(std::move(r));
  }
  // 函数体里的非局部名：包常量（按函数定义处作用域解析，内联其值，YmcFnEmit1 `K` → 4.0）；其余（模型变量、time…）函数里
  // 不可引用 → unsupported（omc 报错，Time3）。
  Id<Expr> fn_outer_cref(const Cref& c) {
    std::vector<Symbol> names;
    bool plain = true;
    for (const CrefPart& p : c.parts) {
      if (!p.subscripts.empty()) plain = false;
      names.push_back(p.name);
    }
    Id<SElement> comp;
    ScodeScope cscope;
    if (plain && resolve_qual_const(names, c.leading_dot, *fn_scope_, comp, cscope)) {
      const Id<SMod> m = I_.scode.elems[comp].mod;
      TypeKind tk;
      if (m.is_valid() && I_.scode.mods[m].binding.is_valid() && builtin_scalar_type_kind(I_.scode.elems[comp].type.path, tk))
        if (std::optional<CVal> v = eval_scode(I_.scode.mods[m].binding, cscope, 0))
          if (Id<Expr> e = cval_expr(*v, tk); e.is_valid()) return e;
    }
    set_unsupported("function body references non-local");
    return lit_int(0);
  }

  // 表达式里有界不是整数字面量 / end 的切片下标（`x[1:n]`、`x[1:c]`）。
  bool has_nonliteral_slice(Id<Exp> id) {
    if (!id.is_valid()) return false;
    const Exp& x = I_.absyn.exps[id];
    if (x.kind == ExpKind::CrefExp) {
      for (const CrefPart& p : I_.absyn.crefs[x.cref].parts)
        for (Id<Exp> s : p.subscripts) {
          const Exp& sx = I_.absyn.exps[s];
          if (sx.kind == ExpKind::Range)
            for (Id<Exp> b : {sx.a, sx.b, sx.c})
              if (b.is_valid() && I_.absyn.exps[b].kind != ExpKind::IntLit && I_.absyn.exps[b].kind != ExpKind::End) return true;
          if (has_nonliteral_slice(s)) return true;
        }
      return false;
    }
    if (has_nonliteral_slice(x.a) || has_nonliteral_slice(x.b) || has_nonliteral_slice(x.c)) return true;
    for (Id<Exp> a : x.args) if (has_nonliteral_slice(a)) return true;
    return false;
  }
  // size(a, k) / size(a)（NFTyping.typeSize :2514-2584）：已知维 → 字面量；函数里的 `:` / 表达式维 → `size(a, k)` 表达式。
  // size(a) → {size(a, 1), …}。k 非常量 / 越界 / a 非数组 → unsupported（omc 报错）。
  Id<Expr> size_call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    for (Symbol n : e.arg_names) if (n.is_valid()) { set_unsupported("named function argument"); return lit_int(0); }
    if (e.args.empty() || e.args.size() > 2) { set_unsupported("function arity"); return lit_int(0); }
    // 模型里切片的界不是字面量（参数 `x[1:n]`，连 constant 的 `x[1:c]` 也是）：1.25.0 把该维当表达式维，size 原样留下、里面的
    // 参数也不代入（AO 探针 a4：`Integer s = size(x[1:n], 1)` 原样、`size(x[1:n]) * {1}` → `n`），不复刻 → SKIP。
    if (!fn_scope_ && has_nonliteral_slice(e.args[0])) { set_unsupported("size of non-literal slice"); return lit_int(0); }
    // size(T, 1)，T 是类型名（Boolean / 枚举）：其取值个数（DM 探针 `Integer n = size(E, 1)` → `n = 3`）；别的维号 omc 报错。
    if (!fn_scope_ && e.args.size() == 2)
      if (const std::optional<std::vector<Id<Expr>>> labels = classify_type_dim(e.args[0], enclosing, prefix)) {
        const Id<Expr> k = simplify_expr(inst_exp(e.args[1], enclosing, prefix));
        if (result_.unsupported) return lit_int(0);
        if (result_.model.exprs[k].kind != ExprKind::IntLit || result_.model.exprs[k].ival != 1) {
          set_unsupported("size index out of range");
          return lit_int(0);
        }
        return lit_int(static_cast<std::int64_t>(labels->size()));
      }
    Id<Expr> a = inst_exp(e.args[0], enclosing, prefix);
    if (result_.unsupported) return lit_int(0);
    // 函数体里实参照常化简定形（SZ 探针 s1：`size(2 * u, 1)` → `size(u * 2.0, 1)`，此前原样留成 `2.0 * u`）。
    if (fn_scope_) a = simplify_expr(a);
    // 形状看穿数组字面量 / 范围的粗类型（`size({1, 2, 3}, 1)` → 3、`size(1:5, 1)` → 5；此前字面量被当成"不是数组"）。
    const std::vector<std::int64_t> sh = expr_shape(a);
    if (result_.unsupported) return lit_int(0);
    if (sh.empty()) { set_unsupported("size of non-array"); return lit_int(0); }
    auto size_expr = [&](std::vector<Id<Expr>> args, Type ty) {
      Expr sx;
      sx.kind = ExprKind::Size;
      sx.args = std::move(args);
      sx.ty = std::move(ty);
      return add(std::move(sx));
    };
    auto dim = [&](std::size_t k) {   // 已知维 → 字面量；未知维（函数形参 `Real[:] u`）→ `size(a, k)`
      return sh[k] >= 0 ? lit_int(sh[k]) : size_expr({a, lit_int(static_cast<std::int64_t>(k) + 1)}, Type::integer());
    };
    if (e.args.size() == 2) {
      const Id<Expr> k = simplify_expr(inst_exp(e.args[1], enclosing, prefix));
      if (result_.unsupported) return lit_int(0);
      if (result_.model.exprs[k].kind != ExprKind::IntLit) {
        // 维号不是常量（参数 / 变量）：omc 原样留调用、参数也不标结构（SZ 探针 `size(m, d)`）。只收整体引用的数组。
        const Expr ae = result_.model.exprs[a];   // 拷贝
        bool plain = ae.kind == ExprKind::Cref && result_.model.exprs[k].ty.kind == TypeKind::Integer;
        for (const ExprCrefPart& cp : ae.cref.parts) plain = plain && cp.subscripts.empty();
        if (!plain) { set_unsupported("size index not constant"); return lit_int(0); }
        return size_expr({a, k}, Type::integer());
      }
      const std::int64_t ki = result_.model.exprs[k].ival;
      if (ki < 1 || static_cast<std::size_t>(ki) > sh.size()) { set_unsupported("size index out of range"); return lit_int(0); }
      // 数组字面量的内层维未知（`{u, u}`，u: Real[:]）：omc 只认它的第 1 维，别的维号报 "Invalid index 2 in call to
      // size of {u, u}, valid index interval is [1,1]"（SZ 探针 s2）→ SKIP。
      if (result_.model.exprs[a].kind == ExprKind::Array && sh[static_cast<std::size_t>(ki - 1)] < 0) {
        set_unsupported("size of array literal with unknown inner dimension");
        return lit_int(0);
      }
      return dim(static_cast<std::size_t>(ki - 1));
    }
    // size(a)：函数体里有未知维时 omc 整个留着（`n := size(u)`，此前拆成 `{size(u, 1), size(u, 2)}`）。
    if (fn_scope_ && std::find(sh.begin(), sh.end(), -1) != sh.end()) {
      Dimension rd;
      rd.kind = DimKind::Integer;
      rd.size = static_cast<std::int64_t>(sh.size());
      return size_expr({a}, Type::array_of(Type::integer(), {rd}));
    }
    std::vector<Id<Expr>> els;
    for (std::size_t k = 0; k < sh.size(); ++k) els.push_back(dim(k));
    Dimension nd;
    nd.kind = DimKind::Integer;
    nd.size = static_cast<std::int64_t>(els.size());
    Expr arr;
    arr.kind = ExprKind::Array;
    arr.ty = Type::array_of(Type::integer(), {nd});
    arr.args = std::move(els);
    return add(std::move(arr));
  }

  // scalar(a)（NFBuiltinCall.typeScalarCall :1036-1085）：已知维须全为 1（否则 INVALID_ARRAY_DIM_IN_SCALAR_OP）；实参能
  // 展开（数组字面量）→ 其唯一元素（FuncBuiltinScalar2 `scalar(size(u))` → `size(u, 1)`，YmcFnArr1 `scalar({x[1]})` →
  // `x[1]`）。不能展开的留 `scalar(a)` 调用（未核实打印）→ unsupported。
  Id<Expr> scalar_call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    for (Symbol n : e.arg_names) if (n.is_valid()) { set_unsupported("named function argument"); return lit_int(0); }
    if (e.args.size() != 1) { set_unsupported("function arity"); return lit_int(0); }
    Id<Expr> a = inst_exp(e.args[0], enclosing, prefix);
    if (result_.unsupported) return lit_int(0);
    for (std::int64_t n : expr_shape(a))
      if (n >= 0 && n != 1) { set_unsupported("scalar of non-singleton array"); return lit_int(0); }
    // 整个留着的 `size(u)`（函数体里维未知，见 size_call）：唯一的元素 = `size(u, 1)`（FuncBuiltinScalar2）。
    if (const Expr ax = result_.model.exprs[a]; ax.kind == ExprKind::Size && ax.args.size() == 1) {
      Expr sx;
      sx.kind = ExprKind::Size;
      sx.args = {ax.args[0], lit_int(1)};
      sx.ty = Type::integer();
      return add(std::move(sx));
    }
    if (result_.model.exprs[a].kind != ExprKind::Array) { set_unsupported("scalar of unexpanded array"); return lit_int(0); }
    while (result_.model.exprs[a].kind == ExprKind::Array && result_.model.exprs[a].args.size() == 1)
      a = result_.model.exprs[a].args[0];
    if (result_.model.exprs[a].kind == ExprKind::Array || result_.model.exprs[a].ty.is_array()) {
      set_unsupported("scalar of unexpanded array");
      return lit_int(0);
    }
    return a;
  }
  // 函数语境的 fill / zeros / ones（typeFillCall2 :938-1005）：zeros / ones = fill(0 / 1, …)，打印成 fill（FILL_FUNC；
  // YmcFnArr1 探针 `ones(n)` → `fill(1, n)`）；维实参须 Integer（常量 → 整数维，其余 → 表达式维，函数里不要求参数变率）。
  Id<Expr> fill_call(const Exp& e, std::string_view name) {
    for (Symbol n : e.arg_names) if (n.is_valid()) { set_unsupported("named function argument"); return lit_int(0); }
    std::size_t first_dim = 0;
    Id<Expr> val;
    if (name == "fill") {
      if (e.args.size() < 2) { set_unsupported("function arity"); return lit_int(0); }
      val = simplify_expr(inst_exp(e.args[0], Inst::empty_node(), {}));
      first_dim = 1;
    } else {
      if (e.args.empty()) { set_unsupported("function arity"); return lit_int(0); }
      val = lit_int(name == "ones" ? 1 : 0);
    }
    if (result_.unsupported) return lit_int(0);
    if (op_type(val).is_array()) { set_unsupported("fill of array value"); return lit_int(0); }
    std::vector<Id<Expr>> cargs{val};
    std::vector<Dimension> ds;
    for (std::size_t i = first_dim; i < e.args.size(); ++i) {
      const Id<Expr> x = simplify_expr(inst_exp(e.args[i], Inst::empty_node(), {}));
      if (result_.unsupported) return lit_int(0);
      if (result_.model.exprs[x].ty.kind != TypeKind::Integer) { set_unsupported("fill dimension type"); return lit_int(0); }
      Dimension d;
      if (result_.model.exprs[x].kind == ExprKind::IntLit) { d.kind = DimKind::Integer; d.size = result_.model.exprs[x].ival; }
      else { d.kind = DimKind::Exp; d.exp = x; }
      cargs.push_back(x);
      ds.push_back(d);
    }
    const Type vty = result_.model.exprs[val].ty;
    Expr r;
    r.kind = ExprKind::Call;
    r.call_name = {in_.intern("fill")};
    r.args = std::move(cargs);
    r.ty = Type::array_of(vty, std::move(ds));
    return add(std::move(r));
  }
  // 函数语境的数组归约 sum / product / min / max(a)（a 为数组）：符号调用，结果 = 元素类型（YmcFnArr1 `sum(y)`）。
  // 实参不是数组 → invalid（调用方落回标量内建路径）。
  Id<Expr> array_reduce_call(const Exp& e, std::string_view name) {
    for (Symbol n : e.arg_names) if (n.is_valid()) return {};
    if (I_.absyn.exps[e.args[0]].kind == ExpKind::Reduction) return {};   // sum(e for i in r) 未做
    Id<Expr> a = simplify_expr(inst_exp(e.args[0], Inst::empty_node(), {}));
    if (result_.unsupported) return lit_int(0);
    const Type at = op_type(a);
    if (!at.is_array()) return {};
    const TypeKind ek = at.element_type().kind;
    if (ek != TypeKind::Integer && ek != TypeKind::Real) { set_unsupported("non-numeric array reduction"); return lit_int(0); }
    // sum / product 的实参可展开（维全已知）→ omc 展开成元素的 + / * 链（simplifySumProduct，NFSimplifyExp.mo:445-481：
    // ExpandExp.expand 成功即展开，空数组 = 0 / 1；FuncVectorization2 函数体 `o := b + sum(a)`、a: Integer[4] →
    // `b + a[1] + a[2] + a[3] + a[4]`）。只做整体引用的局部数组；其余可展开的实参形态未核实 → SKIP。维未知（`Real[:]`）保留
    // 调用。min / max 不展开（simplifyMinMax :332-355）。
    if (name == "sum" || name == "product") {
      const std::vector<std::int64_t> sh = expr_shape(a);
      bool known = !sh.empty();
      for (std::int64_t n : sh) known = known && n >= 0;
      if (known) {
        // 能按元素展开的实参（fn_expand：局部数组的整体 / 切片引用、化简时已展开成字面量的算术式、数组 if、整体转型；FS
        // 探针 t1–t4：`sum(u[1:2])` → `u[1] + u[2]`、`sum(2 * w[1:2])` → `2.0 * w[1] + 2.0 * w[2]`）。其余形态未核实。
        a = fn_expand(a);
        if (!a.is_valid()) { set_unsupported("function sum/product of known-size expression"); return lit_int(0); }
        const bool is_sum = name == "sum";
        Id<Expr> acc;
        std::vector<std::int64_t> ix(sh.size(), 1);
        for (bool more = std::find(sh.begin(), sh.end(), 0) == sh.end(); more;) {
          const Id<Expr> x = scalarize(a, ix);
          if (result_.unsupported) return lit_int(0);
          if (!acc.is_valid()) {
            acc = x;
          } else {
            Expr b;
            b.kind = ExprKind::Binary;
            b.bin = is_sum ? BinOp::Add : BinOp::Mul;
            b.a = acc;
            b.b = x;
            b.ty = Type::of(ek);
            acc = add(std::move(b));
          }
          std::size_t k = sh.size();
          while (k > 0) { --k; if (++ix[k] <= sh[k]) break; ix[k] = 1; if (k == 0) more = false; }
        }
        if (acc.is_valid()) return acc;
        const std::int64_t unit = is_sum ? 0 : 1;
        return ek == TypeKind::Real ? lit_real(static_cast<double>(unit)) : lit_int(unit);
      }
    }
    Expr r;
    r.kind = ExprKind::Call;
    r.call_name = {in_.intern(name)};
    r.args = {a};
    r.ty = Type::of(ek);
    return add(std::move(r));
  }

  // flat 表达式里引用到的函数局部名（kFnLocal cref 首段）。
  void collect_fn_refs(Id<Expr> id, std::unordered_set<std::uint32_t>& out) {
    if (!id.is_valid()) return;
    const Expr& x = result_.model.exprs[id];
    if (x.kind == ExprKind::Cref && x.cref.resolved_node == kFnLocal && !x.cref.parts.empty())
      out.insert(x.cref.parts[0].name.id);
    for (const ExprCrefPart& p : x.cref.parts) for (Id<Expr> s : p.subscripts) collect_fn_refs(s, out);
    collect_fn_refs(x.a, out);
    collect_fn_refs(x.b, out);
    collect_fn_refs(x.c, out);
    for (Id<Expr> a : x.args) collect_fn_refs(a, out);
  }
  // 整数求值（调用处输出维）：整数字面量、已知形状实参的 size(a, k)、整数四则。其余 → nullopt。
  std::optional<std::int64_t> fold_int(Id<Expr> id) {
    const Expr x = result_.model.exprs[id];   // 拷贝（expr_shape 不 add，但保守）
    switch (x.kind) {
      case ExprKind::IntLit: return x.ival;
      case ExprKind::Size: {
        if (x.args.size() != 2) return std::nullopt;
        const std::optional<std::int64_t> k = fold_int(x.args[1]);
        const std::vector<std::int64_t> sh = expr_shape(x.args[0]);
        if (!k || *k < 1 || static_cast<std::size_t>(*k) > sh.size() || sh[*k - 1] < 0) return std::nullopt;
        return sh[*k - 1];
      }
      case ExprKind::Binary: {
        const std::optional<std::int64_t> a = fold_int(x.a), b = fold_int(x.b);
        if (!a || !b) return std::nullopt;
        if (x.bin == BinOp::Add) return *a + *b;
        if (x.bin == BinOp::Sub) return *a - *b;
        if (x.bin == BinOp::Mul) return *a * *b;
        return std::nullopt;
      }
      default: return std::nullopt;
    }
  }

  // 缺省实参代换（evaluateSlotCref）：把引用输入槽的 kFnLocal cref 换成 slot_arg 给的实参；非槽的函数局部引用 / 带下标的
  // 槽引用 → ok=false（SKIP）。模型变量 cref（别名修饰给的默认，FuncDefaultArg6 的 `x`）原样。
  Id<Expr> subst_slots(Id<Expr> id, const std::function<Id<Expr>(Symbol)>& slot_arg, bool& ok) {
    const Expr x = result_.model.exprs[id];   // 拷贝：下面会 add
    if (x.kind == ExprKind::Cref && x.cref.resolved_node == kFnLocal) {
      if (x.cref.parts.size() != 1 || !x.cref.parts[0].subscripts.empty()) { ok = false; return id; }
      const Id<Expr> a = slot_arg(x.cref.parts[0].name);
      if (!a.is_valid()) { ok = false; return id; }
      return a;
    }
    Expr y = x;
    bool changed = false;
    auto sub = [&](Id<Expr>& c) {
      if (!c.is_valid()) return;
      const Id<Expr> n = subst_slots(c, slot_arg, ok);
      if (n.raw != c.raw) { c = n; changed = true; }
    };
    sub(y.a);
    sub(y.b);
    sub(y.c);
    for (Id<Expr>& a : y.args) sub(a);
    for (ExprCrefPart& p : y.cref.parts) for (Id<Expr>& s : p.subscripts) sub(s);
    return changed ? add(std::move(y)) : id;
  }

  // 用前未赋值（checkUseBeforeAssign :2961-3160）：输出 / 局部里无绑定的内建标量从"未赋值"起步，按语句序走——右端 / 条件
  // 用到未赋值者 → WARNING_DEF_USE；赋值目标（末段节点）出列；if 各支分别走、任一支赋过即视为赋过；for / while 体走一遍。
  // 走完仍未赋值的输出 → UNASSIGNED_FUNCTION_OUTPUT。有任一告警 → true（ymc 不产告警文本 → 调用方 SKIP）。
  bool fn_uninit_warns(const FlatFunction& ff) {
    std::vector<std::uint32_t> un;
    std::unordered_set<std::uint32_t> outputs;
    for (const FlatFuncParam& p : ff.params) {
      if (p.direction == FlatDirection::Input) continue;
      if (p.direction == FlatDirection::Output) outputs.insert(p.name.id);
      if (!p.ty.is_array() && p.type_path.empty() && !p.binding.is_valid()) un.push_back(p.name.id);   // 只追内建标量
    }
    bool warn = false;
    auto use = [&](Id<Expr> e, std::vector<std::uint32_t>& u) {
      std::unordered_set<std::uint32_t> refs;
      collect_fn_refs(e, refs);
      for (auto it = u.begin(); it != u.end();)
        if (refs.count(*it)) { warn = true; it = u.erase(it); } else ++it;
    };
    auto assign = [&](Id<Expr> lhs, std::vector<std::uint32_t>& u) {
      const Expr& l = result_.model.exprs[lhs];
      if (l.kind != ExprKind::Cref || l.cref.parts.size() != 1) return;   // 记录字段赋值不动（记录不在追踪表里）
      u.erase(std::remove(u.begin(), u.end(), l.cref.parts.back().name.id), u.end());
    };
    auto walk = [&](auto& self, const std::vector<Id<FlatStatement>>& body, std::vector<std::uint32_t>& u) -> void {
      for (Id<FlatStatement> sid : body) {
        const FlatStatement& s = result_.model.stmts[sid];
        switch (s.kind) {
          case FlatStmtKind::Assign: use(s.b, u); assign(s.a, u); break;
          case FlatStmtKind::For: use(s.a, u); self(self, s.body, u); break;
          case FlatStmtKind::While: use(s.a, u); self(self, s.body, u); break;
          case FlatStmtKind::Assert: use(s.a, u); use(s.b, u); if (s.c.is_valid()) use(s.c, u); break;
          case FlatStmtKind::If: {
            for (const FlatStmtBranch& br : s.branches) use(br.cond, u);
            std::unordered_set<std::uint32_t> gone;
            auto branch = [&](const std::vector<Id<FlatStatement>>& b) {
              std::vector<std::uint32_t> cu = u;
              self(self, b, cu);
              for (std::uint32_t v : u) if (std::find(cu.begin(), cu.end(), v) == cu.end()) gone.insert(v);
            };
            for (const FlatStmtBranch& br : s.branches) branch(br.body);
            branch(s.else_body);
            u.erase(std::remove_if(u.begin(), u.end(), [&](std::uint32_t v) { return gone.count(v) > 0; }), u.end());
            break;
          }
          default: break;
        }
      }
    };
    walk(walk, ff.body, un);
    for (std::uint32_t v : un) if (outputs.count(v)) warn = true;
    return warn;
  }

  // 函数体语句：赋值（目标须是输出 / 局部，可带下标）、if、for（显式范围）、while、return、break、assert（无级别）。
  // 其余 → unsupported（SKIP）。
  Id<FlatStatement> collect_fn_stmt(Id<SStatement> sid) {
    const SStatement& st = I_.scode.stmts[sid];   // SCode 只读（flatten 期不增），引用稳定
    FlatStatement fs;
    fs.loc = st.loc;
    if (st.cmt.str.is_valid()) fs.comment = st.cmt.str;
    auto body = [&](const std::vector<Id<SStatement>>& in, std::vector<Id<FlatStatement>>& out) {
      for (Id<SStatement> b : in) {
        const Id<FlatStatement> s = collect_fn_stmt(b);
        if (result_.unsupported) return false;
        if (s.is_valid()) out.push_back(s);   // invalid = 删去的语句（实参全是常量的无返回值调用）
      }
      return true;
    };
    auto cond = [&](Id<Exp> c) {
      const Id<Expr> x = fn_exp(c, Type::boolean());
      if (!x.is_valid()) set_unsupported("function condition type");
      return x;
    };
    switch (st.kind) {
      case SStmtKind::Assign: {
        const Exp& le = I_.absyn.exps[st.a];
        // 目标：输出 / 局部（可带下标）；或记录输出 / 局部的字段 `r.a`（RF2 探针 `r.a := x`）。
        bool target_ok = false;
        if (le.kind == ExpKind::CrefExp) {
          const Cref& tc = I_.absyn.crefs[le.cref];
          const std::uint32_t head = tc.parts[0].name.id;
          if (tc.parts.size() == 1)
            target_ok = func_params_.count(head) && !fn_inputs_.count(head);
          else if (tc.parts.size() == 2)
            target_ok = fn_records_.count(head) && !fn_inputs_.count(head) && tc.parts[0].subscripts.empty() &&
                        tc.parts[1].subscripts.empty();
        }
        if (!target_ok) { set_unsupported("function assignment target"); return {}; }   // 元组 / 输入 / 非局部目标
        const Id<Expr> lhs = inst_exp(st.a, Inst::empty_node(), {});
        if (result_.unsupported) return {};
        const Type lty = result_.model.exprs[lhs].ty;   // 拷贝：fn_exp 会往 arena add（不跨 add 持引用）
        const Id<Expr> rhs = fn_exp(st.b, lty);
        if (result_.unsupported) return {};
        if (!rhs.is_valid()) { set_unsupported("function assignment type mismatch"); return {}; }
        // 整记录赋值 `r := R(…)` / `r := r2`：右侧须是同一记录类（构造调用的实例类定义 / 记录局部）——coerce_to_param 只比
        // TypeKind，这里补比记录类（RF1 探针）；查不出 / 不同类（要 typeCastRecord）→ SKIP。
        if (lty.kind == TypeKind::Complex) {
          const std::uint32_t ld = record_def_of(lhs);
          if (ld == Id<SElement>::kInvalid || record_def_of(rhs) != ld) { set_unsupported("function record assignment type"); return {}; }
        }
        fs.kind = FlatStmtKind::Assign;
        fs.a = lhs;
        fs.b = rhs;
        break;
      }
      case SStmtKind::If:
        fs.kind = FlatStmtKind::If;
        for (const SStmtBranch& br : st.branches) {
          FlatStmtBranch fb;
          fb.cond = cond(br.cond);
          if (result_.unsupported || !body(br.body, fb.body)) return {};
          fs.branches.push_back(std::move(fb));
        }
        if (!body(st.else_body, fs.else_body)) return {};
        break;
      case SStmtKind::For: {
        Id<Exp> ra = st.a;
        if (!ra.is_valid()) {
          // 隐式范围（见 implicit_range）：`for i in 1:size(u, 1) loop`（`:` 维）、`1:3`、`1:k`（表达式维）。
          IterUses u;
          iter_uses(st.body, st.index, u);
          ra = implicit_range(u, Inst::empty_node(), {});
          if (!ra.is_valid()) { set_unsupported("implicit-range for statement"); return {}; }
        }
        const Id<Expr> range = simplify_expr(inst_exp(ra, Inst::empty_node(), {}));
        if (result_.unsupported) return {};
        const std::uint32_t id = st.index.id;   // 迭代变量（符号，嵌套可遮蔽）
        const bool had = stmt_iters_.count(id) > 0;
        const TypeKind prev = had ? stmt_iters_[id] : TypeKind::Integer;
        stmt_iters_[id] = expr_elem_kind(range);
        fs.kind = FlatStmtKind::For;
        fs.index = st.index;
        fs.a = range;
        ++fn_loops_;
        const bool ok = body(st.body, fs.body);
        --fn_loops_;
        if (had) stmt_iters_[id] = prev; else stmt_iters_.erase(id);
        if (!ok) return {};
        // 空体 / 范围已知为空的 for 整条删去（simplifyStatement，NFSimplifyModel.mo:312-333；函数体同样经它化简 :621）。
        if (st.body.empty() || range_known_empty(range)) return {};
        break;
      }
      case SStmtKind::While: {
        fs.kind = FlatStmtKind::While;
        fs.a = cond(st.a);
        if (result_.unsupported) return {};
        ++fn_loops_;
        const bool ok = body(st.body, fs.body);
        --fn_loops_;
        if (!ok) return {};
        break;
      }
      case SStmtKind::Return: fs.kind = FlatStmtKind::Return; break;
      case SStmtKind::Break:   // 循环外的 break → BREAK_OUTSIDE_LOOP（NFInst.instStatement :3664-3670，Break2）
        if (fn_loops_ == 0) { set_unsupported("break outside loop"); return {}; }
        fs.kind = FlatStmtKind::Break;
        break;
      case SStmtKind::Assert:
        // level：两参形式是共享的缺省 `.AssertionLevel.error`（不打印）；显式的只收字面量 `AssertionLevel.warning / error`。
        if (st.c.is_valid() && st.c.raw != I_.scode.assertion_level_error.raw) {
          const Exp& le = I_.absyn.exps[st.c];
          if (le.kind == ExpKind::CrefExp) fs.c = assertion_level_lit(I_.absyn.crefs[le.cref]);
          if (!fs.c.is_valid()) { set_unsupported("function assert level"); return {}; }
        }
        fs.kind = FlatStmtKind::Assert;
        fs.a = cond(st.a);
        if (result_.unsupported) return {};
        fs.b = fn_exp(st.b, Type::string());
        if (!fs.b.is_valid()) { set_unsupported("function assert message type"); return {}; }
        break;
      case SStmtKind::NoRetCall:
        fs.kind = FlatStmtKind::NoRetCall;
        fs.a = noret_call(st.a, Inst::empty_node(), {});
        if (!fs.a.is_valid()) return {};   // 删去的语句 / unsupported
        break;
      default:
        set_unsupported("function statement");
        return {};
    }
    return result_.model.add_stmt(std::move(fs));
  }

  // external 段（typeFunctionSections :2826-2833）。显式调用：实参只许 cref / size(cref, 常量)（不求值，typeExternalArg
  // evaluate=false）/ 标量常量（求值），输出须是标量（checkExternalCallResult）。隐式（makeDefaultExternalCall :2900-2981）：
  // 调用名 = external 段所在类名（FunctionSections5 `base_f`）；单个非数组输出 → `out = name(…)`，实参 = 其余组件按类树序、
  // 数组组件后跟各维 `size(x, i)`（ExternalFunctionImplicit3）；单个数组输出 → omc 告警 → SKIP。"builtin" 语言的函数不打印
  // （DAEDump.isVisibleFunction）→ SKIP。
  bool fn_external(const FnSig& sig, FlatFunction& ff) {
    const SExternalDecl& ed = *sig.ext;
    const std::string_view lang = ed.lang.is_valid() ? in_.str(ed.lang) : std::string_view("C");
    if (lang != "C" && lang != "FORTRAN 77" && lang != "Fortran 77") return false;   // builtin / 非法语言（checkExternalDeclLanguage）
    Id<Expr> out;
    std::vector<Id<Expr>> args;
    auto local = [&](Id<SElement> eid) {
      CrefPart p;
      p.name = I_.scode.elems[eid].name;
      return fn_local_cref(p, func_params_[p.name.id]);
    };
    if (ed.func_name.is_valid()) {
      for (Id<Exp> a : ed.args) {
        const Exp& ae = I_.absyn.exps[a];
        Id<Expr> x;
        if (ae.kind == ExpKind::CrefExp) {
          x = inst_exp(a, Inst::empty_node(), {});
          if (result_.unsupported || result_.model.exprs[x].kind != ExprKind::Cref ||
              result_.model.exprs[x].cref.resolved_node != kFnLocal)
            return false;
        } else if (ae.kind == ExpKind::Call && I_.absyn.crefs[ae.cref].parts.size() == 1 &&
                   in_.str(I_.absyn.crefs[ae.cref].parts[0].name) == "size" && ae.args.size() == 2 &&
                   I_.absyn.exps[ae.args[0]].kind == ExpKind::CrefExp) {
          const Id<Expr> arr = inst_exp(ae.args[0], Inst::empty_node(), {});
          const Id<Expr> k = simplify_expr(inst_exp(ae.args[1], Inst::empty_node(), {}));
          if (result_.unsupported || result_.model.exprs[arr].cref.resolved_node != kFnLocal ||
              result_.model.exprs[k].kind != ExprKind::IntLit)
            return false;
          Expr s;
          s.kind = ExprKind::Size;
          s.args = {arr, k};
          s.ty = Type::integer();
          x = add(std::move(s));
        } else {
          std::optional<CVal> v = eval_scode(a, *fn_scope_, 0);
          if (!v || v->k == CVal::K::Array || v->k == CVal::K::Record || v->k == CVal::K::Undef) return false;
          x = literal_of(*v);
        }
        args.push_back(x);
      }
      if (ed.output.is_valid()) {
        out = inst_exp(ed.output, Inst::empty_node(), {});
        if (result_.unsupported || result_.model.exprs[out].kind != ExprKind::Cref ||
            result_.model.exprs[out].cref.resolved_node != kFnLocal || result_.model.exprs[out].ty.is_array())
          return false;
      }
      ff.ext_name = ed.func_name;
    } else {
      const bool single = sig.outputs.size() == 1;
      if (single && func_params_[I_.scode.elems[sig.outputs[0]].name.id].is_array()) return false;   // EXT_FN_SINGLE_RETURN_ARRAY 告警
      if (single) out = local(sig.outputs[0]);
      for (Id<SElement> eid : sig.all) {
        if (single && I_.scode.elems[eid].attr.dir == SDirection::Output) continue;
        const Id<Expr> x = local(eid);
        args.push_back(x);
        const Type xt = result_.model.exprs[x].ty;
        for (std::size_t k = 0; k < xt.dims.size(); ++k) {
          Expr s;
          s.kind = ExprKind::Size;
          s.args = {x, lit_int(static_cast<std::int64_t>(k) + 1)};
          s.ty = Type::integer();
          args.push_back(add(std::move(s)));
        }
      }
      ff.ext_name = I_.scode.elems[sig.ext_owner].name;
    }
    ff.is_external = true;
    ff.ext_lang = in_.intern(lang);
    ff.ext_output = out;
    ff.ext_args = std::move(args);
    return true;
  }

  // 模型里整记录引用（等式 `a = b`、绑定）的记录类型 → 收其构造函数（NFFlatten.collectExpFuncs CREF → collectTypeFuncs
  // :2690-2730，EquationComponent1-4 / RecordAssignment）。构造函数名 = 记录类的作用域路径（顶层 → 名；包里 → 包路径 + 名；
  // 根类里 → 根名 + 名）；记录定义在非根的实例类里（实例路径未核实）→ SKIP。
  // 记录类（实例节点 rn）的构造函数：路径（顶层 → 名；包里 → 包路径 + 名；根类里 → 根名 + 名）+ 登记构造函数块（每个
  // 路径一次）。记录定义在非根的实例类里（实例路径未核实）/ 构造函数块发射不了 → nullopt（已标 SKIP）。
  std::optional<std::vector<Symbol>> ensure_record_ctor(NodeId rn) {
    const Id<SElement> rdef = I_.nodes[rn].def;
    const ScodeScope chain = scode_scope_of(rn);   // [R, 外层类, …]
    std::vector<Symbol> path;
    const std::uint32_t root_def = I_.nodes[root_].def.raw;
    if (chain.size() >= 2 && chain[1].raw == root_def) {
      path = result_.model.name;
    } else {
      for (std::size_t k = 1; k < chain.size(); ++k)
        if (chain[k].raw == root_def || I_.scode.elems[chain[k]].restriction != SRestriction::Package) {
          set_unsupported("record constructor naming");
          return std::nullopt;
        }
      for (std::size_t k = chain.size(); k-- > 1;) path.push_back(I_.scode.elems[chain[k]].name);
    }
    path.push_back(I_.scode.elems[rdef].name);
    if (!emit_record_ctor(rdef, chain, path) || result_.unsupported) { set_unsupported("record constructor"); return std::nullopt; }
    if (std::find(model_record_ctors_.begin(), model_record_ctors_.end(), path) == model_record_ctors_.end())
      model_record_ctors_.push_back(path);
    return path;
  }
  // 记录类定义 rec 的某个实例类节点（记录调用 / 构造调用的 Complex 类型要一个节点作 complex_node）；没有 → invalid。
  // ponytail: 线性扫实例节点 + 缓存；节点多时再在 inst 侧给类定义建索引。
  NodeId record_class_node(Id<SElement> rec) {
    if (auto it = rec_node_cache_.find(rec.raw); it != rec_node_cache_.end()) return it->second;
    NodeId hit;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(I_.nodes.size()); ++i) {
      const NodeId n = NodeId::from_index(i);
      // 要已实例化的类（FlatTree：组件已定型）；作用域树里的部分实例化节点没有组件。
      if (!I_.nodes[n].is_empty() && I_.nodes[n].kind == NodeKind::Class && I_.nodes[n].def == rec && I_.nodes[n].cls.is_valid() &&
          I_.node_class(n).tree.is_valid() && I_.trees[I_.node_class(n).tree].state == TreeState::FlatTree) {
        hit = n;
        break;
      }
    }
    rec_node_cache_[rec.raw] = hit;
    return hit;
  }

  void collect_record_ctors() {
    std::unordered_set<std::uint32_t> seen;
    auto visit_cref = [&](const Expr& x) {
      if (x.ty.element_type().kind != TypeKind::Complex) return;
      const std::uint32_t cn = x.ty.element_type().complex_node;
      if (cn == Id<INode>::kInvalid) return;
      const NodeId rn = NodeId::from_index(cn);
      if (!I_.nodes.contains(rn) || I_.nodes[rn].is_empty()) return;
      const Id<SElement> rdef = I_.nodes[rn].def;
      if (!rdef.is_valid() || I_.scode.elems[rdef].restriction != SRestriction::Record) return;
      if (!seen.insert(rdef.raw).second) return;
      ensure_record_ctor(rn);
    };
    auto expr = [&](auto& self, Id<Expr> id) -> void {
      if (!id.is_valid() || result_.unsupported) return;
      const Expr x = result_.model.exprs[id];   // 拷贝：emit_record_ctor 会往 exprs 添加
      // 整记录引用与返回记录的调用（构造调用 / 用户函数）都收其记录类型的构造函数（collectExpFuncs CALL/CREF → collectTypeFuncs）。
      if (x.kind == ExprKind::Cref || x.kind == ExprKind::Call) visit_cref(x);
      for (const ExprCrefPart& cp : x.cref.parts) for (Id<Expr> sub : cp.subscripts) self(self, sub);
      self(self, x.a);
      self(self, x.b);
      self(self, x.c);
      for (Id<Expr> a : x.args) self(self, a);
    };
    auto eqn = [&](auto& self, Id<FlatEquation> id) -> void {
      const FlatEquation q = result_.model.eqs[id];
      expr(expr, q.a); expr(expr, q.b); expr(expr, q.c);
      for (const FlatEqBranch& br : q.branches) { expr(expr, br.cond); for (Id<FlatEquation> b : br.body) self(self, b); }
      for (Id<FlatEquation> b : q.body) self(self, b);
      for (Id<FlatEquation> b : q.else_body) self(self, b);
    };
    auto stmt = [&](auto& self, Id<FlatStatement> id) -> void {
      const FlatStatement st = result_.model.stmts[id];
      expr(expr, st.a); expr(expr, st.b); expr(expr, st.c);
      for (const FlatStmtBranch& br : st.branches) { expr(expr, br.cond); for (Id<FlatStatement> b : br.body) self(self, b); }
      for (Id<FlatStatement> b : st.body) self(self, b);
      for (Id<FlatStatement> b : st.else_body) self(self, b);
    };
    for (std::size_t i = 0; i < result_.model.variables.size(); ++i) expr(expr, result_.model.variables[i].binding);
    for (const std::vector<Id<FlatEquation>>* eqs : {&result_.model.equations, &result_.model.initial_equations})
      for (std::size_t i = 0; i < eqs->size(); ++i) eqn(eqn, (*eqs)[i]);
    for (const std::vector<FlatAlgorithm>* algs : {&result_.model.algorithms, &result_.model.initial_algorithms})
      for (std::size_t a = 0; a < algs->size(); ++a)
        for (std::size_t i = 0; i < (*algs)[a].stmts.size(); ++i) stmt(stmt, (*algs)[a].stmts[i]);
  }

  // 记录类型的压平名：类路径（记录在顶层 → 名字本身；在包里 → 包路径 + 名，与函数同一套 lookup 规则）；定义在根类或
  // 实例类里（实例路径未核实）→ nullopt。
  std::optional<std::vector<Symbol>> rec_flat_name(const SElement& c, const ScodeScope& scope, Id<SElement>& rec,
                                                   ScodeScope& rscope) {
    ScodeScope ds;
    rec = resolve_class_path(c.type.path, c.type.leading_dot, scope, ds);
    if (!rec.is_valid() || I_.scode.elems[rec].kind != SElementKind::Class ||
        I_.scode.elems[rec].restriction != SRestriction::Record || I_.scode.elems[rec].def.kind != SClassDefKind::Parts)
      return std::nullopt;
    const std::uint32_t root_def = I_.nodes[root_].def.raw;
    for (Id<SElement> d : ds)
      if (d.raw == root_def || (fn_home_.is_valid() && d.raw == fn_home_.raw)) return std::nullopt;
    std::vector<Symbol> path;
    for (auto it = ds.rbegin(); it != ds.rend(); ++it) path.push_back(I_.scode.elems[*it].name);
    path.push_back(I_.scode.elems[rec].name);
    rscope = ds;
    rscope.insert(rscope.begin(), rec);
    return path;
  }
  // 记录构造函数块（Record.instDefaultConstructor :140-184 + DAEDumpTpl RECORD_CONSTRUCTOR :124-133 / dumpRecordVar
  // :203-221）：`function R "Automatically generated record constructor for R"`，字段逐个 `input <类型> <名>[ = 默认];`
  //（1.25.0 连 constant 字段也打 input，YmcRec2 探针），末行 `output <末段名> res;`。字段默认在记录作用域按函数语境定型
  //（引用别的字段 = 裸名）。字段只做内建标量、无属性修饰；记录 extends / 数组字段 / 嵌套记录 → nullopt（SKIP）。
  std::optional<std::size_t> emit_record_ctor(Id<SElement> rec, const ScodeScope& rscope, const std::vector<Symbol>& path) {
    for (std::size_t i = 0; i < result_.model.functions.size(); ++i)
      if (result_.model.functions[i].name == path) return i;
    FnCtxGuard guard(*this, &rscope, Id<SElement>{}, false);
    std::vector<Id<SElement>> fields;
    for (Id<SElement> fid : I_.scode.elems[rec].def.elements) {
      const SElement& f = I_.scode.elems[fid];
      if (f.kind == SElementKind::Extends) return std::nullopt;
      if (f.kind != SElementKind::Component) continue;
      TypeKind tk;
      if (!builtin_scalar_type_kind(f.type.path, tk) || !f.attr.dims.empty() || !f.type.dims.empty()) return std::nullopt;
      if (f.mod.is_valid() && !I_.scode.mods[f.mod].subs.empty()) return std::nullopt;
      func_params_[f.name.id] = Type::of(tk);
      fields.push_back(fid);
    }
    FlatFunction ff;
    ff.name = path;
    ff.is_record_ctor = true;
    std::string cmt = "Automatically generated record constructor for ";
    for (std::size_t i = 0; i < path.size(); ++i) { if (i) cmt += '.'; cmt += in_.str(path[i]); }
    ff.comment = in_.intern(cmt);
    for (Id<SElement> fid : fields) {
      const SElement& f = I_.scode.elems[fid];
      FlatFuncParam p;
      p.name = f.name;
      p.ty = func_params_[f.name.id];
      p.direction = FlatDirection::Input;
      if (f.mod.is_valid() && I_.scode.mods[f.mod].binding.is_valid()) {
        p.binding = fn_exp(I_.scode.mods[f.mod].binding, p.ty);
        if (result_.unsupported || !p.binding.is_valid()) return std::nullopt;
      }
      ff.params.push_back(std::move(p));
    }
    FlatFuncParam res;
    res.name = in_.intern("res");
    res.ty = Type::of(TypeKind::Complex);
    res.direction = FlatDirection::Output;
    res.type_path = {path.back()};   // dumpPathLastIndent：输出打末段名
    ff.params.push_back(std::move(res));
    result_.model.functions.push_back(std::move(ff));
    return result_.model.functions.size() - 1;
  }
  // 函数里记录局部的字段引用 `r.x`（kFnLocal，类型 = 字段类型）。嵌套记录 / 带下标 / 非字段 → unsupported。
  Id<Expr> fn_field_cref(const Cref& c) {
    const auto rit = fn_records_.find(c.parts[0].name.id);
    if (rit == fn_records_.end() || c.parts.size() != 2 || !c.parts[0].subscripts.empty() || !c.parts[1].subscripts.empty()) {
      set_unsupported("function record field reference");
      return lit_int(0);
    }
    for (Id<SElement> fid : I_.scode.elems[rit->second.rec].def.elements) {
      const SElement& f = I_.scode.elems[fid];
      TypeKind tk;
      if (f.kind == SElementKind::Component && f.name == c.parts[1].name && builtin_scalar_type_kind(f.type.path, tk)) {
        Expr r;
        r.kind = ExprKind::Cref;
        r.cref.resolved_node = kFnLocal;
        r.ty = Type::of(tk);
        r.cref.parts.push_back({c.parts[0].name, {}});
        r.cref.parts.push_back({c.parts[1].name, {}});
        return add(std::move(r));
      }
    }
    set_unsupported("function record field reference");
    return lit_int(0);
  }
  // 实参的记录类（SCode 类元素 raw）：模型记录组件的整体 cref → 其类实例的定义；函数里的记录局部 → fn_records_。
  // 其余（构造调用、字段…）→ kInvalid。
  std::uint32_t record_def_of(Id<Expr> id) {
    const Expr& x = result_.model.exprs[id];
    // 返回记录的调用（构造调用 / 用户函数）：类型带输出记录类的实例节点 → 其定义。
    if (x.kind == ExprKind::Call && x.ty.kind == TypeKind::Complex && x.ty.complex_node != Id<INode>::kInvalid) {
      const NodeId cn = NodeId::from_index(x.ty.complex_node);
      return I_.nodes.contains(cn) && !I_.nodes[cn].is_empty() ? I_.nodes[cn].def.raw : Id<SElement>::kInvalid;
    }
    if (x.kind != ExprKind::Cref || x.ty.kind != TypeKind::Complex || x.cref.parts.empty()) return Id<SElement>::kInvalid;
    if (x.cref.resolved_node == kFnLocal) {
      const auto it = fn_records_.find(x.cref.parts[0].name.id);
      return (x.cref.parts.size() == 1 && it != fn_records_.end()) ? it->second.rec.raw : Id<SElement>::kInvalid;
    }
    const NodeId cls = NodeId::from_index(x.ty.complex_node);
    if (x.ty.complex_node == Id<INode>::kInvalid || !I_.nodes.contains(cls) || I_.nodes[cls].is_empty())
      return Id<SElement>::kInvalid;
    return I_.nodes[cls].def.raw;
  }

  // 发射函数定义（去重按压平名；先登记形参表再翻译函数体 → 自递归调用能找到自己）。返回在 result_.model.functions 里的
  // 下标；不可发射 → nullopt（调用方 SKIP）。alias_defaults：别名修饰给的输入默认（已在别名定义处按模型语境定型，
  // FuncDefaultArg6 `function g = f(r = x)` → `input Real r = x`）。home：见 fn_flat_name。
  std::optional<std::size_t> emit_function(const FnRef& fr, const FnSig& sig, const std::vector<Symbol>& qname,
                                           const std::unordered_map<std::uint32_t, Id<Expr>>& alias_defaults,
                                           Id<SElement> home) {
    for (std::size_t i = 0; i < result_.model.functions.size(); ++i)
      if (result_.model.functions[i].name == qname) return i;
    const SElement& fel = I_.scode.elems[fr.fe];
    if (fel.is_partial || fn_annotation_blocks(fel.cmt.annotation))   // 调 partial 函数 = 错误
      return fn_fail<std::optional<std::size_t>>("partial function or unsupported class annotation");
    // 类注解 / external 段注解里的 derivative / inverse：形态须在已做范围内，被引用的函数在本函数发射完后连带发射（见末尾）。
    std::vector<Id<Cref>> ann_refs;
    if (!fn_ref_annotations(fel.cmt.annotation, sig, ann_refs) ||
        (sig.ext && !fn_ref_annotations(sig.ext->annotation, sig, ann_refs)))
      return fn_fail<std::optional<std::size_t>>("derivative / inverse annotation form");
    // 纯度（makeAttributes :2626-2633）：显式 pure / impure 照写；未声明时自带 external 段的函数 impure，继承来的 external
    // 段不算（FunctionSections5 `function f extends base_f` 打 `function f`）；外部调用名为 ModelicaError → pure。
    const bool modelica_error = fel.def.external.present && fel.def.external.func_name.is_valid() &&
                                in_.str(fel.def.external.func_name) == "ModelicaError";
    const bool impure = !modelica_error && (fel.purity == Purity::Impure ||
                                            (fel.purity == Purity::None && fel.restriction == SRestriction::ExternalFunction));
    FnCtxGuard guard(*this, &fr.fscope, home, impure);
    std::vector<Id<SElement>> all = sig.inputs;
    all.insert(all.end(), sig.outputs.begin(), sig.outputs.end());
    all.insert(all.end(), sig.locals.begin(), sig.locals.end());
    for (const auto& ad : alias_defaults) {   // 别名修饰只接输入（修饰输出 / 局部的形态未做）
      bool is_input = false;
      for (Id<SElement> eid : sig.inputs) is_input = is_input || I_.scode.elems[eid].name.id == ad.first;
      if (!is_input) return fn_fail<std::optional<std::size_t>>("alias modifier on non-input");
    }
    // 形参类型：先按元素类型登记全部名字（维 / 绑定可互引），再按声明序算维（`Real z[n]`、`Real y[size(x, 1)]`）。
    std::unordered_map<std::uint32_t, std::vector<Symbol>> rec_paths;   // 记录类型形参 / 局部 → 记录压平名
    for (Id<SElement> eid : all) {
      const SElement& c = I_.scode.elems[eid];
      if (c.attr.ct != SConnectorType::Potential || c.prefixes.io != SInnerOuter::None)
        return fn_fail<std::optional<std::size_t>>("flow / stream / inner / outer function variable");
      TypeKind tk;
      if (builtin_scalar_type_kind(c.type.path, tk)) {
        if (c.mod.is_valid() && !I_.scode.mods[c.mod].subs.empty())   // 属性修饰（打印形态未核实）
          return fn_fail<std::optional<std::size_t>>("attribute modifier on function variable");
        func_params_[c.name.id] = Type::of(tk);
        continue;
      }
      // 记录类型（FunctionRecordArg1 `input R r`、FunctionCompOrder4 `R r(x = lx)`）：按类型名打印，构造函数块随之发射
      //（collectClassFunctions → collectTypeFuncs）；字段修饰不打印（只进依赖）；记录绑定 / 记录数组未做。枚举 / 派生
      // 类型形参未做。
      Id<SElement> rec;
      ScodeScope rs;
      std::optional<std::vector<Symbol>> rpath = rec_flat_name(c, fr.fscope, rec, rs);
      if (!rpath) return fn_fail<std::optional<std::size_t>>("function variable type (enumeration / derived / unresolved)");
      if (!c.attr.dims.empty() || !c.type.dims.empty()) return fn_fail<std::optional<std::size_t>>("record array in function");
      if (c.mod.is_valid() && I_.scode.mods[c.mod].binding.is_valid())
        return fn_fail<std::optional<std::size_t>>("record binding in function");
      if (!emit_record_ctor(rec, rs, *rpath) || result_.unsupported)
        return fn_fail<std::optional<std::size_t>>("record constructor");
      func_params_[c.name.id] = Type::of(TypeKind::Complex);
      fn_records_[c.name.id] = FnRecord{rec, rs};
      rec_paths[c.name.id] = std::move(*rpath);
    }
    for (Id<SElement> eid : sig.inputs) fn_inputs_.insert(I_.scode.elems[eid].name.id);
    for (Id<SElement> eid : all) {
      const SElement& c = I_.scode.elems[eid];
      std::vector<Id<Exp>> dexps = c.attr.dims;
      dexps.insert(dexps.end(), c.type.dims.begin(), c.type.dims.end());   // 组件维在前、类型维在后（MLS §10.1）
      if (dexps.empty()) continue;
      std::vector<Dimension> ds;
      for (Id<Exp> d : dexps) {
        Dimension dim;
        if (I_.absyn.exps[d].kind == ExpKind::Colon) {
          dim.kind = DimKind::Unknown;
        } else {
          const Id<Expr> de = fn_exp(d, Type::integer());
          if (result_.unsupported || !de.is_valid()) return fn_fail<std::optional<std::size_t>>("function variable dimension");
          if (result_.model.exprs[de].kind == ExprKind::IntLit) {
            dim.kind = DimKind::Integer;
            dim.size = result_.model.exprs[de].ival;
          } else {
            dim.kind = DimKind::Exp;
            dim.exp = de;
          }
        }
        ds.push_back(dim);
      }
      func_params_[c.name.id] = Type::array_of(func_params_[c.name.id], std::move(ds));
      fn_dim_exps_[c.name.id] = std::move(dexps);
    }
    // 绑定：输入 = 别名默认（优先）或声明默认；输出 / 局部 = 声明绑定。
    std::unordered_map<std::uint32_t, Id<Expr>> binds;
    std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> mod_refs;   // 记录局部的字段修饰引用（排序依赖）
    for (Id<SElement> eid : all) {
      const SElement& c = I_.scode.elems[eid];
      const Type ty = func_params_[c.name.id];
      if (fn_records_.count(c.name.id) && c.mod.is_valid()) {
        // 字段修饰 `R r(x = lx)`：定型只为收依赖（getLocalDependencies2：无绑定的记录看字段绑定），不打印。
        for (const SSubMod& sm : I_.scode.mods[c.mod].subs) {
          const SMod& fm = I_.scode.mods[sm.mod];
          if (!fm.subs.empty()) return fn_fail<std::optional<std::size_t>>("nested record field modifier in function");
          if (!fm.binding.is_valid()) continue;
          const Id<Expr> fb = simplify_expr(inst_exp(fm.binding, Inst::empty_node(), {}));
          if (result_.unsupported) return std::nullopt;
          collect_fn_refs(fb, mod_refs[c.name.id]);
        }
      }
      Id<Expr> b;
      const auto ad = alias_defaults.find(c.name.id);
      if (ad != alias_defaults.end()) {
        b = coerce_to_param(ad->second, ty);
        if (!b.is_valid()) return fn_fail<std::optional<std::size_t>>("alias default type");
      } else if (c.mod.is_valid() && I_.scode.mods[c.mod].binding.is_valid()) {
        b = fn_exp(I_.scode.mods[c.mod].binding, ty);
        if (result_.unsupported || !b.is_valid())   // 绑定类型不符（omc 报错）
          return fn_fail<std::optional<std::size_t>>("function variable binding");
      }
      binds[c.name.id] = b;
    }
    // 局部拓扑序（sortLocals）：依赖 = 绑定与维里引用的其它局部；FIFO Kahn（无依赖者按声明序入队，出队一个就把因它就绪
    // 的按原序追加到队尾，FunctionCompOrder3 `n` 先于 `z[n]`）；环 → CYCLIC_FUNCTION_COMPONENTS → nullopt。
    std::vector<std::size_t> lorder;
    {
      const std::size_t n = sig.locals.size();
      std::unordered_map<std::uint32_t, std::size_t> at;
      for (std::size_t i = 0; i < n; ++i) at[I_.scode.elems[sig.locals[i]].name.id] = i;
      std::vector<std::vector<std::size_t>> deps(n);
      for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t nm = I_.scode.elems[sig.locals[i]].name.id;
        std::unordered_set<std::uint32_t> refs = mod_refs[nm];
        collect_fn_refs(binds[nm], refs);
        for (const Dimension& d : func_params_[nm].dims) if (d.kind == DimKind::Exp) collect_fn_refs(d.exp, refs);
        for (std::uint32_t r : refs) {
          const auto it = at.find(r);
          if (it != at.end() && it->second != i) deps[i].push_back(it->second);
        }
      }
      std::deque<std::size_t> start;
      std::vector<std::size_t> rest;
      for (std::size_t i = 0; i < n; ++i) (deps[i].empty() ? start.push_back(i) : rest.push_back(i));
      while (!start.empty()) {
        const std::size_t v = start.front();
        start.pop_front();
        lorder.push_back(v);
        std::vector<std::size_t> keep;
        for (std::size_t r : rest) {
          deps[r].erase(std::remove(deps[r].begin(), deps[r].end(), v), deps[r].end());
          if (deps[r].empty()) start.push_back(r); else keep.push_back(r);
        }
        rest = std::move(keep);
      }
      if (!rest.empty()) return fn_fail<std::optional<std::size_t>>("cyclic local bindings");
    }
    FlatFunction ff;
    ff.name = qname;
    ff.is_impure = impure;
    // 注释取调用里写的那个类自己的（别名 `function f = g "c"` 用别名的，没写就没有；不继承 g 的。FC 探针 f2）。
    ff.comment = class_comment(I_.scode.elems[fr.named].cmt.str);
    auto param = [&](Id<SElement> eid, FlatDirection dir) {
      const SElement& c = I_.scode.elems[eid];
      FlatFuncParam p;
      p.name = c.name;
      p.ty = func_params_[c.name.id];
      p.direction = dir;
      p.is_protected = dir == FlatDirection::None;
      p.var_kind = FlatVarKind::Var;   // in/out 变率不打；局部保留 parameter / constant / discrete（FunctionCompOrder3）
      if (dir == FlatDirection::None) {
        if (c.attr.var == SVariability::Param) p.var_kind = FlatVarKind::Parameter;
        else if (c.attr.var == SVariability::Const) p.var_kind = FlatVarKind::Constant;
        else if (c.attr.var == SVariability::Discrete) p.var_kind = FlatVarKind::Discrete;
      }
      p.binding = binds[c.name.id];
      if (c.cmt.str.is_valid()) p.comment = c.cmt.str;   // FC 探针
      if (const auto rp = rec_paths.find(c.name.id); rp != rec_paths.end()) {
        p.type_path = rp->second;
        p.record_def = fn_records_[c.name.id].rec.raw;
      }
      ff.params.push_back(std::move(p));
    };
    for (Id<SElement> eid : sig.inputs) param(eid, FlatDirection::Input);
    for (Id<SElement> eid : sig.outputs) param(eid, FlatDirection::Output);
    for (std::size_t i : lorder) param(sig.locals[i], FlatDirection::None);
    const std::size_t fi = result_.model.functions.size();
    result_.model.functions.push_back(std::move(ff));   // 先登记（体里的自递归调用按名找到它）
    std::vector<Id<FlatStatement>> body;
    if (sig.alg)
      for (Id<SStatement> sid : sig.alg->stmts) {
        const Id<FlatStatement> s = collect_fn_stmt(sid);
        if (result_.unsupported) return std::nullopt;
        if (s.is_valid()) body.push_back(s);
      }
    result_.model.functions[fi].body = std::move(body);
    result_.model.functions[fi].has_algorithm = sig.alg != nullptr;
    if (sig.ext) {
      FlatFunction ext_part;   // 先填到临时对象（下面的 inst_exp 不会发射函数，但不跨调用持 functions 引用）
      if (!fn_external(sig, ext_part)) return fn_fail<std::optional<std::size_t>>("external declaration form");
      FlatFunction& f = result_.model.functions[fi];
      f.is_external = true;
      f.ext_lang = ext_part.ext_lang;
      f.ext_name = ext_part.ext_name;
      f.ext_output = ext_part.ext_output;
      f.ext_args = std::move(ext_part.ext_args);
    } else if (fn_uninit_warns(result_.model.functions[fi])) {
      set_unsupported("function use-before-assign warning");
      return std::nullopt;
    }
    for (Id<Cref> rc : ann_refs) {
      const std::optional<std::size_t> li = emit_referenced_function(I_.absyn.crefs[rc]);
      if (!li) return fn_fail<std::optional<std::size_t>>("function referenced by derivative / inverse annotation");
      const std::vector<Symbol> lname = result_.model.functions[*li].name;   // 拷贝：下一句取 functions[fi] 的引用
      result_.model.functions[fi].linked.push_back(lname);
    }
    return fi;
  }

  // ---- 向量化调用（NFCall.vectorizeCall，NFCall.mo:2908-2964）：实参比形参多出前导维 → `array(f(x[$i1]) for $i1 in 1:n)`。
  // 迭代变量名按维序号 `$i1…$ik`、每个调用从 1 起（1.25.0 实测：FuncVectorization1 两个等式都是 `$i1`；源码 HEAD 已改成全局
  // 计数的 newUniqueIterator，测试文件里的 `$i0` / `$i1` 是那个版本的）。V4 探针（T0–T29）核实了下面各形态。----
  Id<Expr> vect_iter(std::size_t k) {   // 第 k（1 基）个向量化维的迭代变量引用
    Expr ic;
    ic.kind = ExprKind::Cref;
    ic.ty = Type::integer();
    ic.cref.parts.push_back({in_.intern("$i" + std::to_string(k)), {}});
    return add(std::move(ic));
  }
  // 向量化实参取下标 `[$i1, …]`（Expression.applySubscript 的已核实形态）：cref → 迭代变量先顶替末段的 `:` 切片位、余下接在
  // 末尾（`x` → `x[$i1]`、`w[1]` / `w[1, :]` → `w[1,$i1]`）；书写成 `{…}` 的数组字面量 → `{…}[$i1]`（Real 字面量的 Integer 叶
  // 先提 Real：`{1.0, 2.0, time}[$i1]`）。范围 / 下标数组切片（omc `x[(2:3)[$i1]]`）、运算式（omc 先展开成字面量
  // `{2.0 * x[1], …}[$i1]`）、嵌套的向量化调用（omc `sin(x[(1:3)[$i1]])`）、内建展开出的字面量未做 → invalid。
  Id<Expr> vectorize_arg(Id<Expr> a, const std::vector<Id<Expr>>& iters, bool written_literal) {
    const Expr x = result_.model.exprs[a];   // 拷贝：下面会 add
    const std::vector<std::int64_t> sh = expr_shape(a);
    if (sh.size() < iters.size()) return {};
    std::vector<Dimension> rest;
    for (std::size_t k = iters.size(); k < sh.size(); ++k) {
      if (sh[k] < 0) return {};
      Dimension d;
      d.kind = DimKind::Integer;
      d.size = sh[k];
      rest.push_back(d);
    }
    const Type elem = Type::of(expr_elem_kind(a));
    const Type rty = rest.empty() ? elem : Type::array_of(elem, std::move(rest));
    if (x.kind == ExprKind::Cref && !x.cref.parts.empty()) {
      Expr c = x;
      std::vector<Id<Expr>>& subs = c.cref.parts.back().subscripts;
      std::size_t next = 0;
      for (Id<Expr>& s : subs) {
        const ExprKind k = result_.model.exprs[s].kind;
        if (k == ExprKind::Range || k == ExprKind::Array) return {};
        if (k == ExprKind::Wild && next < iters.size()) s = iters[next++];
      }
      while (next < iters.size()) subs.push_back(iters[next++]);
      c.ty = rty;
      return add(std::move(c));
    }
    if (x.kind == ExprKind::Array && written_literal) {
      Expr s;
      s.kind = ExprKind::Subscript;
      s.a = elem.kind == TypeKind::Real ? coerce_deep(a, TypeKind::Real) : a;
      s.args = iters;
      s.ty = rty;
      return add(std::move(s));
    }
    return {};
  }
  // 向量化调用的结果：末维在最内层，`array(array(f(…) for $i2 in 1:d2) for $i1 in 1:d1)`（T10 探针）。
  Id<Expr> wrap_vectorized(Id<Expr> call, const std::vector<std::int64_t>& vd) {
    Id<Expr> e = call;
    for (std::size_t k = vd.size(); k-- > 0;) {
      Expr rg;
      rg.kind = ExprKind::Range;
      rg.ty = Type::integer();
      rg.a = lit_int(1);
      rg.b = lit_int(vd[k]);
      Dimension d;
      d.kind = DimKind::Integer;
      d.size = vd[k];
      Expr r;
      r.kind = ExprKind::Reduction;
      r.call_name = {in_.intern("array"), in_.intern("$i" + std::to_string(k + 1))};
      r.a = e;
      r.b = add(std::move(rg));
      r.ty = Type::array_of(result_.model.exprs[e].ty, {d});
      e = add(std::move(r));
    }
    return e;
  }
  // 实参的 Absyn 是书写出来的数组构造子 `{…}`（非推导式）。
  bool written_array_literal(Id<Exp> raw) { return raw.is_valid() && is_array_ctor(raw) && !is_comprehension(raw); }
  static bool is_scalar_literal(const Expr& x) {
    return x.kind == ExprKind::IntLit || x.kind == ExprKind::RealLit || x.kind == ExprKind::BoolLit ||
           x.kind == ExprKind::StringLit || x.kind == ExprKind::EnumLit;
  }
  // 表达式里还留着实参全是字面量的调用。向量化调用 / 推导式整体保留时不求值（V4g 探针 `bk = array(F({1.0, 2.0, 3.0}[$i1],
  // 2.0) for $i1 in 1:3)`），逐元素展开时（参数摊分、sum、属性）迭代变量代入后成常量的 `F(1.0, 2.0)` omc 会求值
  //（`bp[1] = 3.0`、`sum(…)` → `3.0 + 4.0 + 5.0`）；内建的已被 simplify 折掉，用户函数的未做。
  bool has_const_call(Id<Expr> id) {
    if (!id.is_valid()) return false;
    const Expr& x = result_.model.exprs[id];
    if (x.kind == ExprKind::Call && !x.args.empty()) {
      bool all = true;
      for (Id<Expr> a : x.args) all = all && is_scalar_literal(result_.model.exprs[a]);
      if (all) return true;
    }
    for (Id<Expr> a : x.args) if (has_const_call(a)) return true;
    return has_const_call(x.a) || has_const_call(x.b) || has_const_call(x.c);
  }

  // 函数的类注解是否挡住发射 / 求值。对压平文本无影响的放行（FA 探针 16 种：Inline / LateInline / InlineAfterIndexReduction /
  // smoothOrder / Documentation / Icon / GenerateEvents / preferredView / obsolete / 厂商注解 / __OpenModelica_Impure——omc 输出
  // 与无注解时逐字节相同，注解本身不打印）。`__OpenModelica_EarlyInline` 把调用内联掉、其余 `__OpenModelica_*` 未核实 → 挡。
  // derivative / inverse 另由 fn_ref_annotations 把关。
  bool fn_annotation_blocks(Id<SMod> ann) {
    if (!ann.is_valid()) return false;
    for (const SSubMod& sm : I_.scode.mods[ann].subs) {
      const std::string_view n = in_.str(sm.ident);
      if (n.rfind("__OpenModelica_", 0) == 0 && n != "__OpenModelica_Impure") return true;
    }
    return false;
  }
  // derivative / inverse 注解：omc 压平函数时连带压平导数函数与反函数调用里的函数（NFFlatten.mo:2987-2995；
  // FunctionDerivative1 多出 f2、FunctionInverse1 多出 f_inv、ExternalFunctionAnno 多出 df1）。只做已核实的平凡形态：
  //   `derivative = 名字`——不带 order / noDerivative / zeroDerivative（omc 对这些有合法性检查，FunctionDerivativeInvalid*）；
  //   `inverse(入参 = 调用(简单实参…))`——函数恰一个输出、名字确是输入（FunctionInverseInvalid2 / 3 是 omc 报错），值是
  //   调用（Invalid1 是告警）、实参是引用 / 字面量（collectExpFuncs 会连实参里的调用一起收，未做）。
  // 形态在范围内 → true，被引用的函数名收进 refs；否则 false（调用方 SKIP）。
  bool fn_ref_annotations(Id<SMod> ann, const FnSig& sig, std::vector<Id<Cref>>& refs) {
    if (!ann.is_valid()) return true;
    for (const SSubMod& sm : I_.scode.mods[ann].subs) {
      const std::string_view an = in_.str(sm.ident);
      const SMod& dm = I_.scode.mods[sm.mod];
      if (an == "derivative") {
        if (!dm.subs.empty() || !dm.binding.is_valid() || I_.absyn.exps[dm.binding].kind != ExpKind::CrefExp) return false;
        refs.push_back(I_.absyn.exps[dm.binding].cref);
      } else if (an == "inverse") {
        if (dm.binding.is_valid() || dm.subs.empty() || sig.outputs.size() != 1) return false;
        for (const SSubMod& iv : dm.subs) {
          bool is_input = false;
          for (Id<SElement> in : sig.inputs) is_input = is_input || I_.scode.elems[in].name == iv.ident;
          const SMod& im = I_.scode.mods[iv.mod];
          if (!is_input || !im.subs.empty() || !im.binding.is_valid()) return false;
          const Exp& ce = I_.absyn.exps[im.binding];
          if (ce.kind != ExpKind::Call) return false;
          for (Id<Exp> a : ce.args) {
            const ExpKind k = I_.absyn.exps[a].kind;
            if (k != ExpKind::CrefExp && k != ExpKind::IntLit && k != ExpKind::RealLit) return false;
          }
          refs.push_back(ce.cref);
        }
      }
    }
    return true;
  }
  // 发射注解里引用的函数：按"当前函数体里的一次调用"的名字解析规则（fn_scope_ = 引用它的函数的定义作用域）。
  // 返回被引用函数在 model.functions 里的下标；解析 / 发射不了 → nullopt。
  std::optional<std::size_t> emit_referenced_function(const Cref fn) {   // 按值：下面的发射可能往 absyn arena add
    if (!fn_scope_) return std::nullopt;
    const ScodeScope sc = *fn_scope_;
    const std::optional<FnRef> fr = resolve_fn(fn, sc);
    if (!fr || !fr->alias_mods.empty()) return std::nullopt;
    FnSig sig;
    std::unordered_set<std::uint32_t> seen;
    if (!collect_fn_sig(fr->fe, fr->fscope, sig, seen, 0) || sig.outputs.empty()) return std::nullopt;
    Id<SElement> home;
    const std::optional<std::vector<Symbol>> qname = fn_flat_name(fn, sc, Inst::empty_node(), {}, home);
    if (!qname) return std::nullopt;
    const std::optional<std::size_t> li = emit_function(*fr, sig, *qname, {}, home);
    return result_.unsupported ? std::nullopt : li;
  }

  // 用户函数调用 → 发射函数定义 + 符号调用：压平名（fn_flat_name）、实参补全（位置 / 命名 / 缺省代换，fillArgs）、按形参
  // 类型匹配。多输出函数在表达式里取首输出 `f(x)[1]`（TupleOperation1/2、FunctionMultiOutput2/4）。纯函数体里调 impure
  // 函数 → omc 把调用者改标 impure 并告警（PURE_FUNCTION_WITH_IMPURE_CALLS，ImpureCall1）→ SKIP。不可发射 → invalid。
  // lex：函数名的词法起点（通常 = enclosing；直接继承成员 = 基类实例，见 call()）。实参仍在 enclosing 定型。
  Id<Expr> try_emit_user_call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix, NodeId lex) {
    const bool whole = array_call_ok_;   // 本调用是否整个绑定 / 等式一侧（见 array_call_ok_）
    const bool noret = noret_call_;      // 本调用是一条无返回值调用语句（见 noret_call）：可以没有输出、不取首输出
    struct FlagReset {
      bool& f;
      bool v;
      ~FlagReset() { f = v; }
    } flag_reset{array_call_ok_, whole};
    array_call_ok_ = false;
    noret_call_ = false;   // 只管最外层这一个调用，实参里的调用照常
    const Cref& fn = I_.absyn.crefs[e.cref];
    const ScodeScope sc = scode_scope_of(lex);
    fn_why_ = nullptr;
    const std::optional<FnRef> fr = resolve_fn(fn, sc);
    if (!fr) return {};   // 不是用户函数（内建 / 查不到）：无子原因
    FnSig sig;
    std::unordered_set<std::uint32_t> seen;
    if (!collect_fn_sig(fr->fe, fr->fscope, sig, seen, 0)) return fn_fail<Id<Expr>>("invalid or unsupported function signature");
    if (sig.outputs.empty() && !noret) return fn_fail<Id<Expr>>("function without output used as expression");
    for (Id<Exp> a : e.args)   // 归约实参 / 向量化未做
      if (I_.absyn.exps[a].kind == ExpKind::Reduction) return fn_fail<Id<Expr>>("reduction as function argument");
    Id<SElement> home;
    const std::optional<std::vector<Symbol>> qname = fn_flat_name(fn, sc, lex, prefix, home);
    if (!qname) return fn_fail<Id<Expr>>("flattened function name");
    // 别名修饰（`function g = f(r = x)`）在别名定义处求值：只做单层别名、定义在调用实例自身类里、模型语境。
    std::unordered_map<std::uint32_t, Id<Expr>> alias_defaults;
    if (!fr->alias_mods.empty()) {
      ScodeScope ads;
      if (fn_scope_ || fr->alias_mods.size() != 1 || fn.parts.size() != 1 || sc.empty() ||
          !find_scode_element_at(fn.parts[0].name, sc, ads).is_valid() || ads.empty() || ads[0].raw != sc[0].raw)
        return fn_fail<Id<Expr>>("function alias with modifiers");
      for (const SSubMod& s : I_.scode.mods[fr->alias_mods[0]].subs) {
        const SMod& sm = I_.scode.mods[s.mod];
        if (!sm.binding.is_valid() || !sm.subs.empty()) return fn_fail<Id<Expr>>("function alias with modifiers");
        const Id<Expr> x = simplify_expr(inst_exp(sm.binding, enclosing, prefix));
        if (result_.unsupported) return {};
        alias_defaults[s.ident.id] = x;
      }
    }
    // 实参入槽（位置 / 命名）；未知名 / 重复 / 过多 → omc 报错 → SKIP。
    const std::vector<Id<SElement>>& inputs = sig.inputs;
    std::vector<Id<Exp>> raw(inputs.size());
    std::size_t pos = 0;
    for (std::size_t i = 0; i < e.args.size(); ++i) {
      if (i < e.arg_names.size() && e.arg_names[i].is_valid()) {
        std::size_t j = 0;
        while (j < inputs.size() && I_.scode.elems[inputs[j]].name != e.arg_names[i]) ++j;
        if (j >= inputs.size() || raw[j].is_valid()) return fn_fail<Id<Expr>>("named argument does not match an input");
        raw[j] = e.args[i];
      } else {
        if (pos >= inputs.size()) return fn_fail<Id<Expr>>("too many arguments");
        raw[pos++] = e.args[i];
      }
    }
    std::vector<Id<Expr>> args(inputs.size());   // 显式实参先在调用处定型（未强转）
    for (std::size_t i = 0; i < inputs.size(); ++i)
      if (raw[i].is_valid()) {
        args[i] = inst_exp(raw[i], enclosing, prefix);
        if (result_.unsupported) return {};
      }
    const std::optional<std::size_t> fi = emit_function(*fr, sig, *qname, alias_defaults, home);
    if (!fi || result_.unsupported) return fn_fail<Id<Expr>>("function body");
    const std::vector<FlatFuncParam> params = result_.model.functions[*fi].params;   // 拷贝（下面会往 arena add）
    if (fn_scope_ && result_.model.functions[*fi].is_impure && !fn_impure_) {
      set_unsupported("pure function calls impure function (warning)");
      return {};
    }
    // 缺省槽（fillDefaultSlot2）：取输入绑定，引用别的输入槽处代换成该槽实参（按槽类型强转，被引用的缺省槽先递归补齐）；
    // 互相引用 → CYCLIC_DEFAULT_VALUE、无默认 → UNFILLED_SLOT → SKIP。YmcFnEmit1：`f(a)` → `f(a, 2.0 * a, 3)`。
    std::vector<int> status(inputs.size(), 0);   // 0 未求 / 1 求值中 / 2 已定
    for (std::size_t i = 0; i < inputs.size(); ++i) if (args[i].is_valid()) status[i] = 2;
    std::function<bool(std::size_t)> fill = [&](std::size_t i) -> bool {
      if (status[i] == 2) return true;
      if (status[i] == 1 || !params[i].binding.is_valid()) return false;
      status[i] = 1;
      bool ok = true;
      const Id<Expr> v = subst_slots(params[i].binding, [&](Symbol nm) -> Id<Expr> {
        for (std::size_t j = 0; j < inputs.size(); ++j)
          if (params[j].name == nm) return fill(j) ? coerce_to_param(args[j], params[j].ty) : Id<Expr>{};
        return {};   // 非输入槽的函数局部引用
      }, ok);
      if (!ok) return false;
      args[i] = v;
      status[i] = 2;
      return true;
    };
    for (std::size_t i = 0; i < inputs.size(); ++i)
      if (!fill(i) || result_.unsupported) return fn_fail<Id<Expr>>("missing argument / default value");
    // 向量化（模型语境）：实参秩 > 形参秩，多出的前导维 = 向量化维，各向量化实参须相同（T20 `F(w, x)` 维不同 → omc 内部错）、
    // 已知且 ≥ 1。实参换成取了 `[$i1, …]` 的形态后照常按形参强转（`/*Real*/(k[$i1])`）。多输出函数的向量化未核实 → SKIP。
    std::vector<std::int64_t> vd;
    if (!fn_scope_) {
      std::vector<std::size_t> vec;
      for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (params[i].record_def != Id<SElement>::kInvalid) continue;
        const std::vector<std::int64_t> ash = expr_shape(args[i]);
        const std::size_t pr = params[i].ty.dims.size();
        if (ash.size() <= pr) continue;
        const std::vector<std::int64_t> lead(ash.begin(), ash.end() - static_cast<std::ptrdiff_t>(pr));
        for (std::int64_t n : lead) if (n < 1) return fn_fail<Id<Expr>>("vectorization over unknown / empty dimension");
        if (!vd.empty() && vd != lead) return fn_fail<Id<Expr>>("vectorization dimension mismatch");
        vd = lead;
        vec.push_back(i);
      }
      if (!vd.empty()) {
        // 语句位置的向量化调用 omc 打 `array(f(x[$i1]) for $i1 in 1:3);`（N1 探针），未做。
        if (sig.outputs.size() > 1 || noret) return fn_fail<Id<Expr>>("vectorized multi-output / statement call");
        std::vector<Id<Expr>> iters;
        for (std::size_t k = 1; k <= vd.size(); ++k) iters.push_back(vect_iter(k));
        for (std::size_t i : vec) {
          args[i] = vectorize_arg(args[i], iters, written_array_literal(raw[i]));
          if (!args[i].is_valid()) return fn_fail<Id<Expr>>("vectorized argument form");
        }
      }
    }
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      // 记录形参：实参须是同一记录类的整体引用（构造调用等其余形态未做）。
      if (params[i].record_def != Id<SElement>::kInvalid && record_def_of(args[i]) != params[i].record_def)
        return fn_fail<Id<Expr>>("record argument form");
      const Id<Expr> a = coerce_to_param(args[i], params[i].ty);
      if (!a.is_valid()) return fn_fail<Id<Expr>>("argument type / shape");   // 实参类型 / 维不符（omc 报错）→ SKIP
      args[i] = simplify_expr(a);
      // 数组实参里的算术式（维已知）：omc 的化简按元素展开成字面量（GA 探针 `g(2 * x)` → `g({2.0 * x[1], 2.0 * x[2],
      // 2.0 * x[3]})`、`gm(m + m)`，函数体里已知维的局部同样）；整体 / 切片引用、if、字面量、推导式原样。`vector(x)` /
      // `diagonal(x)` 作实参 omc 留成调用（ymc 的内建展开成了字面量）→ 未做，SKIP。
      const ExprKind ak = result_.model.exprs[args[i]].kind;
      if (ak == ExprKind::Binary || ak == ExprKind::Unary) {
        const std::vector<std::int64_t> ash = expr_shape(args[i]);
        if (!ash.empty() && std::find(ash.begin(), ash.end(), -1) == ash.end()) {
          const Id<Expr> kf = kept_array_form(args[i], ash, expr_elem_kind(args[i]));
          if (result_.unsupported) return {};
          if (!kf.is_valid()) { set_unsupported("array expression as function argument"); return {}; }
          args[i] = kf;
        }
      }
      if (raw[i].is_valid() && I_.absyn.exps[raw[i]].kind == ExpKind::Call) {
        const Cref& rc = I_.absyn.crefs[I_.absyn.exps[raw[i]].cref];
        const std::string_view rn = rc.parts.size() == 1 ? in_.str(rc.parts[0].name) : std::string_view{};
        if ((rn == "vector" || rn == "diagonal") && result_.model.exprs[args[i]].kind == ExprKind::Array) {
          set_unsupported("array builtin kept as call by omc");
          return {};
        }
      }
    }
    Type rty = sig.outputs.empty() ? Type{} : params[inputs.size()].ty;   // 首输出类型（无输出的语句调用：Unknown）
    if (!vd.empty() && rty.kind == TypeKind::Complex) return fn_fail<Id<Expr>>("vectorized record result");   // 未核实
    if (rty.kind == TypeKind::Complex) {
      // 返回记录的调用（记录等式 `r = f(x)` / 记录赋值，RF1 探针）：类型取输出记录类的实例类节点（record_match /
      // 构造函数登记要节点）；没有实例节点 / 多输出 → SKIP。
      const std::uint32_t rd = params[inputs.size()].record_def;
      const NodeId rn = rd == Id<SElement>::kInvalid ? NodeId{} : record_class_node(Id<SElement>::from_index(rd));
      if (!rn.is_valid() || sig.outputs.size() > 1) return fn_fail<Id<Expr>>("record result");
      rty.complex_node = rn.raw;
    }
    if (!fn_scope_ && rty.is_array()) {
      // 模型语境里嵌在别的运算中的数组调用：须逐元素展开，未做 → SKIP。向量化调用的体不受此限（T9 `m = array(H(x[$i1]) for
      // $i1 in 1:3)`），它所在的位置由推导式的消费方把关（scalarize 的 red_scalarize_ok_）。
      if (!whole && vd.empty()) return fn_fail<Id<Expr>>("array-valued call inside an expression");
      // 调用处求输出维（NFCall.evaluateCallTypeDim :3061-3088）：维表达式里的输入换成实参再求值；buildParameterTree
      // 只放输入（局部不代换，:3104 TODO）→ 求不出的维留未知（与任何维相容，FuncUnknownDim3 `Real[n] result`）。
      for (Dimension& d : rty.dims) {
        if (d.kind != DimKind::Exp) continue;
        bool ok = true;
        const Id<Expr> sub = subst_slots(d.exp, [&](Symbol nm) -> Id<Expr> {
          for (std::size_t j = 0; j < inputs.size(); ++j) if (params[j].name == nm) return args[j];
          return {};
        }, ok);
        const std::optional<std::int64_t> n = ok ? fold_int(sub) : std::nullopt;
        d = Dimension{};
        if (n) { d.kind = DimKind::Integer; d.size = *n; } else { d.kind = DimKind::Unknown; }
      }
    }
    Expr r;
    r.kind = ExprKind::Call;
    r.call_name = *qname;
    r.args = std::move(args);
    r.ty = rty;
    Id<Expr> call = add(std::move(r));
    if (sig.outputs.size() > 1 && !noret) {   // 语句调用不取首输出（N1 探针：两个输出的 `f(t);` 原样）
      Expr t;
      t.kind = ExprKind::TupleElement;
      t.a = call;
      t.ival = 1;
      t.ty = rty;
      call = add(std::move(t));
    }
    return vd.empty() ? call : wrap_vectorized(call, vd);
  }

  // ---- 用户函数：解析 / 签名 / 记录值（NFFunction + NFEvalFunction）----
  // find_scode_element 并给出命中处的定义作用域链（命中类及其外层，内→外；顶层 → 空链）。
  Id<SElement> find_scode_element_at(Symbol name, const ScodeScope& scope, ScodeScope& def_scope) {
    auto hit = [&](Id<SElement> e) {
      const SElement& el = I_.scode.elems[e];
      return el.name == name && (el.kind == SElementKind::Class || el.kind == SElementKind::Component);
    };
    for (std::size_t k = 0; k < scope.size(); ++k) {
      for (Id<SElement> e : I_.scode.elems[scope[k]].def.elements)
        if (hit(e)) {
          def_scope.assign(scope.begin() + static_cast<std::ptrdiff_t>(k), scope.end());
          return e;
        }
      // 本层的 import（本地元素之后、外层之前：同处一张 LookupTree，本地优先，R12 §2.1）。
      if (const Id<SElement> im = import_lookup(name, scope[k], def_scope); im.is_valid()) return im;
      if (result_.unsupported) return {};
      // encapsulated 类：名字首段的查找止于本类（MLS §5.3.1；Encapsulated4 的 `fn` 在外层 → omc "not found"）。
      if (I_.scode.elems[scope[k]].is_encapsulated) return {};
    }
    for (Id<SElement> e : I_.scode.program)
      if (hit(e)) { def_scope.clear(); return e; }
    return {};
  }
  // 类路径 `P.Q.f`：首段按作用域链找，其后逐段下降为局部类；给出末段类的定义作用域链。前导点 = 从顶层找。
  Id<SElement> resolve_class_path(const std::vector<Symbol>& parts, bool leading_dot, const ScodeScope& scope,
                                  ScodeScope& def_scope) {
    if (parts.empty()) return {};
    Id<SElement> e = find_scode_element_at(parts[0], leading_dot ? ScodeScope{} : scope, def_scope);
    for (std::size_t k = 1; k < parts.size() && e.is_valid(); ++k) {
      if (I_.scode.elems[e].kind != SElementKind::Class) return {};
      Id<SElement> nxt;
      for (Id<SElement> c : I_.scode.elems[e].def.elements)
        if (I_.scode.elems[c].name == parts[k] && I_.scode.elems[c].kind == SElementKind::Class) { nxt = c; break; }
      if (!nxt.is_valid()) return {};
      def_scope.insert(def_scope.begin(), e);
      e = nxt;
    }
    return e;
  }
  // 函数引用：长类定义 + 函数体作用域（定义处，非调用处：ExtendsShort2 的 P.f 体内 f2 在 P 里解析）。短类别名
  // `function f = P.f` / `function g = f(r = x)` 逐层追到长类，沿途修饰记为输入默认。local_to_caller：函数就定义在调用者
  // 所在类里（模型内嵌函数）→ 函数体可见调用实例的常量（func1：`A a1(z = 2)` 里 f 读 z 得 2）。
  std::optional<FnRef> resolve_fn(const Cref& fn, const ScodeScope& scope, SRestriction want = SRestriction::Function) {
    std::vector<Symbol> parts;
    for (const CrefPart& p : fn.parts) {
      if (!p.subscripts.empty()) return std::nullopt;
      parts.push_back(p.name);
    }
    ScodeScope ds;
    Id<SElement> e = resolve_class_path(parts, fn.leading_dot, scope, ds);
    FnRef r;
    r.named = e;
    for (int guard = 0; e.is_valid() && guard < 20; ++guard) {
      const SElement& el = I_.scode.elems[e];
      // 带 external 段的函数在 SCode 里是 FR_EXTERNAL_FUNCTION（AbsynToSCode；ymc 摊成 ExternalFunction），也是函数。
      const bool rok = el.restriction == want ||
                       (want == SRestriction::Function && el.restriction == SRestriction::ExternalFunction);
      if (el.kind != SElementKind::Class || !rok) return std::nullopt;
      if (el.def.kind == SClassDefKind::Parts) {
        r.fe = e;
        r.local_to_caller = !ds.empty() && !scope.empty() && ds[0].raw == scope[0].raw;
        r.fscope = std::move(ds);
        r.fscope.insert(r.fscope.begin(), e);
        return r;
      }
      if (el.def.kind != SClassDefKind::Derived) return std::nullopt;
      if (el.def.derived_mod.is_valid()) r.alias_mods.push_back(el.def.derived_mod);
      ScodeScope nds;
      e = resolve_class_path(el.def.derived_type.path, el.def.derived_type.leading_dot, ds, nds);
      ds = std::move(nds);
    }
    return std::nullopt;
  }
  // 函数签名：extends 的成员就地展开（YmcFnExtOrder 探针：F(a; extends Base(b); c) 位置序 = a, b, c），同名去重先到者留
  // （FuncDuplicateParams1）；算法段整条链至多一个，否则 omc 报错 "more than one algorithm section"（YmcFnExtAlg 探针）。
  bool collect_fn_sig(Id<SElement> fe, const ScodeScope& fscope, FnSig& sig, std::unordered_set<std::uint32_t>& seen,
                      int depth) {
    if (depth > 20) return false;
    const SElement& el = I_.scode.elems[fe];
    if (el.def.kind != SClassDefKind::Parts) return false;
    if (el.def.external.present) {
      if (sig.ext) return false;
      sig.external = true;
      sig.ext = &el.def.external;
      sig.ext_owner = fe;
    }
    if (!el.def.equations.empty() || !el.def.initial_equations.empty() || !el.def.initial_algorithms.empty()) return false;
    for (Id<SElement> eid : el.def.elements) {
      const SElement& c = I_.scode.elems[eid];
      if (c.kind == SElementKind::Extends) {
        if (c.mod.is_valid() && !I_.scode.mods[c.mod].subs.empty()) return false;   // extends 修饰未做
        ScodeScope bds;
        const Id<SElement> b = resolve_class_path(c.base_path, c.base_leading_dot, fscope, bds);
        if (!b.is_valid() || I_.scode.elems[b].kind != SElementKind::Class) return false;
        bds.insert(bds.begin(), b);
        if (!collect_fn_sig(b, bds, sig, seen, depth + 1)) return false;
        continue;
      }
      if (c.kind != SElementKind::Component) continue;     // 嵌套类（记录等）不入签名
      // protected 的输入 / 输出：NFFunction.paramDirection "Invalid protected variable"（FunctionProtectedInput）。
      if (c.prefixes.vis == SVisibility::Protected && c.attr.dir != SDirection::Bidir) return false;
      // 函数变量带 flow / stream 前缀：omc 告警 "Prefix 'stream' used outside connector declaration"（FunctionStreamPrefix），
      // ymc 不产告警 → 不做（调用方 SKIP）。
      if (c.attr.ct != SConnectorType::Potential) return false;
      if (!seen.insert(c.name.id).second) continue;         // 同名去重
      sig.all.push_back(eid);
      if (c.attr.dir == SDirection::Input) sig.inputs.push_back(eid);
      else if (c.attr.dir == SDirection::Output) sig.outputs.push_back(eid);
      else if (c.prefixes.vis == SVisibility::Protected) sig.locals.push_back(eid);
      else return false;   // 公开非 io 变量 → 无效函数（FunctionNonInputOutputParameter）
    }
    for (const SAlgorithm& a : el.def.algorithms) {
      if (sig.alg) return false;
      sig.alg = &a;
    }
    return !(sig.alg && sig.ext);   // 算法段 + external 段 → omc 报错（ExternalFunctionInvalidSection1）
  }
  // 组件的类型若是记录长类 → 该记录类（+ 其定义作用域链，内→外，含记录本身在首）；否则 invalid。
  Id<SElement> record_class_of(const SElement& c, const ScodeScope& scope, ScodeScope& rscope) {
    ScodeScope ds;
    Id<SElement> e = resolve_class_path(c.type.path, c.type.leading_dot, scope, ds);
    if (!e.is_valid() || I_.scode.elems[e].kind != SElementKind::Class ||
        I_.scode.elems[e].restriction != SRestriction::Record || I_.scode.elems[e].def.kind != SClassDefKind::Parts)
      return {};
    rscope = std::move(ds);
    rscope.insert(rscope.begin(), e);
    return e;
  }
  // 记录值（NFEvalFunction.buildRecordBinding :386-434）：字段声明序；显式给定（构造实参）> 修饰（`R r(x = x)`，在 mod_scope
  // 求值）> 字段默认（在记录作用域求值，已定字段按名可见：`Real y = x`，CevalFuncRecord7）；记录字段递归。缺值：函数变量
  // （allow_undef）按 buildRecordBinding 填 EMPTY 占位（Undef，读到即失败；`output R r` 靠算法逐字段赋值，CevalFuncRecord2），
  // 构造调用缺实参 → nullopt。
  std::optional<CVal> record_value(Id<SElement> rec, const ScodeScope& rscope, Id<SMod> mod, const ScodeScope& mod_scope,
                                   const std::vector<std::pair<Symbol, CVal>>& given, int depth, bool allow_undef) {
    if (depth > 40) return std::nullopt;
    CVal r = CVal::Rec();
    const std::size_t base = citers_.size();
    auto done = [&](std::optional<CVal> v) { citers_.resize(base); return v; };
    for (Id<SElement> fid : I_.scode.elems[rec].def.elements) {
      const SElement& f = I_.scode.elems[fid];
      if (f.kind == SElementKind::Extends) return done(std::nullopt);   // 记录继承未做
      if (f.kind != SElementKind::Component) continue;
      if (!f.attr.dims.empty() || !f.type.dims.empty()) return done(std::nullopt);   // 数组字段未做
      std::optional<CVal> v;
      for (const auto& g : given) if (g.first == f.name) v = g.second;
      Id<SMod> fmod;
      if (mod.is_valid())
        for (const SSubMod& s : I_.scode.mods[mod].subs) if (s.ident == f.name) fmod = s.mod;
      if (!v && fmod.is_valid() && I_.scode.mods[fmod].binding.is_valid())
        v = eval_scode(I_.scode.mods[fmod].binding, mod_scope, depth + 1, true);
      ScodeScope frs;
      const Id<SElement> frec = v ? Id<SElement>{} : record_class_of(f, rscope, frs);
      if (!v && frec.is_valid())
        v = fmod.is_valid() ? record_value(frec, frs, fmod, mod_scope, {}, depth + 1, allow_undef)
                            : record_value(frec, frs, f.mod, rscope, {}, depth + 1, allow_undef);
      if (!v && f.mod.is_valid() && I_.scode.mods[f.mod].binding.is_valid())
        v = eval_scode(I_.scode.mods[f.mod].binding, rscope, depth + 1, true);
      if (!v && allow_undef) {
        CVal u;
        u.k = CVal::K::Undef;
        v = u;
      }
      if (!v) return done(std::nullopt);
      v = coerce_cval(*v, f.type.path);
      if (!v) return done(std::nullopt);
      r.set_field(f.name, *v);
      citers_.push_back({f.name.id, *v});
    }
    return done(r);
  }
  // 局部值上逐段取字段 / 下标（`r.x`、`r2.r1.x`、`a[i].x`）。
  std::optional<CVal> cval_path(const CVal& root, const Cref& c, const ScodeScope& scope, int depth) {
    std::optional<CVal> cur = cval_index(root, c.parts[0].subscripts, 0, scope, depth);
    for (std::size_t k = 1; k < c.parts.size() && cur; ++k) {
      if (cur->k != CVal::K::Record || !cur->rec) return std::nullopt;
      const CVal* fv = nullptr;
      for (const auto& fld : *cur->rec) if (fld.first == c.parts[k].name) { fv = &fld.second; break; }
      if (!fv) return std::nullopt;
      cur = cval_index(*fv, c.parts[k].subscripts, 0, scope, depth);
    }
    return cur;
  }
  // 隐式 for 范围（NFTyping.deduceIterationRange :3826-3936）：迭代变量作下标用到的所有 (数组, 维) 的该维长须相同，
  // 范围 = 1:size(数组, 维)；一处都没有 / 长度不一 → omc 报错 → nullopt。数组取当前局部值的形状。
  void collect_iter_uses(Id<Exp> e, Symbol it, std::vector<std::pair<Symbol, std::size_t>>& uses) {
    if (!e.is_valid()) return;
    const Exp& x = I_.absyn.exps[e];
    if (x.kind == ExpKind::CrefExp) {
      for (const CrefPart& p : I_.absyn.crefs[x.cref].parts)
        for (std::size_t k = 0; k < p.subscripts.size(); ++k) {
          const Exp& s = I_.absyn.exps[p.subscripts[k]];
          if (s.kind == ExpKind::CrefExp && I_.absyn.crefs[s.cref].parts.size() == 1 &&
              I_.absyn.crefs[s.cref].parts[0].subscripts.empty() && I_.absyn.crefs[s.cref].parts[0].name == it)
            uses.push_back({p.name, k});
          collect_iter_uses(p.subscripts[k], it, uses);
        }
      return;
    }
    collect_iter_uses(x.a, it, uses);
    collect_iter_uses(x.b, it, uses);
    collect_iter_uses(x.c, it, uses);
    for (Id<Exp> a : x.args) collect_iter_uses(a, it, uses);
  }
  void collect_iter_uses_stmts(const std::vector<Id<SStatement>>& body, Symbol it,
                               std::vector<std::pair<Symbol, std::size_t>>& uses) {
    for (Id<SStatement> sid : body) {
      const SStatement& s = I_.scode.stmts[sid];
      collect_iter_uses(s.a, it, uses);
      collect_iter_uses(s.b, it, uses);
      for (const SStmtBranch& br : s.branches) { collect_iter_uses(br.cond, it, uses); collect_iter_uses_stmts(br.body, it, uses); }
      collect_iter_uses_stmts(s.body, it, uses);
      collect_iter_uses_stmts(s.else_body, it, uses);
    }
  }
  std::optional<std::vector<std::int64_t>> implicit_range(const SStatement& st) {
    std::vector<std::pair<Symbol, std::size_t>> uses;
    collect_iter_uses_stmts(st.body, st.index, uses);
    if (uses.empty() || !locals_) return std::nullopt;
    std::int64_t n = -1;
    for (const auto& u : uses) {
      auto lit = locals_->find(u.first.id);
      if (lit == locals_->end()) return std::nullopt;
      const std::vector<std::int64_t> sh = cval_shape(lit->second);
      if (u.second >= sh.size()) return std::nullopt;
      if (n >= 0 && sh[u.second] != n) return std::nullopt;   // INCOMPATIBLE_IMPLICIT_RANGES
      n = sh[u.second];
    }
    std::vector<std::int64_t> r;
    for (std::int64_t i = 1; i <= n; ++i) r.push_back(i);
    return r;
  }

  // 用户函数常量折叠：解析函数（限定名 / 别名链，见 resolve_fn）+ 签名（extends 展开，见 collect_fn_sig），实参按位置 /
  // 命名绑定输入，缺省取默认（别名修饰优先；记录输入无默认 → 取记录字段默认值，RecordBinding8），protected 局部按依赖序
  // 初始化，解释算法段，返回**首个**输出（多输出函数在表达式里取首输出，FunctionMultiOutput3）。记录类名的调用 = 记录构造
  // （`R(2)`，FuncDefaultArg4）。任何超范围 → nullopt（调用方 SKIP / 落回符号调用）。
  // allow_param：显式实参 / 别名修饰可取 parameter 的值（结构语境）；类型化表达式路径传 false——参数实参的调用变率是
  // parameter，omc 不求值（YmcFnPar1 探针：`g(p)` 保留）。
  // all_outputs 非空：另把全部输出（按声明序、各自按类型强转）填进去（元组赋值 / 元组方程 `(a, b) := f(…)` 用）。
  // noret：调用是一条无返回值调用语句——没有输出的函数也把体求值一遍（体里的 assert 会让求值失败），成功返回占位值。
  std::optional<CVal> eval_user_call(const Exp& call, const ScodeScope& scope, int depth, bool allow_param = true,
                                     std::vector<CVal>* all_outputs = nullptr, bool noret = false) {
    if (depth > 100) return std::nullopt;
    // 实参在调用处求值（可用实例帧）；函数体 / 记录构造在定义处求值，实例帧不适用（函数不能引用模型参数）。
    struct FrameOff {
      Flattener& f;
      int saved;
      bool off = false;
      ~FrameOff() { if (off) f.ceval_frame_ = saved; }
    } frame_off{*this, ceval_frame_};
    auto frame_off_now = [&]() { frame_off.off = true; ceval_frame_ = -1; };
    const Cref& fn = I_.absyn.crefs[call.cref];
    // 记录构造 `R(2)` / `R(x = 2)`：实参按字段声明序 / 名字，缺省取字段默认。
    if (std::optional<FnRef> rr = resolve_fn(fn, scope, SRestriction::Record)) {
      std::vector<Symbol> fields;
      for (Id<SElement> fid : I_.scode.elems[rr->fe].def.elements)
        if (I_.scode.elems[fid].kind == SElementKind::Component) fields.push_back(I_.scode.elems[fid].name);
      std::vector<std::pair<Symbol, CVal>> given;
      std::size_t pos = 0;
      for (std::size_t i = 0; i < call.args.size(); ++i) {
        std::optional<CVal> v = eval_scode(call.args[i], scope, depth + 1, allow_param);
        if (!v) return std::nullopt;
        if (i < call.arg_names.size() && call.arg_names[i].is_valid()) given.push_back({call.arg_names[i], *v});
        else if (pos < fields.size()) given.push_back({fields[pos++], *v});
        else return std::nullopt;
      }
      frame_off_now();
      return record_value(rr->fe, rr->fscope, Id<SMod>{}, scope, given, depth + 1, /*allow_undef=*/false);
    }
    std::optional<FnRef> fr = resolve_fn(fn, scope);
    if (!fr) return std::nullopt;
    bool ext_c = false;
    if (std::optional<std::string> bn = builtin_equiv(*fr, &ext_c)) {   // 内建等价："builtin" 语言按内建求值，"C" 不求值
      if (ext_c) return std::nullopt;
      for (Symbol an : call.arg_names) if (an.is_valid()) return std::nullopt;
      std::vector<CVal> bargs;
      for (Id<Exp> a : call.args) {
        std::optional<CVal> v = eval_scode(a, scope, depth + 1, allow_param);
        if (!v) return std::nullopt;
        bargs.push_back(std::move(*v));
      }
      return eval_scalar_builtin(*bn, bargs);
    }
    struct AliasScope {   // 最外层用户函数求值结束时清空别名监视表
      Flattener& f;
      explicit AliasScope(Flattener& x) : f(x) { ++f.alias_eval_depth_; }
      ~AliasScope() { if (--f.alias_eval_depth_ == 0) f.alias_watch_.clear(); }
    } alias_scope{*this};
    const SElement& fel = I_.scode.elems[fr->fe];
    if (fel.purity == Purity::Impure) return std::nullopt;   // 不纯函数不能常量折叠（须发射，ImpureCall1）
    if (fn_annotation_blocks(fel.cmt.annotation)) return std::nullopt;
    FnSig sig;
    std::unordered_set<std::uint32_t> seen;
    if (!collect_fn_sig(fr->fe, fr->fscope, sig, seen, 0) || sig.external || (sig.outputs.empty() && !noret)) return std::nullopt;
    // derivative / inverse 注解形态不在已做范围内（omc 的告警 / 报错，FunctionInverseInvalid1/2）→ 不求值（发射一侧同样拒）。
    if (std::vector<Id<Cref>> refs; !fn_ref_annotations(fel.cmt.annotation, sig, refs)) return std::nullopt;
    const std::vector<Id<SElement>>& inputs = sig.inputs;
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
      std::optional<CVal> v = eval_scode(bound[i], scope, depth + 1, allow_param);
      if (!v) return std::nullopt;
      {   // 实参秩高于内建标量类型形参的声明秩（`f({1, 2, 3})`，f 的输入是 Real）= 向量化调用：typing 改写成
          // `array(f(x[$i]) for $i in …)`，omc 不求值（FuncVectorization3）→ 不折。
        const SElement& ie = I_.scode.elems[inputs[i]];
        const std::size_t decl_rank = ie.attr.dims.size() + ie.type.dims.size();
        const bool builtin_scalar = ie.type.path.size() == 1 && !ie.type.leading_dot &&
                                    (in_.str(ie.type.path[0]) == "Real" || in_.str(ie.type.path[0]) == "Integer" ||
                                     in_.str(ie.type.path[0]) == "Boolean" || in_.str(ie.type.path[0]) == "String");
        if (builtin_scalar && cval_shape(*v).size() > decl_rank) return std::nullopt;
      }
      v = coerce_cval(*v, I_.scode.elems[inputs[i]].type.path);
      if (!v) return std::nullopt;
      if (I_.absyn.exps[bound[i]].kind == ExpKind::CrefExp) watch_arrays(*v);
      env[I_.scode.elems[inputs[i]].name.id] = *v;
    }
    // 别名修饰（`function g = f(r = x)`）= 输入默认（外层别名优先），在调用者上下文求值。
    for (Id<SMod> am : fr->alias_mods)
      for (const SSubMod& s : I_.scode.mods[am].subs)
        for (std::size_t i = 0; i < inputs.size(); ++i)
          if (!bound[i].is_valid() && I_.scode.elems[inputs[i]].name == s.ident && I_.scode.mods[s.mod].binding.is_valid() &&
              !env.count(s.ident.id)) {
            std::optional<CVal> v = eval_scode(I_.scode.mods[s.mod].binding, scope, depth + 1, allow_param);
            if (!v) return std::nullopt;
            v = coerce_cval(*v, I_.scode.elems[inputs[i]].type.path);
            if (!v) return std::nullopt;
            env[s.ident.id] = *v;
          }
    // 切到函数局部环境；函数体按定义处作用域求值，调用处前缀只对模型内嵌函数保留（local_to_caller）。
    frame_off_now();
    const ScodeScope& fscope = fr->fscope;
    std::unordered_set<std::uint32_t> inset;
    for (Id<SElement> ii : inputs) inset.insert(I_.scode.elems[ii].name.id);
    auto* saved = locals_;
    auto* saved_in = input_names_;
    const std::vector<Symbol>* saved_pfx = ceval_prefix_;
    locals_ = &env;
    input_names_ = &inset;
    if (!fr->local_to_caller) ceval_prefix_ = nullptr;
    auto restore = [&]() { locals_ = saved; input_names_ = saved_in; ceval_prefix_ = saved_pfx; };
    auto fail = [&]() -> std::optional<CVal> { restore(); return std::nullopt; };
    // 缺省输入（声明序，可引用前面输入）；记录输入无默认 → 记录字段默认值（buildRecordBinding）。
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      const SElement& in = I_.scode.elems[inputs[i]];
      if (env.count(in.name.id)) continue;
      std::optional<CVal> v;
      if (in.mod.is_valid() && I_.scode.mods[in.mod].binding.is_valid()) {
        v = eval_scode(I_.scode.mods[in.mod].binding, fscope, depth + 1, true);
      } else {
        ScodeScope rs;
        const Id<SElement> rec = record_class_of(in, fscope, rs);
        if (rec.is_valid()) v = record_value(rec, rs, in.mod, fscope, {}, depth + 1, /*allow_undef=*/true);
      }
      if (!v) return fail();   // 无实参无默认
      v = coerce_cval(*v, in.type.path);
      if (!v) return fail();
      env[in.name.id] = *v;
    }
    // 输出与 protected 局部的初值（NFEvalFunction.createArgumentMap :280-294：绑定互相代换后求值 → 与声明序无关，
    // FunctionCompOrder4）：绑定 / 记录（修饰 + 字段默认）/ 无绑定数组（Undef 占位）。有绑定却求不出的留到依赖就绪再试，
    // 仍求不出 → 整个折叠失败。
    std::vector<Id<SElement>> pending;
    for (Id<SElement> eid : sig.outputs) pending.push_back(eid);
    for (Id<SElement> eid : sig.locals) pending.push_back(eid);
    auto init_one = [&](const SElement& c) -> std::optional<CVal> {
      ScodeScope rs;
      const Id<SElement> rec = record_class_of(c, fscope, rs);
      if (rec.is_valid()) return record_value(rec, rs, c.mod, fscope, {}, depth + 1, /*allow_undef=*/true);
      if (c.mod.is_valid() && I_.scode.mods[c.mod].binding.is_valid()) {
        std::optional<CVal> v = eval_scode(I_.scode.mods[c.mod].binding, fscope, depth + 1, true);
        return v ? coerce_cval(*v, c.type.path) : std::nullopt;
      }
      return undef_array(c, fscope, depth);
    };
    for (bool progress = true; progress && !pending.empty();) {
      progress = false;
      for (auto it = pending.begin(); it != pending.end();) {
        const SElement& c = I_.scode.elems[*it];
        std::optional<CVal> v = init_one(c);
        if (v) {
          env[c.name.id] = std::move(*v);
          it = pending.erase(it);
          progress = true;
        } else {
          ++it;
        }
      }
    }
    for (Id<SElement> eid : pending) {   // 剩下的：无绑定标量（留空，待算法赋值）可以；有绑定 / 记录求不出 → 失败
      const SElement& c = I_.scode.elems[eid];
      ScodeScope rs;
      if ((c.mod.is_valid() && I_.scode.mods[c.mod].binding.is_valid()) || record_class_of(c, fscope, rs).is_valid()) return fail();
    }
    // 算法段：直线赋值 + if/for/while/return（exec_body 返回控制信号）。
    if (sig.alg) {
      const Flow f = exec_body(sig.alg->stmts, fscope, depth + 1);
      if (f == Flow::Fail || f == Flow::Break) return fail();   // 顶层 break（不在循环内）是错误（Break2）→ SKIP
    }
    if (sig.outputs.empty()) { restore(); return CVal::I(0); }   // noret：没有输出可读
    // 读首个输出（all_outputs：全部输出，任一读不到 → 失败）。
    const SElement& oel = I_.scode.elems[sig.outputs[0]];
    auto it = env.find(oel.name.id);
    std::optional<CVal> result;
    if (it != env.end()) result = coerce_cval(it->second, oel.type.path);
    if (all_outputs && result) {
      all_outputs->clear();
      for (Id<SElement> o : sig.outputs) {
        const SElement& oe = I_.scode.elems[o];
        auto ot = env.find(oe.name.id);
        std::optional<CVal> ov = ot != env.end() ? coerce_cval(ot->second, oe.type.path) : std::nullopt;
        if (!ov || ov->k == CVal::K::Undef) { result.reset(); break; }
        all_outputs->push_back(std::move(*ov));
      }
    }
    restore();
    return result;
  }

  // 函数里无绑定的数组变量初值：各维在函数局部环境里求整数（可依赖输入），元素为 Undef 占位（NFEvalFunction.buildBinding
  // :342-365 用 EMPTY 填）。非数组 / 维求不出（含 `:`）→ nullopt。
  std::optional<CVal> undef_array(const SElement& c, const ScodeScope& fscope, int depth) {
    std::vector<Id<Exp>> ds = c.attr.dims;
    ds.insert(ds.end(), c.type.dims.begin(), c.type.dims.end());
    if (ds.empty()) return std::nullopt;
    std::vector<std::int64_t> dims;
    for (Id<Exp> d : ds) {
      std::optional<CVal> dv = eval_scode(d, fscope, depth + 1, true);
      if (!dv || dv->k != CVal::K::Int || dv->i < 0) return std::nullopt;
      dims.push_back(dv->i);
    }
    CVal u;
    u.k = CVal::K::Undef;
    return cval_fill(u, dims);
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
  // written：带步长的范围按书写求出的 {步长, 终点}（推导式的范围照写打印：`for i in 1:2:4`）。
  std::optional<std::vector<std::int64_t>> eval_range(Id<Exp> rexp, const ScodeScope& fscope, int depth,
                                                      std::pair<std::int64_t, std::int64_t>* written = nullptr) {
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
    if (written) *written = {step, b};
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
        // 下标赋值只做单名局部数组（`x[i] :=`、`table[:, 1] :=`）；记录字段带下标未做。
        const bool subscripted = lc.parts.size() == 1 && !lc.parts[0].subscripts.empty();
        for (const CrefPart& p : lc.parts) if (!p.subscripts.empty() && !subscripted) return Flow::Fail;
        if (input_names_ && input_names_->count(lc.parts[0].name.id)) return Flow::Fail;   // 给输入赋值 = 错误（AssignInput1）
        std::optional<CVal> v = eval_scode(st.b, fscope, depth + 1, true);
        if (!v) return Flow::Fail;
        if (subscripted) {
          auto it = locals_->find(lc.parts[0].name.id);
          if (it == locals_->end()) return Flow::Fail;
          CVal target = it->second;   // 共享存储，cval_assign 写时复制
          if (!cval_assign(target, lc.parts[0].subscripts, 0, *v, fscope, depth)) return Flow::Fail;
          (*locals_)[lc.parts[0].name.id] = std::move(target);
          return Flow::Normal;
        }
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
        // 隐式范围 `for i loop`：按迭代变量作下标的数组推 1:size(x, k)（implicit_range，ImplicitRangeAlg2）。
        auto seq = st.a.is_valid() ? eval_range(st.a, fscope, depth) : implicit_range(st);
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
      case SStmtKind::Assert: {
        // 条件为真 → 继续。为假：error 级别 → 求值失败（omc 留下调用，C1 探针 `g(-2.0)`）；warning 级别 omc 告警后继续
        //（CevalFuncAssert1 的 Result 里带那行告警），ymc 不产告警 → 同样按求值失败（调用方 SKIP）。
        const std::optional<CVal> c = eval_scode(st.a, fscope, depth + 1, true);
        if (!c || c->k != CVal::K::Bool) return Flow::Fail;
        if (c->b) return Flow::Normal;
        return Flow::Fail;
      }
      case SStmtKind::NoRetCall: {
        // 被调函数求值一遍（值丢弃，没有输出的也行）；内建 / 求不出 → Fail。
        const Exp& ce = I_.absyn.exps[st.a];
        if (ce.kind != ExpKind::Call) return Flow::Fail;
        return eval_user_call(ce, fscope, depth + 1, true, nullptr, /*noret=*/true) ? Flow::Normal : Flow::Fail;
      }
      default: return Flow::Fail;   // when / terminate / reinit → 未做
    }
  }

  // 记录常量折叠的字段直接发射：`constant R r = f(…)` 折成记录值 rv → 逐字段 `r.field = 值`（继承 r 的变率/final）。
  // 字段须标量内建（非数组/嵌套记录），且折叠值含该字段并能按字段类型强转，否则 false（调用方 SKIP）。
  bool emit_record_fields(CompId cid, const std::vector<Symbol>& name, const std::vector<Symbol>& prefix,
                          const PSubs& psubs, NodeId reci, const CVal& rv, bool is_protected, bool force_final) {
    (void)prefix;
    const SVariability rec_var = I_.comps[cid].attributes.variability;
    const bool rec_fin = I_.comps[cid].attributes.is_final || force_final;
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
      if (!cv || cv->k == CVal::K::Record || cv->k == CVal::K::Undef || cv->k == CVal::K::Array) return false;
      const Type fty = I_.comps[fc].ty;
      const SourceLoc floc = I_.comps[fc].loc;
      // 字段变率 = variabilityMin(记录, 字段自身)、final = 记录 or 字段（mergeComponentAttributes :236/:244；
      // RecordVariability：`abcRec z` 的 parameter 字段 b、constant 字段 c 保留）。
      FlatVarKind vk = FlatVarKind::Var;
      switch (var_min(rec_var, I_.comps[fc].attributes.variability)) {
        case SVariability::Var: vk = FlatVarKind::Var; break;
        case SVariability::Discrete: vk = FlatVarKind::Discrete; break;
        case SVariability::Param: vk = FlatVarKind::Parameter; break;
        case SVariability::Const: vk = FlatVarKind::Constant; break;
      }
      const bool fin = rec_fin || I_.comps[fc].attributes.is_final;
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
      record_const(result_.model.variables.back());
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
      case CVal::K::Undef: break;    // 未赋值占位不应到这里（coerce_to_kind 已拒）
    }
    return lit_int(0);
  }

  // ---- 已发射常量值表 const_vals_：修饰后的最终值（ymc 的修饰在 flatten 期才落到变量上），供两路求值的 cref 先查 ----
  // key 与 var_key / conn_key 同编码：段 id + 各整数下标 `[v]` + '.'；数组常量另存整体（末段无下标）。
  static std::string int_key(const std::vector<Symbol>& names, const PSubs& isubs) {
    std::string k;
    for (std::size_t i = 0; i < names.size(); ++i) {
      k += std::to_string(names[i].id);
      if (i < isubs.size())
        for (std::int64_t v : isubs[i]) { k.push_back('['); k += std::to_string(v); k.push_back(']'); }
      k.push_back('.');
    }
    return k;
  }
  std::optional<CVal> cval_of(Id<Expr> x) {
    const Expr& e = result_.model.exprs[x];
    switch (e.kind) {
      case ExprKind::IntLit: return CVal::I(e.ival);
      case ExprKind::RealLit: return CVal::R(e.rval);
      case ExprKind::BoolLit: return CVal::B(e.bval);
      case ExprKind::StringLit: return CVal::S(e.sval);
      default: return std::nullopt;   // 枚举字面量等 CVal 无对应 → 不记
    }
  }
  void record_const(const FlatVariable& v) {
    if (v.var_kind == FlatVarKind::Parameter) ++param_groups_[int_key(v.name, {})].count;
    if (!v.binding.is_valid() || (v.var_kind != FlatVarKind::Constant && v.var_kind != FlatVarKind::Parameter)) return;
    std::optional<CVal> cv = cval_of(v.binding);
    if (!cv) return;
    if (v.var_kind == FlatVarKind::Constant) { const_vals_[var_key(v)] = std::move(*cv); return; }
    // 参数只记字面量绑定的（见 const_lookup_val），另按名字链（不带下标）分组：同一组件的各元素。
    std::string key = var_key(v);
    param_groups_[int_key(v.name, {})].keys.push_back(key);
    param_lits_[std::move(key)] = std::move(*cv);
  }
  // 行主序扁平值 + 形状 → 嵌套数组值。
  static CVal cval_nest(const std::vector<CVal>& flat, const std::vector<std::int64_t>& dims, std::size_t k, std::size_t& pos) {
    if (k == dims.size()) return flat[pos++];
    if (dims[k] == 0) return empty_arr(std::vector<std::int64_t>(dims.begin() + static_cast<std::ptrdiff_t>(k), dims.end()));
    std::vector<CVal> els;
    for (std::int64_t i = 0; i < dims[k]; ++i) els.push_back(cval_nest(flat, dims, k + 1, pos));
    return CVal::Arr(std::move(els));
  }
  // 值求值路（eval_scode）：书写处前缀（含逐段下标）+ cref 各段（常量整数下标）→ const_vals_ 里的值。
  std::optional<CVal> const_lookup_val(const std::vector<Symbol>& prefix, const PSubs& psubs, const Cref& c,
                                       const ScodeScope& scope, int depth, bool allow_param = false) {
    std::vector<Symbol> names = prefix;
    PSubs isubs = psubs;
    isubs.resize(prefix.size());
    for (const CrefPart& p : c.parts) {
      names.push_back(p.name);
      std::vector<std::int64_t> is;
      for (Id<Exp> s : p.subscripts) {
        std::optional<CVal> sv = eval_scode(s, scope, depth + 1);
        if (!sv || sv->k != CVal::K::Int) return std::nullopt;
        is.push_back(sv->i);
      }
      isubs.push_back(std::move(is));
    }
    const std::string key = int_key(names, isubs);
    auto it = const_vals_.find(key);
    if (it != const_vals_.end()) return it->second;
    // 已发射的字面量绑定参数（子实例 / 记录参数的字段 `a.p`、`r.n`；本实例的单段参数走 frame_value）：编译期求值用到它
    // → 结构参数（同 frame_value 的标记规则：dep_mark_ 时才标）。
    if (!allow_param || c.parts.size() < 2) return std::nullopt;
    const auto pl = param_lits_.find(key);
    if (pl == param_lits_.end()) return std::nullopt;
    // 标记是组件级的：数组实例 / 参数数组的各元素一起成为结构参数（ND 探针 d2：`G g[2](each n = 2); Real z[g[2].n]` →
    // g[1].n、g[2].n 都 final；同 frame_value 对本地参数数组的做法）。有元素不是字面量绑定 → 整组标不了，未做。
    const ParamGroup& g = param_groups_[int_key(names, {})];
    if (g.keys.size() != g.count) return std::nullopt;
    for (const std::string& k : g.keys) {
      if (dep_mark_) record_structural(k);
      const_vals_[k] = param_lits_[k];
    }
    return param_lits_[key];
  }
  // 类型化路（cref()）：同上，下标须字面量 / for 迭代变量；只接标量值。
  Id<Expr> const_lookup_expr(const std::vector<Symbol>& prefix, const Cref& c) {
    std::vector<Symbol> names = prefix;
    PSubs isubs = cur_psubs_;
    isubs.resize(prefix.size());
    for (const CrefPart& p : c.parts) {
      names.push_back(p.name);
      std::vector<std::int64_t> is;
      for (Id<Exp> s : p.subscripts) {
        std::optional<std::int64_t> iv = subscript_int(s);
        if (!iv) return {};
        is.push_back(*iv);
      }
      isubs.push_back(std::move(is));
    }
    auto it = const_vals_.find(int_key(names, isubs));
    if (it == const_vals_.end() || it->second.k == CVal::K::Record) return {};
    // 整体 constant 数组引用 `x = a`、`a .+ b`、`sum(a)`：内联成数组字面量（NFEvalConstants 代入常量，CE3 探针：
    // `y = {4.0, 8.0, 12.0}`、`s = 24.0`、方程逐元素折叠）。
    if (is_arr(it->second)) return literal_any(it->second);
    return literal_of(it->second);
  }
  // 值 → 字面量；数组值 → 嵌套数组字面量（ty = 元素类型 + 各维，不用 inst_exp 的粗类型）。记录 / 未赋值 / 空数组 → invalid。
  Id<Expr> literal_any(const CVal& v) {
    if (v.k == CVal::K::Record || v.k == CVal::K::Undef) return {};
    if (!is_arr(v)) return literal_of(v);
    if (!v.arr || v.arr->empty()) return {};
    Expr arr;
    arr.kind = ExprKind::Array;
    for (const CVal& e : *v.arr) {
      const Id<Expr> x = literal_any(e);
      if (!x.is_valid()) return {};
      arr.args.push_back(x);
    }
    Dimension d;
    d.kind = DimKind::Integer;
    d.size = static_cast<std::int64_t>(arr.args.size());
    arr.ty = Type::array_of(result_.model.exprs[arr.args[0]].ty, {d});   // array_of 把子数组的维接在后面
    return add(std::move(arr));
  }
  // 值求值的书写处上下文（RAII）：期间 eval_scode 的 cref 先按此前缀查 const_vals_。
  struct CevalCtx {
    Flattener& f;
    const std::vector<Symbol>* saved_prefix;
    PSubs saved_psubs;
    CevalCtx(Flattener& fl, const std::vector<Symbol>& p, const PSubs& s)
        : f(fl), saved_prefix(fl.ceval_prefix_), saved_psubs(fl.ceval_psubs_) {
      f.ceval_prefix_ = &p;
      f.ceval_psubs_ = s;
      f.ceval_psubs_.resize(p.size());
    }
    ~CevalCtx() { f.ceval_prefix_ = saved_prefix; f.ceval_psubs_ = std::move(saved_psubs); }
    CevalCtx(const CevalCtx&) = delete;
    CevalCtx& operator=(const CevalCtx&) = delete;
  };
  // 编译期求值的帧跟着表达式的**书写处实例**走：外层写的修饰表达式（`M m(v = c[i])`、`m(x(fixed = b))`）里的参数 / 常量是
  // 外层实例的，不是当前被修饰实例里的同名量（AO 探针 a6：此前按内层帧取值 → `m.v = c[2]`、把内层同名参数标成 final，omc
  // 是 `c[1]`、外层 i final）。scope 所在的帧不在栈上 / 就是当前帧 / 当前无帧（函数求值）→ 不动。
  struct FrameAt {
    Flattener& f;
    int saved;
    std::optional<CevalCtx> ctx;
    FrameAt(Flattener& fl, NodeId scope) : f(fl), saved(fl.ceval_frame_) {
      if (saved < 0 || !scope.is_valid() || f.frames_[static_cast<std::size_t>(saved)].inst.raw == scope.raw) return;
      const int fi = f.frame_of(scope);
      if (fi < 0 || fi == saved) return;
      f.ceval_frame_ = fi;
      ctx.emplace(f, f.frames_[static_cast<std::size_t>(fi)].prefix, f.frames_[static_cast<std::size_t>(fi)].psubs);
    }
    ~FrameAt() {
      ctx.reset();
      f.ceval_frame_ = saved;
    }
    FrameAt(const FrameAt&) = delete;
    FrameAt& operator=(const FrameAt&) = delete;
  };
  // 按声明元素类型强转值（Int→Real 加宽；收窄 / 类型不符 → nullopt）。
  static std::optional<CVal> coerce_to_kind(const CVal& v, TypeKind k) {
    switch (k) {
      case TypeKind::Real:
        if (v.k == CVal::K::Int) return CVal::R(static_cast<double>(v.i));
        return v.k == CVal::K::Real ? std::optional<CVal>(v) : std::nullopt;
      case TypeKind::Integer: return v.k == CVal::K::Int ? std::optional<CVal>(v) : std::nullopt;
      case TypeKind::Boolean: return v.k == CVal::K::Bool ? std::optional<CVal>(v) : std::nullopt;
      case TypeKind::String: return v.k == CVal::K::Str ? std::optional<CVal>(v) : std::nullopt;
      default: return std::nullopt;
    }
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

  // 矩阵构造 `[a, b; c, d]`（typeMatrix / typeMatrixComma，NFTyping.mo:2294-2393）：每个元素提升到 2 维（标量 → 1×1、
  // 向量 → n×1 列、矩阵不变），行内按第 2 维、行间按第 1 维拼接（makeCatExp，NFBuiltinCall.mo:227-343），元素转公共类型；
  // 压平后展开成嵌套数组字面量（M1 探针：`[a, a]` → `{{a[1], a[1]}, {a[2], a[2]}}`、`[1, 2.5; x, 4]` → `{{1.0, 2.5}, {x, 4.0}}`）。
  // 元素须能展开成已知形状的标量网格（字面量 / 已知维 cref 及其逐元素运算）；拼接维不符（omc 报错）/ 其余 → SKIP。
  Id<Expr> matrix_exp(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    using Grid = std::vector<std::vector<Id<Expr>>>;   // 行 × 列的标量元素
    const std::vector<Id<Exp>> rows = e.args;          // 拷贝：inst 期间不持 absyn 引用
    Grid all;
    for (Id<Exp> row : rows) {
      const std::vector<Id<Exp>> els = I_.absyn.exps[row].args;
      Grid block;
      for (Id<Exp> el : els) {
        const Id<Expr> x = inst_exp(el, enclosing, prefix);
        if (result_.unsupported) return lit_int(0);
        const std::vector<std::int64_t> sh = expr_shape(x);
        Grid g;
        if (sh.empty()) {
          g = {{x}};
        } else if (sh.size() == 1 && sh[0] > 0) {
          for (std::int64_t i = 1; i <= sh[0]; ++i) g.push_back({scalarize(x, {i})});
        } else if (sh.size() == 2 && sh[0] > 0 && sh[1] > 0) {
          for (std::int64_t i = 1; i <= sh[0]; ++i) {
            g.emplace_back();
            for (std::int64_t j = 1; j <= sh[1]; ++j) g.back().push_back(scalarize(x, {i, j}));
          }
        } else {
          set_unsupported("matrix element shape"); return lit_int(0);
        }
        if (result_.unsupported) return lit_int(0);
        if (block.empty()) { block = std::move(g); continue; }
        if (g.size() != block.size()) { set_unsupported("matrix concatenation dimensions"); return lit_int(0); }
        for (std::size_t i = 0; i < g.size(); ++i) block[i].insert(block[i].end(), g[i].begin(), g[i].end());
      }
      if (!all.empty() && (block.empty() || block[0].size() != all[0].size())) {
        set_unsupported("matrix concatenation dimensions"); return lit_int(0);
      }
      for (std::vector<Id<Expr>>& r : block) all.push_back(std::move(r));
    }
    if (all.empty() || all[0].empty()) { set_unsupported("empty matrix"); return lit_int(0); }
    // 公共元素类型（matchExpressions：Integer 与 Real 取 Real；其余须相同）。
    TypeKind common = TypeKind::Unknown;
    for (const std::vector<Id<Expr>>& r : all)
      for (Id<Expr> x : r) {
        const TypeKind k = result_.model.exprs[x].ty.kind;
        if (common == TypeKind::Unknown || (common == TypeKind::Integer && k == TypeKind::Real)) common = k;
        else if (k != common && !(common == TypeKind::Real && k == TypeKind::Integer)) {
          set_unsupported("matrix element type mismatch"); return lit_int(0);
        }
      }
    const std::int64_t nr = static_cast<std::int64_t>(all.size()), nc = static_cast<std::int64_t>(all[0].size());
    Dimension dr, dc;
    dr.kind = dc.kind = DimKind::Integer;
    dr.size = nr;
    dc.size = nc;
    Expr m;
    m.kind = ExprKind::Array;
    m.ty = Type::array_of(Type::of(common), {dr, dc});
    for (std::vector<Id<Expr>>& r : all) {
      Expr row;
      row.kind = ExprKind::Array;
      row.ty = Type::array_of(Type::of(common), {dc});
      for (Id<Expr> x : r) row.args.push_back(common == TypeKind::Real ? to_real(x) : x);
      m.args.push_back(add(std::move(row)));
    }
    return add(std::move(m));
  }

  // ---- instExpressions：Absyn Exp → 类型化 Expr（在 model.exprs）----
  // 解析 cref（对 enclosing 的 FlatTree 组件），本地 cref 前缀改写；标量定型 + 整型→实型字面量折叠。
  Id<Expr> inst_exp(Id<Exp> ae, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const FrameAt frame_at(*this, enclosing);   // 式子里的编译期求值（下标 / 范围 / 维实参）按书写处实例的帧
    const Exp& e = I_.absyn.exps[ae];
    switch (is_array_ctor(ae) ? ExpKind::Array : e.kind) {   // `array(…)` = `{…}`（MLS §10.4.1；FuncMap1 的推导式、ES 探针 e5）
      case ExpKind::IntLit: return lit_int(e.ival);
      case ExpKind::RealLit: return lit_real(e.rval);
      case ExpKind::BoolLit: return lit_bool(e.bval);
      case ExpKind::StringLit: return lit_string(e.sval);
      case ExpKind::CrefExp: return cref(e.cref, enclosing, prefix);
      case ExpKind::End:   // 函数体下标里的 end（fn_local_cref 置为所在维大小）；其余语境未做
        if (fn_end_.is_valid()) return fn_end_;
        set_unsupported("expression kind");
        return lit_int(0);
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
        // 推导式 `{e for i in r}`（模型语境）→ Reduction `array(e for i in r)`（comprehension_expr）。
        if (!fn_scope_ && e.args.size() == 1 && I_.absyn.exps[e.args[0]].kind == ExpKind::Reduction)
          return comprehension_expr(e.args[0], enclosing, prefix);
        Expr r;
        r.kind = ExprKind::Array;
        r.ty = Type::real();   // 元素类型粗略；dump 不看，强转在 emit_array/coerce_deep 里做
        std::vector<Id<Expr>> els;
        for (Id<Exp> a : e.args) els.push_back(inst_exp(a, enclosing, prefix));
        r.args = std::move(els);
        return add(std::move(r));
      }
      case ExpKind::Matrix: return matrix_exp(e, enclosing, prefix);
      case ExpKind::Subscript: return subscripted_exp(e, enclosing, prefix);
      case ExpKind::Range: {   // a:b / a:step:b（Absyn 与 flat 同布局：a=起点, b=步长或终点, c=终点）
        Expr r;
        r.kind = ExprKind::Range;
        r.a = inst_exp(e.a, enclosing, prefix);
        r.b = inst_exp(e.b, enclosing, prefix);
        if (e.c.is_valid()) r.c = inst_exp(e.c, enclosing, prefix);
        auto ik = [&](Id<Expr> x) { return result_.model.exprs[x].ty.kind; };   // 迭代变量类型 = 范围元素类型
        auto num = [](TypeKind k) { return k == TypeKind::Integer || k == TypeKind::Real; };
        const bool has_step = r.c.is_valid();
        Id<Expr>& stop = has_step ? r.c : r.b;
        const TypeKind ks = ik(r.a);   // 起点的**原**类型
        if (num(ks) && num(ik(stop)) && (!has_step || num(ik(r.b)))) {
          // 数值范围的配型（typeRange，NFTyping.mo:2371-2412 v1.25.0）：起点-终点先配（Integer 的一方转 Real）；有步长时再拿起点
          // 的原类型与步长配、范围类型取这一步的结果，终点转到范围类型。于是起点、步长都是 Integer 而终点是 Real 时，起点已
          // 转成 Real、范围类型却是 Integer：`for i in 1.0:2:n`，迭代变量按 Integer 用（`y * /*Real*/(i)`）——照 omc 的输出做
          //（DM 探针 s6；来自 simulation/modelica 的 Algorithm.mo）。
          bool real = ks == TypeKind::Real || ik(stop) == TypeKind::Real;
          if (real) { r.a = to_real(r.a); stop = to_real(stop); }
          if (has_step) {
            real = ks == TypeKind::Real || ik(r.b) == TypeKind::Real;
            if (real) { r.a = to_real(r.a); r.b = to_real(r.b); stop = to_real(stop); }
          }
          r.ty = real ? Type::real() : Type::integer();
          return add(std::move(r));
        }
        const bool allbool = !has_step && ks == TypeKind::Boolean && ik(r.b) == TypeKind::Boolean;
        r.ty = allbool ? Type::boolean() : Type::real();
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

  // 下标常量值：整型字面量，或 for 方程迭代变量的当前值（ForEquation1 的 `x[i]`）；给了 scope 时再按常量表达式求值
  //（展开后的 `a[i - 1]`、常量 `x[N]`；参数不求——omc 保留符号下标）。否则 nullopt。
  std::optional<std::int64_t> subscript_int(Id<Exp> s, const ScodeScope* scope = nullptr) {
    const Exp& e = I_.absyn.exps[s];
    if (e.kind == ExpKind::IntLit) return e.ival;
    if (e.kind == ExpKind::CrefExp) {
      const Cref& c = I_.absyn.crefs[e.cref];
      if (c.parts.size() == 1 && c.parts[0].subscripts.empty()) {
        auto it = loop_vars_.find(c.parts[0].name.id);
        // Boolean / 枚举的迭代变量不是整数下标（类型维的下标另行按标签定型，见 cref）。
        if (it != loop_vars_.end()) return loop_labels_.count(it->first) ? std::nullopt : std::optional<std::int64_t>(it->second);
      }
    }
    if (scope && e.kind != ExpKind::Colon && e.kind != ExpKind::End)
      if (std::optional<CVal> v = eval_scode(s, *scope, 0, /*allow_param=*/false); v && v->k == CVal::K::Int) return v->i;
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
    // 函数体发射期：形参 / 局部 → kFnLocal 裸名 cref（可带下标，fn_local_cref）；其余名字 = 包常量内联或不可引用。
    if (fn_scope_) {
      if (!c.leading_dot) {
        auto it = func_params_.find(c.parts[0].name.id);
        if (it != func_params_.end())
          return c.parts.size() == 1 ? fn_local_cref(c.parts[0], it->second) : fn_field_cref(c);
      }
      return fn_outer_cref(c);
    }
    // for 方程迭代变量作为值：`… = i` → 内联成整数字面量（ForEquation1 的 RHS）。
    if (!loop_vars_.empty() && c.parts.size() == 1 && c.parts[0].subscripts.empty()) {
      auto it = loop_vars_.find(c.parts[0].name.id);
      if (it != loop_vars_.end()) {
        // Boolean / 枚举范围上的迭代变量：值是当前的标签（`x[i] = i` → `x[E.one] = E.one`）。
        if (auto lb = loop_labels_.find(it->first); lb != loop_labels_.end()) return lb->second;
        return lit_int(it->second);
      }
    }
    Expr e;
    e.kind = ExprKind::Cref;
    std::vector<Id<Expr>> leaf_subs;   // 末段各下标的定型结果（invalid = 照常 inst：符号下标）
    std::vector<bool> sym_label_ok;    // 末段：该下标是核对过类型的 Boolean / 枚举迭代变量（落在同类型的类型维上）
    std::vector<std::int64_t> empty_dims;   // 非空 = 下标引用的叶是空数组（末尾换成 `{}[…]`）
    NodeId local = c.parts.empty() ? Inst::empty_node() : find_local(enclosing, c.parts[0].name);
    // 命中条件为假被移除的组件 → 在方程/绑定里引用它是错误（omc 报错，ConditionInvalidContext1/2/4）→ SKIP。
    if (local.is_valid() && !I_.nodes[local].is_empty() && cref_removed(prefix, c)) {
      set_unsupported("reference to removed conditional component");
      e.ty = Type::real();
      return add(std::move(e));
    }
    // 首段是复合数组却没下标的跨元素取字段 `r.x`（x 为标量内建字段）：ExpandExp 展开成元素数组 `{r[1].x, r[2].x}`
    //（XR1 探针：Var 绑定 `a = {r[1].x, r[2].x}`、参数逐元素 `b[1] = p[1].x`、`sum(r.y)` → `r[1].y + r[2].y`；常量记录数组
    // 的元素再折成值，CevalRecordArray3/5）。叶是数组 / 复合、更深的段 → 下面照旧 SKIP。
    if (local.is_valid() && !I_.nodes[local].is_empty() && I_.nodes[local].kind == NodeKind::Component &&
        c.parts.size() == 2 && c.parts[1].subscripts.empty())
      if (std::optional<Id<Expr>> ex = expand_field_over_array(ac, local, enclosing, prefix)) return *ex;
    // 一般形态的复合数组元素引用（中间段的洞 / 切片、数组字段、多层）：见 expand_composite_path。
    if (local.is_valid() && !I_.nodes[local].is_empty() && I_.nodes[local].kind == NodeKind::Component && c.parts.size() >= 2) {
      if (std::optional<Id<Expr>> ex = expand_composite_path(ac, enclosing, prefix)) return *ex;
      if (result_.unsupported) {
        e.ty = Type::real();
        return add(std::move(e));
      }
    }
    // 非末段带下标（复合数组元素引用 `a[3].x`）：只做首段（本地复合数组组件），下标须是常量 / end / 结构参数且在界内
    //（同末段规则；越界 omc 报错，ArrayBounds2）→ 打印成字面量下标。更深的段带下标、或首段是数组却没下标（`r.x` 跨元素
    // 取字段）→ SKIP。
    std::vector<Id<Expr>> head_subs;   // 首段下标的定型结果（字面量 / 符号式）
    std::vector<std::vector<Id<Expr>>> mid_subs(c.parts.size());   // 中间段（非首非末）的下标：字面量 / 符号式
    // 复合数组段的一个标量下标：界内的常量 / end / 迭代变量 / 结构参数 → 字面量；保留着的迭代变量（for 语句 / 保留的归约、
    // 推导式）与运行时整型量（`a[n].x`）→ 原样留成符号下标，omc 不做越界检查（CA 探针 c2：`for i in 1:3 loop s := s +
    // a[i].x`、`max(a[i].x for i in 1:3)`、`Real z = a[n].x`）。其余 → invalid（调用方 SKIP）。
    auto comp_sub = [&](Id<Exp> sx, std::int64_t dim, const ScodeScope& sc) -> Id<Expr> {
      std::optional<std::int64_t> iv = I_.absyn.exps[sx].kind == ExpKind::End ? std::optional<std::int64_t>(dim)
                                                                              : subscript_int(sx, &sc);
      const bool sym = !iv && is_stmt_iter_subscript(sx);
      if (!iv && !sym && absyn_refs_param(sx, enclosing))
        if (std::optional<CVal> pv = eval_scode(sx, sc, 0, /*allow_param=*/true, nullptr); pv && pv->k == CVal::K::Int)
          iv = pv->i;
      if (iv) return *iv >= 1 && *iv <= dim ? lit_int(*iv) : Id<Expr>{};
      const ExpKind sk = I_.absyn.exps[sx].kind;
      if (sk == ExpKind::Colon || sk == ExpKind::Range || is_array_ctor(sx)) return {};
      if (absyn_refs_param(sx, enclosing)) return {};   // 符号下标里的参数：omc 标结构参数并代入，未做（同末段）
      const Id<Expr> se = simplify_expr(inst_exp(sx, enclosing, prefix));
      if (result_.unsupported) return {};
      const Type& st = result_.model.exprs[se].ty;
      if (st.kind != TypeKind::Integer || st.is_array()) return {};
      if (!sym && !cond_is_runtime(se)) return {};
      return se;
    };
    if (local.is_valid() && !I_.nodes[local].is_empty() && c.parts.size() > 1) {
      // 中间段带下标（`b[1].q[2].x`、`b.q[2].x`，CA 探针 c1）：该段须是字面量整数维的复合数组、下标取尽各维、都是界内的
      // 常量（子实例里的参数维受实例修饰影响，这里求不准 → SKIP；洞 / 切片已由 expand_composite_path 展开）。
      {
        NodeId mc = I_.nodes[local].comp.is_valid() ? I_.comps[I_.nodes[local].comp].class_inst : NodeId{};
        const ScodeScope mscope = scode_scope_of(enclosing);
        for (std::size_t i = 1; i + 1 < c.parts.size(); ++i) {
          const NodeId mn = is_class_inst(mc) ? find_local(mc, c.parts[i].name) : NodeId{};
          if (!is_comp_node(mn)) break;   // 找不到：下面照常报
          const SElement& mel = I_.scode.elems[I_.comps[I_.nodes[mn].comp].def];
          std::vector<std::int64_t> md;
          bool mok = true;
          for (const std::vector<Id<Exp>>* ds : {&mel.attr.dims, &mel.type.dims})
            for (Id<Exp> d : *ds) {
              if (I_.absyn.exps[d].kind == ExpKind::IntLit) md.push_back(I_.absyn.exps[d].ival); else mok = false;
            }
          const std::vector<Id<Exp>>& ms = c.parts[i].subscripts;
          mok = mok && ms.size() == md.size();
          for (std::size_t k = 0; mok && k < ms.size(); ++k) {
            const Id<Expr> sub = comp_sub(ms[k], md[k], mscope);
            if (!sub.is_valid()) mok = false; else mid_subs[i].push_back(sub);
          }
          if (!mok) {
            set_unsupported("composite array element reference");
            e.ty = Type::real();
            return add(std::move(e));
          }
          mc = I_.comps[I_.nodes[mn].comp].class_inst;
        }
      }
      if (I_.nodes[local].kind == NodeKind::Component) {
        const SElement& hcel = I_.scode.elems[I_.comps[I_.nodes[local].comp].def];
        const bool harr = !hcel.attr.dims.empty() || !hcel.type.dims.empty();
        const std::vector<Id<Exp>>& hs = c.parts[0].subscripts;
        bool ok = !harr ? hs.empty() : !hs.empty();
        if (ok && harr) {
          std::vector<std::int64_t> hd;
          const ScodeScope hscope = scode_scope_of(enclosing);
          for (Id<Exp> d : hcel.attr.dims) if (!eval_dim(d, hd, hscope)) ok = false;
          for (Id<Exp> d : hcel.type.dims) if (!eval_dim(d, hd, hscope)) ok = false;
          ok = ok && hd.size() == hs.size();
          for (std::size_t k = 0; ok && k < hs.size(); ++k) {
            const Id<Expr> sub = comp_sub(hs[k], hd[k], hscope);
            if (!sub.is_valid()) ok = false; else head_subs.push_back(sub);
          }
        }
        if (!ok) {
          set_unsupported("composite array element reference");
          e.ty = Type::real();
          return add(std::move(e));
        }
      }
    }
    if (local.is_valid() && !I_.nodes[local].is_empty()) {
      // 多段 `a.x`：逐段深入复合组件解析到叶子，取**叶子**的类型/可变性（否则会用复合类型误加 Cast）。
      NodeId leaf = local, cur = local;
      bool ok = true;
      for (std::size_t i = 1; i < c.parts.size(); ++i) {
        NodeId cls = I_.comps[I_.nodes[cur].comp].class_inst;
        // 内建类型的组件没有子组件（`x.start` → omc "Variable start not found in scope x"，SimpleIntegrator3 / Type7/8；
        // 内建类节点无类树，find_local 会越界）。
        const bool has_tree = cls.is_valid() && !I_.nodes[cls].is_empty() && I_.nodes[cls].kind == NodeKind::Class &&
                              I_.nodes[cls].cls.is_valid() && I_.node_class(cls).tree.is_valid();
        NodeId nxt = has_tree ? find_local(cls, c.parts[i].name) : Inst::empty_node();
        if (!nxt.is_valid() || I_.nodes[nxt].is_empty()) { ok = false; break; }
        // 从外面访问 protected 元素（ProtectedAccess2 `tm.x`）→ omc 报错（ILLEGAL_ACCESS_PROTECTED）。
        if (I_.nodes[nxt].visibility == Visibility::Protected) { set_unsupported("access of protected element"); ok = false; break; }
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
        // constant 叶经模型实例引用（`a1.w`，a1 是模型组件）：omc 同样内联其值 → 取已发射的最终值（func1）。
        if (var != SVariability::Const && c.parts.size() > 1 &&
            I_.comps[I_.nodes[leaf].comp].attributes.variability == SVariability::Const)
          if (Id<Expr> kv = const_lookup_expr(prefix, c); kv.is_valid()) return kv;
        if (var == SVariability::Const) {
          // 空的 constant 数组（emit_const_array 只记了形状）：整体引用 = 嵌套空数组（同下面变量的空数组替换；Array15）。
          if (c.parts.size() == 1 && c.parts[0].subscripts.empty()) {
            std::vector<Symbol> nm = prefix;
            nm.push_back(c.parts[0].name);
            PSubs ps = cur_psubs_;   // 同 leaf_decl_dims：只取前缀各段的下标
            ps.resize(prefix.size());
            if (auto it = empty_shapes_.find(int_key(nm, ps)); it != empty_shapes_.end()) return empty_array_lit(lty, it->second);
          }
          // constant 数组带类型维（`constant Real c[E]`）的下标引用 `c[E.two]`：各下标定成序数，取已发射元素的值。
          if (c.parts.size() == 1 && !c.parts[0].subscripts.empty()) {
            const SElement& tcl = I_.scode.elems[I_.comps[I_.nodes[leaf].comp].def];
            std::vector<std::int64_t> td;
            std::vector<std::vector<Id<Expr>>> tl;
            bool typed = leaf_decl_dims(tcl, c, enclosing, prefix, td, &tl);
            bool any = false;
            for (const std::vector<Id<Expr>>& l : tl) any = any || !l.empty();
            if (typed && any) {
              const std::vector<Id<Exp>> tsubs = c.parts[0].subscripts;   // 拷贝
              const ScodeScope tscope = scode_scope_of(enclosing);
              std::vector<std::int64_t> ords;
              for (std::size_t k = 0; typed && k < tsubs.size() && k < td.size(); ++k) {
                std::optional<std::int64_t> o;
                if (tl[k].empty()) {
                  o = subscript_int(tsubs[k], &tscope);
                } else {
                  const ExpKind tk = I_.absyn.exps[tsubs[k]].kind;
                  if (tk != ExpKind::Colon && tk != ExpKind::Range && tk != ExpKind::End && !is_array_ctor(tsubs[k])) {
                    const Id<Expr> se = simplify_expr(inst_exp(tsubs[k], enclosing, prefix));
                    if (result_.unsupported) return lit_int(0);
                    o = label_ordinal(se, tl[k]);
                  }
                }
                if (!o || *o < 1 || *o > td[k]) typed = false; else ords.push_back(*o);
              }
              if (typed && ords.size() == td.size()) {
                std::vector<Symbol> nm = prefix;
                nm.push_back(c.parts[0].name);
                PSubs ps = cur_psubs_;
                ps.resize(prefix.size());
                ps.push_back(ords);
                if (auto it = const_vals_.find(int_key(nm, ps)); it != const_vals_.end() && !is_arr(it->second) &&
                                                                 it->second.k != CVal::K::Record)
                  return literal_of(it->second);
              }
              set_unsupported("constant evaluation");
              return lit_int(0);
            }
          }
          // 已发射常量的最终值（修饰后，const_vals_）优先：`A a(x = 1)` 里 `z = x + y`（CevalBinding2）。
          if (Id<Expr> kv = const_lookup_expr(prefix, c); kv.is_valid()) return kv;
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
            const bool alit = leaf_decl_dims(lcel, c, enclosing, prefix, ldims);
            if (!alit) {
              set_unsupported("whole-array reference");
            } else {
              std::vector<Dimension> ads;
              ads.reserve(ldims.size());
              for (std::int64_t n : ldims) { Dimension dd; dd.kind = DimKind::Integer; dd.size = n; ads.push_back(dd); }
              e.ty = Type::array_of(lty, std::move(ads));
              // 空数组（某维为 0）：变量不在压平模型里，引用换成其缺省值 = 嵌套空数组（NFInstUtil.replaceEmptyArraysExp
              // :164-205，makeDefaultValue :5270 = fillType）。记录数组未核实 → SKIP。
              if (std::find(ldims.begin(), ldims.end(), 0) != ldims.end()) {
                if (lty.element_type().kind == TypeKind::Complex) set_unsupported("reference to empty record array");
                return empty_array_lit(lty, ldims);
              }
            }
          } else {
            std::vector<std::int64_t> ldims;
            std::vector<std::vector<Id<Expr>>> llabels;   // 类型维的标签（整数维为空）
            const ScodeScope lscope = scode_scope_of(enclosing);
            const bool lit = leaf_decl_dims(lcel, c, enclosing, prefix, ldims, &llabels);
            // 下标可少于维数（隐式尾随 `:`，`x[i]` 引 2 维 x；1.25.0 打印仍只带给出的下标，ImplicitTrailingSubscript 账本），
            // 余下的维接在引用类型后面，标量化时补末尾下标（ImplicitRangeEq3）。
            if (!lit || subs.size() > ldims.size()) {
              set_unsupported("array subscript");
            } else {
              leaf_subs.assign(subs.size(), Id<Expr>{});
              std::vector<Dimension> sdims;   // 切片下标留下的维（按出现序）→ 引用的数组类型
              for (std::size_t k = 0; k < subs.size(); ++k) {
                // for-语句符号迭代变量下标：omc 不静态越界检查、保持符号；其中的参数会被标结构参数并代入数组值
                //（NestedSubscriptFor `x[{1, 2, 3}[i]]`，下标字面量未做）→ SKIP。
                if (is_stmt_iter_subscript(subs[k])) {
                  if (!llabels[k].empty()) {
                    // 类型维上的符号下标：须恰是取同一类型值的迭代变量（`for i in E loop y[i] := …`、`max(y[i] for i in E)`）。
                    const Id<Expr> il = stmt_iter_label(subs[k]);
                    if (il.is_valid() && same_label_type(il, llabels[k][0])) {
                      sym_label_ok.resize(subs.size(), false);
                      sym_label_ok[k] = true;
                      continue;
                    }
                    // Boolean 维：任何 Boolean 类型的符号式都行（`y[not b]`）；枚举维只认迭代变量本身（别的式子的枚举类型
                    // 核对不到）。
                    if (!il.is_valid() && result_.model.exprs[llabels[k][0]].kind == ExprKind::BoolLit) {
                      const Id<Expr> se = simplify_expr(inst_exp(subs[k], enclosing, prefix));
                      if (result_.unsupported) break;
                      if (result_.model.exprs[se].ty.kind == TypeKind::Boolean && !result_.model.exprs[se].ty.is_array()) {
                        leaf_subs[k] = se;
                        continue;
                      }
                    }
                    set_unsupported("type-dimension subscript");
                    break;
                  }
                  if (absyn_refs_param(subs[k], enclosing)) { set_unsupported("parameter in symbolic subscript"); break; }
                  continue;
                }
                // 类型维（`Real y[E]` / `y[Boolean]`）：下标须是该类型的值——字面量、for 方程的迭代变量、能化简成字面量的
                // 式子（EN 探针 t1：`y[E.two]`、`y[true]`）；记序数，收尾换回标签（relabel_type_dims）。整数下标 / 别的枚举 →
                // omc 类型错；`:` 照常（整维）；范围 / 下标数组 / end 未做。
                if (!llabels[k].empty() && I_.absyn.exps[subs[k]].kind != ExpKind::Colon) {
                  const ExpKind tk = I_.absyn.exps[subs[k]].kind;
                  if (tk == ExpKind::End) {   // `y[end]` = 末标签（EN 探针 t3 → `y[E.three]`）
                    leaf_subs[k] = lit_int(ldims[k]);
                    continue;
                  }
                  // 标签切片 `y[E.one:E.two]`、`y[{E.three, E.one}]`：选中的序数表，其余同整数切片（收尾换回标签）。
                  if (tk == ExpKind::Range || (is_array_ctor(subs[k]) && !is_comprehension(subs[k]))) {
                    const std::optional<IterVals> lv = label_iter_values(subs[k], enclosing, prefix);
                    if (result_.unsupported) break;
                    if (!lv || lv->ords.empty() || !same_label_type(lv->type_label, llabels[k][0])) {
                      set_unsupported("type-dimension subscript");
                      break;
                    }
                    leaf_subs[k] = slice_subscript(tk == ExpKind::Range ? ExpKind::Range : ExpKind::Array, lv->ords, false, ldims[k],
                                                   ldims[ldims.size() - subs.size() + k], {1, 0});
                    Dimension sd;
                    sd.kind = DimKind::Integer;
                    sd.size = static_cast<std::int64_t>(lv->ords.size());
                    sdims.push_back(sd);
                    continue;
                  }
                  const Id<Expr> se = simplify_expr(inst_exp(subs[k], enclosing, prefix));
                  if (result_.unsupported) break;
                  const std::optional<std::int64_t> ord = label_ordinal(se, llabels[k]);
                  // Boolean 维上的运行时 Boolean 下标（`y[a > 1]`、`y[c]`）：原样留着（同运行时整型下标，EN 探针 t5）。枚举
                  // 变量作下标的类型核对不到，未做。
                  if (!ord && result_.model.exprs[llabels[k][0]].kind == ExprKind::BoolLit &&
                      result_.model.exprs[se].ty.kind == TypeKind::Boolean && !result_.model.exprs[se].ty.is_array() &&
                      cond_is_runtime(se)) {
                    leaf_subs[k] = se;
                    continue;
                  }
                  if (!ord) { set_unsupported("type-dimension subscript"); break; }
                  leaf_subs[k] = lit_int(*ord);
                  continue;
                }
                // 切片 `:` / `a:b` / `{i, j}`（Subscript.WHOLE / SLICE）：求出选中的下标表，引用类型带这一维；标量化时按表
                // 取元素（SL1 探针：`q = p[{1, 3}]` → `q[2] = p[3]`，`z[2, :] = x` → `z[2,1] = x[1]` …）。
                const ExpKind sk = I_.absyn.exps[subs[k]].kind;
                if (sk == ExpKind::Colon || sk == ExpKind::Range || (is_array_ctor(subs[k]) && !is_comprehension(subs[k]))) {
                  std::pair<std::int64_t, std::int64_t> written{1, 0};
                  const std::optional<std::vector<std::int64_t>> sel = slice_indices(subs[k], ldims[k], lscope, enclosing, &written);
                  if (!sel) { set_unsupported("array slice"); break; }
                  leaf_subs[k] = slice_subscript(sk, *sel, I_.absyn.exps[subs[k]].c.is_valid(), ldims[k],
                                                 ldims[ldims.size() - subs.size() + k], written);
                  Dimension sd;
                  sd.kind = DimKind::Integer;
                  sd.size = static_cast<std::int64_t>(sel->size());
                  sdims.push_back(sd);
                  continue;
                }
                // `end` = 该维大小；常量下标求值；参数变率的下标是结构参数（typeSubscripts 标 Structural.markSubscript，
                // NFTyping.mo:2122-2127）→ 带参数求值（frame_value 记结构参数，post-pass 打 final）。都打印成字面量。
                std::optional<std::int64_t> iv = sk == ExpKind::End ? std::optional<std::int64_t>(ldims[k])
                                                                    : subscript_int(subs[k], &lscope);
                if (!iv && absyn_refs_param(subs[k], enclosing))
                  if (std::optional<CVal> pv = eval_scode(subs[k], lscope, 0, /*allow_param=*/true, nullptr);
                      pv && pv->k == CVal::K::Int)
                    iv = pv->i;
                // 运行时整型下标（引用离散 / 连续变量，`x[n]`、`m[1,n]`、`x[n + 1]`）：omc 保留符号下标、不做静态越界检查
                //（VS1 探针）。
                if (!iv) {
                  const Id<Expr> se = simplify_expr(inst_exp(subs[k], enclosing, prefix));
                  if (result_.unsupported) break;
                  if (result_.model.exprs[se].ty.kind == TypeKind::Integer && cond_is_runtime(se)) {
                    leaf_subs[k] = se;
                    continue;
                  }
                  // 定型化简后已是整数字面量（`y[Integer(i)]`，i 是枚举迭代变量：值求值路没有枚举值）。
                  if (result_.model.exprs[se].kind == ExprKind::IntLit) iv = result_.model.exprs[se].ival;
                }
                if (!iv || *iv < 1 || *iv > ldims[k]) {
                  set_unsupported("array subscript out of range");
                  break;
                }
                leaf_subs[k] = lit_int(*iv);
              }
              for (std::size_t k = subs.size(); k < ldims.size(); ++k) {   // 隐式尾随 `:` 的维
                Dimension td;
                td.kind = DimKind::Integer;
                td.size = ldims[k];
                sdims.push_back(td);
              }
              if (!sdims.empty()) e.ty = Type::array_of(lty, std::move(sdims));
              if (std::find(ldims.begin(), ldims.end(), 0) != ldims.end()) empty_dims = ldims;
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
      if (c.parts.size() >= 2) {
        std::vector<Symbol> tparts;
        bool plain = true;
        for (const CrefPart& p : c.parts) plain = plain && p.subscripts.empty();
        for (std::size_t i = 0; plain && i + 1 < c.parts.size(); ++i) tparts.push_back(c.parts[i].name);
        const SClassDef* ed = nullptr;
        if (plain && enum_type_path(tparts, enclosing, prefix, ed)) {
          if (ed) {
            for (const SEnumItem& it : ed->enum_items)
              if (it.literal == c.parts.back().name) return make_enum_lit(c, enclosing, prefix);
          } else if (const Id<Expr> lv = assertion_level_lit(c); lv.is_valid()) {
            return lv;
          }
        }
      }
      // 限定名 `P.A.B.c` 与单段名都再按 **SCODE 作用域链** 解析到 constant + 值语义求值 → 内联字面量（单段名：继承元素
      // 的绑定在基类的词法作用域里找，Constant8 的 `N` 在 A.B 里）。
      {
        std::vector<Symbol> names;
        bool ok = true;
        for (const CrefPart& p : c.parts) {
          if (!p.subscripts.empty()) { ok = false; break; }
          names.push_back(p.name);
        }
        // 限定名在**声明处**作用域解析（继承成员 = 基类，const16 的 `P.P.i` 要在 P.M 里解析而非派生类）。本地组件节点的
        // parent_scope 为空（inst 只给继承成员记基类实例）→ 声明处即 enclosing。
        const bool inherited_decl = decl_scope_.is_valid() && !I_.nodes[decl_scope_].is_empty() &&
                                    I_.nodes[decl_scope_].parent_scope.is_valid() &&
                                    !I_.nodes[I_.nodes[decl_scope_].parent_scope].is_empty();
        const NodeId qres = inherited_decl ? decl_scope_ : enclosing;
        Id<SElement> qcomp;
        ScodeScope qscope;
        if (ok && resolve_qual_const(names, c.leading_dot, scode_scope_of(qres), qcomp, qscope)) {
          const Id<SMod> m = I_.scode.elems[qcomp].mod;
          if (m.is_valid() && I_.scode.mods[m].binding.is_valid()) {
            std::optional<CVal> v = eval_scode(I_.scode.mods[m].binding, qscope, 0);
            if (v)
              if (const Id<Expr> lit = literal_any(*v); lit.is_valid()) return lit;   // 数组常量 → 数组字面量（PA1 `y = P.x`）
          }
        }
        // 带下标 / 中间段是记录常量（`P.cr.r`、`P.cr[1].r1.r`、`.P.x[2]`）：沿路径取值（eval_qual_path，RecordConstant1-3）。
        if (!ok || c.parts.size() > 1)
          if (std::optional<CVal> v = eval_qual_path(c, scode_scope_of(qres), 0, /*allow_param=*/false, nullptr))
            if (const Id<Expr> lit = literal_any(*v); lit.is_valid()) return lit;
      }
      // 单段未解析：只有内建变量 `time` 合法（MLS §3.6.7）；其余（import 别名 Import6、外层非常量组件 Lookup3…）ymc 未做
      // 或 omc 报错 → unsupported。多段解析不出（package 常量非字面量、redeclare、跨包 extends…）→ omc 会内联成值 → 同。
      if (c.parts.size() > 1) set_unsupported("qualified/cross-scope reference");
      else if (in_.str(c.parts[0].name) != "time") set_unsupported("unresolved name");
      for (const CrefPart& p : c.parts)
        if (!p.subscripts.empty()) set_unsupported("subscript on non-array");   // `time[2]` → omc 报错
      e.ty = Type::real();
    }
    for (std::size_t pi = 0; pi < c.parts.size(); ++pi) {
      const CrefPart& p = c.parts[pi];
      std::vector<Id<Expr>> subs;
      for (std::size_t k = 0; k < p.subscripts.size(); ++k) {
        if (pi == 0 && c.parts.size() > 1 && k < head_subs.size()) { subs.push_back(head_subs[k]); continue; }
        if (pi > 0 && pi + 1 < c.parts.size() && k < mid_subs[pi].size()) { subs.push_back(mid_subs[pi][k]); continue; }
        // 留成符号的下标（for 语句 / 保留着的归约的迭代变量）照常化简（IR 探针 s1：`x[i + 0]`、`x[1 * i]`、`x[i + (2 - 2)]`
        // → `x[i]`；此前原样留着）。
        const bool typed_sub = pi + 1 == c.parts.size() && k < leaf_subs.size() && leaf_subs[k].is_valid();
        subs.push_back(typed_sub ? leaf_subs[k] : simplify_expr(inst_exp(p.subscripts[k], enclosing, prefix)));
        // Real 下标是类型错（AP p9 探针：Real 迭代变量 `sum(x[i] for i in {m, 2.5})`，omc 报错）。Boolean / 枚举类型的符号
        // 下标只许落在同类型的类型维上（上面核对过的）；落在整数维上同样是类型错。
        if (const Type& st = result_.model.exprs[subs.back()].ty; st.kind == TypeKind::Real && !st.is_array())
          set_unsupported("Real subscript");
        else if (!typed_sub && !st.is_array() && (st.kind == TypeKind::Boolean || st.kind == TypeKind::Enumeration) &&
                 !(pi + 1 == c.parts.size() && k < sym_label_ok.size() && sym_label_ok[k]))
          set_unsupported("subscript type");
      }
      e.cref.parts.push_back({p.name, std::move(subs)});
    }
    if (!empty_dims.empty()) {
      // 空数组的下标引用（只剩符号下标，字面量下标已判越界）→ 缺省值加下标 `{}[m - 1]`（replaceEmptyArraysExp 的
      // SUBSCRIPTED_EXP 支；EmptyArray2）。限单段本地名、无前缀下标、无切片；其余未核实 → SKIP。
      bool simple = c.parts.size() == 1 && e.ty.element_type().kind != TypeKind::Complex;
      for (const std::vector<std::int64_t>& s : cur_psubs_) simple = simple && s.empty();
      for (Id<Expr> s : e.cref.parts.back().subscripts) {
        const ExprKind k = result_.model.exprs[s].kind;
        simple = simple && k != ExprKind::Wild && k != ExprKind::Range && k != ExprKind::Array;
      }
      if (!simple) { set_unsupported("subscripted reference to empty array"); return lit_int(0); }
      Expr s;
      s.kind = ExprKind::Subscript;
      s.a = empty_array_lit(e.ty.element_type(), empty_dims);
      s.args = e.cref.parts.back().subscripts;
      s.ty = e.ty;
      return add(std::move(s));
    }
    return add(std::move(e));
  }

  // 空数组类型的缺省值字面量：嵌套空数组 `{}` / `{{}, {}}`（makeDefaultValue → fillType，NFExpression.mo:5270）。dims 自 k 起
  // 须含 0（递归在 0 维处收住）。
  Id<Expr> empty_array_lit(const Type& elem, const std::vector<std::int64_t>& dims, std::size_t k = 0) {
    Expr arr;
    arr.kind = ExprKind::Array;
    std::vector<Dimension> rest;
    for (std::size_t j = k; j < dims.size(); ++j) {
      Dimension d;
      d.kind = DimKind::Integer;
      d.size = dims[j];
      rest.push_back(d);
    }
    arr.ty = Type::array_of(elem, std::move(rest));
    for (std::int64_t i = 0; i < dims[k]; ++i) arr.args.push_back(empty_array_lit(elem, dims, k + 1));
    return add(std::move(arr));
  }

  // 被引用数组叶的声明维：字面量 / 参数 / size() 走 eval_dim；`:` 维（由绑定推断，NFTyping.deduceDimensionFromExp）取已发射
  // 元素变量的形状（emit_array 已按推出的维逐元素发射；Concatenation2 `[x, y]`、SubscriptedExp4 `(x)[n]`）——只做单段本地
  // cref，且被引用者须先于引用处发射（前向引用 / 多段 → false，调用方 SKIP）。
  bool leaf_decl_dims(const SElement& lcel, const Cref& c, NodeId enclosing, const std::vector<Symbol>& prefix,
                      std::vector<std::int64_t>& out, std::vector<std::vector<Id<Expr>>>* labels = nullptr) {
    const ScodeScope lscope = scode_scope_of(enclosing);
    bool colon = false;
    for (const std::vector<Id<Exp>>* ds : {&lcel.attr.dims, &lcel.type.dims})
      for (Id<Exp> d : *ds) {
        if (labels) labels->emplace_back();
        if (I_.absyn.exps[d].kind == ExpKind::Colon) { colon = true; out.push_back(-1); continue; }
        // 类型维（`Real y[E]` / `y[Boolean]`）：长度 = 取值个数；labels 回报各维的标签（整数维为空）。
        if (std::optional<std::vector<Id<Expr>>> tl = classify_type_dim(d, enclosing, prefix)) {
          out.push_back(static_cast<std::int64_t>(tl->size()));
          if (labels) labels->back() = std::move(*tl);
          continue;
        }
        if (result_.unsupported) return false;
        if (!eval_dim(d, out, lscope)) return false;
      }
    if (!colon) return true;
    if (c.parts.size() != 1) return false;
    std::vector<Symbol> names = prefix;
    names.push_back(c.parts[0].name);
    PSubs ps = cur_psubs_;
    ps.resize(prefix.size());
    const std::optional<std::vector<std::int64_t>> sh = emitted_shape(names, ps);
    if (!sh || sh->size() != out.size()) return false;
    const std::vector<std::vector<Id<Expr>>> el = labels ? emitted_labels(names, ps) : std::vector<std::vector<Id<Expr>>>{};
    for (std::size_t k = 0; k < out.size(); ++k) {
      if (out[k] >= 0 && out[k] != (*sh)[k]) return false;
      // `:` 维推出来是类型维（元素带标签下标）：标签取自已发射的元素。
      if (out[k] < 0 && labels && k < el.size() && !el[k].empty()) (*labels)[k] = el[k];
      out[k] = (*sh)[k];
    }
    return true;
  }
  // 已发射的数组变量 names 各维的标签（按序数排；整数维为空）：`:` 维推出来是类型维的数组（`Real z[:] = {… for i in E}`）
  // 声明里看不出来，只能看发射出来的元素。
  std::vector<std::vector<Id<Expr>>> emitted_labels(const std::vector<Symbol>& names, const PSubs& psubs) {
    std::vector<std::vector<Id<Expr>>> ns = psubs_to_exprs(psubs);
    ns.push_back({});
    std::vector<std::vector<Id<Expr>>> out;
    for (const FlatVariable& v : result_.model.variables) {   // ponytail: 线性扫，同 emitted_shape
      if (v.name != names || v.subscripts.empty() || !name_subs_eq(v.name_subs, ns)) continue;
      if (out.size() < v.subscripts.size()) out.resize(v.subscripts.size());
      for (std::size_t k = 0; k < v.subscripts.size(); ++k) {
        if (result_.model.exprs[v.subscripts[k]].kind == ExprKind::IntLit) continue;
        const std::optional<std::int64_t> o = sub_ordinal(v.subscripts[k]);
        if (!o || *o < 1) continue;
        if (out[k].size() < static_cast<std::size_t>(*o)) out[k].resize(static_cast<std::size_t>(*o));
        out[k][static_cast<std::size_t>(*o) - 1] = v.subscripts[k];
      }
    }
    return out;
  }
  // 已发射的数组变量 names（逐段下标 psubs）的形状：各元素末段字面量下标的逐维最大值，且元素数 = 各维之积。
  // ponytail: 线性扫已发射变量；引用多、模型大时再建索引。
  std::optional<std::vector<std::int64_t>> emitted_shape(const std::vector<Symbol>& names, const PSubs& psubs) {
    if (auto it = empty_shapes_.find(int_key(names, psubs)); it != empty_shapes_.end()) return it->second;
    std::vector<std::vector<Id<Expr>>> ns = psubs_to_exprs(psubs);
    ns.push_back({});
    std::vector<std::int64_t> dims;
    std::int64_t count = 0;
    for (const FlatVariable& v : result_.model.variables) {
      if (v.name != names || v.subscripts.empty() || !name_subs_eq(v.name_subs, ns)) continue;
      if (dims.empty()) dims.assign(v.subscripts.size(), 0);
      if (v.subscripts.size() != dims.size()) return std::nullopt;
      for (std::size_t k = 0; k < dims.size(); ++k) {
        const std::optional<std::int64_t> o = sub_ordinal(v.subscripts[k]);   // 类型维的标签按序数
        if (!o) return std::nullopt;
        dims[k] = std::max(dims[k], *o);
      }
      ++count;
    }
    if (dims.empty()) return std::nullopt;
    std::int64_t total = 1;
    for (std::int64_t d : dims) total *= d;
    if (total != count) return std::nullopt;
    return dims;
  }

  // `r.x` 展开（见 cref）：首段 = 本地复合数组组件（维度可求）、次段 = 其类里的标量内建字段（非 protected）→ 嵌套数组
  // `{r[1].x, …}`，元素经 cref() 解析（首段补字面量下标，复用越界 / 结构参数 / 前缀 / 常量折叠）。首段可带前几维的标量
  // 下标（`a[1].x` = `a[1, :].x` → `{a[1,1].x, a[1,2].x}`，`a[k].x` 的 k 成结构参数，XR2 探针），只展开余下的维。
  // 不适用（无余维、切片下标…）→ nullopt（调用方照旧）。
  std::optional<Id<Expr>> expand_field_over_array(Id<Cref> ac, NodeId local, NodeId enclosing,
                                                  const std::vector<Symbol>& prefix) {
    const CompId hc = I_.nodes[local].comp;
    if (I_.comps[hc].ty.kind != TypeKind::Complex) return std::nullopt;
    const SElement& hcel = I_.scode.elems[I_.comps[hc].def];
    if (hcel.attr.dims.empty() && hcel.type.dims.empty()) return std::nullopt;
    std::vector<std::int64_t> hd;
    const ScodeScope hscope = scode_scope_of(enclosing);
    for (Id<Exp> d : hcel.attr.dims) if (!eval_dim(d, hd, hscope)) return std::nullopt;
    for (Id<Exp> d : hcel.type.dims) if (!eval_dim(d, hd, hscope)) return std::nullopt;
    for (std::int64_t n : hd) if (n < 1) return std::nullopt;
    const NodeId cls = I_.comps[hc].class_inst;
    if (!cls.is_valid() || I_.nodes[cls].is_empty() || I_.nodes[cls].kind != NodeKind::Class || !I_.nodes[cls].cls.is_valid() ||
        !I_.node_class(cls).tree.is_valid())
      return std::nullopt;
    const Cref c = I_.absyn.crefs[ac];   // 拷贝（下面往 absyn arena add）
    const std::size_t ns = c.parts[0].subscripts.size();   // 已给的前几维下标（须是标量下标）
    if (ns >= hd.size()) return std::nullopt;
    for (Id<Exp> s : c.parts[0].subscripts) {
      const ExpKind sk = I_.absyn.exps[s].kind;
      if (sk == ExpKind::Colon || sk == ExpKind::Range || is_array_ctor(s)) return std::nullopt;
    }
    const NodeId f = find_local(cls, c.parts[1].name);
    if (!f.is_valid() || I_.nodes[f].is_empty() || I_.nodes[f].visibility == Visibility::Protected) return std::nullopt;
    const SElement& fel = I_.scode.elems[I_.comps[I_.nodes[f].comp].def];
    const Type fty = I_.comps[I_.nodes[f].comp].ty;
    if (!fel.attr.dims.empty() || !fel.type.dims.empty() || fty.is_array() || fty.kind == TypeKind::Complex ||
        fty.kind == TypeKind::Unknown)
      return std::nullopt;
    std::vector<std::int64_t> idx(hd.size(), 1);
    auto build = [&](auto& self, std::size_t k) -> Id<Expr> {
      if (k == hd.size()) {
        Cref ec = c;   // 首段：已给下标原样在前，余维补字面量
        for (std::size_t j = ns; j < idx.size(); ++j) {
          const std::int64_t i = idx[j];
          Exp lit;
          lit.kind = ExpKind::IntLit;
          lit.ival = i;
          ec.parts[0].subscripts.push_back(synth_exp(std::move(lit)));
        }
        const Id<Expr> el = cref(synth_cref(std::move(ec)), enclosing, prefix);
        xfield_elems_.insert(el.raw);
        return el;
      }
      Expr arr;
      arr.kind = ExprKind::Array;
      std::vector<Dimension> ds;
      for (std::size_t j = k; j < hd.size(); ++j) {
        Dimension d;
        d.kind = DimKind::Integer;
        d.size = hd[j];
        ds.push_back(d);
      }
      arr.ty = Type::array_of(fty, std::move(ds));
      for (std::int64_t i = 1; i <= hd[k]; ++i) {
        idx[k] = i;
        arr.args.push_back(self(self, k + 1));
      }
      return add(std::move(arr));
    };
    return build(build, ns);
  }

  // 复合数组的元素引用（`a[:].x`、`a[1:2].v[1]`、`a.v`、`b.q[2].x`、`a[:, 2].x`）：非末段的复合数组段有"洞"（没给下标 /
  // 下标少于维数 / 切片）时，omc 的 ExpandExp 把整条引用展开成全标量元素的嵌套数组——各段的洞按段序、维序逐层展开，
  // 末段是数组的也一并展开到标量（CA 探针 c1：`a.v` → `{{a[1].v[1], a[1].v[2]}, …}`、`a[:, 2].x` → `{a[1,2].x, a[2,2].x}`、
  // `b.q[2].x` → `{b[1].q[2].x, b[2].q[2].x}`）；元素经 cref() 照常解析。没有洞 → nullopt（照常解析）；未做的形态置
  // unsupported 并返回 nullopt。
  // 各段的维：首段在 enclosing 的作用域里求（参数维随帧）；更深的段只收字面量整数维（子实例里的参数维受实例修饰影响，
  // 这里求不准）。标量下标须是界内的常量 / end / 迭代变量 / 结构参数（越界的 omc 不查、照打一个不存在的变量，不跟）。
  std::optional<Id<Expr>> expand_composite_path(Id<Cref> ac, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const Cref c = I_.absyn.crefs[ac];   // 拷贝（下面往 absyn arena 合成节点）
    if (c.leading_dot || c.parts.size() < 2) return std::nullopt;
    struct Part {
      std::vector<std::vector<std::int64_t>> sel;   // 各维选中的下标表
      std::vector<bool> hole;                       // 该维是洞（展开成一层数组）；否 → sel 恰一个值
    };
    auto unsupported = [&]() -> std::optional<Id<Expr>> {
      set_unsupported("composite array element reference");
      return std::nullopt;
    };
    std::vector<Part> ps(c.parts.size());
    const ScodeScope scope = scode_scope_of(enclosing);
    NodeId cls = enclosing;
    bool any_hole = false;
    for (std::size_t i = 0; i < c.parts.size(); ++i) {
      const bool last = i + 1 == c.parts.size();
      const NodeId n = find_local(cls, c.parts[i].name);
      if (!is_comp_node(n) || (i > 0 && I_.nodes[n].visibility == Visibility::Protected)) return std::nullopt;
      if (last && !any_hole) return std::nullopt;   // 各复合段都取到了元素：照常解析（末段的切片另有处理）
      const SElement& el = I_.scode.elems[I_.comps[I_.nodes[n].comp].def];
      std::vector<std::int64_t> dims;
      for (const std::vector<Id<Exp>>* ds : {&el.attr.dims, &el.type.dims})
        for (Id<Exp> d : *ds) {
          if (i == 0) {
            if (!eval_dim(d, dims, scope)) return std::nullopt;
          } else if (I_.absyn.exps[d].kind == ExpKind::IntLit) {
            dims.push_back(I_.absyn.exps[d].ival);
          } else {
            // 更深的段是参数维 / `:` 维：有洞要展开时求不准 → 未做；没洞的留给照常解析去报。
            if (any_hole || !c.parts[i].subscripts.empty() || !last) return unsupported();
            return std::nullopt;
          }
        }
      const std::vector<Id<Exp>>& subs = c.parts[i].subscripts;
      if (subs.size() > dims.size()) return std::nullopt;   // 下标多于维数：omc 报错，照常解析去拒
      Part& p = ps[i];
      for (std::size_t k = 0; k < dims.size(); ++k) {
        std::vector<std::int64_t> sel;
        bool hole = true;
        const ExpKind sk = k < subs.size() ? I_.absyn.exps[subs[k]].kind : ExpKind::Colon;   // 缺的尾部下标 = `:`
        if (sk == ExpKind::Colon) {
          for (std::int64_t v = 1; v <= dims[k]; ++v) sel.push_back(v);
        } else if (sk == ExpKind::Range || (is_array_ctor(subs[k]) && !is_comprehension(subs[k]))) {
          const std::optional<std::vector<std::int64_t>> sl = slice_indices(subs[k], dims[k], scope, enclosing);
          if (!sl) return last && !any_hole ? std::nullopt : unsupported();
          sel = *sl;
        } else {
          std::optional<std::int64_t> iv = sk == ExpKind::End ? std::optional<std::int64_t>(dims[k]) : subscript_int(subs[k], &scope);
          if (!iv && absyn_refs_param(subs[k], enclosing))
            if (std::optional<CVal> pv = eval_scode(subs[k], scope, 0, /*allow_param=*/true, nullptr); pv && pv->k == CVal::K::Int)
              iv = pv->i;
          // 求不出的（运行时下标 `a[i].x`）/ 越界的：有洞时未做；没洞的留给照常解析。
          if (!iv || *iv < 1 || *iv > dims[k]) return any_hole ? unsupported() : std::nullopt;
          sel.push_back(*iv);
          hole = false;
        }
        if (sel.empty()) return unsupported();   // 空的洞（零长度的复合数组）：未做
        if (hole && !last) any_hole = true;
        p.sel.push_back(std::move(sel));
        p.hole.push_back(hole);
      }
      if (last) break;
      if (I_.comps[I_.nodes[n].comp].ty.kind != TypeKind::Complex) return std::nullopt;
      cls = I_.comps[I_.nodes[n].comp].class_inst;
      if (!is_class_inst(cls)) return std::nullopt;
    }
    if (!any_hole) return std::nullopt;
    std::int64_t total = 1;
    for (const Part& p : ps)
      for (const std::vector<std::int64_t>& sl : p.sel) {
        total *= static_cast<std::int64_t>(sl.size());
        if (total > 1024) { set_unsupported("composite array too large"); return std::nullopt; }
      }
    // 逐层展开：(段, 维) 按序；洞 → 一层数组，标量 → 定值。走到底合成全标量下标的 cref。
    std::vector<std::vector<std::int64_t>> pick(ps.size());
    for (std::size_t i = 0; i < ps.size(); ++i) pick[i].assign(ps[i].sel.size(), 0);
    auto build = [&](auto& self, std::size_t i, std::size_t k) -> Id<Expr> {
      if (result_.unsupported) return lit_int(0);
      if (i == ps.size()) {
        Cref ec = c;
        for (std::size_t j = 0; j < ps.size(); ++j) {
          ec.parts[j].subscripts.clear();
          for (std::int64_t v : pick[j]) {
            Exp lit;
            lit.kind = ExpKind::IntLit;
            lit.ival = v;
            ec.parts[j].subscripts.push_back(synth_exp(std::move(lit)));
          }
        }
        const Id<Expr> el = cref(synth_cref(std::move(ec)), enclosing, prefix);
        xfield_elems_.insert(el.raw);
        return el;
      }
      if (k == ps[i].sel.size()) return self(self, i + 1, 0);
      if (!ps[i].hole[k]) {
        pick[i][k] = ps[i].sel[k][0];
        return self(self, i, k + 1);
      }
      Expr arr;
      arr.kind = ExprKind::Array;
      for (std::int64_t v : ps[i].sel[k]) {
        pick[i][k] = v;
        arr.args.push_back(self(self, i, k + 1));
        if (result_.unsupported) return lit_int(0);
      }
      Dimension d;
      d.kind = DimKind::Integer;
      d.size = static_cast<std::int64_t>(arr.args.size());
      arr.ty = Type::array_of(result_.model.exprs[arr.args[0]].ty, {d});   // array_of 把子数组的维接在后面
      return add(std::move(arr));
    };
    const Id<Expr> r = build(build, 0, 0);
    if (result_.unsupported) return std::nullopt;
    return r;
  }

  // 在 enclosing 实例的 FlatTree 组件里按名字找（v1：直接线性找，规模小）。
  NodeId find_local(NodeId enclosing, Symbol name) {
    if (I_.nodes[enclosing].kind != NodeKind::Class || !I_.nodes[enclosing].cls.is_valid() ||
        !I_.node_class(enclosing).tree.is_valid())
      return Inst::empty_node();
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
    if (op_type(a).is_array() || op_type(b).is_array())
      return array_binary(e.op, op, a, b);   // 数组：逐元素/广播，维度传播，逐元素标量化在 collect_array_equation
    // 算术运算数须是数值，或 String + String（拼接，结果是 String：`"a" + "b" + "c"` 的左操作数）；Boolean / 枚举 → omc
    // 类型错（Terminal2）→ SKIP。
    const TypeKind ka = result_.model.exprs[a].ty.kind, kb = result_.model.exprs[b].ty.kind;
    const bool str_add = op == BinOp::Add && ka == TypeKind::String && kb == TypeKind::String;
    {
      auto num = [](TypeKind k) { return k == TypeKind::Integer || k == TypeKind::Real || k == TypeKind::Unknown; };
      if (!str_add && (!num(ka) || !num(kb))) { set_unsupported("non-numeric arithmetic operand"); return lit_int(0); }
    }
    // Modelica `/` 恒实除：整型操作数也提实型（`i/j` → `/*Real*/(i) / /*Real*/(j)`，MLS §10.6.1）；`^` 两侧都按
    // Real（NFTypeCheck.checkBinaryOperationPow :1170-1185，Pow `2 ^ pi` → `2.0 ^ /*Real*/(pi)`）。
    bool real = op == BinOp::Div || op == BinOp::Pow || result_.model.exprs[a].ty.kind == TypeKind::Real ||
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
    r.ty = str_add ? Type::string() : real ? Type::real() : Type::integer();
    return add(std::move(r));
  }

  Id<Expr> rel_or_logic(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    Id<Expr> a = inst_exp(e.a, enclosing, prefix);
    Id<Expr> b = inst_exp(e.b, enclosing, prefix);
    // 形状看穿数组字面量的粗类型（`{true, false} or b`、`{1, 2} < {2, 3}`）。
    const bool arr = !result_.unsupported && (!expr_shape(a).empty() || !expr_shape(b).empty());
    RelOp rop;
    LogOp lop;
    if (map_relop(e.op, rop)) {
      if (arr) { set_unsupported("array relation"); return lit_int(0); }   // 元素级 Boolean 数组关系本增量暂缓
      bool real = result_.model.exprs[a].ty.kind == TypeKind::Real || result_.model.exprs[b].ty.kind == TypeKind::Real;
      // 函数外 Real 的 == / <>：omc 告警（"... on Real operands is deprecated in non-function contexts"，
      // OperationRelational1），ymc 不产告警文本 → SKIP。
      if (real && !fn_scope_ && (rop == RelOp::Eq || rop == RelOp::Ne)) { set_unsupported("Real equality relation (warning)"); return lit_int(0); }
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
      const Type tya = op_type(a);   // 数组字面量按形状取类型（`not {true, false}`）
      if (result_.model.exprs[a].kind == ExprKind::Array && all_bool_literal(a)) return fold_bool_arrays(nullptr, a, {});
      Expr r; r.kind = ExprKind::LogicalNot; r.a = a;
      r.ty = tya.is_array() ? Type::array_of(Type::boolean(), tya.dims) : Type::boolean();
      return add(std::move(r));
    }
    // UMinus（数组元素级：结果类型 = 操作数类型，含数组维）
    Expr r; r.kind = ExprKind::Unary; r.un = UnOp::Minus; r.a = a; r.ty = result_.model.exprs[a].ty;
    return add(std::move(r));
  }

  // 数组表达式逐元素展开成嵌套数组字面量（各元素各自化简；ek = Real 时元素提 Real）。
  Id<Expr> expand_array_literal(Id<Expr> x, const std::vector<std::int64_t>& sh, TypeKind ek) {
    std::vector<std::int64_t> idx(sh.size(), 1);
    std::function<Id<Expr>(std::size_t)> rec = [&](std::size_t k) -> Id<Expr> {
      if (k == sh.size()) {
        const Id<Expr> se = simplify_expr(scalarize(x, idx));
        return ek == TypeKind::Real ? to_real(se) : se;
      }
      Expr arr;
      arr.kind = ExprKind::Array;
      std::vector<Dimension> rest;
      for (std::size_t j = k; j < sh.size(); ++j) {
        Dimension d;
        d.kind = DimKind::Integer;
        d.size = sh[j];
        rest.push_back(d);
      }
      arr.ty = Type::array_of(Type::of(ek), std::move(rest));
      for (std::int64_t i = 1; i <= sh[k]; ++i) {
        idx[k] = i;
        arr.args.push_back(rec(k + 1));
      }
      return add(std::move(arr));
    };
    return rec(0);
  }
  // 整体保留的数组式（数组 if 的分支、min / max 的数组实参）规整成 omc 的打印形态：整体 / 切片引用、if、推导式原样（元素类型
  // 须已是 ek），字面量逐叶转成 ek（混合字面量 `{time, 1, 2}` 的元素类型已是 Real，Integer 叶仍要转成 `1.0`），算术式（二元 /
  // 一元）逐元素展开成字面量 `{2.0 * x[1], …}`。其余形态未核实 → invalid（调用方 SKIP）。
  Id<Expr> kept_array_form(Id<Expr> x, const std::vector<std::int64_t>& sh, TypeKind ek) {
    const ExprKind xk = result_.model.exprs[x].kind;
    const TypeKind xe = expr_elem_kind(x);
    switch (xk) {
      case ExprKind::Array: return ek == TypeKind::Real || xe != ek ? coerce_deep(x, ek) : x;
      case ExprKind::Cref:
      case ExprKind::IfExp:
      case ExprKind::Logical:      // Boolean 数组的 and / or / not 不展开（`if a > 0.0 then j else not j`，WA 探针 w5）
      case ExprKind::LogicalNot:
      case ExprKind::Reduction: return xe == ek ? x : Id<Expr>{};
      case ExprKind::Binary:
      case ExprKind::Unary: return expand_array_literal(x, sh, ek);
      default: return {};
    }
  }
  // 数组 if 表达式（模型语境；V4r 探针）：两支同形（维已知）、元素同为 Boolean 或同为数值（有 Real 则全提 Real），条件是标量
  // Boolean。绑定里整体保留，各支规整成 omc 的打印形态——整体 / 切片引用原样、字面量按元素类型转、算术式逐元素展开成字面量
  //（`i3 = if time > 1.0 then x else {2.0 * x[1], 2.0 * x[2], 2.0 * x[3]}`）、嵌套 if / 向量化调用原样；显式等式、参数摊分、sum
  // 里逐元素取（scalarize 的 IfExp 支：`y[1] = if time > 1.0 then x[1] else 2.0 * x[1]`）。两支形状不同（omc 对结构参数条件求值
  // 后取一支，IfExpression8 / 12）、Integer 引用配 Real、其余分支形态未做 → SKIP。
  // craw / enclosing：条件的 Absyn、所在实例（两支不相容时按结构参数求值选支，见 select_if_branch）；
  // top：本 if 是整个绑定 / 等式一侧——只有这个位置 1.25.0 才选支（V4z 探针：`2 * (if pt then {1, 2} else {3, 4, 5})` 是类型错，
  // 嵌在另一个 if 的分支里的不相容 if 被原样留下 `d1 = if pf then {1.0} else {2.0, 3.0}`）。
  Id<Expr> array_if_exp(Id<Expr> c, Id<Expr> t, Id<Expr> f, Id<Exp> craw, NodeId enclosing, bool top) {
    const std::vector<std::int64_t> st = expr_shape(t), sf = expr_shape(f);
    if (result_.unsupported) return lit_int(0);
    bool ok = !st.empty() && st == sf;
    for (std::int64_t n : st) ok = ok && n >= 1;
    if (!ok) {
      // 只有"都是数组、同秩、维不同"才按条件求值选支；标量对数组、秩不同是 omc 类型错（V4z 探针 `if pt then {1, 2} else 3`）。
      bool dims_only = !st.empty() && st.size() == sf.size();
      for (std::int64_t n : st) dims_only = dims_only && n >= 1;
      for (std::int64_t n : sf) dims_only = dims_only && n >= 1;
      if (!dims_only || !top) { set_unsupported("if-expression branch shape"); return lit_int(0); }
      return select_if_branch(craw, c, t, f, enclosing);
    }
    if (!expr_shape(c).empty() || result_.model.exprs[c].ty.kind != TypeKind::Boolean) {
      set_unsupported("if-expression condition type");
      return lit_int(0);
    }
    const TypeKind kt = expr_elem_kind(t), kf = expr_elem_kind(f);
    auto num = [](TypeKind k) { return k == TypeKind::Integer || k == TypeKind::Real; };
    TypeKind ek = kt;
    if (kt != kf) {
      if (!num(kt) || !num(kf)) { set_unsupported("if-expression branch type"); return lit_int(0); }
      ek = TypeKind::Real;
    }
    if (!num(ek) && ek != TypeKind::Boolean) { set_unsupported("if-expression branch type"); return lit_int(0); }
    const Id<Expr> tb = kept_array_form(t, st, ek), fb = kept_array_form(f, st, ek);
    if (result_.unsupported) return lit_int(0);
    if (!tb.is_valid() || !fb.is_valid()) { set_unsupported("if-expression array branch"); return lit_int(0); }
    std::vector<Dimension> dims;
    for (std::int64_t n : st) {
      Dimension d;
      d.kind = DimKind::Integer;
      d.size = n;
      dims.push_back(d);
    }
    Expr r;
    r.kind = ExprKind::IfExp;
    r.a = c;
    r.b = tb;
    r.c = fb;
    r.ty = Type::array_of(Type::of(ek), std::move(dims));
    return add(std::move(r));
  }

  // 数组 if 的两支形状不相容：条件可按结构参数求值时取选中的那一支，条件里的参数标结构（1.25.0 实测 IfExpression4 / 8 / 14：
  // `final parameter Boolean b = true` + `x = {1.0, 2.0}`；运行时条件 = 类型错，IfExpression5）。条件参数须字面量绑定 / fixed
  //（传递标记依赖 omc 的求值次序，同 if 方程的 cond_params_fixed）、无 Evaluate = false。未选中的那支已定型过：它登记过的
  // 函数由 prune_functions 收走。选中的支是 Integer 算术式时 1.25.0 丢了对 Real 的整体转型（IfExpression12 `x = {6, 6}`），
  // 不复刻 → SKIP。
  Id<Expr> select_if_branch(Id<Exp> craw, Id<Expr> c, Id<Expr> t, Id<Expr> f, NodeId enclosing) {
    const StructSnap snap = struct_snap();
    // 条件是在压平期才标的（flattenConditionalArrayIfExp，NFFlatten.mo:1733-1737，v1.25.0）：哪怕 if 写在绑定里，声明在后的
    // 别名也不连带（AO 探针 a1：`a1 = if pt …; parameter Boolean pd = pt` → pd 不 final）。
    const PhaseScope late(*this, MarkPhase::Late);
    std::unordered_set<std::uint32_t> used;
    const std::optional<CVal> cv = eval_scode(craw, scode_scope_of(enclosing), 0, /*allow_param=*/true, &used);
    bool ok = cv && cv->k == CVal::K::Bool;
    for (std::uint32_t d : used)
      if (evaluate_annotation(Id<SElement>::from_index(d)) == std::optional<bool>(false)) ok = false;
    // 组件数组元素里（`M m[2](b = {true, false})`）：1.25.0 是内部错 "NFSubscript.toDAEExp failed on unknown subscript"
    //（IfExpression6），不复刻、也不出结果。
    for (const std::vector<std::int64_t>& s : cur_psubs_) ok = ok && s.empty();
    if (ok && !used.empty()) {
      const std::optional<bool> fx = cond_params_fixed(simplify_expr(c));
      ok = fx && *fx;
    }
    if (!ok) {
      struct_restore(snap);
      set_unsupported("if-expression branch shape");
      return lit_int(0);
    }
    const Id<Expr> sel = cv->b ? t : f;
    const ExprKind sk = result_.model.exprs[sel].kind;
    if ((sk == ExprKind::Binary || sk == ExprKind::Unary) && expr_elem_kind(sel) == TypeKind::Integer) {
      set_unsupported("if-expression selected branch cast");
      return lit_int(0);
    }
    // 选中的支里还引用参数（`fill(n, 2)` 的元素 n）：omc 连同结构参数一起求值，形态有怪相（V4z 探针 `Real b2x[:] = if n == 3
    // then {1} else fill(n, 2)` → `b2x = {2, 2}`、`2.0 .* fill(n, 2)` → `{4.0, 4.0}`），未做 → SKIP。
    if (refs_param_var(sel)) { set_unsupported("if-expression selected branch references parameter"); return lit_int(0); }
    return sel;
  }

  Id<Expr> if_exp(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const bool top = if_top_;   // 本 if 是整个绑定 / 等式一侧（见 if_top_）；条件与两支里的 if 都不是
    if_top_ = false;
    Id<Expr> c = inst_exp(e.a, enclosing, prefix);
    Id<Expr> t = inst_exp(e.b, enclosing, prefix);
    Id<Expr> f = inst_exp(e.c, enclosing, prefix);
    if (!fn_scope_ && !result_.unsupported && (!expr_shape(t).empty() || !expr_shape(f).empty())) {
      // 条件折成字面量：直接取那一支，另一支不参与形状 / 类型匹配（IfExpression10 `if false then {1, 2} elseif false then {1}
      // else {3, 4}` → `x = {3.0, 4.0}`）。
      if (const Id<Expr> cs = simplify_expr(c); result_.model.exprs[cs].kind == ExprKind::BoolLit)
        return result_.model.exprs[cs].bval ? t : f;
      return array_if_exp(c, t, f, e.a, enclosing, top);
    }
    // 函数体里的数组 if：两支元素一个 Integer 一个 Real → Integer 的那支提到 Real（引用整体转型、字面量逐叶转；FS 探针 t3
    // `if k > 0 then /*Real[2]*/(n2) else w[1:2]`、`… else {3.0, 4.0}`）；其余不同类 → omc 类型错。
    if (fn_scope_ && !result_.unsupported && (op_type(t).is_array() || op_type(f).is_array())) {
      const TypeKind te = expr_elem_kind(t), fe = expr_elem_kind(f);
      auto num = [](TypeKind k) { return k == TypeKind::Integer || k == TypeKind::Real; };
      if (te != fe) {
        if (!num(te) || !num(fe)) { set_unsupported("if-expression branch type mismatch"); return lit_int(0); }
        t = fn_real_operand(t);
        f = fn_real_operand(f);
        if (!t.is_valid() || !f.is_valid()) { set_unsupported("function array operand cast"); return lit_int(0); }
      }
      Expr r; r.kind = ExprKind::IfExp; r.a = c; r.b = t; r.c = f; r.ty = op_type(t);
      return add(std::move(r));
    }
    // 分支类型统一（MLS §10.6.4）：一支 Real 一支 Integer → 两支都提到 Real
    //（`if c then der(x) else 0` → `else 0.0`，IfExpression17/InnerOuter3）。
    const TypeKind tk = result_.model.exprs[t].ty.kind, fk = result_.model.exprs[f].ty.kind;
    if ((tk == TypeKind::Real && fk == TypeKind::Integer) ||
        (tk == TypeKind::Integer && fk == TypeKind::Real)) {
      t = to_real(t);
      f = to_real(f);
    } else if (tk != fk && tk != TypeKind::Unknown && fk != TypeKind::Unknown && !result_.unsupported) {
      // 其余不同类的两支（`if c then 1 else "a"`）= omc 类型错（P2 探针）→ SKIP（此前按第一支的类型放行）。
      set_unsupported("if-expression branch type mismatch");
    }
    Expr r; r.kind = ExprKind::IfExp; r.a = c; r.b = t; r.c = f; r.ty = result_.model.exprs[t].ty;
    return add(std::move(r));
  }

  // 无返回值的调用 `f(args);`——方程 / 模型算法语句 / 函数体语句（NORETCALL）。化简后还是调用才留下
  //（NFSimplifyModel.simplifyEquation :220-228、simplifyStatement :369-377，v1.25.0）：实参全是常量的纯函数调用被求值掉、
  // 整条消失（N1 / N2 探针：`f(1.0);` 不出行、函数也不发射，`sin(1.0);` 同）；impure / external 的不求值、照留
  //（`f(1.0);` 原样）；带输出的函数也能当语句调（不取首输出）。返回 invalid 且未标 unsupported = 整条删去。
  Id<Expr> noret_call(Id<Exp> ce, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const Exp e = I_.absyn.exps[ce];   // 拷贝
    if (e.kind != ExpKind::Call) { set_unsupported("non-call statement"); return {}; }
    // 实参全是常量（函数体里：不引用形参 / 局部）？
    const ScodeScope sc = fn_scope_ ? *fn_scope_ : scode_scope_of(enclosing);
    bool all_const = !(fn_scope_ && exp_refs_fn_locals(e));
    for (Id<Exp> a : e.args)
      all_const = all_const && eval_scode(a, sc, 0, /*allow_param=*/false, nullptr).has_value();
    if (result_.unsupported) return {};
    if (all_const)
      if (const std::optional<FnRef> fr = resolve_fn(I_.absyn.crefs[e.cref], sc)) {
        FnSig sig;
        std::unordered_set<std::uint32_t> seen;
        const bool impure = I_.scode.elems[fr->fe].purity == Purity::Impure;
        if (!builtin_equiv(*fr) && collect_fn_sig(fr->fe, fr->fscope, sig, seen, 0) && !impure && !sig.external) {
          // 纯的用户函数：omc 把调用求值掉（整条删去）；求值失败（体里的 assert 不成立，N2 探针 `f(-1.0)`）时调用照留——
          // ymc 求不出时分不清是哪种 → SKIP。
          if (eval_user_call(e, sc, 0, /*allow_param=*/false, nullptr, /*noret=*/true)) return {};
          if (!result_.unsupported) set_unsupported("constant no-return call");
          return {};
        }
      }
    noret_call_ = true;
    sm_stmt_ = true;         // initialState / transition 只能作语句（见 state_machine_call）
    array_call_ok_ = true;   // 语句位置 = 整个式子，返回数组的函数也行（OperationSubEW2 `f(r1, r2);`）
    Id<Expr> c = inst_exp(ce, enclosing, prefix);
    noret_call_ = false;
    sm_stmt_ = false;
    array_call_ok_ = false;
    if (result_.unsupported) return {};
    c = simplify_expr(c);
    const ExprKind k = result_.model.exprs[c].kind;
    if (k == ExprKind::Call) return c;
    // 求值成了字面量（带输出的纯函数 / 内建的常量调用）→ 整条删去；别的形态（向量化的推导式 …）未做。
    if (!is_lit(c) && k != ExprKind::Array) set_unsupported("no-return call form");
    return {};
  }

  Id<Expr> call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    fn_why_ = nullptr;   // 子原因按调用清零（见 fn_fail）
    const Cref& fn = I_.absyn.crefs[e.cref];
    // 调用结果的字段访问 `f(i).y`：1.25.0 自己报内部错误（DotOperator），ymc 未做 → SKIP（此前被静默当成 f(i)）。
    if (e.call_suffix.is_valid()) { set_unsupported("field access on call result"); return lit_int(0); }
    // 模型语境里直接写的归约式 `sum / product / min / max(e for i in r, …)`。
    if (!fn_scope_ && e.args.size() == 1 && fn.parts.size() == 1 && I_.absyn.exps[e.args[0]].kind == ExpKind::Reduction) {
      const std::string_view rn = in_.str(fn.parts[0].name);
      if (rn == "sum" || rn == "product" || rn == "min" || rn == "max") return reduction_call(e, rn, enclosing, prefix);
    }
    // 用户函数常量折叠优先（支持命名/默认实参）：单名解析到 function 类且实参可求常量 → 折成字面量（CevalFunc1/FuncSimple）。
    {
      // 折叠结果须是标量（数组 / 记录值在此语境无字面量形式，literal_of 会退化成 0）→ 否则不接，落到下面。
      auto scalar = [](const CVal& v) {
        return v.k == CVal::K::Int || v.k == CVal::K::Real || v.k == CVal::K::Bool || v.k == CVal::K::Str;
      };
      // 函数名在书写处的词法作用域里找：直接继承来的成员（inst_class 把其 parent_scope 改指基类实例，该实例的
      // instance_parent = 本实例）按基类作用域（YmcFnName2 `A2 a2` 里继承自 A 的 `v = h(time)` → `R.a2.h`），
      // 与限定常量按声明处解析同理。
      NodeId lex = enclosing;
      if (!fn_scope_ && decl_scope_.is_valid() && !I_.nodes[decl_scope_].is_empty()) {
        const NodeId ps = I_.nodes[decl_scope_].parent_scope;
        if (ps.is_valid() && !I_.nodes[ps].is_empty() && I_.nodes[ps].kind == NodeKind::Class &&
            I_.nodes[ps].instance_parent.raw == enclosing.raw)
          lex = ps;
      }
      const ScodeScope sc = scode_scope_of(lex);
      if (std::optional<FnRef> bfr = resolve_fn(fn, sc)) {
        bool ext_c = false;
        if (std::optional<std::string> bn = builtin_equiv(*bfr, &ext_c)) {
          array_call_ok_ = false;
          const Id<Expr> r = builtin_call(e, enclosing, prefix, in_.intern(*bn));
          if (ext_c && result_.model.exprs[r].kind == ExprKind::Call) nofold_calls_.insert(r.raw);
          return r;
        }
      }
      // 函数体里实参引用函数局部量（含局部常量）的调用不求值（YmcFnEmit1：局部常量在函数里保留引用）。
      const bool fold = !(fn_scope_ && exp_refs_fn_locals(e));
      // 函数语境的 sum / product / min / max(常量数组) 整体求值。模型语境的归约式走上面的 reduction_call，数组实参先定型再定
      // 去留（字面量数组才求值，数组构造子 / 混合字面量展开成不化简的元素链，见 model_reduce_builtin）。
      if (fold && fn_scope_ && fn.parts.size() == 1)
        if (auto rv = eval_reduction(e, sc, 0); rv && scalar(*rv)) return literal_of(*rv);
      // 用户函数（含限定名 `P.f`、短类别名）常量折叠：只折常量实参（参数实参的调用变率是 parameter，不求值）。
      if (fold)
        if (auto uv = eval_user_call(e, sc, 0, /*allow_param=*/false)) {
          if (scalar(*uv)) return literal_of(*uv);
          // 数组值：只在整个绑定 / 等式一侧收（EvalConstants 求值后成数组字面量，FuncUnknownDim2 `f(3)` →
          // `{1.0, 1.0, 1.0}`）；元素类型取首叶。
          if (array_call_ok_ && !fn_scope_ && is_arr(*uv)) {
            const CVal* leaf = &*uv;
            while (leaf->k == CVal::K::Array && leaf->arr && !leaf->arr->empty()) leaf = &leaf->arr->front();
            const TypeKind lk = leaf->k == CVal::K::Int ? TypeKind::Integer : leaf->k == CVal::K::Real ? TypeKind::Real
                              : leaf->k == CVal::K::Bool ? TypeKind::Boolean : leaf->k == CVal::K::Str ? TypeKind::String
                              : TypeKind::Unknown;
            if (lk != TypeKind::Unknown)
              if (const Id<Expr> lit = cval_expr(*uv, lk); lit.is_valid()) return lit;
          }
        }
      // 折不成常量 → 发射函数定义 + 保留符号调用（加宽(55)-B）。不可发射 → 落到下面 SKIP。
      if (Id<Expr> uc = try_emit_user_call(e, enclosing, prefix, lex); uc.is_valid()) return uc;
      if (result_.unsupported) return lit_int(0);
      if (Id<Expr> rc = record_ctor_call(e, enclosing, prefix, sc); rc.is_valid() || result_.unsupported) return rc;
      const bool top = array_call_ok_;   // 本调用是不是整个绑定 / 等式一侧（同步算子返回数组时要看）
      array_call_ok_ = false;   // 不是用户函数（内建）：其实参里的调用不在"整个绑定 / 等式一侧"位置
      if (const Cref& fc = I_.absyn.crefs[e.cref]; !fn_scope_ && fc.parts.size() == 1 && !fc.leading_dot)
        if (Id<Expr> r = sync_call(e, in_.str(fc.parts[0].name), enclosing, prefix, top); r.is_valid() || result_.unsupported)
          return r;
    }
    return builtin_call(e, enclosing, prefix, Symbol{});
  }

  // 记录构造调用 `R(a, b)` / `R(x = a)`（Record.instDefaultConstructor：输入 = 记录字段，声明序，缺省 = 字段声明绑定）：
  // 打成对构造函数的调用，缺省实参补齐（RC1 探针 `R(x = time)` → `R(time, 2.0, 3)`、`R(p, n = 4)` → `R(p, 2.0, 4)`），
  // Integer 实参进 Real 字段提 Real，并登记构造函数块。只做纯数据记录（字段全是内建标量、非 constant，无 extends / 数组 /
  // 嵌套记录），缺省须求成常量；函数名不是记录类 → invalid（调用方照旧）；其余不符 → SKIP。
  Id<Expr> record_ctor_call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix, const ScodeScope& sc) {
    const Cref fn = I_.absyn.crefs[e.cref];   // 拷贝
    std::vector<Symbol> fpath;
    for (const CrefPart& p : fn.parts) {
      if (!p.subscripts.empty()) return {};
      fpath.push_back(p.name);
    }
    ScodeScope ds;
    const Id<SElement> rec = resolve_class_path(fpath, fn.leading_dot, sc, ds);
    if (!rec.is_valid() || I_.scode.elems[rec].kind != SElementKind::Class || I_.scode.elems[rec].restriction != SRestriction::Record ||
        I_.scode.elems[rec].def.kind != SClassDefKind::Parts)
      return {};
    const NodeId rn = record_class_node(rec);
    if (!rn.is_valid()) { set_unsupported("record constructor without instance"); return lit_int(0); }
    std::vector<Id<SElement>> fields;
    std::vector<TypeKind> ftys;
    for (Id<SElement> f : I_.scode.elems[rec].def.elements) {
      const SElement& fe = I_.scode.elems[f];
      if (fe.kind == SElementKind::Extends) { set_unsupported("record constructor of extended record"); return lit_int(0); }
      if (fe.kind != SElementKind::Component) continue;
      const NodeId fnode = find_local(rn, fe.name);
      const TypeKind fk = (fnode.is_valid() && !I_.nodes[fnode].is_empty() && I_.nodes[fnode].comp.is_valid())
                              ? I_.comps[I_.nodes[fnode].comp].ty.kind : TypeKind::Unknown;
      if (!fe.attr.dims.empty() || !fe.type.dims.empty() || fe.attr.var == SVariability::Const ||
          (fk != TypeKind::Real && fk != TypeKind::Integer && fk != TypeKind::Boolean && fk != TypeKind::String)) {
        set_unsupported("record constructor field kind");
        return lit_int(0);
      }
      fields.push_back(f);
      ftys.push_back(fk);
    }
    std::vector<Id<Expr>> vals(fields.size());
    const std::vector<Id<Exp>> args = e.args;   // 拷贝
    std::size_t pos = 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
      std::size_t idx = fields.size();
      if (i < e.arg_names.size() && e.arg_names[i].is_valid()) {
        for (std::size_t k = 0; k < fields.size(); ++k)
          if (I_.scode.elems[fields[k]].name == e.arg_names[i]) { idx = k; break; }
      } else {
        idx = pos++;
      }
      if (idx >= fields.size() || vals[idx].is_valid()) { set_unsupported("record constructor arguments"); return lit_int(0); }
      vals[idx] = simplify_expr(inst_exp(args[i], enclosing, prefix));
      if (result_.unsupported) return lit_int(0);
    }
    const ScodeScope rscope = scode_scope_of(rn);   // 字段缺省在记录作用域求值
    Expr r;
    r.kind = ExprKind::Call;
    for (std::size_t k = 0; k < fields.size(); ++k) {
      Id<Expr> v = vals[k];
      if (!v.is_valid()) {
        const Id<SMod> fm = I_.scode.elems[fields[k]].mod;
        const std::optional<CVal> dv = (fm.is_valid() && I_.scode.mods[fm].binding.is_valid())
                                           ? eval_scode(I_.scode.mods[fm].binding, rscope, 0) : std::nullopt;
        if (!dv || is_arr(*dv) || dv->k == CVal::K::Record || dv->k == CVal::K::Undef) {
          set_unsupported("record constructor default");
          return lit_int(0);
        }
        v = literal_of(*dv);
      }
      const Expr& ve = result_.model.exprs[v];
      const TypeKind vk = ve.ty.kind;
      const bool ok = expr_shape(v).empty() &&
                      (ftys[k] == TypeKind::Real ? (vk == TypeKind::Real || vk == TypeKind::Integer) : vk == ftys[k]);
      if (!ok) { set_unsupported("record constructor argument type"); return lit_int(0); }
      r.args.push_back(ftys[k] == TypeKind::Real ? to_real(v) : v);
    }
    const std::optional<std::vector<Symbol>> path = ensure_record_ctor(rn);
    if (!path) return lit_int(0);
    r.call_name = *path;
    r.ty.kind = TypeKind::Complex;
    r.ty.complex_node = rn.raw;
    return add(std::move(r));
  }

  // delay(expr, delayTime[, delayMax])（NFModelicaBuiltin.mo:1105-1119：两参形 delay2 早内联成 delay3(expr, d, d)，其 delayTime
  // 是 parameter input；delay3 的 delayMax 是 parameter input；simplifyDelay NFSimplifyExp.mo:577-595：delayTime ≤ parameter 且
  // tryEvalExp 为 0 → 只留 expr）。D1 探针：`delay(x, 0.1)` → `delay(x, 0.1, 0.1)`、`delay(x, 0)` → `x`。tryEvalExp 不标结构
  // 参数 → 判 0 时的求值撤销结构记录。parameter 形参给了变量 → omc 报错 → SKIP。
  Id<Expr> delay_call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    for (Symbol n : e.arg_names)
      if (n.is_valid()) { set_unsupported("named function argument"); return lit_int(0); }
    if (e.args.size() != 2 && e.args.size() != 3) { set_unsupported("function arity"); return lit_int(0); }
    const std::vector<Id<Exp>> args = e.args;   // 拷贝
    std::vector<Id<Expr>> a;
    for (Id<Exp> x : args) {
      a.push_back(simplify_expr(inst_exp(x, enclosing, prefix)));
      if (result_.unsupported) return lit_int(0);
      const TypeKind k = result_.model.exprs[a.back()].ty.kind;
      if ((k != TypeKind::Real && k != TypeKind::Integer) || !expr_shape(a.back()).empty()) {
        set_unsupported("delay argument type");
        return lit_int(0);
      }
      a.back() = to_real(a.back());
    }
    if (refs_runtime_var(a[1]) && a.size() == 2) { set_unsupported("delay time variability"); return lit_int(0); }
    if (a.size() == 3 && refs_runtime_var(a[2])) { set_unsupported("delay max variability"); return lit_int(0); }
    if (!refs_runtime_var(a[1])) {
      const StructSnap snap = struct_snap();   // 撤销求值引起的结构参数记录（tryEvalExp 不标）
      const std::optional<CVal> dv = eval_scode(args[1], scode_scope_of(enclosing), 0, /*allow_param=*/true, nullptr);
      struct_restore(snap);
      if (dv && (dv->k == CVal::K::Int || dv->k == CVal::K::Real) && dv->as_r() == 0.0) return a[0];
    }
    Expr r;
    r.kind = ExprKind::Call;
    r.call_name = {in_.intern("delay")};
    r.args = {a[0], a[1], a.size() == 3 ? a[2] : a[1]};
    r.ty = Type::real();
    return add(std::move(r));
  }

  // String(x, …)（MLS §3.7.2 的四个重载：Real → String(r, significantDigits = 6, minimumLength = 0, leftJustified = true) 或
  // String(r, format)；Integer / Boolean / 枚举 → String(x, minimumLength = 0, leftJustified = true)）：定型后实参按位置补齐缺省
  // 打印（S1 探针 `String(x, 6, 0, true)`、`String(n, 0, true)`、`String(x, "6.2f")`）；实参全常量 → 求值成字符串字面量（S2 探针
  // `String(1/3)` → "0.333333"、`String(3, minimumLength = 4)` → "3   "、`String(E.two)` → "two"）。Real 求值结果带指数时
  // 1.25.0（Windows CRT）打三位指数 `1e+010`，与工具链相关 → SKIP。
  Id<Expr> string_call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    if (e.args.empty()) { set_unsupported("function arity"); return lit_int(0); }
    const std::vector<Id<Exp>> args = e.args;   // 拷贝
    const std::vector<Symbol> names = e.arg_names;
    if (!names.empty() && names[0].is_valid()) { set_unsupported("String argument"); return lit_int(0); }
    const bool raw = raw_msg_;   // 方程里 assert / terminate 的消息：实参不化简、整个调用不求值（见 raw_message）
    auto arg = [&](Id<Exp> a) { const Id<Expr> x = inst_exp(a, enclosing, prefix); return raw ? x : simplify_expr(x); };
    const Id<Expr> v = arg(args[0]);
    if (result_.unsupported) return lit_int(0);
    if (!expr_shape(v).empty()) { set_unsupported("String argument"); return lit_int(0); }
    const TypeKind vk = result_.model.exprs[v].ty.kind;
    const bool real = vk == TypeKind::Real;
    if (!real && vk != TypeKind::Integer && vk != TypeKind::Boolean && vk != TypeKind::Enumeration) {
      set_unsupported("String argument type");
      return lit_int(0);
    }
    // 其余实参：位置（Real：significantDigits, minimumLength, leftJustified，第 2 个是 String → format；其余类型：
    // minimumLength, leftJustified）+ 命名。
    const Symbol s_sd = in_.intern("significantDigits"), s_ml = in_.intern("minimumLength"),
                 s_lj = in_.intern("leftJustified"), s_fmt = in_.intern("format");
    std::unordered_map<std::uint32_t, Id<Expr>> slot;
    const std::vector<Symbol> order = real ? std::vector<Symbol>{s_sd, s_ml, s_lj} : std::vector<Symbol>{s_ml, s_lj};
    for (std::size_t i = 1; i < args.size(); ++i) {
      const Id<Expr> x = arg(args[i]);
      if (result_.unsupported) return lit_int(0);
      Symbol nm = i < names.size() ? names[i] : Symbol{};
      if (!nm.is_valid()) {
        if (real && i == 1 && result_.model.exprs[x].ty.kind == TypeKind::String) nm = s_fmt;
        else if (i - 1 < order.size()) nm = order[i - 1];
        else { set_unsupported("function arity"); return lit_int(0); }
      }
      if (slot.count(nm.id)) { set_unsupported("duplicate String argument"); return lit_int(0); }
      slot[nm.id] = x;
    }
    const bool fmt = slot.count(s_fmt.id) > 0;
    for (const auto& [k, x] : slot) {
      const TypeKind xk = result_.model.exprs[x].ty.kind;
      const bool ok = (k == s_fmt.id && real && xk == TypeKind::String && slot.size() == 1) ||
                      (k == s_sd.id && real && !fmt && xk == TypeKind::Integer) ||
                      (k == s_ml.id && !fmt && xk == TypeKind::Integer) || (k == s_lj.id && !fmt && xk == TypeKind::Boolean);
      if (!ok) { set_unsupported("String argument"); return lit_int(0); }
    }
    std::vector<Id<Expr>> full{v};   // 补齐缺省
    if (fmt) {
      full.push_back(slot[s_fmt.id]);
    } else {
      if (real) full.push_back(slot.count(s_sd.id) ? slot[s_sd.id] : lit_int(6));
      full.push_back(slot.count(s_ml.id) ? slot[s_ml.id] : lit_int(0));
      full.push_back(slot.count(s_lj.id) ? slot[s_lj.id] : lit_bool(true));
    }
    bool all_lit = !raw;   // 常量折叠
    for (Id<Expr> x : full) {
      const ExprKind k = result_.model.exprs[x].kind;
      all_lit = all_lit && (k == ExprKind::IntLit || k == ExprKind::RealLit || k == ExprKind::BoolLit ||
                            k == ExprKind::StringLit || k == ExprKind::EnumLit);
    }
    if (all_lit) {
      const Expr ve = result_.model.exprs[v];   // 拷贝
      const double rv = ve.kind == ExprKind::IntLit ? static_cast<double>(ve.ival) : ve.rval;
      std::string s;
      if (fmt) {
        const std::string f = "%" + std::string(in_.str(result_.model.exprs[full[1]].sval));
        char buf[256];
        std::snprintf(buf, sizeof buf, f.c_str(), rv);
        s = buf;
      } else if (real) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.*g", static_cast<int>(result_.model.exprs[full[1]].ival), rv);
        s = buf;
      } else if (ve.kind == ExprKind::IntLit) {
        s = std::to_string(ve.ival);
      } else if (ve.kind == ExprKind::BoolLit) {
        s = ve.bval ? "true" : "false";
      } else {
        s = std::string(in_.str(ve.enum_path.back()));
      }
      if ((real || fmt) && s.find_first_of("eE") != std::string::npos) { set_unsupported("String exponent format"); return lit_int(0); }
      if (!fmt) {
        const std::int64_t ml = result_.model.exprs[full[full.size() - 2]].ival;
        const bool lj = result_.model.exprs[full.back()].bval;
        if (ml > 0 && static_cast<std::int64_t>(s.size()) < ml) {
          const std::string pad(static_cast<std::size_t>(ml) - s.size(), ' ');
          s = lj ? s + pad : pad + s;
        }
      }
      Expr r;
      r.kind = ExprKind::StringLit;
      r.ty = Type::string();
      r.sval = in_.intern(s);
      return add(std::move(r));
    }
    Expr r;
    r.kind = ExprKind::Call;
    r.call_name = {in_.intern("String")};
    r.args = std::move(full);
    r.ty = Type::string();
    return add(std::move(r));
  }

  // 模型语境的向量 / 矩阵积（形状规则 NFTypeCheck.checkBinaryOperationMul :1019-1093）：维已知时 omc 展开成积和（C1 探针：
  // `A * v` → `{A[1,1] * v[1] + A[1,2] * v[2], …}`、`v * v` → `v[1] * v[1] + v[2] * v[2]`），求和下标 k 升序、左结合。
  // 形状不符（omc 报错）/ 维未知 → SKIP。
  Id<Expr> model_array_product(Id<Expr> a, Id<Expr> b) {
    const std::vector<std::int64_t> sa = expr_shape(a), sb = expr_shape(b);
    for (std::int64_t n : sa) if (n < 0) { set_unsupported("array product shape"); return lit_int(0); }
    for (std::int64_t n : sb) if (n < 0) { set_unsupported("array product shape"); return lit_int(0); }
    const bool real = expr_elem_kind(a) == TypeKind::Real || expr_elem_kind(b) == TypeKind::Real;
    auto bin = [&](BinOp op, Id<Expr> x, Id<Expr> y) {
      Expr r;
      r.kind = ExprKind::Binary;
      r.bin = op;
      r.a = x;
      r.b = y;
      r.ty = real ? Type::real() : Type::integer();
      return add(std::move(r));
    };
    // Σ_k a[ia(k)] * b[ib(k)]，k = 1..n；n = 0（空内维）→ 零（sum 的缺省值，NFExpandExp.makeScalarProduct :796-799；
    // OperationMatrixProduct3 `z[1,1] = 0.0`）。
    auto dot = [&](std::int64_t n, auto&& ia, auto&& ib) -> Id<Expr> {
      if (n == 0) return real ? lit_real(0.0) : lit_int(0);
      Id<Expr> acc;
      for (std::int64_t k = 1; k <= n; ++k) {
        Id<Expr> x = scalarize(a, ia(k)), y = scalarize(b, ib(k));
        if (real) { x = to_real(x); y = to_real(y); }
        const Id<Expr> t = bin(BinOp::Mul, x, y);
        acc = acc.is_valid() ? bin(BinOp::Add, acc, t) : t;
      }
      return simplify_expr(acc);
    };
    auto vec = [&](std::vector<Id<Expr>> els, std::vector<Dimension> dims) {
      Expr r;
      r.kind = ExprKind::Array;
      r.ty = Type::array_of(real ? Type::real() : Type::integer(), std::move(dims));
      r.args = std::move(els);
      return add(std::move(r));
    };
    auto dim = [](std::int64_t n) { Dimension d; d.kind = DimKind::Integer; d.size = n; return d; };
    using Ix = std::vector<std::int64_t>;
    if (sa.size() == 1 && sb.size() == 1 && sa[0] == sb[0])   // 向量 · 向量 → 标量
      return dot(sa[0], [](std::int64_t k) { return Ix{k}; }, [](std::int64_t k) { return Ix{k}; });
    if (sa.size() == 2 && sb.size() == 1 && sa[1] == sb[0]) {   // 矩阵 · 向量 → 向量
      std::vector<Id<Expr>> els;
      for (std::int64_t i = 1; i <= sa[0]; ++i)
        els.push_back(dot(sa[1], [i](std::int64_t k) { return Ix{i, k}; }, [](std::int64_t k) { return Ix{k}; }));
      return vec(std::move(els), {dim(sa[0])});
    }
    if (sa.size() == 1 && sb.size() == 2 && sa[0] == sb[0]) {   // 向量 · 矩阵 → 向量
      std::vector<Id<Expr>> els;
      for (std::int64_t j = 1; j <= sb[1]; ++j)
        els.push_back(dot(sa[0], [](std::int64_t k) { return Ix{k}; }, [j](std::int64_t k) { return Ix{k, j}; }));
      return vec(std::move(els), {dim(sb[1])});
    }
    if (sa.size() == 2 && sb.size() == 2 && sa[1] == sb[0]) {   // 矩阵 · 矩阵 → 矩阵
      std::vector<Id<Expr>> rows;
      for (std::int64_t i = 1; i <= sa[0]; ++i) {
        std::vector<Id<Expr>> els;
        for (std::int64_t j = 1; j <= sb[1]; ++j)
          els.push_back(dot(sa[1], [i](std::int64_t k) { return Ix{i, k}; }, [j](std::int64_t k) { return Ix{k, j}; }));
        rows.push_back(vec(std::move(els), {dim(sb[1])}));
      }
      return vec(std::move(rows), {dim(sa[0]), dim(sb[1])});
    }
    set_unsupported("array product shape");
    return lit_int(0);
  }

  // 推导式绑定 `{e for i1 in r1, …, iN in rN}`（标量体）的形状：最右的迭代变量是最外维（MLS §10.4.2.1；ND 探针 c1
  // `{i + j for i in 1:3, j in 1:2}` 是 [2, 3]），各维 = 对应范围的长度。范围求不出 → nullopt。
  // dim_labels：回报各维的标签——范围写的是类型名（`for i in E` / `Boolean`）的那一维是类型维（EN 探针 t3：
  // `Real z[:] = {y[i] * 2 for i in E}` → `z[E.one]`…；标签范围 `E.one:E.two`、`false:true` 给的是整数维），其余为空。
  std::optional<std::vector<std::int64_t>> comprehension_shape(Id<Exp> braw, NodeId scope, const std::vector<Symbol>& prefix,
                                                               std::vector<std::vector<Id<Expr>>>* dim_labels = nullptr) {
    Exp red = I_.absyn.exps[I_.absyn.exps[braw].args[0]];   // 拷贝
    if (red.iters.empty() || !fill_implicit_ranges(red, scope, prefix)) return std::nullopt;
    const FrameAt frame_at(*this, scope);
    std::vector<std::int64_t> sh;
    for (std::size_t k = red.iters.size(); k-- > 0;) {
      const std::optional<IterVals> v = iter_values(red.iters[k].range, scope, prefix);
      if (!v) return std::nullopt;
      sh.push_back(static_cast<std::int64_t>(v->ords.size()));
      if (dim_labels) dim_labels->push_back(v->type_name ? v->labels : std::vector<Id<Expr>>{});
    }
    return sh;
  }
  // 书写式里引用了带类型维的数组（声明维是类型名，或 `:` 维推出来是类型维）：它参与的数组值的维也是类型维。
  bool absyn_refs_type_dim(Id<Exp> id, NodeId scope, const std::vector<Symbol>& prefix) {
    if (!id.is_valid()) return false;
    const Exp e = I_.absyn.exps[id];   // 拷贝
    if (e.kind == ExpKind::CrefExp) {
      const Cref c = I_.absyn.crefs[e.cref];   // 拷贝
      if (!c.leading_dot && !c.parts.empty() && !loop_vars_.count(c.parts[0].name.id) && !stmt_iters_.count(c.parts[0].name.id)) {
        NodeId cls = scope;
        std::vector<Symbol> names = prefix;
        for (std::size_t i = 0; i < c.parts.size(); ++i) {
          const NodeId n = find_local(cls, c.parts[i].name);
          if (!is_comp_node(n)) break;
          const SElement& el = I_.scode.elems[I_.comps[I_.nodes[n].comp].def];
          names.push_back(c.parts[i].name);
          for (const std::vector<Id<Exp>>* ds : {&el.attr.dims, &el.type.dims})
            for (Id<Exp> d : *ds) {
              if (I_.absyn.exps[d].kind == ExpKind::Colon) {
                if (i == 0) {   // 只查得到本实例的直接组件（同 leaf_decl_dims）
                  PSubs ps = cur_psubs_;
                  ps.resize(prefix.size());
                  for (const std::vector<Id<Expr>>& l : emitted_labels(names, ps)) if (!l.empty()) return true;
                }
              } else if (classify_type_dim(d, cls, names)) {
                return true;
              }
            }
          cls = I_.comps[I_.nodes[n].comp].class_inst;
          if (!is_class_inst(cls)) break;
        }
      }
      for (const CrefPart& cp : c.parts)
        for (Id<Exp> sx : cp.subscripts) if (absyn_refs_type_dim(sx, scope, prefix)) return true;
      return false;
    }
    for (Id<Exp> a : e.args) if (absyn_refs_type_dim(a, scope, prefix)) return true;
    for (const ForIndex& it : e.iters) if (absyn_refs_type_dim(it.range, scope, prefix)) return true;
    return absyn_refs_type_dim(e.a, scope, prefix) || absyn_refs_type_dim(e.b, scope, prefix) ||
           absyn_refs_type_dim(e.c, scope, prefix);
  }
  // 推导式绑定在元素 idx 处的值：各迭代变量取其范围的对应值代入 e 并化简（replaceIterator）；第 k 个迭代变量（书写序）
  // 对第 N-1-k 维。维数与迭代变量个数不符 / 范围求不出 / e 是数组 → SKIP。
  Id<Expr> comprehension_elem(Id<Exp> braw, const std::vector<std::int64_t>& idx, NodeId scope,
                              const std::vector<Symbol>& prefix) {
    Exp red = I_.absyn.exps[I_.absyn.exps[braw].args[0]];   // 拷贝
    const std::size_t n = red.iters.size();
    if (n == 0 || idx.size() != n || !fill_implicit_ranges(red, scope, prefix)) {
      set_unsupported("comprehension shape");
      return lit_int(0);
    }
    std::vector<std::int64_t> vals(n);
    std::vector<Id<Expr>> labs(n);   // Boolean / 枚举范围上的迭代变量：取值的标签
    for (std::size_t k = 0; k < n; ++k) {
      const ForIndex& it = red.iters[k];
      bool dup = loop_vars_.count(it.name.id) || stmt_iters_.count(it.name.id);
      for (std::size_t j = 0; j < k; ++j) dup = dup || red.iters[j].name == it.name;
      if (dup) { set_unsupported("shadowed comprehension iterator"); return lit_int(0); }
      if (absyn_refs_iter(it.range, loop_vars_)) { set_unsupported("comprehension range depends on iterator"); return lit_int(0); }
      const std::optional<IterVals> seq = iter_values(it.range, scope, prefix);
      const std::int64_t ix = idx[n - 1 - k];
      if (!seq || ix < 1 || static_cast<std::size_t>(ix) > seq->ords.size()) {
        if (!result_.unsupported) set_unsupported("comprehension range");
        return lit_int(0);
      }
      vals[k] = seq->ords[static_cast<std::size_t>(ix) - 1];
      if (seq->type_label.is_valid()) labs[k] = seq->labels[static_cast<std::size_t>(ix) - 1];
    }
    for (std::size_t k = 0; k < n; ++k) {
      loop_vars_[red.iters[k].name.id] = vals[k];
      if (labs[k].is_valid()) loop_labels_[red.iters[k].name.id] = labs[k];
    }
    const Id<Expr> x = simplify_expr(inst_exp(red.a, scope, prefix));
    for (std::size_t k = 0; k < n; ++k) {
      loop_vars_.erase(red.iters[k].name.id);
      loop_labels_.erase(red.iters[k].name.id);
    }
    if (!result_.unsupported && !expr_shape(x).empty()) set_unsupported("array-valued comprehension body");
    return x;
  }

  // 推导式表达式 `{e for i in r, j in s}`：omc 保留成 `array(e for i in r)`，最右的迭代变量在最外层（嵌套
  // `array(array(e for j in s) for i in r)` 的反序：`{e for j in 1:3, i in 1:2}` → `array(array(e for j in 1:3) for i in 1:2)`，
  // AC2 / AC3 探针）。迭代变量在体里作符号引用（stmt_iters_，Integer）；范围须求成整数等差序列（参数范围按结构参数求值，
  // 打成字面量 `1:3`、参数标 final，typeRange :2455-2460）。返回 Reduction（call_name = {array, 迭代变量}、a = 体、b = 范围、
  // ty = 体类型 + 各层长度）；不适用 → 标 SKIP、返回占位。
  Id<Expr> comprehension_expr(Id<Exp> red_id, NodeId scope, const std::vector<Symbol>& prefix) {
    Exp red = I_.absyn.exps[red_id];   // 拷贝（`{…}` 里那个 Reduction）
    if (red.iters.empty()) { set_unsupported("comprehension shape"); return lit_int(0); }
    if (!fill_implicit_ranges(red, scope, prefix)) { set_unsupported("comprehension implicit range"); return lit_int(0); }
    std::vector<Id<Expr>> ranges;
    std::vector<std::int64_t> lens;
    std::vector<std::int64_t> seq0;   // 首个迭代变量取的值（单迭代构造子的展开用）
    std::vector<Id<Expr>> ilabels;    // 各迭代变量：Boolean / 枚举范围 → 该类型的一个标签；整数范围 → invalid
    for (const ForIndex& it : red.iters) {
      if (loop_vars_.count(it.name.id) || stmt_iters_.count(it.name.id)) { set_unsupported("shadowed comprehension iterator"); return lit_int(0); }
      // 范围引用 for 方程的迭代变量：1.25.0 定型期求值失败、报内部错（`evalBinaryAdd failed to evaluate 'i + 2'`，FF10 / FF12）。
      if (absyn_refs_iter(it.range, loop_vars_)) { set_unsupported("comprehension range depends on iterator"); return lit_int(0); }
      // Boolean / 枚举上的范围：迭代变量取标签，保留成 `array(y[i] for i in {E.one, E.two, E.three})` / `… in E.one:E.three`
      //（EN 探针 t1）。空的、单迭代只有一个元素的（omc 展开）未核实 → SKIP。
      if (std::optional<IterVals> lv = label_iter_values(it.range, scope, prefix)) {
        if (lv->labels.empty()) { set_unsupported("comprehension range"); return lit_int(0); }
        if (red.iters.size() == 1 && lv->labels.size() == 1) {   // 单元素：展开成 `{体[i := 该值]}`（同整数范围，见下）
          loop_vars_[it.name.id] = lv->ords[0];
          loop_labels_[it.name.id] = lv->labels[0];
          const Id<Expr> el = simplify_expr(inst_exp(red.a, scope, prefix));
          loop_vars_.erase(it.name.id);
          loop_labels_.erase(it.name.id);
          if (result_.unsupported) return lit_int(0);
          Dimension d;
          d.kind = DimKind::Integer;
          d.size = 1;
          Expr arr;
          arr.kind = ExprKind::Array;
          arr.ty = Type::array_of(result_.model.exprs[el].ty, {d});
          arr.args = {el};
          return add(std::move(arr));
        }
        lens.push_back(static_cast<std::int64_t>(lv->labels.size()));
        ranges.push_back(lv->kept);
        ilabels.push_back(lv->type_label);
        continue;
      }
      if (result_.unsupported) return lit_int(0);
      ilabels.emplace_back();
      std::pair<std::int64_t, std::int64_t> written{1, 0};
      const std::optional<std::vector<std::int64_t>> seq = eval_range(it.range, scode_scope_of(scope), 0, &written);
      if (!seq || seq->empty()) { set_unsupported("comprehension range"); return lit_int(0); }
      if (seq0.empty()) seq0 = *seq;
      // 只有一个元素的单迭代构造子：omc 展开成 `{体[i := 该值]}` 再化简（simplifyArrayConstructor 的 dim_size == 1 支，
      // NFSimplifyExp.mo:609-615 v1.25.0；AC 探针 k1：`{i * a for i in 2:2}` → `{2.0 * a}`，等式里随之逐元素 `y[1] = a`）。
      // 多个迭代变量的不展开（`array(array(… for i in 1:1) for j in 1:2)`）。迭代变量按 for 方程的展开取值绑定。
      if (red.iters.size() == 1 && seq->size() == 1) {
        loop_vars_[it.name.id] = seq->front();
        const Id<Expr> el = simplify_expr(inst_exp(red.a, scope, prefix));
        loop_vars_.erase(it.name.id);
        if (result_.unsupported) return lit_int(0);
        Dimension d;
        d.kind = DimKind::Integer;
        d.size = 1;
        Expr arr;
        arr.kind = ExprKind::Array;
        arr.ty = Type::array_of(result_.model.exprs[el].ty, {d});
        arr.args = {el};
        return add(std::move(arr));
      }
      lens.push_back(static_cast<std::int64_t>(seq->size()));
      // 范围写成数组构造子（`for i in {1, 3}`）：照写成字面量（k1 探针 `array(x[i] for i in {1, 3})`；此前按首尾打成 `1:3`）。
      if (is_array_ctor(it.range)) {
        Dimension d;
        d.kind = DimKind::Integer;
        d.size = lens.back();
        Expr arr;
        arr.kind = ExprKind::Array;
        arr.ty = Type::array_of(Type::integer(), {d});
        for (std::int64_t v : *seq) arr.args.push_back(lit_int(v));
        ranges.push_back(add(std::move(arr)));
        continue;
      }
      Expr r;
      r.kind = ExprKind::Range;
      r.ty = Type::integer();
      r.a = lit_int(seq->front());
      if (I_.absyn.exps[it.range].c.is_valid()) {   // 原式带步长 → `a:s:b`，步长 / 终点照写（k2 探针 `for i in 1:2:4`）
        r.b = lit_int(written.first);
        r.c = lit_int(written.second);
      } else {
        r.b = lit_int(seq->back());
      }
      ranges.push_back(add(std::move(r)));
    }
    for (std::size_t k = 0; k < red.iters.size(); ++k) {
      const std::uint32_t id = red.iters[k].name.id;
      stmt_iters_[id] = ilabels[k].is_valid() ? label_type(ilabels[k]).kind : TypeKind::Integer;
      if (ilabels[k].is_valid()) stmt_iter_labels_[id] = ilabels[k];
    }
    const Id<Expr> body = simplify_expr(inst_exp(red.a, scope, prefix));
    for (const ForIndex& it : red.iters) {
      stmt_iters_.erase(it.name.id);
      stmt_iter_labels_.erase(it.name.id);
    }
    if (result_.unsupported) return lit_int(0);
    // 体是数组：只认嵌套推导式（`{{i * j for j in 1:3} for i in 1:2}` → `array(array(… for j in 1:3) for i in 1:2)`，AC2）；
    // 数组字面量等其它数组体的打印形态未核实 → SKIP。
    if (!expr_shape(body).empty() && result_.model.exprs[body].kind != ExprKind::Reduction) {
      set_unsupported("array-valued comprehension body");
      return lit_int(0);
    }
    // 体恰是"数组字面量取迭代变量作下标"（`{({a, b, c})[i] for i in r}`）、范围是字面量：omc 展开成按范围取出的元素
    //（simplifyArrayConstructor 的 isIteratorSubscriptedArray 支，NFSimplifyExp.mo:616-623、:639-650 v1.25.0；AC 探针 k1 / k2：
    // → `{a, b, c}`、`for i in {1, 3}` → `{a, c}`）。
    if (red.iters.size() == 1 && !ilabels[0].is_valid()) {
      const Expr be = result_.model.exprs[body];   // 拷贝
      if (be.kind == ExprKind::Subscript && be.args.size() == 1 && result_.model.exprs[be.a].kind == ExprKind::Array) {
        const Expr se = result_.model.exprs[be.args[0]];
        if (se.kind == ExprKind::Cref && se.cref.parts.size() == 1 && se.cref.parts[0].subscripts.empty() &&
            se.cref.parts[0].name == red.iters[0].name) {
          const std::vector<Id<Expr>> src = result_.model.exprs[be.a].args;
          Dimension d;
          d.kind = DimKind::Integer;
          d.size = static_cast<std::int64_t>(seq0.size());
          Expr arr;
          arr.kind = ExprKind::Array;
          arr.ty = Type::array_of(be.ty, {d});
          for (std::int64_t v : seq0) {
            if (v < 1 || static_cast<std::size_t>(v) > src.size()) { set_unsupported("array subscript out of range"); return lit_int(0); }
            arr.args.push_back(src[static_cast<std::size_t>(v) - 1]);
          }
          return add(std::move(arr));
        }
      }
    }
    Id<Expr> e = body;
    for (std::size_t k = 0; k < red.iters.size(); ++k) {   // 左起第一个迭代变量最内层
      Dimension d;
      d.kind = DimKind::Integer;
      d.size = lens[k];
      Expr r;
      r.kind = ExprKind::Reduction;
      r.call_name = {in_.intern("array"), red.iters[k].name};
      r.a = e;
      r.b = ranges[k];
      r.ty = Type::array_of(result_.model.exprs[e].ty, {d});
      e = add(std::move(r));
    }
    return e;
  }

  // linspace(x1, x2, n)：内建、__OpenModelica_EarlyInline（NFModelicaBuiltin.mo:305-316，体 `v := {x1 + (x2-x1)*(i-1)/(n-1)
  // for i in 1:n}`）——内联成推导式再化简（CT1 探针：`linspace(0, 1, 4)` → `array(/*Real*/(i - 1) / 3.0 for i in 1:4)`、
  // `linspace(0, time, 3)` → `array(time * /*Real*/(i - 1) / 2.0 for i in 1:3)`；参数数组逐元素求值 `q[2] = 1.5`）。n 须求成
  // ≥ 2 的常量（结构参数）；命名实参 / 非数值标量实参 / 迭代变量名 i 被外层占用 → SKIP。
  Id<Expr> linspace_call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    for (Symbol nm : e.arg_names)
      if (nm.is_valid()) { set_unsupported("named function argument"); return lit_int(0); }
    if (e.args.size() != 3) { set_unsupported("function arity"); return lit_int(0); }
    const std::vector<Id<Exp>> args = e.args;   // 拷贝
    const Symbol it = in_.intern("i");
    if (loop_vars_.count(it.id) || stmt_iters_.count(it.id)) { set_unsupported("shadowed comprehension iterator"); return lit_int(0); }
    Id<Expr> x[2];
    for (int k = 0; k < 2; ++k) {
      x[k] = simplify_expr(inst_exp(args[k], enclosing, prefix));
      if (result_.unsupported) return lit_int(0);
      const TypeKind tk = result_.model.exprs[x[k]].ty.kind;
      if ((tk != TypeKind::Real && tk != TypeKind::Integer) || !expr_shape(x[k]).empty()) {
        set_unsupported("non-numeric function argument");
        return lit_int(0);
      }
      x[k] = to_real(x[k]);
    }
    const std::optional<CVal> nv = eval_scode(args[2], scode_scope_of(enclosing), 0, /*allow_param=*/true, nullptr);
    if (!nv || nv->k != CVal::K::Int || nv->i < 2) { set_unsupported("linspace size"); return lit_int(0); }
    auto bin = [&](Id<Expr> a, BinOp op, Id<Expr> b, const Type& ty) {
      Expr r;
      r.kind = ExprKind::Binary;
      r.bin = op;
      r.a = a;
      r.b = b;
      r.ty = ty;
      return add(std::move(r));
    };
    Expr ic;
    ic.kind = ExprKind::Cref;
    ic.ty = Type::integer();
    ic.cref.parts.push_back({it, {}});
    const Id<Expr> im1 = to_real(bin(add(std::move(ic)), BinOp::Sub, lit_int(1), Type::integer()));   // /*Real*/(i - 1)
    const Id<Expr> step = bin(bin(bin(x[1], BinOp::Sub, x[0], Type::real()), BinOp::Mul, im1, Type::real()), BinOp::Div,
                              lit_real(static_cast<double>(nv->i - 1)), Type::real());
    const Id<Expr> body = simplify_expr(bin(x[0], BinOp::Add, step, Type::real()));
    Expr rg;
    rg.kind = ExprKind::Range;
    rg.ty = Type::integer();
    rg.a = lit_int(1);
    rg.b = lit_int(nv->i);
    Dimension d;
    d.kind = DimKind::Integer;
    d.size = nv->i;
    Expr r;
    r.kind = ExprKind::Reduction;
    r.call_name = {in_.intern("array"), it};
    r.a = body;
    r.b = add(std::move(rg));
    r.ty = Type::array_of(Type::real(), {d});
    return add(std::move(r));
  }

  // ---- 归约式 sum / product / min / max(e for i in r, …)（模型语境）。规则读自 v1.25.0：定型 NFCall.typeReduction
  // :2544-2599（范围按 typeIterator 定型，参数范围由 typeRange 标结构；变率 = 各范围与体的最大者）、化简
  // NFSimplifyExp.simplifyReduction :652-705；形态经 RD 探针 r2 / r3 核实：
  //   S1 单迭代变量且范围长度已知：0 → 缺省值（reduction_default）；1 → 体代入那个元素（`max(x[i] for i in 2:2)` → `x[2]`）；
  //   S2 整个归约的变率 ≤ 结构参数（不引用变量，引用的参数在定型此式之前就已是结构参数）→ 求值成字面量；
  //   S3 sum / product 且各范围都能展开 → 逐个取值代入体、各自化简后连成 + / * 链（最后写的迭代变量在最外层：
  //      `sum(x[i] * x[j] for i in 1:2, j in 1:3)` → `x[1] * x[1] + x[2] * x[1] + x[1] * x[2] + …`）；
  //   S4 其余保留归约式，多个迭代变量打成嵌套（先写的在最内层：`max(max(x[i] * x[j] for i in 1:2) for j in 1:3)`），范围
  //      打成求值后的字面量范围 / 原样的数组（`1:3`、`{1, 3}`、`x[1:2]`、`1:integer(time)`）。
  // 迭代变量的类型 = 范围元素类型（Integer / Real / Boolean；枚举未做）。----
  struct RedIter {
    Symbol name;
    TypeKind kind = TypeKind::Integer;
    Id<Expr> range;                 // 保留形态里打印的范围式
    Id<Expr> label;                 // Boolean / 枚举上的范围：该类型的一个标签（体里类型维下标的核对用）
    bool known = false;             // 元素已知（可展开）
    std::vector<Id<Expr>> elems;    // 已知时的各元素
    // sum / product 展开前 omc 对范围做 Ceval.evalExp（foldReduction2，NFExpression.mo:5885 v1.25.0），求不出就整个保留：
    bool foldable = true;           // 各元素是字面量 / 参数式（取值）/ 变量或迭代变量的裸引用（原样）
    bool param = false;             // 有参数式元素（展开时取值代入、不打结构标记）
    bool call = false;              // 求不出的元素里有调用（omc 对 der / noEvent / pre 的求值是怪行为，不跟）
  };
  // 表达式引用了定型它之前还不是结构参数的参数（omc 里变率是 PARAMETER 而非 STRUCTURAL_PARAMETER）。多段 / 带下标的参数引用
  // 保守当普通参数。
  bool absyn_refs_plain_param(Id<Exp> s, NodeId enclosing, const std::vector<Symbol>& prefix) {
    if (!s.is_valid()) return false;
    const Exp& e = I_.absyn.exps[s];
    if (e.kind == ExpKind::CrefExp) {
      const Cref& c = I_.absyn.crefs[e.cref];
      if (!c.leading_dot && !c.parts.empty() && !loop_vars_.count(c.parts[0].name.id) && !stmt_iters_.count(c.parts[0].name.id)) {
        if (absyn_cref_var(c, enclosing) == std::optional<SVariability>(SVariability::Param)) {
          if (c.parts.size() != 1 || !c.parts[0].subscripts.empty()) return true;
          std::vector<Symbol> p = prefix;
          p.push_back(c.parts[0].name);
          std::vector<std::vector<Id<Expr>>> ps = psubs_to_exprs(cur_psubs_);
          ps.resize(prefix.size());
          ps.push_back({});
          if (!structural_set_.count(conn_key(p, ps))) return true;
        }
      }
      for (const CrefPart& part : c.parts)
        for (Id<Exp> x : part.subscripts)
          if (absyn_refs_plain_param(x, enclosing, prefix)) return true;
      return false;
    }
    if (absyn_refs_plain_param(e.a, enclosing, prefix) || absyn_refs_plain_param(e.b, enclosing, prefix) ||
        absyn_refs_plain_param(e.c, enclosing, prefix))
      return true;
    for (Id<Exp> a : e.args) if (absyn_refs_plain_param(a, enclosing, prefix)) return true;
    for (const ForIndex& it : e.iters) if (absyn_refs_plain_param(it.range, enclosing, prefix)) return true;
    return false;
  }
  // 需要在定型期按结构参数求值的式子（fill 的维、归约的范围）里既有迭代变量（展开中的 / 保留着的）、又有还不是结构参数的
  // 参数：omc 定型时迭代变量还没有值，求不了也不打标记（FF16 `sum(x[j] for j in 1:n + 1 - 1) + …`、FF21
  // `max(sum(x[j] for j in i:n) for i in 1:2)`，n 都不 final）。
  bool iter_blocks_structural(Id<Exp> s, NodeId enclosing, const std::vector<Symbol>& prefix) {
    return (absyn_refs_iter(s, loop_vars_) || absyn_refs_iter(s, stmt_iters_)) && absyn_refs_plain_param(s, enclosing, prefix);
  }
  // 缺省值（Call.reductionDefaultValue :2626-2648 + Expression.makeMaxValue / makeMinValue，NFExpression.mo:4908-4934，
  // v1.25.0）：sum 0、product 1、min → 类型最大值、max → 类型最小值。Real = ±8.777798510069901e304（System.realMaxLit）、
  // Boolean = true / false、枚举 = 末 / 首字面量。Integer 的极值是 System.intMaxLit，随 omc 构建的整数位宽而变（本机 1.25.0
  // 打 1073741823，testsuite 文件里是 4611686018427387903）→ 不跟，SKIP（账本）。
  Id<Expr> reduction_default(std::string_view name, const Type& ty) {
    const TypeKind k = ty.kind;
    if (name == "sum" || name == "product") {
      const std::int64_t unit = name == "sum" ? 0 : 1;
      if (k == TypeKind::Real) return lit_real(static_cast<double>(unit));
      if (k == TypeKind::Integer) return lit_int(unit);
      set_unsupported("non-numeric array operand");
      return lit_int(0);
    }
    const bool hi = name == "min";   // min 的缺省 = 最大值
    if (k == TypeKind::Real) return lit_real(hi ? 8.777798510069901e304 : -8.777798510069901e304);
    if (k == TypeKind::Boolean) return lit_bool(hi);
    if (k == TypeKind::Enumeration && !ty.enum_literals.empty() && !ty.enum_path.empty()) {
      Expr r;
      r.kind = ExprKind::EnumLit;
      r.ty = Type::of(TypeKind::Enumeration);
      r.enum_path = ty.enum_path;
      r.enum_path.push_back(hi ? ty.enum_literals.back() : ty.enum_literals.front());
      r.enum_index = hi ? static_cast<std::int64_t>(ty.enum_literals.size()) : 1;
      return add(std::move(r));
    }
    set_unsupported(k == TypeKind::Integer ? "Integer extreme of empty min/max (platform-dependent in omc)"
                                           : "empty min/max of this type");
    return lit_int(0);
  }
  // 字面量的 min / max（数值按值、Boolean false < true、枚举按序数）；平手留先出现者。els 须非空且全是同类字面量。
  Id<Expr> fold_min_max(const std::vector<Id<Expr>>& els, bool is_min) {
    auto rank = [&](Id<Expr> x) -> double {
      const Expr& e = result_.model.exprs[x];
      return e.kind == ExprKind::BoolLit ? (e.bval ? 1.0 : 0.0)
           : e.kind == ExprKind::EnumLit ? static_cast<double>(e.enum_index)
           : as_real(x);
    };
    Id<Expr> best = els[0];
    for (Id<Expr> x : els)
      if (is_min ? rank(x) < rank(best) : rank(x) > rank(best)) best = x;
    return best;
  }
  bool is_minmax_literal(Id<Expr> x) const {
    const Expr& e = result_.model.exprs[x];
    return e.kind == ExprKind::IntLit || e.kind == ExprKind::RealLit || e.kind == ExprKind::BoolLit ||
           (e.kind == ExprKind::EnumLit && e.enum_index > 0);
  }
  // 一个迭代范围的定型。失败 → set_unsupported 并返回 false。
  bool red_iter_range(const ForIndex& it, NodeId enclosing, const std::vector<Symbol>& prefix, RedIter& r) {
    r.name = it.name;
    if (!it.range.is_valid()) { set_unsupported("comprehension implicit range"); return false; }
    // Boolean / 枚举上的范围（类型名、标签范围、标签数组）：元素是标签（EN 探针 t1：`sum(y[i] for i in E)` 展开成
    // `y[E.one] + y[E.two] + y[E.three]`、`max(y[i] for i in E)` 保留成 `max(y[i] for i in {E.one, E.two, E.three})`）。
    if (std::optional<IterVals> lv = label_iter_values(it.range, enclosing, prefix)) {
      r.kind = label_type(lv->type_label).kind;
      r.range = lv->kept;
      r.label = lv->type_label;
      r.known = true;
      r.elems = lv->labels;
      return true;
    }
    if (result_.unsupported) return false;
    const Exp rx = I_.absyn.exps[it.range];   // 拷贝
    if (rx.kind == ExpKind::Range) {
      const ScodeScope sc = scode_scope_of(enclosing);
      // 界里既有迭代变量又有参数：1.25.0 定型期不求值、不标结构参数，展开后留成 `sum(x[j] for j in 1:n + 1 - 1)`
      //（AP ff 探针 FF13 / FF14）——未做 → SKIP。
      if (iter_blocks_structural(it.range, enclosing, prefix)) { set_unsupported("reduction range depends on iterator"); return false; }
      // 整数范围、界可求（含参数 → 标结构，typeRange :2414-2418）：字面量范围 + 元素表。
      if (const std::optional<std::vector<std::int64_t>> seq = eval_range(it.range, sc, 0)) {
        auto bound = [&](Id<Exp> b) {
          const std::optional<CVal> v = eval_scode(b, sc, 0, /*allow_param=*/true, nullptr);
          return lit_int(v && v->k == CVal::K::Int ? v->i : 0);
        };
        Expr rg;
        rg.kind = ExprKind::Range;
        rg.ty = Type::integer();
        rg.a = bound(rx.a);
        rg.b = bound(rx.b);
        if (rx.c.is_valid()) rg.c = bound(rx.c);
        r.range = add(std::move(rg));
        r.known = true;
        for (std::int64_t v : *seq) r.elems.push_back(lit_int(v));
        return true;
      }
      if (result_.unsupported) return false;
      Expr t = result_.model.exprs[inst_exp(it.range, enclosing, prefix)];   // 拷贝
      if (result_.unsupported) return false;
      t.a = simplify_expr(t.a);
      t.b = simplify_expr(t.b);
      if (t.c.is_valid()) t.c = simplify_expr(t.c);
      r.kind = t.ty.kind;
      // 运行时范围：界引用变量，或引用外层保留着的符号迭代变量（`max(sum(x[i] for i in 1:j) for j in 1:2)` 里的 `1:j`）。
      const bool runtime = refs_runtime_var(t.a) || refs_runtime_var(t.b) || (t.c.is_valid() && refs_runtime_var(t.c)) ||
                           is_stmt_iter_subscript(it.range);
      if (r.kind == TypeKind::Boolean && !t.c.is_valid() && result_.model.exprs[t.a].kind == ExprKind::BoolLit &&
          result_.model.exprs[t.b].kind == ExprKind::BoolLit) {
        // Boolean 字面量范围：false < true。
        const bool lo = result_.model.exprs[t.a].bval, hi = result_.model.exprs[t.b].bval;
        r.known = true;
        for (int v = lo ? 1 : 0; v <= (hi ? 1 : 0); ++v) r.elems.push_back(lit_bool(v != 0));
      } else if (r.kind != TypeKind::Integer || !runtime) {
        // Real 范围（omc 也展开 / 求值，元素个数的取整规则未核实）、求不出的参数范围 → SKIP。
        set_unsupported("reduction range");
        return false;
      }
      r.range = add(std::move(t));
      return true;
    }
    // 数组：一维，元素 Integer / Real / Boolean；长度已知 → 元素表。
    const Id<Expr> a = simplify_expr(inst_exp(it.range, enclosing, prefix));
    if (result_.unsupported) return false;
    const std::vector<std::int64_t> sh = expr_shape(a);
    r.kind = expr_elem_kind(a);
    if (sh.size() != 1 || (r.kind != TypeKind::Integer && r.kind != TypeKind::Real && r.kind != TypeKind::Boolean)) {
      set_unsupported("reduction range");
      return false;
    }
    r.range = a;
    if (sh[0] >= 0) {
      r.known = true;
      // 保留形态 = 化简后的范围，同 min / max 的数组实参（kept_array_form）：整体 / 切片引用原样、字面量逐叶转成元素类型
      //（`{m, 2.5}` → `{/*Real*/(m), 2.5}`）、算术式逐元素展开（`2 * x` → `{2.0 * x[1], …}`）；其余（调用）保留时的形态未核实
      //（range 置无效，要保留时 SKIP）。元素从这个形态取——转型也算在元素里（AP p8–p11 探针）。
      r.range = kept_array_form(a, sh, r.kind);
      const Id<Expr> src = r.range.is_valid() ? r.range : a;
      for (std::int64_t i = 1; i <= sh[0]; ++i) {
        const Id<Expr> el = simplify_expr(scalarize(src, {i}));
        if (result_.unsupported) return false;
        r.elems.push_back(el);
        if (is_lit(el)) continue;
        if (!red_elem_symbolic(el)) { r.param = true; continue; }
        if (result_.model.exprs[el].kind == ExprKind::Cref) {
          // 变量引用的下标里有参数：Ceval 是否求下标未核实 → SKIP。
          if (refs_param_var(el)) { set_unsupported("reduction range"); return false; }
          continue;
        }
        r.foldable = false;
        r.call = r.call || has_call(el);
      }
    }
    return true;
  }
  // 范围元素不是编译期可求的：引用了变量 / time，或外层保留着的符号迭代变量。
  bool red_elem_symbolic(Id<Expr> id) {
    if (!id.is_valid()) return false;
    if (refs_runtime_var(id)) return true;
    const Expr& e = result_.model.exprs[id];
    if (e.kind == ExprKind::Cref) {
      if (e.cref.resolved_node == kFnLocal || (e.cref.parts.size() == 1 && stmt_iters_.count(e.cref.parts[0].name.id)))
        return true;
      for (const ExprCrefPart& cp : e.cref.parts) for (Id<Expr> sx : cp.subscripts) if (red_elem_symbolic(sx)) return true;
      return false;
    }
    for (Id<Expr> a : e.args) if (red_elem_symbolic(a)) return true;
    return red_elem_symbolic(e.a) || red_elem_symbolic(e.b) || red_elem_symbolic(e.c);
  }
  bool has_call(Id<Expr> id) {
    if (!id.is_valid()) return false;
    const Expr& e = result_.model.exprs[id];
    if (e.kind == ExprKind::Call) return true;
    for (Id<Expr> a : e.args) if (has_call(a)) return true;
    return has_call(e.a) || has_call(e.b) || has_call(e.c);
  }
  Id<Expr> reduction_call(const Exp& e, std::string_view name, NodeId enclosing, const std::vector<Symbol>& prefix) {
    Exp red = I_.absyn.exps[e.args[0]];   // 拷贝
    const bool is_sum = name == "sum", is_prod = name == "product", is_min = name == "min";
    if (red.iters.empty()) { set_unsupported("reduction iterators"); return lit_int(0); }
    if (!fill_implicit_ranges(red, enclosing, prefix)) { set_unsupported("comprehension implicit range"); return lit_int(0); }
    for (const ForIndex& it : red.iters)
      if (loop_vars_.count(it.name.id) || stmt_iters_.count(it.name.id)) { set_unsupported("shadowed reduction iterator"); return lit_int(0); }
    // 变率（S2 用）：在求范围（会标结构参数）之前看。
    bool plain_param = absyn_refs_plain_param(red.a, enclosing, prefix);
    for (const ForIndex& it : red.iters) plain_param = plain_param || absyn_refs_plain_param(it.range, enclosing, prefix);
    std::vector<RedIter> its(red.iters.size());
    bool all_known = true;
    for (std::size_t j = 0; j < red.iters.size(); ++j) {
      if (!red_iter_range(red.iters[j], enclosing, prefix, its[j])) return lit_int(0);
      all_known = all_known && its[j].known;
    }
    // 体在给定取值下的实例：整数字面量取值走 loop_vars_（体里的下标、常量数组元素照常解析），其余先按符号定型再代入。
    // cur[j] = 第 j 个迭代变量取第几个元素；npos = 保持符号（保留形态用）。
    const std::size_t npos = static_cast<std::size_t>(-1);
    auto instance = [&](const std::vector<std::size_t>& cur) -> Id<Expr> {
      std::unordered_map<std::string, Id<Expr>> sub;
      for (std::size_t j = 0; j < its.size(); ++j) {
        const std::uint32_t id = its[j].name.id;
        const Id<Expr> el = cur[j] == npos ? Id<Expr>{} : its[j].elems[cur[j]];
        if (el.is_valid() && result_.model.exprs[el].kind == ExprKind::IntLit) {
          loop_vars_[id] = result_.model.exprs[el].ival;
        } else if (el.is_valid() && its[j].label.is_valid() && sub_ordinal(el)) {
          // Boolean / 枚举范围的取值：同 for 方程的迭代变量（序数 + 标签），体里的类型维下标、常量数组元素照常解析。
          loop_vars_[id] = *sub_ordinal(el);
          loop_labels_[id] = el;
        } else {
          stmt_iters_[id] = its[j].kind;
          if (its[j].label.is_valid()) stmt_iter_labels_[id] = its[j].label;
          if (el.is_valid()) {
            ExprCref ic;
            ic.parts.push_back({its[j].name, {}});
            sub.emplace(expr_cref_key(ic), el);
          }
        }
      }
      Id<Expr> x = simplify_expr(inst_exp(red.a, enclosing, prefix));
      for (const RedIter& ri : its) {
        loop_vars_.erase(ri.name.id);
        loop_labels_.erase(ri.name.id);
        stmt_iters_.erase(ri.name.id);
        stmt_iter_labels_.erase(ri.name.id);
      }
      if (result_.unsupported) return lit_int(0);
      if (!sub.empty()) x = simplify_expr(clone_subst(x, sub));
      if (!result_.unsupported && !expr_shape(x).empty()) set_unsupported("array-valued reduction body");
      return x;
    };
    // 全部取值的体实例（最后写的迭代变量在最外层）。
    auto expand = [&](std::vector<Id<Expr>>& out) {
      std::vector<std::size_t> cur(its.size(), 0);
      auto rec = [&](auto& self, std::size_t j) -> void {
        if (result_.unsupported) return;
        for (std::size_t i = 0; i < its[j].elems.size(); ++i) {
          cur[j] = i;
          if (j == 0) out.push_back(instance(cur));
          else self(self, j - 1);
          if (result_.unsupported) return;
        }
      };
      rec(rec, its.size() - 1);
    };
    auto symbolic = [&]() { return instance(std::vector<std::size_t>(its.size(), npos)); };
    // S1：单迭代变量、长度 0 / 1。
    if (its.size() == 1 && its[0].known && its[0].elems.size() <= 1) {
      if (its[0].elems.size() == 1) return instance({0});
      const Id<Expr> body = symbolic();
      if (result_.unsupported) return lit_int(0);
      return reduction_default(name, result_.model.exprs[body].ty);
    }
    bool foldable = true, range_call = false;
    for (const RedIter& ri : its) { foldable = foldable && ri.foldable; range_call = range_call || ri.call; }
    if ((is_sum || is_prod) && all_known && !foldable && range_call) {
      set_unsupported("reduction range element with call");
      return lit_int(0);
    }
    if ((is_sum || is_prod) && all_known && foldable) {   // S3（S2 的 sum / product 展开后自然折成字面量）
      // 范围里的参数式取值代入、不打结构标记（AP p2 / p6 探针 `sum(x[i] for i in k)` → `x[1] + x[2]`、k 不 final；
      // `{q[1] * 2, t}` → `2.0 * t + t * t`）。有原样留下的变量引用时只能逐元素求，范围须是书写出来的数组字面量。
      for (std::size_t j = 0; j < its.size(); ++j) {
        RedIter& ri = its[j];
        if (!ri.param) continue;
        const Id<Exp> raw = red.iters[j].range;
        auto eval = [&](Id<Exp> x) {
          const bool saved = dep_mark_;
          dep_mark_ = false;
          std::optional<CVal> v = eval_scode(x, scode_scope_of(enclosing), 0, /*allow_param=*/true, nullptr);
          dep_mark_ = saved;
          return v;
        };
        const std::vector<Id<Exp>> rargs = I_.absyn.exps[raw].args;   // 拷贝
        const bool per_elem = written_array_literal(raw) && rargs.size() == ri.elems.size();
        std::optional<CVal> whole;
        if (!per_elem) {
          whole = eval(raw);
          if (!whole || !is_arr(*whole) || !whole->arr || whole->arr->size() != ri.elems.size()) {
            set_unsupported("reduction range");
            return lit_int(0);
          }
        }
        for (std::size_t i = 0; i < ri.elems.size(); ++i) {
          if (is_lit(ri.elems[i]) || red_elem_symbolic(ri.elems[i])) continue;
          const std::optional<CVal> v = per_elem ? eval(rargs[i]) : std::optional<CVal>((*whole->arr)[i]);
          const std::optional<CVal> c = v ? coerce_to_kind(*v, ri.kind) : std::nullopt;
          if (!c) { set_unsupported("reduction range"); return lit_int(0); }
          ri.elems[i] = literal_of(*c);
        }
      }
      std::vector<Id<Expr>> els;
      expand(els);
      if (result_.unsupported) return lit_int(0);
      if (els.empty()) {
        const Id<Expr> body = symbolic();
        if (result_.unsupported) return lit_int(0);
        return reduction_default(name, result_.model.exprs[body].ty);
      }
      bool real = false;
      for (Id<Expr> x : els) {
        const TypeKind k = result_.model.exprs[x].ty.kind;
        if (k != TypeKind::Real && k != TypeKind::Integer) { set_unsupported("non-numeric array operand"); return lit_int(0); }
        real = real || k == TypeKind::Real;
      }
      Id<Expr> acc;
      for (Id<Expr> x : els) {
        if (real) x = to_real(x);
        if (!acc.is_valid()) { acc = x; continue; }
        Expr b;
        b.kind = ExprKind::Binary;
        b.bin = is_sum ? BinOp::Add : BinOp::Mul;
        b.a = acc;
        b.b = x;
        b.ty = real ? Type::real() : Type::integer();
        acc = add(std::move(b));
      }
      return simplify_expr(acc);
    }
    // S2：min / max 求值。先按取值展开（体里的常量数组元素 `c[i]`、嵌套归约照常解析），各实例全是字面量即求值。
    bool range_runtime = false;
    for (const RedIter& ri : its) range_runtime = range_runtime || refs_runtime_var(ri.range);
    const bool maybe_const = !is_sum && !is_prod && all_known && !plain_param && !range_runtime;
    bool empty = false;
    if (maybe_const) {
      std::vector<Id<Expr>> els;
      expand(els);
      if (result_.unsupported) return lit_int(0);
      bool lit = !els.empty();
      for (Id<Expr> x : els) lit = lit && is_minmax_literal(x);
      if (lit) return fold_min_max(els, is_min);
      empty = els.empty();
    }
    const Id<Expr> body = symbolic();
    if (result_.unsupported) return lit_int(0);
    const Type bty = result_.model.exprs[body].ty;
    if (maybe_const && empty) return reduction_default(name, bty);
    if (maybe_const && !refs_runtime_var(body)) {   // 变率是常量、却没能求值（体里有 ymc 折不动的常量式）
      set_unsupported("constant reduction not evaluable");
      return lit_int(0);
    }
    if (bty.kind != TypeKind::Real && bty.kind != TypeKind::Integer && bty.kind != TypeKind::Boolean &&
        bty.kind != TypeKind::Enumeration) {
      set_unsupported("reduction body type");
      return lit_int(0);
    }
    if ((is_sum || is_prod) && bty.kind != TypeKind::Real && bty.kind != TypeKind::Integer) {
      set_unsupported("non-numeric array operand");
      return lit_int(0);
    }
    Id<Expr> out = body;   // S4：保留（先写的迭代变量在最内层）
    for (const RedIter& ri : its) {
      if (ri.known && ri.elems.empty()) { set_unsupported("reduction over empty range"); return lit_int(0); }
      if (!ri.range.is_valid()) { set_unsupported("reduction range form"); return lit_int(0); }
      Expr r;
      r.kind = ExprKind::Reduction;
      r.call_name = {in_.intern(name), ri.name};
      r.a = out;
      r.b = ri.range;
      r.ty = bty;
      out = add(std::move(r));
    }
    return out;
  }

  // 模型语境的向量 / 矩阵内建（AB1 探针 + FuncBuiltinCross/Skew/OuterProduct/Symmetric/Matrix/Ndims）：
  // - cross / skew：__OpenModelica_EarlyInline 的 Modelica 体（NFModelicaBuiltin.mo:353-372）按实参展开再化简——符号实参照式
  //   保留（`c = {x[2] * y[3] - x[3] * y[2], …}`、`s = {{0.0, -x[3], x[2]}, …}`），字面量折叠（`{-3.0, 6.0, -3.0}`）。
  // - outerProduct（:342-351，体 `matrix(v1) * transpose(matrix(v2))`）：1.25.0 对符号实参留 `promote(…, 2)` 形，只做全字面量
  //   实参（折成积矩阵）。symmetric（external builtin）：非常量 omc 留调用，只做全字面量方阵（b[i,j] = i ≤ j ? a[i,j] : a[j,i]）。
  // - matrix：标量 → {{s}}、向量 → {{v1}, …}、矩阵原样、更高秩须余维为 1（否则 omc 报错 FuncBuiltinMatrixWrongType1；只做
  //   字面量）。ndims → 秩的整数字面量。
  // Integer 实参：cross / skew 的形参是 Real[3]，非字面量 Integer 数组的转型形态未核实 → SKIP。形状不对 = omc 类型错 → SKIP。
  Id<Expr> model_matrix_builtin(const Exp& e, std::string_view name, NodeId enclosing, const std::vector<Symbol>& prefix) {
    for (Symbol n : e.arg_names)
      if (n.is_valid()) { set_unsupported("named function argument"); return lit_int(0); }
    const std::size_t arity = name == "cross" || name == "outerProduct" ? 2 : 1;
    if (e.args.size() != arity) { set_unsupported("function arity"); return lit_int(0); }
    std::vector<Id<Expr>> args;
    for (Id<Exp> a : std::vector<Id<Exp>>(e.args)) {   // 拷贝：inst 期间不持 absyn 引用
      args.push_back(simplify_expr(inst_exp(a, enclosing, prefix)));
      if (result_.unsupported) return lit_int(0);
    }
    if (name == "ndims") return lit_int(static_cast<std::int64_t>(expr_shape(args[0]).size()));
    auto all_lit = [&](auto& self, Id<Expr> x) -> bool {   // 数组字面量、叶全是数值字面量
      const Expr& xe = result_.model.exprs[x];
      if (xe.kind == ExprKind::IntLit || xe.kind == ExprKind::RealLit) return true;
      if (xe.kind != ExprKind::Array || xe.args.empty()) return false;
      for (Id<Expr> c : xe.args) if (!self(self, c)) return false;
      return true;
    };
    for (Id<Expr> a : args) {
      const TypeKind k = expr_elem_kind(a);
      if (k != TypeKind::Real && (k != TypeKind::Integer || !all_lit(all_lit, a))) {
        set_unsupported("non-numeric function argument");
        return lit_int(0);
      }
    }
    auto dim = [](std::int64_t n) { Dimension d; d.kind = DimKind::Integer; d.size = n; return d; };
    auto arr = [&](std::vector<Id<Expr>> els, TypeKind k, std::vector<Dimension> dims) {
      Expr r;
      r.kind = ExprKind::Array;
      r.args = std::move(els);
      r.ty = Type::array_of(Type::of(k), std::move(dims));
      return add(std::move(r));
    };
    auto el = [&](Id<Expr> a, std::vector<std::int64_t> idx) { return simplify_expr(scalarize(a, idx)); };
    auto rel = [&](Id<Expr> a, std::int64_t i) { return to_real(el(a, {i})); };   // Real 形参的第 i 个元素
    auto bin = [&](Id<Expr> l, BinOp op, Id<Expr> r) {
      Expr b;
      b.kind = ExprKind::Binary;
      b.bin = op;
      b.a = l;
      b.b = r;
      b.ty = Type::real();
      return add(std::move(b));
    };
    auto neg = [&](Id<Expr> x) {
      Expr u;
      u.kind = ExprKind::Unary;
      u.un = UnOp::Minus;
      u.a = x;
      u.ty = Type::real();
      return add(std::move(u));
    };
    const std::vector<std::int64_t> s0 = expr_shape(args[0]);
    if (name == "cross" || name == "skew") {
      for (Id<Expr> a : args)
        if (expr_shape(a) != std::vector<std::int64_t>{3}) { set_unsupported("cross/skew argument shape"); return lit_int(0); }
      const Id<Expr> x1 = rel(args[0], 1), x2 = rel(args[0], 2), x3 = rel(args[0], 3);
      if (result_.unsupported) return lit_int(0);
      if (name == "skew") {
        auto row = [&](Id<Expr> a, Id<Expr> b, Id<Expr> c) {
          return arr({simplify_expr(a), simplify_expr(b), simplify_expr(c)}, TypeKind::Real, {dim(3)});
        };
        return arr({row(lit_real(0.0), neg(x3), x2), row(x3, lit_real(0.0), neg(x1)), row(neg(x2), x1, lit_real(0.0))},
                   TypeKind::Real, {dim(3), dim(3)});
      }
      const Id<Expr> y1 = rel(args[1], 1), y2 = rel(args[1], 2), y3 = rel(args[1], 3);
      if (result_.unsupported) return lit_int(0);
      auto term = [&](Id<Expr> a, Id<Expr> b, Id<Expr> c, Id<Expr> d) {
        return simplify_expr(bin(bin(a, BinOp::Mul, b), BinOp::Sub, bin(c, BinOp::Mul, d)));
      };
      return arr({term(x2, y3, x3, y2), term(x3, y1, x1, y3), term(x1, y2, x2, y1)}, TypeKind::Real, {dim(3)});
    }
    if (name == "outerProduct") {
      const std::vector<std::int64_t> s1 = expr_shape(args[1]);
      if (s0.size() != 1 || s1.size() != 1 || s0[0] < 1 || s1[0] < 1) { set_unsupported("outerProduct argument shape"); return lit_int(0); }
      if (!all_lit(all_lit, args[0]) || !all_lit(all_lit, args[1])) { set_unsupported("outerProduct of non-literal"); return lit_int(0); }
      std::vector<Id<Expr>> rows;
      for (std::int64_t i = 1; i <= s0[0]; ++i) {
        std::vector<Id<Expr>> row;
        for (std::int64_t j = 1; j <= s1[0]; ++j) row.push_back(simplify_expr(bin(rel(args[0], i), BinOp::Mul, rel(args[1], j))));
        rows.push_back(arr(std::move(row), TypeKind::Real, {dim(s1[0])}));
      }
      return arr(std::move(rows), TypeKind::Real, {dim(s0[0]), dim(s1[0])});
    }
    const TypeKind ek = expr_elem_kind(args[0]);
    if (name == "symmetric") {
      if (s0.size() != 2 || s0[0] != s0[1] || s0[0] < 1) { set_unsupported("symmetric argument shape"); return lit_int(0); }
      if (!all_lit(all_lit, args[0])) { set_unsupported("symmetric of non-literal"); return lit_int(0); }
      std::vector<Id<Expr>> rows;
      for (std::int64_t i = 1; i <= s0[0]; ++i) {
        std::vector<Id<Expr>> row;
        for (std::int64_t j = 1; j <= s0[0]; ++j) row.push_back(i <= j ? el(args[0], {i, j}) : el(args[0], {j, i}));
        rows.push_back(arr(std::move(row), ek, {dim(s0[0])}));
      }
      return arr(std::move(rows), ek, {dim(s0[0]), dim(s0[0])});
    }
    // matrix
    for (std::int64_t n : s0) if (n < 1) { set_unsupported("matrix argument shape"); return lit_int(0); }
    if (s0.empty()) return arr({arr({args[0]}, ek, {dim(1)})}, ek, {dim(1), dim(1)});
    if (s0.size() == 2) return args[0];
    if (s0.size() > 2 && !all_lit(all_lit, args[0])) { set_unsupported("matrix of non-literal"); return lit_int(0); }
    for (std::size_t k = 2; k < s0.size(); ++k)
      if (s0[k] != 1) { set_unsupported("matrix argument shape"); return lit_int(0); }
    const std::int64_t cols = s0.size() == 1 ? 1 : s0[1];
    std::vector<Id<Expr>> rows;
    for (std::int64_t i = 1; i <= s0[0]; ++i) {
      std::vector<Id<Expr>> row;
      for (std::int64_t j = 1; j <= cols; ++j) {
        std::vector<std::int64_t> idx{i};
        if (s0.size() > 1) idx.push_back(j);
        while (idx.size() < s0.size()) idx.push_back(1);
        row.push_back(el(args[0], idx));
      }
      rows.push_back(arr(std::move(row), ek, {dim(cols)}));
    }
    return arr(std::move(rows), ek, {dim(s0[0]), dim(cols)});
  }

  // 模型语境的 sum / product / min / max(数组)（实参不是直接写的归约式——那种走 reduction_chain / eval_reduction）。omc 的规则
  //（SimplifyExp.simplifyCall + simplifyBuiltinCall，NFSimplifyExp.mo:248-288；V4d / V4e 探针 56 行）：
  // - 实参化简后是字面量数组（不含数组构造子、各元素都化成字面量）→ Ceval 成一个字面量：`sum({1, 2, 3})` → 6、
  //   `sum({1, 2} .* {3, 4})` → 11、`max({F(1, 2), F(2, 2)})` → 4.0。
  // - 否则 sum / product 展开成元素链（simplifySumProduct :445-481）：各元素各自化简，**链本身不再化简**——`1.0 + 2.0 + time`、
  //   `x[1] + 0.0 + x[2]`、`(-x[1]) + (-x[2]) + (-x[3])`、`sum({i for i in 1:3})` → `1 + 2 + 3`（Real 绑定整体转型
  //   `/*Real*/(1 + 2 + 3)`）。链上的 Binary 记进 nofold_bins_（simplify_expr 原样留）。空数组 = 0 / 1（:464-465）。
  // - min / max：单层数组构造子实参改写成归约式（simplifyMinMax :332-355 → simplifyReducedArrayConstructor：
  //   `max({i for i in 1:3})` → `max(i for i in 1:3)`、`max(linspace(0, 1, 3))` → `max(/*Real*/(i - 1) / 2.0 for i in 1:3)`）；
  //   其余数组实参见 min_max_array。
  // 返回 invalid = 不归这里管（实参个数不对 / 命名实参 / 直接归约式 / 标量实参）。
  // min / max(数组)（typeMinMaxCall + simplifyMinMax，v1.25.0 NFBuiltinCall.mo typeMinMaxCall、NFSimplifyExp.mo:305-330；
  // RD 探针 r1 / r4）：元素类型 Real / Integer / Boolean / 枚举；空数组 → 类型极值（reduction_default）；恰一个元素的一维数组
  // → 那个元素（Type.isSingleElementArray）；叶全是字面量 → 求值；否则留调用，实参规整成整体保留的形态（kept_array_form：
  // `max(x)`、`max(x[1:2])`、`max({t, 1.0})`、`max({2.0 * x[1], …})`）。实参是数组构造子 / 向量化调用的照旧（上面的归约式
  // 改写）；实参是其余内建调用（`vector(x)`、`diagonal(x)` omc 留调用不展开）等未核实形态 → SKIP。返回 invalid = 交回调用方
  //（数组构造子）。
  Id<Expr> min_max_array(Id<Expr> a, Id<Exp> raw, const std::vector<std::int64_t>& sh, TypeKind ek, std::string_view name) {
    if (ek != TypeKind::Real && ek != TypeKind::Integer && ek != TypeKind::Boolean && ek != TypeKind::Enumeration) {
      set_unsupported("min/max array argument form");
      return lit_int(0);
    }
    const Type aty = result_.model.exprs[a].ty.element_type();
    const Type ety = aty.kind == ek ? aty : Type::of(ek);
    if (std::find(sh.begin(), sh.end(), 0) != sh.end()) return reduction_default(name, ety);
    if (contains_array_call(a)) return {};
    if (sh.size() == 1 && sh[0] == 1) return simplify_expr(scalarize(a, {1}));
    // 实参整个是调用：omc 在这里只化简不标量化——fill / zeros / ones / cat / transpose 被化简展开（`max(fill(t, 2))` →
    // `max({t, t})`），`vector(x)` / `diagonal(x)` 等留调用（RD 探针 r4）。其余调用的去留未核实：只在结果各叶全是字面量时
    // 收（求值结果一样），否则 SKIP。
    const ExprKind ak = result_.model.exprs[a].kind;
    bool verified_call = true;
    if (const Exp& rx = I_.absyn.exps[raw]; rx.kind == ExpKind::Call) {
      const Cref& rc = I_.absyn.crefs[rx.cref];
      const std::string_view rn = rc.parts.size() == 1 ? in_.str(rc.parts[0].name) : std::string_view{};
      verified_call = ak == ExprKind::Array &&
                      (rn == "fill" || rn == "zeros" || rn == "ones" || rn == "cat" || rn == "transpose" || rn == "size");
    }
    Id<Expr> arg = ak == ExprKind::LogicalNot && result_.model.exprs[result_.model.exprs[a].a].kind == ExprKind::Cref
                       ? a : kept_array_form(a, sh, ek);   // `min(not bb)` 原样
    if (result_.unsupported) return lit_int(0);
    if (!arg.is_valid()) { set_unsupported("min/max array argument form"); return lit_int(0); }
    // 字面量数组（各叶都是字面量）→ 求值。
    std::vector<Id<Expr>> leaves;
    bool lit = true;
    auto walk = [&](auto& self, Id<Expr> x) -> void {
      const Expr& xe = result_.model.exprs[x];
      if (xe.kind == ExprKind::Array) { for (Id<Expr> c : std::vector<Id<Expr>>(xe.args)) self(self, c); return; }
      lit = lit && is_minmax_literal(x) && expr_shape(x).empty();
      leaves.push_back(x);
    };
    walk(walk, arg);
    if (lit && !leaves.empty()) return fold_min_max(leaves, name == "min");
    if (!verified_call) { set_unsupported("min/max array argument form"); return lit_int(0); }
    Expr r;
    r.kind = ExprKind::Call;
    r.call_name = {in_.intern(name)};
    r.args = {arg};
    r.ty = ety;
    return add(std::move(r));
  }
  Id<Expr> model_reduce_builtin(const Exp& e, std::string_view name, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const bool is_sum = name == "sum", is_prod = name == "product";
    if (e.args.size() != 1 || I_.absyn.exps[e.args[0]].kind == ExpKind::Reduction) return {};
    for (Symbol n : e.arg_names) if (n.is_valid()) return {};
    const Id<Exp> raw = e.args[0];
    array_call_ok_ = I_.absyn.exps[raw].kind == ExpKind::Call;   // 实参整个是调用：常量实参的数组函数求值成字面量
    const Id<Expr> a = simplify_expr(inst_exp(raw, enclosing, prefix));
    array_call_ok_ = false;
    if (result_.unsupported) return lit_int(0);
    const std::vector<std::int64_t> sh = expr_shape(a);
    if (sh.empty()) return {};
    for (std::int64_t n : sh) if (n < 0) { set_unsupported("array builtin of unknown / empty shape"); return lit_int(0); }
    const TypeKind ek = expr_elem_kind(a);
    const bool real = ek == TypeKind::Real;
    if (!is_sum && !is_prod) {
      if (Id<Expr> r = min_max_array(a, raw, sh, ek, name); r.is_valid() || result_.unsupported) return r;
    } else if (!real && ek != TypeKind::Integer) {
      set_unsupported("non-numeric array operand");
      return lit_int(0);
    }
    if (std::find(sh.begin(), sh.end(), 0) != sh.end()) {
      const std::int64_t unit = is_sum ? 0 : 1;
      return real ? lit_real(static_cast<double>(unit)) : lit_int(unit);
    }
    const bool ctor = contains_array_call(a);
    if (!is_sum && !is_prod && ctor) {
      const Expr ae = result_.model.exprs[a];   // 拷贝
      if (ae.kind != ExprKind::Reduction || ae.call_name.size() != 2 || !expr_shape(ae.a).empty()) {
        set_unsupported("min/max of array constructor");
        return lit_int(0);
      }
      Expr r = ae;
      r.call_name[0] = in_.intern(name);
      r.ty = result_.model.exprs[ae.a].ty;
      return add(std::move(r));
    }
    std::vector<Id<Expr>> els;   // 行主序的各元素（各自化简）
    {
      const RedScalarize rs(red_scalarize_ok_);
      std::vector<std::int64_t> ix(sh.size(), 1);
      for (bool more = true; more;) {
        Id<Expr> x = simplify_expr(scalarize(a, ix));
        if (result_.unsupported) return lit_int(0);
        els.push_back(real ? to_real(x) : x);
        std::size_t k = sh.size();
        while (k > 0) { --k; if (++ix[k] <= sh[k]) break; ix[k] = 1; if (k == 0) more = false; }
      }
    }
    bool all_lit = true;
    for (Id<Expr> x : els) all_lit = all_lit && is_num_lit(result_.model.exprs[x]);
    Id<Expr> acc = els[0];
    std::vector<Id<Expr>> chain;
    for (std::size_t i = 1; i < els.size(); ++i) {
      Expr b;
      b.kind = ExprKind::Binary;
      b.bin = is_sum ? BinOp::Add : BinOp::Mul;
      b.a = acc;
      b.b = els[i];
      b.ty = real ? Type::real() : Type::integer();
      acc = add(std::move(b));
      chain.push_back(acc);
    }
    if (!ctor && all_lit) return simplify_expr(acc);
    for (Id<Expr> c : chain) nofold_bins_.insert(c.raw);
    return acc;
  }
  // 表达式树里（任意深度）有返回数组的调用 / 数组构造子（Expression.hasArrayCall = contains，NFExpression.mo:5440-5471）。
  bool contains_array_call(Id<Expr> id) {
    if (!id.is_valid()) return false;
    const Expr& x = result_.model.exprs[id];
    if ((x.kind == ExprKind::Call || x.kind == ExprKind::TupleElement || x.kind == ExprKind::Reduction) && x.ty.is_array() &&
        !non_vectorizeable_call(x))
      return true;
    for (Id<Expr> a : x.args) if (contains_array_call(a)) return true;
    return contains_array_call(x.a) || contains_array_call(x.b) || contains_array_call(x.c);
  }

  // 模型语境的数组内建：维已知时 omc 展开成数组字面量 / 运算链（ExpandExp + SimplifyExp；B1 探针：`fill(time, 3)` →
  // `{time, time, time}`、`identity(2)` → `{{1, 0}, {0, 1}}`、`transpose([1, 2; 3, 4])` → `{{1, 3}, {2, 4}}`、`sum(a)` →
  // `a[1] + a[2] + a[3]`）。fill / zeros / ones / identity / diagonal / transpose / vector → 嵌套数组字面量；sum / product →
  // 元素的 + / * 链（行主序、左结合）。维实参按结构参数求值（`ones(n)` 把 n 打 final）。不在此列 → invalid（调用方照常）。
  Id<Expr> model_array_builtin(const Exp& e, std::string_view name, NodeId enclosing, const std::vector<Symbol>& prefix) {
    if (name == "cross" || name == "skew" || name == "outerProduct" || name == "symmetric" || name == "matrix" ||
        name == "ndims")
      return model_matrix_builtin(e, name, enclosing, prefix);
    if (name == "sum" || name == "product" || name == "min" || name == "max")
      if (Id<Expr> r = model_reduce_builtin(e, name, enclosing, prefix); r.is_valid() || result_.unsupported) return r;
    const bool fill = name == "fill", zo = name == "zeros" || name == "ones";
    if (!fill && !zo && name != "identity" && name != "diagonal" && name != "transpose" && name != "vector" &&
        name != "sum" && name != "product" && name != "cat")
      return {};
    for (Symbol n : e.arg_names)
      if (n.is_valid()) { set_unsupported("named function argument"); return lit_int(0); }
    const std::vector<Id<Exp>> args = e.args;   // 拷贝：inst 期间不持 absyn 引用
    const std::size_t first_dim = fill ? 1 : 0;
    const bool arity_ok = name == "cat" ? args.size() >= 2 : (fill || zo) ? args.size() > first_dim : args.size() == 1;
    if (!arity_ok) { set_unsupported("function arity"); return lit_int(0); }
    const ScodeScope scope = scode_scope_of(enclosing);
    // 维 / 元素索引按行主序枚举，elem(idx) 给每个元素（已化简）；嵌套成数组字面量。kind0 = 元素类型（有维为 0 时一个元素
    // 都不产，类型无从取；缺省按首元素定）。
    auto build = [&](const std::vector<std::int64_t>& dims, auto&& elem, TypeKind kind0 = TypeKind::Unknown) -> Id<Expr> {
      std::vector<std::int64_t> idx;
      TypeKind kind = kind0;
      auto rec = [&](auto& self, std::size_t k) -> Id<Expr> {
        if (k == dims.size()) {
          const Id<Expr> x = simplify_expr(elem(idx));
          if (kind == TypeKind::Unknown) kind = result_.model.exprs[x].ty.kind;
          return x;
        }
        Expr arr;
        arr.kind = ExprKind::Array;
        std::vector<Dimension> rest;
        for (std::size_t j = k; j < dims.size(); ++j) { Dimension d; d.kind = DimKind::Integer; d.size = dims[j]; rest.push_back(d); }
        for (std::int64_t i = 1; i <= dims[k]; ++i) {
          idx.push_back(i);
          arr.args.push_back(self(self, k + 1));
          idx.pop_back();
        }
        arr.ty = Type::array_of(Type::of(kind == TypeKind::Unknown ? TypeKind::Real : kind), std::move(rest));
        return add(std::move(arr));
      };
      return rec(rec, 0);
    };
    if (fill || zo) {
      std::vector<std::int64_t> dims;
      for (std::size_t i = first_dim; i < args.size(); ++i) {
        // 维实参里有迭代变量时 1.25.0 在定型期不求值、也不标结构参数（循环还没展开）：`x[:,1:1] = fill(0.0, 3, n + 1 - 1)`、
        // n 不 final（AP ff 探针 FF2 / FF3 / FF9、FuncBuiltinFill3）。保留 fill 的数组方程未做 → SKIP；引用的参数在此之前
        // 已是结构参数的照常求值。
        if (iter_blocks_structural(args[i], enclosing, prefix)) { set_unsupported("fill dimension depends on iterator"); return lit_int(0); }
        const std::optional<CVal> v = eval_scode(args[i], scope, 0, /*allow_param=*/true, nullptr);
        if (!v || v->k != CVal::K::Int) { set_unsupported("non-constant fill dimension"); return lit_int(0); }
        if (v->i < 0) { set_unsupported("negative fill dimension"); return lit_int(0); }
        dims.push_back(v->i);   // 0 → 空数组字面量（类型带全维，EmptyArray1 `ones(size(x, 1))`、Array13 `fill(0.0, 0, 2)`）
      }
      const Id<Expr> val = fill ? inst_exp(args[0], enclosing, prefix) : lit_int(name == "ones" ? 1 : 0);
      if (result_.unsupported) return lit_int(0);
      if (!expr_shape(val).empty()) { set_unsupported("fill of array value"); return lit_int(0); }
      return build(dims, [&](const std::vector<std::int64_t>&) { return val; }, result_.model.exprs[val].ty.kind);
    }
    if (name == "cat") {
      // cat(k, A1, A2, …)（MLS §10.4.2）：维已知时展开成数组字面量（CT1 探针：`cat(1, x, {7, 8})` → `{x[1], x[2], x[3], 7.0, 8.0}`、
      // `cat(2, {{1}, {2}}, {{3, 4}, {5, 6}})` → `{{1.0, 3.0, 4.0}, {2.0, 5.0, 6.0}}`）。k 须常量整数 1..秩；各实参同秩、除第
      // k 维外同形（否则 omc 报错 → SKIP）；有 Real 实参则元素提 Real。
      const std::optional<CVal> kv = eval_scode(args[0], scope, 0, /*allow_param=*/true, nullptr);
      if (!kv || kv->k != CVal::K::Int) { set_unsupported("non-constant cat dimension"); return lit_int(0); }
      std::vector<Id<Expr>> parts;
      std::vector<std::vector<std::int64_t>> shapes;
      bool real = false;
      for (std::size_t i = 1; i < args.size(); ++i) {
        const Id<Expr> p = inst_exp(args[i], enclosing, prefix);
        if (result_.unsupported) return lit_int(0);
        const std::vector<std::int64_t> sh = expr_shape(p);
        if (sh.empty()) { set_unsupported("cat of scalar"); return lit_int(0); }
        for (std::int64_t n : sh)
          if (n < 1) { set_unsupported("array builtin of unknown / empty shape"); return lit_int(0); }
        real = real || expr_elem_kind(p) == TypeKind::Real;
        parts.push_back(p);
        shapes.push_back(sh);
      }
      const std::size_t rank = shapes[0].size();
      if (kv->i < 1 || static_cast<std::size_t>(kv->i) > rank) { set_unsupported("cat dimension out of range"); return lit_int(0); }
      const std::size_t kd = static_cast<std::size_t>(kv->i) - 1;
      std::vector<std::int64_t> dims = shapes[0];
      dims[kd] = 0;
      for (const std::vector<std::int64_t>& sh : shapes) {
        if (sh.size() != rank) { set_unsupported("cat shape mismatch"); return lit_int(0); }
        for (std::size_t j = 0; j < rank; ++j)
          if (j != kd && sh[j] != shapes[0][j]) { set_unsupported("cat shape mismatch"); return lit_int(0); }
        dims[kd] += sh[kd];
      }
      return build(dims, [&](const std::vector<std::int64_t>& ix) {
        std::vector<std::int64_t> src = ix;
        std::size_t pi = 0;
        while (src[kd] > shapes[pi][kd]) { src[kd] -= shapes[pi][kd]; ++pi; }
        const Id<Expr> x = scalarize(parts[pi], src);
        return real ? to_real(x) : x;
      });
    }
    if (name == "identity") {
      const std::optional<CVal> v = eval_scode(args[0], scope, 0, /*allow_param=*/true, nullptr);
      if (!v || v->k != CVal::K::Int || v->i < 1) { set_unsupported("non-constant identity size"); return lit_int(0); }
      return build({v->i, v->i}, [&](const std::vector<std::int64_t>& ix) { return lit_int(ix[0] == ix[1] ? 1 : 0); });
    }
    // sum / product 的数组实参已在 model_reduce_builtin 处理；到这里只剩标量实参（omc 报类型错）。
    if (name == "sum" || name == "product") { set_unsupported("array builtin of scalar"); return lit_int(0); }
    const Id<Expr> a = inst_exp(args[0], enclosing, prefix);
    if (result_.unsupported) return lit_int(0);
    const std::vector<std::int64_t> sh = expr_shape(a);
    for (std::int64_t n : sh)
      if (n < 1) { set_unsupported("array builtin of unknown / empty shape"); return lit_int(0); }
    if (name == "diagonal") {
      if (sh.size() != 1) { set_unsupported("diagonal of non-vector"); return lit_int(0); }
      const bool real = expr_elem_kind(a) == TypeKind::Real;
      return build({sh[0], sh[0]}, [&](const std::vector<std::int64_t>& ix) {
        return ix[0] == ix[1] ? scalarize(a, {ix[0]}) : (real ? lit_real(0.0) : lit_int(0));
      });
    }
    if (name == "transpose") {
      if (sh.size() < 2) { set_unsupported("transpose of non-matrix"); return lit_int(0); }
      std::vector<std::int64_t> td = sh;
      std::swap(td[0], td[1]);
      return build(td, [&](const std::vector<std::int64_t>& ix) {
        std::vector<std::int64_t> src = ix;
        std::swap(src[0], src[1]);
        return scalarize(a, src);
      });
    }
    // vector：行主序逐元素。
    std::vector<std::vector<std::int64_t>> all;
    {
      std::vector<std::int64_t> ix(sh.size(), 1);
      if (sh.empty()) { set_unsupported("array builtin of scalar"); return lit_int(0); }
      for (bool more = true; more;) {
        all.push_back(ix);
        std::size_t k = sh.size();
        while (k > 0) { --k; if (++ix[k] <= sh[k]) break; ix[k] = 1; if (k == 0) more = false; }
      }
    }
    // vector：至多一维 > 1（MLS §10.3.3），否则 omc 报错。
    int big = 0;
    for (std::int64_t n : sh) big += n > 1 ? 1 : 0;
    if (big > 1) { set_unsupported("vector of matrix"); return lit_int(0); }
    return build({static_cast<std::int64_t>(all.size())},
                 [&](const std::vector<std::int64_t>& ix) { return scalarize(a, all[static_cast<std::size_t>(ix[0]) - 1]); });
  }

  // ---- 同步语言元素（MLS §16）：只做定型与打印，钟的推断 / 分区是后端的事。形态 = 1.25.0 实测（SY 探针 s1–s4）；声明见
  // v1.25.0 NFModelicaBuiltin.mo:530-580 / :872-963 / :1035-1099，定型见 NFBuiltinCall.mo:1875-2017 / :2158-2230。----
  bool is_clock_ctor(const Expr& x) const {
    return x.kind == ExprKind::Call && x.ty.kind == TypeKind::Clock && x.call_name.size() == 1 &&
           in_.str(x.call_name[0]) == "Clock";
  }
  // 命名实参按形参名排成位置实参（缺的留 invalid）；重复 / 未知名 / 位置实参过多 → false。
  bool order_args(const Exp& e, const std::vector<std::string_view>& params, std::vector<Id<Exp>>& out) {
    out.assign(params.size(), Id<Exp>{});
    std::size_t pos = 0;
    for (std::size_t i = 0; i < e.args.size(); ++i) {
      std::size_t k = params.size();
      if (i < e.arg_names.size() && e.arg_names[i].is_valid()) {
        for (std::size_t j = 0; j < params.size(); ++j)
          if (in_.str(e.arg_names[i]) == params[j]) k = j;
      } else {
        k = pos++;
      }
      if (k >= params.size() || out[k].is_valid()) return false;
      out[k] = e.args[i];
    }
    return true;
  }
  // Clock 构造（typeClockCall :1875-1922）：`Clock()`；一个实参按类型选重载——Real → `Clock(e)`、Integer → 补 resolution
  // `Clock(e, 1)`、Boolean → 补 startInterval `Clock(e, 0.0)`；两个实参时第二个求值成字面量（Ceval.evalExp，不标结构）：
  // Integer 首参配 ≥ 1 的 Integer、Boolean 首参配 Real、钟首参配 String（solver 钟）。CLKCONST 的首参 omc 既不化简也不代入
  // 结构参数（`Clock(1.0 + 2.0)`、`Clock(n, 1)` 原样）：只收"化简前后同形"的首参，代入见 clone_subst。
  Id<Expr> clock_ctor(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    std::vector<Id<Exp>> args;
    {
      bool named = false;
      for (Symbol n : e.arg_names) named = named || n.is_valid();
      if (!named) {
        args = e.args;
      } else {
        // 四个带参重载的形参名：(intervalCounter, resolution) / (interval) / (condition, startInterval) / (c, solverMethod)；
        // 名字只定位置，重载仍按首参类型选（名字与重载不配的写法 omc 报错，这里落到下面的类型判定里 SKIP）。
        args.assign(2, Id<Exp>{});
        std::size_t pos = 0;
        bool ok = true;
        for (std::size_t i = 0; i < e.args.size() && ok; ++i) {
          const bool nm = i < e.arg_names.size() && e.arg_names[i].is_valid();
          const std::string_view an = nm ? in_.str(e.arg_names[i]) : std::string_view{};
          const std::size_t k = !nm ? pos++
                              : (an == "intervalCounter" || an == "interval" || an == "condition" || an == "c") ? 0
                              : (an == "resolution" || an == "startInterval" || an == "solverMethod") ? 1 : 2;
          if (k > 1 || args[k].is_valid()) ok = false;
          else args[k] = e.args[i];
        }
        if (!ok || !args[0].is_valid()) { set_unsupported("named function argument"); return lit_int(0); }
        if (!args[1].is_valid()) args.pop_back();
      }
    }
    Expr r;
    r.kind = ExprKind::Call;
    r.call_name = {in_.intern("Clock")};
    r.ty = Type::of(TypeKind::Clock);
    if (args.empty()) return add(std::move(r));
    if (args.size() > 2) { set_unsupported("function arity"); return lit_int(0); }
    const Id<Expr> a = inst_exp(args[0], enclosing, prefix);
    if (result_.unsupported) return lit_int(0);
    if (!expr_shape(a).empty()) { set_unsupported("Clock constructor argument"); return lit_int(0); }
    const TypeKind ak = result_.model.exprs[a].ty.kind;
    if (!is_clock_ctor(result_.model.exprs[a]) && !expr_equal(a, simplify_expr(a))) {
      set_unsupported("Clock argument not in simplified form");
      return lit_int(0);
    }
    r.args.push_back(a);
    if (args.size() == 1) {
      if (ak == TypeKind::Integer) r.args.push_back(lit_int(1));
      else if (ak == TypeKind::Boolean) r.args.push_back(lit_real(0.0));
      else if (ak != TypeKind::Real) { set_unsupported("Clock constructor argument"); return lit_int(0); }
      return add(std::move(r));
    }
    std::optional<CVal> bv;
    {
      const StructSnap snap = struct_snap();   // 只求值、不标结构（`Clock(n, n)` → `Clock(n, 2)`，n 不 final）
      bv = eval_scode(args[1], scode_scope_of(enclosing), 0, /*allow_param=*/true, nullptr);
      struct_restore(snap);
    }
    if (!bv) { set_unsupported("Clock constructor argument"); return lit_int(0); }
    if (ak == TypeKind::Integer && bv->k == CVal::K::Int && bv->i >= 1) r.args.push_back(lit_int(bv->i));
    else if (ak == TypeKind::Boolean && (bv->k == CVal::K::Int || bv->k == CVal::K::Real)) r.args.push_back(lit_real(bv->as_r()));
    else if (ak == TypeKind::Clock && bv->k == CVal::K::Str) r.args.push_back(literal_of(*bv));
    else { set_unsupported("Clock constructor argument"); return lit_int(0); }
    return add(std::move(r));
  }
  // 其余同步算子：sample(u[, c])（clocked 重载，结果类型同 u；缺 c → `Clock()`）、hold / noClock(u)（类型同 u）、previous(u)
  //（u 须是组件引用；数组按元素向量化 `array(previous(x[$i1]) for $i1 in 1:n)`）、backSample / shiftSample(u, 计数[,
  // resolution = 1])、subSample / superSample(u[, factor = 0])（计数类实参须是参数变率的 Integer，按结构参数求值成字面量，
  // Structural.markExp :2172-2173 / :2191-2192 / :2210 / :2228；u 可以是钟）、interval([u]) → Real、firstTick([u]) → Boolean。
  // 数组实参只收整体引用 / 字面量 / 数组调用（其余数组式 omc 先展开成字面量 `hold({2.0 * z[1], 2.0 * z[2]})`，未做 → SKIP）。
  // 不是这些算子、或是经典的 sample(start, interval) → invalid（调用方照常）。
  // 状态机算子（MLS §17.1）：1.25.0 的新前端原样留调用（状态机不在前端展开），状态实参是模型 / 块实例的引用（带实例
  // 前缀打印），transition 的缺省实参补齐成 7 个位置实参（SM 探针 m1：`transition(s1, s2, c)` →
  // `transition(s1, s2, c, true, true, false, 1)`、`initialState(w.w1)`、`activeState(s1)`、`ticksInState()` /
  // `timeInState()`）。状态实参须是标量的类实例（变量 / 字段 / 找不到 → omc 报错；实例数组整体 omc 打成
  // `array(initialState(sa[$i1]) for $i1 in 1:2)`，不复刻）。
  bool sm_stmt_ = false;   // 正在定型的是一条无返回值调用语句 / 方程的最外层调用（noret_call 置，第一个状态机算子取走）
  Id<Expr> state_machine_call(const Exp& e, std::string_view name, NodeId enclosing, const std::vector<Symbol>& prefix) {
    const bool stmt = sm_stmt_;
    sm_stmt_ = false;
    // initialState / transition 没有返回值，只能单独成句；当表达式用是 omc 的类型错（SM 探针 m2 `Real y = initialState(s1)`）。
    if (!stmt && (name == "initialState" || name == "transition")) {
      set_unsupported("state machine operator used as expression");
      return lit_int(0);
    }
    const std::vector<Id<Exp>> raw = e.args;            // 拷贝（下面定型会往 arena 加）
    const std::vector<Symbol> names = e.arg_names;
    auto named = [&](std::size_t i) { return i < names.size() && names[i].is_valid(); };
    auto mk = [&](std::vector<Id<Expr>> args, Type ty) {
      Expr r;
      r.kind = ExprKind::Call;
      r.call_name = {in_.intern(name)};
      r.args = std::move(args);
      r.ty = std::move(ty);
      return add(std::move(r));
    };
    auto state = [&](Id<Exp> a) -> Id<Expr> {
      if (I_.absyn.exps[a].kind != ExpKind::CrefExp) { set_unsupported("state machine operand"); return lit_int(0); }
      const Id<Expr> x = state_ref(I_.absyn.crefs[I_.absyn.exps[a].cref], enclosing, prefix);
      if (!x.is_valid() && !result_.unsupported) set_unsupported("state machine operand");
      return x.is_valid() ? x : lit_int(0);
    };
    if (name == "ticksInState" || name == "timeInState") {
      if (!raw.empty()) { set_unsupported("function arity"); return lit_int(0); }
      return mk({}, name == "ticksInState" ? Type::integer() : Type::real());
    }
    if (name == "activeState" || name == "initialState") {
      // 形参名都是 state（NFModelicaBuiltin：`initialState(state = s1)` omc 接受，m2）。
      if (raw.size() != 1 || (named(0) && in_.str(names[0]) != "state")) { set_unsupported("function arity"); return lit_int(0); }
      const Id<Expr> st = state(raw[0]);
      if (result_.unsupported) return lit_int(0);
      return mk({st}, name == "activeState" ? Type::boolean() : Type{});
    }
    // transition(from, to, condition, immediate = true, reset = true, synchronize = false, priority = 1)
    static constexpr std::string_view kSlots[] = {"from", "to", "condition", "immediate", "reset", "synchronize", "priority"};
    std::vector<Id<Exp>> slot(7);
    std::size_t pos = 0;
    for (std::size_t i = 0; i < raw.size(); ++i) {
      std::size_t k = 7;
      if (named(i)) {
        for (std::size_t j = 0; j < 7; ++j)
          if (in_.str(names[i]) == kSlots[j]) k = j;
      } else {
        k = pos++;
      }
      if (k >= 7 || slot[k].is_valid()) { set_unsupported("function arity"); return lit_int(0); }
      slot[k] = raw[i];
    }
    if (!slot[0].is_valid() || !slot[1].is_valid() || !slot[2].is_valid()) { set_unsupported("function arity"); return lit_int(0); }
    std::vector<Id<Expr>> args{state(slot[0]), state(slot[1])};
    if (result_.unsupported) return lit_int(0);
    const Id<Expr> defaults[] = {Id<Expr>{}, Id<Expr>{}, Id<Expr>{}, lit_bool(true), lit_bool(true), lit_bool(false), lit_int(1)};
    for (std::size_t k = 2; k < 7; ++k) {
      Id<Expr> x = defaults[k];
      if (slot[k].is_valid()) {
        x = simplify_expr(inst_exp(slot[k], enclosing, prefix));
        if (result_.unsupported) return lit_int(0);
        const Type xt = result_.model.exprs[x].ty;
        if (xt.is_array() || xt.kind != (k == 6 ? TypeKind::Integer : TypeKind::Boolean)) {
          set_unsupported("state machine operand");   // 类型不符：omc 报错
          return lit_int(0);
        }
      }
      args.push_back(x);
    }
    return mk(std::move(args), Type{});
  }
  // 状态实参：书写的 cref 沿实例树逐段找组件（首段是 enclosing 的本地组件，各段可带字面量下标取实例数组的元素），末段须是
  // 类实例（形参类型是类型参数 `__Block`，NFModelicaBuiltin.mo:582-611 v1.25.0：模型 / 块 / 记录 / 连接器的实例 omc 都收，
  // 内建类型的变量报错）且没有剩余的维。找不到 / 不是 → invalid。
  Id<Expr> state_ref(const Cref& c, NodeId enclosing, const std::vector<Symbol>& prefix) {
    if (c.leading_dot || c.parts.empty()) return {};
    Expr r;
    r.kind = ExprKind::Cref;
    r.ty = Type::of(TypeKind::Complex);
    for (std::size_t i = 0; i < prefix.size(); ++i) {
      std::vector<Id<Expr>> ps;
      if (i < cur_psubs_.size())
        for (std::int64_t v : cur_psubs_[i]) ps.push_back(lit_int(v));
      r.cref.parts.push_back({prefix[i], std::move(ps)});
    }
    NodeId scope = enclosing;
    const ScodeScope sscope = scode_scope_of(enclosing);
    for (std::size_t i = 0; i < c.parts.size(); ++i) {
      const NodeId n = find_local(scope, c.parts[i].name);
      if (!n.is_valid() || I_.nodes[n].is_empty() || !I_.nodes[n].comp.is_valid()) return {};
      const CompId cid = I_.nodes[n].comp;
      if (I_.comps[cid].ty.kind != TypeKind::Complex) return {};
      const SElement& cel = I_.scode.elems[I_.comps[cid].def];
      // 实例数组：下标须把各维取尽、且是整数常量。
      std::vector<std::int64_t> dims;
      for (Id<Exp> d : cel.attr.dims) if (!eval_dim(d, dims, sscope)) return {};
      for (Id<Exp> d : cel.type.dims) if (!eval_dim(d, dims, sscope)) return {};
      if (c.parts[i].subscripts.size() != dims.size()) return {};
      std::vector<Id<Expr>> subs;
      for (std::size_t k = 0; k < dims.size(); ++k) {
        const std::optional<std::int64_t> v = subscript_int(c.parts[i].subscripts[k], &sscope);
        if (!v || *v < 1 || *v > dims[k]) return {};
        subs.push_back(lit_int(*v));
      }
      r.cref.parts.push_back({c.parts[i].name, std::move(subs)});
      scope = I_.comps[cid].class_inst;
      if (!scope.is_valid() || I_.nodes[scope].is_empty() || I_.nodes[scope].kind != NodeKind::Class) return {};
      if (i + 1 == c.parts.size()) {
        // 记录实例也收，但 omc 会连带发射它的构造函数（m2 `initialState(r)`），未做 → invalid（调用方 SKIP）。
        const Id<SElement> def = I_.nodes[scope].def;
        if (def.is_valid() && (I_.scode.elems[def].restriction == SRestriction::Record ||
                               I_.scode.elems[def].restriction == SRestriction::OperatorRecord))
          return {};
        r.cref.resolved_node = n.raw;
      }
    }
    return add(std::move(r));
  }
  Id<Expr> sync_call(const Exp& e, std::string_view name, NodeId enclosing, const std::vector<Symbol>& prefix, bool top) {
    if (name == "Clock") return clock_ctor(e, enclosing, prefix);
    struct Op {
      std::string_view name;
      std::vector<std::string_view> params;
      std::size_t required;   // 必给的前几个形参
    };
    static const Op kOps[] = {
        {"hold", {"u"}, 1},          {"noClock", {"u"}, 1},   {"previous", {"u"}, 1},
        {"backSample", {"u", "backCounter", "resolution"}, 2}, {"shiftSample", {"u", "shiftCounter", "resolution"}, 2},
        {"subSample", {"u", "factor"}, 1},                     {"superSample", {"u", "factor"}, 1},
        {"interval", {"u"}, 0},      {"firstTick", {"u"}, 0}, {"sample", {"u", "c"}, 1},
    };
    const Op* op = nullptr;
    for (const Op& o : kOps) if (o.name == name) op = &o;
    if (!op) return {};
    if (name == "sample") {
      // 经典形 sample(start, interval)：第二个实参不是钟（typeSampleCall :1953-1985）。钟只可能是这些调用（Clock 类型的组件
      // inst 还不认）。
      Id<Exp> second;
      for (std::size_t i = 0; i < e.args.size(); ++i) {
        const bool nm = i < e.arg_names.size() && e.arg_names[i].is_valid();
        if (nm && in_.str(e.arg_names[i]) == "interval") return {};
        // 首参只能按位置给（typeSampleCall 的各 case 都以位置实参开头；`sample(u = x)` 是 omc 报错，SY 探针 s2）。
        if (nm && in_.str(e.arg_names[i]) == "u") { set_unsupported("named function argument"); return lit_int(0); }
        if ((nm && in_.str(e.arg_names[i]) == "c") || (!nm && i == 1)) second = e.args[i];
      }
      if (second.is_valid()) {
        const Exp& sx = I_.absyn.exps[second];
        bool clock = false;
        if (sx.kind == ExpKind::Call) {
          const Cref& sc = I_.absyn.crefs[sx.cref];
          const std::string_view sn = sc.parts.size() == 1 ? in_.str(sc.parts[0].name) : std::string_view{};
          clock = sn == "Clock" || sn == "subSample" || sn == "superSample" || sn == "shiftSample" || sn == "backSample";
        }
        if (!clock) return {};
      }
    }
    std::vector<Id<Exp>> args;
    if (!order_args(e, op->params, args)) { set_unsupported("function arity"); return lit_int(0); }
    for (std::size_t i = 0; i < op->required; ++i)
      if (!args[i].is_valid()) { set_unsupported("function arity"); return lit_int(0); }
    auto mk = [&](std::vector<Id<Expr>> cargs, Type ty) {
      // 返回数组的调用只在整个绑定 / 等式一侧收（等式整体保留）；嵌在运算里时 omc 连运算一起整体打（`hold(z) * 2.0`），未做。
      if (ty.is_array() && !top) { set_unsupported("array-valued clocked operator inside expression"); return lit_int(0); }
      Expr r;
      r.kind = ExprKind::Call;
      r.call_name = {in_.intern(name)};
      r.args = std::move(cargs);
      r.ty = std::move(ty);
      return add(std::move(r));
    };
    // 首参 u（可缺）：定型 + 化简；结果类型 = u 的类型（数组按看穿字面量后的形状）。
    Id<Expr> u;
    Type uty;
    if (args[0].is_valid()) {
      if (name == "previous" && I_.absyn.exps[args[0]].kind != ExpKind::CrefExp) {   // omc：实参须是组件引用
        set_unsupported("previous of non-component expression");
        return lit_int(0);
      }
      u = simplify_expr(inst_exp(args[0], enclosing, prefix));
      if (result_.unsupported) return lit_int(0);
      const Expr& ux = result_.model.exprs[u];
      uty = ux.ty;
      const std::vector<std::int64_t> sh = expr_shape(u);
      if (!sh.empty()) {
        if (ux.kind != ExprKind::Cref && ux.kind != ExprKind::Array && ux.kind != ExprKind::Call && ux.kind != ExprKind::Reduction) {
          set_unsupported("array expression argument of clocked operator");
          return lit_int(0);
        }
        std::vector<Dimension> ds;
        for (std::int64_t n : sh) {
          if (n < 1) { set_unsupported("array expression argument of clocked operator"); return lit_int(0); }
          Dimension d;
          d.kind = DimKind::Integer;
          d.size = n;
          ds.push_back(d);
        }
        uty = Type::array_of(Type::of(expr_elem_kind(u)), std::move(ds));
      }
    }
    if (name == "interval") return mk(u.is_valid() ? std::vector<Id<Expr>>{u} : std::vector<Id<Expr>>{}, Type::real());
    if (name == "firstTick") return mk(u.is_valid() ? std::vector<Id<Expr>>{u} : std::vector<Id<Expr>>{}, Type::boolean());
    if (name == "hold" || name == "noClock") return mk({u}, uty);
    if (name == "previous") {
      if (result_.model.exprs[u].kind != ExprKind::Cref) { set_unsupported("previous of non-component expression"); return lit_int(0); }
      const std::vector<std::int64_t> vd = expr_shape(u);
      if (vd.empty()) return mk({u}, uty);
      std::vector<Id<Expr>> iters;
      for (std::size_t k = 1; k <= vd.size(); ++k) iters.push_back(vect_iter(k));
      const Id<Expr> el = vectorize_arg(u, iters, false);
      if (!el.is_valid()) { set_unsupported("vectorized call argument form"); return lit_int(0); }
      return wrap_vectorized(mk({el}, result_.model.exprs[el].ty), vd);
    }
    if (name == "sample") {
      Id<Expr> c;
      if (args[1].is_valid()) {
        c = inst_exp(args[1], enclosing, prefix);
        if (result_.unsupported) return lit_int(0);
        if (result_.model.exprs[c].ty.kind != TypeKind::Clock) { set_unsupported("sample clock argument"); return lit_int(0); }
      } else {
        Expr ic;
        ic.kind = ExprKind::Call;
        ic.call_name = {in_.intern("Clock")};
        ic.ty = Type::of(TypeKind::Clock);
        c = add(std::move(ic));
      }
      return mk({u, c}, uty);
    }
    // 计数类实参：Integer、≤ parameter → 字面量。
    auto counter = [&](Id<Exp> raw, std::int64_t dflt) -> Id<Expr> {
      if (!raw.is_valid()) return lit_int(dflt);
      const Id<Expr> t = simplify_expr(inst_exp(raw, enclosing, prefix));
      if (result_.unsupported) return lit_int(0);
      if (result_.model.exprs[t].ty.kind != TypeKind::Integer || !expr_shape(t).empty() || refs_runtime_var(t)) {
        set_unsupported("clocked operator counter argument");   // omc：类型 / 变率不符报错
        return lit_int(0);
      }
      if (result_.model.exprs[t].kind == ExprKind::IntLit) return t;
      const std::optional<CVal> v = eval_scode(raw, scode_scope_of(enclosing), 0, /*allow_param=*/true, nullptr);
      if (!v || v->k != CVal::K::Int) { set_unsupported("clocked operator counter argument"); return lit_int(0); }
      return lit_int(v->i);
    };
    std::vector<Id<Expr>> cargs{u};
    if (name == "backSample" || name == "shiftSample") {
      cargs.push_back(counter(args[1], 0));
      cargs.push_back(counter(args[2], 1));
    } else {   // subSample / superSample
      cargs.push_back(counter(args[1], 0));
    }
    if (result_.unsupported) return lit_int(0);
    return mk(std::move(cargs), uty);
  }

  // 内建函数调用。as_builtin 有效 = 内建等价的 external 函数（builtin_equiv），按该内建名处理（不论书写名是否限定）。
  Id<Expr> builtin_call(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix, Symbol as_builtin) {
    const Cref& fn = I_.absyn.crefs[e.cref];
    const std::string_view builtin_name = as_builtin.is_valid() ? in_.str(as_builtin)
                                        : fn.parts.size() == 1 ? in_.str(fn.parts[0].name) : std::string_view{};
    if (builtin_name == "cardinality") return cardinality_call(e, enclosing, prefix);
    if (builtin_name == "size") return size_call(e, enclosing, prefix);
    // 函数体里的 ndims(a) = a 的维数（SZ 探针 `n := ndims(u)` → `n := 1`；模型语境在 model_matrix_builtin）。
    if (fn_scope_ && builtin_name == "ndims" && e.args.size() == 1 && !(e.arg_names.size() == 1 && e.arg_names[0].is_valid())) {
      const Id<Expr> a = inst_exp(e.args[0], enclosing, prefix);
      if (result_.unsupported) return lit_int(0);
      return lit_int(static_cast<std::int64_t>(expr_shape(a).size()));
    }
    if (builtin_name == "scalar") return scalar_call(e, enclosing, prefix);
    if (builtin_name == "String") return string_call(e, enclosing, prefix);
    if (builtin_name == "delay" && !fn_scope_) return delay_call(e, enclosing, prefix);
    // 状态机算子：ticksInState / timeInState 没有实参，函数体里也能用（m2）；带状态实参的只在模型语境。
    if (builtin_name == "ticksInState" || builtin_name == "timeInState" ||
        (!fn_scope_ && (builtin_name == "initialState" || builtin_name == "transition" || builtin_name == "activeState")))
      return state_machine_call(e, builtin_name, enclosing, prefix);
    if (builtin_name == "linspace" && !fn_scope_) return linspace_call(e, enclosing, prefix);
    if (!fn_scope_)
      if (Id<Expr> r = model_array_builtin(e, builtin_name, enclosing, prefix); r.is_valid() || result_.unsupported) return r;
    if (fn_scope_ && (builtin_name == "fill" || builtin_name == "zeros" || builtin_name == "ones"))
      return fill_call(e, builtin_name);
    if (fn_scope_ && e.args.size() == 1 &&
        (builtin_name == "sum" || builtin_name == "product" || builtin_name == "min" || builtin_name == "max"))
      if (Id<Expr> r = array_reduce_call(e, builtin_name); r.is_valid() || result_.unsupported) return r;
    if (builtin_name == "inStream" || builtin_name == "actualStream") return stream_op_call(e, enclosing, prefix, builtin_name);
    // 命名实参的内建（形参名照 NFModelicaBuiltin.mo：homotopy :295-298、semiLinear :156-159）：按形参序重排成位置实参再走
    // 下面（FuncBuiltinHomotopy `homotopy(actual = x, simplified = y)` → `homotopy(x, y)`，NA2 探针）。这些内建无缺省实参：
    // 缺 / 重复 / 未知名 → omc 报错 → 落到下面的 "named function argument" SKIP。smooth 不在此列：1.25.0 对 `smooth(expr = …)`
    // 报 "Function smooth has no input parameter named expr"（NA1 探针，特殊内建不按声明形参名配）。
    {
      static const std::pair<std::string_view, std::vector<std::string_view>> kNamed[] = {
          {"homotopy", {"actual", "simplified"}},
          {"semiLinear", {"x", "positiveSlope", "negativeSlope"}},
      };
      bool named = false;
      for (Symbol n : e.arg_names) named = named || n.is_valid();
      if (named)
        for (const auto& [bname, pnames] : kNamed) {
          if (bname != builtin_name || e.args.size() != pnames.size()) continue;
          std::vector<Id<Exp>> ordered(pnames.size());
          bool ok = true;
          std::size_t pos = 0;
          for (std::size_t i = 0; i < e.args.size() && ok; ++i) {
            std::size_t k = pnames.size();
            if (i < e.arg_names.size() && e.arg_names[i].is_valid()) {
              for (std::size_t j = 0; j < pnames.size(); ++j)
                if (in_.str(e.arg_names[i]) == pnames[j]) k = j;
            } else {
              k = pos++;
            }
            if (k >= pnames.size() || ordered[k].is_valid()) ok = false;
            else ordered[k] = e.args[i];
          }
          if (!ok) break;
          Exp pe = e;   // 位置化副本（栈上，不进 arena）
          pe.args = std::move(ordered);
          pe.arg_names.clear();
          return builtin_call(pe, enclosing, prefix, as_builtin);
        }
    }
    const BuiltinFn* bf = nullptr;
    bool supported = true;
    if (fn.parts.size() != 1 && !as_builtin.is_valid()) {
      set_unsupported_call("qualified function call");
      supported = false;
    }
    for (Symbol name : e.arg_names)
      if (name.is_valid()) {
        set_unsupported_call("named function argument");
        supported = false;
        break;
    }
    if (supported) {
      bf = lookup_builtin(builtin_name);
      if (!bf) {
        const std::string why = "function: " + std::string(builtin_name);
        set_unsupported_call(why.c_str());
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
      // 枚举范围上的 for 方程迭代变量（`Integer(i)`）不是书写的字面量：照常定型，取当前的标签。
      const bool label_iter = is_cref_exp(arg) && I_.absyn.crefs[I_.absyn.exps[arg].cref].parts.size() == 1 &&
                              (loop_labels_.count(I_.absyn.crefs[I_.absyn.exps[arg].cref].parts[0].name.id) > 0 ||
                               stmt_iter_label(arg).is_valid());
      Id<Expr> a = supported && builtin_name == "Integer" && is_cref_exp(arg) && !label_iter
                       ? enum_lit_value(arg, enclosing, prefix)
                       : inst_exp(arg, enclosing, prefix);
      // der 作用在空数组（被换掉的空数组变量引用）上 = 同型空数组；所在方程随后按空数组整条删去（ABCDsystem
      // `der(x) = A*x + B*u`，NFSimplifyModel.mo:254）。实参的合法性（连续 Real，否则 omc 报错）按源 cref 查：单段本地名。
      if (supported && builtin_name == "der" && e.args.size() == 1 && result_.model.exprs[a].kind == ExprKind::Array &&
          result_.model.exprs[a].ty.element_type().kind == TypeKind::Real && I_.absyn.exps[arg].kind == ExpKind::CrefExp)
        if (const std::vector<std::int64_t> sh = expr_shape(a); std::find(sh.begin(), sh.end(), 0) != sh.end()) {
          const Cref& ac = I_.absyn.crefs[I_.absyn.exps[arg].cref];
          const NodeId n = ac.parts.size() == 1 && ac.parts[0].subscripts.empty() ? find_local(enclosing, ac.parts[0].name)
                                                                                   : NodeId{};
          if (n.is_valid() && !I_.nodes[n].is_empty() && I_.nodes[n].comp.is_valid() &&
              I_.comps[I_.nodes[n].comp].attributes.variability == SVariability::Var)
            return a;
        }
      args.push_back(a);
    }
    // 向量化（模型语境，见 vectorize_arg）：标量形参的初等内建收到数组实参 → `array(sin(x[$i1]) for $i1 in 1:3)`，各数组实参
    // 同形（V4 探针 T22-T31：atan2(x, x)、atan2(x, 1)、mod(x, 2)、homotopy(x, x)、semiLinear(x, 1, 2)…）。实参换成取了下标的标量
    // 形态后照常走下面的标量逻辑（`sin(/*Real*/(k[$i1]))`、`sign` / Integer `abs` 的 Integer 结果由绑定处整体转型）。不在表里
    // 的内建不向量化：der / smooth / noEvent 整体保留数组实参，pre(连续) / max(x, 1) / String(x) 是 omc 报错，rem 在 1.25.0 打成
    // `OpenModelica.Internal.realRem(…)`（T14），都照旧 SKIP。
    std::vector<std::int64_t> vd;
    if (supported && !fn_scope_) {
      static const std::unordered_set<std::string_view> kVectorizable = {
          "sin",  "cos",  "tan", "asin", "acos",  "atan", "atan2", "sinh",    "cosh",  "tanh", "exp",      "log",
          "log10", "sqrt", "abs", "sign", "integer", "floor", "ceil",  "mod",     "div",   "homotopy", "semiLinear",
          "edge"};   // edge(b) → `array(edge(b[$i1]) for $i1 in 1:2)`（V4r 探针；change(b) 则不向量化，见下）
      if (kVectorizable.count(builtin_name)) {
        bool same = true;
        for (Id<Expr> a : args) {
          const std::vector<std::int64_t> sh = expr_shape(a);
          if (sh.empty()) continue;
          for (std::int64_t n : sh) same = same && n >= 1;
          if (!vd.empty() && vd != sh) same = false;
          vd = sh;
        }
        if (!vd.empty()) {
          // 向量化的 semiLinear 里 omc 照样做 simplifySemiLinear，还连带把下标式化掉（`semiLinear({a, b}, c, c)` →
          // `array({a, b}[$i1] * c for …)`、`semiLinear(0, x, 2 * x)` → `array(0.0 for …)`，ES 探针 e8）——未做 → SKIP。
          // 按向量化之前的实参判：x 是字面量 / 数组字面量（可能为零），或两个斜率相同（数值字面量此时还没转型，按值比：
          // `semiLinear(x, 2, 2.0)`）。
          if (builtin_name == "semiLinear" && args.size() == 3 &&
              (is_literal(args[0]) || result_.model.exprs[args[0]].kind == ExprKind::Array || expr_equal(args[1], args[2]) ||
               (is_num_lit(result_.model.exprs[args[1]]) && lit_is(args[2], as_real(args[1]))))) {
            set_unsupported("vectorized semiLinear simplification");
            supported = false;
            same = false;
          }
          std::vector<Id<Expr>> iters;
          for (std::size_t k = 1; k <= vd.size(); ++k) iters.push_back(vect_iter(k));
          for (std::size_t i = 0; same && i < args.size(); ++i) {
            if (expr_shape(args[i]).empty()) continue;
            const Id<Expr> v = vectorize_arg(args[i], iters, written_array_literal(e.args[i]));
            if (v.is_valid()) args[i] = v; else same = false;
          }
          if (!same) {
            set_unsupported("vectorized call argument");
            supported = false;
            vd.clear();
          }
        }
      }
    }
    // der 作用在"复合数组取字段"的展开数组上（`der(a.x)`）：见下面 der 的数组实参。
    const bool der_xfield = supported && !fn_scope_ && builtin_name == "der" && args.size() == 1 && is_xfield_array(args[0]);
    for (Id<Expr> a : args)
      if (result_.model.exprs[a].kind == ExprKind::Array && !der_xfield) {
        set_unsupported("array function argument");
        supported = false;
      }
    // pre / change：实参须是变量（cref），且在 when 之外不能是连续变量（NFBuiltinCall.typePreChangeCall :578-588；P1 / P5 / P7
    // 探针 omc 报 "must be a discrete expression" / "must be a variable"）。when 方程 / when 语句体里 1.25.0 接受连续实参
    //（P4 / P8 探针）。
    // edge 的实参须是变量引用（NFBuiltinCall.typeEdgeCall :681-685 v1.25.0，ES 探针 `edge(true)`、`edge(not bb)` omc 报
    // "First argument to edge … must be a variable"）；按书写形态判（常量引用会被内联成字面量）。
    auto written_cref = [&](Id<Exp> x) {   // 括号不算（`edge((bb))` omc 接受）
      while (I_.absyn.exps[x].kind == ExpKind::Tuple && I_.absyn.exps[x].args.size() == 1) x = I_.absyn.exps[x].args[0];
      return I_.absyn.exps[x].kind == ExpKind::CrefExp;
    };
    if (supported && !fn_scope_ && builtin_name == "edge" && e.args.size() == 1 && !written_cref(e.args[0])) {
      set_unsupported("edge argument must be a variable");
      supported = false;
    }
    if (supported && !fn_scope_ && args.size() == 1 && (builtin_name == "pre" || builtin_name == "change")) {
      const Expr& ax = result_.model.exprs[args[0]];
      if (is_literal(args[0]) && written_cref(e.args[0])) {
        // 常量引用（已内联成字面量）：pre(c) 求值成该常量（Ceval 的 pre 支取实参，NFCeval.mo:1933 v1.25.0）；change 没有求值
        // 支 → 留调用、实参是内联后的字面量（`change(true)`，ES 探针 e9）。
        if (builtin_name == "pre") return args[0];
      } else if (ax.kind != ExprKind::Cref) {
        set_unsupported("pre/change argument must be a variable");
        supported = false;
      } else if (alg_in_when_ == 0 && eq_in_when_ == 0 && cref_is_continuous(ax, prefix.size())) {
        set_unsupported("pre/change of continuous variable");
        supported = false;
      }
    }
    // der 的数组实参（整体 / 切片引用的 Real 数组）：不向量化，调用保持实参的数组类型——绑定里整体保留 `k1 = der(x)`，等式里
    // 随等式逐元素 `der(y[1]) = x[1]`（non_vectorizeable_call；V4s 探针）。Integer 数组、非 cref 实参的形态未核实 → SKIP。
    if (supported && !fn_scope_ && builtin_name == "der" && args.size() == 1) {
      if (const std::vector<std::int64_t> sh = expr_shape(args[0]); !sh.empty()) {
        const Expr ax = result_.model.exprs[args[0]];   // 拷贝
        bool ok = ax.kind == ExprKind::Cref && ax.ty.is_array() && ax.ty.element_type().kind == TypeKind::Real;
        // 复合数组取字段的展开数组（CA 探针 c3：绑定里整体保留 `y = der({a[1].x, a[2].x, a[3].x})`，等式里随等式逐元素
        // `der(a[1].x) = …`）。
        // 元素须都是变量（参数字段的 `der(a.k)` omc 折成 `{0.0, 0.0, 0.0}`，未做）。
        auto all_vars = [&](auto& self, Id<Expr> x) -> bool {
          const Expr& xe = result_.model.exprs[x];
          if (xe.kind != ExprKind::Array) return xe.kind == ExprKind::Cref && refs_runtime_var(x);
          for (Id<Expr> el : xe.args) if (!self(self, el)) return false;
          return true;
        };
        const bool xf = der_xfield && expr_elem_kind(args[0]) == TypeKind::Real && all_vars(all_vars, args[0]);
        ok = ok || xf;
        for (std::int64_t n : sh) ok = ok && n >= 1;
        if (!ok) {
          set_unsupported("der of array expression");
          supported = false;
        } else {
          Expr r;
          r.kind = ExprKind::Call;
          r.call_name = {in_.intern("der")};
          r.args = {args[0]};
          r.ty = ax.ty;
          if (xf) {
            std::vector<Dimension> ds;
            for (std::int64_t n : sh) {
              Dimension d;
              d.kind = DimKind::Integer;
              d.size = n;
              ds.push_back(d);
            }
            r.ty = Type::array_of(Type::real(), std::move(ds));
          }
          return add(std::move(r));
        }
      }
    }
    // min / max 的两个标量实参还可以同是 Boolean / 同一枚举（typeMinMaxCall，v1.25.0 NFBuiltinCall.mo）：两个都是字面量 → 求值
    //（false < true、枚举按序数），否则留调用（RD 探针 r1：`max(bb[1], bb[2])`、`min(ee[1], E.two)`）。
    if (supported && args.size() == 2 && (builtin_name == "min" || builtin_name == "max")) {
      const Expr x0 = result_.model.exprs[args[0]], x1 = result_.model.exprs[args[1]];   // 拷贝
      const TypeKind k0 = x0.ty.kind;
      if (k0 == x1.ty.kind && (k0 == TypeKind::Boolean || k0 == TypeKind::Enumeration)) {
        const bool lits = is_minmax_literal(args[0]) && is_minmax_literal(args[1]);
        // 两个枚举字面量须同一类型（类型路径 = enum_path 去掉末段的字面量名）；引用与字面量之间的类型核对未做。
        if (lits && k0 == TypeKind::Enumeration &&
            std::vector<Symbol>(x0.enum_path.begin(), x0.enum_path.end() - 1) !=
                std::vector<Symbol>(x1.enum_path.begin(), x1.enum_path.end() - 1)) {
          set_unsupported("min/max of different enumerations");
          return lit_int(0);
        }
        if (lits) return fold_min_max({args[0], args[1]}, builtin_name == "min");
        Expr r;
        r.kind = ExprKind::Call;
        r.call_name = {in_.intern(builtin_name)};
        r.args = args;
        r.ty = x0.ty.enum_path.empty() ? x1.ty : x0.ty;
        return add(std::move(r));
      }
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
      // expr 是 Real（RealArrayOrRecord，NFModelicaBuiltin.mo:442-445）：标量 Integer 实参插转型（BT1 探针 `smooth(0, i)` →
      // `smooth(0, /*Real*/(i))`）；数组 Integer 实参的转型形态未核实 → 下面照旧 SKIP。
      if (result_.model.exprs[args[1]].ty.kind == TypeKind::Integer && expr_shape(args[1]).empty()) args[1] = to_real(args[1]);
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
    if (as_builtin.is_valid()) r.call_name = {as_builtin};
    else for (const CrefPart& p : fn.parts) r.call_name.push_back(p.name);
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
      // change 的数组实参不向量化，调用整体保留、结果是同维的 Boolean 数组（V4r 探针 `d9 = change(b)`、`v = change(b)`）。
      if (builtin_name == "change" && r.args.size() == 1 && result_.model.exprs[r.args[0]].ty.is_array())
        r.ty = Type::array_of(Type::boolean(), result_.model.exprs[r.args[0]].ty.dims);
    }
    const Id<Expr> call = add(std::move(r));
    return supported && !vd.empty() ? wrap_vectorized(call, vd) : call;
  }

  // 把标量表达式提升到 Real（NFExpression.typeCast :1180-1270）：Integer 字面量 → Real 字面量；一元负号 / if 表达式 /
  // Cast 把转换推进操作数 / 两支 / 内层（IfExpression2 `if time > 1 then 1 else 2` → `… then 1.0 else 2.0`）；其余包 CAST。
  Id<Expr> to_real(Id<Expr> e) {
    const Expr x = result_.model.exprs[e];   // 拷贝：下面会 add
    // 已是 Real（含 Real 数组的整体引用 / 运算：`{x, x}` 里的 x 不需要转，此前被包成 `/*Real*/(x)`）。数组字面量的类型是粗
    // 类型，叶仍可能是 Integer，交给 coerce_deep 逐叶转。
    if (x.ty.kind == TypeKind::Real || (x.kind != ExprKind::Array && x.ty.element_type().kind == TypeKind::Real)) return e;
    switch (x.kind) {
      case ExprKind::IntLit: return lit_real(static_cast<double>(x.ival));
      case ExprKind::Unary:
        if (x.un == UnOp::Minus) {
          const Expr& inner = result_.model.exprs[x.a];
          if (inner.kind == ExprKind::IntLit) return lit_real(-static_cast<double>(inner.ival));
          Expr u = x;
          u.a = to_real(x.a);
          u.ty = Type::real();
          return add(std::move(u));
        }
        break;
      case ExprKind::IfExp: {
        Expr r = x;
        r.b = to_real(x.b);
        r.c = to_real(x.c);
        r.ty = Type::real();
        return add(std::move(r));
      }
      case ExprKind::Cast: return to_real(x.a);
      case ExprKind::Subscript:   // 字面量数组取下标 `{1, 2, 3}[n]`：转型推进数组元素（SubscriptedExp4 → `{1.0, 2.0, 3.0}[n]`）
        if (result_.model.exprs[x.a].kind == ExprKind::Array) {
          Expr r = x;
          r.a = coerce_deep(x.a, TypeKind::Real);
          r.ty = Type::real();
          return add(std::move(r));
        }
        break;
      default: break;
    }
    Expr c; c.kind = ExprKind::Cast; c.a = e; c.ty = Type::real();
    return add(std::move(c));
  }

  // `(e)[i]`（ExpKind::Subscript：任意 primary 后的下标）：基是 cref（含括号分组）→ 下标并进 cref 末段（`(x)[n]` →
  // `x[n]`）；基是一维数组字面量：字面量下标 → 取元素（`({1, 2, 3})[2]` → `2`，越界 SKIP），否则保留 ASUB
  // `{1, 2, 3}[n]`（ExpressionDump ASUB :703-714 打印）。SubscriptedExp1/3/4。其它基（调用结果 `f(x)[1]`、运算式）、
  // 切片 / 多个下标 → SKIP。
  Id<Expr> subscripted_exp(const Exp& e, NodeId enclosing, const std::vector<Symbol>& prefix) {
    Id<Exp> base = e.a;
    while (I_.absyn.exps[base].kind == ExpKind::Tuple && I_.absyn.exps[base].args.size() == 1) base = I_.absyn.exps[base].args[0];
    const std::vector<Id<Exp>> subs = e.args;   // 拷贝（下面往 absyn arena add）
    for (Id<Exp> s : subs) {
      const ExpKind sk = I_.absyn.exps[s].kind;
      if (sk == ExpKind::Colon || sk == ExpKind::Range || is_array_ctor(s)) { set_unsupported("subscripted expression slice"); return lit_int(0); }
    }
    if (I_.absyn.exps[base].kind == ExpKind::CrefExp) {
      Cref c = I_.absyn.crefs[I_.absyn.exps[base].cref];
      if (c.parts.empty() || !c.parts.back().subscripts.empty()) { set_unsupported("subscripted expression"); return lit_int(0); }
      c.parts.back().subscripts = subs;
      return cref(synth_cref(std::move(c)), enclosing, prefix);
    }
    if (!is_array_ctor(base) || is_comprehension(base) || subs.size() != 1) { set_unsupported("subscripted expression"); return lit_int(0); }
    const Id<Expr> b = inst_exp(base, enclosing, prefix);
    const Id<Expr> s = simplify_expr(inst_exp(subs[0], enclosing, prefix));
    if (result_.unsupported) return lit_int(0);
    const Expr be = result_.model.exprs[b];   // 拷贝
    if (be.kind != ExprKind::Array || be.args.empty() || !expr_shape(be.args[0]).empty()) {
      set_unsupported("subscripted expression"); return lit_int(0);
    }
    const Expr se = result_.model.exprs[s];
    if (se.kind == ExprKind::IntLit) {
      if (se.ival < 1 || static_cast<std::size_t>(se.ival) > be.args.size()) { set_unsupported("array subscript out of range"); return lit_int(0); }
      return be.args[static_cast<std::size_t>(se.ival) - 1];
    }
    if (se.ty.kind != TypeKind::Integer) { set_unsupported("subscripted expression"); return lit_int(0); }
    Expr r;
    r.kind = ExprKind::Subscript;
    r.a = b;
    r.args = {s};
    r.ty = result_.model.exprs[be.args[0]].ty;   // 元素类型（数组字面量自身的 ty 是粗类型）
    return add(std::move(r));
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

  // ---- NFSimplifyExp 的二元 / 一元规则（simplifyBinaryAdd/Sub/Mul/Div/Pow :1005-1139，Expression.isNegated / negate
  // NFExpression.mo:5308-5332）----
  bool lit_is(Id<Expr> id, double v) const {
    const Expr& e = result_.model.exprs[id];
    return (e.kind == ExprKind::IntLit && static_cast<double>(e.ival) == v) || (e.kind == ExprKind::RealLit && e.rval == v);
  }
  bool lit_nonzero(Id<Expr> id) const {
    const Expr& e = result_.model.exprs[id];
    return (e.kind == ExprKind::IntLit && e.ival != 0) || (e.kind == ExprKind::RealLit && e.rval != 0.0);
  }
  bool is_negated(Id<Expr> id) const {
    const Expr& e = result_.model.exprs[id];
    switch (e.kind) {
      case ExprKind::IntLit: return e.ival < 0;
      case ExprKind::RealLit: return e.rval < 0;
      case ExprKind::Cast: return is_negated(e.a);
      case ExprKind::Unary: return true;
      default: return false;
    }
  }
  Id<Expr> negate_expr(Id<Expr> id) {
    const Expr e = result_.model.exprs[id];
    switch (e.kind) {
      case ExprKind::IntLit: return lit_int(-e.ival);
      case ExprKind::RealLit: return lit_real(-e.rval);
      case ExprKind::Cast: { Expr c = e; c.a = negate_expr(e.a); return add(std::move(c)); }
      case ExprKind::Unary: return e.a;
      default: { Expr u; u.kind = ExprKind::Unary; u.un = UnOp::Minus; u.a = id; u.ty = e.ty; return add(std::move(u)); }
    }
  }
  Id<Expr> make_bin(Id<Expr> a, BinOp op, Id<Expr> b, const Type& ty) {
    Expr r; r.kind = ExprKind::Binary; r.bin = op; r.a = a; r.b = b; r.ty = ty;
    return add(std::move(r));
  }
  Id<Expr> make_zero(const Type& ty) { return ty.kind == TypeKind::Integer ? lit_int(0) : lit_real(0.0); }
  // ---- 函数体里已知维数组运算的展开（SimplifyExp.simplifyBinary :965-967 在 nfExpandOperations（缺省开，Flags.mo:508）
  // 下调 ExpandExp.expand；ElementWiseMultiplication `positionvector * 2` → `{positionvector[1] * 2.0, …}`）----
  // 能展开的：全整数维的 cref（逐元素下标）、数组字面量。其余（`:` / 表达式维、调用）→ invalid（整式不展开）。
  Id<Expr> fn_expand(Id<Expr> id) {
    const Expr x = result_.model.exprs[id];
    if (x.kind == ExprKind::Array) return id;
    // 整个留着的 `size(u)`（见 size_call）：维数已知，展开成 `{size(u, 1), size(u, 2), …}`（SZ 探针 s2：
    // `n := size(u) + {1, 1}` → `{size(u, 1) + 1, size(u, 2) + 1}`、`sum(size(u))` → `size(u, 1) + size(u, 2)`）。
    if (x.kind == ExprKind::Size && x.args.size() == 1 && x.ty.dims.size() == 1 && x.ty.dims[0].kind == DimKind::Integer) {
      Expr arr;
      arr.kind = ExprKind::Array;
      arr.ty = x.ty;
      for (std::int64_t k = 1; k <= x.ty.dims[0].size; ++k) {
        Expr sx;
        sx.kind = ExprKind::Size;
        sx.args = {x.args[0], lit_int(k)};
        sx.ty = Type::integer();
        arr.args.push_back(add(std::move(sx)));
      }
      return add(std::move(arr));
    }
    // 数组 if：两支都能展开 → 逐元素的 if（FS 探针 t4 `2 * (if k > 0 then n2 else w[1:2])` →
    // `{2.0 * (if k > 0 then /*Real*/(n2[1]) else w[1]), …}`）。
    if (x.kind == ExprKind::IfExp && x.ty.is_array()) {
      const Id<Expr> et = fn_expand(x.b), ef = fn_expand(x.c);
      if (!et.is_valid() || !ef.is_valid()) return {};
      auto zip = [&](auto& self, Id<Expr> tn, Id<Expr> fnn) -> Id<Expr> {
        const Expr te = result_.model.exprs[tn], fe = result_.model.exprs[fnn];   // 拷贝
        if (te.kind != ExprKind::Array || fe.kind != ExprKind::Array) {
          if (te.kind == ExprKind::Array || fe.kind == ExprKind::Array) return {};
          Expr r;
          r.kind = ExprKind::IfExp;
          r.a = x.a;
          r.b = tn;
          r.c = fnn;
          r.ty = x.ty.element_type();
          return add(std::move(r));
        }
        if (te.args.size() != fe.args.size()) return {};
        Expr arr = te;
        for (std::size_t i = 0; i < te.args.size(); ++i) {
          arr.args[i] = self(self, te.args[i], fe.args[i]);
          if (!arr.args[i].is_valid()) return {};
        }
        return add(std::move(arr));
      };
      return zip(zip, et, ef);
    }
    // 整体转型的数组：展开里面的、逐叶转 Real（`/*Real[2]*/(n[1:2])` → `{/*Real*/(n[1]), /*Real*/(n[2])}`）。
    if (x.kind == ExprKind::Cast && x.ty.is_array()) {
      const Id<Expr> in = fn_expand(x.a);
      return in.is_valid() ? coerce_deep(in, TypeKind::Real) : Id<Expr>{};
    }
    // fill(v, n, …)，维都是字面量：展开成字面量（ExpandExp.expandBuiltinFill；FS 探针 t2 `fill(w[1], 2) + w[1:2]` →
    // `{w[1] + w[1], w[1] + w[2]}`）。
    if (x.kind == ExprKind::Call && x.call_name.size() == 1 && in_.str(x.call_name[0]) == "fill" && !x.args.empty() &&
        x.ty.is_array()) {
      for (const Dimension& d : x.ty.dims)
        if (d.kind != DimKind::Integer) return {};
      auto rec = [&](auto& self, std::size_t k) -> Id<Expr> {
        if (k == x.ty.dims.size()) return x.args[0];
        Expr arr;
        arr.kind = ExprKind::Array;
        arr.ty = Type::array_of(x.ty.element_type(),
                                std::vector<Dimension>(x.ty.dims.begin() + static_cast<std::ptrdiff_t>(k), x.ty.dims.end()));
        for (std::int64_t i = 0; i < x.ty.dims[k].size; ++i) arr.args.push_back(self(self, k + 1));
        return add(std::move(arr));
      };
      return rec(rec, 0);
    }
    if (x.kind != ExprKind::Cref || !x.ty.is_array()) return {};
    // 下标数组是变量（`w[idx]`）：元素是 `w[idx[i]]`，scalarize 不认 → 不展开。
    for (Id<Expr> sx : x.cref.parts.back().subscripts)
      if (result_.model.exprs[sx].ty.is_array() && result_.model.exprs[sx].kind != ExprKind::Array) return {};
    std::vector<std::int64_t> dims;
    for (const Dimension& d : x.ty.dims) {
      if (d.kind != DimKind::Integer) return {};
      dims.push_back(d.size);
    }
    auto build = [&](auto& self, std::size_t k, std::vector<std::int64_t>& idx) -> Id<Expr> {
      if (k == dims.size()) return scalarize(id, idx);   // 整体引用补下标；切片按位置换成元素（`g[1,:]` → `g[1,2]`）
      std::vector<Id<Expr>> els;
      for (std::int64_t i = 1; i <= dims[k]; ++i) {
        idx.push_back(i);
        els.push_back(self(self, k + 1, idx));
        idx.pop_back();
      }
      Expr arr;
      arr.kind = ExprKind::Array;
      arr.ty = Type::array_of(x.ty.element_type(),
                              std::vector<Dimension>(x.ty.dims.begin() + static_cast<std::ptrdiff_t>(k), x.ty.dims.end()));
      arr.args = std::move(els);
      return add(std::move(arr));
    };
    std::vector<std::int64_t> idx;
    return build(build, 0, idx);
  }
  // 展开数组二元运算（expandBinaryElementWise / ScalarArray / ArrayScalar :574-690）；不能展开 → invalid。
  Id<Expr> fn_expand_binary(Id<Expr> a, BinOp op, Id<Expr> b, const Type& ty, bool product) {
    const bool aarr = op_type(a).is_array(), barr = op_type(b).is_array();
    if (product) {   // 向量 / 矩阵积：两侧都能展开（维已知）→ 同模型语境展开成积和（FS 探针 t1 / t2：`u[1:2] * w[2:3]` →
      //              `u[1] * w[2] + u[2] * w[3]`、`g * w` → `{g[1,1] * w[1] + …, …}`）；否则保持符号
      const Id<Expr> ea = fn_expand(a), eb = fn_expand(b);   // 展开后的字面量：整体转型已逐叶转（`/*Real*/(n2[1]) * w[1] + …`）
      return ea.is_valid() && eb.is_valid() ? model_array_product(ea, eb) : Id<Expr>{};
    }
    const Type et = ty.element_type();
    if (aarr && barr) {
      const Id<Expr> ea = fn_expand(a), eb = fn_expand(b);
      if (!ea.is_valid() || !eb.is_valid()) return {};
      const std::vector<Id<Expr>> xa = result_.model.exprs[ea].args, xb = result_.model.exprs[eb].args;
      if (xa.size() != xb.size()) return {};
      std::vector<Id<Expr>> els;
      for (std::size_t i = 0; i < xa.size(); ++i) {
        const bool sub_arr = op_type(xa[i]).is_array();
        Id<Expr> el;
        if (sub_arr) {
          Type st = op_type(xa[i]);
          st = Type::array_of(et, st.dims);
          el = fn_expand_binary(xa[i], op, xb[i], st, false);
          if (!el.is_valid()) return {};
        } else {
          el = simplify_binary_op(xa[i], op, xb[i], et);
        }
        els.push_back(el);
      }
      Expr arr; arr.kind = ExprKind::Array; arr.ty = ty; arr.args = std::move(els);
      return add(std::move(arr));
    }
    const Id<Expr> arr_side = aarr ? a : b;
    const Id<Expr> ex = fn_expand(arr_side);
    if (!ex.is_valid()) return {};
    auto map_el = [&](auto& self, Id<Expr> node) -> Id<Expr> {
      const Expr n = result_.model.exprs[node];
      if (n.kind != ExprKind::Array) return aarr ? simplify_binary_op(node, op, b, et) : simplify_binary_op(a, op, node, et);
      std::vector<Id<Expr>> els;
      for (Id<Expr> c : n.args) els.push_back(self(self, c));
      Expr r = n;
      r.ty = Type::array_of(et, n.ty.dims);
      r.args = std::move(els);
      return add(std::move(r));
    };
    return map_el(map_el, ex);
  }

  // 函数体里没能按元素展开（维未知）的数组二元运算的打印形态（FA 探针 f1–f3；对应的 DAE 算符类别见
  // FlatModel::ew_binaries）：
  //   `*` / `.*`  标量挪到右边、不带点（`2 * u`、`a .* u` → `u * a`）；数组 .* 数组带点。
  //   `/` / `./`  数组 / 标量不带点；标量 ./ 数组、数组 ./ 数组带点。
  //   `.^`        都带点。
  //   `.+`        数组在左、带点（`a .+ u` → `u .+ a`）；数组 + 数组不带点。
  //   `.-`        数组 .- 标量 → `u .+ (-a)`（没有 SUB_ARRAY_SCALAR 这个算符，`u .- 2` → `u .+ -2.0`）；标量 .- 数组带点；
  //               数组 - 数组不带点。
  Id<Expr> fn_array_form(Id<Expr> a, Op orig, BinOp op, Id<Expr> b, const Type& ty) {
    const bool aarr = op_type(a).is_array(), barr = op_type(b).is_array();
    auto dot = [&](Id<Expr> x, BinOp o, Id<Expr> y) {
      const Id<Expr> r = make_bin(x, o, y, ty);
      result_.model.ew_binaries.insert(r.raw);
      return r;
    };
    switch (orig) {
      case Op::Mul:
      case Op::EWMul:
        if (aarr && barr) return dot(a, BinOp::Mul, b);   // `.*`（不带点的 数组 * 数组 是积，不到这里）
        return aarr ? make_bin(a, BinOp::Mul, b, ty) : make_bin(b, BinOp::Mul, a, ty);
      case Op::Div:
      case Op::EWDiv:
        return aarr && !barr ? make_bin(a, BinOp::Div, b, ty) : dot(a, BinOp::Div, b);
      case Op::EWPow:
        return dot(a, BinOp::Pow, b);
      case Op::Add:
      case Op::EWAdd:
        if (aarr && barr) return make_bin(a, BinOp::Add, b, ty);
        return aarr ? dot(a, BinOp::Add, b) : dot(b, BinOp::Add, a);
      case Op::Sub:
      case Op::EWSub:
        if (aarr && barr) return make_bin(a, BinOp::Sub, b, ty);
        if (aarr) return dot(a, BinOp::Add, negate_expr(b));
        return dot(a, BinOp::Sub, b);
      default:
        return make_bin(a, op, b, ty);
    }
  }

  Id<Expr> simplify_binary_op(Id<Expr> a, BinOp op, Id<Expr> b, const Type ty) {
    const bool scalar = !ty.is_array();
    switch (op) {
      case BinOp::Add:
        if (lit_is(a, 0)) return b;
        if (lit_is(b, 0)) return a;
        if (is_negated(a)) {
          if (is_negated(b)) return negate_expr(make_bin(negate_expr(a), BinOp::Add, negate_expr(b), ty));
          return simplify_binary_op(b, BinOp::Sub, negate_expr(a), ty);
        }
        if (is_negated(b)) return simplify_binary_op(a, BinOp::Sub, negate_expr(b), ty);
        return make_bin(a, op, b, ty);
      case BinOp::Sub:
        if (lit_is(a, 0)) return negate_expr(b);
        if (lit_is(b, 0)) return a;
        if (scalar && expr_equal(a, b)) return make_zero(ty);
        if (is_negated(a)) {
          if (is_negated(b)) return make_bin(negate_expr(b), BinOp::Sub, negate_expr(a), ty);
          return negate_expr(simplify_binary_op(negate_expr(a), BinOp::Add, b, ty));
        }
        if (is_negated(b)) return simplify_binary_op(a, BinOp::Add, negate_expr(b), ty);
        return make_bin(a, op, b, ty);
      case BinOp::Mul:
        if (lit_is(a, 0)) return a;
        if (lit_is(a, 1)) return b;
        if (lit_is(b, 0)) return b;
        if (lit_is(b, 1)) return a;
        return make_bin(a, op, b, ty);
      case BinOp::Div:
        if (lit_is(b, 1)) return a;
        if (lit_is(b, -1)) return negate_expr(a);
        if (lit_is(a, 0) && lit_nonzero(b)) return a;
        // OMC 原样：match (isNegated(exp1), isNegated(exp1)) 两项都看分子，分子带负号时两边同时取负
        //（SimplifyBinary `(-a*x)/x` → `a * x / (-x)`）。
        if (is_negated(a)) return make_bin(negate_expr(a), op, negate_expr(b), ty);
        return make_bin(a, op, b, ty);
      case BinOp::Pow:
        if (scalar && lit_is(b, 0)) return ty.kind == TypeKind::Integer ? lit_int(1) : lit_real(1.0);
        if (lit_is(b, 1)) return a;
        return make_bin(a, op, b, ty);
    }
    return make_bin(a, op, b, ty);
  }

  Id<Expr> simplify_expr(Id<Expr> id) {
    const ExprKind k = result_.model.exprs[id].kind;
    switch (k) {
      case ExprKind::Binary: {
        if (nofold_bins_.count(id.raw)) return id;   // sum / product 展开出的元素链：omc 不再化简（见 model_array_builtin）
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
            case BinOp::Pow:
              // 负底数配非整数指数：omc 的化简不求值、原样留下（Ceval.evalBinaryPow，NFCeval.mo:1095-1111 v1.25.0；DM 探针 s7
              // `Real y = (-1.0)^0.5` → `(-1.0) ^ 0.5`）；0 的负数次幂 omc 报错。
              if (x < 0 && std::floor(y) != y) break;
              if (x == 0 && y < 0) { set_unsupported("division by zero in constant"); return id; }
              return lit_real(std::pow(x, y));
          }
        }
        // 字符串常量拼接（Ceval.evalBinaryAdd 的字符串分支，StringConcatenation）。
        if (op == BinOp::Add && result_.model.exprs[a].kind == ExprKind::StringLit &&
            result_.model.exprs[b].kind == ExprKind::StringLit) {
          Expr r;
          r.kind = ExprKind::StringLit;
          r.ty = Type::string();
          r.sval = in_.intern(std::string(in_.str(result_.model.exprs[a].sval)) +
                              std::string(in_.str(result_.model.exprs[b].sval)));
          return add(std::move(r));
        }
        // 标量积（向量 * 向量，结果是标量）也走这里：两侧都能展开时 omc 展成积和（FA 探针 f1 `s := w * w` →
        // `w[1] * w[1] + …`），未做 → fn_expand_binary 里 SKIP。
        if (fn_scope_ && (ty.is_array() || result_.model.product_binaries.count(id.raw) > 0)) {
          const bool product = result_.model.product_binaries.count(id.raw) > 0;
          if (Id<Expr> ex = fn_expand_binary(a, op, b, ty, product); ex.is_valid()) return ex;
          if (result_.unsupported) return id;
          if (product) {
            const Id<Expr> r = simplify_binary_op(a, op, b, ty);
            if (result_.model.exprs[r].kind == ExprKind::Binary) result_.model.product_binaries.insert(r.raw);
            return r;
          }
          // 没能按元素展开（维未知）的数组运算：按书写的算符定打印形态；已定形的只换子式。都不做恒等化简
          //（FA 探针 f3：`u * 1` → `u * 1.0`、`u .+ 0` → `u .+ 0.0`、`u / 1` → `u / 1.0`）。
          if (const auto oit = fn_array_ops_.find(id.raw); oit != fn_array_ops_.end()) return fn_array_form(a, oit->second, op, b, ty);
          const Id<Expr> r = make_bin(a, op, b, ty);
          if (result_.model.ew_binaries.count(id.raw)) result_.model.ew_binaries.insert(r.raw);
          return r;
        }
        // 模型语境的数组运算不在数组层做恒等化简：omc 先按元素展开、再逐元素化简（WA 探针 w3：`y = z * 1` →
        // `y = {z[1], z[2]}`、`y = 0 * z` → `{0.0, 0.0}`；此前折成 `y = z` / 标量 0）。
        if (ty.is_array()) return a == a0 && b == b0 ? id : make_bin(a, op, b, ty);
        return simplify_binary_op(a, op, b, ty);
      }
      case ExprKind::Unary: {
        const Id<Expr> a0 = result_.model.exprs[id].a;
        const UnOp op = result_.model.exprs[id].un;
        const Type ty = result_.model.exprs[id].ty;
        // simplifyUnary :1141-1175：`-(-e)` → e；字面量求值；否则 simplifyUnarySign（逐层翻转后 negate，CAST 往里推）。
        if (op == UnOp::Minus && result_.model.exprs[a0].kind == ExprKind::Unary)
          return simplify_expr(result_.model.exprs[a0].a);
        Id<Expr> a = simplify_expr(a0);
        // 函数体里已知维的数组取负：逐元素展开（FA 探针 f1 `v3 := -w` → `{-w[1], -w[2], -w[3]}`；同二元运算的展开）。
        if (fn_scope_ && op == UnOp::Minus && ty.is_array())
          if (const Id<Expr> ex = fn_expand(a); ex.is_valid()) {
            auto neg = [&](auto& self, Id<Expr> node) -> Id<Expr> {
              const Expr n = result_.model.exprs[node];   // 拷贝
              if (n.kind != ExprKind::Array) {
                Expr u; u.kind = ExprKind::Unary; u.un = UnOp::Minus; u.a = node; u.ty = n.ty;
                return simplify_expr(add(std::move(u)));
              }
              Expr r = n;
              for (Id<Expr>& c : r.args) c = self(self, c);
              return add(std::move(r));
            };
            return neg(neg, ex);
          }
        const Expr& ea = result_.model.exprs[a];
        if (op == UnOp::Minus) {
          if (ea.kind == ExprKind::IntLit) return lit_int(-ea.ival);
          if (ea.kind == ExprKind::RealLit) return lit_real(-ea.rval);
          bool neg = true;
          while (result_.model.exprs[a].kind == ExprKind::Unary) { a = result_.model.exprs[a].a; neg = !neg; }
          return neg ? negate_expr(a) : a;
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
        const Type ty = result_.model.exprs[id].ty;   // 保留原类型（Boolean 数组的逐元素 and / or）
        Id<Expr> a = simplify_expr(a0), b = simplify_expr(b0);
        const Expr& ea = result_.model.exprs[a];
        const Expr& eb = result_.model.exprs[b];
        // 吸收律（simplifyLogicBinaryAnd / Or，NFSimplifyExp.mo:1178-1242 v1.25.0；两序）：`false and e` → false、
        // `true and e` → e、`true or e` → true、`false or e` → e；两个字面量是其特例。数组操作数不动（AP p4 探针）。
        // 标量化产生的节点除外（scalarized_logicals_）：omc 的标量化在化简之后，逐元素出来的 `true or b[1]` 不再化简。
        const bool raw = scalarized_logicals_.count(id.raw) > 0;
        if (ea.kind == ExprKind::BoolLit && (!raw || eb.kind == ExprKind::BoolLit)) return ea.bval == (op == LogOp::Or) ? a : b;
        if (eb.kind == ExprKind::BoolLit && !raw) return eb.bval == (op == LogOp::Or) ? b : a;
        Expr r; r.kind = ExprKind::Logical; r.log = op; r.a = a; r.b = b; r.ty = ty.is_array() ? ty : Type::boolean();
        const Id<Expr> n = add(std::move(r));
        if (raw) scalarized_logicals_.insert(n.raw);
        return n;
      }
      case ExprKind::LogicalNot: {
        const Id<Expr> a0 = result_.model.exprs[id].a;
        const Type ty = result_.model.exprs[id].ty;
        // `not (not e)` → e（simplifyLogicUnary，NFSimplifyExp.mo:1244-1266 v1.25.0）：只认书写出来的双重否定——里层化简后
        // 才成 `not e` 的不收（AP p4 探针 `not (true and not bb[1])` → `not (not bb[1])`；PP 探针 `not (not p)` → `p`）。
        if (!ty.is_array() && result_.model.exprs[a0].kind == ExprKind::LogicalNot)
          return simplify_expr(result_.model.exprs[a0].a);
        Id<Expr> a = simplify_expr(a0);
        const Expr& ea = result_.model.exprs[a];
        if (ea.kind == ExprKind::BoolLit) return lit_bool(!ea.bval);
        Expr r; r.kind = ExprKind::LogicalNot; r.a = a; r.ty = ty.is_array() ? ty : Type::boolean();
        return add(std::move(r));
      }
      case ExprKind::Cast: {
        const Id<Expr> a0 = result_.model.exprs[id].a;
        const Type cty = result_.model.exprs[id].ty;   // 保留原转型类型（数组转型 `/*Real[3]*/`，AC3）
        Id<Expr> a = simplify_expr(a0);
        const Expr& ea = result_.model.exprs[a];
        if (ea.kind == ExprKind::IntLit) return lit_real(static_cast<double>(ea.ival));
        if (ea.kind == ExprKind::RealLit) return a;
        Expr r; r.kind = ExprKind::Cast; r.a = a; r.ty = cty.is_array() ? cty : Type::real();
        return add(std::move(r));
      }
      case ExprKind::Array: {
        // 数组字面量逐元素化简（结构参数代入后 `{/*Real*/(3), …}` → `{3.0, …}`，EV14 探针 `Real x[n] = fill(n, n)`）。
        const std::vector<Id<Expr>> args0 = result_.model.exprs[id].args;
        std::vector<Id<Expr>> args;
        bool changed = false;
        for (Id<Expr> a : args0) {
          const Id<Expr> s = simplify_expr(a);
          changed = changed || !(s == a);
          args.push_back(s);
        }
        if (!changed) return id;
        Expr r = result_.model.exprs[id];
        r.args = std::move(args);
        return add(std::move(r));
      }
      case ExprKind::Subscript: {
        // 数组字面量取整数字面量下标 → 该元素（向量化调用的迭代变量被代入后 `{1.0, 2.0, time}[1]` → `1.0`；V4c 探针
        // `sum(sin({1, 2, time}))` → `0.8414709848078965 + 0.9092974268256817 + sin(time)`）。越界留给调用方（omc 报错）。
        const Expr x = result_.model.exprs[id];   // 拷贝
        Id<Expr> base = x.a;
        std::size_t used = 0;
        while (used < x.args.size() && result_.model.exprs[base].kind == ExprKind::Array) {
          const Expr& se = result_.model.exprs[x.args[used]];
          const Expr& be = result_.model.exprs[base];
          if (se.kind != ExprKind::IntLit || se.ival < 1 || static_cast<std::size_t>(se.ival) > be.args.size()) break;
          base = be.args[static_cast<std::size_t>(se.ival) - 1];
          ++used;
        }
        if (used == x.args.size()) return simplify_expr(base);
        return id;
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

        // 结构参数代入后实参全成字面量的纯用户函数调用：omc 的化简会求值（NFSimplifyExp.simplifyCall :182 / :206，
        // NF_EVAL_CONST_ARG_FUNCS；FD3 探针 `parameter Real m = g(n) + n`、n 是数组维 → `m = 6.0`，g 不再打印）。ymc 的函数
        // 求值走 Absyn（eval_user_call），代入后的类型化调用没有求值入口 → SKIP。external / impure 不求值；记录构造求值后
        // 打印不变。
        // 不是代入出来的同理：call() 先试求值、求不出才发射，留下来的全字面量调用就是 ymc 的解释器没求出来（体里有没做的
        // 语句 …），omc 那边要么求值成字面量、要么求值失败照留（C1 探针 `g(2)` → `2.0`、`g(-2)` 留调用）→ SKIP。
        {
          auto lit = [&](auto& self, Id<Expr> a) -> bool {
            const Expr& x = result_.model.exprs[a];
            if (x.kind != ExprKind::Array) return is_literal(a);
            for (Id<Expr> el : x.args) if (!self(self, el)) return false;
            return true;
          };
          bool all = true;
          for (Id<Expr> a : args) all = all && lit(lit, a);
          if (all)
            for (const FlatFunction& f : result_.model.functions)
              if (f.name == call_name && !f.is_record_ctor && !f.is_external && !f.is_impure)
                set_unsupported(subst_calls_.count(id.raw) ? "constant user call after structural substitution"
                                                           : "constant user call not evaluated");
        }

        const bool nofold = nofold_calls_.count(id.raw) > 0;
        auto rebuild = [&]() {
          Expr r; r.kind = ExprKind::Call; r.call_name = call_name; r.args = args; r.ty = ty;
          const Id<Expr> n = add(std::move(r));
          if (nofold) nofold_calls_.insert(n.raw);
          return n;
        };
        if (call_name.size() != 1 || nofold) return rebuild();
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
        // smooth(p, 字面量) → 该字面量（实参全是字面量的内建调用被求值，Ceval.evalBuiltinCall 的 smooth 支取第二个实参，
        // NFCeval.mo:1942 v1.25.0；ES 探针 `smooth(0, 2)` → `2.0`）。
        if (name == "smooth" && args.size() == 2) return is_literal(args[1]) ? args[1] : rebuild();
        // pre(字面量) → 该字面量（同上，Ceval 的 pre 支取实参 :1933）：实参是代入后的结构参数（ES 探针 e9 `pre(n)` → `2`）。
        if (name == "pre" && args.size() == 1 && is_literal(args[0])) return args[0];
        // semiLinear(x, m1, m2)（simplifySemiLinear，NFSimplifyExp.mo:283-303 v1.25.0）：x 为零或两个斜率都为零 → 0；两个斜率
        // 相同 → `x * m1`，造出来的积不再化简（ES 探针 `semiLinear(a, 1, 1)` → `a * 1.0`）；否则原样。实参全是字面量时走的是
        // Ceval（simplifyCall :187-196），而 evalBuiltinCall 里没有 semiLinear、求值失败 → 原样（`semiLinear(0.0, 2.0, 3.0)`）。
        if (name == "semiLinear" && args.size() == 3) {
          if (is_literal(args[0]) && is_literal(args[1]) && is_literal(args[2])) return rebuild();
          if (lit_is(args[0], 0) || (lit_is(args[1], 0) && lit_is(args[2], 0))) return lit_real(0.0);
          if (expr_equal(args[1], args[2])) {
            const Id<Expr> m = make_bin(args[0], BinOp::Mul, args[1], Type::real());
            nofold_bins_.insert(m.raw);
            return m;
          }
          return rebuild();
        }
        if (!bf->foldable) return rebuild();
        if (name == "Integer") {
          const Expr& ea = result_.model.exprs[args[0]];
          if (ea.kind == ExprKind::EnumLit && ea.enum_index) return lit_int(ea.enum_index);
          // 留成符号的枚举迭代变量（for 语句 / 保留的归约）：调用原样留着（EN 探针 t2 `z[Integer(i)] := y[i]`）。
          if (ea.kind == ExprKind::Cref && ea.ty.kind == TypeKind::Enumeration && ea.cref.parts.size() == 1 &&
              stmt_iters_.count(ea.cref.parts[0].name.id))
            return rebuild();
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
      case ExprKind::Range: {   // 起 / 步 / 止各自化简（ForLoopHideVariable `1:4 + 1` → `1:5`）
        Expr r = result_.model.exprs[id];
        const Id<Expr> a = simplify_expr(r.a), b = simplify_expr(r.b);
        const Id<Expr> c = r.c.is_valid() ? simplify_expr(r.c) : r.c;
        if (a == r.a && b == r.b && c == r.c) return id;
        r.a = a; r.b = b; r.c = c;
        return add(std::move(r));
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
  // 变率前缀可下沉到字段的记录（R1 探针：`parameter R r` 的数组 / 嵌套记录 / 继承 / final / 自带 parameter 的字段都按
  // variabilityMin 打前缀、final 保留）。operator record（含操作符 / 构造函数）未做 → false。
  bool is_plain_data_record(NodeId ci, Id<SElement> cdef) {
    (void)ci;
    const SElement& cd = I_.scode.elems[cdef];
    if (cd.restriction != SRestriction::Record || cd.def.kind != SClassDefKind::Parts) return false;
    for (Id<SElement> e : cd.def.elements)   // 嵌套的 operator / function（operator record）不在此列
      if (I_.scode.elems[e].kind == SElementKind::Class &&
          (I_.scode.elems[e].restriction == SRestriction::Operator || I_.scode.elems[e].restriction == SRestriction::Function))
        return false;
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
  // flatten 途中会往 Inst 的 absyn / scode arena 里合成节点（补字面量下标的 cref、拆出来的字段修饰…），而上层栈帧一路
  // 持有 `const Exp&` / `const Cref&` / `const SMod&`——arena 一扩容这些引用就悬垂（base/arena.h 文件头的纪律；ASan 实证：
  // `R r[400]; Real y[400] = r.x + r.x;` 在 binary() 里 heap-use-after-free）。
  // ponytail: 构造时给三个 arena 各预留 kSynthCap 个位置，合成只在预留内进行（不扩容 = 引用不失效），用完 → SKIP；
  // 要去掉这个上限得把 Arena 换成不搬家的容器（base/arena.h 归用户）。
  static constexpr std::size_t kSynthCap = std::size_t{1} << 16;
  std::size_t exp_limit_ = 0, cref_limit_ = 0, mod_limit_ = 0;   // 构造函数里定
  template <class T>
  Id<T> synth(Arena<T>& a, std::size_t limit, T v) {
    if (a.size() < limit) return a.add(std::move(v));
    set_unsupported("too many synthesized nodes");
    // 占位：本 arena 里最后一个合成出来的节点（预留用完 = 它存在）。不能随手给第 0 号——那可能正是触发这次合成的式子
    //（`r.x`），调用方接着解析它就又来合成，无穷递归（U4 探针栈溢出）；合成出来的节点都是已补全下标的，不会再触发。
    return Id<T>::from_index(static_cast<std::uint32_t>(limit - 1));
  }
  Id<Exp> synth_exp(Exp e) { return synth(I_.absyn.exps, exp_limit_, std::move(e)); }
  Id<Cref> synth_cref(Cref c) { return synth(I_.absyn.crefs, cref_limit_, std::move(c)); }
  Id<SMod> synth_mod(SMod m) { return synth(I_.scode.mods, mod_limit_, std::move(m)); }

  void set_unsupported(const char* why) {
    if (!result_.unsupported) { result_.unsupported = true; result_.unsupported_reason = why; }
  }
  // 用户函数调用没接住（落到"function: f" / "qualified function call" / "named function argument"）时的子原因：只进
  // FlattenResult::unsupported_detail（driver 打在 `[…]` 里，供 nf_live_sweep 排桶），主原因不变。一次调用里先到的为准。
  const char* fn_why_ = nullptr;
  template <class R>
  R fn_fail(const char* why) {
    if (!fn_why_) fn_why_ = why;
    return R{};
  }
  void set_unsupported_call(const char* why) {
    if (!result_.unsupported && fn_why_) result_.unsupported_detail = fn_why_;
    set_unsupported(why);
  }

  // 记录构造整体绑定 `R r = R(a, b, x=c)` = 类修饰 `R r(字段=实参…)`：把构造实参按声明序（位置实参）
  // 或名字（命名实参）匹配到字段，合成一个类修饰 SMod（缺省实参不进 subs → 该字段保留声明默认，RecordBinding1）。
  // 返回 invalid = 不适用/未支持（非构造调用、名字对不上、记录有 extends…），调用方据此仍走整体绑定 SKIP。
  // rec_const：记录组件（下沉后）是 constant——字段引用会折成字面量，与代入实参等价。
  Id<SMod> build_record_ctor_mod(Id<Exp> callraw, NodeId ci, Id<SElement> cdef, NodeId scope, bool rec_const) {
    if (I_.absyn.exps[callraw].kind == ExpKind::CrefExp) return split_record_cref_mod(callraw, ci, scope);
    const Exp& call = I_.absyn.exps[callraw];
    if (call.kind != ExpKind::Call) return {};
    if (call.partial_fn || call.call_suffix.is_valid()) return {};
    // 构造函数须解析到组件类型的同一记录类（返回记录的用户函数 → SKIP；同名异包记录 `Heating.Stage(…)` 绑给
    // `Cooling.Stage`：omc 按 Heating 的字段序 / 缺省构造后 typeCastRecord，RecordOrder3——只比末段名会错用目标类的缺省 → SKIP）。
    const Cref& fn = I_.absyn.crefs[call.cref];
    if (fn.parts.empty()) return {};
    std::vector<Symbol> fpath;
    for (const CrefPart& p : fn.parts) {
      if (!p.subscripts.empty()) return {};
      fpath.push_back(p.name);
    }
    ScodeScope fds;
    if (resolve_class_path(fpath, fn.leading_dot, scode_scope_of(scope), fds) != cdef) return {};
    // 记录含 extends（继承字段的构造序/约束）本桶未验证 → SKIP（RecordExtends）。
    for (Id<SElement> eid : I_.scode.elems[cdef].def.elements)
      if (I_.scode.elems[eid].kind == SElementKind::Extends) return {};
    // 构造函数的输入 = 非 protected、非 final 的字段（声明序）；protected / final 字段是构造函数局部，保留自身绑定
    //（NFRecord.collectRecordParam :260-283；RecordBinding6/7 `final constant Real y = 2.0` → `R(1.0, 3.0)` 给 x、z）。
    std::vector<Symbol> inputs;
    for (NodeId f : I_.trees[I_.node_class(ci).tree].components)
      if (I_.nodes[f].visibility != Visibility::Protected && !I_.comps[I_.nodes[f].comp].attributes.is_final)
        inputs.push_back(I_.nodes[f].name);
    std::vector<SSubMod> subs;
    std::size_t pos = 0;
    for (std::size_t i = 0; i < call.args.size(); ++i) {
      Symbol fld;
      if (i < call.arg_names.size() && call.arg_names[i].is_valid()) {
        fld = call.arg_names[i];                       // 命名实参 R(x=..)：须是输入（命名到局部 = omc 报错）
        if (std::find(inputs.begin(), inputs.end(), fld) == inputs.end()) return {};
      } else {                                          // 位置实参 → 第 pos 个输入（声明序）
        if (pos >= inputs.size()) return {};            // 实参多于输入 → 越界，交给上层 SKIP
        fld = inputs[pos++];
      }
      SMod fm;
      fm.kind = SModKind::Mod;
      fm.binding = call.args[i];
      fm.loc = call.loc;
      const Id<SMod> fid = synth_mod(std::move(fm));
      derived_mods_.insert(fid.raw);   // 记录构造绑定拆出的字段：不参与结构参数的别名连带
      subs.push_back({fld, fid});
    }
    // 没给实参的字段（有缺省的输入、protected / final 的局部量）取声明绑定。绑定里引用别的字段时，omc 代入的是那个字段的
    // **实参式**（构造函数的输入），不是记录组件的字段（RC 探针：`R t = R(a = p)`、`b = 2 * a` → `t.b = 2.0 * p`、final 的
    // `y = a / 2` → `t.y = p / 2.0`，都不是 `… t.a …`；实参全是常量的参数记录整体求值走了调用方的 eval_user_call，不到
    // 这里）。代入未做 → SKIP；constant 记录不受影响（字段引用折成字面量，RecordBinding6 / 7）。
    auto refs_field = [&](auto& self, Id<Exp> x) -> bool {
      if (!x.is_valid()) return false;
      const Exp& xe = I_.absyn.exps[x];
      if (xe.kind == ExpKind::CrefExp) {
        const Cref& c = I_.absyn.crefs[xe.cref];
        if (!c.leading_dot && !c.parts.empty())
          if (const NodeId n = find_local(ci, c.parts[0].name); n.is_valid() && !I_.nodes[n].is_empty() && I_.nodes[n].comp.is_valid())
            return true;
        for (const CrefPart& p : c.parts)
          for (Id<Exp> sx : p.subscripts) if (self(self, sx)) return true;
        return false;
      }
      if (self(self, xe.a) || self(self, xe.b) || self(self, xe.c)) return true;
      for (Id<Exp> a : xe.args) if (self(self, a)) return true;
      for (const ForIndex& it : xe.iters) if (self(self, it.range)) return true;
      return false;
    };
    for (NodeId f : I_.trees[I_.node_class(ci).tree].components) {
      if (rec_const) break;
      bool given = false;
      for (const SSubMod& sm : subs) given = given || sm.ident == I_.nodes[f].name;
      const Binding& fb = I_.comps[I_.nodes[f].comp].binding;
      if (!given && fb.kind == BindingKind::Raw && refs_field(refs_field, fb.raw_exp)) {
        set_unsupported("record constructor default depends on other fields");
        return {};
      }
    }
    SMod outer;
    outer.kind = SModKind::Mod;
    outer.subs = std::move(subs);
    outer.loc = call.loc;
    return synth_mod(std::move(outer));
  }

  // 记录 cref 整体绑定 `R r2 = r1`：splitRecordCref（NFFlatten.mo:1027-1053）把 cref 拆成各字段 cref 的记录式
  // `R(r1.x, r1.y)`，getRecordBindings（:877-918）再逐字段分发 = 类修饰 `r2(x = r1.x, y = r1.y)`。字段 = 目标记录类树的
  // 全部组件（含继承，声明序）。常量源的求值（evalExp，:958-961）由字段绑定里的常量 cref 折叠得到（RB1 探针 v1 = c1）。
  // 源须是本作用域可逐段找到的记录标量组件、且含目标的每个字段；否则（包常量 P.g、数组整体、缺字段）→ invalid → SKIP。
  Id<SMod> split_record_cref_mod(Id<Exp> craw, NodeId ci, NodeId scope) {
    const SourceLoc loc = I_.absyn.exps[craw].loc;
    const Cref src = I_.absyn.crefs[I_.absyn.exps[craw].cref];   // 拷贝（下面往 absyn arena add）
    auto local = [&](NodeId cls, Symbol name) {
      const NodeId n = find_local(cls, name);
      return n.is_valid() && !I_.nodes[n].is_empty() && I_.nodes[n].comp.is_valid() ? n : NodeId{};
    };
    if (src.leading_dot || src.parts.empty()) return {};
    NodeId cls = scope;
    for (const CrefPart& p : src.parts) {
      const NodeId n = local(cls, p.name);
      if (!n.is_valid()) return {};
      const CompId cid = I_.nodes[n].comp;
      const SElement& el = I_.scode.elems[I_.comps[cid].def];
      if (I_.comps[cid].ty.kind != TypeKind::Complex || p.subscripts.size() != el.attr.dims.size() + el.type.dims.size())
        return {};
      cls = I_.comps[cid].class_inst;
    }
    if (!I_.nodes[cls].def.is_valid() || I_.scode.elems[I_.nodes[cls].def].restriction != SRestriction::Record) return {};
    const std::vector<NodeId> fields = I_.trees[I_.node_class(ci).tree].components;
    std::vector<SSubMod> subs;
    for (NodeId f : fields) {
      const Symbol fname = I_.nodes[f].name;
      if (!local(cls, fname).is_valid()) return {};
      Cref fc = src;
      fc.parts.push_back({fname, {}});
      Exp fe;
      fe.kind = ExpKind::CrefExp;
      fe.loc = loc;
      fe.cref = synth_cref(std::move(fc));
      SMod fm;
      fm.kind = SModKind::Mod;
      fm.binding = synth_exp(std::move(fe));
      fm.loc = loc;
      const Id<SMod> fid = synth_mod(std::move(fm));
      derived_mods_.insert(fid.raw);   // 记录引用绑定拆出的字段：不参与结构参数的别名连带
      subs.push_back({fname, fid});
    }
    SMod outer;
    outer.kind = SModKind::Mod;
    outer.subs = std::move(subs);
    outer.loc = loc;
    return synth_mod(std::move(outer));
  }

  // 记录整体绑定是 if 式且条件 ≤ parameter：splitRecordCref 的 IF 分支（NFFlatten.mo:1061-1074）——条件求值成功即标结构参数
  // （帧求值读参数即记）、选支再拆（RE1 探针 `p3 = if k > 3 then P(1, 2) else P(a = k)` → k final、`p3.a = 2.0`）；条件引用运行时
  // 变量 → 原样（随后挪成方程）。条件参数须字面量绑定 / fixed（传递标记依赖 omc 求值次序，同 if 方程，见 cond_params_fixed），
  // 否则 / 求不出 → SKIP。返回选中的分支（可再嵌套）。
  Id<Exp> fold_record_if(Id<Exp> raw, NodeId scope, const std::vector<Symbol>& vpfx) {
    const PhaseScope late(*this, MarkPhase::Late);   // 压平期的标记（splitRecordCref）
    const FrameAt frame_at(*this, scope);
    while (I_.absyn.exps[raw].kind == ExpKind::IfExp) {
      const Exp ie = I_.absyn.exps[raw];   // 拷贝
      const StructSnap snap = struct_snap();
      const Id<Expr> ce = simplify_expr(inst_exp(ie.a, scope, vpfx));
      if (result_.unsupported) return raw;
      if (cond_is_runtime(ce)) { struct_restore(snap); return raw; }
      const std::optional<CVal> cv = eval_scode(ie.a, scode_scope_of(scope), 0, /*allow_param=*/true, nullptr);
      const std::optional<bool> fx = cond_params_fixed(ce);
      if (!cv || cv->k != CVal::K::Bool || !fx || !*fx) {
        struct_restore(snap);
        set_unsupported("record binding if condition");
        return raw;
      }
      raw = cv->b ? ie.b : ie.c;
    }
    return raw;
  }

  // 拆不开的记录整体绑定 → 方程 `name = 绑定`（flattenComplexComponent，NFFlatten.mo:994-1004：变率 ≤ parameter 进 initial
  // equation）。绑定须定型成与组件同一记录类（或同名同序兼容）的标量记录式；constant（omc 要求能求值）/ 类型不符 → false。
  bool record_binding_equation(CompId cid, const std::vector<Symbol>& name, const PSubs& psubs, Id<Exp> raw,
                               NodeId bscope, const std::vector<Symbol>& bprefix, const ModCtx& ctx, bool is_final) {
    const SVariability v = var_min(ctx.var_floor, I_.comps[cid].attributes.variability);
    if (v == SVariability::Const) return false;
    cur_psubs_ = psubs;
    const Id<Expr> rhs = simplify_expr(inst_exp(raw, bscope, bprefix));
    if (result_.unsupported) return false;
    // 绑定变率 ≤ 结构参数（全常量）→ omc Ceval 求值（NFFlatten.mo:958-961）；final parameter 且绑定是参数变率 → 先内联
    // 再求值（:962-983，RecordBinding18 账本）。这两种 ymc 都不复刻（不挪方程）→ false。
    const bool runtime = refs_runtime_var(rhs);
    if (!runtime && (!refs_param_var(rhs) || is_final)) return false;
    const Type cty = I_.comps[cid].ty;
    if (result_.model.exprs[rhs].ty.kind != TypeKind::Complex || !expr_shape(rhs).empty() ||
        record_match(result_.model.exprs[rhs].ty, cty) > RecMatch::Plug)
      return false;
    Expr lhs;
    lhs.kind = ExprKind::Cref;
    lhs.ty = cty;
    const std::vector<std::vector<Id<Expr>>> ps = psubs_to_exprs(psubs);
    for (std::size_t k = 0; k < name.size(); ++k)
      lhs.cref.parts.push_back({name[k], k < ps.size() ? ps[k] : std::vector<Id<Expr>>{}});
    FlatEquation fe;
    fe.kind = FlatEqKind::Equality;
    fe.a = add(std::move(lhs));
    fe.b = rhs;
    fe.loc = I_.comps[cid].loc;
    const Id<FlatEquation> id = result_.model.add_eq(std::move(fe));
    if (v == SVariability::Param) result_.model.initial_equations.push_back(id);
    else result_.model.equations.push_back(id);
    return true;
  }

  // 字段修饰去掉各子修饰的绑定（整体记录绑定下字段绑定由整体绑定给），留属性 / 嵌套修饰；去完为空 → invalid。
  // final / each 子修饰带绑定（盖掉后 final 是否留在字段上未核实）→ ok = false。
  Id<SMod> strip_field_bindings(Id<SMod> m, bool& ok) {
    const std::vector<SSubMod> subs = I_.scode.mods[m].subs;   // 拷贝（下面 add_mod 会 realloc mods arena）
    std::vector<SSubMod> out;
    for (const SSubMod& s : subs) {
      SMod sm = I_.scode.mods[s.mod];
      if (sm.binding.is_valid()) {
        if (sm.is_final || sm.is_each || sm.kind != SModKind::Mod) { ok = false; return {}; }
        sm.binding = {};
      }
      if (sm.subs.empty() && sm.kind == SModKind::Mod) continue;
      out.push_back({s.ident, synth_mod(std::move(sm))});
    }
    if (out.empty()) return {};
    SMod n = I_.scode.mods[m];
    n.binding = {};
    n.subs = std::move(out);
    return synth_mod(std::move(n));
  }

  // 复合数组第 aidx 个元素的合成修饰（Modifier.propagate → propagateSubMod，NFModifier.mo:444-513）：每个非 each 子修饰的
  // 绑定按 aidx 逐维下降（`x={1,2,3}` 的元素 i → `x=vi`），并递归到更深的子修饰（`x(start={{…},…})` → `x(start={…})`，余下
  // 的维由 x 自己切分，BuiltinAttribute10）；each 子修饰整棵原样（每元素同值）。下降不了 → 返回 invalid。
  Id<SMod> distribute_mod(Id<SMod> m, const std::vector<std::int64_t>& aidx) {
    const std::vector<SSubMod> subs = I_.scode.mods[m].subs;   // 拷贝（下面 add_mod 会 realloc mods arena）
    const SourceLoc mloc = I_.scode.mods[m].loc;
    std::vector<SSubMod> out;
    for (const SSubMod& s : subs) {
      const Id<SMod> d = split_submod(s.mod, aidx);
      if (!d.is_valid()) return {};
      out.push_back({s.ident, d});
    }
    SMod outer;
    outer.kind = SModKind::Mod;
    outer.subs = std::move(out);
    outer.loc = mloc;
    return synth_mod(std::move(outer));
  }
  Id<SMod> split_submod(Id<SMod> sm, const std::vector<std::int64_t>& aidx) {
    SMod n = I_.scode.mods[sm];   // 拷贝
    if (n.kind != SModKind::Mod) return {};
    if (n.is_each) return sm;   // propagateSubMod 的 else ()：each 整棵跳过
    if (n.binding.is_valid()) {
      n.binding = array_scalar_elem(n.binding, aidx, 0);
      if (!n.binding.is_valid()) return {};
    }
    const std::vector<SSubMod> subs = std::move(n.subs);
    n.subs.clear();
    for (const SSubMod& s : subs) {
      const Id<SMod> d = split_submod(s.mod, aidx);
      if (!d.is_valid()) return {};
      n.subs.push_back({s.ident, d});
    }
    return synth_mod(std::move(n));
  }
  // 复合数组修饰切分前的整体检查（各将被下降的非 each 字面量绑定）：前 N 维须恰是数组维（多 / 少 = omc 维度不符报错）；
  // 数值目标的叶类型须一致（split_leaves_uniform）。子修饰名在 cls（类实例）的组件里查；cls 无效 = 内建类型 bkind 的属性。
  bool split_mod_ok(Id<SMod> m, NodeId cls, TypeKind bkind, const std::vector<std::int64_t>& adims, NodeId scope,
                    const std::vector<Symbol>& prefix) {
    const std::vector<SSubMod> subs = I_.scode.mods[m].subs;   // 拷贝（inst 可能扩 arena）
    for (const SSubMod& s : subs) {
      const SMod sub = I_.scode.mods[s.mod];
      if (sub.kind != SModKind::Mod || sub.is_each) continue;
      // 目标：组件（其元素类型；复合 → 子修饰再进它的类）或内建属性。
      bool numeric = false;
      NodeId sub_cls;
      TypeKind sub_kind = TypeKind::Unknown;
      if (cls.is_valid()) {
        for (NodeId n : I_.trees[I_.node_class(cls).tree].components)
          if (I_.nodes[n].kind == NodeKind::Component && I_.nodes[n].name == s.ident) {
            const Component& fc = I_.comps[I_.nodes[n].comp];
            sub_kind = fc.ty.kind;
            if (sub_kind == TypeKind::Complex) sub_cls = fc.class_inst;
            break;
          }
        numeric = sub_kind == TypeKind::Real || sub_kind == TypeKind::Integer;
      } else {
        for (const AttrSpec& a : attr_table(bkind))
          if (s.ident == in_.intern(a.name)) numeric = a.vk == AttrVK::Real || a.vk == AttrVK::Int;
      }
      const Id<Exp> b = sub.binding;
      if (b.is_valid() && ((is_array_ctor(b) && !is_comprehension(b)) || I_.absyn.exps[b].kind == ExpKind::Matrix)) {
        const std::optional<std::vector<std::int64_t>> ld = lit_dims(b);
        if (!ld || ld->size() < adims.size() || !std::equal(adims.begin(), adims.end(), ld->begin())) return false;
        if (numeric && I_.absyn.exps[b].kind != ExpKind::Matrix && !split_leaves_uniform(b, scope, prefix)) return false;
      }
      if (cls.is_valid() && !split_mod_ok(s.mod, sub_cls, sub_kind, adims, scope, prefix)) return false;
    }
    return true;
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
          k.push_back('[');
          if (const std::optional<std::int64_t> o = sub_ordinal(s)) k += std::to_string(*o);   // 标签下标按序数（见 sub_ordinal）
          else k += "#" + std::to_string(s.raw);   // 符号下标——Id 保唯一
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
  bool member_removed(const ConnInfo& c, const std::vector<Symbol>& mpath) {
    if (removed_.empty()) return false;
    std::string k;
    for (std::size_t i = 0; i < c.path.size(); ++i) {
      const std::string base = k + std::to_string(c.path[i].id);
      if (removed_.count(base + '.')) return true;   // 整个（数组）组件被移除
      k = conn_key({c.path.begin(), c.path.begin() + static_cast<std::ptrdiff_t>(i) + 1},
                   {c.subs.begin(), c.subs.begin() + static_cast<std::ptrdiff_t>(std::min(i + 1, c.subs.size()))});
      if (removed_.count(k)) return true;
    }
    for (Symbol s : mpath) {
      k += std::to_string(s.id);
      k.push_back('.');
      if (removed_.count(k)) return true;
    }
    return false;
  }
  // 书写处前缀（cur_psubs_ 逐段下标）++ cref 逐段（字面量 / for 迭代变量下标）的各前缀是否被移除；某段下标求不出 →
  // 只按无下标形态查（数组组件整体移除）。
  bool cref_removed(const std::vector<Symbol>& prefix, const Cref& c) {
    if (removed_.empty()) return false;
    std::vector<Symbol> names = prefix;
    PSubs isubs = cur_psubs_;
    isubs.resize(prefix.size());
    for (const CrefPart& part : c.parts) {
      names.push_back(part.name);
      isubs.push_back({});
      if (removed_.count(int_key(names, isubs))) return true;
      std::vector<std::int64_t> is;
      for (Id<Exp> sx : part.subscripts) {
        const std::optional<std::int64_t> v = subscript_int(sx);
        if (!v) return false;
        is.push_back(*v);
      }
      isubs.back() = std::move(is);
      if (!part.subscripts.empty() && removed_.count(int_key(names, isubs))) return true;
    }
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
  struct ConnectStmt;   // 定义在类尾（攒下的 connect）
  bool references_removed(const ConnectStmt& cs, Id<Exp> ce) {
    if (I_.absyn.exps[ce].kind != ExpKind::CrefExp) return false;
    const Cref& c = I_.absyn.crefs[I_.absyn.exps[ce].cref];
    const PSubs saved_psubs = cur_psubs_;
    const auto saved_loops = loop_vars_;
    cur_psubs_ = cs.psubs;
    loop_vars_ = cs.loopvars;
    const bool r = cref_removed(cs.prefix, c);
    cur_psubs_ = saved_psubs;
    loop_vars_ = saved_loops;
    return r;
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
      if (!references_removed(cs, cs.a) && !references_removed(cs, cs.b)) live.push_back(cs);
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
        if (member_removed(r.a, m.path) || member_removed(r.b, m.path)) continue;
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
      if (it != vals.end()) return it->second;
      bool sub_changed = false;   // 下标里的结构参数（`z[n]` → `z[5]`）
      for (ExprCrefPart& cp : c.cref.parts)
        for (Id<Expr>& sx : cp.subscripts) {
          const Id<Expr> n = clone_subst(sx, vals);
          sub_changed = sub_changed || !(n == sx);
          sx = n;
        }
      return sub_changed ? add(std::move(c)) : id;
    }
    // Clock 构造的实参不代入（omc 的 EvalConstants 不进 CLKCONST：n 是结构参数时仍打 `Clock(n, 1)`，SY 探针 s4）。
    if (is_clock_ctor(c)) return id;
    bool changed = false;
    for (Id<Expr>& k : c.args) { const Id<Expr> n = clone_subst(k, vals); changed = changed || !(n == k); k = n; }
    for (Id<Expr>* k : {&c.a, &c.b, &c.c}) {
      if (!k->is_valid()) continue;
      const Id<Expr> n = clone_subst(*k, vals);
      changed = changed || !(n == *k);
      *k = n;
    }
    // 代换改到了 sum / product 元素链里：克隆出的链失去"不再化简"标记，代换后的形态未核实 → SKIP。
    if (changed && c.kind == ExprKind::Binary && nofold_bins_.count(id.raw)) set_unsupported("substitution into sum/product chain");
    if (!changed) return id;
    const bool is_call = c.kind == ExprKind::Call;
    const Id<Expr> n = add(std::move(c));
    if (is_call) subst_calls_.insert(n.raw);   // 实参被代入过的调用：化简时判"实参全成字面量的用户函数"（见 simplify_expr）
    // "不折叠"标记跟着克隆走（FE6 探针：external "C" 的 `g(n)`、n 是结构参数 → omc `sin(2.0)`，不求值）。
    if (is_call && nofold_calls_.count(id.raw)) nofold_calls_.insert(n.raw);
    if (scalarized_logicals_.count(id.raw)) scalarized_logicals_.insert(n.raw);
    return n;
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
  //
  // 标记带"时刻"（1.25.0 的标记分三个阶段，后两个阶段的效果取决于次序；AO 探针 a1–a3，源码按 v1.25.0 标签）：
  //   Early   实例化期（NFInst.updateImplicitVariability :3871-3985：数组维、Evaluate = true、条件组件的条件、fixed 属性、绑定
  //           里数组构造子的迭代范围）与组件定型期（`:` 维从绑定推断时对绑定的定型，NFTyping.typeDimension :743）——都在
  //           任何绑定定型之前，时刻 0；
  //   Binding 绑定 / 属性的定型期（typeBindings 按声明序，NFTyping.mo:945-1120：参数下标 :2094-2098、参数范围 :2414-2418、
  //           fill 类的维实参 NFBuiltinCall.mo:974…），时刻 = 当前变量的定型序号 bind_clock_；
  //   Late    方程 / 算法段的定型（在全部绑定之后）与压平期（if 表达式选支 NFFlatten.mo:1736、if 方程条件 :1984、for 范围
  //           :2086、stream 属性 NFConnectEquations.mo:960），时刻 INT_MAX。
  // 时刻用在两处：别名连带（apply_structural_params）与依赖连带（frame_value）。同一参数多次标记取最早的。
  enum class MarkPhase : std::uint8_t { Early, Binding, Late };
  struct PhaseScope {
    MarkPhase& ref;
    MarkPhase saved;
    PhaseScope(Flattener& f, MarkPhase p) : ref(f.mark_phase_), saved(f.mark_phase_) { ref = p; }
    ~PhaseScope() { ref = saved; }
    PhaseScope(const PhaseScope&) = delete;
    PhaseScope& operator=(const PhaseScope&) = delete;
  };
  int mark_time() const {
    return mark_phase_ == MarkPhase::Early ? 0 : mark_phase_ == MarkPhase::Binding ? bind_clock_ : INT_MAX;
  }
  void record_structural(const std::string& k) {
    const int t = mark_time();
    const auto it = structural_set_.find(k);
    if (it == structural_set_.end()) {
      txn_touch();
      structural_set_.emplace(k, t);
      structural_keys_.push_back(k);
    } else if (t < it->second) {
      txn_touch();
      it->second = t;
    }
  }
  // 试探性求值前后的快照 / 撤销（求值会打结构标记，omc 的 tryEvalExp 之类不标）。
  struct StructSnap {
    std::vector<std::string> keys;
    std::unordered_map<std::string, int> set, deps;
  };
  StructSnap struct_snap() const { return {structural_keys_, structural_set_, deps_time_}; }
  // 求值事务（见 eval_scode）：标记状态要变之前，给还没拍快照的各层事务拍一张（内层在前；外层已有快照的更外层必也已有）。
  struct MarkTxn {
    std::optional<StructSnap> snap;
    MarkTxn* outer = nullptr;
  };
  void txn_touch() {
    for (MarkTxn* t = txn_; t && !t->snap; t = t->outer) t->snap = struct_snap();
  }
  void struct_restore(StructSnap s) {
    structural_keys_ = std::move(s.keys);
    structural_set_ = std::move(s.set);
    deps_time_ = std::move(s.deps);
  }
  // 隐式迭代范围（`for i loop …`、`sum(x[i] for i)`、`{x[i] for i}`）——deduceIterationRange（NFTyping.mo:3735-3881 v1.25.0）。
  // iter_uses 收集体里迭代变量 it **恰**作下标的各处 (cref, 段, 第几个下标)，次序同 omc：语句 / 方程按 Statement.foldExp /
  // Equation.foldExp（NFStatement.mo:727-800、NFEquation.mo:897-990：赋值先左后右，for 先体后范围，if / when 逐支先条件
  // 后体），表达式按 Expression.fold（NFExpression.mo:3274-3381：后序、子式自左向右），一个 cref 先各段下标里的子式
  //（ComponentRef.foldExp NFComponentRef.mo:2083-2103，末段在前）再它自己的各处（collectIteratorCrefs2 :3814-3847，末段在前）。
  struct IterUse {
    Cref c;                  // Absyn cref 的拷贝
    std::size_t part, sub;   // 第 part 段的第 sub 个下标
  };
  struct IterUses {
    std::vector<IterUse> v;
    bool ok = true;          // false = 碰到未核实的形态（同名迭代变量遮蔽）→ 不推
  };
  // 书写式里有 `ndims(…)` 或单实参的 `size(…)`：这两种调用在 omc 里总是参数变率（typeNdimsCall，NFBuiltinCall.mo:487-514；
  // typeSize 的无维号支返回 Variability.PARAMETER，NFTyping.mo，v1.25.0）——哪怕实参是字面量。不看是否被同名用户函数遮蔽。
  bool absyn_param_size_call(Id<Exp> id) const {
    if (!id.is_valid()) return false;
    const Exp& e = I_.absyn.exps[id];
    if (e.kind == ExpKind::Call && e.args.size() == 1) {
      const Cref& fn = I_.absyn.crefs[e.cref];
      if (!fn.leading_dot && fn.parts.size() == 1 && (in_.str(fn.parts[0].name) == "ndims" || in_.str(fn.parts[0].name) == "size"))
        return true;
    }
    if (e.kind == ExpKind::CrefExp)
      for (const CrefPart& cp : I_.absyn.crefs[e.cref].parts)
        for (Id<Exp> sx : cp.subscripts) if (absyn_param_size_call(sx)) return true;
    for (Id<Exp> a : e.args) if (absyn_param_size_call(a)) return true;
    for (const ForIndex& it : e.iters) if (absyn_param_size_call(it.range)) return true;
    return absyn_param_size_call(e.a) || absyn_param_size_call(e.b) || absyn_param_size_call(e.c);
  }
  // 两个书写表达式结构相同（同 kind、同字面量 / 运算符 / 名字，子式递归相同）。括号（1 元 Tuple）不看穿、Real 字面量按拼写比。
  bool absyn_equal(Id<Exp> a, Id<Exp> b) const {
    if (a == b) return true;
    if (!a.is_valid() || !b.is_valid()) return false;
    const Exp& x = I_.absyn.exps[a];
    const Exp& y = I_.absyn.exps[b];
    if (x.kind != y.kind || x.ival != y.ival || x.bval != y.bval || x.sval != y.sval || x.op != y.op ||
        x.partial_fn != y.partial_fn || x.args.size() != y.args.size() || x.arg_names != y.arg_names ||
        x.iters.size() != y.iters.size() || x.cref.is_valid() != y.cref.is_valid() ||
        x.call_suffix.is_valid() || y.call_suffix.is_valid())
      return false;
    if (x.cref.is_valid()) {
      const Cref& p = I_.absyn.crefs[x.cref];
      const Cref& q = I_.absyn.crefs[y.cref];
      if (p.leading_dot != q.leading_dot || p.parts.size() != q.parts.size()) return false;
      for (std::size_t i = 0; i < p.parts.size(); ++i) {
        if (p.parts[i].name != q.parts[i].name || p.parts[i].subscripts.size() != q.parts[i].subscripts.size()) return false;
        for (std::size_t k = 0; k < p.parts[i].subscripts.size(); ++k)
          if (!absyn_equal(p.parts[i].subscripts[k], q.parts[i].subscripts[k])) return false;
      }
    }
    for (std::size_t i = 0; i < x.args.size(); ++i) if (!absyn_equal(x.args[i], y.args[i])) return false;
    for (std::size_t i = 0; i < x.iters.size(); ++i)
      if (x.iters[i].name != y.iters[i].name || !absyn_equal(x.iters[i].range, y.iters[i].range)) return false;
    return absyn_equal(x.a, y.a) && absyn_equal(x.b, y.b) && absyn_equal(x.c, y.c);
  }
  bool is_plain_iter(Id<Exp> s, Symbol it) const {
    const Exp& x = I_.absyn.exps[s];
    if (x.kind != ExpKind::CrefExp) return false;
    const Cref& c = I_.absyn.crefs[x.cref];
    return !c.leading_dot && c.parts.size() == 1 && c.parts[0].subscripts.empty() && c.parts[0].name == it;
  }
  void iter_uses(Id<Exp> e, Symbol it, IterUses& u) {
    if (!e.is_valid() || !u.ok) return;
    const Exp x = I_.absyn.exps[e];   // 拷贝
    if (x.kind == ExpKind::CrefExp) {
      const Cref c = I_.absyn.crefs[x.cref];
      for (std::size_t pi = c.parts.size(); pi-- > 0;)
        for (Id<Exp> sx : c.parts[pi].subscripts) if (!is_plain_iter(sx, it)) iter_uses(sx, it, u);
      for (std::size_t pi = c.parts.size(); pi-- > 0;)
        for (std::size_t k = 0; k < c.parts[pi].subscripts.size(); ++k)
          if (is_plain_iter(c.parts[pi].subscripts[k], it)) u.v.push_back({c, pi, k});
      return;
    }
    if (x.kind == ExpKind::Reduction) {
      // 嵌套的归约 / 推导式：体在前、各迭代范围在后（Call.foldExp，NFCall.mo:1622-1697）。它自己的同名迭代变量遮蔽外层
      //（里面的不是我们的迭代变量）→ 未核实，不推。
      for (const ForIndex& f : x.iters) if (f.name == it) { u.ok = false; return; }
      iter_uses(x.a, it, u);
      for (const ForIndex& f : x.iters) iter_uses(f.range, it, u);
      return;
    }
    iter_uses(x.a, it, u);
    iter_uses(x.b, it, u);
    iter_uses(x.c, it, u);
    for (Id<Exp> a : x.args) iter_uses(a, it, u);
  }
  void iter_uses(const std::vector<Id<SStatement>>& body, Symbol it, IterUses& u) {
    for (Id<SStatement> id : body) {
      if (!u.ok) return;
      const SStatement st = I_.scode.stmts[id];   // 拷贝
      if (st.kind == SStmtKind::For) {
        // 内层同名迭代变量遮蔽：体里的 i 是内层的（另一个节点，Subscript.equalsIterator 按节点比，NFSubscript.mo:281-297），
        // 不算；只有它的范围还在外层作用域里（IR 探针 i3：`for i loop v[i] := u[i]; for i loop … end for; end for`）。
        if (st.index != it) iter_uses(st.body, it, u);
        iter_uses(st.a, it, u);
        continue;
      }
      iter_uses(st.a, it, u);
      iter_uses(st.b, it, u);
      iter_uses(st.c, it, u);
      for (const SStmtBranch& br : st.branches) {
        iter_uses(br.cond, it, u);
        iter_uses(br.body, it, u);
      }
      iter_uses(st.body, it, u);        // while 的体
      iter_uses(st.else_body, it, u);
    }
  }
  void iter_uses(const std::vector<Id<SEquation>>& body, Symbol it, IterUses& u) {
    for (Id<SEquation> id : body) {
      if (!u.ok) return;
      const SEquation q = I_.scode.eqs[id];   // 拷贝
      if (q.kind == SEqKind::For) {
        if (q.index != it) iter_uses(q.body, it, u);   // 同名遮蔽：同语句
        iter_uses(q.a, it, u);
        continue;
      }
      iter_uses(q.a, it, u);
      iter_uses(q.b, it, u);
      iter_uses(q.c, it, u);
      for (const SEqBranch& br : q.branches) {
        iter_uses(br.cond, it, u);
        iter_uses(br.body, it, u);
      }
      iter_uses(q.else_body, it, u);
    }
  }
  // 模型里一处用处的维长：逐段沿实例类树找组件，求它的声明维（单段本地引用的 `:` 维取已发射的形状，leaf_decl_dims）。
  // 求不出（找不到组件 / 维不是整数：类型名维 `Real x[Boolean]` 等）→ nullopt。
  std::optional<std::int64_t> iter_use_size(const IterUse& x, NodeId enclosing, const std::vector<Symbol>& prefix,
                                            std::vector<Id<Expr>>* labels = nullptr, Id<Exp>* dim_exp = nullptr) {
    if (x.c.leading_dot) return std::nullopt;
    NodeId owner = enclosing, cls = enclosing, node;
    for (std::size_t j = 0; j <= x.part; ++j) {
      owner = cls;
      node = find_local(cls, x.c.parts[j].name);
      if (!node.is_valid() || I_.nodes[node].is_empty() || !I_.nodes[node].comp.is_valid()) return std::nullopt;
      cls = I_.comps[I_.nodes[node].comp].class_inst;
      if (j < x.part && (!cls.is_valid() || I_.nodes[cls].is_empty())) return std::nullopt;
    }
    const SElement& el = I_.scode.elems[I_.comps[I_.nodes[node].comp].def];
    std::vector<std::int64_t> dims;
    if (x.c.parts.size() == 1) {
      std::vector<std::vector<Id<Expr>>> ll;
      if (!leaf_decl_dims(el, x.c, enclosing, prefix, dims, &ll)) return std::nullopt;
      // 类型维：回报标签与书写的维表达式（隐式范围要合成 `E.one:E.three` / `false:true`）。
      if (x.sub < ll.size() && !ll[x.sub].empty()) {
        if (labels) *labels = ll[x.sub];
        if (dim_exp) *dim_exp = x.sub < el.attr.dims.size() ? el.attr.dims[x.sub] : el.type.dims[x.sub - el.attr.dims.size()];
      }
    } else {
      const ScodeScope sc = scode_scope_of(owner);
      for (const std::vector<Id<Exp>>* ds : {&el.attr.dims, &el.type.dims})
        for (Id<Exp> d : *ds) if (!eval_dim(d, dims, sc)) return std::nullopt;
    }
    if (x.sub >= dims.size() || dims[x.sub] < 0) return std::nullopt;
    return dims[x.sub];
  }
  // 由收集到的用处推范围：相邻两处的维须相等（deduceIterationRange2 → Dimension.isEqualKnownSize，NFDimension.mo:311-338），
  // 范围 = `1:末`，末取自**最先**收集到的那处（用处是往表头加的，List.reduce 留下表尾）：整数维 → 字面量；函数里的 `:`
  // 维 → `size(该数组, 维号)`；函数里的表达式维 → 该维表达式（Dimension.endExp :483-509；IR 探针 `Real w[k]` → `for i in 1:k`、
  // `output Real v[size(u, 1)]` → `1:size(u, 1)`）。返回合成的 Absyn 范围；一处都没有 / 维不等（omc 报错）、未做 → invalid。
  Id<Exp> implicit_range(const IterUses& u, NodeId enclosing, const std::vector<Symbol>& prefix) {
    if (!u.ok || u.v.empty()) return {};
    auto int_lit = [&](std::int64_t n) {
      Exp x;
      x.kind = ExpKind::IntLit;
      x.ival = n;
      return synth_exp(std::move(x));
    };
    auto one_to = [&](Id<Exp> stop) {
      Exp r;
      r.kind = ExpKind::Range;
      r.a = int_lit(1);
      r.b = stop;
      return synth_exp(std::move(r));
    };
    if (!fn_scope_) {
      // 模型里各维都已求成整数：全部相等 → `1:n`。首个用处的维是类型维（`Real y[E]`、`y[Boolean]`）→ 范围是该类型的
      // 首末值 `E.one:E.three`、`false:true`（Dimension.lowerBoundExp / endExp 的 BOOLEAN / ENUM 支，NFDimension.mo:483-539；
      // IR 探针 e1）；别的用处长度相等、类型不同的，展开时下标类型对不上 → omc 也报错。
      std::optional<std::int64_t> n;
      for (const IterUse& x : u.v) {
        const std::optional<std::int64_t> m = iter_use_size(x, enclosing, prefix);
        if (!m || (n && *n != *m)) return {};
        n = m;
      }
      std::vector<Id<Expr>> fl;
      Id<Exp> fdim;
      (void)iter_use_size(u.v.front(), enclosing, prefix, &fl, &fdim);
      if (fl.empty()) return one_to(int_lit(*n));
      Exp lo, hi;
      if (result_.model.exprs[fl[0]].kind == ExprKind::BoolLit) {
        lo.kind = hi.kind = ExpKind::BoolLit;
        hi.bval = true;
      } else {
        // 枚举：维写的类型名 + 首 / 末字面量名。
        if (!fdim.is_valid() || I_.absyn.exps[fdim].kind != ExpKind::CrefExp) return {};
        Cref a = I_.absyn.crefs[I_.absyn.exps[fdim].cref];   // 拷贝
        Cref b = a;
        a.parts.push_back({result_.model.exprs[fl.front()].enum_path.back(), {}});
        b.parts.push_back({result_.model.exprs[fl.back()].enum_path.back(), {}});
        lo.kind = hi.kind = ExpKind::CrefExp;
        lo.cref = synth_cref(std::move(a));
        hi.cref = synth_cref(std::move(b));
      }
      Exp r;
      r.kind = ExpKind::Range;
      r.a = synth_exp(std::move(lo));
      r.b = synth_exp(std::move(hi));
      return synth_exp(std::move(r));
    }
    // 函数体里：只认单段的形参 / 局部量引用（记录字段数组未做）。维的种类按**书写**的维分（omc 比的是定型后、未化简的
    // 维）：`:` → 未知；不引用函数局部量的 → 整数（已求值）；其余 → 表达式维。化简后才成整数 / 才相等的（`Real w2[k + 0]`
    // 对 `Real w[k]`，IR 探针 i3：omc 报错）不算相等。
    enum class DK : std::uint8_t { Unknown, Integer, Exp };
    struct D {
      Symbol name;
      std::size_t k;
      DK kind;
      std::int64_t size;
      Id<Exp> raw;   // 该维的书写表达式
    };
    std::vector<D> ds;
    for (const IterUse& x : u.v) {
      if (x.c.leading_dot || x.c.parts.size() != 1 || stmt_iters_.count(x.c.parts[0].name.id)) return {};
      const auto p = func_params_.find(x.c.parts[0].name.id);
      const auto de = fn_dim_exps_.find(x.c.parts[0].name.id);
      if (p == func_params_.end() || de == fn_dim_exps_.end() || x.sub >= p->second.dims.size() ||
          de->second.size() != p->second.dims.size())
        return {};
      const Dimension& d = p->second.dims[x.sub];
      const Id<Exp> raw = de->second[x.sub];
      D r{x.c.parts[0].name, x.sub, DK::Unknown, 0, raw};
      if (d.kind == DimKind::Integer && !exp_refs_fn_locals(raw)) { r.kind = DK::Integer; r.size = d.size; }
      else if (d.kind == DimKind::Exp && exp_refs_fn_locals(raw)) r.kind = DK::Exp;
      else if (d.kind != DimKind::Unknown) return {};   // 化简后才成整数的表达式维等：未核实
      ds.push_back(r);
    }
    auto size_of = [&](const D& d, const D& o) {   // d 的维恰写成 `size(o, 维号)`（Dimension.isSizeOf :340-356）
      if (d.kind != DK::Exp) return false;
      const Exp& sx = I_.absyn.exps[d.raw];
      if (sx.kind != ExpKind::Call || sx.args.size() != 2) return false;
      for (Symbol n : sx.arg_names) if (n.is_valid()) return false;
      const Cref& fc = I_.absyn.crefs[sx.cref];
      if (fc.leading_dot || fc.parts.size() != 1 || in_.str(fc.parts[0].name) != "size") return false;
      const Exp& k = I_.absyn.exps[sx.args[1]];
      return is_plain_iter(sx.args[0], o.name) && k.kind == ExpKind::IntLit && k.ival == static_cast<std::int64_t>(o.k) + 1;
    };
    for (std::size_t j = 1; j < ds.size(); ++j) {
      const D& p = ds[j];
      const D& q = ds[j - 1];
      if (p.name == q.name && p.k == q.k) continue;       // 同一个数组的同一维：不比
      if (size_of(p, q) || size_of(q, p)) continue;
      if (p.kind == DK::Exp && q.kind == DK::Exp && absyn_equal(p.raw, q.raw)) continue;
      if (p.kind == DK::Integer && q.kind == DK::Integer && p.size == q.size) continue;
      return {};   // 未知维 / 长度不等 → INCOMPATIBLE_IMPLICIT_RANGES；整数维对表达式维 → Dimension.size 失败
    }
    const D& f = ds.front();
    if (f.kind == DK::Integer) return one_to(int_lit(f.size));
    if (f.kind == DK::Exp) return one_to(f.raw);
    Cref arr;
    arr.parts.push_back({f.name, {}});
    Exp ae;
    ae.kind = ExpKind::CrefExp;
    ae.cref = synth_cref(std::move(arr));
    Cref fn;
    fn.parts.push_back({in_.intern("size"), {}});
    Exp call;
    call.kind = ExpKind::Call;
    call.cref = synth_cref(std::move(fn));
    call.args = {synth_exp(std::move(ae)), int_lit(static_cast<std::int64_t>(f.k) + 1)};
    call.arg_names.assign(2, Symbol{});
    return one_to(synth_exp(std::move(call)));
  }
  // 归约 / 推导式（Reduction 节点的**拷贝**——同一个 Absyn 节点在不同实例里推出的范围可以不同，不能改 arena 里的）里没写
  // 范围的迭代变量补上推出的范围（typeReduction / typeArrayConstructor 对整个调用收集：体在前、各迭代范围在后，
  // NFCall.mo:2500-2506、:2570-2575）。推不出 → false。
  bool fill_implicit_ranges(Exp& red, NodeId enclosing, const std::vector<Symbol>& prefix) {
    for (ForIndex& it : red.iters) {
      if (it.range.is_valid()) continue;
      IterUses u;
      iter_uses(red.a, it.name, u);
      for (const ForIndex& o : red.iters) if (o.name != it.name) iter_uses(o.range, it.name, u);
      it.range = implicit_range(u, enclosing, prefix);
      if (!it.range.is_valid()) return false;
    }
    return true;
  }
  // 条件（已 inst + 化简）里有没有 `fixed = false` 的参数（字面量 false）；cref 找不到已发射变量 → false。
  bool cond_has_unfixed_param(Id<Expr> ce) {
    bool hit = false;
    const Symbol fixed = in_.intern("fixed");
    auto walk = [&](auto& self, Id<Expr> id) -> void {
      if (!id.is_valid() || hit) return;
      const Expr e = result_.model.exprs[id];   // 拷贝
      if (e.kind == ExprKind::Cref) {
        const std::string k = expr_cref_key(e.cref);
        for (const FlatVariable& fv : result_.model.variables) {   // ponytail: 线性找（同 cond_params_fixed）
          if (var_key(fv) != k) continue;
          if (fv.var_kind == FlatVarKind::Parameter)
            for (const FlatVarAttr& a : fv.attributes)
              if (a.name == fixed && result_.model.exprs[a.value].kind == ExprKind::BoolLit && !result_.model.exprs[a.value].bval)
                hit = true;
          break;
        }
        return;
      }
      for (Id<Expr> a : e.args) self(self, a);
      self(self, e.a);
      self(self, e.b);
      self(self, e.c);
    };
    walk(walk, ce);
    return hit;
  }
  // if 方程参数条件里的参数是否都 fixed（Structural.isExpressionNotFixed 的反面，NFTyping.mo:3581）：条件（已 inst + 化简）
  // 里每个 cref 须是已发射的参数 / 常量；有参数 `fixed = false` → false（omc 不当结构条件，原样保留）。参数须是字面量绑定：
  // markComponent 会连带标记绑定里的参数，但绑定若已被 Ceval 缓存成 CEVAL_BINDING 就不再传递（getExpOpt，NFBinding.mo:
  // 268-278；IF3 探针 `b = c > 1` 时 c 不 final，维度里 `n = m` 时 m 却 final）——依赖 omc 求值次序，不猜 → 非字面量绑定、
  // 找不到变量（数组 / 记录元素等未核实形态）→ nullopt（调用方 SKIP）。
  std::optional<bool> cond_params_fixed(Id<Expr> ce) {
    bool fixed_all = true, known = true;
    const Symbol fixed = in_.intern("fixed");
    auto walk = [&](auto& self, Id<Expr> id) -> void {
      if (!id.is_valid() || !known) return;
      const Expr e = result_.model.exprs[id];   // 拷贝
      if (e.kind == ExprKind::Cref) {
        const std::string k = expr_cref_key(e.cref);
        const FlatVariable* v = nullptr;
        // ponytail: 线性找（压平期 var_index_ 未建）；条件里的参数少，模型大时再给压平期建增量索引。
        for (const FlatVariable& fv : result_.model.variables)
          if (var_key(fv) == k) { v = &fv; break; }
        if (!v || (v->var_kind != FlatVarKind::Parameter && v->var_kind != FlatVarKind::Constant)) { known = false; return; }
        if (v->var_kind == FlatVarKind::Parameter && (!v->binding.is_valid() || !is_lit(v->binding))) { known = false; return; }
        for (const FlatVarAttr& a : v->attributes)
          if (a.name == fixed && result_.model.exprs[a.value].kind == ExprKind::BoolLit && !result_.model.exprs[a.value].bval)
            fixed_all = false;
        return;
      }
      for (Id<Expr> a : e.args) self(self, a);
      self(self, e.a);
      self(self, e.b);
      self(self, e.c);
    };
    walk(walk, ce);
    if (!known) return std::nullopt;
    return fixed_all;
  }
  bool refs_keys(Id<Expr> id, const std::unordered_map<std::string, Id<Expr>>& vals) {
    if (!id.is_valid()) return false;
    const Expr& e = result_.model.exprs[id];
    if (e.kind == ExprKind::Cref) {
      if (vals.count(expr_cref_key(e.cref))) return true;
      for (const ExprCrefPart& cp : e.cref.parts)
        for (Id<Expr> sx : cp.subscripts) if (refs_keys(sx, vals)) return true;
      return false;
    }
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
      if (it == var_index_.end()) { set_unsupported("structural parameter"); return; }   // 未落成标量变量（数组 / 记录）
      const std::size_t vi = it->second;
      const TypeKind vk = result_.model.variables[vi].ty.kind;
      Id<Expr> lit;
      if (auto cv = const_vals_.find(k); cv != const_vals_.end()) {
        if (std::optional<CVal> c = coerce_to_kind(cv->second, vk)) lit = literal_of(*c);
      } else if (result_.model.variables[vi].binding.is_valid() && is_lit(result_.model.variables[vi].binding)) {
        lit = result_.model.variables[vi].binding;
      }
      if (!lit.is_valid()) { set_unsupported("structural parameter binding"); return; }
      if (vk == TypeKind::Real) lit = to_real(lit);
      FlatVariable& v = result_.model.variables[vi];
      if (v.var_kind != FlatVarKind::Parameter && v.var_kind != FlatVarKind::Constant) { set_unsupported("structural parameter"); return; }
      if (v.var_kind == FlatVarKind::Parameter) v.is_final = true;
      v.binding = lit;
      vals.emplace(k, lit);
    }
    // 绑定恰是某个结构参数的引用的标量 parameter 连带成为结构参数（checkComponentBindingVariability，NFTyping.mo:1150-1159，
    // v1.25.0：绑定定型出来的变率是 STRUCTURAL_PARAMETER 且绑定是 cref）。判定发生在**本参数的绑定定型时**：目标须在那之前
    // 已是结构参数——实例化期标的（维 / Evaluate / 条件 / fixed …）总是；绑定定型期标的要声明在目标被标之后；方程段 / 压平期
    // 标的（for 范围、方程里的下标、if 表达式选支）不连带；别名的别名要目标先定型（AO 探针 a1–a3：`m = k; k = n; Real y[n]`
    // → m 不 final、k final）。只认源码里就是裸 cref 的绑定（alias_keys_，值 = 定型序号）：表达式绑定（`-p`、`2 * p`）、类型
    // 不同的别名（`Real s = n`，绑定是转型不是 cref）、记录表达式绑定拆出的字段、数组字面量的元素不连带，只是下面代入后被
    // 求值。组件数组的修饰分发合成出的裸 cref 绑定连带与否未核实 → SKIP。变量按发射序 = 定型序，一趟即可。
    std::unordered_map<std::string, int> since = structural_set_;   // 成为结构参数的时刻
    for (FlatVariable& v : result_.model.variables) {
      if (v.var_kind != FlatVarKind::Parameter || !v.binding.is_valid()) continue;
      const Expr& b = result_.model.exprs[v.binding];
      if (b.kind != ExprKind::Cref) continue;
      const std::string tk = expr_cref_key(b.cref);
      const auto it = vals.find(tk);
      if (it == vals.end()) continue;
      const std::string vk = var_key(v);
      if (vals.count(vk)) continue;
      if (alias_unknown_keys_.count(vk)) { set_unsupported("structural alias through distributed modifier"); return; }
      const auto at = alias_keys_.find(vk);
      if (at == alias_keys_.end() || result_.model.exprs[it->second].ty.kind != v.ty.kind) continue;
      const auto st = since.find(tk);
      if (st == since.end() || st->second >= at->second) continue;   // 本绑定定型时目标还不是结构参数
      const Id<Expr> lit = it->second;
      vals.emplace(vk, lit);
      since[vk] = at->second;
      v.is_final = true;
      v.binding = lit;
    }
    // 数组别名整体连带：omc 的结构标记是变量级的；只有部分元素的目标是结构参数时形态未核实 → SKIP。
    for (const std::vector<std::string>& g : alias_groups_) {
      std::size_t hit = 0;
      for (const std::string& ek : g) hit += vals.count(ek);
      if (hit != 0 && hit != g.size()) { set_unsupported("structural alias on part of array"); return; }
    }
    // 结构参数数组的整体 / 切片引用（末段不带下标，或下标不全是整数字面量）：omc 代入的是整个数组的值，ymc 只按元素 key 代入
    // → 未做，SKIP。数组的整体 key = 元素 key 去掉末段的下标（`<名>[1][2].` → `<名>.`）。
    std::unordered_set<std::string> structural_arrays;
    for (const auto& [k, lit] : vals) {
      const std::size_t dot = k.size() >= 2 ? k.rfind('.', k.size() - 2) : std::string::npos;
      const std::size_t lb = k.find('[', dot == std::string::npos ? 0 : dot + 1);
      if (lb != std::string::npos) structural_arrays.insert(k.substr(0, lb) + ".");
    }
    auto whole_ref = [&](auto& self, Id<Expr> id) -> bool {
      if (!id.is_valid()) return false;
      const Expr& x = result_.model.exprs[id];
      if (x.kind == ExprKind::Cref && !x.cref.parts.empty()) {
        ExprCref base = x.cref;
        const std::vector<Id<Expr>> subs = base.parts.back().subscripts;
        base.parts.back().subscripts.clear();
        bool elem = !subs.empty();
        for (Id<Expr> sx : subs) elem = elem && result_.model.exprs[sx].kind == ExprKind::IntLit;
        if (!elem && structural_arrays.count(expr_cref_key(base))) return true;
        for (const ExprCrefPart& cp : x.cref.parts)
          for (Id<Expr> sx : cp.subscripts) if (self(self, sx)) return true;
        return false;
      }
      for (Id<Expr> a : x.args) if (self(self, a)) return true;
      return self(self, x.a) || self(self, x.b) || self(self, x.c);
    };
    // EvalConstants：结构参数的引用处一律代入值（变量绑定 / 属性、方程、算法；函数体不涉及模型参数），代入后化简。
    auto subst = [&](Id<Expr>& e) {
      if (e.is_valid() && !structural_arrays.empty() && whole_ref(whole_ref, e))
        set_unsupported("whole reference to structural parameter array");
      if (e.is_valid() && refs_keys(e, vals)) e = simplify_expr(clone_subst(e, vals));
    };
    for (std::size_t i = 0; i < result_.model.variables.size(); ++i) {
      Id<Expr> b = result_.model.variables[i].binding;
      subst(b);
      result_.model.variables[i].binding = b;
      for (std::size_t a = 0; a < result_.model.variables[i].attributes.size(); ++a) {
        Id<Expr> av = result_.model.variables[i].attributes[a].value;
        subst(av);
        result_.model.variables[i].attributes[a].value = av;
      }
    }
    auto eqn = [&](auto& self, Id<FlatEquation> id) -> void {
      FlatEquation q = result_.model.eqs[id];   // 拷贝：subst 会往 exprs 添加（eqs 不变，但不跨调用持引用）
      subst(q.a); subst(q.b); subst(q.c);
      for (FlatEqBranch& br : q.branches) { subst(br.cond); for (Id<FlatEquation> b : br.body) self(self, b); }
      for (Id<FlatEquation> b : q.body) self(self, b);
      for (Id<FlatEquation> b : q.else_body) self(self, b);
      result_.model.eqs[id] = std::move(q);
    };
    auto stmt = [&](auto& self, Id<FlatStatement> id) -> void {
      FlatStatement st = result_.model.stmts[id];
      subst(st.a); subst(st.b); subst(st.c);
      for (FlatStmtBranch& br : st.branches) { subst(br.cond); for (Id<FlatStatement> b : br.body) self(self, b); }
      for (Id<FlatStatement> b : st.body) self(self, b);
      for (Id<FlatStatement> b : st.else_body) self(self, b);
      result_.model.stmts[id] = std::move(st);
    };
    for (const std::vector<Id<FlatEquation>>* eqs : {&result_.model.equations, &result_.model.initial_equations})
      for (Id<FlatEquation> id : *eqs) eqn(eqn, id);
    for (const std::vector<FlatAlgorithm>* algs : {&result_.model.algorithms, &result_.model.initial_algorithms})
      for (const FlatAlgorithm& alg : *algs)
        for (Id<FlatStatement> id : alg.stmts) stmt(stmt, id);
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
        k.push_back('[');
        if (const std::optional<std::int64_t> o = sub_ordinal(s)) k += std::to_string(*o);
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
  // 收尾折叠（NFSimplifyModel + NFScalarize 对方程 / 语句的整形，v1.25.0）——cardinality / 结构参数代入之后跑，没有代入也跑：
  // - 方程 assert 条件恒真 → 删（simplifyEquation :211-215；语句的 assert 保留 :345-351）；
  // - if 的字面量条件：假 → 丢弃；真 → 无前置分支则只留其体，否则成为 else 并截断其余（simplifyIfEqBranches :497-554、
  //   simplifyIfStmtBranches :556-601）；
  // - 体为空的分支：omc 的标量化把它们全删掉（scalarizeIfEquation / scalarizeIfStatement，NFScalarize.mo:372-399、
  //   461-488）。末尾的空分支删掉不改语义，照做；其后还有非空分支的空分支被删就改了条件的语义（`if a then elseif b then e`
  //   → `if b then e`，AP p7 探针）——omc 的错，不跟 → SKIP。if 方程全空 → 整条删去；if 语句全空 omc 也删，但所在算法段
  //   空了仍打 `algorithm` → 未做，SKIP。
  // - 体为空的 for 语句删去（simplifyStatement :308）；语句删光的算法段删去（simplifyAlgorithms :287-300）。
  void simplify_model() {
    if (uses_cardinality_)
      for (FlatVariable& v : result_.model.variables)
        if (v.binding.is_valid() && subst_cardinality(v.binding)) v.binding = simplify_expr(v.binding);
    result_.model.equations = fold_eqs(result_.model.equations);
    result_.model.initial_equations = fold_eqs(result_.model.initial_equations);
    for (std::vector<FlatAlgorithm>* algs : {&result_.model.algorithms, &result_.model.initial_algorithms}) {
      for (FlatAlgorithm& alg : *algs) alg.stmts = fold_stmts(alg.stmts);
      std::erase_if(*algs, [](const FlatAlgorithm& a) { return a.stmts.empty(); });
    }
  }
  // if 的分支整形（方程 / 语句共用）：kept = 留下的条件分支，els = else 体。返回 false = 整条消失（els 并入外层）。
  template <class Branch, class Item>
  bool fold_if_branches(std::vector<Branch>& branches, std::vector<Item>& els, bool is_stmt) {
    std::vector<Branch> kept;
    for (Branch& br : branches) {
      const Expr& c = result_.model.exprs[br.cond];
      if (c.kind != ExprKind::BoolLit) { kept.push_back(std::move(br)); continue; }
      if (!c.bval) continue;        // 假 → 丢弃
      els = std::move(br.body);     // 真 → 成为 else，截断其余
      break;
    }
    branches = std::move(kept);
    if (branches.empty()) return false;
    bool any = !els.empty();
    for (const Branch& br : branches) any = any || !br.body.empty();
    if (!any) {
      if (is_stmt) set_unsupported("if statement with only empty branches");
      els.clear();
      return false;
    }
    while (els.empty() && branches.back().body.empty()) branches.pop_back();
    for (const Branch& br : branches)
      if (br.body.empty()) { set_unsupported("empty if branch before a non-empty one (omc drops it)"); break; }
    return true;
  }
  std::vector<Id<FlatEquation>> fold_eqs(const std::vector<Id<FlatEquation>>& eqs) {
    std::vector<Id<FlatEquation>> out;
    auto subst = [&](Id<Expr>& x) { if (uses_cardinality_ && x.is_valid() && subst_cardinality(x)) x = simplify_expr(x); };
    for (Id<FlatEquation> id : eqs) {
      FlatEquation& fe = result_.model.eqs[id];   // 本趟不新增方程节点，引用稳定
      subst(fe.a); subst(fe.b); subst(fe.c);
      for (FlatEqBranch& br : fe.branches) { subst(br.cond); br.body = fold_eqs(br.body); }
      fe.body = fold_eqs(fe.body);
      fe.else_body = fold_eqs(fe.else_body);
      if (fe.kind == FlatEqKind::Assert) {
        const Expr& c = result_.model.exprs[fe.a];
        if (c.kind == ExprKind::BoolLit && c.bval) continue;   // assert(true, …) → 删除
      }
      if (fe.kind == FlatEqKind::If && !fold_if_branches(fe.branches, fe.else_body, /*is_stmt=*/false)) {
        out.insert(out.end(), fe.else_body.begin(), fe.else_body.end());
        continue;
      }
      out.push_back(id);
    }
    return out;
  }
  std::vector<Id<FlatStatement>> fold_stmts(const std::vector<Id<FlatStatement>>& stmts) {
    std::vector<Id<FlatStatement>> out;
    for (Id<FlatStatement> id : stmts) {
      FlatStatement& fs = result_.model.stmts[id];   // 本趟不新增语句节点，引用稳定
      for (FlatStmtBranch& br : fs.branches) br.body = fold_stmts(br.body);
      fs.body = fold_stmts(fs.body);
      fs.else_body = fold_stmts(fs.else_body);
      if (fs.kind == FlatStmtKind::For && fs.body.empty()) continue;
      if (fs.kind == FlatStmtKind::If && !fold_if_branches(fs.branches, fs.else_body, /*is_stmt=*/true)) {
        out.insert(out.end(), fs.else_body.begin(), fs.else_body.end());
        continue;
      }
      out.push_back(id);
    }
    return out;
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
  // 模型算法段的语句上下文：循环深度（break 合法性）、控制结构嵌套深度 / when 体内（when 语句放置规则、discrete 赋值）、
  // 是否 initial algorithm。
  int alg_loops_ = 0;
  int alg_depth_ = 0;
  int alg_in_when_ = 0;
  int eq_in_when_ = 0;   // when 方程体内（collect_when_body 置）
  bool alg_initial_ = false;
  std::unordered_map<std::uint32_t, TypeKind> stmt_iters_;      // for **语句**（KEPT loop）的迭代变量→元素类型（符号，不内联）
  std::unordered_map<std::uint32_t, Id<Expr>> stmt_iter_labels_;   // 其中取 Boolean / 枚举值的 → 该类型的一个标签
  // 函数体发射语境（FnCtxGuard 设 / 恢复；fn_scope_ 非空 = 正在发射函数体）。
  std::unordered_map<std::uint32_t, Type> func_params_;         // 形参 / 局部名 → 类型（含维，cref 解析成 kFnLocal 裸名）
  std::unordered_set<std::uint32_t> fn_inputs_;                 // 输入名（不可作赋值目标）
  std::unordered_set<std::uint32_t> xfield_elems_;              // "复合数组取字段"展开出来的各元素 cref（语句里原样留着的要拒）
  std::unordered_map<std::uint32_t, Id<Expr>> loop_labels_;     // Boolean / 枚举范围上的 for 方程迭代变量 → 当前取值的标签
                                                                //（loop_vars_ 里存它的序数）
  std::unordered_map<std::uint32_t, std::vector<Id<Exp>>> fn_dim_exps_;   // 形参 / 局部名 → 各维的书写表达式（隐式范围用）
  const ScodeScope* fn_scope_ = nullptr;                        // 函数体名字解析作用域（函数定义处）
  Id<SElement> fn_home_;                                        // 当前函数压平名所出自的实例类（fn_flat_name）
  bool fn_impure_ = false;                                      // 当前函数是 impure（体内调 impure 函数不告警）
  Id<Expr> fn_end_;                                             // 函数体下标里 `end` 的值（所在维大小）
  int fn_loops_ = 0;                                            // 函数体 for / while 嵌套深度（break 只许在循环里）
  // 实例帧栈（压平 DFS 的祖先链，见 FramePush）与当前求值帧（-1 = 不按实例帧解析）。
  struct Frame {
    NodeId inst;
    std::vector<Symbol> prefix;
    PSubs psubs;
    const ModCtx* ctx;
  };
  std::vector<Frame> frames_;
  int ceval_frame_ = -1;
  std::unordered_set<std::string> frame_busy_;                  // 求值中的参数 key（环检测）
  std::unordered_set<std::uint32_t> nofold_calls_;              // 不可常量折叠的内建调用（external "C" 的内建等价函数）
  std::unordered_set<std::uint32_t> subst_calls_;               // 实参被结构参数代入改过的调用（clone_subst 登记）
  std::vector<std::vector<Symbol>> model_record_ctors_;         // 模型表达式里整记录引用需要的构造函数（collect_record_ctors）
  std::unordered_map<std::uint32_t, NodeId> rec_node_cache_;    // 记录类定义 → 某个实例类节点（record_class_node）
  std::unordered_map<std::uint32_t, Op> fn_array_ops_;          // 函数语境的数组二元运算节点 → 书写的算符（fn_array_form）
  std::unordered_set<std::uint32_t> scalarized_logicals_;       // scalarize 逐元素建的 and / or 节点（simplify 不做吸收律）
  bool noret_call_ = false;                                     // 正在定型一条无返回值调用语句的最外层调用（见 noret_call）
  std::unordered_set<std::uint32_t> nofold_bins_;               // sum / product 展开出的元素链的 Binary 节点（simplify 原样留）
  std::unordered_map<std::uint32_t, FnRecord> fn_records_;      // 记录类型的形参 / 局部名 → 记录类（字段访问 `r.x`）
  NodeId root_;                                                 // 被压平的根类实例
  // 模型语境里返回数组的调用只许出现在整个绑定 / 等式一侧（NFScalarize.scalarizeEquation :343-346 把含数组调用的等式
  // 整体保留；嵌在别的运算里要逐元素展开，未做）。emit_array / collect_equation 在对应 inst_exp 前置位，
  // try_emit_user_call 进入即清（实参里的嵌套调用不继承）。
  bool array_call_ok_ = false;
  // 下一个被定型的 if 表达式是整个绑定 / 等式一侧（两支不相容时允许按结构参数求值选支，见 array_if_exp）。emit_array /
  // collect_equation 在对应 inst_exp 前按 Absyn 置位，if_exp 进入即清。
  bool if_top_ = false;
  // scalarize 可否代入展开推导式（见 scalarize 的 Reduction 支）：只在核实过的调用点由 RedScalarize 置位。
  bool red_scalarize_ok_ = false;
  struct RedScalarize {
    bool& f;
    bool old;
    explicit RedScalarize(bool& flag) : f(flag), old(flag) { f = true; }
    ~RedScalarize() { f = old; }
    RedScalarize(const RedScalarize&) = delete;
    RedScalarize& operator=(const RedScalarize&) = delete;
  };
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
  // 已发射常量的最终值（var_key 编码 → 值；数组另存整体），见 const_lookup_*。
  std::unordered_map<std::string, CVal> const_vals_;
  // 结构参数的别名连带（apply_structural_params）：绑定在源码里就是裸 cref 的标量参数（alias_keys_，变量 key）；裸 cref 但来自
  // 组件数组修饰分发的（alias_unknown_keys_，连带与否未核实）。derived_mods_ = 记录表达式绑定拆出的字段修饰；
  // synth_mod_floor_ = flatten 开始时 SCode 修饰的个数（其后新增的都是合成的）。
  std::unordered_map<std::string, int> alias_keys_;      // 候选 → 其绑定的定型序号（bind_clock_）
  std::unordered_set<std::string> alias_unknown_keys_;
  std::vector<std::vector<std::string>> alias_groups_;   // 参数数组的裸 cref 绑定：同一数组的各元素 key（整体连带）
  std::unordered_set<std::uint32_t> derived_mods_;
  std::size_t synth_mod_floor_ = 0;
  // 空数组（某维为 0，不出变量行）的形状，int_key(全名, 逐段下标) → 维；`:` 维的引用 / size() 靠它（emitted_shape 的回退）。
  std::unordered_map<std::string, std::vector<std::int64_t>> empty_shapes_;
  const std::vector<Symbol>* ceval_prefix_ = nullptr;   // 非空：eval_scode 的 cref 先在此前缀下查 const_vals_（CevalCtx 设）
  PSubs ceval_psubs_;
  std::vector<std::pair<std::uint32_t, CVal>> citers_;  // 数组推导式求值期的迭代变量绑定（栈，内层在后）
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
  std::vector<std::string> structural_keys_;   // 结构参数（变量 key，标记序；Structural.markExp）：apply_structural_params 统一处理
  std::unordered_map<std::string, int> structural_set_;   // key → 标记时刻（见 MarkPhase）
  MarkPhase mark_phase_ = MarkPhase::Late;
  int bind_clock_ = 0;                                    // 已开始定型绑定的变量数（标量 / 数组各算一拍）
  bool dep_mark_ = true;                                  // frame_value：当前求值层的参数是否打结构标记
  std::unordered_set<std::string> typed_keys_;            // 绑定已定型（已发射）的参数（数组记整体 key）
  std::unordered_map<std::string, CVal> param_lits_;      // 已发射的字面量绑定参数（标量叶）key → 值（record_const）
  struct ParamGroup {
    std::size_t count = 0;            // 已发射的参数元素个数（含非字面量绑定的）
    std::vector<std::string> keys;    // 其中字面量绑定的各元素 key
  };
  std::unordered_map<std::string, ParamGroup> param_groups_;   // 名字链（不带下标）→ 该组件的各元素
  std::unordered_map<std::string, int> deps_time_;        // 参数 → 其依赖已按哪个时刻连带过（值有缓存时判是否要补走）
  MarkTxn* txn_ = nullptr;                                // 当前最内层的求值事务（栈上对象，见 eval_scode）
  bool card_allowed_ = false;   // 只在 if 方程 / assert 的条件表达式里为 true（其它上下文 omc 报错 → SKIP）
  struct ConnectStmt {
    Id<Exp> a, b; NodeId enclosing; std::vector<Symbol> prefix; SourceLoc loc;
    std::unordered_map<std::uint32_t, std::int64_t> loopvars;   // 采集时的 for 迭代变量快照（连接器下标 `c[i]` 求值）
    PSubs psubs;                                                 // 书写处前缀的逐段下标（子模型数组元素 `a[i]` 内的 connect）
  };
  std::vector<ConnectStmt> connects_;            // 攒下的 connect()，run() 末尾展开
  std::unordered_set<std::string> removed_;             // 条件为假被移除的组件路径 key（引用它的 connect 整条删）
};

}  // namespace

FlattenResult flatten(Inst& inst, Interner& in, NodeId root) { return Flattener(inst, in).run(root); }

}  // namespace ymc
