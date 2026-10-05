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
