// NBInline.mo（OMC v1.25.0）：inline :222-248、collectInlineFunctions :250-262、functionInlineable :145-158、inlineRecordsTuplesArrays
// :264-289、inlineRecordTupleArrayEquation :291-364、inlineRecordTupleArrayIfEquation / IfBody :366-408；NBReplacements.mo：
// replaceFunctions :268-278、applyFuncExp :280-341、applySimpleExp :187-218、getFunctionBody :399-419。
//
// 遍历的是 EqData.mapExp / EqData.map（NBEquation.mo:4358-4400）：simulation、continuous、clocked、discretes、initials、auxiliaries，不碰
// removed（assert、reinit 的 when、无返回值调用都在那里，原样不动）。simulation 和 continuous / discretes 里放的是同一批方程指针，
// 所以 simulation 里的方程被映射两遍，初值方程一遍。applyFuncExp 换下来的函数体不再往里映射（Expression.map 先子后父，换完就返回），
// 体里再调的可内联函数要等第二遍才换：simulation 里能展开两层，初值方程里只展开一层（实跑 Inline3 的递归阶乘、探针 LeiN 系）。
//
// 走不到的分支不写：
//   - inlineRecordsTuplesArrays 里的记录、元组、数组方程，for 方程，迭代量（lowerEquationIterators、collectIterators）：lower 挡了，
//     函数形参和体也只收标量（UserFunction::not_inlinable）。ponytail: 放开数组、记录时照 :291-364、NBReplacements.mo:343-397 补。
//   - 无绑定的局部被体引用时代成 WILD（:315-318），打 `_`，omc 后面报错；体里引用输出（输出的声明绑定不进替换表）时那个名字漏进方程，
//     又被 collectIterators 当成迭代量收进 Artificial 桶（实跑）。这两种都 SKIP。
#include "bdae/inline.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bdae/combine_binaries.h"
#include "bdae/simplify.h"

namespace ymc::bdae {

namespace {

class Inliner {
 public:
  Inliner(BackendDAE& d, Interner& in) : d_(d), in_(in) {
    // collectInlineFunctions（:250-262）：内联类型在表里、且 functionInlineable。内联类型见 BackendDAE::inline_annotation_seen。
    for (const UserFunction& f : d.functions) {
      std::string key;
      for (Symbol s : f.name) key += std::string(in.str(s)) + '.';
      if (f.impure) impure_.insert(key);
      if (f.inlineable) fns_.emplace(std::move(key), &f);
    }
  }

  ModuleResult run() {
    // FunctionAlias 只在初值方程里提的 `$FUN_n` 是 PARAMETER，VarData.addTypedList 的 PARAMETER 支不把它放进 variables（NBVariable.mo:2335-2338，
    // ALGEBRAIC / DISCRETE / ITERATOR 支都放）。这一段的 lowerEquationIterators（NBInline.mo:245）按 variables 认迭代量，初值方程里有它就失败，
    // omc 不打这一段（实跑：`initial equation c = sin(time);` 有 (FunctionAlias) 没有 (Early Inline)，探针 LeiX_init_sin）。simulation 里也提过的
    // 那种是 ALGEBRAIC / DISCRETE，在 variables 里，不受影响。失败在哪一行没钉死（collectIterators :1393-1427 的 try 会报内部错，实跑没有报），
    // 只照实跑的结果不打。
    for (Id<Var> id : d_.vars.parameters.items) {
      const Var& v = d_.var_arena[id];
      if (v.kind == VarKind::Parameter && v.name.parts.size() == 1 && in_.str(v.name.parts[0].name).rfind("$FUN_", 0) == 0) {
        stop("initial-only function alias variable (omc fails in this module)");
        return r_;
      }
    }
    // replaceFunctions（NBReplacements.mo:274-278）：替换表空就不动；照样往下走，if 方程的重造与它无关。
    if (!fns_.empty()) {
      const EqnData& ed = d_.eqns;
      for (const EqnSet* set : {&ed.simulation, &ed.continuous, &ed.discretes, &ed.initials}) {
        const std::vector<Id<Eqn>> items = set->items;
        for (Id<Eqn> id : items) equation(id);
      }
    }
    if (!r_.unsupported) remake_ifs();
    return r_;
  }

 private:
  void stop(std::string why) {
    if (r_.unsupported) return;
    r_.unsupported = true;
    r_.unsupported_reason = std::move(why);
  }

  // ---- Equation.map（NBEquation.mo:1389-1480）：各种方程里交给 Expression.map 的位置。

  void equation(Id<Eqn> id) {
    if (r_.unsupported) return;
    Eqn q = d_.eqn_arena[id];   // 拷贝：下面递归改 if 体里的方程
    switch (q.kind) {
      case EqnKind::Scalar:
        q.lhs = map(q.lhs);
        q.rhs = map(q.rhs);
        break;
      case EqnKind::When:   // WhenEquationBody.map :3113-3131、WhenStatement.map :3607-3669：reinit 的状态是 cref 映射（这里没有），只走值；assert 不走级别
        for (WhenBranch& br : q.when) {
          br.condition = map(br.condition);
          for (WhenStmt& st : br.stmts) {
            if (st.kind != WhenStmtKind::Reinit) st.a = map(st.a);
            st.b = map(st.b);
          }
        }
        break;
      case EqnKind::If:     // IfEquationBody.map :2715-2727：条件，再是体里的方程（指针，原地改）
        for (IfBranch& br : q.if_branches) {
          br.condition = map(br.condition);   // else 支无效，map 原样返回
          equation(br.eqn);
        }
        break;
      case EqnKind::Algorithm:   // Algorithm.mapExp → Statement.mapExpList（NFStatement.mo:548-634）
        statements(q.alg);
        break;
    }
    if (r_.unsupported) return;
    d_.eqn_arena[id] = std::move(q);
  }

  // Statement.mapExp NFStatement.mo:559-634。for 先体后范围，assert 连级别也走。
  void statements(std::vector<Stmt>& stmts) {
    for (Stmt& st : stmts) {
      switch (st.kind) {
        case StmtKind::For:
          statements(st.body);
          st.a = map(st.a);
          break;
        case StmtKind::If:
        case StmtKind::When:
          for (StmtBranch& br : st.branches) {
            br.condition = map(br.condition);
            statements(br.body);
          }
          break;
        case StmtKind::While:
          st.a = map(st.a);
          statements(st.body);
          break;
        case StmtKind::Assign:
        case StmtKind::Assert:
        case StmtKind::Terminate:
        case StmtKind::NoRetCall:
          st.a = map(st.a);
          st.b = map(st.b);
          st.c = map(st.c);
          break;
        case StmtKind::Break:
          break;
      }
    }
  }

  // Expression.map(exp, applyFuncExp)（NFExpression.mo:2651-2810）：先子后父，子式都换过再看本节点是不是要内联的调用。
  Id<Expr> map(Id<Expr> id) {
    if (!id.is_valid() || r_.unsupported) return id;
    Expr e = d_.exprs[id];   // 拷贝：下面往 arena 里加节点
    bool changed = false;
    const auto child = [&](Id<Expr>& x) {
      if (!x.is_valid()) return;
      const Id<Expr> y = map(x);
      changed = changed || y != x;
      x = y;
    };
    switch (e.kind) {
      case ExprKind::IntLit:
      case ExprKind::RealLit:
      case ExprKind::BoolLit:
      case ExprKind::StringLit:
      case ExprKind::EnumLit:
      case ExprKind::Cref:   // 标量 cref 没有下标可走
      case ExprKind::Wild:   // 算法里元组赋值的空位
        return id;
      case ExprKind::Binary:
      case ExprKind::Relation:
      case ExprKind::Logical:
      case ExprKind::Unary:
      case ExprKind::LogicalNot:
      case ExprKind::Cast:
      case ExprKind::IfExp:
      case ExprKind::Range:   // 算法 for 的范围
        child(e.a);
        child(e.b);
        child(e.c);
        break;
      case ExprKind::Multary:
      case ExprKind::Array:   // when 条件顶层的 `{a, b}`、算法 for 的数字字面量范围
      case ExprKind::Tuple:   // 算法里元组赋值的左边
      case ExprKind::Call:
        for (Id<Expr>& x : e.args) child(x);
        for (Id<Expr>& x : e.inv_args) child(x);
        break;
      default:
        stop("expression kind");
        return id;
    }
    if (r_.unsupported) return id;
    const Id<Expr> out = changed ? d_.exprs.add(std::move(e)) : id;
    if (d_.exprs[out].kind != ExprKind::Call) return out;
    const UserFunction* f = inlined(d_.exprs[out].call_name);
    return f != nullptr ? apply(out, *f) : out;
  }

  // applyFuncExp 的 guard（:296）：调用的路径在替换表里。
  const UserFunction* inlined(const std::vector<Symbol>& name) const {
    std::string key;
    for (Symbol s : name) key += std::string(in_.str(s)) + '.';
    const auto it = fns_.find(key);
    return it == fns_.end() ? nullptr : it->second;
  }

  // applyFuncExp :296-337。
  Id<Expr> apply(Id<Expr> call, const UserFunction& f) {
    if (!f.not_inlinable.empty()) {
      stop("inline: " + f.not_inlinable);
      return call;
    }
    if (d_.inline_annotation_seen) {
      stop("inline: an inline annotation in the loaded classes");
      return call;
    }
    const std::vector<Id<Expr>> args = d_.exprs[call].args;
    // 输入和实参 List.zip（:304-306）：定型后的调用补齐了缺省实参，个数相等。
    if (args.size() != f.inputs.size()) {
      stop("inline: argument count");
      return call;
    }
    std::vector<std::pair<Symbol, Id<Expr>>> repl;   // UnorderedMap<ComponentRef, Expression>：名字各不相同，表就够
    for (std::size_t i = 0; i < args.size(); ++i) repl.emplace_back(f.inputs[i], args[i]);
    // 局部按 fn.locals 的序加（:309-320）：有绑定的先代进已有的替换（输入和前面的局部），没有绑定的是 WILD（invalid，引用到就 SKIP）。
    for (const FunctionLocal& l : f.locals) repl.emplace_back(l.name, l.binding.is_valid() ? subst(l.binding, repl) : Id<Expr>{});
    Id<Expr> body = subst(f.body, repl);
    if (r_.unsupported) return call;
    // :330-331：SimplifyExp.combineBinaries（顶层收一次，removeTrivialScalarProduct 只管数组）再 simplify。
    body = combine_binaries(d_.exprs, body);
    std::string why;
    body = simplify(d_.exprs, in_, body, why);
    if (!why.empty()) {
      stop("inline: " + why);
      return call;
    }
    return body;
  }

  // Expression.map(exp, applySimpleExp)（NBReplacements.mo:187-218）：只换 cref，换上的式子不再往里走。
  Id<Expr> subst(Id<Expr> id, const std::vector<std::pair<Symbol, Id<Expr>>>& repl) {
    if (r_.unsupported) return id;
    Expr e = d_.exprs[id];
    switch (e.kind) {
      case ExprKind::IntLit:
      case ExprKind::RealLit:
      case ExprKind::BoolLit:
      case ExprKind::StringLit:
      case ExprKind::EnumLit:
        return id;
      case ExprKind::Cref: {
        if (e.cref.parts.size() == 1 && e.cref.parts[0].subscripts.empty()) {
          for (const auto& [name, value] : repl) {
            if (name != e.cref.parts[0].name) continue;
            if (!value.is_valid()) stop("inline: local without binding");
            return value;
          }
        }
        stop("inline: function body refers to an output or a global");
        return id;
      }
      default:
        break;
    }
    bool changed = false;
    const auto child = [&](Id<Expr>& x) {
      if (!x.is_valid()) return;
      const Id<Expr> y = subst(x, repl);
      changed = changed || y != x;
      x = y;
    };
    switch (e.kind) {
      case ExprKind::Binary:
      case ExprKind::Relation:
      case ExprKind::Logical:
      case ExprKind::Unary:
      case ExprKind::LogicalNot:
      case ExprKind::Cast:
      case ExprKind::IfExp:
        child(e.a);
        child(e.b);
        child(e.c);
        break;
      case ExprKind::Call: {
        const bool was_literal = std::all_of(e.args.begin(), e.args.end(), [&](Id<Expr> x) { return literal(x); });
        for (Id<Expr>& x : e.args) child(x);
        // simplifyCall（NFSimplifyExp.mo:176-204）：纯函数的实参全是字面量就 Ceval。函数体在默认路径化简过，那时实参全是字面量的调用是求值
        // 失败留下的；代进来才变成字面量的要真求值（实跑 `sin(z)`，z := 2.0），没移植 Ceval。实参化简后才变字面量的由 simplify 自己 SKIP。
        // impure 的用户函数不求值（:185、:200 的 is_pure），留着调用（实跑 ImpureCall1：`f2(1.0)` 内联成 `f(1.0)`）；内建的纯度没逐个核，一律 SKIP。
        if (changed && !was_literal && std::all_of(e.args.begin(), e.args.end(), [&](Id<Expr> x) { return literal(x); })) {
          std::string key;
          for (Symbol s : e.call_name) key += std::string(in_.str(s)) + '.';
          if (!impure_.count(key)) stop("inline: call whose arguments became literals");
        }
        break;
      }
      default:   // 数组、元组、记录……：lower 的 inline_shape 只放标量形参，体里出现别的就不猜
        stop("inline: function body expression kind");
        return id;
    }
    if (r_.unsupported) return id;
    return changed ? d_.exprs.add(std::move(e)) : id;
  }

  bool literal(Id<Expr> id) const {
    switch (d_.exprs[id].kind) {
      case ExprKind::IntLit:
      case ExprKind::RealLit:
      case ExprKind::BoolLit:
      case ExprKind::StringLit:
      case ExprKind::EnumLit:
        return true;
      default:
        return false;
    }
  }

  // ---- inlineRecordsTuplesArrays（:264-289，init = false）
  //
  // IfEquationBody.isRecordOrTupleEquation（NBEquation.mo:2941-2955）的第二支 `_ :: _` 接住任何非空的体，所以 inlineRecordTupleArrayEquation 的
  // IF_EQUATION 支（:351-354）对每条 if 方程都成立：拆分（split，一支一条方程时不变）后用 makeIfEquation 造新方程、起新名字，原方程变 DUMMY。
  // - EqData.map 先走 simulation，再 continuous、discretes、initials；同一指针第二次碰到时已是 DUMMY，不再重造。所以 simulation 里的 if 按
  //   simulation 的序先重造，初值里的随后。
  // - 起名（createName :1769-1795）：先给新 if 一个号，再给体里的方程逐支编号（IfEquationBody.createNames :2771-2782）。体里嵌着的 if 在
  //   inlineRecordTupleArrayIfBody（:391-408）里先被重造一次、先占掉一串号，外层起名时再连同它的体重新编一遍（实跑 IfEquation3）。
  // - 新方程头插攒成表，addUntypedList 的 typeList 再头插一次（:4661-4706），回到重造的序，接在各自的桶尾；compress 删掉 DUMMY，其余不动序。

  void remake_ifs() {
    std::vector<Id<Eqn>> sim, init;
    std::unordered_set<std::uint32_t> seen;
    const EqnData& ed = d_.eqns;
    for (const EqnSet* set : {&ed.simulation, &ed.continuous, &ed.discretes, &ed.initials}) {
      for (Id<Eqn> id : set->items) {
        if (d_.eqn_arena[id].kind != EqnKind::If || !seen.insert(id.raw).second) continue;
        remake(id);
        (set == &ed.initials ? init : sim).push_back(id);
      }
    }
    EqnData& out = d_.eqns;
    const auto rebuild = [&](EqnSet& set, const std::vector<Id<Eqn>>& made, bool all, EqnTime time) {
      std::vector<Id<Eqn>> keep;
      for (Id<Eqn> id : set.items)
        if (d_.eqn_arena[id].kind != EqnKind::If) keep.push_back(id);
      for (Id<Eqn> id : made)
        if (all || d_.eqn_arena[id].time == time) keep.push_back(id);
      set.items = std::move(keep);
    };
    rebuild(out.simulation, sim, true, EqnTime::Continuous);
    rebuild(out.continuous, sim, false, EqnTime::Continuous);
    rebuild(out.discretes, sim, false, EqnTime::Discrete);
    rebuild(out.initials, init, true, EqnTime::Continuous);
    // equations 不经 typeList，直接 addList(eq_lst)（NBEquation.mo:4571）：new_eqns 是头插攒的，所以重造的 if 按重造的反序接在尾巴上。
    std::vector<Id<Eqn>> made = sim;
    made.insert(made.end(), init.begin(), init.end());
    rebuild(out.equations, {}, true, EqnTime::Continuous);
    out.equations.items.insert(out.equations.items.end(), made.rbegin(), made.rend());
  }

  // inlineRecordTupleArrayIfEquation :366-389：体先过一遍（嵌着的 if 重造），再 makeIfEquation 起名。体里的方程是同一批指针，留在原处。
  void remake(Id<Eqn> id) {
    const std::vector<IfBranch> branches = d_.eqn_arena[id].if_branches;
    for (const IfBranch& br : branches)
      if (d_.eqn_arena[br.eqn].kind == EqnKind::If) remake(br.eqn);
    name(id);
  }
  void name(Id<Eqn> id) {
    d_.eqn_arena[id].name.parts = {ExprCrefPart{in_.intern("$RES_SIM_" + std::to_string(d_.eqns.unique_index++)), {}}};
    const std::vector<IfBranch> branches = d_.eqn_arena[id].if_branches;
    for (const IfBranch& br : branches) name(br.eqn);
  }

  BackendDAE& d_;
  Interner& in_;
  std::unordered_map<std::string, const UserFunction*> fns_;   // 压平名（每段后跟 '.'）→ 可内联的函数
  std::unordered_set<std::string> impure_;                      // impure 的用户函数（同样的键）
  ModuleResult r_;
};

}  // namespace

ModuleResult early_inline(BackendDAE& dae, Interner& in) { return Inliner(dae, in).run(); }

}  // namespace ymc::bdae
