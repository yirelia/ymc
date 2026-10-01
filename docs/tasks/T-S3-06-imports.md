# 任务卡 T-S3-06：import——类型名 / 基类名经 import 查找（inst 侧）

**执行者**：用户（手写核心：`src/inst/inst.cpp`、`src/inst/inode.h`）
**验收者**：Claude
**归属阶段**：S3（flatten 侧已就绪，见 §2）

---

## 0. 一句话目标
`import P.M; M m;`、`import A.*; extends B;` 这类**用 import 进来的名字作类型 / 基类**，现在 inst 的最小 lookup 不认 import
（`partial_inst` 遇 import 直接跳过）→ `unresolved component type` / `unresolved base class` → 整个模型 SKIP。本卡让
`lookup_simple_class` 认 import。表达式里经 import 的名字（常量、函数）flatten 侧已经能解析（2026-10-02，加宽(58k)），
所以 inst 一接通，这批用例就能整条跑通。

## 1. OMC 依据（引用前重新 grep 行号；研究底稿 `docs/research/R12-nf-lookup.md` §5 已全部核对过）
- **三种 import**（SCode 层已把组导入 `import P.{a, b = c}` 拆成单条，`AbsynToSCode.translateImports`）：
  - 限定 `import A.B.C;` → 名字 `C` 指向路径 `A.B.C`；
  - 重命名 `import X = A.B;` → 名字 `X` 指向 `A.B`；
  - 非限定 `import A.B.*;` → `A.B` 里的**每个类和组件**都以自己的名字可见（不滤 protected）。
- **import 目标永远从顶层查**（`NFLookup.lookupImport`，`NFLookup.mo:276`；起点 `InstNode.topScope`）——`import P.M` 里的 `P`
  即使本类有同名局部包也不算。
- **建表期定优先级**（`NFClassTree.mo`）：本地元素 > import（`addLocalElementConflict` :1654，"Local elements overwrite
  imported elements"）；继承元素 > import；限定 / 重命名 > 非限定（`addImportConflict` :1757-1800）。
- **冲突用到才报错**：同一类里两条同名的限定 / 重命名 import（**即使指向同一个目标**），或两个 `.*` 都提供同名元素 →
  表里记成 CONFLICTING，查到这个名字时报错（`addImportConflict` :1757-1800；探针：`import P.x; import P.x; Real y = x;` →
  "Qualified import name x already exists in this scope."）。没被用到 → 不报错（ImportConflict2 / 4）。
- **`.*` 在建表时立即解析**（`initImports` :227）：包不存在即报错（即使没人用）；**限定 / 重命名延迟到第一次被查到**才解析
  （`resolveImport` :1902），没被用到的坏 import 不报错。
- **import 不被继承**：`addInheritedElement`（:2040-2065）对 IMPORT 项直接跳过（注释 "imports aren't inherited"）。
- **import 名对本类里嵌套的类可见**：它就是本类表里的一项，向外层走自然会碰到。

## 2. 现状（改动锚点）
- `src/inst/inst.cpp` `partial_inst`（约 191 行）：`case SElementKind::Import: break;  // v1 跳过`。
- `src/inst/inode.h` `ClassTree`：`LEntry::Kind::Import` 已声明，但**没有 imports 数组**（注释里提到的 "imports 数组" 未建）。
- `src/inst/inst.cpp` `lookup_simple_class`（约 84 行）：沿 `parent_scope` 逐层查 `tree.lookup`，只接 `LEntry::Kind::Class`。
- flatten 侧（已就绪，可当参考实现读）：`src/flat/flatten.cpp` 的 `resolve_top_path`（顶层路径解析）与 `import_lookup`
  （按名解析一类的 import：先限定 / 重命名 / 组导入，再 `.*`；同档两项 = 冲突；解析不到 = 未解析）。它在 SCode 上做，
  inst 侧在实例类树上做，规则一致。

## 3. 步骤
1. `inode.h`：给 `ClassTree` 加 import 表，例如
   ```cpp
   struct ImportEntry {
     std::vector<Symbol> path;   // 目标全路径（组导入已展开成 path + 元素名；前导点去掉）
     NodeId resolved;            // 延迟解析的缓存；invalid = 未解析
     bool conflict = false;      // 同名第二条 → 用到即报错
   };
   std::vector<ImportEntry> imports;
   ```
   `LEntry{Kind::Import, index}` 的 index 指向 `imports`。
2. `partial_inst`：遍历 `def.elements` 里的 `SElementKind::Import`（`e.imp`：`path / wildcard / alias / multi / multi_alias`）：
   - 限定 / 重命名 / 组导入的每一项：名字 = 别名或末段；若 `tree.lookup` 里**已有本地类**同名 → 跳过（本地优先，静默）；
     已有 Import 项 → 把那项标 `conflict = true`；否则 push 一条 `ImportEntry` 并登记 `lookup[name] = {Import, idx}`。
   - `.*`：**先只记下包路径**（第 4 步解析）。注意本地组件此时还没进表（expand 才登记），所以"本地优先"要在第 5 步再兜一次。
3. 新函数 `NodeId resolve_import(ClassTree&, std::uint32_t idx)`：`lookup_class_name(path, I_.top)`（从顶层！）；结果缓存进
   `resolved`。conflict 或解析不到 → `mark_unsupported("import")` 返回 empty（omc 报错的情形，ymc 不产错误文案）。
4. `.*` 的解析：partial_inst 末尾（树挂上节点之后，同 omc `initImports` 的时机，允许包导入自己的子包，ImportUnqualified4）：
   `lookup_class_name(path, I_.top)` 得包节点 → `partial_inst` / `expand` 它 → 把它树里的每个类（和组件）以原名登记成 Import 项
   （已有本地类 → 跳过；已有限定 Import 项 → 保留限定的；已有来自别的 `.*` 的同名项 → 标 conflict）。包找不到 →
   `mark_unsupported("import")`（omc 即刻报错）。
5. `lookup_simple_class`：在某层 `tree.lookup` 命中 `Kind::Import` → `resolve_import`，若是类节点就返回它；命中组件（常量）→
   这条 lookup 是找类型的，返回 empty（类型名命中组件在 omc 里报 "found component instead of class"，SKIP 即可）。
6. `expand`：登记本地组件 / 继承元素时，若目标名已是 Import 项 → **覆盖**（本地、继承都 > import）；继承元素合并时**不要**把基类
   的 Import 项拷进来（import 不被继承）。
7. 基类名的查找起点：`expand` 里 `lookup_class_name(ee.base_path, I_.nodes[cls].parent_scope)` 从**外层**开始查，所以本类自己的
   import（和本地类）对 extends 不可见。omc 的起点是**含 extends 的类本身**（只有本地类和 import 在表里，继承来的还没合并，
   R12 §3 "extends 首段不见继承类"）。把起点改成 `cls`，但这一步只能看到本类的**本地类 + import**（此时组件 / 继承元素还没
   进表，正好符合 omc 的时机）。

## 4. 坑位
- **arena 纪律**：`resolve_import` / `.*` 解析会 `partial_inst` / `expand` 别的类 → `I_.trees` / `I_.nodes` 可能扩容；
  不要跨这些调用持 `ClassTree&` / `INode&` 引用（先拷下标，用完重新取）。
- **环**：`package P import P.Sub.*; package Sub … end Sub; end P;`（包导入自己的子包，ImportUnqualified4）——第 4 步必须在
  `P` 的树挂上之后做，且 `lookup_class_name` 进入 `P` 时不要再触发 `P` 的 partial_inst（state 已不是 NotInstantiated 即可）。
- 冲突 / 解析失败一律 `mark_unsupported`，**不要** `diags_.error`（ymc 还没对齐 omc 的错误文案，见 (57)）。
- 名字比较用 `Symbol`（intern 过的），路径段同理。

## 5. 验收
- 预计由 SKIP 转 PASS（它们现在都停在 inst 的 `unresolved component type` / `unresolved base class`；表达式侧 flatten 已就绪）：
  scodeinst 的 `ImportNamed1/2`、
  `ImportQualified1`、`ImportUnqualified1/4`、`ExtendImport1/2`、`ImportSubPackage2`；nf-live 的 `enums/Enum3/6/7`、
  `extends/Extends2`、`packages/Import1/3`、`others/DuplicateElementsExtendsEquivalent` 等（每例以实跑 1.25.0 为准，个别还会
  卡在别的未做特性上，属正常）。
- 门：`python tools/flat_sweep.py --cases-from docs/research/s3-easy-slice.json --oracle-live` FAIL 0 / 不一致 0；
  `python tools/flat_sweep.py --oracle-live` 无新增真 FAIL；`python tools/nf_live_sweep.py` 的 FAIL / ACCEPTS-INVALID / CRASH
  不增；三套编译器 + ASan 全绿、零警告。新 PASS 的 scodeinst 用例加进 easy-slice。
