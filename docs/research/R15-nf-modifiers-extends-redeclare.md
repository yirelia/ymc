# R15 NF 修饰（Modifier）的表示、合并与传播；extends；redeclare；条件组件

> 研究对象：`C:\dev\OpenModelica`（HEAD 3ceb0c9d06）；oracle = 安装版 omc 1.25.0（`omc X.mo`，全部带 `timeout 120`）。
> 约定：锚点写作 `文件:行`（路径相对 `OMCompiler/Compiler/`，未写目录的 `NF*.mo` 均在 `NFFrontEnd/`；行号为 HEAD 3ceb0c9d06，引用前已 grep/Read 复核）；探针输出为 omc 1.25.0 实跑原文，探针文件在 `docs/research/probe-r15/`。用例名指 `testsuite/flattening/modelica/scodeinst/<名>.mo`。
> 版本差异：`NFModifier.mo` 在 v1.25.0 与 HEAD 之间只差一处（HEAD 的 `create` 多了 `guard not SCodeUtil.isBreakSubMod(m)`，即 MLS 3.7 的 `break` 修饰；`git show v1.25.0:…/NFModifier.mo | diff` 实测），其余函数逐字相同。
> 状态：**已完成**（§1–§8 + 结论摘要 + 探针 + 未确认 + 对 ymc 建议）。redeclare 探针 P60–P69 新增于 `probe-r15/`。

## 结论摘要

1. **修饰只有三个构造子**（`MODIFIER`/`REDECLARE`/`NOMOD`，`NFModifier.mo:148-168`）；子修饰装在**按名字排序的 AVL 树**里 → 遍历（报错、apply）**按字典序**而非书写序。修饰里表达式的作用域记在 **binding 自己**（`RAW_BINDING.scope`），表达式保持 Absyn 直到 `instBinding` 才在原作用域解析；`ModifierScope` 只是错误文案标签 + binding `Source` 选择，**不是查找作用域**。
2. **合并 `merge(outer, inner)` 恒产生新值**（值语义，无原地改）：`(MODIFIER,MODIFIER)` 时 binding 外层优先、子修饰同名递归合并/异名并集、name/final/each/info 全取外层；`final` 只查内层、只在此分支查。`each` 不参与合并，全部语义在**传播**时体现。同一修饰列表内重名在建表时即 `mergeLocal` 判定（一方无绑定才合法，否则 `DUPLICATE_MODIFICATIONS`）。
3. **没有全局修饰环境**：每层 `instClassDef`/`instComponentDef` 只把 `mod` 按名拆成子修饰、merge 进本层元素槽位，元素实例化时再把自己槽位的修饰传下一层。优先级链：`外层实例修饰 > 内层实例修饰 > 靠近派生类的 extends 修饰 > 远离的 extends > 短类链靠外 > 声明默认`（apply 顺序：先基类链由深到浅的 extends 修饰，后本层总修饰，"后 apply 者当 outer 赢"）。
4. **数组切分靠 binding 的 `SPLIT_PROXY` 下标**（不是 parents 列表）：修饰每穿过一个元素就 `propagate` 追加 `SPLIT_PROXY(origin,parent)`；typing 期展开成 `SPLIT_INDEX`，压平期按前缀替换成实际下标。`each` 子修饰整棵跳过传播 → 每个数组元素拿同一完整表达式。
5. **extends**：expand 期把继承名字并进查找树并给每个 extends 留 `REF_NODE` 占位（→ 继承元素在压平里的位置 = extends 书写位置）；重复继承元素**不报错**，只登记，靠 `checkIdentical`（只比类型 prefixes + binding 语法树，**不比**维度/属性修饰）判等价，不等价才 `DUPLICATE_ELEMENTS_NOT_IDENTICAL`。
6. **redeclare** 走 `redeclareClass`/`redeclareComponent`（`NFInst.mo:1712`/`2155`），修饰形态与元素形态两条路径汇流于此。换类型要求原元素 replaceable；绑定优先级 `outerMod > redeclare > 原组件`；属性合并只准降 variability、final 取 redeclare；条件沿用原组件。**跨层多个 redeclare 不叠加，最外层赢者通吃**（仅继承 constrainingMod），只有写在最初 replaceable 声明上的修饰（innerMod）能穿透。constrainedby 修饰无条件应用（哪怕无 redeclare），且在合并里垫底。
7. **条件组件**：Inst 期照常完整实例化并存条件（RAW_BINDING）；Typing 期判类型/可变性并试求值、false 时不给子组件 typing、绑定错误延迟成 `INVALID_BINDING`；**Flatten 期才真删**（带前缀逐数组元素判定），删除者进 `deletedVars`，任一端被删的 connect 整条丢弃。"非 connect 处引用条件组件"的检查 **1.25.0（Warning/`--strict` Error，Typing 期）与 HEAD（Error，压平后）实现不同**——记入分歧账本。

## 逐项事实

### 1. `NFModifier.mo` 的数据结构与修饰的作用域

**1.1 三个构造子**（`NFModifier.mo:148-168`）

| 构造子 | 字段 | 说明 |
|---|---|---|
| `MODIFIER` (149-156) | `String name`；`SCode.Final finalPrefix`；`SCode.Each eachPrefix`；`Binding binding`；`ModTable.Tree subModifiers`；`SourceInfo info` | 普通修饰。`p = 2` → name="p"、binding=RAW_BINDING(2)、无子修饰；`x(start = 1) = 3` → binding 与 subModifiers 同时存在，两者互相独立。 |
| `REDECLARE` (158-166) | `finalPrefix`；`eachPrefix`；`InstNode element`（**已经建好的 InstNode**，不是 SCode）；`Modifier innerMod`；`Modifier outerMod`；`Modifier constrainingMod`；`list<Subscript> propagatedSubs` | `redeclare` 修饰。`innerMod` = 从更内层合并上来、要作用在**被替换的原元素**上的普通修饰；`outerMod` = 从更外层压下来、要作用在**redeclare 新元素**上的普通修饰；`constrainingMod` = redeclare 元素自带 `constrainedby` 子句里的修饰。 |
| `NOMOD` (168) | — | 空修饰；常量 `EMPTY_MOD = NOMOD()`（:62）。 |

- **子修饰容器是 AVL 树**：`ModTable = BaseAvlTree(Key = String, Value = Modifier)`（:65-86，`keyCompare = stringCompare` :81-84）。后果：`Modifier.toList`（:535-543，`ModTable.listValues`）给出的子修饰**按名字字典序**，不是书写顺序。`applyModifier` 就按这个顺序逐个找元素，所以"修饰了多个不存在的元素"时先报字典序最小的那个（探针 P30：`A a(zz = 1, bb = 2)` 先报 `bb`）。
- **`ModifierScope`**（:89-146）只是"这个修饰写在哪种元素上"的标签：`COMPONENT(name)` / `CLASS(name)` / `EXTENDS(path)`；用途只有两个：① `DUPLICATE_MODIFICATIONS` 报错文案的后半句（`toString` :136-145 → `component a` / `class S` / `extends A`）；② `isClass`（:126）决定 binding 的 `Source` 是 `TYPE` 还是 `BINDING`（`create` :195 → `Binding.fromAbsyn` 的 `fromType` 参数，`NFBinding.mo:145`）。**它不是查找作用域。**

**1.2 从 SCode 创建：`Modifier.create`（:171-215）与 `fromElement`（:266-293）**

- `create(SCode.Mod mod, String name, ModifierScope modScope, InstNode scope)`：
  - `SCode.NOMOD` → `NOMOD`（:190）。
  - `SCode.MOD` → `binding := Binding.fromAbsyn(mod.binding, is_each, isClass(modScope), scope, mod.info)`（:195）；子修饰递归 `createSubMod`（:689-694，**同一个 `modScope` 与同一个 `scope` 原样下传**）；然后 `ModTable.fromList(submod_lst, mergeLocal(scope = modScope, prefix = {}))`（:197-198）——**同层重名子修饰在建表这一刻就用 `mergeLocal` 合并/报错**（见 §2.3）。HEAD 多了 `guard not SCodeUtil.isBreakSubMod(m)`（:196；1.25.0 没有）。
  - `SCode.REDECL` → `node := InstNode.new(elem, scope)`（:204，redeclare 元素的**父作用域 = 写修饰的地方**）；若是类立刻 `Inst.partialInstClass(node)`（:206-208）；`cc_mod := createConstrainingMod(elem, scope)`（:210, :217-235）；产出 `REDECLARE(final, each, node, NOMOD, NOMOD, cc_mod, {})`（:212）。
- `fromElement(SCode.Element, InstNode scope)`（:266-293）按元素种类取 SCode 修饰：`EXTENDS` → `element.modifications`，name=""，`ModifierScope.EXTENDS`（:276-277）；`COMPONENT` → 先 `patchElementModFinal`（:281, :295-323）再 `create`；`CLASS(DERIVED)` → `def.modifications`（:285-286）；`CLASS(CLASS_EXTENDS)` → `def.modifications`（:288-289）；长类定义 → `NOMOD`（:291）。
- `patchElementModFinal`（:295-323）：元素本身带 `final` 前缀时，把它声明处的修饰**标成 final**（没有修饰就造一个空的 final `SCode.MOD`，:320）。源码注释："so that we only need to check for final violations while merging modifiers"。所以 `final parameter Real p;` 被外层 `a(p = 3)` 修饰时，走的是同一条 `FINAL_COMPONENT_OVERRIDE`（探针 P08）。

**1.3 修饰里表达式的作用域记在哪里**

记在 **binding 自己身上**：`RAW_BINDING.scope`（`NFBinding.mo:77-84`），值就是 `create` 的 `scope` 参数；子修饰共用同一个 scope。之后修饰不管被合并、下传多少层，binding 记录原样搬运，到 `instBinding`（`NFInst.mo:2748-2781`）才 `instExp(binding.bindingExp, binding.scope, …)`（:2770）在**原作用域**里解析。`create` 的 `scope` 实参：

| 修饰写在 | 调用点 | scope 实参 |
|---|---|---|
| 组件声明 `A a(p = q)` | `instComponentDef` → `instElementModifier(component, node, parent)`（`NFInst.mo:1944`, :2035） | `parent` = 组件所在类的实例节点 |
| `extends A(p = k)` | `modifyExtends` → `Modifier.fromElement(elem, scope)`（:1386） | 顶层调用 `scope = par`（派生类实例，:1168）；嵌套 extends 时 `scope = extendsNode`（:1390） |
| 短类 `model S = A(p = c)` | `instClassDef` EXPANDED_DERIVED 分支 `instElementModifier(def, node, InstNode.rootParent(node))`（:1212） | 短类的（根）父作用域 |
| `constrainedby C(p = …)` | `instConstrainingMod(element, parent)`（:2045-2064）/ `getConstrainingMod`（:2066-2085） | 同声明所在类 |
| class extends 上的修饰 | `instClassDef` EXPANDED_CLASS 分支 `instElementModifier(def, node, par)`（:1159） | `par` |

探针（原文见"探针"节）：
- P01：`B b(q = q + 1, a(q = q * 100))` 写在 `P01_ModScope` 里 → `b.q = q + 1.0`、`b.a.q = q * 100.0`（两个 `q` 都是顶层的 `q`，没有被前缀成 `b.q`）；而 `B` 里的 `A a(p = q)` → `b.a.p = b.q`。
- P02：`extends A(p = k + r)`，`k` 是继承来的、`r` 是本地的 → `p = k + r`：extends 修饰的作用域是**派生类（含继承元素）**。
- P03：`package Pk … model S = A(p = c)` → `s.p = 42.0`（`Pk.c`），既不是 `A.c`(-1) 也不是使用处的 `c`(1000)。
- 用例 `ModScope1.mo` 同理。

### 2. 合并规则（`Modifier.merge` / `mergeLocal`）

**2.1 `merge(outerMod, innerMod, name = "")`（`NFModifier.mo:390-442`）逐分支**

| (outer, inner) | 结果 | 行 |
|---|---|---|
| `(NOMOD, _)` / `(_, NOMOD)` | 返回另一个 | 402-403 |
| `(MODIFIER, MODIFIER)` | ① `checkFinalOverride(innerMod.finalPrefix, outerMod, innerMod.info)`：**内层是 final 就报错并 fail**；② `binding := outer 有绑定 ? outer.binding : inner.binding`；③ `submods := ModTable.join(inner.subModifiers, outer.subModifiers, merge)`——同名子修饰**递归 merge**（`BaseAvlTree.join` `Util/BaseAvlTree.mo:390-406`，冲突函数签名 `(newValue=outer 的, oldValue=inner 的)`，:78-80），不同名的并集；④ 新节点 `MODIFIER(outer.name, outer.finalPrefix, outer.eachPrefix, binding, submods, outer.info)`——**name/final/each/info 全取外层** | 406-413 |
| `(REDECLARE, MODIFIER)` | `outer.innerMod := merge(outer.innerMod, inner)`，返回 outer（内层普通修饰挂到 redeclare 的 innerMod 上） | 415-419 |
| `(MODIFIER, REDECLARE)` | `inner.outerMod := merge(outer, inner.outerMod)`，返回 inner（外层普通修饰挂到 redeclare 的 outerMod 上——它们将作用于 redeclare 后的新元素） | 421-425 |
| `(REDECLARE 无 cc, REDECLARE 有 cc)` | 取 outer，但**继承 inner 的 constrainingMod** | 427-429 |
| `(REDECLARE, _)` / `(_, REDECLARE)` | 外层 redeclare 整个盖掉内层 redeclare | 431-432 |

要点：
- 合并**总是产生新值**（MetaModelica 值语义；`outerMod.innerMod := …` 是对局部副本的函数式更新）。没有原地改共享节点。
- 绑定与子修饰**各自独立**地"外层优先"：外层只写 `x(min = -5) = 3`、中层写 `x(start = 2)`、声明处 `x(start = 1, fixed = true, min = -1) = 0` → `x(min = -5.0, start = 2.0, fixed = true) = 3.0`（探针 P05）。
- 注意 `final` 只检查**内层**的 `finalPrefix`，且只在 `(MODIFIER, MODIFIER)` 分支检查：结果节点取的是**外层**的 finalPrefix（内层非 final、外层 final → 结果 final，后续更外层再改就报错）。

**2.2 `final` 被覆盖：`FINAL_COMPONENT_OVERRIDE`**

`checkFinalOverride`（:696-714）：`Error.addMultiSourceMessage(Error.FINAL_COMPONENT_OVERRIDE, {name(outerMod), Modifier.toString(outerMod, printName = false)}, {info(outerMod), innerInfo})` 然后 `fail()`。文案模板 `Util/Error.mo`：`Trying to override final element %s with modifier '%s'.`。`addMultiSourceMessage` 的约定（`Util/Error.mo:1491-1529`）：**最后一个 info 是错误本体的位置，前面的每个 info 先各打一条 `Notification: From here:`**。所以输出是"From here = 外层修饰位置；Error = 内层 final 修饰位置"。探针 P06 原文：

```
[…/P06_FinalOverride.mo:10:9-10:14:writable] Notification: From here:
[…/P06_FinalOverride.mo:6:13-6:18:writable] Error: Trying to override final element p with modifier '= 3'.
```

- 元素自带 `final` 前缀（P07 `final Real x(start = 1.0)` 被 `a(x(start = 2.0))`）：`Error` 位置是**整个元素声明**（4:3-4:28），文案 `… final element x with modifier '(start = 2.0)'.`（`printName = false` → 只打印 `(子修饰) = 绑定` 部分）。
- P08：`final parameter Real p;`（无修饰）被 `a(p = 3)` → 同一文案（靠 `patchElementModFinal` 造的空 final 修饰）。
- 用例 `Final1..7.mo`、`FinalParameter1..3.mo`。

**2.3 同一处重复修饰：`mergeLocal`（:716-757）→ `DUPLICATE_MODIFICATIONS`**

触发点 = `Modifier.create` 建子修饰表时（:197-198），即**一个修饰列表内部**的重名。规则：
- 两个都是 `MODIFIER` 且**其中一个没有绑定**（`binding = UNBOUND`）→ 合法：保留有绑定的那个，子修饰表 `join`，冲突时递归 `mergeLocal`（prefix 加一层名字）（:731-744）。所以 `a(x(start = 1), x(fixed = true))` 合法，`a(x = 1, x(start = 2))` 合法（探针 P10 的 a1、P11）。
- 其它（两个都有绑定；或任一是 `REDECLARE`）→ `Error.DUPLICATE_MODIFICATIONS`，token = {带点路径的元素名, `ModifierScope.toString(scope)`}，info = {mod1, mod2}，`fail()`（:746-754）。文案模板：`Duplicate modification of element %s on %s.`。探针 P09 / P10 原文：

```
[…/P09_DupMod.mo:7:14-7:19:writable] Notification: From here:
[…/P09_DupMod.mo:7:7-7:12:writable] Error: Duplicate modification of element p on component a.
[…/P10_DupModNested.mo:8:10-8:19:writable] Notification: From here:
[…/P10_DupModNested.mo:8:24-8:33:writable] Error: Duplicate modification of element x.start on component a2.
```

  （两个 info 的先后取决于 AVL 插入时谁是 new 谁是 old 以及递归层次，P09 与 P10 的顺序相反——ymc 对 L1 文本比对只需保证"两条位置都出现 + Error 行文案一致"，顺序要逐例对。）
- 用例 `DuplicateMod1..5.mo`（其中 extends 上的重复写作 `… on extends A.`，类上 `… on class X.`）。
- **跨层**的重复（外层 `p = 3`、内层 `p = 2`）不是错误，是 §2.1 的正常覆盖。

**2.4 `each` 与非 each**

- `merge` **不看** each：`(MODIFIER, MODIFIER)` 结果取外层的 `eachPrefix`（:413），binding 自己另带 `eachType`（`NFBinding.mo:81`，来自 `create` :194-195）。each / 非 each 的修饰跨层合并没有任何警告或错误。
- each 的全部语义在**传播**时体现：`propagateSubMod`（:490-513）只处理 `eachPrefix = NOT_EACH` 的子修饰（给 binding 追加切分下标并继续向更深的子修饰递归）；带 `each` 的子修饰**整棵原样跳过**（`else ()` :511）。详见 §4。
- `each` **不向下继承**：`A a(each n(fixed = true))`（`Each3.mo`，`n` 是 `Real[2]`）仍报 `Non-array modification 'true' for array component 'fixed', possibly due to missing 'each'.`——each 要写在**带绑定的最内层**修饰上（`n(each fixed = true)`，`Each4.mo`）。
- each 用在非数组上：**1.25.0 的 NF 不报警告**，照常应用（探针 P15：`A a(each p = 1)`、`Real x(each start = 2)` 输出无任何 Warning）。`Error.EACH_ON_NON_ARRAY`（`Util/Error.mo:663`）在 `NFFrontEnd/` 下无引用（grep 为空），只有旧前端用。
- 缺 each：`A a[3](p = 1)` → `MODIFIER_NON_ARRAY_TYPE_ERROR`（`NFTypeCheck.mo:3105-3107`，在**类型检查**阶段而非合并阶段报；探针 P14）。维数不符 `A a[3](p = {1, 2})` → `VARIABLE_BINDING_DIMS_MISMATCH`（`NFTypeCheck.mo:3115-3119`；探针 P16：`Type mismatch in binding 'p = {1.0, 2.0}', expected array dimensions [3], got [2].`）。

### 3. 传播路径与优先级

**3.0 修饰"住"在哪三个槽位里**

| 槽位 | 类型 | 什么时候写入 |
|---|---|---|
| `Component.COMPONENT_DEF.modifier`（`NFComponent.mo:68-71`，初值 `NOMOD` :111） | 未实例化组件身上积攒的**外来**修饰 | `applyModifier` → `Component.mergeModifier(mod, comp)` = `Modifier.merge(新来的, 已有的)`（`NFComponent.mo:219-233`）——**后到的当 outer**，见 3.3 |
| `Class.{PARTIAL_CLASS,EXPANDED_CLASS,EXPANDED_DERIVED,PARTIAL_BUILTIN}.modifier`（`NFClass.mo:388-399`） | 类节点身上积攒的外来修饰（对嵌套类 `a(T(start = 5))`、redeclare 类） | `applyModifier` 对类元素分支 `Class.mergeModifier`（`NFInst.mo:1604-1607`，`NFClass.mo:446-468`） |
| `Component.TYPE_ATTRIBUTE.modifier`（`NFComponent.mo:95-98`） | 内建类型的属性（start/fixed/min/…）最终落脚点 | `applyModifier` 的 `FLAT_TREE` 分支（`NFInst.mo:1536-1552`） |

另外 `Class.*.ccMod` 存"constrainedby 修饰"（`NFClass.mo:401-411`）。**元素自己声明处的修饰不预存**，到 `instComponentDef`/`instClassDef` 现场用 `instElementModifier`（`NFInst.mo:2027-2043`）从 SCode 现造。

**3.1 主干：`instClass` → `instClassDef`（`NFInst.mo:1099-1123`, :1125-1282）**

`instClass(node, modifier /*outerMod*/, attributes, useBinding, instLevel, parent, context)`：先取类节点自己身上的 `Class.getModifier(cls)`，若**带绑定**（`a(In = In2)` 这种）→ `MISSING_REDECLARE_IN_CLASS_MOD`（:1116-1120；探针 P23：`Missing redeclare keyword on attempted redeclaration of class In.`）。然后 `instClassDef` 按类的形态分支：

- **`EXPANDED_CLASS`（长类）**（:1144-1203），顺序即传播顺序：
  1. `ClassTree.instantiate(node, parent)`（:1151）克隆类树得到本实例专属的节点（基类节点 `isBaseClass` 时跳过，:1148-1149，因为派生类 instantiate 时已一并克隆）。
  2. 组出本作用域总修饰 `mod`（:1159-1165）：
     `mod := instElementModifier(def, node, par)`（只对 class extends 非空）→ `propagate` → `merge(mod, cls.ccMod)` → `outer_mod := merge(outerMod /*参数*/, propagate(cls.modifier /*节点上积攒的*/))` → `mod := merge(outer_mod, mod)`。
     即 **参数传入的外层修饰 > 类节点上积攒的修饰 > 类定义自带修饰 > constrainedby 修饰**。
  3. `ClassTree.mapExtends(cls_tree, modifyExtends(scope = par))`（:1168）：**先**把每个 `extends B(…)` 的修饰压进基类树的元素（3.2(b)）。
  4. `applyExtendsVisibility`（:1171-1172）。
  5. `applyModifier(mod, cls_tree, node, context)`（:1175）：**后**把本作用域总修饰拆开压进各元素——因为后到的当 outer，所以**外层实例修饰 > extends 修饰**。
  6. `mapRedeclareChains(… redeclareElements …)`（:1178-1179）处理类体里的 `redeclare` 元素；`redeclareClasses`（:1184）处理带 `REDECLARE` 修饰的类元素。
  7. `mapExtends(instExtends)`（:1187-1189）实例化基类里的组件；`applyLocalComponents(instComponent)`（:1192-1195）实例化本地组件。
  8. `replaceDuplicates` + `checkDuplicates`（:1198-1199），`Restriction.checkClass`。
- **`EXPANDED_DERIVED`（短类）**（:1205-1231）：`mod := instElementModifier(def, node, rootParent(node))`（短类自己的 `= N(p = 4)`）→ `propagate(mod, node, par)` → `merge(mod, cls.ccMod)`；`outer_mod := merge(outerMod, propagate(cls.modifier, node, par))`；`mod := merge(outer_mod, mod)`；属性 `mergeDerivedAttributes`；然后**递归 `instClass(base_node, mod, …)`**（:1222）把合并后的修饰整体当作基类的 outerMod 下传。短类链 `S2 = S1(p=3)`、`S1 = A(p=2,q=2)`、`S2 s(r=4)` → p=3,q=2,r=4（探针 P21）。
- **`PARTIAL_BUILTIN`（Real 等）**（:1246-1260）：`mod := instElementModifier(…)`；`outer_mod := merge(outerMod, cls.modifier)`；`mod := merge(outer_mod, mod)`；`applyModifier(mod, cls_tree, node, context)` 走 `FLAT_TREE` 分支 → 每个子修饰 `ClassTree.lookupElement(name)` 找 `TYPE_ATTRIBUTE` 组件并 `Component.mergeModifier`；找不到 → `MISSING_MODIFIED_ELEMENT`（探针 P26：`Modified element strat not found in class Real.`）。类变 `INSTANCED_BUILTIN`（:1257）。
- `PARTIAL_BUILTIN(EXTERNAL_OBJECT)`（:1233-1244）：只为报错而调 `applyModifier`。

**3.2 五条路径**

- **(a) 组件声明上的修饰 `A a(p = 2)`**：`instComponent`（:1858-1913）取出 `COMPONENT_DEF.modifier` 作为 `outer_mod`（:1894）→ `instComponentDef`（:1915-2025）：
  `mod := instElementModifier(component, node, parent)`（声明处修饰，:1944）→ `merge(mod, innerMod)`（:1950，innerMod 只有 redeclare 场景非空）→ `mod := merge(outerMod, mod)`（:1951，**外来 > 声明处**）→ `binding := Modifier.binding(mod)`（:1954，合并后顶层的绑定就是组件的绑定；`useBinding = false` 时丢弃）→ 若 `mod` 是 final 则组件属性 `isFinal := true`（:1968-1970）→ `mod := Modifier.propagate(mod, node, node)`（:1983，给所有非 each 子修饰的绑定追加 `SPLIT_PROXY(node, node)`，§4）→ `instTypeSpec(typeSpec, mod, attr, useBinding and not isBound(binding), parent, node, …)`（:1984-1985）→ `Lookup.lookupClassName` + `expand` + `instClass(node, modifier, …)`（:2284-2291）——`mod` 的**子修饰**作为组件类型实例的 outerMod 进入 3.1。
  注意 :1985 的 `useBinding and not Binding.isBound(binding)`：组件自己有绑定（`R r = R(…)`）时，其类型内部各字段的声明绑定被忽略。
- **(b) `extends B(p = 3)`**：`modifyExtends(extendsNode, scope)`（:1369-1417）：`ext_mod := Modifier.fromElement(elem, scope)`（:1386）→ `merge(InstNode.getModifier(extendsNode), ext_mod)`（:1387，基类节点上已有的修饰当 outer）→ **先递归**处理基类自己的 extends（:1390，`scope = extendsNode`）→ 重新 `lookupBaseClassName` 校验基类没变（`FOUND_OTHER_BASECLASS` :1400-1406）→ `applyModifier(ext_mod, cls_tree, extendsNode, context)`（:1416）。递归顺序 = 最深的基类先 apply、越靠近派生类越后 apply → **越靠近派生类的 extends 修饰优先**（探针 P20：A.p=1 / B: `extends A(p=2,q=2)` / C: `extends B(p=3)` → p=3,q=2,r=1）。
- **(c) 短类定义 `model M = N(p = 4)`**：见 3.1 EXPANDED_DERIVED。短类修饰的 binding 在 `propagate(mod, node, par)` 时 origin=短类节点、parent=使用它的组件（`Modifier.propagate` 注释 `NFModifier.mo:444-449`），用于数组 fill（§4）。
- **(d) 类上的修饰逐层下传**：没有"全局修饰环境"。每一层 `instClassDef` 只做一件事：把 `mod` **按名字拆成子修饰**（`Modifier.toList`）并 merge 进本层各元素的槽位（`applyModifier` :1514-1621）；元素被实例化时再把自己槽位里的修饰（其子修饰）传给下一层 `instClass`。`b(a(x(start = 2)))` 就是这样一层剥一层到达 `Real` 的 `start`。对**嵌套类**元素：`partialInstClass(node)` + `Class.mergeModifier` + `clearPackageCache`（:1604-1607），等该类被某组件用作类型时，`instClassDef` 从 `cls.modifier` 取出（探针 P22：`A a(T(start = 5), In(k = 8))` → `a.x(start = 5.0)`、`a.i.k = 8.0`）。
- **(e) 内建属性**：终点是内建类型类树里的 `TYPE_ATTRIBUTE(ty, modifier)` 组件；`instComponentExpressions` 对它调 `instBuiltinAttribute`（:2658-2683, :2729-2734；`REDECLARE` → `INVALID_REDECLARE_IN_BASIC_TYPE`）；`NFTyping.typeTypeAttribute`（`NFTyping.mo:1256-1317`）做类型检查（带子修饰如 `start(y = 1)` → `MISSING_MODIFIED_ELEMENT` "start.y"；无绑定如 `final start` → 丢弃；variability ≥ discrete → `HIGHER_VARIABILITY_BINDING`）；压平时 `Class.getTypeAttributes(cls)`（`NFClass.mo:552-571`，`NFFlatten.mo:557/564`）把非空修饰收集成变量的属性表。

**3.3 `applyModifier`（:1514-1621）细节**

- 对每个子修饰 `ClassTree.lookupElementsPtr(name, cls)`（:1559，可能多个——重复继承元素每个都要改）；对每个节点：`resolveOuter`；**protected 检查**：`InstNode.isProtected(node) and not (isExtends(parent) or isBaseClass(parent))` → `NF_MODIFY_PROTECTED`（:1578-1587）——extends 修饰可以改 protected 元素，组件修饰不行（探针 P24：`Protected element 'p' may not be modified, got 'p = 3'.`）；`isOnlyOuter` → `OUTER_ELEMENT_MOD`（:1589-1599）；组件 → `Component.mergeModifier`（:1602）；类 → :1604-1607。
- 找不到 → `MISSING_MODIFIED_ELEMENT {子修饰名, InstNode.name(parent)}`（HEAD :1611-1615；1.25.0 在 lookup 失败的 `else` 分支里直接报，效果相同）。extends 修饰写错名字同一文案（探针 P25：`Modified element y not found in class A.`）。
- `Component.mergeModifier(modifier, component)` = `Modifier.merge(modifier /*outer*/, component.modifier /*inner*/)`：**对同一元素，后 apply 的赢**。NFInst 里的 apply 顺序是 ① 基类链由深到浅的 extends 修饰（modifyExtends 递归）→ ② 本层总修饰（含外层实例传入的）；元素实例化时再 ③ `merge(槽位里的, 声明处的)`。

**3.4 优先级（探针 P04/P19/P20/P21 原文验证）**

`外层实例修饰 > 内层实例修饰 > 靠近派生类的 extends 修饰 > 远离派生类的 extends 修饰 > 短类链上靠外的修饰 > 元素声明处的修饰/默认值`

P04：`A{p=q=r=s=1}`；`B extends A(p=2,q=2,r=2)`；`C{ B b(p=3,q=3) }`；顶层 `C c(b(p=4))` → 

```
  parameter Real c.b.p = 4.0;
  parameter Real c.b.q = 3.0;
  parameter Real c.b.r = 2.0;
  parameter Real c.b.s = 1.0;
```

### 4. Binding 的状态、`each` 与数组切分

**4.1 状态机（`NFBinding.mo:74-125`）**

| 构造子 | 字段 | 由谁产生 |
|---|---|---|
| `UNBOUND` (74) | — | 无绑定；`EMPTY_BINDING`（:54） |
| `RAW_BINDING` (77-84) | `Absyn.Exp bindingExp`；`InstNode scope`；`list<Subscript> subs`；`EachType eachType`；`Source source`；`info` | `Binding.fromAbsyn`（:128-151），即 `Modifier.create` 时。表达式还是 **Absyn**，没做名字解析 |
| `UNTYPED_BINDING` (86-93) | `Expression bindingExp`；`isProcessing`；`scope`；`eachType`；`source`；`info` | `NFInst.instBinding`（`NFInst.mo:2748-2781`）：`instExp(exp, binding.scope)`；若 `subs` 非空，包成 `Expression.SUBSCRIPTED_EXP(exp, subs, UNKNOWN, split = true)`（:2772-2774） |
| `TYPED_BINDING` (95-105) | `bindingExp`；`bindingType`；`variability`；`purity`；`eachType`；`Mutable<EvalState> evalState`；`isFlattened`；`source`；`info` | `NFTyping.typeBinding`（`NFTyping.mo:1168-1200`）；随后 `TypeCheck.matchBinding`（`NFTypeCheck.mo:2967-2984`） |
| `FLAT_BINDING` (107-111) | `bindingExp`；`variability`；`source` | `NFFlatten.flattenBinding`（`NFFlatten.mo:1563-1609`） |
| `CEVAL_BINDING` (113-118) | `bindingExp` | 常量求值临时生成（记录字段拼出来的绑定），压平时丢弃（`NFFlatten.mo:1593`） |
| `INVALID_BINDING` (120-123) | `binding`；`errors` | 带条件的组件绑定出错时**延迟报错**（`NFTyping.mo:1043-1044`）；压平时若组件没被删才 `addTotalMessages` + fail（`NFFlatten.mo:1596-1600`） |
| `WILD` (125) | — | 后端新 cref 用的占位（1.25.0 也有该 record，`v1.25.0:NFBinding.mo:121`）；前端不产生 |

`Source`（:67-72）：`BINDING`（声明处 `= e`）/ `TYPE`（类修饰，`ModifierScope.isClass` 时）/ `MODIFIER`（**一旦被 `Binding.propagate` 追加过下标就改成 MODIFIER**，:847）/ `GENERATED`。

**4.2 "origin/parents" 信息 = `subs` 里的 `SPLIT_PROXY`**

1.25.0 与 HEAD 一致（`git show v1.25.0:…/NFBinding.mo` 的 diff 只涉及 WILD/applyExp 等无关函数）：没有 `parents` 列表，而是给 RAW_BINDING 追加"占位下标"。

- `Subscript.SPLIT_PROXY(InstNode origin, InstNode parent)` / `SPLIT_INDEX(InstNode node, Integer dimIndex)`（`NFSubscript.mo:86-98`，注释："Proxies are added during the instantiation and then replaced with split indices during typing once the number of dimensions on elements are known"）。
- **追加时机**：每当修饰"穿过一个元素"就 `Modifier.propagate(mod, origin, parent)`（`NFModifier.mo:444-454`）→ 对 `mod` 的**每个非 each 子修饰**递归 `Binding.propagate(binding, {SPLIT_PROXY(origin, parent)})`（`propagateSubMod` :490-513；`NFBinding.mo:838-853` 追加到 `subs` 末尾）。调用点：组件 `instComponentDef` :1983（`origin = parent = 组件节点`）；类 `instClassDef` :1160/:1163/:1213/:1215（`origin = 类节点, parent = par`，par 是使用该类的组件）。注意 `propagate` 只处理**子修饰**，`mod` 自己顶层的 binding（= 组件自己的绑定）不加——它属于上一层。
- **each**：`propagateSubMod` 的两个 case 都要求 `eachPrefix = NOT_EACH`，each 子修饰整棵跳过 → 它的 binding 不带这一层的下标 → 每个数组元素拿到**同一个完整表达式**。
- **Typing**：`typeSubscriptedExp`（`NFTyping.mo:1540-1579`）先 `expandProxySubscripts`（:1588-1653）：每个 `SPLIT_PROXY` 展开成 `InstNode.dimensionCount(s.parent)` 个 `SPLIT_INDEX(parent, i)`（标量父节点 → 0 个，等于没切）；`origin ≠ parent`（类修饰）且父维数多于 origin 维数时补 `fill(exp, size(parent,1), …)`（:1623-1643，源码例子 `type T = Real[3](start = {1,2,3}); T x[1,2]`）。
- **类型检查**：`elaborateBindingType`（`NFTypeCheck.mo:2996-3088`）按 `SPLIT_INDEX` 把对应维度补回组件类型再比：`A a[3](x = {1, 2})` 才能报 `expected array dimensions [3], got [2]`；绑定是标量而期望数组 → `MODIFIER_NON_ARRAY_TYPE_ERROR`（:3105-3107）。
- **压平**：`flattenExp` 遇 `SUBSCRIPTED_EXP(split = true)` → `replaceSplitIndices`（`NFFlatten.mo:1625-1670`）：沿当前前缀（`Prefix.indexedPrefix`）逐段取实际下标，把 `SPLIT_INDEX(node, i)` 换成该 node 在前缀里的第 i 个下标（`replaceSplitIndices2` :1672-1683 用 `InstNode.refEqual` 对节点），然后 `Expression.applySubscripts` —— 字面数组直接取元素、cref 变 `v[1]`。

**4.3 探针**

- P12：`A a[3](p = {1, 2, 3}, each q = 5, x(start = v))` →

```
  parameter Real a[1].p = 1.0;
  parameter Real a[1].q = 5.0;
  Real a[1].x(start = v[1]);
  parameter Real a[2].p = 2.0;
  …
  Real a[3].x(start = v[3]);
```

- P13（两层数组）：`B{ A a[3](q = {7,8,9}); }`、`B b[2](a(p = {{1,2,3},{4,5,6}}))` → `b[i].a[j].p = M[i][j]`，`b[i].a[j].q = {7,8,9}[j]`（q 的绑定只带 `a` 这一层的 proxy，因为它写在 `B` 里面、没穿过 `b`）。
- P17（类修饰 + fill）：`type T = Real[3](start = {1,2,3}); T x[2];` → `x[i,j](start = j)`；`type S = Real(start = 4); S y[2];` → 每个 `start = 4.0`，不需要 each（类修饰 origin = 类，标量类 0 维 → fill）。用例 `ClassMod4.mo` 同理。
- P14 / P16：缺 each、维数不符的报错原文见 §8。
- P15：each 用在标量上，无警告。

**4.4 无绑定 parameter 借用 start：`updateParameterBinding`（`NFInst.mo:2357-2398`）**

条件（:2016-2019）：`useBinding`、组件无绑定、不在函数里、variability ≤ parameter、类型是 `type` 限制类；再要求 `fixed = true`、所在记录没有绑定（:2376）、start 修饰**不是来自类型**（`Binding.hasTypeOrigin` :928-939：第一个 proxy 的 origin 是类）。动作：警告 `UNBOUND_PARAMETER_WITH_START_VALUE_WARNING`；`Binding.unpropagate(binding, node)`（去掉 origin = 本组件的 proxy，:855-868）；若 each 则 `expandEach` 包成 `fill(e, size(x,1), …)`（:941-969）；设为组件绑定。探针 P18：

```
  parameter Real a.p(start = 1.0) = 1.0;
  parameter Real b[1].p(start = 5.0) = 5.0;
  parameter Real b[2].p(start = 6.0) = 6.0;
[…:3:3-3:30:writable] Warning: Parameter b.p has no value, and is fixed during initialization (fixed=true), using available start value (start={5, 6}) as default value.
```

（警告里的组件名是 `b.p` 不带下标、start 打印的是**原始 Absyn**（`{5, 6}` 不是 `{5.0, 6.0}`），且每个声明只报一次而不是每个数组元素一次——因为发生在实例化阶段，数组还没展开。）

### 5. extends 的实例化

**5.1 三个阶段里 extends 各做什么**

| 阶段 | 函数 | 对 extends 做的事 |
|---|---|---|
| expand | `expandClassParts`（`NFInst.mo:710-753`）→ `ClassTree.mapFoldExtends(… expandExtends …)`（:734）→ `ClassTree.expand`（:748，`NFClassTree.mo:353-430`） | 查基类、递归 expand 基类、把基类节点塞进 `exts[]`、把继承来的**名字**并进派生类的查找树 |
| instantiate | `ClassTree.instantiate`（`NFClassTree.mo:432-662`） | 克隆：每个 `exts[i]` 递归 instantiate（:498-510），把本地 + 继承组件排进同一个 `comps[]`（`Mutable<InstNode>` 数组） |
| instClassDef | `modifyExtends`（`NFInst.mo:1369-1417`）→ `applyExtendsVisibility`（:1421-1468）→ `instExtends`（:1470-1512） | 压 extends 修饰、改可见性、实例化基类树里的组件 |

**5.2 `NFInst.expandExtends`（:755-805）**

1. `scope := InstNode.parent(ext)`；`base_nodes as (base_node :: _) := Lookup.lookupBaseClassName(base_path, scope, context, info)`（:780-781）。找不到 → `LOOKUP_BASECLASS_ERROR`（`NFLookup.mo:94-112`；探针 P41：`Base class Missing not found in scope P41_BaseNotFound.`，位置是整个 extends 子句）。
2. `checkExtendsLoop`（:807-845）：基类正处于 expand 中（`EXPANDED_CLASS(elements = PARTIAL_TREE)`，:821）或基类就是当前作用域的某个外层（:832-840）→ `EXTENDS_LOOP`（用例 `ExtendsLoop1.mo`，需 `-i=ExtendsLoop1.M`：`extends ExtendsLoop1 causes an instantiation loop.`）。
3. `checkReplaceableBaseClass`（:847-888）：基类路径上任何一段是 replaceable → `REPLACEABLE_BASE_CLASS`（用例 `ExtendReplaceable1.mo`）。
4. `base_node := expand(base_node, context)`（:789）；`ext := InstNode.setNodeType(BASE_CLASS(scope, def, 原 nodeType), base_node)`（:791）——extends 节点就是**基类节点换了 nodeType**（parent 指向派生类、带上 `SCode.EXTENDS` 定义）。
5. 基类是内建类型（或派生自内建类型）→ 记到 `builtinExt`（:797-799），随后 `checkBuiltinTypeExtends`（:908-921）：派生类还有别的组件或多于 1 个 extends → `BUILTIN_EXTENDS_INVALID_ELEMENTS`。

**5.3 类树如何合并：`ClassTree.expand`（`NFClassTree.mo:353-430`）**

- 本地组件先进查找树（`addLocalElement` :1644-1652）；两个**本地**同名 → `DOUBLE_DECLARATION_OF_ELEMENTS`（`addLocalElementConflict` :1654-1677；探针 P39：`An element with name x is already declared in this scope.`，From here = 先声明的，Error = 后声明的）。本地元素覆盖同名 import（:1665）。
- `comps[]` 里每个 extends 占一个 `REF_NODE` 占位（:396-401），继承组件的下标紧接占位处展开 → **继承元素在压平结果里的位置 = extends 子句的书写位置**（探针 P40：`Real x; extends A; Real y;` → `x, a1, a2, y`）。类不保序（`instantiate` 注释 :541-544）。
- 继承名字逐个 `addInheritedElement`（:2040-2066；import 不继承 :2063）；与已有名字冲突 → `addInheritedElementConflict`（:2068-2157）：**不报错**，只在 `DuplicateTree` 里登记，下标小的（= 书写位置靠前的）当"保留者"（:2096-2102）。本地元素与继承元素同名也走这条路。

**5.4 重复元素的等价性检查**

`instClassDef` 末尾（`NFInst.mo:1198-1199`）：`replaceDuplicates`（`NFClassTree.mo:726-743`, :2320-2383）把所有重复节点的 `Mutable` 指针改指向保留者；`checkDuplicates`（:1334-1380）对每个重复者 `InstNode.checkIdentical(kept, dup)`（`NFInstNode.mo:1659-1683`）：

- 同一节点（`referenceEq`）→ 直接过。
- 组件 vs 组件 → `Component.isIdentical`（`NFComponent.mo:727-752`）：只比 ① `Class.isIdentical`（`NFClass.mo:479-503`：内建类型比 `Type.isEqual`；长类只比 prefixes，因为 `ClassTree.isIdentical` **恒为 true**，`NFClassTree.mo:1382-1388`）② `Binding.isEqual`（`NFBinding.mo:497-517`：`RAW_BINDING` 用 `AbsynUtil.expEqual` **比语法树**，源码自注 `TODO: Handle propagated dims`）。**不比**维度、variability、属性修饰（start 等）、注释。
- 类 vs 类 → `Class.isIdentical`；类 vs 组件 → 直接报错。
- 不相同 → `DUPLICATE_ELEMENTS_NOT_IDENTICAL`，两个 token 是 `InstNode.toString`（组件 = `Attributes + Type + name + binding`，:766-769）。

探针：
- P31（菱形：`extends B1; extends B2; extends A;`）→ 只输出一份 `p`、`x`。
- P32：`Real x = 1` vs `Real x = 2` →

```
[…/P32_DupNotIdentical.mo:2:3-2:13:writable] Notification: From here:
[…/P32_DupNotIdentical.mo:5:3-5:13:writable] Error: Duplicate elements (due to inherited elements) not identical:
  first element is:  Real x = 1
  second element is: Real x = 2
```

- P33：`extends A; Real x = 1;`（A 里也是 `Real x = 1`）→ 合法，`x` 留在**继承位置**（输出 `x, y, z`）。P34：本地 `Integer x = 1` → 报错（`Real x = 1` / `Integer x = 1`）。
- P35：`extends A(p = 2); extends A(p = 3);` → 不相同（`parameter Real p = 2` / `= 3`）——说明检查发生在**修饰合并之后**（比较的是已实例化组件的 binding）。只有一个 info（两份 `p` 是同一声明，`addMultiSourceMessage` 的 `listMember` 去重，`Util/Error.mo:1514`）。
- P36：`B{extends A; extends A;}`、`B b(p = 5)` → 合法，`b.p = 5.0`：外层修饰经 `lookupElementsPtr` **同时打到两份**上（`NFInst.mo:1559-1567` 的注释 "Might be several in case of duplicate inherited elements"）。
- 用例 `DuplicateElements1..11.mo`、`DuplicateElementsValid1/2.mo`、`DuplicateElementsCond1.mo`。

**5.5 `extends` 在 protected 段**

`applyExtendsVisibility`（`NFInst.mo:1421-1468`）：`InstNode.isProtectedBaseClass(node)`（`NFInstNode.mo:1767-1778`，看 `SCode.EXTENDS.visibility`）为真 → 该基类树里**所有**类与组件 `protectClass/protectComponent`（:1442-1448），并向更深的 extends 递归传 `PROTECTED`（:1451）；短类基类走 `DERIVED_PROTECTED`（:1455-1463）。探针 P37：`protected extends A;` → `protected parameter Real b.p = 1.0; protected Real b.x = b.p;`；P38：外部 `B b(p = 2)` → `Protected element 'p' may not be modified, got 'p = 2'.`（因为 `applyExtendsVisibility` 在 :1171 先于 `applyModifier` :1175 执行）。用例 `ExtendsVisibility1..5.mo`。

**5.6 其它规则**

- `modifyExtends` 里二次查基类（:1396-1406）：expand 之后再按同一路径查，若查到的定义不同（多半是查到了继承进来的同名类）→ `FOUND_OTHER_BASECLASS`（用例 `ExtendInherited1.mo`：`Found other base class for extends A after instantiating extends.`）。
- 对继承元素的修饰写了不存在的名字 → `MISSING_MODIFIED_ELEMENT`，`%s` 的类名是 `InstNode.name(extendsNode)` = 基类名（探针 P25：`Modified element y not found in class A.`）。
- 短类 `model S = A(...)` 不是 extends 节点：`expandClassDerived`（:1017-1066）查基类 → `expand` → **`InstNode.clone(ext_node)`**（:1047）→ `EXPANDED_DERIVED(baseClass, mod, cc_mod, dims, prefs, attrs, res)`；`class A = A` → `RECURSIVE_SHORT_CLASS_DEFINITION`（:1040-1044）。基类 partial 则短类也 partial（:1054-1056）。
- 类可以经由自己的名字 extends 自己的局部类（用例 `ExtendSelf1.mo`，`extends ExtendSelf1.A;` 合法）。

### 6. redeclare

**6.0 两种句法形态，两条代码路径**

| 形态 | 例子 | 表示 | 消费点 |
|---|---|---|---|
| redeclare 作为**修饰** | `A a(redeclare BB c)`、`A a(redeclare model B = C)`、`extends P(redeclare BB a)` | `Modifier.REDECLARE`（§1 构造子）——`Modifier.create` 的 `SCode.REDECL` 分支（`NFModifier.mo:200-213`）造出，随普通修饰一起 `applyModifier` 落到目标元素的槽位 | 组件：`instComponent`（`NFInst.mo:1858`）检出 `Modifier.isRedeclare(outer_mod)`（:1896）→ `redeclareComponent`（:2155）。类：`redeclareClasses`（:1623）遍历类元素，`Modifier.isRedeclare(mod)`（:1640）→ `redeclareClass`（:1712）。 |
| redeclare 作为**元素** | 类体里直接写 `redeclare Real x = 2;`、`redeclare model B … end B;`、`redeclare model extends B …`（class extends） | 不是 Modifier，是一条普通 SCode 元素，`SCodeUtil.isElementRedeclare` 为真；在 `ClassTree` 里与被替换元素登记成一条 **redeclare 链**（`DuplicateTree` 的 `REDECLARE` 条目） | `instClassDef`（EXPANDED_CLASS）里 `ClassTree.mapRedeclareChains(cls_tree, redeclareElements)`（:1178-1179）→ `redeclareElements`（:1654）分派到 `redeclareClassElement`（:1682）/`redeclareComponentElement`（:1696），再进 `redeclareClass`/`redeclareComponent`。 |

两条路径**汇流到同一对底层函数** `redeclareClass` / `redeclareComponent`，区别只是外来修饰（`outerMod`）从哪来：修饰形态带着实例上写的 `(…)`，元素形态的 `outerMod = NOMOD`（`redeclareClassElement` :1692、`redeclareComponentElement` :1708 都传 `Modifier.NOMOD()`）。

**6.1 组件 redeclare：`redeclareComponent`（`NFInst.mo:2155-2238`）**

调用前，`instComponent`（:1896-1909）已经把三份修饰拆好（`Modifier.REDECLARE(element=rdcl_node, innerMod=inner_mod, outerMod=outer_mod, constrainingMod=cc_mod)`）：
1. 先用 **innerMod** 实例化**被替换的原元素**：`instComponentDef(def, NOMOD, inner_mod, DEFAULT_ATTR, …)`（:1900-1901）——`def` 是原 replaceable 组件的 SCode。所以原声明处的绑定/子修饰（如 `replaceable A a(x = 1)` 的 `x=1`）先落进原组件。
2. `cc_mod := getConstrainingMod(def, parent, cc_mod)`（:1904）：把**原元素自带的 `constrainedby` 修饰**并进 cc_mod；再 `cc_mod := merge(cc_mod, innerMod)`（:1905）——innerMod 垫在最下。
3. `outer_mod := merge(InstNode.getModifier(rdcl_node), outer_mod)`（:1907）后 `redeclareComponent(rdcl_node, node, NOMOD, cc_mod, …)`（:1909）——注意实参里 `outerMod=NOMOD`、`constrainingMod=cc_mod`。

`redeclareComponent` 本体：
- `rdcl_node := setNodeType(REDECLARED_COMP(parent(orig_node)), redeclareNode)` + `copyInstancePtr(orig_node, …)`（:2182-2185）：redeclare 节点**顶替原节点的实例指针**（后续对 `a` 的查找都命中新节点）。
- `instComponent(rdcl_node, outerAttr, constrainingMod /*作为 innerMod*/, true, …, SOME(orig 的 Attributes), propagatedSubs)`（:2187）：实例化 redeclare 的新类型，把上一步的 cc_mod 当 innerMod 压进去；`instElementModifier`（:2027）此时因 `InstNode.isRedeclared` 为真走 `propagateRedeclaredMod`（:2037, :2087）**而不再叠 constrainedby**。
- **非 replaceable 保护**：`if not isReplaceable(orig_node) and not inInstanceAPI and not Type.isEqual(元素类型 orig, rdcl)` → `REDECLARE_NON_REPLACEABLE {orig 名}`，两个 info（orig、rdcl），`fail()`（:2191-2197）。**只有换类型才查**——`redeclare Real x` 重声明成同一类型不需要 replaceable（探针见 §8 说明）。
- **绑定优先级**（:2201-2205）：`outerMod 的绑定 > redeclare 组件自己的绑定 > 原组件的绑定`。redeclare2 用例：`A a(redeclare parameter Real x = 0)` → `parameter Real a.x = 0.0`（redeclare 自带绑定）。
- **条件**：redeclare **不得**带条件 → `REDECLARE_CONDITION`（:2208-2213）；`condition := orig_comp.condition`（:2215）——**沿用原组件的条件**（用例 `RedeclareElementCondition1..3.mo`；§7 亦述）。
- **维度**：redeclare 没写维度就取原组件的（:2219-2221，`RedeclareDim1/2.mo`）。
- **属性合并**：`Attributes.mergeRedeclaredComponentAttributes(orig 属性, redecl 属性, node)`（在 `instComponentDef` :1965，`NFAttributes.mo:279-352`）：redeclare 未给的前缀沿用原声明；**不许改**已有前缀，**例外** ① variability 只能降（parameter→constant 合法，反向 `REDECLARE_MISMATCHED_PREFIX`，探针 P68）② final 永远取 redeclare 的。注释原文见 `NFAttributes.mo:301-305`。
- 注释：comment 取原组件的（:2224，`TODO: Use comment of redeclare if available?`）。

探针 **P60**（`Wrap w(redeclare BB c)`，`BB extends AA`）→ `w.c.x = 1.0; w.c.y = 2.0;`（换成子类型，多出 `y`）。**P62**（对非 replaceable 的 `A a` 写 `redeclare B a`）→ `Redeclaration with a new type requires 'a' to be replaceable.`。

**6.2 类 redeclare：`redeclareClass`（`NFInst.mo:1712-1803`）**

- `if not InstNode.isClass(redeclareNode)` → `INVALID_REDECLARE_AS {原类型名, 名, 新类型名}`（:1729-1733）——把类 redeclare 成组件（`RedeclareClassComponent.mo`：`Invalid redeclaration of class M as component.`）或反之（`RedeclareComponentClass.mo`）都撞这里。
- `partialInstClass(redeclareNode)`（:1738）；`prefs := mergeRedeclaredClassPrefixes(orig 的, rdcl 的, redeclareNode)`（:1745-1746，`NFAttributes.mo:354-389`，inner/outer 冲突 → `REDECLARE_MISMATCHED_PREFIX`）。
- **class extends 分支**（:1748-）：`SCodeUtil.isClassExtends(定义)` 为真时（`redeclare model extends B …`），基类是**被继承来的同名类**；空 class extends（`ClassExtends8`）走 :1757-1761。普通 `redeclare model B = C` 走 :1785/1788。
- **枚举**：`redeclareEnum`（:1805-1856）要求是子类型，否则 `REDECLARE_ENUM_NON_SUBTYPE`（:1837）/`REDECLARE_CLASS_NON_SUBTYPE`（:1849）；`enumeration(:)` 是通配（`RedeclareEnum1..6.mo`）。
- `redeclaredNode := replaceClass(new_cls, redeclareNode)`（:1800）；nodeType 设成 `REDECLARED_CLASS(parent(originalNode), 原 nodeType, SOME(orig))`（:1801）——**保留 originalNode**，`InstNode.getRedeclaredNode`（`NFInstNode.mo:1744-1752`）能取回，查找时 `enclosingScope` 用 `ignoreRedeclare` 决定看新类还是原类的作用域（`NFInstNode.mo:738-799`）。
- redeclare 类上的**修饰**照常合并进新类节点的 `modifier` 槽（`redeclareClasses` :1641-1643 先 `getConstrainingMod` 再进 `redeclareClass` 的 `outerMod`）。redeclare12 用例：`redeclare model ReplaceableInternalModel = MyInternalModel(final par = localPar)` → `final parameter Real …par = localPar`（修饰穿过 redeclare 落到新类字段，且 final 生效）。

探针 **P61**（`A a(redeclare model B = C)`，C 有 x、z）→ `a.b.x = 1.0; a.b.z = 5.0;`（原 B 的 `x` 被 C 的定义整体替换）。**P65**（`redeclare model extends B` 加 `Real y`）→ 每个 B 实例都有 `x` 和 `y`（class extends 在原类基础上**追加**，`ClassExtendsMod1` 还能 `extends B(x=y)` 修饰继承字段）。

**6.3 `constrainedby`：约束类及其修饰**

两个函数分工（都在 `NFInst.mo`）：
- `instConstrainingMod(element, parent)`（:2045-2064）：从元素**自己**的 `replaceable … constrainedby C(smod)` 里取 `smod`，`Modifier.create`。在 `instElementModifier`（:2027-2043）里，**非 redeclare** 的 replaceable 元素默认 `merge(元素修饰, cc_mod)`——即 **constrainedby 的修饰无条件应用到该元素**（哪怕没人 redeclare 它）。探针 **P67**：`replaceable Real x constrainedby Real(start = 1.0)`（无 redeclare）→ `Real x(start = 1.0);`。
- `getConstrainingMod(element, parent, outerMod)`（:2066-2085）：从**被替换的原元素**取 `SCodeUtil.getConstrainingMod`，`merge(outerMod, ccMod)`（outer 在上）。用于 redeclare 时把**原元素的 constrainedby 修饰**继续作用到新元素上（`instComponent` :1904、`redeclareClasses` :1642）。

合并次序（探针 **P63**，`replaceable Real x constrainedby Real(start=1)`，外部 `a(redeclare Real x(min=1))`）→ `Real a.x(min = 1.0, start = 1.0);`：redeclare 自己的 `min=1` 在上，原 constrainedby 的 `start=1` 垫底。redeclare 元素**自己也带** `constrainedby`（`ConstrainingClass3`：`redeclare replaceable Real x(min=1) constrainedby Real(start=2)`）时，新的约束类修饰 `start=2` 覆盖旧的 `start=1` → `x(min=1, start=2)`。约束类**只约束类型**、其修饰参与合并；MLS 语义"约束类是 redeclare 合法性的类型上界"在 NF 的这条压平路径里**没有做子类型校验**（只在 `redeclareEnum`/属性前缀处查），这点记入 §未确认。

**6.4 被 redeclare 元素"原有修饰"的三种去向**

以 **P64**（= `RedeclareMod3`）为证：`B` 里 `replaceable A a(x=1)`；`C: extends B(redeclare replaceable A a(y=2))`；顶层 `extends C(redeclare replaceable A a(z=3))` → 结果 `a.x=1, a.y=0, a.z=3`。

| 原修饰 | 来源槽位 | 去向 | 机制 |
|---|---|---|---|
| `x=1`（写在**原 replaceable 声明**上） | `innerMod` | **保留** | `instComponent` :1900 先用 innerMod 实例化原元素，`x=1` 落进去；后续 :1905 `merge(cc_mod, innerMod)` 把它垫到 redeclare 新类型下 → 新类型没覆盖 x 就沿用 |
| `y=2`（写在**中间那层 redeclare** 上） | 中间 REDECLARE 的 outerMod | **丢失** | 顶层 redeclare 作 outer 与中间 redeclare 作 inner 合并时走 `merge` 的 `(REDECLARE, REDECLARE)` 分支（`NFModifier.mo:431-432`）：**外层 redeclare 整个盖掉内层 redeclare**（只继承其 constrainingMod，:427-429），中间那层挂着的 `y=2` 随之被丢 |
| `z=3`（写在**最外层 redeclare** 上） | 最外 REDECLARE 的 outerMod | **保留** | 它是胜出的那个 redeclare，其 outerMod 作用到最终新元素 |

要点：**跨层的多个 redeclare 不叠加**，最外层赢者通吃（唯一继承的是 constrainingMod）；只有写在最初 replaceable 声明上的修饰（innerMod）能穿过所有 redeclare 活到最后。这与普通修饰"外层优先但各子修饰并集"（§2.1）**不同**——redeclare 是整体替换。

**6.5 报错清单（本节相关，详见 §8）**

| 消息 | 触发 | 探针/用例 |
|---|---|---|
| `REDECLARE_NON_REPLACEABLE`（41） | redeclare 换类型但原元素非 replaceable | P62 / `RedeclareNonReplaceable1` |
| `INVALID_REDECLARE_AS`（40） | 类↔组件 redeclare 错配 | `RedeclareClassComponent` / `RedeclareComponentClass` |
| `REDECLARE_MISMATCHED_PREFIX`（327） | 提升 variability、改 in/out 等前缀 | P68 / `RedeclareInvalidVariability1` |
| `REDECLARE_NONEXISTING_ELEMENT`（228） | `redeclare` 元素找不到同名被继承元素 | P69 / `RedeclareElementMissing1` |
| `CLASS_EXTENDS_TARGET_NOT_FOUND`（579） | `redeclare model extends X` 的 X 不在继承类里 | `ClassExtendsMissing1` |
| `REDECLARE_CONDITION`（325） | redeclare 带条件 | `RedeclareElementCondition1..3` |
| `REDECLARE_ENUM_NON_SUBTYPE`（356）/`REDECLARE_CLASS_NON_SUBTYPE`（355） | 枚举/类 redeclare 非子类型 | `RedeclareEnum3..5` |
| `MISSING_REDECLARE_IN_CLASS_MOD`（56） | 给类修饰但漏 `redeclare`（`a(In = In2)`） | P23 |
| `INVALID_REDECLARE_IN_BASIC_TYPE`（215） | redeclare 内建类型的属性（`x(redeclare Real start)`） | — |
| `CLASS_EXTENDS_MISSING_REDECLARE`（299，Warning） | `model extends X` 漏 redeclare 前缀，NF 照 redeclare 处理 | `NFInst.mo:582-583` |

### 7. 条件组件

**7.1 在哪一步被删：Typing 判定 + Flatten 真正删除**

| 阶段 | 位置 | 动作 |
|---|---|---|
| Inst | `instComponentDef`（`NFInst.mo:1955`） | `condition := Binding.fromAbsyn(component.condition, false, false, parent, info)` → RAW_BINDING，作用域 = 组件所在类；存进 `Component.COMPONENT.condition`（`NFComponent.mo:77`）。**组件照常完整实例化**（类型、修饰、子组件），此时不知道条件真假。`instComponentExpressions` :2711 把它 inst 成 UNTYPED。 |
| Typing | `typeComponent`（`NFTyping.mo:467-472`）→ `typeComponentCondition(c.condition, context, evaluate = true)`（:1202-1254） | 类型必须匹配 Boolean，否则 `IF_CONDITION_TYPE_ERROR`（:1225-1229）；variability > parameter → `COMPONENT_CONDITION_VARIABILITY`（:1231-1235）；**尝试** `Ceval.evalExp`，失败不报错（checkpoint 回滚，:1239-1248）。`is_deleted := Expression.isFalse(...)`；若为 false 条件，**不再给子组件做 typing**（:479-487）。之后 `typeComponentBinding`（:1016-1019）、`typeComponentSections`（:3019）都用 `Component.isDeleted`（`NFComponent.mo:957-969`：condition 已 typed 且表达式是 `false`）跳过。 |
| Typing（绑定出错时） | `typeComponentBinding`（:1042-1049） | 组件**带条件**时，绑定的 typing/类型检查错误不立即报，存成 `INVALID_BINDING(binding, errors)`。 |
| Flatten | `flattenComponent`（`NFFlatten.mo:539-545`）→ `isDeletedComponent(condition, prefix)`（:591-622） | 在**带前缀的上下文**里重新 `flattenBinding` + `Ceval.evalExp` 条件（数组实例每个元素各判一次）；得不到 Boolean 字面量 → `CONDITIONAL_EXP_WITHOUT_VALUE`（:614-617）；为 false → `deleteComponent`（:624-633）：把带前缀的 cref 放进 `deletedVars` 集合，**不产生任何变量**（子组件也不展开）。 |
| Flatten（connect） | `resolveConnections` → `Connections.collectConnections(flatModel, isDeletedCref(deletedVars))`（HEAD :2458-2459；1.25.0 同机制，函数名 `isDeletedConnector`，`v1.25.0:NFFlatten.mo:417/2390`）；`NFConnections.mo:185` | 任一端（或其任一前缀）在 `deletedVars` 里的 connect **整条丢弃**。 |

**7.2 探针**

- P50：`C c1 if on;`（on=false）、`connect(c1, c3); connect(c2, c3);` → `c1.*` 与 `connect(c1, c3)` 消失，其余照常；`Real x(start = 1) if on` 也消失。副作用：被条件用到的参数变成 `final parameter Boolean on = false;`（结构参数被求值并标 final，R14 范畴）。
- P58：条件依赖外层修饰 `A a1(on = false)`、`A a3[2](on = {true, false})` → `a1.x` 被删、`a3[1].x` 保留、`a3[2].x` 被删（Flatten 阶段按元素判定的证据）。
- **被删组件上的修饰**：
  - P53：`A a(q = 2) if on`（q 不存在，on=false）→ 仍报 `Modified element q not found in class A.`——`applyModifier` 发生在 Inst 阶段，与条件无关。
  - P51：`A a(p = "str") if on; Real y = true if on;`（on=false）→ **不报错**，输出只有 `on`：子组件 typing 被跳过 + 自身绑定错误被 `INVALID_BINDING` 延迟，组件被删后错误被丢弃。P52（on=true）→ 照常报 `Type mismatch in binding y = true, expected subtype of Real, got type Boolean.`。用例 `ConditionInvalidBinding1/2.mo`。
- **条件求值失败**：用例 `ConditionInvalid1.mo` → `The conditional expression b could not be evaluated.`；P56（`parameter Boolean on;` 无值）→ 先撞上 `Parameter on has neither value nor start value, and is fixed during initialization (fixed=true).`（结构参数求值的报错，先于 CONDITIONAL_EXP_WITHOUT_VALUE）。
- **条件类型/可变性**：P57 → `Type error in conditional 'r'. Expected Boolean, got Real.`；P55 → `Component condition must be parameter or constant expression (in b).`。模板里另有 `CONDITION_TYPE_ERROR`（`Util/Error.mo:513`），NF 这条路径没用它。
- **在非 connect 处引用条件组件（1.25.0 与 HEAD 不同！）**：
  - **1.25.0**：`typeCref2` 里只要 cref 的节点 `Component.hasCondition`（不管真假）且不在 connect 上下文 → `Error.addStrictMessage(CONDITIONAL_COMPONENT_INVALID_CONTEXT)`（`v1.25.0:NFTyping.mo:2018-2023`）：默认是 **Warning**，`--strict` 才是 Error。探针 P54（on=false，`y = x`）：输出里**方程 `y = x` 原样保留**（x 已无声明），外加 `Warning: Conditional component 'x' is used in a non-connect context.`；`--strict` 下为 `Error: Conditional component 'x' is used in a non-connect context.`（`T_ConditionInvalidContext1_strict.out`）。
  - **HEAD**：该检查已删（`CONDITIONAL_COMPONENT_INVALID_CONTEXT` 在 HEAD `NFFrontEnd/` 无引用），改为压平后 `checkDeletedVarRefsInExp_traverser`（`NFFlatten.mo:3297-3319`）→ `INVALID_DELETED_COMPONENT_CONTEXT`：`'x' refers to a component with a false condition.`。HEAD 的 `ConditionInvalidContext1.mo` 连**用例源码**都改了（1.25.0 版是 `Real x if true;` + `--strict` 注解）。⇒ ymc 以 1.25.0 为 oracle 时，这组用例要用 `git show v1.25.0:testsuite/…` 的版本，记入分歧账本。
- redeclare 不允许带条件：`REDECLARE_CONDITION`（`NFInst.mo:2210-2215`）；被 redeclare 的组件**沿用原组件的条件**（:2217）。用例 `RedeclareElementCondition1..3.mo`。

### 8. 错误文案清单

模板取自 `Util/Error.mo`（括号内为消息号，即 `MESSAGE(n,…)`）；`%s` 按触发点实参顺序。多 info 的排版规则见 §2.2（`addMultiSourceMessage`：最后一个 info 是 `Error` 本体，之前的每个 info 各出一行 `Notification: From here:`）。omc 原文均为 1.25.0 实跑（探针 `Pxx` 见 `probe-r15/`；`用例` 指 `scodeinst/<名>.mo` 的 Result 块）。

**8.1 修饰相关**

| 消息（号，severity） | 模板 | 触发点 | omc 原文（Error 行） |
|---|---|---|---|
| `FINAL_COMPONENT_OVERRIDE`（222，E） | `Trying to override final element %s with modifier '%s'.` | `checkFinalOverride`（`NFModifier.mo:696-714`），`merge` 检出内层 final | P06：`… Error: Trying to override final element p with modifier '= 3'.` |
| `DUPLICATE_MODIFICATIONS`（103，E） | `Duplicate modification of element %s on %s.` | `mergeLocal`（`NFModifier.mo:746-754`），同一修饰列表内重名且均有绑定 | P09：`… Error: Duplicate modification of element p on component a.` |
| `MULTIPLE_MODIFIER`（516，E） | `Multiple modifiers in same scope for element %s.` | `NFModifier` 同名冲突另一路径（`ModTable.join` 无合法解） | 用例 `DuplicateMod*` 少数分支 |
| `MISSING_MODIFIED_ELEMENT`（214，E） | `Modified element %s not found in class %s.` | `applyModifier`（`NFInst.mo:1611-1615`）/ 内建 FLAT_TREE 分支 | P25：`… Modified element y not found in class A.`；P26：`… Modified element strat not found in class Real.` |
| `MODIFIER_NON_ARRAY_TYPE_ERROR`（348，E） | `Non-array modification '%s' for array component '%s', possibly due to missing 'each'.` | `NFTypeCheck.mo:3105-3107`（类型检查阶段） | P14：`… Non-array modification '1' for array component 'p', possibly due to missing 'each'.` |
| `VARIABLE_BINDING_DIMS_MISMATCH`（347，E） | `Type mismatch in binding '%s = %s', expected array dimensions %s, got %s.` | `NFTypeCheck.mo:3115-3119` | P16：`… expected array dimensions [3], got [2].` |
| `NF_MODIFY_PROTECTED`（396，E） | `Protected element '%s' may not be modified, got '%s'.` | `applyModifier`（`NFInst.mo:1578-1587`），非 extends 上下文改 protected | P24：`… Protected element 'p' may not be modified, got 'p = 3'.`；P38 同（继承后被外部改） |
| `OUTER_ELEMENT_MOD`（307，E） | `Modifier '%s' found on outer element %s.` | `checkOuterComponentMod`（`NFInst.mo:2238-2260`）/`applyModifier` :1589-1599 | 用例 `OuterMod*` |
| `INVALID_REDECLARE_IN_BASIC_TYPE`（215，E） | `Invalid redeclaration of %s, attributes of basic types may not be redeclared.` | `instBuiltinAttribute`（`NFInst.mo:2729-2734`），内建属性上写 `redeclare` | — |
| `UNBOUND_PARAMETER_WITH_START_VALUE_WARNING`（499，W） | `Parameter %s has no value, and is fixed during initialization (fixed=true), using available start value (start=%s) as default value.` | `updateParameterBinding`（`NFInst.mo:2357-2398`） | P18：`… using available start value (start={5, 6}) as default value.`（名 `b.p` 不带下标、start 是原始 Absyn） |

**8.2 继承（extends / 重复元素）相关**

| 消息（号，severity） | 模板 | 触发点 | omc 原文 |
|---|---|---|---|
| `LOOKUP_BASECLASS_ERROR`（39，E） | `Base class %s not found in scope %s.` | `expandExtends`（`NFInst.mo:780-781`）→ `NFLookup.mo:94-112` | P41：`… Base class Missing not found in scope P41_BaseNotFound.` |
| `EXTENDS_LOOP`（19，E） | `extends %s causes an instantiation loop.` | `checkExtendsLoop`（`NFInst.mo:807-845`） | 用例 `ExtendsLoop1`：`extends ExtendsLoop1 causes an instantiation loop.` |
| `REPLACEABLE_BASE_CLASS`（172，E） | `Class '%s' in 'extends %s' is replaceable, the base class name must be transitively non-replaceable.` | `checkReplaceableBaseClass`（`NFInst.mo:847-888`） | 用例 `ExtendReplaceable1` |
| `FOUND_OTHER_BASECLASS`（306，E） | `Found other base class for extends %s after instantiating extends.` | `modifyExtends` 二次查基类（`NFInst.mo:1400-1406`） | 用例 `ExtendInherited1` |
| `BUILTIN_EXTENDS_INVALID_ELEMENTS`（293，E） | `A class extending from builtin type %s may not have other elements.` | `checkBuiltinTypeExtends`（`NFInst.mo:908-921`） | 用例 `ExtendsBuiltin*` |
| `DOUBLE_DECLARATION_OF_ELEMENTS`（161，E） | `An element with name %s is already declared in this scope.` | `addLocalElementConflict`（`NFClassTree.mo:1654-1677`），两个**本地**同名 | P39：`… An element with name x is already declared in this scope.` |
| `DUPLICATE_ELEMENTS_NOT_IDENTICAL`（90，E） | `Duplicate elements (due to inherited elements) not identical:\n  first element is:  %s\n  second element is: %s` | `checkDuplicates`（`NFInst.mo:1334-1380`）→ `InstNode.checkIdentical` | P32：`first element is:  Real x = 1` / `second element is: Real x = 2`（比较发生在修饰合并后，P35） |

**8.3 redeclare 相关**

| 消息（号，severity） | 模板 | 触发点 | omc 原文 |
|---|---|---|---|
| `REDECLARE_NON_REPLACEABLE`（41，E） | `Redeclaration with a new type requires '%s' to be replaceable.` | `redeclareComponent`（`NFInst.mo:2191-2197`），换类型且原非 replaceable | P62：`… Redeclaration with a new type requires 'a' to be replaceable.` |
| `INVALID_REDECLARE_AS`（40，E） | `Invalid redeclaration of %s %s as %s.` | `redeclareClass`（:1729-1733）/`redeclareComponent`（:2174-2179），类↔组件错配 | 用例 `RedeclareClassComponent`：`… Invalid redeclaration of class M as component.` |
| `REDECLARE_MISMATCHED_PREFIX`（327，E） | `Invalid redeclaration '%s %s', original element is declared '%s'.` | `printRedeclarePrefixError`（`NFAttributes.mo:394-397`），提升 variability / 改 in-out 等 | P68：`… Invalid redeclaration 'parameter x', original element is declared 'constant'.` |
| `REDECLARE_NONEXISTING_ELEMENT`（228，E） | `Illegal redeclare of element %s, no inherited element with that name exists.` | `getRedeclareChain`（`NFClassTree.mo:2301`），`redeclare` 元素无对应被继承元素 | P69：`… Illegal redeclare of element x, no inherited element with that name exists.` |
| `CLASS_EXTENDS_TARGET_NOT_FOUND`（579，E） | `Base class targeted by class extends %s not found in the inherited classes.` | `getRedeclareChain`（`NFClassTree.mo:2298`），`redeclare model extends X` 的 X 不在继承类里 | 用例 `ClassExtendsMissing1` |
| `MISSING_REDECLARE_IN_CLASS_MOD`（56，E） | `Missing redeclare keyword on attempted redeclaration of class %s.` | `instClass`（`NFInst.mo:1116-1120`），给类修饰但漏 `redeclare` | P23：`… Missing redeclare keyword on attempted redeclaration of class In.` |
| `REDECLARE_CONDITION`（325，E） | `Invalid redeclaration of %s, a redeclare may not have a condition attribute.` | `redeclareComponent`（`NFInst.mo:2208-2213`） | 用例 `RedeclareElementCondition*` |
| `REDECLARE_ENUM_NON_SUBTYPE`（356，E）/`REDECLARE_CLASS_NON_SUBTYPE`（355，E） | `Redeclaration of enumeration '%s' is not a subtype …` / `Redeclaration of %s '%s' is not a subtype of the redeclared element.` | `redeclareEnum`（`NFInst.mo:1837/1849`） | 用例 `RedeclareEnum3..5` |
| `CLASS_EXTENDS_MISSING_REDECLARE`（299，W） | `Missing redeclare prefix on class extends %s, treating like redeclare anyway.` | `NFInst.mo:582-583`，`model extends X` 漏 redeclare | 用例 `ClassExtends*`（Warning，不阻断） |

**8.4 条件组件相关**

| 消息（号，severity） | 模板 | 触发点 | omc 原文 |
|---|---|---|---|
| `IF_CONDITION_TYPE_ERROR`（10，E） | `Type error in conditional '%s'. Expected Boolean, got %s.` | `typeComponentCondition`（`NFTyping.mo:1225-1229`） | P57：`… Type error in conditional 'r'. Expected Boolean, got Real.` |
| `COMPONENT_CONDITION_VARIABILITY`（100，E） | `Component condition must be parameter or constant expression (in %s).` | `typeComponentCondition`（`NFTyping.mo:1231-1235`） | P55：`… Component condition must be parameter or constant expression (in b).` |
| `CONDITIONAL_EXP_WITHOUT_VALUE`（137，E） | `The conditional expression %s could not be evaluated.` | `isDeletedComponent`（`NFFlatten.mo:614-617`），压平期条件求不出常量 | 用例 `ConditionInvalid1`：`… The conditional expression b could not be evaluated.`（P56 会先撞结构参数无值错） |
| `CONDITIONAL_COMPONENT_INVALID_CONTEXT`（357，**W**，`--strict` 为 E）**仅 1.25.0** | `Conditional component '%s' is used in a non-connect context.` | `v1.25.0:NFTyping.mo:2018-2023`（`addStrictMessage`） | P54：`… Warning: Conditional component 'x' is used in a non-connect context.` |
| `INVALID_DELETED_COMPONENT_CONTEXT`（420，E）**仅 HEAD** | `'%s' refers to a component with a false condition.` | `checkDeletedVarRefsInExp_traverser`（HEAD `NFFlatten.mo:3297-3319`） | HEAD 用例（源码已改，见 §7 分歧） |

**说明**：`CONDITIONAL_COMPONENT_INVALID_CONTEXT`↔`INVALID_DELETED_COMPONENT_CONTEXT` 是同一功能在 1.25.0 与 HEAD 的两种实现，触发时机（Typing vs 压平后）与默认严重度都不同——ymc 以 1.25.0 为 oracle，取 Warning 版并按 `--strict` 升 Error（§7 已记入分歧账本）。

## 探针

统一命令 `timeout 120 omc <file>.mo`（打印文件里最后一个顶层类的压平结果）。全部 `.mo`/`.out` 在 `probe-r15/`。下列 13 个覆盖优先级、each/切分、start 借用、extends 链/重复、redeclare 类与组件、constrainedby、条件组件。

**P04 — 优先级阶梯（实例修饰 > extends 修饰 > 声明默认）**
```modelica
model A parameter Real p=1; parameter Real q=1; parameter Real r=1; parameter Real s=1; end A;
model B extends A(p=2, q=2, r=2); end B;
model C B b(p=3, q=3); end C;
model P04_Priority C c(b(p=4)); end P04_Priority;
```
→ `c.b.p=4.0`（外层实例）`c.b.q=3.0`（C 层实例）`c.b.r=2.0`（B 的 extends）`c.b.s=1.0`（声明默认）。

**P05 — 同名子修饰递归合并，绑定与属性各自"外层优先"**
```modelica
model A Real x(start=1, fixed=true, min=-1)=0; end A;
model B A a(x(start=2)); end B;
model P05_SubModMerge B b(a(x(min=-5)=3)); end P05_SubModMerge;
```
→ `Real b.a.x(min = -5.0, start = 2.0, fixed = true) = 3.0;`（min 取最外、start 取中层、fixed 取声明；绑定取最外 `=3`）。

**P12 — 数组组件：非 each 绑定按元素切，each 原样复制**
```modelica
model A parameter Real p=0; parameter Real q=0; Real x(start=0); end A;
model P12_ArraySplit parameter Real v[3]={10,20,30}; A a[3](p={1,2,3}, each q=5, x(start=v)); end P12_ArraySplit;
```
→ `a[i].p={1,2,3}[i]`、`a[i].q=5.0`（each，全同）、`a[i].x(start=v[i])`（cref 也按元素切）。

**P18 — 无绑定 parameter 借用 start（`updateParameterBinding`）+ each 展开**
```modelica
model A parameter Real p(start=1); parameter Real w[2](each start=3); end A;
model P18_ParamStartBinding A a; A b[2](p(start={5,6})); end P18_ParamStartBinding;
```
→ `a.p(start=1.0)=1.0`、`b[1].p(start=5.0)=5.0`、`b[2].p(start=6.0)=6.0`；每个声明报一条 Warning，名不带下标、start 打印原始 Absyn：`Parameter b.p … using available start value (start={5, 6}) as default value.`

**P20 — extends 链：越靠近派生类的 extends 修饰赢**
```modelica
model A parameter Real p=1; parameter Real q=1; parameter Real r=1; end A;
model B extends A(p=2, q=2); end B;
model C extends B(p=3); end C;
model P20_ExtendsChain extends C; C c(r=9); end P20_ExtendsChain;
```
→ `p=3.0`（C 的 extends 盖 B）`q=2.0`（B）`r=1.0`（声明）；组件 `c` 另加实例修饰 `c.r=9.0`。

**P32 — 继承重复元素不等价 → 报错（比较发生在修饰合并之后）**
```modelica
model A Real x=1; end A;   model B Real x=2; end B;
model P32_DupNotIdentical extends A; extends B; end P32_DupNotIdentical;
```
→
```
[…:2:3-2:13:writable] Notification: From here:
[…:5:3-5:13:writable] Error: Duplicate elements (due to inherited elements) not identical:
  first element is:  Real x = 1
  second element is: Real x = 2
```

**P60 — 组件 redeclare 换成子类型**
```modelica
model AA Real x=1.0; end AA;
model BB extends AA; Real y=2.0; end BB;
model Wrap replaceable AA c; end Wrap;
model P60_RedeclareComp Wrap w(redeclare BB c); end P60_RedeclareComp;
```
→ `Real w.c.x = 1.0; Real w.c.y = 2.0;`（多出继承自 BB 的 `y`）。

**P61 — 类 redeclare（`redeclare model B = C`）整体替换**
```modelica
model A replaceable model B Real x; end B; B b; end A;
model C Real x=1.0; Real z=5.0; end C;
model P61_RedeclareClass A a(redeclare model B = C); end P61_RedeclareClass;
```
→ `Real a.b.x = 1.0; Real a.b.z = 5.0;`（原 B 定义被 C 替换）。

**P62 — 对非 replaceable 换类型 → 报错**
```modelica
model A Real x=1.0; end A;
model B extends A; Real y=3.0; end B;
model Wrap A a; end Wrap;
model P62_RedeclareNonReplaceable extends Wrap(redeclare B a); end P62_RedeclareNonReplaceable;
```
→ `Error: Redeclaration with a new type requires 'a' to be replaceable.`

**P63 — constrainedby 修饰与 redeclare 修饰合并（redeclare 在上）**
```modelica
model A replaceable Real x constrainedby Real(start=1.0); end A;
model P63_ConstrainingMerge A a(redeclare Real x(min=1.0)); end P63_ConstrainingMerge;
```
→ `Real a.x(min = 1.0, start = 1.0);`（`min` 来自 redeclare、`start` 来自约束类；`ConstrainingClass3` 加 `constrainedby Real(start=2)` 则 start 变 2）。

**P64 — 跨层多个 redeclare 不叠加，最外层赢；仅原 replaceable 声明上的修饰穿透**
```modelica
model A Real x=0.0; Real y=0.0; Real z=0.0; end A;
model B replaceable A a(x=1.0); end B;
model C extends B(redeclare replaceable A a(y=2.0)); end C;
model P64_OrigModToRedeclare extends C(redeclare replaceable A a(z=3.0)); end P64_OrigModToRedeclare;
```
→ `a.x=1.0`（原声明 innerMod 穿透）`a.y=0.0`（**中层 redeclare 的 y=2 丢失**）`a.z=3.0`（最外 redeclare）。

**P50 — 条件组件 false：变量与涉及它的 connect 一起消失**
```modelica
connector C Real e; flow Real f; end C;
model P50_CondBasic
  parameter Boolean on=false; parameter Boolean on2=true;
  C c1 if on; C c2 if on2; C c3; Real x(start=1) if on;
equation connect(c1,c3); connect(c2,c3); end P50_CondBasic;
```
→ 输出无 `c1.*`、无 `x`、无 `connect(c1,c3)` 生成的方程；`c2/c3` 及其 connect 照常。`on` 被求值成 `final parameter Boolean on = false;`。

**P58 — 条件来自外层修饰，数组每个元素各自判定（压平阶段）**
```modelica
model A parameter Boolean on=true; Real x=1 if on; Real y=2; end A;
model P58_CondFromMod A a1(on=false); A a2; A a3[2](on={true,false}); end P58_CondFromMod;
```
→ `a1.x` 删、`a2.x` 留、`a3[1].x` 留、`a3[2].x` 删（同一数组内逐元素删除，证明删除发生在带前缀展开之后）。

## 未确认 / 待验证

- **1.25.0 vs HEAD 分歧（已确认，需入账本）**：非 connect 处引用条件组件——1.25.0 `CONDITIONAL_COMPONENT_INVALID_CONTEXT`（Warning，`--strict` 升 Error，Typing 期，`v1.25.0:NFTyping.mo:2018-2023`）；HEAD `INVALID_DELETED_COMPONENT_CONTEXT`（Error，压平后 `NFFlatten.mo:3297-3319`）。HEAD 连 `ConditionInvalidContext1.mo` 用例源码都改了。ymc 以 1.25.0 为准，这组用例须取 `git show v1.25.0:testsuite/…` 版本。
- **constrainedby 的子类型校验缺失**：NF 这条压平路径**没有**检查 redeclare 新类型是否为约束类的子类型（约束类修饰只是参与合并，见 §6.3）；MLS 3.6 §7.3.2 要求约束类是类型上界。是 NF 有意省略还是别处（typing）补做，未在本轮定位——待查 `NFTyping` 是否对 redeclare 结果做 subtype 断言。
- **`DUPLICATE_MODIFICATIONS` 两条 info 的先后**取决于 AVL 插入时谁 new 谁 old 及递归层次（P09 与 P10 顺序相反，§2.3）——ymc 做 L1 文本比对时须逐例对齐，不能假定固定顺序。
- **`MULTIPLE_MODIFIER`（516）** 的确切触发路径未用探针复现（§8 列出模板但未跑出原文），只从 grep 推断是 `ModTable.join` 无合法解的分支；待补探针。
- **`redeclareEnum` 的 `enumeration(:)` 通配**与普通枚举子类型判定（`RedeclareEnum*` 用例）只读了代码分支，未逐例实跑对齐输出。
- 修饰里表达式**跨 redeclare 的作用域**（redeclare 新类型内部字段引用原声明处名字时）只在 redeclare12（`final par = localPar`）一例上验证 binding 作用域随 `propagateRedeclaredMod` 搬运，边界情形未穷尽。

## 对 ymc 的建议

**Modifier 的表示（arena + Id）**
- 三态与 OMC 对齐：`Nomod` / `Mod{name, final, each, binding: BindingId, subs}` / `Redeclare{final, each, elem: InstNodeId, innerMod, outerMod, ccMod, propagatedSubs}`。全部存进 `ModifierArena`，对外传 `ModifierId`（32 位）。`Nomod` 用固定哨兵 Id（如 0），省一次分配、便于短路。
- **子修饰容器：用"按 name 排序的小 vector `<(InternedStr, ModifierId)>`"，不要 AVL、也不要 hash map**。理由：① 实测子修饰个数几乎总是个位数，线性/二分足够，缓存友好；② **要主动选定遍历序**——OMC 因 AVL 字典序导致"多个不存在元素时报字典序最小者"（P30）、`DUPLICATE_MODIFICATIONS` 两 info 顺序不稳定（未确认节）。ymc 若照 L1 文本比 OMC，就**复刻字典序**（排序 vector 天然给出）；若想更友好可留书写序但要清楚这会与 OMC 输出错位，须记入分歧账本。二分查 name、合并两个有序 vector 是线性归并。
- **合并产生新节点，绝不原地改**：`merge(outer, inner) -> ModifierId` 在 arena 里 append 新节点，子修饰未变的部分**直接复用旧 Id**（结构共享）。这是硬约束——同一个修饰会被传播到数组的每个元素、下传到多层类，`InstNode` 克隆时也会共享它；任何原地写都会污染共享者。immutable + arena 让"克隆节点先共享同一 ModifierId，直到它自己累积修饰再分裂"成为免费午餐。`final` 只查 inner、只在 `(Mod,Mod)` 分支查；结果 name/final/each/info 取 outer——照抄 §2.1 表，别自作聪明。

**修饰作用域的记录方式**
- **不建全局修饰环境，也不给 Modifier 单独存 scope**。作用域记在 **binding 上**（`RawBinding{ absynExpr, scope: InstNodeId, subs, eachType, source }`），子修饰共享父 binding 传下来的 scope 语义即"表达式在被写下的那个类里解析"。表达式保持未解析（Absyn/你的 AST）直到 `instBinding` 阶段才 `instExp(expr, scope)`。
- `ModifierScope`（Component/Class/Extends + name）只做两件事：错误文案后半句、binding 的 `Source` 选 `Type` 还是 `Binding`。**别把它当查找作用域**用——这是 OMC 里最容易误解的点。

**传播时机与数组切分**
- 复刻 `SPLIT_PROXY`/`SPLIT_INDEX` 机制，**不要**存 parents 列表。传播是"惰性、逐元素穿越"：`instComponentDef`/`instClassDef` 里，修饰每穿过一个元素就对**其子修饰**（非 each）在 binding 尾部追加 `SplitProxy(origin, parent)`；`mod` 自身顶层 binding 属于上一层、不追加。each 子修饰整棵跳过（这是 each 的**唯一**实现点，合并阶段对 each 完全无感）。typing 期 proxy→index 展开、压平期按 `indexedPrefix` 替换实际下标——三阶段职责别混。
- 传播只碰子修饰这一条要刻进函数签名（`propagate(mod, origin, parent)` 只递归 subs），否则很容易顺手把顶层 binding 也切了，导致数组组件自身绑定被误切。

**与 InstNode 克隆的关系**
- 因为 Modifier immutable，`ClassTree.instantiate` 克隆节点时只需**拷 ModifierId**（浅拷），无深拷成本；节点后续 `mergeModifier` 时才在 arena 里生成新 Id 覆盖自己那份。
- redeclare 要复刻 `copyInstancePtr` + `RedeclaredComp/Class` nodeType 保留 `originalNode` 的做法：redeclare 节点顶替原节点的实例指针，但保留原节点供作用域回溯（`enclosingScope` 的 `ignoreRedeclare` 开关）。**跨层 redeclare 用"外层整体替换内层、仅继承 ccMod"的语义**（§6.4），不要试图把多个 redeclare 的修饰并起来——那会得到与 OMC 不一致的 `a.y`。
- 建议给 `ModifierArena` 配一个 `dump(ModifierId)` 还原成 `name(subs) = binding` 文本的调试器（对应 `Modifier.toString`），apply/merge/propagate 每步可 dump——这正是复刻 OMC 输出、定位切分下标错位时最省命的探针。
