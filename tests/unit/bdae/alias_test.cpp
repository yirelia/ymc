// Alias 模块端到端：源码 → 压平 → lower → bindings → function alias → early inline → simplify → alias → dump，逐字对照
// omc 1.25.0 的 `BackendDAE: (Alias)` 段。期望值都是 omc 实跑的输出（由 scratchpad 的 mkut.py 现跑生成），命令统一是
// `omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo <模型>.mo`，不是 ymc 的输出。
#include "ymc_test.h"

#include <string>
#include <vector>

#include "absyn/absyn.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "bdae/alias.h"
#include "bdae/bdae_dump.h"
#include "bdae/bindings.h"
#include "bdae/function_alias.h"
#include "bdae/inline.h"
#include "bdae/lower.h"
#include "bdae/simplify_module.h"
#include "flat/flatten.h"
#include "inst/inst.h"
#include "parse/lexer.h"
#include "parse/parser.h"
#include "scode/absyn_to_scode.h"
#include "scode/scode.h"

using namespace ymc;

namespace {

// 全流水线（同 driver，不扫内联注解：这里的模型都不带）。没做的构造返回 `unsupported: <原因>`。
std::string alias_src(const char* src) {
  Interner in;
  Diagnostics d;
  Absyn a(in);
  Lexer lx(src, 0, in, d);
  std::vector<Token> toks = lx.tokenize();
  Parser p(toks, a, d);
  p.parse_program();
  SCode sc(a);
  translate_program(sc, a.top);
  Inst inst(in, a, sc);
  Instantiator instr(inst, d);
  instr.make_top_node();
  const NodeId root = instr.instantiate_root_path({a.classes[a.top.back()].name});
  if (instr.unsupported()) return "unsupported: " + instr.unsupported_reason();
  FlattenResult fr = flatten(inst, in, root, &instr);
  if (fr.unsupported) return "unsupported: " + fr.unsupported_reason;
  bdae::LowerResult lr = bdae::lower(std::move(fr.model), in);
  if (lr.unsupported) return "unsupported: " + lr.unsupported_reason;
  for (bdae::ModuleResult (*m)(bdae::BackendDAE&, Interner&) :
       {&bdae::bindings, &bdae::function_alias, &bdae::early_inline, &bdae::simplify_module, &bdae::alias}) {
    const bdae::ModuleResult r = m(lr.dae, in);
    if (r.unsupported) return "unsupported: " + r.unsupported_reason;
  }
  return bdae::dump_stage(lr.dae, in, "Alias");
}

}  // namespace

// 两个集合：带常量绑定的 {a, b, c, y} 全换成常量（a、b、c 改参数，start = 绑定、fixed = true；y = 3.0 + time 含 time，是非平凡别名，
// 绑定方程进 removed）；{x, z, $FUN_1, e, d} 留 x（$FUN 评分 -10000），e 由 solveLinear 解成 -x 进 Alias 桶，d = p - 2.0 * x 进 removed。
// 集合按首次出现的反序处理（后建的 {x, …} 先），桶内按 Tarjan 的完成序。
YMC_TEST(sets_constant_trivial_nontrivial) {
  const char* src = R"omc(model AlSets
  parameter Real p = 2;
  Real y, x, z, a, b, c, d, e;
equation
  a = b;
  b = c;
  x = z;
  c = 3;
  y = time + c;
  z = sin(time);
  d = 2 * e + p;
  e = -x;
end AlSets;
)omc";
  const char* omc = R"omc(###########################

    BackendDAE: (Alias)

###########################

=================================================
  Variable Data Simulation (scalar unknowns: 1)
=================================================

Algebraic Variables (1/1)
***************************
(1)       [ALGB] (1) Real x

Parameter Variables (4/4)
***************************
(1)       [PRMT] (1) parameter Real p = 2.0 (fixed = true)
(2)       [PRMT] (1) Real c = 3.0 (fixed = true, start = 3.0)
(3)       [PRMT] (1) Real b = 3.0 (fixed = true, start = 3.0)
(4)       [PRMT] (1) Real a = 3.0 (fixed = true, start = 3.0)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time

Alias Variables (3/3)
***********************
(1)       [ALGB] (1) Real e = -x
(2)       [ALGB] (1) Real z = x
(3)       [ALGB] (1) Real $FUN_1 = x


=============================================================
  Equation Data Simulation (scalar simulation equations: 1)
=============================================================

Continuous Equations (1/1)
****************************
(1)       [SCAL] (1) x = sin(time) ($RES_AUX_8)

Removed Equations (2/2)
*************************
(1)       [SCAL] (1) d = p - 2.0 * x ($RES_BND_9)
(2)       [SCAL] (1) y = 3.0 + time ($RES_BND_10)

)omc";
  YMC_CHECK_EQ(alias_src(src), std::string(omc));
}

// j 出现在 pre(j) 里：它的替换不做，补一条 `j = k`（$RES_SIM_7）接在 Discrete 桶尾；i 照换，连 when 的赋值左边一起换成 k。
// removed 里的 assert 不走替换（EqData.mapExp 不碰 removed），v 是留下的变量，看不出差别。
YMC_TEST(pre_exception_and_removed) {
  const char* src = R"omc(model AlPre
  Integer i, j, k;
  Real u, v;
equation
  j = i;
  k = j;
  when time > 1 then
    i = pre(k) + pre(j);
  end when;
  v = u;
  u = cos(time);
  assert(v > -2, "v too small");
end AlPre;
)omc";
  const char* omc = R"omc(###########################

    BackendDAE: (Alias)

###########################

=================================================
  Variable Data Simulation (scalar unknowns: 3)
=================================================

Algebraic Variables (1/1)
***************************
(1)       [ALGB] (1) Real v

Discrete Variables (2/2)
**************************
(1)       [DISC] (1) Integer j
(2)       [DISC] (1) Integer k

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time

Alias Variables (3/3)
***********************
(1)       [ALGB] (1) Real u = v
(2)       [ALGB] (1) Real $FUN_1 = v
(3)       [DISC] (1) Integer i = k


=============================================================
  Equation Data Simulation (scalar simulation equations: 3)
=============================================================

Continuous Equations (1/1)
****************************
(1)       [SCAL] (1) v = cos(time) ($RES_AUX_6)

Discrete Equations (2/2)
**************************
(1)       [WHEN] (1) ($RES_SIM_3)
(1)       [----] when time > 1.0 then
(1)       [----]   k := pre(k) + pre(j)
(1)       [----] end when;
(2)       [SCAL] (1) j = k ($RES_SIM_7)

Removed Equations (1/0)
*************************
(1)       [WHEN] (0) ($RES_SIM_0)
(1)       [----] when v > (-2.0) then
(1)       [----]   assert(v > (-2.0), "v too small", AssertionLevel.error)
(1)       [----] end when;

)omc";
  YMC_CHECK_EQ(alias_src(src), std::string(omc));
}

// 属性合并：a = 2 * b 留 a，b 的 min / max / start 按 a = 2.0 * b 折算（0.0、4.0、2.0），min 取大、max 取小；b 的 fixed 让给 a，
// a 取它折算后的 start。w 的 start 不 fixed、留下的 q 不取它；q 的 nominal 不合并。
YMC_TEST(attributes_merged_into_kept_variable) {
  const char* src = R"omc(model AlAttr
  Real a(min = -1, max = 3), b(start = 1, fixed = true, min = 0, max = 2);
  Real c, w(start = 2), q(nominal = 5);
equation
  a = 2 * b;
  der(a) = -a;
  c = -w;
  w = q;
  q = cos(time);
end AlAttr;
)omc";
  const char* omc = R"omc(###########################

    BackendDAE: (Alias)

###########################

=================================================
  Variable Data Simulation (scalar unknowns: 2)
=================================================

Algebraic Variables (2/2)
***************************
(1)       [ALGB] (1) Real a (fixed = true, start = 2.0, min = 0.0, max = 3.0)
(2)       [ALGB] (1) Real q (nominal = 5.0)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time

Alias Variables (3/3)
***********************
(1)       [ALGB] (1) Real w = q (start = 2.0)
(2)       [ALGB] (1) Real c = -q
(3)       [ALGB] (1) Real $FUN_1 = q


=============================================================
  Equation Data Simulation (scalar simulation equations: 2)
=============================================================

Continuous Equations (2/2)
****************************
(1)       [SCAL] (1) der(a) = -a ($RES_SIM_3)
(2)       [SCAL] (1) q = cos(time) ($RES_AUX_5)

Removed Equations (1/1)
*************************
(1)       [SCAL] (1) b = 0.5 * a ($RES_BND_6)

)omc";
  YMC_CHECK_EQ(alias_src(src), std::string(omc));
}

// 顶层 input u 被换成 y：仍在 Top Level Input 桶（removeList 不碰它）、也进 Alias 桶；m 的绑定 sin(r) 里的 r 换成 y（applySimpleVar）。
YMC_TEST(top_level_input_and_binding) {
  const char* src = R"omc(model AlStar
  input Real u;
  Real y, s, t, r;
  Real m = sin(r);
equation
  y = u;
  s = t;
  s = r;
  t = cos(time);
  r = y;
end AlStar;
)omc";
  const char* omc = R"omc(###########################

    BackendDAE: (Alias)

###########################

=================================================
  Variable Data Simulation (scalar unknowns: 2)
=================================================

Algebraic Variables (2/2)
***************************
(1)       [ALGB] (1) Real y
(2)       [ALGB] (1) Real m = sin(y)

Top Level Input Variables (1/1)
*********************************
(1)       [ALGB] (1) input Real u = y

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time

Alias Variables (6/6)
***********************
(1)       [ALGB] (1) Real $FUN_2 = m
(2)       [ALGB] (1) Real r = y
(3)       [ALGB] (1) Real s = y
(4)       [ALGB] (1) Real t = y
(5)       [ALGB] (1) Real $FUN_1 = y
(6)       [ALGB] (1) input Real u = y


=============================================================
  Equation Data Simulation (scalar simulation equations: 2)
=============================================================

Continuous Equations (2/2)
****************************
(1)       [SCAL] (1) y = cos(time) ($RES_AUX_7)
(2)       [SCAL] (1) m = sin(y) ($RES_AUX_6)

)omc";
  YMC_CHECK_EQ(alias_src(src), std::string(omc));
}

// 自指方程 a = 17 + 2 * a 让 a 在集合里出现两次，留下的正是 a：去重后（VariablePointers.fromList）a 和 m.b 两个变量两条方程，a 解成 -17.0，
// 两个都改参数。fixValues 对 m.b 解 `m.b = -17.0` 解不出 a（导数 0，solveBody 只回 UNSOLVABLE），拿原右边：min 1 折成 -17.0，
// a 自己的 max 9 也折成 -17.0。
YMC_TEST(self_reference_kept_variable) {
  const char* src = R"omc(model AlSelf
  model M
    Real b(min = 1, start = 2);
  end M;
  M m;
  Real a(max = 9);
  Real c, d;
equation
  a = 17 + 2 * a;
  m.b = a;
  c = -d;
  d = sin(time);
end AlSelf;
)omc";
  const char* omc = R"omc(###########################

    BackendDAE: (Alias)

###########################

=================================================
  Variable Data Simulation (scalar unknowns: 1)
=================================================

Algebraic Variables (1/1)
***************************
(1)       [ALGB] (1) Real c

Parameter Variables (2/2)
***************************
(1)       [PRMT] (1) Real a = -17.0 (fixed = true, start = -17.0, min = -17.0, max = -17.0)
(2)       [PRMT] (1) Real m.b = -17.0 (fixed = true, start = -17.0, min = 1.0)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time

Alias Variables (2/2)
***********************
(1)       [ALGB] (1) Real d = -c
(2)       [ALGB] (1) Real $FUN_1 = -c


=============================================================
  Equation Data Simulation (scalar simulation equations: 1)
=============================================================

Continuous Equations (1/1)
****************************
(1)       [SCAL] (1) -c = sin(time) ($RES_AUX_4)

)omc";
  YMC_CHECK_EQ(alias_src(src), std::string(omc));
}

// 简单方程成环（a = b、b = a 连同一对变量）：omc 报内部错、没有这一段（实跑 LalB_loop），ymc 退 SKIP。
YMC_TEST(loop_is_unsupported) {
  const char* src = R"omc(model AlLoop
  Real a, b;
equation
  a = b;
  b = a;
end AlLoop;
)omc";
  YMC_CHECK(alias_src(src).rfind("unsupported: ", 0) == 0);
}

// String 拼接的集合：fixValues 前的 solveBody 不论有没有属性都做，对 String 残差求导时 makeOne(String) 报内部错，omc 没有这一段
//（实跑评审探针 LalX_str1：Internal error NFExpression.makeOne failed for: String），ymc 退 SKIP，不能打出一段。
YMC_TEST(string_concat_set_is_unsupported) {
  const char* src = R"omc(model AlStr
  String s1, s2;
equation
  s2 + "x" = s1;
  s2 = String(time);
end AlStr;
)omc";
  YMC_CHECK(alias_src(src).rfind("unsupported: ", 0) == 0);
}

// 自指方程之后又被别的简单方程接上、留下的不是重复的那个变量：去重后方程比变量多，选哪条方程由 PFPlusExternal 定（没移植）。omc 有这一段
//（实跑评审探针 LalX_selfattr2：a = b），ymc 退 SKIP；以前这里因果化失败后没停，fixValues 取不到替换式而崩溃。
YMC_TEST(self_reference_with_extra_equation_is_unsupported) {
  const char* src = R"omc(model AlSelfExtra
  Real a(min = 0, nominal = 4), b(start = 2);
equation
  a = 17 + 2 * a;
  b = a;
end AlSelfExtra;
)omc";
  YMC_CHECK(alias_src(src).rfind("unsupported: ", 0) == 0);
}

YMC_TEST_MAIN
