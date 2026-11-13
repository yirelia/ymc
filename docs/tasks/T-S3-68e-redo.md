# T-S3-68e-redo 函数调用的压平名与模型语境的内建 print（重做）

> 第 1、2 步产出（§7.4）。修订：v1 首版 → v2 用户四条决定 → v3 评审"不通过"后重写 → v3 复评"有条件通过"、剩余问题全在纯度部分 →
> **v4：用户 11-11 决定不批准试发射兜底、放弃 AlgorithmNoRetCall，纯度改动全部撤出 68e（todo 已改：68e 不改任何共用纯度路径）** →
> **v5：v4 快速核对的三处小改（G0 认 `.print`、G0 传递扫描与位置、sibling_fn_name 条件 7）+ oprec 漏洞注明** →
> **v6：v5 快速核对不通过（encapsulated 里的 print、oprec 起点漏洞、LookupSnapshot 复原不了 Instantiator）→ 三个新判定全部改成只读：print_kind 三值、G0 改成全程序静态扫描、sibling_fn_name 条件 5 / 7 改读实例树 / SCode；删掉 LookupSnapshot**。
> 备份：`.v1.md` / `.v2.md` / `.v3.md` / `.v4.md` / `.v5.md`（v3 的 Q 节、深度 0 入口表与两轮评审意见留给纯度桶复用，见附录）。
> `$S` = `C:/Users/yangrui/AppData/Local/Temp/claude/C--dev-ymc/4e49f17e-0607-4a2d-8c06-17ac54b3dead/scratchpad`。
> **底线 B = `$S/redo/ymc_base_5c7.exe`**（HEAD 5c7cd5b，src/flat、src/inst 与 9bd9f02 相同，ymc 行号照 9bd9f02 的 `C:/dev/ymc/src/flat/flatten.cpp`）。OMC 只认 `v1.25.0`（副本 `$S/redo/68e/src_*.mo`）。

## 目标（10 个，定死）

| # | 用例 | 底线 SKIP 原因 | 放开点 |
|---|---|---|---|
| 1-4 | testsuite simulation/modelica/asserts/AssertTest4.mo / 5 / 6 / 7 | `function: print [flattened function name]` | A（实例类里函数体调兄弟函数） |
| 5 | testsuite simulation/modelica/types/IntegerTest.mo | `function: print` | P（模型语境的内建 print） |
| 6 | testsuite simulation/modelica/algorithms_functions/FunctionTupleRecord.mo | `function: f4 [function variable type (enumeration / derived / unresolved)]` | B |
| 7 | testsuite simulation/modelica/external_functions/RecordMemberArguments.mo | `function: foo [function variable type …]` | B |
| 8 | testsuite simulation/modelica/records/Ticket5134.mo | `function: f [function variable type …]` | B |
| 9 | testsuite simulation/modelica/types/ExtRecordMemberArguments.mo | `function: foo [function variable type …]` | B |
| 10 | MSL Modelica.Mechanics.Rotational.Examples.GenerationOfFMUs | `function: position [function referenced by derivative / inverse annotation]` | G（derivative 注解引兄弟函数，与 A 共用条件） |

- AssertTest4-7 的 `print` 是实例类里声明的 external 用户函数，不是内建。AlgorithmNoRetCall（函数体里的内建 `.print` 要让外层函数后标 impure）已移出本桶、归纯度桶，68e 里保持底线 SKIP。
- 口径：1-9 按 `tools/nf_live_sweep.py --all`（去 `-d=-newInst` 注解）与实跑 omc 1.25.0 比 stdout（期望 `$S/redo/68e/run/<名>/<名>.omc`；AssertTest4-7 的文件 Result 是旧版本，以实跑为准）；
  10 按 `tools/msl_sweep.py`（omc 读主检出 `build/msl-oracle` 缓存）。都不进 `s3-easy-slice.json`。

## 来源

- OMC v1.25.0：NFModelicaBuiltin、NFCall、NFFunction、NFFunctionDerivative、NFFunctionInverse、NFLookup、NFInstNode、NFClassTree、NFInst、NFType、NFTyping、NFSimplifyModel、NFSimplifyExp、NFFlatten。
- 探针（omc + B + 旧 68e 终版三方，`python -I -X utf8 $S/redo/68e/runp.py <集> --old`，输出 `$S/redo/68e/pr/<集>/<模型>/`）：
  与本桶放开点有关的：p1（print 30）、p2（A 族 30）、p3（B 族 28）、p4（补充 14）、p5（次序 / 类别 10）、p7（实例类带 extends / derivative 8）、gf1（derivative 10）、rv_r3（评审：派生类顶替 8）、rv_r4（评审：print 识别 15）、
  rv_r8（评审：G0 / 条件 7，RV8G1-G5）、rv_r9（评审：G0 传递，RV9G6）、rv_r10（RVAO0-2：encapsulated operator 'constructor' 里裸 print）、rv_r11（RVAD0-2：'constructor' 里 `.print`）、
  rv_r12（RVEP1-4：encapsulated 类 / 函数里的 print）——全部用 B 重跑核实（r8 / r9 B SKIP；RVAO0 B ACCEPTS-INVALID 是底线老错；RVAO1 / RVAO2 / RVEP1 / RVEP2 / RVEP4 B 两拒；RVAD0 B FAIL 底线老错；其余 B SKIP）；
  只作"保持底线输出"回归的（纯度相关，68e 不动）：p6（26）、p8（8）、p9（17）、p10（7）、p11（5）、rv_r1（16）、rv_r2（7）。
- 回归床：旧 pk-p54..54h 589 个（`oldscan.tsv`；落在放开点上的 230 个 `oldhit.probes` / `oldhit.out`）。
- MSL GenerationOfFMUs：9bd9f02 / 旧 68e SKIP，旧 68f 终版（在 :11487 改名）PASS。

## OMC 处理逻辑（第 1 步结论）

### P 内建 print（模型语境）

- P1 声明 NFModelicaBuiltin.mo:804-808：`impure function print input String str; external "builtin"; …`，顶层作用域，一个 String 输入、无输出。
- P2 查名：NFCall.instNormalCall :2286-2347（只按首段名特判 size :2321 / array :2325）→ Function.instFunction（:2342）→ lookupFunction :318-345 → NFLookup.lookupFunctionName :193-215 →
  lookupCref :304-344 → lookupSimpleCref :838-908（先 lookupSimpleBuiltinCref :855 / :815-836，再逐层 lookupLocalSimpleCref :910-938、parentScope :898）到顶层才找到内建 → scopeList 为空 → 裸名 `print`。
  词法上先找到别的同名元素就用它：组件 / 包 / 类 → 报错（P17、C01、X26-X37、RV4P04 / P06）；枚举 → 转换（P30）；迭代变量叫 print → 内部错误（Y44）。
- P3 不打函数块：NFFunction.new :282-299（内建 `status := COLLECTED` :296）。
- P4 定型：NFTyping :3358-3362 / :3043-3047 → typeNormalCall（NFCall.mo:274）：类型错（P12）、个数错（P13 / P14）、当值用（P11）报错；命名实参照收（P15）；String 数组向量化（P16）。
- P5 化简：NFSimplifyModel :223-233 / :367-377 化简后仍是调用就留；NFSimplifyExp.simplifyCall :166-219 实参照常化简（P29 `"ab"`）、impure 内建不 Ceval（P03 常量实参照留）。
  方程段、算法段、初值算法、when / elsewhen / if 体、for 语句里都照打（P02 / P18-P22 / D06 / C14 / RV4P07 / P09 / P12 / P14）。
- P6 实参里调了"体里有内建 print 的函数"：omc 把那个函数后标 impure（NFFunction.mo:1645-1682），化简不折（NFSimplifyExp :204-206），照留调用（RV4P10 `print(fs("a"))`、带 `impure function fs`）——
  ymc 不认这种后标（纯度桶），见方案的 G0。

### A 实例类里函数体调兄弟函数（AssertTest4-7）

- A1 NFClassTree.instantiate :428-660：实例为空时 `instance := clsNode`（:472-475）；非类型类的本地类 clone 后 `setParent(instance, c)`（:521-523）——本地类（含从基类拷来的）的父节点都是组件实例。
- A2 体里查名：f 自己不中 → parentScope（:898）= 组件 → COMPONENT_NODE 支在组件的整棵实例类树里 Class.lookupElement（:926）。
- A3 lookupFunction :343 `prefix := fromNodeList(scopeList(found_scope, includeRoot = true))` → `Test4.assertTest.print`；f 自己 `Test4.assertTest.f` 底线已打对。
- A4 函数缓存按节点（instFunctionRef :362-389）：两实例各一份（A03 / A28）、数组不带下标（NFComponentRef.fromNodeList :1534-1541；A04 / E08）。
- A5 external 未声明 pure 即 impure；f 顶层调了它 → `impure function Test4.assertTest.f`——ymc 既有 fn_marked_impure 已能算（它用 resolve_fn 解析到 external 的兄弟），不需要新纯度逻辑。
- A6 名字特例：size / array（NFCall.mo:2321 / :2325）、time / Boolean / Integer / String（NFLookup.mo:815-836、:855）不按词法调用户函数（A15 / A16）；其余内建名被兄弟遮蔽时调用户的（C10 `R68eC10.c.sin`）。
- A7 因 A1 / A2，基类里的 f 调 g 时在组件实例的类树里查，派生类（组件类）自己的 g 优先：RV3A1（3u）、RV3A6（7u）、C08；derivative 同理 RV3G1（3dx）。实例类有无关 extends 照常（E01 / E04）。encapsulated 的 f 查不到外层（D04）。
- A8 实例上改过的常量按实例值求（A11）。

### G derivative 注解（GenerationOfFMUs）

- G1 NFFunctionDerivative.instDerivatives :68-82，`scope := InstNode.parent(fnNode)`（:77）→ instDerivativeMod :240-280 的 `Function.instFunction(acref, scope, …)`（:257）→ 同 A3。E04 / E07 / E08 / GF01 / GF03-GF05；
  GenerationOfFMUs 的 `…move_w.position_der` / `…move.position_der2`。
- G2 MSL Move：`model Move extends …PartialElementaryOneFlangeAndSupport2`；`function position extends Modelica.Icons.Function; … derivative(noDerivative = q_qd_qdd) = position_der`；方程 `phi = position(u, time)` 写在 Move 本身。
- G3 inverse：NFFunctionInverse.mo:182 在函数节点自己的作用域定型（E06 / GF08），不放。

### B 根类里的记录作函数变量类型

- B1 lookupClassName :76-88 → lookupSimpleName :434-492；B2 Type.toDAE（NFType.mo:1134-1135）→ toFullDAEType :1857-1887（:1879 fullPath）→ `Root.R`；
  B3 NFInst.instRecordConstructor :2616-2639（:2633）、NFFlatten.collectTypeFuncs :2621-2661。B4 变体见 p3 / p5（限定、import、根类 extends、实例类里的记录 `R.c.R`、根类外函数报错）。

## ymc 现状（底线 B）

- 点 P：builtin_call `!bf`（:25638-25642）；内建表 :84-130 无 print；路线 noret_call :21588-21625 → inst_exp :19661-19667 → call() :21626 → try_emit_user_call（:15147 清 noret_call_）→ builtin_call :21782。
- 点 A：try_emit_user_call :15186（fn_flat_name :11487 nullopt；:15186 只记子原因，call() 回落 builtin_call，A15 / A16 碰巧 PASS、C10 FAIL）。
- 点 G：emit_function :14322-14327 → emit_referenced_function :14776-14792（:14787-14789），在引用者的 FnCtxGuard 里跑。
- 点 B：emit_function :14058（rec_flat_name :13470-13499，唯一调用点 :14035，根类 nullopt :13477-13478）。
- 实例节点：组件类实例 `instantiate(base, comp_node, …)`（inst.cpp:2217，instance_parent = 组件）；基类实例 `instantiate(base_def, inst_node, …)`（inst.cpp:1533，instance_parent = 派生类实例）；根 :2589（父为空）。
- 收尾检查的先例：check_enclosing_refs（:16194-16205）在 run() 末尾（:357）扫实例树与用到的函数，开头 `if (result_.unsupported) return;`、:16198-16201 会清空并重设 SKIP 状态——G0 放在它前面一行。

## 对照的 lessons

2026-11-11 五条（返工反复：新代码只挂 SKIP 点之后、不动共用纯度路径；检查挂一个入口：fn_flat_name 5 个调用方只放 :15186 / :14789 两处、共用 sibling_fn_name；import 定义链：单段名、实例层逐层相同、无 import；
碰巧对的点名：A15 / A16 名字排除、C10 FAIL→PASS 列名；自测 0 FAIL 不算：独立对抗评审）；2026-11-04"标志位的名字不等于它的语义"（语句顶层用专门的 `noret_stmt_cref_`）；
2026-11-03"内建性质看声明"（print 不进 lookup_builtin）、"拒绝条件写在调用点"、"判 SKIP 后返回占位"、"定义作用域链不等于书写路径"、"查名探针带次序变体"。

## 方案

### 共用小函数 / 新成员

> v6 原则：三个新判定（print_kind、sibling_fn_name、G0）**全部只读**——只看 SCode 元素表和 Inst 已建好的实例树，**不调** find_scode_element_at / inherited_member_at /
> resolve_fn / resolve_class_path，所以不会 set_unsupported、不碰 inh_bases_ / enc_fns_，也不会经 pkg_instance（:16310）/ class_def_node（:16341）/ class_extends_base（:16381）
> 调到 Instantiator（它的 mark_unsupported 是粘的、没有复原接口，inst.h:140-151，两遍压平共用 instr_ :33378-33389）。v5 的 LookupSnapshot 整个删掉。

- `print_kind(e, chain, allow_dot)` → {Builtin, NotBuiltin, Unknown}（v6，替代 v5 的 is_builtin_print_call；纯语法、只读）：e 是 cref 单段 `print`、无下标的调用（否则 NotBuiltin）。
  - 无前导点：沿 `chain`（内→外的 SCode 类元素；P 用 `scode_scope_of(enclosing)`，G0 用该函数的 SCode 词法外层链）逐层：
    1. 该层本地元素（类 / 组件）有叫 print 的 → NotBuiltin；
    2. 该层 import：别名 == print、限定 import 末段 == print、`{…, print, …}` → NotBuiltin；`.*` 通配 → Unknown（不解析被导入的包）；
    3. 该层有 extends 子句 → Unknown（继承来的元素不解析）；
    4. 该层是 encapsulated → Unknown（omc 在 encapsulated 层直接跳顶层、要求 `InstNode.isBuiltin`，NFLookup.mo:885-888 / :876-878、NFInstNode.mo:1889-1897——print 是
       NFModelicaBuiltin 里的普通函数，不满足 → "Function print not found"，RVEP1 / RVEP2 / RVEP4、RVAO1 / RVAO2）；
    5. 走完各层：顶层 program 有叫 print 的元素 → NotBuiltin，否则 Builtin。
  - 有前导点（只在 `allow_dot = true` 时认，否则 NotBuiltin）：顶层 program 有叫 print 的元素 → NotBuiltin，否则 Builtin（CREF_FULLYQUALIFIED 从顶层查，NFLookup.mo:335-336；RV8G3、RVAD1）。
  - 用法：P 只在 **Builtin** 时放开（Unknown 一律不放，保持底线 SKIP）；G0 在 **Builtin 或 Unknown** 时算命中（偏保守，SKIP）。
- `sibling_fn_name(fr, name)`（A / G 共用；v6 把条件 5 / 7 改成只读）：E = `fn_emit_names_.back()`、H = `fn_home_`：
  1. `fn_scope_ && H.is_valid()`、`fn_emit_names_` 非空；2. name ∉ {size, array, time, Boolean, Integer, String}；
  3. `E.first == (*fn_scope_)[0].cls.raw`、`fn_scope_->size() >= 2`、`(*fn_scope_)[1].cls == H`、`(*fn_scope_)[1].inst` 有效且 `nodes[inst].def == H`、`E.second.size() >= 2` 且末段 == E 名；
  3'. `nodes[nodes[inst].instance_parent].kind == NodeKind::Component`（H 那层是组件自己的类实例，inst.cpp:2217；排除基类实例 inst.cpp:1533 与根；RV3A1 / A4 / A6 / A7 / G1、C08、A22 不放）；
  4. `fr.fe == fr.named`、`fr.alias_mods.empty()`、`!fr.node.is_valid()`、`fr.fscope.size() == fn_scope_->size()`、`fr.fscope[1..]` 与 `(*fn_scope_)[1..]` 逐层 cls 与 inst 都同（`ScopeLevel::operator==` 只比 cls，显式比 inst）、
     `fr.fe` ∈ H 的 `def.elements`、非 replaceable、Function / ExternalFunction 长定义；
  5. **（v6 只读）不重复**：取 `inst = (*fn_scope_)[1].inst`（组件的类实例），沿它的基类实例（`I_.trees[I_.node_class(inst).tree].exts`，递归）逐个看各自类树的 `lookup` 表，
     都没有 `name`、也没有 E 的名字（H 的各层基类不声明同名元素，避开 A7 / DUP 的实例树取舍）；只读 Inst 已建好的树，不调 inherited_member_at；
  6. H、被调函数不在 `I_.redeclared_classes`；H、E 不含 import；
  7. **（v6 只读，RV8G2）E 的形参 / 局部里没有"旧口径看不到的、点名不纯兄弟的位置"**：对 E 自己的 SCode 组件元素（`def.elements` 里 kind == Component 的输入 / 输出 / 局部；
     不调 collect_fn_sig。E 若有 extends 指向 H 的本地类（单段名且等于 H 的某个本地类元素名）→ 直接不放；指向别处的基函数在词法上看不到 H 的兄弟，不用看），下列位置出现的单段调用名，
     若等于 H 里某个**本地函数元素**（兄弟，含本次被调者；按名字在 H 的 `def.elements` 里比，不做查名、偏保守不管 E 层是否遮蔽）的名字，且该兄弟**可能不纯**——
     声明 `impure`；或带 external 段而未声明 `pure`；或未声明 `pure` 且它的算法段 / 形参局部绑定里出现任何调用（可能被旧口径后标，不去求 fn_marked_impure）——→ 不放：
     (a) 绑定式里**不是整个绑定式本身**的调用（嵌在运算、实参、if 式子、数组、括号里，如 RV8G2 `Real t = sib(x) + 1`）；
     (b) 整个绑定式就是调用、但旧口径因"Real 目标 + 被调首输出是 Integer"而不算的（:13845；判 Integer 首输出只看兄弟输出的书写类型名）；
     (c) 维式子（`attr.dims`、`type.dims`）里的任何调用；
     (d) 类型名是单段、且等于 H 的某个本地 record 元素名的形参 / 局部：那个记录类各字段绑定里的任何调用（H 外声明的记录类在词法上看不到 H 的兄弟，不用看）。
     依据：omc 对形参 / 局部是深遍历（foldExpParameter NFFunction.mo:2230-2257：Binding.foldExp NFBinding.mo:683-697、Dimension.foldExp NFDimension.mo:637-654、递归进分量类树），
     既有 fn_marked_impure 对绑定只看顶层（:13877 `top(binding, …, true)`）、不看维和记录字段——照 omc 补齐要改纯度路径（用户已排除），所以在放开点入口收窄。
     语句一侧两边都是浅的（NFStatement.foldExp :727-808 与 fn_marked_impure 的 `stmts`），不需要收窄。
  返回 `E.second` 去末段 + {name}。
  - **与底线相比多做了什么**：条件 1-7 只读 SCode 元素表和 Inst 实例树，不查名、不求值、不碰 Instantiator、不改任何状态；
    条件 4 用的 `fr` 是底线 :15175-15186 / :14779 已经解析好的。所以放开点上"放不放"的判定本身不会改变任何底线有输出的用例；不放时一切与底线相同（SKIP，或 C10 式回落内建）。
- 新成员：`Id<Cref> noret_stmt_cref_`；`bool released_68e_`（本次压平是否走过 P / A / G / B 任一放开，G0 用）。

### 放开点 P：模型语境的内建 print（IntegerTest）

位置 builtin_call `!bf`（:25639）之前；noret_call 进 inst_exp 前记 `noret_stmt_cref_ = I_.absyn.exps[ce].cref`、返回后清（只传数据）。条件（全部满足，否则照旧 `function: print` SKIP）：
1. `!fn_scope_`（**只在模型语境**；函数体里的 print 保持底线 SKIP）；`!as_builtin.is_valid()`、`fn.parts.size() == 1`、无前导点（`.print` 保持 SKIP，P04 / RV4P11）、`print_kind(e, scode_scope_of(enclosing), false) == Builtin`（Unknown——遇 extends / `.*` / encapsulated 层——一律不放，RVEP1 / RVEP2 两拒、RVEP3 `.print` 保持 SKIP）；
2. `e.cref == noret_stmt_cref_`（`x := print(…)`、`g(print(…))`、`print(print(…))` 内层不中）；
3. 迭代变量容器（`loop_vars_`、`stmt_iters_`、`stmt_iter_labels_`、`loop_labels_`、`citers_`、`var_iters_`，实现者 grep 补齐）里没有叫 print 的（for 里的 print 照收 P20；`for print in` 仍 SKIP）；
4. 恰一个位置实参（命名实参在 :25633 已 SKIP），`inst_exp` 后是标量 String。
输出 `Expr{Call, {print}, {实参}, Type{}}`（同无输出用户调用 :15557），noret_call 随后 simplify_expr（:21618）、仍是 Call 就留（:21620）；置 `released_68e_`。依据 P2 / P3 / P5。

### 放开点 A（AssertTest4-7）与 G（GenerationOfFMUs）

- A：try_emit_user_call :15186，仅 fn_flat_name 支（`!cfn.cls.is_valid() && fr->ctor_name.empty() && !fr->node.is_valid()`）、单段无前导点、`sibling_fn_name` 有值 → qname = 它、home = fn_home_、置 `released_68e_`。
- G：emit_referenced_function :14787-14789 qname 为空之后；单段无前导点、`sibling_fn_name` 有值、且该名是当前函数类注解 / external 注解里某个 derivative 修饰的绑定（不认 inverse）→ qname = 它、home = fn_home_、置 `released_68e_`。
- 依据 A1-A4 / G1；条件 3' 落实 A7。GenerationOfFMUs：position / position_der 直接写在组件类 Move 里、Move 的基类不声明同名、position 的 `extends Icons.Function` 不影响；旧 68f 终版同一行改名即 PASS，推断无下一层 SKIP，以实跑为准。

### 放开点 B（四个记录目标）

同 v3：emit_function :14058 之前；单段类型名；E 直接在根类、根类长定义无 extends / import，E 无 import / extends；记录在根类层本地、普通 record 长定义非 replaceable 非 partial、只含组件；
字段内建标量、变率 Var、维字面量或 `:`、只许标量字面量绑定。rpath = `result_.model.name + {R}`，rs 同 rec_flat_name；置 `released_68e_`。依据 B1-B3。
- 副作用说明：B 唯一的查名是条件 3 的 `resolve_class_path(c.type.path, c.type.leading_dot, fr.fscope, ds)`——底线在这个点已经用完全相同的参数做过两次
  （rec_flat_name :13474、operator record 别名支 :14043），B 再做一次是重复同一查名，不走新的 Instantiator 路径；实现时可把别名支的 `ac` / `ads` 提到块外直接复用。其余条件只读 SCode 元素表。

### G0：放开过的模型、程序里有"内建 print 调用"的函数 → SKIP（v6：全程序只读静态扫描）

- 问题：68e 不再认"内建 print 让函数后标 impure"（omc NFFunction.mo:1645-1682 + 化简不折 impure，P6）。底线里这类函数一被发射就 SKIP（函数体 print 不支持），但被常量折叠掉时底线照样出结果
  （与 omc 不同，底线老错，如 p1 P09 / P10）。四个放开点会把原本 SKIP 的模型打开，若这个模型里另有这种折叠，就成了放开后的新 FAIL（RV4P10、RV8G1 / G3-G5、RV9G6、RVAD1）。
- v4 / v5 沿 resolve_fn 追传递闭包，两轮被找出漏洞：追踪覆盖不到的路径（operator record 'constructor' 经 oprec_ctor :16461 提前返回、不进 note_fn_body，RVAD1 / RVAO1），
  追踪本身有副作用（resolve_fn → resolve_class_path 经 pkg_instance :16310 / class_def_node :16341 / class_extends_base :16381 调 Instantiator，mark_unsupported 是粘的、没有复原接口，inst.h:140-151）。
- **v6 写法（更保守，与"放弃纯度后收窄"的用户决定一致）**：新函数 `scan_program_print()`——
  - 范围：**已加载程序里的全部函数类元素**（用户文件 + 已装载的库；从 `I_.scode.program` 的顶层类起递归遍历各类的 `def.elements`，碰到 restriction 为 Function / ExternalFunction /
    OperatorFunction 的类元素就扫——含 operator record / operator 里的 'constructor' 与运算符函数、嵌在模型 / 包 / 函数里的函数、encapsulated 的、`redeclare function extends` 的；短类定义没有体，跳过）；
    遍历时同时维护该函数的 **SCode 词法外层链**（内→外的类元素），供 print_kind 用。
  - 每个函数扫：算法段全部语句（任意深度，含 if / for / while / when 的各分支——没走到的分支也扫）里的所有式子；external 段的实参；本函数各组件元素（输入 / 输出 / 局部）的绑定式、维式与修饰里的式子；
    嵌在本函数里的类（局部记录等）各组件的绑定式。每个调用节点（语句或式子）都过一次 `print_kind(调用, 外层链, allow_dot = true)`：**Builtin 或 Unknown → 命中**。
  - 命中 → `set_unsupported("builtin print in a function of a released model")`（主原因），子原因记函数名。
  - 不调 resolve_fn / find_scode_element_at / inherited_member_at，不碰 Instantiator、inh_bases_、enc_fns_，不改任何状态（只读 SCode）；不需要快照。
  - 何时跑：`released_68e_` 为真、此前 `!result_.unsupported`、且**是最后一遍**——`!(lenient_ && need_second_pass_)`（两遍压平的第一遍若要第二遍，第一遍不跑，:33376-33389）。
  - **位置**：run() 里 `check_enclosing_refs();`（:357）**之前**一行（prune_functions 与 deferred_fns_ 循环之后）。check_enclosing_refs 开头 `if (result_.unsupported) return;`，
    G0 先判的 SKIP 不会被它在 :16198-16201 清掉；G0 不改其它状态，check_enclosing_refs 的扫描不受影响。不能插在 check_enclosing_refs 中间。
- 偏保守的代价：程序里**任何**函数（包括这个模型根本不用的）有内建 print（或 print_kind 判不清的 Unknown），放开过的模型就 SKIP——只少 PASS、不出错。
- 不动底线输出：没走过放开点的模型不扫；走过放开点的模型在底线上都停在放开点（SKIP）——唯一例外是 A / G / B 失败后 call() 回落成内建的情形（函数与 ymc 认识的内建同名，如 C10 族，底线有输出），
  那里若程序里另有内建 print 函数会从 FAIL 变 SKIP，出现逐个报。
- 覆盖面（B 上全 SKIP，omc 都把相关函数打 impure、留调用或报错）：RV4P10、RV8G1、RV8G3（`.print`）、RV8G4、RV8G5、RV9G6、RVAD1（'constructor' 里 `.print`）→ SKIP；
  RVAO1 / RVAO2（encapsulated operator 'constructor' 里裸 print，omc 报 "Function print not found"）、RVEP4（encapsulated 函数）→ Unknown 命中 → 两拒；RVAO0 / RVAD0（没有模型级 print、不放开）照底线。
- **10 个目标都不受影响**（逐个核过源码）：
  - IntegerTest：没有函数；
  - AssertTest4-7：两个函数 `print`（external，无算法段）与 `f`；f 体里唯一一处 `print(…)`（文件第 18 / 18 / 19 / 19 行）沿外层链 [f, AssertTest] 在 AssertTest 层碰到本地元素 `print` → NotBuiltin；
  - FunctionTupleRecord（f4）、RecordMemberArguments / ExtRecordMemberArguments（foo，external）、Ticket5134（f，external）：函数里没有 print 调用（Include 字符串里的 printf 不是调用）；
  - GenerationOfFMUs（装载 Modelica 4.0.0 + Complex 4.0.0 + ModelicaServices 4.0.0）：脚本 `$S/redo/68e/mslprint.py` 扫全部源码（结果 `mslprint.tsv`）——MSL 里不带限定的 `print(` 共 81 处、`.print(` 0 处，
    分布在 Math/Nonlinear.mo 50、Math/FastFourierTransform.mo 13、Mechanics/MultiBody/Visualizers/Colors/colorMapToSvg.mo 9、Math/package.mo 5、Utilities/Examples.mo 2、Media/package.mo 1、
    Electrical/QuasiStatic/Polyphase/Basic/MultiDelta.mo 1；逐处核对，每处所在的最内层函数 / 模型里都有 `import Modelica.Utilities.Streams.print;`（Nonlinear 的四个例子函数第 10 / 89 / 162 / 238 行，
    Media 的 Inverse_sine 第 2917 行，其余 import 在同一函数 / 模型头之后）——print_kind 在最内层就判 NotBuiltin，不会走到外层的 extends / encapsulated。Complex、ModelicaServices 各 0 处。
- 代价：MSL 4.0.0 约 1900 个函数头、约 8.5k 条赋值语句；扫描是对已在内存的 SCode 一次线性遍历，只有 81 个 print 调用需要走外层链，估计远小于 0.1 s；
  对照：底线跑一个小 MSL 例子（Rotational.Examples.First）连装载共 4.2 s。实现后用 GenerationOfFMUs 前后各计时一次确认，msl_sweep 的超时（240 s）不受影响。
- **同一模型里无关的老错（点名，KNOWN + open-questions）**：A / G / B 放开的模型里，若某函数的形参 / 局部绑定**深处**调的是与放开无关的 impure 函数（声明 impure 的顶层函数，RV1Q05b 那类），
  omc 把它标 impure、ymc 旧口径不标——按用户口径属"同一模型里无关的底线老错"，不在本桶修；与放开有关的那种（调的是不纯的兄弟）由 sibling_fn_name 条件 7 收窄。

### 删掉纯度后 P / A / G / B 的逐处依赖检查

| 位置 | 原来（v3）是否依赖 Q 节 | v4 的处理 |
|---|---|---|
| P 在函数体里 | 依赖（要 Q-D1 给外层函数打 impure） | 撤回：只在模型语境，函数体里的 print 走底线 SKIP |
| P 的实参调了 print 函数（RV4P10） | v3 由 Q-D2 实参深查处理 | G0 → SKIP |
| A：被调兄弟函数体里有内建 print | 无（P 的函数体分支） | 发射兄弟函数时撞函数体 print → SKIP（底线行为；原来在命名处 SKIP，现在晚一步，结果同） |
| A：E 本身体里有内建 print | 无 | 同上，发射 E 时已 SKIP |
| A：兄弟是 external 用户 print，E 要打 impure | 不依赖 Q：既有 fn_marked_impure（:13832-13881，resolve_fn 解析到兄弟 → fn_impure 真；调用在语句顶层） | 不改 |
| A / G：E 的形参 / 局部绑定深处、维、记录字段缺省里调了不纯的兄弟（RV8G2） | v3 由 Q-D1 深遍历处理（且只认 print 来源，本就漏这类） | v5 sibling_fn_name 条件 7 → 不放（SKIP） |
| A / G / B：模型里别的函数绑定深处调了与放开无关的 impure 函数（RV1Q05b 类） | 无 | 同一模型里无关的老错：KNOWN + open-questions |
| A / G：模型别处折叠了 print 函数（含传递、`.print`、operator record 'constructor'：RV8G1 / G3 / G5、RV9G6、RVAD1） | v3 由 Q-D2 处理 | G0（v6 全程序只读扫描）→ SKIP |
| G：被引导数函数体里有 print | 无 | 发射时 SKIP（底线行为） |
| B：用记录类型的函数体里有 print | 无 | 发射时 SKIP；别处折叠 → G0 |
| is_builtin_print_call（v6 改名 print_kind，三值、只读） | 被 P、Q-D1、Q-D6 共用 | 只剩 P 与 G0 用；P 只认 Builtin，G0 认 Builtin / Unknown |
| `ceval_owner_` / `purity_site_` / C-S-L / 试发射兜底 / l_folded_ / print_only_impure_ / print_reached_ / fn_marked_impure_p | Q 节专用 | 全部删除（附录留给纯度桶） |

### 输入 → 输出、不变量；数据结构

- 输出新增：模型语境的 `print` Call；实例路径命名的兄弟函数 / 导数函数；根类记录作类型的函数块与构造函数。
- 后置：各分支条件不满足时走底线原来的 SKIP / 回落路径；三个新判定只读，不改任何求值 / 纯度路径。
- 断言：sibling_fn_name 的返回以 `result_.model.name` 开头、长度 ≥ 2；B 的 rec 是 Record 长类；P 的实参类型为标量 String。
- 新成员 `noret_stmt_cref_`、`released_68e_`；新函数 `print_kind`、`sibling_fn_name`、`scan_program_print`；不改 struct / enum 字段与 FnCtxGuard / emit_function / fn_flat_name / rec_flat_name 签名。

### 规则 (c) 补充：放开后的模型经过的老路线

| 路线 | 放开点 | 出过错的写法 | 处理（依据） |
|---|---|---|---|
| 查名·遮蔽 / encapsulated | P | X26-X37、Y41-Y45、C01、RV4P03 / P04 / P06、RVEP1 / RVEP2 | print_kind 只认 Builtin（遇 extends / `.*` / encapsulated 为 Unknown，不放）+ 迭代变量按名（P2；NFLookup.mo:885-888） |
| 查名·名字特例 | A / G | A15 / A16 | 排除表（A6） |
| 查名·派生类顶替兄弟函数 | A / G | RV3A1 / A6 / G1、C08 | 条件 3'（A1 / A7） |
| 查名·重复元素 | A / G | DUP 族、E02 / E03 | 条件 5——实例树取舍补不了（65b） |
| 查名·import / 类路径 / 别名 / 外层类 / 重声明 | A / G / B | F1 / F7 / F9 / IN62 / IN73 / A10 / A14 / B27-B29 | 条件 3 / 4 / 6；到不了放开点的保持底线 |
| 求值·改过的常量 | A / G | A11、IN14 / IN75 | 既有 enclosing_const_check / check_comp_fn_consts |
| 纯度·内建 print 的后标 | P / A / G / B | RV4P10、RV8G1 / G3 / G4 / G5、RV9G6、RVAD1、RVAO1、P09 / P10 式折叠 | G0 收窄（全程序只读扫描、认 `.print` 与 encapsulated）——照 omc 要改共用纯度路径，用户已排除，归纯度桶 |
| 纯度·形参 / 局部深处调不纯兄弟 | A / G | RV8G2 | sibling_fn_name 条件 7 收窄（omc 深遍历 NFBinding.mo:683-697，ymc 只看顶层 :13877） |
| 纯度·形参 / 局部深处调无关 impure 函数 | A / G / B 放开的模型 | RV1Q05b | 同一模型里无关的老错：KNOWN + open-questions |
| 发射·实参匹配 | A / P | IN18 / X09 / X20 / X60、P11-P14 | 既有 fillArgs；P 条件 2 / 4 |
| 记录构造 | B | RR07 / VR01、X21 | 只收内建标量字段、根类无 extends |

补不了、单列：重复元素的实例树取舍（65b）；实例类里的记录作类型（65b / 65d）；inverse 注解路线；内建 print 的后标纯度与形参 / 局部深遍历（纯度桶）。

## 仍需用户定

无。（G0 与 sibling_fn_name 条件 7 是撤出纯度改动后补的入口收窄，挂在放开点之后 / 只在走过放开点的模型上判，不改共用路径。）

## 不做

函数体里的 print（含 AlgorithmNoRetCall）、`.print`、限定名 print、命名实参、String 数组实参、`for print in`；A / G 的 DUP、基类实例层、外层 / 嵌套类、包里函数、import / 类路径、别名、replaceable、重声明、inverse、
函数名当值（:14969）、名字特例；B 的条件外写法与实例类里的记录；任何纯度 / 求值路径的改动。与底线输出相同的老错一律 KNOWN + open-questions。

## 验收（与 OMC 对比，§7.4 第 2 步 a–e）

`B` = `$S/redo/ymc_base_5c7.exe`，`N` = 新二进制，`P` = `$S/redo/68e`；omc PATH 上 1.25.0、240 s；omc / ymc 串行。
- a. `python -I -X utf8 $P/run1.py --ymc $N --tag .new <9 个 testsuite 目标>` 全部"逐字节一致"；`python tools/msl_sweep.py --ymc $N --jobs 1 --timeout 240 --only Modelica.Mechanics.Rotational.Examples.GenerationOfFMUs` PASS 1。
- b. 机械差分：`python -I -X utf8 $S/pks68f/diffbin.py --a $B --b $N --jobs 2 C:/dev/ymc/tests/probes/*.probes <旧 pk-p54..54g> $S/rv6/68e/pk-p54h.probes $P/p1..p11.probes $P/gf1.probes $P/rv_r1..r4.probes $P/rv_r8..r12.probes <新集>`
  与 `bash $S/rv5/corpus.sh m68er $N`。期望：
  - B 退 0 的模型 stdout 与退出码逐字节不变；~~**例外清单只有 C10 族**（实例类里的兄弟函数与 ymc 认识的内建同名、底线回落成内建而 FAIL，放开点 A 照 A3 / A6 改成 PASS），逐个对 omc 核、列名~~；
    **（2026-11-13 撤：用户决定 2 选 B，C10 族与同名回落在判定点入口撤回到底线行为，例外清单为空；见补充卡 `T-S3-68e-redo-A3.md` §6.3。）**
  - **必须保持底线输出（逐字节）**：p6、p8、p9、p10、p11、rv_r1、rv_r2 全部模型，p1 的 P05-P10、P23-P26（函数体 print / 纯度相关，PASS 的保持 PASS、FAIL 的保持 FAIL）——68e 不动纯度，这些一个都不该变；
  - **必须保持 PASS**（与放开点有关的底线 PASS）：p2 A08、A15、A16、A30；p3 B09；gf1 GF02、GF06、GF10；rv_r4 RV4P01、P02、P05；
  - B 退 66 → N 退 0 的须与 omc 逐字节一致（预期：IntegerTest、AssertTest4-7、四个记录目标、p1 / p4 / p5 / rv_r4 的模型语境 print、p2 / p4 / p5 / p7 / gf1 / rv_r3 的兄弟调用 / 导数、p3 / p5 的根类记录）。
- c. `python -I -X utf8 tools/probe_recheck.py`（加新集、oldhit、p1-p11、gf1、rv_r1-r4、rv_r8-r12）FAIL / AI / CRASH = 0（KNOWN 除外，RVAO0 是底线就有的 ACCEPTS-INVALID，登记 KNOWN）；oldhit 里 omc 报错的 40 个仍两拒；
  rv_r3 的 RV3A1 / A6 / G1 不许 FAIL（应 SKIP）；**rv_r8 / rv_r9 全部 SKIP**（RV8G1 / G3 / G4 / G5、RV9G6 → G0；RV8G2 → 条件 7 不放，停在原来的 "flattened function name"）；
  **rv_r10-r12**：RVEP1 / RVEP2 / RVEP4、RVAO1 / RVAO2 两拒（P 不放 / G0 命中）；RVEP3、RVAD1、RVAD2 SKIP；RVAO0 / RVAD0 不放开、保持底线输出。
- d. CLAUDE.md 四条（slice + `--oracle-live`、全 scodeinst、gcc-debug / msvc-debug / msvc-asan 零警告、ASan 0）。
- e. 规则 (c)：本桶引入的错先查根因，漏考虑的老路线照 OMC 补齐（回第 2 步、过第 3 步），补不了的入口收窄 SKIP 并报用户；B PASS → 非 PASS 撤改动；与底线同输出的老错 KNOWN；对抗一轮 + 核对一轮收口。
- 附（可选）：新 PASS 里带 print 调用的模型过一次后端 `python -I tools/bdae_sweep.py --cases <cases.json> --ymc $N`，确认不 CRASH。

## 单测计划（flatten_test.cpp 末尾 `flatten_68e_redo`，期望值从 `$P/pr/…/omc.out` 原样拷）

- 等值：P01（模型算法 print）、P02（方程段 print 带换行）、P03（常量实参照留）、A01、A03（两实例）、A04（数组实例）、E04（derivative 链）、B01、B02。
- `<unsupported:` 开头：P11、P12、P17、RVEP1（omc 拒）、P05（函数体 print，保持 SKIP）、RV4P10、RV8G3、RV9G6、RVAD1、RVEP4（G0）、RV8G2（条件 7）、RV3A1、RV3G1（派生类顶替，SKIP）。
- 护栏等值：A16（用户 String，保持回落内建，与 omc 同）。

## 新探针集计划

- 自检集 `tests/probes/pk-p54r.probes`（只收 PASS / 两拒 / SKIP；与底线同输出的老错不入库）：p1-p5、p7、gf1、rv_r3、rv_r4 的模型，外加：
  P：继承来的组件 / 外层包常量 / import 的枚举叫 print、`for print in …`、`noEvent(print(…))`、`g(print(…))` 语句、实参调纯函数 / 调体里有 print 的函数（G0）、模型别处折叠了 print 函数（G0）；
  G0：rv_r8-r12 全部，外加 print 在函数的输入缺省 / 维 / 局部记录字段缺省 / external 实参里、在没用到的函数里（保守 SKIP）、在 operator / operator record 的运算符函数里、`.print` 而顶层有用户类 print（NotBuiltin、不 SKIP）、函数有 extends 且无 import 的裸 print（Unknown、SKIP）、`import P.*` 通配（Unknown、SKIP）；
  P：encapsulated 模型 / 嵌在包里的 encapsulated 模型（两拒）、模型有 extends 时的模型级 print（Unknown、保持 SKIP）；条件 7：不纯兄弟在绑定深处 / 维 / 记录字段 / 整式 Real-Integer 的情形，以及在语句顶层（不收窄、应 PASS）的对照；
  A / G：组件路线（`c.f(time)`）+ 改过常量、protected 兄弟、递归、三实例、实例类在根类后声明、`replaceable C c` 被重声明、基类声明同名非函数元素、derivative 带 order / zeroDerivative、
  derivative 指向基类里的函数、派生类顶替（组件类 / 基类 / 根类 / 两层继承）、兄弟函数体里有内建 print（SKIP）；
  B：package 根、记录字段数组字面量缺省（SKIP）、两函数共用记录、记录作输出的元组方程、字段带属性修饰（SKIP）。
- 只作回归、不入库：p6、p8-p11、rv_r1、rv_r2（"保持底线输出"）、旧 589、oldhit 230。

## 方案评审

- 第 1 轮（2026-11-11）：不通过（Q-D2 语境、派生类顶替、Q1 深遍历、范围）→ v3。
- 第 2 轮（复评）：有条件通过——条件 3'、入口全表、todo 同步已关；剩 4 处纯度问题（试发射复原不全 RV5T3 / RV6T4、L 类收尾误伤 RV5L1 / L2、owner 3 过宽 RV5R1a / b / RV5A1、深遍历漏混合来源 RV5D1，
  材料 `$S/redo/68e-review/r5-r7`）且试发射兜底超出批准范围 → 用户撤出全部纯度改动、放弃 AlgorithmNoRetCall → v4。
- 第 3 轮（v4 快速核对）：不通过，3 处小改 + 1 小项（r8 / r9，已用 B 重跑核实）→ v5：G0 认 `.print`（RV8G3）、G0 改成传递静态扫描并写明位置在 check_enclosing_refs 之前（RV9G6）、
  sibling_fn_name 加条件 7（RV8G2）、无关 impure 深调点名 KNOWN、oprec 'constructor' 起点漏洞注明。
- 第 4 轮（v5 快速核对）：不通过（r10-r12，已用 B 重跑核实）——P 把 encapsulated 类里的 print 当内建（RVEP1 / RVEP2 会 ACCEPTS-INVALID）、oprec 起点漏洞造出静默错（RVAD1 FAIL / RVAO1 AI）、LookupSnapshot 复原不了 Instantiator 状态 → v6：print_kind 三值只读（encapsulated 为 Unknown）、G0 改全程序只读静态扫描（只在最后一遍跑）、sibling_fn_name 条件 5 / 7 只读、删 LookupSnapshot。第 5 轮核对待填。

## 调试 / C++ 要点 / 完成定义

- 新 SKIP 文案：`builtin print in a function of a released model`（G0）。实现期临时打印用完删。
- `ScopeLevel::operator==` 只比 cls；`noret_stmt_cref_` 用 RAII；print_kind / sibling_fn_name / scan_program_print 只读 SCode 与 Inst 实例树，**不许**调 find_scode_element_at / inherited_member_at /
  resolve_fn / resolve_class_path / collect_fn_sig（评审时 grep 这几个函数名确认）；不跨 inst_exp / add 持 arena 引用；
  判 SKIP 返回 `lit_int(0)`；replaceable 看 `prefixes.is_replaceable`。
- 完成定义：验收 a–e + ASan + 方案评审与对抗 / 核对评审通过 + todo / progress / probes README 更新 + lessons。

## 待查（不猜）

1. 组件路线（component_fn）的 fscope 带不带实例节点——只影响组件路线放不放（A13 / C09 / C11），实现首个构建核。
2. GenerationOfFMUs 放开后有无下一层 SKIP：以实现后 msl_sweep 实跑为准。

## 附录：留给纯度桶（68e 不做）

- AlgorithmNoRetCall 与"内建 print 让函数后标 impure"的完整 OMC 逻辑、方案草稿（Q1-Q7、Q-D1 / D2 / D5 / D6、C / S / L 三类、深度 0 入口全表）在 `T-S3-68e-redo.v3.md`；入口表 `$S/redo/68e/sites.tsv`（`sites.py` 生成）。
- 两轮评审意见与反例：`$S/redo/68e-review/`（r1-r7、NOTES.md）；纯度探针 p6、p8-p11、rv_r1、rv_r2 及其三方输出 `$S/redo/68e/pr/`。
- 已知要先解决的：拒折后发射被调函数失败如何回滚（用户不批准在 68e 加试发射兜底）；L 类收尾不能即时判（RV5L1 / L2）；记录绑定 owner 不能整段盖（RV5R1a / b / RV5A1）；深遍历要处理混合来源（RV5D1）；
  声明 impure 的来源深遍历（RV1Q05b）、声明 impure 在 Ceval 语境（U16）、:31691 / visit_nf_ 看旧后标（todo 纯度桶已列）。

## 实现订正（第 4 步，2026-11-11 / 12）

主代理定的两处（评审建议）：
1. 条件 7(b)：目标可能是 Real（书写类型不是单段的 `Integer` / `Boolean` / `String`）、整个绑定式就是对可能不纯兄弟的调用时，兄弟首输出的书写类型不是 `Real` 就不放（派生 Integer 类型判不出，偏保守）。n1 R68eN04（Integer 首输出进 Real 局部）不放、R68eN05（Real 首输出）放开且 PASS。
2. G0 的词法外层链：Loader::load_dir 把库目录装成一棵嵌套的 Absyn（loader.cpp:331 / :366 只把顶层类放进 top），translate_program 逐顶层类翻译 → MSL 在 I_.scode.program 里是一个顶层 `Modelica`、内部按包嵌套，外层链是完整的包链（不是按 within 拆开的多个顶层项）；组导入在 SCode 里已拆成单条（absyn_to_scode.cpp:220-238）。

实现时的收窄 / 读法（都只少放、不多放；放开点外的路线不变）：
3. 条件 7(a) 补"组件修饰里属性的式子"：omc 的 foldExpParameter 递归进组件的类树，内建类型的属性组件也在里面——n1 R68eN01 `output Real y(start = g(1.0))`、R68eN02 `protected Real t(min = g(0.0))`（g 是 impure 兄弟）omc 都打 `impure function …c.f`。按规则 (c) 第一条在放开入口收窄。
4. 条件 7 的"可能不纯"另把体看不全的（短类、class extends、带 extends）未声明 pure 的兄弟算进去；(d) 记录带 extends 直接不放，递归进 H 本地记录类型的字段（含字段的维 / 修饰）。E 自己的本地记录作类型到不了放开点（rec_flat_name 在定义链上碰到 fn_home_ 给 nullopt、B 要求函数直接在根类），不另看。
5. print_kind 第 3 步：短类定义 / class extends 层的元素来自基类，与 extends 子句同样算 Unknown。
6. 放开点 G 只认"当前函数类注解 / 自身 external 段注解里 derivative 的绑定、且同名不出现在 inverse 的调用里"（derivative_only_ref）：同名函数又作 inverse 引用时不放（R68eRD05 照旧 SKIP）。
7. 放开点 B：根类是短类别名时不放（result_.model.name 取别名，函数与记录在链底长类里，命名未核实）。
8. 条件 5：组件类实例或某层基类实例没有建好的类树 → 不放。
9. 新探针集 pk-p54r（225：PASS 104 / BOTH-REJECT 26 / SKIP 95）按"只收 PASS / 两拒 / SKIP"拼，r10 RVAO0（底线 ACCEPTS-INVALID）、r11 RVAD0 与 p1 P09 / P10（底线 FAIL）是与底线同输出的老错，不入库、KNOWN.txt 不加行，记在 docs/open-questions.md TBD-19（与"rv_r8-r12 全部入库"的字面不一致，按"老错不入库"处理，报主代理定）。

10. 条件 7 的 extends 判定补"经 H 的类名走的限定路径"：卡只挡单段 `extends g`，以为"指向别处的基函数在词法上看不到 H 的兄弟"；但 `function f2 extends C.g`
   （C = H）/ `extends Pk.C.g` 指的正是 H 的本地函数，omc 经类名查到的是定义层的 C.g，继承来的体里的调用按类路径命名（`C.m` / `Pk.C.m`），
   sibling_fn_name 会给实例路径 → 旧 68e 回归床 oldhit A68eY29 / Y30 与 v1 R68eV04 / V05 从 SKIP 变 FAIL（本桶引入）。按规则 (c) 第一条在入口收窄：
   E 的 extends 路径末段是 H 的本地类名、或路径里有 H 的名字 → 不放（GenerationOfFMUs 的 `extends Modelica.Icons.Function` 不受影响）。单测加 R68eV05。

规则 (c) 第二条，停下报告、未补（交主代理走"补方案 → 评审"）：
11. oldhit A68eIN58：`C c; B b;`（C extends B，f / g 声明在 B 里），`y = c.f(time) + b.f(time) + c.g2(1)`。`b.f` 的体里调 g 经放开点 A 打成 `A68eIN58.b.g`（对）；
   `c.f`（组件路线，f 是 C 从 B 继承来的）的体里调 g 走的是**老路线** fn_flat_name 的类路径支，打成 `B.g`，omc 是 `A68eIN58.c.g`（本地类含从基类拷来的
   都 clone 进组件的实例类树、父节点是组件，NFClassTree.instantiate :472-475 / :521-523 v1.25.0；查名 NFLookup.mo:898 / :926，prefix NFFunction.mo:343）。
   只有 `c.f` 的 v1 R68eV01 底线就是这个 FAIL（同输出，老错）；A68eIN58 原先停在 `b.f` 的 SKIP，放开 A 后撞上它，按 (c) 第二条报。建议补法：组件路线（component_fn）
   发射的函数，若被调兄弟在组件类的基类里声明，照 omc 按组件实例路径命名（与 sibling_fn_name 同形，但 H 换成"声明它的基类、经组件类继承"），或先收窄成
   "fn_flat_name 的类路径支遇到 ds[0] 是 fn_home_ 的基类就 nullopt"（SKIP，与 IN58 原状一致）——后者改的是共用的 fn_flat_name，须用户批准。

待查结论：
- 待查 1：组件路线（`c.f(time)`，component_fn）的 fscope 带实例节点 → 照放开，p4 C09 / C11、n2 R68eRA12 与 omc 逐字节一致。
- 待查 2：GenerationOfFMUs 放开后无下一层 SKIP，msl_sweep PASS 1；计时与底线同量级（底线 7.5-11 s 在 SKIP 处早停，新版 7.5-9 s 全程含 G0）。

## 实现订正：补充卡 A2（2026-11-12，卡 `docs/tasks/T-S3-68e-redo-A2.md` v2，用户批准改 fn_flat_name）

- 对应上面第 11 条（A68eIN58，规则 (c) 第二条）：照 OMC 补齐，作为阶段 stage_A2。fn_flat_name 只在类路径支最后一步（`inh_tainted` 之后、拼类路径之前，
  底线本来就要返回类路径名的地方）问只读判定 `inherited_sibling_name`（C0-C8），全部满足 → 正在发射的组件路线函数的压平名去掉末段 + 调用名、home = fn_home_；
  否则照旧拼类路径。不置 released_68e_。
- 组件实例照卡用 comp_fn_uses_ 回查（路径 == E 压平名的中间段、实例的定义 == fn_home_，按不同的 cls 值恰好一个），没有改别的取法。
- 与卡逐条一致，未加卡外条件。C7 的计数遇到不是 E / 被调者的同名元素即判不放（与"恰好 1 个且就是它"等价）。
- 结果（对 ymc_final）：快照 A68eIN51 / IN56 / IN59 / Z54 与目标 A68eIN58 FAIL→PASS；新探针 a2 / a2b / r13 / v1 的变化与卡 §6 逐个相同
  （A2V01、A2M01、A2S03、A2X01、A2N01、A2N02、A2P01、A2P02、A2L01、A2L02、A2I01、A2Q01、A2Q03、A2G01、A2G02、A2E03、A2E06、A2E08、A2E10；
  A2B01、A2B02、A2B07、A2B10、A2B15、A2B20；RVA2F1、RVA2W1；R68eV01），A2E09 不变（SKIP）。三语料与机械差分见第 4 步报告。
- 入库：pk-p54r 追加 A2 的 52 个（PASS 32 / 两拒 4 / SKIP 16；A2B12 是探针笔误 `inner` 作类名，不收）与 KNOWN 3 个（A68eIN70 / IN73 / A2R04，
  replaceable 兄弟不放）；R68eV01 修好入库，从 open-questions TBD-19 的老错里划掉。单测 flatten_68e_redo_a2（IN58 / V01 / A2L01 / A2G01 / A2B06 / RVA2F1，
  期望值取自 omc 实跑）。
