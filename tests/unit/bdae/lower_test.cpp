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
  YMC_CHECK_EQ(lower_src("model M Real x; algorithm x := 1; end M;"), std::string("unsupported: algorithm section"));
  YMC_CHECK_EQ(lower_src("model M Real x, y; equation when time > 1 then x = 1; y = 2; end when; end M;"), std::string("unsupported: multi-assign when"));
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
