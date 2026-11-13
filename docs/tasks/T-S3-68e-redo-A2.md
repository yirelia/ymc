# T-S3-68e-redo-A2 补充卡：组件路线调继承来的函数，体里兄弟调用的压平名（A68eIN58 / R68eV01）

> **2026-11-13：已由补充卡 `T-S3-68e-redo-A3.md` 取代。A2 作为共用路径改动整项撤掉（用户 11-13 决定 1A 2B）**；本卡 §1 OMC 逻辑、§2 探针表、§3 现状、§4 C0–C8
> 仍作 A3 卡 Y 支的依据（只在本次压平已放开时生效），§0、§5–§10 作废。A2 顺带修好的底线 FAIL 回到底线输出（老错，见 open-questions TBD-19 ④）。

> 原卡 `T-S3-68e-redo.md`（v6）不改。本卡只补一条老路线：fn_flat_name 类路径支（规则 (c) 第二条"之前漏掉的照 OMC 补齐"）。
> 依据只认 omc 1.25.0 实跑 + OMC v1.25.0 源码（本地拷贝 `src_NF*.mo` 已与 `git show v1.25.0:…` 逐字节核过）。
> 对比基准：底线 `redo/ymc_base_5c7.exe`，实现基准 `redo/68e/ymc_final.exe`（68e 终版）。
> v2（2026-11-12）：用户已批准 A2（认可"同一机制"，照 OMC 补，改共用路径 fn_flat_name）；评审有条件通过，按评审改：扫描改成整词
> 匹配（函数值引用）并重扫、C4 写明 partial 函数类型、C6 按不同 cls 值判唯一、A2E09 不自动接受、replaceable 定为不放、加 r13 探针。
> v1 备份 `T-S3-68e-redo-A2.v1.md`。

## 0. 结论

- 症状：`C c;`（`class C extends B`，f、g 都声明在 B 里）的 `c.f(time)` 走组件路线，压平名 `M.c.f` 是对的；f 体里的 `g(x)` 经
  fn_flat_name 走到**类路径支**（flatten.cpp :11496-11498，final 行号），打成 `B.g`。omc 1.25.0 打 `M.c.g`。底线就错（R68eV01 /
  A68eIN51 等底线 FAIL）；IN58 是放开点 A 打开 `b.f` 之后整模型才露出来。
- 修法（放开点 **A2**）：只在类路径支的最后一步（底线本来就要返回类路径名的地方）加一个只读判定 `inherited_sibling_name`。
  满足 C0–C9 全部条件 → 名字改成"正在发射的组件路线函数的压平名去掉末段 + 被调名"，home 置为 fn_home_；任一条不满足 →
  与底线逐字节相同（照旧返回类路径名）。底线返回 nullopt 的情形（B 在根类里、inh_tainted、包实例）一律不碰。
- 预测：探针快照里底线退出 0 且会变的只有 4 个，全是 FAIL→PASS 且与 omc 逐字节一致（A68eIN51 / IN56 / IN59、A68eZ54）；
  外加目标 A68eIN58（final FAIL→PASS）、R68eV01（FAIL→PASS），评审探针 RVA2F1（函数值）/ RVA2W1（两个子模型）FAIL→PASS。
  scodeinst / testsuite / MSL 三个语料 0 变化。按整词口径重扫后，快照命中从 53 个模型增到 123 个，新增的 70 个都是记录类型名
  引用，A2 无名可改，预测清单不变（§6）。

## 1. OMC 逻辑（v1.25.0，文件:行）

1. 组件的类在组件下实例化：`NFInst.instTypeSpec` → `instClass(node, …, parent)`（NFInst.mo:2285，parent = 组件节点）→
   `instClassDef` → `ClassTree.instantiate(node, parent)`（NFInst.mo:1146）。
2. `NFClassTree.instantiate`（NFClassTree.mo:428 起）：
   - 传进来的 instance 非空（= 组件）时不改；extends 节点改成 `BASE_CLASS(instance, ext_def, inst_ty)`（:498），即**基类节点
     的 ty.parent = 组件**；再递归 `instantiate(node, EMPTY_NODE(), inst_scope)`（:500）。
   - 递归里 instance 为空 → `instance := clsNode`（:474，克隆出的基类节点）；基类的本地类克隆后 `setParent(instance, c)`（:523）
     → **f、g 的父节点 = 基类节点**。
   - 继承来的类整份拷进派生类的类数组（`Array.copyRange(ext_clss, …)` :548）：组件类实例的类树里能查到 f。多级 extends 同理：
     H 的基类节点 ty.parent = B 的基类节点，B 的 ty.parent = 组件。
3. 调用 `c.f`：`NFFunction.lookupFunction`（NFFunction.mo:318-345）→ `Lookup.lookupFunctionName`（NFLookup.mo:193-215）→
   prefix = `scopeList(found_scope, includeRoot = true)`（NFFunction.mo:343）+ 调用名 → `M.c.f`。
4. f 体里的 `g`：从 f 节点查，本地没有 → `parentScope`（NFLookup.mo:898）到基类节点 → `Class.lookupElement`（:924，CLASS_NODE 支）找到 g，
   found_scope = 基类节点。`scopeListClass` 的 `BASE_CLASS` 支换成 `ty.parent`（NFInstNode.mo:1148-1149）→ 组件 →
   `scopeList(COMPONENT_NODE)`（:1132）→ 根 → **`M.c.g`**（多级经各层基类节点同样落到组件，A2M01 `A2M01.c.g`）。
5. 函数缓存按节点（`instFunctionRef` NFFunction.mo:362-389）：两个组件各一份（A2L02 `c1.g` / `c2.g`）；组件路径不带下标
   （NFComponentRef.fromNodeList :1534-1541）。
6. 注解引用同一套：derivative 在 `InstNode.parent(fnNode)` 里实例化（NFFunctionDerivative.mo:77）→ 基类节点 → `M.c.df`（A2G01）；
   inverse 在函数节点里 `Inst.instExp(call_aexp, fnNode, …)`（NFFunctionInverse.mo:182，instInverseSubMod :152 起）→ 同上
   `M.c.finv`（A2B01）。
7. 例外（都要排除）：
   - import：`lookupLocalSimpleCref` 命中 import 时 `foundScope := InstNode.parent(node)`（NFLookup.mo:932），import 目标是定义层的
     类 → `B.g`（A2B06，底线 PASS，A2 不得改动）。
   - 记录类型名走 `scopePath`（NFInstNode.mo:1217-1240，BASE_CLASS 换 ty.parent :1230 / :1266）→ `M.c.R`（A2T02）。ymc 的
     rec_flat_name 仍打 `B.R`，A2 只管函数名 → 基类里有本地记录 / 类型 / 枚举的一律不放（C4）。
   - 重名（派生类 / 兄弟基类 / 中间层也声明 g）：omc 取实例树里另一份（A2D01 用 C 的 g、A68eV13 用 B2 的 "b2"、A68eW03 用
     "first"、A2B08 用 B 的 u+7），属 65b 的 DUP / 实例树取舍 → 一律不放（C7）。
   - replaceable g 被组件修饰重声明：用 gg 的体（A68eIN62、A2R02 `c2.g` = u+10）→ replaceable 一律不放（C2）。

## 2. 探针与 omc 实跑（PATH omc 1.25.0，240 s，串行）

文件：`redo/68e/a2.probes`（42）、`a2b.probes`（19）、`a2ra.probes`（53，探针快照命中抽出来的）；结果 `pr/<集>/<模型>/{omc,base,old}.out`，
汇总 `a2.out` / `a2b.out` / `a2ra.out`（base = 底线，old = ymc_final）。另 `pr/a2t/`：两个目标用 ymc_final 重跑。
评审探针 `redo/68e-review/r13.probes`（5 个，三方结果在 `68e-review/pr/r13/`）：

| 探针 | 形态 | omc | 底线 / final | A2 后 |
|---|---|---|---|---|
| RVA2F1 | f 体里 `app(x, g)`：g 只当函数值出现，B 里另有 partial 函数类型 F | `M.c.app(x, M.c.g)`，app 形参打 `h<function>(#Real u) => #Real h`（不带类型路径） | FAIL（`B.app(x, B.g)`） | PASS（a2pred3 SAME） |
| RVA2W1 | 两个子模型 s1 / s2 各有 `C c` 并调 `c.f` | `M.s1.c.g` / `M.s2.c.g` | FAIL | PASS（a2pred3 SAME） |
| RVA2H1 | g 在 B 的基类 H（f 在 B） | `M.c.g` | SKIP（inh_tainted） | SKIP 不变 |
| RVA2K1 | `C c(k = 3)` 改继承来的常量 | — | SKIP（check_comp_fn_consts） | SKIP 不变 |
| RVA2E1 | 根类 `extends C` 又有 `C c`，`c.f` 与 `f(time)` 并存 | — | SKIP（根类链路发射的 `M.f` 体里 g 无名） | SKIP 不变 |

| 形态 | 探针 | omc | 底线 / final |
|---|---|---|---|
| 基本（V01 同形） | A2V01、impl/v1 R68eV01 | `M.c.f` 体里 `M.c.g` | FAIL（`B.g`） |
| IN58（再加 `B b` 的 b.f） | A68eIN58、A2E10 | `M.c.g` + `M.b.g` | 底线 SKIP / final FAIL |
| 多级 extends（H 是 C 的祖父） | A2M01、A68eIN56 | `M.c.g` | FAIL（`H.g` / `A.g`） |
| f 在 B、g 在 H（不同层） | A2M02 | — | SKIP（inh_tainted，不碰） |
| C 是短类 `class C = B` | A2S01、A2S02（带修饰） | `M.c.g` | 底线 SKIP / final PASS（走 A，不碰） |
| `class C = D; D extends B` | A2S03 | `M.c.g` | FAIL |
| extends 经短类 `class D = B; C extends D` | A2B10 | `M.c.g` | FAIL |
| extends 带修饰（改 parameter） | A2X01 | `M.c.g` | FAIL |
| extends 改常量 `extends B(k = 3)` | A2X02 | — | SKIP（component_fn via_mod，不碰） |
| B 里有常量被 g 读 | A2X03 | `M.c.g`，常量折成 1.0 | FAIL |
| 嵌套组件 `c.d.f` | A2N01 | `M.c.d.g` | FAIL |
| 子模型里调 `m.c.f` 的路径 | A2N02 | `M.m.c.g` | FAIL |
| 组件数组 `c[2].f` | A2A01 | 报错 "contains subscripts" | 两拒 |
| 数组实例里 `m[2]` 内部调 | A2A02 | — | SKIP |
| C 也声明同名 g（体不同 / 相同） | A2D01、A68eX17 / A2D02 | 用 C 的 g（u+2 / u+5）/ `M.c.g` | FAIL |
| C 也声明同名 g、签名不同 | A68eX23 | 报错 | ACCEPTS-INVALID |
| 兄弟基类都有 g | A68eW03、A68eV13、A2E01 | 取另一份 | FAIL |
| 中间层 B 也声明 g | A2B08 | 用 B 的 u+7 | FAIL |
| 菱形（C、D 都 extends B） | A2B22 | `M.e.g` | FAIL |
| C 里有同名组件 `Real g` | A2B17 | 报错（DUP 不同） | 两拒 |
| g replaceable、未重声明 | A68eIN70、A2R04 | `M.c.g` | FAIL |
| g replaceable、组件修饰重声明 | A68eIN62、A68eIN73（调 c1）、A2R02 | `c2.g` 用 gg 体 / `c1.g` 原体 | FAIL |
| extends 修饰里重声明 / C 里元素重声明 | A2R01 / A2R03 | — | SKIP（不碰） |
| 类路径直调 `B.f` 与 `c.f` 并存 | A2P01；`P.f` 与 `c.f`（C extends 包 P）A2P02 | `B.f` 体里仍 `B.g`；`M.c.g` 另一份 | FAIL |
| 模型里直调 `B.g` | A2B20 | `B.g` 与 `M.c.g` 各一份 | FAIL |
| f→g→h 链 | A2L01 | `M.c.g`、`M.c.h` | FAIL |
| 两个组件 + `c1.g` 直调 | A2L02 | `c1.g`、`c2.g` | FAIL |
| `c.g` 直调与 f 体里的 g 同名 | A2Q03 | 一份 `M.c.g` | FAIL |
| g 返回 B 的本地记录 / 局部变量是 B 的记录 | A2T01 / A2T02 | omc 内部错 / 记录名 `M.c.R` | 两拒 / FAIL |
| g 形参是 B 的本地短类型 | A2T03 | `M.c.g`，`Real u(unit = "m")` | FAIL |
| B 有本地记录（不用） / 枚举 | A2B04 / A2B05 | `M.c.g` / — | FAIL / SKIP |
| f 里 `import B.g` | A2B06 | **`B.g`（类路径）** | PASS |
| external g | A2B07 | `impure function M.c.g` | FAIL |
| B 嵌在 C 里且 C extends B | A2B11 | `M.c.g` | FAIL（`C.B.g`） |
| f 体深处调 impure g（`{g(x)}`） | A2B16 | `impure function M.c.f` + `M.c.g` | FAIL（没后标 impure） |
| 用户函数叫 `size` | A2B18 | 报错 | ACCEPTS-INVALID |
| outer / inner | A2I01 | `M.c.g`（inner 路径） | FAIL |
| B 在包里 `extends P.B`（另调 P.h） | A2Q01、A68eZ54 | `M.c.g`，`P.h` 不变 | FAIL |
| B 在根类里 | A2Q02 | `M.c.g` | SKIP（底线 nullopt，不碰） |
| derivative / inverse 注解 | A2G01、A2B02（再调 g）/ A2B01 | `M.c.df` / `M.c.finv` | FAIL |
| 算法段、impure、数组输出、encapsulated B | A2G02、A2E03、A2E06、A2E08 | `M.c.g` | FAIL |
| 实例类里还按简单名调 f | A2E09 | — | SKIP（链路先发射，见 §6） |
| 函数值实参 `app(x, g)`（app 在顶层、写在绑定里） | A2B03 | — | SKIP（函数型实参在绑定里，不碰） |
| 函数值实参 `app(x, g)`（app、g 都在 B、写在算法段） | RVA2F1 | `M.c.app(x, M.c.g)` | FAIL |

## 3. ymc 现状（final 行号，worktree agent-a604f892029abe22b）

- 组件路线：try_emit_user_call :15613-15614（component_fn 成功即 `comp_fn_uses_.emplace_back(cfn.path, cfn.cls)`）→ qname =
  模型名 + cfn.path + 名、home = cfn.cls_def（组件类的定义）。f 是继承来的：component_fn 经 class_member_at 在 ext_bases_at 链上
  找到，fscope = `[f, B, B 的外层…]`，**各层不带实例节点**。
- f 体里调 g：scode_scope_of(空) = *fn_scope_ → fn_flat_name（:11440）：fn_emit_names_ 不命中、chain 为空（函数体语境 enclosing
  为空）、ds[0] = B ≠ 根、≠ fn_home_（= C）、ds 里无根、inh_tainted 假 → **类路径支 :11496-11498 → `B.g`**，home 无效。
  放开点 A（sibling_fn_name :11768）只在 fn_flat_name 给 nullopt 时才问，且要求 fs[1] == fn_home_，这里不满足。
- 同一个 fn_flat_name 还被 emit_referenced_function（:15225，derivative / inverse）与 fn_value_target（:15416，函数值）在函数体里
  调用，所以改在 fn_flat_name 内部，三条路线的命名一致。

## 4. 放开点 A2（只改 fn_flat_name 的类路径支）

位置：fn_flat_name 里 `if (inh_tainted(ds)) return std::nullopt;`（:11495）之后、拼类路径 head 之前：

```cpp
if (std::optional<std::vector<Symbol>> an = inherited_sibling_name(fn, scope, enclosing, first, ds)) {
  home = fn_home_;   // 被调函数照样按组件路线的家发射，它体里再调兄弟（f→g→h）同样走这里
  return an;
}
```

`inherited_sibling_name` 只读 SCode 元素表、Inst 已建好的实例树、comp_fn_uses_ / fn_emit_names_ / fn_scope_ / fn_home_，不查名
（不调 find_scode_element_at / resolve_* / class_member_at / ext_bases_at / collect_fn_sig）、不求值、不碰 Instantiator、不写任何成员。
记号：fs = *fn_scope_，E = fs[0] 的元素，B = fs[1] 的元素，en = fn_emit_names_.back()，name = 调用名，mlen = 模型名段数。
条件**全部**满足才返回名字，否则 nullopt（调用方照旧拼类路径）：

- **C0 语境**：fn_scope_ 非空、fn_home_ 有效、fn_emit_names_ 非空；enclosing 无效或空节点；传进来的 scope 与 fs 逐层 cls 相同；
  fs.size() ≥ 2；en.first == fs[0].cls；en.second 以模型名开头、长度 ≥ mlen + 2、末段 == E 的名字。
- **C1 调用名**：单段、无前导点、无下标；name 不是 size / array / time / Boolean / Integer / String / print。
- **C2 被调者**（first）：ds[0].cls == fs[1].cls 且 first 是 B 的**直接**本地元素（在 B.def.elements 里）；first 是长定义（Parts）、
  restriction 为 Function / ExternalFunction、非 replaceable、非 partial、非 redeclare；它自己没有本地类元素、没有 import 元素。
- **C3 E**：长定义 Function / ExternalFunction、非 replaceable；没有本地类元素、没有 import 元素（A2B06）。
- **C4 B**：Parts 类；本地类元素全是"长定义、非 replaceable、非 redeclare 的 Function / ExternalFunction"（有记录 / 短类型 / 枚举 /
  包 / 模型 → 不放：A2T02、A2T03、A2B04、A68eZ51）；没有 import 元素；没有 constant 组件（A2X03；parameter / 变量允许，函数看不到）。
  partial 的长定义函数（只作函数型形参的类型，如 RVA2F1 的 `partial function F`）算函数、允许：函数型形参打成
  `h<function>(#Real u) => #Real h`，不带类型路径，不会出现类路径名；被调者本身仍须非 partial（C2）。
- **C5 E 是继承来的**：fs 的任何一层都 ≠ fn_home_（排除 E 就声明在组件类里——那是 A 的地盘——以及 B 词法嵌在组件类里的 A2B11）。
- **C6 组件实例**：在 comp_fn_uses_ 里找 path == en.second[mlen, size-1) 且 `I_.nodes[cls].def == fn_home_` 的项。同一个
  (path, cls) 会被记多次（每调一次 component_fn 记一条），"唯一"按**不同的 cls 值**算：去重后恰好一个 cls 才放，零个或多个 → 不放。
  cls 须是类实例（is_instance），且 `I_.nodes[cls].instance_parent` 是 Component 节点（同 sibling_fn_name 3'）。路径按完整的组件路径
  比，不同子模型里的同名组件（RVA2W1 的 `s1.c` / `s2.c`）各对各的。
- **C7 实例树不重名**：N = {cls} ∪ 经 `I_.trees[I_.node_class(n).tree].exts` 递归到的全部基类实例（任一节点无效 / 无类树 / def 不是
  Parts 类 → 不放）。对 N 里每个节点的 def 的本地元素（类或组件）计数：叫 name 的恰好 1 个且就是 first；叫 E 名字的恰好 1 个且就是
  fs[0]；N 里至少一个节点的 def == fs[1].cls。另查 cls 类树 lookup 里的 name 与 E 名：须是 Class 项、对应节点 node_type 不是
  RedeclaredClass、def 分别 == first / fs[0]、下标不在 classes_via_mod 里。（计数按节点不按定义去重：菱形 A2B22 计 2 → 不放。）
- **C8 纯度可见**：`sibling_calls_visible(E, B)` 为真（原卡条件 7 的同一个只读函数，H 换成 B；A2B16）。
- **C9 不碰底线的 nullopt**：位置本身保证——只在底线已决定返回类路径名时才问。

生成：`q = en.second[0, size-1) + name`；断言 q 以模型名开头、长度 > mlen（同 sibling_fn_name 的 YMC_ASSERT）。**不置 released_68e_**。

## 5. 与原卡各条的关系

- 条件 3'：A 要求 H 层带实例节点且实例父是组件；A2 恰是互补情形——fscope 不带实例节点（继承来的 E），组件实例改由 C6 从
  comp_fn_uses_ 取回，同样要求 instance_parent 是组件。两者不会同时命中（A 要 fs[1] == fn_home_，A2 要 C5）。
- 条件 5：C7 是它的加强版——不止看基类实例，连组件类自身、兄弟基类、中间层都计数，并查实例树没被重声明。
- 条件 7：C8 直接复用 sibling_calls_visible（H → B）。A2 不新增纯度判定，不改 fn_marked_impure / fn_impure。
- G0：A2 只改名，不打开底线 SKIP 的模型（底线已发射同样的函数体），所以不置 released_68e_、G0 不因 A2 多扫。IN58 这类模型由
  A 置位，G0 照原卡跑。
- G（derivative）：A2 在 fn_flat_name 里，emit_referenced_function 拿到非 nullopt 的名字就不进 G 那支，二者不冲突。
- home：A2 返回时 home = fn_home_（组件类的定义）。被调者体里用到 fn_home_ 的只有 fn_flat_name :11490、rec_flat_name、
  sibling_fn_name——C5 保证组件类不在被调者的词法链上，三处行为与底线（home 无效）相同。

## 6. 规则 (a) 预测（只有 FAIL→PASS 且逐字节同 omc，其余一律不变）

扫描方法（v2 口径）：`a2scan.py` 在压平输出里找"名以 `M.` 开头且至少 M+2 段的函数体里**整词出现**不以 `M.` 开头的多段函数名"，
不要求后面跟括号，所以调用和函数值实参（`app(x, B.g)`，fn_value_target 那条路线）都算，外加只被注解引用的多段函数（"unref"，
口径不变）。v1 只认 `名(`，会漏掉"兄弟只当函数值出现"的情形（评审 RVA2F1）；v1 结果留在 `a2scan_ra.v1.tsv` / `a2scan_msl.v1.tsv`。
命中的抽成 a2ra 实跑 omc。`a2pred.py` / `a2pred2.py` / `a2pred3.py`（多处改名）在 ymc_final 输出上模拟 A2 的改名（只改组件路线
函数体里的兄弟引用）后与 omc 逐字节比。

**探针快照**（impl/ra_base.json，底线退出 0 共 14578；ra_final.json 共 14842）：
- v2 口径命中 123 个模型 / 281 行（a2scan_ra.tsv；v1 是 53 个模型）；ra_final 125 个，多出的是 A68eIN58、pk-p54r GF06（与 v1 相同）。
- 新增的 70 个：oc-f1 ProbeRand*（56）、oc-p1（11）、oc-ut（2）、pk-p54 A68eIN57（1），共 213 行，**全是记录类型名**
  （`L.Orientation` 195、`OCa.Orientation` 11、`OCv.Orientation` 4、`MB.Orientation` 2、`B.R` 1）。它们在函数体里只作形参的
  类型出现（如 `input OCa.Orientation R1`），输出里那个同名"函数"是记录的默认构造函数块。这些函数体里的调用只有内建 `atan2`，
  没有一个经 fn_flat_name 查的用户函数名，A2 无名可改。另外：oc 系列的 E 是 `equalityConstraint`，经 emit_oc_constraint 走
  component_fn，但它就声明在记录 Orientation 里（fs[1] == fn_home_），C5 也挡；IN57 的 B 里有记录 R，C4 挡。→ 全部不变。
- 会变的（与 v1 相同）：

| 用例 | 底线 → A2 | 依据 |
|---|---|---|
| pk-p54 A68eIN51 | FAIL → PASS | a2pred SAME |
| pk-p54 A68eIN56 | FAIL → PASS | a2pred SAME（三层 A→B→C） |
| pk-p54 A68eIN59 | FAIL → PASS | a2pred SAME（方程 + 算法） |
| pk-p54d A68eZ54 | FAIL → PASS | a2pred SAME（`extends P.B`，`P.h` 不变） |
| pk-p54 A68eIN58（目标） | 底线 SKIP、final FAIL → PASS | a2pred2 SAME |

v1 口径就命中、被条件挡住而保持不变的：IN62 / IN70 / IN73（C2 replaceable）、X17 / X23 / W03 / V13（C7 重名）、Z51（C4：B 里有包 Q）、
X59 / Z55（包在实例类里，C5/C6/C7）、Y58 / Y59 / Z62 / Z63 / W55 / W56 / W57（E 声明在组件类里，C5）、Y32（import，C2/C3）、
其余 PASS 命中（Z10-Z13、W34-W37、IN24、VA02、Y01-Y38、PKT13 / PKT14 / PKZ6 / PKF4 / HRA38 / HCX20 / d05、GF06 / pk-p54r GF06）
是多段调用名、E 在组件类本身、包实例或注解引包函数，C0-C7 都挡。被挡的 FAIL 若硬放会出错（IN62 / X17 / W03 / V13 / Z51 的
a2pred 都是 DIFF），所以挡住是必须的。

**语料**（$S/sweep_cur.json、ts_cur.json、msl_cur.json 是底线；rv5/*_m68erF.json 是 final）：
- scodeinst：底线 FAIL 54（final 同一组），逐个用底线 ymc 跑出输出（54 个全退出 0），v2 口径重扫（`a2fail/*.base.txt`）：0 命中。
- testsuite：底线退出 0 = PASS 1918 + ACCEPTS-INVALID 3（final PASS 1927 + AI 3，AI 同一组）。AI 3 个（ConnectArrayCond、
  DimUnknown17、IfExpression13）v2 照 nf_live_sweep 的口径去掉 `-d=-newInst` 注解后重跑（`a2fail/strip.py` 调
  nf_live_sweep.strip_annotation，副本在 `a2fail/ts/`）：3 个都退出 0，输出里函数块 0 / 1 / 0 个，0 命中。（v1 拿原文件直接跑，
  因为带注解而非 0 退出，那次的"0 命中"不算数，以这次为准。）
- MSL：底线 PASS 500 / SKIP 14（final PASS 501）；omc 缓存 build/msl-oracle 501 份按 v2 口径重扫，仍只有 2 处（与 v1 相同）：
  TwoTanks 的 `pipe.flowModel.WallFriction.*` 引 `Modelica.Fluid.Utilities.regFun3` / `cubicHermite_withDerivative`，都是包函数
  （被调者不是 E 的兄弟，C2 挡）。
- PASS 用例不可能被 A2 改：A2 只替换底线本来要返回的类路径名，而 C0-C8 下 omc 必是 `M.<组件路径>.g`；底线若在这里打了类路径名，
  输出就与 omc 不同、不会是 PASS（类路径首段是顶层类，不会等于模型名，ds 里有根时底线早返回 nullopt）。
- **预测：三个语料 0 变化。**

**新探针 a2 / a2b**（相对 ymc_final；全部"变化"都是 FAIL→PASS 且 a2pred SAME）：
- 变 PASS：A2V01、A2M01、A2S03、A2X01、A2N01、A2N02、A2P01、A2P02、A2L01、A2L02、A2I01、A2Q01、A2Q03、A2G01、A2G02、A2E03、
  A2E06、A2E08、A2E10；A2B01、A2B02、A2B07、A2B10、A2B15、A2B20；impl/v1 R68eV01。
- 保持 final 的结果：A2M02 / A2X02 / A2A02 / A2R01 / A2R03 / A2Q02 / A2E04 / A2E07 / A2B03 / A2B05 / A2B19 / A2B21（SKIP）；
  A2A01 / A2T01 / A2E02 / A2B12 / A2B17（两拒）；A2B18（AI）；A2S01 / A2S02 / A2E05 / A2B06（PASS）；A2X03 / A2D01 / A2D02 / A2R02 /
  A2R04 / A2T02 / A2T03 / A2E01 / A2B04 / A2B08 / A2B11 / A2B16 / A2B22（FAIL，KNOWN）。
- A2E09：预期 SKIP 不变（c 的绑定 `z = f(time)` 先走链路发射 `M.c.f`，home = B，C5 挡；底线报错就是这条）。实现后若它变了，
  **不自动接受**：A2 只改名、不改发射顺序，它一变就说明 A2 影响了别的控制流，要先查清根因；查明合理、并与 omc 逐字节一致之后，
  才能列进清单。查不清 → 按 §8 处理。
- A2B12 是探针笔误（`inner` 是关键字，两边都拒），可改名后重跑，不影响结论。

**评审探针 r13**（相对 ymc_final）：
- RVA2F1 FAIL → PASS：f 体里 `app(x, g)`。app 是调用，经 try_emit_user_call → fn_flat_name；g 是函数值，经 fn_value_target（:15416）
  → fn_flat_name；两者都满足 C0-C8（B 里的 F 是 partial 函数类型，按 C4 允许），A2 分别改成 `M.c.app` / `M.c.g`。app 以 home = C
  发射，它的形参 `F h` 打成不带路径的函数型。a2pred3 模拟（c.f 体里两处改名、复制改名 app / g 两块、删掉不再被引用的 `B.app` /
  `B.g`）与 omc 逐字节相同。
- RVA2W1 FAIL → PASS：`M.s1.c.f` / `M.s2.c.f` 各自的 en.second 去掉末段，分别是路径 [s1, c] / [s2, c]，C6 各找到自己的 cls，g 改成
  `M.s1.c.g` / `M.s2.c.g`，`B.g` 不再被引用而消失。a2pred3 与 omc 逐字节相同。
- RVA2H1 / RVA2K1 / RVA2E1：SKIP 不变（原因分别是 inh_tainted、check_comp_fn_consts、根类链路发射的 `M.f` 体里 g 无名，都发生在
  A2 之外，A2 也不会改变发射顺序）。

## 7. 验收（全部过才算完成）

1. 目标：pk-p54 A68eIN58、impl/v1 R68eV01 与实跑 omc 1.25.0 逐字节一致。
2. 新探针：a2.probes、a2b.probes、评审的 r13.probes（`redo/68e-review/r13.probes`）逐个与 §6 的预期一致（omc 实跑，240 s，并发 ≤ 2）：
   r13 里 RVA2F1、RVA2W1 PASS，RVA2H1 / RVA2K1 / RVA2E1 保持 SKIP。固化进 tests/probes（建议 `pk-p54s.probes`，沿用原名 A2* / RVA2*）。
   单测期望取 omc 输出（IN58 / V01 / A2L01 / A2G01 / A2B06 / RVA2F1）。
3. 机械差分（对 `redo/68e/ymc_final.exe`）：
   - 探针快照（runall.sh 同一份探针清单 + a2 / a2b / r13 / impl/v1）：只允许 §6 列出的变化——快照里 IN51 / IN56 / IN58 / IN59 / Z54
     FAIL→PASS，新探针按 §6；其余 rc 与 stdout 全部逐字节相同。
   - 语料（corpus.sh：scodeinst / testsuite / MSL）：changed 0。
4. 原卡验收全部保持：10 个目标逐字节；p6 / p8-p11 / rv_r1 / rv_r2、p1 P05-P10 / P23-P26 0 变化。
5. gcc-debug / msvc-debug / msvc-asan：ctest 全绿、零警告、ASan 0。

## 8. 退路

任何一条不在 §6 清单里的差异（含 PASS→别的、SKIP→别的、FAIL→不同的 FAIL、预期 PASS 却与 omc 不一致）→ **整个 A2 退回**
（删掉 fn_flat_name 里那一段与 inherited_sibling_name，恢复 ymc_final 行为），IN58 照原卡记成规则 (c) 第二条的 KNOWN，报主代理。
不做部分保留、不在共享函数里加新判定去补洞。

## 9. 不做 / 仍是 KNOWN

- replaceable g（已定：不放）。IN70 / IN73 / A2R04 即使没被重声明也记 KNOWN（a2pred 若放会 SAME，但外层修饰重声明的情形没逐一探）；
  IN62 / A2R02 被重声明，放了就错。
- 重名 / 菱形（X17 / X23 / W03 / V13 / A2D01 / A2D02 / A2E01 / A2B08 / A2B22）——65b 的实例树取舍。
- 基类里有记录 / 类型 / 枚举 / 包 / 常量（A2T02 / A2T03 / A2B04 / A2X03 / Z51）——记录要连 rec_flat_name 一起按实例命名，不在本桶。
- E 声明在组件类本身、体里调继承来的兄弟（A2E04）、f 与 g 不同层（A2M02）、B 在根类里（A2Q02）、B 嵌在组件类里（A2B11）、
  包在实例类里（X59 / Z55）、外层类的函数（Y58 / Z63 / W55-W57）——底线 nullopt 或另一条路线，不在本卡。
- 纯度：深处调 impure 兄弟（A2B16）照原卡条件 7 收窄。

## 10. 已定的点（评审 / 用户，2026-11-12）与仍拿不准的

已定：
1. replaceable 但没被重声明的 g：不放，IN70 / IN73 / A2R04 记 KNOWN（§9）。
2. A2 不置 released_68e_（不触发 G0），照 §5；"底线 FAIL 的 A2 形态模型里另有 print 折叠差异"这类清单外变化靠 §8 整项退回兜底
   （快照与语料里都没有）。
3. A2E09 变了不自动接受，先查根因（§6）。
4. C6 的"唯一"按不同的 cls 值算（§4）。

仍拿不准（不影响开工）：
1. 拿组件实例的方式：本卡用 comp_fn_uses_ 按路径 + 定义回查（只读、零新状态）。另一种是给 FnCtxGuard 加一个"组件类实例"字段，
   由组件路线传进 emit_function（改 emit_function 签名和 6 个调用点）。实现者若想换写法，须保证 C6 的判定结果不变。
2. testsuite PASS 1918 个没有输出可扫，"PASS 不会被 A2 改"靠 §6 的推理加验收 3 的语料差分兜底。语料差分若出现 PASS 变化，就说明
   推理在某个角落不成立，按 §8 处理。
