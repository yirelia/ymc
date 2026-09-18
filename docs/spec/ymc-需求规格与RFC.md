# Spec: ymc 自研 Modelica 编译器需求规格与 RFC（增量）

- 状态：草案 v0.1（待用户确认）
- 日期：2026-09-14
- 关系：本文是《自研编译器技术方案-C++.md》（下称"方案"）的**增量**。方案已定 S0–S8 路线、L1/L2/L3 三级验收、工程规范、协作分工；本文只补方案没有的部分——**三方（ymc / OMC / Dymola）结果对比体系**与**每阶段可执行的验收门**，并把方案 §6/§8 的若干占位条目落成具体接口。凡方案已写清的内容，本文用「见方案 §x.y」引用，不复述。
- 术语：oracle = 参考实现（OMC、Dymola）；语料代号 C-OMC-TS / C-MSL-EX / C-MAPLIB / C-PROBE（见 §7.1）；三道验收门 G1/G2/G3（见 §8）。
- 事实来源：本文对 OMC/Dymola 行为的每条陈述都带「文件:行」锚点或标「待探针验证」。研究底稿在 `scratchpad/R1..R7-*.md`，其锚点已核到 `C:\dev\OpenModelica`（基线 `3ceb0c9d06`）与 Dymola 2025x Refresh 1 安装目录；引用格式为 `R1§9` 表示研究底稿 R1 第 9 条。

---

## 1. 背景与目标

### 1.1 背景

方案已经把"自研编译器怎么造"写清楚了（九阶段、文本 IR 隔离、OMC 当参考实现）。但方案里对"怎么证明每一阶段做对了"只给了级别定义（L1 文本、L2 数值、L3 脚本，见方案 §1.3）和零散的 oracle 工具设想（方案 §6），存在三个缺口：

1. **没有可执行的验收门**。方案说"某子集 L1 全 PASS"，但没有一条能直接跑出 PASS/FAIL/SKIP 计数的命令，也没有定义通过阈值、失败如何归档。
2. **Dymola 只作为"能力对标"出现，没有对比基础设施**。方案全篇（及设计总览难点账本 B3/B6）把 Dymola 当作能力上限来提，但从未定义 ymc 结果如何真的与 Dymola 仿真结果做比对。用户本次的核心诉求正是补上这一条。
3. **数值/结构比对的判据未固定**。方案 §6.4 只说"容差对齐 diffSimulationResults"，但没写清 OMC 那套 tube 算法的真实语义（研究发现默认容差由 `rangeDelta` 主导而非 `relTol`，见 R1§3），也没定义与 Dymola 比时用哪套判据。

### 1.2 业务目标

- **B-G1**：把方案的 L1/L2/L3 验收变成可一键运行、可量化、可归档的三方对比体系，让"某阶段是否做对"有客观、可复现的判定。
- **B-G2**：所有仿真类结果同时能与 OMC 和 Dymola 对比；不在对比范围内的用例显式计数（skip + 原因码），不悄悄漏掉（呼应方案约束 3）。
- **B-G3**：对比先于开发就位——先有 oracle 与差分工具，用户手写核心时每一步都能立即验证（呼应设计总览难点 E1"没有测试就是盲飞"）。

### 1.3 用户/涉众目标

- **用户（手写核心 + 读任务卡）**：每写完一个 pass/子功能，跑一条命令就能看到"与 OMC/Dymola 差在哪一步、哪个变量、哪条方程"，并据此继续或回退。
- **Claude（写基建/工具/任务卡/评审）**：本文第 9 节 RFC 就是 Claude 接下来要实现的工程清单；核心编译器代码仍由用户手写（见方案 §7.2，本文 §7.2 复述边界）。
- **间接涉众**：OMC（唯一 testsuite oracle 与运行时借用方）、Dymola（MSL/MAP-LIB 数值真值）、MLS 3.6（语义仲裁的最终依据）。

---

## 2. 需求类型总览

| 类型 | 是否适用 | 证据来源 |
|---|---|---|
| 业务需求 | 是 | 用户任务描述（"所有结果需与 OMC 和 Dymola 仿真结果对比"）、§1.2 |
| 用户/涉众需求 | 是 | 用户 2026-09-14 的四条决定、§1.3 |
| 解决方案需求 | 是 | 本文 §9 RFC；方案 §2/§6 |
| 功能需求 | 是 | §3（FR-0xx…FR-6xx） |
| 非功能需求 | 是 | §4（确定性、超时、性能、可复现、平台、许可证依赖、数据体量） |
| 外部接口需求 | 是 | §5（运行器/比对器/Dymola 驱动 CLI、报告与台账文件格式、与 OMC/Dymola 的系统接口） |
| 过渡需求 | 是 | §6（首次提交、方案条目覆盖、open-questions 建立、MAP-LIB 克隆、版本映射） |

---

## 3. 功能需求

优先级用 MoSCoW（Must/Should/Could）。分组：FR-0xx 运行器与 OMC 对齐；FR-1xx 数值与结构比对器；FR-2xx Dymola 驱动；FR-3xx 参考结果管理；FR-4xx 报告与台账；FR-5xx 分阶段验收门；FR-6xx 开发流程与任务卡。

### FR-001: testsuite 运行器复刻 rtest 协议

- **描述**：系统必须提供 `tests/omc/run.py`，逐条复刻 OMC `testsuite/rtest` 的用例执行协议：解析测试文件头部 `// name/keywords/status/cflags/depends/env/setup_command/teardown_command` 指令；`.mo` 与 `.mos` 用同一条命令行构造（二者只在比较容差上不同）；提取 `// Result:`…`// endResult` 期望块；对实际输出做同样的归一化（去行首空白、首段连续空白折一个空格、跳过 `GC Warning:` 等噪声行）；用 FR-002 的 token 比对判定。
- **验收标准**：
  - `python tests/omc/run.py --tool=omc --dir C:/dev/OpenModelica/testsuite/flattening/modelica/scoping` 对 OMC 自身运行，输出该目录全部用例 PASS（自检门，证明运行器协议正确；见 FR-060）。
  - 头部解析单测：喂入含全部 8 种指令的样例文件，断言解析出的字段与手写期望一致。
  - 归一化单测：喂入含 `GC Warning:` 行、行首制表符、UCRT 临时路径段的原始输出，断言归一化结果与 rtest 规则一致（rtest:296-316，见 R2§A）。
- **优先级**：Must
- **类型映射**：功能/解决方案
- **来源**：R2§A（rtest:266 命令行、:363-389 期望提取、:280-293 status 语义）；用户决定 4

### FR-002: omc-diff token 级比对器

- **描述**：系统必须提供 `tests/omc/omc_diff.py`，复刻 `testsuite/difftool/omc-diff.l` 的词法级模糊比对：把两侧文本切成 `T_REAL / T_TEXT / T_NEWLINE` token；token 类型不同即失败；纯整数 token 精确相等；实数 token 判据为 `|actual − expected| > eps·|actual| + eps` 才算失败（相对+绝对混合，同一个 eps）；文本 token 折叠连续空白后逐字符比较。默认 eps：`.mo` 用 `1e-7`，`.mos` 用 `5e-3`。
- **验收标准**：
  - 对拍：对 10 组已知 (expected, actual, eps) 三元组，`omc_diff.py` 的判定与本机 `C:/dev/OpenModelica/build_cmake/install_cmake/bin/omc-diff.exe`（或安装版）逐组一致（PASS/FAIL 与首个差异位置）。
  - 边界单测：`1.0000001` vs `1.0` 在 eps=1e-7 判 PASS、在更小 eps 判 FAIL；整数 `10` vs `10.0` 的 token 类型差异行为与 omc-diff 一致。
- **优先级**：Must
- **类型映射**：功能
- **来源**：R2§B（omc-diff.l:113-133 实数判据、:48-68 文本比较）；R2§结论 3

### FR-003: cflags 到 ymc flag 的映射表

- **描述**：系统必须维护 `tests/omc/cflags_map.json`，把用例 `// cflags:` 里的 OMC flag 映射到 ymc 的等价 flag 或映射到"跳过"。映射不到的 flag → 该用例标 SKIP 并记原因码（见 FR-040）。`-d=-newInst`（关新前端，2000 个用例）默认映射为 `skip:old-frontend`（这些用例钉在 OMC 旧前端，文本不可比，见方案 §1.2、R2§结论5）；`+simCodeTarget=Cpp`（256 个）映射为 `skip:cpp-runtime`；`--newBackend` 映射为 ymc 默认（ymc 只有一套新语义）。
- **验收标准**：
  - `python tests/omc/run.py --print-cflags-histogram --dir <testsuite根>` 输出全量 cflags 直方图，且每个 flag 都在 `cflags_map.json` 中有条目（passthrough / rename / skip:<原因码> 三选一），无"未分类"项。
  - 期望直方图与 R2§D 一致（`-d=-newInst 2000`、`+simCodeTarget=Cpp 256`、`-d=newInst 239`、`--newBackend 68` 等）。
- **优先级**：Must
- **类型映射**：功能/过渡
- **来源**：R2§D（cflags 取值分布）、方案 §1.2/§1.4

### FR-004: 单一命令行运行 ymc 或 OMC，产出统一结果对象

- **描述**：运行器必须能用 `--tool=ymc|omc` 切换被测对象，对同一用例分别产出结构一致的结果记录（tool、用例路径、退出码、耗时、归一化 stdout、PASS/FAIL/SKIP、原因），供 FR-040 汇总与 FR-110 三方差分。运行 OMC 时必须支持 `--omc=installed|workspace`；workspace 版（源码 HEAD）对 index-3+StateSelect 模型会死循环，必须强制带超时（见 NFR-002）。
- **验收标准**：`python tests/omc/run.py --tool=omc --omc=installed --dir <某目录> --json out.json` 产出的 `out.json` 每条记录含上述字段；对同一目录 `--tool=ymc` 产出同 schema（ymc 未实现的阶段允许全部 SKIP:not-implemented）。
- **优先级**：Must
- **类型映射**：功能
- **来源**：R2§A（rtest:266）、方案 §2.3；用户决定关于工作区 omc 死循环（CLAUDE.md）

### FR-010: 结果文件读取器（.mat / .csv）

- **描述**：系统必须提供 `tools/resultio.py`，读取仿真结果文件：MATLAB v4 `.mat`（OMC 与 Dymola 通用格式）与 `.csv`；能列出变量名、按变量名取时间序列、取终值。`.mat.xz` 先用外部 `xz --keep --decompress` 解压再读（与 ModelTesting.mos:157 一致）。读取语义须与 OMC 一致：变量在事件重复时刻取右极限；参数在 data_1、时变量在 data_2。**.mat v4 布局（R4）**：固定 6 个矩阵 `Aclass, name, description, dataInfo, data_1, data_2`（read_matlab4.c:336），全列主序、binTrans 转置；`Aclass` 4×11 CHAR 解码为 `Atrajectory / 1.1 / 空 / binTrans`（mat4.cpp:531）；`dataInfo` 4×nSignals INT32，每变量一列，四行 = channel(0参数/1时变量/2)、index(1 基，**负号 = 取反别名**)、interpolation(0 线性)、extrapolation(-1/0/1)；`data_1` nData1×2 double（列 0/1=start/stop，第 1 行保留 `[tStart,tStop]`，存参数/常量）；`data_2` 第 1 行为 time、逐时间点追加为列。String 变量不进 .mat。CSV：首行 `"time",...`，real 用 `,%.16g`、int/bool 用 `,%i`（csv.cpp:118/195）。
- **验收标准**：
  - 对本机某个 OMC 产出的 `_res.mat` 与其转成的 `.csv`，`resultio.py` 列出的变量集合、`time` 序列、任取 3 个变量的终值三者一致。
  - 对 `testsuite/ReferenceFiles/msl32/*.mat.xz` 抽 5 个，解压后能读出变量名与维度。
- **优先级**：Must
- **类型映射**：功能
- **来源**：R1§8（结果文件格式与读取入口）、R2§F（ReferenceFiles、xz）、R4（.mat 布局，待落地）

### FR-011: 数值比对器——diffSimulationResults（tube 算法）复刻

- **描述**：系统必须提供 `tools/matcompare.py --algo=tube`，复刻 OMC `diffSimulationResults` 的 tube 判据（ITI csv-compare 移植）：以**参考曲线**为中心，按 `rangeDelta` 在时间方向平移 ±`delta = rangeDelta·(tStop−tStart)`、按局部斜率折算竖直半宽 `delta·sqrt(m²+S²)`（`S = 4·(max−min)/(tStop−tStart)`）生成上下管道；再与 `ref ± max(|ref|·relTol, |max−min|·relTolDiffMinMax)` 取并集；把**实际曲线线性插值到参考时间轴**后逐点判断是否落在管道内；事件点（参考轴相邻时间相等）不计误差；实际文件比参考长则截断、比参考短则末值保持并判失败；变量缺失只告警不失败。默认参数与 OMC 一致：`relTol=1e-3, relTolDiffMinMax=1e-4, rangeDelta=0.002`；testsuite 标准流程用 `relTol=0.01`（其余取默认）。**必须实现 tube 算法而非纯 relTol/absTol**——因为默认容差实际由 `rangeDelta` 主导（竖直半宽 ≥ 0.8%·幅值，远大于 relTol），纯 y 容差会把 OMC 判 Equal 的判成 Not Equal（R1§结论3、§4）。禁止 `rangeDelta=0`（OMC 会段错误，R1§13），需要"无管道"用极小正值。
- **验收标准**：
  - 对拍：对 15 组 (参考 .mat, 实际 .mat) —— 取 testsuite 里 8 个 `diffSimulationResults` 用例的输入 + 7 个合成信号 —— `matcompare.py` 的 `(success, failVars)` 与 OMC `diffSimulationResults` 实跑输出逐组一致。
  - 合成信号单测：正弦参考 vs 相位平移 0.5%·周期的实际 → 因 rangeDelta 判 Equal；vs 幅值 +2% 的实际 → 判 Not Equal 且 failVars 命中该变量。
- **优先级**：Must
- **类型映射**：功能
- **来源**：R1§4（Tubes.c 逐步）、§结论2-7；用户决定 2/Q6

### FR-012: 数值比对器——compareSimulationResults（旧算法）复刻

- **描述**：系统应提供 `tools/matcompare.py --algo=classic`，复刻已弃用但 testsuite 仍在用（simulation/modelica 里 107 个用例、libraries 里经 ModelTesting 用）的 `compareSimulationResults`：单一绝对阈值 `average = relTol·mean(|ref|) + absTol`，逐实际点与最近/插值参考点比 `|d−dr| > average`；默认 `relTol=0.01, absTol=1e-4`；变量缺失 → Not Equal；返回 `{"Files Equal!"}` 或 `{"Files not Equal!", var…}`。
- **验收标准**：对 8 个用 `compareSimulationResults` 的 testsuite 用例输入，`--algo=classic` 判定与 OMC 实跑一致（含 "Files Equal!" 文本）。
- **优先级**：Should
- **类型映射**：功能
- **来源**：R1§6、§结论8；R2§E（ModelTesting.mos:185）

### FR-013: 与 Dymola/MAP-LIB 比对——csv-compare 管道判据

- **描述**：系统应提供 `tools/matcompare.py --algo=csvtube --ref-source=dymola|maplib`，用于把 ymc 结果与 Dymola 产出或 MAP-LIB 参考结果对比。判据采用 csv-compare 的 tube 管道算法（与 FR-011 同族；`--tolerance` = x 向不连续处半宽，**默认 0.002**，源码 `Options.cs` 实证，R5）。MAP-LIB 参考 csv 首列 `"time"`、信号为双引号点分全路径、数组用方括号 `r_0[1]`（下标从 1）、**不含 der()**、只存 `comparisonSignals.txt` 列出的信号（R5）。只比对两侧变量名归一化后的**交集**（见 FR-014），并列出未匹配变量，未匹配不计失败但进台账。**真值口径注意**：MAP-LIB 参考由 **Dymola 2020x + MSL v4.0.0-rc.1** 生成（R5），与本机 Dymola 2025x + MSL 4.0.0 正式版可能有差异；三方比对时应区分"MAP-LIB 冻结参考"与"本机 Dymola 实跑"两个 Dymola 口径，差异入台账。
- **验收标准**：对 3 个 MSL Examples 模型，ymc（或占位用 OMC）结果 vs Dymola `simulateExtendedModel` 结果，`--algo=csvtube` 输出一致率报告与未匹配变量清单；默认容差 0.002 与 R5 记录一致。
- **优先级**：Should（S6 起才真正需要）
- **类型映射**：功能
- **来源**：用户决定 2/Q6；R3§7（Dymola 结果读取）；R5（csv-compare，待落地）

### FR-014: 变量名归一化与交集匹配

- **描述**：系统必须提供变量名归一化规则，用于跨工具对齐：统一 `der(x)` 写法、数组下标写法（`x[1,2]` 空格）、record 字段 `r.a`、别名/负号、protected/生成变量前缀 `$`。比对时取归一化后两侧变量名的交集；OMC 与 Dymola 命名差异（别名消除策略、protected 变量、`der` 命名）导致的未匹配变量单独列出。
- **验收标准**：喂入 OMC 与 Dymola 对同一模型的变量名清单样本，归一化后交集 ≥ 期望阈值（阈值随模型，先记录基线），未匹配清单逐条可解释（是别名/protected/命名差异中的哪一类）。
- **优先级**：Must
- **类型映射**：功能
- **来源**：用户决定 2/Q6；R4（变量命名规则，待落地）；R3§7

### FR-015: 结构级比对器（压平与因果化统计）

- **描述**：系统必须提供 `tools/structcompare.py`，对 S3/S5 阶段做结构级对比：
  - 前端（S3）：把 ymc 压平输出与 OMC `omc -f <model>.mo`（Base Modelica，`//! base 0.1.0` 头）做文本 diff；规范形用 `--baseModelicaOptions=scalarize,moveBindings`（完全标量化、绑定移入 equation 段）。第二道校验用 `checkModel` 的 `Class X has N equation(s) and M variable(s)` 计数。与 Dymola 比用 `getLastError()` 文本里 `Number of components / Continuous time states / Sizes of linear|nonlinear systems of equations` 等统计（见 FR-021）。
  - 后端（S5）：把 ymc 的 `.bdae` dump 与 OMC `omc -s --newBackend -d=backenddaeinfo,bltdump` 的输出比"块头种类序列 + 每块 size + 每块变量名集合"，忽略表达式文本（OMC 会做 alias/inline 改写）。**注意**：新旧后端结构统计不可直接互比（R6§8 D）；块头字段集合随 OMC 版本变化（安装版 1.25.0 无 `size=N`/`Homotopy=`），对比须记录所用二进制版本。
- **验收标准**：
  - `omc -f probe.mo` 与 `--baseModelicaOptions=scalarize,moveBindings` 两种输出都能被 `structcompare.py` 解析成 (变量集, 方程集)。
  - 对 C-PROBE 里 3 个探针模型，能产出 OMC 新后端 backenddaeinfo 的 4 段计数（前端后计数、分区、变量、强连通分量）解析结果，字段与 R6§1 一致；把 `Number of clocked partitions` 与 tuple 计数标为"不可信项"。
- **优先级**：Must（S3 起）
- **类型映射**：功能
- **来源**：R6§1/§2/§6（backenddaeinfo、bltdump、Base Modelica 入口、必须带 `-s`）、§4（checkModel 计数）

### FR-020: Dymola 无窗口驱动

- **描述**：系统必须提供 `tools/dymola_driver.py`，用 Dymola Python 接口（wheel `dymola-2025.1`）无窗口驱动 Dymola 2025x Refresh 1：`DymolaInterface(showwindow=False)` 启动；每个用例用独立工作目录；`SetDymolaCompiler("vs", [...])` + `verifyCompiler()` 配置并验证 C 编译器一次；`openModel(path, changeDirectory=False)` → `translateModel(name)` / `simulateExtendedModel(...)`；结束 `close()`。翻译/仿真失败时 Dymola 默认返回 False 不抛异常，驱动必须每步调 `getLastError()` 判 `numErrors>0` 为失败。
- **验收标准**：
  - 冒烟：对一个不依赖 MSL 的最小 `.mo`（C-PROBE），`dymola_driver.py --translate` 返回成功并落盘 `<model>.dymola.log`；`--simulate --final J1.w,J4.w` 返回终值向量。
  - 失败路径：对一个故意语法错误的模型，驱动捕获 `numErrors>0`，退出码非零，台账记 `dymola:translate-failed`。
- **优先级**：Must（S3 起；S1/S2/S4 不需要 Dymola）
- **类型映射**：功能/外部接口
- **来源**：R3§1（构造与关闭）、§2（translate/simulate 签名）、§3（getLastError 语义）、§5（SetDymolaCompiler/verifyCompiler）；用户决定 1

### FR-021: Dymola 翻译日志解析成结构统计

- **描述**：系统必须把 `getLastError()` 返回四元组 `(message, numErrors, numWarnings, numMessages)` 的 message 原文落盘，并正则解析其 `Statistics` 段为 JSON：`Number of components`、`Continuous time states`、`Sizes of linear systems of equations`、`Sizes of nonlinear systems of equations` 等（字段名以 `TranslationStatistics_v_1_2` 为准，见 R3§4）。该 JSON 是 S3/S5 与 Dymola 结构级对比的数据来源（FR-015）。
- **验收标准**：对 CoupledClutches（R3§3 给了完整样例）与 2 个 MSL Examples，解析出的字段值与 message 原文人工核对一致；解析器对"某字段缺失"（如无非线性系统）返回空数组而非报错。
- **优先级**：Must（S3 起）
- **类型映射**：功能
- **来源**：R3§3（getLastError 示例 dymola_interface.html:1193-1236）、§4（TranslationStatistics 字段）
- **备注**：`dsmodel.mof` 未在 Dymola Python 接口文档中找到（R3§7、§未找到1），故结构统计来源改为 `getLastError()` 文本解析，不依赖 `dsmodel.mof`；`dsmodel.mof` 能否取得列入 TBD-07。

### FR-022: Dymola 数值结果获取

- **描述**：系统必须能从 Dymola 取仿真轨迹用于数值对比：优先 `simulateExtendedModel(..., finalNames=[...])` 直接取终值；全轨迹用 `readTrajectoryNames + readTrajectorySize + readTrajectory`（Dymola 内置磁盘中转，无需自行解析 .mat）；或用 `DataFiles.convertMATtoCSV` 经 `ExecuteCommand` 转 CSV 后用 FR-010 读取。结果 `.mat` 为 Matlab v4，与 OMC 同格式。
- **验收标准**：对 1 个 MSL Example，`dymola_driver.py --simulate` 产出 `_dymola_res.mat`，`resultio.py` 能读出其变量与 `time`；`--final` 模式返回的终值与从 `.mat` 读末值一致。
- **优先级**：Should（S6 起）
- **类型映射**：功能/外部接口
- **来源**：R3§7（readTrajectory、DataFiles.convertMATtoCSV）、§2（simulateExtendedModel finalNames）

### FR-030: 参考结果生成与存放

- **描述**：系统必须提供 `tools/refgen.py`，为一批模型批量生成参考结果并存到**仓库外**目录 `C:\dev\ymc-refs\<oracle>\<oracle版本>\<语料>\<模型>.{mat,csv,stats.json,log}`。oracle ∈ {omc-installed, omc-workspace, dymola-2025x}。生成时每个产物写入 manifest（见 FR-031）。OMC testsuite 自带的 `ReferenceFiles/*.mat.xz` 直接复用、不重生成。
- **验收标准**：`python tools/refgen.py --oracle=dymola-2025x --corpus=C-MSL-EX --limit=5` 在 refs 目录下生成 5 个模型的 `.mat` + `stats.json` + `.log` + 更新 manifest；重复运行同命令为幂等（已存在且 manifest 匹配则跳过）。
- **优先级**：Must（S3 起结构参考；S6 起数值参考）
- **类型映射**：功能/过渡
- **来源**：用户决定 4/Q7；R3§对规格建议；R2§F（ReferenceFiles 复用）

### FR-031: 参考结果 manifest 与过期检测

- **描述**：每个 refs 目录必须有 `manifest.json`，记录每个参考产物的：oracle 名与版本串（OMC `--version` / Dymola `DymolaVersion()`）、生成时间、生成命令、源模型路径与内容哈希、产物哈希。比对时若当前 oracle 版本或源模型哈希与 manifest 不符，报告"参考过期"（reason `ref-stale`），**不自动重生成**（重生成需显式跑 `refgen.py --force`）。
- **验收标准**：改动某模型源文件后跑比对 → 报告该模型 `ref-stale`；`refgen.py --force` 后再比对 → 正常；manifest 每条含全部字段（schema 见 §5 IF-005）。
- **优先级**：Must
- **类型映射**：功能/过渡
- **来源**：用户决定 Q7；R7（oracle 版本差异，需记录 `4.0.0+maint.om` sha）

### FR-040: 统一报告（JSON + Markdown）

- **描述**：系统必须提供 `tools/report.py`，把运行器与比对器的结果汇总成 `report.json`（机器可读，schema 见 IF-004）与 `report.md`（人读）。每条含：用例路径、阶段、tool、判定（PASS/FAIL/SKIP）、SKIP 原因码、比对类型（L1文本/L2数值/L3脚本/结构级）、失败摘要（首个差异位置或失败变量）。顶部有按阶段/子目录分组的 PASS/FAIL/SKIP 计数与通过率。
- **验收标准**：`python tools/report.py --stage=S1 --out report.json` 产出的 JSON 通过 schema 校验；`report.md` 顶部计数与逐条明细数目一致；SKIP 项 100% 带原因码。
- **优先级**：Must
- **类型映射**：功能
- **来源**：用户决定 Q9；方案 §6

### FR-041: 三方差异台账

- **描述**：系统必须维护 `ledger.json`（三方差异台账）：当 ymc、OMC、Dymola 三者对同一模型结果不一致时——**尤其 OMC 与 Dymola 本身就不一致**（事件时刻、初始化解不同很常见）——记入台账（模型、变量、三方值/判定、首次记录时间、分类：`omc-dym-disagree`/`ymc-only-fail`/`var-unmatched`/`ref-stale`/`known-nonequiv`）。按用户决定 2：testsuite 用例以 OMC 的 `// Result:`/参考 .mat 为真值；MSL/MAP-LIB 模型以 Dymola 为真值；**三方不一致本身不算 ymc 失败**，只入台账。
- **验收标准**：构造一个 OMC 与 Dymola 已知不一致的 MSL 模型（或从 R7§3 的 70 个差异 .mo 里取），跑三方比对 → 该模型进 ledger 分类 `omc-dym-disagree`，且不计入 ymc 的 FAIL 分母。
- **优先级**：Must
- **类型映射**：功能
- **来源**：用户决定 2（真值裁决）；R7§3（70 个差异 .mo）

### FR-042: 失败汇总成可粘贴待办块

- **描述**：系统应把 `report.json` 里的 FAIL 项汇总成可直接粘贴进 `tasks/todo.md` 的 Markdown 待办块（按阶段/子目录分组，每条含用例路径与失败摘要）。**不自动改写** `tasks/todo.md`（由用户手动挑，呼应用户决定 Q9）。
- **验收标准**：`python tools/report.py --todo-block --stage=S1` 输出 Markdown 复选框列表到 stdout；不触碰 `tasks/todo.md` 文件。
- **优先级**：Should
- **类型映射**：功能
- **来源**：用户决定 Q9

### FR-050: 阶段验收门统一入口

- **描述**：系统必须提供 `python tests/omc/run.py --stage=<Sk>`，一条命令跑完某阶段的语料子集，输出 PASS/FAIL/SKIP 计数与通过率，并触发对应对比物（见各 FR-05x）。阶段→语料子集→对比类型的映射写在 `tests/omc/stages.json`。
- **验收标准**：`--stage=S0` 跑通并输出计数；`--stage=S1..S8` 至少能识别阶段并对未实现部分标 SKIP:not-implemented。
- **优先级**：Must
- **类型映射**：功能/解决方案
- **来源**：用户决定 4/Q8；方案 §5

### FR-051: S1–S2 验收门（语法/SCode，仅对 OMC）

- **描述**：S1 门 = C-OMC-TS 的 `flattening/modelica/**`（2628 个 .mo，2514 带 `// Result:`）+ MSL 4.0.0 全量做 parse→unparse→parse round-trip（byte-identical），零解析错误；错误用例位置与 OMC 一致（措辞不要求）。S2 门 = 加载 MSL 4.0.0 + Complex + ModelicaServices 无错，SCode 元素计数与 OMC 一致。**S1/S2 只对 OMC，不对 Dymola**（用户决定 3）。
- **验收标准**：`python tests/omc/run.py --stage=S1` 输出 round-trip PASS 率；对 `flattening/modelica` 非旧前端用例 parse 零错误。
- **优先级**：Must
- **类型映射**：功能
- **来源**：方案 §5 S1/S2；用户决定 3；R7§6（flattening 语料）

### FR-052: S3 验收门（压平，OMC 文本 + Dymola 结构统计）

- **描述**：S3 门 = (a) L1：C-OMC-TS 的 `flattening/**` 与 `.mos` 中 `instantiateModel` 用例文本级对齐 OMC（`-d=-newInst` 用例除外，按 FR-003 skip）；(b) 结构级：对 C-MSL-EX 的 Examples 模型，ymc 压平的 (变量数/方程数/状态数) 与 **OMC `checkModel` 计数**及 **Dymola `getLastError()` 统计**对比，不一致进台账。真值：testsuite 用例按 OMC；MSL 模型按 Dymola（用户决定 2/3）。
- **验收标准**：`--stage=S3` 输出 L1 通过率 + 结构统计三方对比表；MSL Examples 的变量/方程/状态数三方并列。
- **优先级**：Must
- **类型映射**：功能
- **来源**：方案 §5 S3；用户决定 3；R6§4（checkModel）、R3§3（Dymola 统计）；R7§3（70 个差异 .mo 标 known-nonequiv）

### FR-053: S5 验收门（因果化结构）

- **描述**：S5 门 = 对每个用例，ymc 的因果化结构（状态数、强连通分量按类型计数、线性/非线性块规模）与 OMC `-s --newBackend -d=backenddaeinfo,bltdump` 的"块头种类序列 + 每块 size"一致（结构级，忽略表达式文本）；与 Dymola 比线性/非线性系统规模（`getLastError()` 的 `Sizes of linear|nonlinear systems`）。**新旧后端不可直接互比**，只与新后端比。
- **验收标准**：`--stage=S5` 输出结构级一致率；对 C-PROBE 的探针模型，块头序列与 OMC 新后端逐块对齐。
- **优先级**：Must
- **类型映射**：功能
- **来源**：方案 §5 S5；用户决定 3；R6§1/§2/§8（backenddaeinfo/bltdump、新旧后端不可比）

### FR-054: S6–S7 验收门（数值 .mat 曲线三方对比）

- **描述**：S6 门 = ymc 生成 C、借 OMC 运行时跑出 `_res.mat`，与 OMC 参考（testsuite `ReferenceFiles` 或 OMC 实跑）按 FR-011 tube 判据对比（L2），与 Dymola 结果按 FR-013 csv-compare 判据对比；三方不一致进台账。S7 门 = 全部 S6 通过用例在自研运行时上 PASS（差分 ymc-runtime vs OMC-runtime 的 `.mat`）。数值真值：testsuite 按 OMC 参考，MSL/MAP-LIB 按 Dymola（用户决定 2）。
- **验收标准**：`--stage=S6` 输出 L2 通过率 + 三方数值一致率；S7 退出阈值为 TBD（见 TBD-08）。
- **优先级**：Must
- **类型映射**：功能
- **来源**：方案 §5 S6/S7；用户决定 2/3；R1§4（tube）、R2§F（ReferenceFiles）

### FR-055: S0 与 S8 门

- **描述**：S0 门 = 三套预设零警告 + ASan 全绿 + 运行器对 OMC 自身跑 `flattening/modelica/scoping` 全 PASS（FR-060）+ 比对器/驱动的单测与冒烟全绿。S8 门 = 逐项单独立项（撕裂补全、动态状态选择、时钟、状态机、FMI 等），每项到时补一条 FR 与对比门；不在本轮范围。
- **验收标准**：`--stage=S0` 触发工程门检查脚本（见 FR-062）全绿。
- **优先级**：Must（S0）/ Could（S8）
- **类型映射**：功能
- **来源**：方案 §5 S0/S8

### FR-060: 运行器自检（对 OMC 必须全 PASS）

- **描述**：系统必须提供运行器自检：用 `--tool=omc` 对 OMC 自身跑若干已知全 correct 的目录（`flattening/modelica/scoping` 等），必须 100% PASS。若不全 PASS，说明运行器协议或归一化实现有 bug，阻断（这是"运行器正确性"的证明，方案 §S0 验收）。
- **验收标准**：`python tests/omc/run.py --self-check` 对指定 oracle 目录集合返回全 PASS，否则退出码非零并列出偏差用例。
- **优先级**：Must
- **类型映射**：功能/非功能
- **来源**：方案 §S0 验收；R2§对规格建议

### FR-061: 逐阶段差分工具

- **描述**：系统应提供 `tools/diff_omc.py <model>.mo`，把同一模型分别过 ymc 与 OMC（工作区版，带超时），逐阶段并排 dump（Base Modelica / bltdump / backenddaeinfo / `.mat`），第一处不同即停并打印。这是日常开发主工具（方案 §6.5）。
- **验收标准**：对一个探针模型，`diff_omc.py` 打印各阶段并排 dump；人为在某阶段引入差异 → 工具在该阶段停并高亮。
- **优先级**：Should
- **类型映射**：功能
- **来源**：方案 §6.5；R6（各阶段 dump 命令）

### FR-062: 工程门检查脚本

- **描述**：系统必须提供 `tools/gate_engineering.py`（或等价 CMake/CTest 目标），一键检查工程门 G3：两套编译器（gcc-debug、msvc-debug）零警告、ASan 预设全绿、确定性检查（同输入两次运行 byte-identical）。
- **验收标准**：`python tools/gate_engineering.py` 依次构建三套预设、跑 ctest、对选定 dump 做两次运行 diff，全绿返回 0。
- **优先级**：Must
- **类型映射**：功能/非功能
- **来源**：方案 §3.1/§3.5；G3 定义（§8）

### FR-070: 任务卡流水线与评审清单

- **描述**：系统应把方案 §7.1 任务卡模板落成实际任务卡（`docs/tasks/T-<阶段>-<序号>.md`），每张含"完成定义"里的对比门引用（跑哪条 `run.py --stage`）。评审清单固定：来源标注齐全？§3.4/§3.5 逐条？dump 前后附上？新增单测快照？ASan 绿？对比门通过？
- **验收标准**：S1 首批任务卡（词法/语法/Absyn/unparse）产出，每张"完成定义"含可跑的验收命令。
- **优先级**：Should
- **类型映射**：功能/过渡
- **来源**：方案 §7.1；用户全局 CLAUDE.md（任务管理）

---

## 4. 非功能需求

### NFR-001: 确定性

- **描述**：运行器、比对器、报告生成对同一输入两次运行必须 byte-identical（含 JSON 键顺序、报告行序）；不依赖哈希随机种子或字典无序遍历。
- **度量**：`tools/gate_engineering.py` 的确定性检查连跑两次 diff 为空。
- **优先级**：Must
- **来源**：方案 §3.5 第 6 条

### NFR-002: 超时强制

- **描述**：所有对 omc/dymola/仿真器的外部调用必须带超时。默认值：翻译/单用例 ≤ 900s（对齐 rtest `--alarm=900`）、ModelTesting ≤ 480s、仿真器 ≤ 360s。对工作区 omc（源码 HEAD，对 index-3+StateSelect 会死循环）超时是硬性要求。超时 → 该用例判 FAIL 原因码 `timeout`，不阻断整批。
- **度量**：对一个已知会超时的模型，运行器在超时后继续跑后续用例，报告标 `timeout`。
- **优先级**：Must
- **来源**：R2§结论6（rtest --alarm）；CLAUDE.md（工作区 omc 死循环）

### NFR-003: 性能基线（先测后定阈值）

- **描述**：S3g 压平耗时、S6 仿真耗时相对 OMC 的倍数需先测基线再定门槛（先记录，不设硬阈值）。平台仅 Windows。
- **度量**：`refgen.py`/`run.py` 记录每用例耗时进 report；基线报告出来后在 TBD-06 定阈值。
- **优先级**：Should
- **来源**：用户决定 Q10；方案 §8 #5

### NFR-004: 可复现与归档

- **描述**：每次 oracle 运行必须记录 oracle 版本串（OMC `--version`、Dymola `DymolaVersion()`）、命令行、时间到 manifest/log；日志按 `<oracle版本>/<语料>/<模型>.log` 归档，可回溯。
- **度量**：任取一条参考产物，能从 manifest 追到生成它的 oracle 版本与命令。
- **优先级**：Must
- **来源**：用户决定 Q7；R3§对规格建议（记录 DymolaVersion）

### NFR-005: 可调试

- **描述**：比对器失败时必须输出可复现的最小信息：失败变量、参考轴上首个越界时间点、期望/实际/管道上下界五元组（对齐 OMC `diffSimulationResults` 写的 `time,reference,actual,high,low,error,actual(raw)` CSV，R1§4 第 11 条），并能生成 dygraph HTML（对齐 `diffSimulationResultsHtml`）。
- **度量**：一个失败用例产出上述 CSV，字段与 OMC 一致。
- **优先级**：Should
- **来源**：R1§4（Tubes.c:731 表头）

### NFR-006: 平台与依赖

- **描述**：工具链 Python 3.11 + numpy/scipy（本机已有）；外部依赖仅 `xz`（解压参考）、OMC（安装版 1.25.0 + 工作区版）、Dymola 2025x（Python 接口 wheel）。仅 Windows。Dymola 需预先 `SetDymolaCompiler("vs")` + `verifyCompiler()` 通过。
- **度量**：全新 clone 后按 README 安装 `dymola-2025.1` wheel 到 Python 3.11，`dymola_driver.py --smoke` 通过。
- **优先级**：Must
- **来源**：R3§0（wheel Requires-Python>=3.7）、§5；本机环境核实

### NFR-007: 许可证依赖与降级

- **描述**：Dymola 对比依赖本机许可证（用户确认可用）。若某次运行许可证不可用/Dymola 启动失败，Dymola 线必须优雅降级：该批 G2 门标 `dymola-unavailable`（不是 ymc FAIL），G1（OMC）门照常跑完。
- **度量**：模拟 Dymola 不可用（改 dymolapath 为无效），`run.py --stage=S3` 仍完成 OMC 部分，Dymola 部分标 `dymola-unavailable`。
- **优先级**：Must
- **来源**：用户决定 1；R3§1（启动失败抛 DymolaConnectionException）

### NFR-008: 参考数据体量

- **描述**：仓库外 refs 目录可能达数百 MB（testsuite ReferenceFiles 484 个 .mat.xz + MSL/MAP-LIB）。refs 不进 git；单个参考 `.mat` 建议 ≤ 1MB（对齐 OMC ReferenceFiles README）。
- **度量**：refs 目录有 `.gitignore` 或位于仓库外；生成脚本对 >1MB 的参考告警。
- **优先级**：Should
- **来源**：R2§F（README ≤1MB）；用户决定 Q7

---

## 5. 外部接口需求

### IF-001: `tests/omc/run.py` CLI

- **类型**：CLI
- **入口/参数**：`--tool=ymc|omc`、`--omc=installed|workspace`、`--dir <路径>`、`--stage=<Sk>`、`--json <out>`、`--self-check`、`--print-cflags-histogram`、`--timeout <秒>`、`--jobs <N>`（并行）、`--filter <keywords>`。
- **交互/输出**：stdout 打印 `== N/M PASS, K SKIP [time] ==` 汇总；`--json` 写结果数组。
- **退出码**：0 = 全 PASS 或仅 SKIP；1 = 有 FAIL；2 = 运行器/环境错误（如 omc 找不到）。
- **错误处理**：单用例异常不阻断整批；环境级错误立即退 2。
- **来源**：R2§A（rtest CLI 对照）；FR-001/004/050/060

### IF-002: `tools/matcompare.py` CLI

- **类型**：CLI
- **入口/参数**：`--algo=tube|classic|csvtube`、`--ref <file>`、`--actual <file>`、`--vars <名单|@file>`、`--reltol`、`--reltoldiffminmax`、`--rangedelta`、`--abstol`、`--ref-source=omc|dymola|maplib`、`--out-csv <prefix>`、`--html`。
- **输出**：stdout `(success, [failVars])`；`--out-csv` 写 `<prefix>.<var>.csv`（表头 `time,reference,actual,high,low,error,actual (raw)`）。
- **退出码**：0 = Equal；1 = Not Equal；2 = 读取/参数错误。
- **默认值**：tube → `reltol=1e-3, reltoldiffminmax=1e-4, rangedelta=0.002`；classic → `reltol=0.01, abstol=1e-4`；csvtube → `tolerance=0.002`（csv-compare `Options.cs` 默认，R5）。禁止 `rangedelta=0`（报错退 2）。
- **来源**：R1§1/§4；FR-011/012/013

### IF-003: `tools/dymola_driver.py` CLI

- **类型**：CLI + 外部系统（Dymola Python 接口）
- **入口/参数**：`--model <path>`、`--name <类名>`、`--workdir <dir>`、`--translate`、`--simulate`、`--final <var,var>`、`--stop <t>`、`--compiler-vs <MSVCDir>`、`--dymolapath <path>`。
- **输出**：`<model>.dymola.log`（getLastError message 原文）、`<model>.stats.json`（解析后统计）、`<model>_dymola_res.mat`（仿真）。
- **退出码**：0 = 成功；1 = 翻译/仿真失败（numErrors>0）；3 = Dymola 不可用/许可证/启动失败。
- **错误处理**：每步 `getLastError()`；失败落 log + 台账；`close()` 保证进程回收。
- **来源**：R3§1/§2/§3/§5；FR-020/021/022；NFR-007

### IF-004: `report.json` schema

- **类型**：文件格式
- **字段**：`{ schema_version, generated_at, oracle_versions:{omc,dymola}, stage, summary:{pass,fail,skip,rate_by_dir:{}}, cases:[ { path, stage, tool, verdict:PASS|FAIL|SKIP, skip_reason?, compare_kind:L1|L2|L3|struct, fail_summary?, first_diff?:{line|time,expected,actual}, duration_s } ] }`。
- **错误处理**：schema 校验失败 → 生成中止并报字段。
- **来源**：FR-040；用户决定 Q9

### IF-005: `manifest.json` schema（refs）

- **类型**：文件格式
- **字段**：`{ schema_version, oracle, oracle_version, corpus, entries:[ { model_path, model_hash, artifact:{mat?,csv?,stats?,log?}, artifact_hash, gen_command, gen_at } ] }`。
- **来源**：FR-031；NFR-004

### IF-006: `ledger.json`（三方差异台账）schema

- **类型**：文件格式
- **字段**：`{ schema_version, entries:[ { model, var?, category:omc-dym-disagree|ymc-only-fail|var-unmatched|ref-stale|known-nonequiv, values:{ymc?,omc?,dymola?}, verdicts:{omc?,dymola?}, first_seen, note } ] }`。
- **来源**：FR-041；用户决定 2

### IF-007: `cflags_map.json` / `stages.json` / skip 清单

- **类型**：文件格式
- **cflags_map.json**：`{ "<omc-flag>": {"action":"passthrough|rename|skip","to?":"<ymc-flag>","reason?":"<原因码>"} }`。
- **stages.json**：`{ "S3": {"corpora":["C-OMC-TS/flattening","C-MSL-EX"],"compare":["L1","struct"],"dymola":true,"threshold":<TBD>} }`。
- **skip 原因码枚举**：`old-frontend`、`cpp-runtime`、`out-of-scope-api`、`not-implemented`、`dymola-unavailable`、`ref-stale`、`timeout`、`known-nonequiv`。
- **来源**：FR-003/050；R2§D；用户决定 Q8

### IF-008: 与 OMC / Dymola 的外部系统接口

- **OMC**：命令行 `omc [flags] file.{mo,mos}`；关键 flag `-s`（跑后端，必需）、`--newBackend`、`-f`（Base Modelica）、`--baseModelicaOptions=scalarize,moveBindings`、`-d=backenddaeinfo,bltdump`、`--simCodeTarget=None`；`omc-diff eps expected actual`。安装版 1.25.0 在 PATH；工作区版路径 `C:/dev/OpenModelica/build_cmake/install_cmake/bin/omc.exe`（带超时）。
- **Dymola**：Python 接口 `from dymola.dymola_interface import DymolaInterface`；`DymolaInterface(showwindow=False)`；核心方法 `openModel/translateModel/simulateExtendedModel/getLastError/SetDymolaCompiler/verifyCompiler/close`（签名见 R3§1/§2/§3/§5）。
- **来源**：R2§A、R6§5/§7、R3§1-§5

---

## 6. 过渡需求

### TR-001: 首次 git 提交

- **描述**：仓库当前零提交（git 已 init）。基建落地前先由用户做首次提交（S0 骨架），随后基建按 PR/提交推进。
- **策略**：先提交现有 S0 骨架 + 本 Spec；基建工具与任务卡分批提交。
- **回滚**：git revert。
- **来源**：gitStatus（zero commits）；方案 §7

### TR-002: 覆盖方案的占位条目

- **描述**：方案 §6.1（运行器）、§6.2（归一化）、§6.4（数值比对）、§8 #1（omc-diff 规则，已在 R1/R2 解决）、#3（ModelTesting，R2§E 解决）由本文 FR-001/002/011/013/015 具体化。方案里这些条目改为"见需求规格 FR-xxx"。
- **策略**：本文落地后在方案对应处加指针，不删方案原文。
- **回滚**：文档层面无损。
- **来源**：方案 §6/§8

### TR-003: docs/open-questions.md 建立

- **描述**：建立 `docs/open-questions.md`，把本文 §10 TBD 与研究底稿的"未找到/待探针验证"合并为活文档（呼应方案 §3.6、§8）。
- **策略**：TBD 关闭时更新该文件。
- **来源**：方案 §3.6；R1-R7 各"未找到"节

### TR-004: MAP-LIB 参考结果克隆

- **描述**：MAP-LIB 参考结果在 `modelica/MAP-LIB_ReferenceResults` 的 **v4.0.0 分支**（另有 v4.1.0、v3.2.3），仓库已于 2026-09-11 归档只读并迁入 MSL 主仓 `ReferenceResults/`（R5）。布局：`Modelica/` + `ModelicaTest/`，按包名分 12 域，点分模型名映射为路径，每模型一文件夹含 `<Model>.csv` + `comparisonSignals.txt` + `creation.txt` + 日志。许可证 BSD-3-Clause（但仓库根**无 LICENSE 文件**，GitHub API 报 license:null）。仓库体量大（API 约 1.73 GiB，.mat 走 LFS 更大）。**注意**：v4.0.0 抽样文件夹只见 `.csv` 未见 `.mat`（与 wiki 称 MAT+CSV 有出入，见 TBD-14）。S6 前克隆并核实，进 refs 目录 C-MAPLIB。
- **策略**：克隆 v4.0.0 分支（或从迁入后的 MSL 仓 `ReferenceResults/` 取）到 `C:\dev\ymc-refs\maplib\`；manifest 记录 commit 与来源分支；只取 `comparisonSignals` 列出的信号做 csvtube 比对。
- **回滚**：删目录。
- **来源**：方案 §8 #10；R5（MAP-LIB 布局/许可证/生成工具 Dymola 2020x + MSL v4.0.0-rc.1）

### TR-005: loadModel 版本映射策略

- **描述**：OMC 把 `loadModel(Modelica,{"3.2.1"})` 映射到 `3.2.3+maint.om`、`{"2.2.2"}` 映射到 `4.0.0+maint.om`（R7§5 探针实证）。testsuite 里仅 27 个 .mos 请求 MSL ≥4.0.0，151 个请求 3.2.x、26 个请求 2.2.x。ymc 的 `loadModel` 须定版本匹配策略（是否复刻 OMC 回退映射，还是只支持 4.0.0 并把 3.2.x 用例标 skip）。
- **策略（2026-09-17 定稿，TBD-05 关闭）**：复刻 OMC 1.25.0 的选择**结果**（两层机制：provides 替换 + 版本前缀逐级放宽），
  但不读 OMC 的 `index.json`，provides 信息从候选库自身的 `conversion` 注解重建；3.2.x / 2.2.x 用例**不再标 skip**。
  细则与依据见 `docs/adr/005-library-lookup-and-versions.md`；判别探针 13 + 9 例固化在 `tests/unit/scode/loader_test.cpp`。
- **来源**：R7§5/§对规格建议3；R8（`docs/research/R8-omc-classloader.md`）

---

## 7. 约束与假设

### 7.1 技术约束

- 语料代号与来源：
  - **C-OMC-TS** = OMC `testsuite/`（simulation/modelica 383 .mo + 1269 .mos；flattening/modelica 2628 .mo，2514 带 `// Result:`；libraries 1357 .mos，805 带 compareVars；ReferenceFiles 484 .mat.xz）。
  - **C-MSL-EX** = MSL 4.0.0 Examples（595 .mo，404 带 `experiment(`；OMC 与 Dymola 各一份，见下"已知不等价"约束）。
  - **C-MAPLIB** = MAP-LIB 参考 csv（`MAP-LIB_ReferenceResults` v4.0.0 分支，CSV-only，信号为 `comparisonSignals.txt` 子集，Dymola 2020x + MSL v4.0.0-rc.1 生成；待克隆，R5）。
  - **C-PROBE** = `tools/probe/` 探针模型（每个规范角落一个最小模型 + oracle 输出）。
- omc-diff 判据（已核实）：token 级；整数精确；实数 `|actual−expected| > eps·|actual| + eps` 失败；eps_mo=1e-7、eps_mos=5e-3（R2§B）。
- diffSimulationResults 判据（已核实）：tube 算法，默认容差由 `rangeDelta=0.002` 主导（R1§3）；禁 `rangeDelta=0`。
- 必须带 `-s` 才跑 OMC 后端（R6§7）；结构级对比只与 OMC **新**后端比（新旧不可互比，R6§8 D）。
- OMC 结构 dump 块头字段随版本变化，对比须记录二进制版本（R6§结论9）。
- 分工边界（见 §7.2）。

### 7.2 业务约束

- **核心代码由用户手写**（方案 §7.2、CLAUDE.md）：`src/` 下除 `base/assert.h` 与 `driver/` 外的一切（parse/absyn/scode/inst/flat/bdae/simcode/codegen/script）、`runtime/` 全部。**本文所有"系统必须提供"的工具（tests/omc/*、tools/*、CMake、docs、src/base 骨架、src/driver）均由 Claude 实现；编译器核心一律由用户手写，配任务卡 T-…**。
- 不臆想（方案 §3.6）：语义结论只允许 MLS §/OMC 文件:行/探针三种来源。
- 命名前缀统一 `ymc` / `YMC_`（lessons.md：避开 OMC 的 `MMC_`）。

### 7.3 假设

- Dymola 2025x Refresh 1 许可证可用、可无窗口批跑 — **已验证（用户确认；R3 文档核实接口存在，未实跑 Dymola）**。
- Dymola 结构统计从 `getLastError()` 文本解析可得 — **假设（R3§3 有 CoupledClutches 完整样例，但未对 MSL 批量实跑验证）**，见 TBD-02。
- OMC 与 Dymola 的 MSL 4.0.0 语义等价 — **部分否定**：70 个 .mo 内容不同（R7§3），其中 M_OLine/R134a/Tables.mo 等为实质变更；这些进"已知不等价"清单，ModelicaServices 整包排除。
- 工作区 omc 与安装版 1.25.0 在比对/结果读取源码上一致 — **假设（R1§未找到1 未逐行 diff）**。
- 1.25.0 运行时头文件足够借用（S6）— **假设（方案 §8 #7，未验证）**，见 TBD-09。

---

## 8. 优先级与里程碑建议

三道验收门（每阶段退出需三门齐过）：
- **G1 OMC 对齐门**：`python tests/omc/run.py --stage=Sk` 输出 PASS/FAIL/SKIP；范围内用例 PASS 率达阶段阈值（stages.json，阈值先记 TBD-08），FAIL 全进台账，SKIP 必带原因码。
- **G2 Dymola 对齐门**（S3 起）：结构级（S3/S5）或数值级（S6+）一致率报告；不一致进台账；S6 前无硬阈值，S7 退出阈值 TBD-08。
- **G3 工程门**：`tools/gate_engineering.py` 全绿（两套编译器零警告、ASan、确定性）+ ADR/任务卡/lessons 更新。

| ID | 需求 | 优先级 | 理由 |
|---|---|---|---|
| FR-001/002/003/004 | 运行器 + omc-diff + cflags 映射 | Must | 一切 OMC 对齐的前提 |
| FR-060/062 | 自检 + 工程门 | Must | 证明工具本身正确 |
| FR-010/011/014/015 | 结果读取 + tube 比对 + 归一化 + 结构比对 | Must | L2 与 S3/S5 对比核心 |
| FR-020/021 | Dymola 驱动 + 统计解析 | Must | 用户核心诉求 |
| FR-030/031/040/041 | 参考管理 + 报告 + 台账 | Must | 可复现与真值裁决 |
| FR-050..055 | 阶段门 | Must | 每阶段退出判定 |
| FR-012/013/022/042/061/070 | 旧算法/csv-compare/Dymola 数值/待办/差分/任务卡 | Should | 完善与体验 |

里程碑（与 S0–S8 对应；估算时间标 TBD-10，用户投入未定）：

- **M0（本轮立即做的基建，Spec 确认后开工）**：
  1. `tests/omc/run.py` + `omc_diff.py` + `--self-check`（FR-001/002/060）
  2. `cflags_map.json` + cflags 直方图（FR-003）
  3. `tools/resultio.py` + `tools/matcompare.py`（tube + classic + 合成信号单测）（FR-010/011/012）
  4. `tools/dymola_driver.py` 冒烟 + 统计解析（FR-020/021）
  5. `tools/refgen.py` + `manifest.json` + 过期检测（FR-030/031）
  6. `tools/report.py` + `report.json`/`report.md` + `ledger.json`（FR-040/041/042）
  7. `tools/gate_engineering.py`（FR-062）
  8. `tasks/todo.md`（M0 任务清单）、`docs/open-questions.md`（TR-003）、首次提交（TR-001）
  每项完成定义 = 其 FR 验收标准 + 单测/冒烟通过。
- **M1–M2**：S1/S2 门（FR-051）+ S1 任务卡（FR-070）。
- **M3**：S3 门（FR-052）+ 结构比对器 + Dymola 结构统计（FR-015/021）。
- **M4**：S4 脚本层门（仅 OMC）。
- **M5**：S5 门（FR-053）。
- **M6–M7**：S6/S7 数值三方门（FR-054）+ MAP-LIB 克隆（TR-004）。
- **M8**：S8 逐项立项。

---

## 9. 变更/设计提案（RFC）

### 9.1 现状分析

- **当前架构**：仓库是 S0 骨架——`src/base/assert.h`、`src/driver/main.cpp`、`tests/ymc_test.h`（60 行自写测试框架）、`tests/unit/base/{symbol,arena}_test.cpp`（待 `symbol.h`/`arena.h` 出现才进 CMake）、三套 CMake 预设全绿。无 `tests/omc/`、无 `tools/`、无 `tasks/todo.md`、无 `docs/spec/`（本文新建）。
- **当前问题**：方案定义了验收级别但无可执行门；无任何 oracle 对接代码；Dymola 对比完全缺失。
- **相关代码路径**：OMC 参考 `C:/dev/OpenModelica`（rtest、omc-diff.l、SimulationResultsCmpTubes.c、NBackendDAE.mo、ModelicaBuiltin.mo 等，锚点见研究底稿）；Dymola `C:/Program Files/Dymola 2025x Refresh 1`（python_interface wheel + doc）。

### 9.2 目标状态

```
                         ┌──────────────────────────────────────────────┐
   用例/模型语料          │  tests/omc/run.py  (--stage / --tool / --self-check)
   C-OMC-TS/C-MSL-EX ───▶│    ├─ 解析 // 指令 + cflags_map.json           │
   C-MAPLIB/C-PROBE      │    ├─ 跑 ymc | omc(installed|workspace,超时)   │
                         │    ├─ 归一化 + omc_diff.py (L1/L3 文本)        │
                         │    └─ 触发比对物：                              │
                         │         ├─ matcompare.py (L2 数值: tube/classic/csvtube)
                         │         └─ structcompare.py (S3/S5 结构级)     │
                         └───────────────┬──────────────────────────────┘
   oracle 外部系统                        │ 产出统一结果记录
   ┌── omc (CLI, -s/-f/-d=…) ◀───────────┤
   ├── omc-workspace (超时) ◀────────────┤
   └── dymola_driver.py ◀────────────────┘ (DymolaInterface 无窗口)
                                          │
   refs (仓库外)                          ▼
   C:\dev\ymc-refs\<oracle>\<ver>\  ◀── refgen.py + manifest.json (过期检测)
                                          │
                                          ▼
                         report.py ──▶ report.json / report.md
                                   └──▶ ledger.json (三方差异台账) + 待办块
```

关键变更：新增 `tests/omc/`（运行器族）与 `tools/`（比对器/驱动/参考/报告），全部 Python；不碰 `src/`（用户手写核心）。

### 9.3 详细设计

**模块与职责**（均 Claude 实现，Python 3.11）：

| 模块 | 职责 | 关键依赖 |
|---|---|---|
| `tests/omc/run.py` | 用例发现、`//` 指令解析、命令构造、归一化、判定、阶段门入口、自检 | `omc_diff`、`matcompare`、`structcompare`、`cflags_map.json`、`stages.json` |
| `tests/omc/omc_diff.py` | token 级模糊比对（FR-002） | 无（纯 Python 词法） |
| `tools/resultio.py` | 读 `.mat`(v4)/`.csv`/`.mat.xz`，取变量/序列/终值 | numpy、外部 `xz` |
| `tools/matcompare.py` | tube / classic / csvtube 判据（FR-011/012/013） | `resultio`、numpy |
| `tools/structcompare.py` | Base Modelica / bltdump / backenddaeinfo / checkModel 解析与比对（FR-015） | 文本解析 |
| `tools/dymola_driver.py` | Dymola 无窗口驱动 + getLastError 解析（FR-020/021/022） | `dymola` wheel |
| `tools/refgen.py` | 批量生成参考 + manifest + 过期（FR-030/031） | 各 oracle 驱动 |
| `tools/report.py` | report.json/md + ledger + 待办块（FR-040/041/042） | jsonschema |
| `tools/diff_omc.py` | 逐阶段并排 dump 差分（FR-061） | ymc、omc |
| `tools/gate_engineering.py` | 工程门 G3（FR-062） | cmake/ctest |
| `tools/varnorm.py` | 变量名归一化（FR-014） | 被 matcompare/structcompare 共用 |

**数据模型**：见 §5 IF-004..IF-007（report/manifest/ledger/cflags_map/stages/skip 全部字段已定义）。

**主要流程**：
1. **跑一个阶段门**：`run.py --stage=S3` → 读 stages.json 取语料子集与对比类型 → 逐用例：解析指令、按 cflags_map 决定 passthrough/skip → 跑 ymc 与 oracle → L1 用 omc_diff、struct 用 structcompare、L2 用 matcompare → 汇总 report + ledger → 打印计数。
2. **生成参考**：`refgen.py --oracle=dymola-2025x --corpus=C-MSL-EX` → dymola_driver 逐模型 translate+simulate → 落 mat/stats/log → 写 manifest（含版本/哈希/命令）。
3. **三方比对**：对一个 MSL 模型，取 ymc 结果、OMC 参考、Dymola 参考 → varnorm 取交集 → matcompare 两两判定 → 按用户决定 2 裁真值 → 不一致入 ledger。
4. **Dymola 探针/冒烟**：`dymola_driver.py --smoke` 对 C-PROBE 最小模型 translate+simulate，验证接口与编译器可用。
5. **失败闭环**：report 的 FAIL → `--todo-block` 输出可粘贴 Markdown（不改 todo.md）。

### 9.4 备选方案

| 方案 | 优点 | 缺点 | 决定 |
|---|---|---|---|
| 比对工具用 Python | 快速迭代、numpy/scipy 现成、非核心代码 | 与 C++ 主体异构 | **选用**：属基建，分工上归 Claude，方案依赖白名单也未禁 Python 工具 |
| 比对工具用 C++ | 与主体同栈 | 拖慢基建、占用手写预算 | 拒绝 |
| 参考结果放仓库内 | 自包含 | 数百 MB 撑爆 git（OMC 自己都用 .xz 外挂） | 拒绝 |
| 参考结果放仓库外 + manifest | git 干净、版本可控 | 需额外目录约定 | **选用**（用户决定 Q7） |
| Dymola 驱动用 Python 接口 | 官方支持、无窗口、拿终值/轨迹/日志齐全 | 依赖 wheel 与许可证 | **选用**（R3 证实接口完备） |
| Dymola 驱动写 .mos 脚本 | 无 Python 依赖 | 取不到结构化统计、日志难解析 | 拒绝 |
| 直接跑 dymosim.exe | 绕过 GUI | 需自己管翻译/编译/dsin，重复造轮子 | 拒绝 |
| 数值判据用 OMC tube 算法 | 与 testsuite 同判 | 实现较繁 | **选用**（对 OMC 侧；纯 relTol 会误判，R1§3） |
| 数值判据自定义简单 relTol | 好写 | 与 OMC/Dymola 都不同判 | 拒绝 |
| 结构级对比来源=Dymola 翻译日志 | Python 接口可得 | 需正则解析、字段随版本 | **选用**（getLastError 文本） |
| 结构级对比来源=dsmodel.mof | 更结构化 | Python 接口未暴露（R3§7） | 拒绝（列 TBD-07 探针） |
| S6 借 OMC 运行时 | 快速拿 .mat 做 L2 | 依赖 1.25.0 ABI | **选用**（方案 §2.2；TBD-09 验证 ABI） |
| S6 直接自研运行时 | 无外部依赖 | 阶段跨度大、无法早验证 | 拒绝（放 S7） |

### 9.5 实施与迁移计划

- **实施顺序**：M0 内部顺序 = omc_diff → run.py+self-check（先证明协议）→ resultio → matcompare（tube，配合成信号单测）→ report/ledger → dymola_driver 冒烟+统计 → refgen/manifest → gate_engineering → todo.md/open-questions/首次提交。
- **风险与缓解**：
  - Dymola 批跑不稳/许可证波动 → NFR-007 降级；每用例独立工作目录 + close() 回收。
  - tube 算法实现偏差 → FR-011 对拍 OMC 实跑（15 组）作为验收硬门。
  - 工作区 omc 死循环 → NFR-002 强制超时。
  - OMC/Dymola MSL 不等价（70 文件）→ FR-041 台账 + known-nonequiv 清单，不计 FAIL。
  - 版本漂移（块头字段、maint.om sha）→ manifest 记录版本；结构对比按运行二进制解析。
- **测试策略**：
  - 单元：omc_diff 边界（10 组对拍 omc-diff.exe）；matcompare 合成信号（相位/幅值/事件）；varnorm（der/下标/别名）；结果读取（mat vs csv 一致）。
  - 集成：run.py 对 OMC 自跑 `flattening/modelica/scoping` 全 PASS（自检）；refgen 幂等。
  - 端到端：一个 MSL Example 走完 ymc(占位=omc)→OMC 参考→Dymola 参考三方比对，产出 report + ledger。
  - 冒烟：dymola_driver 对 C-PROBE translate+simulate。
- **回滚**：所有新增在 `tests/omc/`、`tools/`、`docs/`，不改 `src/`；git revert 即可；refs 在仓库外，删目录不影响主体。

---

## 10. TBD 清单

| ID | 事项 | 缺失信息 | 下一步 |
|---|---|---|---|
| TBD-01 | R4/R5 已完成并入 FR-010/013、TR-004；剩余细节 | `$cse`/`$DER` 默认可见性（R4 未定）、name 矩阵数组下标逐字节是否与 Dymola 一致、CSV 在 Windows 是否 `\r\n` | 实跑 dump 一份 .mat 探针核实（M0 内 resultio 单测覆盖） |
| TBD-14 | MAP-LIB v4.0.0 是否含 .mat（还是仅 .csv） | 抽样只见 .csv，wiki 称 MAT+CSV（R5） | 本地克隆 v4.0.0 分支后核实；只 .csv 则 FR-013 走 csv 读取 |
| TBD-02 | Dymola 结构统计批量解析可行性 | getLastError 文本格式仅有 CoupledClutches 一例（R3§4/§未找到5） | 对若干 MSL 模型实跑取样后定正则（M3 前） |
| TBD-03 | Dymola `Advanced.*` 翻译开关（EliminateAliasVariables/OutputModelicaCode 等）是否存在 | Python 文档 0 命中（R3§未找到1） | 在 Dymola 里 `list(variables={"Advanced.*"})` 探针 |
| TBD-04 | reference_reltol/reltolDiffMinMax/rangeDelta 三名字来源 | testsuite 全树 0 命中（R2§未找到1） | 核对 OpenModelicaLibraryTesting（外部仓库） |
| ~~TBD-05~~ | ymc loadModel 版本回退映射是否复刻 OMC | **已关闭（2026-09-17）**：规则已在源码定位（R8：`ClassLoader.mo:152-166`、`PackageManagement.mo:352-371`、`systemimpl.c:2211-2270`）并实现 | 复刻结果、不读 index.json（ADR-005）；MSL 3.2.3 / Buildings 9.1.0 装载集合与 OMC 一致 |
| TBD-06 | 性能门阈值 | 基线未测（NFR-003） | S3g/S6 基线测得后定倍数阈值 |
| TBD-07 | dsmodel.mof 能否经 Dymola 取得 | Python 接口未暴露（R3§7） | 探针 runTestCases generateReferenceTranslationStructure 或读 User Manual 2C |
| TBD-08 | 各阶段 G1/G2 通过率阈值、S7 退出阈值 | 需用户定 | 用户决定；先在 stages.json 留空 |
| TBD-09 | 1.25.0 libSimulationRuntimeC 头文件/ABI 是否够（S6） | 未验证（方案 §8 #7） | 用 OMC 生成的 Demo.c 手工链接一次（S1 期间） |
| TBD-10 | 里程碑时间估算 | 用户每周投入未知（Q8） | 用户告知投入后估算 |
| TBD-11 | Dymola 语料优先级最终确认 | 默认 C-MSL-EX→C-MAPLIB→提取的 .mo（Q5 未答） | 用户确认或采用默认 |
| TBD-12 | 错误消息文本对齐深度 | Q11 默认：位置一致 Must、文本一致 Should | 用户确认默认 |
| TBD-13 | 工作区 omc 与安装版在比对/读取源码是否一致 | 未逐行 diff（R1§未找到1） | 需要时 diff 两版对应 .c |
