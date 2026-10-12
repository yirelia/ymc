# ADR-007 类重声明（redeclare class / package / function）与压平期的 SCode 词法查找

- 日期：2026-10-10（提议）；2026-10-12（第 2 阶段裁定：**A 实例树查找**，用户拍板）
- 状态：已决定（第 1 阶段已落地：63o / 63p / 63q；第 2 阶段 = A，按下文"第 2 阶段迁移计划"分步做）
- 依据：`docs/research/R15`（§6.2 redeclareClass，`NFInst.mo:1712-1803` v1.25.0；§1 redeclare 元素的父作用域 = 写修饰处）、
  `R12`（NF 查找走实例树）、ADR-006 D2 / D4。

## 背景
MSL 的 Fluid / Media（`redeclare package Medium = …`，约 40 个示例）、MultiBody（`redeclare function surfaceCharacteristic /
gravityAcceleration`、`redeclare model Cylinder`，5 个）与 testsuite 里 omc 1.25.0 能过的类重声明 / 嵌套类修饰 / class extends
（约 58 个）都卡在类重声明上。OMC 的 `redeclareClass` 在实例树里用新类节点顶替原类节点（`REDECLARED_CLASS`），名字查找
（NFLookup）走实例树，所以继承来的基类代码里对被替换类的引用也看到新类。

ymc 现状：inst 有组件重声明（`redeclare_component`，合成 SElement），没有类重声明。inst 自己的类型查找（`lookup_class_name`）
走实例树的 ClassTree；但 flatten 大量名字解析走 **SCode 词法作用域链**——`scode_scope_of` 把实例节点的 `parent_scope` 链映射成
类 SElement 列表，`find_scode_element_at` 扫各层的静态元素表（函数 `resolve_fn`、记录类 `record_class_of`、包常量 / 类型
`Medium.nX`、`eval_scode` …，上百处）。这条路看不到实例上的类替换；基类代码的作用域链（[基类, 基类所在包…]）甚至不经过做
重声明的实例。

## 决定（分两阶段）
**第 1 阶段（照 ADR-006 D4，不另立决定）**：inst 实现类重声明——组件修饰 / extends 修饰 / 元素形态的 `redeclare model|block|
record|package|function|type B = …`、`redeclare … extends`，在实例的 ClassTree 里顶替原类节点（新节点的词法父作用域 = 写修饰处，
R15 §1），组件类型经 inst 的实例树查找自然拿到新类。flatten 的 SCode 词法查找**撞到被重声明过的 replaceable 类元素就 SKIP**
（不静默用原类），于是只靠组件类型生效的模型（MultiBody Cylinder、多数 testsuite 用例）先过。

**第 2 阶段（待裁定）**：让 flatten 的 SCode 查找认实例上的类替换（函数调用 / 包常量 / 类型经 `Medium.` 解析）。备选：
- **A 实例树查找**：flatten 的查找点逐步改走实例节点（同 OMC）。最贴近 OMC，改动面最大、风险最高。
- **B SCode 特化**：重声明作用到实例时合成该实例类的 SCode 副本（被替换元素换掉；被替换的是继承元素就沿继承链复制声明它的
  基类与中间各层，extends 元素加"解析目标覆盖"；重声明元素加"词法作用域覆盖"）。flatten 不动；代价是合成元素量（按
  (类, 重声明集) 记忆化）与两类覆盖要处处被认。
- **C 作用域链叠加表**：`ScodeScope` 带一张由实例链收集的叠加表（(声明类, 名) → 替换元素 + 其作用域），`find_scode_element_at`
  每层先查表。不合成元素；但所有派生作用域都要正确携带叠加表。
B / C 的共同风险是"漏认 = 静默错"，须配专门的探针族（被替换类在基类代码里被引用、跨层传递 `redeclare package Medium = Medium`、
函数 / 常量 / 类型三种用法）。建议：第 1 阶段做完、以其探针为基础对 B / C 各做一次小原型再定。

## 补充（63k，2026-10-10）：同一个缺口不止类重声明
继承来的元素进查找表（inst 的 expand + flatten 的 SCode 查找）落地时实测出两类同源问题——都是"SCode 词法链 ≠ OMC 的实例链"：
- **访问路径命名**：OMC 里继承元素的父亲是 extends 实例，scopeList 把 BASE_CLASS 换成派生类，所以经 `P` 访问的继承函数 /
  枚举叫 `P.f`、`P.E.b`，连函数体 / 常量绑定里再查到的同类也一样（IL4 / IL19）；ymc 的定义作用域链只记声明处 `Base`。
- **外层实例上的修饰**：嵌套类、模型内函数读外层常量时 OMC 读实例上改过的值（`A a(n = 3)`：IL5 `x[1..3]`、IL9 `3.0 * x`、
  IL10 `6.0`）；ymc 读 SCode 声明值——**此前就静默错**（x[1] / `y := x` / 2.0），63k 起 SKIP；`extends Base(n = 3)` 继承来的
  嵌套类同理（IL7）。
63k 只做守卫（经继承访问过的基类不再照词法链拼名、外层实例改过的常量、经改了常量的 extends 继承的类 → SKIP）。第 2 阶段的
A / B / C 都得同时解决这三件事（类替换、访问路径命名、外层实例修饰），选型时一并评估。

## 第 2 阶段裁定（2026-10-12）：A 实例树查找
理由：上面三件事（类替换、访问路径命名、外层实例修饰）同根——都是"SCode 词法链 ≠ OMC 的实例链"。A 从根上照 OMC 走实例链，
三件事一并解决；B / C 只治类替换，且要在约 280 个查找点（2026-10-12 计：`eval_scode` 103、`scode_scope_of` 97、
`resolve_class_path` 35、`resolve_fn` 17、`find_scode_element_at` 13、`record_class_of` 12 …）上维持一个"漏认 = 静默错"的新不变量。
A 改动面最大，风险由逐字节门禁（testsuite 1787 / MSL 438 / 探针 9612，与实跑 omc 1.25.0 逐字节一致）兜底，并且分步迁移、每步单独
过门提交。

### 第 2 阶段迁移计划（每步：OMC v1.25.0 源码对照 + 探针 + 门禁 + 单独提交）
1. **基础**：
   - inst 侧：查找进入类时按需实例化包 / 类（`NFLookup.lookupLocalName` :679-742 进类前调 `Inst.instPackage`，NFInst.mo:1312），
     重声明过的类节点留在实例树里可被查到。
   - flatten 侧：`lookup_inst(NodeId scope, path)` 照 `NFLookup.lookupName` :580 / `lookupFirstIdent` :660 /
     `lookupSimpleName` :434（本地 ClassTree → 词法父实例链，encapsulated 截断后只认内建，`lookupLocalName` 逐段进类）。
   - **双跑期**：迁移中的查找点新旧两条路都跑，结果不一致且不属已知的类替换情形 → SKIP 并计数（把潜在静默错变成可见的 SKIP）。
2. **函数解析**（`resolve_fn` 等）：经实例链得到函数节点与打印路径（`P.f` 而非 `Base.f`，IL4 / IL19）；重声明的函数
   （MultiBody `gravityAcceleration` / `surfaceCharacteristic`）。
3. **包常量 / 类型**（`resolve_qual_const` / `resolve_class_path` / `eval_scode` 的限定 cref）：`Medium.nX`、`Medium.ThermodynamicState`
   → MSL Fluid / Media（约 44 个示例）。
4. **记录类与其余查找点**（`record_class_of` …）。
5. **外层实例上的修饰**：嵌套类 / 模型内函数读外层常量时取实例上改过的值（IL5 / IL9 / IL10，`extends Base(n = 3)` 的 IL7）。
6. **拆守卫、退役**：63k 与第 1 阶段的 SKIP 守卫随各类迁移撤掉；全部迁完后 SCode 词法查找只留确有需要处（或删除），
   `scode_scope_of` 的调用点清零或说明理由。

## 后果
- 第 1 阶段：测试面立即可见（testsuite 的类重声明、MultiBody），不改 flatten 的查找模型；SKIP 守住静默错。
- 第 2 阶段（A）各步落地前，相应用法照旧 SKIP；Fluid / Media 在第 3 步后开始成批转 PASS。
- flatten 的 SCode 词法查找逐步退役；迁移期新旧查找并存，以双跑一致性检查护住。
