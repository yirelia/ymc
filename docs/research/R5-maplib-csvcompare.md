# R5：MAP-LIB 参考结果与 csv-compare 工具（联网调研）

> 调研日期 2026-09-14。硬规则：每条结论均来自实抓网页/原始文件并附 URL（工具 WebFetch/WebSearch）。

## 结论摘要

- MSL 4.0.0 官方参考结果在 `modelica/MAP-LIB_ReferenceResults` 仓库的 **`v4.0.0` 分支**（另有 `v4.1.0`、`v3.2.3` 等版本分支；默认分支 `master` 只存搬迁说明）。该仓库已于 **2026-09-11 归档只读**，结果已迁入 `modelica/ModelicaStandardLibrary` 的 `ReferenceResults/` 目录。
- 目录布局：根 = `Modelica/`、`ModelicaTest/` + `.gitattributes` + `README.md`；`Modelica/` 下按 MSL 包名分 12 个子目录，逐级把点分模型名映射为文件夹路径，最末层每模型一个文件夹，内含 `<Model>.csv` + `comparisonSignals.txt` + `creation.txt` + 若干 `*_passed.log`。
- CSV 格式：首列 `"time"`，各列为**双引号包裹的点分全路径信号名**，数组元素用**方括号** `r_0[1]`（下标从 1），逗号分隔；抽样文件**未见 der()**、无单位行。CSV 只含 `comparisonSignals.txt` 列出的“待比信号”（主库每模型少量标量曲线）。
- 生成工具（原句）：“They have been generated with **Dymola 2020x** and **MSL v4.0.0-rc.1**.” 单模型 `creation.txt` 记 “Dymola Version 2020x (64-bit)”，Dassl、tol=1e-6、stopTime=4、interval=0.0008。
- csv-compare 判据 = **tube（管道）算法**；`--tolerance` = 管道在 x 方向不连续处的半宽，**默认 0.002**（源码 `DefaultValue = "0.002"`）；输出 HTML+SVG 报告；返回码 0/1/2；工具本身 BSD-3-Clause。
- 体量/许可证：MAP-LIB README **未给克隆体量**；API `size = 1,818,473 KB`（≈1.73 GiB，git 仓库体量，.mat 走 Git LFS 则实际更大）。MAP-LIB **无 LICENSE**（API `license: null`，根无 LICENSE 文件）；csv-compare = BSD-3-Clause。

## 逐项事实（带 URL）

### 1. 仓库分支/目录布局与 CSV 格式细节

**分支**（`https://api.github.com/repos/modelica/MAP-LIB_ReferenceResults/branches?per_page=100`）：`master`、`v3.2.3-restructured-for-v4.0.0`、`v3.2.3+build.4`、`v3.2.3`、`v4.0.0`、`v4.1.0` → MSL 4.0.0 对应 **`v4.0.0` 分支**。

**归档/搬迁**（`https://github.com/modelica/MAP-LIB_ReferenceResults` + `https://api.github.com/repos/modelica/MAP-LIB_ReferenceResults`）：主页 “This repository was archived by the owner on Sep 11, 2026. It is now read-only.” 与 “Reference results are moved into the ModelicaStandardLibrary”。新址已核实存在 `ReferenceResults/{Modelica,ModelicaTest}`（`https://api.github.com/repos/modelica/ModelicaStandardLibrary/contents/ReferenceResults?ref=master`）。

**根目录**（`https://github.com/modelica/MAP-LIB_ReferenceResults/tree/v4.0.0`）：`Modelica/`、`ModelicaTest/`、`.gitattributes`、`README.md`。README 仅 131 字节（`.../v4.0.0/README.md`），正文只一句 “Reference results of the Modelica libraries maintained by the Modelica Association Project 'Libraries'”，**不含任何格式/工具说明**。

**Modelica/ 下 12 子目录**（`.../contents/Modelica?ref=v4.0.0`）：Blocks, Clocked, ComplexBlocks, Electrical, Fluid, Magnetic, Math, Mechanics, Media, StateGraph, Thermal, Utilities。逐级深入如 `Blocks/ → Examples/ → PID_Controller/`，即点分名 `Modelica.Blocks.Examples.PID_Controller` 映射为路径。ModelicaTest 顶层同为 12 域（`.../contents/ModelicaTest?ref=v4.0.0`）。

**单模型文件夹内容**（`.../contents/Modelica/Blocks/Examples/PID_Controller?ref=v4.0.0`）：`PID_Controller.csv`(662,535 B)、`comparisonSignals.txt`(77 B)、`creation.txt`(1,571 B)、`check_passed.log`、`compare_passed.log`、`simulate_passed.log`、`translate_passed.log`。**该文件夹只见 .csv，未见 .mat**（存疑见下）。

**CSV 头两行**（`.../v4.0.0/Modelica/Blocks/Examples/PID_Controller/PID_Controller.csv`）：
- 行1 头：`"time","PI.I.y","inertia1.phi","inertia1.w","integrator.y","spring.phi_rel","spring.w_rel"`
- 行2 数据：`0,-0.10000000000000001,0,0,0,0.001,0`
→ 时间列名 `time`（小写、带引号）；信号名为双引号点分全路径；逗号分隔。

**数组下标 = 方括号**（`.../v4.0.0/Modelica/Mechanics/MultiBody/Examples/Elementary/FreeBody/comparisonSignals.txt`）：
```
time
body.body.frame_a.r_0[1]
body.body.frame_a.r_0[2]
body.body.frame_a.r_0[3]
body.body.w_a[1] ... body.v_0[3]
```
→ 数组写作 `x[i]`（**非** `x_1_`），下标从 1；无 der()。

**“待比信号”即 CSV 列**：`comparisonSignals.txt`（PID：`time,PI.I.y,inertia1.phi,inertia1.w,integrator.y,spring.phi_rel,spring.w_rel`）与 CSV 表头逐列一致 → CSV 仅存该清单中的信号。主库抽样每模型 4–9 条标量曲线（PID 6 条；DoublePendulum `revolute1/2.phi/w`；MixtureGases `medium1/2.T/p`）。

### 2. 参考结果由哪个工具产生（原句）+ 对应 MSL 版本

MSL wiki “Reference results”（`https://github.com/modelica/ModelicaStandardLibrary/wiki/Reference-results`）原句：
> “Reference results for Modelica and ModelicaTest are available from the v4.0.0 branch of the repository MAP-LIB_ReferenceResults. They have been generated with **Dymola 2020x** and **MSL v4.0.0-rc.1**.”

（同页：v3.2.3 用 Dymola 2019 + MSL v3.2.3-rc.2；v3.2.1/3.2.2 用 Dymola 2014 FD01 / 2017 Dev 4。）

单模型 `creation.txt`（`.../v4.0.0/Modelica/Blocks/Examples/PID_Controller/creation.txt`）：工具 “Dymola Version 2020x (64-bit), 2019-10-10”；生成时间 “2020-05-05T00:27:15Z”；testDescription 注明 MSL v4.0.0-rc.1；求解器 Dassl、tolerance 1e-06、startTime 0、stopTime 4、interval 0.0008（≈5000 点，等距）。

→ 结论：MSL 4.0.0 参考结果由 **Dymola 2020x** 对 **MSL v4.0.0-rc.1** 仿真产生。

### 3. csv-compare 判据算法与默认容差（原句）

工具 `modelica-tools/csv-compare`（README：`https://github.com/modelica-tools/csv-compare/blob/master/README.md`），MA 官方回归比对工具。
- 算法（原句）：“The files are not compared 1:1 but **a tube is generated around the curve** and the values are checked to be inside the tube.”
- 容差（原句）：“The tolerance is a double value setting the **width of the tube at discontinuity in x-direction**.” 经 `--tolerance` 设定。
- **默认值 0.002**（源码 `Modelica_ResultCompare/Options.cs`，`https://raw.githubusercontent.com/modelica-tools/csv-compare/master/Modelica_ResultCompare/Options.cs`）：
  `[Option('t', "tolerance", Required = false, DefaultValue = "0.002", HelpText = "Set the width of the tube at discontinuity in x-direction [Default is 0.002].")]`
- tube 几何（源码 `CurveCompare/TubeSize.cs`，`https://raw.githubusercontent.com/modelica-tools/csv-compare/master/Modelica_ResultCompare/CurveCompare/TubeSize.cs`）：tube 为沿曲线滑动的矩形，半宽 X、半高 Y；`baseX = ref.X.max−ref.X.min`，`baseY = max(ref.Y.max−ref.Y.min, nominalValue)`，`ratio = baseY/baseX`；相对容差下 `x = tolerance·baseX`、`y = ratio·x` → 容差是相对参考曲线 x 跨度的比例（x 半宽 = tolerance×(t_max−t_min)）。
- CLI（README）：`compare.exe --mode csvTreeCompare --reportdir "..." --tolerance 4e-3 <testDir> <baseDir>`；模式 `csvFileCompare`/`csvTreeCompare`/`FmuChecker`；参数支持 `--a=v`/`--a v`/`-a=v`/`-a v`。
- 输出：HTML 报告，含 “scalable vector graphics of all results that are found in both files”（结果多时报告可能过大难浏览）。返回码：0=全部有效、1=有无效结果、2=异常/错误。
- MSL 回归实践（wiki “Regression testing…”，`https://github.com/modelica/ModelicaStandardLibrary/wiki/Regression-testing-of-Modelica-and-ModelicaTest`）：`Compare.exe --mode csvTreeCompare --override --bitmap --tolerance 2e-3`；主库每模型只比 2 条信号（全程），ModelicaTest 比全部信号（短时窗）。

### 4. 克隆体量与许可证

- 体量：MAP-LIB README **未给**。API（`https://api.github.com/repos/modelica/MAP-LIB_ReferenceResults`）`size = 1,818,473 KB`（≈1.73 GiB，含全历史/分支的 git 体量；.mat 若走 LFS，完整 checkout 更大）。csv-compare `size = 781 KB`。
- 许可证：MAP-LIB **无**（API `license: null`；根目录无 LICENSE 文件）→ 复用/再分发前须向 Modelica Association 确认授权。csv-compare = **BSD-3-Clause**（`https://api.github.com/repos/modelica-tools/csv-compare`）。

## 抓取失败或未找到

- 首次抓 `https://github.com/modelica/MAP-LIB_ReferenceResults` 报 “Socket is closed”，重试成功；其余 URL 均一次或重试后成功。
- **.mat 参考结果**：v4.0.0 抽样模型（PID_Controller）文件夹只见 `.csv` 未见 `.mat`；MSL wiki 称 MAT+CSV 且 MAT 走 Git LFS，二者出入未在该分支证实 → 建议本地克隆 v4.0.0 后 `ls` 确认是否另有 .mat / LFS 指针。
- **der() 是否出现**：抽样 4 个 `comparisonSignals.txt`（PID_Controller / DoublePendulum / MixtureGases / FreeBody）均无 der()；不能据此断言全库无 der()，仅能说主库待比信号以状态/输出标量与数组元素为主。
- **tube 判定逻辑细节**：README 无公式；几何仅从 `TubeSize.cs` 归纳，未逐行通读 `Tube.cs` 的落点判定与不连续处处理。
- 建议：ymc 落地比对时直接**本地克隆 `MAP-LIB_ReferenceResults` 的 `v4.0.0` 分支**（或改用 MSL 仓库 `ReferenceResults/`），按上述格式解析 CSV + `comparisonSignals.txt`，并对齐 csv-compare 的 tube 判据（默认 tol=0.002，相对 x 跨度）。
