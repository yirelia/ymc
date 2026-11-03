// NBBindings.main（Modules/2_Pre/NBBindings.mo:50-143）+ Equation.generateBindingEquation（Classes/NBEquation.mo:2186-2240），均 OMC v1.25.0。
//
// 只看 unknowns（:69）：顶层 input 和 stateSelect = always 的状态带绑定也不造方程，它们在 knowns 里（lowerVariableData NBackendDAE.mo:551-566）。
//
// ponytail: 记录分支（:73-77 跳过有绑定父记录的子量、:91-94 未知记录）、时钟分支（:96-100，进 Clocked 桶）都走不到：这些模型在 lower 就 SKIP 或 NOT-ADMITTED。
// 放开记录、Clock 时照原文补，顺序在 :103-117。
#include "bdae/bindings.h"

#include <string>
#include <utility>
#include <vector>

#include "bdae/simplify.h"

namespace ymc::bdae {

ModuleResult bindings(BackendDAE& dae, Interner& in) {
  ModuleResult r;
  std::vector<Id<Eqn>> cont, disc;
  for (Id<Var> id : dae.vars.unknowns.items) {
    const Var& v = dae.var_arena[id];
    if (!v.binding.is_valid()) continue;     // isBound NBVariable.mo:1511-1520
    Eqn e;
    // 方程的 rhs 不是变量上那个式子，是它再化简一遍（:2226）。比如绑定 `2.0 * time + p + 3.0` 的方程是 `3.0 + p + 2.0 * time`。
    // Iterator.extract（:2225）在标量式上是恒等：它只认数组构造，lower 挡了。
    e.rhs = simplify(dae.exprs, in, v.binding, r.unsupported_reason);
    if (!r.unsupported_reason.empty()) {
      r.unsupported = true;
      return r;
    }
    Expr lhs;
    lhs.kind = ExprKind::Cref;
    lhs.ty = v.ty;
    lhs.cref = v.name;
    e.lhs = dae.exprs.add(std::move(lhs));
    // 编号接着 $RES_SIM 往上走，连续、离散共用一个序列（createName NBEquation.mo:1769-1795，context = "BND"）。
    ExprCref name;
    name.parts.push_back(ExprCrefPart{in.intern("$RES_BND_" + std::to_string(dae.eqns.unique_index++)), {}});
    e.name = std::move(name);
    // BVariable.isContinuous NBVariable.mo:373-385（init = false）。
    const bool continuous = v.kind != VarKind::Discrete && v.kind != VarKind::Parameter && v.kind != VarKind::Constant;
    e.time = continuous ? EqnTime::Continuous : EqnTime::Discrete;
    (continuous ? cont : disc).push_back(dae.eqn_arena.add(std::move(e)));
  }
  // binding_cont、binding_disc 头插（:80-83），EquationPointers.addList 从表头 fold（:3964-3969），所以新方程按 unknowns 的反序接在桶尾：编号大的在前。
  // 打印按数组序（EquationPointers.toString :3879-3894），不走 typeList，原有方程的序不动。
  EqnData& ed = dae.eqns;
  for (auto it = cont.rbegin(); it != cont.rend(); ++it) {
    ed.continuous.items.push_back(*it);
    ed.simulation.items.push_back(*it);
  }
  for (auto it = disc.rbegin(); it != disc.rend(); ++it) {
    ed.discretes.items.push_back(*it);
    ed.simulation.items.push_back(*it);
  }
  return r;
}

}  // namespace ymc::bdae
