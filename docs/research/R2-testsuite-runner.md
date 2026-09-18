# R2：OMC testsuite 运行器协议、ModelTesting 框架与参考文件

> 只读研究结论。路径相对 `C:\dev\OpenModelica\`（`TS/` = `testsuite/`，`OMC/` = `OMCompiler/Compiler/`）。行号来自 `cat -n` / `sed -n`。统计命令均为 Git Bash 实跑。

## 结论摘要（≤ 10 条）

1. **运行器协议只有一个：`TS/rtest`（Perl，582 行）。** 每个测试文件（.mo 或 .mos）用同一条命令行跑 omc：`$env $ulimit $OPENMODELICAHOME/bin/omc --running-testsuite=<tmp> +locale=C $omcflags $cflags <file> >>$log 2>&1`（rtest:266）。**.mo 与 .mos 的命令行构造没有任何区别**，唯一差异是比较容差：.mo 用 `eps_mo=1e-7`，.mos 用 `eps_mos=5e-3`（rtest:105-106, 320-324）。
2. **期望结果内嵌在测试文件中**：`// Result:` … `// endResult` 之间的行去掉 `// ` 前缀即为期望输出（rtest:366-376）；实际输出取 omc 的 stdout+stderr 合并日志，经"去行首空白、首段连续空白折成一个空格、剔除 GC Warning 等噪声行"后与期望做 **omc-diff 词法级比较**（rtest:296-325）。
3. **omc-diff 容差公式**：整数精确比较；实数 `|got-exp| > eps*|got| + eps` 才判失败（相对+绝对混合，同一个 eps）；文本 token 逐字符比较但空白折叠（`TS/difftool/omc-diff.l`:113-133, 48-68）。
4. **`//` 指令解析是通用正则** `^//[ \|]*([a-z_]*):[ \|]*(value)`，任何小写键都进 `%info`（rtest:381-387）；rtest 真正使用的键只有 `status/name/keywords/setup_command/cflags/env/teardown_command/stack_size`（rtest:348-355, 240-245）。`depends:` **rtest 本身不用**，是 `TS/partest/runtest.pl:164` 用它在沙箱里建符号链接。
5. **cflags 原样拼进命令行**（rtest:266 的 `$cflags`），`-d=-newInst`、`--newBackend`、`+simCodeTarget=Cpp` 都这样透传；`-d=-newInst` 是**关掉**默认开启的新前端（`OMC/Util/Flags.mo:300` `DEBUG_FLAG(66, "newInst", true, ...)`）。
6. **超时不是 rtest 做的**：rtest 只在 omcflags 里固定加 `--alarm=900`（rtest:9,11），由 omc 自己到点退出（Flags.mo:1091-1093）。ModelTesting 另加 `alarm(timeout)`=480 s 与仿真器 `-alarm=360`（`TS/simulation/libraries/common/ModelTestingDefaults.mos`:19-20；`ModelTesting.mos`:34,41）。
7. **环境变量在 rtest 里设，不在 Makefile 里**：`REFERENCEFILES=<root>testsuite/ReferenceFiles`、`OMLIBRARYCOMMON=<root>testsuite/simulation/libraries/common`、`OPENMODELICALIBRARY=<root>/libraries/.openmodelica/libraries`、`OPENMODELICAHOME`、`HOME`、`APPDATA`（rtest:47-53）。`MODELICAPATH` 在 rtest 与 `TS/Makefile` 中**未找到**。`Makefile.common`/`*.mk` **不存在**（find 无结果）。
8. **ModelTesting 框架 = 三个文件**：`ModelTesting.mo`（枚举）、`ModelTestingDefaults.mos`（21 行，全部默认值）、`ModelTesting.mos`（260 行，主逻辑）。默认：`VerifiedSimulation`、C runtime、`diffSimulationResults`、`relTol=0.01`、`absTol=0.0001`、`compareVars={}`、`referenceFileDir="msl32"`、参考文件 `$REFERENCEFILES/<dir>/<model>.mat`，`.mat.xz` 由脚本内 `system("xz --keep --decompress --force …")` 现场解压（ModelTesting.mos:155-157）。
9. **规格里提到的 `reference_reltol / reference_reltolDiffMinMax / reference_rangeDelta` 在 testsuite 中未找到**（grep 全树 0 命中）。对应物是 OMC API `diffSimulationResults` 的输入 `relTol=1e-3, relTolDiffMinMax=1e-4, rangeDelta=0.002`（`OMC/FrontEnd/ModelicaBuiltin.mo`:3363-3372）；ModelTesting.mos:195 只覆盖 `relTol`（传 0.01），其余两个走 API 默认。
10. **统计**：`simulation/modelica` 1269 个 .mos，1135 含 `simulate(`，**0 含 `compareVars`**，963 含 `-d=-newInst`，9 含 `simCodeTarget=Cpp`，196 含 `--newBackend`（全在 `NBackend/`）。`simulation/libraries` 1357 个 .mos，805 含 `compareVars`（msl32 279/278，msl32_cpp 278/277）。`ReferenceFiles` 489 个文件，484 个 `.mat.xz`，msl32 占 292。

## 逐项事实（带锚点与引用）

### A. rtest 命令行与流程

- **必须从 `testsuite/<子目录>` 运行**，按 `build/install_cmake`、`build_cmake/install_cmake`、`build` 顺序找 omc：rtest:22 `if ($cwd =~ m/(.*)testsuite\/(.+)$/) {`；rtest:27,32,37 三个 `-e "$1.../bin/$OMCEXE"` 分支；rtest:59 `print "You must run rtest from the testsuite (was run from $cwd)\n";`
- **环境变量**（rtest:47-53）：
  ```
  $ENV{'REFERENCEFILES'} = "$1testsuite/ReferenceFiles";
  $ENV{'OMLIBRARYCOMMON'} = "$1testsuite/simulation/libraries/common";
  $ENV{'OPENMODELICALIBRARY'} = "$1/libraries/.openmodelica/libraries";
  ```
- **omc-diff 版本门禁**：rtest:63 `system "$OPENMODELICAHOME/bin/omc-diff -v1.4";` 失败 `exit 2`（rtest:64-68）。
- **omcflags 来源**：rtest:8-12 `$omcflags=$ENV{"RTEST_OMCFLAGS"} . " --alarm=900"` 或仅 `"--alarm=900"`；命令行上以 `+` 开头的参数也追加进 `$omcflags`（rtest:468-469）。
- **命令行构造**（.mo/.mos 同一条）：rtest:266
  `system "$env $ulimit $OPENMODELICAHOME/bin/$OMCEXE --running-testsuite=$testTempFile$f +locale=C $omcflags $cflags $f >>$log 2>&1";`
  - `--running-testsuite=<文件>`：omc 把生成的临时文件名写进去，rtest 跑完逐行 `unlink`（rtest:268-274；`-nodelete` 可关）。Flags.mo:573-575 `CONFIG_FLAG(3, "running-testsuite", NONE(), INTERNAL(), STRING_FLAG(""), ... "Used when running the testsuite.")`。
  - `$ulimit` 仅当 `stack_size` 非空且非 OMDEV 时为 `ulimit -s N ;`（rtest:128-138）。
  - `$env` 在 OMDEV 下把 `:` 换成 `\;`（rtest:262-264）。
- **setup/teardown**：rtest:250-255 `system "$setup_command >>$log 2>&1"` 失败即打印 ` setup_command failed`；rtest:275-277 teardown 同样追加到日志。
- **日志与临时目录**：rtest:89-97 `$tmpdir = $tmpvar . "/omc-rtest-".$username."/$dirname"`（Windows 用 `%TEMP%`）；`$log = "$tmpdir/log-$f"`（rtest:248）；`equations-got / equations-expected / equations-diff / baseline` 四个文件（rtest:94-97）。
- **status 语义**（rtest:280-293）：`erroneous` → 直接打印 `erroneous` 并算通过；omc 退出码 ≠0 且 status=`correct` → `execution failed`；退出码 =0 且 status≠`correct` → `this test should have failed`。合法值正则 `^(erroneous|(in|)correct)$`（rtest:439）。
- **实际输出归一化**（rtest:296-316）：`s/^[ \t]*//; s/[ \t]+/ /;`（去行首空白；只把**第一段**连续空白折成一个空格）；跳过 `^Unexpected end of /proc/mounts line` 与 `^GC Warning:`；UCRT 下抹掉 `/X.mos_tempNNNN/` 路径段（rtest:303-305）。
- **期望输出提取**（rtest:363-389）：仅 `// Result:`..`// endResult` 范围；`s/^\/\/ Result://; s/^\/\/ endResult//; s/[ \t]+/ /;` 后取 `substr($_,3)`（`// ` 行）或 `substr($_,2)`（`//` 空行）。
- **比较与失败输出**：rtest:325 `system "$OPENMODELICAHOME/bin/omc-diff $epsilon $expected $got > $difference";` 非零 → 打印 `equation mismatch [time: N]`，并把 `diff -U 5 -w`（rtest:82）与 omc-diff 输出追加进日志（rtest:327-338）；成功打印 `ok [time: N]`（rtest:341）。
- **汇总行**：rtest:577 `printf "\n== %d out of %d tests failed [%s, time: %d]\n"`；`--return-with-error-code` 时有失败则 `exit 1`（rtest:580-582）。
- **基线模式 `-b`**（rtest:141-233）：重新跑一遍，把日志每行加 `// ` 前缀写回测试文件的 `// Result:` 块，并 `binmode` 强制 LF（rtest:223-228）。
- **其余 CLI**（rtest:463-510）：`--with-omc=`、`-v`、`-c`(dwdiff)、`-p`(pager)、`-k a,b`(keywords 过滤，全部匹配才跑，rtest:394-402)、`-s status`、`-l`(列 keywords 计数)、`-L`(列有 status 的文件名)、`-nolib`(忽略 `env:` 指令，rtest:385,504-505)。无文件参数直接退出（rtest:563-567）。

### B. omc-diff（`TS/difftool/omc-diff.l`，163 行，flex）

- 词法（:9-14）：`digit [0-9]`，`letter [^0-9\-\n\r]`；`T_REAL` = `"-"?{digit}+("."({digit}+))?(("e"|"E")("-"|"+")?{digit}+)?`；`T_TEXT` = `"-"?{letter}+`；四种换行都算 `T_NEWLINE`。
- 用法（:34-36）：`Usage (Normal): %s epsilon $file_expected $file_actual`，`0.0 < epsilon < 1.0`（main 检查 `eps < 0.0 || eps > 1.0`，:153）。
- 比较（:96-99 token 类型不同即失败；:105-110 文本 `strcmp_omc_diff`；:113-121 两边都是纯整数则 `l1 != l2` 失败；:126-133）：
  ```
  double err = fabs(d2-d1);
  if (err > eps*fabs(d2)+eps) {   // d2 = actual
  ```
- 文本比较把连续空白折叠后逐字符比（:48-68）。

### C. Makefile 族

- `TS/Makefile`（884 行）只做目录分发：每个 `xxx.log: omc-diff` 规则形如 `$(MAKE) -C simulation/modelica/NBackend/alias/ -f Makefile test > $@`（:345-347）；`omc-diff: $(MAKE) -C difftool`（:218-219）；`ReferenceFiles: $(MAKE) -C ReferenceFiles`（:202-203）。grep `REFERENCEFILES|OPENMODELICALIBRARY|MODELICAPATH` 在该文件 **0 命中**。
- 子目录 Makefile（例 `TS/simulation/modelica/NBackend/basics/Makefile`）：:1 `TEST = ../../../../rtest -v`；:3-17 `TESTFILES = \ emptyModel.mos ...`；:21 `FAILINGTESTFILES =`；:34-39 `test:` → `@$(TEST) $(TESTFILES)`；:42-44 `clean` 删除不在 `DEPENDENCIES` 里的文件。
- `find testsuite -name Makefile.common -o -name "*.mk"` → **无结果**。
- `TS/CMakeLists.txt`（12 行）只 `omc_add_subdirectory(difftool)`、`omc_add_subdirectory(ReferenceFiles)` 和 `testsuite-depends` 目标（依赖 `ffi-test-lib libs-for-testing omc-diff reference-files`）；无 `add_test`。`TS/CTest/Readme.md`:1 自称 "Experimental CTest"。
- `TS/partest/runtest.pl`（并行沙箱驱动）：:233 `my $rtest = $test_suit_path_rel . "rtest $rtest_extra_args -v -nolib ";` :239 `"$rtest $test > $test.test_log 2>&1"`；沙箱链接规则 :163-164 `elsif (/setup_command.*\s(.*\.c)/) { make_link($1); } elsif (/depends: *([A-Za-z0-9_.-]*)/) { make_link($1); }`，另对 `loadFile/runScript/importFMU/system("gcc ...")/external "…h"` 建链（:165-183）；:185-189 把 `env: OPENMODELICALIBRARY=…` 的相对路径多加一层 `../` 后设进 `%ENV`。

### D. `//` 指令在全树的使用（grep `--include=*.mos --include=*.mo`）

- `status:` 取值：`correct 6116`、`incorrect 695`、`erroneous 21`；另有非法值 `omc 2`、`wrong 3`（rtest 会报 `unknown testcase status`）。
- `cflags:` 出现在 2924 个文件；Top 值：`-d=-newInst 2000`、`+simCodeTarget=Cpp 256`、`-d=newInst 239`、空 114、`--newBackend 68`、`+g=MetaModelica -d=-newInst 58`、`+simCodeTarget=Cpp -d=-newInst 58`、`-g=MetaModelica -d=gen -d=-newInst 45`、`+std=2.x -d=-newInst 40`、`+d=backenddaeinfo -d=-newInst 48（两种缩进）`、`--newBackend -d=dumpSolve 4`。
- `depends:` 123 个文件（样例 `// depends: matrix.mat`、`// depends: ext_ExternalFunctionResultOrder.c`、`// depends: TestLibrary`）。
- `env:` 7 处：6 处 `LD_LIBRARY_PATH="$OPENMODELICAHOME/../testsuite/3rdParty/siemens/TTSE_libs/"`，1 处 `MODELICAUSERCFLAGS=-g`。
- `setup_command:` 11 个文件（均为 `gcc … -c -o X.o X.c` 编外部函数）；`teardown_command:` 1415 个文件；`stack_size:` **0 处**。

### E. ModelTesting 框架

- 定义处（`find -iname "ModelTesting*.mos"`）：`TS/simulation/libraries/common/ModelTesting.mos`、`…/ModelTestingDefaults.mos`，以及两份本地副本 `TS/openmodelica/cppruntime/hpcom/ModelTestingDefaults.mos`、`TS/openmodelica/cppruntime/libraries/msl32/ModelTestingDefaults.mos`。
- 引用统计：941 个 .mos 含 `modelTesting`；`runScript` 形式计数：`"../common/ModelTestingDefaults.mos"` 593+2，`"../../common/…"` 210，`"ModelTestingDefaults.mos"` 96+31+1，`getEnvironmentVar("OMLIBRARYCOMMON")+"/ModelTestingDefaults.mos"` 4+3，`"../common/ModelTesting.mos"` 1，`"../../../simulation/libraries/common/ModelTestingDefaults.mos"` 1。
- 枚举 `ModelTesting.mo`:3-14：`Kind = enumeration(Instantiation, Translation, Compilation, SuppressedSimulation, SimpleSimulation, SuppressedVerifiedSimulation, VerifiedSimulation)`；`SimulationRuntime = enumeration(C, Cpp)`；`DiffAlgorithm = enumeration(compareSimulationResults, diffSimulationResults)`。
- 默认值 `ModelTestingDefaults.mos`:1-21（逐行）：
  ```
  loadFile(getEnvironmentVar("OMLIBRARYCOMMON")+"/ModelTesting.mo");   // :2
  packageName := $TypeName(Modelica); packageVersion := "3.2.1";       // :4-5
  setCommandLineOptions("-d=nogen");                                    // :6
  modelTestingType := ...Kind.VerifiedSimulation; simulationRuntime := ...C;  // :7-8
  diffAlgorithm := ...DiffAlgorithm.diffSimulationResults;              // :9
  stopTime := 0.0; relTol := 0.01; absTol := 0.0001;                    // :10-12
  compareVars := fill("",0); referenceFileDir := "msl32"; referenceFile := "";  // :13-15
  modelTesting := getEnvironmentVar("OMLIBRARYCOMMON")+"/ModelTesting.mos";      // :16
  outputFormat :="mat"; simflags := ""; timeout := 480; timeoutSim := 360;       // :17-20
  ```
- 主流程 `ModelTesting.mos`：
  - :24-30 `b:=loadModel(packageName,{if packageVersion == "" then "default" else packageVersion});` 失败 `exit(1)`。
  - :34 `alarm(timeout);`；:38-42 Cpp → `setCommandLineOptions("+simCodeTarget=Cpp")`，否则 `simflags := simflags + " -abortSlowSimulation -alarm=" + String(timeoutSim);`
  - :43-58 Instantiation：`instantiateModel` → 打印 `Frontend succeeded/failed`；若随后 `translateModel` 竟成功则 `exit(1)`（"Please update the test"）。:59-73 Translation、:74-100 Compilation 同构（会真正启动可执行文件 `-noemit`，:91）。
  - :107-124 有 `compareVars` 时把变量名转正则拼成 `variableFilter := "time" + sum("|" + toRegex(var) ...)` 并 `simflags += " -emit_protected"`，否则 `variableFilter := ".*"`。
  - :126-130 `simulate(modelName, [stopTime=…,] outputFormat=…, simflags=…, variableFilter=…)`。
  - 打印格式：:137 `"Simulation options: " + options`；:144 `"Result file: " + resultFile`；:147 `"Messages: " + messages`；无结果文件 → :139-141 `"Error: Simulation did not produce a result-file"` + `exit(1)`。
  - :155 `referenceFile := if referenceFile == "" then getEnvironmentVar("REFERENCEFILES") + "/"+referenceFileDir+"/" + modelNameStr + ".mat" else referenceFile;`
  - :156-157 `if regularFileExists(referenceFile+".xz") then system("if test ! -f … || test ….xz -nt …; then xz --keep --decompress --force ….xz ; fi");`；:165-174 再试 `"../" + referenceFile`。
  - :178-181 结果文件改用相对名 `modelNameStr+"_res.mat"`（或 `_res.csv`）；`csvFile := modelNameStr + ".csv"`。
  - :185 `compareSimulationResults(resultFile,referenceFile,csvFile,relTol,absTol,compareVars)`，成功判据 `res[1] == "Files Equal!"`（:187）。
  - :195 `(res,failVars) := OpenModelica.Scripting.diffSimulationResults(resultFile,referenceFile,csvFile+"diff.",relTol,vars=compareVars);`
  - :207 成功打印 `"Files Equal!\n"`；失败 :210 `"Failed variables: " + sum(var for var in failVars)`，:213-215 生成 `<model>.minimal.mat` 并 `xz --best`，:225-230 每个失败变量写 `<model>.<var>.html`（`diffSimulationResultsHtml`），:231-232 `exit(1)`。
  - :240-243 VerifiedSimulation 但无参考文件 → `"Error: Reference file … does not exist …"` + `exit(1)`；SimpleSimulation 无参考文件只打印 Note（:245）。
- OMC API 签名（`OMC/FrontEnd/ModelicaBuiltin.mo`）：
  - :3363-3374 `function diffSimulationResults`：`input String actualFile; input String expectedFile; input String diffPrefix; input Real relTol = 1e-3; input Real relTolDiffMinMax = 1e-4 "…difference between the maximum and minimum of the signal"; input Real rangeDelta = 0.002 "x tolerance"; input String[:] vars = fill("",0); input Boolean keepEqualResults = false; output Boolean success; output String[:] failVars;`
  - :3337-3345 `function compareSimulationResults`：`filename, reffilename, logfilename, relTol = 0.01, absTol = 0.0001, vars = fill("",0); output String[:] result;`
  - :3314-3322 `function filterSimulationResults`：`inFile, outFile, vars, numberOfIntervals = 0 "0=Do not resample", removeDescription = false, hintReadAllVars = true; output Boolean success;`
- 典型库测试 `TS/simulation/libraries/msl32/Modelica.Blocks.Examples.PID_Controller.mos`：:9 `runScript("../common/ModelTestingDefaults.mos"); getErrorString();` :11-21 设 `modelTestingType/modelName/compareVars`；:23 `runScript(modelTesting);getErrorString();`；期望块 :25-41 依次为 `"true\n"`、`""`、枚举回显、模型名、变量数组、`Simulation options: startTime = 0.0, stopTime = 4.0, … simflags = ' -abortSlowSimulation -alarm=360 -emit_protected'`、`Result file: …_res.mat`、`Messages: LOG_SUCCESS …`、`Files Equal!`、`"true\n"`、`""`。
- 典型 NBackend 测试 `TS/simulation/modelica/NBackend/basics/helloWorld.mos`：:1-3 `// name / keywords: NewBackend / status: correct`（**无 cflags**）；:14 `setCommandLineOptions("--newBackend"); getErrorString();`；:16 `simulate(helloWorld); getErrorString();`；:18 `val(a, 0.0);`；期望块 :25-31 是 `record SimulationResult resultFile = "helloWorld_res.mat", simulationOptions = "startTime = 0.0, stopTime = 1.0, numberOfIntervals = 500, tolerance = 1e-06, method = 'dassl', …", messages = "LOG_SUCCESS …" end SimulationResult;`。
- `simulation/modelica` 里的参考比较不走 ModelTesting：107 个文件直接调 `compareSimulationResults`，116 个调 `diffSimulationResults`，61 个引用 `REFERENCEFILES`（例 `daemode/testDAEmodeDrumBoiler.mos`:14-15 `compareSimulationResults("…DrumBoiler_res.mat", getEnvironmentVar("REFERENCEFILES")+"/msl32/….mat", …)`）。

### F. ReferenceFiles

- `README.md`:3-6 "No reference file should be greater than 1MB, and ideally less than 200 kB"；:14 `filterSimulationResults("inFile.mat", "outFile.mat", {"x", "y", "z"});`；:19-20 `xz --best MyPackage/MyModel.mat` / `git add MyPackage/MyModel.mat.xz`。
- `Makefile`:1 `ALL_MAT := $(patsubst %.mat.xz,%.mat,$(wildcard */*.mat.xz))`；:5-6 `%.mat: %.mat.xz` → `xz --keep --decompress --force $< && touch $@`。
- `CMakeLists.txt`:3 `file(GLOB_RECURSE COMPRESSED_REF_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.mat.xz)`；:11-16 `add_custom_command(... COMMAND xz --keep --decompress --force ${compressed_file_path} OUTPUT ${output_file_path})`；:23-26 `add_custom_target(reference-files DEPENDS ${OMC_EXTRACTED_REFERENCE_FILES})`。
- 目录 → 库映射与文件数（`find . -type f | awk -F/ '{c[$2]++}'`；总 489）：

  | 目录 | 文件数 | 后缀 |
  |---|---|---|
  | msl32 | 293 | 292 `.mat.xz` + 1 `.txt` |
  | Modelica_Synchronous | 74 | `.mat.xz` |
  | ThermoSysPro | 71 | `.mat.xz` |
  | PlanarMechanics | 15 | `.mat.xz` |
  | resolveLoops | 8 | `.mat.xz` |
  | Modelica_DeviceDrivers | 7 | `.mat.xz` |
  | msl31 | 5 | `.mat.xz` |
  | hpcom | 5 | `.mat.xz` |
  | linear_system | 3 | `.mat.xz` |
  | solver | 2 | `.mat.xz` |
  | SiemensPower / cppruntime | 1 / 1 | `.mat.xz` |
  | msl40 | 1 | `.csv` |
  | 顶层 | 3 | `CMakeLists.txt` `Makefile` `README.md` |

  全树后缀分布：`mat.xz 484`、`txt 2`、`md 1`、`csv 1`、无后缀 1。

### G. 统计：`simulation/modelica/<子目录>`（命令：`find -name '*.mos'` + `grep -rl <pat> --include='*.mos'`，按目录 awk 分组）

总计：**mos=1269, simulate(=1135, compareVars=0, -d=-newInst=963, simCodeTarget=Cpp=9, --newBackend=196**。

| 子目录 | mos | simulate( | compareVars | -d=-newInst | Cpp | --newBackend |
|---|---|---|---|---|---|---|
| NBackend/ModelicaTest | 19 | 19 | 0 | 0 | 0 | 19 |
| NBackend/ScalableTestsuite | 26 | 26 | 0 | 0 | 0 | 26 |
| NBackend/alias | 10 | 10 | 0 | 0 | 0 | 10 |
| NBackend/array_handling | 41 | 41 | 0 | 0 | 0 | 40 |
| NBackend/basics | 15 | 14 | 0 | 0 | 0 | 15 |
| NBackend/clocked | 26 | 26 | 0 | 0 | 0 | 26 |
| NBackend/daemode | 1 | 1 | 0 | 0 | 0 | 1 |
| NBackend/differentation | 8 | 1 | 0 | 0 | 0 | 8 |
| NBackend/event_handling | 5 | 5 | 0 | 0 | 0 | 5 |
| NBackend/functions | 5 | 5 | 0 | 0 | 0 | 5 |
| NBackend/index_reduction | 4 | 4 | 0 | 0 | 0 | 4 |
| NBackend/init_xml | 7 | 0 | 0 | 0 | 0 | 7 |
| NBackend/initialization | 5 | 5 | 0 | 0 | 0 | 5 |
| NBackend/msl | 1 | 1 | 0 | 0 | 0 | 1 |
| NBackend/records | 8 | 8 | 0 | 0 | 0 | 8 |
| NBackend/simplification | 4 | 1 | 0 | 0 | 0 | 4 |
| NBackend/solve | 4 | 4 | 0 | 0 | 0 | 4 |
| NBackend/tearing | 4 | 4 | 0 | 0 | 0 | 4 |
| NBackend/tickets | 4 | 4 | 0 | 0 | 0 | 4 |
| algorithms_functions | 53 | 46 | 0 | 52 | 0 | 0 |
| arrays | 68 | 62 | 0 | 62 | 0 | 0 |
| asserts | 14 | 14 | 0 | 14 | 0 | 0 |
| built_in_functions | 20 | 18 | 0 | 16 | 0 | 0 |
| built_in_functions/delay | 2 | 2 | 0 | 1 | 0 | 0 |
| built_in_functions/spatialDistribution | 9 | 9 | 0 | 0 | 0 | 0 |
| commonSubExp | 40 | 40 | 0 | 40 | 0 | 0 |
| connectors | 16 | 14 | 0 | 16 | 0 | 0 |
| daemode | 13 | 12 | 0 | 12 | 0 | 0 |
| declarations | 2 | 2 | 0 | 2 | 0 | 0 |
| enums | 5 | 5 | 0 | 5 | 0 | 0 |
| equations | 48 | 47 | 0 | 47 | 0 | 0 |
| events | 35 | 35 | 0 | 34 | 0 | 0 |
| external_functions | 18 | 17 | 0 | 16 | 0 | 0 |
| functions_eval | 25 | 23 | 0 | 23 | 0 | 0 |
| hpcom | 19 | 19 | 0 | 19 | 0 | 0 |
| hpcom/BenchmarkSuite | 1 | 1 | 0 | 0 | 0 | 0 |
| hpcom/Profiling | 7 | 7 | 0 | 0 | 0 | 0 |
| inStream | 13 | 13 | 0 | 13 | 0 | 0 |
| indexreduction | 8 | 8 | 0 | 7 | 0 | 0 |
| inheritances | 5 | 4 | 0 | 5 | 0 | 0 |
| initialization | 91 | 74 | 0 | 71 | 0 | 0 |
| inlineFunction | 26 | 26 | 0 | 25 | 0 | 0 |
| jacobian | 1 | 0 | 0 | 1 | 0 | 0 |
| japaneselanguage | 1 | 1 | 0 | 1 | 0 | 0 |
| linear_system | 16 | 10 | 0 | 16 | 0 | 0 |
| msl22 | 37 | 37 | 0 | 37 | 0 | 0 |
| newtonDiagnostics | 1 | 1 | 0 | 0 | 0 | 0 |
| nonlinear_system | 61 | 61 | 0 | 61 | 0 | 0 |
| others | 90 | 87 | 0 | 89 | 0 | 0 |
| packages | 2 | 2 | 0 | 2 | 0 | 0 |
| parallel | 1 | 1 | 0 | 1 | 0 | 0 |
| parameters | 21 | 21 | 0 | 21 | 0 | 0 |
| parmodauto | 2 | 2 | 0 | 0 | 0 | 0 |
| qss | 9 | 9 | 0 | 9 | 0 | 0 |
| records | 12 | 11 | 0 | 4 | 0 | 0 |
| resolveLoops | 13 | 13 | 0 | 13 | 0 | 0 |
| solver | 40 | 39 | 0 | 40 | 0 | 0 |
| solver/gbode | 8 | 1 | 0 | 0 | 0 | 0 |
| start_value_selection | 8 | 6 | 0 | 5 | 0 | 0 |
| statemachines | 14 | 14 | 0 | 14 | 6 | 0 |
| synchronous | 22 | 13 | 0 | 19 | 3 | 0 |
| synchronous_c | 18 | 18 | 0 | 0 | 0 | 0 |
| tearing | 108 | 108 | 0 | 108 | 0 | 0 |
| types | 5 | 3 | 0 | 2 | 0 | 0 |
| unitcheck | 25 | 0 | 0 | 21 | 0 | 0 |
| unitcheck/OFunitcheck | 19 | 0 | 0 | 19 | 0 | 0 |

注：`--newBackend` 196 个文件中只有 72 个走 `// cflags:`，其余像 helloWorld.mos:14 那样在脚本内 `setCommandLineOptions("--newBackend")`。Flags.mo:1274-1276 `CONFIG_FLAG(140, "newBackend", … BOOL_FLAG(false) … "This also activates the new frontend. [WIP]")`。

### H. 统计：`simulation/libraries/<子目录>`（总 mos=1357，compareVars=805）

| 子目录 | mos | compareVars |
|---|---|---|
| msl32 | 279 | 278 |
| msl32_cpp | 278 | 277 |
| msl31 | 139 | 38 |
| msl31/fluid / msl31/media | 21 / 27 | 0 / 0 |
| ModelicaTest (+Utilities) | 86 (+2) | 0 |
| msl22/Electrical / Mechanics | 14 / 14 | 0 / 0 |
| msl40 | 1 | 0 |
| common | 4 | 3 |
| 3rdParty/PowerSystems_cpp | 129 | 128 |
| 3rdParty/DrModelica | 90 | 0 |
| 3rdParty/Modelica_Synchronous | 75 | 74 |
| 3rdParty/ThermoSysPro | 71 | 0 |
| 3rdParty/Exercises | 26 | 0 |
| 3rdParty/PlanarMechanics | 17 | 0 |
| 3rdParty/TestMediaFrancesco | 17 | 0 |
| 3rdParty/PNlib | 13 | 0 |
| 3rdParty/MathematicalAspects / ThermoPower | 11 / 11 | 0 / 0 |
| 3rdParty/Modelica_DeviceDrivers | 9 | 7 |
| 3rdParty/siemens | 6 | 0 |
| 3rdParty/TestMedia | 4 | 0 |
| 3rdParty/Buildings | 3 | 0 |
| 3rdParty/HumMod / SiemensPower | 2 / 2 | 0 |
| 3rdParty/{Annex60,FCSys,GitLibraries,ScalableTestSuite,VTT,Xogeny} | 各 1 | 0 |

## 未找到或待探针验证

1. `reference_reltol` / `reference_reltolDiffMinMax` / `reference_rangeDelta`：`grep -rn` 全 testsuite **0 命中**。仅有 `diffSimulationResults` 的 `relTol/relTolDiffMinMax/rangeDelta`（ModelicaBuiltin.mo:3368-3370）。这三个名字很可能来自 OpenModelicaLibraryTesting（不在本仓库），待核对。
2. `MODELICAPATH`：rtest 与 `TS/Makefile` 均**未设置**；库路径靠 `OPENMODELICALIBRARY`（rtest:53）及 `-nolib`/`env:` 机制。omc 内部如何合并 `OPENMODELICALIBRARY` 与 `MODELICAPATH` 未在本任务范围内查证。
3. `Makefile.common` / `*.mk`：**不存在**。每个子目录 Makefile 自带 `TEST = …/rtest -v`。
4. `stack_size:` 指令：rtest 支持（:128-138, 149-150）但全树 **0 个测试使用**。
5. `diffSimulationResults` 的比较算法（`relTolDiffMinMax`、`rangeDelta` 的精确判据）在 C 运行时实现，本任务未读其源码；ymc 若要复现"Files Equal!"判定需另开任务读运行时源码（具体文件路径未验证）。
6. `.mos` 在 ymc 侧的可行性依赖 OMC 脚本 API 子集（`loadString/loadFile/loadModel/setCommandLineOptions/simulate/val/runScript/getErrorString/print/system/regularFileExists/…`）与 `record SimulationResult` 的精确打印格式（helloWorld.mos:25-31）；本任务只确认了格式样例，未穷举 NBackend 目录用到的 API 集合。
7. `-d=-newInst` 的确切效果（旧前端 vs 新前端）只查到 Flags.mo:300-301 的定义（默认 true，"Enables new instantiation phase"）；这 963 个用例走的是**旧**实例化前端，其输出文本对 ymc 未必可比，需探针。

## 对规格的建议（可选）

- **ymc 的 L1/L2 验收运行器应逐字复刻 rtest 协议**：解析 `// name/keywords/status/cflags/depends/env/setup_command/teardown_command`；提取 `// Result:`…`// endResult`；同样的输出归一化（rtest:299-300）；用 omc-diff 的 token 语义与 `err > eps*|got|+eps` 判据，.mo 用 1e-7、.mos 用 5e-3。这样可直接复用 OMC 现成的 6116 个 `status: correct` 用例作为 oracle，而不必改写期望块。
- **优先级建议**：`simulation/modelica/NBackend/*`（196 个 .mos，全部 `--newBackend`，**0 个 compareVars**，纯文本比较，且不带 `-d=-newInst`）是与"新前端+新后端"目标最贴合、门槛最低的一批；其次是 `simulation/libraries/msl32`（279 个，278 个走 ModelTesting+参考文件）。
- **参考文件比较**：ymc 需要 (a) 读 `.mat.xz`（外部 `xz` 解压即可，与 ModelTesting.mos:157 一致）；(b) 实现等价于 `diffSimulationResults(relTol=0.01, relTolDiffMinMax=1e-4, rangeDelta=0.002, vars)` 的判据，或先用更简单的 `compareSimulationResults(relTol=0.01, absTol=1e-4)`（107 个 simulation/modelica 用例用的正是它）。
- **超时**：规格里应把 `--alarm`（rtest 900 s、ModelTesting 480 s、仿真器 360 s）列为运行器必备参数，避免 index-3 死循环那类问题拖死整批。
- **`-d=-newInst` 用例（963/1269）应单独标记**，不计入 ymc 的 L1 通过率分母，或只比较仿真数值而非文本输出。
