# R6：OMC 压平/因果化阶段可用于「结构级对比」的输出格式

基线：`C:\dev\OpenModelica` @ `3ceb0c9d06`（`git log -1` 实查）。实测用 PATH 中安装版 `omc` 1.25.0（`omc --version` → `OpenModelica v1.25.0 (64-bit)`），所有命令包在 `timeout 60` 内。文件路径均相对 `C:\dev\OpenModelica\OMCompiler\Compiler\`。

## 结论摘要

1. 新后端 `-d=backenddaeinfo` 输出 4 段 `Notification`：前端后计数（`NBBindings.mo:130`）、分区统计、变量统计、按阶段（Simulation / Initialization / Initialization (lambda=0)）的强连通分量统计（`NBackendDAE.mo:1584-1694`）。每行格式固定为 ` * Number of …: …… N`，变量/方程计数为 `标量数 (元素数)` 双计数。
2. 新后端 `-d=bltdump` 的层级为：`headline_1("[ODE] Differential-Algebraic: (Causalize)")` → `headline_2("(1) Continuous ODE Partition")` → `headline_3("BLOCK i: <种类> (status = …, size = …)")` → `### Variable:` / `### Equation:`（`NBackendDAE.mo:161-172`、`NBPartition.mo:326`、`NBStrongComponent.mo:186-223`）。只在模块名为 `Causalize` 或 `Solve` 后打印（`NBackendDAE.mo:410`）。
3. 强连通分量块头共 8 种原文：`Single Strong Component`、`Multi Strong Component`、`Sliced Component`、`Resizable Component`、`Entwined Component`、`Generic Component`、`Algebraic Loop (Linear = …, Mixed = …, Homotopy = …, size = …)`、`--- Alias of … ---`。代数环内部用 `Strict Tearing Set` / `Casual Tearing Set` + `### Iteration Variables:` / `### Residual Equations:` / `### Inner Equations:`（`NBTearing.mo:129-132`）。**新后端源码中没有 "Torn" 字样**（`grep -rn "\bTorn\b" NBackEnd/` 无结果）。
4. 旧后端 `-d=backenddaeinfo` 走 `BackendDump.dumpCompShort`（`BackendDump.mo:3525`），文本由 `Error.mo:595-606` 的 6 个消息模板（编号 258-263）拼出；"Sizes of linear/nonlinear systems" 字样**未找到**，对应的是 `Equation system details (not torn)` 与 `Torn system details for %s tearing set`。
5. `checkModel` 的 `Class X has N equation(s) and M variable(s).` 来自 `CevalScriptBackend.mo:6184-6185`；计数函数是 `FrontEnd/CheckModel.mo:61-75`（按 `Expression.sizeOf` 标量尺寸累加，参数/常量不计，public input 计一条方程），新前端路径下输入是 `NFConvertDAE.convert` 转出的 DAE（`CevalScriptBackend.mo:3628-3629`）。
6. 相关 flag：`-d=bltdump`(51) / `optdaedump`(55) / `backenddaeinfo`(68) / `stateselection`(70) / `dumpSimCode`(75) / `discreteinfo`(91) / `bltmatrixdump`(153) / `dumpBackendClocks`(188)；配置 flag `-f`/`--baseModelica`(135, bool, 默认 false)、`--newBackend`(140, bool, false)、`--dumpFlatModel`(147, 默认 {"all"})、`--baseModelicaFormat`(153)、`--baseModelicaOptions`(154)。**没有名为 `flatModelica` 的 flag**（`Flags.mo` grep 无结果；`Config.flatModelica()` 只是读 `BASE_MODELICA`，`Config.mo:653-656`）。
7. Base Modelica 文本入口：`NFInst.instClassInProgram(dumpFlat=true)` → `InstUtil.dumpFlatModel` → `FlatModel.toFlatString` → `appendFlatStream`（`NFFlatModel.mo:246-342`），首行固定 `//! base 0.1.0`，随后 `package 'X'` / `model 'X'` / 变量 / `initial equation` / `equation` / `initial algorithm` / `algorithm` / `end 'X';` 两次。
8. 非交互 `omc file.mo` 默认**不跑后端**（`runBackend := Config.simulationCg() or Config.simulation()`，`Main.mo:455`）；要看 `backenddaeinfo`/`bltdump` 必须加 `-s`。`--simCodeTarget=None` 只是让代码生成阶段空转（`SimCodeMain.mo:709-710`）。
9. 实测差异：安装版 1.25.0 的块头是 `BLOCK 1: Single Strong Component (status = Solve.UNPROCESSED)`、`Algebraic Loop (Linear = false, Mixed = false)`，**没有** 基线源码里的 `, size = N` 与 `Homotopy = …`；做 oracle 对比时字段集合以运行的二进制为准。
10. 新后端 `backenddaeinfo` 里 `Number of clocked partitions` 硬编码为 `"0"`（`NBackendDAE.mo:1601` `p_clk := "0";`），不是实际计数。

## 逐项事实

### 1. 新后端 `-d=backenddaeinfo`（`NBackEnd/Classes/NBackendDAE.mo`）

- 调用点：`NBackendDAE.mo:355` `backenddaeinfo(bdae);`（`main` 末尾，所有模块跑完后）。
- 门控：`NBackendDAE.mo:1587` `if Flags.isSet(Flags.DUMP_BACKENDDAE_INFO) then`，只匹配 `MAIN(varData = VarData.VAR_DATA_SIM(), eqData = EqData.EQ_DATA_SIM())`（:1595）。
- 分区计数来源（:1597-1603）：`p_ode := intString(listLength(bdae.ode))`，同理 `bdae.algebraic / ode_event / alg_event / init / init_0`；`p_clk := "0";`（硬编码）。
- 变量计数来源（:1606-1611）：每项 = `VariablePointers.scalarSize(varData.X) + " (" + VariablePointers.size(varData.X) + ")"`，X ∈ `states, discretes, discrete_states, clocked_states, clocks, top_level_inputs`。
  - `NBVariable.mo:2094-2098` `size`："returns the number of elements, not the actual scalarized number of variables!"；`:2100-2108` `scalarSize` 逐变量累加 `BVariable.size`。
- 名单扩展（:1613-1634）：`-d=stateselection` 追加状态名单；`-d=discreteinfo` 追加离散变量/时钟/顶层输入名单；否则追加提示 `('-d=stateselection' for the list of states)` 等。
- 打印原文（`Error.addCompilerNotification`，:1638-1654）：
  ```
  Partition statistics after passing the back-end:
   * Number of ODE partitions: ..................... N
   * Number of algebraic partitions: ............... N
   * Number of ODE event partitions: ............... N
   * Number of algebraic event partitions: ......... N
   * Number of clocked partitions: ................. 0
   * Number of initial partitions: ................. N
   * Number of initial(lambda=0) partitions: ....... N
  Variable statistics after passing the back-end:
   * Number of states: ............................. s (n) …
   * Number of discrete states / clocked states / discrete variables / clocks / top-level inputs: …
  ```
- 强连通分量统计（:1657-1662 调 `strongcomponentinfo`，定义 :1669-1694）：按阶段 `"Simulation"`（ode+algebraic+ode_event+alg_event）、`"Initialization"`（init）、`"Initialization (lambda=0)"`（init_0）各打一段：
  ```
  [<phase>] Strong Component statistics after passing the back-end:
   * Number of single strong components: ........... T (scalar:a, array:b, record:c)
   * Number of multi strong components: ............ T (algorithm:a, when:b, if:c, tuple:d)
   * Number of for-loop strong components: ......... T (resizable: a, generic: b, entwined:c)
   * Number of algebraic-loop strong components: ... T (linear: a, nonlinear:b)
  ```
  计数器 `CountCollector`（`NBStrongComponent.mo:241-254`，12 个整数）由 `strongComponentInfo`（:257-300）按分量种类累加：`SINGLE_COMPONENT`/`SLICED_COMPONENT` 再按方程种类 `SCALAR/ARRAY/RECORD_EQUATION` 分；`MULTI_COMPONENT` 按 `ALGORITHM/WHEN/IF/RECORD_EQUATION(tuple)` 分；`ALGEBRAIC_LOOP guard(comp.linear)` 进 `loop_lin` 否则 `loop_nlin`；`ALIAS` 递归到 `comp.original`。注意 multi 的合计 `multi_algorithm + multi_when + multi_if` **不含 tuple**（:1684）。
- 前端后计数（新后端版 "Number of equations"）：`NBackEnd/Modules/2_Pre/NBBindings.mo:129-134`：
  ```
  if Flags.isSet(Flags.DUMP_BACKENDDAE_INFO) then
    Error.addSourceMessage(Error.BACKENDDAEINFO_LOWER,{
      intString(EqData.scalarSize(eqData)) + " (" + intString(EqData.size(eqData)) + ")",
      intString(VarData.scalarSize(varData)) + " (" + intString(VarData.size(varData)) + ")"}, …
  ```
  模板 `Error.mo:595-596`（消息 258）：`"Model statistics after passing the front-end and creating the data structures used by the back-end:\n * Number of equations: %s\n * Number of variables: %s"`。时点在 Bindings 模块（绑定方程已加入 `eqData.equations` 之后，:118-120）。

### 2. 新后端 `-d=bltdump` 结构（`NBackendDAE.mo` / `NBPartition.mo` / `NBStrongComponent.mo` / `NBTearing.mo`）

- 打印时机：`NBackendDAE.mo:410-412` `if Flags.isSet(Flags.OPT_DAE_DUMP) or (Flags.isSet(Flags.BLT_DUMP) and (name == "Causalize" or name == "Solve")) then print(toString(bdae, "(" + name + ")"));`。即 bltdump 只在 Causalize、Solve 两个模块后各打一次（实测 Causalize 段出现两次：见 §8）。
- `BackendDAE.toString`（:146-176）：bltdump 开启且有分区时按分区列表打印，标题（`headline_1`）原文：`"[ODE] Differential-Algebraic: " + str`、`"[ALG] Algebraic: "`、`"[ODE_EVENT] Event Handling: "`、`"[ALG_EVENT] Event Handling: "`、`"[CLOCKED] Event Handling: "`、`"[INI] Initialization: "`、`"[INI_0] Initialization Lambda=0: "`、`"[DAE] DAEMode: "`（:161-172）。未开 bltdump 时退化为 `headline_1("BackendDAE: " + str)` + `VarData.toString` + `EqData.toString`（:157-159）。
- `Partition.toString`（`NBPartition.mo:321-355`）：`headline_2("(" + index + ") " + Association.toStringShort + " Partition")`；`toStringShort`（:92-101）返回 `"Continuous " + kindToString(kind)` 或 `"Clocked"`；`kindToString`（:609-627）取值 `ODE, ALG, ODE_EVT, ALG_EVT, INI, INI_0, DAE, JAC, CLK`。有 `strongComponents` 时逐个 `StrongComponent.toString(comps[i], i)`（:332-334），否则打印 `VariablePointers.toString(partition.unknowns, "Unknown")` + `EquationPointers.toString`（:339）。`level==1/3` 附加邻接矩阵与匹配（:343-351）。
- `StrongComponent.toString`（`NBStrongComponent.mo:175-234`），`s = StrongComponent.size(comp, true)`（:180），块头全部走 `headline_3`：
  - :186 `"BLOCK" + indexStr + ": Single Strong Component (status = " + Solve.statusString(comp.status) + ", size = " + s + ")"`，正文 `"### Variable:\n"` + `Variable.toString(…, "\t")` + `"### Equation:\n"` + `Equation.toString(…, "\t")`
  - :192 `": Multi Strong Component (status = …, size = …)"`，正文 `### Variables:` / `### Equation:`
  - :199 `": Sliced Component (status = …, size = …)"`（`index == -2` 时无块头）
  - :205 `": Resizable Component (status = …, size = …)"`
  - :211 `": Entwined Component (status = Solve.EXPLICIT, size = …)"` + `"call order: {…}"`
  - :217 `": Generic Component (status = Solve.EXPLICIT, size = …)"`
  - :223 `": Algebraic Loop (Linear = " + boolString(comp.linear) + ", Mixed = " + boolString(comp.mixed) + ", Homotopy = " + boolString(comp.homotopy) + ", size = " + s + ")"`，随后 `Tearing.toString(comp.strict, "Strict Tearing Set")`，若 `comp.casual` 存在再打 `"Casual Tearing Set"`（:224-227）
  - :230 `ALIAS()` → `"--- Alias of " + AliasInfo.toString(comp.aliasInfo) + " ---\n" + toString(comp.original, index)`
- `Solve.statusString`（`NBSolve.mo:79-90`）：`Solve.UNPROCESSED / Solve.EXPLICIT / Solve.IMPLICIT / Solve.UNSOLVABLE / Solve.FAILED`。
- `Tearing.toString`（`NBTearing.mo:125-135`）：`headline_4(str)` + `"### Iteration Variables:\n"` + `"\n### Residual Equations:\n"` + `"\n### Inner Equations:\n"`（内部方程用 `StrongComponent.toString(index = -1)` 即无编号）+ 可选 `BJacobian.toString(jac, "NLS")`。
- 方程/变量行前缀：`NBEquation.mo:1183` `s = "(" + intString(Equation.size(…, true)) + ")"`；`:1187-1190` 前缀 `"[SCAL] "`, `"[ARRY] "`, `"[TUPL] "`/`"[RECD] "`；变量种类前缀 `NFBackendExtension.mo:263` `case ALGEBRAIC() then "[ALGB]"`。
- 标题装饰（`Util/StringUtil.mo:56-82`）：`headline_1` = `#`×(len+8) 两行夹 `\n\n    title\n\n`；`headline_2` = `=`×(len+4) 夹 `\n  title\n`；`headline_3` = `title\n` + `-`×(len+2)；`headline_4` = `title\n` + `*`×(len+2)。
- 其他 bltdump 消费点：`NBCausalize.mo:189`（类型冲突报错时附 `Partition.toString`）、`NBResolveSingularities.mo:434-437`（奇异时附全部变量/方程/邻接矩阵/匹配，否则提示 `Use -d=bltdump for more verbose debug output.`）。

### 3. 旧后端 `-d=backenddaeinfo`（`BackEnd/BackendDump.mo` + `Util/Error.mo`）

- 前端后计数：`BackendDAECreate.mo:188-192` `Error.addSourceMessage(Error.BACKENDDAEINFO_LOWER,{ String(BackendEquation.equationArraySize(syst.orderedEqs)), String(BackendVariable.varsSize(syst.orderedVars))}, …)`（单一数字，无 `(n)` 后缀）。
- 后端后统计：`BackendDAEUtil.mo:7774` `BackendDump.dumpCompShort(outSimDAE);`（`backenddaeinfo` 或 `stateselection` 或 `discreteinfo` 任一开启，:7773）；初始化系统另在 `Initialization.mo:238` 调用一次。
- `dumpCompShort`（`BackendDump.mo:3525-3624`）：状态数 `st`、离散变量 `dvar`、离散状态 `dst`（pre 变量集合大小，:3551-3554）、时钟状态、顶层输入 `inp`，用 `Error.BACKENDDAEINFO_STATISTICS`（:3595）；强连通分量分类 `seq/sarr/salg/sce/swe/sie/eqsys/teqsys/meqsys` 用 `BACKENDDAEINFO_STRONGCOMPONENT_STATISTICS`（:3608）；`strcomps := seq+eqsys+meqsys+sarr+salg+sce+swe+sie+teqsys`（:3573）。
- 消息模板原文（`Error.mo`）：
  - :598（259）`"Model statistics after passing the back-end for %s:\n * Number of independent subsystems: %s\n * Number of states: %s\n * Number of discrete variables: %s\n * Number of discrete states: %s\n * Number of clocked states: %s\n * Top-level inputs: %s"`
  - :602（261）`"Strong component statistics for %s (%s):\n * Single equations (assignments): %s\n * Array equations: %s\n * Algorithm blocks: %s\n * Record equations: %s\n * When equations: %s\n * If-equations: %s\n * Equation systems (not torn): %s\n * Torn equation systems: %s\n * Mixed (continuous/discrete) equation systems: %s"`
  - :604（262）`"Equation system details (not torn):\n * Constant Jacobian (size): %s\n * Linear Jacobian (size,density): %s\n * Non-linear Jacobian (size): %s\n * Without analytic Jacobian (size): %s"`
  - :606（263）`"Torn system details for %s tearing set:\n * Linear torn systems (#iteration vars, #inner vars, density): %s\n * Non-linear torn systems (#iteration vars, #inner vars): %s"`
  - :600（260）`"Mixed equation statistics: …"`（10 项）
- 尺寸列表格式：`equationSizesStr`（:3676-3690）→ `"1 system"` / `"N systems"` + `"\n   {…, …}"`；线性 torn 元组 `sizeNumNonZeroTornTplString`（:3706-3717）→ `"(sz,others,density%)"`，如实测 `{(1,1,100.0%)}`。
- 旧后端 bltdump 的 torn 块原文：`BackendDump.mo:1171` `print("torn " + s + " Equationsystem:\n");`，:1194 `"Strict torn … Equationsystem:"`，:1209 `"Casual torn … Equationsystem:"`，s ∈ `"linear"/"nonlinear"`；子段 `internal vars (n)` / `residual vars (n)` / `internal equations (n)` / `residual equations (n)`（:1175-1184）。

### 4. `checkModel` 的计数字符串（`Script/CevalScriptBackend.mo` + `FrontEnd/CheckModel.mo`）

- 字符串：`CevalScriptBackend.mo:6184-6185` `"Check of ",classNameStr," completed successfully.\nClass ",classNameStr," has ",String(eqnSize)," equation(s) and ",String(varSize)," variable(s).\n",String(simpleEqnSize)," of these are trivial equation(s)."`。
- 数据来源：:6174-6176 `(cache, _, odae) := runFrontEnd(cache, env, className, false); SOME(dae) := odae; (varSize, eqnSize, simpleEqnSize) := CheckModel.checkModel(dae);`。新前端下 `runFrontEndWork`（:3597-3640）走 `runFrontEndWorkNF` 后 `(dae, funcs) := NFConvertDAE.convert(flat_model, nf_funcs);`（:3628-3629），即计数对象是 NF 压平模型转成的旧 DAE。
- 计数规则（`CheckModel.mo:61-75, 82-215`）：`countVarEqnSize` 对 `DAE.VAR`：`EXTOBJECTCLASS`/外部对象/`PARAM`/`CONST` 不计（:109-118）；其余变量 `size := Expression.sizeOf(element.ty); varSize += size`，若是 public input 则 `eqnSize += size`，否则若有 binding 则 `eqnSize += size` 并把 binding 当方程收集（:120-137）。方程侧 `EQUATION/EQUEQUATION/DEFINE/COMPLEX_EQUATION/ARRAY_EQUATION/WHEN/FOR/IF` 按 `Expression.sizeOf(typeof(exp))` 累加（:140-215 各 case，`INITIAL*` 不计）。`simpleEqnSize := countSimpleEqnSize(eqns, 0, hs)`（:75）。
- 前后端不一致时的错误文本：`Error.mo:143-146` `UNDERDET_EQN_SYSTEM`(32) `"Too few equations, under-determined system. The model has %s equation(s) and %s variable(s)."`、`OVERDET_EQN_SYSTEM`(33) 对称。调用者只在旧后端（`BackendDAEUtil.mo:10055`、`Causalize.mo:108/118`、`SynchronousFeatures.mo:3006`）；`NFFrontEnd/`、`NBackEnd/` 下 grep 无调用。
- `NFInst.mo:279-280` 有被注释掉的 NF 侧计数 `//(var_count, eq_count) := CheckModel.checkModel(flatModel);`。

### 5. 相关 flag（`Util/Flags.mo`）

| flag | 行 | 类型/默认 | 帮助原文 |
|---|---|---|---|
| `-d=bltdump` `BLT_DUMP` | :270 | DebugFlag 51, false | "Dumps information from index reduction." |
| `-d=optdaedump` `OPT_DAE_DUMP` | :278 | 55, false | "Dumps information from the optimization modules." |
| `-d=backenddaeinfo` `DUMP_BACKENDDAE_INFO` | :304 | 68, false | "Enables dumping of back-end information about system (Number of equations before back-end,...)." |
| `-d=stateselection` | :308 | 70, false | "Enables dumping of selected states. Extends -d=backenddaeinfo." |
| `-d=dumpSimCode` `DUMP_SIMCODE` | :318 | 75, false | "Dumps the simCode model used for code generation." |
| `-d=discreteinfo` | :350 | 91, false | "Enables dumping of discrete variables. Extends -d=backenddaeinfo." |
| `-d=bltmatrixdump` | :474 | 153, false | "Dumps the blt matrix in html file. …" |
| `-d=dumpBackendClocks` | :544 | 188, false | "Dumps times for each backend module (only new backend)." |
| `-f` / `--baseModelica` `BASE_MODELICA` | :1253 | ConfigFlag 135, `SOME("f")`, BOOL false | "Outputs experimental Base Modelica." |
| `--newBackend` `NEW_BACKEND` | :1274 | 140, BOOL false | "Activates experimental new backend for better array handling. This also activates the new frontend. [WIP]" |
| `--dumpFlatModel` `DUMP_FLAT_MODEL` | :1312 | 147, STRING_LIST {"all"}；选项 flatten/connections/eval/simplify/scalarize/translateResidualsDAE | "Dumps the flat model at the given stages of the frontend." |
| `--baseModelicaFormat` | :1356 | 153, STRING_LIST {}；选项 scalarized/partiallyScalarized/nonScalarized/withRecords/withoutRecords | "Formatting options for Base Modelica" |
| `--baseModelicaOptions` | :1365 | 154, STRING_LIST {}；选项 moveBindings/scalarize/inlineFunctions | "Enables optional Base Modelica options." |
| `-s` / `--simulationCg` | :793 | 27, BOOL false | "Turns on simulation code generation." |
| `--simCodeTarget` | :762 | 17, STRING "C"；选项含 "None" | "Sets the target language for the code generation." |

`Flags.mo` 中 grep `flatModelica` 无结果；`Config.flatModelica()`（`Config.mo:653-663`）= 读 `BASE_MODELICA`，若未开 `-d=newInst` 则报 `INVALID_FLAG_CONDITION {"-f", "flat modelica requires flag -d=newInst to be set"}` 并返回 false。

### 6. Base Modelica 输出入口（`NFFrontEnd/`）

- 入口链：`SimCodeMain.mo:1182` `dumpValidFlatModelicaNF := not runSilent and Config.flatModelica();` → `CevalScriptBackend.runFrontEndWorkNF(className, false, dumpValidFlatModelicaNF)`（:1190）→ `NFInst.instClassInProgram(cls_name, scode_p, annotation_p, relaxedFrontend, dumpFlat)`（`CevalScriptBackend.mo:3711-3712`）。最终 `SimCodeMain.mo:1257-1264`：非 silent 时 `print(flatString)`；未开 `-f` 时 `flatString := DAEDump.dumpStr(dae, funcs)`（旧式 `class X … end X;` 文本，:1206-1207）。交互 API `instantiateModel` 同样 `dumpFlat = Config.flatModelica() and not Config.silent()`（`CevalScriptBackend.mo:8829, 8860`）。
- `NFInst.instClassInProgram`（`NFInst.mo:148-262`）流水：`instantiateRootClass` → `instExpressions` → `updateImplicitVariability` → `Typing.typeClass` → `Flatten.flatten` → `EvalConstants.evaluate` → `dumpFlatModelDebug("eval")` → `UnitCheck` → `SimplifyModel.simplify` → `Package.collectConstants` → `Flatten.collectFunctions` → **未设 `scalarize` 选项时此处生成 flatString**（:227-229）→ `dumpFlatModelDebug("simplify")` → `Scalarize.scalarize`（`-d=nfScalarize` 时）→ `dumpFlatModelDebug("scalarize")` → **设了 `--baseModelicaOptions=scalarize` 时此处生成 flatString**（:247-249）→ `--newBackend` 时再 `combineBinaries` + 数组构造器替换（:251-259）→ `VerifyModel.verify`。
- `InstUtil.dumpFlatModel`（`NFInstUtil.mo:147-156`）= `combineSubscripts` + `FlatModel.toFlatString(flat_model, functions)`；`dumpFlatModelDebug`（:71-100）由 `--dumpFlatModel=<stage>` 触发，前后各打 `########################################` 行，`-f` 开启时用 `printFlatString` 否则 `printString`。
- `FlatModel.appendFlatStream`（`NFFlatModel.mo:246-342`）文本布局：`//! base 0.1.0` → `package 'X'` → 函数（`Function.toFlatStream`，跳过默认 record 构造器与外部对象构造/析构）→ 类型声明 → `  model 'X'` → 变量（`Variable.toFlatStream`，缩进 4）→ `  initial equation` → `  equation` → `  initial algorithm` → `  algorithm` → `  end 'X';` → `end 'X';`。开 `scalarize` 时先 `Scalarize.scalarizeVariables/Equations(forceScalarize = true)`、`splitRecordEquations`、`unrollForStatementsInAlg`、`expandCallArgs`；否则 `reconstructRecordInstances` + 去空数组（:270-284）；`moveBindings` 时 `moveBindings(flat_model)`（:286-288）。
- 选项语义（`NFFrontEnd/BaseModelica.mo`）：`defaultFormat = (PARTIALLY_SCALARIZED, WITH_RECORDS, moveBindings=false)`（:60-64）；`formatFromFlags`（:66-88）：未开 `-d=nfScalarize` → `NOT_SCALARIZED`；`--baseModelicaOptions=scalarize` → `SCALARIZED + WITHOUT_RECORDS`；再按 `--baseModelicaFormat` 逐项覆盖；`moveBindings := isConfigFlagSet(BASE_MODELICA_OPTIONS, "moveBindings")`；`inlineFunctions()`（:90-92）读 `"inlineFunctions"` 选项。

### 7. 非交互命令行的后端触发条件（`Main/Main.mo`）

- `Main.mo:455-459` `runBackend := Config.simulationCg() or Config.simulation(); … CevalScriptBackend.translateModel(…, cname, fileNamePrefix, runBackend, runSilent, NONE());`。因此 `omc --newBackend -d=backenddaeinfo,bltdump --simCodeTarget=None probe.mo` **不带 `-s` 时只打印压平文本**（实测 §8 A1）。
- `SimCodeMain.mo:709-710` `case "None" then ();`：`--simCodeTarget=None` 只跳过模板代码生成，后端与 SimCode 仍执行。

### 8. 实测（安装版 omc 1.25.0，`scratchpad/probe_r6*.mo`）

探针 `probe_r6.mo`（线性环，7 行）：`der(x) = -x + y; y = 2*z + x; z = 1 - 0.5*y;`。

- A1 `timeout 60 omc --newBackend -d=backenddaeinfo,bltdump --simCodeTarget=None probe_r6.mo`：exit 0，仅 9 行 `class ProbeR6 … end ProbeR6;`（无后端输出）。
- A2 加 `-s`：exit 0，174 行。节选：
  ```
  #################################################

      [ODE] Differential-Algebraic: (Causalize)

  #################################################

  ================================
    (1) Continuous ODE Partition
  ================================

  BLOCK 1: Single Strong Component (status = Solve.UNPROCESSED)
  ---------------------------------------------------------------
  ### Variable:
  	Real z
  ### Equation:
  	[SCAL] (1) 2.0 * (1.0 - z) = 2.0 * z + x ($RES_SIM_1)
  …
      [ODE] Differential-Algebraic: (Solve)
  …
  --- Alias of INI[1 | 2] ---
  BLOCK 1: Single Strong Component (status = Solve.EXPLICIT)
  ------------------------------------------------------------
  ### Variable:
  	Real z
  ### Equation:
  	[SCAL] (1) z = -0.25 * ((-2.0) + x) ($RES_SIM_1)
  …
  Notification: Model statistics after passing the front-end and creating the data structures used by the back-end:
   * Number of equations: 3 (3)
   * Number of variables: 3 (3)
  Notification: Partition statistics after passing the back-end:
   * Number of ODE partitions: ..................... 1
   * Number of algebraic partitions: ............... 0
   * Number of ODE event partitions: ............... 0
   * Number of algebraic event partitions: ......... 0
   * Number of clocked partitions: ................. 0
   * Number of initial partitions: ................. 1
   * Number of initial(lambda=0) partitions: ....... 0
  Notification: Variable statistics after passing the back-end:
   * Number of states: ............................. 1 (1) ('-d=stateselection' for the list of states)
   * Number of discrete states: .................... 0 (0) ('-d=discreteinfo' or '-d=stateselection' for the list of discrete states)
   …
  Notification: [Simulation] Strong Component statistics after passing the back-end:
   * Number of single strong components: ........... 2 (scalar:2, array:0, record:0)
   * Number of multi strong components: ............ 0 (algorithm:0, when:0, if:0, tuple:0)
   * Number of for-loop strong components: ......... 0 (resizable: 0, generic: 0, entwined:0)
   * Number of algebraic-loop strong components: ... 0 (linear: 0, nonlinear:0)
  Notification: [Initialization] Strong Component statistics after passing the back-end:
   * Number of single strong components: ........... 3 (scalar:3, array:0, record:0)
  ```
  （线性环被别名/内联消掉，代数环计数为 0；`[ODE] … (Causalize)` 段在输出里出现两次，随后 `[INI] Initialization: (Causalize)`、`[ODE] … (Solve)`、`[INI] … (Solve)` 各一次。）
- E 非线性探针 `probe_r6_nl.mo`（`y = sin(z) + x; z = y*y - 3;`）同命令，exit 0，366 行，`[ODE] (Solve)` 段：
  ```
  BLOCK 1: Algebraic Loop (Linear = false, Mixed = false)
  ---------------------------------------------------------
  Strict Tearing Set
  ********************
  ### Iteration Variables:
  	[ALGB] (1) Real y;
  	[ALGB] (1) Real z;
  	[ALGB] (1) Real $FUN_1;
  ### Residual Equations:
  	[SCAL] (1) $RES_SIM_0 = ((-3.0) + y * y) - z ($RES_SIM_0);
  	[SCAL] (1) $RES_AUX_3 = sin(z) - $FUN_1 ($RES_AUX_3);
  	[SCAL] (1) $RES_SIM_1 = ($FUN_1 + x) - y ($RES_SIM_1);
  ### Inner Equations:
  	
  #########################################

      [NLS] Jacobian ODE_NLS_JAC_1: NLS
  …
  Notification: [Simulation] Strong Component statistics after passing the back-end:
   * Number of single strong components: ........... 1 (scalar:1, array:0, record:0)
   * Number of algebraic-loop strong components: ... 1 (linear: 0, nonlinear:1)
  ```
- D 旧后端 `timeout 60 omc -s -d=backenddaeinfo --simCodeTarget=None probe_r6.mo`：exit 0，节选：
  ```
  Notification: Model statistics after passing the front-end and creating the data structures used by the back-end:
   * Number of equations: 3
   * Number of variables: 3
  Notification: Model statistics after passing the back-end for initialization:
   * Number of independent subsystems: 1
   * Number of states: 0 ('-d=stateselection' for list of states)
   …
  Notification: Strong component statistics for initialization (3):
   * Single equations (assignments): 2
   …
   * Torn equation systems: 1
  Notification: Torn system details for strict tearing set:
   * Linear torn systems (#iteration vars, #inner vars, density): 1 system
     {(1,1,100.0%)}
   * Non-linear torn systems (#iteration vars, #inner vars): 0 systems
  Notification: Model statistics after passing the back-end for simulation:
   * Number of independent subsystems: 1
   * Number of states: 1 ('-d=stateselection' for list of states)
  ```
  同一模型旧后端保留了 1 个线性 torn 系统，新后端为 0 个代数环——两套后端的结构统计**不可直接互比**。
- B `timeout 60 omc -f probe_r6.mo`（与 `--baseModelica` 逐字节相同，`diff -q` 通过）：
  ```
  //! base 0.1.0
  package 'ProbeR6'
    model 'ProbeR6'
      Real 'x'(start = 1.0);
      Real 'y';
      Real 'z';
    equation
      der('x') = 'y' - 'x';
      'y' = 2.0 * 'z' + 'x';
      'z' = 1.0 - 0.5 * 'y';
    end 'ProbeR6';
  end 'ProbeR6';
  ```
- C 数组/绑定探针 `probe_r6_arr.mo`（`Real v[2](each start=0); Real w = k*v[1]; der(v) = -k*v + {1,2};`）：默认 `--baseModelica` 输出 `Real[2] 'v'(each start = 0.0); Real 'w' = 'k' * 'v'[1]; … der('v') = {1.0 - 'k' * 'v'[1], 2.0 - 'k' * 'v'[2]};`；加 `--baseModelicaOptions=scalarize,moveBindings` 后变成 `Real 'v[1]'(start = 0.0); Real 'v[2]'(start = 0.0); Real 'w';` 且 `'w' = 'k' * 'v[1]';` 移到 `equation` 段、`der('v[1]') = …`/`der('v[2]') = …` 拆成两条；`--baseModelicaFormat=nonScalarized` 与默认输出相同（本探针无差异）。
- F `checkModel(ProbeR6)`（.mos）：`"Check of ProbeR6 completed successfully.\nClass ProbeR6 has 3 equation(s) and 3 variable(s).\n0 of these are trivial equation(s)."`。
- 安装版 `omc --help` 第 493 行：`-f, --baseModelica         Outputs experimental Base Modelica.`；:579-584 列出 `--baseModelicaFormat`、`--baseModelicaOptions`。

## 未找到或待探针验证

- "Sizes of linear/nonlinear systems" 字样：`BackendDump.mo`、`Error.mo` grep 无结果；旧后端对应的是 `Equation system details (not torn)` / `Torn system details for %s tearing set`。
- 新后端源码无 "Torn" 块头；块头 `size = N`、`Homotopy = …`（`NBStrongComponent.mo:186, 223`）在安装版 1.25.0 输出中不存在（版本差异，工作区 HEAD 二进制因死循环风险未跑）。
- 新后端 `Number of clocked partitions` 硬编码 0（`NBackendDAE.mo:1601`），是否为已知 TODO 未在源码注释中找到说明。
- `[ODE] … (Causalize)` 在实测中打印两次的原因（是否 Pre-Opt 阶段也有名为 `Causalize` 的模块）未从源码核实，需 grep 模块注册表（`NBackendDAE.mo` 中 `getPreOptModules`/`getMainModules` 附近）确认。
- `--dumpFlatModel=<stage>` 各阶段的实际输出未实测（本任务未要求）。
- `-d=dumpSimCode` 的输出格式未读（`SimCodeDump`），本任务只记录了 flag 定义。

## 对规格的建议

1. ymc 的结构统计 dump 建议逐行对齐新后端 4 段 `Notification` 文本（§1），并同样采用 `标量数 (元素数)` 双计数，便于 `diff`；但对比测试时把 `Number of clocked partitions` 与 tuple 计数视为不可信项。
2. BLT dump 建议复用 `headline_1/2/3/4` 四级装饰与 `BLOCK i: <kind> (status = …, size = …)` / `### Variable:` / `### Equation:` 骨架，以及 `[SCAL] (n) lhs = rhs (name)`、`[ALGB] (n) Real y;` 行格式；oracle 对比用"块头种类序列 + 每块 size + 每块变量名集合"做结构级 diff，忽略表达式文本（OMC 会做 alias/inline 改写，见 §8 A2）。
3. 前端级对比优先用 `-f` 的 Base Modelica 文本（`//! base 0.1.0` 头、单引号标识符、固定段顺序），用 `--baseModelicaOptions=scalarize,moveBindings` 得到完全标量化、绑定移入 `equation` 的规范形，与 ymc 压平结果做文本 diff；`checkModel` 的 `N equation(s) and M variable(s)` 可作为第二道计数校验，但要注意其规则（public input 计一条方程、参数/常量不计，`CheckModel.mo:109-137`）。
4. oracle 脚本必须带 `-s`（否则后端不跑，§7），并固定记录运行的 omc 版本，因为块头字段集合随版本变化（§8 与 `NBStrongComponent.mo:186` 的差异）。
