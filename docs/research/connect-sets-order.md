# connect 连接集的元素序 / 方程序（OMC 1.25.0 实测 + 源码锚点）

日期 2026-09-27。ymc 目前只做"不相交"多 connect（每个连接器最多出现一次）；本底稿把 OMC 连接集（并查集）的
可见顺序规则从源码推出来并用 12 个探针逐字节验证，供之后做共享端点（Connect2 传递、切片、cardinality、stream）时直接复刻。
**结论：不要自创并查集顺序，按下面的插入序/合并序/根序原样实现即可。**

## 流水线（NFFlatten.mo:2433 resolveConnections）

1. `Connections.collectConnections`（NFConnections.mo:105）：遍历 flatModel.equations，每条 CONNECT →
   `makeConnections`（:168）：`makeConnectors` 把每个实参展成元素列表（整体数组 → ExpandExp.expand + `Connector.fromExp`
   行主序，NFConnector.mo:110-116），左右**按位**配对，`CONNECTION(c1,c2) :: connections`（**cons → 语句序反转**）。
2. `Connections.collectFlows`（:139）：遍历 flatModel.variables（声明序），每个 flow 变量以 **INSIDE 面** cons 进
   `conns.flows`（**→ 声明序反转**）。
3. `Connections.split`（:237）→ `List.mapFlat(conns.connections, Connection.split)`。`Connector.split`（NFConnector.mo:238）
   → `splitImpl`（:328）：先 potentials、后 flows、再 streams（:344-346），数组成员保留（:363），末尾 reverse → 每个连接器
   `[pots…, flows…]`。`Connection.split`（NFConnection.mo:51）按位配对后 reverse → 正序。
   **关键**：`List.mapFlat` = `listReverse(mapFlatReverse(…))`（Util/List.mo:2308-2341，mapFlatReverse 逐块 `listAppend(f(e), out)`）
   → 块序保持、**块内反转** → 每条 connect 的合并序变成 `[flows…, pots…]`（反）；对单元素块（flows 列表）无影响。
4. `Connections.scalarize(conns, keepSingleConnectedArrays = not settings.scalarize)`（:252）：默认全摊，
   `Connector.scalarize`（NFConnector.mo:249）行主序（`ComponentRef.scalarizeAll` 反序后 cons 再正）。
5. `ConnectionSets.fromConnections`（NFConnectionSets.mo:67）：**先** fold flows `addSingleConnector`=`find`（逐个插入节点），
   **再** fold connections `addConnection`=`merge(lhs, rhs)`（`find` 缺则插入：lhs 先于 rhs）。
6. `DisjointSets`（Util/DisjointSets.mo）：节点表 `UnorderedMap`（插入序，`addEntry` Vector.push，UnorderedMap.mo:861）；
   `union`（:256）：秩为负数，`rank1 > rank2` → nodes[set2] := set1（set1 成根）；`rank1 < rank2` → nodes[set1] := set2；
   相等 → set1 秩 −1、set2 挂到 set1。（注释与代码相反，以代码为准。）
   `extractSets`（:295）：按**节点下标序**给根编集合号；遍历 entries 反序 + cons → 每个集合内元素 = **插入序**。
7. `ConnectEquations.generateEquations`（NFConnectEquations.mo:86）：按集合号遍历，`equations := listAppend(set_eql, equations)`
   （**前插 → 输出 = 集合号反序**）。potential 集：`head = 其余`（:213-217）；flow 集：`makeFlowExp` 逐元素相加（inside 取正、
   outside 取负，:406-421），单元素集（未连接 inside flow）直接 `f = 0.0`（:386-387，不取负）。
8. NFFlatten.mo:2497：`flatModel.equations := listAppend(conn_eql, flatModel.equations)`（连接方程在前，模型自身方程在后，
   ConnectArrays1 的 `a[1].e = 1.0` 排最后即此）。

## 可见规则（12 探针逐字节一致：ChainOut/StarOut/ChainIn/Mixed/ChainOutRev/InRevOperands/InFwdOperands/Star4/InOutOut/InInOutOut/OutInIn/InOut）

- **集合内元素序 = 节点插入序**：inside flow 先（声明序**反转**），再连接端点（合并遇到序：语句反序 → 元素反序 → 块内 [flows, pots] → lhs, rhs）。
  例：`connect(a.c, b.c)` → `b.c.f + a.c.f = 0.0`（flow 先插入且反序）；`c1.e = c2.e` 的左端 = 集合首元素。
- **集合输出序 = 最终根节点下标的反序**；根会随 union 迁移（ChainOut 的 potential 集根迁到最后合并的 c1.e 节点），
  所以共享端点时集合位置不等于书写序——必须按上面 union 规则算。
- 不相交时退化为：语句正序 → 元素正序 → potential 后 flow → 成员元素正序；再全部未连接/outside flow `=0` 按变量声明序。
  这正是 ymc 现有 process_connects 的输出序。
- **flow 求和打印**：集合元素恒为 `[inside…, outside…]`（inside 预插入）。有 inside：`in1 + … + ink - out1 - … - outm`；
  无 inside：`-(out1 + … + outm)`。（`x + (-y)`→`x - y`、`(-a)+(-b)`→`-(a+b)`、`-(a+b)+(-c)`→`-(a+b+c)`；
  NFSimplifyExp.mo:1020/1054/1108-1110 一带的取负折叠。）
- 顶层（outside）连接器的 flow 同时进求和集（OUTSIDE 面）与单元素集（INSIDE 面，`Connector.isEqual` 含 face，NFConnector.mo:143-148），
  故既有 `-(c1.f + c2.f) = 0` 又有 `c1.f = 0`；inside 连接器已连接则只在求和集。

## 2026-09-27 补充：两个已确认的潜在偏差（探针，当前无 testsuite PASS 用例触发）

1. **集合输出序 ≠ "连接方程在前、flow=0 在后"**。探针 OrderTrap（`C p; A a;`，A 含 `C c`，`connect(a.c, p)`）：
   omc `a.c.v = p.v; p.i = 0.0; a.c.i - p.i = 0.0;`，ymc 把 `p.i = 0.0` 放最后。原因：inside 端 a.c.i 是预插入节点
   （低下标）且成为合并集的根；outside 端 p 的 INSIDE 单元素集 p.i 因 p 声明在前、cons 反序而下标更高 → 集合号反序输出时
   排在合并集之前。Cardinality2 期望里 `p.i = 0.0` 夹在连接方程之间同理。**结论：必须按上面流水线忠实实现 DisjointSets
   的插入序/根序，"不相交简化"只是碰巧对。**
2. **连接方程排在模型自身方程之前**（NFFlatten.mo:2497 `listAppend(conn_eql, flatModel.equations)`）。探针 EqPlace
   （`x = 1; connect(c1, c2);`）：omc 先 4 条连接/flow 方程再 `x = 1.0`；ymc 相反（run() 末尾追加）。ConnectArrays1 /
   Cardinality2/3 的期望亦然；initial equation 段独立。
3. 子模型内的 connect：面按**书写类**本地判定（`crefFace` 用 `firstNonScope`，NFComponentRef.mo:429-440，前缀段是 SCOPE 来源），
   发射路径加前缀。Cardinality2 里 Resistor 内 `connect(p, q)` → `-(R1.p.i + R1.q.i) = 0.0`（both outside）。
