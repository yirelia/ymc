# R11 OMC 新前端（NF）实例化管线与数据结构

> 研究对象：`C:\dev\OpenModelica`（HEAD 3ceb0c9d06）`OMCompiler/Compiler/NFFrontEnd/`；oracle = 安装版 omc 1.25.0。
> 约定：锚点写作 `文件:行`（均相对 `OMCompiler/Compiler/`，行号为 HEAD 3ceb0c9d06，引用前已 grep/Read 复核）；探针输出为 omc 1.25.0 实跑原文。
> 状态：**增量写作中**（每完成一节即追加；未完成的节标注"待写"）。

## 结论摘要

1. `omc X.mo` 实例化文件里**最后一个**顶层类（`Main.mo:449-452` → `AbsynUtil.lastClassname`），打印的是 **DAE dump**（`DAEDump.dumpStr`），不是 NF 自己的 `FlatModel.toString`；后者只在 `-f`/`--flatModelica` 时用（§1.0）。
2. `instClassInProgram` 是一条 23 步的原地流水线（§1.1 表）：makeTopNode → lookupRootClass → **instantiate**（=expand+instClass）→ **instExpressions** → updateImplicitVariability → **typeClass** → **flatten** → evalConstants → simplify → collectFunctions → scalarize → verify …。1.25.0 与 HEAD 顺序完全一致，仅 3 处细节不同（§1 末）。
3. "实例化"是三趟：`instantiate` 只建树 + 合并修饰 + 处理 redeclare，**不碰表达式**；`instExpressions` 才把 Absyn 绑定/维度/方程解析成 NF `Expression`（cref 查找），并把树压成 `FLAT_TREE`；`typeClass` 定类型/维度/variability（§1 要点）。
4. 数据结构四层：`InstNode`（身份 + 父指针，值语义）→ `Pointer<Class>`/`Pointer<Component>`（可变单元，多节点共享）→ `Class` 持 `ClassTree` → `ClassTree` 持子 `InstNode` 数组（§2）。可变性靠三种盒子：`Pointer.update`（原地改、共享者同步）、`Pointer.create`（=克隆、断共享）、`Mutable<InstNode>`（数组槽间接层，redeclare/去重用）。
5. **extends 也用 `CLASS_NODE` 表示**（不是独立 record）：`node_type=BASE_CLASS`，expand 后与基类定义**共享 `Pointer<Class>`**，`ClassTree.instantiate` 时才把 parent 改成新实例并递归克隆（§2.2、§3.2）。
6. `Class` 状态机 `NOT_INSTANTIATED→PARTIAL_CLASS→EXPANDED_CLASS→INSTANCED_CLASS`（短类走 `EXPANDED_DERIVED→TYPED_DERIVED`，内建走 `PARTIAL_BUILTIN→INSTANCED_BUILTIN`）；`Component` 状态机 `COMPONENT_DEF→PartiallyInstantiated→FullyInstantiated→Typed→TypeChecked`（§2.3、§2.5）。任务书里的旧结构名（COMPONENT_DEF→UNTYPED→TYPED 三 record）在 1.25.0 已合并成单 record + `state` 枚举。
7. **克隆边界 = `ClassTree.instantiate`**：`PARTIAL_CLASS`/`EXPANDED_CLASS(EXPANDED_TREE)` 在**定义节点上算一次、全局共享**；从 instantiate 起（→`INSTANTIATED_TREE`→`FLAT_TREE`）**每个使用点各有私有克隆**。浅克隆 + 惰性：嵌套类用到时才继续克隆（§3.3、§7.3/§7.6）。
8. **压平变量顺序 = SCode 元素声明顺序，extends 在其子句出现的位置就地展开**（`REF_NODE` 占位 → `instExtendsComps` 整段拷入）。组件下标保序；**类下标不保序**（继承类统一排局部类之后）——两套规则不同（§3.2 末、§3.4、§7.4 不变量 1-2）。
9. **重复继承元素保留下标最小（最靠前）者**的位置，其余槽在 `FLAT_TREE` 化时剔除；菱形继承同名且语法相同 → 静默去重，不同 → `DUPLICATE_ELEMENTS_NOT_IDENTICAL`（§3.4 第 3 点、P04/P05）。
10. **方程顺序 = 头插 + extends 逆序**：子组件方程（按组件序 DFS）在前，然后本类方程在前、各 extends 方程按 exts 逆序在后（`instExpressions` 用 `Sections.join(new, accumulated)` 头插，§3.4 第 6 点、P03）。
11. 组件实例化时 `class_inst` **先写 EMPTY 占位**打破"组件要先就位 / class_inst 要 instClass 才出"的循环——也是 inner/outer 查找不会找到自己的原因（§3.3 第 3 点、§5.4、§7.5 不变量 2）。
12. inner/outer：outer 组件在 `linkInnerOuter` 时向上找同名 inner；找不到 → **自动生成 inner + WARNING `MISSING_INNER_ADDED`**（追加在根类组件末尾），顶层 outer → `TOP_LEVEL_OUTER` 升 Error（§5、P08/P14）。
13. 实例化趟报的错分四类（§6）：结构/继承（EXTENDS_LOOP、LOOKUP_BASECLASS_ERROR、REPLACEABLE_BASE_CLASS…）、组件/修饰/redeclare（RECURSIVE_DEFINITION、PARTIAL_COMPONENT_TYPE、MISSING_MODIFIED_ELEMENT、NF_MODIFY_PROTECTED…）、根类/restriction（INST_PARTIAL_CLASS、INST_INVALID_RESTRICTION）、inner/outer。**维度/条件/绑定类型检查不在实例化趟**，全部推迟到 typing（§6.5）——ymc 提前报会与 OMC 时机不符。
14. 给 ymc：`InstNode`/`Class`/`Component` 各一个 arena（`NodeId`/`ClassId`/`ComponentId`），`Pointer<T>` = arena 格（update=原地改、create=新分配格=克隆），`Mutable<InstNode>` = 再加一层 `SlotId`；内建常量节点只读（§7.0-7.6，含 5 条克隆/遍历不变量）。
15. S3.1 竖切片最小子集：标量组件 + 单继承链 + 修饰合并 + 内建类型属性 + inner/outer（P01–P17 逐字对拍）；**排除**数组/维度、函数、redeclare、条件组件求值、package 常量收集（文末"对 ymc 的建议"，5 个高频坑）。

## 逐项事实

### 1. 总管线

#### 1.0 从命令行到 `instClassInProgram`

| 环节 | 锚点 | 事实 |
|---|---|---|
| `omc X.mo` 选哪个类 | `Main/Main.mo:449-452`；`FrontEnd/AbsynUtil.mo:2774-2784` | `Config.classToInstantiate()` 为空（没给 `+i=`/`-i=`）时取 `AbsynUtil.lastClassname(program)` = `List.last(classes)`，即**文件里最后一个顶层类**。探针 P01 实证（只输出 `class Second`）。 |
| 调前端 | `Main.mo:458` → `Script/CevalScriptBackend.mo:3725 translateModel` → `SimCode/SimCodeMain.mo:1190`（新后端）/`:1215`（旧后端，经 `runFrontEnd`→`runFrontEndWork` `CevalScriptBackend.mo:3597`） | 旧后端路径在 `CevalScriptBackend.mo:3628-3629` 先 `runFrontEndWorkNF` 再 `NFConvertDAE.convert`。 |
| 拼程序 | `CevalScriptBackend.mo:3684-3703` | `scode_p := listAppend(builtin_p, scode_p)`：**ModelicaBuiltin 的 SCode 拼在用户程序前面**，一起成为 `<top>` 的元素；`annotation_p` 是另一份独立 SCode（只在注解里可见）。 |
| 关 nfAPI | `CevalScriptBackend.mo:3707` | 进入 `instClassInProgram` 前强制 `NF_API=false`（HEAD 的 `nfAPI` 默认 true，`Util/Flags.mo:510`），所以普通实例化的 context = `NO_CONTEXT`（`NFInst.mo:167-168`）。 |
| 打印压平结果 | `SimCodeMain.mo:1205-1207, 1263` | 未 `--silent` 时 `NFConvertDAE.convertModel` → `DAEDump.dumpStr` → `print`。也就是 `omc X.mo` 看到的文本是 **DAE dump**，不是 `FlatModel` 自己的 toString（`-f`/`--flatModelica` 时才用 NF 的 `dumpFlatModel`，`SimCodeMain.mo:1182,1203-1204`）。 |

#### 1.1 `NFInst.instClassInProgram`（`NFInst.mo:148-283`）步骤表

| # | 步骤 | 函数:行 | 输入 → 输出 | 作用一句话 | flag / 条件 |
|---|---|---|---|---|---|
| 0 | resetGlobalFlags | `NFInst.mo:166`（定义 `:325`） | — | `--newBackend` 时关 `nfScalarize`、开 `vectorizeBindings`；`-d=-nfScalarize` 时连带关 `nfExpandOperations`/`nfExpandFuncArgs` | `NEW_BACKEND`、`NF_SCALARIZE` |
| 1 | 定 context | `NFInst.mo:167-168` | — | `relaxedFrontend`/`checkModel`/`nfAPI` 任一成立 → `RELAXED`，否则 `NO_CONTEXT` | `CHECK_MODEL`、`NF_API` |
| 2 | makeTopNode | `NFInst.mo:171`（定义 `:450-523`） | SCode.Program ×2 → `top: InstNode` | 造假的 `<top>` package 节点（`TOP_SCOPE`），把所有顶层类建成 `PARTIAL_CLASS`/`PARTIAL_TREE`；另造 `<annotations>` 隐式作用域；给带 `__OpenModelica_builtin` 注解的顶层类打 `BUILTIN_CLASS`；用 `NFBuiltin.CLOCK_NODE` 顶替 ModelicaBuiltin 里的假 `Clock` | `BASE_MODELICA` 时多塞一个内建函数（`:468-470`） |
| 3 | lookupRootClass | `NFInst.mo:174`（定义 `:354-396`） | path, top → `cls` | `Lookup.lookupClassName`（RELAXED、不查访问权限）→ `mergeScalars`（仅 `-d=mergeComponents` 有效）→ `checkInstanceRestriction`（function/package 不可实例化）→ `InstNode.makeRootClass`（nodeType := `ROOT_CLASS(parent)`） | — |
| 3' | 根类是 function | `NFInst.mo:179-182` → `instantiateRootFunction :409` | | 只在 checkModel 下走到；返回空 FlatModel | — |
| 4 | **instantiate** | `NFInst.mo:185` → `instantiateRootClass :398-407` → `instantiate :427-440` | `cls` → `inst_cls`（克隆出的新节点，`EXPANDED_CLASS`+`INSTANTIATED_TREE`，组件 = `COMPONENT`(UNTYPED, RAW_BINDING)） | = `expand`（`partialInstClass`+`expandClass`）+ `instClass`；之后 `checkPartialClass :404`、`insertGeneratedInners :406` | 根类 partial 且非 RELAXED → 不进 `instClass`，随后 `checkPartialClass` 报错 |
| 5 | **instExpressions** | `NFInst.mo:194`（定义 `:2461-2578`） | 同一棵实例树，原地更新 | 把 RAW 的绑定/维度/条件/方程节从 Absyn 变成 NF `Expression`（做 cref 查找）；**同时把 `INSTANTIATED_TREE` 压成 `FLAT_TREE`、Class 变 `INSTANCED_CLASS`**；extends 的 sections 合并进派生类 | `InstSettings.mergeExtendsSections` 恒 true（`:129-131`）；`RESIZABLE_ARRAYS` |
| 6 | updateImplicitVariability | `NFInst.mo:198`（定义 `:3885-3926`） | 原地 | 标记结构参数（维度/条件/`Evaluate=true`/…里用到的 parameter → `STRUCTURAL_PARAMETER`）；when 里被赋值的连续变量 → `IMPLICITLY_DISCRETE` | `-d=evaluateAllParameters`（`EVAL_PARAM`） |
| 7 | **Typing.typeClass** | `NFInst.mo:202` → `NFTyping.mo:124-138` | 原地 | 四个子步：`typeClassType`→`typeComponents`→`typeBindings`→`typeClassSections`；组件 `COMPONENT.state` 由 `FullyInstantiated` → `Typed`（`NFTyping.mo:476`）→ `TypeChecked`（`:1053`） | — |
| 8 | **Flatten.flatten** | `NFInst.mo:205` → `NFFlatten.mo:320-398` | 实例树 → `FlatModel{name, variables, equations, initialEquations, algorithms, initialAlgorithms, source}` | 深度优先遍历 `FLAT_TREE.components` 生成带前缀的 `Variable` 列表并收集方程；然后 `resolveConnections`（`:388`）生成连接方程；`checkDeletedVarRefs` | `NF_SCALARIZE`、`ARRAY_CONNECT`、`NEW_BACKEND`、`VECTORIZE_BINDINGS` 等打包进 `FlattenSettings`（`:336-345`） |
| 9 | EvalConstants.evaluate | `NFInst.mo:206` → `NFEvalConstants.mo:77` | FlatModel → FlatModel | 把常量/结构参数引用替换成值 | — |
| 10 | UnitCheck.checkUnits | `NFInst.mo:211` → `NFUnitCheck.mo:83` | | 单位检查；默认直接返回（`NFUnitCheck.mo:91`） | `--unitChecking` 或 checkModel |
| 11 | SimplifyModel.simplify | `NFInst.mo:214-216` → `NFSimplifyModel.mo:63` | | 表达式化简、删平凡方程 | `--noSimplify` 关闭 |
| 12 | Package.collectConstants | `NFInst.mo:221` → `NFPackage.mo:84` | | 没能内联成值的 package 常量作为变量补进模型 | — |
| 13 | Flatten.collectFunctions | `NFInst.mo:224` → `NFFlatten.mo:418-429` | FlatModel → `FunctionTree` | 收集仍被引用的函数 | — |
| 14 | dumpFlatModel（标量化前） | `NFInst.mo:227-229` | → `flatString` | `-f` 输出 Base/Flat Modelica | `dumpFlat` 且 `--baseModelicaOptions` 不含 `scalarize` |
| 15 | Scalarize.scalarize | `NFInst.mo:235-241` → `NFScalarize.mo:67` | | 数组变量/方程拆标量；关闭时只删空数组 + 补全向量化绑定 | `-d=nfScalarize`（默认 true，`Flags.mo:504`） |
| 16 | replaceEmptyArrays | `NFInst.mo:243` | | | — |
| 17 | combineBinaries / toArrayConstructor | `NFInst.mo:251-260` | | 仅新后端 | `--newBackend` |
| 18 | VerifyModel.verify | `NFInst.mo:262` → `NFVerifyModel.mo:63` | | 压平后的合法性检查（when 冲突、离散实变量等） | 1.25.0 签名是 `verify(flatModel)`，HEAD 多了 `isPartial` 参数 |
| 19 | expandSlicedCrefs / combineSubscripts | `NFInst.mo:264-266` | | | — |
| 20 | propagate HideResult / removeNonTopLevelDirections | `NFInst.mo:270-272` | | 非顶层的 input/output 前缀在这里被抹掉 | — |
| 21 | obfuscate | `NFInst.mo:274-277` | | | `--obfuscate` |
| 22 | clearCaches | `NFInst.mo:282` | | | — |
| 23 | （管线外）ConvertDAE.convert | `CevalScriptBackend.mo:3629` → `NFConvertDAE.mo:77`（`convertModel :91`、`convertFunctionTree :1052`） | FlatModel+FunctionTree → `DAE.DAElist`+`DAE.FunctionTree` | 给旧后端/DAEDump 用；新后端路径只在打印时转（`SimCodeMain.mo:1206`） | — |

要点：

- 第 4、5、7 步都是**原地改同一棵 InstNode 树**（靠 `Pointer<Class>`/`Pointer<Component>`）；第 8 步起才是纯函数式的 `FlatModel` 列表变换。
- "实例化"在 NF 里是三趟：**(a)** `instantiate` 只建树 + 合并修饰 + 处理 redeclare，不碰表达式；**(b)** `instExpressions` 才解析 cref——注释原文 "done as a separate step after instantiation to make sure that lookup is able to find the correct nodes"（`NFInst.mo:191-193`）；**(c)** `typeClass` 定类型/维度/variability。
- 1.25.0 与 HEAD 在本函数的差异仅两处：`VerifyModel.verify` 的参数、`lookupRootClass` 末尾由 `setNodeType(ROOT_CLASS(EMPTY_NODE()))` 改为 `makeRootClass`（`git diff v1.25.0 HEAD -- NFInst.mo`）。另 1.25.0 的 `makeTopNode` 在 `Class.fromSCode` 后多一句 `cls := Class.initExpandedClass(cls)`（HEAD 已删）。管线顺序完全一致。

### 2. InstNode / Class / ClassTree / Component 数据结构

四层嵌套关系：`InstNode`（身份 + 父指针）→ `Pointer<Class>` / `Pointer<Component>`（可变单元）→ `Class` 持有 `ClassTree` → `ClassTree` 持有子 `InstNode` 数组。

#### 2.1 `InstNode`（`NFInstNode.mo:223-274`）

| record | 字段 | 用途 |
|---|---|---|
| `CLASS_NODE` `:224-232` | `name`；`definition: SCode.Element`（非 Option，`<top>` 用假 SCode 凑，`NFInst.mo:472-477`）；`visibility`；`cls: Pointer<Class>`；`caches: array<CachedData>`（2 槽：函数缓存、package 缓存，`:128,194-215`）；`parentScope: InstNode`；`nodeType: InstNodeType` | 一个类（或一条 extends——extends 也用 `CLASS_NODE` 表示，`newExtends :314-328`，name = 基类路径最后一段，definition = `SCode.EXTENDS`，实例化后被换成基类节点本身，见 §3.2） |
| `COMPONENT_NODE` `:234-241` | `name`；`definition: Option<SCode.Element>`（内建属性/枚举字面量为 NONE，`fromComponent :357-365`）；`visibility`；`component: Pointer<Component>`；`parent: InstNode`（"The instance that this component is part of"）；`nodeType` | 一个组件 |
| `INNER_OUTER_NODE` `:243-247` | `innerNode`, `outerNode` | outer 元素被链接到 inner 后的包装；`resolveInner`/`resolveOuter`（`:1466-1484`）取两端 |
| `REF_NODE` `:249-251` | `index: Integer` | **只出现在 `PARTIAL_TREE/EXPANDED_TREE.components` 数组里**，表示"这里插第 index 条 extends 的组件"——顺序保持的关键（§3.4） |
| `NAME_NODE` `:253` | `name` | 无实体的名字（如 `:` 维度 cref、后端用） |
| `IMPLICIT_SCOPE` `:257-260` | `parentScope`, `locals: list<InstNode>` | for 迭代器等隐式作用域 |
| `ITERATOR_NODE` `:262`、`VAR_NODE` `:266`（后端用）、`EMPTY_NODE` `:274` | | 空节点用作"无父/无类实例"哨兵 |

**可变性模型（对 C++ 重写最重要的一点）**：MetaModelica 的 record 是值，`node.parentScope := p`（`setParent :870-893`）、`node.nodeType := t`（`setNodeType :1046`）、`node.visibility := …`（`protectClass :1807`）得到的是**新 InstNode 值**，但新旧两个值共享同一个 `Pointer<Class>`；`updateClass :963-974` 是 `Pointer.update`——所有共享该指针的 InstNode 都看到变化；`replaceClass :1013-1024` 是 `Pointer.create`——断开共享，这就是"克隆"（`NFClassTree.mo:466-468` 注释原文："Clone the class node by replacing the class in the node with itself."）。`copyInstancePtr :2083-2101` 反过来让两个节点共享同一单元（redeclare 用，`NFInst.mo:2186`）。

**父指针**：`parent()`（`:652-662`）= 类的 `parentScope` / 组件的 `parent`。类节点的 `parentScope` 有两种含义并存：未实例化时是**词法外层类**；被 `ClassTree.instantiate` 克隆的局部类被 `setParent(instance, c)`（`NFClassTree.mo:526-527`）改成**所在实例**（组件节点或根类节点）。`instanceParent :676-689`、`parentScope :713-750`（组件 → 其类型类的外层）、`getDerivedNode :939-961`（沿 `BASE_CLASS.parent` 从基类节点回到派生实例）、`rootParent :691`、`topScope :830` 是几种"往上走"的变体。

#### 2.2 `InstNodeType`（`NFInstNode.mo:70-126`）

| 值 | 字段 | 含义 / 何时设置 |
|---|---|---|
| `NORMAL_CLASS` | — | 默认（`newClass :290`） |
| `BASE_CLASS` | `parent: InstNode`（派生类/实例）；`definition`（那条 `SCode.EXTENDS`，或 class extends 时是被扩展类的 SCode.CLASS）；`ty`（基类节点原来的 nodeType） | `expandExtends` 把查到的基类节点换上此类型后**写回 exts 数组**（`NFInst.mo:791`）；`ClassTree.instantiate` 再把 `parent` 改成新实例（`NFClassTree.mo:501-502`）；短类的 baseClass 也套一层（`:620-621`）。`isProtectedBaseClass :1767` 从 `definition.visibility` 读 protected extends |
| `DERIVED_CLASS` | `ty`（套壳前的类型） | 短类定义实例化时（`NFInst.mo:1208`） |
| `BUILTIN_CLASS` | — | `NFBuiltin` 的常量节点；`<annotations>` 内的类（`NFInst.mo:525-529`）；带 `__OpenModelica_builtin` 注解的顶层类（`:531-537`） |
| `TOP_SCOPE` | `annotationScope`；`generatedInners: UnorderedMap<String,InstNode>` | `<top>`（`NFInst.mo:481,504`）。自动生成的 inner 缓存在这里 |
| `ROOT_CLASS` | `parent`；`context: Option<Path>` | 被实例化的根类（`makeRootClass :417-423`）；函数实例化时 parent 指向引用它的组件 |
| `NORMAL_COMP` | — | 组件默认（`:311`） |
| `REDECLARED_COMP` | `parent`（被替换组件的 parent） | `redeclareComponent`（`NFInst.mo:2184`）；使 redeclare 元素的修饰在原位置作用域求值 |
| `REDECLARED_CLASS` | `parent`；`originalType`；`originalNode: Option`（仅 instance API 留） | `redeclareClass`（`NFInst.mo:1801`） |
| `GENERATED_INNER` | — | 找不到 inner 时自动生成的节点（§5） |
| `IMPLICIT_SCOPE` | — | `<annotations>` 这种不计入路径的作用域 |

#### 2.3 `Class`（`NFClass.mo:101-158`）状态机

| 状态 | 字段 | 进入点 |
|---|---|---|
| `NOT_INSTANTIATED` `:101` | — | `InstNode.newClass`（`NFInstNode.mo:298`） |
| `PARTIAL_CLASS` `:103-108` | `elements: ClassTree`(PARTIAL_TREE)；`modifier`（外部施加的类修饰，`applyModifier` 写入）；`ccMod`；`prefixes` | `partialInstClass`（`NFInst.mo:539-556`）：长类 / class extends → `Class.fromSCode`；**短类、overload、pder → 空树的 `PARTIAL_CLASS`**（`:596`） |
| `PARTIAL_BUILTIN` `:110-116` | `ty`；`elements`(FLAT_TREE，属性组件)；`modifier`；`prefixes`；`restriction` | 内建类型常量（`NFBuiltin.mo`）；enumeration 定义（`Class.fromEnumeration :188-199`）；ExternalObject（`NFInst.mo:903`）；`polymorphic`（`:1095`） |
| `EXPANDED_CLASS` `:118-124` | `elements`；`modifier`；`ccMod`；`prefixes`；`restriction` | `expandClassParts`（`NFInst.mo:710-753`）。先以 `initExpandedClass`（`NFClass.mo:213`，restriction=UNKNOWN、树仍是 PARTIAL_TREE）占位防循环，结束时换成 EXPANDED_TREE + 真 restriction。`ClassTree.instantiate` 后仍是 `EXPANDED_CLASS`，只是树变 `INSTANTIATED_TREE` |
| `EXPANDED_DERIVED` `:126-134` | `baseClass: InstNode`；`modifier`；`ccMod`；`dims: array<Dimension>`；`prefixes`；`attributes`（短类上的 input/flow/… 前缀）；`restriction` | `expandClassDerived`（`NFInst.mo:1017-1066`） |
| `INSTANCED_CLASS` `:136-142` | `ty: Type`；`elements`(FLAT_TREE)；`sections`；`prefixes`；`restriction` | `instExpressions`（`NFInst.mo:2501, 2537`） |
| `INSTANCED_BUILTIN` `:144-148` | `ty`；`elements`；`restriction` | `instClassDef`（`NFInst.mo:1235, 1257`）、`instExtends :1505` |
| `TYPED_DERIVED` `:150-154` | `ty`；`baseClass`；`restriction` | `Typing.typeClassType`（`NFTyping.mo:300`）；无维度的随后被"塌缩"成基类的 Class（`NFTyping.mo:186-196`） |
| `DAE_TYPE` `:156` | `DAE.Type` | 转 DAE 时的缓存（`NFInstNode.mo:1987`） |

主线：`NOT_INSTANTIATED → PARTIAL_CLASS → EXPANDED_CLASS[PARTIAL_TREE→EXPANDED_TREE] →(clone)→ EXPANDED_CLASS[INSTANTIATED_TREE] → INSTANCED_CLASS[FLAT_TREE]`；短类：`… → PARTIAL_CLASS(空) → EXPANDED_DERIVED → TYPED_DERIVED`；内建：`PARTIAL_BUILTIN → INSTANCED_BUILTIN`。前两步（partial/expand）发生在**定义节点**上且被所有使用者共享（只做一次）；从 `ClassTree.instantiate` 起发生在**每个实例各自的克隆**上。

`Class.Prefixes`（`:75-82`）= encapsulated / partial / final / innerOuter / replaceable；全默认时共用常量 `DEFAULT_PREFIXES`（`NFInst.mo:600-625`）。

#### 2.4 `ClassTree`（`NFClassTree.mo:72-116`）

| 状态 | 字段 | 能查什么 |
|---|---|---|
| `PARTIAL_TREE` `:73-81` | `tree: LookupTree`（名字 → `CLASS(i)`/`COMPONENT(i)`/`IMPORT(i)`，`NFLookupTree.mo`）；`classes: array<InstNode>`；`components: array<InstNode>`（**含 `REF_NODE` 占位**，长度 = 组件数 + extends 数）；`exts: array<InstNode>`；`imports`；`duplicates: DuplicateTree` | 只有**局部类和 import** 在 lookup tree 里；组件此时还没登记（`fromSCode :184-194` 注释） |
| `EXPANDED_TREE` `:83-93` | 同上 | lookup tree 已含**局部 + 继承**的全部类名和组件名（索引已按最终位置算好），但数组还没合并 |
| `INSTANTIATED_TREE` `:95-104` | `classes/components: array<Mutable<InstNode>>`（局部 + 继承，全量）；`localComponents: list<Integer>`（本类自己声明的组件下标）；`exts`；`imports`；`duplicates` | 局部和继承元素都可查。`Mutable` 这层间接让 redeclare / 重复元素替换能"一处改、派生类和基类视图同时变" |
| `FLAT_TREE` `:106-113` | `classes/components: array<InstNode>`；无 exts | 去掉 Mutable、去掉重复组件后的最终形态 |
| `EMPTY_TREE` `:115` | | |

`DuplicateTree.Entry`（`NFDuplicateTree.mo:45-52`）= `{entry: LookupTree.Entry, node: Option<InstNode>, children: list<Entry>, ty: DUPLICATE|REDECLARE|ENTRY}`：同名元素链，`REDECLARE` 链头是 `redeclare` 元素，孩子是被它替换的元素。

#### 2.5 `Component`（`NFComponent.mo:61-105`）

> 注意：任务书里的 "COMPONENT_DEF → UNTYPED → TYPED" 是旧版结构。**1.25.0 与 HEAD 都已合并成单一 `COMPONENT` record + `state` 枚举**（`git show v1.25.0:…/NFComponent.mo` 第 56-77 行与 HEAD 相同，仅 `comment` 由 `Option<SCode.Comment>` 变 `SCode.Comment`）。

| record | 字段 | 说明 |
|---|---|---|
| `COMPONENT_DEF` `:68-71` | `definition: SCode.Element`；`modifier: Modifier` | 初始状态（`Component.new :107-112`）。外层修饰先 `mergeModifier`（`:219-233`）累积在这里，等 `instComponent` 消费 |
| `COMPONENT` `:73-82` | `classInst: InstNode`；`ty: Type`；`binding`；`condition`；`attributes`；`comment`；`state: ComponentState`；`info` | 状态 `PartiallyInstantiated`（`NFInst.mo:1979`；ty=`UNTYPED(ty_node, dims)`、binding=`RAW_BINDING`）→ `FullyInstantiated`（`NFInst.mo:2707`；binding=`UNTYPED_BINDING`，dims=`Dimension.UNTYPED`）→ `Typed`（`NFTyping.mo:476`；ty 已定）→ `TypeChecked`（`NFTyping.mo:1053`；binding=`TYPED_BINDING`） |
| `ITERATOR` `:84-88` | `ty`, `variability`, `info` | for 迭代器 |
| `ENUM_LITERAL` `:90-93` | `literal: Expression`, `comment` | 枚举字面量作为组件挂在枚举类的 FLAT_TREE 里 |
| `TYPE_ATTRIBUTE` `:95-98` | `ty`, `modifier` | 内建属性 `start`/`unit`/… ；修饰值就存在 `modifier.binding` |
| `INVALID_COMPONENT` `:100`、`WILD` `:105` | | instance API 容错 / 后端 |

"条件为 false 被删"不是独立 record：`Component.isDeleted :957-970` = condition 已 typed 且为 false。

### 3. 三个阶段：partialInst / expand / instClass，以及压平后变量顺序

入口 `instantiate`（`NFInst.mo:427-440`）= `expand`（`:442-448` = `partialInstClass` + `expandClass`）+ `instClass`。

#### 3.1 `partialInstClass`（`NFInst.mo:539-598`）——只建"目录"，不看 extends

- 只对 `NOT_INSTANTIATED` 生效，幂等（`:544-555`）。
- 长类：`Class.fromSCode` → `ClassTree.fromSCode`（`NFClassTree.mo:118-225`）**一趟扫 SCode 元素表**：
  - `SCode.CLASS` → `InstNode.newClass(e, parent)` 放进 `classes[cls_idx]`，并**立刻**登记到 lookup tree（`:169-182`）；是 `redeclare`/class extends 的还要进 `duplicates`（`:178-180`）。
  - `SCode.COMPONENT` → `InstNode.newComponent(e)`（parent 先空着）放进 `components[comp_idx]`，**不进 lookup tree**（`:184-194`，注释：要等 extends 展开后才知道最终下标）。
  - `SCode.EXTENDS` → `InstNode.newExtends` 放进 `exts[ext_idx]`，**同时在 `components[comp_idx]` 放一个 `REF_NODE(ext_idx)` 占位**（`:196-206`，注释原文："also add a reference in the component array so we can preserve the order of components"）。
  - `SCode.IMPORT` → 存成 `UNRESOLVED_IMPORT`；随后 `Class.initImports`（`NFInst.mo:549`）把非限定 import 展开并登记进 lookup tree（`NFClassTree.mo:227-270`）。
  - class extends 在 `exts[1]`/`components[1]` 预留一格（`:143-146,157-164`），`redeclareClass` 时用 `setClassExtends` 填入被扩展的原类（`NFInst.mo:1772`）。
- enumeration：`Class.fromEnumeration` 直接给 `PARTIAL_BUILTIN` + `FLAT_TREE`（5 个属性 + 字面量，`NFClassTree.mo:272-312`）。
- 短类 / overload / pder：空树 `PARTIAL_CLASS`（`NFInst.mo:596`），留给 expand。
- 同名局部元素在这一步（类）或 `ClassTree.expand`（组件）报 `DOUBLE_DECLARATION_OF_ELEMENTS`（`NFClassTree.mo:1654-1677`）；局部元素覆盖同名 import（`:1665`）。

#### 3.2 `expandClass`（`NFInst.mo:640-753`）——解析 extends、算出最终下标

只对 `PARTIAL_CLASS` 生效（`:644-647`）。长类走 `expandClassParts`（`:710-753`）：

1. **先把 Class 换成 `EXPANDED_CLASS`（树仍是 `PARTIAL_TREE`）再干活**（`:726-728` `initExpandedClass`）——这是 extends 环检测的标记：`checkExtendsLoop`（`:807-845`）看到"`EXPANDED_CLASS` 里装着 `PARTIAL_TREE`"就知道该类正在展开中 → `EXTENDS_LOOP`；另外沿 `parentScope` 往上走，基类是自己的外层类也算环（`:832-840`）。
2. 对每条 extends 调 `expandExtends`（`:755-805`，经 `ClassTree.mapFoldExtends`，按 `exts` 数组顺序 = 源码顺序）：
   - `Lookup.lookupBaseClassName(base_path, scope=InstNode.parent(ext), …)`（`:780-781`）；
   - `checkReplaceableBaseClass`（路径每一段都不能 replaceable，`:847-888`）；
   - **递归 `expand(base_node)`**（`:789`）；
   - `ext := setNodeType(BASE_CLASS(scope, def, 原 nodeType), base_node)`（`:791`）——即 **exts[i] 从"extends 占位节点"被替换成"基类节点本身的一个带 BASE_CLASS 标记的值拷贝"，与基类定义节点共享同一个 `Pointer<Class>`**（此时还没克隆）。
   - 基类是内建类型（或派生自内建类型）→ 记到 `builtinExt`（`:797-799`），随后 `checkBuiltinTypeExtends`（`:908-921`：不许再有组件或第二条 extends）；基类叫 `ExternalObject` → `expandExternalObject`（`:741-742, 890-906`）。
3. `ClassTree.expand`（`NFClassTree.mo:353-430`）：顺序扫 `components` 数组，维护 `comp_idx`（下一个最终下标）：
   - 真组件：以 `COMPONENT(comp_idx)` 登记到 lookup tree，`comp_idx++`（`:378-391`）；
   - `REF_NODE`：记下 `(cls_idx-1, comp_idx-1)` 作为该 extends 的**偏移量**，再用 `countInheritedElements`（`:1960-1990`，递归数基类的组件数）把 `comp_idx` 往前推（`:396-401`）；
   - 然后对每个 extends 调 `ClassTree.expandExtends`（`:1992-2038`）：把基类 lookup tree 的每个条目 **index + 偏移** 后加入本类 lookup tree（`addInheritedElement :2040-2066`；import 不继承 `:2063`）。
   - 名字冲突 → `addInheritedElementConflict`（`:2068-2157`）：不报错，只在 `DuplicateTree` 里记一条，**lookup tree 保留下标小的那个**（`if new_id < old_id then entry := newEntry`，`:2096-2102, 2126-2133`）。是否"相同"留到实例化后再查（`:411-414` 注释）。
4. 结束：`EXPANDED_CLASS(EXPANDED_TREE, mod, cc_mod, prefs, Restriction.fromSCode(...))`（`NFInst.mo:748-751`）。

注意 expand 后**数组还没合并**，只是 lookup tree 知道了每个名字的最终下标；类的下标规则不同：继承来的类统一排在局部类之后（`cls_idx := arrayLength(clss)+1`，`NFClassTree.mo:370`），不保序（`:541-544` 注释："The order shouldn't matter for classes"）。

#### 3.3 `instClass` / `instClassDef`（`NFInst.mo:1099-1282`）——克隆 + 修饰 + 递归组件

`EXPANDED_CLASS` 分支（`:1144-1203`）的固定顺序：

| # | 动作 | 锚点 |
|---|---|---|
| 1 | `ClassTree.instantiate(node, parent)`：**克隆类节点**并合并数组（见下）。节点是 `BASE_CLASS` 则跳过（已随派生类一起克隆过，`:1148-1149`） | `:1151` |
| 2 | `updateComponentType(parent, node)`：把克隆出来的类节点写进父组件的 `classInst` | `:1154, 1284-1292` |
| 3 | 组装修饰：元素自身的 mod（class extends/cc）→ `ccMod` → 类上已有的 `cls.modifier` → 传入的 `outerMod`，外层优先 `Modifier.merge(outer, inner)` | `:1159-1165` |
| 4 | `modifyExtends`：每条 extends 子句上的修饰 `extends A(x=1)` 递归下发到基类树的元素上；并复查基类名现在解析到的还是同一个定义，否则 `FOUND_OTHER_BASECLASS` | `:1168, 1369-1417` |
| 5 | `applyExtendsVisibility`：protected extends 把继承来的类/组件节点改 protected（改的是 `Mutable` 里的值） | `:1171-1172, 1421-1468` |
| 6 | `applyModifier(mod, cls_tree, …)`：把本层修饰按名字拆开，`Component.mergeModifier` 进目标 `COMPONENT_DEF.modifier`，或 `Class.mergeModifier` 进目标类；找不到名字 → `MISSING_MODIFIED_ELEMENT`；改 protected → `NF_MODIFY_PROTECTED`；改纯 outer → `OUTER_ELEMENT_MOD` | `:1175, 1514-1621` |
| 7 | `redeclareElements`（元素式 redeclare 链）、`redeclareClasses`（修饰式类 redeclare） | `:1178-1184` |
| 8 | **`instExtends`**：对每个基类（按 exts 顺序、递归先基类的基类）实例化其**局部组件** | `:1187-1189, 1470-1512` |
| 9 | **`applyLocalComponents(instComponent)`**：实例化本类局部组件，按 `localComponents` 下标升序 | `:1192-1195`；`NFClassTree.mo:1105-1120` |
| 10 | `replaceDuplicates` + `checkDuplicates`：重复元素各槽位的 `Mutable` 全部指向被保留的节点；再 `InstNode.checkIdentical`，不同 → `DUPLICATE_ELEMENTS_NOT_IDENTICAL` | `:1198-1199`；`NFClassTree.mo:726-743, 1334-1380, 2320-2383`；`NFInstNode.mo:1659-1683` |
| 11 | `Restriction.checkClass` | `:1201` |

**`ClassTree.instantiate`（`NFClassTree.mo:432-662`）的克隆规则：**

- 类节点：`clsNode := InstNode.replaceClass(cls, clsNode)`（`:467-468`）——新 `Pointer<Class>`，内容先浅拷。
- extends：`arrayCopy(exts)` 后逐个 `BASE_CLASS.parent := instance` 并**递归 `instantiate`**（`:497-510`）——每个使用点都有自己的一份基类实例；返回的计数累加得总 `classCount/compCount`。
- 局部类：逐个 `InstNode.clone(c)` + `setParent(instance, c)`（`:522-539`）。`InstNode.clone`（`NFInstNode.mo:2025-2052`）只新建 `Pointer` 并清空 caches，`ClassTree.clone`（`NFClassTree.mo:687-704`）只对 `EXPANDED_TREE` 复制 `classes` 数组——是**浅克隆 + 惰性**（嵌套类真正被用到时才在自己的 instantiate 里继续克隆）。例外：type / operator record / operator 里的局部类不克隆，只改 parent（`is_typish`，`:518-528`）。
- 继承来的类：直接把基类 `INSTANTIATED_TREE.classes` 的 `Mutable` **原样拷进**新数组的尾部（`:545-560`）——派生类与基类视图共享同一个 `Mutable<InstNode>` 槽。
- 组件：**按 `old_comps` 顺序扫**（`:563-596`）：真组件 → `InstNode.cloneComponent(c, instance)`（`NFInstNode.mo:2054-2064`：新 `Pointer<Component>`，内容仍是 `COMPONENT_DEF`，`parent := instance`），outer 的当场 `linkInnerOuter`，下标记入 `local_comps`；`REF_NODE` → `instExtendsComps`（`:1567-1605`）把该基类 `INSTANTIATED_TREE.components` 的 `Mutable` 整段 `Array.copyRange` 进来（同样是共享槽，不再克隆）。
- 短类（`EXPANDED_DERIVED`，`:618-625`）：给 baseClass 套 `BASE_CLASS(clsNode, …)` 后递归 instantiate。
- 内建（`PARTIAL_BUILTIN` + `FLAT_TREE`，`:627-640`）：属性组件逐个 `cloneComponent`——所以每个 `Real x(start=1)` 都有自己的一套 `TYPE_ATTRIBUTE` 节点。
- 已是 `INSTANTIATED_TREE` → 什么都不做（`:471-472`）；`INSTANCED_CLASS` 且是基类 → `EXTENDS_LOOP`（`:644-651`）。

**`instComponent` → `instComponentDef` → `instTypeSpec`（`NFInst.mo:1858-2025, 2268-2309`）：**

1. 已不是 `COMPONENT_DEF` 的跳过（只做递归定义检查，`:1887-1892`）——这保证共享槽里的继承组件只被实例化一次。
2. 合并修饰：声明自带 mod ← innerMod ← `COMPONENT_DEF.modifier`（外层，优先）（`:1944-1951`）。
3. 先写入半成品 `COMPONENT(EMPTY_NODE, UNKNOWN, binding, condition, attr, …, PartiallyInstantiated)`（`:1977-1980`），注释解释了为什么要先写：instClass 里的查找需要组件已就位，而 classInst 要 instClass 才能产出——"To break the circle we leave the class node empty here"。
4. `instTypeSpec`：`Lookup.lookupClassName(typeSpec.path, scope=parent)` → `expand(node)` → `instClass(node, mod, attr, useBinding, instLevel, parent=组件节点)`（`:2284-2291`）。`instLevel >= 100` 时做递归定义检查（`:2286-2288`）。
5. 类型节点写回 `ty := UNTYPED(ty_node, dims)`（`:1987`）；`checkPartialComponent`、`checkBindingRestriction`、variability/connectorType 修正；无绑定的 parameter 用 `start` 补绑定并给 Warning（`updateParameterBinding :2357-2398`）。

**共享 vs 克隆小结**：定义节点上的 `PARTIAL_CLASS/EXPANDED_CLASS(EXPANDED_TREE)` 全局只算一次、所有使用点共享；**每个组件、每条 extends 使用点、根类**各有一份从 `ClassTree.instantiate` 起的私有克隆；package 走 `instPackage`（`NFInst.mo:1317-1367`）按定义节点缓存一份实例（`CachedData.PACKAGE` + `PackageCacheState`），函数走 func cache。

#### 3.4 压平后变量顺序由什么决定

结论（源码 + 探针 P02/P03/P04 三方一致）：

1. **组件顺序 = `FLAT_TREE.components` 数组顺序**。`Flatten.flattenClass` 就是 `for c in comps loop flattenComponent(...)`（`NFFlatten.mo:468-488`），复合组件当场递归（深度优先），结果表头插、末尾 `listReverseInPlace`（`NFFlatten.mo:358`）。
2. 该数组顺序 = **SCode 元素表里"组件与 extends 子句"的声明顺序，extends 在其子句出现的位置就地展开**（`REF_NODE` 占位 → `instExtendsComps` 整段拷入，§3.1/§3.3）。多层 extends 递归同理。public/protected 分段**不改变顺序**，只影响 visibility 标记（P02：`c3, z1, z2, z3, c4` 夹在中间，且 protected extends 使 `z1/z3` 也变 protected——`applyExtendsVisibility`）。
3. **重复继承元素保留下标最小（最靠前）的那个位置**，其余槽位在 `ClassTree.flatten`（`NFClassTree.mo:802-850`）里被剔除（`enumerateDuplicates` 收集非保留者下标 → `createFlatOffsets` → `flattenElementsWithOffset`）。P04：菱形继承 `Root` 两次 + 本地再声明 `q`，输出里 `r, q` 只出现在第一次继承的位置。
4. 自动生成的 inner 组件追加在**根类（的最后一个基类）组件数组末尾**（`insertGeneratedInners`，`NFInst.mo:3838-3844` → `appendComponentsToInstTree`）。P08：`T0` 排在最后。
5. 条件为 false 的组件在 flatten 时跳过（`NFFlatten.mo:541-545`）；纯 `outer` 组件不产生变量（`:531-533`）。
6. **方程顺序**（P03 实证）：先是各子组件的方程（按组件顺序，深度优先；`flattenClass` 先遍历组件再 `flattenSections`，`NFFlatten.mo:480-490`），然后是本类：**本类自己的方程在前，随后是各 extends 的方程且按 extends 的逆序**——因为 `instExpressions` 先依次处理 extends 再处理本地，每次都是 `Sections.join(new, accumulated)` 把新来的拼在**前面**（`NFInst.mo:2511-2514, 2533, 3205`；`NFSections.mo:192-209`）。P03 输出 `sub.s; a; b; x2; x1`。

### 4. 短类定义 / 类修饰 / 内建类型

#### 4.1 短类定义 `class A = B(mods)`、`type V = T[3]`

| 阶段 | 动作 | 锚点 |
|---|---|---|
| partialInst | 短类（`SCode.DERIVED`）得到**空树**的 `PARTIAL_CLASS`（不看右边） | `NFInst.mo:596` |
| expand | `expandClassDerived`：`Lookup.lookupBaseClassName(typeSpecPath, scope = InstNode.parent(node))` → 自指（`A = A`）报 `RECURSIVE_SHORT_CLASS_DEFINITION` → **`expand(ext_node)` 后 `InstNode.clone(ext_node)`** → 基类 partial 则本类也 partial（`:1053-1056`）→ 短类上的前缀 `Attributes.fromDerivedSCode`（`connector RealInput = input Real` 的 `input`）→ 维度存成 `Dimension.RAW_DIM(d, 作用域 = parent(node))` → `Class.EXPANDED_DERIVED(ext_node, mod, cc_mod, dims, prefs, attrs, res)` | `NFInst.mo:1017-1066` |
| expand（`polymorphic` 等 TCOMPLEX） | `expandClassDerivedComplex` → `PARTIAL_BUILTIN(Type.POLYMORPHIC)`，其它名字报 `LOOKUP_BASECLASS_ERROR` | `NFInst.mo:1068-1097` |
| instClass | `EXPANDED_DERIVED` 分支：`ClassTree.instantiate`（给 baseClass 套 `BASE_CLASS(clsNode,…)` 再递归克隆，`NFClassTree.mo:618-625`）→ `nodeType := DERIVED_CLASS(原类型)` → 修饰合并顺序与长类相同（右边的 `B(mods)` 是"元素自身 mod"，由 `instElementModifier` 从 SCode 取，最内层；外部修饰优先）→ `Attributes.mergeDerivedAttributes` → **递归 `instClass(base_node, mod, attributes, …, par)`**，修饰整体下传给基类 → `cls.dims := arrayCopy(cls.dims)`（每个实例自己的维度数组，因为后面原地 type）→ `updateComponentType` | `NFInst.mo:1205-1231` |
| instExpressions | 先递归基类，再把 `RAW_DIM` 实例化成 `Dimension.UNTYPED`；record 短类顺带建构造函数 | `NFInst.mo:2544-2558` |
| typing | `typeClassType`：type 维度 → 递归取基类 type → `Type.liftArrayLeftList(ty, dims)`（短类维度加在基类型**左边**）→ `Class.TYPED_DERIVED(ty, baseClass, restriction)`；随后 `typeComponents` 对**无维度**的 `TYPED_DERIVED` 直接用基类的 Class 覆盖本节点（"collapse"，只保留本类 restriction），有维度的保留 | `NFTyping.mo:295-303, 176-198` |
| flatten | `TYPED_DERIVED` → 直接转发到 `baseClass` | `NFFlatten.mo:494-499` |

探针 P10 实证：`type Length = Real(unit="m", min=0)`；`type Vec3 = Length[3](each start=1)`；`Length[2] w(each max=10)`；`model M = N(p=2)` + `M m3(p=3)`（外层 3 覆盖短类的 2）；`connector RealInput = input Real` → `input Real u`。

`Class.classTree`/`getModifier`/`lastBaseClass` 等访问器对 `EXPANDED_DERIVED`/`TYPED_DERIVED` 都是"透传到 baseClass"（`NFClass.mo:349, 352, 785-795`），所以对短类做名字查找 = 在基类实例里查。

#### 4.2 类修饰如何落到元素上

- 修饰先以 `Modifier` 树形式**挂在目标上等消费**：目标是组件 → `COMPONENT_DEF.modifier`（`Component.mergeModifier`，`NFComponent.mo:219-233`）；目标是类 → `PARTIAL_CLASS/EXPANDED_CLASS/EXPANDED_DERIVED/PARTIAL_BUILTIN.modifier`（`Class.mergeModifier`，`NFClass.mo:446-477`；`applyModifier` 对类目标先 `partialInstClass` 再 `replaceClass`（断开与定义节点的共享）并清 package cache，`NFInst.mo:1604-1607`）。
- 类级 `A = B` 形式（对类用绑定而非 redeclare）→ `MISSING_REDECLARE_IN_CLASS_MOD`（`NFInst.mo:1116-1120`）。
- 对内建类型的修饰：`PARTIAL_BUILTIN` 分支（`NFInst.mo:1246-1260`）克隆属性组件后 `applyModifier` 走 `FLAT_TREE` 分支（`:1536-1551`），直接 `mergeModifier` 到对应 `TYPE_ATTRIBUTE.modifier`；未知属性名 → `MISSING_MODIFIED_ELEMENT`。属性值在 `instExpressions` 的 `INSTANCED_BUILTIN` 分支实例化（`:2560-2566, 2729-2734`）；对属性写 `redeclare` → `INVALID_REDECLARE_IN_BASIC_TYPE`（`:2674-2679`）。
- **压平输出里属性的顺序是内建 `FLAT_TREE` 的组件数组顺序，与源码里修饰书写顺序、来自短类还是组件无关**：`Class.getTypeAttributes`（`NFClass.mo:552-571`）按数组遍历。Real 的顺序 = `quantity, unit, displayUnit, min, max, start, fixed, nominal, unbounded, stateSelect, uncertain`（`NFBuiltin.mo:168-205`）。探针 P11 实证。

#### 4.3 内建类型 / 变量 / 函数挂在哪里

| 东西 | 表示 | 如何被找到 | 锚点 |
|---|---|---|---|
| `Real` `Integer` `Boolean` `String` | **编译期常量** `InstNode.CLASS_NODE`：`Pointer.createImmutable(PARTIAL_BUILTIN(Type.X, X_CLASS_TREE(FLAT_TREE), NOMOD, DEFAULT_PREFIXES, Restriction.TYPE))`，`parentScope = EMPTY_NODE`，`nodeType = BUILTIN_CLASS`，definition 是伪造的空 `SCode.CLASS`（`NFBuiltin.Elements`） | **不在 `<top>` 的类树里**。`Lookup.lookupFirstIdent` 每次查名字**先**试 `lookupSimpleBuiltinName`（硬编码 4 个名字），命中即 `LookupState.PREDEF_CLASS`，根本不走作用域链——所以用户无法遮蔽它们 | `NFBuiltin.mo:85-118, 209-214, 253-258, 285-290, 320-325`；`NFLookup.mo:655-674, 800-810` |
| 内建属性 | 各类型 `FLAT_TREE.components` 里的 `COMPONENT_NODE(name, NONE(), PUBLIC, TYPE_ATTRIBUTE(ty, NOMOD))`；lookup tree 是手写常量（`makeBuiltinLookupTree` 生成后粘贴）。Real 11 个；Integer 5 个（quantity,min,max,start,fixed）；Boolean/String 3 个（quantity,start,fixed） | 只通过修饰名查（`ClassTree.lookupElement`）；注释说明不允许点号访问 | `NFBuiltin.mo:131-133, 141-207, 217-251, 261-283, 296-318` |
| `enumeration` | 每个枚举定义在 partialInst 时现造：`PARTIAL_BUILTIN(Type.ENUMERATION(path, lits), FLAT_TREE[5 属性 + 字面量 ENUM_LITERAL 组件], …, Restriction.ENUMERATION)`；`ENUM_NODE` 常量代表 `enumeration(:)` | 字面量是枚举类里的组件，`Color.green` 走普通 cref 查找 | `NFInst.mo:589-594, 627-638`；`NFClassTree.mo:272-312`；`NFBuiltin.mo:330-347` |
| `StateSelect` `AssertionLevel` `Uncertainty` | **普通 Modelica 源码**，在 `NFModelicaBuiltin.mo`（安装后 `lib/omc/NFModelicaBuiltin.mo`）里 `type StateSelect = enumeration(...) annotation(__OpenModelica_builtin = true)`；`NFBuiltin.STATESELECT_TYPE` 只是给 `stateSelect` 属性定类型用的 `Type` 常量 | 随 builtin SCode 拼到用户程序前，成为 `<top>` 的普通元素；`makeTopNode` 见到 `__OpenModelica_builtin` 注解就把节点标成 `BUILTIN_CLASS`（encapsulated 作用域里查不到的名字会回退到"top 里的 builtin"——`require_builtin`） | `NFModelicaBuiltin.mo:36-50, 84`；`FFrontEnd/FBuiltin.mo:376`；`CevalScriptBackend.mo:3684-3699`；`NFInst.mo:510-514, 531-537`；`NFLookup.mo:470-475` |
| `Clock` | `NFModelicaBuiltin.mo:52-54` 里有个占位 `partial class Clock`（防止用户再声明），`makeTopNode` 用 `ClassTree.replaceClass(NFBuiltin.CLOCK_NODE, elems)` 顶掉它；`CLOCK_NODE` 的函数缓存里预装 5 个 Clock 构造函数重载 | `<top>` 类树 | `NFInst.mo:516-519`；`NFBuiltin.mo:389-406` |
| `ExternalObject` | `NFModelicaBuiltin.mo:56-58` 的 `partial class`；`expandClassParts` 看到 builtin 基类名叫 `"ExternalObject"` 就走 `expandExternalObject` | 普通查找 | `NFInst.mo:741-742, 890-906` |
| `time` | 常量 `COMPONENT_NODE("time", NONE(), PUBLIC, COMPONENT(REAL_NODE, Type.REAL, EMPTY_BINDING, EMPTY_BINDING, INPUT_ATTR, noComment, TypeChecked, dummyInfo), EMPTY_NODE, NORMAL_COMP)` | `lookupSimpleCref` **先**试 `lookupSimpleBuiltinCref`：`"time"` → `(TIME, TIME_CREF, PREDEF_COMP)`；另三个硬编码名是 `Boolean`（类型名当 cref）、`Integer`/`String`（转换函数，`NFBuiltinFuncs`）。foundScope 记为 top | `NFBuiltin.mo:411-425, 443`；`NFLookup.mo:812-833, 851-853` |
| 内建函数 `der` `sin` `abs` `Connections.*` … | `NFModelicaBuiltin.mo` 里的普通 `function … external "builtin"` 声明 | `<top>` 的普通类；需要特殊类型规则的在 typing 时由 `NFBuiltinCall` 按名字接管（本报告范围外） | `NFModelicaBuiltin.mo:59, 177, 718` |
| 注解内可见的类（`Icon`、`Placement`…） | 另一份 SCode（`annotation_p`）→ `<annotations>` 节点，存在 `TOP_SCOPE.annotationScope`，全部标 `BUILTIN_CLASS` | 只有 `InstContext.inAnnotation` 时 `lookupSimpleName`/`lookupSimpleCref` 先查它 | `NFInst.mo:484-505`；`NFLookup.mo:449-456, 855-863` |

注意内建常量节点用 `Pointer.createImmutable`：任何对它们的 `Pointer.update` 会段错误（`NFBuiltin.mo:122-126` 注释）。所以对内建类型的"实例化"一定先 `replaceClass`/`cloneComponent` 出可变副本（`ClassTree.instantiate` 的 `PARTIAL_BUILTIN` 分支，`NFClassTree.mo:627-632`）。

### 5. inner / outer

#### 5.1 机制

1. **判定**：`isOuter` = `outer` 或 `inner outer`；`isInner` = `inner` 或 `inner outer`；`isOnlyOuter` = 仅 `outer`（组件：`NFComponent.mo:629-670`；类：从 SCode 前缀读，`NFInstNode.mo:1406-1443`）。
2. **链接时机 = `ClassTree.instantiate` 克隆元素的那一刻**（不是查找 cref 时）：
   - 局部类：`if InstNode.isOuter(c) then checkOuterClass(c); c := linkInnerOuter(c, parent_scope)`（`NFClassTree.mo:530-535`）。`checkOuterClass`（`:2415-2448`）：纯 outer 类必须是**无修饰的短类定义**，否则 `OUTER_ELEMENT_MOD` / `OUTER_LONG_CLASS`。
   - 组件：`cloneComponent` 后 `if InstNode.isOuter(node) then node := linkInnerOuter(node, inst_scope)`（`:568-581`）。
3. `linkInnerOuter`（`NFClassTree.mo:2393-2413`）= `Lookup.lookupInner` + 种类检查（类/组件不一致 → `FOUND_WRONG_INNER_ELEMENT`）+ 包成 `InstNode.INNER_OUTER_NODE(inner_node, outerNode)` **放回组件/类数组的原槽位**。
4. `Lookup.lookupInner`（`NFLookup.mo:393-424`）：从起始作用域开始，`Class.lookupElement(name, getClass(cur_scope))`，命中且 `isInner` 即返回；否则 `cur_scope := InstNode.instanceParent(cur_scope)`（**沿实例树**往上，不是词法作用域）；到 `ROOT_CLASS` 仍没有 → `generateInner`。注意起始作用域是"正在被实例化的那个组件节点"，此刻它的 `classInst` 还是 `EMPTY_NODE`（§3.3 第 3 步），`getClass` 失败 → 自然落到 else 分支从父实例开始找，所以不会找到自己。
5. **自动生成 inner**：`generateInner`（`NFLookup.mo:1069-1108`）在 `TOP_SCOPE.generatedInners`（按名字的 map）里查/建；`makeInnerNode`（`:1110-1157`）拷贝 outer 节点并把其 SCode 前缀改成 `inner`，nodeType = `GENERATED_INNER`。同名 outer 共用一个生成的 inner。
6. 根类实例化完后 `insertGeneratedInners`（`NFInst.mo:3778-3845`）：逐个 `checkTopLevelOuter` → **无条件给 Warning `MISSING_INNER_ADDED`**（`:3810-3813`）→ 组件型的 `instComponent(n, DEFAULT_ATTR, NOMOD, true, 0, CLASS)`（`:3820`）→ 若其类有 `missingInnerMessage` 注解再给 Notification `MISSING_INNER_MESSAGE`（`:3824-3831`）→ 追加到根类（`Class.lastBaseClass(node)`）的 `INSTANTIATED_TREE.components` 末尾（`:3839-3844`）。类型的生成 inner 不加入（类不进压平结果）。
7. `checkTopLevelOuter`（`NFInst.mo:3847-3883`）：在根类作用域里 `lookupSimpleName(name)`；找到且是 inner（= 根类自己就有个 outer，被自己生成的 inner 命中）→ `TOP_LEVEL_OUTER`（消息定义级别是 WARNING，但此处用 `addSourceMessageAsError` 升为 Error；`--allowNonStandardModelica=nonStdTopLevelOuter` 或 RELAXED 时保持 Warning）；找到但不是 inner → `MISSING_INNER_NAME_CONFLICT`。
8. 之后各趟对 `INNER_OUTER_NODE` 的取向：`instComponent`、`instComponentExpressions` 用 **`resolveInner`**（`NFInst.mo:1883, 2698`）——即"实例化 outer 槽位"实际是去实例化它的 inner（已实例化则跳过）；`applyModifier`、`updateImplicitVariabilityComp`、`typeComponent`、`flattenComponent` 用 **`resolveOuter`**（`NFInst.mo:1576, 3940`；`NFTyping.mo:449`；`NFFlatten.mo:535`），且 typing/flatten 开头先 `isOnlyOuter → return`（`NFTyping.mo:445`；`NFFlatten.mo:531`）——纯 outer 不产生变量。
9. cref 指向 outer 时：`lookupLocalSimpleCref` 把找到的 `INNER_OUTER_NODE` 立刻换成 inner 并把 `foundScope` 改成 inner 的 parent（`NFLookup.mo:930-934`）；`resolveInnerCref`（`:1033-1067`）把 cref 前缀砍到 inner 的父为止——`a.b.c.d.e`（e 是 outer，inner 在 b）→ `a.b.e`。P07/P09 输出里的 `T0`、`mid.T0` 即此结果。
10. outer 上的修饰：`checkOuterComponentMod`（`NFInst.mo:2242-2266`，声明自带修饰，如 `outer Real T0 = 5`）与 `applyModifier` 里的 `isOnlyOuter` 分支（`:1589-1599`，外部施加的修饰）都报 `OUTER_ELEMENT_MOD`。

#### 5.2 探针结论

| 探针 | 现象 |
|---|---|
| P07 | inner 在根、outer 在两层之下：outer 不产生变量，引用被改写成 `T0` |
| P08 | 无 inner：自动生成，变量 `Real T0;` **排在最后**，exit=0，Warning 原文：`Warning: An inner declaration for outer component T0 could not be found and was automatically generated.`（位置 = outer 声明处；`%s %s` = `InstNode.typeName` + 名字，故有 "component"/"class" 字样） |
| P09 | `inner outer`：`mid.T0` 自成变量并服务下层，`mid.leaf.t = mid.T0 + 1.0`；`leaf.t = T0 + 1.0` |
| P12 | `outer Real T0 = 5` → `Error: Modifier ' = 5' found on outer element T0.` |
| P13 | outer 组件遇到同名 inner 类 → `Error: Found inner class X instead of expected component.`（前置一条 `Notification: From here:` 指向 outer） |
| P14 | 根类自己有 outer → `Error: The model can't be instantiated due to top-level outer element 'T0', it may only be used as part of a simulation model.` |
| P15 | 无 inner 但根类有同名非 inner 元素 → `Error: An inner declaration for outer element 'T0' could not be found, and could not be automatically generated due to an existing declaration of that name.` |
| P16 | `outer model M = MBase;` + `inner model M = MImpl;` → 用 MImpl（`leaf.m.q`, `leaf.m.extra`） |
| **P17** | **OMC 1.25.0 的缺陷**：`inner outer` 组件若下层没有任何 outer 引用它，会报 `Internal error NFTyping.typeComponent got noninstantiated component T0`。原因正是第 8 点：`instComponent` 对 `INNER_OUTER_NODE` 只实例化 inner 端，`inner outer` 自己那一端只有在被下层 outer 当作 inner 解析到时才被实例化（P09 能过就是因为有 `Leaf`）。**ymc 不应复刻**，应记入分歧账本。 |

### 6. 实例化阶段的常见报错

范围：只列 **expand（partialInstClass/expandClass）+ instClass** 这两趟会抛的错，以及紧跟其后仍属"结构实例化"的收尾检查（`checkPartialClass`、`insertGeneratedInners`）。类型检查那趟（`typeClass`）单独的错（维度不匹配、绑定类型不符、variability 越界等）不在此列，但**条件变量性**和**内建属性维度**因为和实例化耦合紧，末尾单列一小节标明它们在 typing 才触发。消息 id 与文案取自 `Util/Error.mo`（HEAD，行号已复核）；触发点为 `NFFrontEnd/*.mo`。`addMultiSourceMessage` 会先打一条 `Notification: From here:` 指向第二个 info（见探针原文里的两行）。

#### 6.1 结构 / 继承类错误（expand 趟）

| 消息名 (id) | 文案模板 | 触发函数:行 | 探针 |
|---|---|---|---|
| `EXTENDS_LOOP` (19) | `extends %s causes an instantiation loop.` | `checkExtendsLoop`（`NFInst.mo:807-845`，由 `expandExtends :782` 调；另 `NFClassTree.mo:648` 在克隆时撞到 `INSTANCED_CLASS` 基类） | **P21** |
| `RECURSIVE_SHORT_CLASS_DEFINITION` (153) | `Recursive short class definition of %s in terms of %s.` | `expandClassDerived`（`NFInst.mo:1041`，`referenceEq(ext_node, node)` 判自指 `A = A`） | **P22** |
| `LOOKUP_BASECLASS_ERROR` (39) | `Base class %s not found in scope %s.` | `Lookup.lookupBaseClassName`（由 `expandExtends :780-781` / `expandClassDerived :1037` 调）——基类名解析失败，最常见的 extends 错 | 未采（源码锚点确凿） |
| `REPLACEABLE_BASE_CLASS` (172) | `Class '%s' in 'extends %s' is replaceable, the base class name must be transitively non-replaceable.` | `checkReplaceableBaseClass`（`NFInst.mo:883`，`expandExtends` 里对基类路径逐段查 replaceable） | **P28** |
| `BUILTIN_EXTENDS_INVALID_ELEMENTS` (293) | `A class extending from builtin type %s may not have other elements.` | `checkBuiltinTypeExtends`（`NFInst.mo:917`；另 `:1760` class extends 路径）——`extends Real` 后还有组件/第二条 extends | **P29** |
| `DOUBLE_DECLARATION_OF_ELEMENTS` (161) | `An element with name %s is already declared in this scope.` | `ClassTree` 建/展开 lookup tree（`NFClassTree.mo:1672` 组件冲突、`:1742` 类冲突）——同作用域**本地**重名 | **P06** |
| `INVALID_REDECLARATION_OF_CLASS` (162) | `Invalid redeclaration of class %s, class extends only allowed on inherited classes.` | `class extends X` 的 X 不是继承来的类（`Error.mo:403-404`；NFInst redeclareClasses 路径） | 未采（源码锚点确凿） |

注：**重复继承同一基类**不一定报错——菱形继承里两条路径带来同名且**语法相同**的元素时静默去重（P04，保留最靠前槽位，§3.4 第 3 点）；只有**不相同**才报下一节的 `DUPLICATE_ELEMENTS_NOT_IDENTICAL`。

#### 6.2 组件 / 修饰 / redeclare 错误（instClass 趟）

| 消息名 (id) | 文案模板 | 触发函数:行 | 探针 |
|---|---|---|---|
| `RECURSIVE_DEFINITION` (92) | `Declaration of element %s causes recursive definition of class %s.` | `checkRecursiveDefinition`（`NFInst.mo:2311-2345`，由 `instComponent :1890` 调；沿实例父链 `referenceEq(definition,…)` 比对；另 `:2287` 在 `instLevel>=100` 兜底） | **P20** |
| `PARTIAL_COMPONENT_TYPE` (375) | `Component '%s' has partial type '%s'.` | `checkPartialComponent`（`NFInst.mo:2106-2128`，由 `instComponentDef :2003` 调）——**例外**：`outer` 组件允许 partial 类型（`:2122` 判 `innerOuter <> OUTER`） | **P19** |
| `MISSING_MODIFIED_ELEMENT` (214) | `Modified element %s not found in class %s.` | `applyModifier`（`NFInst.mo:1543`；`:1612` 短类基类；内建属性走 `:1536` FLAT_TREE 分支；redeclare 走 `NFClassTree.mo:2505`）——修饰名在目标类里没有 | **P25** |
| `NF_MODIFY_PROTECTED` (396) | `Protected element '%s' may not be modified, got '%s'.` | `applyModifier`（`NFInst.mo:1579`，`addMultiSourceMessage`） | **P26** |
| `MISSING_REDECLARE_IN_CLASS_MOD` (56) | `Missing redeclare keyword on attempted redeclaration of class %s.` | `NFInst.mo:1117`——对类成员用绑定式修饰 `Sub a(M = Base)` 而没写 `redeclare` | **P27** |
| `FOUND_OTHER_BASECLASS` (306) | （见 `Error.mo:689`） | `modifyExtends`（`NFInst.mo:1402`）——下发 extends 修饰时复查基类名，现在解析到了另一个定义 | 未采（源码锚点确凿） |
| `DUPLICATE_ELEMENTS_NOT_IDENTICAL` (90) | `Duplicate elements (due to inherited elements) not identical:\n  first element is:  %s\n  second element is: %s` | `InstNode.checkIdentical`（`NFInstNode.mo:1677`，由 `checkDuplicates`/`replaceDuplicates` 触发，§3.3 第 10 步） | **P05** |

#### 6.3 根类 / restriction 错误（instClass 收尾）

| 消息名 (id) | 文案模板 | 触发函数:行 | 探针 |
|---|---|---|---|
| `INST_PARTIAL_CLASS` (38) | `Illegal to instantiate partial class %s.` | `checkPartialClass`（`NFInst.mo:4175-4184`，`instantiateRootClass :404` 在 instClass 之后调；非 RELAXED 才报） | **P18** |
| `INST_INVALID_RESTRICTION` (525) | `Cannot instantiate %s due to class specialization %s.` | `checkInstanceRestriction`（`NFInst.mo:4186-4204`，由 `lookupRootClass` 调）——根类是 `package`/`function`（不可实例化的 restriction） | **P30** |

#### 6.4 inner / outer 错误（已在 §5 详列，此处汇总消息名）

| 消息名 (id) | 文案模板 | 触发函数:行 | 探针 |
|---|---|---|---|
| `OUTER_ELEMENT_MOD` (307) | `Modifier '%s' found on outer element %s.` | `checkOuterComponentMod`（`NFInst.mo:2258`，声明自带修饰）；`applyModifier` `isOnlyOuter` 分支（`:1590`，外部修饰）；`checkOuterClass`（`NFClassTree.mo:2432`，outer 类带修饰） | **P12** |
| `FOUND_WRONG_INNER_ELEMENT` (305) | `Found inner %s %s instead of expected %s.` | `linkInnerOuter`（`NFClassTree.mo:2406`）——outer 组件却找到 inner 类（或反之） | **P13** |
| `TOP_LEVEL_OUTER` (379, **定义级别 WARNING**) | `The model can't be instantiated due to top-level outer element '%s', …` | `checkTopLevelOuter`（`NFInst.mo:3867` 用 `addSourceMessageAsError` 升为 Error；`:3869` RELAXED/`nonStdTopLevelOuter` 时保持 Warning） | **P14** |
| `MISSING_INNER_NAME_CONFLICT` (380) | `An inner declaration for outer element '%s' could not be found, and could not be automatically generated due to an existing declaration of that name.` | `checkTopLevelOuter`（`NFInst.mo:3872`，`addMultiSourceMessage`） | **P15** |
| `MISSING_INNER_ADDED` (309, **WARNING**) | `An inner declaration for outer %s %s could not be found and was automatically generated.` | `generateInner` 后 `insertGeneratedInners`（`NFInst.mo:3811`）——**不报错，自动补 inner** | **P08** |

#### 6.5 边界：以下相邻错误在 typing 趟才触发（不属实例化，但实现时易误判归属）

| 消息名 (id) | 文案模板 | 触发函数:行 | 探针 | 说明 |
|---|---|---|---|---|
| `COMPONENT_CONDITION_VARIABILITY` (100) | `Component condition must be parameter or constant expression (in %s).` | `NFTyping.mo:1232` | **P24** | 条件组件的 `if` 表达式在 inst 只存 RAW，variability 检查要等 typing。P23 里 parameter 条件正常删除组件（`Sub a if use` 被删，输出无 `a.s`） |
| `DUPLICATE_ELEMENTS_NOT_IDENTICAL`（typing 侧）| 同 90 | `NFTyping.mo` 对 `TYPE_ATTRIBUTE` 也复查 | — | 内建属性重复修饰的相等性在 typing 比 |
| 维度非法 (`INST_ARRAY_DIM_*` / 326 / 1091) | `Invalid dimensions %s …, no more than one dimension may have size > 1.` 等 | `NFTyping.mo` / `NFInstUtil`（dimension typing） | **未确认**（inst 趟只把维度存成 `Dimension.RAW_DIM`→`UNTYPED`，§3.3/§4.1；合法性全部推迟到 typing，未采探针） | 数组维度错**不在实例化趟报**，ymc 若把维度检查提前会与 OMC 行为不符 |

### 7. 对 ymc 的接口级建议（结构与不变量）

只讲数据结构与不变量，不写实现代码。落地顺序与陷阱在文末 `## 对 ymc 的建议`。

#### 7.0 为什么用 arena + Id 而不是照抄指针

OMC 的核心机制是"MetaModelica record 是值 + `Pointer<Class>`/`Pointer<Component>`/`Mutable<InstNode>` 三种可变盒子共享/断开"（§2.1 可变性模型）。C++ 里对应关系：

- `InstNode` 值语义 → arena 里的 `NodeId`（`uint32` 索引 + 代际位，或纯下标）。"得到新 InstNode 值但共享同一 Pointer<Class>" → **多个 `InstNode` 记录持同一个 `ClassId`**。
- `Pointer<Class>` = 一个可变单元，被多个节点共享 → arena 里的 `ClassId`，`Class` 存在 `class_arena[ClassId]`，`Pointer.update` = 原地改 `class_arena[id]`，所有持该 id 的节点自动看到。
- `Pointer.create`（replaceClass，"克隆"）= 在 `class_arena` 里**新分配一格**、内容浅拷、把节点的 `ClassId` 改指新格 → 断开共享。
- `Mutable<InstNode>`（ClassTree 数组元素）= 再加一层 `SlotId`（indirection），让 redeclare / 重复元素"一处改、派生类与基类视图同步"（§2.4 `INSTANTIATED_TREE`）。**这一层不能省**：数组里存的不是 `NodeId` 而是 `SlotId`，`slot_arena[SlotId] -> NodeId`。

统一 Id 化的收益：克隆 = memcpy 一格 + 改若干 Id 字段；父子/继承全是 Id，没有裸指针失效问题；序列化/dump/diff 直接按 arena 顺序走。

#### 7.1 `InstNode`（arena `node_arena`，`NodeId`）

字段（对应 §2.1）：

- `kind`: 枚举 `{Class, Component, InnerOuter, Ref, Name, ImplicitScope, Iterator, Empty}`（EMPTY = 哨兵 `NodeId{0}`）。
- `name: StrId`（interned；空节点为空）。
- Class 节点：`def: SCodeId`（指向 absyn/scode arena；`<top>` 用合成 SCode）；`cls: ClassId`（共享单元）；`parent_scope: NodeId`；`node_type: InstNodeType`；`visibility`；`caches`（函数缓存 slot + package 缓存 slot，见 §7.4）。
- Component 节点：`def: optional<SCodeId>`；`comp: ComponentId`（共享单元）；`parent: NodeId`（"所属实例"）；`node_type`；`visibility`。
- InnerOuter 节点：`inner: NodeId`, `outer: NodeId`（§5）。
- Ref 节点：`ext_index: u32`（**只出现在 ClassTree 的 components 槽数组里**，占位第 index 条 extends 的组件，§3.1）。

**不变量**：
1. `EMPTY` 是 id 0，任何"往上走"（parent/parentScope/topScope）以 `EMPTY` 收尾；不得对 `EMPTY` 解引用 cls/comp。
2. Class 节点的 `parent_scope` 有二相：**未实例化=词法外层类**；被 `ClassTree.instantiate` 克隆后=**所在实例**（组件节点或根类节点）。同一 `NodeId` 的 `parent_scope` 会被 `setParent` 就地改写——所以"取词法外层"必须在实例化前做，或另存一份原始外层。（§2.1 父指针）
3. extends 用 **Class 节点**表示（不是独立 record）：`kind=Class`、`node_type=BASE_CLASS{parent, def(那条 EXTENDS), inner_ty}`、`name` = 基类路径末段。expand 后它与基类定义节点**共享 `cls: ClassId`**；`ClassTree.instantiate` 时才把 `parent` 改成新实例并递归克隆。
4. 内建常量节点（Real/Integer/…、time）必须是**只读**的（对应 `Pointer.createImmutable`，§4.3）：arena 里划一段"不可 update"区，或克隆时强制先分配可变副本。任何对内建 ClassId 的原地 update 都是 bug。

#### 7.2 `InstNodeType`（打在 `InstNode.node_type` 上的 tagged union）

值与载荷照 §2.2：`NormalClass` / `BaseClass{parent:NodeId, def:SCodeId, inner_ty:Box<InstNodeType>}` / `DerivedClass{inner_ty}` / `BuiltinClass` / `TopScope{annotation_scope:NodeId, generated_inners:Map<StrId,NodeId>}` / `RootClass{parent:NodeId, context:optional<Path>}` / `NormalComp` / `RedeclaredComp{parent:NodeId}` / `RedeclaredClass{parent, original_ty, original_node}` / `GeneratedInner` / `ImplicitScope`。

**不变量**：`BaseClass.parent` 在 expand 后指派生类定义、在 instantiate 后指派生实例（两阶段，别混）；`RootClass` 只出现在被实例化的那一个根节点上；`GeneratedInner` 节点全部登记在 `TopScope.generated_inners`（同名 outer 共用一个，§5.5）。

#### 7.3 `Class`（arena `class_arena`，`ClassId`）状态机

一个 `Class` = `{ state: ClassState, tree: ClassTreeId, modifier: ModId, cc_mod: ModId, prefixes: ClassPrefixes, restriction: Restriction, ... }`，`state` 是枚举 tag，按 tag 决定哪些字段有效（照 §2.3）：

`NotInstantiated → PartialClass → ExpandedClass → InstancedClass`（长类主线）；`PartialClass(空) → ExpandedDerived{base:NodeId, dims} → TypedDerived{base}`（短类）；`PartialBuiltin{ty} → InstancedBuiltin{ty}`（内建）；`DaeType`（转 DAE 缓存，ymc 若不复用旧后端可暂缺）。

**核心不变量（决定共享/克隆边界，最重要）**：
1. `PartialClass` 与 `ExpandedClass`（树=`ExpandedTree`）这两个状态是**定义节点上算一次、全局共享**的（partialInstClass/expandClass 幂等，§3.1/§3.2）。所有使用点持同一 `ClassId`。
2. 从 `ClassTree.instantiate` 起（`ExpandedClass` 树变 `InstantiatedTree` → `InstancedClass` 树变 `FlatTree`）发生在**每个使用点各自的克隆**上（新 `ClassId`）。
3. 状态**只能单向前进**（arena 里用 assert 守：`advance_state(old,new)` 只允许上表相邻迁移）；`applyModifier` 对类目标会先 `partialInstClass` 再 `replaceClass`（断共享）并清 package cache——即"改一个类的修饰"必然伴随克隆，绝不能就地改共享定义（§4.2）。
4. 短类的访问器（classTree/getModifier/lastBaseClass）对 `ExpandedDerived`/`TypedDerived` 一律**透传到 base**（§4.1 末）；ymc 的 `Class::classTree(id)` 要内建这层转发，否则对短类查名字会落空。
5. `initExpandedClass` 占位（`ExpandedClass` + 树仍是 `PartialTree` + restriction=UNKNOWN）是 extends 环检测的信号（§3.2 第 1 点）——`checkExtendsLoop` 靠"`ExpandedClass` 装着 `PartialTree`"判定"正在展开中"。这个中间态不能跳过。

#### 7.4 `ClassTree`（arena `tree_arena`，`ClassTreeId`）

`ClassTree` = `{ state, lookup: LookupTree, classes: Vec<NodeId|SlotId>, components: Vec<NodeId|SlotId>, exts: Vec<NodeId>, imports, duplicates: DuplicateTree, local_components: Vec<u32> }`，字段有效性按 state（照 §2.4）：`PartialTree`（组件数组含 `Ref` 占位、组件**未进** lookup）→ `ExpandedTree`（lookup 含本地+继承的最终下标、数组未合并）→ `InstantiatedTree`（classes/components 是 **SlotId** 数组、全量本地+继承）→ `FlatTree`（去 Slot、去重复、无 exts，最终形态）。

**不变量**：
1. `components` 数组在 `PartialTree`/`ExpandedTree` 阶段长度 = 真组件数 + extends 数（`Ref` 占位），**顺序 = SCode 元素声明顺序**——这是压平变量顺序的根（§3.4）。ymc 建树时必须**一趟扫 SCode 保序**，extends 就地放 `Ref`。
2. 组件下标保序、继承元素"就地展开"（`Ref` → `instExtendsComps` 整段拷入基类槽）；**类下标不保序**（继承类统一排局部类之后，§3.2 末）——两套数组规则不同，别统一。
3. `InstantiatedTree` 起 classes/components 存 `SlotId` 而非 `NodeId`（§7.0 第 4 点）：继承来的元素与基类**共享同一 SlotId**，redeclare/去重改 slot 内容即全视图同步。
4. 重复元素在 `FlatTree` 化时按 `DuplicateTree` 剔除，**保留 lookup 下标最小（最靠前）者**的槽位（§3.4 第 3 点，`if new_id < old_id`）。
5. lookup tree 条目区分 `Class(i)`/`Component(i)`/`Import(i)`；只有局部类和 import 在 `PartialTree` 就登记，组件等到 `ClassTree.expand` 算完偏移才登记。

#### 7.5 `Component`（arena `comp_arena`，`ComponentId`）状态机

`Component` = tagged union，主 record `Comp{ class_inst:NodeId, ty:TypeId, binding, condition, attributes, comment, state: ComponentState, info }`，另有 `CompDef{def:SCodeId, modifier:ModId}`（初态）/`Iterator`/`EnumLiteral`/`TypeAttribute{ty,modifier}`/`Invalid`/`Wild`（照 §2.5）。

`state: ComponentState` 枚举（关键，任务书点名要）：`CompDef(初态，只有 def+累积 modifier) → PartiallyInstantiated（class_inst=EMPTY、ty=UNTYPED、binding=RAW） → FullyInstantiated（binding=UNTYPED_BINDING、dims=UNTYPED） → Typed（ty 已定） → TypeChecked（binding=TYPED）`。

**不变量**：
1. `instComponent` 开头判"已不是 `CompDef` 就跳过"（§3.3 `instComponent` 第 1 点）——保证**继承来的共享槽组件只被实例化一次**。ymc 必须以 state 做幂等门。
2. `PartiallyInstantiated` 时 `class_inst` 先写 `EMPTY`（§3.3 第 3 点"打破循环"）：因为组件要先就位供 instClass 内的查找用，而 `class_inst` 要 instClass 完才产出。**不能等 class_inst 齐了再写组件**。inner/outer 查找也依赖这个空态（§5.4：起点组件 `class_inst`=EMPTY 使查找自然落到父实例，不会找到自己）。
3. `TypeAttribute`（内建属性）的修饰值就存在自己的 `modifier.binding`，值在 instExpressions 才求（§4.2）；压平时属性顺序 = 内建 `FlatTree` 组件数组顺序，与书写顺序无关（§4.2 末、P11）。
4. "条件为 false 被删"不是独立状态：`is_deleted(comp)` = condition 已 typed 且求值为 false（§2.5 末）；纯 outer 不产生变量（§5.8）。

#### 7.6 克隆策略（何时深拷类树、何时共享）

对应 §3.3 `ClassTree.instantiate` 的克隆规则，落到 arena：

- **浅克隆 + 惰性**：`InstNode.clone` = 新分配 node 格、新 `ClassId`（内容浅拷）、清 caches；`ClassTree.clone` 只对 `ExpandedTree` 复制 `classes` 数组。嵌套类**用到时才在自己的 instantiate 里继续克隆**——ymc 不要一次性深拷整棵类树。
- **每个使用点一份私有克隆**：每个组件、每条 extends 使用点、根类，从 instantiate 起各有自己的 `ClassId`/`ComponentId` 链。
- **共享（不克隆）**：继承来的类/组件把基类 `InstantiatedTree` 的 **SlotId 原样拷进新数组尾部**（共享 slot）；`type`/`operator record`/`operator` 里的局部类不克隆只改 parent（`is_typish`）；package/function 按定义节点缓存一份实例（cache slot）。
- **内建**：`PartialBuiltin` 的属性组件逐个 `cloneComponent`（因为常量节点只读，必须先出可变副本，§7.1 不变量 4）。

**遍历顺序不变量（保证与 OMC 压平结果逐字一致）**：
1. instClass 内固定 11 步顺序（§3.3 表）：instantiate → updateComponentType → 组装修饰 → modifyExtends → applyExtendsVisibility → applyModifier → redeclare → **instExtends（先各基类的局部组件，按 exts 顺序、递归先基类的基类）** → **applyLocalComponents（本类局部，按 local_components 升序）** → replaceDuplicates/checkDuplicates → checkClass。
2. 压平遍历 = `for c in FlatTree.components: flattenComponent(c)`，复合组件深度优先当场展开（§3.4 第 1 点）。
3. **方程顺序**：子组件方程（按组件序，DFS）在前，然后本类：本类方程在前、各 extends 方程按 **extends 逆序**在后（因 instExpressions 用 `join(new, accumulated)` 头插，§3.4 第 6 点、P03）。ymc 收集方程时要复刻这个"头插 + extends 逆序"，否则方程顺序对不上。

## 探针

环境：`omc --version` → `OpenModelica v1.25.0 (64-bit)`（`/c/Program Files/OpenModelica1.25.0-64bit/bin/omc`）。目录 `docs/research/probe-r11/`，命令统一为 `timeout 120 omc <文件>`（在该目录下执行）。输出为原文照抄；报错路径前缀 `C:/dev/ymc/docs/research/probe-r11/` 保留。

### P01 `omc X.mo` 实例化最后一个顶层类

```modelica
model First
  Real a = 1;
end First;

model Second
  Real b = 2;
end Second;
```
```
class Second
  Real b = 2.0;
end Second;
```

### P02 多层 extends、extends 夹在组件中间、protected 段

```modelica
model A
  Real a1 = 1;
  Real a2 = 2;
end A;

model B
  Real b1 = 3;
  extends A;
  Real b2 = 4;
end B;

model C
  Real c1 = 5;
  extends B;
  Real c2 = 6;
protected
  Real c3 = 7;
  extends A2;
  Real c4 = 8;
end C;

model A2
  Real z1 = 9;
protected
  Real z2 = 10;
public
  Real z3 = 11;
end A2;

model P02
  extends C;
  Real top = 0;
end P02;
```
```
class P02
  Real c1 = 5.0;
  Real b1 = 3.0;
  Real a1 = 1.0;
  Real a2 = 2.0;
  Real b2 = 4.0;
  Real c2 = 6.0;
  protected Real c3 = 7.0;
  protected Real z1 = 9.0;
  protected Real z2 = 10.0;
  protected Real z3 = 11.0;
  protected Real c4 = 8.0;
  Real top = 0.0;
end P02;
```

### P03 方程顺序（子组件 / 本地 / 多条 extends）

```modelica
model Sub
  Real s;
equation
  s = 100;
end Sub;

model Base1
  Real x1;
equation
  x1 = 1;
end Base1;

model Base2
  Real x2;
equation
  x2 = 2;
end Base2;

model P03
  Real a;
  extends Base1;
  Sub sub;
  extends Base2;
  Real b;
equation
  a = 10;
  b = 20;
end P03;
```
```
class P03
  Real a;
  Real x1;
  Real sub.s;
  Real x2;
  Real b;
equation
  sub.s = 100.0;
  a = 10.0;
  b = 20.0;
  x2 = 2.0;
  x1 = 1.0;
end P03;
```

### P04 菱形继承 + 本地重复声明（相同）→ 保留最靠前的位置

```modelica
model Root
  Real r = 1;
  Real q = 2;
end Root;

model L
  Real l0 = 3;
  extends Root;
  Real l1 = 4;
end L;

model R
  extends Root;
  Real r1 = 5;
end R;

model P04
  Real first = 0;
  extends L;
  Real mid = 6;
  extends R;
  Real q = 2;
  Real last = 7;
end P04;
```
```
class P04
  Real first = 0.0;
  Real l0 = 3.0;
  Real r = 1.0;
  Real q = 2.0;
  Real l1 = 4.0;
  Real mid = 6.0;
  Real r1 = 5.0;
  Real last = 7.0;
end P04;
```

### P05 继承来的重复元素不相同

```modelica
model Root1
  Real r = 1;
end Root1;

model Root2
  Real r = 2;
end Root2;

model P05
  extends Root1;
  extends Root2;
end P05;
```
```
Error processing file: P05_dup_nonidentical.mo
[C:/dev/ymc/docs/research/probe-r11/P05_dup_nonidentical.mo:2:3-2:13:writable] Notification: From here:
[C:/dev/ymc/docs/research/probe-r11/P05_dup_nonidentical.mo:6:3-6:13:writable] Error: Duplicate elements (due to inherited elements) not identical:
  first element is:  Real r = 1
  second element is: Real r = 2

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```
（exit=1）

### P06 同一作用域内重复声明

```modelica
model P06
  Real x = 1;
  Integer x = 2;
end P06;
```
```
Error processing file: P06_dup_local.mo
[C:/dev/ymc/docs/research/probe-r11/P06_dup_local.mo:2:3-2:13:writable] Notification: From here:
[C:/dev/ymc/docs/research/probe-r11/P06_dup_local.mo:3:3-3:16:writable] Error: An element with name x is already declared in this scope.

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```
（exit=1）

### P07 inner/outer 基本

```modelica
model Leaf
  outer Real T0;
  Real t = T0 + 1;
end Leaf;

model Mid
  Leaf leaf;
  Real m = 2;
end Mid;

model P07
  Mid mid;
  inner Real T0 = 300;
  Leaf leaf2;
end P07;
```
```
class P07
  Real mid.leaf.t = T0 + 1.0;
  Real mid.m = 2.0;
  Real T0 = 300.0;
  Real leaf2.t = T0 + 1.0;
end P07;
```

### P08 缺 inner → 自动生成 + Warning

```modelica
model Leaf
  outer Real T0;
  Real t = T0 + 1;
end Leaf;

model P08
  Real before = 0;
  Leaf leaf;
  Real after = 1;
end P08;
```
```
class P08
  Real before = 0.0;
  Real leaf.t = T0 + 1.0;
  Real after = 1.0;
  Real T0;
end P08;
[C:/dev/ymc/docs/research/probe-r11/P08_missing_inner.mo:2:3-2:16:writable] Warning: An inner declaration for outer component T0 could not be found and was automatically generated.

```
（exit=0；注意 Warning 打在压平结果**之后**，位置信息指向 outer 声明）

### P09 `inner outer`

```modelica
model Leaf
  outer Real T0;
  Real t = T0 + 1;
end Leaf;

model Mid
  inner outer Real T0 = 2 * 1;
  Leaf leaf;
end Mid;

model P09
  inner Real T0 = 300;
  Mid mid;
  Leaf leaf;
end P09;
```
```
class P09
  Real T0 = 300.0;
  Real mid.T0 = 2.0;
  Real mid.leaf.t = mid.T0 + 1.0;
  Real leaf.t = T0 + 1.0;
end P09;
```

### P10 短类定义 / 类修饰 / 数组短类 / `time`

```modelica
type Length = Real(unit = "m", min = 0);
type Vec3 = Length[3](each start = 1);
type Color = enumeration(red, green, blue);

model N
  parameter Real p = 1;
  Real y = p * time;
end N;

model M = N(p = 2);

connector RealInput = input Real;

model P10
  Length len = 1.5;
  Vec3 v;
  Length[2] w(each max = 10);
  M m;
  M m3(p = 3);
  Color c = Color.green;
  RealInput u;
  Real t = time;
equation
  v = {1, 2, 3} * time;
  w = {4, 5};
  u = 1;
end P10;
```
```
class P10
  Real len(unit = "m", min = 0.0) = 1.5;
  Real v[1](unit = "m", min = 0.0, start = 1.0);
  Real v[2](unit = "m", min = 0.0, start = 1.0);
  Real v[3](unit = "m", min = 0.0, start = 1.0);
  Real w[1](unit = "m", min = 0.0, max = 10.0);
  Real w[2](unit = "m", min = 0.0, max = 10.0);
  parameter Real m.p = 2.0;
  Real m.y = m.p * time;
  parameter Real m3.p = 3.0;
  Real m3.y = m3.p * time;
  enumeration(red, green, blue) c = Color.green;
  input Real u;
  Real t = time;
equation
  v[1] = time;
  v[2] = 2.0 * time;
  v[3] = 3.0 * time;
  w[1] = 4.0;
  w[2] = 5.0;
  u = 1.0;
end P10;
```

### P11 内建属性输出顺序与书写顺序无关

```modelica
type T = Real(start = 1, max = 5);
model P11
  T x(unit = "m", nominal = 2, min = -1, quantity = "Q");
  Real y(fixed = true, start = 0, displayUnit = "mm", unit = "m");
equation
  der(x) = 1;
  y = x;
end P11;
```
```
class P11
  Real x(quantity = "Q", unit = "m", min = -1.0, max = 5.0, start = 1.0, nominal = 2.0);
  Real y(unit = "m", displayUnit = "mm", start = 0.0, fixed = true);
equation
  der(x) = 1.0;
  y = x;
end P11;
```

### P12 outer 带修饰

```modelica
model Leaf
  outer Real T0 = 5;
end Leaf;

model P12
  inner Real T0 = 300;
  Leaf leaf;
end P12;
```
```
Error processing file: P12_outer_mod.mo
[C:/dev/ymc/docs/research/probe-r11/P12_outer_mod.mo:2:3-2:20:writable] Error: Modifier ' = 5' found on outer element T0.

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```

### P13 inner 种类不符

```modelica
model Leaf
  outer Real X;
  Real t = X;
end Leaf;

model P13
  inner model X
    Real q = 1;
  end X;
  Leaf leaf;
end P13;
```
```
Error processing file: P13_inner_wrong_kind.mo
[C:/dev/ymc/docs/research/probe-r11/P13_inner_wrong_kind.mo:2:3-2:15:writable] Notification: From here:
[C:/dev/ymc/docs/research/probe-r11/P13_inner_wrong_kind.mo:7:9-9:8:writable] Error: Found inner class X instead of expected component.

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```

### P14 根类自带 outer

```modelica
model P14
  outer Real T0;
  Real t = T0 + 1;
end P14;
```
```
Error processing file: P14_top_level_outer.mo
[C:/dev/ymc/docs/research/probe-r11/P14_top_level_outer.mo:2:3-2:16:writable] Error: The model can't be instantiated due to top-level outer element 'T0', it may only be used as part of a simulation model.

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```

### P15 缺 inner 且根类有同名普通元素

```modelica
model Leaf
  outer Real T0;
  Real t = T0 + 1;
end Leaf;

model P15
  Real T0 = 1;
  Leaf leaf;
end P15;
```
```
Error processing file: P15_missing_inner_conflict.mo
[C:/dev/ymc/docs/research/probe-r11/P15_missing_inner_conflict.mo:7:3-7:14:writable] Notification: From here:
[C:/dev/ymc/docs/research/probe-r11/P15_missing_inner_conflict.mo:2:3-2:16:writable] Error: An inner declaration for outer element 'T0' could not be found, and could not be automatically generated due to an existing declaration of that name.

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```

### P16 inner/outer 类

```modelica
model Leaf
  outer model M = MBase;
  M m;
end Leaf;

model MBase
  Real q = 1;
end MBase;

model MImpl
  extends MBase;
  Real extra = 2;
end MImpl;

model P16
  inner model M = MImpl;
  Leaf leaf;
end P16;
```
```
class P16
  Real leaf.m.q = 1.0;
  Real leaf.m.extra = 2.0;
end P16;
```

### P17 `inner outer` 无下层使用者 → OMC 1.25.0 内部错误

```modelica
model Mid
  inner outer Real T0 = 2 * 1;
  Real m = 1;
end Mid;

model P17
  inner Real T0 = 300;
  Mid mid;
end P17;
```
```
Error processing file: P17_inner_outer_unused.mo
[C:/OM125/OM64bit/OMCompiler/Compiler/NFFrontEnd/NFTyping.mo:493:9-493:127:writable] Error: Internal error NFTyping.typeComponent got noninstantiated component T0

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```

### P18 实例化 partial 根类

```modelica
partial model P18
  Real x = 1;
end P18;
```
```
[C:/dev/ymc/docs/research/probe-r11/P18_partial_root.mo:1:1-3:8:writable] Error: Illegal to instantiate partial class P18.
```
（exit=1；`INST_PARTIAL_CLASS`，`checkPartialClass` NFInst.mo:4180）

### P19 组件类型是 partial 类

```modelica
partial model PBase
  Real x = 1;
end PBase;

model P19
  PBase b;
end P19;
```
```
[.../P19_partial_component.mo:1:1-3:10:writable] Notification: From here:
[.../P19_partial_component.mo:6:3-6:10:writable] Error: Component 'b' has partial type 'PBase'.
```
（exit=1；`PARTIAL_COMPONENT_TYPE`，`checkPartialComponent` NFInst.mo:2124。注：`outer PBase b;` 不报，outer 允许 partial 类型）

### P20 递归定义（组件类型是自身）

```modelica
model P20
  Real x = 1;
  P20 child;
end P20;
```
```
[.../P20_recursive_def.mo:3:3-3:12:writable] Error: Declaration of element child causes recursive definition of class P20.
```
（exit=1；`RECURSIVE_DEFINITION`，`checkRecursiveDefinition` NFInst.mo:2335）

### P21 extends 环

```modelica
model A
  extends B;
end A;

model B
  extends A;
end B;

model P21
  extends A;
end P21;
```
```
[.../P21_extends_loop.mo:6:3-6:12:writable] Error: extends A causes an instantiation loop.
```
（exit=1；`EXTENDS_LOOP`，`checkExtendsLoop` NFInst.mo:823/834。报的是 B 里的 `extends A`——环闭合处）

### P22 递归短类定义

```modelica
model P22
  model A = A;
  A a;
end P22;
```
```
[.../P22_recursive_short.mo:2:3-2:14:writable] Error: Recursive short class definition of A in terms of A.
```
（exit=1；`RECURSIVE_SHORT_CLASS_DEFINITION`，`expandClassDerived` NFInst.mo:1041）

### P23 条件组件（parameter 条件，正常删除）

```modelica
model Sub
  Real s = 1;
end Sub;

model P23
  parameter Boolean use = false;
  parameter Boolean use2 = true;
  Sub a if use;
  Real mid = 0;
  Sub b if use2;
  Real r if not use2;
end P23;
```
```
class P23
  final parameter Boolean use = false;
  final parameter Boolean use2 = true;
  Real mid = 0.0;
  Real b.s = 1.0;
end P23;
```
（exit=0；`use=false` → `a` 删；`use2=true` → `b` 保留；`not use2=false` → `r` 删。条件为 false 的组件不产生变量，§3.4 第 5 点）

### P24 条件组件（非 parameter 条件 → 报错）

```modelica
model P24
  Boolean flag = time > 1;
  Real r = 1 if flag;
end P24;
```
```
[.../P24_conditional_nonparam.mo:3:3-3:21:writable] Error: Component condition must be parameter or constant expression (in flag).
```
（exit=1；`COMPONENT_CONDITION_VARIABILITY`，**在 typing 趟报** NFTyping.mo:1232，不是实例化趟）

### P25 修饰名不存在

```modelica
model Sub
  Real s = 1;
end Sub;

model P25
  Sub a(nosuch = 2);
end P25;
```
```
[.../P25_mod_missing.mo:6:9-6:19:writable] Error: Modified element nosuch not found in class Sub.
```
（exit=1；`MISSING_MODIFIED_ELEMENT`，`applyModifier` NFInst.mo:1543）

### P26 修饰 protected 元素

```modelica
model Sub
protected
  Real s = 1;
end Sub;

model P26
  Sub a(s = 2);
end P26;
```
```
[.../P26_mod_protected.mo:7:9-7:14:writable] Notification: From here:
[.../P26_mod_protected.mo:3:3-3:13:writable] Error: Protected element 's' may not be modified, got 's = 2'.
```
（exit=1；`NF_MODIFY_PROTECTED`，`applyModifier` NFInst.mo:1579）

### P27 对类成员用绑定修饰而缺 redeclare

```modelica
model Sub
  replaceable model M = Base;
  M m;
end Sub;

model Base
  Real q = 1;
end Base;

model P27
  Sub a(M = Base);
end P27;
```
```
[.../P27_class_mod_no_redeclare.mo:11:9-11:17:writable] Error: Missing redeclare keyword on attempted redeclaration of class M.
```
（exit=1；`MISSING_REDECLARE_IN_CLASS_MOD`，NFInst.mo:1117）

### P28 基类可 replaceable

```modelica
model Base
  Real q = 1;
end Base;

model P28
  replaceable model M = Base;
  extends M;
end P28;
```
```
[.../P28_replaceable_base.mo:6:15-6:29:writable] Notification: From here:
[.../P28_replaceable_base.mo:7:3-7:12:writable] Error: Class 'M' in 'extends M' is replaceable, the base class name must be transitively non-replaceable.
```
（exit=1；`REPLACEABLE_BASE_CLASS`，`checkReplaceableBaseClass` NFInst.mo:883）

### P29 extends 内建类型后还有元素

```modelica
type BadReal
  extends Real;
  Real extra;
end BadReal;

model P29
  BadReal x = 1;
end P29;
```
```
[.../P29_builtin_extends.mo:1:1-4:12:writable] Error: A class extending from builtin type Real may not have other elements.
```
（exit=1；`BUILTIN_EXTENDS_INVALID_ELEMENTS`，`checkBuiltinTypeExtends` NFInst.mo:917）

### P30 实例化 package

```modelica
package P30
  constant Real c = 1;
end P30;
```
```
[.../P30_inst_package.mo:1:1-3:8:writable] Error: Cannot instantiate P30 due to class specialization package.
```
（exit=1；`INST_INVALID_RESTRICTION`，`checkInstanceRestriction` NFInst.mo:4200，由 lookupRootClass 调）

## 未确认 / 待验证

- **数组维度合法性错**（`Error.mo:326`/`:1091` "no more than one dimension may have size > 1" 等、`:472` 维度大小不符）在实例化趟**不触发**：inst 只把维度存成 `Dimension.RAW_DIM`→`UNTYPED`（§3.3/§4.1），合法性检查全部在 typing。未采探针实证具体消息与触发行（typing 不在本报告范围）。ymc 若把维度检查提前会与 OMC 行为不符（§6.5）。
- `LOOKUP_BASECLASS_ERROR`（基类名找不到）、`FOUND_OTHER_BASECLASS`（修饰下发时基类改指）、`CLASS_EXTENDS` 目标非继承类等：源码锚点确凿（§6.1/§6.2），未单独采探针。
- P17 的 `inner outer` 内部错误是 OMC 1.25.0 缺陷，已入分歧账本预留（§5.2 P17），非 ymc 应复刻的行为。

## 对 ymc 的建议

**S3.1 竖切片的"实例化最小子集"边界。** 先做能让 `omc X.mo` 与 `ymc X.mo` 对同一批标量模型输出逐字一致的最小闭环：(1) **标量组件**（`Real/Integer/Boolean/String` + 用户 model 的标量字段），(2) **单继承链**（`extends A;` 多层，含 extends 夹在组件中间、public/protected 分段），(3) **修饰合并**（组件修饰、extends 子句修饰、外层优先的 `merge(outer,inner)`），(4) **内建类型与属性**（`Real x(start=…,unit=…)`，属性按内建 `FlatTree` 顺序输出），(5) **inner/outer**（含自动生成 inner + Warning）。这五项正好覆盖 §3 三阶段（partialInst/expand/instClass）+ §4.1/4.2 修饰 + §5 inner/outer 的主干，且每项都有探针 oracle（P01–P17）可逐字对拍。后做、明确排除在最小子集外：**数组/维度**（要连带 typing 才有意义，§6.5）、**函数实例化**（func cache 路径，§3.3 末）、**redeclare**（元素式 + 修饰式两条链，§3.3 第 7 步）、**条件组件求值**（依赖 typing 求 variability，P23/P24）、**package 常量收集**（§1 步骤 12）、**短类里的数组维度**（`type Vec3 = Length[3]`，P10 的维度部分）。短类的**无维度**形式（`model M = N(p=2)`、`connector RealInput = input Real`）可纳入最小子集（纯修饰透传），带维度的推迟。

**落地顺序建议**：先把 arena 三件套（node/class/comp）+ SlotId 间接层立起来（§7.0），再实现 partialInstClass（建保序的 ClassTree、Ref 占位）→ expandClass（解析 extends、算最终下标、环检测占位态）→ instClass 的 11 步（§7.6 遍历不变量）→ Flatten 的 DFS + 头插方程序。每加一步就用对应探针对拍，不要攒到最后。

**最容易踩的 3–5 个坑（均有报告内证据）**：

1. **组件与类两套下标规则不同**（§3.2 末、§7.4 不变量 2）：组件保序、extends 就地展开（`Ref` 占位）；继承类统一排局部类之后、不保序。若用一套逻辑处理，压平变量顺序会错（对拍 P02/P04 立刻暴露）。
2. **克隆边界 = `ClassTree.instantiate`**（§7.3 不变量 1-3、§7.6）：partial/expand 在定义节点算一次全局共享，从 instantiate 起每个使用点私有克隆。若过早克隆（每次查名字就拷）会性能爆炸且破坏共享语义；若该克隆时不克隆（就地改共享定义的修饰）会污染所有使用点。`applyModifier` 到类目标**必然伴随 replaceClass**，这条最易漏。
3. **组件 `class_inst` 必须先写 EMPTY 占位**（§3.3 第 3 点、§7.5 不变量 2）：这是打破"组件要先就位 / class_inst 要 instClass 才出"循环的关键，也是 inner/outer 查找不会找到自己的原因（§5.4）。若等 class_inst 齐了再登记组件，instClass 内部查找和 inner 解析都会崩。
4. **方程顺序 = 头插 + extends 逆序**（§3.4 第 6 点、§7.6 不变量 3、P03）：直觉会写成顺序 append，结果 extends 方程顺序反了。收集时用 `join(new, accumulated)` 语义。
5. **维度/条件/绑定类型检查不能提前到实例化趟**（§6.5、P24、未确认节）：inst 只存 RAW 表达式，所有需要求值/定类型的合法性检查都在 typing。ymc 若在 inst 阶段就报维度非法/条件非 parameter，会与 OMC 的报错时机和消息不符——最小子集里干脆不碰这些检查，留给后续 typing 竖切片。
