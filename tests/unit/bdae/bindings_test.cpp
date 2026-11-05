// Bindings 模块端到端：源码 → 压平 → lower → bindings → dump，逐字对照 omc 1.25.0 的 `BackendDAE: (Bindings)` 段。
// 期望值都是 omc 实跑的输出，命令统一是 `omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo <模型>.mo`，不是 ymc 的输出。
#include "ymc_test.h"

#include <string>
#include <vector>

#include "absyn/absyn.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "bdae/bdae_dump.h"
#include "bdae/bindings.h"
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
std::string bindings_src(const char* src) {
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
  const bdae::ModuleResult r = bdae::bindings(lr.dae, in);
  if (r.unsupported) return "unsupported: " + r.unsupported_reason;
  return bdae::dump_stage(lr.dae, in, "Bindings");
}

// 只看方程段：变量段和 (After Lowering) 一样，lower_test 比过了。
std::string eq_data(const char* src) {
  const std::string text = bindings_src(src);
  const std::size_t at = text.find("Equation Data");
  return at == std::string::npos ? text : text.substr(text.find('\n', text.find('\n', at) + 1) + 1);
}

}  // namespace

// 编号接 $RES_SIM，连续、离散共用一个序列；新方程按 unknowns 的反序接在桶尾；只看 unknowns：顶层 input 和 stateSelect = always 的状态带绑定也不造方程；
// 方程的 rhs 是绑定再化简一遍（`p + 2.0 * time + 3.0` → `3.0 + p + 2.0 * time`），变量行的绑定不变。
YMC_TEST(order_and_buckets) {
  const char* src = R"omc(model BndOrder
  parameter Real p = 2;
  Real x(start = 1) = 2 * time + p + 3;
  Integer n = 3 + 4;
  Boolean b = time > 0.5;
  String s = "a";
  Real y;
  Real z = 1;
  input Real u = 5;
  Real st(stateSelect = StateSelect.always) = time;
equation
  y = x + 1;
end BndOrder;
)omc";
  const char* omc = R"omc(##############################

    BackendDAE: (Bindings)

##############################

=================================================
  Variable Data Simulation (scalar unknowns: 6)
=================================================

State Variables (1/1)
***********************
(1)       [STAT] (1) Real st = time (StateSelect = always)

Algebraic Variables (3/3)
***************************
(1)       [ALGB] (1) Real x = p + 2.0 * time + 3.0 (start = 1.0)
(2)       [ALGB] (1) Real y
(3)       [ALGB] (1) Real z = 1.0

Discrete Variables (3/3)
**************************
(1)       [DISC] (1) Integer n = 7
(2)       [DISC] (1) Boolean b = time > 0.5
(3)       [DISC] (1) String s = "a"

Top Level Input Variables (1/1)
*********************************
(1)       [ALGB] (1) input Real u = 5.0

Parameter Variables (1/1)
***************************
(1)       [PRMT] (1) parameter Real p = 2.0 (fixed = true)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 6)
=============================================================

Continuous Equations (3/3)
****************************
(1)       [SCAL] (1) y = x + 1.0 ($RES_SIM_0)
(2)       [SCAL] (1) z = 1.0 ($RES_BND_5)
(3)       [SCAL] (1) x = 3.0 + p + 2.0 * time ($RES_BND_1)

Discrete Equations (3/3)
**************************
(1)       [SCAL] (1) s = "a" ($RES_BND_4)
(2)       [SCAL] (1) b = time > 0.5 ($RES_BND_3)
(3)       [SCAL] (1) n = 7 ($RES_BND_2)

)omc";
  YMC_CHECK_EQ(bindings_src(src), std::string(omc));
}

// if 方程占了 7 个编号（外层 + 每支一条体），$RES_BND 从 7 接着编。
YMC_TEST(numbering_after_if_and_when) {
  const char* src = R"omc(model BndIf
  Real a = 1;
  Real b, c, d;
  Integer n = 2;
equation
  if time > 1 then
    b = 1;
    c = 2;
  else
    b = 2;
    c = 3;
  end if;
  when time > 2 then
    d = 1;
  end when;
end BndIf;
)omc";
  const char* omc = R"omc(Continuous Equations (3/3)
****************************
(1)       [-IF-] (1) ($RES_SIM_4)
(1)       [----] if time > 1.0 then
(1)       [----]   [SCAL] (1) b = 1.0 ($RES_SIM_5)
(1)       [----] else
(1)       [----]   [SCAL] (1) b = 2.0 ($RES_SIM_6)
(1)       [----] end if;
(2)       [-IF-] (1) ($RES_SIM_1)
(2)       [----] if time > 1.0 then
(2)       [----]   [SCAL] (1) c = 2.0 ($RES_SIM_2)
(2)       [----] else
(2)       [----]   [SCAL] (1) c = 3.0 ($RES_SIM_3)
(2)       [----] end if;
(3)       [SCAL] (1) a = 1.0 ($RES_BND_7)

Discrete Equations (2/2)
**************************
(1)       [WHEN] (1) ($RES_SIM_0)
(1)       [----] when time > 2.0 then
(1)       [----]   d := 1.0
(1)       [----] end when;
(2)       [SCAL] (1) n = 2 ($RES_BND_8)

)omc";
  YMC_CHECK_EQ(eq_data(src), std::string("\n") + omc);
}

// 过定也照造：when 和算法赋值的量带绑定，omc 在这一段打出来，后面才崩（实跑 exit 139）。a 是隐式离散，进 Discrete 桶。
YMC_TEST(assigned_variable_keeps_its_binding) {
  const char* src = R"omc(model BndOver
  Real a = 1;
  Integer n = 1;
algorithm
  n := 2;
equation
  when time > 1 then
    a = 2;
  end when;
end BndOver;
)omc";
  const char* omc = R"omc(
Discrete Equations (4/4)
**************************
(1)       [WHEN] (1) ($RES_SIM_1)
(1)       [----] when time > 1.0 then
(1)       [----]   a := 2.0
(1)       [----] end when;
(2)       [ALGO] (1) ($RES_SIM_0)
(2)       [----] n := 2;

(3)       [SCAL] (1) n = 1 ($RES_BND_3)
(4)       [SCAL] (1) a = 1.0 ($RES_BND_2)

)omc";
  YMC_CHECK_EQ(eq_data(src), std::string(omc));
}

// simplifyMultary 的各支：常数并到表头、负常数打括号、全逆项打 `1/`、负项挪到逆项、积里偶数个负号抵消、奇数个提到外面、常数积为 1 时消掉。
// simplifyIf：两支化简后相同取其一，`then true else false` 取条件。
YMC_TEST(simplify_shapes) {
  const char* src = R"omc(model BndSimp
  Real y = time;
  Real w = 2 * time;
  Real a = 1 / y;
  Real b = -1 - y;
  Real c = y - 1 - 2;
  Real d = (-y) * (-w) * (-y);
  Real e = 2 / (y * w);
  Real f = if y > 1 then 1 + y + 2 else y + 3;
  Real g = y * 0.5 * 2 * w;
  Real h = -(y + 1 + 2);
  Integer n = integer(time);
  Real i = -n + y;
  Real j = y + (-n);
  Boolean k = if y > 1 then true else false;
end BndSimp;
)omc";
  const char* omc = R"omc(
Continuous Equations (12/12)
******************************
(1)       [SCAL] (1) j = y - n ($RES_BND_12)
(2)       [SCAL] (1) i = y - n ($RES_BND_11)
(3)       [SCAL] (1) h = -(3.0 + y) ($RES_BND_9)
(4)       [SCAL] (1) g = y * w ($RES_BND_8)
(5)       [SCAL] (1) f = 3.0 + y ($RES_BND_7)
(6)       [SCAL] (1) e = 2.0 / (w * y) ($RES_BND_6)
(7)       [SCAL] (1) d = -w * y * y ($RES_BND_5)
(8)       [SCAL] (1) c = (-3.0) + y ($RES_BND_4)
(9)       [SCAL] (1) b = -(1.0 + y) ($RES_BND_3)
(10)      [SCAL] (1) a = 1/y ($RES_BND_2)
(11)      [SCAL] (1) w = 2.0 * time ($RES_BND_1)
(12)      [SCAL] (1) y = time ($RES_BND_0)

Discrete Equations (2/2)
**************************
(1)       [SCAL] (1) k = y > 1.0 ($RES_BND_13)
(2)       [SCAL] (1) n = integer(time) ($RES_BND_10)

)omc";
  YMC_CHECK_EQ(eq_data(src), std::string(omc));
}

// 常数并出来的 NaN 是 `0.0 / 0.0` 这个式子（makeNaN），不再并；溢出的 Real 打 inf / -inf（realString），负的进和式要括号。
YMC_TEST(nan_and_inf) {
  const char* src = R"omc(model BndNan
  Real y = time;
  Real a = 0 / 0;
  Real b = 0.0 / 0.0 + y;
  Real c = 0 * y / 0;
  Real d = y * (0 / 0);
  Real e = 1e308 * 10;
  Real f = -1e308 * 10;
  Real g = y + 1e308 * 10;
  Real h = y - 1e308 - 1e308;
  Real i = -y * 1e308 * 1e308;
  Real k = 1 + 0 / 0 + y;
  Real l = if 0 / 0 > 0 then y else 1 + y + 2;
end BndNan;
)omc";
  const char* omc = R"omc(
Continuous Equations (12/12)
******************************
(1)       [SCAL] (1) l = if 0.0 / 0.0 > 0.0 then y else 3.0 + y ($RES_BND_11)
(2)       [SCAL] (1) k = 1.0 + 0.0 / 0.0 + y ($RES_BND_10)
(3)       [SCAL] (1) i = -inf * y ($RES_BND_9)
(4)       [SCAL] (1) h = (-inf) + y ($RES_BND_8)
(5)       [SCAL] (1) g = inf + y ($RES_BND_7)
(6)       [SCAL] (1) f = -inf ($RES_BND_6)
(7)       [SCAL] (1) e = inf ($RES_BND_5)
(8)       [SCAL] (1) d = (0.0 / 0.0) * y ($RES_BND_4)
(9)       [SCAL] (1) c = 0.0 / 0.0 ($RES_BND_3)
(10)      [SCAL] (1) b = 0.0 / 0.0 + y ($RES_BND_2)
(11)      [SCAL] (1) a = 0.0 / 0.0 ($RES_BND_1)
(12)      [SCAL] (1) y = time ($RES_BND_0)

)omc";
  YMC_CHECK_EQ(eq_data(src), std::string(omc));
}

// 常数并完两个斜率才相等，simplifySemiLinear（NFSimplifyExp.mo:283-303）改写成 BINARY(x, *, m1)，这个 BINARY 不再化简（S5a-2d 放开，
// 原来 SKIP）。期望值是 omc 实跑：`omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo M.mo` 的 (Bindings) 段。
YMC_TEST(semilinear_rewrite) {
  const char* omc = R"omc(##############################

    BackendDAE: (Bindings)

##############################

=================================================
  Variable Data Simulation (scalar unknowns: 3)
=================================================

Algebraic Variables (3/3)
***************************
(1)       [ALGB] (1) Real y = time
(2)       [ALGB] (1) Real w = 2.0 * time
(3)       [ALGB] (1) Real a = semiLinear(y, 2.0 + 1.0 + w, 3.0 + w)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 3)
=============================================================

Continuous Equations (3/3)
****************************
(1)       [SCAL] (1) a = y * (3.0 + w) ($RES_BND_2)
(2)       [SCAL] (1) w = 2.0 * time ($RES_BND_1)
(3)       [SCAL] (1) y = time ($RES_BND_0)

)omc";
  YMC_CHECK_EQ(bindings_src("model M Real y = time; Real w = 2 * time; Real a = semiLinear(y, 1 + w + 2, w + 3); end M;"), std::string(omc));
}

// 没做的形态不打印。
YMC_TEST(bindings_stops_on_unsupported) {
  // 非零常数除以 0：omc 的 Bindings 模块失败，实跑 exit 1，只有 (After Lowering) 段。
  YMC_CHECK_EQ(bindings_src("model M Real a = 1.0 / 0.0; end M;"), std::string("unsupported: simplify: constant division by zero"));
  YMC_CHECK_EQ(bindings_src("model M Real y = time; Real a = (y + 1) / 0.0; end M;"), std::string("unsupported: simplify: constant division by zero"));
  // combineConstantNumbers 在 Real 上算再 realInt，超过 2^53 的值没核实。
  YMC_CHECK_EQ(bindings_src("model M Integer n = integer(time); Integer a = n * 2147483647 * 2147483647; end M;"),
               std::string("unsupported: simplify: integer constant out of the exact Real range"));
}

YMC_TEST_MAIN
