// NBFunctionAlias.mo（OMC v1.25.0）：functionAliasDefault :219-337、introduceFunctionAliasEquation :339-371、introduceFunctionAlias
// :373-411、introduceAlias :426-476、checkCallReplacement / replaceException :478-520、addAuxVar :564-591。
//
// 走不到的分支不写：
//   - 迭代量（filterFrames、带下标的辅助变量）：方程段的 for 在 lower 就 SKIP 了，Iterator 恒为 EMPTY。
//   - 元组结果（TUPLE_ELEMENT 那一支、多个 `$FUN` 的 CALL_AUX）、记录结果（addAuxVar 的 isRecord 支）：多输出调用在单值位置是
//     TUPLE_ELEMENT，lower 挡了；记录在 lower 就 SKIP。ponytail: 放开时照 :399-406、:462-466、:576-582 补。
//   - 数组构造器当实参（introduceArrayConstructorAlias）、Inline.inlineArrayConstructorSingle：只认数组，lower 挡了。
//   - 时钟常量（addClockedAlias :593-611，`$CLK_n`）：标量方程里也会有（`y = sample(u, Clock(dt))`，ClockedSource），这里 SKIP。
//     ponytail: 放开 Clock 时照 Partitioning.extractClocks（NBPartitioning.mo:472-499）补，它扫的是 simulation 全部方程，含 when / if / 算法。
#include "bdae/function_alias.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bdae/bdae_dump.h"
#include "bdae/exp_compare.h"
#include "bdae/omc_unordered.h"

namespace ymc::bdae {

namespace {

// 内建函数按 Function.nameConsiderBuiltin 的名字分三类（checkCallReplacement :478-486）。名字不在下面三张表里的内建一律 SKIP：
// 它换不换取决于它在 NFModelicaBuiltin.mo / NFBuiltinFuncs.mo 里的声明（纯度、有无输出），没逐个核过的不猜。
//
// 不换：isSpecialBuiltin（NFFunction.mo:1802-1895）整张表。
constexpr std::string_view kSpecial[] = {
    "array", "actualStream", "backSample", "branch", "cardinality", "cat", "change", "der", "edge", "fill", "getInstanceName",
    "initial", "inStream", "isRoot", "matrix", "max", "min", "ndims", "noEvent", "ones", "potentialRoot", "pre", "promote", "pure",
    "root", "rooted", "uniqueRoot", "uniqueRootIndices", "scalar", "size", "shiftSample", "smooth", "subSample", "superSample",
    "symmetric", "terminal", "transpose", "vector", "zeros", "sample"};
// 不换：replaceException（:488-520）。
//   integer、String、$OMC$PositiveMax、$OMC$inStreamDiv：:511-517 点名的。
//   homotopy、delay：impure（:498）。NFModelicaBuiltin.mo:291 `impure function homotopy`；delay 是 $overload 到 OpenModelica.Internal.delay2 /
//     delay3（:370），两个都是 impure（:1101、:1110）。探针 LfaB 系（delay(sin(time), 0.1) 只换里面的 sin）。
//   Integer：枚举转整数定型成 NFBuiltinFuncs.INTEGER_FUNCTION（NFBuiltinFuncs.mo:142-145），它的 outputs 是空表，落进 replaceException 的
//     listEmpty(fn.outputs)（:500）。实跑 `m = Integer(e)` 不换。
//   ticksInState、timeInState：impure（NFModelicaBuiltin.mo:965、:973），语料 b 的 TicksInStateTest / TimeInStateTest 不换。
constexpr std::string_view kKeep[] = {"integer", "String",       "$OMC$PositiveMax", "$OMC$inStreamDiv", "homotopy",
                                      "delay",   "Integer",      "ticksInState",     "timeInState"};
// 换：纯、有输出、不在上面两张表里的内建。逐个有探针（LfaB / LfaK 系）；rem 在前端就展开成 `x - div(x, y) * y` 了，不会到这里。
// previous、hold 是泛型的 external "builtin"，不带 impure（NFModelicaBuiltin.mo:872-879、:947-954），结果类型跟着实参（Integer 进 [DISC]）。
constexpr std::string_view kReplace[] = {"sin",  "cos",  "tan",  "asin", "acos", "atan", "atan2",      "sinh",     "cosh",
                                         "tanh", "exp",  "log",  "log10", "sqrt", "abs",  "sign",       "div",      "mod",
                                         "ceil", "floor", "semiLinear", "previous", "hold"};

template <std::size_t N>
bool listed(const std::string_view (&table)[N], std::string_view name) {
  for (std::string_view s : table)
    if (s == name) return true;
  return false;
}

// Call_Id（:117-154）。hash = stringHashDjb2(Call_Id.toString)，迭代量为空时就是 Expression.toString（:131-145），插入时算一次存着；
// isEqual = Expression.isEqual（compare == 0）且迭代量相同（恒为 EMPTY）。
// 哈希只决定 Map 的查找桶，不决定序：Map 的 toList 是插入序（omc_unordered.h）。照搬它，是为了"相等但文本不同"的两个调用也和 omc 一样
// 落不落在同一个桶里。
struct CallKey {
  Id<Expr> call;
  OmcInt hash = 0;
};
struct CallKeyHash {
  [[nodiscard]] OmcInt operator()(const CallKey& k) const noexcept { return k.hash; }
};
struct CallKeyEq {
  const ExpCompare* compare;
  [[nodiscard]] bool operator()(const CallKey& x, const CallKey& y) const { return (*compare)(x.call, y.call) == 0; }
};
// Call_Aux（:156-164）。replacer 恒是一个 cref（元组走不到，见文件头），指向 var；kind 由类型定，只进方程属性，dump 不打，不存。
struct CallAux {
  Id<Var> var;
  Id<Expr> replacer;
  bool parsed = false;
};

class FunctionAlias {
 public:
  FunctionAlias(BackendDAE& d, Interner& in)
      : d_(d), in_(in), compare_(d.exprs, in), map_(CallKeyHash{}, CallKeyEq{&compare_}), fixed_(in.intern("fixed")) {
    for (const UserFunction& f : d.functions) {
      std::string key;
      for (Symbol s : f.name) key += std::string(in.str(s)) + '.';
      user_.emplace(std::move(key), &f);
    }
  }

  ModuleResult run() {
    for (Id<Eqn> id : d_.eqns.simulation.items) clocks(id);
    // 先收 simulation 方程里的调用（:243-244）。EquationPointers.map 按数组序走（NBEquation.mo:4018-4045），就是 simulation 视图的序。
    for (Id<Eqn> id : d_.eqns.simulation.items) equation(id, /*init=*/false);
    if (!r_.unsupported) simulation_aux();
    // 再收初值方程里的（:274-275）。
    if (!r_.unsupported)
      for (Id<Eqn> id : d_.eqns.initials.items) equation(id, /*init=*/true);
    if (!r_.unsupported) initial_aux();
    if (!r_.unsupported) add();
    return r_;
  }

 private:
  void stop(std::string why) {
    if (r_.unsupported) return;
    r_.unsupported = true;
    r_.unsupported_reason = std::move(why);
  }

  // ---- 时钟常量：见文件头。addClockedAlias 用 EquationPointers.mapExp 扫 simulation 的全部方程，when / if / 算法也算。

  void clocks(Id<Eqn> id) {
    const Eqn& q = d_.eqn_arena[id];
    clock_exp(q.lhs);
    clock_exp(q.rhs);
    for (const WhenBranch& br : q.when) {
      clock_exp(br.condition);
      for (const WhenStmt& st : br.stmts) {
        clock_exp(st.a);
        clock_exp(st.b);
        clock_exp(st.c);
      }
    }
    for (const IfBranch& br : q.if_branches) {
      clock_exp(br.condition);
      clocks(br.eqn);
    }
    clock_stmts(q.alg);
  }
  void clock_stmts(const std::vector<Stmt>& stmts) {
    for (const Stmt& st : stmts) {
      clock_exp(st.a);
      clock_exp(st.b);
      clock_exp(st.c);
      for (const StmtBranch& br : st.branches) {
        clock_exp(br.condition);
        clock_stmts(br.body);
      }
      clock_stmts(st.body);
    }
  }
  void clock_exp(Id<Expr> id) {
    if (!id.is_valid() || r_.unsupported) return;
    const Expr& e = d_.exprs[id];
    if (e.ty.kind == TypeKind::Clock) return stop("clock constructor");
    clock_exp(e.a);
    clock_exp(e.b);
    clock_exp(e.c);
    for (Id<Expr> x : e.args) clock_exp(x);
    for (Id<Expr> x : e.inv_args) clock_exp(x);
  }

  // ---- introduceFunctionAliasEquation :339-371

  void equation(Id<Eqn> id, bool init) {
    if (r_.unsupported) return;
    // WHEN、IF、ALGORITHM 整条跳过（:361-363）：辅助方程没有条件，提到外面会改变语义（:123-128 的 ToDo）。
    if (d_.eqn_arena[id].kind != EqnKind::Scalar) return;
    // Equation.map 的 SCALAR_EQUATION 支（NBEquation.mo:1412-1421）：先 lhs 后 rhs，各自走 Expression.map。lhs 上的调用编号在前（`sin(y) = …`）。
    const Id<Expr> lhs = map(d_.eqn_arena[id].lhs, init);
    const Id<Expr> rhs = map(d_.eqn_arena[id].rhs, init);
    if (r_.unsupported) return;
    Eqn& q = d_.eqn_arena[id];
    q.lhs = lhs;
    q.rhs = rhs;
  }

  // Expression.map（NFExpression.mo:2651-2810）：深度优先、后序，先换子式再对本节点调 introduceFunctionAlias。所以 `sin(cos(t))` 先提
  // cos(t) 成 $FUN_1，再提 sin($FUN_1) 成 $FUN_2。各节点子式的次序照原文：IF 先条件再两支，MULTARY 先正项再逆项。
  // 只有 CALL 会被 introduceFunctionAlias 换掉（:383-410，TUPLE_ELEMENT 那支走不到），别的节点只是带着换过的子式重建。
  Id<Expr> map(Id<Expr> id, bool init) {
    if (r_.unsupported) return id;
    Expr e = d_.exprs[id];   // 拷贝：下面往 arena 里加节点
    switch (e.kind) {
      case ExprKind::IntLit:
      case ExprKind::RealLit:
      case ExprKind::BoolLit:
      case ExprKind::StringLit:
      case ExprKind::EnumLit:
      case ExprKind::Cref:   // ComponentRef.mapExp 只走下标，标量 cref 没有
        return id;
      case ExprKind::Binary:
      case ExprKind::Relation:
      case ExprKind::Logical: {
        const Id<Expr> x = map(e.a, init), y = map(e.b, init);
        if (x == e.a && y == e.b) return id;
        e.a = x;
        e.b = y;
        return d_.exprs.add(std::move(e));
      }
      case ExprKind::Unary:
      case ExprKind::LogicalNot:
      case ExprKind::Cast: {
        const Id<Expr> x = map(e.a, init);
        if (x == e.a) return id;
        e.a = x;
        return d_.exprs.add(std::move(e));
      }
      case ExprKind::IfExp: {
        const Id<Expr> c = map(e.a, init), t = map(e.b, init), f = map(e.c, init);
        if (c == e.a && t == e.b && f == e.c) return id;
        e.a = c;
        e.b = t;
        e.c = f;
        return d_.exprs.add(std::move(e));
      }
      case ExprKind::Multary: {
        bool changed = false;
        for (std::vector<Id<Expr>>* side : {&e.args, &e.inv_args}) {
          for (Id<Expr>& x : *side) {
            const Id<Expr> y = map(x, init);
            changed = changed || y != x;
            x = y;
          }
        }
        return changed ? d_.exprs.add(std::move(e)) : id;
      }
      case ExprKind::Call: {
        bool changed = false;
        for (Id<Expr>& x : e.args) {
          const Id<Expr> y = map(x, init);
          changed = changed || y != x;
          x = y;
        }
        const Id<Expr> call = changed ? d_.exprs.add(std::move(e)) : id;
        if (r_.unsupported) return id;
        return replace(call) ? alias(call, init) : call;
      }
      default:
        stop("expression kind");
        return id;
    }
  }

  // checkCallReplacement（:478-486）：not (Inline.functionInlineable or Function.isSpecialBuiltin or replaceException)。
  bool replace(Id<Expr> call) {
    const std::vector<Symbol>& name = d_.exprs[call].call_name;
    std::string key;
    for (Symbol s : name) key += std::string(in_.str(s)) + '.';
    if (const auto it = user_.find(key); it != user_.end()) {
      const UserFunction& f = *it->second;
      // 用户函数不是内建：isSpecialBuiltin 假（:1810-1811）；replaceException 只看记录构造、impure、无输出（:495-503），之后 isBuiltin 假 → 换（:504-505）。
      return !(f.inlineable || f.record_ctor || f.impure || f.no_output);
    }
    // 内建（和 external "builtin" / 认得的数学 external "C"，NFFunction.makeAttributes :2504-2516）：nameConsiderBuiltin 是单个标识符。
    if (name.size() != 1) {
      stop("call of a function without definition");
      return false;
    }
    const std::string_view n = in_.str(name[0]);
    if (listed(kSpecial, n) || listed(kKeep, n)) return false;
    // ponytail: 整个模型一刀切；要细到单个调用得在压平的调用节点上留纯度。
    if (d_.impure_builtin_equiv) {
      stop("impure external function printed as a builtin");
      return false;
    }
    if (listed(kReplace, n)) return true;
    stop("builtin " + std::string(n));
    return false;
  }

  // introduceAlias :426-476。
  Id<Expr> alias(Id<Expr> call, bool init) {
    const CallKey key{call, string_hash_djb2(exp_string(d_.exprs, in_, call))};
    if (CallAux* aux = map_.get(key)) {
      // 已有：用它的变量，但重新 add 一个 parsed = false 的 CALL_AUX（:455-458、:473-475）。所以 simulation 里提过的调用在初值方程里再遇到，
      // 初值那一轮会再给它造一条方程、把变量再挂进 Parameter 桶（见 initial_aux）。键还是第一次的（Map.add 只改值，UnorderedMap.mo:161-177）。
      aux->parsed = false;
      return aux->replacer;
    }
    const Type ty = d_.exprs[call].ty;
    switch (ty.kind) {
      case TypeKind::Real:
      case TypeKind::Integer:
      case TypeKind::Boolean:
      case TypeKind::String:
        break;
      // 枚举要打类型全名（InstNode.scopePath，实跑 `enumeration E(a, b, c) $FUN_2`），Type::enum_path 只有简名，同 lower 的 enumeration iterator。
      case TypeKind::Enumeration: stop("enumeration result"); return call;
      default: stop("result type"); return call;
    }
    // Call_Aux.createName :190-208 → BVariable.makeAuxVar（NBVariable.mo:1264-1291）：名字 `$FUN_<aux_index>`，aux_index 从 1 起（:226），
    // 每造一个加一；种类 VariableKind.fromType（NFBackendExtension.mo:293-311）：初值方程里的是 PARAMETER，否则 Type.isDiscrete 的是
    // DISCRETE，其余 ALGEBRAIC。属性是 DUMMY_BACKEND_INFO 的空属性（NFBackendExtension.mo:177），声明前缀全缺省，所以只打类型和名字。
    Var v;
    v.name.parts.push_back(ExprCrefPart{in_.intern("$FUN_" + std::to_string(aux_index_++)), {}});
    v.ty = ty;
    v.kind = init ? VarKind::Parameter : ty.kind == TypeKind::Real ? VarKind::Algebraic : VarKind::Discrete;
    Expr ref;
    ref.kind = ExprKind::Cref;
    ref.ty = ty;
    ref.cref = v.name;
    CallAux aux;
    aux.var = d_.var_arena.add(std::move(v));
    aux.replacer = d_.exprs.add(std::move(ref));
    map_.add(key, aux);
    return aux.replacer;
  }

  // ---- functionAliasDefault 的两轮造方程 :247-267、:278-293

  // Equation.makeAssignment（NBEquation.mo:1187-1201）：`replacer = 调用`，名字 `$RES_AUX_<uniqueIndex>`（createName :1769-1795），uniqueIndex 接着往上。
  Id<Eqn> assignment(const CallKey& key, const CallAux& aux, EqnTime time) {
    Eqn q;
    q.lhs = aux.replacer;
    q.rhs = key.call;
    q.name.parts.push_back(ExprCrefPart{in_.intern("$RES_AUX_" + std::to_string(d_.eqns.unique_index++)), {}});
    q.time = time;
    return d_.eqn_arena.add(std::move(q));
  }

  // simulation 一轮（:247-267）：Map 按插入序（UnorderedMap.toList）倒着走，所以**最后提的调用拿最小的 $RES_AUX 号**。新变量、新方程都头插进表，
  // 走完又回到插入序：$FUN_1 的变量和方程排在最前。连续 / 离散看变量：BVariable.isContinuous(var, init = false)（NBVariable.mo:373-385），
  // ALGEBRAIC 是连续，DISCRETE 不是（addAuxVar :585-590）。
  void simulation_aux() {
    const std::size_t n = static_cast<std::size_t>(map_.size());
    sim_eqns_.resize(n);
    for (std::size_t i = n; i-- > 0;) {
      const CallKey key = map_.keys()[i];
      CallAux* aux = map_.get(key);   // `aux.parsed := true; UnorderedMap.add(id, aux, map)`（:265-266）：按键找回自己
      const bool disc = d_.var_arena[aux->var].kind != VarKind::Algebraic;
      sim_eqns_[i] = assignment(key, *aux, disc ? EqnTime::Discrete : EqnTime::Continuous);
      aux->parsed = true;
    }
    sim_count_ = n;
  }

  // 初值一轮（:278-293）：同样倒着走整个 Map，只给 parsed 为假的造。这包括 simulation 里提过、又在初值方程里遇到的（见 alias）：变量是
  // simulation 那个 [ALGB] / [DISC]，照样 setFixed(false) 再挂进 Parameter 桶；指针共享，所以 Algebraic 桶里那一行也带上 `(fixed = false)`。
  // setFixed 不覆盖已有的 fixed（overwrite = false，NFBackendExtension.mo:559-566），这些变量原来都没有。
  void initial_aux() {
    const std::size_t n = static_cast<std::size_t>(map_.size());
    init_eqns_.assign(n, Id<Eqn>{});
    for (std::size_t i = n; i-- > 0;) {
      const CallAux& aux = map_.values()[i];
      if (aux.parsed) continue;
      Var& v = d_.var_arena[aux.var];
      bool has_fixed = false;
      for (const FlatVarAttr& a : v.attrs) has_fixed = has_fixed || a.name == fixed_;
      if (!has_fixed) {
        Expr f;
        f.kind = ExprKind::BoolLit;
        f.ty = Type::boolean();
        f.bval = false;
        v.attrs.push_back(FlatVarAttr{fixed_, d_.exprs.add(std::move(f))});
      }
      const bool disc = d_.exprs[aux.replacer].ty.kind != TypeKind::Real;   // CALL_AUX.kind（:474），初值桶里不影响什么
      init_eqns_[i] = assignment(map_.keys()[i], aux, disc ? EqnTime::Discrete : EqnTime::Continuous);
    }
  }

  // :301-310：变量 ALGEBRAIC → unknowns + algebraics，DISCRETE → unknowns + discretes，初值的 PARAMETER → parameters（VarData.addTypedList
  // NBVariable.mo:2290-2370）；方程 CONTINUOUS / DISCRETE 各进自己的桶和 simulation，INITIAL 进 initials（EqData.addTypedList NBEquation.mo:4498-4550）。
  // 都是 addList，按表序接在桶尾。迭代量集合恒空（没有 for），时钟见 clocks。
  void add() {
    VarData& vd = d_.vars;
    EqnData& ed = d_.eqns;
    for (std::size_t i = 0; i < sim_count_; ++i) {
      const Id<Var> v = map_.values()[i].var;
      (d_.var_arena[v].kind == VarKind::Algebraic ? vd.algebraics : vd.discretes).items.push_back(v);
      vd.unknowns.items.push_back(v);
    }
    for (std::size_t i = 0; i < init_eqns_.size(); ++i)
      if (init_eqns_[i].is_valid()) vd.parameters.items.push_back(map_.values()[i].var);
    // 新方程表也是头插（:259-263），回到插入序；连续的先加（:307-308）。三种都同时进 equations（addTypedList NBEquation.mo:4512、:4523、:4543）。
    for (EqnTime t : {EqnTime::Continuous, EqnTime::Discrete}) {
      for (Id<Eqn> q : sim_eqns_) {
        if (d_.eqn_arena[q].time != t) continue;
        (t == EqnTime::Continuous ? ed.continuous : ed.discretes).items.push_back(q);
        ed.simulation.items.push_back(q);
        ed.equations.items.push_back(q);
      }
    }
    for (Id<Eqn> q : init_eqns_) {
      if (!q.is_valid()) continue;
      ed.initials.items.push_back(q);
      ed.equations.items.push_back(q);
    }
  }

  BackendDAE& d_;
  Interner& in_;
  ExpCompare compare_;
  UnorderedMap<CallKey, CallAux, CallKeyHash, CallKeyEq> map_;   // UnorderedMap.new：1 个桶（:222）
  Symbol fixed_;
  std::unordered_map<std::string, const UserFunction*> user_;     // 压平名（每段后跟 '.'）→ 函数
  int aux_index_ = 1;
  std::size_t sim_count_ = 0;
  std::vector<Id<Eqn>> sim_eqns_, init_eqns_;   // 按 Map 的插入序
  ModuleResult r_;
};

}  // namespace

ModuleResult function_alias(BackendDAE& dae, Interner& in) { return FunctionAlias(dae, in).run(); }

}  // namespace ymc::bdae
