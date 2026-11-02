// lower 端到端：源码 → 压平 → lower → dump，逐字对照 omc 1.25.0 的 `BackendDAE: (After Lowering)` 段。
// 期望值都是 omc 实跑的输出（每个用例的注释写了命令），不是 ymc 的输出。
#include "ymc_test.h"

#include <string>
#include <vector>

#include "absyn/absyn.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "bdae/bdae_dump.h"
#include "bdae/combine_binaries.h"
#include "bdae/lower.h"
#include "flat/flatten.h"
#include "inst/inst.h"
#include "parse/lexer.h"
#include "parse/parser.h"
#include "scode/absyn_to_scode.h"
#include "scode/scode.h"

using namespace ymc;

namespace {

// 全流水线（同 driver）：压平文件里最后一个顶层类再 lower。没做的构造返回 `unsupported: <原因>`。
std::string lower_src(const char* src) {
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
  return bdae::dump_after_lowering(lr.dae, in);
}

Id<Expr> cref(ExprArena& a, Symbol s) {
  Expr e;
  e.kind = ExprKind::Cref;
  e.ty = Type::real();
  e.cref.parts.push_back(ExprCrefPart{s, {}});
  return a.add(std::move(e));
}
Id<Expr> binary(ExprArena& a, BinOp op, Id<Expr> l, Id<Expr> r, TypeKind k = TypeKind::Real) {
  Expr e;
  e.kind = ExprKind::Binary;
  e.ty = Type::of(k);
  e.bin = op;
  e.a = l;
  e.b = r;
  return a.add(std::move(e));
}

}  // namespace

// ADR-008 附录 A 的 Adr008A：omc 把方程原样留到这里，Alias 之后才改写。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo Adr008A.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(adr008a) {
  const char* src = R"omc(model Adr008A
  parameter Real k = 2;
  Real x(start = 1, fixed = true);
  Real a, b, y;
equation
  der(x) = -k * a;
  a + b = x;
  a - 2 * b = time;
  y = 2 * x + b;
end Adr008A;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 4)
=================================================

Algebraic Variables (4/4)
***************************
(1)       [ALGB] (1) Real x (fixed = true, start = 1.0)
(2)       [ALGB] (1) Real a
(3)       [ALGB] (1) Real b
(4)       [ALGB] (1) Real y

Parameter Variables (1/1)
***************************
(1)       [PRMT] (1) parameter Real k = 2.0 (fixed = true)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 4)
=============================================================

Continuous Equations (4/4)
****************************
(1)       [SCAL] (1) der(x) = -k * a ($RES_SIM_3)
(2)       [SCAL] (1) a + b = x ($RES_SIM_2)
(3)       [SCAL] (1) a - 2.0 * b = time ($RES_SIM_1)
(4)       [SCAL] (1) y = 2.0 * x + b ($RES_SIM_0)

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 链：a + b + c + d → c + b + a + d；a - b + c → (a + c) - b；乘除不并；^ 不下探。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerChain.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(chain_shapes) {
  const char* src = R"omc(model LowerChain
  Real a, b, c, d, y1, y2, y3, y4;
equation
  y1 = a + b + c + d;
  y2 = a - b + c;
  y3 = (a + b) * c / d;
  y4 = a * b ^ (c + d);
end LowerChain;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 8)
=================================================

Algebraic Variables (8/8)
***************************
(1)       [ALGB] (1) Real a
(2)       [ALGB] (1) Real b
(3)       [ALGB] (1) Real c
(4)       [ALGB] (1) Real d
(5)       [ALGB] (1) Real y1
(6)       [ALGB] (1) Real y2
(7)       [ALGB] (1) Real y3
(8)       [ALGB] (1) Real y4

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 4)
=============================================================

Continuous Equations (4/4)
****************************
(1)       [SCAL] (1) y1 = c + b + a + d ($RES_SIM_3)
(2)       [SCAL] (1) y2 = (a + c) - b ($RES_SIM_2)
(3)       [SCAL] (1) y3 = (c * (a + b)) / d ($RES_SIM_1)
(4)       [SCAL] (1) y4 = a * b ^ (c + d) ($RES_SIM_0)

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 同一式子绑定走深映射、方程走浅映射：绑定 sqrt(b * a)，方程 sqrt(a * b)。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerShape.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(binding_vs_equation) {
  const char* src = R"omc(model LowerShape
  Real a, b;
  Real c = sqrt(a * b);
  Real d = sin(cos(a + b + 1));
  Real e;
equation
  e = sqrt(a * b);
  a = sin(cos(a + b + 1));
end LowerShape;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 5)
=================================================

Algebraic Variables (5/5)
***************************
(1)       [ALGB] (1) Real a
(2)       [ALGB] (1) Real b
(3)       [ALGB] (1) Real c = sqrt(b * a)
(4)       [ALGB] (1) Real d = sin(cos(b + a + 1.0))
(5)       [ALGB] (1) Real e

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 2)
=============================================================

Continuous Equations (2/2)
****************************
(1)       [SCAL] (1) e = sqrt(a * b) ($RES_SIM_1)
(2)       [SCAL] (1) a = sin(cos(b + a + 1.0)) ($RES_SIM_0)

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 声明前缀、桶、属性次序、Evaluate 的 final、top level input。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerPrefix.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(declaration_prefixes) {
  const char* src = R"omc(model LowerPrefix
  connector C
    Real v;
    flow Real i;
  end C;
  type E = enumeration(one, two);
  input Real u;
  output Real y;
  final parameter Real p = 1;
  constant Integer n = 2;
  discrete Integer di;
  inner Boolean b;
  E e(start = E.two, fixed = true);
  C c;
  String s(start = "x");
  parameter Boolean ev = true annotation(Evaluate = true);
  parameter Real q = 3;
  Real w(min = p, stateSelect = StateSelect.prefer, unit = "m");
protected
  Real h(nominal = 2);
end LowerPrefix;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 9)
=================================================

Algebraic Variables (5/5)
***************************
(1)       [ALGB] (1) output Real y
(2)       [ALGB] (1) Real c.v
(3)       [ALGB] (1) flow Real c.i
(4)       [ALGB] (1) Real w (min = p, StateSelect = prefer)
(5)       [ALGB] (1) protected Real h (nominal = 2.0)

Discrete Variables (4/4)
**************************
(1)       [DISC] (1) Integer di
(2)       [DISC] (1) inner Boolean b
(3)       [DISC] (1) enumeration E(one, two) e (fixed = true, start = E.two)
(4)       [DISC] (1) String s (start = "x")

Top Level Input Variables (1/1)
*********************************
(1)       [ALGB] (1) input Real u

Parameter Variables (3/3)
***************************
(1)       [PRMT] (1) final parameter Real p = 1.0 (fixed = true)
(2)       [PRMT] (1) final parameter Boolean ev = true (fixed = true)
(3)       [PRMT] (1) parameter Real q = 3.0 (fixed = true)

Constant Variables (1/1)
**************************
(1)       [CNST] (1) constant Integer n = 2

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 1)
=============================================================

Continuous Equations (1/1)
****************************
(1)       [SCAL] (1) c.i = 0.0 ($RES_SIM_0)

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 一个 when 同时赋值和 reinit：拆成 [离散, 状态]，离散编号小；reinit 进 Removed；状态那一轮的条件带上取反的前一支（WhenEquationBody.split 不清 acc_condition）。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerWhenSplit.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(when_reinit_split) {
  const char* src = R"omc(model LowerWhenSplit
  Real x(start = 1, fixed = true);
  discrete Real y;
  Real w;
equation
  der(x) = 1;
  w = 1;
  when x > 1 then
    y = 1;
  elsewhen x + w + 1 > 2 then
    y = 2;
  elsewhen {x < 0, w > 3} then
    y = pre(y) + w + 1;
    reinit(x, -0.8 * pre(x));
  end when;
  w = 2;
end LowerWhenSplit;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 3)
=================================================

Algebraic Variables (2/2)
***************************
(1)       [ALGB] (1) Real x (fixed = true, start = 1.0)
(2)       [ALGB] (1) Real w

Discrete Variables (1/1)
**************************
(1)       [DISC] (1) discrete Real y

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 4)
=============================================================

Continuous Equations (3/3)
****************************
(1)       [SCAL] (1) der(x) = 1.0 ($RES_SIM_4)
(2)       [SCAL] (1) w = 1.0 ($RES_SIM_3)
(3)       [SCAL] (1) w = 2.0 ($RES_SIM_0)

Discrete Equations (1/1)
**************************
(1)       [WHEN] (1) ($RES_SIM_1)
(1)       [----] when x > 1.0 then
(1)       [----]   y := 1.0
(1)       [----] elsewhen w + x + 1.0 > 2.0 then
(1)       [----]   y := 2.0
(1)       [----] elsewhen {x < 0.0, w > 3.0} then
(1)       [----]   y := w + pre(y) + 1.0
(1)       [----] end when;

Removed Equations (1/0)
*************************
(1)       [WHEN] (0) ($RES_SIM_2)
(1)       [----] when (not x > 1.0 and not w + x + 1.0 > 2.0) and {x < 0.0, w > 3.0} then
(1)       [----]   reinit(x, -0.8 * pre(x))
(1)       [----] end when;

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// when 里赋值的 Real 是 [DISC]，不打 discrete；各类型的目标；只有 terminate / 无返回值调用的 when 整条消失、不占编号。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerWhenKinds.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(when_implicit_discrete) {
  const char* src = R"omc(model LowerWhenKinds
  type E = enumeration(one, two);
  function report
    input Real u;
  algorithm
    assert(u > 0, "h");
  end report;
  Real y(start = 1, fixed = true);
  Real z = 3;
  input Real u;
  Integer n;
  Boolean b;
  String s;
  E e;
protected
  Real h;
equation
  when {time > 1, b} then
    y = pre(y) + 1;
  end when;
  when time > 2 then
    terminate("stop");
  end when;
  when sample(0, 1) then
    z = 1;
  end when;
  when edge(b) then
    u = 2;
  end when;
  when change(n) then
    n = pre(n) + 1;
  end when;
  when time > 3 then
    b = not pre(b);
    report(time);
  end when;
  when initial() then
    s = "a" + "b";
  end when;
  when time > 4 then
    e = E.two;
  end when;
  when time > 5 then
    h = 1;
  end when;
end LowerWhenKinds;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 7)
=================================================

Discrete Variables (7/7)
**************************
(1)       [DISC] (1) Real y (fixed = true, start = 1.0)
(2)       [DISC] (1) Real z = 3.0
(3)       [DISC] (1) Integer n
(4)       [DISC] (1) Boolean b
(5)       [DISC] (1) String s
(6)       [DISC] (1) enumeration E(one, two) e
(7)       [DISC] (1) protected Real h

Top Level Input Variables (1/1)
*********************************
(1)       [DISC] (1) input Real u

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 8)
=============================================================

Discrete Equations (8/8)
**************************
(1)       [WHEN] (1) ($RES_SIM_7)
(1)       [----] when {time > 1.0, b} then
(1)       [----]   y := pre(y) + 1.0
(1)       [----] end when;
(2)       [WHEN] (1) ($RES_SIM_6)
(2)       [----] when sample(0.0, 1.0) then
(2)       [----]   z := 1.0
(2)       [----] end when;
(3)       [WHEN] (1) ($RES_SIM_5)
(3)       [----] when edge(b) then
(3)       [----]   u := 2.0
(3)       [----] end when;
(4)       [WHEN] (1) ($RES_SIM_4)
(4)       [----] when change(n) then
(4)       [----]   n := pre(n) + 1
(4)       [----] end when;
(5)       [WHEN] (1) ($RES_SIM_3)
(5)       [----] when time > 3.0 then
(5)       [----]   b := not pre(b)
(5)       [----] end when;
(6)       [WHEN] (1) ($RES_SIM_2)
(6)       [----] when initial() then
(6)       [----]   s := "ab"
(6)       [----] end when;
(7)       [WHEN] (1) ($RES_SIM_1)
(7)       [----] when time > 4.0 then
(7)       [----]   e := E.two
(7)       [----] end when;
(8)       [WHEN] (1) ($RES_SIM_0)
(8)       [----] when time > 5.0 then
(8)       [----]   h := 1.0
(8)       [----] end when;

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// assert 成 [WHEN] (0)：默认 AssertionLevel.error；进 Removed，初始方程里的进 Initial；消息的 + 不交换、不下探，条件走 combineBinaries；占编号。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerAssert.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(assert_removed) {
  const char* src = R"omc(model LowerAssert
  parameter Real p = 1;
  Real x;
initial equation
  x = 1;
  assert(p > 0, "ini");
equation
  x = time;
  assert(x > p, "a" + String(x), AssertionLevel.warning);
  assert(x + p + 1 > 0, "b" + "c" + String(x + p + 1));
  assert(x > 0, String(x + p, significantDigits = 3), AssertionLevel.error);
end LowerAssert;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 1)
=================================================

Algebraic Variables (1/1)
***************************
(1)       [ALGB] (1) Real x

Parameter Variables (1/1)
***************************
(1)       [PRMT] (1) parameter Real p = 1.0 (fixed = true)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 1)
=============================================================

Continuous Equations (1/1)
****************************
(1)       [SCAL] (1) x = time ($RES_SIM_5)

(Exclusively) Initial Equations (2/1)
***************************************
(1)       [SCAL] (1) x = 1.0 ($RES_SIM_1)
(2)       [WHEN] (0) ($RES_SIM_0)
(2)       [----] when p > 0.0 then
(2)       [----]   assert(p > 0.0, "ini", AssertionLevel.error)
(2)       [----] end when;

Removed Equations (3/0)
*************************
(1)       [WHEN] (0) ($RES_SIM_4)
(1)       [----] when x > p then
(1)       [----]   assert(x > p, "a" + String(x, 6, 0, true), AssertionLevel.warning)
(1)       [----] end when;
(2)       [WHEN] (0) ($RES_SIM_3)
(2)       [----] when p + x + 1.0 > 0.0 then
(2)       [----]   assert(p + x + 1.0 > 0.0, "b" + "c" + String(x + p + 1.0, 6, 0, true), AssertionLevel.error)
(2)       [----] end when;
(3)       [WHEN] (0) ($RES_SIM_2)
(3)       [----] when x > 0.0 then
(3)       [----]   assert(x > 0.0, String(x + p, 3, 0, true), AssertionLevel.error)
(3)       [----] end when;

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 多赋值 when：离散量按语句反序进 keyList，拆完打印回到源序；条目之间 acc_condition 不清零，s 的那条带上 x、y 没用完的取反条件；elsewhen 的序不影响条目序。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerWhenMulti.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(when_multi_assign) {
  const char* src = R"omc(model LowerWhenMulti
  Real x, y;
  Real s(start = 1, fixed = true), r(start = 2, fixed = true);
equation
  der(s) = 1;
  der(r) = 1;
  when time > 1 then
    x = 1;
    y = 2;
    reinit(r, 1);
  elsewhen time > 2 then
    y = 3;
    x = 4;
    reinit(s, 2);
  elsewhen time > 3 then
    reinit(s, 3);
    reinit(r, 4);
    x = 5;
    y = 6;
  end when;
end LowerWhenMulti;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 4)
=================================================

Algebraic Variables (2/2)
***************************
(1)       [ALGB] (1) Real s (fixed = true, start = 1.0)
(2)       [ALGB] (1) Real r (fixed = true, start = 2.0)

Discrete Variables (2/2)
**************************
(1)       [DISC] (1) Real x
(2)       [DISC] (1) Real y

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 4)
=============================================================

Continuous Equations (2/2)
****************************
(1)       [SCAL] (1) der(s) = 1.0 ($RES_SIM_5)
(2)       [SCAL] (1) der(r) = 1.0 ($RES_SIM_4)

Discrete Equations (2/2)
**************************
(1)       [WHEN] (1) ($RES_SIM_1)
(1)       [----] when time > 1.0 then
(1)       [----]   x := 1.0
(1)       [----] elsewhen time > 2.0 then
(1)       [----]   x := 4.0
(1)       [----] elsewhen time > 3.0 then
(1)       [----]   x := 5.0
(1)       [----] end when;
(2)       [WHEN] (1) ($RES_SIM_0)
(2)       [----] when time > 1.0 then
(2)       [----]   y := 2.0
(2)       [----] elsewhen time > 2.0 then
(2)       [----]   y := 3.0
(2)       [----] elsewhen time > 3.0 then
(2)       [----]   y := 6.0
(2)       [----] end when;

Removed Equations (2/0)
*************************
(1)       [WHEN] (0) ($RES_SIM_3)
(1)       [----] when time > 1.0 then
(1)       [----]   reinit(r, 1.0)
(1)       [----] elsewhen not time > 2.0 and time > 3.0 then
(1)       [----]   reinit(r, 4.0)
(1)       [----] end when;
(2)       [WHEN] (0) ($RES_SIM_2)
(2)       [----] when not time > 1.0 and time > 2.0 then
(2)       [----]   reinit(s, 2.0)
(2)       [----] elsewhen time > 3.0 then
(2)       [----]   reinit(s, 3.0)
(2)       [----] end when;

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 15 个 reinit 过了 13 桶 Set 的 rehash 阈值：打印序是 state_set 的桶序（UnorderedSet.toList 再反一次），不是源序。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerWhenReinit.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(when_many_reinit) {
  const char* src = R"omc(model LowerWhenReinit
  Real qr(start = 1);
  Real mu(start = 1);
  Real alpha(start = 1);
  Real zeta9(start = 1);
  Real r2d2(start = 1);
  Real kappa(start = 1);
  Real omega(start = 1);
  Real nu0(start = 1);
  Real psi(start = 1);
  Real tau7(start = 1);
  Real beta(start = 1);
  Real gamma3(start = 1);
  Real eps(start = 1);
  Real xi(start = 1);
  Real rho(start = 1);
equation
  der(qr) = 1;
  der(mu) = 1;
  der(alpha) = 1;
  der(zeta9) = 1;
  der(r2d2) = 1;
  der(kappa) = 1;
  der(omega) = 1;
  der(nu0) = 1;
  der(psi) = 1;
  der(tau7) = 1;
  der(beta) = 1;
  der(gamma3) = 1;
  der(eps) = 1;
  der(xi) = 1;
  der(rho) = 1;
  when time > 1 then
    reinit(qr, 0);
    reinit(mu, 1);
    reinit(alpha, 2);
    reinit(zeta9, 3);
    reinit(r2d2, 4);
    reinit(kappa, 5);
    reinit(omega, 6);
    reinit(nu0, 7);
    reinit(psi, 8);
    reinit(tau7, 9);
    reinit(beta, 10);
    reinit(gamma3, 11);
    reinit(eps, 12);
    reinit(xi, 13);
    reinit(rho, 14);
  end when;
end LowerWhenReinit;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

==================================================
  Variable Data Simulation (scalar unknowns: 15)
==================================================

Algebraic Variables (15/15)
*****************************
(1)       [ALGB] (1) Real qr (start = 1.0)
(2)       [ALGB] (1) Real mu (start = 1.0)
(3)       [ALGB] (1) Real alpha (start = 1.0)
(4)       [ALGB] (1) Real zeta9 (start = 1.0)
(5)       [ALGB] (1) Real r2d2 (start = 1.0)
(6)       [ALGB] (1) Real kappa (start = 1.0)
(7)       [ALGB] (1) Real omega (start = 1.0)
(8)       [ALGB] (1) Real nu0 (start = 1.0)
(9)       [ALGB] (1) Real psi (start = 1.0)
(10)      [ALGB] (1) Real tau7 (start = 1.0)
(11)      [ALGB] (1) Real beta (start = 1.0)
(12)      [ALGB] (1) Real gamma3 (start = 1.0)
(13)      [ALGB] (1) Real eps (start = 1.0)
(14)      [ALGB] (1) Real xi (start = 1.0)
(15)      [ALGB] (1) Real rho (start = 1.0)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


==============================================================
  Equation Data Simulation (scalar simulation equations: 15)
==============================================================

Continuous Equations (15/15)
******************************
(1)       [SCAL] (1) der(qr) = 1.0 ($RES_SIM_29)
(2)       [SCAL] (1) der(mu) = 1.0 ($RES_SIM_28)
(3)       [SCAL] (1) der(alpha) = 1.0 ($RES_SIM_27)
(4)       [SCAL] (1) der(zeta9) = 1.0 ($RES_SIM_26)
(5)       [SCAL] (1) der(r2d2) = 1.0 ($RES_SIM_25)
(6)       [SCAL] (1) der(kappa) = 1.0 ($RES_SIM_24)
(7)       [SCAL] (1) der(omega) = 1.0 ($RES_SIM_23)
(8)       [SCAL] (1) der(nu0) = 1.0 ($RES_SIM_22)
(9)       [SCAL] (1) der(psi) = 1.0 ($RES_SIM_21)
(10)      [SCAL] (1) der(tau7) = 1.0 ($RES_SIM_20)
(11)      [SCAL] (1) der(beta) = 1.0 ($RES_SIM_19)
(12)      [SCAL] (1) der(gamma3) = 1.0 ($RES_SIM_18)
(13)      [SCAL] (1) der(eps) = 1.0 ($RES_SIM_17)
(14)      [SCAL] (1) der(xi) = 1.0 ($RES_SIM_16)
(15)      [SCAL] (1) der(rho) = 1.0 ($RES_SIM_15)

Removed Equations (15/0)
**************************
(1)       [WHEN] (0) ($RES_SIM_14)
(1)       [----] when time > 1.0 then
(1)       [----]   reinit(xi, 13.0)
(1)       [----] end when;
(2)       [WHEN] (0) ($RES_SIM_13)
(2)       [----] when time > 1.0 then
(2)       [----]   reinit(beta, 10.0)
(2)       [----] end when;
(3)       [WHEN] (0) ($RES_SIM_12)
(3)       [----] when time > 1.0 then
(3)       [----]   reinit(alpha, 2.0)
(3)       [----] end when;
(4)       [WHEN] (0) ($RES_SIM_11)
(4)       [----] when time > 1.0 then
(4)       [----]   reinit(kappa, 5.0)
(4)       [----] end when;
(5)       [WHEN] (0) ($RES_SIM_10)
(5)       [----] when time > 1.0 then
(5)       [----]   reinit(zeta9, 3.0)
(5)       [----] end when;
(6)       [WHEN] (0) ($RES_SIM_9)
(6)       [----] when time > 1.0 then
(6)       [----]   reinit(tau7, 9.0)
(6)       [----] end when;
(7)       [WHEN] (0) ($RES_SIM_8)
(7)       [----] when time > 1.0 then
(7)       [----]   reinit(gamma3, 11.0)
(7)       [----] end when;
(8)       [WHEN] (0) ($RES_SIM_7)
(8)       [----] when time > 1.0 then
(8)       [----]   reinit(qr, 0.0)
(8)       [----] end when;
(9)       [WHEN] (0) ($RES_SIM_6)
(9)       [----] when time > 1.0 then
(9)       [----]   reinit(rho, 14.0)
(9)       [----] end when;
(10)      [WHEN] (0) ($RES_SIM_5)
(10)      [----] when time > 1.0 then
(10)      [----]   reinit(r2d2, 4.0)
(10)      [----] end when;
(11)      [WHEN] (0) ($RES_SIM_4)
(11)      [----] when time > 1.0 then
(11)      [----]   reinit(eps, 12.0)
(11)      [----] end when;
(12)      [WHEN] (0) ($RES_SIM_3)
(12)      [----] when time > 1.0 then
(12)      [----]   reinit(psi, 8.0)
(12)      [----] end when;
(13)      [WHEN] (0) ($RES_SIM_2)
(13)      [----] when time > 1.0 then
(13)      [----]   reinit(omega, 6.0)
(13)      [----] end when;
(14)      [WHEN] (0) ($RES_SIM_1)
(14)      [----] when time > 1.0 then
(14)      [----]   reinit(nu0, 7.0)
(14)      [----] end when;
(15)      [WHEN] (0) ($RES_SIM_0)
(15)      [----] when time > 1.0 then
(15)      [----]   reinit(mu, 1.0)
(15)      [----] end when;

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// if 按方程拆开：每支离散的在前，再按左边排（cref 从末段比起，调用比函数名，种类不同比 record 序）；$RES_SIM 外层先、体里按支；入桶看第一支。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerIfSplit.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(if_split_sort) {
  const char* src = R"omc(model LowerIfSplit
  model A
    Real x, y;
  end A;
  A a, b;
  Real s(start = 1, fixed = true), w, z;
  Integer n;
  Boolean c;
equation
  if time > 1 then
    a.y = 1;
    w + z = 2;
    b.x = 3;
    n = 1;
    der(s) = 4;
    0 = z;
    c = true;
    a.x = 5;
    b.y = 6;
  elseif time > 2 then
    der(s) = 7;
    0 = z + 1;
    b.y = 8;
    c = false;
    b.x = 9;
    w + z = 10;
    a.x = 11;
    n = 2;
    a.y = 12;
  else
    n = 3;
    c = true;
    w + z = 13;
    a.x = 14;
    a.y = 15;
    b.x = 16;
    b.y = 17;
    der(s) = 18;
    0 = z + 2;
  end if;
end LowerIfSplit;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 9)
=================================================

Algebraic Variables (7/7)
***************************
(1)       [ALGB] (1) Real a.x
(2)       [ALGB] (1) Real a.y
(3)       [ALGB] (1) Real b.x
(4)       [ALGB] (1) Real b.y
(5)       [ALGB] (1) Real s (fixed = true, start = 1.0)
(6)       [ALGB] (1) Real w
(7)       [ALGB] (1) Real z

Discrete Variables (2/2)
**************************
(1)       [DISC] (1) Integer n
(2)       [DISC] (1) Boolean c

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 9)
=============================================================

Continuous Equations (7/7)
****************************
(1)       [-IF-] (1) ($RES_SIM_24)
(1)       [----] if time > 1.0 then
(1)       [----]   [SCAL] (1) 0.0 = z ($RES_SIM_25)
(1)       [----] elseif time > 2.0 then
(1)       [----]   [SCAL] (1) 0.0 = z + 1.0 ($RES_SIM_26)
(1)       [----] else
(1)       [----]   [SCAL] (1) 0.0 = z + 2.0 ($RES_SIM_27)
(1)       [----] end if;
(2)       [-IF-] (1) ($RES_SIM_20)
(2)       [----] if time > 1.0 then
(2)       [----]   [SCAL] (1) a.x = 5.0 ($RES_SIM_21)
(2)       [----] elseif time > 2.0 then
(2)       [----]   [SCAL] (1) a.x = 11.0 ($RES_SIM_22)
(2)       [----] else
(2)       [----]   [SCAL] (1) a.x = 14.0 ($RES_SIM_23)
(2)       [----] end if;
(3)       [-IF-] (1) ($RES_SIM_16)
(3)       [----] if time > 1.0 then
(3)       [----]   [SCAL] (1) b.x = 3.0 ($RES_SIM_17)
(3)       [----] elseif time > 2.0 then
(3)       [----]   [SCAL] (1) b.x = 9.0 ($RES_SIM_18)
(3)       [----] else
(3)       [----]   [SCAL] (1) b.x = 16.0 ($RES_SIM_19)
(3)       [----] end if;
(4)       [-IF-] (1) ($RES_SIM_12)
(4)       [----] if time > 1.0 then
(4)       [----]   [SCAL] (1) a.y = 1.0 ($RES_SIM_13)
(4)       [----] elseif time > 2.0 then
(4)       [----]   [SCAL] (1) a.y = 12.0 ($RES_SIM_14)
(4)       [----] else
(4)       [----]   [SCAL] (1) a.y = 15.0 ($RES_SIM_15)
(4)       [----] end if;
(5)       [-IF-] (1) ($RES_SIM_8)
(5)       [----] if time > 1.0 then
(5)       [----]   [SCAL] (1) b.y = 6.0 ($RES_SIM_9)
(5)       [----] elseif time > 2.0 then
(5)       [----]   [SCAL] (1) b.y = 8.0 ($RES_SIM_10)
(5)       [----] else
(5)       [----]   [SCAL] (1) b.y = 17.0 ($RES_SIM_11)
(5)       [----] end if;
(6)       [-IF-] (1) ($RES_SIM_4)
(6)       [----] if time > 1.0 then
(6)       [----]   [SCAL] (1) der(s) = 4.0 ($RES_SIM_5)
(6)       [----] elseif time > 2.0 then
(6)       [----]   [SCAL] (1) der(s) = 7.0 ($RES_SIM_6)
(6)       [----] else
(6)       [----]   [SCAL] (1) der(s) = 18.0 ($RES_SIM_7)
(6)       [----] end if;
(7)       [-IF-] (1) ($RES_SIM_0)
(7)       [----] if time > 1.0 then
(7)       [----]   [SCAL] (1) w + z = 2.0 ($RES_SIM_1)
(7)       [----] elseif time > 2.0 then
(7)       [----]   [SCAL] (1) w + z = 10.0 ($RES_SIM_2)
(7)       [----] else
(7)       [----]   [SCAL] (1) w + z = 13.0 ($RES_SIM_3)
(7)       [----] end if;

Discrete Equations (2/2)
**************************
(1)       [-IF-] (1) ($RES_SIM_32)
(1)       [----] if time > 1.0 then
(1)       [----]   [SCAL] (1) c = true ($RES_SIM_33)
(1)       [----] elseif time > 2.0 then
(1)       [----]   [SCAL] (1) c = false ($RES_SIM_34)
(1)       [----] else
(1)       [----]   [SCAL] (1) c = true ($RES_SIM_35)
(1)       [----] end if;
(2)       [-IF-] (1) ($RES_SIM_28)
(2)       [----] if time > 1.0 then
(2)       [----]   [SCAL] (1) n = 1 ($RES_SIM_29)
(2)       [----] elseif time > 2.0 then
(2)       [----]   [SCAL] (1) n = 2 ($RES_SIM_30)
(2)       [----] else
(2)       [----]   [SCAL] (1) n = 3 ($RES_SIM_31)
(2)       [----] end if;

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 左边相同的方程：List.sort 平局时反着排，两条、三条的结果不同。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerIfTie.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(if_tie_is_reversed) {
  const char* src = R"omc(model LowerIfTie
  Real x, y;
equation
  if time > 1 then
    x = 1;
    x = 2;
  else
    x = 3;
    x = 4;
  end if;
  if time > 2 then
    y = 1;
    y = 2;
    y = 3;
  else
    y = 4;
    y = 5;
    y = 6;
  end if;
end LowerIfTie;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 2)
=================================================

Algebraic Variables (2/2)
***************************
(1)       [ALGB] (1) Real x
(2)       [ALGB] (1) Real y

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 5)
=============================================================

Continuous Equations (5/5)
****************************
(1)       [-IF-] (1) ($RES_SIM_12)
(1)       [----] if time > 1.0 then
(1)       [----]   [SCAL] (1) x = 1.0 ($RES_SIM_13)
(1)       [----] else
(1)       [----]   [SCAL] (1) x = 3.0 ($RES_SIM_14)
(1)       [----] end if;
(2)       [-IF-] (1) ($RES_SIM_9)
(2)       [----] if time > 1.0 then
(2)       [----]   [SCAL] (1) x = 2.0 ($RES_SIM_10)
(2)       [----] else
(2)       [----]   [SCAL] (1) x = 4.0 ($RES_SIM_11)
(2)       [----] end if;
(3)       [-IF-] (1) ($RES_SIM_6)
(3)       [----] if time > 2.0 then
(3)       [----]   [SCAL] (1) y = 1.0 ($RES_SIM_7)
(3)       [----] else
(3)       [----]   [SCAL] (1) y = 4.0 ($RES_SIM_8)
(3)       [----] end if;
(4)       [-IF-] (1) ($RES_SIM_3)
(4)       [----] if time > 2.0 then
(4)       [----]   [SCAL] (1) y = 2.0 ($RES_SIM_4)
(4)       [----] else
(4)       [----]   [SCAL] (1) y = 5.0 ($RES_SIM_5)
(4)       [----] end if;
(5)       [-IF-] (1) ($RES_SIM_0)
(5)       [----] if time > 2.0 then
(5)       [----]   [SCAL] (1) y = 3.0 ($RES_SIM_1)
(5)       [----] else
(5)       [----]   [SCAL] (1) y = 6.0 ($RES_SIM_2)
(5)       [----] end if;

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// if 套 if、if 里的 when（种类跟 when 走）、if 里的 assert 和无返回值调用（一支一条时不比左边，体是 EMPTY，if 算 CONTINUOUS，(0)）；条件和体的式子各 combine 一次。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerIfNested.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(if_nested_when_assert) {
  const char* src = R"omc(model LowerIfNested
  function h
    input Real u;
  algorithm
    assert(u > 0, "h");
  end h;
  Real x, y, z, w;
  Boolean b, c;
equation
  if b then
    if c then
      x = 1;
      y = 2;
    else
      y = 3;
      x = 4;
    end if;
  else
    y = 5;
    x = 6;
  end if;
  if x + y + 1 > 2 then
    when time > 1 then
      z = 1;
    end when;
    w = 1;
  else
    when time > 2 then
      z = x + y + 1;
    end when;
    w = 2;
  end if;
  if b then
    assert(x + y + 1 > 0, "a");
  else
    h(x + y + 1);
  end if;
end LowerIfNested;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 6)
=================================================

Algebraic Variables (3/3)
***************************
(1)       [ALGB] (1) Real x
(2)       [ALGB] (1) Real y
(3)       [ALGB] (1) Real w

Discrete Variables (3/3)
**************************
(1)       [DISC] (1) Real z
(2)       [DISC] (1) Boolean b
(3)       [DISC] (1) Boolean c

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 4)
=============================================================

Continuous Equations (4/3)
****************************
(1)       [-IF-] (1) ($RES_SIM_14)
(1)       [----] if b then
(1)       [----]   [-IF-] (1) ($RES_SIM_15)
(1)       [----]   [----] if c then
(1)       [----]   [----]   [SCAL] (1) x = 1.0 ($RES_SIM_16)
(1)       [----]   [----] else
(1)       [----]   [----]   [SCAL] (1) x = 4.0 ($RES_SIM_17)
(1)       [----]   [----] end if;
(1)       [----] else
(1)       [----]   [SCAL] (1) x = 6.0 ($RES_SIM_18)
(1)       [----] end if;
(2)       [-IF-] (1) ($RES_SIM_9)
(2)       [----] if b then
(2)       [----]   [-IF-] (1) ($RES_SIM_10)
(2)       [----]   [----] if c then
(2)       [----]   [----]   [SCAL] (1) y = 2.0 ($RES_SIM_11)
(2)       [----]   [----] else
(2)       [----]   [----]   [SCAL] (1) y = 3.0 ($RES_SIM_12)
(2)       [----]   [----] end if;
(2)       [----] else
(2)       [----]   [SCAL] (1) y = 5.0 ($RES_SIM_13)
(2)       [----] end if;
(3)       [-IF-] (1) ($RES_SIM_3)
(3)       [----] if y + x + 1.0 > 2.0 then
(3)       [----]   [SCAL] (1) w = 1.0 ($RES_SIM_4)
(3)       [----] else
(3)       [----]   [SCAL] (1) w = 2.0 ($RES_SIM_5)
(3)       [----] end if;
(4)       [-IF-] (0) ($RES_SIM_0)
(4)       [----] if b then
(4)       [----]   [WHEN] (0) ($RES_SIM_1)
(4)       [----]   [----] when y + x + 1.0 > 0.0 then
(4)       [----]   [----]   assert(y + x + 1.0 > 0.0, "a", AssertionLevel.error)
(4)       [----]   [----] end when;
(4)       [----] else
(4)       [----]   [ALGO] (0) ($RES_SIM_2)
(4)       [----]   [----] LowerIfNested.h(y + x + 1.0);

(4)       [----] end if;

Discrete Equations (1/1)
**************************
(1)       [-IF-] (1) ($RES_SIM_6)
(1)       [----] if y + x + 1.0 > 2.0 then
(1)       [----]   [WHEN] (1) ($RES_SIM_7)
(1)       [----]   [----] when time > 1.0 then
(1)       [----]   [----]   z := 1.0
(1)       [----]   [----] end when;
(1)       [----] else
(1)       [----]   [WHEN] (1) ($RES_SIM_8)
(1)       [----]   [----] when time > 2.0 then
(1)       [----]   [----]   z := y + x + 1.0
(1)       [----]   [----] end when;
(1)       [----] end if;

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 初始方程里的 if 可以没有 else（lowerIfEquationBody 的 init 分支）。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerIfInitial.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(if_initial_no_else) {
  const char* src = R"omc(model LowerIfInitial
  Real x, y;
initial equation
  if time > 1 then
    x = 1;
  end if;
  if time > 2 then
    y = 1;
  elseif time > 3 then
    y = 2;
  end if;
equation
  der(x) = 1;
  der(y) = 1;
end LowerIfInitial;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 2)
=================================================

Algebraic Variables (2/2)
***************************
(1)       [ALGB] (1) Real x
(2)       [ALGB] (1) Real y

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 2)
=============================================================

Continuous Equations (2/2)
****************************
(1)       [SCAL] (1) der(x) = 1.0 ($RES_SIM_6)
(2)       [SCAL] (1) der(y) = 1.0 ($RES_SIM_5)

(Exclusively) Initial Equations (2/2)
***************************************
(1)       [-IF-] (1) ($RES_SIM_3)
(1)       [----] if time > 1.0 then
(1)       [----]   [SCAL] (1) x = 1.0 ($RES_SIM_4)
(1)       [----] end if;
(2)       [-IF-] (1) ($RES_SIM_0)
(2)       [----] if time > 2.0 then
(2)       [----]   [SCAL] (1) y = 1.0 ($RES_SIM_1)
(2)       [----] elseif time > 3.0 then
(2)       [----]   [SCAL] (1) y = 2.0 ($RES_SIM_2)
(2)       [----] end if;

)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 语句打印：多行语句前后空一行，else 打 `elseif true then`，while 打 `then`，break 打 `#UNKNOWN STATEMENT#`，assert 补默认级别，元组的空位打 `_`；每个顶层式 combine 一次；消息照常化简。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerAlgStmts.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(algorithm_statements) {
  const char* src = R"omc(model LowerAlgStmts
  function g
    input Real u;
    output Real a;
    output Real b;
  algorithm
    a := u;
    b := u;
  end g;
  function h
    input Real u;
  algorithm
    assert(u > 0, "h");
  end h;
  Real x, y, z;
  Integer n;
algorithm
  x := y + z + 1;
  (x, y) := g(time);
  (, y) := g(time + 1 + 2);
  if x + y + 1 > 2 then
    z := x + y + 1;
  elseif n > 1 then
    z := 1;
  else
    z := 2;
  end if;
  assert(x + y + 1 > 0, "a" + "b");
  assert(x > 0, "w", AssertionLevel.warning);
  h(x + y + 1);
  while n < 3 loop
    n := n + 1;
    if n > 2 then
      break;
    end if;
  end while;
  for i in 1:n + 1 loop
    x := x + i + 1;
    for r in 1.0:0.5:2.0 loop
      y := r;
    end for;
    for j in {1, 3} loop
      y := j;
    end for;
  end for;
  terminate("t");
end LowerAlgStmts;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 4)
=================================================

Algebraic Variables (3/3)
***************************
(1)       [ALGB] (1) Real x
(2)       [ALGB] (1) Real y
(3)       [ALGB] (1) Real z

Discrete Variables (1/1)
**************************
(1)       [DISC] (1) Integer n

Artificial Variables (5/4)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time
(3)       [ITER] (1) Integer j
(4)       [ITER] (1) Integer i
(5)       [ITER] (1) Real r


=============================================================
  Equation Data Simulation (scalar simulation equations: 4)
=============================================================

Discrete Equations (1/4)
**************************
(1)       [ALGO] (4) ($RES_SIM_0)
(1)       [----] x := z + y + 1.0;
(1)       [----] (x, y) := LowerAlgStmts.g(time);
(1)       [----] (_, y) := LowerAlgStmts.g(1.0 + time + 2.0);

(1)       [----] if y + x + 1.0 > 2.0 then
(1)       [----]   z := y + x + 1.0;
(1)       [----] elseif n > 1 then
(1)       [----]   z := 1.0;
(1)       [----] elseif true then
(1)       [----]   z := 2.0;
(1)       [----] end if;

(1)       [----] assert(y + x + 1.0 > 0.0, "ab", AssertionLevel.error);
(1)       [----] assert(x > 0.0, "w", AssertionLevel.warning);
(1)       [----] LowerAlgStmts.h(y + x + 1.0);

(1)       [----] while n < 3 then
(1)       [----]   n := n + 1;

(1)       [----]   if n > 2 then
(1)       [----]     #UNKNOWN STATEMENT#;
(1)       [----]   end if;
(1)       [----] end while;

(1)       [----] for i in 1:n + 1 loop
(1)       [----]   x := i + x + 1.0;

(1)       [----]   for r in 1.0:0.5:2.0 loop
(1)       [----]     y := r;
(1)       [----]   end for;

(1)       [----]   for j in {1, 3} loop
(1)       [----]     y := j;
(1)       [----]   end for;
(1)       [----] end for;

(1)       [----] terminate("t");


)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// when 语句里赋值的 Real 是 [DISC]（updateImplicitVariabilityStmt），但入桶看类型：全 Real 的算法进 Continuous；有离散类型的进 Discrete；`{a, b}` 条件照写。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerAlgWhen.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(algorithm_when_statement) {
  const char* src = R"omc(model LowerAlgWhen
  Real y, z;
  Integer n;
  Boolean b;
  Real r;
algorithm
  when {b, time > 1} then
    y := 1;
  elsewhen time > 2 then
    y := 2;
    z := 3;
  end when;
algorithm
  when time > 3 then
    n := 1;
  end when;
  r := 1;
end LowerAlgWhen;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 5)
=================================================

Algebraic Variables (1/1)
***************************
(1)       [ALGB] (1) Real r

Discrete Variables (4/4)
**************************
(1)       [DISC] (1) Real y
(2)       [DISC] (1) Real z
(3)       [DISC] (1) Integer n
(4)       [DISC] (1) Boolean b

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 4)
=============================================================

Continuous Equations (1/2)
****************************
(1)       [ALGO] (2) ($RES_SIM_1)
(1)       [----] when {b, time > 1.0} then
(1)       [----]   y := 1.0;
(1)       [----] elsewhen time > 2.0 then
(1)       [----]   y := 2.0;
(1)       [----]   z := 3.0;
(1)       [----] end when;


Discrete Equations (1/2)
**************************
(1)       [ALGO] (2) ($RES_SIM_0)
(1)       [----] when time > 3.0 then
(1)       [----]   n := 1;
(1)       [----] end when;

(1)       [----] r := 1.0;


)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 输出是压平时定的，化简删掉的语句里赋值的变量照算（size、入桶）；重复赋值只算一个；没有输出进 Removed；初始算法进 Initial；编号：方程、算法、初始方程、初始算法。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerAlgOutputs.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(algorithm_outputs) {
  const char* src = R"omc(model LowerAlgOutputs
  function h
    input Real u;
  algorithm
    assert(u > 0, "h");
  end h;
  Real x, y, z, s(start = 1);
  Integer n, m;
initial equation
  x = 1;
initial algorithm
  z := 1;
  z := 2;
equation
  der(s) = 1;
algorithm
  x := 1;
  if false then
    y := 1;
    n := 2;
  end if;
  for i in 3:1 loop
    m := i;
  end for;
algorithm
  y := 2;
  if true then
    z := 1;
  else
    s := 2;
  end if;
algorithm
  assert(y > 0, "m");
  h(y);
end LowerAlgOutputs;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 6)
=================================================

Algebraic Variables (4/4)
***************************
(1)       [ALGB] (1) Real x
(2)       [ALGB] (1) Real y
(3)       [ALGB] (1) Real z
(4)       [ALGB] (1) Real s (start = 1.0)

Discrete Variables (2/2)
**************************
(1)       [DISC] (1) Integer n
(2)       [DISC] (1) Integer m

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 8)
=============================================================

Continuous Equations (2/4)
****************************
(1)       [SCAL] (1) der(s) = 1.0 ($RES_SIM_5)
(2)       [ALGO] (3) ($RES_SIM_3)
(2)       [----] y := 2.0;
(2)       [----] z := 1.0;


Discrete Equations (1/4)
**************************
(1)       [ALGO] (4) ($RES_SIM_4)
(1)       [----] x := 1.0;


(Exclusively) Initial Equations (2/2)
***************************************
(1)       [SCAL] (1) x = 1.0 ($RES_SIM_1)
(2)       [ALGO] (1) ($RES_SIM_0)
(2)       [----] z := 1.0;
(2)       [----] z := 2.0;


Removed Equations (1/0)
*************************
(1)       [ALGO] (0) ($RES_SIM_2)
(1)       [----] assert(y > 0.0, "m", AssertionLevel.error);
(1)       [----] LowerAlgOutputs.h(y);


)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 用到的迭代量进 Artificial，[ITER]，序是集合的桶序；没用到的不进；和变量同名的不进；范围里的也算。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerAlgIter.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(algorithm_iterators) {
  const char* src = R"omc(model LowerAlgIter
  Real x, y;
  parameter Integer n = 3;
algorithm
  for alpha in 1:2 loop
    x := alpha;
  end for;
  for unused in 1:2 loop
    x := 1;
  end for;
  for x in 1:2 loop
    y := x;
  end for;
  for beta in 1:n loop
    for gamma in 1:beta loop
      y := 1;
    end for;
  end for;
  for r in 1.0:2.0 loop
    y := r;
  end for;
end LowerAlgIter;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 2)
=================================================

Algebraic Variables (2/2)
***************************
(1)       [ALGB] (1) Real x
(2)       [ALGB] (1) Real y

Parameter Variables (1/1)
***************************
(1)       [PRMT] (1) final parameter Integer n = 3 (fixed = true)

Artificial Variables (5/4)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time
(3)       [ITER] (1) Integer beta
(4)       [ITER] (1) Integer alpha
(5)       [ITER] (1) Real r


=============================================================
  Equation Data Simulation (scalar simulation equations: 2)
=============================================================

Continuous Equations (1/2)
****************************
(1)       [ALGO] (2) ($RES_SIM_0)
(1)       [----] for alpha in 1:2 loop
(1)       [----]   x := alpha;
(1)       [----] end for;

(1)       [----] for unused in 1:2 loop
(1)       [----]   x := 1.0;
(1)       [----] end for;

(1)       [----] for x in 1:2 loop
(1)       [----]   y := x;
(1)       [----] end for;

(1)       [----] for beta in 1:3 loop
(1)       [----]   for gamma in 1:beta loop
(1)       [----]     y := 1.0;
(1)       [----]   end for;
(1)       [----] end for;

(1)       [----] for r in 1.0:2.0 loop
(1)       [----]   y := r;
(1)       [----] end for;


)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 顶层无返回值调用是 [ALGO] (0)，进 Removed；初始方程里的进 Initial；实参 combine 一次；有输出的函数也一样；when 里的被 split 丢弃。
// 期望值：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo LowerNoRet.mo` 的 (After Lowering) 段，omc 1.25.0 实跑。
YMC_TEST(no_return_call) {
  const char* src = R"omc(model LowerNoRet
  function f
    input Real u;
    output Real v;
  algorithm
    v := u;
  end f;
  function h
    input Real u;
  algorithm
    assert(u > 0, "h");
  end h;
  Real x, y;
initial equation
  h(x + y + 1);
equation
  x = 1;
  h(time);
  y = 2;
  f(x + y + 1);
  when time > 1 then
    h(x);
  end when;
end LowerNoRet;
)omc";
  const char* omc = R"omc(####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 2)
=================================================

Algebraic Variables (2/2)
***************************
(1)       [ALGB] (1) Real x
(2)       [ALGB] (1) Real y

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 2)
=============================================================

Continuous Equations (2/2)
****************************
(1)       [SCAL] (1) x = 1.0 ($RES_SIM_4)
(2)       [SCAL] (1) y = 2.0 ($RES_SIM_2)

(Exclusively) Initial Equations (1/0)
***************************************
(1)       [ALGO] (0) ($RES_SIM_0)
(1)       [----] LowerNoRet.h(y + x + 1.0);


Removed Equations (2/0)
*************************
(1)       [ALGO] (0) ($RES_SIM_3)
(1)       [----] LowerNoRet.h(time);

(2)       [ALGO] (0) ($RES_SIM_1)
(2)       [----] LowerNoRet.f(y + x + 1.0);


)omc";
  YMC_CHECK_EQ(lower_src(src), std::string(omc));
}

// 同一棵 a + b + c + d：浅映射只收一次 → c + b + a + d；深映射逐层收 → c + a + b + d（NFSimplifyExp.combineBinaries 与
// Expression.map 的组合，手推见 combine_binaries.cpp 头注；两个结果 omc 1.25.0 探针 LwaC02n 的方程 / 绑定行实跑一致）。
YMC_TEST(combine_shallow_vs_deep) {
  Interner in;
  ExprArena a;
  const Id<Expr> x = cref(a, in.intern("a")), y = cref(a, in.intern("b")), z = cref(a, in.intern("c")), w = cref(a, in.intern("d"));
  const Id<Expr> sum = binary(a, BinOp::Add, binary(a, BinOp::Add, binary(a, BinOp::Add, x, y), z), w);
  const std::size_t before = a.exprs.size();
  YMC_CHECK_EQ(bdae::exp_string(a, in, bdae::combine_binaries(a, sum)), std::string("c + b + a + d"));
  YMC_CHECK_EQ(bdae::exp_string(a, in, bdae::combine_binaries_deep(a, sum)), std::string("c + a + b + d"));
  YMC_CHECK(a.exprs.size() > before);                       // 只加新节点
  YMC_CHECK(a[sum].kind == ExprKind::Binary);               // 纯函数：原节点不动
}

// String 的 `+` 是拼接，不可交换（Operator.isCommutative 先看类型）：不成 Multary，次序不变。
YMC_TEST(combine_string_add_is_not_commutative) {
  Interner in;
  ExprArena a;
  const Id<Expr> x = cref(a, in.intern("a")), y = cref(a, in.intern("b"));
  const Id<Expr> cat = binary(a, BinOp::Add, x, y, TypeKind::String);
  YMC_CHECK(bdae::combine_binaries(a, cat) == cat);
}

// 没做的构造不打印：退回原因（driver 据此退 66）。
YMC_TEST(lower_stops_on_unsupported) {
  // omc 在 lower 里报内部错（实跑 omc 1.25.0：lowerIfEquationBody 的 missing else、getLHS、getType、splitCollect / List.zip），ymc 不出文本。
  YMC_CHECK_EQ(lower_src("model M Real x; equation if time > 1 then assert(x > 1, \"m\"); end if; end M;"), std::string("unsupported: if equation without else"));
  YMC_CHECK_EQ(lower_src("model M Real x; equation if time > 1 then x = 1; assert(x > 1, \"m\"); else x = 2; assert(x > 2, \"n\"); end if; end M;"),
               std::string("unsupported: if branch equation without a unique left side"));
  YMC_CHECK_EQ(lower_src("model M Real x, y; equation when time > 1 then x = 1; x = 2; y = 3; end when; end M;"), std::string("unsupported: when branch assigns a variable twice"));
  // 默认路径展开了，NB 路径留 `E`（实跑 omc 1.25.0），分不清；空分支默认路径删了，NB 路径留着。
  YMC_CHECK_EQ(lower_src("model M type E = enumeration(a, b); Real x; algorithm for e in E loop x := 1; end for; end M;"), std::string("unsupported: for range"));
  YMC_CHECK_EQ(lower_src("model M Real x; algorithm if time > 1 then x := 1; elseif time > 2 then end if; end M;"), std::string("unsupported: if statement branch emptied by simplification"));
  // omc 在 lower 里报内部错的三种，ymc 不出文本（实跑 omc 1.25.0：lowerWhenBranchStatement、lowerEquation 的 TERMINATE 支）。
  YMC_CHECK_EQ(lower_src("model M Real x; equation when time > 1 then assert(x > 1, \"m\"); end when; end M;"), std::string("unsupported: assert in when"));
  YMC_CHECK_EQ(lower_src("model M Real x, y; equation when time > 1 then if x > 1 then y = 1; else y = 2; end if; end when; end M;"), std::string("unsupported: if in when"));
  YMC_CHECK_EQ(lower_src("model M equation terminate(\"a\"); end M;"), std::string("unsupported: terminate outside when"));
  // 没选中的分支里的 when 也把 y 标成隐式离散（实跑 omc 1.25.0：`[DISC] (1) Real y`），ymc 没收这条方程，不猜。
  YMC_CHECK_EQ(lower_src("model M parameter Boolean p = false; Real y; equation if p then when time > 1 then y = 1; end when; else y = 2; end if; end M;"),
               std::string("unsupported: when equation in a dropped branch or removed component"));
  YMC_CHECK_EQ(lower_src("model M Real x[2]; end M;"), std::string("unsupported: array variable"));
  YMC_CHECK_EQ(lower_src("model M record R Real x; end R; R r; end M;"), std::string("unsupported: record variable"));
}

YMC_TEST_MAIN
