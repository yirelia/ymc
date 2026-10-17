# MSL 4.0.0 示例模型压平通过率（S3 退出条件，方案 S3 3g）

对拍对象：MSL 4.0.0 里带 `experiment` 注解、非 partial 的全部 514 个模型（清单 `docs/research/msl-examples.json`，由 omc 生成，
命令见 `tools/msl_sweep.py` 文件头）。每个模型两边各压平一次——`ymc --load-model=Modelica --class=<模型>` 对
`omc -i=<模型> <库>/package.mo`（安装版 omc 1.25.0），只比 stdout，逐字节。

复现：`python tools/msl_sweep.py`（omc 的结果缓存在 `build/msl-oracle/`；gcc-debug 版约 8 分钟）。

## 2026-10-06 第一版（加宽 61f）

| 判定 | 个数 |
|---|---|
| PASS | 4 |
| FAIL | 0 |
| ACCEPTS-INVALID | 0 |
| SKIP（ymc 声明未实现） | 510 |
| BOTH-REJECT / CRASH / OMC-TIMEOUT | 0 |

PASS：`Modelica.Blocks.Examples.CompareSincExpSine`、`Modelica.Electrical.Analog.Examples.ChuaCircuit`、
`Modelica.Electrical.Spice3.Examples.CoupledInductors`、`Modelica.Utilities.Examples.WriteRealMatrixToFile`。

| 包 | 模型数 | PASS |
|---|---|---|
| Modelica.Electrical | 194 | 2 |
| Modelica.Clocked | 77 | 0 |
| Modelica.Mechanics | 73 | 0 |
| Modelica.Magnetic | 66 | 0 |
| Modelica.Blocks | 28 | 1 |
| Modelica.Fluid | 23 | 0 |
| Modelica.Media | 21 | 0 |
| Modelica.Thermal | 16 | 0 |
| Modelica.StateGraph | 7 | 0 |
| Modelica.Math | 4 | 0 |
| Modelica.Utilities | 3 | 1 |
| Modelica.ComplexBlocks | 2 | 0 |

SKIP 的首因（每个模型只记第一个撞上的未实现特性；`[…]` 是查不到的名字）：

| 首因 | 个数 | 说明 / 下一步 |
|---|---|---|
| unresolved component type [StateSelect] | 112 | 内建枚举 `StateSelect` 当组件类型（`parameter StateSelect stateSelect = …`），连带 `stateSelect = <参数>` 的结构求值。**下一桶。** |
| extends redeclare/break modifier | 111 | `extends X(redeclare …)`（Fluid / Media / 多相电机的 `replaceable package Medium`、`redeclare model …`） |
| unresolved derived base [Clock] | 64 | `Modelica.Clocked`：同步时钟（方案定在 S8） |
| unresolved component type [Medium.*] | 34 | `replaceable package Medium = …` 当路径前缀（短类别名 + redeclare） |
| extends builtin type | 28 | `type X extends Real`（长形式的类型类） |
| short class restriction mismatch | 23 | `connector RealInput = input MyReal`（MyReal 是 extends Real 的长 type 类）等 |
| composite/unresolved outer | 22 | `outer World world` 这类复合 outer |
| qualified function call / qualified/cross-scope reference | 37 | 跨包的函数调用 / 常量引用的剩余形态 |
| unresolved base class [ExternalObject] | 14 | 外部对象（`Modelica.Utilities.Streams` 的表格文件等） |
| 其余 | 65 | Evaluate 注解、记录构造、枚举类型路径、connect 形状… |

## 2026-10-06 加宽 61g 之后：PASS 4 → 32

内建枚举 StateSelect 当组件类型 + stateSelect 里的结构参数（Rotational / Translational 的 Inertia、Mass、Spring…），以及
库里的短类连接器（Blocks.Interfaces.RealInput / RealOutput 继承来、限定名）能 connect 之后：

| 判定 | 个数 |
|---|---|
| PASS | 32 |
| FAIL / ACCEPTS-INVALID / CRASH | 0 |
| SKIP | 482 |

| 包 | 模型数 | PASS |
|---|---|---|
| Modelica.Mechanics | 73 | 21（Rotational 10、Translational 11） |
| Modelica.Electrical | 194 | 5 |
| Modelica.Blocks | 28 | 5 |
| Modelica.Utilities | 3 | 1 |
| 其余（Clocked、Magnetic、Fluid、Media、Thermal、StateGraph、Math、ComplexBlocks） | 216 | 0 |

SKIP 首因（前 10）：extends redeclare 113、Clock 72、extends 修饰改复合组件（`extends X(flange(…))` 之类）71、extends
内建类型 28、短类受限类型不一致 24、复合 outer 22、Medium 前缀 30、限定名引用 18、ExternalObject 16、Evaluate 注解 13。

## 2026-10-06 加宽 61h 之后：PASS 32 → 49

extends 修饰里的组件重声明（Electrical.Analog.Sources 一族的 `redeclare Modelica.Blocks.Sources.Sine signalSource(…)`）、
连接器里经短类定型的成员不打 final、类型带 absoluteValue = false 时组件注释换成类型的、零取负得正零之后：Electrical 5 → 22
（Analog 的整流、加热、晶体管门电路…），其余不变；FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-06 加宽 61i 之后：PASS 49 → 54

修饰各层带自己的作用域（外层实例修饰叠组件声明修饰、extends 修饰改继承来的复合组件）之后：Electrical.Analog 的 CauerLowPassSC、
DifferenceAmplifier、IdealTriacCircuit、NandGate、SimpleTriacCircuit 通过；FAIL / ACCEPTS-INVALID / CRASH 0。原"extends modifier
on composite"的 103 个（Machines / PowerConverters / FundamentalWave / QuasiStatic）露出的下一层是重声明形态（constrainedby、
条件、维、前缀，78 个），同桶补齐后再往下露出：if 方程条件求不出（28，电机的 useThermalPort 一类）、函数变量类型（15）、短类
受限类型不符（11）、组件修饰里的重声明（8）、ExternalObject（6）等——这一层暂无整模型通过。SKIP 前几类：Clock 72、Evaluate 注解
50、函数变量类型 47、短类受限类型不符 37、ExternalObject 30、if 条件求不出 28、extends 内建类型 28。

## 2026-10-06 加宽 61j 之后：PASS 54 → 61

函数变量的类型是短类链（`input SI.Voltage v`）/ 带属性修饰之后：Blocks.Examples.Noise.Densities、Electrical.Analog 的
AmplifierWithOpAmpDetailed、Mechanics 的两个 EddyCurrentBrake 与三个 LossyGearDemo 通过；FAIL / ACCEPTS-INVALID / CRASH 0。原
"函数变量类型"一类露出的下一层主要是函数体里的数组推导式（`{(k - 1)*2*pi/m for k in 1:m}`，Polyphase.Functions.
symmetricOrientation 等，"expression kind [Reduction]" 34 个）。

## 2026-10-06 加宽 61k / 61l 之后：PASS 61 → 83

61k（函数体里的归约 / 推导式）让 Polyphase / Machines 一族露出下一层（模型语境里嵌在运算中的数组值函数调用，34 个），本身
不增通过数。61l（用户枚举参数的结构求值 + import 来的枚举类型名）一次过了 22 个：Electrical.Analog.Examples.OpAmps 一族 19 个
（`LimiterHomotopy homotopyType … annotation(Evaluate = true)`）、Blocks 的 InverseModel / SlewRateLimiter / SinCosEncoder、
Clocked 的 SimpleControlledDrive.Continuous / MixingUnitWithContinuousControl；FAIL / ACCEPTS-INVALID / CRASH 0（KinematicPTP2 的
`nout = max([size(q_begin, 1); …])` 1.25.0 有标记怪癖，SKIP 入账）。

## 2026-10-06 加宽 61m / 61n 之后：PASS 83（不变）

61m（长类型 extends 内建类型 + 短类带维）让 MultiBody 露出下一层（composite outer 的 World）。61n（结构求值顺参数绑定链 + 两遍
压平处理声明在后的参数 + omc 的不固定检查 / 求值副作用）让电机一族的 if 条件求得出了（`friction.frictionParameters.PRef` ←
`aimc.frictionParameters` ← 声明在最后的 `aimcData`）：原"if condition not evaluable"的 31 个露出下一层——复合数组元素引用
（`plug.pin[m]` 这类子实例里的参数维，22 个）、记录构造调用（9 个）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-06 加宽 61o 之后：PASS 83 → 100

外层包里的枚举类型（Blocks.Nonlinear.Limiter 的 `Types.LimiterHomotopy.Linear`）+ when 里赋值的变量的 pre（Blocks.Discrete.
ZeroOrderHold 的 `y = pre(ySample)`）之后：Blocks.Examples.TotalHarmonicDistortion 与 PowerConverters 的 16 个（ACDC 的晶闸管
整流一族 11 个、DCAC SinglePhaseTwoLevel 两个、DCDC Chopper 三个）通过；FAIL / ACCEPTS-INVALID / CRASH 0。同族剩下的露出：
duplicate variable 2（HBridge）、矩阵构造套 size 的结构绑定 1（PID_Controller，1.25.0 怪癖，在账）、常量数组符号下标后切片 1、
函数调用 1。

## 2026-10-06 加宽 61p 之后：PASS 100（不变）

复合引用穿过子实例里参数维的数组（电机的 `vs[m] = plug_sp.pin.v - plug_sn.pin.v`，plug 声明在后）+ Evaluate 顺向前的记录绑定
（`statorCore.m = coreParameters.m` → 最后才声明的 aimcData）之后，电机一族再往下露出：元素是数组的推导式（SpacePhasor 的
`InverseTransformation[m, 2] = {{…} for k in 1:m}`，"array binding shape/type mismatch" 22 个）、表达式里的数组值函数调用 5、
常量求值 3、算法里的数组元素赋值 2、矩阵元素形状 1。FAIL / ACCEPTS-INVALID / CRASH 0。另：TDM-GCC 构建的超越函数（libmingwex）
与 omc 1.25.0 的 UCRT 末位常差一个 ulp（MSVC 构建一致），推导式放开后会碰到，下一桶先做 CRT 数学函数垫片。

## 2026-10-06 加宽 61q 之后：PASS 100（不变）

超越函数折叠改走 UCRT（gcc 构建此前与 omc 末位常差一个 ulp）+ 元素是数组的推导式（SpacePhasor 的变换矩阵）之后，电机一族
再往下：参数数组绑到常量实参的函数调用（`phi[m] = symmetricOrientation(m)`，omc 求值成字面量；"array binding higher
variability"）、条件组件的条件。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-06 加宽 61r 之后：PASS 100（不变）

参数绑到结构实参的用户函数调用求值（ToSpacePhasor 的 `phi[m] = symmetricOrientation(m)`、结构参数代入后的 `g(m)`）之后，
电机一族再往下是表达式里的数组值调用（"qualified function call [array-valued call inside an expression]"，MSL 全库此类
三十余个，下一桶）与条件组件的条件。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-06 加宽 61s 之后：PASS 100（不变）

参数数组绑定里嵌在运算中的数组值调用（Polyphase.Sources 的 `phase[m] = -symmetricOrientation(m)`）+ 调用输出维的实参成结构
参数 + 取负的零（旧错：`-{0.0, …}` 的零元素 omc 打 -0.0）之后，SKIP 原因的挪动：20 个 qualified function call → connect operand shape；7 个 qualified function call → constant evaluation；6 个 qualified function call → conditional component condition；4 个 qualified function call → non-numeric function argument；3 个 qualified function call → vectorized call argument；3 个 qualified function call → constant user call not evaluated。FAIL / ACCEPTS-INVALID /
CRASH 0。

## 2026-10-06 加宽 61t 之后：PASS 100 → 104

连接器里的子连接器数组（Polyphase `Plug` 的 `Pin pin[m]`，connect 逐元素展开、下标落在 pin 段、维取本实例的修饰值）+ 函数形参的
常量维（SpacePhasors.activePower 的 `input SI.Voltage v[m]` → `Real[3] v`）+ Evaluate 求值不再把碰到的向前参数打成字面量（omc
在压平之后才求结构参数的绑定；IMC_YD 的 `aimc.statorCoreParameters.m` 因此恢复别名连带、打 3）之后：Machines 的 IMC_YD /
IMC_YDarc、Polyphase 的 TransformerYD / TransformerYY 转 PASS；16 个 PowerConverters 示例从 connect operand shape 往下露出到
duplicate variable（多重继承来的同名元素 `hbridge.fire_p`、`rectifier.m`，omc 的 replaceDuplicates / checkDuplicates 只留一个，见 R11 §3.4 第 3 点、探针 P31；ymc 未做）。FAIL /
ACCEPTS-INVALID / CRASH 0。

## 2026-10-06 加宽 61u 之后：PASS 104（不变）

继承来的声明（绑定 / 维 / 声明修饰 / 中间类的 extends 修饰）改在声明所在类的词法作用域里查名之后，514 个示例逐例
verdict 都不变（MSL 里派生类没有遮住基类包里名字的同名元素）；15 个的 SKIP 原因往下挪：7 个 qualified function call → constant evaluation，8 个 enumeration type path → unqualified enum literal 4 / qualified/cross-scope reference（Digital.Tables 的枚举表）3 / qualified function call 1。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 61v 之后：PASS 104（不变）

Evaluate = true 的结构判定改照 omc 的访问序 / 深度 4 / fixed 属性之后，514 个示例逐例 verdict 与 SKIP 原因都不变（MSL 的
Evaluate 参数链都短，链上没有 Evaluate = false / fixed 属性）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 61w 之后：PASS 104（不变）

外部对象（`extends ExternalObject`）接上之后，30 个"unresolved base class [ExternalObject]"的示例都往下露出：size() 矩阵构造进
结构绑定 8、记录构造无实例 6、if 式选中的支引用参数 5、类注释带转义 2、unresolved derived base 2、矩阵元素形状 2、redeclare
修饰 2、条件组件 / 整体数组引用 / 继承记录的构造各 1。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 61x 之后：PASS 104 → 122

继承来的同名元素照 omc 去重（留先出现的，其余须相同）之后，PowerConverters 的 18 个示例（RectifierBridge2mPulse 6、
RectifierCenterTap2mPulse 5、RectifierCenterTapmPulse 5、HBridge 2）转 PASS——它们的多相整流器同时继承了两个都声明 `m` /
`fire_p` 的接口类。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 61y 之后：PASS 122（不变）

子实例里数组的维改按它自己的实例取（发射时记下的实际维）、一维长 1 的向量化收成单元素数组之后，514 个示例逐例 verdict 与
SKIP 原因都不变（受益的是 testsuite 里老 MSL 块 BC* / BM* 等 41 个）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 61z 之后：PASS 122（不变）

无绑定的固定参数拿 start 当绑定（updateParameterBinding）之后，514 个示例逐例 verdict 与 SKIP 原因都不变（MSL 示例里没有只靠
start 的参数）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 62a 之后：PASS 122（不变）

复合 inner/outer 接上之后，此前卡在"composite/unresolved outer"的 59 个示例都往下露出：参数数组的 Evaluate = true 37（MultiBody
全系，World 的 `parameter Types.Axis n = {0, -1, 0} annotation(Evaluate = true)`）、函数里给输出赋值的形态 6、复合数组元素引用 5、
条件组件的条件 3、`inner outer` 2、Medium 包里的类型 3、getInstanceName / 嵌套类修饰各 1-2。FAIL / ACCEPTS-INVALID / CRASH 0。
另：msl_sweep 的 omc 结果缓存曾把被打断（扫描进程被杀）的 omc 记成空文件，以后一直当 omc 拒绝——211 个空缓存已清掉，工具改为
空输出不缓存、读到空缓存重跑。

## 2026-10-07 加宽 62b 之后：PASS 122（不变）

Evaluate = true 的参数数组当结构参数之后，此前卡在它上面的 37 个 MultiBody 示例（World 的 `parameter Types.Axis n`）往下露出：
带记录形参的函数里记录构造（Frames.Orientation：数组字段 + extends Icons.Record）25、参数数组绑函数调用 6、复合组件的变率前缀 2、
限定名函数调用 2、类注释带转义 2。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 62c 之后：PASS 122 → 132

条件组件的条件能比较字符串、能经修饰引用声明在后的记录字段之后，Machines 的 10 个示例转 PASS：IMC_DOL / IMC_Initialize /
IMC_Steinmetz / IMS_Start（TerminalBox 的 `star if terminalConnection <> "D"`）、SMPM_Braking / SMPM_CurrentSource /
SMPM_NoLoad / SMPM_ResistiveBraking / SMPM_VoltageSource / SMR_DOL（`useDamperCage = smpmData.useDamperCage`，smpmData 声明在后）。
SMPM_NoLoad 还顺带露出 rem 的内联（omc 把 rem 早内联成 `x - div(x, y) * y`，此前 ymc 原样打 `rem(…)`）。getInstanceName 接上后
ActuatorWithNoise 往下卡在噪声种子的外部函数调用。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 62d 之后：PASS 132 → 143

记录构造函数能带数组 / 继承字段、函数里能取数组字段、函数体里的 transpose / identity 展开之后，DC 电机全系 11 个示例转 PASS
（DCPM_* / DCEE_Start / DCSE_* / DC_CompareCharacteristics / ControlledDCDrives 两个——它们的损耗记录参数传进函数）。MultiBody
往下卡在记录值的常量函数调用（Frames.nullRotation）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 62e 之后：PASS 143 → 146

记录值的常量函数调用打成构造调用、记录组件的折叠绑定并进修饰通路之后，此前卡在 composite/record binding 的 3 个 FluxTubes 示例
转 PASS（QuadraticCoreAirgap / ToroidalCoreAirgap / ToroidalCoreQuadraticCrossSection）。MultiBody 的 25 个示例越过
Frames.nullRotation，往下卡在模型里嵌在运算中的数组值调用（26 个，Visualizers.Internal.Lines 的
`r_abs = r + T.resolve1(R.T, r_lines)`）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 62f 之后：PASS 146（不变）

模型里嵌在运算中的数组值调用整体保留、走符号形态之后，此前卡在它上面的 26 个 MultiBody 示例往下露出：24 个卡在
`world.gravityAcceleration(…)`（组件里的可替换函数调用，Parts.Body 的重力）、1 个卡在嵌套类修饰、1 个卡在 derivative 注解的
形态。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 62g 之后：PASS 146 → 151

函数体引用派生类型的包常量（`Modelica.Constants.T_zero` 是 `NonSI.Temperature_degC`，Units.Conversions 的 from_degC /
to_degC）能内联之后，5 个示例转 PASS：Analog.Examples.Resistor、Rotational / Translational.Examples.HeatLosses、
HeatTransfer.Examples.ControlledTemperature / TwoMasses。组件里的函数调用（`world.gravityAcceleration`）接上后 MultiBody 往下
卡在 World 的 `Connections.root`（超定连接图）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 62h 之后：PASS 151 → 154

结构参数绑定里矩阵构造套 size(a, k) 的连带标记照 1.25.0 做了之后，KinematicPTP2 / CombiTimeTable 的 `nout` 那一族里 3 个示例
转 PASS：Blocks.Examples.PID_Controller、Clocked.Examples.CascadeControlledDrive.Continuous、MultiBody.Examples.Systems.RobotR3.OneAxis。
其余 10 个往下露：9 个卡在对结构参数数组 `columns` 的整体引用（CombiTimeTable 的外部对象构造，omc 代成 `{2, 3}`），1 个卡在
qd_max 的求值。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-07 加宽 62i 之后：PASS 154 → 155

结构参数数组的整体引用代成整个数组的字面量（调用随即求值）之后，此前卡在 `columns` 整体引用上的 CombiTimeTable 一族 10 个示例
往下露：Thermal.HeatTransfer.Examples.Motor 转 PASS；4 个（Blocks.Examples.ShowLogicalSources / LogicalNetwork1 / Interaction1、
Analog.Examples.CharacteristicThyristors）卡在数组式里的推导式，3 个（Machines IMC_Conveyor、Translational.Examples.Vehicle、
FluidHeatFlow.Examples.TestOpenTank）卡在 derivative / inverse 注解的形态，SoftStarter 卡在常量求值，TestCylinder 卡在 connect
操作数形状。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62j 之后：PASS 155 → 166

初始算法里可给 fixed = false 的参数赋值、连接器里的参数记录成员按字段生成相等断言之后，FluidHeatFlow 的 10 个示例转 PASS
（SimpleCooling / ParallelCooling / IndirectCooling / PumpAndValve / PumpDropOut / ParallelPumpDropOut / OneMass / TwoMass /
TwoTanks / TestCylinder——TestCylinder 还要 fixed 预扫按合并后的修饰取值，此前会把 cylinder2.T0fixed 错打成 final），另有
Machines DCMachines.DCPM_Cooling。Digital 的 15 个示例越过枚举属性值的参数引用、when 条件向量里的 `not initial()`、组件数组
实例的算法段、枚举维常量表的运行时下标，往下卡在包里的枚举值真值表（`Tables.OrTable[…]` 等）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62k 之后：PASS 166 → 189

包里的枚举值真值表能按运行时枚举下标取元素（矩阵写法 / 3 维 `[{{{…}}}]` 的求值、限定名常量各维按声明处作用域解析类型）、
枚举维变量数组的运行时下标照写、枚举数组属性逐元素摊分、枚举区间常量的 `:` 维、import 别名下的短类连接器之后，Digital 的
22 个示例转 PASS（Multiplexer / FlipFlop / HalfAdder / FullAdder / Adder4 / Counter3 / Counter / VectorDelay / DFFREG ×4 /
DLATREG ×4 / NXFER / NRXFER / BUF3S / INV3S / WiredX / MUX2x1），另有 Analog.Examples.AD_DA_conversion。Digital 只剩 RAM
（限定名函数调用）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62l 之后：PASS 189 → 250

Clock 成了可实例化的内建类型（顶层作用域里查得到，`connector ClockInput = input Clock`）、`sample(u, clock)` 认 Clock 组件
引用、Clock 两实参构造的第二实参求值存回参数（没选中的 if 分支里也做，同步算子计数实参照样标结构参数）之后，Clocked.Examples
的 61 个示例转 PASS（Elementary 各信号类型、CascadeControlledDrive / SimpleControlledDrive 的钟控版、Systems 的混合单元与
发动机节气门等）。Clocked 余下 13 个：推导式嵌在数组式里（BooleanTable）6、向量化调用实参形态 3、constrainedby 2、元组 /
函数赋值目标 2。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62m 之后：PASS 250 → 259

逐元素摊开的参数数组绑定里矩阵的推导式列照样展开、外层 if 的条件参数向前引用时第二遍再判、选中支只引用非结构参数的照常之后，
MSL BooleanTable 一族的 9 个示例转 PASS（Blocks.Examples.ShowLogicalSources / LogicalNetwork1、Analog.Examples.CharacteristicThyristors、
Clocked BooleanSignals 的 Hold / SubSample / SuperSample / ShiftSample / BackSample / UpSample）。顺带修了取元素后再化简的静默错
（`if b then 1.0 else 1.0` 被收成 `1.0`）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62n 之后：PASS 259 → 259

`Connections.*` 不再一律当限定名函数调用 SKIP：超定连接图（root / potentialRoot / branch 摘除、环里断开出 equalityConstraint
方程、isRoot / rooted 按最终根与 BFS 距离代值）和函数体元组赋值做完之后，MultiBody 的 27 个示例越过 World 的
`Connections.root(frame_b.R)`，往下卡在：参数数组整个绑定是函数调用（关节的 `e = normalizeWithAssert(n)`）11（连同此前就卡在这里的共 17 个）、复合组件的变率
6、类注释里的转义（omc 加载路径相关的怪相，入账）5、derivative 注解形式 2 等；Noise 的 7 个越过函数体元组赋值，卡在纯函数调
非纯函数的告警。PASS 数不变。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62o 之后：PASS 259 → 259

参数数组整个绑定是实参求不成字面量的函数调用时整个变量不标量化（`parameter Real[3] e(unit = "1") =
normalizeWithAssert(n)`，带 min / max / nominal 的照逐元素）之后，MultiBody 的 17 个示例越过关节的轴向量参数，往下卡在
函数体里的 outerProduct 15（Frames.planarRotation，下一桶）、derivative 注解形式 2。PASS 数不变。
FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62p 之后：PASS 259 → 266

derivative 注解带修饰（`derivative(noDerivative = …)` / `zeroDerivative` / `order = 2`）的函数照样发射之后，7 个示例转 PASS
（Analog.Examples.GenerationOfFMUs / ResonanceCircuits、Translational.Examples.GenerationOfFMUs / Vehicle、
HeatTransfer.Examples.GenerationOfFMUs、FluidHeatFlow.Examples.TestOpenTank、MultiBody.Examples.Elementary.
PointGravityWithPointMasses）。outerProduct 在函数体 / 等式里留符号形态（`promote(e, 2) * transpose(promote(e, 2))`）之后，
MultiBody 的 15 个示例越过 Frames.planarRotation 与 resolve2，往下卡在类注释的转义（下一桶）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62q 之后：PASS 266 → 272

类注释的转义照 translateCommentList 的怪相走（没有类级 annotation 的长类双重转义、有 annotation 的照原样）之后，
Blocks.Examples.BooleanNetwork1、Batteries.Examples.SuperCapDischargeCharge 转 PASS；向量化调用的实参是运算式
（`to_unit1(r - r_shape)` 等）之后，Machines 的 IMC_Inverter / IMC_Conveyor / SMR_Inverter / SMPM_Inverter 转 PASS。
MultiBody 越过类注释与 `to_unit1(r - r_shape)` 的那批，往下集中卡在记录参数的绑定（"variability on composite component"，
29 个，`final parameter Frames.Orientation R_start = Frames.axesRotations(…)`）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62r 之后：PASS 272 → 276

带属性修饰的空数组照样整体不出、元组方程的目标可以是整个数组 / 空数组（打 `_`）、函数体里空数组作实参打 `{}` 之后，
Blocks.Examples.Filter / FilterWithDifferentiation / FilterWithRiseTime 与 Clocked RealSignals.UniformNoise 转 PASS。
FastFourierTransform 的两个往下卡在实参类型 / 形状。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62s 之后：PASS 276 → 276

记录参数的整体绑定照 flattenComplexComponent 的三支走（含 equalityConstraint 的 Frames.Orientation 不再被当 operator record
挡下；final 参数记录内联放弃后 tryEvalExp 成字面量、求不出挪初始方程）之后，MultiBody 的 29 个示例越过
"variability on composite component"：14 个往下卡在属性值里的数组调用（`w_a(start = Frames.resolve2(R_start, w_0_start))`，
下一桶）、13 个卡在 BodyBox / BodyCylinder 的 `R = Frames.from_nxy(r, widthDirection)`（omc 因 Evaluate = true 连带把 r 当
结构参数、走求值支；ymc 的 Evaluate 数组参数这条路还没通）、2 个卡在数组 if 的调用分支。PASS 数不变。
FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62t 之后：PASS 276 → 281

属性值里的数组调用逐元素取、数组值的 fixed 结构求值、数组 if 按结构条件选支、实例化期经子实例求值之后：MultiBody 的
Elementary.Pendulum / PointGravity / SpringWithMass 逐字节一致（Body 的 `w_a(start = Frames.resolve2(R_start, w_0_start),
fixed = fill(w_0_fixed, 3))`、`phi_start` 的数组 if、`sphere(…) if world.enableAnimation and animation and sphereDiameter >
0`），Clocked 的 UniformNoiseXorshift64star（`state64(start = Xorshift64star.initialState(localSeed, globalSeed))`）与
Machines 的 IMC_DCBraking（条件组件 `terminalBox.star` 经子实例求值）也通过。MultiBody 其余：15 个卡在 BodyBox /
BodyCylinder 的 `R = Frames.from_nxy(r, widthDirection)`（r 由 Evaluate 的 `lengthDirection = to_unit1(r - r_shape)` 连带
成结构参数，要先通向量化调用的结构求值）、8 个卡在 Vectors.length 的结构代入、8 个卡在 world 重力函数别名修饰的缺省值。
FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62u 之后：PASS 281 → 301

向量化调用 / 推导式绑定的结构求值（推导式里调的函数按 omc 的化简决定打不打）、diagonal 留调用、函数别名修饰缺省的向前引用、
全零数组运算数消掉、别名结构参数的调用实参逐元素之后，MultiBody 越过 BodyBox / BodyCylinder 的 `R = Frames.from_nxy(r,
widthDirection)`、BodyShape 的 `length = Vectors.length(r - r_shape)`、World 重力函数的别名缺省：Elementary 的 DoublePendulum /
DoublePendulumInitTip / ForceAndTorque / FreeBody / HeatLosses / InitSpringConstant / LineForceWithTwoMasses /
PendulumWithSpringDamper / PointGravityWithPointMasses2 / SpringDamperSystem / SpringMassSystem / ThreeSprings，Loops 的
Fourbar1 / Fourbar2，Rotational3DEffects 的 BevelGear1D / GearConstraint / GyroscopicEffects，Systems.RobotR3.FullRobot，
以及 Machines 的 SMEE_LoadDump / SMEE_Rectifier 逐字节一致（MultiBody 42 个示例里 23 个）。MultiBody 其余：5 个卡在压平末尾
标成结构参数的整体数组变量（RevolutePlanarLoopConstraint 的 `ey_a`）、5 个卡在结构参数的非字面量绑定（Constraints.* /
ActuatedDrive）、4 + 1 个卡在修饰名指向嵌套类、2 个卡在组件数组修饰分发出的结构别名、RollingWheel 卡在混合类型的数组修饰
字面量、PlanarLoops_analytic 卡在无绑定的 Evaluate 参数数组。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-08 加宽 62v 之后：PASS 301 → 311

Evaluate 数组参数的调用绑定逐元素（RevolutePlanarLoopConstraint 的 `ey_a`）、实例化期向前引用的依赖链（Body 的 `sphere(…) if
… sphereDiameter > 0` 读到声明在后的 world）之后，MultiBody 的 Loops.Engine1a / Engine1b / EngineV6 / PlanarFourbar、
Constraints.PrismaticConstraint / RevoluteConstraint / SphericalConstraint / UniversalConstraint、Rotational3DEffects
.ActuatedDrive / MovingActuatedDrive 逐字节一致（MultiBody 42 个示例里 33 个）。MultiBody 其余 9 个：4 + 1 个卡在修饰名指向
嵌套类（`redeclare function` 修饰）、2 个卡在组件数组修饰分发出的结构别名、RollingWheel 卡在混合类型的数组修饰字面量、
PlanarLoops_analytic 卡在无绑定的 Evaluate 参数数组。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-09 加宽 62w 之后：PASS 311 → 312

operator record（MSL Complex）第一段：`Complex(…)` → `Complex.'constructor'.fromReal(…)`、记录类型的函数缓存（默认构造函数 +
'constructor'，短类别名按别名的路径）、短类连接器别名 `connector ComplexOutput = output Complex`（类型是记录、组件按连接器）
之后，ComplexBlocks.Examples.TestConversionBlock 逐字节一致。ComplexBlocks / Electrical.QuasiStatic / Magnetic.QuasiStatic 的
其余 37 个 Complex 示例：多数卡在记录运算数的重载运算符（`v = R_actual*i`、`pin_p.i + pin_n.i = Complex(0)`、`-i`，下一桶），
另有组件数组的外来修饰、函数形参的别名记录类型（`input SI.ComplexVoltage v[:]`）等。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-09 加宽 62x 之后：PASS 312 → 325

operator record 的重载运算符（记录运算数的 `+ - * / ^ ==` 等按 omc 的 checkOverloadedBinaryOperator：左运算数类型里的同名运算符
函数、只认精确匹配、没有就隐式构造另一侧，函数名用运算数类型——别名照别名——的路径）、operator record 常量换成记录式
（`Modelica.ComplexMath.j`、模型里的 `constant Complex j`）、括号里的 if 记录绑定选支、写在声明之前的别名运算数之后，
Electrical.QuasiStatic.SinglePhase 的 SeriesResonance / ParallelResonance / SeriesBode / Rectifier / Transformer、
ComplexBlocks.ShowTransferFunction、Magnetic.QuasiStatic.FluxTubes 的 7 个（BasicExamples 3、FixedShapes 2、Leakage 2）逐字节
一致（38 个 Complex 示例里 14 个）。其余 24 个：Polyphase / 电机卡在组件数组的外来修饰、函数形参的别名记录类型
（`input SI.ComplexVoltage v[:]`）、final 参数记录绑定的强制内联（FundamentalWave 的 `final parameter Complex N = …`），
FluxTubes 的 Linear / NonLinearInductor 卡在材料数据记录的构造调用。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-09 加宽 62y 之后：PASS 325 → 343

final parameter 记录绑定的强制内联（FundamentalWave 的 `final parameter Complex N = effectiveTurns*Modelica.ComplexMath.exp(
Complex(0, orientation))` 内联化简成 `N.re = effectiveTurns * …`）之后，Magnetic.FundamentalWave 的 BasicMachines 15 个
（InductionMachines 的 IMC_Conveyor / IMC_DOL / IMC_Initialize / IMC_Inverter / IMC_Steinmetz / IMC_YD / IMS_Start，
SynchronousMachines 的 SMEE_DOL / SMEE_LoadDump / SMEE_Rectifier / SMPM_Braking / SMPM_CurrentSource / SMPM_Inverter /
SMPM_VoltageSource / SMR_Inverter）与 Components 的 EddyCurrentLosses / PolyphaseInductance / SinglePhaseInductance 逐字节一致。
电机例子走得更远后暴露的两处一并修了：结构参数代入后算法段里的 `String(m)` 照常求值（`assert(true, "3 phases …")`），记录整体
绑定写在数据记录之前时字段的结构别名（`aimc.statorCoreParameters.m = 3`）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-09 加宽 62z 之后：PASS 343（不变）

记录数组三处（带修饰短类的声明类型、作记录形参的实参的向量化、作重载运算符的运算数逐元素）做完，QuasiStatic Polyphase / 电机
示例越过了此前的 `composite array external modifier` / `record argument form` / `overloaded operator on array operand`，停在
下一处：BalancingStar / EddyCurrentLosses 的 symmetricOrientation 常量调用、BalancingDelta / TransformerTestbench /
PolyphaseInductance 的记录数组整体绑定、TestSensors 的函数里的记录数组（`input SI.ComplexVoltage v[:]`）、UnsymmetricalLoad
的组件数组修饰分发。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-09 加宽 63a 之后：PASS 349（+6）

记录数组续（推导式 / fill 整体绑定、拆不开时挪成逐元素方程、形参是记录数组的 scalarProduct、组件声明修饰里只换类型的成员
重声明）做完：QuasiStatic Polyphase 的 BalancingStar / BalancingDelta 转 PASS（与 omc 逐字节一致：PowerSensor 的 Sum 块
`uInternal` 推导式、`k = fill(Complex(1, 0), nin)`、`y = Complex.'*'.scalarProduct(k, uInternal)`，多相电流 / 电压传感器的
`redeclare each final SI.Current re`）；FundamentalWave ComparisonPolyphase 的四个（IMC_DOL / IMS_Start / SMPM_Inverter /
SMR_Inverter_Polyphase）因结构代入后的常量用户调用照常求值转 PASS。QuasiStatic 其余示例停在下一处：函数形参是 operator
record 短类别名的记录数组（FundamentalWave 电机 12 个停在 function variable type，IMC_YD 核实是
`activePower(input SI.ComplexVoltage v[:], …)`）、函数里记录字段的
引用形态（TestSensors / IMC_DOL / SMPM_Mains 等）、final parameter 的 Complex 矩阵绑定（`sTM = symmetricTransformationMatrix(m)`，
UnsymmetricalLoad）、PolyphaseInductance / EddyCurrentLosses 的多相电磁转换器里的记录数组整体绑定（子原因待查）、无实例的材料记录构造（FluxTubes Linear/NonLinearInductor）。
FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-09 加宽 63b 之后：PASS 349→361（+12）

函数里的记录（数组）做完：记录数组元素字段（quasiRMS 的 `u[k].re`）、operator record 短类别名形参（activePower 的 `input
SI.ComplexVoltage v[:]`）、函数体里推导式实参的向量化（`sum(real(… v[(1:size(v, 1))[$i1]] …) for $i1 in 1:size(v, 1))`）、返回
记录数组的函数求值后拆字段（多相电磁转换器的 `sTM = symmetricTransformationMatrix(m)`，含 -0.0 的取负）、范围作数组运算数与
输出维里的调用（indexPositiveSequence）。MSL Complex 示例 38 个里 PASS 16→28：QuasiStatic FundamentalWave 的 InductionMachines
/ SynchronousMachines 电机示例（IMC_Characteristics / IMC_DOL / IMC_YD / IMC_Initialize / IMS_Characteristics / IMS_Start /
SMPM_OpenCircuit / SMPM_Mains / SMEE_Generator）、Components 的 PolyphaseInductance / EddyCurrentLosses、Polyphase 的
UnsymmetricalLoad 等，均与 omc 逐字节一致。余下 10 个停在：外部字符串函数的常量求值（PartialBasicTransformer 的
`C1 = substring(VectorGroup, 1, 1)`，TransformerTestbench / IMC_Transformer）、TestSensors 的限定名调用、FluxTubes 的无实例材料
记录构造、组件里的函数 `singleToMultiPhase.u` 一类（SMPM_CurrentSource / SMPM_MTPA / SMR_CurrentSource）、向量化实参形态
（IMC_Conveyor）、when 体（IMC_withLosses）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-09 加宽 63c 之后：PASS 361→369（+8）

外部 C 字符串函数（ModelicaStrings_substring / length）照 omc 的 FFI 求值就地算、经组件向前引用的常量照样内联：变压器一族
（PartialBasicTransformer 的 `constant String C1 = substring(VectorGroup, 1, 1)`、示例里 `transformerData(C1 = substring(
transformer.VectorGroup, 1, 1))`）转 PASS——Electrical.Machines.Examples.Transformers 的 AsymmetricalLoad / Rectifier6pulse /
Rectifier12pulse / IMC_Transformer、InductionMachines.IMC_Transformer、FundamentalWave 与 QuasiStatic FundamentalWave 的
IMC_Transformer、QuasiStatic TransformerTestbench，均与 omc 逐字节一致。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-09 加宽 63d 之后：PASS 369→376（+7）

记录类 `extends Base(field = …)` 的缺省（构造函数输入、记录值求值按继承展开）做完：FluxTubes 的 MovingCoilActuator
（ArmatureStroke / ForceCurrentBehaviour / ForceStrokeBehaviour，`PlasticNdFeB extends BaseData(H_cBRef = …)`）、Hysteresis 的
InductorWithHysteresis / SinglePhaseTransformerWithHysteresis2 / ThreePhaseTransformerWithRectifier、FluidHeatFlow 的 WaterPump
（`Media.Water extends Medium(rho = 995.6, …)`）转 PASS，均与 omc 逐字节一致。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-09 加宽 63e 之后：PASS 376→383（+7）

函数体里的元素式内建向量化做完（MSL SpacePhasors.Functions.ToSpacePhasor 的 `2/m*{+cos(+phi), +sin(+phi)}` 与
`{{+cos(-phi[k]), -sin(-phi[k])} for k in 1:m}`、`protected final parameter Integer m`）：Machines 的 SMEE_DOL / SMEE_Generator /
TransformerTestbench、Polyphase 的 Rectifier / TestSensors、FundamentalWave 的 SMEE_Generator / SMEE_Generator_Polyphase 转 PASS，
均与 omc 逐字节一致。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-09 加宽 63f 之后：PASS 383→387（+4）

被字面量条件剪掉的 if 分支里的越界下标不再 SKIP（MSL StateGraph PartialStep 的 for 展开 `inPort[i].occupied = if i == 1 then
localActive else inPort[i-1].occupied or inPort[i-1].set`，i = 1 时剪掉的支引用 `inPort[0]`）：StateGraph 的 FirstExample /
FirstExample_Variant2 / FirstExample_Variant3 / ExecutionPaths 转 PASS，均与 omc 逐字节一致；ControlledTanks 往前走到
`inner outer` 组件（CompositeStep 的 stateGraphRoot）照旧 SKIP。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-10 加宽 63g 之后：PASS 387→397（+10）

纯函数后标 impure（MSL Random.Utilities.initializeImpureRandom 体里 `setInternalState(rngState, id);`）、ModelicaStrings_hashString
就地求值（`automaticLocalSeed(getInstanceName())`）、when 体里的元组方程（Blocks.Interfaces.PartialNoise 的 `(r_raw, state) =
generator.random(pre(state))`）做完：Blocks.Examples.Noise 的 UniformNoise / AutomaticSeed / Distributions / UniformNoiseProperties /
NormalNoiseProperties / ImpureGenerator / ActuatorWithNoise / DrydenContinuousTurbulence 转 PASS；元组目标算进 discrete Real 的
赋值后 Blocks.Examples.Interaction1、PowerConverters HBridge_DC_Drive 一并转 PASS。均与 omc 逐字节一致；FAIL / ACCEPTS-INVALID /
CRASH 0。Math.Random.Examples.GenerateRandomNumbers 往前走到算法里给非连续变量赋值，照旧 SKIP。

## 2026-10-10 加宽 63h 之后：PASS 397→402（+5）

派生记录的构造调用求值、基类在别的包里（FluxTubes 材料记录 `record M350_50A extends FluxTubes.Material.SoftMagnetic.BaseData(
mu_i = 1210, …)`，BaseData 的字段带缺省、类型是 SI 短类）做完：FluxTubes 的 SaturatedInductor / SolenoidActuator
ComparisonQuasiStatic / ComparisonPullInStroke、QuasiStatic.FluxTubes 的 LinearInductor / NonLinearInductor 转 PASS，均与 omc
逐字节一致。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-10 加宽 63i 之后：PASS 402（不变）

常量记录的整体引用作实参（`mos1RenameParameters(modelcard, C)`）、记录构造函数的外来基类字段（`MosModelLineParams extends
Mosfet.MosfetModelLineParams`）做完，Spice3 的 Inverter / Nand / CascodeCircuit 往前走到 `final parameter … p = Mos1.
mos1RenameParameters(modelcard, C) annotation(Evaluate = true)`——实参里整个记录参数（modelcard）在编译期求值未做，照旧 SKIP
（此前同形态的 final 记录绑定会被挪成初始方程，静默错，现 SKIP）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-10 加宽 63j 之后：PASS 402→410（+8）

整个记录参数的编译期求值（`mos1RenameParameters(modelcard, C)` 的 modelcard）、Evaluate = true 记录的结构语义（带 start 的字段
才是结构参数并代入、结构记录的整体引用代成记录式、记录引用整体绑定求值成字段字面量）、函数体元组赋值 / 字面量条件的 if / 派生
记录实参做完：Spice3 的 Inverter / InvertersApartRecord / InvertersExtendedModel / FourInverters / Nand / Nor（MOSFET）、
Oscillator（BJT）、CascodeCircuit（JFET）转 PASS，均与 omc 逐字节一致。FAIL / ACCEPTS-INVALID / CRASH 0。Graetz /
Spice3BenchmarkFourBitBinaryAdder 停在记录函数折叠的字段形态，三个 Benchmark 停在结构记录绑定求值，照旧 SKIP。

## 2026-10-10 加宽 63k 之后：PASS 410（不变）

继承来的类 / 元素进查找表（inst 的类树合并 + flatten 的 SCode 查找本地 → 继承 → import）做完，MSL 示例的 PASS / SKIP 逐例不变：
剩下的 SKIP 主要卡在类重声明（Media 的 `redeclare package Medium`、MultiBody 的重声明函数 / 模型），继承查找只是它们的前提之一。
新加的守卫（经改了常量的 extends / 元素级重声明继承来的类、外层实例上改过的常量）在 MSL 示例里没有触发。FAIL / ACCEPTS-INVALID /
CRASH 0。

## 2026-10-10 加宽 63l 之后：PASS 410（不变）

根类是短类别名（`model R = A(p = 2) "c";`）做完；MSL 示例都是长类定义，不受影响，PASS / SKIP 逐例不变。FAIL / ACCEPTS-INVALID /
CRASH 0。

## 2026-10-10 加宽 63m 之后：PASS 410（不变）

找不到 inner 的 outer 生成 inner（generateInner / insertGeneratedInners）做完；MSL 示例都自带 inner（World 等），PASS / SKIP
逐例不变。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-10 加宽 63n 之后：PASS 410→412（+2）

约束子句的修饰（`replaceable … constrainedby T(mods)`，instElementModifier：声明修饰之后、短类链之前）做完：Clocked 的
Elementary.RealSignals.SampleWithADeffects、SimpleControlledDrive.ClockedWith_AD_DA_Effects 转 PASS，均与 omc 逐字节一致。
FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-10 加宽 63o 之后：PASS 412（不变）

派生类里的元素级类重声明（`redeclare model B … end B;` / `redeclare model B = X(p = 3)`）在基类实例的类树里顶替做完；MSL 示例的
类重声明几乎都写在修饰里（`redeclare package Medium = …`、`redeclare model HeatTransfer = …`），PASS / SKIP 逐例不变。FAIL /
ACCEPTS-INVALID / CRASH 0。

## 2026-10-10 加宽 63p 之后：PASS 412（不变）

修饰里的类重声明（extends / 组件声明 / 短类修饰、根类别名）做完；MSL 示例里的类重声明几乎都是包（`redeclare package Medium = …`），
包里的类型、常量、函数经 SCode 链解析仍待第 2 阶段，PASS / SKIP 逐例不变。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-10 加宽 63q 之后：PASS 412（不变）

class extends（`redeclare model extends B … end B;`）做完；MSL 里的 class extends 集中在 Media 的介质包（`redeclare record extends
ThermodynamicState`、`redeclare model extends BaseProperties`），随包重声明一起仍待第 2 阶段，PASS / SKIP 逐例不变。FAIL /
ACCEPTS-INVALID / CRASH 0。

## 2026-10-11 加宽 63r 之后：PASS 412→415（+3）

`inner outer` 组合前缀做完：StateGraph 的复合步（PartialCompositeStep 的 `inner outer CompositeStepState stateGraphRoot(suspend = …,
resume = …)`，步里的 `outer` 连它、本层的 `connect(outerState.subgraphStatePort, stateGraphRoot.subgraphStatePort)` 落到外层的
stateGraphRoot）——Examples.ShowCompositeStep、ShowExceptions、ControlledTanks 转 PASS，均与 omc 逐字节一致。FAIL /
ACCEPTS-INVALID / CRASH 0。

## 2026-10-11 加宽 63s 之后：PASS 415（不变）

元素级组件重声明（`extends A; redeclare Real x = 2.0;`）做完，修饰里重声明取自原声明的条件 / 维改在原声明处求；MSL 示例里的
组件重声明都写在修饰里（`redeclare Medium.BaseProperties medium(…)` 之类，随包重声明待第 2 阶段），PASS / SKIP 逐例不变。
FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-11 加宽 63t 之后：PASS 415→419（+4）

MSL Batteries 一路：CombiTable1Ds 的 `columns` 作下标 / 经记录字段传下来的表的 `:` 维、枚举参数经记录字段求值、记录构造的继承
字段、复合数组修饰经记录数组中间段分发；另改两处静默错（整体绑定记录里 Evaluate 字段只看 start；if 方程条件里继承来的记录字段不算
固定）。Electrical.Batteries.Examples.BatteryDischargeCharge、CCCVcharging、CCCV_Cell、CCCV_CellRC 转 PASS，均与 omc 逐字节一致；
CCCV_Stack / CCCV_StackRC（记录里的记录重声明）、ShowImpedance（字符串常量经记录绑定）未做。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-11 加宽 63u 之后：PASS 419→422（+3）

MultiBody 解析式闭环一路：组件数组修饰分发的结构参数别名连带（JointSSP / JointRRP 的 `Constant position_b[3](k = rRod2_ib)`，
omc 看分发前的原式是不是 cref）；另改负零（CylinderBase 的 `rRod2_ib = -{0, pistonLength, 0}`：Integer / Real 混排数组定型成 Real
再取负得 `-0.0`）。Mechanics.MultiBody.Examples.Loops.Fourbar_analytic、Engine1b_analytic、EngineV6_analytic 转 PASS，均与 omc
逐字节一致。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-11 加宽 64a 之后：PASS 422→428（+6）

TransferFunction / PowerConverters 一路：矩阵构造的空块（`vector([zeros(max(0, na - nb), 1); b])`，na = nb）、零长切片
（`der(x_scaled[2:nx]) = x_scaled[1:nx - 1]`，nx = 1）、组件数组里的数组方程（PowerConverters Filter 的 `transferFunction[na]`）、
向前引用的数组常量与 elseif 链的条件数组（VoltageToAngle 的 `table = if … then Lin elseif … then H01 else RMS`）、被移除的
条件组件里的结构标记（MultiTerminalBox 三角形接法的 `starpoint(final m = mSystems)` → mBasic 代成 3）。
Electrical.PowerConverters.Examples.ACDC.RectifierBridge2Pulse.ThyristorBridge2Pulse_DC_Drive、RectifierBridge2mPulse.
ThyristorBridge2mPulse_DC_Drive、ACAC.Dimmer_R、Dimmer_RL、SoftStarter、Electrical.Polyphase.Examples.PolyphaseRectifier 转 PASS，
均与 omc 逐字节一致。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-11 加宽 64b 之后：PASS 428→433（+5）

FFT 一路：函数体里的向量化调用（realFFT 的 `phases := to_deg(Phi[1:nfi])` →
`array(Modelica.Units.Conversions.to_deg(Phi[(1:nfi)[$i1]]) for $i1 in 1:nfi)`）、when 算法里元组赋值的整体数组目标
（`(info, Ai, Phii) := realFFT(y_buf, nfi)`；GenerateRandomNumbers 的 `(r64, state64) := Generators.Xorshift64star.random(pre(state64))` 同）、函数输入缺省 `phases[:] = fill(0.0, 0)`
折成 `{}`。Math.FastFourierTransform.Examples.RealFFT1、RealFFT2、Blocks.Examples.Rectifier6pulseFFT、Rectifier12pulseFFT、
Math.Random.Examples.GenerateRandomNumbers 转 PASS，均与 omc 逐字节一致（stdout；omc 在 stderr 另报 realFFTwriteToFile 调 impure
removeFile 的 Warning）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-11 加宽 64c 之后：PASS 433→436（+3）

Clocked 一路：模型语境向量化调用的范围切片实参（applySubscript 对 SLICE 下标 → `x[(a:b)[$i1]]`），向量化出来的 previous
不算数组调用、所在等式逐元素展开——FIRbyCoefficients 的 `u_buffer = cat(1, {u}, previous(u_buffer[1:n]))` →
`u_buffer[2] = … previous(u_buffer[1])`。Clocked.Examples.Elementary.RealSignals.UpSample2、FractionalDelay、
Clocked.Examples.Systems.EngineThrottleControl 转 PASS，均与 omc 逐字节一致。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-11 加宽 64d 之后：PASS 436→438（+2）

SVPWM 一路：常量数组在符号下标后跟 `:`（`fire_p = fire[ka + 1, :]`）由所在等式逐元素展开（`fire_p[1] = {{true, false, false}, …}
[ka + 1, 1]`）；另改常量矩阵绑定逐元素丢注释的静默错（`constant Boolean fire[6, m] = […] "Switching patterns"`）。
Electrical.Machines.Examples.InductionMachines.IMC_InverterDrive、Electrical.PowerConverters.Examples.DCAC.PolyphaseTwoLevel.
ThreePhaseTwoLevel_PWM 转 PASS，均与 omc 逐字节一致。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-13 ADR-007 第 2 阶段 65a-1 之后：PASS 438（不变）

inst 进类查名前顺短类链到底（`replaceable package Medium = …` 当路径前缀）。PASS / SKIP 逐例不变；Fluid / Media 的 SKIP 原因
从 "unresolved component type [Medium.BaseProperties]"（34 例）推进到 "inherited class seen through modifier / redeclare"
（Media 包 `extends PartialMedium(…)` 改了常量后继承来的 ThermodynamicState / BaseProperties，32 例）——下一步要按实例化包
（Inst.instPackage）让继承类看到改过的常量。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-13 ADR-007 第 2 阶段 65a-2 之后：PASS 438（不变）

实例树按 OMC 建类（本地类按实例克隆、类重声明每实例一份、继承类取自基类实例），flatten 在实例层认修饰里重声明的包。PASS / SKIP
逐例不变：Fluid / Media 卡在 Medium 包里经 `extends PartialMedium(…)` 改过常量后继承来的类与常量（"inherited class seen through
modifier"），要等包实例（下一步）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-13 ADR-007 第 2 阶段 65a-3 之后：PASS 438（不变）

包实例（Inst.instPackage）：重声明的 Medium 包按实例查常量 / 函数 / 类，函数按实例路径命名。PASS / SKIP 逐例不变；Fluid / Media
的 SKIP 原因从 "inherited class seen through modifier [ThermodynamicState]"（29 例）推进到 "extends modifier names non-component
(nested class)"——Media 包 `extends PartialTwoPhaseMedium(Temperature(min = 273.15, …), …)` 这类在 extends 修饰里改嵌套类型（类修饰），
下一步做。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-14 ADR-007 第 2 阶段 65a-4 之后：PASS 438（不变）

类修饰（Media 包 `extends PartialTwoPhaseMedium(Temperature(min = 273.15, …), …)`）、短类链修饰按写在处求值、多层 `redeclare model
extends BaseProperties` 的重声明链。PASS / SKIP 逐例不变；Fluid / Media 的 SKIP 原因推进到 flatten 里定义层 SCode 查找撞上经
改了常量的 extends 继承来的 `FluidConstants` / `SpecificHeatCapacity`（包函数体 / 记录在定义层的作用域里查名），下一步。FAIL /
ACCEPTS-INVALID / CRASH 0。

## 2026-10-14 ADR-007 第 2 阶段 65a-5 之后：PASS 438（不变）

查名穿过重声明的包时扫 outer 不再下钻包常量的类型。PASS / SKIP 逐例不变；Fluid / Media 的 44 例 SKIP 不再卡在类重声明上，
分散到各自的长尾特性：stateSelect 参数未预扫（11，PumpingSystem 等）、整数组引用 `tank.X_start`（8，HeatingSystem / Tanks）、
非字面量维（8，DrumBoiler 等）、`Medium.singleState` 等包常量引用（5）、`Medium.specificHeatCapacityCp` 等函数（5，记录类型
按实例路径命名 / 函数的 class extends）、带修饰的短类包（3，TraceSubstances）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-14 66a 之后：PASS 438（不变）

Medium 包常量的长尾：class extends 修饰里的 stateSelect 预扫、常量数组空切片、`:` 维常量的 `size`（绑定被 extends 修饰改过）、
String / Boolean 数组常量、空数组常量。PASS / SKIP 逐例不变；Fluid / Media 的 44 例 SKIP 越过 `reference_X` / `substanceNames` /
`extraPropertiesNames` 等常量，现停在：限定引用 11（`fluidConstants.molarMass` 记录数组常量、`Medium.singleState`、`Medium.h_default`
函数求值等）、限定函数调用 8（`Medium.density` 等经重声明的包的函数）、复合数组元素引用 4（`ports.C_outflow`）、未解析名 4（`MMX`）、
stateSelect 值 3、带修饰的短类包 3、函数体内的 Medium 函数 / 类型 7 等。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-14 66b 之后：PASS 438（不变）

记录常量的继承字段（`fluidConstants[1].molarMass`）、String(Real) 的三位指数（Windows 版 omc）、stateSelect 经声明在后的
inner 参数（ThreeTanks / EmptyTanks / OneTank）。PASS / SKIP 逐例不变；Fluid / Media 的 SKIP 前移到 Medium 函数的发射
（`saturationTemperature` / `bubbleEnthalpy` / `specificEnthalpy` / `temperature`，以及经重声明的包调用的限定函数 8 例）与
复合数组（`ports.C_outflow` 4、`tank.topPorts` / `tank1.ports` 修饰分发 2、非 parts 类型的复合数组 2）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-14 66c 之后：PASS 438（不变）

Medium 函数 / 记录的 class extends（`redeclare function extends`、`redeclare record extends ThermodynamicState`）、包实例里的记录
按实例路径命名、函数变量类型的 extends 类修饰。PASS / SKIP 逐例不变；Fluid / Media 里调 Medium 函数的示例越过函数解析，8 例停在
定义层继承守卫（`FixedPhase` 6、`AbsolutePressure` 2：class extends 记录 / 函数的隐式基类按定义层解析，经改过修饰的 extends
继承来的名字被拦），其余停在复合数组、`Medium.singleState` 等。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-14 66d 之后：PASS 438（不变）

class extends 记录继承来的字段改按实例层的隐式基类解析（含编译期函数求值）。PASS / SKIP 逐例不变；Fluid 里经 Medium 函数的
示例全部越过 Medium 层，SKIP 收拢到端口数组（`ports.C_outflow` 复合数组元素引用 7、端口数组修饰分发 3、非 parts 类型复合数组 2、
stream 算子下标 2）；Media 的余项是 Medium 函数里的非本地引用 / 限定函数调用（`Medium.setState_pT` 等）与带修饰的短类包。
FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-14 66e 之后：PASS 438（不变）

Fluid 端口数组的空成员（`Xi_outflow` / `C_outflow` 对水是空数组、`nPorts = 0`）：复合数组整体引用、fill、连接与 cardinality 都照
omc 丢掉空的部分。PASS / SKIP 逐例不变；Fluid 的 SKIP 前移到端口数组的修饰分发（`tank.topPorts` 等 5）、非 parts 类型的复合数组
（4）、stream 算子带下标（4）、向量化调用实参（2）等。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-14 66f 之后：PASS 438（不变）

端口数组上不带 each 的类重声明、class extends 复合数组（`Medium.BaseProperties[n] mediums`）、空 stream 数组成员进方程、
`m_flow * actualStream(h)` 包 smooth。PASS / SKIP 逐例不变；Fluid 的 SKIP 前移到其余形态的空数组下标引用（6）、向量化调用实参（4）、
管道摩擦函数里嵌套函数的压平名（`interpolateInRegion2`，2）、类重声明的函数（`flowCharacteristic`，2）等。
FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-14 66g 之后：PASS 439（+1，Modelica.Fluid 首个）

空数组的下标向量 / 范围切片（`C_start[1:Medium.nC]`）。**Modelica.Fluid.Examples.ControlledTankSystem.ControlledTanks 转 PASS**——
Fluid 的第一个：从 65a（实例树查找）到 66g 一路越过 Medium 包常量、class extends 的函数 / 记录、端口数组的空成员、stream 算子。
Fluid 余下 22 例 SKIP：管道摩擦函数里嵌套函数的压平名（`interpolateInRegion2`，7）、向量化调用实参（5）、类重声明的函数
（`flowCharacteristic`，2）等；Media 21 例停在 Medium 函数里的非本地引用 / 限定函数调用等。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-14 66h 之后：PASS 439（不变）

函数里的嵌套函数按外层函数的压平名命名、点分名访问 protected 元素照 omc 拒绝、继承来的 outer（`system`）在条件里的两处误拦。
PASS / SKIP 逐例不变；Fluid / Media 44 例里 43 例 SKIP，头号原因换成 Medium 函数体里的非本地引用（12）、限定函数调用（7）、
向量化调用实参（5）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-14 66i 之后：PASS 445（+6，Fluid 7/23）

继承来的函数成员（输入缺省 `crossArea = pi*diameter^2/4`）在声明它的函数的作用域里解析（import 不继承）。转 PASS：
Modelica.Fluid.Examples.Tanks.ThreeTanks、Tanks.EmptyTanks、AST_BatchPlant.Test.TwoTanks、TankWithEmptyingPipe2、
TanksWithEmptyingPipe1、TanksWithEmptyingPipe2（连同 66g 的 ControlledTanks，Fluid 7/23，均与 omc 1.25.0 逐字节一致）。
Fluid 余下 16 例：向量化调用实参（5）、类重声明的函数 `flowCharacteristic`（3）、带修饰的短类包（2）等；Media 21 例停在 Medium
函数的限定调用（7）、函数体非本地引用（`data.MM`、`Constants.Tred`、`R134aData.data.FPCRIT`）等。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-14 66j 之后：PASS 449（+4，Fluid 11/23）

向量化内建的零长数组实参与复合数组字段实参（PartialFlowSource 的 `assert(abs(sum(abs(ports.m_flow)) - max(abs(ports.m_flow))) <= …)`）。
转 PASS：Modelica.Fluid.Examples.DrumBoiler.DrumBoiler、AST_BatchPlant.Test.OneTank、AST_BatchPlant.Test.TankWithEmptyingPipe1、
Explanatory.MeasuringTemperature。Fluid 余下 12 例：类重声明的函数 `flowCharacteristic`（3）、更高变率的绑定（2）、带修饰的短类包（2）、
`Medium.singleState`、`MMX`、`reference_h`、derivative 注解引用的函数、stateSelect 预扫各 1。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-15 66k 之后：PASS 450（+1，Fluid 12/23）

组件 / extends 修饰里重声明的函数（`redeclare function flowCharacteristic = quadraticFlow(V_flow_nominal = …)`）按实例路径命名、
短类修饰成输入缺省；连带：`ones` / `fill` 列的矩阵构造、形参维引用自己（Matrices.solve）、去重后的 `outer system`、按包实例的记录
构造函数、input 记录实参不求值。转 PASS：Modelica.Fluid.Examples.InverseParameterization。另两例过了函数重声明、卡在后面：
PumpingSystem——常量实参调用 `flowCharacteristic(0)` 要经 Matrices.solve → LAPACK dgesv 求值（omc 打 100.0）；
AST_BatchPlant.BatchPlant_StandardWater——数组推导式的范围（comprehension range，同 NonCircularPipes）。HeatExchangerSimulation
卡 shadowed reduction iterator。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-15 66l 之后：PASS 450（不变）

空范围的推导式、der 的零长数组实参、组件修饰里推导式的范围、派生标量输入的向量化判定。"comprehension range" 的 4 例越过此处、
卡到下一处：NonCircularPipes——整个结构参数数组的引用；AST_BatchPlant.BatchPlant_StandardWater——MultiPort 的
`sum({positiveMax(ports_b[j].m_flow) * inStream(ports_b[j].h_outflow) for j in 1:nPorts_b})`（omc 按连接集展开成和式）；
两个 IMC_withLosses——`wtable = from_rpm({…})` 向量化调用求值后 omc 照打函数定义（V4g 规矩，未做）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-15 66m 之后：PASS 452（+2，Fluid 14/23）

推导式里的流算子先展开再求、结构参数数组以迭代变量下标引用时代入整个数组（元素全相同取一个）、sum 链代入后全字面量求值、
调用的空切片实参换成 `{}`。转 PASS：Modelica.Fluid.Examples.NonCircularPipes、AST_BatchPlant.BatchPlant_StandardWater。
Fluid 余 9 例：PumpingSystem（LAPACK dgesv 求值）、HeatExchangerSimulation（shadowed reduction iterator）、RoomCO2 ×2（带修饰的
短类包）、IncompressibleFluidNetwork（Medium.singleState）、BranchingDynamicPipes（MMX）、HeatingSystem（reference_h）、
TanksWithOverflow（derivative 注解）、MomentumBalanceFittings（stateSelect 预扫）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-15 66n 之后：PASS 453（Fluid 15/23）

带修饰的短类包、经包实例取记录常量的字段、记录常量的数组字段与自引用字段、derivative 注解函数的实例路径命名、函数里的 cat、
归约迭代变量遮住函数局部。转 PASS：Modelica.Fluid.Examples.Tanks.TanksWithOverflow。MoistAir 一族（RoomCO2 ×2、
BranchingDynamicPipes、Media.Examples.MoistAir）卡到 "function body references non-local [steam]"；HeatExchangerSimulation 卡在
向量化 homotopy（嵌套向量化的打印形态）；SimpleLiquidWater 卡在短类包的类修饰（`SpecificEnthalpy(max = 1e6)`）。FAIL / ACCEPTS-INVALID /
CRASH 0。

## 2026-10-15 66o 之后：PASS 453（不变）

函数体里引用包常量（记录字段、整个记录常量 → 构造式、记录数组取字段、带下标、继承来的成员）。原先卡在
"function body references non-local" 的 9 例越过此处、卡到下一处：MoistAir 一族（Media.Examples.MoistAir、RoomCO2 ×2、
BranchingDynamicPipes）与 ReferenceAir ×2——"named function argument [missing argument / default value]"；MixtureGases——
`Medium1.mediumName`；R134a ×2——经带修饰 extends 继承的类型 `Density`。HeatingSystem 卡在经短类包调函数（另做）。修了函数里
定长输入上的归约没展开的静默错（PKT17）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-15 66p 之后：PASS 455（+2）

函数型实参与部分函数应用（ADR-009）。转 PASS：Modelica.Media.Examples.SolveOneNonlinearEquation.Inverse_sine、
Modelica.Math.Nonlinear.Examples.QuadratureLobatto3。MoistAir 一族越过部分应用、卡到下一处：Media.Examples.MoistAir——模型语境的
`Medium.dryair.MM`；RoomCO2 ×2、BranchingDynamicPipes——stream connector；ReferenceAir.MoistAir / MoistAir2——函数里记录局部的
绑定（Ice09 Gibbs 的 `t`）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-16 66q 之后：PASS 460（+5）

模型里不带修饰的本地短类包（`package Medium = …`）经包实例查名、按实例路径命名。转 PASS：Media.Examples.IdealGasH2O、
WaterIF97、PsychrometricData、MoistAir、SolveOneNonlinearEquation.Inverse_sh_TX。同簇余下 7 例卡到下一处：Inverse_sh_T——包常量
记录作部分应用实参（PKZ23）；MixtureGases——经 extends 修饰重声明的 FluidConstants；InverseIncompressible_sh_T、
Fluid.Examples.IncompressibleFluidNetwork——编译期多项式拟合（Poly.fitting）；ReferenceAir.Inverse_sh_T / Inverse_sh_TX /
DryAir1——带修饰 extends 的包要定义层 instPackage、h_default 经 Helmholtz 状态方程求值。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-16 66r 之后：PASS 463（+3）

记录整体绑定：构造调用里 `:` 维字段的维由值推出；final 连续记录的调用绑定照 omc 强制内联化简前的函数体。转 PASS：
Magnetic.FluxTubes.Examples.Hysteresis.SinglePhaseTransformerWithHysteresis1、Mechanics.MultiBody.Examples.Elementary.
RollingWheelSetDriving / RollingWheelSetPulling。同簇卡到下一处：FluxTubes HysteresisModelComparison——when 里的 if 方程；
MultiBody Elementary.Surfaces——matrix element shape。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-16 66s 之后：PASS 467（+4）

向量化调用在标量化时逐元素求值、函数定义照 omc 的收函数时机去留；修饰分发里取负 / 四则运算中的数组调用。转 PASS：
Electrical.Machines.Examples.InductionMachines.IMC_withLosses、Magnetic.FundamentalWave.Examples.BasicMachines.InductionMachines.
IMC_withLosses、Electrical.PowerConverters.Examples.DCAC.PolyphaseTwoLevel.PolyphaseTwoLevel_R / _RL。Magnetic.QuasiStatic 的
IMC_withLosses 卡到下一处：MultiDelta 的 `when initial() then if … then print(…); end if; end when;`（omc 打成空的 when）。
FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-16 66t 之后：PASS 468（+1）

when 体里的 if 方程照顶层压平（结构条件选支）；书写的推导式里的用户函数调用照 omc 的收函数时机留函数定义（修一串语料外的静默错）。
转 PASS：Magnetic.QuasiStatic.FundamentalWave.Examples.BasicMachines.InductionMachines.IMC_withLosses。FluxTubes Hysteresis.
HysteresisModelComparison 越过 when 里的 if 方程与切片界里的 `end`，卡到下一处：when 里带 cat 支的数组 if 式。
FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-16 66u 之后：PASS 472（+4）

结构记录参数绑定到声明在后的记录（第一遍推后、第二遍再求）；函数求值出的记录里没赋到的字段照 EMPTY_BINDING 打（fixed = false）；
首输出是记录的多输出调用。转 PASS：Electrical.Spice3.Examples.Spice3BenchmarkDifferentialPair、Spice3BenchmarkMosfetCharacterization、
Spice3BenchmarkRtlInverter、Spice3BenchmarkFourBitBinaryAdder。Spice3 Graetz 卡到下一处：记录类型的算法赋值目标（R_SEMI 的
`vp := resistorInitEquations(…)`）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-17 66v 之后：PASS 478（+6）

定义层的包经带修饰的 extends / 短类时按包实例查名、函数 / 记录按访问路径命名；短类的类修饰；括号单元素的赋值目标；integer 的实参
提 Real。转 PASS：Media.Examples.R134a.R134a1 / R134a2、SimpleLiquidWater、ReferenceAir.DryAir1 / DryAir2 / Inverse_sh_T。卡到下一处：
ReferenceAir.MoistAir / MoistAir1 / MoistAir2 / Inverse_sh_TX——Ice09 Gibbs 里带绑定的 Complex 记录数组局部；TwoPhaseWater.
TestTwoPhaseStates——extends 短类后的 class extends；MixtureGases——extends 修饰里重声明的记录类型。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-17 66x 之后：PASS 482（+4）

函数变量的记录（数组）绑定照打并参与求值；多输出常量调用取首输出；函数体里带字面量修饰的函数别名；包实例里 class extends 函数的
隐式基类。转 PASS：Media.Examples.ReferenceAir.MoistAir / MoistAir1 / MoistAir2 / Inverse_sh_TX。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-17 66y 之后：PASS 482（不变）

非空的 stream 数组成员（逐元素成 stream 集，inStream / actualStream 的数组实参逐元素展开）。Fluid.Examples.BranchingDynamicPipes、
TraceSubstances.RoomCO2 / RoomCO2WithControls 越过 "stream connector"，卡到下一处：管流模型的 `homotopy(array(…))` 向量化实参；
FixedBoundary.d 两支调用的求值时机（omc 先代入 Evaluate = true 的 use_T 再化简）。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-17 66z 之后：PASS 482（不变）

OpenModelica.Scripting.uriToFilename 的编译期求值（照 omc 运行时 utility.c 的路径规则）。Electrical.Digital.Examples.RAM、
Utilities.Examples.ReadRealMatrixFromFile / readRealParameterModel 越过它，求得的文件路径与 omc 一致，卡到下一处：函数体里的枚举数组
包常量与枚举输入（RAM）、经 FFI 读 MAT 文件求维（ReadRealMatrixFromFile）、记录构造函数的枚举字段（readRealParameterModel）。
FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-17 67a 之后：PASS 484（+2）

函数与记录里的枚举（枚举类型的函数变量 / 记录字段打字面量表、函数体里以枚举为维的包常量表、枚举实参的常量调用）；多输出调用取首输出时
调用者的纯度。转 PASS：Electrical.Digital.Examples.RAM、Utilities.Examples.readRealParameterModel。FAIL / ACCEPTS-INVALID / CRASH 0。

## 2026-10-18 67b 之后：PASS 487（+3）

返回记录的向量化调用作重载运算符的运算数随即展开、逐元素化简（SingleToPolyphase 的 `u * fromPolar(fill(1, m), -symmetricOrientation(m))`）。
转 PASS：Magnetic.QuasiStatic.FundamentalWave.Examples.BasicMachines.SynchronousMachines.SMPM_MTPA / SMPM_CurrentSource /
SMR_CurrentSource。FAIL / ACCEPTS-INVALID / CRASH 0。
