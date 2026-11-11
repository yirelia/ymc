# S5a-0 准入普查与 F3 实测（2026-10-31）

ADR-008 的 S5a 按模块增量推进，动手前用实跑数据回答：哪些模型能进后端对拍、模块先做哪个、NB 的输入和 ymc 的 FlatModel 差多少。
本文所有计数都是对 omc 1.25.0 全量实跑（`--newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo`，每模型 120 s），不是抽样，
例外处已标明。OMC 行号均为 v1.25.0 tag。工具用法见 `tools/README.md`，产物在仓库外 `C:\dev\ymc-refs\omc-installed\1.25.0\bdae\`。

## 结论

1. **`(After Lowering)` 能逐字节复现。** 用 Python 原型从 ymc 压平文本按 NB 规则预测整段：741 个标量模型里 615 个逐字节相同，再加 94 个只差
   FlatVariable 没存的前缀或打印细节。真正的前端路径差异只有 2 个，都和数组有关（§3）。这回答了 ADR-008 Q3 的前半；Simplify 之后的模块没测。
2. **A2（N == n）是错的**：放进 133 个带数组的模型，又误挡 33 个标量多输出 algorithm。换成按 `(After Lowering)` 形状判的 A2'（§4）。
3. **A3 不该卡 5a / 5b**：跑到 `(Events)` 就有 5a 参考。A3 留给 S6。
4. **哈希序在 lowering 就起作用**（when 拆分，§3 #10），S5b-0 要提前到多赋值 when 进门之前。

## 1 语料

- 语料 a（109 条）：`simulation/modelica/NBackend/{basics,alias,event_handling}` 34 条（来自 30 个脚本）、`equations` / `parameters` 的 `.mo` 45 条、
  内联在 `.mos` 里的模型 30 条（parameters 目录几乎都内联，所以收了，比 ADR D4 略宽，`origin` 字段可区分）。目标类取脚本里
  `simulate / buildModel` 的目标。脚本设的语义旗标只记入 `script_flags`，不进命令。
- 语料 b（1857 条）：nf_live_sweep 的全部 PASS，A1 由该记录作证。原文总量 272.5 MB（中位 19.5 KB，p99 1.15 MB，最大 24.6 MB），只留签名。
- 语料文件取自 testsuite 工作树；NBackend 三目录是 15 / 10 / 5 个脚本，tag 上是 16 / 10 / 4。只影响借用的模型，参考都是实跑。

## 2 准入表

**A3 判据（`.mo` + `-s` 路线）**：exit 0 且 stderr 无 `Error` / `Internal error`。`Main.mo:444` 丢弃 `translateModel` 的 success；
`SimCodeMain.mo:1218` 只有 `success := true`，失败都走 fail()，落到 `Main.mo:484`，exit 1。只看 exit 0 不够，有 exit 0 但带 Error 的模型。

| | a（109） | b（1857） |
|---|---|---|
| A2（ADR 原文）拒 | 15 | 558（其中 278 个同时 A3 失败） |
| A3 拒：超时 / Error / 其它 | 0 / 26 / 4 | 1 / 404 / 24 |
| 准入 | 64，加 A1 后 62 | 870 |
| A2' 拒 | 19 | 731 |
| A3 拒：超时 / Error / 其它（A2' 口径） | 0 / 25 / 4 | 1 / 333 / 23 |
| **准入（A2'）** | 61，加 A1 后 **59** | **769** |
| A1 不满足 | 13（omc 拒 8，ymc SKIP 5） | 0 |

语料 b 的 A3 Error 前几名：`NBPartitioning.partitioningClocked` 196、`NFConvertDAE.convertFunction got unknown function` 48、
`NBAlias.findSimpleEquation` 47、`NBResolveSingularities.noIndexReduction` 38、`NBInitialization.main` 10。另有 omc 崩溃（0xC0000005）9 个、超时 1 个（`VTT/FreeBody18`）。

**不要求 A3 时各段参考是否在手**（A2'，含 A1）：

| | 有 `(After Lowering)` | 有 `(Events)` | 有第一个 Causalize | A3 过 |
|---|---|---|---|---|
| a | 74 | 69 | 47 | 59 |
| b | 1086 | 1028 | 351 | 769 |

**pre 模块非恒等模型数**（text = 段体逐字不同 / sig = 5a 签名不同）：

| 模块 | a（59） | b（769） |
|---|---|---|
| Bindings | 2 / 2 | 218 / 218 |
| FunctionAlias | 16 / 16（S5a-2b 实测非恒等 PASS 22） | 101 / 101（实测 105） |
| Early Inline | 6 / 0（S5a-2c 实测非恒等 PASS 6） | 52 / 1（实测 56） |
| Simplify 1 | 18 / 0（S5a-2d 实测非恒等 PASS 24） | 88 / 3（实测 87，另 1 个 omc 无段） |
| Alias | 26 / 26（S5a-2e 实测非恒等 PASS 30） | 398 / 397（实测 477） |
| Simplify 2 | 0 / 0 | 58 / 1 |
| Remove Stream | 0 / 0 | 0 / 0 |
| Detect States | 33 / 33 | 174 / 174 |
| Events | 28 / 28 | 148 / 148 |

**累计准入**（Simplify 1 / 2 同一函数算一个单元；最优链 = 穷举实现序取累计面积最大，守 Simplify < Alias、Detect States < Events，这两条是按 NB 运行序取的）：

| 口径 | lower only | 之后逐步 |
|---|---|---|
| a text 最优链 | 9 | Detect States 12，Events 20，Simplify 28，Alias 37，FunctionAlias 52，Early Inline 57，Bindings 59 |
| a text 按 NB 运行序 | 9 | Bindings 9，FA 10，EI 10，Simplify 11，Alias 23，DS 31，Events 59 |
| b text 最优链 | 227 | Simplify 231，Alias 327，Bindings 474，DS 531，Events 620，FA 717，EI 769 |

b 的 lower only 227 里有 202 个是 0 未知量 + 0 方程的纯参数模型。b 被 flattening 测试主导，a 才是动态模型，排序以 a 为主。

**`(After Lowering)` 直方图**（模型数 / 出现次数）：

| 项 | a（59） | b（769） |
|---|---|---|
| `[ALGB]` | 41 / 102 | 486 / 3132 |
| `[PRMT]` | 32 / 105 | 217 / 1315 |
| `[DISC]` | 21 / 68 | 151 / 346 |
| `[CNST]` | 4 / 5 | 121 / 291 |
| `[CLCK]` | 0 | 15 / 43 |
| `[EXTO]` | 0 | 5 / 6 |
| `[SCAL]` | 44 / 127 | 342 / 2807 |
| `[WHEN]` | 21 / 45 | 93 / 224 |
| `[ALGO]` | 3 / 3 | 53 / 61 |
| `[-IF-]` | 4 / 5 | 11 / 12 |

方程种类（b）：纯 `[SCAL]` 249，无方程 364，`[SCAL]+[WHEN]` 68，`[ALGO]` 相关 53，纯 `[WHEN]` 23，`[-IF-]` 相关 11，其它 1。
5b 语料偏薄：b 的 769 个里有 5b 段的 342 个，其中有状态 132 个、有代数环 54 个；a 的 61 个是 44 / 22 / 5。

## 3 F3 实测

在 741 个模型上机械比对（A2' + A3 的 769 个去掉 28 个无 `class` 文本的）。原型 `proto\nbexpr.py`、`f3pred.py` 解析 ymc 压平文本，照 OMC 实现
combineBinaries 与 `Expression.toString`，拼出整段 `(After Lowering)` 与实跑逐字节比。

- 标量方程文本 2570 条中 2567 条相同（1 条真差异 `z = {} * {}`，2 条是原型语法缺口）；变量绑定 1924 条中 1922 条相同。
- 变量序：每个桶都是压平序，0 例外。方程序：`$RES_SIM_n` 按 n 降序就是源序。
- 整段：615 逐字节相同，94 个规范化后相同（只抹下表 #4 的前缀、类型名和 #12 的打印细节），24 个不同，8 个原型解析失败。
- 这 32 个里真的前端路径差异只有 2 个：`Scalar.mo`（默认 `3.0`，NB 留 `Scalar.f({1.0 for $f1 in 1:3})`）、`ArrayVectorVectorMul4.mo`（默认 `0.0`，NB 留 `{} * {}`）。
  其余 30 个判断是原型对 when 拆分、NoRetCall 尺寸、String 类型、转义的近似不够；24 个不同的条目看过（`redeclare6.mo` 除外），8 个解析失败只看了前 3 条。

| # | 差异 | 规则 | 来源 |
|---|---|---|---|
| 1 | `a + b + c` → `b + a + c`；`a - b + c` → `(a + c) - b`；`(a + p) * q / 2` → `(q * (a + p)) / 2` | combineBinaries 是纯函数。`+ *` 链最后一项不动、前缀反序。只有 ADD/SUB 互相合并，MUL 与 DIV 不合并。`^` 不下探。String 的 `+` 不可交换 | `NFInst.mo:249`；`NFSimplifyModel.mo:644`；`NFSimplifyExp.mo:1481`、`:1573`、`:1628`、`:1781-1786`；`NFOperator.mo:879`、`:956` |
| 2 | 同一表达式在方程和绑定里形状不同：`sqrt(a * b)` 绑定变 `sqrt(b * a)`，方程不变 | 方程浅映射（每个顶层式调一次）；绑定和属性深映射，自底向上每节点调一次 | `NFEquation.mo:707`；`NFVariable.mo:490`；`NFBinding.mo:573-586` |
| 3 | 打印版式：cast 不打印，MULTARY 打括号 | `Expression.toString` 版式与 DAEDump 不同，需新打印器 | `Flags.mo:506`、`CevalScriptBackend.mo:3611`、`NFExpression.mo:1967`、`:1936-1947` |
| 4 | 变量声明行 | 打 `redeclare final inner outer flow`，打类型全名 `enumeration M.Enum(e1, e2)`。Integer / Boolean 上的 `discrete` 不打。`final` 打的是 `Attributes.isFinal`（S5a-1a 修正）：声明或修饰的，加上压平之前被 `Structural.markComponent` 标过的（它同时 setFinal，`NFStructural.mo:318-319`）；别名连带只改变率（`NFTyping.mo:1150-1155`），压平之后只由 `Flatten.updateVariability` 补变率（`NFFlatten.mo:3101-3113`），都不打。属性只打 fixed、start、min、max、nominal、StateSelect、TearingSelect。parameter 和外部对象补 `fixed = true` | `NFAttributes.mo` toString；`NFBackendExtension.mo:396-420`、`:1003`；`NBackendDAE.mo:774`。`src/flat/flat.h:47` 这些前缀不存 |
| 5 | 变量种类 | when 里赋值的 Real 是 `[DISC]`（S5a-1b 补充：`updateImplicitVariability` 在实例化时走全类树，`NFInst.mo:193`、`:3847-4097`，所以结构 if 没选中的分支、移除的条件组件、零长度组件数组里的 when 也算；打印不带 `discrete`，stateSelect = always 和顶层 input 也变 `[DISC]`）。顶层 input 优先于种类进 Top Level Input。Clock → `[CLCK]`，外部对象 → `[EXTO]` | `NBackendDAE.mo:549-555`、`:700-777`；`NFVariable.mo:316` |
| 6 | 绑定不下沉 | `(After Lowering)` 里绑定还在变量上，Bindings 模块才造方程。方程的 rhs 是再化简一遍的式子，变量行不变（S5a-2a 补充） | `NBBindings.mo:68-81`；`NBEquation.mo:2226` |
| 7 | 方程编号 | `$RES_SIM_n` 对头插列表逐个编号，最后一条是 0。if 方程外层先、内层前序。`$RES_BND / _AUX / _EVT` 共用 uniqueIndex 继续往上 | `NBackendDAE.mo:836`；`NBEquation.mo:1769`、`:2771`；`NBBindings.mo:68-81` |
| 8 | 方程入桶 | 看类型不看变量：Real 输出的 algorithm 进 Continuous，即使输出变量是 `[DISC]`。输出压平时就定了，含被化简删掉的语句（S5a-1c，`NFFlatten.mo:387`） | `NBackendDAE.mo:1290`；`NFComponentRef.mo:2173`；`NBEquation.mo:4661` |
| 9 | assert | 变成 `[WHEN] (0)`，进 Removed，打默认 `AssertionLevel.error`。initial 段的进 Initial 桶；when 体里的 assert、if、for 和顶层 terminate 在 lower 报内部错（S5a-1b 实跑，`NBackendDAE.mo:1284-1286`、`:1003-1005`）；when 里的 terminate、无返回值调用被 `split` 丢弃（`NBEquation.mo:3241-3254`） | `NBackendDAE.mo:1167` |
| 10 | when 拆分 | 一个 when 赋多个变量时拆成多条。多赋值走 `UnorderedMap.keyList`，是插入序，打印回到源序；只有多个 reinit 走 `Set.toList` 的桶序（S5b-0 更正，原文写的"走哈希"不准确）。741 个里 19 个涉及，2 个与源序不同，这 2 个没有定位 | `NBEquation.mo:3151`、`:3173-3174`；`UnorderedMap.mo:429-433`；打印反序 `NBEquation.mo:4661` |
| 11 | 人工变量 | `$ANY$`（`[DMMY] (0)`）、`time` 恒在 Artificial 桶，计数 `(2/1)`。绑定里的迭代量（`$i1`）、算法里用到的 for 迭代量（`[ITER]`，桶序，S5a-1c，`NBackendDAE.mo:1393-1427`、`:1476-1494`）追加到这里 | `NBackendDAE.mo:533-537`、`:649-654` |
| 12 | 打印怪癖 | 块语句后空行、`while … then`、`elseif true then`、`#UNKNOWN STATEMENT#`（break） | 实跑 |
| 13 | 路径差异 | NB 路径不展开、不对数组实参的调用求值（`NF_EXPAND_OPERATIONS` / 标量化关闭） | `NFInst.mo:324-333`；`NFSimplifyExp.mo:189-198`、`:941`、`:1135` |
| 14 | 顶层无返回值调用 | 包成一条语句的 `[ALGO] (0)`，EMPTY 进 Removed，初始方程里的进 Initial（S5a-1c 实跑） | `NBackendDAE.mo:995-999` |
| 15 | 空分支 | if 语句分支被化简成空：默认路径的标量化删掉，NB 路径留着。带 else 的 if 方程全空：默认路径整条删，NB 路径 lower 报内部错，没有这一段（S5a-1c 实跑） | `NFScalarize.mo:461-488`；`NFSimplifyModel.mo:517-522` |

主 agent 另用探针 `Chk1`（一个数组常量 + 标量方程）独立复核了 #1、#2、#11 与"数组常量时 N == n 仍成立"：方程 `y = b + a + s`、绑定 `sqrt(b * a)`、方程里 `sqrt(a * b)`、`5 (5)`。

## 4 对 ADR-008 的修正

修订正文见 ADR-008 §6。依据：`VarData.size` 只数 unknowns，所以 A2 误放 133 个（数组只在 Constant 42、只在 Parameter 39、Algebraic + Parameter 42、其它 10）；
误挡 33 个全有多输出 `[ALGO]`（如 `equations/WatchDog1AlgorithmWhen` 的 `8 (7)`），其中 32 个 A3 能过。记录：探针 R1 / R2 / R3 / R5 都 N == n 但有 Record 桶或 `[RECD]`；
census 里 112 个有 Record 桶的模型 A3 全部失败，靠 A3 才挡住，A2 挡不住。

## 5 未决

- A3 过不代表结构对：探针 Sing（`der(x) = y; der(y) = -x; x + y = time`，`3 (3)` / `2 (2)`）exit 0，Alias 消掉 x 后留下 2 条方程对 `$DER.y`，NB 没报错。建议加平衡检查（769 里 2 个不等）。
- 79 个模型 NB 打完统计后才失败（推测在 SimCode 阶段，未查）。
- ~~`redeclare6.mo` 的 `redeclare Real x = 3.0` 为什么是 `[DISC]`~~ 已答（S5a-1a）：redeclare 不写变率就留原声明的（`mergeRedeclaredComponentAttributes`，`NFAttributes.mo:322-328`）。
- A4 的 identity 比打印文本，dump 看不到的字段判不出来。
- Alias 的逐字节可达性未测。原型基于压平文本，树形信息有损，换成内存 Expr 后还没在 C++ 里验证。标量 Simplify 已在 C++ 里对上 omc（S5a-2a，只有绑定走的路径）。
- D3 回读：`a + b + c` 的文本分不清 MULTARY 和嵌套 BINARY，后续 pass 可能依赖树形，这是读代码的推断，没有实测。
