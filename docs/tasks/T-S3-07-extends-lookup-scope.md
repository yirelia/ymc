# 任务卡 T-S3-07：extends 的基类名在**扩展类自身**里查（+ 类查找的后续缺口清单）

**执行者**：用户（手写核心：`src/inst/inst.cpp`）
**验收者**：Claude
**归属阶段**：S3

---

## 0. 一句话目标
`unresolved base class` / `unresolved component type` 两个 SKIP 原因挡着 147 例（nf-live 75+29、scodeinst correct 16+27，
2026-10-02 逐例归因）。其中 **72 例的首个失败**是同一个 bug：extends 的基类名从**扩展类的外层**开始查，漏掉了扩展类**自己的
嵌套类**——`package Modelica … extends Icons.Library;`（Icons 嵌在 Modelica 里，内嵌 MSL 的 58 个 msl\B*.mo 全是这个形态）、
`model C model D … end D; extends D; end C;`（scodeinst DuplicateElementsValid2）。改两行。

## 1. 依据（引用前重新 grep 行号）
- MLS 3.6 §7.1：extends 子句的基类名在"扩展类（extends 子句所在的类）的部分展平形态"里查（§5.6.1.4），即从扩展类自身开始，
  看得见它的**本地**嵌套类，看不见继承来的元素。
- OMC 链（NFFrontEnd，基线 3ceb0c9d06）：
  - `NFInst.mo:547` `partialInstClass2(InstNode.definition(node), node)`——类节点自己作 scope；
  - `NFClass.mo:169` `ClassTree.fromSCode(elements, isClassExtends, scope)`；
  - `NFClassTree.mo:202` `InstNode.newExtends(e, parent)`——extends 节点的 parent = **扩展类节点本身**；
  - `NFInst.mo:780-781` `scope := InstNode.parent(ext); Lookup.lookupBaseClassName(base_path, scope, …)`——从扩展类自身查。
- 短类定义 `class A = B` 的被派生名则从**外层**查：`NFInst.mo:1037` `lookupBaseClassName(…, InstNode.parent(node), …)`
  （node 是短类自己，parent = 外层）——**这一处不要改**（ymc 对应的派生链解析保持原样）。

## 2. 现状（改动锚点）
- `src/inst/inst.cpp:215`（`expand`，校验并展开各基类）：
  `NodeId base = lookup_class_name(ee.base_path, I_.nodes[cls].parent_scope);`
- `src/inst/inst.cpp:286`（`inst_class`，extends 就地展开）：
  `NodeId base_def = lookup_class_name(ee.base_path, I_.nodes[def_node].parent_scope);`
- 两处都从 `parent_scope`（扩展类的**外层**）开始，`lookup_simple_class`（:84）沿外层链只查各层的本地类 → 扩展类自己的嵌套类
  永远查不到。

## 3. 步骤
1. :215 改成从 `cls` 开始查：`lookup_class_name(ee.base_path, cls)`。
2. :286 改成从 `def_node` 开始查：`lookup_class_name(ee.base_path, def_node)`。
3. 两处保持一致（:286 的注释说"expand 已报错"——两处查到的必须是同一个类）。
4. **同时补"基类名须传递地非 replaceable"检查**（MLS §7.1.4；`NFInst.checkReplaceableBaseClass`，NFInst.mo:847-880，
   :783 调用）：查到的基类（及路径上的各段类）的 SCode 元素带 `replaceable` 前缀 → omc 报错
   `Class 'M' in 'extends M' is replaceable, the base class name must be transitively non-replaceable.` → ymc 先
   `mark_unsupported("replaceable base class")`。不加这一步，ExtendReplaceable1/2/3（`replaceable model M … extends M;`）会从
   SKIP 变成**错出结果**（见 §5 实测）。

## 4. 坑位
- "部分展平"：基类名查找**不能**看到继承来的类。ymc 的类查找表本来就只登记本地类（继承类未登记，见 :224 的 TODO），从 `cls`
  开始查不会误命中继承类——将来做"继承类进查找表"（下面 §6 D）时要给基类名查找留一个"只看本地"的开关。
- 自引用 `model A extends A; end A;`：从 A 自身查，A 不是 A 的本地类 → 仍沿外层找到 A 自己 → 交给既有的 extends 环检测
  （`ClassState::ExpandedClass` 占位）。改动前后行为一致，别删环检测。
- 改完后不少用例会**换一个**首个失败原因（不是 PASS）：内嵌 MSL 的 58 例全带 `import SI = Modelica.SIunits;`，其中 25 例用
  `SI.*` 声明组件 → 接着卡 import（T-S3-06）；ExtendReplaceable4 / ExtendsVisibility3 → `extends short/derived class`；
  Extends12 / ShortClassdefExtends1 → 短类别名前缀（§6 C）；ReplaceableBaseClass 期望报错 "Base class T is replaceable"。

## 5. 验收
- 实测（2026-10-02，Claude 本地临时打上 §3 第 1、2 步、未提交 inst.cpp，已还原）：全 scodeinst PASS 441 不变、FAIL 17→20
  ——新增 ExtendReplaceable1/2/3（期望 omc 报错，ymc 错出 `Real x;`）→ 第 4 步必须一起做；nf-live PASS 442→445、FAIL 0。
  直接 PASS 不多：多数用例换成下一个首个失败原因（主要是 import → T-S3-06），本卡是那批用例的前置。
- 门：`python tools/flat_sweep.py --cases-from docs/research/s3-easy-slice.json --oracle-live` FAIL 0 / 不一致 0；
  `python tools/flat_sweep.py --oracle-live` 无新增真 FAIL；`python tools/nf_live_sweep.py` 的 FAIL / ACCEPTS-INVALID / CRASH 不增；
  三套编译器 + ASan 全绿、零警告。

## 6. 类查找的后续缺口（同一次归因，按收益排序；每项单独开卡）
| 类 | 原因 | 例数 | 例子 |
|---|---|---|---|
| B | import（限定 9 / 别名 6 / `.*` 5 / 外层包的 import 1 / 经 import 的点分名 1） | 22 | packages\Import1、arrays\ArrayDim1、scodeinst ImportUnqualified4 → **T-S3-06** |
| E | 预定义类：ExternalObject 13 / Clock 8 / StateSelect 3 / AssertionLevel 1 | 25 | scodeinst StateSelect2 `parameter StateSelect s = StateSelect.default;`、Assert4 `AssertionLevel.warning`（这两个是内建枚举，最便宜）|
| C | 短类别名当路径前缀（纯别名 4 / 带修饰 `P(n = 1)` 6 / replaceable + redeclare 4） | 14 | scodeinst inst8 `package Medium = …PartialMedium; Medium.BaseProperties medium;`、TypeDim4 `package P2 = P(n = 1); P2.Reals x;` |
| D | 继承来的类不在查找表（:224 TODO） | 7 | redeclare\RedeclareClass2 `P2.M1 m1;`（M1 来自 P1）、extends\Extends6 |
| F | MSL 未加载（自动加载 / `loadModel`） | 3 | scodeinst LookupLibrary1 `Modelica.Units.SI.Angle` |
| G | 用例本就是错误的（期望报错文案） | 4 | extends\InvalidComponentExtends1 `extends m.A;` |
另：`lookup_class_name` 忽略前导点（`replaceable model M = .M;` 会误解析到自身）；`encapsulated` 作用域边界未执行
（packages\Encapsulated3 期望 "Base class B.C not found"）。
