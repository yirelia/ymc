# T-S3-68e-redo-A3 补充卡：函数体里经词法查到"被实例化的外层"的名字（XE01 族）+ A2 收编 + C10 回落撤回 + XE08 收窄

> 第 1、2 步产出（§7.4）。用户决定 **1A 2B**（todo「68e / 68f 收敛方案」11-13 两条 + brief 末尾「更正」节）：
> 照 omc 实例树查名统一补一次、A2 并入；规则 (a) **不加** FAIL→FAIL′ 例外；A2 作为共用路径改动**整项撤掉**；C10 族与 B 的同名回落在放开点入口撤回底线行为；
> A3 **只在本次压平走过 68e 放开点时生效**，底线退出 0 的模型一个都不变；XE08 按规则 (c) 第一条收窄成 SKIP。
> **v2（2026-11-13）**：按第 3 步评审（`$A/review/REPORT.md`，有条件通过）改：①FB 超集加"指向这些函数的改名 / 限定 import 别名"（RVG01-04）；②X0 不成立而 ds / 传入 scope 带实例层 → U（RVL01 / 02）；
> ③A / G / B 判定点不论放不放都判 FB（RVC01、RVX01）；④G1 补记录字段修饰与多段记录类型逐段下降（RVI01 / 03）；⑤推测性定型遍（raw typing）不放、列全吞 SKIP 的点；⑥改写 N 的论述，RVX07 记 KNOWN；
> ⑦§15 第 2、3 条按评审更新（不加帧栈、改超集）；⑧rv_* 与新写的门控探针 a3g 并入验收；⑨size 等特判、行号。§7.1 预测按 v2 重算。v1 备份 `T-S3-68e-redo-A3.v1.md`。
> 原卡 `T-S3-68e-redo.md`（v6）不改。**本卡取代 A2 卡 `T-S3-68e-redo-A2.md` 的 §0、§4–§10**（A2 卡 §1 OMC 逻辑、§2 探针表、§3 现状仍作本卡 Y 支的依据，引用时注明）。
> `$S` = `C:/Users/yangrui/AppData/Local/Temp/claude/C--dev-ymc/4e49f17e-0607-4a2d-8c06-17ac54b3dead/scratchpad`；`$A` = `$S/redo/68e/A3`。
> 底线 **B0** = `$S/redo/ymc_base_5c7.exe`（5c7cd5b）；参照 **final2** = `$S/redo/68e/ymc_final2.exe`（worktree `C:/dev/ymc/.claude/worktrees/agent-a604f892029abe22b`，未提交）。
> ymc 行号一律是 final2 worktree 的 `src/flat/flatten.cpp`；OMC 只认 v1.25.0（`$A/src/*.mo` = `git show v1.25.0:…`，已与 `$S/redo/68e/src_*.mo` 逐字节核过）。
> 笔记 `$A/NOTES.md`；探针三方结果 `$A/pr/<集>/<模型>/{omc,base,new}.out`（new = final2），评审探针 `$A/review/pr/<集>/<模型>/`。

## 0. 结论

- **XE01 族 8 个**（XE01 / XE01c / XE01d / XE01e / XE01f / XE01h、XE12、XE16）全部改成 **PASS**（与 omc 逐字节）。机制：fn_flat_name 类路径支（:11502-11504）在函数体语境
  （chain 为空）不看作用域层上带的实例节点；而 ymc 的 `ScodeScope` 层本来就带着 omc found_scope 对应的实例（`S@Sinst` / `S@Sb`），已有的 `scope_path()`（:16988，
  = NFInstNode.scopePath / scopeList）照它拼就是 omc 的前缀。
- **门控**（2B 的难点）：A3 的改名 / SKIP 只在 `released_68e_` **已经为真**时做；放开之前遇到 A3 形态（或 C10 回落），照底线做并置 `blocked_`，之后四个放开点入口一律不放
  （= 底线 SKIP）。"走过放开点"在名字落定时就已知（`released_68e_` 的当前值），名字先于放开落定的就在放开点入口 SKIP。推测性定型遍里一律不放（v2）。
  底线退出 0 的模型从不放开 → A3 不生效、`blocked_` 无作用 → **逐字节不变**。
- **A2 收编**：A2（:11496-11501）作为独立改动撤掉；它的 C0–C8 原样成为 A3 的 **Y 支**（只在放开后）。A2 修好的底线 FAIL 回到 B0 字节（老错，KNOWN / open-questions）。
  A2 原目标 **IN58 类**（A68eIN58、A2E10：`c.f` 先于 `b.f`）→ **入口 SKIP**；放开先于命名的同形（A3Y01 / Y02 / Y04 / Y05）→ PASS。
- **C10 回落撤回**（v2）：A / G / B 三个判定点**不论本来放不放**都查 FB：A 用调用名本身，G / B 用"被发射函数元素名 ∪ 别名链头 ∪ fn_emit_names_ 末段 ∪ 指向这些函数的改名 / 限定 import 别名"。
  命中 → 不放；回落是 omc 特名（size / array / time / Boolean / Integer / String）的到此为止，否则放开前置 `blocked_`、放开后 SKIP。不改 call()（§15-2）。
- **XE08 收窄 = G1**：放开过的模型、本次解析到的函数（enc_fns_）的形参 / 局部深处（含记录字段修饰、多段记录类型的字段）调了"可能不纯"的函数 → SKIP（与 G0 同位、同门控，只读 SCode）。
  G1 同时盖住同根因的 A3X20 / A3X33 / RVI01 / RVI03 / RVJ02（§15-3 已定扫全部 enc_fns_）。
- 不做：重跑压平（§15-1）；数组 / DUP / 包内包 / replaceable / 根基类的兄弟链 / X0 不成立的实例层等一律 U → 放开后 SKIP（§15-5）；继承来的 E 里的外层实例调用（RVX07）、
  放开模型里与放开点无关的回落暴露 → KNOWN（§15-4、todo 老路线补齐范围）。

## 1. 目标（定死）

| # | 用例 | B0 | final2 | A3 后（期望） | 走哪条 |
|---|---|---|---|---|---|
| 1 | adv_x XE01 | SKIP | FAIL | PASS（`XE01.s.h`） | X |
| 2 | adv_x2 XE01c（S 在包 P 里） | SKIP | FAIL | PASS（`XE01c.s.h`） | X |
| 3 | adv_x2 XE01d（derivative = h） | SKIP | FAIL | PASS | X（emit_referenced_function，体发射完之后 :14902） |
| 4 | adv_x2 XE01e（兄弟 g 体里调 h） | SKIP | FAIL | PASS | X |
| 5 | adv_x2 XE01f（两个 S 实例） | SKIP | FAIL | PASS（`s1.h` / `s2.h` 各一份） | X |
| 6 | adv_x2 XE01h（`Q.h`，Q 是 S 的本地包） | SKIP | FAIL | PASS（`XE01h.s.Q.h`） | X（两段名） |
| 7 | adv_x3 XE12（S 是根的基类） | SKIP | FAIL | PASS（`XE12.h`） | X（基类实例 → 根） |
| 8 | adv_x3 XE16（S 里 `c.f` 组件路线） | SKIP | FAIL | PASS | X |
| 9 | pk-p54 A68eIN58、a2 A2E10（A2 原目标） | SKIP | PASS | **SKIP**（入口，blocked_） | Y 形在放开前命名 |
| 10 | adv_x XE08 | SKIP | FAIL | **SKIP**（G1） | 规则 (c) 第一条 |
| 11 | C10 族与同名回落：p4 R68eC10、pk-p54r R68eC10；adv XA06、XB30-XB36、XB41、XF05、XF06；评审 RVG01-04；a3g A3G01、A3G05 | FAIL（退 0） | PASS / FAIL′ / SKIP | **与 B0 逐字节相同** | §6.3 |
| 12 | A2 修好的底线 FAIL（§7.1 清单 A） | FAIL（退 0） | PASS / FAIL′ | **与 B0 逐字节相同** | A2 撤掉 |
| 13 | 原卡 10 个目标（AssertTest4-7、IntegerTest、4 个记录、GenerationOfFMUs） | SKIP | PASS | PASS 不变 | — |

须保持的邻近写法（brief）：XE01g（改 S 的常量）、XE11（S 的枚举）SKIP；XE13（S 的短类型）、XE14（h 在模型语境调）、XE15（S 的常量）PASS；XE01b（不调兄弟，B0 FAIL 退 0）与 B0 同。
评审反例（v2 必须收住）：RVL01 / RVL02、RVC01、RVX01 / RVX02、RVI01 / RVI03 / RVI04、RVJ02 → SKIP；A3G04 → SKIP。

## 2. 来源

- OMC v1.25.0：NFFunction、NFLookup、NFInstNode、NFClassTree、NFInst、NFFunctionDerivative、NFFunctionInverse、NFComponentRef、NFCall（`$A/src/`）；NFModelicaBuiltin.mo（`$S/redo/68e/NFModelicaBuiltin.v125.mo`）。
- 探针（omc 1.25.0 / B0 / final2 三方，`python -I -X utf8 $A/run3a.py <集>`）：对抗评审 `$S/redo/68e-adv/adv_x*.probes`；本卡 `$A/a3x.probes`（27）、`a3y.probes`（5）、`a3c.probes`（7）、
  `a3g.probes`（5，v2 门控定向）；第 3 步评审 `$A/review/rv_{g,x,c,i,j,k,l}.probes`（27）。
- 扫描：`$A/a3scan.py`（探针快照 ra_base_all / ra_stA2）、`a3scan_dir.py`（三方目录）、`a3corpus.py`（MSL omc 缓存 + testsuite Result 段）、`a2land.py`（A2 系列落点）、`fnext_scan.py`（放开模型里的函数继承）。

## 3. OMC 处理逻辑（第 1 步结论）

- O1 函数名 = found_scope 的 scopeList + 调用名：`lookupFunction` NFFunction.mo:317-345，`prefix := ComponentRef.fromNodeList(InstNode.scopeList(found_scope, includeRoot = is_class))`（:343）。
  found_scope = 调用名**首段**找到处（CREF_QUAL 先 `lookupSimpleCref` 首段、再 `lookupCrefInNode`，NFLookup.mo:323-333；进类先 `Inst.instPackage`，:965-977）。
- O2 查找走法：`lookupSimpleCref` NFLookup.mo:838-908，逐层 `lookupLocalSimpleCref`（:910-938），不中 → `parentScope`（:898）；encapsulated → topScope + 只认内建（:885-888）。
  import 命中 → `foundScope := InstNode.parent(node)`（:932-933，定义层 → 类路径；探针 A3X10 omc `P.h`）。名字特例：time / Boolean / Integer / String 先查内建（:815-836、:855），
  size / array 按名特判（NFCall.mo:2321 / :2325，原卡 A6）——这些名字 omc 一律用内建，用户同名函数不被调。
- O3 实例树的父节点：`ClassTree.instantiate` NFClassTree.mo:428-560——实例空时 `instance := clsNode`（:472-475）；extends 改成 `BASE_CLASS(instance, …)`（:496-498）再递归（:500）；
  非 typish 本地类 `clone` + `setParent(instance, c)`（:517-524）；组件 `setParent(instance, c)`（:559-560）。组件的类在组件下实例化（`instTypeSpec` NFInst.mo:2276-2283 → `instClassDef` :1140-1146），
  类里的表达式以**组件节点**为作用域（`instComponentExpressions` → `instExpressions(c.classInst, node)` NFInst.mo:2689）。
- O4 parentScope：COMPONENT_NODE → `parentScope(classInstance)`（NFInstNode.mo:714）；CLASS_NODE → `node.parentScope`（:713）。所以 f（在 c 的类里 clone、父 = c）体里查不到的名字：
  f → c → c 的类（H 的 clone）的父 = s（H 在 S 的实例里 clone，父 = s）→ 在 s 的类树里找到 h，**found_scope = s**。根的基类：S 的本地类父 = S 的 BASE_CLASS 节点 → found_scope = 该节点。
- O5 scopeList：NFInstNode.mo:1118-1136（COMPONENT_NODE：parent 链 + 本节点 :1132）；scopeListClass :1138-1170（NORMAL_CLASS 加名后 parent :1146-1147；**BASE_CLASS → ty.parent、不加名** :1148-1149；
  ROOT_CLASS 带根名 :1156-1160）；fromNodeList 不带下标（NFComponentRef.mo:1534-1541）。→ XE01 `XE01.s.h`、XE12 `XE12.h`、A3X27（两层基类）`A3X27.h`、A3X02（两层外层）`A3X02.t.h`、
  A3X17（`S s[2]`）`A3X17.s.h`；根是短类别名 RVX05 `RVX05.s.h`、S 里的包 Q 里的 H RVX04 `RVX04.s.h`、W 里的 H RVX06 `RVX06.s.h`、根里 S2 extends S RVX08 `RVX08.s.h`。
- O6 重名：派生类与基类同名元素时，`replaceDuplicates`（NFInst.mo:1193 → NFClassTree.mo:724-739、replaceDuplicates3 :2301-2316 的 `Mutable.update(node_ptr, kept)`）把共享槽换成保留的那份，
  基类节点里查到的也是派生类的：A3X07（S2 extends S 又声明 h）omc 用 S2 的 h（`u * 30.0`）→ 属 65b 的实例树取舍。
- O7 注解与函数值走同一套：derivative 在 `InstNode.parent(fnNode)` 里实例化（NFFunctionDerivative.mo:77；XE01d、A3X34）；inverse 在函数节点里 `Inst.instExp`（NFFunctionInverse.mo:182；A3X12 `A3X12.s.h`）；
  函数值（A3X13 `A3X13.s.app(A3X13.s.h, x)`）。
- O8 A2 形（Y 支依据，A2 卡 §1）：组件类的基类节点 ty.parent = 组件（NFClassTree.mo:498），基类本地类父 = 基类节点（:474 / :523），scopeListClass 的 BASE_CLASS 支换成 ty.parent → `M.c.g`。
- O9 包 / 定义层仍是类路径：S 是包（A3X04 `S.h`）、h 在包 P 层（adv XA34 `P.ph`）、写全名 `S.h`（A3X30，首段在顶层）、`S.H c` 而 S 不被实例化（RVX03 `S.h`）。
- O10 继承来的函数成员：`f2 extends f1`（f1 在 S 里）的输入缺省 / 局部 `a = h(x)`，omc 在 f1 的基类节点里查（ty.parent = f2），仍落到 s → `RVL01.s.h`（RVL01 / RVL02 实跑）；
  E 从基类 B 继承进 H（RVX07）omc 同样 `RVX07.s.h`。
- O11 形参 / 局部深遍历标 impure：foldExpParameter NFFunction.mo:2230-2257（绑定、维、递归进组件的类树——记录字段的绑定与修饰、短类型的属性）：XE08、A3X20、A3X33、RVI01（`P.R` 字段缺省调 `P.ti`）、
  RVI03（`R r(a = 1 + ti(x))`）、RVJ02 omc 都打 `impure function …`。NFModelicaBuiltin.mo 顶层 impure：initial :64、terminal :72、homotopy :291、sample（:537 / :549 所在重载）、DynamicSelect :629、
  print :804、firstTick / interval（:881 / :887 重载到 :894-:924 的 impure 版本）、ticksInState :965、timeInState :973（system :1551、subSample* / delay2 / delay3 :1073-1110 在 encapsulated package
  OpenModelica :987 里，不是顶层名）。

## 4. ymc 现状（final2）

- fn_flat_name :11440-11505：嵌套函数支 :11458-11459；chain（只在模型语境）:11461-11488；根 :11489；`ds[0] == fn_home_` → nullopt（放开点 A 的地盘）:11490；ds 有根 / chain 层 → nullopt :11491-11494；
  `inh_tainted` :11495；A2 :11496-11501；**类路径支 :11502-11504**。函数体语境 chain 为空，:11491-11494 判不到"外层被实例化"，于是 `S.h`。
- 作用域层带实例：`scode_scope_of` :8912-8921（层 = `ScopeLevel{def, is_instance(n) ? n : {}}`）；`find_scode_element_at` 本层命中 `def_scope = scope[k..]`（:16325 起，带 inst）；
  `resolve_fn_from` 的 `fscope = {fe} + ds`（:17234 起）；组件路线 `component_fn` 本地函数 `hscope = scode_scope_of(cls)`（带 inst），继承来的走 `class_member_at`（不带 inst，A2 形）。
  继承来的函数成员的绑定在 `sig.decl_scope` 里定型（:14762-14769，临时把 fn_scope_ 换成基函数的定义链）→ 此时 en.first（f2）≠ fs[0]（f1）。
  ymc 实例：`Instantiator::instantiate` inst.cpp:1210-1238（instance_parent = parent）、本地类 `instance_class` inst.cpp:1177-1187（parent_scope = 实例）。
- `scope_path` :16988-17013：组件 → 名 + instance_parent；类实例（有 instance_parent）→ 跳过不加名（= BASE_CLASS / 组件的类）；无 instance_parent 的类 → 名 + parent_scope；根 → 模型名。
- 放开点：P `model_print_call` :12038-12063（builtin_call `!bf` :26238-26240 调）；A try_emit_user_call :15775-15782；G emit_referenced_function :15371-15379；B emit_function :14626-14637；
  `released_68e_` :33525；G0 run() :359（check_enclosing_refs :360 之前）。
- 回落：call() :22225 → try_emit_user_call（:22362，失败未 unsupported）→ record_ctor_call（:22373）→ sync_call（模型语境）→ builtin_call（:22381；特名先接，:26236 lookup_builtin，`!bf` → P / SKIP）。
  fn_fail 只记子原因、不置 unsupported（:28558-28561），失败一路传到最内层 call() 帧才回落：A 是兄弟调用本身；G 经 emit_function(E) 的 fn_fail（:14904）到 E 的帧；B 是 F 的帧
  （F 经 emit_referenced_function / fn_value_target 发射时是更外层的帧）。函数名当函数型实参（fn_value_target :15547）失败时外层调用回落 builtin_call 也定型不了函数值（A3G03：B0 SKIP
  `unresolved name [g]`），不会退 0。
- 会把 SKIP 撤掉 / 吞掉的点（门控要逐个过，§6.1）：推测性定型遍 emit_function :14866-14886（raw_typing_，存 / 复原 unsupported；RawOff :14488-14493 在被调函数发射时把 raw_typing_ 关掉）；
  QuietLookup :19920-19943（只包 resolve_class_path / resolve_cref_class 的分类查名，:8824 / :12673 / :19899）；check_enclosing_refs :16791-16801（开头见 SKIP 就返回，之后才清）；两遍压平 flatten() :33982-33995（r2 优先）。
- XE01 族 8 个在 final2 的落点（全是类路径支 :11502-11504，ds[0] 带实例却没人看）：

  | 用例 | E 怎么命名（fscope） | 外层名首段的 ds | final2 打 |
  |---|---|---|---|
  | XE01 | 模型语境 chain → `XE01.s.c.f`（[f, H@Hinst, S@Sinst]）；g 在 :11490 nullopt → 放开点 A | [S@Sinst] | `S.h` |
  | XE01c | 同上，S 在包 P 里 | [S@Sinst, P] | `P.S.h` |
  | XE01d | 同 XE01；h 是 derivative，emit_referenced_function :15368 → fn_flat_name | [S@Sinst] | `S.h` |
  | XE01e | E = g（放开点 A 命名 `XE01e.s.c.g`，fscope 同 f） | [S@Sinst] | `S.h` |
  | XE01f | 两个实例各自的 f | [S@Sinst1] / [S@Sinst2] | 两处都 `S.h`、只发射一份（omc 两份） |
  | XE01h | 同 XE01，调用名 `Q.h`，首段 Q 在 S 层 | [S@Sinst] | `S.Q.h` |
  | XE12 | `XE12.c.f`（[f, H@Hinst, S@Sb]，Sb = 根的基类实例） | [S@Sb] | `S.h` |
  | XE16 | 组件路线 component_fn（hscope = scode_scope_of(Hinst)，带实例）→ `XE16.s.c.f` | [S@Sinst] | `S.h` |

- 纯度：fn_marked_impure（:14400）对形参 / 局部只看绑定顶层；原卡条件 7 `sibling_calls_visible`（:11706-11767）只看兄弟；`maybe_impure_fn` :11669-11681；`print_kind` :11520-11567（词法走查模板）；
  `collect_mod_calls` 递归进全部子修饰（属性与记录字段）；`enc_fns_`（:16805，note_fn_body :16786 记本次解析到的函数的定义链；operator record 构造不记）。

## 5. 对照的 lessons

- 2026-11-11「返工反复」：新判定只挂在 fn_flat_name 类路径支末尾（用户已批准）与放开点 / 判定点入口；不改 call()（§15-2）；停损写死（§7.3）。
- 「检查挂一个入口，兄弟路线绕过去」：A3 放在 fn_flat_name 里，try_emit_user_call / emit_referenced_function（derivative、inverse）/ fn_value_target / decl_scope 定型同一个判定；
  blocked_ 挂在 P / A / G / B 四个入口；FB 在 A / G / B 三个判定点**不论放不放**都判（评审 RVC01 / RVX01：只在"本来要放"后判会漏）。
- 「import 命中给的是定义链」：X1 要求 ds 是 E 自己作用域链的后缀、X2 要求首段是 ds[0] 的直接本地元素；FB 超集补"改名 import 别名"（评审 RVG01-04：书写名不等于元素名）。
- 「基线碰巧对的要点名」：C10 族撤回按用户 2B 回到 B0；A3X04 / X10 / X30 / XA34 / RVX03 是类路径本来就对的，必须 N；omc 特名的回落本来就对（A15 / A16），FB 命中也不置位。
- 「实现者自测 0 FAIL 不等于没错」：§10 的预期清单逐个比；独立对抗评审照旧。
- 「用户的决定只记进度账」：1A 2B、§15 已进 todo（主代理 11-13 补）；open-questions TBD-19 ③ / ④ 要同步改。

## 6. 方案

### 6.1 门控 R（"走过放开点"在名字落定时已知）

- 新成员 `bool blocked_ = false;`（本次压平已经按底线做过"一旦放开就会错"的事）、`int raw_pass_ = 0;`（推测性定型遍深度，v2）。
- **置位 blocked_**（只在 `!released_68e_` 时）：(i) A3 判定分类为 X / Y / U 形（§6.2）而照底线给了类路径名；(ii) FB 命中（§6.3，回落名不是 omc 特名）。
- **四个放开点入口各加 `!blocked_ && (raw_pass_ == 0 || released_68e_)`**（不满足 = 不放 = 底线行为）：P `model_print_call` 开头；A :15776 的条件；G :15371 的条件；B :14626 的条件。
- **推测性定型遍**（v2，评审第 5 条）：emit_function :14877-14879 的 `raw_typing_ = true / false` 外面再包 `++raw_pass_ / --raw_pass_`（RAII，RawOff 不碰它）。这一遍里（含它触发的被调函数发射）
  一律不放：B0 在这一遍撞到放开点会 SKIP、被 :14881 复原后照旧退 0，新二进制若在这里放开就会留下发射的函数 / 置 released_68e_；不放就与 B0 同。blocked_ 在这一遍里照常置位（只多挡）。
  例外（v2.1，快速核对建议，主代理采纳）：`released_68e_` 已为真时 raw 遍照常放（此时模型 B0 必 SKIP，不影响 (a)；否则 raw 遍不存 fn_stmt_typed_，强制内联改用化简过的体，会与 final2 无谓不同）。入口条件因此写成 `!blocked_ && (raw_pass_ == 0 || released_68e_)`。
- **放开后**（`released_68e_` 为真）：X / Y 改名；U → `set_unsupported("A3: instance-scope function name not computed")` 并返回 nullopt（硬 SKIP，避免 fn_fail 走回落）；
  FB 命中（非特名）→ `set_unsupported("68e: builtin-named user function in a released model")`；G1 在末尾扫。
- **吞 SKIP 的点逐个过**（评审第 5 条）：
  - raw typing（:14866-14886）：上面 raw_pass_ 收住。
  - QuietLookup（:19920-19943）：只包 resolve_class_path / resolve_cref_class，到不了 try_emit_user_call / emit_function，不经放开点。
  - check_enclosing_refs（:16791-16801）：开头 `if (result_.unsupported) return;`，只在没有 SKIP 时清再重判，不撤放开点的 SKIP；G0 / G1 放在它前面。
  - 两遍压平（:33982-33995）：每遍新 Flattener，blocked_ / released_68e_ / raw_pass_ 各算各的；B0 退 0 的模型其最终一遍（r1 或 r2）不撞放开点，新二进制同一遍同样不放；
    第一遍交给第二遍的 carry 是参数值，不受命名 / 放开影响（评审核过无串扰）。
- 依据：每个放开点都只在底线 SKIP 的地方放（原卡 v6「不动底线输出」+ §6.3 把回落例外撤掉 + raw 遍不放）→ `released_68e_` 为真的模型底线必 SKIP；放开前一切照底线、不放 = 底线 SKIP。
  所以 A3 / FB-SKIP / G1 只可能出现在底线退出 66 的模型里。名字在放开前落定（IN58 的 `c.f` 先于 `b.f`；binary() :21687 先左后右；形参 / 局部先于体）→ 被 blocked_ 在入口 SKIP。

### 6.2 统一判定 A3（fn_flat_name 类路径支末尾，替换 A2 :11496-11501）

位置：`inh_tainted` 之后、拼类路径之前（底线本来就要返回类路径名的地方）；只读 SCode 元素表、Inst 实例树、comp_fn_uses_ / fn_emit_names_ / fn_scope_ / fn_home_，
不查名（不调 find_scode_element_at / resolve_* / class_member_at / ext_bases_at / collect_fn_sig）、不求值、不碰 Instantiator。三值：**Rename(名, home) / U（A3 形但算不准）/ N（照底线）**。

**X0 语境**：`fn_scope_` 非空、enclosing 无效或空节点、传入 scope 与 *fn_scope_ 逐层 cls 相同、`fn_emit_names_` 非空且 `en.first == fs[0].cls`。
**X0 不成立**（v2.1，快速核对改窄）：只在函数体语境（`fn_scope_` 非空）且 **ds 本身**有一层 `is_instance(l.inst)` 时 → **U**；其余一律 N（不看传入 scope——模型语境的传入 scope 至少带根实例层，看它会把根模型在包 Pk 里调 Pk.g 这类本来对的类路径名判 U、置 blocked_ 挡掉后面的放开；emit_record_ctor 的 rscope 同理）。RVL01 / RVL02 仍是 U：collect_fn_sig :17505 经 resolve_class_path → find_scode_element_at 在本层命中，decl_scope = [f1, S@Sinst]，h 的 ds = [S@Sinst]。

X0 成立时：

**X 支（XE01 族：首段落在带实例节点的外层）**——形态：ds 有一层 `is_instance(l.inst)`（:8924）。形态成立后下列全满足 → Rename，任一不满足 → U：
- X1 ds 是 fs 的后缀：`ds.size() < fs.size()`，逐层 cls 与 inst 都等于 fs 末尾对应层（首段在 E 自己的词法链上找到；import / 继承改道的 ds 不是后缀）。〔O2〕
- X2 `first ∈ ds[0].def.elements`（直接本地元素；import 命中 omc 打定义层类路径 :932-933，A3X10；继承来的由 inh_tainted 先挡，A3X03）。
- X3 `is_instance(ds[0].inst)` 且 `nodes[ds[0].inst].def == ds[0].cls`（首段所在层本身是实例；ds[0] 无实例而更深层有的——包内包 A3X16 的 `Q.k`——U）。〔O4〕
- X4 实例类树里就是它：`inst_tree_class(ds[0].inst, parts[0])` 有效、node_type 不是 RedeclaredClass、`def == first`、下标不在 classes_via_mod。〔O1：omc 在实例类树里查〕
- X5 不重名：从 ds[0].inst 沿"instance_parent 是类实例"的基类链上到最派生的实例 D，D 与它递归的全部基类实例（`I_.trees[...].exts`）的 def 本地元素里叫 parts[0] 的恰好 1 个且是 first。
  〔O6；A3X07 计 2 → U〕
- X6 首段形态：parts.size()==1 → first 是长定义 Function / ExternalFunction，非 replaceable / partial / redeclare（A3X05 replaceable → U，沿用 A2 §10.1 的定）；
  parts.size()==2 → first 是长定义 package，非 replaceable / redeclare / partial，无 extends / import，parts[1] 是它的直接本地长定义函数、非 replaceable（XE01h）；更长 → U。
- X7 parts[0] 不是 size / array / time / Boolean / Integer / String（omc 不按词法查，O2）→ 是则 U。
- X8 实例路径：`W = scope_path(ds[0].inst)` 有值且以 `result_.model.name` 开头；沿途组件节点的 SCode 声明不带维（attr.dims、type.dims 都空；A3X17 → U）、io 为 None（inner / outer → U）、
  不是重声明组件；沿途类节点都是实例（is_instance）或根，无 RedeclaredClass、无包实例。〔O5〕
- 生成：`名 = W ++ parts`；**home = ds[0].cls**（与 chain 支 :11484 同：同一个函数在模型语境命名时 home 也是它，体里再调 S 的兄弟走放开点 A；记录走 rec_flat_name 的 nullopt → SKIP）。
  断言名以模型名开头、长度 > 模型名段数。

**Y 支（A2 形：组件路线调继承来的 E，体里调基类兄弟）**——形态：`ds[0].cls == fs[1].cls`、fs[1].inst 无效、A2 C5（fs 各层都 ≠ fn_home_）、
comp_fn_uses_ 里至少一项路径 == `en.second[mlen, size-1)` 且 `nodes[cls].def == fn_home_`。形态成立后 **A2 卡 §4 的 C0–C8 全满足 → Rename(inherited_sibling_name 的结果, home = fn_home_)**，否则 U。
- 与"统一"的关系：C6 找到的组件类实例 cls 的基类实例里 def == fs[1] 的那个 Bb，`scope_path(Bb)` = 模型名 + 组件路径 = `en.second` 去末段（基类实例跳过不加名、组件加名，O5 / O8），
  所以 Y 的名字就是"首段所在层的实例 + scope_path"，与 X 同一公式；C0–C8 只是"这个实例能唯一、可靠地取回"的条件（A2 卡 §4，评审已过）。
- inherited_sibling_name（:11840-11956）函数体原样保留，只改调用处（不再在 :11498 直接调）。

**N**（v2 改写，评审第 6 条）：X0 成立、ds 各层都没有 ymc 的实例标记、且不是 Y 形 → 照底线类路径名（不置位、不 SKIP）。N **只在 ymc 的层标记如实时**与 omc 一致：
标记如实时 found_scope 是包实例 / 定义层，scopeList 就是类路径（O9；A3X04、A3X10、A3X30、XA34、RVX03 PASS；快照 255 个放开模型的输出里实例名函数体内类路径名 0 处，`a3scan.tsv`）。
已知不如实的：继承来的 E 的定义链经 ext_bases_at（按类元素记忆化 :16438 起）得来、可能不带实例层——RVX07（f 从 B 继承进 H，调 S 的 h；另一个组件 K 把模型放开）omc `RVX07.s.h`，按本卡可能 N → `S.h` FAIL。
f 本身不是放开函数，按 todo「老路线补齐范围」与 §15-4 记 **KNOWN + open-questions**（若实现时它的 S 层带实例，就走 X 得 PASS，同样合规）。N 里再有别的变种按 §7.3 停损。

**流程**：
```
cls = classify(...)                       // N / X / Y / U，Rename 时带名字与 home
if (cls == N) → 照底线拼类路径
else if (!released_68e_) { blocked_ = true; → 照底线拼类路径 }
else if (Rename) { home = …; return 名; }
else { set_unsupported("A3: instance-scope function name not computed"); return nullopt; }
```

### 6.3 C10 / 同名回落撤回（判定点入口，与底线回落条件等价；不改 call()，§15-2）

- 底线回落的条件：判定点不放 → fn_fail（不置 unsupported）一路传到**最内层 call() 帧**，该帧 try_emit_user_call 失败 → record_ctor_call（函数名不是记录，空）→ sync_call（模型语境）→
  builtin_call。帧的书写名被 builtin_call 在 `!bf` SKIP 之前接住（或被 sync_call 接住）→ 底线照内建继续（退 0，C10 老错）；否则 → SKIP。
- **判定点**（v2：不论本来放不放都判；只改这三处入口，评审第 3 条）：
  - A：try_emit_user_call 里 fn_flat_name 返回 nullopt、单段无前导点无下标处（:15776，放开点 A 的地盘，函数体与模型语境都算；含 sibling_fn_name 不放的：RVC01 的 replaceable sin、RVX01 的根基类兄弟 abs、
    A3G02 模型语境里短类别名 sin 若走到这里——B0 退 0，只置 blocked_，输出不变）。
    候选名 = 调用名 fn.parts[0]（就是最内层帧的书写名，精确）。
  - G：emit_referenced_function 里 qname 为空处（:15371，含 derivative_only_ref / sibling_fn_name 不放的、inverse 引用）。候选名 = 超集（下）。
  - B：emit_function 里 rpath 为空处（:14626，含 root_record_type 不放的）。候选名 = 超集。
- **超集**（G / B；§15-2 + 评审第 1 条）：被发射函数元素名 ∪ 别名链头（fr.named）∪ fn_emit_names_ 全部末段 ∪ **整程序 SCode 里改名 import / 限定 import 的有效别名**——
  只收"目标路径末段等于前面某个函数元素名"的那些（RVG01 `import abs = RVG01.f;` → f 的超集加 abs；只读扫，不解析 import 目标）。不能"程序里有任何 FB 名 import 就命中"
  （MSL 的 `import Modelica.Math.sin;` 会误挡 GenerationOfFMUs：其超集元素名 position / position_der / position_der2 与 sin 不等）。G 的 E 经短类别名调用时末段就是别名（fn_flat_name 用首段元素名
  :11449，短类别名的元素就是它自己），A3G05（用户 sin 带 derivative）由 fn_emit_names_ 末段 `sin` 命中。函数名当函数型实参的外层调用不用进超集（O / §4：回落定型不了函数值，A3G03）。
- **判定**：候选名任一 ∈ FB → **不放**；若命中的都是 omc 特名（size / array / time / Boolean / Integer / String，O2：omc 也用内建，回落是对的，A15 / A16）→ 到此为止；
  否则放开前 `blocked_ = true`（然后照底线回落），放开后 SKIP（§6.1）。不命中 → 再判 `!blocked_ && (raw_pass_ == 0 || released_68e_)` → 本来要放的才放。
- `fb_name(n)`（超集即可：多拒只少放；漏一个就会改底线退 0 的输出——验收 b 抓）：
  - 单段名：`lookup_builtin(n) != nullptr`（:84-130：abs sign sqrt sin cos tan asin acos atan atan2 sinh cosh tanh exp log log10 ceil floor integer div mod rem Integer max min der pre initial terminal
    edge change sample smooth semiLinear homotopy noEvent），或 builtin_call 在 `!bf` 之前按名接的（:26097-26236：String activeState actualStream cardinality cat cross delay der fill getInstanceName identity
    inStream initialState linspace max min ndims ones outerProduct product scalar size skew sum ticksInState timeInState transition transpose zeros），或 model_array_builtin（:25342 起：cat cross fill matrix max min
    ndims ones outerProduct product skew sum symmetric zeros），或 sync_call（:25961 起：Clock backSample firstTick hold interval noClock previous sample shiftSample subSample superSample）；
    另加 array / time / Boolean（omc 特名）。实现者以 grep 这几处为准写进注释；**`print` 不在其中**（函数语境 `!bf` 即 SKIP；模型语境走 P）。
  - 多段名：首段 `Connections`，或 `OpenModelica.Scripting.uriToFilename`（call() :22238）→ 真；其余假（builtin_call 对限定名报 "qualified function call" SKIP）。
- 覆盖：XA06（兄弟叫 noEvent）、C10 `R68eC10`（sin）、XF05 / XF06（sin）、RVC03（replaceable sin，只调 sin）→ A 判定点命中 → B0 字节；XB30-XB36 / XB41（根类 abs / sin / max / sqrt 带根类记录形参）、
  RVG01-04、A3G01（外层类里的改名 import）→ B 判定点命中 → B0 字节；A3G05 → G 判定点命中 → B0 字节；A3C01 / A3C02 / A3G04（C10 在前、别的放开点在后）→ blocked_ → SKIP；
  RVC01（g 先放开、replaceable sin 在后）、RVX01 / RVX02（放开后 h 体里的兄弟 abs）→ 放开后 SKIP。

### 6.4 G1：放开模型里函数形参 / 局部深处的可能不纯调用 → SKIP（XE08 收窄，§15-3）

- 问题：omc 对形参 / 局部深遍历标 impure（O11），ymc 只看绑定顶层（TBD-19 ③）。放开把底线 SKIP 的模型打开，就露出来：XE08（E 的局部）、A3X20（别的顶层函数）、A3X33（A3 改名的 h）、
  RVI01（局部 `P.R r`，字段缺省调 `P.ti`）、RVI03（`R r(a = 1 + ti(x))`）、RVI04（多段被调 `P.ti`）、RVJ02（ymc 把常量实参调用折掉、omc 留调用），final2 都 FAIL。
- 写法（只读，同 G0）：新函数 `scan_deep_impure()`，run() 里紧跟 G0（:359，check_enclosing_refs 之前），条件同 G0：`released_68e_ && !result_.unsupported && !(lenient_ && need_second_pass_)`。
  对 `enc_fns_` 里每条定义链（首层 = 函数 E）的每个组件（形参 / 局部）：
  - (a) **修饰里的全部式子**——绑定里不是整个绑定式本身的调用，加上属性修饰与记录字段修饰（含嵌套子修饰，`collect_mod_calls` 递归；RVI03）。
  - (b) 整个绑定式是调用而旧口径因"Real 目标 + 首输出 Integer"不算的（同原卡条件 7(b)）。
  - (c) 维式子（attr.dims、type.dims）。
  - (d) **类型**（v2）：Real / Integer / Boolean / String 与枚举 → 不看；其余解析成类（下面"类型走查"），落到长定义 record → 递归看它每个字段的 (a)(c)(d)（字段绑定**整个**也算，
    omc 递归进类树；字段是记录继续递归；记录带 extends → 坏）；落到短类 → 看它的修饰与维（derived_mod / derived_attr），再沿 derived_type 往下（≤ 20 层），落到内建标量停；下不去 → 坏。
  每个调用判"坏"（被调名的解析都用**该调用所在类的定义链**：E 的组件用 E 的链，记录字段用记录的链）：
  - 被调名是 size / array / String / Integer / Boolean / time → 不坏（omc 特名，O2；评审第 9 条），不走查。
  - 其余单段名：print_kind 式走查（:11520-11567 的四步，把 `print` 换成被调名、返回找到的元素）：找到函数类 → `maybe_impure_fn`（:11669）；找到记录类 → 不坏；找到别的元素 / Unknown
    （`.*` 导入、extends / 短类层、encapsulated 层）→ 坏；走完各层顶层 program 也没有 → 内建：名字是 O11 的顶层 impure（initial terminal homotopy sample DynamicSelect print firstTick interval
    ticksInState timeInState）→ 坏，否则不坏。改名 / 限定 import 命中 → 按 import 目标从顶层逐段下降（同下面多段名）。
  - 多段名：首段同上走查（找到类元素才往下），之后**只沿 SCode 本地类元素逐段下降**（不经继承 / import / Instantiator），落到函数类 → `maybe_impure_fn`；下不去 → 坏（RVI04）。
  - 任一坏 → `set_unsupported("deep impure call in a function of a released model")`，子原因记函数名。
- **类型走查**（(d) 用）：类型名首段沿该组件所在类的定义链找——每层先本地类元素，再改名 / 限定 import（命中按 import 目标从顶层下降）；`.*` 导入层且未命中 → 坏；**extends / 短类层对类型名透明**
  （不当 Unknown：MSL 函数多 `extends Modelica.Icons.Function`，GenerationOfFMUs 的 enc_fns_ 有 Modelica.Math.sin，形参 `input Modelica.Units.SI.Angle u`，评审第 4 条；"继承来的类与外层同名类撞名"记残差）；
  encapsulated 层之后只看顶层；走完看顶层 program。多段类型名之后各段只沿本地类元素下降，下不去 → 坏。`Modelica.Units.SI.Angle` → 顶层 Modelica → Units → SI → Angle（短类 `Real(final quantity = "Angle", …)`，
  修饰全是字面量）→ 不坏。
- 不调 resolve_fn / find_scode_element_at / inherited_member_at / collect_fn_sig，不碰 Instantiator、不改状态。偏保守（A3C04：q 体里调 sin → maybe_impure → SKIP，final2 PASS），只少 PASS。
- 不影响：10 个目标的函数没有深处调用、类型是内建标量或根类本地记录（字段缺省字面量）；快照 255 个放开模型里深处调用的被调者（A68eIN52 的 f、VA03-VA09 的 g、RR16 的 g、IN47 的 String、IN48 的 size）
  都判不坏（`a3scan.tsv`）。operator record 构造（RVJ01）不进 enc_fns_（note_fn_body :16786 跳过 ctor_name），final2 PASS 不变。
- 原卡条件 7（sibling_calls_visible）不改；G1 是它的放开后全模型版本，覆盖 XE08 本身。

### 6.5 A2 撤掉

删 :11496-11501 的直接调用（A2 作为共用路径改动不再存在）；Y 支只在 §6.2 流程里、放开后用。不置 released_68e_ 的口径不变。

### 6.6 插入点一览与为什么不碰别的支

| 点 | 改什么 | 为什么只在这里 |
|---|---|---|
| fn_flat_name :11496-11504 | A2 调用换成 §6.2 流程 | 底线在此已决定返回类路径名；之前各支（嵌套函数 / chain / 根 / fn_home_ / ds 有根 / inh_tainted）返回的名字或 nullopt 一律不碰 |
| P / A / G / B 入口 | `!blocked_ && (raw_pass_ == 0 \|\| released_68e_)` | 放开点本身（原卡 v6 已批准位置） |
| A / G / B 判定点（:15776 / :15371 / :14626） | FB 判定（不论放不放） | 判定点入口；回落在 call() 里发生，但只在这三处判，不改 call()（§15-2） |
| emit_function :14877-14879 | raw_pass_ 计数 | 推测性定型遍的边界，只记深度 |
| run() :359 | G1 紧跟 G0 | 同 G0：只在放开模型、最后一遍 |

不碰：call()、chain 支（模型语境的实例名本来就对）、`ds[0] == fn_home_` 支（放开点 A 的地盘）、inh_tainted / pkg_scope 支（继承 / 包实例，留 65b）、rec_flat_name、fn_marked_impure、resolve_fn。

### 6.7 判定外的写法如何保持

- B0 退出 0 的模型：从不放开（§6.1）→ A3 只置 blocked_、FB 只置 blocked_（无作用）、G1 不跑 → 与 B0 逐字节相同（含 C10 族、A2 修过的、XE01b、RV1Q05b、RVG01-04、A3G01 / A3G05）。
- B0 SKIP 的模型：A3 形态 / FB 回落在放开前 → blocked_ → 停在原放开点（与 B0 同原因 SKIP）；N → 与 final2 相同；放开后 X / Y 改名、U SKIP、FB SKIP、G1 SKIP。

### 6.8 输入 → 输出、不变量、数据结构

- 新成员：`bool blocked_`、`int raw_pass_`；新函数：`a3_classify`（Y 复用 inherited_sibling_name）、`fb_name`、`fb_hit(fr)`（G / B 超集 + import 别名扫，import 别名表可在第一次用时建一次）、
  `scan_deep_impure`；print_kind 的走查抽成 `lexical_find(name, chain, mode)`（mode：被调名 / 类型名；print_kind 改调它，行为不变）。不改 struct / enum 字段、FnCtxGuard、emit_function / fn_flat_name 签名、call()。
- 断言：A3 Rename 名以模型名开头；`released_68e_` 为假时 A3 不得返回与底线不同的名字（YMC_ASSERT）；raw_pass_ 进出配对。

## 7. 规则核对

### 7.1 (a) 预测清单（新二进制 N，v2 重算）

**对 B0**：B0 退出 0 的模型 **0 个变化**（全部探针 + 三语料）；B0 退 66 的变化只许 PASS / BOTH-REJECT / SKIP（原卡 v6 已列的 SKIP→PASS 保留）。没有 FAIL→PASS / FAIL→SKIP / AI→两拒（C10 已撤）。
三语料对 B0：只有 10 个目标 SKIP→PASS（scodeinst 0、testsuite 9、MSL 1）。`a3corpus.py` 在 MSL omc 缓存里命中 6 个模型（Fluid / Media 的 Medium 包实例里嵌套函数 f_nonlinear 调外层包实例函数），
都是 B0 PASS、走嵌套函数 / 包实例支，不进类路径支、也不放开 → 不变；testsuite Result 段 0 命中。

**对 final2**（全部列名，清单外 0）：

A. B0 退 0、final2 改过 → 回到 B0 字节：
- A2 撤掉（35）：a2 A2E03 A2E06 A2E08 A2G01 A2G02 A2I01 A2L01 A2L02 A2M01 A2N01 A2N02 A2P01 A2P02 A2Q01 A2Q03 A2S03 A2V01 A2X01；a2b A2B01 A2B02 A2B07 A2B10 A2B15 A2B20；
  a2ra A68eIN51 A68eIN56 A68eIN59 A68eZ54；pk-p54 A68eIN51 A68eIN56 A68eIN59；pk-p54d A68eZ54；r13 RVA2F1 RVA2W1；v1 R68eV01（final2 PASS → B0 FAIL）。
- 上面在 final2 的 pk-p54r 里的副本（27）：R68eV01 A2V01 A2M01 A2S03 A2X01 A2N01 A2N02 A2P01 A2P02 A2L01 A2L02 A2I01 A2Q01 A2Q03 A2G01 A2G02 A2E03 A2E06 A2E08 A2B01 A2B02 A2B07 A2B10 A2B15 A2B20 RVA2F1 RVA2W1。
- adv 里的 A2 新形态（29）：adv_a2 XC01-XC08、XC10-XC13、XC17-XC28、XC30（PASS → B0 FAIL）；adv_x XF01-XF04（FAIL′ → B0 FAIL）。
- C10 / 同名回落（19）：p4 R68eC10、pk-p54r R68eC10、adv_a XA06、adv_b2 XB30 XB31 XB32 XB34、adv_c XB41、rv_g RVG01 RVG02 RVG03 RVG04、a3g A3G01 A3G05（PASS → B0 FAIL）；
  adv_b2 XB33、adv_x XF06（SKIP → B0 FAIL）；adv_b3 XB35 XB36、adv_x XF05（FAIL′ → B0 FAIL）。

B. B0 SKIP：
- final2 FAIL → PASS（名单 25，加 §17 实测 PASS 的待核 3 个共 28；核对评审订正标题数，与 omc 逐字节）：XE01 族 8；a3x A3X02 A3X12 A3X13 A3X15 A3X21 A3X26 A3X27 A3X29 A3X31 A3X32 A3X34；a3c A3C05 A3C07；rv_x RVX04 RVX05 RVX06 RVX08（评审推断 X 改名对）。
- final2 FAIL → SKIP（21；核对评审订正标题数）：adv_x XE08（G1）；a3x A3X01（h 先于 g，blocked_）、A3X22（f2 先于 f，blocked_）、A3X05（replaceable，U）、A3X07（DUP，U）、A3X14（根基类里 h→k：k 走 A 的 3' 不放、k 非 FB 名 → 回落 SKIP）、
  A3X16（`Q.k`，U）、A3X17（数组，U）、A3X20 / A3X33（G1）；a3y A3Y03（B 有记录，Y 的 C4 不过 → U）；a3g A3G04（C10 在前 → blocked_）；
  rv_x RVX01 RVX02（放开后兄弟 abs → FB SKIP）；rv_c RVC01（放开后 replaceable sin → FB SKIP）；rv_i RVI01 RVI03 RVI04（G1）；rv_j RVJ02（G1）；rv_l RVL01 RVL02（X0 不成立带实例 → U）。
- final2 PASS → SKIP（7）：pk-p54 A68eIN58、a2 A2E10、pk-p54r A2E10（Y 形放开前命名，blocked_）；oldhit A68eIN58（同）；a3c A3C01 A3C02（C10 → blocked_）、A3C04（G1 偏保守）。
- 待实现核（2 选 1 都合规）：a3x A3X09、rv_k RVK03（短类 `S2 = S` / `S3 = S` 的组件）→ PASS（ymc 实例节点 def = S）或 SKIP（def = 短类 → X3 不过 → U）；
  rv_x RVX07 → FAIL 记 KNOWN（继承来的 E，N）或 PASS（S 层带实例 → X）。
- 不变：a3x A3X03（SKIP）、A3X04 A3X06 A3X10 A3X30（PASS）、A3X11（两拒）；a3y A3Y01 A3Y02 A3Y04 A3Y05（PASS，Y 放开后改名 = final2 的 A2 名）；a3c A3C03（PASS）、A3C06（SKIP）；
  a3g A3G02（B0 = final2，FAIL 退 0：短类别名 sin 在 G 之前就回落）、A3G03（PASS）；rv_x RVX03（PASS）；rv_c RVC02（SKIP）、RVC03（B0 = final2，FAIL 退 0）；rv_i RVI02（SKIP）；rv_j RVJ01（PASS）、RVJ03（SKIP）；
  rv_k RVK01 RVK02（SKIP）；adv XE01g XE11（SKIP）、XE13 XE14 XE15（PASS）、XE01b（B0 FAIL）、XA14 XA34（PASS，N）；其余全部探针与 final2 逐字节相同。

A2 系列逐个落点见 `$A/a2land.tsv`（a2 42、a2b 19、a2ra 53、r13 5；B0 退 0 的一律与 B0 同，B0 SKIP 的同 final2，唯一例外 A2E10 → SKIP；r13 的 RVA2H1 / RVA2K1 / RVA2E1 SKIP 不变）。

### 7.2 (b)

- 共用路径只动 fn_flat_name 类路径支（用户已批准 A2 / A3）；放开点 / 判定点入口（原卡已批准位置）；emit_function 的推测性定型遍只加深度计数（放开点门控用，不判别的）；run() 里 G1 与 G0 同位、只在放开模型跑。
- **不改 call()**（§15-2）；不改 rec_flat_name、fn_marked_impure、resolve_fn、find_scode_element_at、Instantiator。

### 7.3 (c) 与停损

- 本卡之后的核对评审若再找出变种（本桶引入的 FAIL / AI / CRASH）：**一律在放开点 / 判定点入口或 A3 判定里收窄成 U / SKIP，留给 65b**，不再加照 omc 补齐的分支（用户 11-13 止损）。
- B0 PASS → 非 PASS：撤造成它的改动；B0 老错：KNOWN + open-questions；继承来的 E 里的外层实例调用（RVX07）、放开模型里与判定点无关的回落暴露：KNOWN（§15-4）。收口：对抗评审一轮 + 核对评审一轮。

## 8. 不做

- 名字先于放开落定的 A3 / A2 形态改成 PASS（要重跑压平，§15-1）。
- X 的 U 形：重名 / 派生层顶替（A3X07，65b DUP）、包内包与非实例的嵌套类（A3X16 `Q.k`）、组件数组（A3X17）、inner / outer、replaceable 函数（A3X05）、三段以上名字、重声明组件 / 类在路径上、
  X0 不成立的实例层（RVL01 / 02）。
- 根基类里 h 再调 S 的兄弟（A3X14：A 的条件 3' 只认组件实例，65b）。
- 继承来的 E 里的外层实例调用（RVX07，KNOWN）。
- G1 之外的纯度：语句里的调用（两边都浅）；operator record 构造函数（不进 enc_fns_）。
- 放开模型里与判定点无关的"用户函数发射失败回落内建"（§15-4，KNOWN）。
- 记录 / 类型按实例命名（rec_flat_name，65b / 65d）。

## 9. 越界清单（主代理已按规则定，见 §15；保留原列以便追溯）

1. 重跑让 IN58 类也 PASS（G-b，改 flatten() 驱动）→ §15-1 不做。
2. call() 帧栈 → §15-2 不加；改用 G / B 超集 + import 别名（v2）。
3. G1 范围 → §15-3 扫全部 enc_fns_（v2 补记录字段修饰与多段记录类型）。
4. 放开模型里别处的回落暴露 → §15-4 KNOWN。
5. 需要 ymc 没有的实例树数据才能算准的形态 → §15-5 一律 U → 放开后 SKIP：DUP / 派生层顶替（A3X07）、包实例内的函数互调（A3X16）、数组实例路径（A3X17）、根基类实例上的兄弟调用（A3X14）、
   decl_scope 里的实例层（RVL01 / 02）。

## 10. 验收（与 OMC 对比，§7.4 第 2 步 a–e）

`B` = `$S/redo/ymc_base_5c7.exe`，`F2` = `$S/redo/68e/ymc_final2.exe`，`N` = 新二进制；omc 用 PATH 上 1.25.0、240 s、并发 ≤ 2；ymc 并发 ≤ 4。
开工前先把 final2 的 `tests/probes/pk-p54r.probes` 拷成 `$A/pk-p54r.final2.probes`（差分用旧版，入库后再重算）。

- **a. 目标逐字节**：
  - `python -I -X utf8 $S/redo/68e/run1.py --ymc N --tag .a3 <9 个 testsuite 目标>` 全部"逐字节一致"；`python tools/msl_sweep.py --ymc N --jobs 1 --timeout 240 --only Modelica.Mechanics.Rotational.Examples.GenerationOfFMUs` PASS 1。
  - `for s in adv_x adv_x2 adv_x3; do python -I -X utf8 $A/run3a.py $S/redo/68e-adv/$s.probes --new N --tag a3; done`：XE01 / XE01c / XE01d / XE01e / XE01f / XE01h / XE12 / XE16 PASS，XE08 SKIP，
    XE01g / XE11 SKIP，XE13 / XE14 / XE15 PASS，XE01b 与 B0 同（a3.out == base.out）。
  - `python -I -X utf8 $A/run3a.py $A/a3x.probes --no-omc --new N --tag a3`（a3y、a3c、a3g 同）与
    `for s in g x c i j k l; do python -I -X utf8 $A/run3a.py $A/review/rv_$s.probes --new N --tag a3; done`（omc 输出在 `$A/review/pr/` 已有，可拷进 `$A/pr/rv_*` 后加 `--no-omc`）：逐个等于 §7.1 的预期。
- **b. 机械差分**（`python -I -X utf8 $S/pks68f/diffbin.py --a <基> --b N --jobs 2 <包…>`，包 = runall.sh 的清单（worktree `tests/probes/*.probes` 但 pk-p54r 用 `$A/pk-p54r.final2.probes`、
  W1 pk-p54 / pk-p54[a-g]、rv6 pk-p54h、redo/68e 的 p1-p11 / gf1 / rv_r1-r4 / rv_r8-r12）+ `$S/redo/68e/a2.probes a2b.probes a2ra.probes oldhit.probes impl/v1.probes impl/n1.probes impl/n2.probes`
  + `$S/redo/68e-review/r13.probes` + `$S/redo/68e-adv/*.probes` + `$A/a3x.probes a3y.probes a3c.probes a3g.probes` + `$A/review/rv_*.probes`）：
  - 基 = B：只许 B 退 66 的模型变（且新判定 PASS / BOTH-REJECT / SKIP）；**B 退 0 的变化数 = 0**（重点看 C10 族、rv_g、a3g A3G01 / A3G05、adv_b2 / b3 / c、a2 / a2b / r13 / v1）。
  - 基 = F2：变化**恰好**是 §7.1"对 final2"A + B 两节列的模型（A 节逐个 N 输出 == B 输出；B 节逐个对 omc 核），清单外 0；"待实现核"三个按实测归入 PASS / SKIP / KNOWN。
  - 三语料：`bash $S/rv5/corpus.sh m68eA3 N`：对 B0 changed 只有 10 个目标；`python $S/vdiff.py $S/rv5/{sw,ts,msl}_m68erA2.json $S/rv5/{sw,ts,msl}_m68eA3.json`（对 final2）changed 0。
- **c. 全部探针 0 FAIL / AI / CRASH**（KNOWN 除外）：`python -I -X utf8 tools/probe_recheck.py` 加 a3x / a3y / a3c / a3g、rv_*、adv 全部、a2 / a2b / a2ra / r13 / v1 / oldhit / p1-p11 / gf1 / rv_r*；
  B0 退 0 的老错（A2 / C10 / XE01b / RV1Q05b / RVG / A3G01 / A3G02 / A3G05 等）不入库、不算本桶（与 B0 同输出）；RVX07 若 FAIL 进 KNOWN.txt（须有 open-questions 条目）。
- **d. 门**：CLAUDE.md 四条（slice + `--oracle-live`、全 scodeinst、gcc-debug / msvc-debug / msvc-asan 零警告、ASan 0）。
- **e. 评审结果处理**：按 §7.3；任何清单外差异先查根因——若是 B0 退 0 的变化 → 门控 R 有漏洞（最可能是 FB 名单 / 超集漏名、raw 遍、或回落的消费者不止 call()），修门控而不是放过；
  A2 / A3 的预期变化与 omc 不一致 → 对应形态改 U。
- 单测（期望值只来自 omc 实跑输出 `$A/pr/…/omc.out`、`$A/review/pr/…/omc.out` 与 `$S/redo/68e-adv/pr/…/omc.out`）：见 §12。
- 文档：todo 68e 节（§15 已由主代理补）；open-questions TBD-19 ④ 恢复（A2 撤掉，列 §7.1 A 的 A2 老错、IN58 / A2E10 的入口 SKIP、RVX07）、③ 改 G1 口径（放开模型全部函数、含记录字段）、
  新增 C10 族老错条目（含 RVG / A3G01 / A3G05 的改名 import 与 derivative 形）；KNOWN.txt 删 A2 的 3 行（A68eIN70 / IN73 / A2R04 是 B0 同输出的老错，按"老错不入库"从 pk-p54r 去掉）；progress.md 记账。

## 11. 三个最大风险与哪条验收抓

1. **门控有洞，底线退 0 的模型被改**（FB 名单 / G-B 超集漏名——如别的改名途径；raw 遍以外的推测性定型；不经 call() 的失败消费者）→ 违反 (a)。抓：b 的"基 = B，B 退 0 变化 = 0"（全部探针 + 三语料），
   重点 C10 族、rv_g、a3g、adv_b2 / b3 / c、a2 / a2b / r13 / v1。
2. **X 的实例结构与 omc 不同**（短类实例 A3X09 / RVK03、重声明 / 类修饰过的外层、outer、多层基类）或 N 的层标记不如实（RVX07 类）→ 放开模型里出新 FAIL。抓：c（0 FAIL，KNOWN 只许 RVX07）+ a 的
   a3x / rv_x / adv 逐个预期；不一致按 §7.3 改 U。
3. **收窄过头伤到目标或已有 PASS**（G1 的类型走查把 GenerationOfFMUs 求值期解析的 MSL 函数判坏；A 判定点不论放不放的 FB 在放开模型里多挡；blocked_ 被剪掉的函数里看不见的 A3 形态触发）。
   抓：a（10 目标 + msl_sweep）+ b 的"基 = F2 清单外 0"（PASS→SKIP 只许 §7.1 列的 7 个）。

## 12. 单测计划（flatten_test.cpp 新 `flatten_68e_redo_a3`，替换 `flatten_68e_redo_a2`）

- 等值（omc 输出原样）：XE01、XE12、XE01h、XE16、XE01d、A3Y01（Y 放开后）、A3X21（放开后再命名的 h）、A3C05、RVX04；护栏 A2B06（import，B0 PASS）、A3X04（S 是包，N）、RVX03（N）。
- `<unsupported:` 开头：A68eIN58、A3X01（blocked_）、A3C01 / A3G04（C10 → blocked_）、RVC01 / RVX01（放开后 FB）、XE08 / A3X20 / RVI01 / RVI03（G1）、A3X07（DUP → U）、A3X17（数组 → U）、RVL01（X0 不成立 → U）。
- 删掉 a2 单测里 V01 / A2L01 / A2G01 / RVA2F1 的等值（回到 B0 FAIL，期望不能取 omc）；IN58 改成 `<unsupported:`。

## 13. 新探针集（追加到 tests/probes/pk-p54r.probes，只收 PASS / 两拒 / SKIP）

- 追加：`A3X*`（a3x 27 个，A3X09 按实测）、`A3Y*`（5）、`A3C*`（7）、`A3G03` `A3G04`；评审 rv_* 里 PASS / SKIP 的（RVX01-06、RVX08、RVC01、RVC02、RVI01-04、RVJ01-03、RVK01-03、RVL01、RVL02；RVK03 按实测）；
  adv 的 XE01 / XE01c / XE01d / XE01e / XE01f / XE01h / XE12 / XE16（PASS）、XE08 / XE01g / XE11（SKIP）、XE13 / XE14 / XE15（PASS）。
  不入库：B0 同输出的老错（RVG01-04、RVC03、A3G01、A3G02、A3G05）；RVX07 FAIL 则进 KNOWN、PASS 则入库。
- 去掉：final2 时追加的 A2 条目里回到 B0 FAIL 的 27 个（§7.1 A 第二条）、R68eC10、KNOWN 3 个（A68eIN70 / IN73 / A2R04）；A2E10 保留（SKIP）。
- 前缀说明写进 tests/probes/README.md 的 pk-p54r 行（"68e A3：实例外层函数名、门控、C10 撤回、G1"）。

## 14. 待查（不猜）

1. A3X09 / RVK03（短类 `S2 = S` 的组件）在 ymc 里 s 的类实例节点 def 是长类 S 还是短类——决定 PASS 还是 U（都合规），实现后实跑定。
2. RVX07：继承来的 f 的定义链（ext_bases_at 记忆化）里 S 层带不带实例——决定 X（PASS）还是 N（FAIL，KNOWN）。
3. GenerationOfFMUs 求值期解析到的 MSL 函数（enc_fns_）在 v2 的 G1（类型走查对 extends 透明）下是否全判不坏——实现后 msl_sweep 实跑（验收 a）。
4. 除 call() 外是否还有"发射失败 → 退 0"的消费者——评审核过 emit_function 的 7 个调用方（:5443 / :13901 / :15380 / :15561 / :15900 / :23235 / :24738），软失败被吞的只剩 call() 回落链与
   fn_value_target → 外层调用帧；靠验收 b 的"B 退 0 变化 = 0"兜底。
5. omc 对数组外层 `S s[2]` 打 `A3X17.s.h`（不带下标），ymc 数组组件的实例节点是一份还是逐元素未核实 → 本卡算 U。

## 方案评审

- 第 1 轮（2026-11-13，独立评审，`$A/review/REPORT.md`）：有条件通过。必须先改 4 条（FB 超集漏 import 改名、X0 不成立带实例 → U、A / G / B 不论放不放判 FB、G1 记录字段修饰与多段记录类型）+ 同步改 5 条
  （raw typing 门控与吞 SKIP 点、N 论述与 RVX07 KNOWN、todo 同步、rv_* 与门控探针进验收、size 特判 / 行号）→ v2 全改（见卷首 v2 注）。核对评审待填。

## 调试 / C++ 要点 / 完成定义

- 新 SKIP 文案：`A3: instance-scope function name not computed`、`68e: builtin-named user function in a released model`、`deep impure call in a function of a released model`。
- `blocked_` / `released_68e_` / `raw_pass_` 只在一遍 Flattener 内有效；raw_pass_ 用 RAII（:14877-14879 之间有提前 return 的可能要核）；A3 / G1 / fb_hit 不许调 find_scode_element_at / inherited_member_at /
  resolve_fn / resolve_class_path / collect_fn_sig（评审时 grep）；`ScopeLevel::operator==` 只比 cls，X1 显式比 inst；不跨 inst_exp / add 持 arena 引用；import 别名表只读 SCode（absyn_to_scode 已把组导入拆单条）。
- 完成定义：验收 a–e + ASan + 方案评审与对抗 / 核对评审通过 + todo / progress / open-questions / KNOWN / probes README 更新 + lessons。

## 15. 主代理定（2026-11-13，按 todo 规则定 §9 越界清单；以本节为准，覆盖 §6 / §9 冲突处；第 2、3 条按第 3 步评审更新）

1. 重跑压平（G-b）：**不做**。IN58 / A2E10 / A3X01 / A3X22 在入口 SKIP（B0 本来就 SKIP，合规则 (a)）。
2. call() 帧栈：**不加**（规则 (b)：共用函数不为本桶改）。C10 判定改在 A / G / B 三个判定点、**不论本来放不放都判**（评审第 3 条）：A 用调用名本身；G / B 用"被发射函数元素名 ∪ 别名链头 ∪
   fn_emit_names_ 全部末段 ∪ 整程序里目标末段等于这些函数元素名的改名 / 限定 import 的有效别名"做 FB 超集（评审第 1 条，RVG01-04）。命中且不是 omc 特名 → 放开前 blocked_、放开后 SKIP。
   §6.3 已相应改写；多挡掉的 G / B 放开点只会变 SKIP（B0 本来 SKIP）。10 个目标必须仍 PASS，否则停下报主代理。
3. G1 范围：**扫全部 enc_fns_**。位置含修饰里的全部式子（属性与记录字段修饰、嵌套）、维、记录类型的字段（单段 / 多段类型名都按 SCode 本地类逐段下降，落到 record 递归字段、落到内建标量 / 短类看它的修饰，
   下不去才算坏；类型名首段走查对 extends 层透明）；被调名 size / array / String / Integer / Boolean / time 不走查（评审第 4、9 条）。A3X20 / A3X33 / RVI01 / RVI03 / RVJ02 是本桶放开后才出的 SKIP→FAIL，
   按规则 (c) 第一条在入口收窄；open-questions TBD-19 ③ 的说法随之改。
4. 放开模型里别处的回落暴露、继承来的函数里的外层实例调用（RVX07）：**登记 KNOWN**（todo「老路线补齐范围：同一模型里无关的底线老错仍 KNOWN」）。
5. 缺实例树数据的形态：**一律 U → 放开后 SKIP**，留给 65b（用户 11-13 决定 1A 的止损）。

## 16. v2.1（2026-11-13，第 3 步快速核对：v2 不通过只差 1 条，改完不再评）
- X0 不成立判 U 改窄为"函数体语境且 ds 本身有实例层"（§6.2）。原条文"ds 或传入 scope"会让模型语境走到类路径支的名字一律 U、置 blocked_，挡掉 final2 已 PASS 的放开模型与在包里的目标型模型。
- 采纳建议：raw 遍在 released_68e_ 已为真时照常放（§6.1）。
- 核对评审确认：评审 1-4 全部收住（RVG01-04 / A3G01、RVL01 / 02、RVC01 / RVX01 / RVX02 / A3G04、RVI01 / 03 / 04 落点与 §7.1 一致）；10 个目标与 B0 退 0 零变化的论证成立。§7.1 预测清单不变。

## 17. 实现订正（第 4 步，2026-11-13；只记与上文不同或上文没写清的，结论以实测为准）

1. **§7.1 预测漏列 1 个 PASS→SKIP**：adv_a6 **XA62**（H 的兄弟函数叫 `sample`，f 体里先调 `sample(x)` 再调 g）。判定点 A 的 FB 命中 `sample`（非特名）→ 放开前置 blocked_ → 不放，
   照底线回落内建 sample（实参个数不符 → SKIP "function arity"），之后 g 的放开点也关着。输出与 B0 逐字节相同（B0 本来 SKIP，合规则 (a)）；final2 曾在 A 放开 sample 得 PASS。
   这是 §6.3"FB 名单超集即可、多拒只少放"的直接结果，没有加卡外逻辑去救（要看内建能否接住实参才知道底线会不会退 0，属卡外）。§7.1"对 final2 · PASS→SKIP"应为 8 个。
2. **§7.1 待实现核三个都 PASS**：a3x A3X09、rv_k RVK03（短类 `S2 = S` 的组件：ymc 的类实例节点 def 是长类 S，X3 过）、rv_x **RVX07**（继承来的 f 的定义链里 S 层带实例 → 走 X 得
   `RVX07.s.h`，与 omc 逐字节）。RVX07 因此入库 pk-p54r，**不进 KNOWN**（§10 c、§13 里"RVX07 FAIL 则进 KNOWN"不触发）。
3. **fb_name 的多段名两条用不上**：判定点 A 只收单段调用名，G / B 的候选是元素名 / 别名链头 / 发射名末段 / import 别名，全是单段；`Connections.*` 与 `uriToFilename` 由 call() 在回落之前
   按书写名接走（:22238 一带），不进判定点。代码里只实现单段名单，注释写明。
4. **G1 的三处实现取向（都偏保守、都照 omc foldExpParameter 的"递归进组件的类树"）**：
   - (d) 类型落到**任何长定义类**都递归看它的组件（记录、函数类型形参的类型等；omc 对所有组件的类树都 foldComponents），带 extends 即坏；落到枚举 / 内建标量不坏；
   - E 自己有 extends（基函数）：按 type 模式解析基函数、照同样规则看它的组件（omc 的类树含继承来的组件），解析不了即坏；MSL 的 `extends Modelica.Icons.Function` 解析到空的 partial 函数、不坏；
   - (b) 整个绑定式就是调用：旧口径 fn_marked_impure 只认用户函数，被调的是**顶层 impure 内建**时也算坏（同原卡条件 7(b) 的用户函数情形之外再多挡一类）。
   10 个目标、GenerationOfFMUs 不受影响（msl_sweep PASS 1）；放开模型里因 G1 而 SKIP 的只有 §7.1 列的 7 个（XE08 / A3X20 / RVI01 / RVI03 / RVI04 / RVJ02 / A3C04）。
5. **A3X33 在 A3 判定那一阶段就已 SKIP**（不等 G1）：h 改名后 home = S，h 体里的 ti 走 `ds[0] == fn_home_` → 判定点 A → sibling_fn_name 条件 7（sibling_calls_visible）挡 → 回落 SKIP。
   终态与 §7.1 相同（SKIP）。
6. 实现细节（不改语义）：raw_pass_ 用 ScopedSet 计数（RAII，进出配对）；X3 补"实例类树有效"的防御判断；print_kind 的四步抽成 lexical_find（callee / type 两模式），print_kind 行为不变。

实测结果（终版 `$S/redo/68e/ymc_final3.exe`：stage_A3c 之后只做了不改语义的源码整理再重建，全部 20766 个探针与 stage_A3c 逐字节相同）：全部探针 20766 个对 B0：B0 退 0 的变化 0、B0 退 66 而 FAIL / AI / CRASH 0；对 final2 变 167 = §7.1 A 节 110 + B 节 56 + XA62。

## 18. 核对评审（2026-11-13，第 4 步最后一轮）：通过
- 四个放开点门控、FB 判定、a3_classify、G1 位置都对；对 B0 退 0 变化 0、B0 非 0 而有输出的 635 个实跑 omc 全 PASS；对 final2 的 167 个与 §7.1 + §17 逐个相同（清单外 0）。
- §7.1 两处标题数订正（FAIL → PASS 名单 25、FAIL → SKIP 21）。
- 边界探针 137 个入库为 pk-p54r-ver（模型名前缀 R68eV，PASS 57 / 两拒 13 / SKIP 58）；与底线同输出的老错 9 个不入库，记 open-questions：VF20a / 21a / 23a / 30a / 31a、VR06 归 C10 族那条，VG07-09 归纯度那条 ③。
