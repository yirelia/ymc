# R12 OMC 新前端（NF）名字查找

> 研究对象：`C:\dev\OpenModelica`（HEAD 3ceb0c9d06）`OMCompiler/Compiler/NFFrontEnd/`；oracle = 安装版 omc 1.25.0。
> 约定：锚点写作 `文件:行`（未写目录的均相对 `OMCompiler/Compiler/NFFrontEnd/`，行号为 HEAD 3ceb0c9d06，引用前已 grep/Read 复核）；探针输出为 omc 1.25.0 实跑原文。用例名指 `testsuite/flattening/modelica/scodeinst/<名>.mo`。
> 状态：**增量写作中**（每完成一节即追加；未完成的节标注"待写"）。

## 结论摘要

1. 查找有**两族入口**共用一套状态机：**Path 族**（类型名/基类名/import 名，输入 `Absyn.Path`）和 **Cref 族**（表达式里的变量/函数名，输入 `Absyn.ComponentRef`）；入口的差别只有三点——失败报哪条 "not found"、结束用哪个 `assertXxx` 验收、起始作用域（§1）。
2. **保留内建名先短路，不看任何作用域**：`Real/Integer/Boolean/String`（Path 版）、`time`/`Integer`/`String`（Cref 版）在进主循环前就返回内建节点，因此**不可被用户遮蔽**（`NFLookup.mo:655-674,800-833`、§2.1-1、§6）。
3. **单层查找是一次 map 查询**：`LookupTree.get(name)` → `CLASS(i)/COMPONENT(i)/IMPORT(i)`。**本地元素、继承元素、import 名同处一张 LookupTree**，优先级在**建表期**由冲突函数定死（本地 > 继承 > import；限定 import > `.*`），查找期不按序试多张表（§2.1-2）。
4. 简单名逐层向外（`lookupSimpleName`/`lookupSimpleCref`，最多 256 层）：本层查不到 → 若本层 encapsulated 直接跳 top 且此后只认内建；否则 → 外层 `parentScope`；到 top 仍无 → 自动装库再试一次（§2.1 主循环）。
5. **"外层作用域"分两条父链**：`parentScope`（词法外层，查名字向外走用它；组件取其**类型定义处**的外层）与 `instanceParent`（实例树父亲，resolveInner/inner-outer 用它）——**不可合并**，混用会让继承元素在错作用域查名（探针 p26、§2.2）。
6. **继承元素仍在基类的词法环境里查名**：extends 拿来的基类节点保留自己原来的 `parentScope`（`NFInst.mo:791` 只改 nodeType），所以基类里的名字看不到派生类的同名元素（用例 Extends4、探针 p26）。
7. **Cref 版多一条**：在外层（非 for 隐式作用域）找到的变量必须是 constant，否则 `NON_CONSTANT_IN_ENCLOSING_SCOPE`（parameter 也不行，§2.3）。
8. **修饰的两侧在不同作用域查**：`A a(p = q)` 的左名 `p` 在被修饰组件的**类型**里查，右表达式 `q` 在**写修饰的类**里查（`Binding.fromAbsyn` 记下 scope，§2.4、探针 p05a/p05b）。
9. **复合名 A.B.C**：首段按简单名查（**不过 `next`、不查 protected**），后续段在上一段元素内部逐段下行，每段过 `LookupState.next`；合法形态只有"类…类.组件…组件"与"组件…组件[.类.函数]"两种（§3）。
10. **LookupState 是二维转移表 + `assertXxx` 验收**：状态机内部报的错（protected/partial/经组件找到类/import 在中段）发生在 not found **之前或代替**它；`assertState` 报的错发生在成功之后。**protected 违规是两条错误**（声明处 + 使用处），普通 not found 是一条（§1 要点、§3、§7）。
11. **import**：目标路径**永远从 top 查**（所以报错 scope 恒 `<top>`）；`.*` 在 `init_imports` 时立即解析（不过滤 protected），限定/重命名延迟到用到再解析；**import 不被继承**；本地遮蔽 import 静默（不发 `LOOKUP_SHADOWING`）；同名两条限定 import 或两条 `.*` 冲突用到才报（§5、§7-D）。
12. **内建作用域两层**：(A) 硬编码短路（不看作用域、不可遮蔽）；(B) 顶层内建类装载期预置进 top 的 LookupTree + 每节点 `is_builtin` 位，唯一特殊语义是"encapsulated 之后仍可见"，普通遮蔽规则照常（§6、探针 p46/p04b）。
13. **查找不独立于实例化**：进入复合名中间段的类前要 `instPackage`（三态缓存破环）；`lookup_*` 必须在 inst 层、能回调 `expand`/`instPackage`，纯 SCode 层做不了查找（§5-与实例化关系、§4.2）。
14. HEAD 与 1.25.0 差异仅 4 处小改（许可证头、self-reference 判定、`--loadMissingLibraries`、connect 语境 EMPTY 容错），算法主体/状态机/文案完全一致（§1）。
15. 错误消息 24 条分五组（未找到入口 / 复合名·LookupState / 语义限制 / import 冲突 / extends·装库），位置分三类：not found 用**使用处**、状态机错用**出错那段声明处**、多源错先 "From here:" 再使用处（§7、§4）。
16. 给 ymc（§对 ymc 的建议）：`INode` 双父链、每类一张 `LookupTree`、建表两阶段（partial→expand，组件与继承元素在 expand 后并入）、LookupState 用 enum+switch（分支顺序即语义）、`Diag` 要支持"先记一条、令查找失败、外层补第二条"；**明确不做**：自动装库、`LOOKUP_SHADOWING`、MLS §5.3.2 完整 package 访问检查（各记一条 ADR + 在 diff 归一化里登记）。

## 逐项事实

### 1. NFLookup.mo 入口函数族

**HEAD 与 1.25.0 的差异**（`git diff v1.25.0 HEAD -- NFLookup.mo NFLookupState.mo NFImport.mo`）：只有 4 处小改——(a) 许可证头 +4 行（所以 1.25.0 行号 ≈ HEAD 行号 − 4）；(b) "自引用"判定从 `isSelfReference(node, scope)`（沿 instanceParent 链比对）改成 `lookupSimpleName` 直接回传 `selfReference`（`NFLookup.mo:443,476-481`）；(c) `loadLibrary` 多了 `--loadMissingLibraries` 开关（`:1169`）；(d) `lookupCrefInNode`/`nodeState` 对 connect 语境的 EMPTY_NODE 容错（`:966`、`NFLookupState.mo:477`）。算法主体、状态机、报错文案完全一致，下面的行号一律是 HEAD。

两族入口：**Path 族**（输入 `Absyn.Path`，用于类型名/基类名/import 名，内部走 `lookupName`→`lookupFirstIdent`→`lookupLocalName`）和 **Cref 族**（输入 `Absyn.ComponentRef`，用于表达式里的变量/函数名，内部走 `lookupCref`→`lookupSimpleCref`→`lookupCrefInNode`）。两族共用 `LookupState` 状态机，入口的差别只有三点：**失败时报哪条 "not found" 消息**、**结束状态用哪个 `assertXxx` 验收**、**起始作用域**。

| 入口（`NFLookup.mo` 行） | 输入 | 找不到时的消息 | 结束状态验收 | 起始作用域 / 特殊处理 | 调用点 |
|---|---|---|---|---|---|
| `lookupClassName` `:80-92` | Path | `LOOKUP_ERROR` "Class %s not found in scope %s." | `assertClass`（期望 CLASS；PACKAGE/CLASS/PREDEF_CLASS/FUNC 都算对，`NFLookupState.mo:237-240`） | 给定 scope；`checkAccessViolations` 可关 | 组件类型 `NFInst.mo:2284`（`instTypeSpec`）；根类 `NFInst.mo:369`（RELAXED + 不查 protected） |
| `lookupBaseClassName` `:94-112` | Path | `LOOKUP_BASECLASS_ERROR` "Base class %s not found in scope %s." | `assertClass`（对 `listHead(nodes)`） | 返回**路径上每一段的节点列表**（倒序，`lookupNames` `:620-653`），供 `checkReplaceableBaseClass` 逐段查 replaceable | `extends` `NFInst.mo:781`；短类定义 `class A = B` `NFInst.mo:1037`；extends 二次核对 `NFInst.mo:1396` |
| `lookupComponent` `:114-136` | Cref | `LOOKUP_VARIABLE_ERROR` "Variable %s not found in scope %s." | `assertComponent`（期望 COMP） | 找到 NAME_NODE 也算失败（`:128`）；`fixTypenameState`：找到的类若是 enumeration/Boolean 类型则当 COMP（类型名当表达式用，`:161-177`） | 表达式里的 cref `NFInst.mo:2949`（`instCref`） |
| `lookupConnector` `:138-159` | Cref | 同上 `LOOKUP_VARIABLE_ERROR` | `assertComponent` | 与 lookupComponent 唯一差别：不拒绝 NAME_NODE（expandable connector 的虚成员） | connect 参数 `NFInst.mo:3480` |
| `lookupLocalComponent` `:179-195` | Cref | `LOOKUP_VARIABLE_ERROR`（在 `lookupLocalCref` `:386` 里报） | `assertComponent` | **只查给定 scope，不向外层走**（`lookupLocalSimpleCref`） | external 声明的 output `NFInst.mo:3233` |
| `lookupFunctionName` `:197-219` | Cref | `LOOKUP_FUNCTION_ERROR` "Function %s not found in scope %s." | `assertFunction`（期望 FUNC；CLASS 且是 record/operator/enumeration/Clock 等"可调用类型"也算对，`NFLookupState.mo:161-182,244`） | `fixExternalObjectCall`：ExternalObject 类名改写成其 constructor（`:237-274`） | 函数调用 `NFFunction.mo:344` |
| `lookupFunctionNameSilent` `:221-235` | Cref | 不报错，直接 fail | `isFunction` | 同上 | `NFFunction.mo:318` |
| `lookupImport` `:276-287` | Path | `LOOKUP_IMPORT_ERROR` "Import %s not found in scope %s." | `assertImport`（任何种类都接受，`NFLookupState.mo:362`；但 ERROR 状态和 COMP_CLASS 先匹配先报） | **强制从 `InstNode.topScope(scope)` 起查**（`:284`）——import 名永远是全限定的，所以报错里的 scope 永远是 `<top>` | `NFImport.mo:140,143,166` |
| `lookupCrefWithError` `:289-306` | Cref | 调用者给定 | 无 | 返回 state 给调用者 | （NFFrontEnd 内未见调用） |
| `lookupCref` `:308-348` | Cref | 静默 fail | 无 | Cref 族的核心；`.a.b` 全限定转 topScope（`:339-340`）；`_` 通配返回 PREDEF_COMP | `NFFlatModel.mo:1097` 及上面各入口 |
| `lookupLocalCref` `:350-391` | Cref | `LOOKUP_VARIABLE_ERROR` | 无 | 只查本层 | `NFFunctionInverse.mo:185` |
| `lookupNameWithError` `:569-585` | Path | 调用者给定的消息 | 无（返回 state） | Path 族的"带报错"包装 | `lookupClassName`、`lookupImport` |
| `lookupName` `:587-618` | Path | 静默 fail | 无 | Path 族核心：IDENT → `lookupFirstIdent`；QUALIFIED → 首段 + `lookupLocalName`；FULLYQUALIFIED → topScope 重来 | `NFClassTree.mo:2542`、`NFInst.mo:378` |
| `lookupLocalName` `:676-739` | Path + 起点节点 | —— | 无 | 在"已找到的元素内部"逐段往下查，不向外层走；每段过 `LookupState.next` | `lookupName` |
| `lookupSimpleName` `:438-499` | String | 静默 fail（或 `RECURSION_DEPTH_REACHED`） | 无 | §2 的逐层向外算法（Path 族） | `lookupFirstIdent`；`NFInst.mo:3861` |
| `lookupSimpleCref` `:835-905` | String+下标 | 同上 | 无 | §2 的逐层向外算法（Cref 族；多了 for 迭代器的 IMPLICIT_SCOPE 和"外层找到"标记） | `lookupCref` |
| `lookupLocalSimpleName` `:426-436` | String | fail | 无 | 单层查：`Class.lookupElement` + `resolveInner`；回传 `isImport` | `NFRecord.mo:127`、`NFConnectBreakTree.mo:172` |
| `lookupInner` `:393-424` | outer 节点 | 找不到就 `generateInner` | —— | 沿 **instanceParent**（实例树）向上，不是沿词法作用域；到 root class 为止 | `NFClassTree.mo:2402` |

要点：
- "找不到"消息的两个 `%s` 永远是 **(整个名字的字符串, `InstNode.scopeName(起始 scope)`)**；`scopeName` 只取**作用域自己的短名**（不是全路径；组件作用域取其类型名，`NFInstNode.mo:597-602`），顶层是 `<top>`（`NFInst.mo:474`）。探针 p09a–d、Encapsulated1（"Class A not found in scope B."）。
- 状态机内部报的错（protected、partial、经组件找到类……）发生在"not found"**之前或代替它**：`checkProtection` 先 `addSourceMessage` 再 `fail()`，外层入口 catch 后**再追加**一条 not found，所以 protected 违规是**两条**错误（探针 p08、Visibility3/4）；而 `assertState` 报的错（如 lookup1 的 "was found via a component"）发生在查找成功之后，只有**一条**。

### 2. 简单名查找算法（MLS §5.3.1）

#### 2.1 主循环（`lookupSimpleName` `NFLookup.mo:438-499`；Cref 版 `lookupSimpleCref` `:835-905` 同构）

```
cur = scope
loop (最多 Global.recursionDepthLimit = 256 次，Global/Global.mo:45):
  try  node = lookupLocalSimpleName(name, cur)            // 单层：本类元素 + 继承元素 + import，同一张表
       if require_builtin and not isBuiltin(node): fail   // encapsulated 之后只认内建
       return node
  else if isEncapsulated(cur):                            // :472-475
         cur = topScope(parentScope(cur)); require_builtin = true
       elif name == name(cur) 且 cur 是类（或 cur 的类型名 == name）:   // :476-481  只有 Path 版有
         return classScope(cur), selfReference = true
       elif isTopScope(cur) and not loaded and not require_builtin:    // :483-487
         loaded = true; loadLibrary(name, cur)            // 按名字自动装库，然后在 top 再试一次
       else cur = parentScope(cur)                        // :490
```

逐条锚点与含义：
1. **在此之前先过"保留内建名"短路**：Path 族 `lookupFirstIdent` `:655-674` 先试 `lookupSimpleBuiltinName`（`:800-810`：`Real/Integer/Boolean/String` → `NFBuiltin.*_NODE`，状态 `PREDEF_CLASS`）；Cref 族 `lookupSimpleCref` `:851-853` 先试 `lookupSimpleBuiltinCref`（`:812-833`：`time` → PREDEF_COMP，`Boolean` → PREDEF_CLASS，`Integer`/`String` → 同名内建**函数** FUNC）。这一步**不看任何作用域**，所以这几个名字不可被用户遮蔽（§6 探针）。
2. **单层查找是一次 map 查询**：`lookupLocalSimpleName` `:426-436` → `Class.lookupElement` `NFClass.mo:249-256` → `ClassTree.lookupElement` `NFClassTree.mo:941-952` → `LookupTree.get(name)` 得到 `Entry.CLASS(i) | COMPONENT(i) | IMPORT(i)`，再 `resolveEntry` `:1827-1839`。**本地元素、继承元素、import 名在同一棵 LookupTree 里**，优先级在**建表时**由冲突函数决定，不是查的时候按序试：
   - 本地元素 vs import：本地赢（`addLocalElementConflict` `:1665` "Local elements overwrite imported elements"；`addImportConflict` `:1798-1799` "Other elements overwrite an imported name"）。探针 p02c。
   - 继承元素 vs import：继承赢（`addInheritedElementConflict` `:2084-2089`）。探针 p02b。
   - 限定 import vs 非限定 `.*`：限定赢（`addImportConflict` `:1790-1793`）。探针 p02a。
   - 两个同名限定 import / 同名来自两个 `.*`：建表时只把该槽替换成 `CONFLICTING_IMPORT`，**用到才报错**（`:1777-1788`，`NFImport.mo:103-107`）。用例 ImportConflict1/2/3/4。
   - 本地元素 vs 本地元素同名：`DOUBLE_DECLARATION_OF_ELEMENTS`（`:1672`）；本地 vs 继承同名走 DuplicateTree（R11 已述）。
3. **找到 outer 元素时返回对应 inner**：`InstNode.resolveInner` `NFInstNode.mo:1466-1474`（`lookupLocalSimpleName` `:435`；Cref 版 `:930-934` 还把 `foundScope` 换成 inner 的 parent）。
4. **encapsulated**（`:472-475`，Cref 版 `:883-885`）：本层没找到且本层是 encapsulated → 直接跳到 top scope 并置 `require_builtin`；此后找到的节点必须 `InstNode.isBuiltin`（nodeType 为 `BUILTIN_CLASS`，`NFInstNode.mo:1994-2013`），否则继续失败——而 top 的 parentScope 是 EMPTY_NODE，`parentScope(EMPTY)` 无匹配分支 → fail → 查找失败。`BUILTIN_CLASS` 标记来自 `NFInst.mo:513-514`（顶层类带 `annotation(__OpenModelica_builtin=true)` 的，`markBuiltinTypeNodesByAnnotation` `:531-537`）。用例 Encapsulated1（报 "Class A not found in scope B."）、Encapsulated2（`div`、`time` 可用）；探针 p04a（用户顶层包 `Top` 在 encapsulated 里不可见）、p04b（`import Top;` 后可见，`sin`/`time`/`integer` 可见）。
5. **"名字等于当前作用域名"捷径**（`:476-481`，仅 Path 版）：向外走的途中若 `name == 当前类名`，直接返回该类并标 `selfReference`——这使 `lookupLocalName` 跳过对它的 `instPackage`（`:696-704`），避免"类内部引用自己的全名"时重入实例化（注释举例 MSL 的 `extends Modelica.Icons.***`，`:761-762`）。副作用：这一步**优先于**外层作用域里的同名元素（因为还没走到外层）。
6. **自动装库**（`:483-487`，`loadLibrary` `:1159-1177`）：在 top 也没找到时，按名字 `BackendInterface.appendLibrary` 装默认版本，成功则发 Notification `NOTIFY_IMPLICIT_LOAD` "Automatically loaded package %s %s due to usage."，失败回滚错误缓冲。用例 LookupLibrary1/2。**ymc 建议不实现**（或做成显式开关），但要知道 oracle 有这个行为：探针 p10 里 `instantiateModel(NoSuchModel)` 先报 "Failed to load package NoSuchModel (default) using MODELICAPATH …" 再报 "Class NoSuchModel not found in scope <top>."。
7. **注解语境**（`:449-456`）：`InstContext.inAnnotation` 时先查 top scope 上挂的 `<annotations>` 隐式包（`NFInst.mo:484-505`）。

#### 2.2 "外层作用域"是什么：`InstNode.parentScope`（`NFInstNode.mo:713-750`）

- 类节点：`node.parentScope` = **定义处的词法外层类**。经 `extends` 拿来的基类实例节点保留自己原来的 parentScope（`NFInst.mo:791` 只改 nodeType 为 `BASE_CLASS`），所以**继承来的元素仍在基类的词法环境里查名字**——用例 Extends4（"Class C not found in scope A."：基类 A 里的 `C c;` 看不到派生类里的 `C`）、探针 p26（`Lib.A` 里的 `k` 取 `Lib.k = 7`，不取使用处 `P26.k = 100`）。
- 组件节点：`parentScope(组件) = parentScope(组件的类型实例)`（`:747`）——即**类型定义处**的外层，不是组件所在的实例父亲。
- 短类定义 `DERIVED_CLASS`：走到 `lastBaseClass` 的 parentScope；若最终基类是内建类型则去 top（`:724-736`）。
- for 迭代器：`IMPLICIT_SCOPE` 节点，`locals` 线性表（`lookupIterator` `:937-950`），Cref 版专有（`lookupLocalSimpleCref` `:918-919`）。

#### 2.3 Cref 版多出的一条规则：外层找到的变量必须是常量

`lookupSimpleCref` 每向外走一层就置 `inEnclosingScope := not isImplicit(foundScope)`（`:894`；从 for 隐式作用域走出来不算）。查完后 `LookupState.checkCrefVariability`（`NFLookupState.mo:653-669`）：若是在外层找到、且节点是**非 constant 组件**（`isNonConstantComponent` `:671-676`，注意 parameter 也不行），状态置 `ERROR(NON_CONSTANT)` → `NON_CONSTANT_IN_ENCLOSING_SCOPE` "Component '%s' was found in an enclosing scope but is not a constant."（多源消息：先 "From here:" 指向声明，再错误指向使用处）。用例 PackageConstant5。RELAXED 语境（checkModel）不查。

#### 2.4 组件的类型名、修饰里的名字各从哪个作用域查

- **类型名**：`instComponent` 取 `parent := InstNode.parent(comp_node)`（`NFInst.mo:1885`）= 组件**声明所在的类实例**，一路传到 `instTypeSpec(…, scope = parent, …)`（`:1984-1985`）→ `Lookup.lookupClassName(typeSpec.path, scope, …)`（`:2284`）。对继承来的组件，parent 是基类实例节点，所以是基类的词法环境（Extends4）。
- **修饰 `A a(p = q)`**：`instElementModifier` → `Modifier.fromElement(element, parent)`（`NFInst.mo:2035`）→ `Modifier.create(…, scope)` 里绑定表达式用 `Binding.fromAbsyn(mod.binding, …, scope, mod.info)` 记下 **scope = 写这条修饰的类**（`NFModifier.mo:171-195`）。即：修饰**右边的表达式**在"修饰所在的类"里查；修饰**左边的元素名** `p` 在被修饰组件的类型里查（applyModifier，R11）。探针 p05a（`a.p = q` 取 `P05a.q`，输出 `parameter Real a.p = q;`）、p05b（`A a(p = zz)`，`zz` 只在 A 里有 → "Variable zz not found in scope P05b."，位置是修饰子项 `6:7-6:13`）。
- extends 上的修饰同理：`Modifier.fromElement(elem, scope)`，scope = 含 extends 的类（`NFInst.mo:1386`）。
- 数组维度 `Dimension.RAW_DIM(d, parent)`、条件 `Binding.fromAbsyn(component.condition, …, parent, info)`（`NFInst.mo:1953-1955`）也记 parent。

### 3. 复合名 A.B.C 与 LookupState 状态机（MLS §5.3.2）

#### 3.1 两条下行路径

首段一律按 §2 的简单名查（`lookupFirstIdent` / `lookupSimpleCref`），初始状态 = `LookupState.nodeState(首段节点)`（`NFLookupState.mo:473-482`：组件/NAME_NODE/EMPTY → `COMP`；类按 SCode restriction → `PACKAGE`（R_PACKAGE）/`FUNC`（R_FUNCTION）/`CLASS`（其余，`elementState` `:484-499`））。**首段不经过 `next`**，所以首段不查 protected（`BEGIN` 状态在 NFLookup 里其实从未被构造，`checkProtection` 的 `BEGIN` 分支 `:456` 只是保险）。

后续每一段：

| | Path 族 `lookupLocalName` `NFLookup.mo:676-739` | Cref 族 `lookupCrefInNode` `:952-1031` |
|---|---|---|
| ① 当前节点是组件 | **立即**置 `COMP_CLASS` 并返回（`:691-694`）——类型名/基类名/import 名里**不允许任何一段是组件** | 允许，在组件的类型实例里继续查（`classScope` `:987`）；未实例化的组件（类实例为空）→ 把剩余 cref 原样拼上返回（`:989-992`） |
| ② 当前节点是类 | `Inst.instPackage(node)`（`:697`，selfReference 时跳过）；之后若 `isPartial` 且非 RELAXED/REDECLARED → `ERROR(PARTIAL_CLASS)` 返回（`:700-703`） | 同（`:973-979`） |
| ③ 单层查下一段 | `lookupLocalSimpleName`；失败 → 整个查找 fail → 入口报 not found | `Class.lookupElement`；失败 → fail，**例外**：节点是组件且其类是 expandable connector（或 instance API）→ 当虚成员原样拼上（`:996-1003`） |
| ④ 命中的是 import 名 | `ERROR(IMPORT)`（`:712-713,724-725`） | 同（`:1005-1009`） |
| ⑤ outer → inner | `resolveInner`（在 `lookupLocalSimpleName` 里） | `resolveInnerCref`：还要把已拼好的 cref 前缀裁到 inner 的父亲（`:1033-1067`） |
| ⑥ "在类里找到非常量组件" | 不查 | `scope_is_class` 且非 RELAXED 且命中 `isNonConstantComponent` → `ERROR(NON_ENCAPSULATED)`（`:1014-1020`） |
| ⑦ 状态转移 | `LookupState.next(node, state, context, checkAccessViolations)` | `LookupState.next(n, state, context)` |
| ⑧ 递归下一段 | `:728` | `:1029`；入口处 `isError(state)` 直接返回（`:966`）——**出错后不再往下查**，错误节点就是出错那一段 |

**中间段是类时要不要求是 package？NF 不要求。** MLS §5.3.2 的"非 package 的类里只能查 encapsulated 元素"在 NF 里被整体注释掉（`NFLookupState.mo:437` 的调用与 `:588-651` 的 `checkPackageLikeAccess` 函数体全是注释），只留下 ⑥ 这一条弱化版：**经由类（不论是不是 package）用 cref 访问到非 constant 组件才报错**。探针 p14：`model A` 里的 `A.B b;`（类）与 `A.c`（constant）都通过；用例 PackageConstant7：`R.x`（非常量）→ `NON_ENCAPSULATED_CLASS_ACCESS`，文案里的 "Class R does not satisfy the requirements for a package…" 只是沿用旧文案。

#### 3.2 状态与转移（`next2` `NFLookupState.mo:501-586`）

状态全集（`:100-116`）：`BEGIN, COMP, CLASS_COMP, COMP_CLASS, COMP_FUNC, PACKAGE, CLASS, FUNC, PREDEF_COMP, PREDEF_CLASS, IMPORT, PARTIAL_CLASS, NON_CONSTANT, NON_ENCAPSULATED, ERROR(errorState)`。其中 `IMPORT/PARTIAL_CLASS/NON_CONSTANT/NON_ENCAPSULATED` 只作为 `ERROR(...)` 的载荷或 `assertState` 的"期望值"出现，不是真正的行进状态。

`next` = `checkProtection`（§4.4）+ `next2(nodeState(node), current)`：

| 当前 \ 找到的元素 | COMP | FUNC | CLASS | PACKAGE |
|---|---|---|---|---|
| `COMP`（经组件） | COMP | COMP_FUNC | COMP_CLASS | COMP_CLASS |
| `COMP_CLASS`（经组件找到的类里） | **ERROR(COMP_FUNC)** | COMP_FUNC | COMP_CLASS | COMP_CLASS |
| `COMP_FUNC` | **ERROR(COMP_FUNC)** | COMP_FUNC | COMP_CLASS | COMP_CLASS |
| `PACKAGE` / `CLASS` / `FUNC` | CLASS_COMP | FUNC | CLASS | PACKAGE |
| `CLASS_COMP`（类里的组件） | CLASS_COMP | **ERROR(CLASS_COMP)** | **ERROR(CLASS_COMP)** | **ERROR(CLASS_COMP)** |
| `PREDEF_CLASS` 等其余 | ERROR(COMP_FUNC)（落到 `(COMP(), _)` `:571`） | 断言失败 `:579-583` | 同左 | 同左 |

读法：合法形态只有两种——"类…类.组件…组件"（`P.Q.c.d`，全程 PACKAGE/CLASS → CLASS_COMP）和"组件…组件[.类…类.函数]"（`a.b.C.f`，只为函数调用服务）。

#### 3.3 结束验收 `assertState`（`:217-372`）：每个 (结束状态, 期望) 的报错

| 结束状态 | 期望 | 结果 / 消息（行号） | 样例 |
|---|---|---|---|
| COMP / CLASS_COMP / PREDEF_COMP / FUNC / COMP_FUNC | COMP | 通过（`:232-236`；函数名可当 cref 值用） | 探针 p34：`Real x = a.f` 查找通过，错在后面的类型检查 |
| PACKAGE / CLASS / PREDEF_CLASS / FUNC | CLASS | 通过（`:237-240`） | |
| FUNC / COMP_FUNC | FUNC | 通过（`:241-242`） | FuncViaComp3 `b.a.f(1)` |
| CLASS（可调用类型：record/operator/enumeration/Clock） / COMP（类型是函数的组件） | FUNC | 通过（`:244-245`） | |
| COMP_CLASS | FUNC | `LOOKUP_FOUND_WRONG_TYPE`（`:248-252`） | 探针 p33 "Expected a.R to be a function, but found class instead." |
| COMP_FUNC | 非 FUNC/COMP | `FOUND_FUNC_NAME_VIA_COMP_NONCALL`（`:255-260`）——Path 族遇组件即 COMP_CLASS，实际到不了这里 | 未能构造触发样例 |
| COMP_CLASS | 其余 | `FOUND_CLASS_NAME_VIA_COMPONENT`（`:264-269`） | lookup1 `a.B b;`、lookup2 `A.b.C c;`、lookup3 `Real x = a.B.C;`、探针 p25（import） |
| ERROR(COMP_FUNC) | FUNC | `NON_CLASS_IN_COMP_FUNC_NAME`，**位置用出错那段节点的声明处** `InstNode.info(node)`（`:273-279`） | 探针 p30b `a.BB.c.f()` |
| ERROR(COMP_FUNC) | COMP | `UNEXPECTED_COMPONENT_IN_COMPOSITE_NAME`（`:282-288`） | lookup4 `a.b.C.d.x` |
| ERROR(COMP_FUNC) | 其余 | `LOOKUP_CLASS_VIA_COMP_COMP`（`:291-297`） | |
| ERROR(CLASS_COMP) | COMP | `CLASS_IN_COMPOSITE_COMP_NAME`（`:300-306`） | 探针 p31 `P.c.M.x` |
| ERROR(CLASS_COMP) | 其余 | `LOOKUP_CLASS_VIA_COMP_COMP`（`:309-315`） | 探针 p32 `P.c.M.f()` |
| ERROR(IMPORT) | 任意 | `IMPORT_IN_COMPOSITE_NAME`（`:319-325`） | ImportInCompositeName1/2 |
| ERROR(PARTIAL_CLASS) | 任意 | 见 §4.2（`:327-344`） | PartialLookup1/2/3 |
| ERROR(NON_CONSTANT) | 任意 | `NON_CONSTANT_IN_ENCLOSING_SCOPE`，多源 {声明处, 使用处}（`:346-351`） | PackageConstant5 |
| ERROR(NON_ENCAPSULATED) | 任意 | `NON_ENCAPSULATED_CLASS_ACCESS`，多源 {声明处, 使用处}（`:353-359`） | PackageConstant7、探针 p30 |
| 任意 | IMPORT | 通过（`:362`，但排在上面所有 ERROR / COMP_CLASS 分支之后） | 探针 p07：import 到非常量组件也放行 |
| 其余 | | `LOOKUP_FOUND_WRONG_TYPE` "Expected %s to be a %s, but found %s instead."（`:365-369`；种类词来自 `lookupStateString` `:384-401`：component/class/function/package） | ClassAsComponentError、ComponentAsTypeError、CompAsFunc |

match 是**自上而下首个命中**，顺序本身就是语义（例如 `(COMP_CLASS, FUNC)` 必须排在 `(COMP_CLASS, _)` 前）。

### 4. 全限定名、部分实例化类里的查找、extends 基类名限制、protected

#### 4.1 全限定名 `.A.B`

`Absyn.Path.FULLYQUALIFIED` → `lookupName(name.path, InstNode.topScope(scope), …)`（`NFLookup.mo:614-615`；`lookupNames` `:649-650`；cref 版 `CREF_FULLYQUALIFIED` `:339-340`）。即"把起始作用域换成 top，其余照常"：首段仍走 `lookupFirstIdent`（内建名短路 + top 的单层查 + 自动装库）。`topScope` 沿 `InstNode.parent` 链走到 nodeType 为 `TOP_SCOPE` 的节点（`NFInstNode.mo:830-838`）。探针 p06：同名嵌套包下 `A.c = 2.0`、`.A.c = 1.0`。

#### 4.2 查找何时触发实例化；partial 类里不许查

- **首段**不触发任何实例化，只读各层作用域**现有的** class tree。外层作用域此时一定至少是 partial-inst 过的（因为我们是顺着它们下来的）。
- **进入一个类去查下一段之前**调用 `Inst.instPackage(node, context)`（`NFInst.mo:1317-1367`）：带三态缓存 `NOT_INITIALIZED → PROCESSING → PARTIALLY_INSTANTIATED → INSTANTIATED`；`PROCESSING` 时原样返回节点以破环（`:1346-1349`）；实质动作是 `instantiate(node)`（= `expand` + 非 partial 才 `instClass`，`:427-440`）再 `instExpressions`（`:1360-1364`）。所以**复合名的中间段类会被完整实例化一份"包实例"并缓存**，继承来的元素因此可见（用例 ImportSubPackage2：`P1.A`，`A` 是 `P1` 经 `extends P2` 继承来的）。
- `partialInstClass`（`NFInst.mo:539-556`）：`NOT_INSTANTIATED` → 建 `PARTIAL_TREE`（`ClassTree.fromSCode` `NFClassTree.mo:118-225`：只把**本地类**登记进 LookupTree，本地组件只进数组不进表）→ 挂到节点上 → `Class.initImports`（§5）。
- class tree 四态与可查范围（`NFClassTree.mo:72-113`）：`PARTIAL_TREE` 可查本地类 + import；`EXPANDED_TREE` 表里已有全部名字（本地组件 + 继承元素，`ClassTree.expand`，`NFInst.mo:748`），但组件数组还没就位——`resolveComponent` 只有 `INSTANTIATED_TREE/FLAT_TREE` 两个分支（`:1891-1900`），在此态查组件名会 match 失败 → 被当成"本层没找到"继续向外层找；`INSTANTIATED_TREE`/`FLAT_TREE` 全可查。
- **partial 类**：`instantiate` 对 partial 类只 expand 不 instClass（`:436-439`），随后查找方在 ② 处置 `ERROR(PARTIAL_CLASS)`。`assertState`（`NFLookupState.mo:327-344`）：若 partial 类的最外层实例祖先是组件（`listHead(scopeList(node))` 是组件）→ `USE_OF_PARTIAL_CLASS`（多源：{partial 类声明处, 组件声明处}，用例 PartialLookup2）；否则 `LOOKUP_IN_PARTIAL_CLASS` "%s is partial, name lookup is not allowed in partial classes."（用例 PartialLookup1 类型名、PartialLookup3 基类名）。RELAXED / REDECLARED 语境放行——**根类查找恒为 RELAXED**（`NFInst.mo:365`），所以 `omc` 可以直接实例化 `PartialPkg.M`。`lookupLocalNames`（基类名）对名为 `PartialModelicaServices` 的类特判放行（`:766-773`，兼容旧 MSL）。
- 这里的"partial"指 **`partial` 前缀**；"部分实例化"（PARTIAL_TREE）的类里查找是允许的、只是可见范围小，二者别混。

#### 4.3 extends 基类名的查找限制

机制不是一条显式检查，而是**时机**：`expandClassParts`（`NFInst.mo:710-753`）先把类换成 `EXPANDED_CLASS` 外壳但**保留 `PARTIAL_TREE`**（`:727-730`），然后 `mapFoldExtends(expandExtends)` 逐个查基类（`:734`，`lookupBaseClassName` 的起始 scope = 含 extends 的类 `:780-781`），全部 extends 处理完才 `ClassTree.expand` 把继承元素并入表（`:748`）。于是查基类首段时，本类的表里**只有本地类和 import**——继承来的类天然不可见：
- 探针 p11（`extends A; extends B;`，B 是 A 里的类）→ "Base class B not found in scope P11."。
- 漏网情形：继承来的名字在**外层**也有同名类时，expand 期会先找到外层那个；实例化后 `modifyExtends` 再查一次（`NFInst.mo:1396`），若两次的 SCode 定义不是同一个对象 → `FOUND_OTHER_BASECLASS` "Found other base class for extends %s after instantiating extends."（多源 {`InstNode.info(extendsNode)`, `InstNode.info(ext_node)`}，`:1400-1406`；BASE_CLASS 节点的 info 取的是 **extends 子句**的位置 `NFInstNode.mo:1150-1151`，所以 "From here:" 指向 `extends A;`，Error 指向后找到的那个类的声明）。用例 ExtendInherited1。旧前端的 `INHERITED_EXTENDS` / `EXTENDS_INHERITED_FROM_LOCAL_EXTENDS`（`Error.mo:197,407`）**NF 不用**（grep NFFrontEnd 无命中）。
- 基类路径每一段都不得是 replaceable：`checkReplaceableBaseClass`（`NFInst.mo:847-888`）→ `REPLACEABLE_BASE_CLASS`，用 `<B>` 标出是哪一段（用例 ExtendReplaceable2）。这就是 `lookupBaseClassName` 要返回整条节点链的原因。
- extends 环：`checkExtendsLoop`（`NFInst.mo:807-845`）——基类正处于"EXPANDED_CLASS + PARTIAL_TREE"（正在展开中），或基类就是当前类的某个外层 → `EXTENDS_LOOP` "extends %s causes an instantiation loop."（用例 ExtendSelf2/3、ExtendsLoop1、RecursiveExtends1）。
- 基类名经过组件 → `COMP_CLASS` → `FOUND_CLASS_NAME_VIA_COMPONENT`（同 lookup1）；旧的 `EXTEND_THROUGH_COMPONENT` NF 不用。

#### 4.4 protected

`checkProtection`（`NFLookupState.mo:446-471`）在**每个非首段**命中时检查 `InstNode.isProtected(node)`（节点上的 visibility 字段，`NFInstNode.mo:1791-1800`；protected extends 会把继承元素改成 protected，`protectClass/protectComponent` `:1807-1833`）。违规 → `PROTECTED_ACCESS` "Illegal access of protected element %s."，**位置 = 被访问元素的声明处**（`InstNode.info(node)`，`:463-464`），然后 `fail()`；入口函数 catch 到失败后再补一条 not found（位置 = 使用处）。所以输出恒为两条：
- Visibility3：`[…:7:13-7:19] Illegal access of protected element x.` + `[…:11:3-11:15] Variable a.x not found in scope Visibility3.`
- Visibility4 / ExtendsVisibility4：函数版，第二条是 "Function a.f not found …"。
- 探针 p08：类版，第二条是 "Class P.M not found in scope P08."。

放行条件：`checkAccessViolations = false`（只有根类查找 `NFInst.mo:369,378` 和 `NFClassTree.mo:2542` 这样传）、instance API 语境、或 `--allowNonStandardModelica=protectedAccess`（`:462`）。首段不查：在类内部直接用自己的 protected 元素、或 `P.a` 里 `P` 本身是 protected，都合法（注释 `:447-449`；用例 Visibility1/2）。修饰 protected 元素是另一条消息 `NF_MODIFY_PROTECTED`（`Error.mo:872`，在 NFInst 的修饰应用处，不属查找）。

### 5. import（MLS §13.2）

#### 5.1 数据与时机

- **SCode 层已把组导入拆平**：`AbsynToSCode.translateImports`（`FrontEnd/AbsynToSCode.mo:1245-1269`）把 `import P.{a, bb = b}` 拆成一条 `QUAL_IMPORT(P.a)` + 一条 `NAMED_IMPORT(bb, P.b)`（`translateGroupImport` `:1271-1293`），并**剥掉 import 路径上的前导点**（`:1258-1263`，`import .A.B` ≡ `import A.B`）。NF 因此只见三种：`NAMED_IMPORT`（重命名）、`QUAL_IMPORT`（限定）、`UNQUAL_IMPORT`（`.*`）。探针 p18。
- **三态**（`NFImport.mo:50-65`）：`UNRESOLVED_IMPORT(imp, scope, info)` → `RESOLVED_IMPORT(node, shortName, info)`；冲突则为 `CONFLICTING_IMPORT(imp1, imp2)`。
- **登记**：`ClassTree.fromSCode` 只把 import 收集成 `UNRESOLVED` 列表，**不进表**（`NFClassTree.mo:208-213`，注释 `:119-122`）；等 class tree 挂到节点上之后 `Class.initImports`（`NFInst.mo:549` → `NFClassTree.mo:227-270`）才进表——为的是允许包用 `.*` 导入自己的子包（用例 ImportUnqualified4：`package A import A.Units.*; …`）。
  - **非限定 `.*` 在 initImports 时立刻解析**（`:250-256` → `Import.instUnqualified` `NFImport.mo:154-190`）：`lookupImport` 找到包 → `instPackage` → 把其 `FLAT_TREE` 的**全部类和全部组件**各生成一条 `RESOLVED_IMPORT`（`:173-179`，**不过滤 protected**——探针 p40：`import P.*` 能用到 P 的 protected 常量，无报错）。代价：包不存在时**即使没人用也立刻报错并使整个类实例化失败**（探针 p19a "Import Nope not found in scope <top>."）。
  - **限定/重命名 import 延迟到第一次被查到时解析**（注释 `:247-249`；`resolveImport` `:1902-1925` → `Import.resolve` `NFImport.mo:87-110` → `instQualified` `:129-152`），解析结果用 `arrayUpdate` 写回 imports 数组作缓存（`:1921-1924`）。所以**没被用到的坏 import 不报错**（探针 p19b；用例 ImportConflict2/4）。
- **进表名字**：`Import.name`（`NFImport.mo:67-75`）——未解析的取 `AbsynUtil.importName`（重命名取别名，限定取末段），已解析的（来自 `.*`）取节点名。表项是 `LookupTree.Entry.IMPORT(index)`。
- **import 的目标路径永远从 top scope 查**（`lookupImport` `NFLookup.mo:284`）：用例 ImportQualifiedInvalid2（本地有 `package P` 也不算，"Import P.M not found in scope <top>."）。encapsulated 对它无影响（探针 p04b）。
- **import 名对嵌套类可见**：它就是外层类表里的一项，§2 的向外走自然会碰到（探针 p42）。

#### 5.2 规则小结（带锚点）

| 规则 | 锚点 | 样例 |
|---|---|---|
| import 不被继承 | `addInheritedElement` 对 `IMPORT` 项直接跳过 `NFClassTree.mo:2063-2064` | 探针 p03 "Variable c not found in scope P03." |
| 本地元素 > import（静默遮蔽，无告警） | `:1665`、`:1798-1799` | 探针 p02c、p43；用例 ImportShadowing1 期望的 `LOOKUP_SHADOWING` 告警**在 1.25.0 与 HEAD 都不发**（该消息在编译器源码里无人引用；用例列在 `scodeinst/Makefile` 的 `FAILINGTESTFILES`） |
| 继承元素 > import | `:2084-2089` | 探针 p02b |
| 限定 import > `.*` | `:1790-1793` | 探针 p02a |
| 两条限定 import 同名 | 延迟错误 `:1777-1781`；用到时 `printImportError` `NFImport.mo:192-206`：Notification `ERROR_FROM_HERE` 指第一条 + Error `MULTIPLE_QUALIFIED_IMPORTS_WITH_SAME_NAME` 指第二条；随后该名字按"没找到"继续，入口再补一条 not found | ImportConflict1（三行输出）；不用则不报 ImportConflict2 |
| 同名来自两条 `.*` | 同上，消息 `IMPORT_SEVERAL_NAMES` "%s found in several unqualified import statements." | ImportConflict3 / 4 |
| import 名不能出现在复合名的非首段 | `ERROR(IMPORT)` → `IMPORT_IN_COMPOSITE_NAME` | ImportInCompositeName1（类型名）/2（cref） |
| 目标不存在 | `LOOKUP_IMPORT_ERROR`，位置 = import 子句；限定 import 是"用到时"报，并再补一条使用处的 not found | ImportNamedInvalid1、ImportQualifiedInvalid1 |
| 目标路径经过组件 | `COMP_CLASS` → `FOUND_CLASS_NAME_VIA_COMPONENT`（import 子句位置）+ 使用处 not found | 探针 p25 |
| 目标是 protected（限定） | `PROTECTED_ACCESS`（声明处）+ `LOOKUP_IMPORT_ERROR`（import 处）+ 使用处 not found，共三条 | 探针 p41 |
| 目标是组件 | 允许（`assertImport` 对任何种类放行 `NFLookupState.mo:362`）；**连非 constant、非 package 成员也放行** | 用例 ImportComponent1（`import P.pi`）；探针 p07（`import Q.x`，Q 是 model、x 是变量 → 压平结果出现悬空的 `Q.x`，属 OMC 的宽松/缺陷，见"未确认"） |

### 6. 内建作用域

内建元素分**两层**，可遮蔽性完全不同：

**(A) 硬编码短路名——不可遮蔽。** 在走任何作用域之前就匹配字符串：
- Path 族 `lookupSimpleBuiltinName`（`NFLookup.mo:800-810`）：`Real`、`Integer`、`Boolean`、`String` → `NFBuiltin.REAL_NODE/INTEGER_NODE/BOOLEAN_NODE/STRING_NODE`（`NFBuiltin.mo:209,253,285,320`；各带一棵预制 `FLAT_TREE`，里面是 `start/min/max/…` 等 `TYPE_ATTRIBUTE` 组件，`:141-207` 等）。
- Cref 族 `lookupSimpleBuiltinCref`（`:812-833`）：`time` → `NFBuiltin.TIME`（`NFBuiltin.mo:411`），`Boolean` → 类型名，`Integer` / `String` → **同名内建函数** `NFBuiltinFuncs.INTEGER_NODE/STRING_NODE`（枚举转整数、转字符串）。注意 `Real` 不在 cref 表里。
- 探针：p01a（顶层 `type Real = Integer;`——**不报重复声明**，因为 top 的表里本来就没有 `Real`；`Real x = 1.5` 仍是 Real）、p01b（类内 `type Real = Integer;` 被无视，同类里的 `type Foo = Integer` 正常）、p46（类内 `model String` 被无视，`String s;` 仍是内建 String）、p01e（`parameter Real time = 3; parameter Real z = time;` → "Component z of variability parameter has binding 'time' of higher variability continuous."，证明取到的是内建 `time`）、用例 BuiltinLookup1（本地 `model Boolean`，`Boolean.x` → "Variable Boolean.x not found …"）。
- 访问类型属性当变量（`x.start`）：查找能找到 `TYPE_ATTRIBUTE` 组件，但 `instCrefComponent` 拒绝（`NFInst.mo:3001-3006`）→ `LOOKUP_VARIABLE_ERROR` 且两个参数是 (属性名, 父节点名)："Variable start not found in scope x."（用例 inst7）。

**(B) top scope 里的普通顶层类——按普通规则查，可被内层遮蔽。** `CevalScriptBackend.runFrontEndWorkNF` 把 `FBuiltin.getInitialFunctions()` 读出的 `NFModelicaBuiltin.mo`（安装目录 `lib/omc/`）翻成 SCode 后**拼在用户程序前面**（`Script/CevalScriptBackend.mo:3684,3699`），`makeTopNode` 用这串顶层类建 `<top>` 包（`NFInst.mo:450-523`）。里面有：`der/sin/abs/…` 内建函数、`StateSelect/Uncertainty/AssertionLevel` 枚举、`Clock`、`ExternalObject`、`Connections`、`OpenModelica` 等（`NFModelicaBuiltin.mo:36-84,177,718,991`）。
- 带 `annotation(__OpenModelica_builtin=true)` 的顶层类被标成 `BUILTIN_CLASS`（`NFInst.mo:513-514,531-537`）——这个标记的**唯一查找语义**是"encapsulated 之后仍可见"（§2.1-4）。探针 p45b：encapsulated 模型里 `StateSelect.prefer`、`der`、`Integer(…)`、`String(…)` 全可用。
- `Clock` 特殊：`NFModelicaBuiltin.mo:51` 放一个占位 partial class 防止用户重名，`makeTopNode` 再用 `NFBuiltin.CLOCK_NODE` 替换（`NFInst.mo:516-519`）。
- 因为是 top 的普通成员：**内层同名元素遮蔽它们**（探针 p01c：类内 `function sin` 生效，`sin(1.0)` 求值为 `2.0`）；**用户在顶层重名则是重复声明**（探针 p44：顶层 `function sin` → Notification "From here:" 指向安装目录的 `NFModelicaBuiltin.mo:173:1-180:8` + Error `DOUBLE_DECLARATION_OF_ELEMENTS` "An element with name sin is already declared in this scope."）。
- 注解里的名字另有一个 `<annotations>` 隐式 encapsulated 包，挂在 TOP_SCOPE 的 nodeType 上，只在 `inAnnotation` 语境先查（`NFInst.mo:484-505`，`NFLookup.mo:449-456`）。

对 ymc 的含义：(A) 做成查找函数开头的一个 `switch`（4 个类型名 + `time` + cref 语境下的 `Integer/String`）；(B) 做成装载期预置进顶层表的普通类 + 一个 `is_builtin` 位。

### 7. 错误消息清单

均来自 `OMCompiler/Compiler/Util/Error.mo`（`ErrorTypes.MESSAGE(id, 类别, 级别, gettext(模板))`；引用前已重 grep，id 与模板为 HEAD 3ceb0c9d06 原文）。触发点为 NFFrontEnd 里的 `addSourceMessage`/`addMultiSourceMessage` 调用行。级别未标注即 `ERROR()`，类别除注明外均 `TRANSLATION()`。

**(A) "未找到" 入口消息**——两个 `%s` 恒为 `(整个名字的字符串, InstNode.scopeName(起始 scope))`（`scopeName` 只取作用域短名，组件取其类型名，顶层 `<top>`）：

| 消息（id） | 模板 | 触发点（emit） | 场景 |
|---|---|---|---|
| `LOOKUP_ERROR` (3) | `Class %s not found in scope %s.` | `NFLookup.mo:90`（经 `lookupNameWithError`） | 类型名/短类定义基类 `lookupClassName`（探针 p09d、Encapsulated1、p06 反例） |
| `LOOKUP_VARIABLE_ERROR` (5) | `Variable %s not found in scope %s.` | `NFLookup.mo:130`(comp)、`:152`(connector)、`:386`(local cref) | 表达式里变量/连接器名 `lookupComponent`（探针 p09a、p05b、BuiltinLookup1） |
| `LOOKUP_BASECLASS_ERROR` (39) | `Base class %s not found in scope %s.` | `NFLookup.mo:106` | `extends` 基类名 `lookupBaseClassName`（探针 p09c、p11） |
| `LOOKUP_FUNCTION_ERROR` (165) | `Function %s not found in scope %s.` | `NFLookup.mo:213` | 函数调用名 `lookupFunctionName`（探针 p09b） |
| `LOOKUP_IMPORT_ERROR` (70) | `Import %s not found in scope %s.` | `NFLookup.mo:285`（`lookupNameWithError`），实由 `NFImport.mo:140,143,166` 发起 | import 目标路径失败；起始 scope 恒 `<top>`（探针 p19a、ImportQualifiedInvalid2） |
| `RECURSION_DEPTH_REACHED` (527) | `The maximum recursion depth of %s was reached, probably due to mutual recursion. The current scope: %s.` | `NFLookup.mo:496`(name)、`:901`(cref) | 向外走超过 `Global.recursionDepthLimit=256` 层 |

**(B) 复合名 / LookupState `assertState` 报错**——位置多为出错那段元素的声明处 `InstNode.info(node)`；`assertState` 自上而下首个命中（`NFLookupState.mo:217-372`）：

| 消息（id） | 模板 | 触发点（emit） | 场景 |
|---|---|---|---|
| `FOUND_CLASS_NAME_VIA_COMPONENT` (101) | `Class name '%s' was found via a component (only component and function call names may be accessed in this way).` | `NFLookupState.mo:266` | 结束状态 `COMP_CLASS`、期望非 FUNC（lookup1/2/3、p25 import） |
| `UNEXPECTED_COMPONENT_IN_COMPOSITE_NAME` (395) | `Found component '%s' in composite name '%s', expected class.` | `NFLookupState.mo:285` | `ERROR(COMP_FUNC)` 期望 COMP（lookup4） |
| `CLASS_IN_COMPOSITE_COMP_NAME` (96) | `Found class %s during lookup of composite component name '%s', expected component.` | `NFLookupState.mo:303` | `ERROR(CLASS_COMP)` 期望 COMP（p31） |
| `LOOKUP_CLASS_VIA_COMP_COMP` (112) | `Illegal access of class '%s' via a component when looking for '%s'.` | `NFLookupState.mo:294`(COMP_FUNC)、`:312`(CLASS_COMP) | `ERROR(COMP_FUNC/CLASS_COMP)` 其余期望（p32） |
| `NON_CLASS_IN_COMP_FUNC_NAME` (94) | `Found non-class %s while looking for function via component. The only valid form is c1..cN.C1..CN.f where c1..cN are scalar components and C1..CN are classes.` | `NFLookupState.mo:277`（位置=出错段声明处 `info2`） | `ERROR(COMP_FUNC)` 期望 FUNC（p30b） |
| `FOUND_FUNC_NAME_VIA_COMP_NONCALL` (102) | `Found function %s by name lookup via component, but this is only valid when the name is used as a function call.` | `NFLookupState.mo:258` | `COMP_FUNC` 期望非 FUNC/COMP（Path 族遇组件即 COMP_CLASS，实到不了） |
| `IMPORT_IN_COMPOSITE_NAME` (359) | `Found imported name '%s' while looking up composite name '%s'.` | `NFLookupState.mo:322` | `ERROR(IMPORT)`：import 名出现在非首段（ImportInCompositeName1/2） |
| `LOOKUP_FOUND_WRONG_TYPE` (143) | `Expected %s to be a %s, but found %s instead.` | `NFLookupState.mo:416`（兜底），亦 `:248` 特化（COMP_CLASS 期望 FUNC，p33） | 种类不匹配兜底（ClassAsComponentError、ComponentAsTypeError） |

**(C) 语义限制**——多为多源消息 `{声明处, 使用处}`：

| 消息（id） | 模板 | 触发点（emit） | 场景 |
|---|---|---|---|
| `PROTECTED_ACCESS` (61) | `Illegal access of protected element %s.` | `NFLookupState.mo:463`（位置=元素声明处） | 非首段命中 protected；随后入口再补一条 not found（Visibility3、p08 → 共两条） |
| `LOOKUP_IN_PARTIAL_CLASS` (107) | `%s is partial, name lookup is not allowed in partial classes.` | `NFLookupState.mo:337` | 在 `partial` 类里查名（PartialLookup1 类型名、PartialLookup3 基类名） |
| `USE_OF_PARTIAL_CLASS` (285) | `component %s contains the definition of a partial class %s.\nPlease redeclare it to any package compatible with %s.` | `NFLookupState.mo:333`（multi-source） | partial 类的实例祖先是组件（PartialLookup2） |
| `NON_CONSTANT_IN_ENCLOSING_SCOPE` (403) | `Component '%s' was found in an enclosing scope but is not a constant.` | `NFLookupState.mo:348`（multi-source） | Cref 在外层作用域找到非常量组件（parameter 也算）（PackageConstant5） |
| `NON_ENCAPSULATED_CLASS_ACCESS` (86) | `Class %s does not satisfy the requirements for a package. Lookup is therefore restricted to encapsulated elements, but %s is not encapsulated.` | `NFLookupState.mo:355`（multi-source） | 经非 package 类用 cref 访问非常量组件（PackageConstant7、p30；文案是旧 package 措辞） |

**(D) import 冲突 / 重复声明**——延迟到用到时报，位置=import 子句：

| 消息（id） | 模板 | 触发点（emit） | 场景 |
|---|---|---|---|
| `MULTIPLE_QUALIFIED_IMPORTS_WITH_SAME_NAME` (163) | `Qualified import name %s already exists in this scope.` | `NFImport.mo:201`（`printImportError`，前接 `ERROR_FROM_HERE`） | 两条同名限定 import（ImportConflict1） |
| `IMPORT_SEVERAL_NAMES` (57) | `%s found in several unqualified import statements.` | `NFImport.mo:202`（同 `printImportError`） | 同名来自两条 `.*`（ImportConflict3/4） |
| `DOUBLE_DECLARATION_OF_ELEMENTS` (161) | `An element with name %s is already declared in this scope.` | `NFClassTree.mo:1672`（`addLocalElementConflict`） | 同名本地元素；用户顶层重名内建（p44，前接 `ERROR_FROM_HERE`） |

**(E) extends / 自动装库**：

| 消息（id） | 模板 | 触发点（emit） | 场景 |
|---|---|---|---|
| `REPLACEABLE_BASE_CLASS` (172) | `Class '%s' in 'extends %s' is replaceable, the base class name must be transitively non-replaceable.` | `checkReplaceableBaseClass` `NFInst.mo:847-888` | 基类路径某段 replaceable（ExtendReplaceable2） |
| `FOUND_OTHER_BASECLASS` (306) | `Found other base class for extends %s after instantiating extends.` | `NFInst.mo:1400-1406`（multi-source `{extends 子句, 后找到的类}`） | 实例化后基类改变（ExtendInherited1） |
| `EXTENDS_LOOP` (19) | `extends %s causes an instantiation loop.` | `checkExtendsLoop` `NFInst.mo:807-845` | extends 环（ExtendSelf2/3、ExtendsLoop1、RecursiveExtends1） |
| `ERROR_FROM_HERE` (174, **Notification**) | `From here:` | `NFImport.mo:198`、多源消息第一源 | 多源消息的"指向另一处"前缀 |
| `NOTIFY_IMPLICIT_LOAD` (391, **Notification**, 类别 `SCRIPTING()`) | `Automatically loaded package %s %s due to usage.` | `NFLookup.mo:1171`（`loadLibrary`） | 自动装库成功（LookupLibrary1/2；ymc 不实现，见 §2.1-6） |

**已定义但 NF 不发的旧前端消息**（grep NFFrontEnd 零命中，勿在 ymc 模仿）：`LOOKUP_SHADOWING` (71, Warning) "Import %s is shadowed by a local element."（本地遮蔽 import 时 NF **静默**，探针 p02c/p43、ImportShadowing1 列 FAILING）；`INHERITED_EXTENDS` (59)、`EXTEND_THROUGH_COMPONENT` (60)、`EXTENDS_INHERITED_FROM_LOCAL_EXTENDS` (164)——继承相关的旧诊断，NF 一律换成上面的 `LOOKUP_BASECLASS_ERROR` / `FOUND_OTHER_BASECLASS` / `FOUND_CLASS_NAME_VIA_COMPONENT`。

## 探针

全部采集自 `docs/research/probe-r12/`（141 个文件），oracle = 安装版 `omc` 1.25.0（`OpenModelica v1.25.0 (64-bit)`）。下列 12 个覆盖八类关键行为。**命令**：`.mo` 直接压平走 `omc +locale=C <file>.mo`（`translateFile`，`Main.mo:498`，失败框架为 `# Error encountered! Exiting...`）；脚本探针走 `omc +locale=C <file>.mos`（`instantiateModel(X); getErrorString()`，其失败尾行是 `getErrorString` 里的 `Error occurred while flattening model X`——见 p10）。输出为实跑原文，`[…:writable]` 位置框内路径按采集目录给出。

### 探针一览

| 探针 | 覆盖点 | 结论一句话 |
|---|---|---|
| p26 | 简单名逐层外找（词法作用域） | 继承来的元素在**基类的词法环境**里查名，取 `Lib.k=7` 而非使用处 `P26.k=100` |
| Encapsulated1 | encapsulated 边界 | encapsulated 类里向外查普通名失败 → `Class A not found in scope B.` |
| p04b | encapsulated 后仍可见的 | `import Top;` 后 `Top.k` 可见；内建 `sin`/`integer` 穿透 encapsulated |
| lookup1 | 复合名经组件访问到类 | `a.B`（组件 a 的类型里的类 B）→ `FOUND_CLASS_NAME_VIA_COMPONENT` |
| lookup4 | LookupState 中段状态错 | `a.b.C.d.x`：组件 d 出现在期望类的段 → `UNEXPECTED_COMPONENT_IN_COMPOSITE_NAME` |
| p06 | 全限定名 `.A.B` | `A.c` 取内层同名包 `=2`，`.A.c` 从 top 取 `=1` |
| p11 | extends 基类不经继承元素找 | `extends A; extends B;`（B 在 A 里）→ `Base class B not found` |
| ImportConflict1 | import 冲突（用到才报） | 两条同名限定 import → 三行：From here + 重名 Error + not found |
| p19a | 非限定 import 目标不存在 | `import Nope.*;` 即使没人用也立刻失败 `Import Nope not found in scope <top>.` |
| p46 | 内建类型被用户类遮蔽 | 类内 `model String` 被无视，`String s;` 仍是内建 String（压平出 `String s;`） |
| BuiltinLookup1 | 内建名短路优先于本地类 | 本地 `model Boolean` 存在，但 `Boolean.x` 仍解析到内建 Boolean → `Variable Boolean.x not found` |
| p08 | protected（两条错误） | protected 类 `P.M` → `Illegal access...` + `Class P.M not found`（两条） |

### p26 — 简单名逐层外找：继承元素在基类词法环境里查

```modelica
package Lib
  constant Real k = 7;
  model A
    Real x = k;
  end A;
end Lib;
model P26
  constant Real k = 100;
  Lib.A a;
end P26;
```
命令：`omc +locale=C p26_lexical_scope.mo`
```
class P26
  constant Real k = 100.0;
  Real a.x = 7.0;
end P26;
```
`a.x` 取 `Lib.k=7`——`A` 的 parentScope 是 `Lib`，不是使用处 `P26`（§2.2）。

### Encapsulated1（testsuite）— encapsulated 边界

```modelica
package P1
  model A
    Real x;
  end A;
  encapsulated model B
    A a;      // B 是 encapsulated，看不到外层 P1 的 A
  end B;
end P1;
model Encapsulated1
  P1.B b;
end Encapsulated1;
```
命令：`omc +locale=C ts_Encapsulated1.mo`
```
Error processing file: ts_Encapsulated1.mo
[C:/dev/ymc/docs/research/probe-r12/ts_Encapsulated1.mo:14:5-14:8:writable] Error: Class A not found in scope B.

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```

### p04b — encapsulated 后：import 名与内建穿透

```modelica
package Top
  constant Real k = 5;
end Top;
encapsulated model P04b
  import Top;                     // 显式 import 把 Top 拉进来
  Real x = Top.k + sin(time);    // sin/time 是内建，穿透 encapsulated
  Integer n = integer(2.5);
end P04b;
```
命令：`omc +locale=C p04b_encapsulated_import.mo`
```
class P04b
  Real x = 5.0 + sin(time);
  Integer n = 2;
end P04b;
```

### lookup1（testsuite）— 复合名经组件访问到类

```modelica
model A
  model B
    model C
      Real x;
    end C;
  end B;
  B b;
end A;
model M
  A a;
  a.B b;     // a 是组件，B 是其类型里的类 → 非法
end M;
```
命令：`omc +locale=C ts_lookup1.mo`
```
Error processing file: ts_lookup1.mo
[C:/dev/ymc/docs/research/probe-r12/ts_lookup1.mo:18:3-18:8:writable] Error: Class name 'a.B' was found via a component (only component and function call names may be accessed in this way).

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```
状态轨迹 `COMP`（a）→ `COMP_CLASS`（B），结束期望 CLASS → `FOUND_CLASS_NAME_VIA_COMPONENT`（§3.3）。

### lookup4（testsuite）— LookupState 中段：组件出现在期望类的位置

```modelica
model A
  model B
    model C
      model D
        Real x;
      end D;
      constant D d;
    end C;
  end B;
  constant B b;
end A;
model M
  A a;
  Real x = a.b.C.d.x;   // a,b 组件；C 类；d 又是组件 → 位置违规
end M;
```
命令：`omc +locale=C ts_lookup4.mo`
```
Error processing file: ts_lookup4.mo
[C:/dev/ymc/docs/research/probe-r12/ts_lookup4.mo:22:3-22:21:writable] Error: Found component 'd' in composite name 'a.b.C.d.x', expected class.

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```

### p06 — 全限定名 `.A.B` 从 top 重查

```modelica
package A
  constant Real c = 1;
  model M
    package A
      constant Real c = 2;
    end A;
    Real x = A.c;    // 就近的内层 package A → 2
    Real y = .A.c;   // 前导点 → 从 top 查外层 package A → 1
  end M;
end A;
model P06
  A.M m;
end P06;
```
命令：`omc +locale=C p06_fully_qualified.mo`
```
class P06
  Real m.x = 2.0;
  Real m.y = 1.0;
end P06;
```

### p11 — extends 基类不经继承元素查

```modelica
model A
  model B
    Real y;
  end B;
  Real x;
end A;
model P11
  extends A;
  extends B;    // B 是从 A 继承来的类，但 extends 阶段继承尚未并入表 → 不可见
end P11;
```
命令：`omc +locale=C p11_extends_inherited.mo`
```
Error processing file: p11_extends_inherited.mo
[C:/dev/ymc/docs/research/probe-r12/p11_extends_inherited.mo:9:3-9:12:writable] Error: Base class B not found in scope P11.

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```
机制是**时机**：`expandExtends` 逐个查基类时本类表里只有本地类 + import，继承元素要 `ClassTree.expand`（`NFInst.mo:748`）之后才进表（§4.3）。

### ImportConflict1（testsuite）— 两条同名限定 import（用到才报）

```modelica
package P
  model M Real x; end M;
  model N Real x; end N;
end P;
model ImportConflict1
  import M = P.M;
  import M = P.N;    // 同名限定 import 冲突
  M m;               // 用到 M 才触发
end ImportConflict1;
```
命令：`omc +locale=C ts_ImportConflict1.mo`
```
Error processing file: ts_ImportConflict1.mo
[C:/dev/ymc/docs/research/probe-r12/ts_ImportConflict1.mo:19:3-19:17:writable] Notification: From here:
[C:/dev/ymc/docs/research/probe-r12/ts_ImportConflict1.mo:20:3-20:17:writable] Error: Qualified import name M already exists in this scope.
[C:/dev/ymc/docs/research/probe-r12/ts_ImportConflict1.mo:21:3-21:6:writable] Error: Class M not found in scope ImportConflict1.

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```
三行：`ERROR_FROM_HERE`(指第一条 import) + `MULTIPLE_QUALIFIED_IMPORTS_WITH_SAME_NAME`(指第二条) + 入口补的 `LOOKUP_ERROR`（§5.2）。

### p19a — 非限定 `.*` 目标不存在：即使未用也立刻失败

```modelica
model P19a
  import Nope.*;   // Nope 不存在
  Real x;          // 根本没用到 import
end P19a;
```
命令：`omc +locale=C p19a_unqual_import_missing_unused.mo`
```
Error processing file: p19a_unqual_import_missing_unused.mo
[C:/dev/ymc/docs/research/probe-r12/p19a_unqual_import_missing_unused.mo:2:3-2:16:writable] Error: Import Nope not found in scope <top>.

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```
对照 p19b（限定 import 坏且未用 → 不报）：`.*` 在 `initImports` 时立即解析（§5.1），限定/重命名延迟。

### p46 — 内建类型名被用户类遮蔽（用户类被无视）

```modelica
model P46
  model String        // 与内建类型 String 同名
    Real x = 1;
  end String;
  String s;            // 仍解析为内建 String
end P46;
```
命令：`omc +locale=C p46_local_class_named_like_builtin_type.mo`
```
class P46
  String s;
end P46;
```
`String` 走硬编码短路（§6-A），不看作用域，用户的 `model String` 拿不到（对照 p01b：`type Real=Integer;` 同样被无视）。

### BuiltinLookup1（testsuite）— 内建名短路优先于本地类

```modelica
model BuiltinLookup1
  model Boolean
    Real x;
  end Boolean;
  Real y = Boolean.x;   // Boolean 短路到内建类型，没有成员 x
end BuiltinLookup1;
```
命令：`omc +locale=C ts_BuiltinLookup1.mo`
```
Error processing file: ts_BuiltinLookup1.mo
[C:/dev/ymc/docs/research/probe-r12/ts_BuiltinLookup1.mo:11:3-11:21:writable] Error: Variable Boolean.x not found in scope BuiltinLookup1.

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```

### p08 — protected 类：两条错误（声明处 + 使用处）

```modelica
package P
protected
  model M
    Real x;
  end M;
end P;
model P08
  P.M m;     // M 是 P 的 protected 成员
end P08;
```
命令：`omc +locale=C p08_protected_class.mo`
```
Error processing file: p08_protected_class.mo
[C:/dev/ymc/docs/research/probe-r12/p08_protected_class.mo:3:3-5:8:writable] Error: Illegal access of protected element M.
[C:/dev/ymc/docs/research/probe-r12/p08_protected_class.mo:8:3-8:8:writable] Error: Class P.M not found in scope P08.

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```
第一条 `PROTECTED_ACCESS` 位置=元素声明处（`3:3-5:8`），`fail()` 后入口 catch 再补第二条 `LOOKUP_ERROR` 位置=使用处（`8:3-8:8`）（§4.4）。

**脚本框架对照（p10）**：`instantiateModel(X); getErrorString()` 路径下，错误由 `getErrorString()` 以带引号字符串返回，且 `instantiateModel(NoSuchModel)` 会先触发自动装库失败——
```
"Error: Failed to load package NoSuchModel (default) using MODELICAPATH C:/Users/yangrui/AppData/Roaming/.openmodelica/libraries/.
Error: Class NoSuchModel not found in scope <top>.
"
```
（`getErrorString` 每次调用后清空缓冲；`.mo` 直接压平不走这条路径，故无 "flattening model" 尾行。）

## 未确认 / 待验证

- **`import` 目标是组件/变量的宽松放行**（§5.2 表末、探针 p07）：`import Q.x`（Q 是 model、x 是变量）OMC 不报错，压平结果里出现悬空的 `Q.x` 引用——疑似 OMC 的宽松/缺陷。ymc 是否复刻待定；先按 oracle 行为记入分歧账本，L1 对拍时若两侧都产出该悬空引用则一致。
- **self-reference 判定的 1.25.0↔HEAD 差异**（§1）：1.25.0 由 `lookupSimpleName` 直接回传 `selfReference` 标志，HEAD 改用 `isSelfReference(node, scope)` 沿 instanceParent 链比对。两版对本报告所有探针给出相同结果；边角（多层 self-reference + redeclare）未单独探针，实现时以安装版 1.25.0 为准。
- **自动装库触发的错误行**（§2.1-6、探针 p10）：`instantiateModel(NoSuchModel)` 先报 "Failed to load package …" 再报 "Class … not found"；`.mo` 直接压平不走脚本路径，尾行不同。ymc 若不实现自动装库，需在 L1/L2 diff 归一化里抹掉这行（已在"明确不做"登记）。
- **完整 verification 依赖 inst 层落地**：本报告的查找规则全部有源码锚点 + 探针，但"ymc 实现后逐条对拍"要等 S3 的 `src/inst/` 有 LookupTree 与 `instPackage` 破环后才能做；届时用 scodeinst 里 Lookup*/Import*/Encapsulated*/Extends* 的 incorrect 用例做错误文案回归。
- **`RECURSION_DEPTH_REACHED`（256 层）** 的触发仅源码可见（`Global.recursionDepthLimit`），未构造探针——正常模型不会撞到，属防御性上限。

## 对 ymc 的建议

沿用方案 §3.4 的表示（每类节点一个 `Arena<T>` + `Id<T>` 互指、胖 struct + Kind + switch、不用 variant/继承）与 §3.5 可调试规范。查找属 inst 层（S3+），下面的类型名按 `I*` 前缀拟（对标 OMC `InstNode`/`ClassTree`），与既有 `S*`（SCode）区分。

### 1. 作用域与元素表

- **`INode`（对标 `InstNode`）**：`Arena<INode> inodes`。`INodeKind { Class, Component, BaseClass, Ref, Implicit, Empty, TopScope }`。关键：**两条父链分开存**——
  - `Id<INode> parent_scope`：词法外层，**查名字向外走用这条**（类=定义处外层类；组件=其类型定义处的外层，即 `parentScope(comp)=parentScope(comp的类型实例)`，`NFInstNode.mo:713-750` / §2.2）。
  - `Id<INode> instance_parent`：实例树父亲，**resolveInner / inner-outer 用这条**（§2.1-3）。
  - 二者不可合并：OMC 把它们分成 `parentScope` 与 `instanceParent`，混用会让继承元素在错误作用域里查名（探针 p26 会取错 k）。
- **每个类一张 `LookupTree`**：`std::unordered_map<Symbol, LEntry>`，`LEntry { LEntryKind kind; std::uint32_t index; }`，`LEntryKind { Class, Component, Import }`（对标 `LookupTree.Entry`）。**本地元素、继承元素、import 名同处一张表**；优先级在**建表期**由冲突函数定死，查找期只做一次 `map.find` + `resolve`，不按序试多张表（§2.1-2）。
- **建表两阶段**（对标 PARTIAL_TREE → EXPANDED_TREE，§4.2；用 class tree 的 Kind 位区分可查范围）：
  1. `build_partial(cls)`：只把**本地类**登记进表，本地组件只进数组不进表；import 收集成未解析列表（`init_imports` 时才进表，允许包 `.*` 导入自己子包）。
  2. `expand(cls)`：**处理完所有 extends 之后**再把本地组件 + 继承元素并入表。这条时序就是"extends 基类名查不到继承来的类"的机制（探针 p11、§4.3），别提前 expand。
  - `merge_inherited` 冲突序：本地 > 继承 > import；限定 import > `.*`；同名限定 import 或同名两条 `.*` → 槽位标 `Conflicting`（**用到才报**）。**import 不被继承**：并继承元素时跳过 `Import` 项（`NFClassTree.mo:2063-2064`、探针 p03）。本地遮蔽 import **静默**（不发 `LOOKUP_SHADOWING`，§7）。
- **import 表**：单独 `std::vector<IImport>`，`IImport` 三态 `{ Unresolved, Resolved, Conflicting }`（对标 `NFImport.mo:50-65`）。`.*` 在 `init_imports` 时**立即解析**（把目标包 flat 表的全部类+组件各生成一条 Resolved，**不过滤 protected**，探针 p40）；限定/重命名**延迟**到第一次被查到再解析并 `arrayUpdate` 回写缓存（坏 import 未用不报，探针 p19b）。目标路径**永远从 `top_scope` 查**（探针 p04b、ImportQualifiedInvalid2）。

### 2. 查找函数签名

两族（Path 族=类型名/基类名/import 名；Cref 族=表达式里的变量/函数名）共用状态机，入口只在三点不同：**失败消息 / 结束 `assert_*` / 起始 scope**。

```cpp
struct Found      { Id<INode> node; bool is_import = false; };
struct LookupResult { Id<INode> node; LookupState state; bool found_outer = false; };

// 核心（静默 fail，走 Diag 记录而非返回码）
std::optional<Found> lookup_local_simple(Symbol name, Id<INode> scope);          // 一次 map 查 + resolve_inner
LookupResult          lookup_simple(Symbol name, Id<INode> scope, const InstContext& ctx);  // §2.1 逐层外找主循环
LookupResult          lookup_name (const Path& p, Id<INode> scope, const InstContext& ctx);  // 复合名下行
LookupResult          lookup_cref (const Cref& c, Id<INode> scope, const InstContext& ctx);

// 带报错入口（use = 使用处 SourceLoc）
Id<INode> lookup_class_name (const Path& p, Id<INode> scope, const InstContext& ctx, SourceLoc use); // 失败 LOOKUP_ERROR；assert_class
Id<INode> lookup_base_class (const Path& p, Id<INode> scope, const InstContext& ctx, SourceLoc use); // 失败 LOOKUP_BASECLASS_ERROR；返回整条节点链（供 replaceable 逐段查）
Id<INode> lookup_component  (const Cref& c, Id<INode> scope, const InstContext& ctx, SourceLoc use); // 失败 LOOKUP_VARIABLE_ERROR；assert_component
Id<INode> lookup_function   (const Cref& c, Id<INode> scope, const InstContext& ctx, SourceLoc use); // 失败 LOOKUP_FUNCTION_ERROR；assert_function
```

- import 入口 / 全限定名（Path `.A.B`、Cref `.a.b`）在进核心前把 `scope := top_scope(scope)`——这就是 import 报错里 scope 恒 `<top>` 的来由（§4.1、§5.1）。
- `lookup_base_class` 返回**整条链**（对标 `lookupNames`），因为 replaceable 检查要逐段看（§4.3）。其余入口只需末节点。

### 3. LookupState：enum + 纯函数转移

```cpp
enum class LookupState : std::uint8_t {
  Begin, Comp, ClassComp, CompClass, CompFunc, Package, Class, Func, PredefComp, PredefClass, Error
};
enum class LookupErr : std::uint8_t { Import, PartialClass, NonConstant, NonEncapsulated, CompFunc, ClassComp };
struct StateBox { LookupState st; LookupErr err; Id<INode> at; };  // Error 时 err+at 有效（at=出错那段，报错位置用它）

LookupState node_state(Id<INode> n);                                  // 组件→Comp；类按 SRestriction→Package/Func/Class
StateBox    next(StateBox cur, Id<INode> found, const InstContext& ctx); // = check_protection(非首段) + next2
void        assert_state(StateBox s, Expect want, std::string_view name, SourceLoc use); // 结束验收
```

- `next2` 把 §3.2 的二维表写成 `switch(cur.st)` 内嵌 `switch(node_state(found))`，逐格照抄——合法形态只有"类…类.组件…组件"和"组件…组件[.类.函数]"两种。
- **首段不过 `next`**（不查 protected）；出错后（`st==Error`）立即返回、不再下行，`at` 就是出错那段（§3.1-⑧）。
- `assert_state` 是另一个 switch，**分支顺序即语义**（§3.3）：`(CompClass, Func)` 必须排在 `(CompClass, *)` 前，各分支逐条对应 §7 的消息（`FOUND_CLASS_NAME_VIA_COMPONENT` / `UNEXPECTED_COMPONENT_IN_COMPOSITE_NAME` / …）。
- **弱化 MLS §5.3.2**：NF 已把"非 package 类里只能查 encapsulated 元素"整段注释掉，ymc 同样只保留 §3.1-⑥ 弱化版——经类用 cref 访问到非常量组件才报 `NON_ENCAPSULATED_CLASS_ACCESS`（探针 p14 正例、PackageConstant7 反例）。

### 4. 错误位置用哪个 SourceLoc

- **"not found" 入口消息**：位置 = **使用处**（入口传入的 `use`）；两个 `%s` = `(整名字符串, scope_name(起始 scope))`。`scope_name`：作用域短名，组件取类型名，顶层 `<top>`（`NFInstNode.mo:597-602`）——单独写个 `scope_name(Id<INode>)`。
- **状态机报错**（protected / partial / 经组件找到类 / import 在中段 / 种类不符）：位置 = **出错那段元素的声明处** `inodes[at].loc`（对标 `InstNode.info(node)`）。
- **多源消息**（partial-via-component、非常量、非 encapsulated、import 冲突、`FOUND_OTHER_BASECLASS`）：先 `ERROR_FROM_HERE`（Notification，指声明/第一处）再 Error（指使用/第二处）——`Diag` 要有 `add_multi_source(msg, {loc1, loc2})`（对标 `addMultiSourceMessage`）。
- **protected 恒两条**：状态机发 `PROTECTED_ACCESS`（声明处）后让本次查找**失败**，入口 catch 再补 not found（使用处，探针 p08）。所以 `Diag` 必须支持"先记一条、令查找失败、由外层补第二条"，**不能一遇错就 abort**——这是 ymc 现有 `diag.h` 要预留的能力。

### 5. 与 InstNode / 实例化的关系

- **查找不独立于实例化**：进入复合名中间段的类之前要 `inst_package(node)`（对标 `Inst.instPackage`，三态缓存 `NotInit/Processing/Done`，`Processing` 时原样返回破环，§4.2）。所以 `lookup_*` 必须放在 inst 层、能回调 `inst_package`/`expand`；纯 SCode 层做不了查找。首段只读现有 class tree、不触发实例化。
- `parent_scope` 建 INode 时定死：`extends` 拿来的基类节点**保留原 parent_scope**（只改 `INodeKind` 为 `BaseClass`，对标 `NFInst.mo:791`）——继承元素仍在基类词法环境查名（探针 p26、Extends4）。
- `top_scope(scope)`：沿 `parent_scope` 走到 `INodeKind::TopScope`。全限定名/import 用它。
- **内建两层**（§6）：(A) 硬编码短路做成 `lookup_simple` 开头的 `switch`——`Real/Integer/Boolean/String`（Path 版）+ `time`/`Integer`/`String`（Cref 版），**不看作用域、不可遮蔽**（探针 p46、BuiltinLookup1）；(B) 顶层内建类装载期预置进 top 的 LookupTree + 每节点一个 `is_builtin` 位，其**唯一查找语义**是"encapsulated 之后仍可见"（探针 p04b、p45b），普通遮蔽/重名规则照常（探针 p01c、p44）。

### 6. 明确不做（有意偏离 oracle，各记一条 ADR）

- **自动装库**（`loadLibrary`，§2.1-6）：不做，或做成显式开关。副作用：ymc 的 "not found" 不会先出 "Failed to load package…"（探针 p10 尾），比 oracle 少一行——测试对照时要归一化掉这行。
- **`LOOKUP_SHADOWING`**：保持静默（OMC 本身也不发）。
- **MLS §5.3.2 完整 package 访问检查**：只保留弱化版（见 §3 末）。
- 三者都要在 `docs/adr/` 立条目、并在 L1/L2 验收的 diff 归一化规则里登记，避免被当成回归。
