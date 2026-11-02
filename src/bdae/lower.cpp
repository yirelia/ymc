// 变量：lowerVariableData NBackendDAE.mo:505-667、lowerVariableKind :700-777；方程：lowerEquationData :815-862、
// lowerEquationsAndAlgorithms :864-902、lowerEquation :904-1014、lowerWhenEquation :1141-1288（均 OMC v1.25.0）。
//
// 输入是默认路径的 FlatModel，不是 NB 路径的（ADR-008 F3）。标量范围内两者只差 combineBinaries，这一步在这里补。
#include "bdae/lower.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

#include "bdae/combine_binaries.h"

namespace ymc::bdae {

namespace {

class Lowerer {
 public:
  Lowerer(FlatModel&& m, Interner& in) : m_(std::move(m)), in_(in) {}

  LowerResult run() {
    r_.dae.exprs = std::move(m_.exprs);
    if (!m_.algorithms.empty() || !m_.initial_algorithms.empty()) stop("algorithm section");
    if (m_.unrolled_for) stop("for equation");
    if (m_.unseen_when_assign) stop("when equation in a dropped branch or removed component");
    artificials();
    known_.insert("time");
    for (const FlatVariable& v : m_.variables) known_.insert(dotted(v.name, [](Symbol x) { return x; }));
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
      case VarKind::Time: YMC_UNREACHABLE("artificial kind from a flat variable");
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
      case ExprKind::Cref:
        for (const ExprCrefPart& p : e.cref.parts)
          if (!p.subscripts.empty()) { stop("subscripted reference"); return false; }
        // 指向变量表以外的名字：OMC 把它当迭代量追加到 Artificial 桶（collectIterators :1393-1427；状态机的 `activeState(aState)`）。
        if (!known_.count(dotted(e.cref.parts, [](const ExprCrefPart& p) { return p.name; }))) { stop("reference to a non-variable"); return false; }
        return true;
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
  void equation(Id<FlatEquation> id, std::vector<Id<Eqn>>& out) {
    const FlatEquation& q = m_.eqs[id];
    switch (q.kind) {
      case FlatEqKind::Equality: break;
      case FlatEqKind::If: return stop("if equation");
      case FlatEqKind::For: return stop("for equation");
      case FlatEqKind::When: return when_equation(q, out);
      case FlatEqKind::Assert: return assert_equation(q, out);
      case FlatEqKind::Terminate: return stop("terminate outside when");   // lowerEquation :1003-1005 报内部错，omc 没有这一段
      case FlatEqKind::Reinit: return stop("reinit outside when");         // 同上 :1006-1008
      case FlatEqKind::NoRetCall: return stop("no-return call");
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
  // - 条目的先后：离散量走 UnorderedMap.keyList（:3173），是插入序，语句先被头插成反序，所以打印回到源序；状态走 UnorderedSet.toList（:3174），是
  //   ComponentRef.hash 的桶序（S5b-0 实测）。这里只做离散量、状态各至多一个的情形，顺序是定的：[离散, 状态]；其余退出，放开时用 omc_unordered.h。
  // - 第三段（:3241-3254）把 terminate、无返回值调用收进 flat_new，之后没人用：这些语句就此消失。全是这类语句的 when 整条消失，也不占 $RES_SIM 编号。
  // - acc_condition 不随条目清零：离散量那一轮没用完的取反条件会漏到状态那一轮（:3195、:3226），所以 reinit 只出现在 elsewhen 里时条件是
  //   `not c1 and c2`。这里照做，两轮共用一个 acc。
  void when_equation(const FlatEquation& q, std::vector<Id<Eqn>>& out) {
    std::vector<WhenBranch> flat;
    std::vector<std::string> discretes, states;
    const auto target = [&](Id<Expr> id, std::vector<std::string>& seen) {
      const Expr& t = r_.dae.exprs[id];
      if (t.kind != ExprKind::Cref) { stop("when target is not a reference"); return false; }
      std::string name = dotted(t.cref.parts, [](const ExprCrefPart& p) { return p.name; });
      if (std::find(seen.begin(), seen.end(), name) == seen.end()) seen.push_back(std::move(name));
      return true;
    };
    for (const FlatEqBranch& br : q.branches) {
      if (!when_condition_ok(br.cond)) return;
      WhenBranch b;
      b.condition = combine_binaries(r_.dae.exprs, br.cond);   // 前端先 combine，lower 才拆：拼出来的 not / and 不再 combine
      for (Id<FlatEquation> id : br.body) {
        const FlatEquation& s = m_.eqs[id];
        switch (s.kind) {
          case FlatEqKind::Equality:
            if (!expr_ok(s.a) || !expr_ok(s.b) || !target(s.a, discretes)) return;
            b.stmts.push_back({WhenStmtKind::Assign, s.a, combine_binaries(r_.dae.exprs, s.b), {}});
            break;
          case FlatEqKind::Reinit:
            if (!expr_ok(s.a) || !expr_ok(s.b) || !target(s.a, states)) return;
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
    if (discretes.size() > 1 || states.size() > 1) return stop("multi-assign when");

    Id<Expr> acc;
    std::vector<Id<Eqn>> bodies;
    const auto round = [&](WhenStmtKind kind) {
      Eqn e;
      e.kind = EqnKind::When;
      for (const WhenBranch& b : flat) {
        std::vector<WhenStmt> picked;
        for (const WhenStmt& s : b.stmts)
          if (s.kind == kind) picked.push_back(s);
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
    if (!discretes.empty()) round(WhenStmtKind::Assign);
    if (!states.empty() && !r_.unsupported) round(WhenStmtKind::Reinit);
    // listAppend(lowerEquation(…), equations)（:878）：离散条目在表头一侧、编号小。out 是源序，表头在尾巴上，所以反着接。
    out.insert(out.end(), bodies.rbegin(), bodies.rend());
  }

  void equations() {
    // 编号走 equation_lst：OMC 每条方程头插（:876-901，顺序：方程、算法、初始方程、初始算法），所以表头是最后一条初始方程，它拿
    // $RES_SIM_0，源里第一条方程编号最大（:834-836）。打印序靠 typeList 再头插一次（NBEquation.mo:4671-4705）翻回源序。
    EqnData& ed = r_.dae.eqns;
    std::vector<Id<Eqn>> eqs, inits;
    for (Id<FlatEquation> id : m_.equations) {
      equation(id, eqs);
      if (r_.unsupported) return;
    }
    for (Id<FlatEquation> id : m_.initial_equations) {
      equation(id, inits);
      if (r_.unsupported) return;
    }
    auto name_reversed = [&](const std::vector<Id<Eqn>>& v) {
      for (std::size_t i = v.size(); i-- > 0;)
        r_.dae.eqn_arena[v[i]].name = cref_of("$RES_SIM_" + std::to_string(ed.unique_index++));
    };
    name_reversed(inits);
    name_reversed(eqs);
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
  std::unordered_set<std::string> known_;   // 变量名（点分）+ time
};

}  // namespace

LowerResult lower(FlatModel&& model, Interner& in) { return Lowerer(std::move(model), in).run(); }

}  // namespace ymc::bdae
