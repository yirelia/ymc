// Early Inline 模块端到端：源码 → 压平 → lower → bindings → function alias → early inline → dump，逐字对照 omc 1.25.0 的
// `BackendDAE: (Early Inline)` 段。期望值都是 omc 实跑的输出，命令统一是
// `omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo <模型>.mo`，不是 ymc 的输出。
#include "ymc_test.h"

#include <string>
#include <vector>

#include "absyn/absyn.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "bdae/bdae_dump.h"
#include "bdae/bindings.h"
#include "bdae/function_alias.h"
#include "bdae/inline.h"
#include "bdae/lower.h"
#include "flat/flatten.h"
#include "inst/inst.h"
#include "parse/lexer.h"
#include "parse/parser.h"
#include "scode/absyn_to_scode.h"
#include "scode/scode.h"

using namespace ymc;

namespace {

// 全流水线（同 driver，不扫内联注解：这里的模型都不带）。没做的构造返回 `unsupported: <原因>`。
std::string early_inline_src(const char* src) {
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
  r = bdae::early_inline(lr.dae, in);
  if (r.unsupported) return "unsupported: " + r.unsupported_reason;
  return bdae::dump_stage(lr.dae, in, "Early Inline");
}

}  // namespace

// simulation 的方程映射两遍（simulation 和 continuous / discretes 是同一批指针），初值方程一遍：f → g → h 在 simulation 里展开到 h，
// 初值里只到 g 的体（`1.0 + h(time)`）；der、when 体照换；局部 t = k * x 和缺省实参 k = 2 代进体；实参是式子、实参里再调可内联函数。
YMC_TEST(two_passes_locals_defaults) {
  const char* src = R"omc(function h
  input Real x;
  output Real y;
algorithm
  y := 3 * x;
end h;
function g
  input Real x;
  output Real y;
algorithm
  y := h(x) + 1;
end g;
function f
  input Real x;
  input Real k = 2;
  output Real y;
protected
  Real t = k * x;
algorithm
  y := t * g(x);
end f;
model EiPasses
  Real a(start = 1), b, c;
  discrete Real d(start = 0, fixed = true);
equation
  der(a) = -g(a);
  b = f(time);
  c = h(g(time)) + f(b, 3);
  when sample(0, 0.1) then
    d = g(pre(d));
  end when;
initial equation
  a = g(time);
end EiPasses;
)omc";
  const char* omc = R"omc(##################################

    BackendDAE: (Early Inline)

##################################

=================================================
  Variable Data Simulation (scalar unknowns: 4)
=================================================

Algebraic Variables (3/3)
***************************
(1)       [ALGB] (1) Real a (start = 1.0)
(2)       [ALGB] (1) Real b
(3)       [ALGB] (1) Real c

Discrete Variables (1/1)
**************************
(1)       [DISC] (1) discrete Real d (fixed = true, start = 0.0)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 4)
=============================================================

Continuous Equations (3/3)
****************************
(1)       [SCAL] (1) der(a) = -(1.0 + 3.0 * a) ($RES_SIM_4)
(2)       [SCAL] (1) b = 2.0 * time * (1.0 + h(time)) ($RES_SIM_3)
(3)       [SCAL] (1) c = 3.0 * (1.0 + 3.0 * time) + 3.0 * b * (1.0 + h(b)) ($RES_SIM_2)

Discrete Equations (1/1)
**************************
(1)       [WHEN] (1) ($RES_SIM_1)
(1)       [----] when sample(0.0, 0.1) then
(1)       [----]   d := 1.0 + 3.0 * pre(d)
(1)       [----] end when;

(Exclusively) Initial Equations (1/1)
***************************************
(1)       [SCAL] (1) a = 1.0 + h(time) ($RES_SIM_0)

)omc";
  YMC_CHECK_EQ(early_inline_src(src), std::string(omc));
}

// 不论有没有内联，simulation 和初值里的 if 方程都重造：起新名字（unique_index 接着编，simulation 的先、初值的后），挪到桶尾；
// 嵌着的 if 先被重造一次占掉一串号（$RES_SIM_19-21 …），外层起名时连体重编。条件和体里的调用照内联（两遍）。
YMC_TEST(if_equations_remade) {
  const char* src = R"omc(function f
  input Real x;
  output Real y;
algorithm
  y := 2 * x + 1;
end f;
model EiIfs
  Real a(start = 1), b, c;
  Integer k;
  Real w = sin(time);
equation
  der(a) = -a;
  if f(time) > 2 then
    if time > 0.7 then
      b = 1;
      k = 1;
    else
      b = f(time);
      k = 2;
    end if;
    c = 3;
  else
    b = 4;
    k = 3;
    c = 5;
  end if;
initial equation
  if time > 0 then
    a = f(time);
  else
    a = 2;
  end if;
end EiIfs;
)omc";
  const char* omc = R"omc(##################################

    BackendDAE: (Early Inline)

##################################

=================================================
  Variable Data Simulation (scalar unknowns: 6)
=================================================

Algebraic Variables (5/5)
***************************
(1)       [ALGB] (1) Real a (start = 1.0)
(2)       [ALGB] (1) Real b
(3)       [ALGB] (1) Real c
(4)       [ALGB] (1) Real w = sin(time)
(5)       [ALGB] (1) Real $FUN_1

Discrete Variables (1/1)
**************************
(1)       [DISC] (1) Integer k

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 6)
=============================================================

Continuous Equations (5/5)
****************************
(1)       [SCAL] (1) der(a) = -a ($RES_SIM_16)
(2)       [SCAL] (1) w = $FUN_1 ($RES_BND_17)
(3)       [SCAL] (1) $FUN_1 = sin(time) ($RES_AUX_18)
(4)       [-IF-] (1) ($RES_SIM_30)
(4)       [----] if 1.0 + 2.0 * time > 2.0 then
(4)       [----]   [-IF-] (1) ($RES_SIM_31)
(4)       [----]   [----] if time > 0.7 then
(4)       [----]   [----]   [SCAL] (1) b = 1.0 ($RES_SIM_32)
(4)       [----]   [----] else
(4)       [----]   [----]   [SCAL] (1) b = 1.0 + 2.0 * time ($RES_SIM_33)
(4)       [----]   [----] end if;
(4)       [----] else
(4)       [----]   [SCAL] (1) b = 4.0 ($RES_SIM_34)
(4)       [----] end if;
(5)       [-IF-] (1) ($RES_SIM_35)
(5)       [----] if 1.0 + 2.0 * time > 2.0 then
(5)       [----]   [SCAL] (1) c = 3.0 ($RES_SIM_36)
(5)       [----] else
(5)       [----]   [SCAL] (1) c = 5.0 ($RES_SIM_37)
(5)       [----] end if;

Discrete Equations (1/1)
**************************
(1)       [-IF-] (1) ($RES_SIM_22)
(1)       [----] if 1.0 + 2.0 * time > 2.0 then
(1)       [----]   [-IF-] (1) ($RES_SIM_23)
(1)       [----]   [----] if time > 0.7 then
(1)       [----]   [----]   [SCAL] (1) k = 1 ($RES_SIM_24)
(1)       [----]   [----] else
(1)       [----]   [----]   [SCAL] (1) k = 2 ($RES_SIM_25)
(1)       [----]   [----] end if;
(1)       [----] else
(1)       [----]   [SCAL] (1) k = 3 ($RES_SIM_26)
(1)       [----] end if;

(Exclusively) Initial Equations (1/1)
***************************************
(1)       [-IF-] (1) ($RES_SIM_38)
(1)       [----] if time > 0.0 then
(1)       [----]   [SCAL] (1) a = 1.0 + 2.0 * time ($RES_SIM_39)
(1)       [----] else
(1)       [----]   [SCAL] (1) a = 2.0 ($RES_SIM_40)
(1)       [----] end if;

)omc";
  YMC_CHECK_EQ(early_inline_src(src), std::string(omc));
}

// 体代换后 combineBinaries 再 simplify：`^` 的操作数里留 BINARY（simplifyBinaryAdd / Sub / Mul / Div），字面量实参代进后在 Real 上折掉
// （z * z → 4.0、(z + 1) → 3.0），除式的负号照原文；Boolean、Integer 结果进 Discrete 桶。
YMC_TEST(body_simplified) {
  const char* src = R"omc(function f
  input Real x;
  input Real z;
  input Integer n;
  output Real y;
algorithm
  y := (x + z) ^ 2 + x ^ (z * z) - (z + 1) ^ x;
end f;
function g
  input Real x;
  input Real z;
  output Real y;
algorithm
  y := (x / z - z) * (-x) / (-z) + x * 1.0 - 0 * x;
end g;
function b
  input Real x;
  output Boolean c;
algorithm
  c := x > 0.5 and not x > 0.9;
end b;
function i
  input Integer n;
  output Integer m;
algorithm
  m := if n > 2 then 2 * n - 1 else n;
end i;
model EiSimplify
  parameter Real p = 2.5;
  Real a, a2;
  Boolean c;
  Integer m, k(start = 1, fixed = true);
equation
  a = f(time, 2.0, 3);
  a2 = g(time, p) + g(2 * time, -2.0);
  c = b(time);
  when sample(0, 0.1) then
    k = pre(k) + 1;
  end when;
  m = i(k);
end EiSimplify;
)omc";
  const char* omc = R"omc(##################################

    BackendDAE: (Early Inline)

##################################

=================================================
  Variable Data Simulation (scalar unknowns: 5)
=================================================

Algebraic Variables (2/2)
***************************
(1)       [ALGB] (1) Real a
(2)       [ALGB] (1) Real a2

Discrete Variables (3/3)
**************************
(1)       [DISC] (1) Boolean c
(2)       [DISC] (1) Integer m
(3)       [DISC] (1) Integer k (fixed = true, start = 1)

Parameter Variables (1/1)
***************************
(1)       [PRMT] (1) parameter Real p = 2.5 (fixed = true)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 5)
=============================================================

Continuous Equations (2/2)
****************************
(1)       [SCAL] (1) a = (time ^ 4.0 + (time + 2.0) ^ 2.0) - 3.0 ^ time ($RES_SIM_4)
(2)       [SCAL] (1) a2 = ((time * (time / p - p)) / p + time) + (2.0 * time - 0.5 * (2.0 * time) * (2.0 - time)) ($RES_SIM_3)

Discrete Equations (3/3)
**************************
(1)       [SCAL] (1) c = time > 0.5 and not time > 0.9 ($RES_SIM_2)
(2)       [WHEN] (1) ($RES_SIM_1)
(2)       [----] when sample(0.0, 0.1) then
(2)       [----]   k := pre(k) + 1
(2)       [----] end when;
(3)       [SCAL] (1) m = if k > 2 then (-1) + 2 * k else k ($RES_SIM_0)

)omc";
  YMC_CHECK_EQ(early_inline_src(src), std::string(omc));
}

// 多条语句的函数、外部函数（打成 cos）不内联，FunctionAlias 留下的 $FUN 照旧；impure 的单条赋值函数也内联（Inline 不看纯度）；
// sin(f(time)) 的 $FUN 方程里 f 被换掉；变量行的绑定不动；Removed（reinit 的 when、assert）不映射。
YMC_TEST(kept_calls) {
  const char* src = R"omc(function f
  input Real x;
  output Real y;
algorithm
  y := 2 * x;
end f;
function many
  input Real x;
  output Real y;
algorithm
  y := 2 * x;
  y := y + 1;
end many;
impure function imp
  input Real x;
  output Real y;
algorithm
  y := 2 * x;
end imp;
function ext
  input Real x;
  output Real y;
external "C" y = cos(x);
end ext;
model EiKeep
  Real s(start = 1, fixed = true);
  Real a, b;
  Real v = f(time);
equation
  der(s) = -many(s);
  a = imp(time) + ext(time);
  b = sin(f(time)) + f(sin(time));
  when time > 0.5 then
    reinit(s, f(time));
  end when;
  assert(f(time) < 10, "too big");
end EiKeep;
)omc";
  const char* omc = R"omc(##################################

    BackendDAE: (Early Inline)

##################################

=================================================
  Variable Data Simulation (scalar unknowns: 8)
=================================================

Algebraic Variables (8/8)
***************************
(1)       [ALGB] (1) Real s (fixed = true, start = 1.0)
(2)       [ALGB] (1) Real a
(3)       [ALGB] (1) Real b
(4)       [ALGB] (1) Real v = f(time)
(5)       [ALGB] (1) Real $FUN_1
(6)       [ALGB] (1) Real $FUN_2
(7)       [ALGB] (1) Real $FUN_3
(8)       [ALGB] (1) Real $FUN_4

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 8)
=============================================================

Continuous Equations (8/8)
****************************
(1)       [SCAL] (1) der(s) = -$FUN_1 ($RES_SIM_4)
(2)       [SCAL] (1) a = 2.0 * time + $FUN_2 ($RES_SIM_3)
(3)       [SCAL] (1) b = $FUN_3 + 2.0 * $FUN_4 ($RES_SIM_2)
(4)       [SCAL] (1) v = 2.0 * time ($RES_BND_5)
(5)       [SCAL] (1) $FUN_1 = many(s) ($RES_AUX_9)
(6)       [SCAL] (1) $FUN_2 = cos(time) ($RES_AUX_8)
(7)       [SCAL] (1) $FUN_3 = sin(2.0 * time) ($RES_AUX_7)
(8)       [SCAL] (1) $FUN_4 = sin(time) ($RES_AUX_6)

Removed Equations (2/0)
*************************
(1)       [WHEN] (0) ($RES_SIM_1)
(1)       [----] when time > 0.5 then
(1)       [----]   reinit(s, f(time))
(1)       [----] end when;
(2)       [WHEN] (0) ($RES_SIM_0)
(2)       [----] when f(time) < 10.0 then
(2)       [----]   assert(f(time) < 10.0, "too big", AssertionLevel.error)
(2)       [----] end when;

)omc";
  YMC_CHECK_EQ(early_inline_src(src), std::string(omc));
}

// 打不出和 omc 一样的字节就不打：体引用输出（输出的声明绑定不进替换表），omc 把 `y` 漏进方程（`a = 1.0 + y`）又当迭代量收进 Artificial 桶；
// 字面量实参代进体后 sin(2.0) 要 Ceval（omc：`a = 0.9092974268256817 * time`），没移植。
YMC_TEST(early_inline_stops_on_unsupported) {
  YMC_CHECK_EQ(early_inline_src(R"omc(function f
  input Real x;
  output Real y = 2 * x;
algorithm
  y := y + 1;
end f;
model EiOutputRef
  Real a;
equation
  a = f(time);
end EiOutputRef;
)omc"),
               std::string("unsupported: inline: function body refers to an output or a global"));
  YMC_CHECK_EQ(early_inline_src(R"omc(function f
  input Real x;
  input Real z;
  output Real y;
algorithm
  y := sin(z) * x;
end f;
model EiLiteralArg
  Real a;
equation
  a = f(time, 2.0);
end EiLiteralArg;
)omc"), std::string("unsupported: inline: call whose arguments became literals"));
}

YMC_TEST_MAIN
