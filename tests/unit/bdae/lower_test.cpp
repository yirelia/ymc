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
  YMC_CHECK_EQ(lower_src("model M Real x; equation when time > 1 then x = 1; end when; end M;"), std::string("unsupported: when equation"));
  YMC_CHECK_EQ(lower_src("model M Real x[2]; end M;"), std::string("unsupported: array variable"));
  YMC_CHECK_EQ(lower_src("model M record R Real x; end R; R r; end M;"), std::string("unsupported: record variable"));
}

YMC_TEST_MAIN
