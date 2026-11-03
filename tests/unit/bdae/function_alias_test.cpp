// FunctionAlias 模块端到端：源码 → 压平 → lower → bindings → function alias → dump，逐字对照 omc 1.25.0 的 `BackendDAE: (FunctionAlias)` 段。
// 期望值都是 omc 实跑的输出，命令统一是 `omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo <模型>.mo`，不是 ymc 的输出。
#include "ymc_test.h"

#include <string>
#include <vector>

#include "absyn/absyn.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "bdae/bdae_dump.h"
#include "bdae/bindings.h"
#include "bdae/function_alias.h"
#include "bdae/lower.h"
#include "flat/flatten.h"
#include "inst/inst.h"
#include "parse/lexer.h"
#include "parse/parser.h"
#include "scode/absyn_to_scode.h"
#include "scode/scode.h"

using namespace ymc;

namespace {

// 全流水线（同 driver）。没做的构造返回 `unsupported: <原因>`。
std::string function_alias_src(const char* src) {
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
  bdae::ModuleResult r = bdae::bindings(lr.dae, in);
  if (r.unsupported) return "unsupported: " + r.unsupported_reason;
  r = bdae::function_alias(lr.dae, in);
  if (r.unsupported) return "unsupported: " + r.unsupported_reason;
  return bdae::dump_stage(lr.dae, in, "FunctionAlias");
}

}  // namespace

// 编号：$FUN 按遍历序（simulation 视图的序，方程内先 lhs 后 rhs、后序），$RES_AUX 按 Map 插入序倒着分（最后提的号最小）；
// 同一调用只提一次（跨方程、绑定方程里）；嵌套先提里层；Integer 结果（sign、Integer 实参的 abs）进 [DISC] 和 Discrete 桶；变量行的绑定不动。
YMC_TEST(order_and_buckets) {
  const char* src = R"omc(model FaOrder
  Real x(start = 1, fixed = true);
  Real y, w;
  Integer n;
  Real z = cos(time) * sin(x);
equation
  der(x) = -sin(x);
  n = sign(time - 0.5);
  y = sin(x) + cos(time) + atan2(time, 2) + atan2(2, time);
  w = sin(cos(time)) * abs(n);
end FaOrder;
)omc";
  const char* omc = R"omc(###################################

    BackendDAE: (FunctionAlias)

###################################

==================================================
  Variable Data Simulation (scalar unknowns: 12)
==================================================

Algebraic Variables (9/9)
***************************
(1)       [ALGB] (1) Real x (fixed = true, start = 1.0)
(2)       [ALGB] (1) Real y
(3)       [ALGB] (1) Real w
(4)       [ALGB] (1) Real z = cos(time) * sin(x)
(5)       [ALGB] (1) Real $FUN_1
(6)       [ALGB] (1) Real $FUN_3
(7)       [ALGB] (1) Real $FUN_4
(8)       [ALGB] (1) Real $FUN_5
(9)       [ALGB] (1) Real $FUN_6

Discrete Variables (3/3)
**************************
(1)       [DISC] (1) Integer n
(2)       [DISC] (1) Integer $FUN_2
(3)       [DISC] (1) Integer $FUN_7

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


==============================================================
  Equation Data Simulation (scalar simulation equations: 12)
==============================================================

Continuous Equations (9/9)
****************************
(1)       [SCAL] (1) der(x) = -$FUN_1 ($RES_SIM_3)
(2)       [SCAL] (1) y = $FUN_3 + $FUN_4 + $FUN_1 + $FUN_5 ($RES_SIM_1)
(3)       [SCAL] (1) w = $FUN_6 * $FUN_7 ($RES_SIM_0)
(4)       [SCAL] (1) z = $FUN_4 * $FUN_1 ($RES_BND_4)
(5)       [SCAL] (1) $FUN_1 = sin(x) ($RES_AUX_11)
(6)       [SCAL] (1) $FUN_3 = atan2(time, 2.0) ($RES_AUX_9)
(7)       [SCAL] (1) $FUN_4 = cos(time) ($RES_AUX_8)
(8)       [SCAL] (1) $FUN_5 = atan2(2.0, time) ($RES_AUX_7)
(9)       [SCAL] (1) $FUN_6 = sin($FUN_4) ($RES_AUX_6)

Discrete Equations (3/3)
**************************
(1)       [SCAL] (1) n = $FUN_2 ($RES_SIM_2)
(2)       [SCAL] (1) $FUN_2 = sign(time - 0.5) ($RES_AUX_10)
(3)       [SCAL] (1) $FUN_7 = abs(n) ($RES_AUX_5)

)omc";
  YMC_CHECK_EQ(function_alias_src(src), std::string(omc));
}

// 初值方程：只在初值里的调用是 [PRMT]、(fixed = false)，编号在 simulation 的之后；simulation 里提过又在初值里遇到的（sin(time)）
// 再造一条初值方程，同一个变量加 fixed = false、挂进 Parameter 桶（标签还是 [ALGB]，两个桶里都带 fixed）。
YMC_TEST(initial_equations) {
  const char* src = R"omc(model FaInit
  Real x(start = 1, fixed = true);
  Integer n;
  Real y;
  parameter Real p = 2;
initial equation
  n = sign(p) + integer(p);
  y = sign(p - 1) * 2.0 + sin(time);
equation
  der(x) = -x;
  n = sign(time - 0.5);
  y = sin(time);
end FaInit;
)omc";
  const char* omc = R"omc(###################################

    BackendDAE: (FunctionAlias)

###################################

=================================================
  Variable Data Simulation (scalar unknowns: 5)
=================================================

Algebraic Variables (3/3)
***************************
(1)       [ALGB] (1) Real x (fixed = true, start = 1.0)
(2)       [ALGB] (1) Real y
(3)       [ALGB] (1) Real $FUN_2 (fixed = false)

Discrete Variables (2/2)
**************************
(1)       [DISC] (1) Integer n
(2)       [DISC] (1) Integer $FUN_1

Parameter Variables (4/4)
***************************
(1)       [PRMT] (1) parameter Real p = 2.0 (fixed = true)
(2)       [ALGB] (1) Real $FUN_2 (fixed = false)
(3)       [PRMT] (1) Integer $FUN_3 (fixed = false)
(4)       [PRMT] (1) Integer $FUN_4 (fixed = false)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 5)
=============================================================

Continuous Equations (3/3)
****************************
(1)       [SCAL] (1) der(x) = -x ($RES_SIM_4)
(2)       [SCAL] (1) y = $FUN_2 ($RES_SIM_2)
(3)       [SCAL] (1) $FUN_2 = sin(time) ($RES_AUX_5)

Discrete Equations (2/2)
**************************
(1)       [SCAL] (1) n = $FUN_1 ($RES_SIM_3)
(2)       [SCAL] (1) $FUN_1 = sign(time - 0.5) ($RES_AUX_6)

(Exclusively) Initial Equations (5/5)
***************************************
(1)       [SCAL] (1) n = $FUN_3 + integer(p) ($RES_SIM_1)
(2)       [SCAL] (1) y = $FUN_4 * 2.0 + $FUN_2 ($RES_SIM_0)
(3)       [SCAL] (1) $FUN_2 = sin(time) ($RES_AUX_9)
(4)       [SCAL] (1) $FUN_3 = sign(p) ($RES_AUX_8)
(5)       [SCAL] (1) $FUN_4 = sign(p - 1.0) ($RES_AUX_7)

)omc";
  YMC_CHECK_EQ(function_alias_src(src), std::string(omc));
}

// 用户函数：体恰一条赋值（带 protected 局部也算）不换；输出绑定无算法、单个 if、两条语句的换；impure、体里顶层调 impure 的、
// 未声明纯度的 external "C" 不换；pure external 换；external "C" v = cos(u) 打成 cos，和内建 cos 是同一个调用；调用套调用、内建套用户函数。
YMC_TEST(user_functions) {
  const char* src = R"omc(model FaUser
  function f1
    input Real u;
    output Real v;
  protected
    Real w;
  algorithm
    v := u * 3;
  end f1;
  function f2
    input Real u;
    output Real v;
  algorithm
    v := u * 2;
    v := v + 1;
  end f2;
  function f0
    input Real u;
    output Real v = u * 2;
  end f0;
  function fif
    input Real u;
    output Real v;
  algorithm
    if u > 0 then
      v := u;
    else
      v := -u;
    end if;
  end fif;
  impure function fi
    input Real u;
    output Real v;
  algorithm
    v := u * 2;
    v := v + 1;
  end fi;
  function g
    input Real u;
    output Real v;
  algorithm
    v := fi(u);
    v := v + 1;
  end g;
  function fx
    input Real u;
    output Real v;
    external "C";
  end fx;
  pure function fpx
    input Real u;
    output Real v;
    external "C" v = fx(u);
  end fpx;
  function fc
    input Real u;
    output Real v;
    external "C" v = cos(u);
  end fc;
  function fn
    input Real u;
    output Boolean v;
  algorithm
    v := u > 0.5;
    v := not v;
  end fn;
  Real y1, y2, y3, y4, y5, y6, y7, y8;
  Boolean b;
equation
  y1 = f1(time) + f2(time);
  y2 = f0(time) + fif(time);
  y3 = fi(time) + g(time);
  y4 = fx(time) + fpx(time);
  y5 = fc(time) + cos(time);
  y6 = f2(f2(time));
  y7 = f2(sin(time));
  y8 = if fn(time) then 1 else 2;
  b = fn(time);
end FaUser;
)omc";
  const char* omc = R"omc(###################################

    BackendDAE: (FunctionAlias)

###################################

==================================================
  Variable Data Simulation (scalar unknowns: 18)
==================================================

Algebraic Variables (16/16)
*****************************
(1)       [ALGB] (1) Real y1
(2)       [ALGB] (1) Real y2
(3)       [ALGB] (1) Real y3
(4)       [ALGB] (1) Real y4
(5)       [ALGB] (1) Real y5
(6)       [ALGB] (1) Real y6
(7)       [ALGB] (1) Real y7
(8)       [ALGB] (1) Real y8
(9)       [ALGB] (1) Real $FUN_1
(10)      [ALGB] (1) Real $FUN_2
(11)      [ALGB] (1) Real $FUN_3
(12)      [ALGB] (1) Real $FUN_4
(13)      [ALGB] (1) Real $FUN_5
(14)      [ALGB] (1) Real $FUN_6
(15)      [ALGB] (1) Real $FUN_7
(16)      [ALGB] (1) Real $FUN_8

Discrete Variables (2/2)
**************************
(1)       [DISC] (1) Boolean b
(2)       [DISC] (1) Boolean $FUN_9

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


==============================================================
  Equation Data Simulation (scalar simulation equations: 18)
==============================================================

Continuous Equations (16/16)
******************************
(1)       [SCAL] (1) y1 = FaUser.f1(time) + $FUN_1 ($RES_SIM_8)
(2)       [SCAL] (1) y2 = $FUN_2 + $FUN_3 ($RES_SIM_7)
(3)       [SCAL] (1) y3 = FaUser.fi(time) + FaUser.g(time) ($RES_SIM_6)
(4)       [SCAL] (1) y4 = FaUser.fx(time) + $FUN_4 ($RES_SIM_5)
(5)       [SCAL] (1) y5 = $FUN_5 + $FUN_5 ($RES_SIM_4)
(6)       [SCAL] (1) y6 = $FUN_6 ($RES_SIM_3)
(7)       [SCAL] (1) y7 = $FUN_8 ($RES_SIM_2)
(8)       [SCAL] (1) y8 = if $FUN_9 then 1.0 else 2.0 ($RES_SIM_1)
(9)       [SCAL] (1) $FUN_1 = FaUser.f2(time) ($RES_AUX_17)
(10)      [SCAL] (1) $FUN_2 = FaUser.f0(time) ($RES_AUX_16)
(11)      [SCAL] (1) $FUN_3 = FaUser.fif(time) ($RES_AUX_15)
(12)      [SCAL] (1) $FUN_4 = FaUser.fpx(time) ($RES_AUX_14)
(13)      [SCAL] (1) $FUN_5 = cos(time) ($RES_AUX_13)
(14)      [SCAL] (1) $FUN_6 = FaUser.f2($FUN_1) ($RES_AUX_12)
(15)      [SCAL] (1) $FUN_7 = sin(time) ($RES_AUX_11)
(16)      [SCAL] (1) $FUN_8 = FaUser.f2($FUN_7) ($RES_AUX_10)

Discrete Equations (2/2)
**************************
(1)       [SCAL] (1) b = $FUN_9 ($RES_SIM_0)
(2)       [SCAL] (1) $FUN_9 = FaUser.fn(time) ($RES_AUX_9)

)omc";
  YMC_CHECK_EQ(function_alias_src(src), std::string(omc));
}

// 不换的内建：delay、homotopy（impure）、noEvent、smooth、max、min、edge、pre（special）、integer、String（点名的例外）、Integer(e)（无输出），
// 但它们的实参照换；if 表达式里照换；when、if 方程、算法段、assert（Removed）整条不动。
YMC_TEST(kept_calls_and_skipped_equations) {
  const char* src = R"omc(model FaKeep
  type E = enumeration(a, b);
  Real x(start = 1, fixed = true);
  Real y1, y2, y3, y4;
  Integer n;
  String s;
  E e;
  Boolean b;
  Boolean c;
  discrete Real d(start = 0, fixed = true);
equation
  der(x) = -x;
  y1 = delay(sin(time), 0.1) + homotopy(cos(time), time);
  y2 = noEvent(tan(time)) + smooth(1, exp(time)) + max(time, 0.5) + min(time, 0.5);
  y3 = if time > 0.5 then sqrt(time) else 0;
  n = integer(time * 3) + Integer(e);
  s = String(time);
  e = if time > 0.5 then E.a else E.b;
  c = time > 0.5;
  b = edge(c) or pre(b);
  when time > 0.5 then
    d = sin(time);
  end when;
  if time > 0.7 then
    y4 = cos(time);
  else
    y4 = sin(time);
  end if;
  assert(tan(time) < 10, "t");
algorithm
  if time > 0.9 then
    terminate("end");
  end if;
end FaKeep;
)omc";
  const char* omc = R"omc(###################################

    BackendDAE: (FunctionAlias)

###################################

==================================================
  Variable Data Simulation (scalar unknowns: 16)
==================================================

Algebraic Variables (10/10)
*****************************
(1)       [ALGB] (1) Real x (fixed = true, start = 1.0)
(2)       [ALGB] (1) Real y1
(3)       [ALGB] (1) Real y2
(4)       [ALGB] (1) Real y3
(5)       [ALGB] (1) Real y4
(6)       [ALGB] (1) Real $FUN_1
(7)       [ALGB] (1) Real $FUN_2
(8)       [ALGB] (1) Real $FUN_3
(9)       [ALGB] (1) Real $FUN_4
(10)      [ALGB] (1) Real $FUN_5

Discrete Variables (6/6)
**************************
(1)       [DISC] (1) Integer n
(2)       [DISC] (1) String s
(3)       [DISC] (1) enumeration E(a, b) e
(4)       [DISC] (1) Boolean b
(5)       [DISC] (1) Boolean c
(6)       [DISC] (1) discrete Real d (fixed = true, start = 0.0)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


==============================================================
  Equation Data Simulation (scalar simulation equations: 16)
==============================================================

Continuous Equations (10/10)
******************************
(1)       [SCAL] (1) der(x) = -x ($RES_SIM_14)
(2)       [SCAL] (1) y1 = delay($FUN_1, 0.1, 0.1) + homotopy($FUN_2, time) ($RES_SIM_13)
(3)       [SCAL] (1) y2 = max(time, 0.5) + smooth(1, $FUN_3) + noEvent($FUN_4) + min(time, 0.5) ($RES_SIM_12)
(4)       [SCAL] (1) y3 = if time > 0.5 then $FUN_5 else 0.0 ($RES_SIM_11)
(5)       [-IF-] (1) ($RES_SIM_2)
(5)       [----] if time > 0.7 then
(5)       [----]   [SCAL] (1) y4 = cos(time) ($RES_SIM_3)
(5)       [----] else
(5)       [----]   [SCAL] (1) y4 = sin(time) ($RES_SIM_4)
(5)       [----] end if;
(6)       [SCAL] (1) $FUN_1 = sin(time) ($RES_AUX_19)
(7)       [SCAL] (1) $FUN_2 = cos(time) ($RES_AUX_18)
(8)       [SCAL] (1) $FUN_3 = exp(time) ($RES_AUX_17)
(9)       [SCAL] (1) $FUN_4 = tan(time) ($RES_AUX_16)
(10)      [SCAL] (1) $FUN_5 = sqrt(time) ($RES_AUX_15)

Discrete Equations (6/6)
**************************
(1)       [SCAL] (1) n = integer(time * 3.0) + Integer(e) ($RES_SIM_10)
(2)       [SCAL] (1) s = String(time, 6, 0, true) ($RES_SIM_9)
(3)       [SCAL] (1) e = if time > 0.5 then E.a else E.b ($RES_SIM_8)
(4)       [SCAL] (1) c = time > 0.5 ($RES_SIM_7)
(5)       [SCAL] (1) b = edge(c) or pre(b) ($RES_SIM_6)
(6)       [WHEN] (1) ($RES_SIM_5)
(6)       [----] when time > 0.5 then
(6)       [----]   d := sin(time)
(6)       [----] end when;

Removed Equations (2/0)
*************************
(1)       [WHEN] (0) ($RES_SIM_1)
(1)       [----] when tan(time) < 10.0 then
(1)       [----]   assert(tan(time) < 10.0, "t", AssertionLevel.error)
(1)       [----] end when;
(2)       [ALGO] (0) ($RES_SIM_0)
(2)       [----] if time > 0.9 then
(2)       [----]   terminate("end");
(2)       [----] end if;


)omc";
  YMC_CHECK_EQ(function_alias_src(src), std::string(omc));
}

// 打不出和 omc 一样的字节就不打：枚举结果的 $FUN 要打类型全名（omc：`enumeration E(a, b, c) $FUN_1`、`$FUN_1 = FaEnum.fe(e1) ($RES_AUX_2)`，Type::enum_path 只有简名）；
// 时钟常量 omc 提成 `$CLK_0` 进 Clock 桶和 Clocked 桶（`ud = sample(time * u, $CLK_0)`、`$CLK_0 = Clock(dt) ($RES_AUX_2)`），没做。
YMC_TEST(function_alias_stops_on_unsupported) {
  YMC_CHECK_EQ(function_alias_src(R"omc(model FaEnum
  type E = enumeration(a, b, c);
  function fe
    input E u;
    output E v;
  algorithm
    v := u;
    v := v;
  end fe;
  E e1, e2;
equation
  e1 = if time > 0.5 then E.a else E.b;
  e2 = fe(e1);
end FaEnum;
)omc"), std::string("unsupported: enumeration result"));
  YMC_CHECK_EQ(function_alias_src(R"omc(model FaClock
  parameter Real dt = 0.1;
  Real u = 1;
  output Real ud;
equation
  ud = sample(time * u, Clock(dt));
end FaClock;
)omc"), std::string("unsupported: clock constructor"));
}

YMC_TEST_MAIN
