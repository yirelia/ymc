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
