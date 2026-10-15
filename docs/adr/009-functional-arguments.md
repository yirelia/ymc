# ADR-009 函数型实参：BOX / UNBOX / 部分函数应用 / 函数引用进 flat IR

- 日期：2026-10-15
- 状态：已采纳（主 agent 决定，镜像 OMC 的 BOX/UNBOX/PARTIAL_FUNCTION_APPLICATION；用户可复议）
- 依据：OMC **v1.25.0 tag** 源码（`git -C C:/dev/OpenModelica show v1.25.0:OMCompiler/Compiler/<path>`，下文行号均为该 tag）；
  安装版 omc 1.25.0 实跑探针 `tests/probes/pk-p24.probes`（PKZ1–PKZ25）；MLS §12.4.2（函数型形参与部分函数应用）。

## 背景
MSL 用 `Modelica.Math.Nonlinear.solveOneNonlinearEquation(function f_nonlinear(p = p, h = h, X = X[1:nXi]), 190, 647)` 求反函数
（Media MoistAir.T_phX、ReferenceMoistAir 的 Tsub / T_phX、IdealGases 的 T_h / T_ps、Incompressible 的 T_ph …）。7 个 MSL 示例
（Media MoistAir、ReferenceAir.MoistAir / MoistAir2、Fluid TraceSubstances.RoomCO2 / RoomCO2WithControls、BranchingDynamicPipes、
SolveOneNonlinearEquation.Inverse_sine）卡在这里：ymc 把部分函数应用当普通调用，`f_nonlinear` 继承来的输入 `u` 没实参也没缺省
→ SKIP "missing argument / default value"。omc 1.25.0 的压平输出（DAE dump）有三种 ymc 的 flat IR 表达不了的形态：
- 被调函数的函数型输入：`input f<function>(#Real u) => #Real f "Function y = f(u); …";`
- 体里经它的调用：`fa := unbox(f(#(u_min)));`
- 调用处：`function Modelica.Media.Examples.MoistAir.Medium.T_phX.f_nonlinear(#(p), #(h), #(X[1:1]))`

## 决定
照 OMC 的表示加 IR，不用"名字叫 `#` 的 Call"之类的变通（那样会被按名找函数的各处——prune、常量求值、记录构造——误认）。
1. **表达式**（`src/inst/expr.h`）：ExprKind 加五种——`Box`（`#(a)`）、`Unbox`（`unbox(a)`）、`PartialApp`（`function F(#(a), …)`：
   call_name = F 的压平名，args = 已装箱的绑定实参、按 F 的输入槽序）、`FnRef`（函数名当值，打压平名）、`FnParamCall`（经函数型
   输入 f 的调用 `f(#(x))`，名字是形参不是函数，所以与 Call 分开）。打印照 ExpressionDumpTpl.tpl（PARTEVALFUNCTION :60-63、
   BOX :148-149、UNBOX :150-151），都不加括号（旧 Expression.priority 落 else 0，Expression.mo:10842-10866）。
2. **函数型形参**（`src/flat/flat.h` 的 FlatFuncParam）：`ty.kind == Function` 时带函数类型（`fn_inputs` / `fn_outputs`：名、未装箱
   类型、缺省），flat_dump 打 `<形参名><function>(#T a[ := 缺省], …) => #R`（DAEDumpTpl.dumpFunctionType :373-382、dumpFuncArg
   :384-394；路径 = 形参名，NFFunction.lookupFunction :318-345 找到的是组件、不加前缀；partial 函数的形参一律装箱，
   NFFunction.typeFunctionSignature :1598-1600 → boxFunctionParameter :1684-1692）。
3. **压平**（`src/flat/flatten.cpp`）：
   - 函数型输入 `input PSF f`（PSF 是 partial function，NFTyping.typeClassType :278-287 的 FUNCTIONAL_PARAMETER）：emit_function
     打函数类型。不是 partial → omc 报 META_FUNCTION_TYPE_NO_PARTIAL_PREFIX（NFInst.checkPartialComponent :2104-2127）→ SKIP；
     体里没被引用 → omc 实例化内部错（PKZ11）→ SKIP。
   - 经它的调用 → `unbox(f(#(x)))`：实参照形参类型匹配后装箱（TypeCheck.matchTypes_cast 的 (_, METABOXED) 支，NFTypeCheck.mo
     :2598-2604），结果按目标拆箱（(METABOXED, _) 支 :2590-2596）；缺的实参补函数类型里的缺省（PKZ4 `f(#(v), #(2))`）。
   - 部分应用：命名实参入槽、按槽序装箱（NFFunction.typePartialApplication :1694-1738、makePartialApplicationFromSlots
     :1740-1785；PKZ2 `g(b = b, a = a)` → `function PZ2.g(#(a), #(b))`），没给的槽成为结果函数类型的输入；没有实参的
     `function F()` 就是函数名（NFInst.instPartEvalFunction :3087-3109）。绑定实参不化简（NFSimplifyExp.simplify :78-124 没有
     PARTIAL_FUNCTION_APPLICATION 支）。
   - 函数值（部分应用 / 函数名 / 转传的函数型输入）只在函数型输入的实参位置定型：try_emit_user_call 把这些实参推到
     emit_function 之后，按 TypeCheck.matchFunctionTypes / matchFunctionParameters（NFTypeCheck.mo:2080-2169）比函数类型，不符
     （PKZ14 / PKZ20，omc 报 Type mismatch）→ SKIP；别处遇到部分应用 → SKIP。
   - 部分应用 / 函数引用引用的函数参与 prune（NFFlatten.collectExpFuncs_traverse :2878-2896）；新种类在化简、相等、运行时判定、
     打印里显式处理；常量求值（eval_user_call）对部分应用一律求不出（没有闭包）。

## 未做（照旧 SKIP，各有探针）
- omc 经 Ceval 求值的常量调用：PKZ12 `parameter Real T0 = PZ12.T_ph(2, 3)` → `834.0`（Ceval 能解释函数值）；ymc 的解释器没有
  函数值，留下的全字面量调用由 "constant user call not evaluated" 拦成 SKIP。
- 包常量记录作部分应用实参：PKZ23（MSL IdealGases.T_h 的 `data = data`），omc 把装箱分进记录构造的各字段
  `PZ23.DataRecord(#(0.018), #(461.5))`，未复刻。
- 部分应用实参里还要化简 / 展开的形态（PKZ17 `#(q * 2.0)`）、展开的 for 方程 / 推导式里的部分应用、函数变量绑定里的函数值；
  函数型的输出 / 局部 / 数组、多输出的函数类型、语句位置经函数型输入的调用、向量化。

## 后果
- MSL：Inverse_sine 转 PASS；其余 6 个越过了部分应用，卡在后面别的缺口（SKIP 原因：MoistAir "qualified/cross-scope reference
  [Medium.dryair.MM]"、Fluid 三个 "stream connector"、ReferenceAir 两个 "record binding in function"——`Ice09_Utilities.Basic.Gibbs`
  的局部 `t`）。
- 新的表达式种类只在两处产生（函数型输入的实参位置、函数算法段里经函数型输入的调用），其余遍历按通用子式处理或显式拒。
- 以后要重看：做 Ceval 的函数值（闭包，PKZ12 一类）、记录常量实参的装箱分配（PKZ23）时。
