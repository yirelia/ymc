# 待验证清单（活文档）

方案 §3.6 / §8 与需求规格 §10 TBD 的合并。每条：问题 / 验证方法 / 影响 / 状态。
研究底稿在 scratchpad `R1..R7-*.md`（结论已并入需求规格与本仓库工具）。

## 需用户拍板

| ID | 问题 | 下一步 | 状态 |
|---|---|---|---|
| TBD-05 | ymc loadModel 是否复刻 OMC 版本回退映射（{"3.2.1"}→3.2.3+maint、{"2.2.2"}→4.0.0+maint，R7§5 实证） | 用户决定；先只支持 4.0.0，其余标 skip:msl-version | 待定 |
| TBD-08 | 各阶段 G1/G2 通过率阈值、S7 退出阈值 | 用户定；stages.json 现留 null | 待定 |
| TBD-11 | Dymola 语料优先级 | 默认 C-MSL-EX→C-MAPLIB→提取 .mo | 默认执行 |
| TBD-12 | erroneous 错误文本对齐深度 | 默认位置一致 Must、文本一致 Should | 默认执行 |
| TBD-10 | 里程碑时间估算 | 用户告知每周投入 | 待定 |

## 需探针/实跑验证

| ID | 问题 | 验证方法 | 影响 |
|---|---|---|---|
| TBD-02 | Dymola getLastError 统计文本格式（仅有 CoupledClutches 一例，R3§4） | 对若干 MSL 模型实跑 Dymola 取样，固化 parse_translation_stats 正则 | S3/S5 Dymola 结构对比 |
| TBD-03 | Dymola `Advanced.*`（EliminateAliasVariables/OutputModelicaCode）是否存在 | 在 Dymola 里 `list(variables={"Advanced.*"})` | S3/S5 |
| TBD-07 | dsmodel.mof 能否经 Dymola Python 接口取得（文档未暴露，R3§7） | runTestCases(generateReferenceTranslationStructure=True) 或读 User Manual 2C | S3 结构对比来源 |
| TBD-09 | 1.25.0 libSimulationRuntimeC 头/ABI 是否够（方案 §8 #7） | 用 OMC 生成的 Demo.c 手工 clang 链接一次 | S6 |
| TBD-13 | 工作区 omc 与安装版在比对/读取源码是否一致（R1§未找到1） | 需要时 diff 两版对应 .c | S6/S7 |
| TBD-14 | MAP-LIB v4.0.0 是否含 .mat（抽样仅见 .csv，R5） | 克隆 v4.0.0 分支核实 | S6 |
| — | omc 版本回退映射函数位置（R7§未找到3） | grep OMC 源码 conversion(noneFromVersion/from) | TBD-05 |
| — | Blocks/Tables.mo 去 Include= 后 OMC 如何找头文件（R7§未找到） | 读 OMC codegen | 外部函数 S3e/S6 |
| — | R4：$cse/$DER 默认可见性、name 数组下标逐字节、CSV 换行 \r\n？ | 实跑 dump 一份 .mat 探针 | resultio 精度 |
| — | `integer(Real)` 在 NFCeval.mo:2476 调 `realInt`，但 omc 1.25.0 实跑 `integer(-9.9)=-10`（floor） | 查 1.25.0 `realInt` 运行时实现及负数约定；当前按 oracle 实测 floor | S3 标量内建常量折叠 |
| — | `Integer(E.two)` 需枚举字面量 1 基序数；当前 Expr::EnumLit 只有路径、无 ordinal | 枚举 typing 桶给 EnumLit 补 ordinal 后实现 NFCeval.mo:2481-2487 | S3 枚举求值；当前 SKIP `enum ordinal` |
| — | 任务卡把 `delay`/`noEvent` 列为恒符号，但 testsuite `FuncBuiltinDelay.mo` 会补第三参并化简零延迟，`CevalNoEvent1.mo` 会去包装 | 后续按两个测试文件补专用规范化语义；当前继续诚实 SKIP，避免把既有 SKIP 变真 FAIL | S3 内建函数加宽 |

## Oracle 分歧账本（testsuite `// Result:` vs 安装版 omc 1.25.0）

testsuite 的 `// Result:` 由某版 OMC 生成，与项目 oracle（安装版 omc 1.25.0）行为不一致时记此。
ymc 以 **omc 1.25.0 实测**为准；这些用例从 `docs/research/s3-easy-slice.json`（文件 Result 对拍）剔除，避免误记 FAIL。

| 用例 | 文件 `// Result:` | omc 1.25.0 实测 | 结论 |
|---|---|---|---|
| flattening/.../FinalParameter1.mo | `Real y = 3.0;`（final 参数内联） | `Real y = x;`（不内联） | 同下：1.25.0 不内联 final parameter，ymc 与 1.25.0 一致。 |
| flattening/.../FinalParameter2.mo | `Real y = 3.0;`（final 参数内联） | `Real y = a.x;`（不内联） | 1.25.0 默认 DAE dump 不内联 final parameter；ymc 与 1.25.0 逐字一致。较新 OMC 才内联。 |
| flattening/.../FinalParameter3.mo | `Real y = 3.0;` | `Real y = a.x;` | 同上。 |
| flattening/.../OperationRelational1.mo | class 体 + 2 行 `Warning: … == on Real operands is deprecated …` | 仅 class 体（告警走 stderr） | flat 模型逐字一致；ymc 尚无诊断/告警（S 级诊断另做）。文件 Result 含 omc 告警文本。 |
| flattening/.../InnerOuter13.mo（反向） | `class InnerOuter13 Real x = 1.0;`（成功） | **报错退出**（1.25.0 更严） | 罕见反向：1.25.0 拒绝、文件+较新 OMC 接受。ymc 与文件/较新 OMC 一致（嵌套 outer 连接到 inner），sweep 按文件对拍 PASS。 |
| flattening/.../IfExpression17.mo | `final parameter … dynamic = false;` + if 化简成 `m.x = m.u;` | `parameter … dynamic=false;` + 保留 `(if m.dynamic then der(m.x) else 0.0) + m.x = m.u;` | 较新 OMC 把常量参数求值成 final 并按 dynamic=false 化简 if；1.25.0 不做。ymc 与 1.25.0 逐字一致（if 分支类型统一 `0`→`0.0` 已修）。 |
| flattening/.../Connect3.mo | `class Connect3 … (-c1.f) + (-c2.f) = 0.0; …`（连接器别名 `class C2 = C` 成功） | **报错退出**：`c1 is not a valid connector`（短类别名当连接器，1.25.0 拒绝） | ymc 也 SKIP（`short class aliasing composite`），与 1.25.0 一致（都不产出）；文件+较新 OMC 才接受。故 Connect3 不进 easy-slice。 |
| flattening/.../InvalidConnectorDirection1.mo（status: incorrect） | `Error: Cannot connect input component c1 to non-input component c2.` | **接受**：`class … input Real c1; Real c2; equation c1 = c2;`（+ stderr 告警连接器不平衡） | 该错误（`Error.CONNECT_PREFIX_MISMATCH`）只在**旧前端** `FrontEnd/InstSection.mo:4012` 抛，新前端无此检查（NFTyping.typeConnect/checkConnector :3110-3213 只查连接器形态/类型/下标变率）。ymc 与 1.25.0 逐字节一致；sweep 按文件记 FAIL，属账本内分歧（2026-09-27 起 FAIL 基线 69→71）。 |
| flattening/.../InvalidConnectorDirection2.mo（status: incorrect） | `Error: Cannot connect input component c2 to non-input component c1.` | **接受**：`class … Real c1; input Real c2; equation c1 = c2;` | 同上。 |
| flattening/.../InvalidConnectorDirection3.mo（status: incorrect） | `Error: Cannot connect input component c2.y to non-input component c1.y.` | **接受**：`input Real c1.x; Real c1.y; … input Real c2.y; … c1.x = c2.x; c1.y = c2.y; -(c1.f + c2.f) = 0.0; …` | 同上（旧前端 InstSection.mo:4012 才查方向匹配）。ymc 与 1.25.0 逐字节一致（顶层连接器成员保留方向 + 成员按名配对）；FAIL 基线 71→73。 |
| flattening/.../InvalidConnectorDirection4.mo（status: incorrect） | `Error: Cannot connect input component c1.x to non-input component c2.x.` | **接受**（同上，成员声明序不同） | 同上。 |

## 已研究待实现：多条 connect 连接集（union-find）

**状态**：算法已从 OMC 新前端源码 + 实跑 omc 1.25.0 探针完整摸清，暂不实现——理由是当前 testsuite 里
每个多条-connect 标量用例都被另一未做特性卡住（expandable / 条件组件 / stream / 数组连接器），做了 **+0 新 PASS**，
且只能拿合成探针对拍。真正需要它的是 **S3.g / MSL Examples** 里的真实多路连接。届时按下述算法忠实实现即可。
单条 connect（2 连接器）已在 `flatten.cpp:process_connects` 落地并逐字节验证；多条只差把"连接集数据结构"补全。

来源：`OMCompiler/Compiler/NFFrontEnd/NFConnectEquations.mo`、`NFConnectionSets.mo`、`NFConnector.mo`、
`NFConnections.mo`、`Util/DisjointSets.mo`、`Util/UnorderedMap.mo`（新前端）。实跑校核：Connect2/Connect5/
ConnectDiffOrder1/BothInside/ThreeWay（探针在 scratchpad）。

**数据结构**：`DisjointSets` = 并查集，元素 = **标量连接器变量**，键 = `(flat cref, face)`，face ∈ {INSIDE, OUTSIDE}。
`UnorderedMap` 的 `toArray` 按**插入序**返回键（非哈希序，可复刻，UnorderedMap.mo:494-510）。

**建集 `NFConnectionSets.fromConnections`（NFConnectionSets.mo:77-82）**：
1. **seed（`collectFlows`, NFConnections.mo:151-156）**：把**每个 flow 变量**当 **INSIDE** 单元素集加入，`addFlow`
   **前插**（line 95）→ seed 列表 = **声明序的反序**。（受 `Flags.DISABLE_SINGLE_FLOW_EQ` 门控，默认开。）
2. **merge**：按源码 connect 顺序，每条 `connect(a,b)` 展开成"每个同名成员变量"的连接对；对每对，两端各按
   `(cref, face)` `findSet`（不存在则**追加插入**新元素）后 union。
   - **face（`NFConnector.crefFace`, NFConnector.mo:312-326）**：简单标识符 `c` → OUTSIDE；否则 `firstNonScope(cref)`
     是连接器 → OUTSIDE，是非连接器组件（`m.c`）→ INSIDE。
   - INSIDE 端命中已 seed 的 `(cref, INSIDE)` → **复用** seed 元素；OUTSIDE 端的 `(cref, OUTSIDE)` 与 seed 的
     `(cref, INSIDE)` **键不同** → **新插**元素，原 INSIDE seed 存活成单元素集 → 该 flow 出 `f = 0`（这正是
     "outside/未连接连接器的 flow=0"的真正来源，`Connector.isEqual` 比较 name **且** face，NFConnector.mo:146-147）。

**取集 `DisjointSets.extractSets`（DisjointSets.mo:326-339）**：对 `toArray`（插入序）**反向**遍历，每个元素**前插**到其
根所属集合的列表 → 集合内元素序 = 由此涌现（非声明序、非根序）。

**出方程 `NFConnectEquations`**：
- `generateEquations`（NFConnectEquations.mo:110-127）按**集合创建序**（根首次出现序，DisjointSets.mo:313-318）逐集处理，
  每集方程 `listAppend(set_eql, equations)` **前插**进累加器；整块再 `listAppend(conn_eql, flatModel.equations)`
  **前插**到模型方程（NFFlatten.mo:2497）——**没有**"potential 全前、flow 全后"的硬编码相位，观测到的顺序是
  上述创建序 + 两次前插的涌现结果，**复刻它要按创建序模拟、别按 kind 排序**。
- **potential（`generatePotentialEquations`, 213-217）**：`c1 = listHead(elements)`，`c1.v = c2.v for c2 in rest`——
  **以集合表头为参照星形展开**（非相邻链），2 连接器即 `a.v = b.v`。
- **flow 求和（`makeFlowExp`, 406-421 / `generateFlowEquations`, 364-404）**：按集合元素序左折叠 `sum := makeFlowExp(e0)`，
  `for e in rest: sum := BINARY(sum, Add, makeFlowExp(e))`；INSIDE 取裸 cref、OUTSIDE 取 `UNARY(-, cref)`（418-420）。
  SimplifyExp 后：全 OUTSIDE→`-(a+b+…)`、全 INSIDE→`a+b+…`、混合→`inside − outside`。单元素集（存活 seed）→ `f = 0`（386-387,397）。

**实测涌现结果（用来对拍模拟）**：
- `connect(c1,c2)` 都 outside：集元素序 `[c1,c2]`，`c1.e=c2.e; -(c1.f+c2.f)=0; c1.f=0; c2.f=0`。
- `connect(a1.c,a2.c)` 都 inside：集元素序 `[a2.c,a1.c]`（**反序**，源自 seed 前插），`a1.c.e=a2.c.e; a2.c.f+a1.c.f=0`（无 f=0）。
- `connect(a.c1,c2)` inside+outside：`a.c1.e=c2.e; a.c1.f−c2.f=0; c2.f=0`（inside seed 被并、outside seed 存活）。
- `connect(c1,c2);connect(c2,c3)` 都 outside（ThreeWay 探针）：集元素序 `[c2,c3,c1]`，出
  `c2.e=c3.e; c2.e=c1.e; -(c2.f+c3.f+c1.f)=0; c1.f=0; c2.f=0; c3.f=0`——3 路的元素序 `[c2,c3,c1]` 必须靠忠实模拟
  seed 反序 + merge 插入 + extract 反序才能复刻，**不能臆想**。

## 已研究待实现：数组 connect（sub-bucket 3，暂缓）

数组进阶已做 spread（BindingArray8）+ 复合数组标量化/修饰分发（phase 2a/2b，+8）。**数组 connect** 是硬尾巴——
非单桶，而是一簇特性叠加：(1) 连接器数组 `C c[n]` 标量化 + 逐元素 flow=0；(2) for 循环方程展开
`for i loop connect(a[i],b[i]) end for`；(3) 切片/整体 connect `connect(a[1:n-1], a[2:n])` / `connect(x, y[2:p+1])`
按元素配对展开；(4) 多条 connect 连接集（**依赖已延后的 union-find**，见上节；ArrayConnect1 的 for-connect 成多路集）。
且多数用例还带 `-nfScalarize` 注解或大维（omc 本就不标量化）。部分做（如仅连接器数组标量化）payoff ≈ 0（testsuite
无独立无-connect 连接器数组用例）。故整簇暂缓，待 union-find + for 循环 + 切片下标就绪后一并做。
连接器数组标量化的最小目标（探针实测）：`C c[2]` → `c[1].e c[1].f c[2].e c[2].f` + `c[i].f = 0.0`。

## 已解决（并入工具/规格）

- omc-diff 模糊比对规则（方案 §8 #1）→ `tests/omc/omc_diff.py`，对拍 omc-diff.exe 一致。
- ModelTesting 框架（方案 §8 #3）→ R2§E 已解析（relTol=0.01 等）。
- diffSimulationResults tube 算法 → `tools/matcompare.py`，对拍 OMC 一致；默认容差由 rangeDelta 主导。
- .mat v4 布局 → `tools/resultio.py`，val() 与 OMC 逐位一致。
- Dymola 无窗口接口 → `tools/dymola_driver.py`（R3）。
- MSL 4.0.0 三方语料：OMC 与 Dymola 各 2508 .mo，70 个内容不同（R7§3，需列 known-nonequiv 清单）；ModelicaServices 整包排除。
