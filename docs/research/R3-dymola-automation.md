# R3：Dymola 2025x Refresh 1 无窗口自动化接口（只读文档调研）

> 来源范围：仅读取安装目录下的 Python 接口 HTML 文档、示例脚本、wheel 内的 Python 源码、`Modelica/Library` 下的 `.mo`/help 文件，以及 `Documentation/` 目录文件名。未启动 Dymola，未运行 omc。
>
> 路径缩写：
> - `DOC` = `C:\Program Files\Dymola 2025x Refresh 1\Modelica\Library\python_interface\doc`
> - `EX`  = `C:\Program Files\Dymola 2025x Refresh 1\Modelica\Library\python_interface\examples`
> - `WHL` = wheel `dymola-2025.1-py3-none-any.whl` 解压后的 `dymola\` 目录（解压到 scratchpad `whl\dymola\`；行号为 wheel 内源码行号）
> - `LIB` = `C:\Program Files\Dymola 2025x Refresh 1\Modelica\Library`
> - HTML 行号 = 原始 `.html` 文件行号（仅做去标签替换、未删行）。

## 结论摘要

1. 无窗口启动：`DymolaInterface(dymolapath='', port=-1, showwindow=False, nolibraryscripts=False, startDymola=True, dsls=False, closeDymola=True, customArgs='', kernel=False)`，`showwindow` 默认 False 即隐藏窗口；底层实际拉起 `bin64/Dymola.exe /externalinterface /serverport <port> /nowindow`，通过 `http://127.0.0.1:<port>` 的 JSON-RPC（POST JSON）通信。关闭用 `close()`（发 `exit` 再等进程退出，20 s 超时后 SIGTERM）；`__del__` 在 `closeDymola=True` 时自动关闭。
2. `translateModel(problem) -> bool`；`simulateModel(problem, startTime, stopTime, numberOfIntervals, outputInterval, method, tolerance, fixedstepsize, resultFile='dsres') -> bool`；`simulateExtendedModel(..., resultFile, initialNames, initialValues, finalNames, autoLoad) -> Tuple[bool, List[float]]`，第二项即 `finalNames` 的终值；`resultFile` 自动加扩展名（通常 `.mat`），默认 `dsres`。
3. 翻译日志文本：`getLastError() -> (message, numErrors, numWarnings, numMessages)`；`getLastErrorLog()` 只取其第一项；文档示例直接展示 `translateModel` 后 `getLastError()` 返回含 `Statistics / Original Model / Translated Model` 的完整统计文本（`Number of components`、`Continuous time states`、`Sizes of linear systems of equations` 等）。
4. 翻译失败不抛异常而是返回 False（`DymolaFunctionException` 文档明确）；可把 `DymolaInterfaceInternal.error_raises_exception=True` 改为出错即抛。
5. `ExecuteCommand(cmd)` 可执行任意脚本命令（含设 `Advanced.*` 标志），但"无法取回命令输出"，出错信息用 `getLastErrorLog()` 取。`RunScript(script, silent, scriptDir) -> bool` 执行 `.mos`。`clearlog()` 清命令窗口，`savelog(logfile='dymolalg.txt')` 保存命令窗口全文（`.txt` 保存全部，`.mos` 仅保存命令）。
6. 翻译统计的结构化载体：`dymola.model_management.TranslationStatistics{,_v_1_1,_v_1_2}` 类字段与 `getLastError` 文本一一对应；`ModelManagement.compareTranslationStatistics(modelName, reference_file, actual_file)` 比较"统计文件"，由 `checkLibrary/runTestCases` 的 `translationStructure` 生成。但文档未说明统计文件如何单独生成，需探针。
7. `dslog.txt` 仅在 `ModelManagement.getSimTimeFromDslog(fileName='dslog.txt', label='time for integration')` 出现；`dsmodel.mof`、`Advanced.OutputModelicaCode`、`Advanced.Translation.*`、`Advanced.EliminateAliasVariables`、`Advanced.CompileWith64` 在 Python 接口文档、`DymolaCommands`、`DataFiles`、`Dymola.mo`、`Utilities.mo` 中均未找到。
8. 编译器：`SetDymolaCompiler("vs", {"CCompiler=MSVC","MSVCDir=.../VC/Auxiliary/Build"})`、`GetDymolaCompiler() -> (str, List[str])`、`verifyCompiler() -> bool`（编译小 C 文件验证）。无 `CompileWith64` 记载。
9. 结果文件：`.mat`（Matlab v4，`readTrajectory/readTrajectoryNames/readTrajectorySize/readMatrix*`），`experimentSetupOutput(textual=..., doublePrecision=...)` 切换文本/双精度；CSV 转换靠库 `DataFiles.convertMATtoCSV(resultFileName, variableNames, csvFileName)`（`LIB\DataFiles 1.1.0\package.mo`），Python 接口文档本身没有 CSV/`convertMATFile`。
10. wheel 要求 `Requires-Python: >=3.7`，纯 Python（`py3-none-any`），版本号 `2025.1`；构造时核对 `DymolaVersionNumber()==2025.1`，不匹配即抛 `DymolaConnectionException`。

## 逐项事实

### 0. 文件清单

- `DOC\`：`dymola_enums.html`(433 行)、`dymola_exception.html`(121)、`dymola_interface.html`(3511)、`model_management.html`(3664)、`genindex.html`、`index.html`、`modules.html`、`py-modindex.html`、`search.html`、`searchindex.js`、`_sources\*.rst.txt`、`_static\`（`ls` 实测）。
- `EX\`：`A.mo`、`DymolaConnectExample.py`、`DymolaExample.py`、`ModelManagementExample.py`、`ModelManagementExample2.py`、`ModelManagementExample3.py`、`MultithreadingExample.py`、`SimulateExtendedExample.py`。
- wheel：`...\python_interface\dymola-2025.1-py3-none-any.whl`，内含 `dymola/dymola_interface.py`(208812 B)、`dymola_interface_internal.py`、`model_management.py`、`dymola_enums.py`、`dymola_exception.py`、`__init__.py`（`python -m zipfile -l` 实测）。
  - `dymola-2025.1.dist-info\METADATA:3-7`：`Version: 2025.1` / `Summary: Python interface for Dymola 2025x Refresh 1` / `Requires-Python: >=3.7`
  - `dymola-2025.1.dist-info\WHEEL:3-4`：`Root-Is-Purelib: true` / `Tag: py3-none-any`
  - `WHL\dymola_interface.py:51-53`：`__version__ = 2025.1` / `_DEFAULT_INSTALL_PATH = "Dymola 2025x Refresh 1"`
- `C:\Program Files\Dymola 2025x Refresh 1\Documentation\`（仅列名，未读 PDF，共 125 个文件）：`Dymola User Manual 1A/1B/2A/2B/2C.pdf`、`Dymola Full User Manual.pdf`、`Dymola Release Notes.pdf`、`Dymola Release Notes_PreviousVersions.pdf`、`Dymola Referential.pdf`、`Dymola Legal Notices.html/.pdf`、`AnalyzingEventLog.pdf`、`Debugging_Initialization_in_Dymola_2019.pdf`、`Dymola-analyzing-numeric-integration.pdf`、`Dymola-equation-incidence.pdf`、`Dymola-sparse-solvers.pdf`、`Dymola-sweep-parameters.pdf`、`Dymola-WSL.pdf`、`DymolaApplicationNoteHILS.pdf`、`Installing Visual Studio Build Tools for Dymola.pdf`、`Parameter arrays in Dymola.pdf`、`MigrationModelica2.pdf`、`ModelicaExamples.pdf`、`ModelicaSpec32Revision2/33Revision1/34/35/36.pdf`、`FMI-Specification-2.0.2.pdf`、`FMI_for_CoSimulation_v1.0.1.pdf`、`FMI_for_ModelExchange_v1.0.1.pdf`、`FMI30_documentation\`、`SDF\`、`MBSE_Primer*.pdf`、`efmi-*.pdf`、`esm98his.pdf`、`index.html`。
  - `Documentation\index.html`（去标签后第 18-20 行）：`Dymola User Manual 2B: Simulation Interfaces and Export` … `Scripting and Reporting, with Java/Python/JavaScript interfaces, and the report generator` → Python 接口的正式手册章节在 2B。
- `C:\Program Files\Dymola 2025x Refresh 1\bin64\`：存在 `Dymola.exe`（`ls | grep` 实测）；`dmc.exe`（kernel 模式用）未在 bin64 中找到。
- `LIB\ModelManagement 1.4.1\`：仅 `package.moe`（加密）+ `package.order` + `help\*.html`；源码不可读，只能读 help HTML。

### 1. 无窗口启动 / 关闭

- 构造签名 `DOC\dymola_interface.html:94`：`__init__(dymolapath: str = '', port: int = -1, showwindow: bool = False, nolibraryscripts: bool = False, startDymola: bool = True, dsls: bool = False, closeDymola: bool = True, customArgs: str = '', kernel: bool = False)`
- 各参数（`DOC\dymola_interface.html:99-107`）：
  - `:99` `dymolapath (str) – The path to the Dymola executable. Default is an empty string, which means that the path is auto-detected.`
  - `:100` `port (int) – ... Default is -1, which means that the port number is auto-assigned.`
  - `:101` `showwindow (bool) – Whether the Dymola window will be visible or not. The default value is False, which means that Dymola is hidden.`
  - `:103` `startDymola (bool) – If Dymola should be automatically started. The default value is True.`
  - `:105` `closeDymola (bool) – If Dymola should close automatically when out of scope. ... The default value is True.`
  - `:106` `customArgs (str) – Any custom arguments to the Dymola executable. For example, "-nosettings".`
  - `:107` `kernel (bool) – Use Dymola kernel instead of Dymola program. The default value is False.`
- 连接已运行实例 `DOC\dymola_interface.html:80-81`：`static connectTo(port: int)` / `Connect to a running instance of Dymola or DBM.`；实现 `WHL\dymola_interface.py:64`：`return DymolaInterface(port = port, startDymola = False)`；示例 `EX\DymolaConnectExample.py:8` `dymola = DymolaInterface.connectTo(8082)`。
- 实际命令行（`WHL\dymola_interface_internal.py:281-295`）：`args.append("/externalinterface")` / `args.append("/serverport")` / `args.append(str(port))` / `if self._nowindow: args.append("/nowindow")` / `if self._nolibraryscripts: args.append("/nolibraryscripts")` / `if self._dsls: args.append("/DSLS")`，再追加 `shlex.split(self._customArgs)`；`:299` `self._dymola_process = subprocess.Popen(args)`。
- 可执行文件定位（`WHL\dymola_interface_internal.py:105-107`）：注册表 `HKLM\SOFTWARE\Dassault Systemes\Dymola 2025x Refresh 1` 的 `InstallDir`；回退 `%ProgramW6432%\Dymola 2025x Refresh 1`（`:114-116`）；`:133-135`：kernel 用 `/bin64/dmc.exe`，否则 `/bin64/Dymola.exe`。
- 端口：`:88-89` `_FIRST_AVAILABLE_PORT: int = 44000` / `_LAST_AVAILABLE_PORT: int = 44400`（随机挑可用端口 `:331-336`）；端口被占则抛 `DymolaConnectionException`（`:259-260`）。
- 传输：`:460` `url = f"http://127.0.0.1:{self._portnumber}"`，`:449-454` JSON 体 `{"method": cmd, "params": params, "fileAllowed": ..., "id": ...}`，`:466` `method = "POST"`；`:474-480` 每次调用最多重试 10 次、`timeout=DymolaInterfaceInternal.timeout`（`:77` 默认 `None` 即无超时）。
- 版本校验 `:312-316`：`version = self.DymolaVersionNumber()` … `raise DymolaConnectionException("Mismatching Dymola version. ...")`。
- 关闭：`DOC\dymola_interface.html:160-162`：`close() → int` / `This method exits Dymola gracefully. It calls exit() and then waits until Dymola has terminated before returning.`；实现 `WHL\dymola_interface_internal.py:346-358`：先发 `exit` RPC，`self._dymola_process.wait(20)`，超时则 `os.kill(pid, signal.SIGTERM)`。`exit(status=0)`（`DOC:755-756` `Function to exit Dymola from script.`）Python 侧 `WHL\dymola_interface.py:847` 同样走 `super().close(status)`。`__del__`（`internal.py:200-204`）在 `_auto_close_dymola`（= `startDymola and closeDymola`，`:156`）时自动 `close()`。
- 多实例：`EX\MultithreadingExample.py:9-10,18` 每线程各起一个 `DymolaInterface()`，用 `dymola._portnumber` 取端口，`dymola.cd(path)` 切独立工作目录。

### 2. 打开 / 翻译 / 仿真函数签名与返回值

- `openModel`（`DOC\dymola_interface.html:1580-1588`）：`openModel(path: str, mustRead: bool = True, changeDirectory: bool = True) → bool`；`:1587` `By default, Dymola will automatically change directory to the one for the model file.`；返回 `true if successful`（`:1597`）。示例 `EX\SimulateExtendedExample.py:78`：`dymola.openModel(script_folder + "/A.mo")`。
- `openModelFile`（`:1610-1618`）：`openModelFile(modelName: str, path: str = '', version: str = '', newTab: bool = False) → bool`；`:1611` `Opens a Modelica model. It can be loaded from a file.`
- `translateModel`（`:3115-3116`）：`translateModel(problem: str) → bool` / `Compile the model (with current settings). This corresponds to Translate (Normal) in the menus.`；RPC 名同名（`WHL\dymola_interface.py:3623`）。Modelica 侧定义 `LIB\DymolaCommands 1.19\package.mo:520`。
- `simulateModel`（`:2900`）：`simulateModel(problem: str = '', startTime: float = 0.0, stopTime: float = 1.0, numberOfIntervals: int = 0, outputInterval: float = 0.0, method: str = 'Dassl', tolerance: float = 0.0001, fixedstepsize: float = 0.0, resultFile: str = 'dsres') → bool`
  - `:2904-2906` `Note that file extension is automatically added to resultFile (normally ".mat"). For backwards compatibility the default for resultFile is "dsres".`
  - `:2912-2913` `the default for problem is "" corresponding to the most recently used model.`
  - `:2915` `It is possible to specify a model name with modifiers for translateModel, simulateModel and simulateExtendedModel.`（示例 `:2926` `"TestSource(redeclare Modelica.Blocks.Sources."+source+" Source)"`；`EX\SimulateExtendedExample.py:83` 以 `simulateModel("A(s=...)")` 传入 String 参数 "foo"，源码中为转义双引号）
- `simulateExtendedModel`（`:2843`）：`simulateExtendedModel(problem, startTime, stopTime, numberOfIntervals, outputInterval, method, tolerance, fixedstepsize, resultFile: str = None, initialNames: List[str] = None, initialValues: List[float] = None, finalNames: List[str] = None, autoLoad: bool = None) → Tuple[bool, List[float]]`
  - `:2846-2848` `gives the possibility to set parameters and startvalues before simulation and to get the final values at end-point of simulation. autoLoad=true is default. If false the result file is not loaded in the plot window`
  - `:2860` 示例输出 `= true, {6.213412958654301, 0.9999999999999936}`；`:2864-2865` `structural parameters that are evaluated cannot be part of initialNames, use modifiers as described in simulateModel for them.`
  - `:2879` `resultFile (str or "dsres") – Where to store result.`；`:2882` `finalNames (List[str] or fill("", 0)) – Variables at end-point.`；`:2887` 返回 `true if successful, Values at end-point`
  - Python 侧仅把非 None 参数以命名参数传入（`WHL\dymola_interface.py:3280-3307`）。示例 `EX\SimulateExtendedExample.py:10,15,22`：`output = dymola.simulateExtendedModel(..., "test3", ["J1.J","J2.J"], [2,3], ["J1.w","J4.w"], True)` → `status = output[0]` / `values = output[1]`。
- `simulateMultiExtendedModel`（`:2955`）：多组 `initialValues`（维度 `[:, size(initialNames,1)]`，`:2984`）返回终值矩阵；新增 `resultFileNames: List[str]`（`:2986` `Simulation result files; nothing loaded for default of empty array.`）。
- `simulateMultiResultsModel`（`:3004`）：`... resultNames: List[str] = None, resultFileNames: List[str] = None, autoLoad: bool = None) → Tuple[bool, List[float]]`；`:3007-3008` `stores the full trajectories of several simulations instead of just the endpoints.`；示例 `:3015` 结果维度 `Real traj [4, 2, 11]`。
- 其他：`experiment(StartTime, StopTime, NumberOfIntervals, OutputInterval, Algorithm, Tolerance, FixedStepSize) → bool`（`:768`）；`getExperiment() → Tuple[float,float,int,float,str,float,float]`（`:1171`）；`getLastResultFileName() → str`（`:1252-1253` `Returns the file name of the last trajectory.`）；`linearizeModel(..., resultFile='dslin')`（`:1470-1473` 默认写 `dslin.mat`）；`importInitial(dsName='dsfinal.txt')`（`:1320-1321` `corresponds to copying dsfinal.txt ... to dsin.txt`）；`exportInitialDsin(scriptName)`（`:1034` `Generate a copy of internal dsin.txt`）。

### 3. 日志获取

- `getLastError`（`DOC\dymola_interface.html:1188-1191`）：`getLastError() → Tuple[str, int, int, int]` / `Returns the messages from the last command. ... For check, translate, etc, the log is returned. The number of errors, warnings, and info messages are also returned.`；返回顺序 `:1239`：`The messages..., The number of error messages..., The number of warning messages..., The number of info messages...`
- 文档示例（`:1193-1236`）在 `translateModel("Modelica.Mechanics.Rotational.Examples.CoupledClutches")` 后 `getLastError()` 返回整段翻译日志，含：`:1197` `The DAE has 106 scalar unknowns and 106 scalar equations.`、`:1202` `Number of components: 14`、`:1214` `Continuous time states: 8 scalars`、`:1218` `Sizes of linear systems of equations: {13}`、`:1220` `Sizes of nonlinear systems of equations: { }`、`:1224-1233` `Selected continuous time states / Statically selected continuous time states` 列表，`:1236` 计数 `0, 0, 8`。Modelica 侧同源定义 `LIB\DymolaCommands 1.19\package.mo:711-716`（`function getLastError` … `external "builtin";`）。
- `getLastErrorLog() → str`（`:168-169`）`Returns the texual output from getLastError().`；实现 `WHL\dymola_interface.py:143-146`：`output = self.getLastError()` … `return output[0]`。
- 失败语义：`DOC\dymola_exception.html:59-61`：`The exception is not thrown if, e.g., a model fails to translate. In that case the return code is used instead.`；可选严格模式 `WHL\dymola_interface_internal.py:73-74` `error_raises_exception: bool = False`，`:536-540` 为 True 时每次调用后自动 `getLastError`，`num_errors > 0` 即抛 `DymolaFunctionException`。
- `ExecuteCommand(cmd: str) → Any`（`:118-127`）：`Executes a command in the scripting window.` … `It is not possible to retrieve the output from the command. If the command failed to execute the error message can be retrieved using getLastErrorLog().`；实现 `internal.py:207-214` 捕获 `DymolaException` 后返回 None。
- `RunScript(script: str, silent: bool = False, scriptDir: str = '') → bool`（`:2551-2561`）：`Execute a given Modelica script file.` … 返回 `false, when its execution failed due to an error, true otherwise.`
- `clearlog() → bool`（`:409-410`）`Function to clear the history in the Commands Window.`
- `savelog(logfile: str = 'dymolalg.txt') → bool`（`:2597-2598`）：`If a .txt file extension is used, all text in the log is saved. If however a .mos extension ... is used, neither outputs from Dymola (results etc.) nor commands that have no equivalent Modelica function will be included`。用法 `EX\ModelManagementExample3.py:25-27`：`dymola.clearlog()` → `checkLibrary(...)` → `dymola.savelog("C:/temp/PID_Controller/log.txt")`。
- `showMessageWindow(show: bool) → bool`（`:2795`）；`DymolaLicenseInfo() → str`（`:620-621`）用于排查许可证。

### 4. 翻译统计关键字逐个 grep（范围：`DOC\*.html` + `LIB\{DymolaCommands 1.19, DataFiles 1.1.0, ModelManagement 1.4.1\help, Dymola.mo, Utilities.mo}`）

| 关键字 | 结果 | 锚点 / 引用 |
|---|---|---|
| `Number of components` | 命中 | `DOC\dymola_interface.html:1202`（getLastError 示例）；`DOC\model_management.html:148,194,247` `original_Components (int) – Number of components`；`LIB\DymolaCommands 1.19\package.mo:730` |
| `Sizes of linear systems of equations` | 命中 | `DOC\dymola_interface.html:1218`；`DOC\model_management.html:166,212,265` `translated_LinearSizes (int) – Sizes of linear systems of equations`；`:271` `initial_LinearSizes ... of the initialization problem` |
| `Sizes of nonlinear systems of equations` | 命中 | `DOC\dymola_interface.html:1220`；`DOC\model_management.html:168,214,267`；`:273` `initial_NonlinearSizes` |
| `Number of continuous time states` | 命中 | `DOC\model_management.html:161,207,260` `translated_ContinuousStates (int) – Number of continuous time states`；日志文本形式为 `Continuous time states: 8 scalars`（`dymola_interface.html:1214`） |
| `dslog.txt` | 命中 | `DOC\model_management.html:731` `getSimTimeFromDslog(fileName: str = 'dslog.txt', label: str = 'time for integration') → float`；`LIB\ModelManagement 1.4.1\help\ModelManagement_Check_RegressionTesting_Internal.html:84` `Extract simulation time from dslog.txt` |
| `dsmodel.mof` / `.mof` | 未找到 | 0 命中 |
| `Advanced.OutputModelicaCode` | 未找到 | 0 命中 |
| `Advanced.Translation.*` | 未找到 | 0 命中；文档中出现的 `Advanced.*` 仅为绘图/legend 类标志（`DOC\dymola_interface.html:465-474`，如 `Advanced.FilenameInLegend = false;`）及 `Advanced.SaveSettings`（`:393`） |
| `Evaluate` | 命中（仅提及） | `DOC\dymola_interface.html:1509` `interactive setting of translator switches such as Evaluate.`（`listvariables` 说明）；`:1518` `list(variables={"Advanced.*"}) – lists all items starting with Advanced.` |
| `Advanced.EliminateAliasVariables` | 未找到 | 0 命中 |
| `CompileWith64` | 未找到 | 0 命中 |
| `TranslationStatistics` | 命中 | `DOC\model_management.html:143,189,242` 三个类；`WHL\model_management.py:201,311,430` `_modelicaName = "ModelManagement.Check.Translation.TranslationStatistics[_v_1_1/_v_1_2]"` |
| `translationStructure` | 命中 | `DOC\model_management.html:461` `translationStructure (bool) – Translation statistics.`、`:462` `generateReferenceTranslationStructure (bool) – Generate reference translation structure.`（checkLibrary）；`:614-615`（runTestCases，默认 `True`/`False`） |
| `compareTranslationStatistics` | 命中 | `DOC\model_management.html:1336,1341-1342`：`compareTranslationStatistics(modelName: str, reference_file: str, actual_file: str) → bool` / `reference_file (str) – Name of reference statistics file.`；help `ModelManagement_Check_Translation.html:220` `Compares translation statistics and generates a log if it differs` |
| `translationFailed` / `simulationFailed` | 命中 | `DOC\model_management.html:794,775`（`fileName: str → bool`）；help `..._RegressionTesting_Internal.html:287` `Search in validation file if translation failed` |

- `TranslationStatistics_v_1_2` 完整字段（`DOC\model_management.html:247-283`）：`original_{Components,Variables,Constants,Parameters,Unknowns,DifferentiatedVariables,Equations,Nontrivial}`、`translated_{Constants,FreeParameters,ParameterDepending,Inputs,Outputs,ContinuousStates,TimeVarying,Alias,AssumedInitialConditions,NumberMixedSystemsOfEquations,LinearSizes,LinearSizesAfter,NonlinearSizes,NonlinearSizesAfter,NumericalJacobians}`、`initial_{NumberMixedSystemsOfEquations,LinearSizes,LinearSizesAfter,NonlinearSizes,NonlinearSizesAfter,NumericalJacobians}`、`states (str) – Selected state variables`、`initIterVar`、`simuIterVar`、`timingSections/timingValues`、`maxMemory`、`memoryVec`、`warnings`。help 页显示 `translated_LinearSizes[:]` 为数组（`ModelManagement_Check_Translation.html:111-114`）。
- `WHL\model_management.py` 中没有任何函数以 `TranslationStatistics` 作为返回类型（grep 0 命中）→ Python 侧没有"翻译后直接取结构化统计"的 API，只能解析 `getLastError()` 文本，或走 `checkLibrary/runTestCases` 生成的文件。

### 5. 编译器设置

- `SetDymolaCompiler(compiler: str, settings: List[str] = [''], mergeSettings: bool = True) → None`（`DOC\dymola_interface.html:2733-2734`）`Set up the compiler configuration. Call GetDymolaCompiler() for current setting and format of arguments.`
  - `:2737` `SetDymolaCompiler("vs", {"CCompiler=MSVC","MSVCDir=C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build"});`
  - `:2739` `SetDymolaCompiler("gcc", {"CCompiler=GCC","GCC32Path=C:/MinGW/bin/gcc"});`；`:2741` `SetDymolaCompiler("wsl", {"CCompiler=WSL","WSLPath=C:/Windows/System32/wsl.exe"});`
  - `:2743-2744` `Set any of DLL or DDE to 1 to enable` … `"DLL=0", "DDE=0"`；`:2753` `mergeSettings (bool or True) – Keep previous name=value pairs unless overwritten.`
- `GetDymolaCompiler() → Tuple[str, List[str]]`（`:1155-1158`）`Compiler type, for example, vs, Settings in name=value pairs`
- `verifyCompiler() → bool`（`:3242-3245`）`Verifies the current compiler settings by compiling a small C file.` … `a message explaining the problem is shown in the command log.`
- `CompileWith64`：未找到（见 §4）。相关 PDF 仅有文件名 `Documentation\Installing Visual Studio Build Tools for Dymola.pdf`（未读）。

### 6. checkModel / getClassText / ModelManagement 结构与统计函数

- `checkModel(problem: str, simulate: bool = False, constraint: bool = False) → bool`（`DOC\dymola_interface.html:340-343`）`Check the model validity. This corresponds to Check (Normal) in the menus. If simulate=true ... the model will be simulated with stored simulation setup.`
- `getClassText(fullName: str, includeAnnotations: bool = False, formatted: bool = False) → Tuple[str, bool]`（`:1106-1119`）`Returns the Modelica text for a given model.`，返回 `The Modelica text., true if the model is read-only.`；`setClassText(parentName, fullText) → bool`（`:2707-2708`）。
- `saveTotalModel(fileName, modelName, skipStandard=False, completePackage=False) → bool`（`:2682-2684`）`saves a model and its dependencies (from non-encrypted libraries) as a modelica package`。
- `getDependentLibraries(modelName, dependentModels=False) → List[str]`（`:1132-1133` `This function lists used libraries.`）。
- `exportEquations(path) → bool`（`:927-928`）`Supported file formats are PNG and MathML.`（图像/MathML，非扁平模型文本）。
- `listvariables(filename='', variables=['*'])`（`:1507-1512`）`Also interactive settings of translator switches such as Evaluate are listed.`，`:1522` 可写到 `script.mos`。
- `ModelManagement`（`DOC\model_management.html`，Python 类 `ModelManagement(dymola)` `:431`）中与"结构/统计"相关者：
  - `nrEquations(Model: str) → float`（`:3317`）、`nrUnknowns(Model) → float`（`:3336`）、`listPossibleContinuousStates(Model) → List[str]`（`:3355`）、`listPossibleDiscreteStates`（`:3374`）、`usedModels`（`:3393`）、`countUsedModels`（`:3412`）；RPC 目标 `WHL\model_management.py:3602-3673`：`ModelManagement.Structure.Instantiated.NrEquations / NrUnknowns / ListPossibleContinuousStates / ListPossibleDiscreteStates / UsedModels / CountUsedModels`。文档对这些函数没有文字说明（仅参数名）。
  - `equationBlocks(fullName, includeAnnotations=False) → List[str]`（`:2986`，RPC `ModelManagement.Structure.AST.Equations.EquationBlocks`，`model_management.py:3348`）；`connectionsText`（`:2918`）；`componentsInClassAttributes`（`:2503`，示例 `EX\ModelManagementExample2.py:7`）；`getClassAttributes`（`:2380`）；`getComponentAttributes`（`:2613`，示例 `EX\ModelManagementExample.py:21`）；`classesInPackage/allClassesInPackage`（`:3056,3119`）；`getClassText → str`（`:2418`，RPC `ModelManagement.Structure.AST.Classes.GetClassText`）；`saveTotalModel(fileName, modelName)`（`:3268-3274`）。
  - 测试/统计：`checkLibrary(...)`（`:440-462`，示例 `EX\ModelManagementExample3.py:26`）、`runTestCases(...)`（`:595-619`，含 `compareSimTime`、`compareTranslationTime (only internal)`）、`compareTranslationStatistics`（`:1336`）、`getSimTimeFromDslog`（`:731`）、`translationFailed/simulationFailed(fileName)`（`:794/775`）。
- 未找到：任何返回"扁平化模型文本"（instantiate/flatten）的 Python 函数；`flat`/`instantiat` 在 `DOC` 中 0 命中。

### 7. 结果文件格式

- `.mat`：`DOC\dymola_interface.html:2301` `The file must either be a Matlab v4-file or a textual file.`（readMatrix）；`:2304-2305` `readMatrix("CoupledClutches.mat", "data_2")` → `Real data [63, 1522]`；`:2360-2364` `name` 矩阵按转置存储，读名字应用 `readTrajectoryNames`。
- 轨迹读取：`readTrajectory(fileName, signals: List[str], rows: int) → List[float]`（`:2386-2387`）`Returns all output points of a trajectory.`，`:2402` `duplicate times indicate before and after event`；`readTrajectoryNames(fileName) → List[str]`（`:2415`，示例 `{"Time", "f", "T2", ...}`）；`readTrajectorySize(fileName) → int`（`:2440`）；`interpolateTrajectory(fileName, signals, times) → List[float]`（`:1447`）；`existTrajectoryNames`（`:727`）；`writeTrajectory/writeMatrix`（`:3316/3283`）。大结果经磁盘中转：`internal.py:79-81` `very large results can be transferred using the disk. Only implemented for readTrajectory().`
- 文本/精度：`experimentSetupOutput(textual: bool = False, doublePrecision: bool = False, states=True, derivatives=True, inputs=True, outputs=True, auxiliaries=True, equidistant=True, events=True, onlyStopTime=False, debug=False) → bool`（`:815-829`）`textual (bool or False) – Textual storage.` / `doublePrecision (bool or False) – Double precision.` / `onlyStopTime ... Write variables to result file only at stop time.`
- `dsin.txt / dsfinal.txt`：`:1320-1321`（importInitial），`:1004-1008`（exportInitial 生成 `.mos` 重建仿真设置），`:2309` `fileName (str) – File containing the matrix, e.g. A.mat, dsin.txt.`
- CSV / 转换：Python 接口文档中 `csv`、`convertMATFile`、`DataFiles` 均 0 命中；转换能力在 Modelica 库 `LIB\DataFiles 1.1.0\package.mo`：`:2` `package DataFiles "Functions for reading and writing data files"`；`:17-18` `function convertMATtoCSV "Reads a Dymola result file (MAT file) and writes selected variables to the specified CSV file"`，输入 `:21` `resultFileName`、`:24` `variableNames[:]`、`:26` `csvFileName=""`（默认同名 `.csv`），内部 `:41-43` 用 `readTrajectorySize/readTrajectory`，`:47` `DataFiles.writeCSVmatrix(`；另有 `readMATmatrices`(`:66`)、`readMATmatrix`(`:98`)、`writeMATmatrix`(`:137`)、`readCSVmatrix`(`:241`)、`writeCSVmatrix`(`:275`)。可经 `ExecuteCommand("DataFiles.convertMATtoCSV(...)")` 调用（`DOC:70-73` ExecuteCommand 说明）。

## 未找到或待探针验证

1. `dsmodel.mof`、`Advanced.OutputModelicaCode`、`Advanced.Translation.*`、`Advanced.EliminateAliasVariables`、`Advanced.CompileWith64`：在可读文档/库源码中 0 命中。是否存在及确切拼写需在 Dymola 中 `list(variables={"Advanced.*"})`（`DOC\dymola_interface.html:1518`）实跑确认，或读 `Documentation\Dymola User Manual 2C.pdf`（本任务未读 PDF）。
2. "翻译统计文件"（`compareTranslationStatistics` 的 `reference_file/actual_file`）由哪个内建函数单独生成、格式为何：文档仅说 `checkLibrary/runTestCases` 的 `translationStructure` 会生成；`ModelManagement` 源码为 `package.moe`（加密），不可读。待探针：跑一次 `runTestCases(..., generateReferenceTranslationStructure=True)` 看产物。
3. Python 接口是否能取扁平化模型：未找到任何 flatten/instantiate API；`nrEquations/nrUnknowns/listPossibleContinuousStates` 无文字说明，其语义（是否等于 `getLastError` 里 Original Model 的 `Equations`/`Unknowns`）需探针。
4. `kernel=True` 模式需要 `bin64/dmc.exe`（`internal.py:133`），但 `bin64` 中未列出 `dmc.exe`；该模式在本机是否可用待验证。
5. `getLastError()` 统计文本的完整格式（尤其 `Sizes after manipulation`、`Number of numerical Jacobians`、初始化问题一节）只有 CoupledClutches 一例；设计解析器前应对若干 testsuite 模型实跑取样。
6. `simulateModel` 的 `resultFile` 是否受 `cd()` 影响、是否可带绝对路径：文档未说明（仅 `Where to store result.`）。
7. `DymolaInterfaceInternal.timeout` 默认 `None`（无 HTTP 超时）：长翻译时需自行加看门狗；`close()` 仅等 20 s。

## 对规格的建议

- oracle 驱动层（Python）最小接口：`DymolaInterface(showwindow=False)` → `cd(workdir)` → `openModel(path, changeDirectory=False)` → `translateModel(name)`；无论成败都取 `getLastError()` 四元组，把 message 原文落盘为 `<model>.dymola.log`，再用正则解析 `Statistics` 段（字段名以 `TranslationStatistics_v_1_2` 为准，`DOC\model_management.html:247-283`），得到与 ymc `--dump stats` 对齐的 JSON。
- 数值对比：`simulateExtendedModel(..., finalNames=[...])` 直接拿终值向量做验收；全轨迹用 `readTrajectoryNames + readTrajectorySize + readTrajectory`（内置磁盘中转），无需自行解析 `.mat`。
- 可复现性：每个 case 用独立工作目录（参考 `MultithreadingExample.py`），并显式 `SetDymolaCompiler("vs", [...])` + `verifyCompiler()` 一次，记录 `DymolaVersion()`。
- 把 `error_raises_exception=True` 作为可选严格模式；把 `getLastError` 的 `numErrors>0` 视作翻译失败判据（`DymolaFunctionException` 文档：翻译失败默认只返回 False）。
