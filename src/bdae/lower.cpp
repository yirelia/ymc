// 变量：lowerVariableData NBackendDAE.mo:505-667、lowerVariableKind :700-777；方程：lowerEquationData :815-862、
// lowerEquationsAndAlgorithms :864-902、lowerEquation :904-1014、lowerIfEquation :1016-1139、lowerWhenEquation :1141-1288、
// lowerAlgorithm :1290-1309（均 OMC v1.25.0）。
//
// 输入是默认路径的 FlatModel，不是 NB 路径的（ADR-008 F3）。标量范围内两者只差 combineBinaries，这一步在这里补。
#include "bdae/lower.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "bdae/combine_binaries.h"
#include "bdae/exp_compare.h"
#include "bdae/omc_unordered.h"

namespace ymc::bdae {

namespace {

// List.sort（Util/List.mo:780-817 + merge :962-1000），不用 std::stable_sort：相等的元素它是**反着**排的（两个元素时 gt(e2, e1) 为假就交换，
// 归并时 gt(r, l) 为假就先取右边）。if 方程一个分支里左边相同的方程，先后就看这个。
template <class T, class Gt>
std::vector<T> omc_list_sort(const std::vector<T>& in, const Gt& gt) {
  if (in.size() < 2) return in;
  if (in.size() == 2) return gt(in[1], in[0]) ? in : std::vector<T>{in[1], in[0]};
  const auto middle = in.begin() + static_cast<std::ptrdiff_t>(in.size() / 2);
  const std::vector<T> left = omc_list_sort(std::vector<T>(in.begin(), middle), gt), right = omc_list_sort(std::vector<T>(middle, in.end()), gt);
  std::vector<T> out;
  std::size_t l = 0, r = 0;
  while (l < left.size() && r < right.size()) out.push_back(gt(right[r], left[l]) ? left[l++] : right[r++]);
  out.insert(out.end(), left.begin() + static_cast<std::ptrdiff_t>(l), left.end());
  out.insert(out.end(), right.begin() + static_cast<std::ptrdiff_t>(r), right.end());
  return out;
}

// ComponentRef.hash / isEqual，给 when 拆分的 state_set 和迭代量集合用。这里的 cref 都没有下标（lower 挡了数组）。
struct CrefHash {
  const Interner* in;
  [[nodiscard]] OmcInt operator()(const ExprCref& c) const { return cref_hash(c, *in, [](Id<Expr>) { return std::string_view{}; }); }
};
struct CrefEq {
  [[nodiscard]] bool operator()(const ExprCref& a, const ExprCref& b) const {
    return std::equal(a.parts.begin(), a.parts.end(), b.parts.begin(), b.parts.end(),
                      [](const ExprCrefPart& p, const ExprCrefPart& q) { return p.name == q.name; });
  }
};
using CrefSet = UnorderedSet<ExprCref, CrefHash, CrefEq>;

class Lowerer {
 public:
  Lowerer(FlatModel&& m, Interner& in) : m_(std::move(m)), in_(in) {}

  LowerResult run() {
    r_.dae.exprs = std::move(m_.exprs);
    if (m_.unrolled_for) stop("for equation");
    if (m_.unseen_when_assign) stop("when equation in a dropped branch or removed component");
    if (m_.stmt_branch_emptied) stop("if statement branch emptied by simplification");
    if (m_.if_eq_emptied) stop("if equation emptied by simplification");
    if (m_.alg_outputs_unknown) stop("array element assigned in a dropped statement");
    artificials();
    functions();
    known_.emplace("time", TypeKind::Real);
    for (const FlatVariable& v : m_.variables) known_.emplace(dotted(v.name, [](Symbol x) { return x; }), v.ty.element_type().kind);
    for (const FlatVariable& v : m_.variables) variable(v);
    if (!r_.unsupported) equations();
    return std::move(r_);
  }

 private:
  void stop(std::string why) {
    if (r_.unsupported) return;
    r_.unsupported = true;
    r_.unsupported_reason = std::move(why);
  }
  template <class Parts, class Name>
  std::string dotted(const Parts& parts, Name name) const {
    std::string s;
    for (const auto& p : parts) {
      if (!s.empty()) s += '.';
      s += in_.str(name(p));
    }
    return s;
  }
  ExprCref cref_of(std::string_view name) {
    ExprCref c;
    c.parts.push_back(ExprCrefPart{in_.intern(name), {}});
    return c;
  }

  // `$ANY$` 和 time 恒在（lowerVariableData :533-537），放在 Artificial 桶的最前面。
  void artificials() {
    Var dummy;
    dummy.kind = VarKind::Dummy;
    r_.dae.vars.artificials.items.push_back(r_.dae.var_arena.add(std::move(dummy)));
    Var t;
    t.kind = VarKind::Time;
    t.name = cref_of("time");
    t.ty = Type::real();
    r_.dae.vars.artificials.items.push_back(r_.dae.var_arena.add(std::move(t)));
  }

  // 被调用户函数的性质，给 FunctionAlias 用（见 UserFunction）。函数至多一个算法段（多个时 omc 报错，见 flatten 的 collect_fn_sig），
  // 所以 hasSingleOrEmptyBody 对非外部函数恒真，只剩"体恰是一条 ASSIGNMENT"（getBody NFFunction.mo:2078-2081、:2656-2681）；
  // 元组赋值在 NF 里也是 ASSIGNMENT，FlatStmtKind::Assign 同样包含它。
  //
  // Early Inline 还要形参、局部和体（applyFuncExp NBReplacements.mo:296-331）：输入按 fn.inputs 的序、局部按 fn.locals 的序（sortLocals 排过，
  // params 里就是这个序），体是那条赋值的右边。函数体的式子和模型的在同一个 arena 里，Id 原样有效。
  void functions() {
    for (const FlatFunction& f : m_.functions) {
      UserFunction u;
      u.name = f.name;
      u.impure = f.is_impure;
      u.record_ctor = f.is_record_ctor;
      u.no_output = std::none_of(f.params.begin(), f.params.end(), [](const FlatFuncParam& p) { return p.direction == FlatDirection::Output; });
      u.inlineable = !f.is_external && f.body.size() == 1 && m_.stmts[f.body[0]].kind == FlatStmtKind::Assign;
      if (u.inlineable) inline_shape(f, u);
      r_.dae.functions.push_back(std::move(u));
    }
    r_.dae.impure_builtin_equiv = m_.impure_builtin_equiv;
  }
  // 能内联的函数：抄形参、局部、体；形状超出标量的记在 not_inlinable（只有真要内联它时才 SKIP，FunctionAlias 只看 inlineable）。
  // - 数组形参要重新定型（applyFuncExp :327-329），记录形参还要配子量（addInputArgTpl :343-397），函数型形参、外部对象都走不到标量门里。
  // - 元组赋值（多输出）在单值位置是 TUPLE_ELEMENT，lower 本来就挡；这里一并不收。
  // ponytail: 放开数组、记录时照 :327-329、:343-397 补。
  void inline_shape(const FlatFunction& f, UserFunction& u) {
    int outputs = 0;
    for (const FlatFuncParam& p : f.params) {
      const TypeKind k = p.ty.kind;
      if (p.ty.is_array() || !p.type_path.empty() ||
          (k != TypeKind::Real && k != TypeKind::Integer && k != TypeKind::Boolean && k != TypeKind::String)) {
        u.not_inlinable = "function parameter type";
        return;
      }
      if (p.direction == FlatDirection::Input) u.inputs.push_back(p.name);
      else if (p.direction == FlatDirection::Output) ++outputs;
      else u.locals.push_back({p.name, p.binding});
    }
    const FlatStatement& s = m_.stmts[f.body[0]];
    if (outputs != 1 || r_.dae.exprs[s.a].kind != ExprKind::Cref) {
      u.not_inlinable = "function with several outputs";
      return;
    }
    u.body = s.b;
  }

  // lowerVariableKind :728-770。Clock、外部对象、记录走不到这里（variable() 先拦），数组同。
  static VarKind kind_of(const FlatVariable& v, bool always_state) {
    switch (v.var_kind) {
      case FlatVarKind::Parameter: return VarKind::Parameter;
      case FlatVarKind::Constant: return VarKind::Constant;
      case FlatVarKind::Discrete: return VarKind::Discrete;
      case FlatVarKind::Var: break;
    }
    if (v.ty.kind != TypeKind::Real) return VarKind::Discrete;   // Integer / Boolean / 枚举 :754-756；String 是 IMPLICITLY_DISCRETE（NFAttributes.mo:539-540）
    if (v.implicit_discrete) return VarKind::Discrete;           // :760，变率已不是 CONTINUOUS，stateSelect = always 那一支也进不去
    return always_state ? VarKind::State : VarKind::Algebraic;   // stateSelect = always → 人工状态 :736-738
  }

  void variable(const FlatVariable& v) {
    if (r_.unsupported) return;
    if (!v.subscripts.empty() || v.ty.is_array()) return stop("array variable");
    for (const std::vector<Id<Expr>>& s : v.name_subs)
      if (!s.empty()) return stop("array variable");
    if (v.in_record) return stop("record variable");
    if (v.in_expandable) return stop("expandable connector member");
    if (v.redeclared_from_other_type) return stop("redeclared to Real from another type");
    if (v.string_fixed_opaque) return stop("non-literal fixed on String");
    if (!v.type_path.empty() || v.ty.kind == TypeKind::Complex) return stop("external object");
    if (v.ty.kind == TypeKind::Clock) return stop("Clock variable");
    if (v.ty.kind != TypeKind::Real && v.ty.kind != TypeKind::Integer && v.ty.kind != TypeKind::Boolean &&
        v.ty.kind != TypeKind::String && v.ty.kind != TypeKind::Enumeration)
      return stop("variable type");
    if (v.resizable) return stop("resizable parameter");
    if (v.ty.kind == TypeKind::Enumeration && v.enum_type_path.empty()) return stop("enumeration without type path");
    Var out;
    for (const Symbol part : v.name) out.name.parts.push_back(ExprCrefPart{part, {}});
    out.ty = v.ty;
    if (v.ty.kind == TypeKind::Enumeration) out.ty.enum_path = v.enum_type_path;
    out.is_protected = v.is_protected;
    out.is_final = v.attr_final;
    out.is_redeclare = v.is_redeclare;
    out.is_inner = v.is_inner;
    out.is_outer = v.is_outer;
    out.connector = v.connector;
    out.variability = v.var_kind;
    out.direction = v.direction;
    bool always_state = false, has_fixed = false;
    for (const FlatVarAttr& a : v.attributes) {
      if (!expr_ok(a.value)) return;
      out.attrs.push_back({a.name, combine_binaries_deep(r_.dae.exprs, a.value)});
      const std::string_view name = in_.str(a.name);
      has_fixed = has_fixed || name == "fixed";
      if (name == "stateSelect") {
        const Expr& e = r_.dae.exprs[a.value];
        if (e.kind != ExprKind::EnumLit || e.enum_path.empty()) return stop("stateSelect is not an enumeration literal");
        always_state = in_.str(e.enum_path.back()) == "always";
      }
    }
    if (v.string_fixed.is_valid()) {
      out.attrs.push_back({in_.intern("fixed"), v.string_fixed});
      has_fixed = true;
    }
    if (v.binding.is_valid()) {
      if (!expr_ok(v.binding)) return;
      out.binding = combine_binaries_deep(r_.dae.exprs, v.binding);
    }
    // 只有 createReal 读这个注解，而且无类型属性且非 final 时直接取空记录、不看注解（NFBackendExtension.mo:1110-1111、:1137）：光写注解打不出来。
    if (v.ty.kind == TypeKind::Real && (!v.attributes.empty() || v.is_final)) out.tearing_select = v.tearing_select;
    out.kind = kind_of(v, always_state);
    if (out.kind == VarKind::Parameter && !has_fixed) {   // setFixed(overwrite = false) :774
      Expr t;
      t.kind = ExprKind::BoolLit;
      t.ty = Type::boolean();
      t.bval = true;
      out.attrs.push_back({in_.intern("fixed"), r_.dae.exprs.add(std::move(t))});
    }
    const VarKind kind = out.kind;
    const Id<Var> id = r_.dae.var_arena.add(std::move(out));
    VarData& vd = r_.dae.vars;
    // 大小为 0 的变量（:549）标量化时已没了。顶层 input 优先于种类（:551）：`c.u` 这样的连接器成员不算顶层（ComponentRef.isTopLevel）。
    if (v.direction == FlatDirection::Input && v.name.size() == 1) { vd.top_level_inputs.items.push_back(id); return; }
    switch (kind) {
      case VarKind::Algebraic: vd.algebraics.items.push_back(id); vd.unknowns.items.push_back(id); break;
      case VarKind::Discrete: vd.discretes.items.push_back(id); vd.unknowns.items.push_back(id); break;
      case VarKind::State: vd.states.items.push_back(id); break;   // knowns，不进 unknowns :562-566
      case VarKind::Parameter: vd.parameters.items.push_back(id); break;
      case VarKind::Constant: vd.constants.items.push_back(id); break;
      case VarKind::Dummy:
      case VarKind::Time:
      case VarKind::Iterator: YMC_UNREACHABLE("artificial kind from a flat variable");
    }
  }

  // 只放行这几种节点。其余的 Expression 在 NB 路径上的形状 ymc 的默认路径看不到（数组不展开、元组、函数型实参……），不猜。
  // 递归校验类型：combineBinaries 的可交换性、方程入桶都看类型，缺类型就算错。
  bool expr_ok(Id<Expr> id) {
    const Expr& e = r_.dae.exprs[id];
    if (e.ty.is_array()) { stop("array expression"); return false; }
    switch (e.kind) {
      case ExprKind::IntLit:
      case ExprKind::RealLit:
      case ExprKind::BoolLit:
      case ExprKind::StringLit:
      case ExprKind::EnumLit:
        return true;
      case ExprKind::Cref: {
        for (const ExprCrefPart& p : e.cref.parts)
          if (!p.subscripts.empty()) { stop("subscripted reference"); return false; }
        // 指向变量表以外的名字：OMC 把它当迭代量追加到 Artificial 桶（collectIterators :1393-1427；状态机的 `activeState(aState)`）。
        // 算法里 for 的迭代量正是这样进去的（见 collect_iterators），别的情形没有参考。
        const std::string name = dotted(e.cref.parts, [](const ExprCrefPart& p) { return p.name; });
        if (!known_.count(name) && std::find(iters_.begin(), iters_.end(), name) == iters_.end()) { stop("reference to a non-variable"); return false; }
        return true;
      }
      case ExprKind::Binary:
        if (e.ty.kind == TypeKind::Unknown) { stop("untyped binary expression"); return false; }
        return expr_ok(e.a) && expr_ok(e.b);
      case ExprKind::Relation:
      case ExprKind::Logical:
        return expr_ok(e.a) && expr_ok(e.b);
      case ExprKind::Unary:
      case ExprKind::LogicalNot:
      case ExprKind::Cast:
        return expr_ok(e.a);
      case ExprKind::IfExp:
        return expr_ok(e.a) && expr_ok(e.b) && expr_ok(e.c);
      case ExprKind::Call:
        for (Id<Expr> arg : e.args)
          if (!expr_ok(arg)) return false;
        return true;
      case ExprKind::Tuple:
      case ExprKind::TupleElement:
      case ExprKind::Wild:
        stop("tuple expression");
        return false;
      case ExprKind::Box:
      case ExprKind::Unbox:
      case ExprKind::PartialApp:
      case ExprKind::FnRef:
      case ExprKind::FnParamCall:
        stop("function-typed argument");
        return false;
      default:   // Array、Range、Subscript、Size、Reduction、End；Multary 只由 lower 产出，不会在输入里
        stop("array expression");
        return false;
    }
  }

  // 方程入桶看方程的类型，不看变量（lowerEquationAttributes :1311-1323）。类型来自 TypeCheck.matchExpressions，两边已被 cast 成同型，取一边就够。
  bool eqn_time(Id<Expr> lhs, Id<Expr> rhs, EqnTime& out) {
    Type t = r_.dae.exprs[lhs].ty;
    if (t.kind == TypeKind::Unknown) t = r_.dae.exprs[rhs].ty;
    switch (t.kind) {
      case TypeKind::Real: out = EqnTime::Continuous; return true;
      case TypeKind::Integer:
      case TypeKind::Boolean:
      case TypeKind::String:
      case TypeKind::Enumeration: out = EqnTime::Discrete; return true;   // Type.isDiscrete NFType.mo:1254-1268
      case TypeKind::Clock: stop("clocked equation"); return false;
      case TypeKind::Unknown: stop("untyped equation"); return false;
      default: stop("non-scalar equation"); return false;
    }
  }

  // 一条 FlatEquation → 零到多条 Eqn，按源序接到 out 后面。方程式左右各调一次 combineBinaries（Equation.mapExp NFEquation.mo:716-722）。
  void equation(Id<FlatEquation> id, std::vector<Id<Eqn>>& out, bool init) {
    const FlatEquation& q = m_.eqs[id];
    switch (q.kind) {
      case FlatEqKind::Equality: break;
      case FlatEqKind::If: return if_equation(q, out, init);
      case FlatEqKind::For: return stop("for equation");
      case FlatEqKind::When: return when_equation(q, out);
      case FlatEqKind::Assert: return assert_equation(q, out);
      case FlatEqKind::Terminate: return stop("terminate outside when");   // lowerEquation :1003-1005 报内部错，omc 没有这一段
      case FlatEqKind::Reinit: return stop("reinit outside when");         // 同上 :1006-1008
      case FlatEqKind::NoRetCall: return no_return_call(q, out);
    }
    if (!expr_ok(q.a) || !expr_ok(q.b)) return;
    Eqn e;
    if (!eqn_time(q.a, q.b, e.time)) return;
    e.lhs = combine_binaries(r_.dae.exprs, q.a);
    e.rhs = combine_binaries(r_.dae.exprs, q.b);
    out.push_back(r_.dae.eqn_arena.add(std::move(e)));
  }

  // lowerWhenEquation 的 ASSERT 支（:1167-1171）：条件同时当 when 条件，种类 EMPTY，所以进 Removed；初始方程里的进 Initial（typeList 先看 init）。
  // 消息走 combineBinaries 的顶层：String 的 `+` 不可交换，也不往下探（Equation.mapExp 的 ASSERT 支 NFEquation.mo:766-772）。
  void assert_equation(const FlatEquation& q, std::vector<Id<Eqn>>& out) {
    if (!expr_ok(q.a) || !expr_ok(q.b)) return;
    Eqn e;
    e.kind = EqnKind::When;
    e.size = 0;
    e.time = EqnTime::Empty;
    const Id<Expr> cond = combine_binaries(r_.dae.exprs, q.a);
    const Id<Expr> msg = combine_binaries(r_.dae.exprs, q.b);
    e.when.push_back({cond, {{WhenStmtKind::Assert, cond, msg, q.c.is_valid() ? q.c : default_assert_level()}}});
    out.push_back(r_.dae.eqn_arena.add(std::move(e)));
  }
  // 两参 assert 的 AssertionLevel.error（AbsynToSCode.mo:64-65、:1597）：FlatEquation 里是空的，DAE dump 不打。
  Id<Expr> default_assert_level() {
    Expr e;
    e.kind = ExprKind::EnumLit;
    e.enum_path = {in_.intern("AssertionLevel"), in_.intern("error")};
    return r_.dae.exprs.add(std::move(e));
  }

  // lowerEquation 的 NORETCALL 支（:995-999）：包成一条语句的算法。没有输出，所以 EMPTY，进 Removed；初始方程里的进 Initial。
  void no_return_call(const FlatEquation& q, std::vector<Id<Eqn>>& out) {
    if (!expr_ok(q.a)) return;
    Eqn e;
    e.kind = EqnKind::Algorithm;
    e.size = 0;
    e.time = EqnTime::Empty;
    Stmt s;
    s.kind = StmtKind::NoRetCall;
    s.a = combine_binaries(r_.dae.exprs, q.a);
    e.alg.push_back(std::move(s));
    out.push_back(r_.dae.eqn_arena.add(std::move(e)));
  }

  // lowerIfEquation（:1016-1053）+ IfEquationBody.split（NBEquation.mo:2890-2910）：一个 if 拆成每条方程一个 if，条件链不变。
  // - 字面量条件 lower 看不到：前端已折叠（fold_if_branches 和 lowerIfEquationBody :1073-1081 是同一组规则）。
  // - 分支体的序：lowerIfBranchBody（:1137）头插，then_eqns 是 out 的反序，再由 sortForSplit 重排。
  // - 没有 else 时只有初始方程或 for 里能过（:1083），否则 "invalid missing else case"，omc 没有这一段。
  // - 各分支方程数要相同：多了 splitCollect 越界，少了 List.zip 失败（NBEquation.mo:2990、:2904），都没有这一段。
  // - 拆出来的先后：bodies 头插（:2908），所以 equation_lst 里 n 在前，打印时反回来，排序靠前的在前、编号大。out 是源序，按 1..n 接。
  // - 种类只看第一支（IfEquationBody.toEquation :2626-2638）：DISCRETE 或 CONTINUOUS，then 里是 EMPTY 的也算 CONTINUOUS。
  void if_equation(const FlatEquation& q, std::vector<Id<Eqn>>& out, bool init) {
    struct Body {
      Id<Expr> condition;              // 无效 = END
      std::vector<Id<Eqn>> eqns;       // 排好的 then_eqns
    };
    std::vector<Body> chain;
    const auto add_body = [&](Id<Expr> condition, const std::vector<Id<FlatEquation>>& body) {
      std::vector<Id<Eqn>> lowered;
      for (Id<FlatEquation> id : body) {
        equation(id, lowered, init);
        if (r_.unsupported) return;
      }
      std::reverse(lowered.begin(), lowered.end());
      chain.push_back({condition, sort_for_split(lowered)});
    };
    for (const FlatEqBranch& br : q.branches) {
      if (!expr_ok(br.cond)) return;
      // combineBinaries 在 lower 之前：条件和体里的式子各调一次（Branch.mapExp NFEquation.mo:65-90 → Equation.mapExp）。
      add_body(combine_binaries(r_.dae.exprs, br.cond), br.body);
      if (r_.unsupported) return;
    }
    if (!q.else_body.empty()) {
      add_body({}, q.else_body);
      if (r_.unsupported) return;
    } else if (!init) {
      return stop("if equation without else");
    }
    const std::size_t n = chain.front().eqns.size();
    for (const Body& b : chain)
      if (b.eqns.size() != n) return stop("if branches with different equation counts");
    for (std::size_t i = 0; i < n; ++i) {
      Eqn e;
      e.kind = EqnKind::If;
      const Eqn& first = r_.dae.eqn_arena[chain.front().eqns[i]];
      e.size = first.size;
      e.time = first.time == EqnTime::Discrete ? EqnTime::Discrete : EqnTime::Continuous;
      for (const Body& b : chain) e.if_branches.push_back({b.condition, b.eqns[i]});
      out.push_back(r_.dae.eqn_arena.add(std::move(e)));
    }
  }

  // sortForSplit（NBEquation.mo:2958-2978）：离散的在前，两组各按左边排。一组不到两条时不比较，getLHS 也就不调，没有左边的方程（when、算法）不出错。
  std::vector<Id<Eqn>> sort_for_split(const std::vector<Id<Eqn>>& eqns) {
    std::vector<Id<Eqn>> discretes, continuous;
    for (Id<Eqn> id : eqns) (r_.dae.eqn_arena[id].time == EqnTime::Discrete ? discretes : continuous).push_back(id);
    const ExpCompare compare(r_.dae.exprs, in_);
    const auto sorted = [&](const std::vector<Id<Eqn>>& group) {
      std::unordered_map<std::uint32_t, Id<Expr>> lhs;
      if (group.size() > 1) {
        for (Id<Eqn> id : group) {
          const Id<Expr> l = lhs_of(id, compare);
          if (!l.is_valid()) { stop("if branch equation without a unique left side"); return group; }   // getLHS 报内部错，omc 没有这一段
          lhs[id.raw] = l;
        }
      }
      return omc_list_sort(group, [&](Id<Eqn> x, Id<Eqn> y) { return compare(lhs.at(x.raw), lhs.at(y.raw)) > 0; });
    };
    std::vector<Id<Eqn>> out = sorted(discretes);
    if (r_.unsupported) return out;
    const std::vector<Id<Eqn>> rest = sorted(continuous);
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
  }
  // Equation.getLHS（NBEquation.mo:1522-1545）。if 要各支左边相同（IfEquationBody.getLHS :2840-2867）；when、算法没有左边，返回无效。
  Id<Expr> lhs_of(Id<Eqn> id, const ExpCompare& compare) {
    const Eqn& e = r_.dae.eqn_arena[id];
    if (e.kind == EqnKind::Scalar) return e.lhs;
    if (e.kind != EqnKind::If) return {};
    Id<Expr> lhs;
    for (const IfBranch& br : e.if_branches) {
      const Id<Expr> l = lhs_of(br.eqn, compare);
      if (!l.is_valid() || (lhs.is_valid() && compare(lhs, l) != 0)) return {};
      lhs = l;
    }
    return lhs;
  }

  // when 条件：标量式，或标量元素的数组字面量 `{a, b}`。只有这个位置收数组：别处的数组式 NB 路径不展开，这里两条路径相同（ADR-008 §6 A5）。
  bool when_condition_ok(Id<Expr> id) {
    const Expr& e = r_.dae.exprs[id];
    if (e.ty.kind == TypeKind::Clock) { stop("clocked when"); return false; }
    if (e.kind != ExprKind::Array) return expr_ok(id);
    for (Id<Expr> x : e.args)
      if (!expr_ok(x)) return false;
    return true;
  }

  // Expression.logicNegate NFExpression.mo:5048-5058。
  Id<Expr> logic_negate(Id<Expr> id) {
    Expr e = r_.dae.exprs[id];
    if (e.kind == ExprKind::BoolLit) {
      e.bval = !e.bval;
      return r_.dae.exprs.add(std::move(e));
    }
    if (e.kind == ExprKind::LogicalNot) return e.a;
    Expr n;
    n.kind = ExprKind::LogicalNot;
    n.ty = e.ty;
    n.a = id;
    return r_.dae.exprs.add(std::move(n));
  }
  // combineConditions NBEquation.mo:3443-3456。acc 无效 = Expression.EMPTY。
  Id<Expr> combine_conditions(Id<Expr> acc, Id<Expr> cond, bool invert) {
    if (invert) cond = logic_negate(cond);
    if (!acc.is_valid()) return cond;
    Expr e;
    e.kind = ExprKind::Logical;
    e.ty = Type::boolean();
    e.log = LogOp::And;
    e.a = acc;
    e.b = cond;
    return r_.dae.exprs.add(std::move(e));
  }

  // lowerWhenEquation + WhenEquationBody.split（NBEquation.mo:3151-3257）。split 给每个被赋值的离散量、每个被 reinit 的状态各造一条 when：
  // - 条目的先后：collectForSplit（:3337-3375）逐分支走语句，语句先被 lowerWhenBranchBody（:1243）头插成反序。离散量走 UnorderedMap.keyList（:3173），
  //   是插入序（重复的键不动位置，UnorderedMap.add :161-177）；状态走 UnorderedSet.toList（:3174），是 ComponentRef.hash 的桶序（S5b-0 实测）。
  //   结果是 [离散 1, 离散 2, …, 状态 1, …]，编号从小到大（:3256 listReverse 把头插反回来）。打印再反一次（typeList），离散量就回到源序。
  // - 第三段（:3241-3254）把 terminate、无返回值调用收进 flat_new，之后没人用：这些语句就此消失。全是这类语句的 when 整条消失，也不占 $RES_SIM 编号。
  // - acc_condition 不随条目清零：上一条没用完的取反条件会漏到下一条（:3195、:3226），所以 reinit 只出现在 elsewhen 里时条件是
  //   `not c1 and c2`。这里照做，各条共用一个 acc。
  void when_equation(const FlatEquation& q, std::vector<Id<Eqn>>& out) {
    std::vector<WhenBranch> flat;
    const auto is_target = [&](Id<Expr> id) {
      if (r_.dae.exprs[id].kind == ExprKind::Cref) return true;
      stop("when target is not a reference");
      return false;
    };
    for (const FlatEqBranch& br : q.branches) {
      if (!when_condition_ok(br.cond)) return;
      WhenBranch b;
      b.condition = combine_binaries(r_.dae.exprs, br.cond);   // 前端先 combine，lower 才拆：拼出来的 not / and 不再 combine
      for (Id<FlatEquation> id : br.body) {
        const FlatEquation& s = m_.eqs[id];
        switch (s.kind) {
          case FlatEqKind::Equality:
            if (!expr_ok(s.a) || !expr_ok(s.b) || !is_target(s.a)) return;
            b.stmts.push_back({WhenStmtKind::Assign, s.a, combine_binaries(r_.dae.exprs, s.b), {}});
            break;
          case FlatEqKind::Reinit:
            if (!expr_ok(s.a) || !expr_ok(s.b) || !is_target(s.a)) return;
            b.stmts.push_back({WhenStmtKind::Reinit, s.a, combine_binaries(r_.dae.exprs, s.b), {}});
            break;
          case FlatEqKind::Terminate:
          case FlatEqKind::NoRetCall:
            break;
          // lowerWhenBranchStatement 只认上面几种（:1262-1277），其余报内部错，omc 没有这一段。
          case FlatEqKind::Assert: return stop("assert in when");
          case FlatEqKind::If: return stop("if in when");
          case FlatEqKind::For:
          case FlatEqKind::When: return stop("nested equation in when");
        }
      }
      flat.push_back(std::move(b));
    }
    std::vector<ExprCref> discretes;
    CrefSet state_set{CrefHash{&in_}};
    for (const WhenBranch& b : flat) {
      for (auto s = b.stmts.rbegin(); s != b.stmts.rend(); ++s) {
        const ExprCref& c = r_.dae.exprs[s->a].cref;
        if (s->kind == WhenStmtKind::Reinit) state_set.add(c);
        else if (std::none_of(discretes.begin(), discretes.end(), [&](const ExprCref& d) { return CrefEq{}(c, d); })) discretes.push_back(c);
      }
    }

    Id<Expr> acc;
    std::vector<Id<Eqn>> bodies;
    const auto entry = [&](WhenStmtKind kind, const ExprCref& target) {
      Eqn e;
      e.kind = EqnKind::When;
      for (const WhenBranch& b : flat) {
        std::vector<WhenStmt> picked;
        for (const WhenStmt& s : b.stmts)
          if (s.kind == kind && CrefEq{}(r_.dae.exprs[s.a].cref, target)) picked.push_back(s);
        // 同一分支几个 reinit 指同一状态时取源序第一个（实跑，探针 LwbReinitTwice；getFirstReinit :3426-3441 的 break 写在 match 里，
        // 跳不出 for，取到的是反序的 when_stmts 里最后一个）。
        if (kind == WhenStmtKind::Reinit && picked.size() > 1) picked.resize(1);
        if (picked.empty()) {
          acc = combine_conditions(acc, b.condition, true);
        } else {
          e.when.push_back({combine_conditions(acc, b.condition, false), std::move(picked)});
          acc = {};
        }
      }
      // getType 要第一支恰好一条语句（:3055-3062，createName 用它），后面的支不管（size 也只看第一支，:3043-3048）。
      if (e.when.front().stmts.size() != 1) return stop("when branch assigns a variable twice");
      e.size = kind == WhenStmtKind::Assign ? 1 : 0;
      e.time = e.size > 0 ? EqnTime::Discrete : EqnTime::Empty;
      bodies.push_back(r_.dae.eqn_arena.add(std::move(e)));
    };
    for (const ExprCref& d : discretes) {
      entry(WhenStmtKind::Assign, d);
      if (r_.unsupported) return;
    }
    for (const ExprCref& s : state_set.to_list()) {
      entry(WhenStmtKind::Reinit, s);
      if (r_.unsupported) return;
    }
    // listAppend(lowerEquation(…), equations)（:878）：离散条目在表头一侧、编号小。out 是源序，表头在尾巴上，所以反着接。
    out.insert(out.end(), bodies.rbegin(), bodies.rend());
  }

  // lowerAlgorithm（:1290-1309）。alg.outputs 是压平时定的（NFFlatten.mo:387），早于 SimplifyModel，所以被化简删掉的语句里赋值的变量照算，
  // 见 FlatAlgorithm::dropped_outputs。入桶看输出变量的类型，不看种类（ComponentRef.listHasDiscrete NFComponentRef.mo:2173-2185）。
  void algorithm(const FlatAlgorithm& a, std::vector<Id<Eqn>>& out) {
    Eqn e;
    e.kind = EqnKind::Algorithm;
    for (Id<FlatStatement> id : a.stmts) {
      if (!statement(id, e.alg)) return;
    }
    std::unordered_set<std::string> outputs;
    collect_outputs(a.stmts, outputs);
    for (const std::vector<Symbol>& name : a.dropped_outputs) outputs.insert(dotted(name, [](Symbol x) { return x; }));
    bool discrete = false;
    for (const std::string& name : outputs) {
      const auto v = known_.find(name);
      if (v == known_.end()) return stop("algorithm output is not a variable");
      discrete = discrete || v->second == TypeKind::Integer || v->second == TypeKind::Boolean || v->second == TypeKind::String ||
                 v->second == TypeKind::Enumeration;   // Type.isDiscrete NFType.mo:1254-1268
    }
    e.size = static_cast<int>(outputs.size());   // 都是标量：ComponentRef.size 是 1
    e.time = outputs.empty() ? EqnTime::Empty : (discrete ? EqnTime::Discrete : EqnTime::Continuous);
    out.push_back(r_.dae.eqn_arena.add(std::move(e)));
  }
  // statementInputsOutputs（NFAlgorithm.mo:214-308）的输出部分：赋值左边，for / if / when / while 往里走，`_` 不算。
  void collect_outputs(const std::vector<Id<FlatStatement>>& stmts, std::unordered_set<std::string>& out) {
    const auto target = [&](Id<Expr> id) {
      const Expr& e = r_.dae.exprs[id];
      if (e.kind == ExprKind::Cref) out.insert(dotted(e.cref.parts, [](const ExprCrefPart& p) { return p.name; }));
    };
    for (Id<FlatStatement> id : stmts) {
      const FlatStatement& s = m_.stmts[id];
      if (s.kind == FlatStmtKind::Assign) {
        if (r_.dae.exprs[s.a].kind == ExprKind::Tuple) {
          for (Id<Expr> x : r_.dae.exprs[s.a].args) target(x);
        } else {
          target(s.a);
        }
      }
      for (const FlatStmtBranch& br : s.branches) collect_outputs(br.body, out);
      collect_outputs(s.body, out);
      collect_outputs(s.else_body, out);
    }
  }

  // 一条 FlatStatement → 一条 Stmt。combineBinaries 按 Statement.mapExp（NFStatement.mo:559-634）：每个顶层式调一次，for 的范围也算。
  bool statement(Id<FlatStatement> id, std::vector<Stmt>& out) {
    const FlatStatement& s = m_.stmts[id];
    Stmt st;
    const auto combined = [&](Id<Expr> e) { return combine_binaries(r_.dae.exprs, e); };
    const auto block = [&](const std::vector<Id<FlatStatement>>& in, std::vector<Stmt>& to) {
      return std::all_of(in.begin(), in.end(), [&](Id<FlatStatement> x) { return statement(x, to); });
    };
    switch (s.kind) {
      case FlatStmtKind::Assign:
        st.kind = StmtKind::Assign;
        if (!assign_target_ok(s.a) || !expr_ok(s.b)) return false;
        st.a = s.a;
        st.b = combined(s.b);
        break;
      case FlatStmtKind::For: {
        st.kind = StmtKind::For;
        st.index = s.index;
        if (!for_range_ok(s.a)) return false;
        st.a = combined(s.a);
        iters_.emplace_back(in_.str(s.index));
        const bool ok = block(s.body, st.body);
        iters_.pop_back();
        if (!ok) return false;
        break;
      }
      case FlatStmtKind::If:
      case FlatStmtKind::When:
        st.kind = s.kind == FlatStmtKind::If ? StmtKind::If : StmtKind::When;
        for (const FlatStmtBranch& br : s.branches) {
          if (!(s.kind == FlatStmtKind::If ? expr_ok(br.cond) : when_condition_ok(br.cond))) return false;
          StmtBranch b;
          b.condition = combined(br.cond);
          if (!block(br.body, b.body)) return false;
          st.branches.push_back(std::move(b));
        }
        if (!s.else_body.empty()) {          // NF 的 else 就是条件为 true 的末分支（toStream 打 `elseif true then`）
          StmtBranch b;
          Expr t;
          t.kind = ExprKind::BoolLit;
          t.ty = Type::boolean();
          t.bval = true;
          b.condition = r_.dae.exprs.add(std::move(t));
          if (!block(s.else_body, b.body)) return false;
          st.branches.push_back(std::move(b));
        }
        break;
      case FlatStmtKind::While:
        st.kind = StmtKind::While;
        if (!expr_ok(s.a)) return false;
        st.a = combined(s.a);
        if (!block(s.body, st.body)) return false;
        break;
      case FlatStmtKind::Assert:
        st.kind = StmtKind::Assert;
        if (!expr_ok(s.a) || !expr_ok(s.b)) return false;
        st.a = combined(s.a);
        st.b = combined(s.b);
        st.c = s.c.is_valid() ? s.c : default_assert_level();
        break;
      case FlatStmtKind::Terminate:
        st.kind = StmtKind::Terminate;
        if (!expr_ok(s.a)) return false;
        st.a = combined(s.a);
        break;
      case FlatStmtKind::NoRetCall:
        st.kind = StmtKind::NoRetCall;
        if (!expr_ok(s.a)) return false;
        st.a = combined(s.a);
        break;
      case FlatStmtKind::Break:
        st.kind = StmtKind::Break;
        break;
      case FlatStmtKind::Reinit:
      case FlatStmtKind::Return:
        stop("statement kind");
        return false;
    }
    out.push_back(std::move(st));
    return true;
  }
  // 赋值的左边：标量 cref，或元组里的 cref 和 `_`。
  bool assign_target_ok(Id<Expr> id) {
    const Expr& e = r_.dae.exprs[id];
    if (e.kind == ExprKind::Cref) return expr_ok(id);
    if (e.kind != ExprKind::Tuple) { stop("algorithm assignment target"); return false; }
    for (Id<Expr> x : e.args)
      if (r_.dae.exprs[x].kind != ExprKind::Wild && !assign_target_ok(x)) return false;
    return true;
  }
  // for 的范围：a:b、a:s:b，或数字字面量组成的数组。Boolean、枚举的数组不收：`for e in E` 默认路径展开成 `{E.a, E.b}`，NB 路径留 `E`（实跑 B1），
  // 这里分不清是写成数组的还是展开的。
  bool for_range_ok(Id<Expr> id) {
    const Expr& e = r_.dae.exprs[id];
    if (e.kind == ExprKind::Range) return expr_ok(e.a) && expr_ok(e.b) && (!e.c.is_valid() || expr_ok(e.c));
    if (e.kind != ExprKind::Array) { stop("for range"); return false; }
    for (Id<Expr> x : e.args) {
      const ExprKind k = r_.dae.exprs[x].kind;
      if (k != ExprKind::IntLit && k != ExprKind::RealLit) { stop("for range"); return false; }
    }
    return true;
  }

  // lowerEquationIterators（:1476-1494，lowerEquationData :839）：算法里引用到、又不是变量的名字收进集合，再按 UnorderedSet.toList 的桶序追加到 Artificial 桶
  //（:841，VarData.addTypedList :2340-2344）。只有用到的迭代量才进去（实跑 B3ForNoUse）。走的序照 Equation.map：表头（最后一条方程）先，
  // 语句内 Statement.mapExp 的序（for 先体后范围），表达式 Expression.map 的后序。序只影响同桶的先后和 rehash。
  void collect_iterators(const std::vector<Id<Eqn>>& inits, const std::vector<Id<Eqn>>& eqs) {
    CrefSet set{CrefHash{&in_}};
    std::unordered_map<std::string, Type> types;
    const auto visit = [&](const std::vector<Id<Eqn>>& v) {
      for (std::size_t i = v.size(); i-- > 0;) {
        const Eqn& e = r_.dae.eqn_arena[v[i]];
        if (e.kind == EqnKind::Algorithm) iter_stmts(e.alg, set, types);
      }
    };
    visit(inits);
    visit(eqs);
    for (const ExprCref& c : set.to_list()) {
      const std::string name(in_.str(c.parts[0].name));
      const Type ty = types.at(name);
      // 枚举要打类型全名（InstNode.scopePath），Type::enum_path 只有简名；变量那边单有 FlatVariable::enum_type_path，迭代量没有。
      if (ty.kind == TypeKind::Enumeration) return stop("enumeration iterator");
      Var it;
      it.kind = VarKind::Iterator;
      it.name = c;
      it.ty = ty;
      r_.dae.vars.artificials.items.push_back(r_.dae.var_arena.add(std::move(it)));
    }
  }
  void iter_stmts(const std::vector<Stmt>& stmts, CrefSet& set, std::unordered_map<std::string, Type>& types) {
    for (const Stmt& s : stmts) {
      if (s.kind == StmtKind::For) iter_stmts(s.body, set, types);
      iter_exp(s.a, set, types);
      iter_exp(s.b, set, types);
      iter_exp(s.c, set, types);
      for (const StmtBranch& br : s.branches) {
        iter_exp(br.condition, set, types);
        iter_stmts(br.body, set, types);
      }
      if (s.kind != StmtKind::For) iter_stmts(s.body, set, types);
    }
  }
  void iter_exp(Id<Expr> id, CrefSet& set, std::unordered_map<std::string, Type>& types) {
    if (!id.is_valid()) return;
    const Expr& e = r_.dae.exprs[id];
    if (e.kind == ExprKind::Cref) {
      if (!known_.count(dotted(e.cref.parts, [](const ExprCrefPart& p) { return p.name; }))) {
        set.add(e.cref);
        types.emplace(std::string(in_.str(e.cref.parts[0].name)), e.ty);
      }
      return;
    }
    iter_exp(e.a, set, types);
    iter_exp(e.b, set, types);
    iter_exp(e.c, set, types);
    for (Id<Expr> x : e.args) iter_exp(x, set, types);
    for (Id<Expr> x : e.inv_args) iter_exp(x, set, types);
  }

  // createName（NBEquation.mo:1769-1795）：if 先给外层编号，再逐支给体里的方程（IfEquationBody.createNames :2771-2782）。
  void name_equation(Id<Eqn> id) {
    r_.dae.eqn_arena[id].name = cref_of("$RES_SIM_" + std::to_string(r_.dae.eqns.unique_index++));
    for (const IfBranch& br : r_.dae.eqn_arena[id].if_branches) name_equation(br.eqn);
  }

  void equations() {
    // 编号走 equation_lst：OMC 每条方程头插（:876-901，顺序：方程、算法、初始方程、初始算法），所以表头是最后一条初始方程，它拿
    // $RES_SIM_0，源里第一条方程编号最大（:834-836）。打印序靠 typeList 再头插一次（NBEquation.mo:4671-4705）翻回源序。
    EqnData& ed = r_.dae.eqns;
    std::vector<Id<Eqn>> eqs, inits;
    for (Id<FlatEquation> id : m_.equations) {
      equation(id, eqs, false);
      if (r_.unsupported) return;
    }
    for (const FlatAlgorithm& a : m_.algorithms) {
      algorithm(a, eqs);
      if (r_.unsupported) return;
    }
    for (Id<FlatEquation> id : m_.initial_equations) {
      equation(id, inits, true);
      if (r_.unsupported) return;
    }
    for (const FlatAlgorithm& a : m_.initial_algorithms) {
      algorithm(a, inits);
      if (r_.unsupported) return;
    }
    auto name_reversed = [&](const std::vector<Id<Eqn>>& v) {
      for (std::size_t i = v.size(); i-- > 0;) name_equation(v[i]);
    };
    name_reversed(inits);
    name_reversed(eqs);
    collect_iterators(inits, eqs);
    for (Id<Eqn> id : eqs) {
      switch (r_.dae.eqn_arena[id].time) {
        case EqnTime::Continuous: ed.continuous.items.push_back(id); ed.simulation.items.push_back(id); break;
        case EqnTime::Discrete: ed.discretes.items.push_back(id); ed.simulation.items.push_back(id); break;
        case EqnTime::Empty: ed.removed.items.push_back(id); break;
      }
    }
    ed.initials.items = std::move(inits);
  }

  FlatModel m_;
  Interner& in_;
  LowerResult r_;
  std::unordered_map<std::string, TypeKind> known_;   // 变量名（点分）+ time → 元素类型
  std::vector<std::string> iters_;                    // 正在 lower 的语句外层的 for 迭代量
};

}  // namespace

LowerResult lower(FlatModel&& model, Interner& in) { return Lowerer(std::move(model), in).run(); }

}  // namespace ymc::bdae
