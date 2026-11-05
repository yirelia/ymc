// Simplify 模块（第一次运行）端到端：源码 → 压平 → lower → bindings → function alias → early inline → simplify → dump，逐字对照
// omc 1.25.0 的 `BackendDAE: (Simplify 1)` 段。期望值都是 omc 实跑的输出，命令统一是
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
std::string simplify1_src(const char* src) {
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
       {&bdae::bindings, &bdae::function_alias, &bdae::early_inline, &bdae::simplify_module}) {
    const bdae::ModuleResult r = m(lr.dae, in);
    if (r.unsupported) return "unsupported: " + r.unsupported_reason;
  }
  return bdae::dump_stage(lr.dae, in, "Simplify 1");
}

}  // namespace

// when：条件是 false 的支去掉（elsewhen 顶上来；数组条件去掉 false 元素，剩一个就用它）；全去掉的 when 整条删，第一支赋值的变量从 Discrete
// 挪到 Parameter 桶尾、打 [PRMT]（先写的 when 的变量在前）；removed 里的 assert(false) 同样删掉，条件里的常数并掉（6.0 * p）。
YMC_TEST(when_branches_and_parameters) {
  const char* src = R"omc(model S1When
  parameter Real p = 2;
  Real x, v, w, z;
  Integer i;
equation
  w = time;
  when false then z = 1; end when;
  when time > 2 * p * 3 then x = 2; end when;
  when false then i = 3; end when;
  when false then v = 1; elsewhen {false, w > 1} then v = 2; end when;
  assert(false, "removed with the when");
  assert(w > 2 * p * 3, "kept");
end S1When;
)omc";
  const char* omc = R"omc(################################

    BackendDAE: (Simplify 1)

################################

=================================================
  Variable Data Simulation (scalar unknowns: 3)
=================================================

Algebraic Variables (1/1)
***************************
(1)       [ALGB] (1) Real w

Discrete Variables (2/2)
**************************
(1)       [DISC] (1) Real x
(2)       [DISC] (1) Real v

Parameter Variables (3/3)
***************************
(1)       [PRMT] (1) parameter Real p = 2.0 (fixed = true)
(2)       [PRMT] (1) Real z
(3)       [PRMT] (1) Integer i

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 3)
=============================================================

Continuous Equations (1/1)
****************************
(1)       [SCAL] (1) w = time ($RES_SIM_6)

Discrete Equations (2/2)
**************************
(1)       [WHEN] (1) ($RES_SIM_4)
(1)       [----] when time > 6.0 * p then
(1)       [----]   x := 2.0
(1)       [----] end when;
(2)       [WHEN] (1) ($RES_SIM_2)
(2)       [----] when w > 1.0 then
(2)       [----]   v := 2.0
(2)       [----] end when;

Removed Equations (1/0)
*************************
(1)       [WHEN] (0) ($RES_SIM_0)
(1)       [----] when w > 6.0 * p then
(1)       [----]   assert(w > 6.0 * p, "kept", AssertionLevel.error)
(1)       [----] end when;

)omc";
  YMC_CHECK_EQ(simplify1_src(src), std::string(omc));
}

// if：各支左边相等就并成一条 `lhs = if … else …`，名字沿用；嵌套 if 一起并；左边不等的不动；条件化简成 false 的支去掉、true 的支砍掉后面的；
// `if c then true else false` 化成 c；只剩一支（含没有 else 的初值 if）时整条换成体里那条方程、用它的名字。
YMC_TEST(if_equations_folded) {
  const char* src = R"omc(function S1IfOne
  input Real u;
  output Real y;
algorithm
  y := 1;
end S1IfOne;
model S1If
  Real x, y, a, b(start = 0);
  Boolean c;
equation
  der(b) = 1;
  if time > 1 then
    if time > 2 then x = 1; else x = 2 * time * 3; end if;
  else
    x = 3;
  end if;
  if time > 1 then y = 1; else 2 = y; end if;
  if time > S1IfOne(time) then c = true; else c = false; end if;
  if S1IfOne(time) < 0 then a = 1; elseif time > 1 then a = time + 1 + 2; else a = 3; end if;
initial equation
  if time > 0 then b = 1; end if;
end S1If;
)omc";
  const char* omc = R"omc(################################

    BackendDAE: (Simplify 1)

################################

=================================================
  Variable Data Simulation (scalar unknowns: 5)
=================================================

Algebraic Variables (4/4)
***************************
(1)       [ALGB] (1) Real x
(2)       [ALGB] (1) Real y
(3)       [ALGB] (1) Real a
(4)       [ALGB] (1) Real b (start = 0.0)

Discrete Variables (1/1)
**************************
(1)       [DISC] (1) Boolean c

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 5)
=============================================================

Continuous Equations (4/4)
****************************
(1)       [SCAL] (1) der(b) = 1.0 ($RES_SIM_17)
(2)       [SCAL] (1) x = if time > 1.0 then if time > 2.0 then 1.0 else 6.0 * time else 3.0 ($RES_SIM_21)
(3)       [-IF-] (1) ($RES_SIM_26)
(3)       [----] if time > 1.0 then
(3)       [----]   [SCAL] (1) y = 1.0 ($RES_SIM_27)
(3)       [----] else
(3)       [----]   [SCAL] (1) 2.0 = y ($RES_SIM_28)
(3)       [----] end if;
(4)       [SCAL] (1) a = if time > 1.0 then 3.0 + time else 3.0 ($RES_SIM_32)

Discrete Equations (1/1)
**************************
(1)       [SCAL] (1) c = time > 1.0 ($RES_SIM_29)

(Exclusively) Initial Equations (1/1)
***************************************
(1)       [SCAL] (1) b = 1.0 ($RES_SIM_37)

)omc";
  YMC_CHECK_EQ(simplify1_src(src), std::string(omc));
}

// 两边都是字面量的二元式照 Ceval：String 相加拼接（方程里 assert 的消息前端不化简）；除以 0.0 原样留 BINARY（Bindings 造的 NaN）；
// when 拆分拼出的 `not {false, false}` 逐元素取反成 `{true, true}`。
YMC_TEST(literal_operands) {
  const char* src = R"omc(model S1Lit
  Real s(start = 1, fixed = true), x, y;
  Real a = 0 / 0;
equation
  y = time;
  der(s) = 1;
  when {false, false} then x = 1; elsewhen y > 9 then x = 2; reinit(s, 1); end when;
  assert(y > 1, "a" + "b" + String(y));
end S1Lit;
)omc";
  const char* omc = R"omc(################################

    BackendDAE: (Simplify 1)

################################

=================================================
  Variable Data Simulation (scalar unknowns: 4)
=================================================

Algebraic Variables (3/3)
***************************
(1)       [ALGB] (1) Real s (fixed = true, start = 1.0)
(2)       [ALGB] (1) Real y
(3)       [ALGB] (1) Real a = 0.0 / 0.0

Discrete Variables (1/1)
**************************
(1)       [DISC] (1) Real x

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 4)
=============================================================

Continuous Equations (3/3)
****************************
(1)       [SCAL] (1) y = time ($RES_SIM_4)
(2)       [SCAL] (1) der(s) = 1.0 ($RES_SIM_3)
(3)       [SCAL] (1) a = 0.0 / 0.0 ($RES_BND_5)

Discrete Equations (1/1)
**************************
(1)       [WHEN] (1) ($RES_SIM_1)
(1)       [----] when {false, false} then
(1)       [----]   x := 1.0
(1)       [----] elsewhen y > 9.0 then
(1)       [----]   x := 2.0
(1)       [----] end when;

Removed Equations (2/0)
*************************
(1)       [WHEN] (0) ($RES_SIM_2)
(1)       [----] when {true, true} and y > 9.0 then
(1)       [----]   reinit(s, 1.0)
(1)       [----] end when;
(2)       [WHEN] (0) ($RES_SIM_0)
(2)       [----] when y > 1.0 then
(2)       [----]   assert(y > 1.0, "ab" + String(y, 6, 0, true), AssertionLevel.error)
(2)       [----] end when;

)omc";
  YMC_CHECK_EQ(simplify1_src(src), std::string(omc));
}

// 式子：和 / 积里的常数并到最前（不约掉 x / x，嵌套的不展平）；两边相等的标量方程换成 0 = 0（Real 0.0、Integer 0）；
// 内联换进来的常数在关系、逻辑里折掉；算法再过一遍 simplifyStatements：if 语句第一支条件为 true 就换成它的体。
YMC_TEST(expressions_and_algorithms) {
  const char* src = R"omc(function S1ExpOne
  input Real u;
  output Real y;
algorithm
  y := 1;
end S1ExpOne;
model S1Exp
  parameter Integer n = 3;
  Real x, y, z, q, s(start = 0);
  Integer i, j;
  Boolean b;
equation
  2 * x = x * 2;
  y = (time - 1) * (time + 1) * 2;
  z = time / time + 1.0 / (2 * time);
  i = (1 + n) - 1;
  j = j;
  b = S1ExpOne(time) > 0 and time > 1;
  der(s) = -2 * s * 3;
algorithm
  if S1ExpOne(time) > 0 then
    q := time - 1;
  else
    q := 2;
  end if;
  while q / 10 < y loop
    q := q + 0.5;
  end while;
initial equation
  s = 2 * time * 3 + 1 + 2;
end S1Exp;
)omc";
  const char* omc = R"omc(################################

    BackendDAE: (Simplify 1)

################################

=================================================
  Variable Data Simulation (scalar unknowns: 8)
=================================================

Algebraic Variables (5/5)
***************************
(1)       [ALGB] (1) Real x
(2)       [ALGB] (1) Real y
(3)       [ALGB] (1) Real z
(4)       [ALGB] (1) Real q
(5)       [ALGB] (1) Real s (start = 0.0)

Discrete Variables (3/3)
**************************
(1)       [DISC] (1) Integer i
(2)       [DISC] (1) Integer j
(3)       [DISC] (1) Boolean b

Parameter Variables (1/1)
***************************
(1)       [PRMT] (1) parameter Integer n = 3 (fixed = true)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 8)
=============================================================

Continuous Equations (5/5)
****************************
(1)       [SCAL] (1) 0.0 = 0.0 ($RES_SIM_8)
(2)       [SCAL] (1) y = 2.0 * (1.0 + time) * ((-1.0) + time) ($RES_SIM_7)
(3)       [SCAL] (1) z = time / time + 0.5 / time ($RES_SIM_6)
(4)       [SCAL] (1) der(s) = -6.0 * s ($RES_SIM_2)
(5)       [ALGO] (1) ($RES_SIM_1)
(5)       [----] q := (-1.0) + time;

(5)       [----] while 0.1 * q < y then
(5)       [----]   q := 0.5 + q;
(5)       [----] end while;


Discrete Equations (3/3)
**************************
(1)       [SCAL] (1) i = n ($RES_SIM_5)
(2)       [SCAL] (1) 0 = 0 ($RES_SIM_4)
(3)       [SCAL] (1) b = time > 1.0 ($RES_SIM_3)

(Exclusively) Initial Equations (1/1)
***************************************
(1)       [SCAL] (1) s = 3.0 + 6.0 * time ($RES_SIM_0)

)omc";
  YMC_CHECK_EQ(simplify1_src(src), std::string(omc));
}

// omc 没有这一段的两种形态，ymc 也不打（退 66）：
//   - `e = e`（枚举）：makeZero 没有枚举的零，NFExpression.makeZero 报内部错，模块失败（实跑：Error: Internal error NFExpression.makeZero failed for:
//     enumeration E(a, b)，探针 Ls1E_enum1）；
//   - 删掉的 when 变量 z 排在留下的 v 前面：Discrete 桶留了空位，VariablePointers.toString 取空位 fail，段打不出来（实跑无报错文本，探针 Ls1W_order2）。
YMC_TEST(omc_has_no_section) {
  const char* enum_self = R"omc(model S1Enum
  type E = enumeration(a, b);
  E e;
  Real y;
equation
  e = e;
  y = time;
end S1Enum;
)omc";
  YMC_CHECK(simplify1_src(enum_self).rfind("unsupported: ", 0) == 0);
  const char* hole = R"omc(model S1Hole
  Real z, w, v;
equation
  w = time;
  when false then z = 1; end when;
  when w > 1 then v = 2; end when;
end S1Hole;
)omc";
  YMC_CHECK(simplify1_src(hole).rfind("unsupported: ", 0) == 0);
}

YMC_TEST_MAIN
