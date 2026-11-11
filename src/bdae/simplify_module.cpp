// 函数名、分支顺序照 OMC v1.25.0：NBackendDAE.simplify（Classes/NBackendDAE.mo:386-444）；NBEquation.mo 的 Equation.simplify :1652-1767、
// Equation.map :1389-1485、Equation.getLHS / getRHS :1522-1562、IfEquationBody.map :2715-2754、inline / getLHS / getRHS :2825-2888、
// simplify :2912-2939、WhenEquationBody.map :3113-3131、simplify :3259-3297、WhenStatement.map :3607-3670、EquationPointers.compress
// :4226-4251；DetectStates.findDiscreteStatesFromWhenBody（Modules/2_Pre/NBDetectStates.mo:587-612）；NFSimplifyModel.mo 的 simplifyStatements
// :289-380、simplifyIfStmtBranches :556-601；Statement.mapExp（NFStatement.mo:559-634）。
//
// 式子的化简是 SimplifyExp.simplify（bdae/simplify.cpp）。Equation.simplify 用 `mapFunc = apply`：每个顶层式子直接调一次 simplify，
// 不走 Expression.map（simplify 自己会递归）。
//
// 走不到的分支不写：ARRAY / RECORD / FOR / AUX 方程（lower 挡了）；when 里的 $PRE 变量（getVarPre，:601-606）：这一段还没有 $PRE 变量
// （Previous 桶是 DetectStates 才填的），ponytail: DetectStates 做完后照 :428-429 补 previous / variables 的删除。
#include "bdae/simplify_module.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bdae/exp_compare.h"
#include "bdae/simplify.h"

namespace ymc::bdae {

namespace {

class EqnSimplifier {
 public:
  EqnSimplifier(BackendDAE& d, Interner& in) : d_(d), in_(in), compare_(d.exprs, in) {}

  ModuleResult run() {
    // EquationPointers.map(eqData.equations, func)（:414）。eqData.equations 是"全部方程"（见 EqnData::equations），初值和 removed 的也在里面，
    // 但 Alias 的非平凡别名绑定方程不在（它们只进 removed），所以第二次 Simplify 不碰它们。每条方程恰好化简一遍（不像 EqData.mapExp 那样
    // simulation 和 continuous / discretes 各走一遍）。这里的先后只影响改成参数的变量的序，那个序在 vars() 里按 omc 的走法单独算。
    const std::vector<Id<Eqn>> all = d_.eqns.equations.items;
    for (Id<Eqn> id : all) {
      equation(id);
      if (r_.unsupported) return r_;
    }
    const std::vector<Id<Eqn>> discretes = d_.eqns.discretes.items;   // compress 之前的序，vars() 要
    compress();
    vars(discretes);
    return r_;
  }

 private:
  void stop(std::string why) {
    if (r_.unsupported) return;
    r_.unsupported = true;
    r_.unsupported_reason = std::move(why);
  }

  // SimplifyExp.simplifyDump(includeScope = true)（NFSimplifyExp.mo:62-76）：只是 simplify 外面包一层打印。includeScope 只影响 cref 的类型重算，
  // 标量 cref 打印不变。
  Id<Expr> s(Id<Expr> e) {
    if (!e.is_valid() || r_.unsupported) return e;
    std::string why;
    const Id<Expr> out = simplify(d_.exprs, in_, e, why);
    if (!why.empty()) stop(why);
    return out;
  }
  bool is_bool(Id<Expr> id, bool v) const {
    const Expr& e = d_.exprs[id];
    return e.kind == ExprKind::BoolLit && e.bval == v;
  }

  // ---- Equation.simplify :1652-1767

  void equation(Id<Eqn> id) {
    Eqn q = d_.eqn_arena[id];   // 拷贝：if 体里的方程在 arena 里原地改（Pointer.apply）
    map_eqn(q);                 // :1676 先化简所有式子
    if (r_.unsupported) return;
    bool keep = true;           // false = DUMMY_EQUATION
    switch (q.kind) {           // :1680 再看方程结构
      case EqnKind::Scalar: scalar(q); break;
      case EqnKind::When: keep = when(id, q); break;
      case EqnKind::If: keep = if_equation(q); break;
      case EqnKind::Algorithm: q.alg = statements(q.alg); break;   // :1709-1711 SimplifyModel.simplifyAlgorithm
    }
    if (r_.unsupported) return;
    if (!keep) {
      dummies_.insert(id.raw);
      return;
    }
    d_.eqn_arena[id] = std::move(q);   // Pointer.update：同一指针在各桶里都看得见
  }

  // Equation.map(eq, simplifyExp, mapFunc = apply)（:1389-1485）。
  void map_eqn(Eqn& q) {
    switch (q.kind) {
      case EqnKind::Scalar:
        q.lhs = s(q.lhs);
        q.rhs = s(q.rhs);
        break;
      case EqnKind::When:   // WhenEquationBody.map + WhenStatement.map：reinit 的状态是 cref（只给 funcCrefOpt），assert 不走级别
        for (WhenBranch& br : q.when) {
          br.condition = s(br.condition);
          for (WhenStmt& st : br.stmts) {
            if (st.kind != WhenStmtKind::Reinit) st.a = s(st.a);
            st.b = s(st.b);
          }
        }
        break;
      case EqnKind::If:     // IfEquationBody.map：条件，再是体里的方程（Pointer.apply，只走 Equation.map，体里的方程不做结构化简）
        for (IfBranch& br : q.if_branches) {
          br.condition = s(br.condition);   // else 支无效，s 原样返回
          Eqn inner = d_.eqn_arena[br.eqn];
          map_eqn(inner);
          d_.eqn_arena[br.eqn] = std::move(inner);
        }
        break;
      case EqnKind::Algorithm:   // Algorithm.mapExp → Statement.mapExp；改了的话 setInputsOutputs 重算输入输出，size 不动（:1443-1449）
        map_stmts(q.alg);
        break;
    }
  }

  // Statement.mapExp（NFStatement.mo:559-634）：for 先体后范围，assert 连级别也走。
  void map_stmts(std::vector<Stmt>& stmts) {
    for (Stmt& st : stmts) {
      switch (st.kind) {
        case StmtKind::For:
          map_stmts(st.body);
          st.a = s(st.a);
          break;
        case StmtKind::If:
        case StmtKind::When:
          for (StmtBranch& br : st.branches) {
            br.condition = s(br.condition);
            map_stmts(br.body);
          }
          break;
        case StmtKind::While:
          st.a = s(st.a);
          map_stmts(st.body);
          break;
        case StmtKind::Assign:
        case StmtKind::Assert:
        case StmtKind::Terminate:
        case StmtKind::NoRetCall:
          st.a = s(st.a);
          st.b = s(st.b);
          st.c = s(st.c);
          break;
        case StmtKind::Break:
          break;
      }
    }
  }

  // SCALAR_EQUATION 支（:1688-1693）：两边相等就换成 makeZero(eq.ty) = makeZero(eq.ty)。makeZero（NFExpression.mo:4836-4850）只有 Real、Integer、
  // Boolean（还有数组、记录），别的类型报内部错，整个模块失败，omc 没有这一段（实跑 EnumEquation1 的 `E.a = E.a`、探针 Ls1E_str1 的 `s = s`）。
  // eq.ty 是等式的类型：两边已被 TypeCheck 转成同型，取左边的（同 lower 的 eqn_time）。
  void scalar(Eqn& q) {
    if (compare_(q.lhs, q.rhs) != 0) return;
    Type t = d_.exprs[q.lhs].ty;
    if (t.kind == TypeKind::Unknown) t = d_.exprs[q.rhs].ty;
    const auto zero = [&]() {
      Expr e;
      e.ty = t;
      switch (t.kind) {
        case TypeKind::Real: e.kind = ExprKind::RealLit; e.rval = 0.0; break;
        case TypeKind::Integer: e.kind = ExprKind::IntLit; e.ival = 0; break;
        default: e.kind = ExprKind::BoolLit; e.bval = false; break;
      }
      return d_.exprs.add(std::move(e));
    };
    if (t.kind != TypeKind::Real && t.kind != TypeKind::Integer && t.kind != TypeKind::Boolean)
      return stop("equation with equal sides of a type without zero (omc fails in this module)");
    q.lhs = zero();
    q.rhs = zero();
  }

  // WHEN_EQUATION 支（:1713-1722）+ WhenEquationBody.simplify（:3259-3297）。递归是从后往前化简 else_when，每一支只看自己的条件，所以逐支走就行：
  //   - 条件是数组：去掉字面量 false 的元素；剩一个就用它，剩几个就是新数组。全是 false 时那一支 `body := b.else_when` 被后面的 `then SOME(b)`
  //     盖掉，原样留着（探针 Ls1W_arr2：`when {false, false}` 不变，omc 到 Events 才报错）；
  //   - 否则条件是 false 就跳过这一支（elsewhen 顶上来）。
  // 全部跳过时方程变 DUMMY，第一支里赋值的变量记下来（findDiscreteStatesFromWhenBody 看的是 map 之后、结构化简之前的 eq.body，只看第一支）。
  bool when(Id<Eqn> id, Eqn& q) {
    std::vector<WhenBranch> out;
    for (const WhenBranch& br : q.when) {
      const Expr& c = d_.exprs[br.condition];
      if (c.kind != ExprKind::Array) {
        if (!is_bool(br.condition, false)) out.push_back(br);
        continue;
      }
      std::vector<Id<Expr>> keep;
      for (Id<Expr> x : c.args)
        if (!is_bool(x, false)) keep.push_back(x);
      WhenBranch b = br;
      if (keep.size() == 1) {
        b.condition = keep.front();
      } else if (!keep.empty() && keep.size() != c.args.size()) {   // makeArrayCheckLiteral(Boolean[n], …)
        Expr arr = c;
        arr.args = std::move(keep);
        if (arr.ty.is_array() && arr.ty.dims.size() == 1) {
          arr.ty.dims[0].kind = DimKind::Integer;
          arr.ty.dims[0].size = static_cast<std::int64_t>(arr.args.size());
        }
        b.condition = d_.exprs.add(std::move(arr));
      }
      out.push_back(std::move(b));
    }
    if (!out.empty()) {
      q.when = std::move(out);
      return true;
    }
    std::vector<Id<Var>> acc;
    for (const WhenStmt& st : q.when.front().stmts) {
      if (st.kind != WhenStmtKind::Assign) continue;   // 只有 ASSIGN(lhs = CREF) 这一支（:599）；reinit、assert 的 when 删了不动变量
      const Id<Var> v = var_of(st.a);
      if (!v.is_valid()) {
        stop("when target is not a variable");
        return false;
      }
      acc.push_back(v);
    }
    if (!acc.empty()) removed_when_.emplace(id.raw, std::move(acc));
    return false;
  }

  // BVariable.getVarPointer(cref)：lower 时 cref 已指向后端变量，这里按名字找。
  Id<Var> var_of(Id<Expr> cref) {
    if (names_.empty()) {
      const VarData& vd = d_.vars;
      for (const VarSet* set : {&vd.unknowns, &vd.states, &vd.algebraics, &vd.discretes, &vd.top_level_inputs, &vd.parameters, &vd.constants})
        for (Id<Var> v : set->items) names_.emplace(dotted(d_.var_arena[v].name), v);
    }
    const Expr& e = d_.exprs[cref];
    if (e.kind != ExprKind::Cref) return {};
    const auto it = names_.find(dotted(e.cref));
    return it == names_.end() ? Id<Var>{} : it->second;
  }
  std::string dotted(const ExprCref& c) const {
    std::string s;
    for (const ExprCrefPart& p : c.parts) s += std::string(in_.str(p.name)) + '.';
    return s;
  }

  // IF_EQUATION 支（:1724-1742）+ IfEquationBody.simplify（:2912-2939）：条件是 true 的那一支变成 else（END）、后面的砍掉；条件是 false 的跳过。
  //   - 一支都不剩：DUMMY。
  //   - 只剩一支（没有 else_if）、体里一条方程（split 之后恒成立）：整条换成体里那条方程，名字也是它的。原文的注释说"第一支是 true"，判的却只是
  //     "没有 else_if"，所以没有 else 的初值 if 也这样：`if c then x = 1; end if;` 变成 `x = 1`，条件丢了（探针 Ls1I_initnoelse1）。
  //   - 否则试 IfEquationBody.inline（:2825-2838）：各支左边都相等时并成 `lhs = if c1 then r1 else if … else rn`，名字、种类不变。
  //     左边不等就不动；Equation.getLHS 报内部错 fail 时（嵌套 if 的左边不唯一，探针 Ls1I_nested2）被 try 接住，也不动。
  bool if_equation(Eqn& q) {
    std::vector<IfBranch> out;
    for (const IfBranch& br : q.if_branches) {
      if (br.condition.is_valid() && is_bool(br.condition, true)) {
        out.push_back({Id<Expr>{}, br.eqn});
        break;
      }
      if (br.condition.is_valid() && is_bool(br.condition, false)) continue;
      out.push_back(br);
    }
    if (out.empty()) return false;
    if (out.size() == 1) {
      q = d_.eqn_arena[out.front().eqn];
      return true;
    }
    q.if_branches = out;
    Id<Expr> lhs;
    bool equal = true;
    if (!if_lhs(out, lhs, equal) || !equal) return true;
    // rhs := SimplifyExp.simplify(getRHS(body))：各支的右边和条件在 map 里化简过，拼成 IF 之后整个再化简一遍。
    const Id<Expr> rhs = s(if_rhs(out, 0));
    if (r_.unsupported) return true;
    // Equation.makeAssignmentUpdate（:1203-1214）：SCALAR_EQUATION(typeOf(lhs), lhs, rhs, attr = 原方程的)，残差变量（名字）沿用。
    Eqn n;
    n.kind = EqnKind::Scalar;
    n.name = q.name;
    n.lhs = lhs;
    n.rhs = rhs;
    n.size = 1;
    n.time = q.time;
    q = std::move(n);
    return true;
  }

  // IfEquationBody.getLHS（:2840-2867）：逐支取左边，和上一支比；不等就停（success = false）。返回 false = 抛错（Equation.getLHS 的 else 支，
  // 或者嵌套 if 的左边不等时 Equation.getLHS 报内部错 fail，:1534-1543）。lhs 是最后看的那一支的左边。
  bool if_lhs(const std::vector<IfBranch>& brs, Id<Expr>& lhs, bool& equal) {
    equal = true;
    lhs = {};
    for (const IfBranch& br : brs) {
      Id<Expr> l;
      if (!eqn_lhs(br.eqn, l)) return false;
      if (lhs.is_valid() && compare_(lhs, l) != 0) {
        equal = false;
        return true;
      }
      lhs = l;
    }
    return true;
  }
  bool eqn_lhs(Id<Eqn> id, Id<Expr>& l) {
    const Eqn& e = d_.eqn_arena[id];
    if (e.kind == EqnKind::Scalar) {
      l = e.lhs;
      return true;
    }
    if (e.kind != EqnKind::If) return false;
    const std::vector<IfBranch> inner = e.if_branches;
    bool equal = true;
    return if_lhs(inner, l, equal) && equal;
  }
  // IfEquationBody.getRHS（:2869-2888）：IF(typeOf(r_i), cond_i, r_i, 后面的)。最后一支只取右边，它的条件不看（没有 else 时条件就丢了）。
  Id<Expr> if_rhs(const std::vector<IfBranch>& brs, std::size_t i) {
    const Id<Expr> r = eqn_rhs(brs[i].eqn);
    if (i + 1 == brs.size()) return r;
    Expr e;
    e.kind = ExprKind::IfExp;
    e.ty = d_.exprs[r].ty;
    e.a = brs[i].condition;
    e.b = r;
    e.c = if_rhs(brs, i + 1);
    return d_.exprs.add(std::move(e));
  }
  Id<Expr> eqn_rhs(Id<Eqn> id) {
    const Eqn& e = d_.eqn_arena[id];
    if (e.kind == EqnKind::Scalar) return e.rhs;
    const std::vector<IfBranch> inner = e.if_branches;   // eqn_lhs 成功过，这里只会是 if
    return if_rhs(inner, 0);
  }

  // ---- SimplifyModel.simplifyStatements（NFSimplifyModel.mo:289-380）。式子在 map 里化简过，这里再化简一遍（simplify 调两次）。

  std::vector<Stmt> statements(const std::vector<Stmt>& in) {
    std::vector<Stmt> out;
    for (const Stmt& st : in) statement(st, out);
    return out;
  }
  void statement(const Stmt& in, std::vector<Stmt>& out) {
    Stmt st = in;
    switch (st.kind) {
      case StmtKind::Assign:    // simplifyAssignment :382-409：元组对元组的拆分只有字面量元组的右边，算法里走不到
        st.a = s(st.a);
        st.b = s(st.b);
        break;
      case StmtKind::For:
        if (st.body.empty()) return;            // :313
        if (empty_range(st.a)) return;          // :326 范围的第一维是 0
        st.a = s(st.a);
        st.body = statements(st.body);
        break;
      case StmtKind::If:
        return if_statement(st, out);
      case StmtKind::When:
        for (StmtBranch& br : st.branches) {
          br.condition = s(br.condition);
          br.body = statements(br.body);
        }
        break;
      case StmtKind::Assert:
        st.a = s(st.a);
        st.b = s(st.b);
        st.c = s(st.c);
        break;
      case StmtKind::Terminate:
        st.a = s(st.a);
        break;
      case StmtKind::While:
        st.a = s(st.a);
        st.body = statements(st.body);
        break;
      case StmtKind::NoRetCall: {               // :367-376 化简后不是调用就删掉；removeEmptyFunctionArguments 只管空数组实参
        const Id<Expr> e = s(st.a);
        if (d_.exprs[e].kind != ExprKind::Call) return;
        st.a = e;
        break;
      }
      case StmtKind::Break:
        break;
    }
    out.push_back(std::move(st));
  }
  // Type.nthDimension(typeOf(range), 1) 是 0：只算得出整数字面量的 a:b、a:s:b（别的范围维不是已知的 0）。
  bool empty_range(Id<Expr> id) const {
    const Expr& r = d_.exprs[id];
    if (r.kind != ExprKind::Range) return false;
    const auto lit = [&](Id<Expr> x, std::int64_t& v) {
      if (!x.is_valid() || d_.exprs[x].kind != ExprKind::IntLit) return false;
      v = d_.exprs[x].ival;
      return true;
    };
    std::int64_t a = 0, b = 0, c = 0;
    if (!r.c.is_valid()) return lit(r.a, a) && lit(r.b, b) && b < a;
    if (!lit(r.a, a) || !lit(r.b, b) || !lit(r.c, c) || b == 0) return false;
    return b > 0 ? c < a : c > a;
  }
  // simplifyIfStmtBranches（:556-601）：条件是 true 的那一支，是第一支就把 if 换成它的体，否则留着它、砍掉后面的；条件是 false 的跳过。
  void if_statement(const Stmt& st, std::vector<Stmt>& out) {
    std::vector<StmtBranch> accum;
    for (const StmtBranch& br : st.branches) {
      const Id<Expr> c = s(br.condition);
      if (r_.unsupported) return;
      if (is_bool(c, true)) {
        if (accum.empty()) {
          for (Stmt& x : statements(br.body)) out.push_back(std::move(x));
          return;
        }
        accum.push_back({c, statements(br.body)});
        break;
      }
      if (!is_bool(c, false)) accum.push_back({c, statements(br.body)});
    }
    if (accum.empty()) return;
    Stmt n = st;
    n.branches = std::move(accum);
    out.push_back(std::move(n));
  }

  // ---- EqData.compress（NBEquation.mo:4625-4658）：各桶删掉 DUMMY，其余不动序。

  void compress() {
    EqnData& ed = d_.eqns;
    for (EqnSet* set : {&ed.equations, &ed.simulation, &ed.continuous, &ed.discretes, &ed.initials, &ed.removed}) {
      std::vector<Id<Eqn>> keep;
      for (Id<Eqn> id : set->items)
        if (!dummies_.count(id.raw)) keep.push_back(id);
      set->items = std::move(keep);
    }
  }

  // :418-437：整条消失的 when 里赋值的变量从 unknowns、discretes 删掉，加进 parameters，种类改 PARAMETER（打印 [PRMT]，声明前缀不变，
  // 实跑 Ls1W_false1 / 2 / 3）。acc_discrete_states 是头插的：eqData.equations 按插入序走（lower 的方程按 $RES_SIM 号从小到大），最后走到的在表头，
  // addList 从表头加，所以号大的先进 parameters。when 方程只来自 lower，号大的就是 Discrete 桶里靠前的（打印序 = 源序），所以按 compress 之前的
  // discretes 序走；同一条方程里的变量也是后记的先加（实跑 Ls1W_two：先写的 when 的变量在前）。
  //
  // 这几行的 removeList / addList 返回值都没接（:423-432）：VariablePointers 底下的 ExpandableArray 和 Map 是原地改的，所以删除生效，但 removeList
  // 末尾的 compress 结果丢了，桶里留着空位。打印 VariablePointers.toString（NBVariable.mo:1575-1609）按 1..元素个数逐个 ExpandableArray.get，
  // 不看空位，碰到空位就 fail，omc 打不出这一段（实跑 Ls1W_order2：`when false then z = 1` 的 z 排在留下的 v 前面）。所以删掉的变量必须是 Discrete 桶
  // 的末尾几个，否则 SKIP；是末尾时空位都在 lastUsedIndex 之后（ExpandableArray.delete 会把它往回收），打印同压紧的。unknowns 只打标量个数
  //（toList 跳过空位），不受影响。
  void vars(const std::vector<Id<Eqn>>& discretes) {
    if (removed_when_.empty()) return;
    std::size_t seen = 0;
    VarData& vd = d_.vars;
    {
      std::unordered_set<std::uint32_t> gone;
      for (const auto& [eqn, vs] : removed_when_)
        for (Id<Var> v : vs) gone.insert(v.raw);
      std::size_t found = 0;
      for (Id<Var> v : vd.discretes.items) found += gone.count(v.raw);
      if (found != gone.size()) return stop("when target outside the discrete bucket");
      const std::size_t keep = vd.discretes.items.size() - found;
      for (std::size_t i = 0; i < keep; ++i)
        if (gone.count(vd.discretes.items[i].raw))
          return stop("removed when target ahead of a kept discrete variable (omc fails printing the bucket)");
    }
    for (Id<Eqn> e : discretes) {
      const auto it = removed_when_.find(e.raw);
      if (it == removed_when_.end()) continue;
      ++seen;
      for (auto v = it->second.rbegin(); v != it->second.rend(); ++v) {
        for (VarSet* set : {&vd.unknowns, &vd.discretes}) {   // VariablePointers.removeList：不在就不动，余下的保序
          std::vector<Id<Var>> keep;
          for (Id<Var> x : set->items)
            if (x != *v) keep.push_back(x);
          set->items = std::move(keep);
        }
        bool present = false;
        for (Id<Var> x : vd.parameters.items) present = present || x == *v;
        if (!present) vd.parameters.items.push_back(*v);   // addList：已在就原地更新
        d_.var_arena[*v].kind = VarKind::Parameter;           // setVarKind(PARAMETER(NONE()))
      }
    }
    if (seen != removed_when_.size()) stop("removed when equation outside the discrete bucket");
  }

  BackendDAE& d_;
  Interner& in_;
  ExpCompare compare_;
  ModuleResult r_;
  std::unordered_set<std::uint32_t> dummies_;
  std::unordered_map<std::uint32_t, std::vector<Id<Var>>> removed_when_;   // 整条消失的 when → 第一支里赋值的变量（语句序）
  std::unordered_map<std::string, Id<Var>> names_;                        // 点分名（每段后跟 '.'）→ 变量，用到时才建
};

}  // namespace

ModuleResult simplify_module(BackendDAE& dae, Interner& in) { return EqnSimplifier(dae, in).run(); }

}  // namespace ymc::bdae
