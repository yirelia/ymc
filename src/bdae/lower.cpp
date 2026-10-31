// 变量：lowerVariableData NBackendDAE.mo:505-667、lowerVariableKind :700-777；方程：lowerEquationData :815-862、
// lowerEquationsAndAlgorithms :864-902、lowerEquation :904-1014（均 OMC v1.25.0）。
//
// 输入是默认路径的 FlatModel，不是 NB 路径的（ADR-008 F3）。标量范围内两者只差 combineBinaries，这一步在这里补。
#include "bdae/lower.h"

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

  // 一条 FlatEquation → Eqn；方程式左右各调一次 combineBinaries（Equation.mapExp NFEquation.mo:716-722）。
  Id<Eqn> equation(Id<FlatEquation> id) {
    const FlatEquation& q = m_.eqs[id];
    switch (q.kind) {
      case FlatEqKind::Equality: break;
      case FlatEqKind::If: stop("if equation"); return {};
      case FlatEqKind::For: stop("for equation"); return {};
      case FlatEqKind::When: stop("when equation"); return {};
      case FlatEqKind::Assert: stop("assert"); return {};
      case FlatEqKind::Terminate: stop("terminate"); return {};
      case FlatEqKind::Reinit: stop("reinit"); return {};
      case FlatEqKind::NoRetCall: stop("no-return call"); return {};
    }
    if (!expr_ok(q.a) || !expr_ok(q.b)) return {};
    Eqn out;
    if (!eqn_time(q.a, q.b, out.time)) return {};
    out.lhs = combine_binaries(r_.dae.exprs, q.a);
    out.rhs = combine_binaries(r_.dae.exprs, q.b);
    return r_.dae.eqn_arena.add(std::move(out));
  }

  void equations() {
    // 编号走 equation_lst：OMC 每条方程头插（:876-901，顺序：方程、算法、初始方程、初始算法），所以表头是最后一条初始方程，它拿
    // $RES_SIM_0，源里第一条方程编号最大（:834-836）。打印序靠 typeList 再头插一次（NBEquation.mo:4671-4705）翻回源序。
    EqnData& ed = r_.dae.eqns;
    std::vector<Id<Eqn>> eqs, inits;
    for (Id<FlatEquation> id : m_.equations) {
      eqs.push_back(equation(id));
      if (r_.unsupported) return;
    }
    for (Id<FlatEquation> id : m_.initial_equations) {
      inits.push_back(equation(id));
      if (r_.unsupported) return;
    }
    auto name_reversed = [&](const std::vector<Id<Eqn>>& v) {
      for (std::size_t i = v.size(); i-- > 0;)
        r_.dae.eqn_arena[v[i]].name = cref_of("$RES_SIM_" + std::to_string(ed.unique_index++));
    };
    name_reversed(inits);
    name_reversed(eqs);
    for (Id<Eqn> id : eqs) {
      (r_.dae.eqn_arena[id].time == EqnTime::Continuous ? ed.continuous : ed.discretes).items.push_back(id);
      ed.simulation.items.push_back(id);
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
