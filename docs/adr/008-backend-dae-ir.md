# ADR-008 S5 后端：BackendDAE IR、`.bdae` 文本格式与结构对拍口径

- 日期：2026-10-12（提议）；2026-10-13（用户裁定，见 §3）
- 状态：**已决定**（§3 七条照草稿接受，其中第 3 条块序改为与 OMC 逐位同序；Q1 / Q2 按 §4 的裁定）
- 依据：OMC **v1.25.0 tag** 源码（`git -C C:/dev/OpenModelica show v1.25.0:<path>`；下文路径相对 `OMCompiler/Compiler/`，
  行号均为该 tag，不引工作区 HEAD）；安装版 omc 1.25.0 实跑（附录 A，均套 `timeout 120`）；方案 §2.1 / §4.2 / §5 S5–S6；
  spec FR-015 / FR-053；研究底稿 R6（R6 的行号是 HEAD `3ceb0c9d06` 的，本文已按 v1.25.0 重新定位）。

## 背景
S3 的 FlatModel 已与 omc 1.25.0 逐字节一致（testsuite 1787 / MSL 438）。S5 要把它变成因果化的 BackendDAE：5a lower + 预处理 →
5b 邻接 / 匹配 / 排序 → 5c 指标约简 → 5d 初始化 → 5e Solve / 撕裂 / 雅可比；S6 再据此出 C 代码、填 OMC 运行时回调表。方案 §2.1
要求 `.bdae` "能 dump、能 parse、对齐 `-d=bltdump`"；spec FR-053 已定 S5 门"只与新后端（`--newBackend`）比"。动手前要定三件
改起来很贵的事：IR 形状、`.bdae` 格式、对拍口径。下面先列核实过的事实，其中几条改变了方案里的默认假设。

## 1 已核实的事实
- **F1 1.25.0 默认走旧后端。** `Util/Flags.mo:1264-1266` `NEW_BACKEND` 默认 `BOOL_FLAG(false)`，帮助文本标 "[WIP]"；
  `SimCode/SimCodeMain.mo:1140` 开了它才进 `translateModelCallBackendNB`（:1445；lower :1463、main :1467、出码 :1471）。实跑：
  不带 `--newBackend` 时 `-d=backenddaeinfo` 打旧后端的 "Model statistics after passing the back-end for …"
  （`BackEnd/BackendDAEUtil.mo:7767-7768` → `BackendDump.dumpCompShort`），带了才是 NB 的 "Partition statistics …"
  （`NBackEnd/Classes/NBackendDAE.mo:1534 backenddaeinfo`）。
- **F2 块结构 dump 只在 NB 里有。** `-d=bltdump` 的帮助是 "Dumps information from index reduction."（`Flags.mo:266-267`），
  旧后端 51 处用法里 40 处在 `BackEnd/IndexReduction.mo`，其余在 Matching / Initialization / SymbolicJacobian / DAEMode。
  NB 在名为 `Causalize` 或 `Solve` 的模块之后打印整个 BackendDAE（`NBackendDAE.mo:374`）；`Causalize` 段出现两次——主流程一次（mainModules :280-285），初始化子流程里同名模块再一次
  （`NBackEnd/Modules/1_Main/NBInitialization.mo:149`，顺带打出 `[INI]` 段）。`-d=optdaedump` 在**每个**模块之后都打（同在 :374），
  另有一段 "(After Lowering)"（`SimCodeMain.mo:1464-1466`）。分区为空或没开 bltdump 时打 VarData + EqData，否则只打分区与块
  （`NBackendDAE.mo:150-169`）。
- **F3 NB 吃的 FlatModel 不是 ymc 这份。** `--newBackend` 时 `NFFrontEnd/NFInst.mo:320-339 resetGlobalFlags` 关掉 `nfScalarize`、
  打开 `vectorizeBindings`，`:246-254` 再做 `combineBinaries` 与数组构造器替换；ymc 对齐的是默认路径（`Flags.mo:500`
  `nfScalarize` 默认 true）。实跑 Adr008C（数组状态）：NB 前端后计数 `4 (3)`（标量 4、元素 3），保留 `Real[2] $DER.v` 与
  `[ARRY] (2)` 方程，`translateModel` 返回 `false` 且无错误文本。
- **F4 NB 1.25.0 的能力边界。** 无指标约简：`Modules/1_Main/NBMatching.mo:149` 对非初始化分区调
  `ResolveSingularities.noIndexReduction`（`NBResolveSingularities.mo:282`，"fails if the system has unmatched variables"），
  `indexReduction`（:63）没人调。连续代数环不撕裂：`Modules/3_Post/NBTearing.mo:214-228` 把所有 `--tearingMethod` 都映射到
  `minimal`，而 `minimal`（:364）只把离散变量拿出来当内部方程；实跑 Adr008B 的环是 `Iteration Variables: u, v`、
  `Inner Equations` 为空。统计里 `Number of clocked partitions` 硬编码 `"0"`（`NBackendDAE.mo:1551`），multi 合计不含 tuple（:1634）。
- **F5 新旧后端在三个探针上结构都不同**（附录 A）：

| 探针 | NB（`--newBackend`）`[Simulation]` | 旧后端（默认）simulation |
|---|---|---|
| Adr008A：1 状态 + 线性 2×2 环 | 3 single、0 环（Alias 把 `a` 消成 `a = 2.0 * b + time`） | 2 single + 1 线性 torn 系统 `{(1,1,100.0%)}` |
| Adr008B：非线性 2×2 环 + when | 2 single + 1 multi(when) + 1 非线性环（2 迭代变量）；ODE 分区 0、ODE event 分区 1 | 2 single + 1 when + 1 非线性 torn `{(1,1)}` |
| Adr008C：数组状态 + 别名 | `translateModel` 失败（F3） | 2 状态、3 single |

- **F6 OMC 的若干输出顺序是哈希桶序。** 邻接行 = `UnorderedSet.toList(occ[index])` 过滤（`Util/NBAdjacency.mo:500-501`）；分区的
  未知量 / 方程 = `UnorderedSet.toList(cluster.…)`（`Modules/1_Main/NBPartitioning.mo:551-552`），分区列表 =
  `UnorderedMap.valueList(cluster_map)`（:772）。桶序由 `ComponentRef.hash`（`NFFrontEnd/NFComponentRef.mo:1448`，djb2）决定
  （`Util/UnorderedSet.mo:250-260`），Windows 版 djb2 用 32 位 `long`（`docs/open-questions.md` 平台分歧条，探针 YmcExpHash2）。
  匹配按行序取第一个空闲变量（`NBMatching.mo:358-381`），Tarjan 按行序走前驱（`Modules/1_Main/NBSorting.mo:320`、`:661`）。
  实跑 Adr008A：分区未知量序是 `$DER.x, y, b`，VarData 桶序是 `x | $DER.x | b, y`。
- **F7 S6 的 ABI 与选哪个后端无关。** NB 的 SimCode 先转成旧 SimCode 再走同一套模板（`SimCodeMain.mo:406` create、
  `:411 NSimCode.SimCode.convert`、`:425 callTargetTemplates`；`--simCodeTarget=None` 只跳过模板，:701-702）；回调表
  `OMCompiler/SimulationRuntime/c/openmodelica_func.h:56`（v1.25.0 与安装版 `include/omc/c` 逐字节相同）。
- **F8 方案点名的首批子集是"旧前端 + 旧后端"用例。** `simulation/modelica/{equations,parameters,commonSubExp}` 共 108 个 `.mos`
  （47 / 21 / 40），全部 `// cflags: -d=-newInst`、`simulate(...)` 走旧后端；commonSubExp 多数还开了旧后端专有模块
  （`--postOptModules+=wrapFunctionCalls,cseBinary`、`-d=dumpCSE`）。它们的 `// Result:` 当不了 NB 结构 oracle。v1.25.0 另有专为
  NB 写的 `simulation/modelica/NBackend/`（111 个 `.mos`：basics 16、alias 10、event_handling 4、solve 4、simplification 4、
  tearing 2 …，用 `--newBackend` 配 `-d=dumprepl / stateselection / dumpSolve` 等）。

## 2 决定（提议）

### D1 IR：照 NB 的 `BackendDAE.MAIN`，一处所有，桶是视图
```cpp
namespace ymc::bdae {
// 变量：NFFrontEnd/NFBackendExtension.mo VariableKind :179-236、BackendInfo :69-78
enum class VarKind : std::uint8_t {
  Time, Algebraic, State, StateDer, Discrete, DiscreteState, Previous, Parameter, Constant,   // 5a–5b
  DummyDer, DummyState, Start, Iterator, Clock, Clocked, Record, ExtObj,                       // 5c / 5d / 以后
  JacVar, JacTmpVar, SeedVar, ResidualVar };                                                   // 5e
struct Var {
  ExprCref name;                // x / $DER.x / $PRE.n / $SEV_0 / $FUN_1（NBVariable.mo:100-113）
  Type ty; VarKind kind; std::vector<FlatVarAttr> attrs; Id<Expr> binding;
  Id<Var> partner;              // State↔StateDer（STATE.derivative / STATE_DER.state）、Previous↔原变量（var_pre）
  int state_index = 0;          // STATE.index，5c 用
  std::uint32_t flat_origin; SourceLoc loc; };     // flat_origin：来自哪个 FlatVariable，--trace 用（方案 §3.5-5）
struct VarSet { std::vector<Id<Var>> items; };     // VariablePointers NBVariable.mo:1568-1573；名→位置索引只查不遍历
struct VarData {                                   // VAR_DATA_SIM NBVariable.mo:2053-2084，22 个视图
  VarSet variables, unknowns, knowns, initials, auxiliaries, alias_vars, non_trivial_alias,
         derivatives, algebraics, discretes, discrete_states, clocked_states, previous, clocks,
         states, top_level_inputs, resizables, parameters, constants, records, external_objects, artificials; };
// 方程：NBEquation.mo Equation :899-966、EquationAttributes :3714-3721、EquationKind :3790
enum class EqnKind : std::uint8_t { Scalar, When, If, Algorithm, Aux, Dummy, /* 数组后端 */ Array, Record, For };
enum class EqnTime : std::uint8_t { Continuous, Discrete, Clocked, Empty, Unknown };
struct Eqn { EqnKind kind; ExprCref name /* $RES_SIM_n */; Id<Expr> lhs, rhs; int size = 1; EqnTime time;
             bool initial_only = false; Id<Eqn> derivative; /* When/If/Algorithm 体复用 flat 的分支结构 */ SourceLoc loc; };
struct EqnSet { std::vector<Id<Eqn>> items; };     // EquationPointers :3841-3845
struct EqnData { int unique_index = 0;             // EQ_DATA_SIM :4303-4313，8 个视图
  EqnSet equations, simulation, continuous, clocked, discretes, initials, auxiliaries, removed; };
// 5b：NBAdjacency.mo MatrixStrictness :70、Matrix :367-391、Solvability :1441-1453；NBMatching.mo MATCHING :66-69
enum class Strictness : std::uint8_t { Linear, Matching, Sorting, Full };
enum class Solvability : std::uint8_t { Unknown, Unsolvable, Implicit, ExplicitNonlinear, ExplicitLinear };
struct Adjacency { Strictness st; std::vector<std::vector<int>> m, mT;              // FINAL
                   std::vector<std::vector<std::pair<int, Solvability>>> full; };     // FULL 的可解性
struct Matching { std::vector<int> var_to_eqn, eqn_to_var; };
// NBStrongComponent.mo :102-164；Solve.Status NBSolve.mo:70
enum class CompKind : std::uint8_t { Single, Multi, AlgebraicLoop, /* 数组 / 5e */ Sliced, Resizable, Generic, Entwined, Alias };
enum class SolveStatus : std::uint8_t { Unprocessed, Explicit, Implicit, Unsolvable };
struct Comp;
struct TearingSet { std::vector<Id<Var>> iteration; std::vector<Id<Eqn>> residual; std::vector<Id<Comp>> inner; };
struct Comp { CompKind kind; std::vector<Id<Var>> vars; std::vector<Id<Eqn>> eqns; SolveStatus status;
              bool linear = false, mixed = false; TearingSet strict; std::optional<TearingSet> casual; };
// NBPartition.mo Kind :66、PARTITION :176-185；BackendDAE.MAIN NBackendDAE.mo:107-124
enum class PartKind : std::uint8_t { Ode, Alg, OdeEvt, AlgEvt, Ini, Dae, Jac, Clk };
struct Partition { int index; PartKind kind; VarSet unknowns; EqnSet equations;
                   std::optional<Adjacency> adj; std::optional<Matching> matching; std::vector<Id<Comp>> comps; };
struct BackendDAE { std::vector<Partition> ode, algebraic, ode_event, alg_event, clocked, init;
  std::optional<std::vector<Partition>> init_0, dae; VarData vars; EqnData eqns; EventInfo events;  // NBEvents.mo:127-133
  Arena<Var> var_arena; Arena<Eqn> eqn_arena; Arena<Comp> comp_arena; ExprArena exprs; };
}
```
1. **所有权**：Var / Eqn / Comp 各一个 Arena；VarData 的 22 个桶、EqData 的 8 个桶、分区的 unknowns / equations 都是
   `std::vector<Id<…>>` 视图，对应 OMC 多处共享同一个 `Pointer<Variable>`（方案 §4.2）。改种类（DetectStates 把 x 从 `[ALGB]`
   改成 `[STAT]`）= 改 arena 里那一格 + 重建受影响的视图。
2. **顺序**：视图保持插入序，按名查找的索引只查不遍历（方案 §3.5-6）。OMC 里本来就是哈希序的地方（F6）IR 不复刻，交给对拍口径（D4）。
3. **名字**：后端变量名照 NB 常量 `$DER $PRE $START $SEV $TEV $FUN $AUX $RES …`（`NBVariable.mo:100-113`）。方程名照
   `lowerEquationData`：`Equation.createName`（`NBackendDAE.mo:836`）按 `lowerEquationsAndAlgorithms` 头插得到的列表编号（:878–:900），
   所以**最后一条方程是 `$RES_SIM_0`**。实跑 Adr008A：`y = …` 是 `$RES_SIM_0`，`der(x) = …` 是 `$RES_SIM_3`。
4. **表达式**：复用 `Expr`（ADR-006 D1），`ExprCref` 加一个 `Id<Var>` 解析位，对应 NB lower 时把 cref 指向后端变量
   （`NBackendDAE.mo:657`、`:843`）。`der(x)` 到 DetectStates 才换成 `$DER.x`（实跑：Remove Stream 后仍是 `der(x)`、x 仍是 `[ALGB]`；
   Detect States 后是 `$DER.x = …`、x 变 `[STAT]`）。
5. **先做的 kind**：5a–5b 只实现注释里标 "5a–5b" 的那几种（加 `EqnKind` 的 Scalar / When / If / Algorithm / Aux，`CompKind` 的
   Single / Multi / AlgebraicLoop，`PartKind::Ode`）；其余先进枚举，处理处 `YMC_UNREACHABLE`，靠 `-Werror=switch` 保证日后补齐不漏。

### D2 输入：ymc 内存里的 FlatModel，配四条准入
- 从 `src/flat/flat.h` 的 `FlatModel` 直接 lower，不先写 Base Modelica parser。变量种类照 `lowerVariableKind`（`NBackendDAE.mo:700-777`）：
  连续 Real → ALGEBRAIC；连续 Integer / Boolean / 枚举 → DISCRETE（:754-756）；discrete → DISCRETE（:759）；parameter →
  PARAMETER 且 fixed = true（:762、:774）；constant → CONSTANT（:765）；`stateSelect = always` → STATE（:736）；顶层 input 进
  inputs + knowns（:551）；大小为 0 的变量不进任何桶（:549）。方程逐 case 对照 `lowerEquation`（:904 起），任务卡细化。
- **准入**（进 S5 对拍集前四条全满足，否则 SKIP 并按原因计数）：
  - A1 该模型 ymc 压平已与 omc 1.25.0 逐字节一致（S3 门）。
  - A2 NB 前端后计数 `Number of equations: N (n)` / `variables: N (n)` 里 N == n（`NBBindings.mo:120`），即 NB 的输入里没有
    残留数组 / 记录，与 ymc 的标量化输入等价（F3）。
  - A3 `omc --newBackend` 跑通：exit 0、`translateModel` 为 true、无 Error。
  - A4 OMC 在该模型上"非恒等"的 pre 模块（D5）ymc 都已实现。
- Base Modelica parser（方案 §2.1）留作后续选项：ymc 前端某类模型还没过 S3 门、又想先推后端时再做。注意 `-f` 的文本是 NF
  **未标量化**的 FlatModel（`NFInst.mo:221-223`），要 `--baseModelicaOptions=scalarize` 才是标量化的（:241-244）。

### D3 `.bdae`：沿用 NB 自己的 dump 文本，只补回读必需的东西
一个 `.bdae` = 某 pass 之后 BackendDAE 的全量快照，四段，顺序固定：
1. 头行 `//! ymc-bdae 0 <模型名> after=<pass>`。凡 `//!` 开头的行是 ymc 私有，对拍前删掉。
2. VarData：照 `VarData.toStringVerbose(full = true)`（`NBVariable.mo:2187`，桶序 :2205-2222），行格式 `(i) [TAG] (size) 声明`
   （`VariablePointers.toString` :1575；TAG 表 `NFFrontEnd/NFBackendExtension.mo:243-274`）。
3. EqData：照 `EqData.toString(level = 1)`（`NBEquation.mo:4416`），行格式 `(i) [SCAL] (size) lhs = rhs ($RES_…)`（:976-986），
   when / if / algorithm 的多行体用 `[----]` 前缀。
4. 分区与块：照 bltdump 分支（`NBackendDAE.mo:155-169`、`Partition.toString` `NBPartition.mo:187`、块头
   `NBStrongComponent.mo:166`）和 `EventInfo.toString`（`NBackendDAE.mo:169`）；匹配、邻接矩阵是可选附段（`//! matching …`；
   邻接矩阵版式对齐 `-d=bltmatrixdump`，`NBAdjacency.mo:436`、`Matrix.toString` :854）。

装饰横线逐字照 `Util/StringUtil.mo:52-78 headline_1..4`。于是同一个 parser 既读 ymc 的 `.bdae`，也读 omc
`-d=optdaedump,bltdump` 的输出。分区建立后、bltdump 开着时 OMC 不再打 VarData / EqData（F2），所以第 2、3 段在 ymc 里常驻，
是为回读补的，版式仍是 OMC 的。

**解析**：表达式文法 = S1 的 Modelica 表达式文法 + 允许 `$` 开头的名字段（`$DER.x`、`$RES_SIM_2`、`$SEV_0`；MLS §2.3.1 的 IDENT
不含 `$`）。块里的 `### Variable:` / `### Equation:` 只按名字（变量名、行尾 `($RES_…)`）回指第 2、3 段，不重复解析声明。
**回读契约**：`dump(parse(dump(x))) == dump(x)` 逐字节；`ymc --from=x.bdae --run=<pass>` 与内存流水线结果逐字节相同（方案 §3.5-4 / -12）。

例：Adr008A 在 Causalize 之后（除头行和 `//!` 行外逐字摘自附录 A3 的实跑输出，`…` 为省略）：
```
//! ymc-bdae 0 Adr008A after=Causalize
=================================================
  Variable Data Simulation (scalar unknowns: 3)
=================================================

State Variables (1/1)
***********************
(1)       [STAT] (1) Real x (fixed = true, start = 1.0)

…（Derivative：(1)       [DER-] (1) Real $DER.x）
Algebraic Variables (2/2)
***************************
(1)       [ALGB] (1) Real b
(2)       [ALGB] (1) Real y
…（Parameter：(1)       [PRMT] (1) parameter Real k = 2.0 (fixed = true)；Artificial：$ANY$、time）
=============================================================
  Equation Data Simulation (scalar simulation equations: 3)
=============================================================

Continuous Equations (3/3)
****************************
(1)       [SCAL] (1) $DER.x = -k * (2.0 * b + time) ($RES_SIM_3)
(2)       [SCAL] (1) (2.0 * b + time) + b = x ($RES_SIM_2)
(3)       [SCAL] (1) y = 2.0 * x + b ($RES_SIM_0)

Removed Equations (1/1)
*************************
(1)       [SCAL] (1) a = 2.0 * b + time ($RES_BND_4)
…（headline_1 "[ODE] Differential-Algebraic: (Causalize)"）
================================
  (1) Continuous ODE Partition
================================

BLOCK 1: Single Strong Component (status = Solve.UNPROCESSED)
---------------------------------------------------------------
### Variable:
	Real b
### Equation:
	[SCAL] (1) (2.0 * b + time) + b = x ($RES_SIM_2)
…（BLOCK 2：$DER.x ← $RES_SIM_3；BLOCK 3：y ← $RES_SIM_0）
//! matching ODE(1) b=$RES_SIM_2 $DER.x=$RES_SIM_3 y=$RES_SIM_0
```
它投影出的结构签名（D4）：`5a: STAT{x} DER-{$DER.x} ALGB{b,y} PRMT{k} | continuous 3, removed 1`；`5b: ODE(1) = {S{b}, S{$DER.x}, S{y}}`。
同一模型旧后端是"2 single + 1 线性 torn"（F5），这正是只能和 NB 比的原因。

### D4 结构对拍协议
- **参考生成**（每模型一次；安装版 omc；cwd = 临时目录；`timeout 120`）：
  `omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo M.mo`
  ——一次输出里有：每个 pre 模块后的 VarData / EqData，之后每个模块后的分区与块，末尾四段统计（附录 A3）。不带 `-s` 的命令行
  不跑后端（`Main/Main.mo:441`）。库模型用 `.mos`：`loadModel(Modelica, {"4.0.0"}); setCommandLineOptions("--newBackend
  --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo"); translateModel(P.M);`（`translateModel` 以 runBackend = true 调用：
  `Script/CevalScriptBackend.mo:1169`，形参见 :3623）。参考照 spec FR-030 存仓库外 `C:\dev\ymc-refs\omc-installed\1.25.0\bdae\`、写
  manifest，记录 omc 版本串。
- **投影成结构签名**（扩展 `tools/structcompare.py`），只比结构、不比表达式文本：

| 子阶段 | 取 OMC 哪一段 | 签名 |
|---|---|---|
| 5a | `BackendDAE: (<模块>)`；门用 `(Events)`（pre 末尾，`NBackendDAE.mo:268-278`） | 各 VarData 桶的变量名集合；各 EqData 桶的方程数与标量数；`Number of equations/variables: N (n)` |
| 5b | 第一个 `[ODE] … (Causalize)` 段（`[INI]` 归 5d） | 各分区按种类的块多重集，块 = (Single / Multi / Loop(Linear, Mixed), 变量名集合, 标量数) |
| 5e | `(Solve)` 段 + 末尾 `[Simulation]` / `[Initialization]` 统计 | 状态数、各类分量计数（Categorize 会把含 when 的分区挪到 ODE_EVENT，见 F5 Adr008B） |

- **序的口径（用户裁定 2026-10-13：与 OMC 逐位同序）**：块序、分区内未知量 / 方程的列举序、分区列表的序都要与 OMC 逐位相同——
  照 S3 的逐字节标准。为此 ymc 在后端里复刻 OMC 的哈希容器语义：`UnorderedSet` / `UnorderedMap`（`Util/UnorderedSet.mo`、
  `Util/UnorderedMap.mo`）的桶数与扩容策略、插入与遍历序，以及 `ComponentRef.hash`（`NFFrontEnd/NFComponentRef.mo:1448`，djb2）在
  Windows 上的 32 位 `long` 行为（F6），再照 NB 的取行序做匹配（`NBMatching.mo:358-381`）与 Tarjan（`NBSorting.mo:320` / `:661`）。
  这一块是 S5b 的前置子任务（"OMC 哈希容器复刻"），先用探针核实遍历序与 OMC 逐位一致再接匹配 / 排序。块仍以变量名集合为键做
  诊断（键集合相同 = SCC 划分相同，Pothen & Fan, ACM TOMS 16(4), 1990），但门禁比的是逐位序。
- **不计项**：`//!` 行；`--- Alias of … ---` 包装与 `status = …`（5e 再比）；Jacobian 段；`Number of clocked partitions` 与 tuple 计数（F4）。
- **旧后端**（默认）只作旁证，不进门：两套后端结构不同是常态（F5）。
- **首批子集**（替换方案的 equations / parameters / commonSubExp，理由 F8）：先 `simulation/modelica/NBackend/{basics,alias,event_handling}`
  里满足准入的模型，再加 `equations`、`parameters` 两目录 `.mo` 里满足准入的模型。只借模型，参考一律实跑生成，不用文件 `// Result:`。
  commonSubExp 推到 FunctionAlias（`Modules/2_Pre/NBFunctionAlias.mo:68`，NB 里管 CSE 的模块）实现之后。

### D5 S5a 范围与验收
**范围**：lower（`lowerVariableData` `NBackendDAE.mo:505`、`lowerEquationData` :815）+ 按 NB 原序的九个 pre 模块（:268-278）：
Bindings（`Modules/2_Pre/NBBindings.mo:50`）、FunctionAlias（`NBFunctionAlias.mo:68`）、Early Inline、Simplify 1、Alias
（`NBAlias.mo:113`；`-d=dumprepl` 打替换集，:313）、Simplify 2、Remove Stream、Detect States（`NBDetectStates.mo:71`）、Events
（`NBEvents.mo:80`）。**按模块增量做**：OMC 的逐模块 dump 能机械判断"这个模块对这个模型是不是恒等"，于是模块一个个实现、准入 A4
随之放宽，不用等九个一起完工。

**验收**（全部满足才算 S5a 完成）：
1. 准入集里每个模型，ymc `--dump-after=events` 的 5a 签名与 omc `(Events)` 段签名一致，`Number of equations/variables` 一致；逐模块
   中间签名作诊断，第一处不同即停（方案 §6.5）。
2. 回读：每个准入模型 `dump(parse(dump))` 逐字节不变，`--from=*.bdae --run=<pre 模块>` 与内存流水线逐字节一致；同输入跑两次逐字节一致。
3. 单测期望值来自 oracle（探针实跑或引 OMC 行号），不照自己的实现写；三套编译器 + ASan 全绿、零警告（CLAUDE.md 收尾清单）。
4. SKIP 按 A1–A4 分桶计数，进报告；S3 门（flat_sweep、MSL）不退——S5 与前端共用 `Expr`。

### D6 给 5c–5e 与 S6 留的口子
- 5c：`VarKind::DummyDer / DummyState`、`Var.state_index`、`Eqn.derivative`（`EquationAttributes.derivative`，`NBEquation.mo:3715`）。
  1.25.0 NB 没有可对的参考（F4）→ Q1。
- 5d：`PartKind::Ini`、`VarKind::Start`（`$START`）、`init_0`；1.25.0 的 `[INI] … (Causalize)` / `(Solve)` 段可直接当参考（附录 A1）。
- 5e：`Comp.status`、`TearingSet strict / casual`、`CompKind::Alias`（`NBStrongComponent.mo:159-164`）、雅可比
  （`BackendDAE.JACOBIAN`，`NBackendDAE.mo:126-133`）。
- S6：`NSimCode.create`（`NSimCode/NSimCode.mo:267`）按分区建块——`init` :348，`ode / algebraic / ode_event / alg_event` :376-379，
  `ode + ode_event` 出 Jacobian A :419——再加 VarData 桶与 EventInfo，对到回调 `functionODE` :109、`functionAlgebraics` :113、
  `functionDAE` :117、`functionInitialEquations` :163、`function_ZeroCrossings` :220、`initialNonLinearSystem / LinearSystem /
  MixedSystem` :70 / :78 / :86、`initialAnalyticJacobianA` :267（F7）；变量下标顺序照 `NSimCode/NSimVar.mo:207 traverseCreate`。
  预计 S6 只读不改 IR 形状，留给 S6 的 ADR 复核。

## 3 需要用户拍板的决定（2026-10-13 裁定：1、2、4–7 照草稿；3 改为与 OMC 逐位同序）
1. **结构 oracle = 安装版 1.25.0 的 NB（`--newBackend`）**，与 FR-053 一致；旧后端只作旁证、不计 FAIL。代价：数组、指标约简、连续
   撕裂这几块 NB 1.25.0 给不出参考（F3 / F4），另定（Q1 / Q2）。
2. **准入 A1–A4**。A2（N == n）会把数组 / 记录模型整体挡在 S5 结构门外，留给 S6 的 L2 数值验收和以后的数组后端。
3. **块序口径**：推荐"块集合相同 + 拓扑序合法"；另一选项是要求与 OMC 逐位同序，代价是在遍历里复刻 `UnorderedSet / UnorderedMap`
   的桶序和 32 位 djb2（F6）。
4. **`.bdae` = NB dump 原文版式 + 头行 + 常驻 VarData / EqData + `//!` 附段**；表达式文法加 `$` 名字段。
5. **首批子集**改为 `NBackend/{basics,alias,event_handling}` + equations / parameters 的准入模型，commonSubExp 推后（F8）。
6. **S5a 按 pre 模块增量推进**（A4），不一次做完九个。
7. **更正方案**：§4.2 的 NB 锚点换成 v1.25.0 行号（`VAR_DATA_SIM` `NBVariable.mo:2053`，22 个桶而非 23；`Equation` `NBEquation.mo:899`；
   `EQ_DATA_SIM` :4303；`PARTITION` `NBPartition.mo:176`；块记录 `NBStrongComponent.mo:102-164`）；§2.1 的"对齐 `-d=bltdump`"注明
   "块结构只在 `--newBackend` 下有"（F2）。

## 4 待决问题（确认后转入 `docs/open-questions.md`）
- **裁定（2026-10-13）**：S5 的验证口径 = 5a / 5b 对 1.25.0 NB 的结构（本 ADR D4），之后**尽早接 S6**（借 OMC 运行时出码仿真），
  5c 指标约简与 5e 撕裂**以 L2 仿真数值为准**、旧后端统计（`-d=backenddaeinfo` 的状态数 / torn system 规模）作旁证——即 Q1、Q2
  的答案。其余各问仍待定。
- Q1 5c 的 oracle：1.25.0 NB 没有指标约简。候选：旧后端（`-d=backenddaeinfo,stateselection` 的状态集与个数）、只看 L2 数值，或别的；
  工作区 HEAD 的 NB 有死循环风险、也不是 1.25.0，按现行规则不能当 oracle。
- Q2 5e 撕裂：NB 1.25.0 只有 minimal（F4）。若照方案"Cellier 看旧后端"去做，就只能对旧后端的 `Torn system details` 统计，与"只和新
  后端比"冲突。
- Q3 要不要追求 `.bdae` 与 OMC dump 全文逐字节一致（S3 的标准）？障碍：NB 输入做过 `combineBinaries`（F3），Alias / Simplify 改写后的
  表达式形状、`$RES_SIM_n` 编号都得复刻。建议结构签名稳定后再评估。
- Q4 `NBackend/` 用例文件里的 `// Result:` 与实跑 1.25.0 不一致时，是否照 S3 惯例记分歧账本、以实跑为准？
- Q5 Dymola 结构对比（FR-053 后半：线性 / 非线性系统规模）从哪个子阶段接入？
- Q6 记录：NB 把 record 变量 lower 成 `RECORD` 种类（`NBackendDAE.mo:747-752`）。A2 是否足以挡住所有"NB 输入与 ymc 输入不等价"的
  情形，要用含 record 的探针确认。

## 5 后果
- 得到：IR 与 NB 一一对应、每个字段有 v1.25.0 锚点；一个 parser 读两边的文本；对拍全程机械化；S5a 可以按模块增量推进。
- 放弃：S5 结构门不覆盖数组 / 记录模型和 NB 1.25.0 跑不通的模型（SKIP 计数可见）；若采纳推荐，块序不与 OMC 逐位对齐。
- 风险：NB 1.25.0 是实验性后端（"[WIP]"），与 omc 默认路径（旧后端）结构不同。S5 结构一致不等于和默认 omc 仿真一致，最终靠 S6 的
  L2 数值验收兜底。
- 何时重看：用户裁定 Q1 / Q2 后；开始做保持数组的后端时；oracle 换成更高版本 omc 时。

## 附录 A 实跑记录
安装版 `omc --version` = `OpenModelica v1.25.0 (64-bit)`；cwd 为会话 scratch（探针与原始输出未入库，入库时建议放 `tools/probe/s5/`）；
每条命令都包 `timeout 120`，全部 exit 0。探针 `adr008.mo` 的三个模型（声明 + 方程）：
- Adr008A：`parameter Real k = 2; Real x(start = 1, fixed = true); Real a, b, y;` `der(x) = -k * a; a + b = x; a - 2 * b = time; y = 2 * x + b;`
- Adr008B：`Real x(start = 1, fixed = true); Real u, v; discrete Integer n(start = 0, fixed = true);`
  `der(x) = -u; u = v * v + x; v = time - u * u; when x < 0.5 then n = pre(n) + 1; end when;`
- Adr008C：`parameter Real p = 3; Real v[2](each start = 0, each fixed = true); Real s, t;` `der(v) = {-v[1] + s, -p * v[2]}; s = t; t = sin(time);`

- **A1 NB**：`timeout 120 omc nb.mos`，`nb.mos` = `loadFile("adr008.mo"); setCommandLineOptions("--newBackend --simCodeTarget=None
  -d=backenddaeinfo,bltdump"); translateModel(Adr008A); translateModel(Adr008B); translateModel(Adr008C);`（每句后接 `getErrorString();`）。
  A、B 返回 `true`，C 返回 `false`。摘录：A 的 `Number of equations: 4 (4)`、`[Simulation]` 段 `single strong components: … 3
  (scalar:3, array:0, record:0)`、`algebraic-loop strong components: … 0 (linear: 0, nonlinear:0)`；B 的 `multi strong components: … 1
  (algorithm:0, when:1, if:0, tuple:0)`、`algebraic-loop strong components: … 1 (linear: 0, nonlinear:1)`、`Number of ODE event partitions:
  … 1`；C 的 `Number of equations: 4 (3)`。
- **A2 旧后端**：`timeout 120 omc ob.mos`，`ob.mos` = `loadFile("adr008.mo"); instantiateModel(Adr008A); setCommandLineOptions(
  "--simCodeTarget=None -d=backenddaeinfo"); translateModel(Adr008A); translateModel(Adr008B); translateModel(Adr008C);`。摘录（simulation 段）：
  A 的 `Single equations (assignments): 2`、`Torn equation systems: 1`、`Linear torn systems (#iteration vars, #inner vars, density): 1 system
  {(1,1,100.0%)}`；B 的 `When equations: 1`、`Non-linear torn systems (#iteration vars, #inner vars): 1 system {(1,1)}`；C 的
  `Number of states: 2`、`Single equations (assignments): 3`。
- **A3 一次拿全**：`timeout 120 omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo adr008a.mo`（`adr008a.mo`
  只含 Adr008A）→ 1265 行：`BackendDAE: (After Lowering)`、`(Bindings)` … `(Events)` 十段 VarData / EqData，之后 `[ODE] …` / `[INI] …`
  每模块一段（`(Causalize)` 在主流程与初始化里各一次），末尾四段统计与压平文本。单跑 `-d=optdaedump,backenddaeinfo`：1234 行，全程
  VarData / EqData（D3 例子中的 VarData / EqData 段取自这次）。
