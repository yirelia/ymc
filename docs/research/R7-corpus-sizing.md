# R7：三方对比语料的规模与 MSL 版本差异

调查日期：2026-09-14。只读；所有数字来自本机实际命令输出（bash，Git Bash）。
路径缩写：
- `OMC_MSL` = `C:/Users/yangrui/AppData/Roaming/.openmodelica/libraries/Modelica 4.0.0+maint.om`
- `DYM_MSL` = `C:/Program Files/Dymola 2025x Refresh 1/Modelica/Library/Modelica 4.0.0`
- `OMC_LIB` = `C:/Users/yangrui/AppData/Roaming/.openmodelica/libraries`
- `DYM_LIB` = `C:/Program Files/Dymola 2025x Refresh 1/Modelica/Library`
- `TS_SIM` = `C:/dev/OpenModelica/testsuite/simulation/modelica`
- `TS_FLAT` = `C:/dev/OpenModelica/testsuite/flattening/modelica`

## 结论摘要

1. 两份 MSL 文件规模完全一致：各 2508 个 `.mo`；`/Examples/` 下各 595 个 `.mo`，其中含 `experiment(` 的文件各 404 个、出现次数各 414 处。
2. `package.mo` 顶层注解的 `version="4.0.0"` / `versionDate="2020-06-04"` / `dateModified = "2020-06-04 11:00:00Z"` 两边逐字相同；唯一不同是 `revisionId`：OMC 为 `8971ba054 2023-05-24 17:39:48 +0000`（OpenModelica 维护分支提交），Dymola 为 `6626538a2 2020-06-04 19:56:34 +0200`（MSL 4.0.0 官方发布提交），Dymola 另多一行 `Dymola(checkSum=...)`。
3. `diff -rq`：内容不同 78 个文件（70 个 `.mo` + 8 个非 `.mo`），仅一方有 6 项（OMC 独有 3：2 张 png + `openmodelica.metadata.json`；Dymola 独有 3：`help/`、`libraryinfo.mos`、`Resources/Library/`）。
4. 差异性质**不全是**维护修补。大头是 maint/4.0.x 修补（新增 `Version_4_0_1` 发行说明、`choicesAllMatching=true` 规范化、补 `final` 修饰符），但也有会改变平坦化结果或数值的实质变更：`M_OLine` 新增参数 `useInternalGround` 与连接器 `refPin`（差 139 行）、`R134a` 样条系数由 p 基改为 T 基、`Blocks/Tables` 的外部函数去掉了 `Include=` 注解。三方对比时这 70 个文件不能假定两边语义等价。
5. `ModelicaServices`：Dymola 版自称 "Dymola implementation"，`target="Dymola"`，898 行，`dateModified 2023-01-25`；OMC 版 305 行、`revisionId` 与 MSL 同为 `8971ba054`。两者差 1203 行、`package.order` 也不同（Dymola 有 `FlexibleShape`，OMC 有 `System`）。`Complex`：仅 `revisionId` 一行不同（Dymola 侧是未展开的 `$Format:%h %ci$` 占位符）。
6. Dymola 额外带 `ModelicaServices 4.1.0`（`uses(Modelica(version="4.0.0"))`）；OMC 库目录没有任何 4.1.0；探针：`omc 1.25.0` 对 `loadModel(Modelica,{"4.1.0"})` 返回 `false` 并报 "Failed to load package"。
7. `TS_SIM`：383 个 `.mo`、1269 个 `.mos`。**生效的**（非注释行）`loadModel(Modelica*` 出现在 239 个 `.mos`（其中 `ModelicaTest` 19、`Modelica_Synchronous` 2、`Modelica_DeviceDrivers` 1）；999 个 `.mos` 只用 `loadFile`/`loadString`；2 个两者皆无。另有 3 个 `.mos` 的 `loadModel(Modelica` 只出现在注释行——朴素 `grep -l` 会给出 242，多算 3。
8. 生效版本串分布（按文件）：3.2.1 → 64，3.2.2 → 44，3.2.3 → 43，4.0.0 → 25，2.2.2 → 25，无版本 → 11，3.1 → 4，4.1.0 → 2，3.2 → 1，2.2 → 1。库目录只装了 `Modelica 3.2.3+maint.om` 与 `4.0.0+maint.om`；探针：`{"3.2.1"}`→3.2.3+maint.om，`{"2.2.2"}`→4.0.0+maint.om，无版本→4.0.0+maint.om。
9. `TS_FLAT`：2628 个 `.mo`，其中 2514 个含 `// Result:`；126 个 `.mos`。`// status:` 标记：correct 1918、incorrect 636、wrong 3、erroneous 1（合计 2558）。`scodeinst/` 一个子目录就有 1325 个。没有任何 `.mo` 用 `loadModel(Modelica`；`msl/` 下 60 个文件自带 stub `package Modelica`；仅 30 个文件引用 `Modelica.` 且不自带 stub。
10. 对规格的含义：三方对比以 MSL 4.0.0 为基准是成立的（版本三元组一致），但需把 70 个差异 `.mo` 列为"已知不等价"清单，`ModelicaServices` 整体排除；OMC `.mos` 语料里约 2/3 的 MSL 依赖指向 3.2.x（被 OMC 静默映射到 3.2.3+maint.om / 4.0.0+maint.om）。

## 逐项事实

### 1. OMC 的 MSL（`OMC_MSL`）

命令与输出：
```
find "$OMC_MSL" -name '*.mo' | wc -l                                             → 2508
find "$OMC_MSL" -path '*/Examples/*' -name '*.mo' | wc -l                        → 595
find ... -path '*/Examples/*' -name '*.mo' -print0 | xargs -0 grep -l 'experiment(' | wc -l   → 404
find ... -print0 | xargs -0 grep -c 'experiment(' | awk -F: '{s+=$NF} END{print s}'          → 414
```
（注意路径含空格与 `+`，必须 `-print0 | xargs -0`；第一次用裸 `xargs` 时 grep 全部报 "No such file"。）

`OMC_MSL/package.mo:8957-8961` 顶层注解原句：
```
version="4.0.0",
versionDate="2020-06-04",
dateModified = "2020-06-04 11:00:00Z",
revisionId="8971ba054 2023-05-24 17:39:48 +0000",
uses(Complex(version="4.0.0"), ModelicaServices(version="4.0.0")),
```
`OMC_MSL/package.mo:8963`：`from(version={"3.0", "3.0.1", "3.1", "3.2", "3.2.1", "3.2.2", "3.2.3"}, script="modelica://Modelica/Resources/Scripts/Conversion/ConvertModelica_from_3.2.3_to_4.0.0.mos")`。

`OMC_MSL/openmodelica.metadata.json`（单行）："sha":"8971ba0546b1136aeaca67b039eeadfa19d08bf6", "version":"4.0.0+maint.om", "zipfile":"https://github.com/OpenModelica/OpenModelica-ModelicaStandardLibrary/archive/8971ba05...zip"。
即 OMC 这份 MSL 来自 OpenModelica 自己维护的 fork，不是 modelica/ModelicaStandardLibrary 的 v4.0.0 tag。

`OMC_MSL/package.mo:2353`：`class Version_4_0_1 "Version 4.0.1 (Month D, 20YY)"`（Dymola 侧没有这个类，见 §3）。

### 2. Dymola 的 MSL（`DYM_MSL`）

同样命令：`.mo` 总数 2508；`/Examples/` 下 595；含 `experiment(` 文件 404；出现 414 次。与 OMC 完全一致。

`DYM_MSL/package.mo:8927-8932` 原句：
```
version="4.0.0",
versionDate="2020-06-04",
dateModified = "2020-06-04 11:00:00Z",
revisionId="6626538a2 2020-06-04 19:56:34 +0200",
uses(Complex(version="4.0.0"), ModelicaServices(version="4.0.0")),
Dymola(checkSum="469888163:3704811029"),
```
`package.mo` 行数：OMC 9040，Dymola 9011（差 29 行 = `Version_4_0_1` 块 30 行 − 1 行 checkSum，与 §3 的 diff 吻合）。

Dymola 独有的 `DYM_MSL/libraryinfo.mos`（共 14 行，Dymola 菜单元数据），关键行：
```
  reference="Modelica",
  version="4.0.0",
LibraryInfoPreload("Modelica", "Modelica Standard Library");
```
Dymola 独有 `DYM_MSL/help/`（4708 个文件）与 `DYM_MSL/Resources/Library/{linux64,win32,win64}/`（预编译 `ModelicaIO/ModelicaMatIO/ModelicaStandardTables/zlib` 静态库，`win64/` 下另有 `gcc510/`）。OMC 侧无 `Resources/Library`。

### 3. 两份 MSL 的差异

```
diff -rq "$OMC_MSL" "$DYM_MSL" > r7-diffrq.txt        → exit=1，共 84 行
grep -c "^Only in $OMC_MSL"     → 3
grep -c "^Only in $DYM_MSL"     → 3
grep -c '^Files .* differ$'     → 78
… | grep -c '\.mo differ$'      → 70
```
仅一方有（全部 6 项）：
```
Only in OMC/Resources/Images/Electrical/Analog/Lines: segment.png
Only in OMC/Resources/Images/Electrical/Analog/Lines: segment_last.png
Only in OMC: openmodelica.metadata.json
Only in DYM/Resources: Library
Only in DYM: help
Only in DYM: libraryinfo.mos
```
非 `.mo` 的 8 个差异文件（`diff | grep -c '^[<>]'` 行数）：`ModelicaInternal.c` 312、`ModelicaStandardTables.c` 54、`ModelicaStrings.c` 4、`safe-math.h` 4、`ModelicaInternal.h` 2、`Generate-ReleaseNotes.py`（未计行）、`LICENSE_ModelicaStandardTables.txt` 2、`ConvertModelica_from_3.2.3_to_4.0.0.mos` 4。
转换脚本的 4 行是 OMC 独有追加（`OMC_MSL/Resources/Scripts/Conversion/ConvertModelica_from_3.2.3_to_4.0.0.mos:1943-1945`）：
```
// Reported by https://trac.openmodelica.org/OpenModelica/ticket/5297#comment:13
convertElement("Modelica.Media.Interfaces.PartialCondensingGases.gasConstant", "R", "R_s");
```

内容不同的 `.mo`（按 `diff | grep -c '^[<>]'` 行数降序，前 20）：
```
139 Electrical/Analog/Lines/M_OLine.mo
 50 Blocks/Tables.mo
 42 Electrical/PowerConverters/DCAC/Control/IntersectivePWM.mo
 33 package.mo
 32 Fluid/Examples/HeatExchanger.mo
 24 Media/R134a.mo
 17 Blocks/Routing.mo
 12 Math/Random.mo
 12 Electrical/PowerConverters/Examples/DCDC/ExampleTemplates/ChopperStepUp.mo
 12 Electrical/Analog/Lines/OLine.mo
 12 Blocks/Types.mo
 11 Electrical/PowerConverters/Examples/DCDC/ExampleTemplates/ChopperStepDown.mo
 10 Mechanics/MultiBody/Joints/Constraints/Prismatic.mo
 10 Electrical/Analog/Lines/ULine.mo
  8 Magnetic/QuasiStatic/FundamentalWave/Examples/BasicMachines/SynchronousMachines/SMPM_CurrentSource.mo
  8 Fluid/Pipes.mo
  8 Electrical/PowerConverters/DCAC/Control/SVPWM.mo
  8 Electrical/Analog/Examples/Utilities/DirectCapacitor.mo
  6 Utilities/Internal.mo
  6 Thermal/FluidHeatFlow/Examples/TestCylinder.mo
```
其余 50 个每个 ≤ 6 行（完整 70 行清单在本次会话的命令输出中；`diff -rq` 原始输出存于同目录 `r7-diffrq.txt`）。按目录粗分：`Electrical/` 36 个、`Magnetic/` 10、`Fluid/` 7、`Blocks/` 3、`Mechanics/` 3、`Media/` 2、`Math/` 2、其他 7。

抽样 diff（方向 OMC → Dymola，`<` 为 OMC 独有，`>` 为 Dymola 独有）：

**(a) `package.mo`**（33 行）—— 纯维护性：`2353,2382d2352` 删除整块 `class Version_4_0_1 … end Version_4_0_1;`（发行说明，内文只列了一条修复：`CombiTable2Ds/CombiTable2Dv` 在 `HoldLastPoint` 外推下导数恒为零）；`8960c8930` 的 `revisionId` 互换；`8961a8932` Dymola 增 `Dymola(checkSum="469888163:3704811029")`。

**(b) `Electrical/Analog/Lines/M_OLine.mo`**（139 行）—— 实质变更。OMC 独有（`OMC_MSL/Electrical/Analog/Lines/M_OLine.mo:29-30`）：
```
  parameter Boolean useInternalGround=true "= true if internal ground is used, otherwise use reference pin"
    annotation(Evaluate=true, HideResult=true, choices(checkBox=true));
```
OMC 独有 `:56-59`：`Modelica.Electrical.Analog.Interfaces.NegativePin refPin "Reference pin"`；OMC 把 `M` 从 `Basic.Ground` 改为 `Ideal.Short`（OMC `:99-102` vs Dymola `:94-95`）。→ 连接器集合、参数集合、方程都变了；同名模型两边平坦化结果不可比。

**(c) `Blocks/Tables.mo`**（50 行，25 处同型替换）—— 外部函数注解差异，`:973` 等：
```
< annotation (IncludeDirectory="modelica://Modelica/Resources/C-Sources", Library={"ModelicaStandardTables", ...});
> annotation (IncludeDirectory="modelica://Modelica/Resources/C-Sources", Include="#include \"ModelicaStandardTables.h\"", Library={...});
```
OMC 版去掉了 `Include=`。对代码生成阶段（外部函数 C 头文件包含）有影响，对平坦化方程无影响。

**(d) 小型修补样例**：`Electrical/Polyphase/Basic/Resistor.mo:15-16` OMC 多 `final T=T,`；`Fluid/Examples/HeatExchanger.mo:114/117/130/136` OMC 为 `choicesAllMatching=true`、Dymola 为裸 `choicesAllMatching`；`IntersectivePWM.mo:18-33` OMC 补了 `final m=m`、`final k=0`、`each final nperiod=-1` 等修饰符；`Media/R134a.mo:2474-2479` OMC 用 `T_breaks/dlt_coef/dvt_coef`（对 T 的样条）替换 Dymola 的 `p_breaks/dl_coef/dv_coef`（对 p 的样条）——这是数值算法变更。

### 4. ModelicaServices 与 Complex

`OMC_LIB` 目录内容（`ls`）：`Buildings 9.1.0/`、`Complex 4.0.0+maint.om/`、`Modelica 3.2.3+maint.om/`、`Modelica 4.0.0+maint.om/`、`ModelicaReference 4.0.0+maint.om/`、`ModelicaServices 4.0.0+maint.om/`、`ObsoleteModelica4 4.0.0+maint.om/`、`index.json`。
`DYM_LIB` 相关条目：`Complex.mo`、`ModelicaServices 4.0.0/`、`ModelicaServices 4.1.0/`、`ObsoleteModelica4.mo`、`ModelicaReference/`（另有 40 余个 Dymola 自带库，未统计）。

`DYM_LIB/ModelicaServices 4.0.0/package.mo:3`：`"ModelicaServices (Dymola implementation) - Models and functions used in the Modelica Standard Library requiring a tool specific implementation"`；`:5` `constant String target="Dymola"`；`:829-834`：
```
  version="4.0.0",
  Dymola(checkSum="1620747660:3470466242"),
  versionBuild=1,
  versionDate="2020-01-15",
  dateModified = "2023-01-25 10:08:00Z",
  uses(Modelica(version="4.0.0")),
```
`DYM_LIB/ModelicaServices 4.1.0/package.mo:828-833`：`version="4.1.0"`, `versionBuild=1`, `versionDate="2024-01-12"`, `dateModified = "2024-01-12 19:40:00Z"`, `uses(Modelica(version="4.0.0"))`。
`OMC_LIB/ModelicaServices 4.0.0+maint.om/package.mo:237-240`：
```
    version="4.0.0",
    versionDate="2020-06-04",
    dateModified = "2020-06-04 11:00:00Z",
    revisionId="8971ba054 2023-05-24 17:39:48 +0000",
```
对比：`wc -l` Dymola 898 行 / OMC 305 行；`diff | grep -c '^[<>]'` = 1203；`diff -rq` 报 `package.mo` 与 `package.order` 均不同。`package.order` 差异：Dymola `target/UsersGuide/Animation/FlexibleShape/ExternalReferences/Machine/Types`，OMC `target/UsersGuide/Animation/ExternalReferences/Machine/System/Types`。Dymola 文件内 `grep -ci dymola` = 23。

`DYM_LIB/Complex.mo:238-241`：
```
version="4.0.0",
versionDate="2020-06-04",
dateModified = "2020-06-04 11:00:00Z",
revisionId="$Format:%h %ci$",
```
`OMC_LIB/Complex 4.0.0+maint.om/package.mo:238-241` 前三行相同，`revisionId="8971ba054 2023-05-24 17:39:48 +0000"`。`diff` 全文只有这 1 处（2 行）。

### 5. OMC testsuite `simulation/modelica`

```
find "$TS_SIM" -name '*.mo'  | wc -l   → 383
find "$TS_SIM" -name '*.mos' | wc -l   → 1269
朴素统计：find … -name '*.mos' -print0 | xargs -0 grep -l 'loadModel(Modelica' | wc -l → 242
朴素统计：… grep -L 'loadModel(' | xargs -0 grep -l -E 'loadFile\(|loadString\(' | wc -l → 996
```
朴素统计的坑：`TS_SIM/algorithms_functions/FuncDer.mos:10` 是 `//loadModel(Modelica, {"3.2.3"});`（注释掉的）。改用 gawk 按"行首非 `//`"分类（脚本 `r7-classify.awk`，逐文件输出 `am/cm/aa/lf` 标志到 `r7-mos-class.txt`）：
```
生效 loadModel(Modelica*  的 .mos                → 239
loadModel(Modelica* 只出现在注释行的 .mos           → 3
生效 loadModel( 任意库 的 .mos                   → 268
无生效 loadModel( 但有生效 loadFile/loadString    → 999
两者皆无                                        → 2   (others/NoLoadModel.mos, others/unitConvertTests.mos)
生效 loadModel(Modelica* 且同时 loadFile/loadString → 181
```
生效版本串分布（`grep -h 'loadModel(Modelica' | grep -v '^\s*//' | grep -o 'loadModel(Modelica[^)]*)' | sort | uniq -c`；按文件计的"首次出现"分布数字完全相同）：
```
 64 loadModel(Modelica, {"3.2.1"})      25 loadModel(Modelica, {"2.2.2"})     6 loadModel(ModelicaTest, {"4.0.0"})
 44 loadModel(Modelica, {"3.2.2"})      11 loadModel(Modelica)                4 loadModel(Modelica, {"3.1"})
 43 loadModel(Modelica, {"3.2.3"})       7 loadModel(ModelicaTest)            2 loadModel(Modelica_Synchronous)
 25 loadModel(Modelica, {"4.0.0"})       6 loadModel(ModelicaTest, {"4.1.0"}) 2 loadModel(Modelica, {"4.1.0"})
  1 loadModel(Modelica_DeviceDrivers)    1 loadModel(Modelica, {"3.2"})       1 loadModel(Modelica, {"2.2"})
```
注释行里的：`loadModel(Modelica, {"3.2.3"})` 2、`loadModel(ModelicaTest)` 1、`loadModel(Modelica)` 1。
严格只算包名为 `Modelica` 的：220 个文件（64+44+43+25+25+11+4+2+1+1）；其中请求 4.0.0 及以上的仅 27 个。
样例锚点：`TS_SIM/built_in_functions/spatialDistribution/pulseInput.mos:8` `loadModel(Modelica, {"4.0.0"}); getErrorString();`；`TS_SIM/NBackend/alias/Alias.mos:5` `loadString("`（NBackend 197 个 `.mos` 基本走 `loadString`，只有 7 个 `.mo`）。
子目录规模（`.mos`/`.mo`）：NBackend 197/7、tearing 108/22、initialization 91/1、others 90/39、arrays 68/57、nonlinear_system 61/6、algorithms_functions 53/33、equations 48/39、solver 48/10、unitcheck 44/0、commonSubExp 40/1、msl22 37/17，其余各 ≤ 35。

探针（`which omc` → `/c/Program Files/OpenModelica1.25.0-64bit/bin/omc`，`omc --version` → `OpenModelica v1.25.0 (64-bit)`；每个脚本 `timeout 60 omc x.mos`，内容 `loadModel(Modelica, {V}); getErrorString(); getSourceFile(Modelica);`）：
```
{"4.0.0"} → true, ".../Modelica 4.0.0+maint.om/package.mo"
{"3.2.3"} → true, ".../Modelica 3.2.3+maint.om/package.mo"
{"3.2.1"} → true, ".../Modelica 3.2.3+maint.om/package.mo"
{"2.2.2"} → true, ".../Modelica 4.0.0+maint.om/package.mo"
无版本    → true, ".../Modelica 4.0.0+maint.om/package.mo"
{"4.1.0"} → false, "Error: Failed to load package Modelica (4.1.0) using MODELICAPATH C:/Users/yangrui/AppData/Roaming/.openmodelica/libraries/."
```

### 6. OMC testsuite `flattening/modelica`

```
find "$TS_FLAT" -name '*.mo' | wc -l                                  → 2628
find … -name '*.mo' -print0 | xargs -0 grep -l '// Result:' | wc -l   → 2514
find "$TS_FLAT" -name '*.mos' | wc -l                                 → 126
find … -print0 | xargs -0 grep -l -i '// status:' | wc -l             → 2558
状态值分布（大小写/空格归一后）：correct 1918，incorrect 636，wrong 3，erroneous 1
含 'loadModel(Modelica' 的 .mo                                        → 0
引用 'Modelica.' 的 .mo                                                → 133；其中不自带 `package Modelica` stub 的 → 30
```
按子目录（`.mo` / 含 Result）：scodeinst 1325/1313、arrays 184/171、algorithms-functions 128/124、others 103/96、declarations 100/97、modification 86/83、built-in-functions 79/79、connectors 79/75、redeclare 68/66、msl 60/60、equations 57/55、extends 56/49、operators 56/54、mosfiles 47/1，其余各 ≤ 33。
格式锚点：`TS_FLAT/msl/BC1.mo:1-3` 为 `// name: … / // keywords: … / // status:   correct`，`:8` `package Modelica "Modelica Standard Library"`（测试文件内嵌 stub），`:950` `// Result:`，末行 `// endResult`。`TS_FLAT/scodeinst/ActualStream.mo:1-3` 同一头部格式。
运行器：`C:/dev/OpenModelica/testsuite/rtest:146` 读 `$info{"cflags"}`，`:173` 拼成 `omc --running-testsuite=… +locale=C $omcflags $cflags $f`；rtest 中没有出现 `loadModel` 或 `Modelica` 字样，即 MSL 不会被运行器自动加载。`// cflags:` 在 `.mo` 中仅 13 处（5 个 `-d=-newInst`，其余 `-i=<类名>`）。

## 未找到或待探针验证

- 未找到：Dymola 安装里 MSL 4.0.0 的"补丁级"版本标识（无 `Version_4_0_1`，`revisionId` 指向 2020-06-04 官方发布提交）；Dymola 是否在内部对 MSL 打过不写入 `.mo` 的补丁，本调查无法判断。
- 未找到：OMC `index.json` 中 `Modelica 4.0.0+maint.om` 与上游 `v4.0.0` tag 之间的 changelog；只有 metadata 的 sha `8971ba05…`。要精确判定 70 个差异文件哪些是上游 maint/4.0.x 回合、哪些是 OMC 私有改动，需 `git log` 该 fork（本机无该仓库，未做）。
- 待探针：`loadModel(Modelica,{"2.2.2"})` 为何被解析到 4.0.0+maint.om 而 `{"3.2.1"}` 解析到 3.2.3+maint.om（推测与 `conversion(noneFromVersion/from)` 注解有关，未在 OMC 源码中核对函数）。与 msl22 子目录（37 个 `.mos`）的通过与否直接相关。
- 待探针：`Blocks/Tables.mo` 去掉 `Include=` 后 OMC 如何找到 `ModelicaStandardTables.h`（推测靠 `IncludeDirectory` + 运行时库预编译，未核对 OMC 代码生成代码）。
- 待探针：Dymola 对 `ModelicaServices 4.0.0` 与 `4.1.0` 同时存在时的选择规则（`uses(ModelicaServices(version="4.0.0"))` 应选 4.0.0，未实跑验证，且禁止启动 Dymola）。
- `TS_FLAT` 中 30 个引用 `Modelica.` 且不自带 stub 的 `.mo`（如 `extends/Oscillator.mo`、`types/RefinedSimpleCircuitValid.mo`）究竟依赖哪份 MSL、还是预期报错（status incorrect），未逐个查看。

## 对规格的建议

1. 三方对比语料的 MSL 基准定为 4.0.0，但规格里要写明"OMC 侧实际是 `4.0.0+maint.om`（sha 8971ba05，2023-05-24）"，并附上本报告的 70 个差异 `.mo` 清单作为"已知不等价文件"——对比工具应自动跳过或单独标记这些模型，而不是让它们进入 L2/L3 数值验收。
2. `ModelicaServices` 整包从对比集合中排除（MLS 设计上即工具特定实现，两边差 1203 行）。`Complex` 可视为等价。
3. OMC `simulation/modelica` 的 1269 个 `.mos` 中仅 27 个文件生效请求 MSL ≥ 4.0.0，151 个请求 3.2.x、26 个请求 2.2.x；若 ymc 只实现 MSL 4.0.0 加载，这 177 个用例需要靠 OMC 那种"版本回退映射"才能跑，规格里应明确 ymc 的 `loadModel` 版本匹配策略（探针显示 OMC 会把 3.2.1 映射到 3.2.3+maint.om、把 2.2.2 映射到 4.0.0+maint.om）。
4. `flattening/modelica` 的 2514 个带 `// Result:` 的 `.mo` 是零 MSL 依赖的平坦化 oracle（`msl/` 用内嵌 stub），可作为 L1 验收的主语料；`scodeinst/`（1325 个）占一半，宜按子目录分批。
5. 统计脚本要点：路径含空格/`+` 必须 `-print0 | xargs -0`；`grep 'loadModel(Modelica'` 会同时匹配 `ModelicaTest`/`Modelica_Synchronous`，且会把注释行算进去（本例差 3 个文件）；本机 Git Bash 的 `grep -P` 因 locale 不可用，需用 gawk 或 `grep -v '^\s*//'`。
