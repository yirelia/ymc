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
