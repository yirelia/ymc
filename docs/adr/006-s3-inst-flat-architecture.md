# ADR-006 S3 实例化/压平的 IR 分层与管线：inst 树、类型化 Expr、FlatModel、DAE-dump 版式

- 日期：2026-09-18
- 状态：已接受
- 依据：研究底稿 `docs/research/R11`（NF 实例化管线与数据结构）、`R12`（名字查找）、`R13`（压平输出格式）、
  `R14`（类型/求值/化简）、`R15`（修饰/extends/redeclare）；方案 §4.2 IR 家族表、§5 S3。全部锚点见各报告。

## 背景
S3 把 SCode（S2 产出）实例化成一棵可查找、可类型化、可压平的实例树，最终产出与 `omc X.mo` 逐字一致的压平文本。
研究表明 OMC 的实例化是"原地改一棵共享 InstNode 树"的三趟流程（instantiate → instExpressions → typeClass），再函数式地压成 FlatModel。
`omc X.mo` 打印的是 **DAE dump 版式**（`DAEDumpTpl.tpl` + `ExpressionDumpTpl.tpl`），不是 Flat/Base Modelica。

## 决定

### D1. IR 分四层，各自 arena + Id（方案 §3.4）
- **`inst`（可变实例树）**：`INode`（身份 + 双父链）/ `Class` / `ClassTree` / `Component`，各一个 `Arena`，对标 `NFInstNode/NFClass/NFClassTree/NFComponent`。可变性用 arena 格模拟 OMC 的 `Pointer<T>`：改一格 = `Pointer.update`（共享者同步）、新分配一格 = `Pointer.create`（克隆、断共享）；`Mutable<InstNode>` 再加一层 `SlotId`（R11 §7.0）。
- **类型化 `Expr`（inst/flat/后端共用，方案 §4.2 末行）**：对标 `NFExpression`，**不复用 S1 的 `Absyn::Exp`**——压平表达式是定型 + 化简后的树（带 `Type`、有 `CAST` 节点、cref 是解析到 INode 的点分路径、字面量已折叠、`end`→`size(a,1)`，R13 §4）。arena + `Id<Expr>`，胖 struct + `ExprKind` + switch。
- **`flat`（FlatModel）**：`FlatModel{name, variables, equations, initialEquations, algorithms, initialAlgorithms}`，`FlatVariable`/`FlatEquation`/`FlatStatement`；对标 `NFFlatModel`。S3 出口、S5 入口。
- **文本 dump**：三种 IR 各一个文本格式（方案 §2.1）；压平的默认 dump **对齐 DAE dump 版式**（见 D5）。

### D2. INode 存两条父链，不可合并（R11 §2.2、R12 §对 ymc 建议）
`parent_scope`（词法外层，**查名字向外走用它**；组件取其类型定义处的外层）与 `instance_parent`（实例树父亲，**resolveInner/inner-outer 用它**）分开存。合并会让继承元素在错误作用域查名（R12 探针 p26）。extends 拿来的基类节点保留原 `parent_scope`（只改 kind=BaseClass）。

### D3. Modifier 独立 arena、不可变、结构共享（R15 §对 ymc 建议）
`ModifierArena` + `ModifierId`；三态 `Nomod`（哨兵 Id 0）/`Mod`/`Redeclare`。**子修饰用按 name 排序的小 vector**（不用 map/AVL）——个位数规模，且要主动选定遍历序以复刻 OMC 字典序（L1 文本对拍）。`merge(outer,inner)` **append 新节点、复用未变子树的旧 Id，绝不原地改**（同一修饰会传播到数组每元素、下传多层类、被 InstNode 克隆共享）。作用域记在 **binding 上**（`RawBinding{absynExpr, scope, ...}`），不建全局修饰环境。数组修饰切分复刻 `SPLIT_PROXY`（惰性逐元素穿越，只碰子修饰，each 整棵跳过）——但**数组不在 v1 竖切片**（D6）。

### D4. 类型化管线，硬顺序（R14 §各趟先后依赖）
```
SCode → instantiate(建树+合并修饰+redeclare，不碰表达式)
      → instExpressions(cref 查找，Absyn→Expr；树压成 FLAT_TREE)
      → updateImplicitVariability(标 structural parameter + final)   ← 必须在定型前
      → typeClass(三趟：typeComponents→typeBindings→typeSections；维度/条件在此求值)
      → flatten(DFS 带前缀遍历；if 方程/条件组件裁剪；无绑定参数校验)
      → evalConstants(cref→值，只替换)                              ← 必须在 simplify 前
      → simplify(自底向上单趟折叠)         ← --noSimplify 可与上一趟分离对拍
      → scalarize(数组拆标量；v1 无数组时是 no-op)
      → dump
```
- **实例化三趟不碰表达式**是关键：instExpressions 才解析 cref，"done as a separate step after instantiation to make sure lookup can find the correct nodes"（R11 §1）。
- **方程顺序 = 头插 + extends 逆序**：子组件方程（组件序 DFS）在前，本类方程在前、各 extends 方程按 exts 逆序在后（R11 §3.4 第 6 点、探针 P03）。
- **类型/维度/条件/绑定的合法性检查全在 typing 趟，不在实例化趟**（R11 §6.5、R14 §6）：inst 只存 RAW 表达式。ymc 提前报维度非法/条件非 parameter 会与 OMC 时机不符。

### D5. 压平默认 dump 对齐 DAE dump 版式，表达式打印器是新的一套（R13）
- 默认 `ymc X.mo` = 压平文件最后一个顶层类，输出对齐 `DAEDumpTpl.tpl`：头恒 `class <全名>`、段序 变量→initial equation→initial algorithm→equation→algorithm、变量行字段顺序固定（`protected final par* const|param|discrete in|out 类型 名字(属性) = 绑定 "注释"`）、属性按类型固定顺序、不打 flow/stream/inner/outer/each、结构参数强制 `final parameter`、用户 type/record 塌陷成内建标量。
- **表达式打印器用优先级表加括号**（R13 §4.2），**与 S1/S2 的 unparse 不同**（后者保留源码括号当单元素 Tuple）。压平表达式无源码括号，靠 `Expression.shouldParenthesize` 的优先级表；`CAST` 打 `/*Real*/(exp)`。**Real 字面量复用已有的 `real_literal_text`**（S2 已按 `ryu_to_hr` 实现）。
- ymc **不复刻**的 OMC 行为（各记归一化 + 分歧账本）：自动装库触发的额外错误行（R12）、`import` 到组件的悬空引用（R12 未确认）、多行字符串续行缩进（R13 §8 已在 dump-compare 归一化）。

### D6. v1 竖切片边界：标量最小闭环（R11 文末建议）
**先做**（覆盖 R11 §3 三阶段 + §4 修饰 + §5 inner/outer，每项有 P01–P17 探针 oracle）：
标量组件 + 单继承链（多层 extends、夹在组件中间、public/protected 分段）+ 修饰合并（外层优先 merge）+ 内建类型与属性 + inner/outer（含自动生成 inner + Warning）+ 无维度短类（`model M = N(p=2)`）。
**后做、排除在 v1 外**：数组/维度、函数实例化、redeclare（元素式 + 修饰式）、条件组件求值、package 常量收集、带维度短类。
验收集 = `flattening/modelica/scodeinst/` 里 `docs/research/s3-easy-slice.json` 那 220 个无硬特征用例，逐行对拍 `omc X.mo`。

## 依据（关键锚点）
- IR 分层与可变性模型：R11 §2、§7.0-7.6（`NFInstNode.mo:223-274`、`NFClass.mo:101-158`、`NFClassTree.mo:72-116`、`NFComponent.mo:61-105`）。
- 管线顺序：R11 §1.1（`NFInst.mo:148-283`）；硬依赖 R14 §各趟。
- 输出版式：R13 §1-§6（`DAEDumpTpl.tpl`、`ExpressionDumpTpl.tpl`、`Expression.mo:10757-10946` 括号表）。
- 查找：R12（`NFLookup.mo`、`NFLookupState.mo`、`NFImport.mo`）。
- 修饰：R15（`NFModifier.mo`、`NFInst.mo` 的 apply/merge/propagate/redeclare）。

## 后果
- 得到：一套 OMC-faithful 的实例化/压平架构，每个决策有源码锚点 + 探针 oracle；v1 竖切片可逐字对拍 220 个真用例。
- 放弃：不追求一次做全 S3（方案估 4-6 个月）；数组/函数/redeclare/条件组件/package 常量推迟到 v1 之后按失败桶加宽。
- 引入新 IR `Expr`（类型化，inst/flat/后端共用），与 S1 的 `Absyn::Exp` 并存——两者不互转，各服务各自阶段。
- 何时重看：接后端（S5）时 `Expr` 要加后端需要的节点；数组竖切片时 Type/Dimension/scalarize 会大幅长肉；接 Dymola 结构比对（spec FR-052）时压平 dump 之外要加结构化导出。

## 落地顺序（先立地基，每步配探针对拍，不攒到最后）
1. `src/inst/type.h`（Type：enum kind + 载荷，扁平 ARRAY；Dimension）、`src/inst/expr.h`（类型化 Expr：arena + Id + kind）。
2. `src/inst/inode.h` 等（INode/Class/ClassTree/Component + SlotId 间接层；双父链）、`src/inst/modifier.h`（ModifierArena）。
3. partialInst → expand（extends、最终下标、环检测占位态）→ instClass 11 步 → lookup（LookupTree + LookupState）。
4. instExpressions（Absyn→Expr + cref 查找）→ updateImplicitVariability → typeClass（标量）→ evalConstants → simplify。
5. flatten（DFS + 头插方程序）→ `src/flat/`（FlatModel）→ 压平 dump（DAE 版式，Codex 任务卡，R13 为 oracle）。
6. driver：`ymc X.mo` 默认压平；`--dump-after=inst|flat`。`tools/flat_sweep.py` 驱动到 easy slice FAIL=0。
