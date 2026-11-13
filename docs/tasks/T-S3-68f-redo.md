# T-S3-68f-redo 修饰 / 重声明 / 类修饰 / `= break`（从底线重做）

> 状态：定稿（第六版 + 第三次快速核对的三处）。第 3 步第三次快速核对通过，可开工（§15）。2026-11-11 共修订五版：
> - 第二版：加 RICM / partial 组退路与 F1-F5；
> - 第三版：放宽规则 (a) 例外，F4 去门控，PP 族进本桶 S5；
> - 第四版：按已定规则落四件（非 partial 的 instPackage 错误 KNOWN、确定性预算、真错误只限照 OMC 移植的判定、丙-2 / 丙-4 补进：F1 改看直接点名的节点、新增 F6）；§3.4-6 引文按源码重核更正；
> - 第五版：按评审七条与用户决定（F1-F6 按类修饰触发、不分新旧）改：新增 S0 实例化序（§6.9）与 F7 类修饰预挂（删 F3）；F4 收窄；S5 补根类路径 RELAXED、沙盒三项、flatten 期进类改队列；S2 拆子阶段；S3-S5 退路加"底线 SKIP 变 FAIL / AI / CRASH"；
> - 第六版：F4 只核胜出来源（N1）；F1 施加多条类修饰的次序（N2）；F1 / F2 / F6 挪到放开之前（N3）；F7 生成 rid 时带上类修饰（N4，RW3）；队列处理在沙盒里跑（N5）；截断规则按事件判（N6）；S0 风险点名（N7）。S0 与 F7 已获用户批准（todo）；
> - 定稿：F1 并进 S1a（S1a = F7 + F5 + F1）；N4 的影响面（组件修饰路线 RW7 / RW8）列名；F7 退路连带撤 N4 对 plain 形态的拷贝。
> 底线 B0 = **重做起点提交的构建**（s1fix / Alias 提交后的那次提交；它的 src/flat、src/inst 与 9bd9f02 相同）。下文"底线"均指 B0；行号均为 9bd9f02 的 `src/flat/flatten.cpp`、`src/inst/inst.cpp`（主检出现状）。
> `$S` = `C:/Users/yangrui/AppData/Local/Temp/claude/C--dev-ymc/4e49f17e-0607-4a2d-8c06-17ac54b3dead/scratchpad`。第 1 步的全部探针、omc 输出在 `$S/redo/68f/`：`cases/` 是目标，`pout/` 是新探针，`allp/` 是旧评审探针 567 个的 omc / 底线 / 旧 68f 三方结果。
> 规则 (a)(b)(c) 见 `tasks/todo.md`「68e / 68f 收敛方案」（(c) 已按用户 2026-11-11 补充："放开后原先 SKIP 的模型出错、根因是方案漏考虑的老路线 → 照 OMC 补齐"）。
> 探针命名：不带 A68f 前缀的大写名（BR01、BV41、PR01、CMX01、CMY01、RAX01、SX01、EQ01、PX01、PP01、Q01、OR1）是本卡写的，源码在 `$S/redo/68f/<集>.probes`（br1 / br2 / pr1 / cm1 / cm2 / ra1 / sx1 / eq1 / px1 / pp1 / q1 / o1-o3）；第五版用的底线二进制是 `$S/redo/ymc_base_5c7.exe`，o1-o3、f6 与复跑的评审探针输出在 `pout5/`。RV / RVC / RVR / RW / RF / SA / SB 是第 3 步两次评审写的（`$S/redo/68f-review/rv1-rv8、rw1、rf1、s0a.probes`、`out/RVR/Lib.mo`），本卡已全部复跑核过（`pout5/`，与评审结果一致，只差 omc 报错里的路径），三方输出在 `pout/<模型>/{omc,base,f7}.out`；`A68f*` 是旧 68f 评审探针，输出在 `allp/w/<模型>/`。文中写"br1 的 BR03"，以区分两套里同号的模型。

---

## 0. 用户决定（2026-11-11）在本卡的落点

| 决定 | 落点 |
|---|---|
| GenerationOfFMUs 归 68e | 从目标删掉，68f 目标 11 个（§1）。第 1 步对它的结论移交 68e：根因 `flatten.cpp:11487`，放开点在 `emit_referenced_function` :14787-14789（见 `pout/GF*`，§19-1） |
| RICM、partial 组都做，各有预设退路 | §6.5（RICM）、§6.3（partial 组）写明改动边界；§12 b 写退路：底线有输出的用例只要变了一个，该项整项放弃、不修补 |
| 规则 (c) 补充：漏考虑的老路线照 OMC 补齐 | §6.6 逐条列出放开模型会经过的老路线，分三类：甲（放开条件已排除，走不到）、乙（本卡补齐，第五版为 F7、F1、F2、F4、F5、F6，每条带 OMC 文件:行 + 探针）、丙（补不了，交用户）。PP 族单列在 §3.5 与 §6.6 丙 |
| 底线二进制 | 写成"重做起点提交的构建" |
| Bug2727 分歧账 | §12 a / d 写明要记 `docs/open-questions.md` |
| 规则 (a) 例外放宽（第三版）：照 OMC 移植、变化后与 omc 逐字节一致的都允许（FAIL→PASS、ACCEPTS-INVALID→两边都拒），逐个列名记账 | §6.7 例外清单（定稿）：S0 / F7 / F5 / F1 / F2 / F6 的 34 个 FAIL→PASS；F4 的 12 个（A68fTAU01-04 / 06、RV48、RF4-RF9）与 PP 族 47 个 AI→两边都拒 |
| 老路线补齐范围（第三版）：只补消费本桶放开数据的路线 | §6.6 口径不变，§7 记为已定 |
| PP 族本桶做（第三版） | §3.5 OMC 逻辑读到底；§6.8 方案（实例层落点、改动边界、全局表、会变的用例、退路）；独立阶段 S5，排在最后 |
| GenerationOfFMUs 已由 68e 收走 | §19-1 交接说明保留 |
| 第四版 ①：instPackage 报的非 partial 错误 | 不进本桶，仍 KNOWN。依据：补齐范围只限消费本桶放开数据的路线，无关老错仍 KNOWN（§6.8 PP-1） |
| 第四版 ②：检查实例化设预算，超了就停，宁漏报不误报 | 接受：确定性计数、`ponytail:` 注释写上限与升级路径、上限按 MSL 实测（§6.8 PP-B）；验收加"MSL 500 PASS 与扫描耗时不变"（§12 b） |
| 第四版 ③：F4 / PP 报真错误、退出 1 | 接受，只限条件逐条对到 OMC 文件:行的判定；拿不准的仍 SKIP（§6.6 F4、§6.8 PP-E） |
| 第五版：F1-F6 按类修饰触发、不分新旧写法（用户 11-11），底线已支持的普通类修饰也走新代码；批准的改动点照 todo「用户已批准的越界改动」 | §6.6 乙开头列出；F3 删去、新增 F7（预挂）；评审第 6 条随之关闭（§15） |
| 第五版：评审七条 | 逐条落点见 §15 |
| 用户批准 S0（精确做法）与 F7（todo「用户已批准的越界改动」） | §6.9、§6.6 F7；F7 的退路按用户批准的写：先退到只对新写法预挂，仍不行整项撤 |
| 第六版：复评 N1-N7（主代理定 N3 阶段顺序、N4 补上） | 逐条落点见 §15 |
| 第四版 ④：丙-2 / 丙-4 现在补进 | omc 行为：§3.4-6（丙-2）、§3.4-9（丙-4），q1 探针 Q01-Q08；补齐：§6.6 F1（只看直接点名的节点）、F6（实例里重展开本地类克隆）；做不到时的替代见 F6 末 |

## 1. 目标（定死，11 个，不增加）

| # | 用例 | 底线 SKIP 原因 | 组 |
|---|---|---|---|
| 1 | scodeinst/BreakModifier1.mo | expression kind | B |
| 2 | scodeinst/BreakModifier2.mo | expression kind | B |
| 3 | scodeinst/BreakModifier3.mo | expression kind | B |
| 4 | scodeinst/ModClass4.mo | modifier names non-component (nested class / typo) [B] | C |
| 5 | scodeinst/ModClass6.mo | extends modifier names non-component (nested class) | C |
| 6 | redeclare/RedeclareInClassModification.mo（下称 RICM） | extends modifier names non-component (nested class) | C（单独一项，§6.5） |
| 7 | scodeinst/const15.mo | lookup into modified class [B] | C |
| 8 | scodeinst/RedeclareClass2.mo | partial type | P |
| 9 | scodeinst/RedeclareMod11.mo | lookup in partial class [PartialMedium] | P |
| 10 | scodeinst/RedeclareMod12.mo | lookup in partial class [PartialMedium] | P |
| 11 | redeclare/Bug2727.mo | redeclare form [after extends modifier] | R |

依赖核实（实跑，`$S/redo/68f/cases/`）：
- 11 个目标都先去掉 `-d=-newInst` 注解。
- 底线、旧 68e 终版 `$S/pks68e/ymc_final5.exe` 都 SKIP，原因同上。
- 旧 68f 终版 `$S/pks68f/ymc_final7.exe` 全部与 omc 1.25.0 逐字节一致。
- 结论：11 个都是 68f 自己放开的，不依赖 68e。

omc 1.25.0 对 Bug2727 的输出与 testsuite 文件 `// Result:` 不同：
- 1.25.0 丢掉内层 extends 修饰，打 `clock.offset = 0.0`、`clock.startTime = 0.0`；
- 文件写 10.0 / 20.0。
- ymc 以 1.25.0 实测为准，在 `docs/open-questions.md` 分歧账本登记。

## 2. 来源

- MLS 3.6：§7.2.2（`break`）、§7.3（redeclaration）、§7.2（类修饰）。
- OMC v1.25.0（`git -C C:/dev/OpenModelica show v1.25.0:OMCompiler/Compiler/NFFrontEnd/<文件>`），行号本次重新 grep 过，见 §3 各条。
- 语法：`OMCompiler/Parser/Modelica.g:839-844`。
- 探针（omc 实跑）：`$S/redo/68f/{br1,br2,sx1,pr1,cm1,cm2,ra1,eq1,px1}.probes`；旧评审探针在 `allp/`。

## 3. OMC 处理逻辑（第 1 步结论）

### 3.1 `= break`（B 组）

1. 语法：`= break` 只能作整个修饰值（Modelica.g:839-844）。`1 + break`、方程里的 `break`、`(break)`、`{break}` 都是语法错（sx1 SX01-SX04：omc 与底线都拒）。
2. 合并：break 在合并期是普通的 `RAW_BINDING(BREAK)`，`Binding.isBound` 为真。外层绑定盖内层（`Modifier.merge` MODIFIER/MODIFIER 支，NFModifier.mo:402-408），所以：
   - 外层 break 盖掉内层值；
   - 外层值也盖掉内层 break（BV42 底线就 PASS）。
3. 变成无绑定：
   - 组件绑定在 NFInst.mo:2686 处理，内建属性在 :2652；
   - `instBinding` 把 BREAK 换成 UNBOUND（:2742）；
   - 属性绑定变 UNBOUND 后被换成 NOMOD，不打印（NFTyping.mo:1275-1280）。
4. 变成 UNBOUND 之前 break 仍算"有绑定"的地方：
   - 复合组件的字段不用自己的绑定（`instTypeSpec(..., useBinding and not Binding.isBound(binding))`，NFInst.mo:1982-1983）。br1 的 BR19 `R r = break` → 字段全无绑定。
   - 参数不拿 start 当绑定（`updateParameterBinding` 只在无绑定时调，:2014-2017）。br1 的 BR01：`parameter Real p(start = 2) = break` → `parameter Real p(start = 2.0);`。
   - `verifyBinding` 只查 `var < DISCRETE`（NFFlatten.mo:746-750），所以变量不查。
5. 常量 break：omc 打无绑定常量，引用处留名字（br1 的 BR03、BR18；旧 TBC 族）。

### 3.2 extends 修饰改过、外层 extends 再重声明（R 组）

1. 内层修饰并进 REDECLARE 的 innerMod（NFModifier.mo:411-415）。
2. innerMod 只用来在 REDECLARED 语境里实例化原组件（NFInst.mo:1894-1907，:1898-1900）。新组件由 `redeclareComponent` 建（:1906-1907），不用 innerMod，所以内层修饰丢掉。
3. 重声明之后更外层再给的修饰照常作用到新组件（RAX05 → 7.0）。
4. 被丢掉的内层修饰仍会核名字：改不存在的元素或 protected 元素，omc 报错（NFInst.mo:1540-1592；RAX01、RAX02）。修饰值里的式子不核（RAX06 照收）。

### 3.3 REDECLARED 语境下的 partial（P 组）

1. 修饰里的组件重声明：原组件在 REDECLARED 语境里实例化（NFInst.mo:1898）。这个语境传遍原组件的整棵子树。
2. 这个语境放过三种 partial 检查：
   - 进 partial 类查名（NFLookup.mo:703、:772、:979；NFLookupState.mo:325）；
   - partial 组件类型（NFInst.mo:2000-2002）；
   - 实例化 partial 类（NFInst.mo:429-431）。
3. 元素级重声明按普通语境实例化原组件（NFInst.mo:1691-1705，:1702），所以照报（A68fRC03）。
4. 没被换掉的组件照报：A68fRC09、A68fRC10、PR05、PR07。换成的新类型仍是 partial 也照报（A68fRC20）。
5. 包缓存：`instPackage` 按节点缓存（NFInst.mo:1312-1372）。在 REDECLARED 语境里建好的包实例，之后被普通语境访问不再重查：
   - PR01 omc 照收；
   - 对照 PR02：同一个包在普通语境里第一次进入，omc 拒。
6. **实例化次序决定包第一次在哪种语境建**（第五版补，评审 RV01-RV07）：
   - `instClassDef` 先 `instExtends` 全部 extends（NFInst.mo:1181-1184），再实例化本地组件（:1186-1190）；`instExtends` 递归时同样先 extends、后组件（:1489-1497）。组件的输出次序仍是声明序（RV03 `t` 在 `port.q` 前，OR11）。
   - 实测（底线 = ymc_base_5c7）：
     - RV01：Src 里 `replaceable FP port; extends Base2;`，Base2 普通访问 P → omc 拒（Base2 先实例化，P 在普通语境第一次进入）；底线 AI。
     - RV02：Src 里 extends 写在前 → omc 拒；底线 AI。
     - RV03 / RV04：`P.T t`（或含它的 `Mid m`）写在 `extends Src(redeclare Outlet port)` 之前 → omc 收（extends 先走，P 在 REDECLARED 里建好）；底线 PASS。
     - RV05：同 RV01，包在实例语境里（M 的本地包）→ omc 拒；底线 SKIP。
     - RV06 / RV07：同 RV03 / 本地组件写在后，实例语境 → omc 收；底线 SKIP。

### 3.4 类修饰（C 组）

1. 施加：
   - `applyModifier` 对类元素做 `Class.mergeModifier`，并清掉该节点的包缓存（NFInst.mo:1604-1608）。
   - 组件修饰改 protected 类报 NF_MODIFY_PROTECTED（:1579-1590；CMX03）。extends 修饰改类不查 protected（`modifyExtends` :1374-1421）。
   - 修饰在实例化之前挂上：`modifyExtends`（:1163）、`applyModifier`（:1170）先于 extends 与本地组件的实例化（:1181-1189）。所以与声明次序无关。
2. 生效：类节点每次被实例化，都把自己的修饰当外层修饰并进来：
   - EXPANDED_CLASS 支 :1158-1170；
   - EXPANDED_DERIVED 支 :1200-1222（类修饰 :1210，实例化基类 :1217）；
   - 当组件类型、当包（`instPackage` → `instantiate` → `instClass`，:1360）、当短类的基类都一样；
   - 修饰里的类重声明由 `redeclareClasses` 顶替（:1179）。
3. 优先级：短类节点上的类修饰 > 短类自己的修饰 > 基类节点上的类修饰 > 基类自己的修饰（:1205-1217）。
4. 查名进类前先 `instPackage`（NFLookup.mo:700、:767）。所以经被改过的类查 `B.C` / `B.i`，拿到的是带修饰的包实例。CMY01-CMY09 在函数体、方程、维、嵌套模型、if 条件里的 `B.i`，omc 都取 4。
5. 其它实测：
   - 重声明不可替换的类，omc 照收（CMX04）；
   - 组件数组上的类修饰照收（CMX05）；
   - 同名重复的类重声明，omc 拒（CMX20）；
   - 类修饰值类型不符，omc 拒（A68fYPT01）；
   - 修饰值是表达式时在写修饰处求值（A68fXSC01）；
   - 改 protected 常量，omc 拒（A68fWPR 族）。
6. **extends 一个被类修饰改过的类**（第四版按源码重核，更正第三版引的 :1143-1170）：
   - `modifyExtends`（NFInst.mo:1374-1422）：
     - 取被 extends 的那个类节点自己的修饰（`InstNode.getModifier(extendsNode)`，即这个节点上合并过的类修饰），放在外层，并到 extends 子句修饰之上（:1391-1392）；
     - 再用 `applyModifier` 施加到基类的类树（:1421）。类树来自 `Class.classTree`；若是短类（EXPANDED_DERIVED），取它基类的树（NFClass.mo:345）。
   - 基类随后由 `instExtends` 实例化（:1475-1517）：只递归处理 extends、实例化组件，不再读任何修饰。
   - 结论：**被 extends 直接点名的那个类节点上的修饰，盖过 extends 子句自己的修饰**。实测：
     - PX01：类修饰 `B(i = 4, j = 8)`，`extends B(i = 5)` → e.i = 4、e.j = 8；
     - PX02 → a.e.i = 4；
     - PX03：类修饰里的类重声明 D 盖过 extends 子句里的 D2 → 1.0；
     - EQ04 / EQ06 / EQ09 / A68fXSB01-03 都取类修饰的值。
   - **丙-2：extends 经短类别名**（`model B2 = B; … extends B2`），只读被点名的 B2 节点自己的修饰：
     - B2 节点上的类修饰生效（Q04：`extends A(B2(i = 4))`，E `extends B2` → e.i = 4；底线 FAIL，取 3）；
     - B2 定义自带的修饰丢掉（Q03：`B2 = B(i = 6)` → e.i = 3；即 open-questions.md:85 已记的 1.25.0 怪癖）；
     - 链下 B 节点上的类修饰也丢掉（Q01、Q08 omc 取 3，底线同为 PASS；Q02：B 上的类重声明经 B2 被 extends，omc 得 `Real a1.b3.c.x;`，无绑定）。
     - 原因：短类自带修饰与链下节点修饰只在 `instClassDef` 的 EXPANDED_DERIVED 支里读（:1200-1222）。extends 走 `instExtends`，不进那一支。
7. **短类别名到被改过的类，用作组件类型**：组件实例化走 `instClass` 的 EXPANDED_DERIVED 支，别名节点与链下各节点的修饰逐层并进来（:1200-1222 → 基类的 :1158-1159）。所以经别名声明的组件，类修饰照样生效（EQ05、PX04、A68fCM20、A68fYLP01 / 02）。与第 6 条 extends 经别名不对称，两边都以实测为准。
8. **改过的类又被重声明顶替**：
   - 修饰重声明（`redeclareClasses`，NFInst.mo:1618-1647）只把 REDECLARE 的 outerMod 带到新类（:1636、:1735-1737）。外层修饰进 outerMod（NFModifier.mo:419），内层的进 innerMod、被丢掉。
   - 元素级重声明（`redeclareClassElement`，:1677-1689）传 NOMOD（:1687）。
   - 结论：被顶替类上内层的类修饰都丢掉。A68fYLP03 / YLP04 omc 取新类的值。
   - 但**顶替进来的那个元素自己身上的修饰**照带（第六版补，复评 RW3）：`redeclareClass` 取新类自己的修饰 `Class.getModifier(rdcl_cls)`，与外来的 outerMod 合并（NFInst.mo:1735-1737），写回新类（:1787）。外层 extends 的类修饰在 `modifyExtends` / `applyModifier` 时已并到中间层那个重声明元素上（:1163、:1170 先于 :1173 的 `redeclareElements`；`applyModifier` 对重复的继承元素逐个施加，:1562 / :1574-1575），重声明链上各槽随后都换成新节点（:1672-1674）。
   - 实测（rw1）：RW3：D1 `extends A; redeclare model B = B5;`，`extends D1(B(i = 6))` → `b.i = 6`（底线 FAIL，取 5）；RW1 / RW2 同形态、经 extends / 嵌套类修饰 → 6 / `b3.c.x = 2.0`（底线 SKIP）；RW4：类修饰在内层 D1 的 extends 里、外层再修饰重声明 → 丢，`e.i = 5`（底线 SKIP）。
9. **丙-4：本地类在定义层先被用过，再进挂了类修饰的实例**：
   - 定义层查名（`A.E`）进 A 时，omc 先 `instPackage(A)`（NFLookup.mo:700 / :767），实例化的是 A 的克隆（NFInst.mo:1360）。E 在克隆里查到、展开，A 定义节点上的本地类 E 不被展开。
   - 每个实例化都先克隆本地类（`ClassTree.instantiate`，NFClassTree.mo:518-524）。克隆在本实例第一次用到时才展开，基类名在本实例的类树里查，看得到本实例上挂的类修饰。
   - 只有本地类在定义节点上就已展开时，克隆才会带着旧展开：`InstNode.clone` 复制类树（NFInstNode.mo:1920-1935），已展开的树连 extends 节点一起照搬（NFClassTree.mo:685-702）。这种情形包括根类，以及定义层单段名查到的类；ymc 已据探针（ESC107 / LMX38，inst.cpp:1176 注释）用 `settled_classes_` 区分（inst.cpp:345 / :2216 / :2588）。
   - 实测：先写定义层引用 `A.E pre;`，再写 `extends A(B(i = 4))` 或 `A a1(B(i = 4))`，omc 仍取 4（Q05：`e.i = 4`、`y = 4.0`；Q07：`a1.e.i = 4`）；类重声明也一样（Q06：`a1.b3.c.x = 1.0`）。`pre` 本身取未改的 3。
   - 底线：Q05、Q07 FAIL（取 3），Q06 SKIP。
10. **extends 修饰里的类修饰先挂、后实例化**（第五版补，评审 RVC2 / RVC3）：
   - `instClassDef` 先 `modifyExtends`（NFInst.mo:1163）：extends 修饰经 `applyModifier`（:1421）并到基类类树里的类节点上，之后才 `instExtends`（:1181-1184）、实例化本地组件（:1186-1190）。所以基类里任何组件实例化时，类修饰都已挂好。
   - `modifyExtends` 先递归内层 extends（:1395）、再施加本层（:1421），后施加的在外：外层 extends 的类修饰盖过内层的；继承来的类与基类共用槽（NFClassTree.mo:492-556），外层改继承了几层的类也落到同一个节点上。
   - 实测：
     - RVC2：A 里 `model B3 extends B; B3 b3;`，`extends A(B(C(x = 2)))` → `b3.c.x = 2.0`（底线 SKIP）；
     - RVC3：A 里 `E e0;`（E extends B），`extends A(B(i = 4))` → `e0.i = 4`、`x = B.i` 取 4（底线 SKIP）；
     - RVC4 / RVC5：redecl-only 形态同理（底线 SKIP）；
     - RV20：同 RVC3 去掉 `x` → omc 4，底线 FAIL（3）；RV22：A 里 `B2 b0;`（B2 = B）、两层 extends 改 B → omc 6，底线 FAIL（3）；RV17 / RV19 / RV21 外层改继承了两层的 B → omc 取外层值，底线 FAIL；
     - OR17：外层类修饰盖在内层 extends 修饰里的类重声明上 → 带到新类（`e.i = 6`）；OR18：派生类的元素级重声明 → 类修饰丢掉（`e.i = 5`），与 §3.4-8 一致。

### 3.5 omc 的整包实例化与 partial（PP 族；本桶做，阶段 S5，方案见 §6.8）

1. **在哪一步触发**（instPackage 的全部调用点）：
   - 限定类名逐段进类：`lookupName` → `lookupLocalName`（NFLookup.mo:580-606 → :679-742）。进类前先 `Inst.instPackage`（:700），再查 partial（:703）。涵盖组件类型路径、短类基类名、`lookupClassName`（:76-88）。
   - extends 基类名：`lookupBaseClassName`（:90-108）→ `lookupNames`（:627-658）→ `lookupLocalNames`（:744-801）。进类前 `instPackage`（:767），partial 检查（:772-777）。基类名进 `PartialModelicaServices` 免查。
   - 表达式里的限定 cref 与函数名：`lookupCref`（:304-344），`lookupFunctionName`（:193-215）也经 `lookupCref` → `lookupCrefInNode`（:955-1034）。只有类作用域才先 `instPackage`（:974-981），再查 partial（:979）；经组件不进类。
   - 自引用豁免：只在 `lookupName` / `lookupNames` 的第一步（`isSelfReference`，:608-625；调用处 :597 / :651）。`lookupCref` 没有这个豁免。
   - import：
     - 非限定 `import A.*`：类树展开时立即解析并 `instPackage`（NFClassTree.mo:241-252 → NFImport.mo:150-163）。语境是 NO_CONTEXT，REDECLARED 不放过。PP03：一个导入名都不用也报。
     - 限定 / 重命名 import：用到时才解析（NFClassTree.mo:1851-1872 → NFImport.mo:125-145 `instQualified` → `lookupImport` NFLookup.mo:272-283），路径上逐段进类。实测 A68fVLK11 `import Lib.Q.c`、VLK14 `import Lib.Q.f` 都报。
2. **instPackage 做什么**（NFInst.mo:1312-1372）：
   1. 按节点缓存（PackageCache，:1324-1342）。
   2. `instantiate`（:1360 → :420-433），partial 类除 REDECLARED / RELAXED 外不实例化。
   3. `instClassDef` 实例化全部本地组件（:1186-1190）与 extends（:1181-1184），末尾 `Restriction.checkClass`（:1195）。
   4. 包本身非 partial 时，再做 `instExpressions`（:1364-1368 → :2450-2525）：
      - extends 递归；
      - `instComponentExpressions`（:2666-2700）：绑定 :2686、条件 :2687、组件类实例递归 :2689-2691、维 :2693-2695；
      - `instBuiltinAttribute`（:2641-2664，属性修饰的绑定）；
      - `instSections`（本类的方程 / 算法段）。A68fUMB11 是模型类 M2 经 `Lib.M2.L` 进入，方程里的 `PM.k` 照报。
3. **组件实例化里的 partial**：
   - 类型路径经过 partial 类（见第 1 条）；
   - 组件类型本身是 partial：`checkPartialComponent`（:2000-2002 → :2104-2127）。REDECLARED 不查；`outer` 组件的类型本身不查（:2120），但类型路径照样进类（PP07、PP08 omc 报）；函数型组件另判（:2113-2119）。
4. **表达式里的函数调用**：调用 → `Function.instFunction`（NFCall.mo:2333-2342）→ `instFunction3`（NFFunction.mo，`Inst.instantiate(fn, FUNCTION, instPartial = true)` + `Inst.instExpressions(fn)`）。被调函数的形参类型路径、体里的 cref / 调用照样查。
   - PP01：包常量绑定里调的函数，形参类型经 partial → 报；
   - PP05：调用的函数体经 partial → 报（底线就 SKIP）；
   - PP04：包里的函数没被调用 → 不报。
5. **报的错**（不在 REDECLARED / RELAXED 时）：
   - 经 partial 类查名 → ERROR(PARTIAL_CLASS)（NFLookupState.mo:323-336）：
     - 路径首个节点是组件 → USE_OF_PARTIAL_CLASS（"component a contains the definition of a partial class Medium"，A68fXPK05）；
     - 否则 → LOOKUP_IN_PARTIAL_CLASS（"X is partial, name lookup is not allowed in partial classes"，其余各例）。
   - partial 组件类型 → PARTIAL_COMPONENT_TYPE（NFInst.mo:2120-2125）。
6. **不报的情形**（探针核过）：
   - 包从没被经限定名进入：PP02；包里的嵌套包 / partial 包没被进入（PP10、PP12）——类元素不实例化，只实例化组件。
   - 包实例第一次在 REDECLARED 语境里建好、缓存后，再被普通语境访问（PR01、PP06）。
   - 自引用的第一步（A68fZSR01-06、WSS01-06）。但：
     - 第一步豁免后再进的 partial 类照报（A68fVSR02 / VSR03 / VSW04）；
     - 经 import 别名到达自身不算自引用（A68fWSB01）；
     - cref 没有豁免（A68fVPQ06：模型 M 里的 `M.k` 照样实例化未重声明的 M，撞上 `Medium.T`）。
   - 基类名进 PartialModelicaServices（A68fRC12 / RC15 / RC16 omc 收）；组件类型名不免（A68fRC13 omc 拒）。
   - 被 extends 重声明换成非 partial 的（A68fVPT01-09、VMC02、UPV01-03 / 05-08、TTP01-04、UPU01 / 02 / 04 / 05 / 11-13 / 16-18 omc 收）。换成 partial、或再换回 partial 的照报（A68fUPU06 / UPU07）。
   - 派生类的元素级重声明不改变基类自己词法作用域里的名字（A68fUPU09：基类 B 里的 `S.T` 解析到 Lib.S；UPU21：B 里 `import S = Lib.PS`）。
7. **其它错误**：instPackage 还会报 partial 以外的错（缺元素、NON_ENCAPSULATED NFLookup.mo:1022、protected 访问 A68fVSW01、encapsulated 边界 A68fWSB02、`checkClass`）。这些不属 PP 族，见 §6.8 末"做不到的"。
8. **ymc 现状**：
   - `inst_package` 只给实例语境里的类和带修饰的短类建包实例；定义层的包经限定名进入时不实例化（inst.cpp:627-640 注释已标 ponytail）。
   - 包实例里组件的类型查找会撞 :624 / :2184（报 SKIP）；直接写的 partial 组件类型只在 flatten 的 `bad_type_path` 查。
   - inst 不做 instExpressions 这一步；flatten 按需取用绑定，进类时只查被进的那一个类是否 partial（`cannot_enter`，flatten.cpp:16252）。
   - 结果：底线在 PP 族上 ACCEPTS-INVALID 44 个（§6.8 列名）。
9. **根类全名在 RELAXED 语境里查**（第五版补，评审 RVR）：
   - `lookupRootClass`（NFInst.mo:347-389）先把语境设成 RELAXED（:358），再 `lookupClassName`（:362）。根路径上的各个包都在 RELAXED 下 `instPackage`：partial 检查跳过（NFLookup.mo:703 / :772 的 `inRelaxed`）；包非 partial 或在 RELAXED 时做 `instExpressions`（NFInst.mo:1365-1368），也在 RELAXED 下；按 INSTANTIATED 缓存。检查中再进的包同样 RELAXED、同样缓存。之后模型里普通语境访问这些包不再报。根类本身（最后一段）不 instPackage，由 `instantiateRootClass` 按普通语境实例化。
   - 实测（`out/RVR/Lib.mo`；omc `-i=…`，ymc `--modelica-path=lib --load-model=Lib --class=…`）：
     - `Lib.Ex.M`：omc 收，底线 PASS；
     - `Lib.Ex.M2`（`x = Lib.Ex.c`，c 的类型经 partial 的 Medium）：omc 收（Lib.Ex 已在 RELAXED 里缓存），底线 PASS；
     - `Lib.M3`（同样 `x = Lib.Ex.c`，根路径只有 Lib）：omc 拒，底线 AI。

## 4. ymc 现状（底线的 not_implemented 点）

| 目标 | 报错点（文件:行） | 走的层 / 为什么停 |
|---|---|---|
| BM1 | flatten.cpp:19726（`inst_exp` 的 default 支）← emit_variable :5953 | flatten 拿 SCode 绑定式定型，`ExpKind::Break` 没有分支 |
| BM2 | 同上 ← emit_variable :5945（外来修饰） | 同上 |
| BM3 | 同上 ← plan_attributes :7725 `attr_value` → :8054 | 属性值同样定型 |
| ModClass4 | flatten.cpp:7543；根因 inst.cpp:2205-2211 | 组件修饰里的类修饰被 `plain_class_mod`（:1616-1628）拒，没施加。下一层是 :631-633（`B.C`）→ :441-443 |
| ModClass6 | inst.cpp:1574 | 只放开 1574 会静默错：`inst_package` 顺短类链（:448）不读链上节点的类修饰，b.z 会取 1.0 |
| RICM | inst.cpp:1574 | B 的组件 `b2` 在 :1533 已按 P = Real 实例化，事后补不了。下一层是 :631-633 |
| const15 | inst.cpp:442 ← flatten `pkg_instance` :16310 ← `resolve_qual_const` :9077 | 普通类修饰底线已挂上（:1577-1580），但经 B 查名一律 SKIP |
| RedeclareClass2 | inst.cpp:2184 | 原组件 `con` 子树里有 partial 的短类别名 |
| RedeclareMod11 / 12 | inst.cpp:624 | 原组件子树里 `Medium.X` 进了 partial 包。底线先实例化原组件、后 `redeclare_component`（:1676） |
| Bug2727 | inst.cpp:1806 | 组件 `extends_mods` 非空就 SKIP |

## 5. 对照的 lessons

| 条目 | 本方案怎么避开 |
|---|---|
| 2026-11-11 返工方案反复 | 每处新代码都标明进入条件（§6.7）；放开条件最窄；漏考虑的老路线事先列清（§6.6），不边评边补 |
| 2026-11-11 实现者自测 0 FAIL 不等于没错 | §12 e：对抗评审一轮 + 核对评审一轮 |
| 2026-11-11 检查挂在一个入口，兄弟路线绕过去 | B 组列出读绑定的全部路线（§6.1）；C 组把放开点放在汇合点 `inst_package`，其余兄弟路线（extends、别名、被顶替）在 §6.6 逐条处理 |
| 2026-11-11 import 命中给的是定义链 | 本桶已无 G 组；C 组只认实例树里的类节点 |
| 2026-11-11 基线碰巧对的用例要点名 | §12 b：例外只限 §6.7 列名的 FAIL→PASS |
| 2026-11-03 查名和展开类的探针要带调换声明次序的变体 | CMX07 / CMX08 已核：底线因 `classes_via_mod`（inst.cpp:256-257）SKIP；新探针集都带两种次序 |
| 2026-11-03 放开一条 SKIP 时，新加的拒绝条件写在调用点 | 不改 `plain_class_mod`，新谓词独立 |
| 2026-11-03 判 SKIP 之后压平还在走 | P 组挂起后查找照常往下，返回有效节点 |
| 2026-11-03 语法位置也要对抗 | sx1 入新集 |
| 2026-11-03 omc 能压平不等于能放开 | Bug2727 分歧记账本 |
| 2026-11-03 定义作用域链不等于书写路径 | F1 / F2 的类修饰按实例树里的节点取，不按定义 |

## 6. 方案

总原则：
- 新代码的进入条件都标明（§6.7）。
- 放开条件外的写法走原来的代码。
- 新判定写成独立谓词，不改共用判定函数。
- 放开模型会经过的老路线，照 OMC 补齐（§6.6）。

### 6.1 B 组：`= break`（BM1-3）

**B-1 组件绑定**（emit_variable，flatten.cpp:5940-5954）：
- 放开点：`mraw` 或 `comp.binding.raw_exp` 是 `ExpKind::Break` → `binding` 留空，不调 `inst_exp`。
- 条件 cond_B（全部满足）：
  - 有效变率 `var_min(var_floor, 组件变率)` ∈ {Var, Discrete}；
  - `cty.kind == TypeKind::Real`；
  - `!ctx.in_record`。
- 不满足：照旧调 `inst_exp` → SKIP。
- 为什么与 omc 一致：
  - break 在合并期算有绑定，ymc 的优先级与 `Modifier.merge` 相同（§3.1-2）；
  - 胜出的是 break → UNBOUND（NFInst.mo:2742）；
  - 变量没有"有无绑定"的前置检查：`updateParameterBinding` 只对参数，`verifyBinding` 只对参数 / 常量，`useBinding` 只对复合组件；
  - ymc 这边，`start_as_binding` 只对参数（flatten.cpp:5556），`frame_value` 对变量直接 Fail（:10723）。
- 读绑定的全部路线，对 Break 都安全：
  - emit_variable 的 `braw0` 分析（:5836-5880）：只是空走；
  - `needs_forward_shape`（:5906 / :5908）：空走；
  - 常量支 `eval_scode`（:5918-5941）：常量不放；
  - 两处 `inst_exp`（:5945 / :5953）：就是放开点；
  - `frame_value`：变量 Fail；
  - `eval_const_value` / `eval_const_array_element`：只对常量；
  - `emit_array`、`emit_ext_object`、复合组件的 `first_bound(matched)`、函数变量绑定：都不放。

**B-2 属性**（plan_attributes，flatten.cpp:7725，`attr_value` 之前）：
- 放开点：胜出来源的 `sm.binding` 是 Break → `continue`。这个属性不出现，**不再往内层来源找**。
- 条件 cond_B2：`dims.empty()`；`break_ok` 为真（只有 emit_variable 在 cond_B 成立时传）；属性名 ∉ {fixed, stateSelect}。
- 为什么一致：NFInst.mo:2652 + :2742 + NFTyping.mo:1275-1280。
- fixed / stateSelect 另有按来源扫的路线，不放：
  - `fixed_mod_of` / `eval_fixed`（:5530-5549）、`each_fixed_true_`（:5828）；
  - stateSelect 预扫（:1153-1190）、`state_scanned_`（:7708）。
- 开头的校验循环（:7670-7689）不动：break 在那里算有值，与 omc 合并期一致。

**F4（老路线补齐，第三版去掉门控，第五版收窄）**：被丢弃属性的核查，见 §6.6 乙。

条件外照旧 SKIP。`A a(x = break) if false` 这类到不了放开点的，底线就 PASS（BV51）。所以**不在 inst 层判 break**。

### 6.2 R 组：Bug2727

**放开点**：inst.cpp:1673-1677，`apply_extends_modifier` 的 Redecl 支、调 `redeclare_component` 之前。
- 底线这种情况必在 :1806 SKIP。
- `apply_extends_modifier` 只有两个调用方：
  - :1547（extends 修饰）；
  - :502（`inst_package` 的短类修饰）。那里的组件重声明在 :476 已被拒，到不了 :1676。
- 所以这个调用点就是 extends 路线。

**动作**：cond_RA 成立 → 清空这个组件的 `extends_mods` / `extends_scopes`，再调 `redeclare_component`；否则照旧 SKIP。

**cond_RA**（全部满足）：
- `extends_mods` 里每个修饰：Mod、无绑定、非 final、非 each、子修饰非空；
- 每个子修饰：Mod、有绑定、无子修饰、非 final、非 each，名字是原组件类实例里的 public 组件；
- `comp.attributes.is_final` 为假；
- 原声明不带维。

不满足时照旧 SKIP：RAX01 / RAX02（omc 拒）、RAX03（嵌套）、RAX08（each）、RAX09（数组）、YRG01（final）。

**为什么一致**：§3.2。名字核查对应 omc 实例化原组件时的核查；式子不核（RAX06）。

### 6.3 P 组：REDECLARED 语境下的 partial（RedeclareClass2、RedeclareMod11 / 12）——用户已批准，带退路

**机制："挂起，被 extends 重声明换掉就丢，最后照报"**
1. **组件栈**：`inst_component`（inst.cpp:1920 起）入口压入本组件节点、出口弹出（RAII）。纯记账，不改任何判断。
2. **根阶段标记**：`instantiate_root_class`（:2542）进入时置位；`insert_generated_inners(root)`（:2590）之后 flush 并清位。
3. **挂起**：:624 与 :2184 在"根阶段且组件栈非空"时，记一条 pending {原因, 子原因, 组件栈拷贝}，查找照常往下：
   - :624-625 不再 `return empty`，接着走 :630 起的实例 / 定义树查找；
   - :2184 本来就不 return。
   - 否则（flatten 回调的 `pkg_instance`、组件栈空、根阶段外）照旧当场报。
4. **丢弃**：:1675-1676，`apply_extends_modifier` 调 `redeclare_component(bc, …)` 之前，删掉栈里含 `bc` 的 pending。只这一个调用点丢。
5. **flush**：根阶段结束仍有 pending → `mark_unsupported(第一条)`。

**为什么一致**：
- 原组件子树（组件栈上有它）对应 omc 的 REDECLARED 范围，被 extends 重声明换掉对应 REDECLARE 支（§3.3-1/2）；
- 元素级重声明不丢（§3.3-3）；
- 没换掉的、换成仍 partial 的照报（新组件重新 `inst_component(bc)` 的 pending 记在丢弃之后）；
- 包实例缓存后复用不再报（PR01），所以**不加** barrier。

**条件外**：组件修饰 / 短类修饰里的重声明（PR03 / PR04、A68fRC04 / RC05 / XRC01）、constrainedby（A68fRC24 / RC26）、PartialModelicaServices（A68fRC12 / RC15 / RC16）——都 flush → SKIP。

**对共用实例化的改动边界**（用户批准的范围，实现不得越出）：
1. `inst_component`：只加 RAII 组件栈压 / 弹，不加任何判断、不改任何分支。
2. inst.cpp:624：从"`mark_unsupported` + `return empty`"改为"根阶段且栈非空时挂起、照常往下查"；其余情况一字不改。:2184 同理（本来就不 return）。
3. inst.cpp:1675：调 `redeclare_component` 前删 pending，不改调用本身。
4. `instantiate_root_class` 末尾：flush。
5. 挂起后继续实例化原组件子树，会写到这些全局表：
   - arena：nodes / classes / trees / comps / SCode 合成元素（`redecl_elems_`、`dims_elems_`，受 `synth_cap_` 限）；
   - 包缓存：`pkg_cache_`、`I_.package_instances`；
   - 已施加记账：`I_.applied_redecls`、`I_.applied_class_mods`；
   - 类修饰：`I_.class_mods`、`I_.class_modded`；
   - 类重声明：`I_.redeclared_classes`、`I_.replaced_in`、`I_.redecl_cc`、`redecl_class_nodes_`、`mod_redecl_nodes_`、`inst_redecls_`；
   - 其它：`settled_classes_`、`instances_`、`I_.dup_kept`、`I_.class_extends_target`、`I_.class_extends_via_mod`、`I_.final_class_mod_comps`。
   - 这些写入与底线实例化**不撞 partial 的原组件**时（PR08 那类，底线就 PASS）完全同类、同路；新增的只是"原本在 :624 / :2184 中途停下的原组件"现在也走完。
   - `I_.generated_inners` 不会被写：`insert_generated_inners` 只遍历最终实例树（:2616 起），被顶替的原组件子树不在里面。
6. **退路（用户定）**：§12 b 的机械比对里，只要有一个底线有输出（退出 0）的用例因 P 组阶段的改动而变化，或有一个底线 SKIP 的用例变成 FAIL / AI / CRASH（第五版加，RV05 这类），P 组整项放弃（三个目标一起），不修补。
7. **继续实例化原组件子树**（第五版补，评审第 7 条）：
   - 挂起后照常往下走，子树里撞到的其它 `mark_unsupported` 不挂起、照常生效（被换掉也不丢）：那些是 ymc 没做的构造，子树写到全局表的东西不可信，宁可 SKIP。
   - flush 时若 `unsupported_` 已置位，原因取最早发生的那件（pending 与 mark_unsupported 按发生先后），保证 SKIP 原因可复现。
   - 继续走的是"撞 partial 后底线从没走过"的路线，有 CRASH 风险；由第 6 条的"底线 SKIP 变 CRASH 也触发退路"兜住。
8. **依赖 S0**：包实例第一次在哪种语境建要与 omc 同序（§3.3-6）。S0（§6.9）放弃时，P 组改用 §6.9 的窄做法。

### 6.4 C 组：类修饰（ModClass4、ModClass6、const15；RICM 见 §6.5）

**公共限制**：
- 被修饰的类（及嵌套修饰改到的类）`restriction == Model`；
- 修饰与子修饰不带 final / each；
- 同一修饰里同名子修饰只有一个；
- 目标是 public。

**三个独立谓词**（新写，不改 `plain_class_mod`）：
- `redecl_only_class_mod(cls, m)`（ModClass4 / RICM）：
  - m 无绑定、子修饰非空；
  - 每个子修饰都是短形态类重声明：`= 名字`，无修饰、无维、无前缀，restriction ∈ {Model, Type}；
  - 重声明的名字是 cls 的本地 public 类。
- `nested_class_mod(cls, m)`（ModClass6）：
  - 每个子修饰改 cls 的本地 public Model 类 C；
  - 再下一层每个子修饰都是 `名字 = 字面量`（含数值取负），名字是 C 的 public 组件。
- `const_literal_class_mod(cls, m)`（const15）：
  - 每个子修饰都是 `名字 = 字面量`，名字是 cls 直接声明的 public `constant`；
  - 字面量种类与声明类型相容（Integer ← IntLit；Real ← IntLit / RealLit；Boolean ← BoolLit；String ← StringLit）；
  - cls 的定义只有组件声明（无嵌套类、无 extends、无方程 / 算法）。

**CM-1（ModClass4）**——inst.cpp:2205-2213：
- 放开：`!plain_class_mod` 但 `redecl_only_class_mod` 成立、且 cond_CM1 成立 → 照 plain 的做法 `comp_cmods.push_back` + `applied_class_mods.insert`。由 `instantiate` 的 cmods 支（:1311-1318）在克隆出类节点时挂上。
- cond_CM1：
  - 组件非 replaceable、无 constrainedby、无维、无条件；
  - 不是重声明合成的（`orig_def` 无效）；
  - 目标类在 base 里是 public（CMX03）；
  - 不在 `inst_package` 的实例化里（RAII 深度计数为 0）。
  - 后两条保证底线必 SKIP：flatten 走不到的组件，:7543 不会报。
- 跨实例一致（旧 CT 族）：两个集合只记新形态。放开时该 SMod 已在"拒过"集 → `mark_unsupported`；拒时已在"放开过"集 → 同样。
- 之后由现有路线生效：
  - `B b` 经类型链读 `class_mods`（inst.cpp:2037-2052），再 `collect_class_redecls`（:2198-2199）得到 `C → D`；
  - `B.C c` 走 CM-3。

**CM-2（ModClass6）**——第五版改为经 F7 预挂（不再当场挂）：
- 放开：extends 修饰里 `nested_class_mod(cls, s.mod)` 且 cond_CM2 → 收进 F7 的 `ext_cmods`（底线在 :1574 SKIP）。
- cond_CM2：
  - 目标是 base 的本地类；
  - 派生类没有对它的元素级重声明（`base_redecls` 无同名；与 F5 的丢弃规则一致）；
  - extends 修饰里无同名的另一个子修饰。
  - 第四版的"基类实例里没有已实例化、类型链经过 cls 的组件"一条删去：预挂后这些组件实例化时就读到修饰。
- 之后：
  - 以 B 或其别名为类型的组件：类型链读到嵌套类修饰 → flatten :7543 SKIP（不是错，同第四版；ModClass6 里没有这种组件）；
  - 类 extends B 的（RVC2 的 B3）：F1 把条目里的嵌套子修饰继续预挂到 B 的基类实例上，omc `b3.c.x = 2.0`。

**CM-3（ModClass4 的 `B.C`、RICM 的 `B2.P`）**——inst.cpp:631-633：
- 放开：`class_mods[entered]` 全是 `redecl_only_class_mod`、entered 是 Model、`pkg_cache_` 里还没有它 → 不 SKIP，落到 :635 的 `inst_package`。
- 第五版（F7 附带）：`class_mods[entered]` 全是底线 plain 形态（`plain_class_mod`）时同样落到 `inst_package`。原因：预挂后，被改的类在基类里任何查名之前就已是 class_modded；底线靠"先查名、后挂修饰"绕过这道守卫的写法（XST04-06 是读到陈旧包实例的老错；只读未改部分的会碰巧 PASS）会撞上 :631-633，PASS 变 SKIP。omc 的包实例本来就带着修饰建（§3.4-2）。
- 其余照旧 SKIP。

**CM-4（const15，以及 CM-3 之后）**——inst.cpp:441-443（缓存未命中且 class_modded）：
- 放开条件：cls 是 Model，且 `class_mods[cls]` 全是 `redecl_only_class_mod`，或全是 `const_literal_class_mod`。
- 第五版（F7 附带）：`class_mods[cls]` 全是底线 plain 形态时也放开，不限 restriction（理由同 CM-3）。交给同一段短类修饰代码；它不收的写法（组件修饰带子修饰 `B(x(start = 1))` 之类，:492-499）照旧 SKIP——这是 F7 的已知风险，见 F7。
  - XST04：`B.C c` 进 B 时按 `B(n = 2)` 建包实例，C 里 `y = n` 取 2；`z = B.n` 取同一份 → 与 omc 一致（底线 FAIL）。XST05 / XST06 同理。
- 动作：把这些修饰（外层在前、各带写处）排在 `short_mods` 最前，交给现有短类修饰代码：
  - :470-480 核形态；
  - :481-482 `collect_class_redecls`；
  - :484 `instantiate`；
  - :500-511 内层先、外层后 `apply_extends_modifier`。
- 为什么一致：§3.4-2/4。经 B 的所有路线（CMY01-CMY09）底线都汇到这里 SKIP；放开后取同一份包实例。

**CM-5（ModClass6 的 `D.C`）**——inst.cpp:448 之后：
- 链上节点 n（n ≠ cls）的 `class_mods[n]` 全是三种新形态之一 → 插在 `short_mods` 里 n 自己那层之后（§3.4-3），交给 :470-511。
  - redecl-only 子修饰由 :482 收；
  - nested 子修饰由 :503-511 挂到包实例的类上；
  - const-literal 子修饰由 :502 施加。
- 链上只有底线形态的 plain 修饰 → 不动（底线行为）。
- 混合 → `mark_unsupported`。
- ModClass6：`short_mods = [D 的 C(x = 2), B 的 B(C(z = 2))]` → `class_mods[C@pk] = [C(x = 2), C(z = 2)]` → b 得到 x = 2、y = 2、z = 2。

### 6.5 RICM（单独一项）——用户已批准，带退路；第五版改为经 F7 预挂

1. **动作**：F7 收 `ext_cmods` 时，多认 redecl-only 一种形态（`redecl_only_class_mod` 且满足 cond_CM2 前两条），底线在 :1574 SKIP。
   - F7 把它预挂到基类实例的 B2 克隆上；
   - `b2` 经类型链读到，`collect_class_redecls`（:2198-2199）得到 `P → Integer`；
   - `B2.P p` 走 CM-3 / CM-4。
   - 同一机制也放开 CMX06 / CMX17，以及 RVC4 / RVC5（类 extends B / B2 的组件由 F1 收到重声明）。
2. **为什么必须预挂**：omc 先 `modifyExtends`（:1163）、后实例化基类（:1181）。第四版 RICM 自带的 `ext_cmods` 已并进 F7，不再单独写。
3. **改动边界**：只在 F7 的形态门里多认 redecl-only 一种。底线有输出的用例里，这一形态必在 :1574 SKIP，放开不改它们的输出。
4. **退路（用户定）**：§12 b 里只要有一个底线有输出的用例因 S3 而变，或有一个底线 SKIP 的用例变成 FAIL / AI / CRASH，RICM 整项放弃（F7 的门里去掉 redecl-only），不修补。

### 6.6 放开模型会经过的老路线（规则 (c) 补充，事先补齐）

范围口径："放开模型会经过的老路线"指读取或消费本桶放开的数据的路线，包括：
- 被 break 去掉的绑定 / 属性；
- 被清空的内层 extends 修饰；
- 被顶替的原组件；
- 挂了新形态或被放开的类修饰的类节点。

同一模型里与这些数据无关的底线老错，照旧 KNOWN（这一口径见 §7-4，请用户确认）。

依据：
- 旧评审 567 个探针里放开模型出过错的写法（f7 的 FAIL / AI、f7 为躲错加的 SKIP 守卫原因，见 `allp/*.txt`）；
- 本次新写的 eq1 / px1。eq1 用底线已支持的等价写法核老路线：组件修饰重声明等价于 CM-1 的组件类型路线，短类包修饰等价于 CM-4 的包路线。

**甲：放开条件已排除，放开模型走不到（不改代码，理由 + 探针）**

| 老路线 | 为什么走不到 | 依据 |
|---|---|---|
| 类修饰改函数 / 记录 / 包 / 类型 / operator record（A68fYFN / ZFN / ZRS / RW / TF / OR / WKP 族；构造器、函数发射不读类修饰） | 公共限制 restriction == Model | §6.4 |
| 类修饰值在写处求（A68fXSC 族）、类型不符（A68fYPT 族） | 只收字面量，种类相容 | §3.4-5 |
| protected 目标（A68fWPR / VPR / PD 族、CMX03） | public 限制 | NFInst.mo:1579-1590 |
| 包缓存陈旧（A68fXST04-06，底线 FAIL） | 第五版不再是"走不到"：F7 把所有 extends 类修饰改成预挂，XST04-06 改由 F7 + CM-3 / CM-4 的 plain 门补齐（乙 F7） | NFInst.mo:1163 → :1181（先挂后实例化） |
| 定义层在 extends 之前查到被改的类（CMX07 / CMX08） | 底线已因 `classes_via_mod` SKIP | inst.cpp:256-257 |
| 函数里用被重声明的类型（EQ01） | 底线的同一机制（类重声明进组件实例）已在 flatten 报 "lookup of redeclared class"，是 SKIP，不是错 | `pout/EQ01` |
| 包实例里的下游取值（EQ02 底线 PASS） | CM-4 复用的就是短类包修饰的同一套代码 | `pout/EQ02` |
| applied_class_mods 按 SMod 全局记账（A68fXCT01-04） | CM-1 的两个集合 | §6.4 |
| flatten 回调里建的包实例（A68fXPK01 / 02 / 08） | 根阶段外不挂起，当场报 | §6.3 |
| 常量 / 包常量 / 记录的 break（A68fTBC / XBK / DB / RB / XB / BC / DC 族） | B 组只放 Real 变量 | §6.1 |
| R 组放开后的新组件 | 与底线 RA01（PASS）同路 | `allp/w/A68fRA01` |

**乙：放开模型走得到，本卡照 OMC 补齐（F7、F1、F2、F4、F5、F6；第五版删 F3）**

用户 11-11 定：F 系列按类修饰触发，不分新旧写法，底线已支持的普通类修饰也走新代码；兜底仍是规则 (a) 的机械比对，底线有输出的用例变得与 omc 不同就撤该项。todo 批准的改动点：expand 的 `ClassTree.ext_named`、inst_class 的 extends 处理、inst_component 类型链循环与 `Component.chain_layers`、`apply_class_mod`、instantiate 的 refresh、flatten `find_scode_element_at` 的 pkg_instance 分支。第五版实际用到其中的 ext_named、inst_class 的 extends 处理、instantiate 的 refresh、find_scode_element_at；另有两处超出 todo 字面，报用户批准（§7）：F7 要改 instantiate 的 cmods 支并给 inst_class 加 cmods 形参；S0 改 inst_class 的循环次序。`chain_layers` 与 `apply_class_mod` 的改动不再需要（F3 删去）。

**F7：extends 修饰里的类修饰在实例化基类之前挂（第五版新增；F1 / F5 / F6、CM-2、RICM 都建在它上面）**
- 暴露：
  - 基类里早已实例化、类 extends 了被改类的组件：RVC2（CM-2 形态）、RVC3（const15 形态）、RV20（底线 FAIL）；
  - 基类里经短类别名声明的组件：A68fCM20、EQ05、PX04、A68fYLP01 / 02、RV22（第四版 F3 的范围）；
  - 外层 extends 改继承了两层的类：RV17、RV19、RV21、RV22；
  - 基类里经被改类查名、读到陈旧包实例：A68fXST04-06。
  - 底线做法：extends 修饰里的类修饰在 `instantiate(base)` 返回之后才挂（inst.cpp:1556-1581），再用 `apply_class_mod`（:1630-1647）补 `type_node == cls` 的组件；别名链、extends 链上的组件和已建好的包实例都补不到。
- OMC：§3.4-10（NFInst.mo:1163 先于 :1181-1190；:1395 内层先、:1421 本层后；NFClassTree.mo:492-556 共用槽）。
- ymc 落点（inst_class 的 extends 处理 inst.cpp:1500-1582；instantiate 的 cmods 支 :1311-1318）：
  1. 实例化基类之前，收这条 extends 的 `ext_cmods`，外层在前：
     - 外来的：本实例收到的 cmods 里，目标不是本类本地类、而是经这条 extends 继承来的类（`member_def(ext_bases[k], 名字)` 是类；几条 extends 都有时取第一条，同 adopt 的先到先得）；
     - F1：`class_mods[ext_named[k]]` 各条目里的类修饰子修饰；
     - 本条 extends 修饰里的类修饰子修饰：名字在基类里是类（用 `member_def` 判；继承来的组件不进定义树的 lookup，inst.cpp:922，不能靠 lookup 判组件），并过底线 :1557-1575 的同一组核查（施加过的类重声明跳过、`plain_class_mod`、final），不过的照旧 SKIP。
  2. `instantiate(base_def, inst_node, ext_redecls, &ext_cmods)`：
     - cmods 支把目标是本地类的挂到克隆上。`class_mods` 外层在前，改为按传入次序追加（底线是头插；组件修饰路线同名只有一个，次序不受影响）；
     - 其余的交给 `inst_class`（加一个 cmods 形参），由它继续往对应的 extends 转发。
  3. 删掉 :1556-1581 的事后挂和 :1580 的 `apply_class_mod` 调用。`apply_class_mod` 本身留给 inst_package 的短类修饰路线（:511，底线不动）。
  4. 第 1 步里，目标名在 `base_redecls`（外来的类重声明 + 本类的元素级类重声明）里的不收，即 F5。
  5. 附带：CM-3 / CM-4 的门对底线 plain 形态同样放开（§6.4）。
  6. 第六版（N4，复评 RW3）：instantiate 的 redecls 支按 `node` 拷出顶替节点 `rid` 时（inst.cpp:1268-1271），把 `class_mods[node]` 与 `class_modded` 一起拷到 `rid` 上。类修饰按节点 id 记，不拷就丢在原节点上。omc 依据见 §3.4-8 末：新类带着重声明元素自己身上已合并的修饰（NFInst.mo:1735-1737、:1787）。
     - 精确性：中间层的元素级重声明（D1 的 `redeclare model B = B5`）是 D1 的本地类，外层转发来的 `B(i = 6)` 在 D1 实例化时挂到它的克隆上（cmods 支，本地类），D1 再把它作为类重声明传给 A（`base_redecls`），A 拷出 rid 时带上 → b 的类型链读到 6。与 omc 的"先并修饰、后换节点"一致。
     - 不影响 F5：RW4 的类修饰挂在 D1 的 extends 修饰里（内层），目标名在 D1 的 `base_redecls`（外来的 `B → B5`）里，F5 先丢了，rid 由修饰里的重声明元素生成，身上没有类修饰 → 5。
     - 影响面（定稿，第三次核对）：组件修饰路线的类修饰也挂在中间层重声明元素的克隆上（cmods 支，本地类），同样随 rid 带过去。RW7（`D1 d(B(i = 6))`，D1 里元素级 `redeclare model B = B5`）omc `d.b.i = 6`、底线 5；RW8（`d(B(k(min = 0)))`）omc 打 `d.b.k(min = 0.0)`、底线没有 → 都 FAIL→PASS（列名）。RW5 / RW6（经 `B.i` 取值）底线 SKIP 在 "lookup of redeclared class"，须 PASS 或 SKIP。
- 为什么一致：挂的时机与 omc 相同（实例化任何组件、任何查名之前），组件经类型链（inst.cpp:2037-2052）、extends（F1）、包实例（CM-4）读到的都是挂好的修饰；转发对应 omc 继承类与基类共用槽。
- 改动边界：只改 inst_class 的 extends 处理、instantiate 的 cmods 支（追加次序 + 其余转发）与 redecls 支（rid 带类修饰）、inst_class 加 cmods 形参、CM-3 / CM-4 的门；inst_component、类型链循环不改。
- 改变的底线输出（FAIL → PASS，均已对 omc 核过）：A68fCM20、EQ05、A68fYLP01、A68fYLP02、RV22、A68fXST04、A68fXST05、A68fXST06、RW3、RW7、RW8；与 S0 一起：OR1。
- 已知风险：底线里"extends 类修饰带短类代码不收的写法（如组件修饰带子修饰）、又经被改类查名"的模型，底线碰巧 PASS（读的是未改的包实例），预挂后撞 CM-4 的 SKIP。
- 退路（用户批准，todo）：§12 b 若出现名单外的底线变化：
  - 先退到**只对新写法预挂**：CM-2 的嵌套形态、RICM 的 redecl-only 照预挂，底线 plain 形态恢复事后挂（:1556-1581 + `apply_class_mod`），CM-3 / CM-4 的 plain 门随之撤回；
    - 第 6 步（rid 带类修饰）对 plain 形态的拷贝也一起撤：rid 只拷新形态的条目，拷完没有条目就不标 `class_modded`。否则 rid 被标 class_modded 而 plain 门已关，经它查名撞 :631-633 / :441-443 变 SKIP；
    - 这时 F7 改变的底线输出（CM20、RW3、RW7、RW8 等）回到 B0，RVC3 这类 const15 放开要按 §6.7 处理；
  - 仍不行 → 整项撤，连带见 §6.7 撤项。

**F1：extends 一个挂了类修饰的类节点（实例路线；第四版改为只看直接点名的节点，含丙-2；第五版类修饰子修饰改走 F7 预挂）**
- 暴露：
  - ModClass4 / CM-1 形态里 A 有 `model B3 extends B; B3 b3;`（A68fXSB02，omc `a.b3.c.x = 1.0`）；
  - extends 修饰形态（A68fXSB03、RVC2、RVC4、RVC5）；
  - const15 形态加 `model E extends B; E e;`（EQ04 / EQ06、RVC3 的 e0）；
  - 经短类别名被 extends（丙-2：Q02、Q04）。
  - 不补则 b3 / e 静默拿到未改的类。
- OMC：§3.4-6。依据是 NFInst.mo:1391-1392（被点名节点的修饰在外）、:1421（施加到 `Class.classTree`，短类取基类的树，NFClass.mo:345）、:1475-1517（`instExtends` 不再读修饰）。被点名的是短类时，链下节点的类修饰与短类自带修饰都不读（Q01 / Q02 / Q03 / Q08）。
- ymc 落点：
  - expand 解析 extends 时（inst.cpp:885-921），`ClassTree` 加平行数组 `ext_named`，记下 :908 `extends_long_class` 顺链之前、extends 名直接查到的节点；
  - inst_class 处理 extends 时（inst.cpp:1500-1582），若 `ext_named[ext_i]` 在 `I_.class_modded` 里，把 `class_mods[ext_named[ext_i]]` 当这个基类实例最外层的修饰，按条目拆三份：
    - redecl-only 子修饰：在 :1532 `collect_class_redecls(ext_mod, …)` **之前**收进 `ext_redecls`（collect 不覆盖已有名字，先收者胜）；
    - 类修饰子修饰（nested、plain）：进 F7 的 `ext_cmods`，排在本条 extends 子句自己的类修饰之前（外层），随 `instantiate(base_def)` 预挂；
    - 组件子修饰（const-literal、plain 绑定）：在 :1547 施加 `ext_mod` **之后**，再对基类组件 `apply_extends_modifier`（绑定后写者胜，子修饰头插为外）。组件修饰事后施加与底线对 extends 子句组件修饰的做法相同。
  - **多条条目的次序（第六版，N2）**：F7 改追加后，`class_mods[ext_named]` 是外层在前（RV17：`[B(i = 6), B(i = 4)]`）。三份的取法：
    - 组件子修饰按**逆序**施加（内层先、外层后）：`apply_extends_modifier` 是后写者胜，逆序才让外层的 6 盖过内层的 4（RV17 / RV19 / RV21 omc 取 6）；
    - 类修饰子修饰与 redecl-only 子修饰按**正序**收（外层先）：`ext_cmods` 外层在前、`collect_class_redecls` 先收者胜，正序才让外层胜；
    - 与 omc 一致：`modifyExtends` 内层先施加、外层后施加，后施加的在外（§3.4-10）。
  - 只看 `ext_named`，**不看**链底长类 `ext_bases` 上的类修饰。底线本来也不读链下的类修饰，所以 Q01 / Q08 / RV10 不变。
- 前提：`ext_named` 恰是挂了修饰的那个克隆节点。克隆在实例里展开时，基类名按实例树查（inst.cpp:885 `lookup_class_name(…, name_scope(…, cls))`），拿到的是克隆；F7 保证查名之前修饰已挂好；克隆复制了定义层旧展开的情形由 F6 保证重展开（丙-4）。
- 改变的底线输出（FAIL → PASS，均已对 omc 核过）：A68fXSB01、EQ04、EQ06、PX01、PX02、Q04、RV11、RV12、RV15、RV17、RV18、RV19、RV20、RV21；S0 在时还有 Q05（S0 让 `A.E pre` 晚于 extends 实例化，A 实例里的 E 克隆不再陈旧）。
- 依赖：RVC2 / RVC3 / RV17-RV21 要 F7。阶段：S1a，与 F7 / F5 同一阶段（§6.7）——F7 的 CM-3 / CM-4 plain 门本身就放开底线 SKIP 的 const15 形态（const15、CMY 系列、RVC3），F1 不同时上，RVC3 会先出 FAIL（`x = B.i` 取 4、`e0.i` 仍 3）。
- 放开模型里应得的结果：Q02 由 CM-1 放开。F1 不施加（被点名的 B2 没有类修饰），得 `Real a1.b3.c.x;`，与 omc 一致。

**F2：flatten 经限定名进一个"基类挂了类修饰"的类（值路线）**
- 暴露：const15 形态加 `model E extends B; end E; Real y = E.i;`（EQ09、CMX14，omc 4.0，底线 3.0）。flatten 在 `find_scode_element_at` 对实例层的类克隆 E 走 SCode 定义层下降，取 B 定义里的 3。
- OMC：NFLookup.mo:767，进 E 先 `instPackage(E)`，E 的基类带修饰（同 F1）。
- ymc 落点：
  - flatten.cpp:15738-15781（`find_scode_element_at` 的实例层分支）：命中的类克隆 ic（不是短类包、不是被顶替的）的 extends 直接点名的节点（`ext_named`，递归到它们各自的 extends）里有 class_modded 节点时，同"实例里的本地短类包"那支，设 `node_out = ic` 并返回定义；
  - 调用方（`resolve_qual_const` :9070-9100、`eval_qual_path` :9119-9140、`resolve_class_path` :16268-16280）随之走 `pkg_instance(ic)`；
  - inst 侧由 F1 施加。
  - 需要 Instantiator 加一个只读查询 `bool bases_class_modded(NodeId cls)`：必要时展开 cls，查 `ext_named`（递归）。与 F1 同一口径：经短类别名点名、只有链下节点挂修饰的不触发（Q01 的 `y = E.i` 仍取 3，底线 PASS 不变）。
- 改变的底线输出（FAIL → PASS）：EQ09、CMX14、RV13、RV16（RV13 / RV16 的 `e` 由 F1 改对，`y = E.i` 由 F2 改对）。
- 依赖：F1（不依赖任何放开）。阶段：S1b。RV34（`E.f(time)`、`E.T t`）底线 SKIP 在 "qualified function call"，F2 不碰函数路线，照旧 SKIP。

**F3：第五版删去**
- 事后挂改成预挂（F7）后，经短类别名声明的组件在实例化时经类型链（inst.cpp:2037-2052）直接读到修饰；第四版列的 A68fCM20、EQ05、A68fYLP01 / 02 归 F7。
- `Component.chain_layers` 不加，`apply_class_mod` 不改（只剩 inst_package 短类修饰路线用它，底线行为）。

**F4：被丢弃属性的核查（第五版按评审收窄：只核字面量与 startOrigin，数组不进；第六版只核胜出来源）**
- 底线：plan_attributes 的 `droppable`（flatten.cpp:7659-7662：Real 的 `unbounded` / `startOrigin`、String 的 `fixed`）只核名字、不核值，属性直接丢弃。
- OMC：
  - 修饰先逐层合并：`Modifier.merge` 外层绑定胜，被盖掉的内层绑定不再出现（NFModifier.mo:402-408，绑定取舍在 :405-406）。之后 `typeTypeAttribute`（NFTyping.mo:1250 起）只对合并后的那个绑定 `typeBinding` + `TypeCheck.matchBinding`（:1288-1289）。`unbounded`、String 的 `fixed` 是 Boolean（NFBuiltin.mo 的属性表）。
    - 所以被外层盖掉的内层字面量不定型：RF1（`type T = Real(unbounded = 1); T x(unbounded = true)`）、RF2（extends 修饰盖掉）、RF3（组件修饰盖掉）omc 收，底线 PASS；
    - 胜出的那个是非 Boolean 字面量就拒：RF8（类型里 true、组件上 2）、RF9（extends 修饰给 3）、RF4（`fixed = 2`）、RF5（只有类型上的 `unbounded = 1`）omc 拒，底线 AI。
  - `startOrigin` 不在 v1.25.0 NFBuiltin.mo 的 Real 属性表里（quantity / unit / displayUnit / min / max / start / fixed / nominal / unbounded / uncertain / stateSelect）。Real 的类树是 FLAT_TREE（NFBuiltin.mo:165；REAL_NODE :209），合并后的修饰在 `applyModifier` 的 FLAT_TREE 支里找不到元素 → `MISSING_MODIFIED_ELEMENT`（NFInst.mo:1547）。哪一层写的都一样，无值的也一样：RV48、RF6（`x(final startOrigin)`）、RF7（类型与组件两层都写）omc 都报 "Modified element startOrigin not found in class Real"，底线 AI。
- ymc 落点（plan_attributes；不在 :7670-7689 的校验循环里做——那个循环遍历所有来源，会核到被盖掉的内层值）：
  1. `startOrigin`：任何一个来源里出现这个名字的子修饰（有值无值、不论维），经 `Instantiator::report_error` 报真错误、退出 1。放在校验循环里、"无值无子修饰就 continue"（:7682）之前判。MSL 与 testsuite 的 flattening 用例里都没有 startOrigin（已 grep，只有 simulation 下一个 .mos）。
  2. `unbounded` / String 的 `fixed`：校验循环之后，照属性值的选法（:7695-7702：sources 按优先级从高到低，取第一个带值的同名子修饰）只取**胜出来源**的那个值来核。只在组件是标量（dims 为空）时核：值是字面量（IntLit / RealLit / StringLit，或数值字面量取负）→ 类型确定不是 Boolean → 报真错误；Boolean 字面量 → 照旧丢弃。
  3. 胜出值不是字面量的一律照底线丢弃，不 SKIP、不报错、不定型：cref、调用、表达式、枚举字面量。RV40（`unbounded = b`）、RV43（`= f()`）、RV44（`= n > 1`）、RV49（`= e == E.a`）、RV45 / RV46 底线 PASS 必须不变。原因：给它们定型要走 `inst_exp`，有副作用（RV43 的 `f` 会被当成引用到的函数发射出来，omc 丢弃属性后输出里没有它；不认识的写法会置 SKIP）。
  4. 数组组件（dims 非空，含 each）不进第 2 步，照底线丢弃：RV41（`x[2](unbounded = {true, false})`，omc 收）、RV42 保持 PASS；RV47（`x[2](unbounded = true)`，omc 报 MODIFIER_NON_ARRAY_TYPE_ERROR）保持 AI，记 KNOWN。理由：plan_attributes 现有的数组形状核查（:7720-7760）对不合的情形报 SKIP，套到被丢弃属性上会把底线 AI 变 SKIP，不在规则 (a) 允许的变化里。
- 无副作用：第 2 步只看胜出来源那个 Absyn 字面量的种类，不建表达式、不进 arena、不置 unsupported。
- 控制组（底线 PASS、omc 收，必须不变）：RF1、RF2、RF3、RV40-RV46、RV49。
- 改变的底线输出：A68fTAU01-04、A68fTAU06、RV48、RF4、RF5、RF6、RF7、RF8、RF9，AI → 两边都拒（列名，§6.7）。
- 不含（仍 KNOWN）：A68fTAU05（`unbounded = p`，p 是 Real 参数：cref 不定型，见第 3 步）、RV47（数组）、A68fUTU02 / 04 / 05 / 08（函数变量那条路，不消费本桶数据）。

**F5：类修饰挂到被外层重声明顶替的类上（第五版：规则放进 F7 第 1 步）**
- 暴露：const15 形态里 B 是 replaceable 且被外层重声明（派生类元素级 `redeclare model B = B5`，或组件修饰 `A2 a2(redeclare model B = B5)`）。预挂后若不丢，类修饰会挂到顶替进来的节点上，`B.i` 放开后取修饰值（omc 丢弃）。
- OMC：§3.4-8（NFInst.mo:1636、:1735-1737、:1687；NFModifier.mo:419）。OR18（元素级重声明 → 丢，`e.i = 5`）；OR17（外层类修饰盖在内层 extends 修饰里的重声明上 → 带到新类，`e.i = 6`）。
- ymc 落点：F7 第 1 步收 `ext_cmods` 时，目标名在本层 `base_redecls`（外来的类重声明 + 本类元素级类重声明）里的类修饰丢弃。来自基类内部的顶替（更内层 extends 修饰里的重声明，OR17）不在本层 `base_redecls` 里，照转发、在那一层顶替之后挂到新类上（instantiate 先做 redecls 再做 cmods）。
- 改变的底线输出（FAIL → PASS）：A68fYLP03、A68fYLP04。
- 依赖：与 F7 同在 S1a 实现（§6.7）。

**F6：挂了类修饰的实例里，受影响的本地类克隆在实例里重展开（补丙-4；第五版改触发与范围）**
- 暴露：放开的模型里，某个本地类此前已被 ymc 的定义层查名展开过（例：`A.E pre;`、`A.B3 pre;`、`X.E pre;`）。它克隆进挂了类修饰的实例时，复制的是定义层的旧展开，基类指向定义节点，F1 / F2 看不到实例上的类修饰，静默错。
- OMC：§3.4-9（NFLookup.mo:700 / :767 → NFInst.mo:1360；NFClassTree.mo:518-524；NFInstNode.mo:1920-1935）。实测 Q05 / Q06 / Q07。
- ymc 现状：
  - 定义层查名直接展开定义节点：`lookup_class_name` 的 `partial_inst` / `expand`（inst.cpp:642-645），以及按定义节点实例化组件类型时 `instantiate` 开头的展开（:1229-1230）；
  - `instance_class` 复制已展开的类（:1177-1187）；
  - 只有带类重声明的实例，才对"非 settled 的已展开定义"重展开（`refresh`，:1254）。
- ymc 落点（inst.cpp:1254 `refresh` 与其后的本地类循环）：
  - 名字集 N = 本实例收到的 cmods 的目标名（本地挂的 + 要转发的，F7）∪ def_node 自己各条 extends 修饰里类修饰子修饰的目标名。
  - N 非空时，本地类里类体首段写到 N 中某个名字的（复用 `mentions_any`，inst.cpp:1332 起：组件类型、extends 名、短类基类、嵌套类递归），以及写到这些类名的（迭代到不再增加，至多本地类个数轮），克隆时重展开；`settled_classes_` 里的不重展开（与 omc 一致）。其余本地类照旧共用定义层展开。
  - 与第四版的区别：
    - 第四版"基类 extends 修饰带类修饰 → 传标记"一条，F7 之后就是"收到非空 cmods"，不再加形参；
    - 不再对全部本地类重展开，只重展开会受影响的，免得 MSL Media 这类包的无关本地类（函数、记录）换成实例里的展开，改了常量的取值路线（65e 还没做）。
- 改动边界：只改 `instantiate` 里 `refresh` 的取值（按类判）；`instance_class` 不改；重展开走现有的 `expand`。
- 改变的底线输出（FAIL → PASS）：Q07；S0 放弃时还有 Q05（S0 在时 Q05 由 F1 改对，见 F1）。
- 依赖：F7（cmods 里含转发来的 extends 类修饰）、F1（重展开后 extends 读类修饰），不依赖任何放开。阶段：S1c。
- 退路：§12 b 差分里 F6 改了名单外的底线输出，或 S1c 的 MSL 专项不过 → 撤 F6；放开模型里"本地类先被定义层展开"的写法在放开条件里排除、保持 SKIP（与规则 (c) 冲突，报用户定）。

**丙：补不了 / 需用户定**

1. **PP 族**：第三版已改为本桶做，方案见 §6.8（阶段 S5）。第二版"交 65c"的结论作废。
2. **extends 链经短类、中间节点挂类修饰**：第四版已补，见 F1。按 omc，只读被直接点名节点上的修饰（§3.4-6 丙-2）。
3. **包缓存陈旧本身（A68fXST04-06，底线 FAIL）**——第五版已由 F7 补齐（先挂后查，CM-3 / CM-4 收 plain 形态），下面第四版的分析作废：
   - 放开模型走不到（甲）；
   - 修它要把 extends 类修饰挂到实例化基类之前，并跨继承层下传（XST04 的 B 是基类的基类里的类），改所有底线 plain extends 类修饰的时机。
   - 建议：保持 KNOWN。
4. **类克隆复制了定义节点已展开的树**：第四版已补，见 F6（§3.4-9 丙-4）。
5. **A68fUTU 族**（函数变量类型链上的被丢弃属性 / `startOrigin`）：不经放开数据，属底线老错，KNOWN。A68fTAU01-06 已由去掉门控的 F4 补齐。

### 6.7 改到共用路径的部分、规则 (a)(b) 与分阶段实现

| 位置 | 新代码 | 进入条件 | 底线在该条件下 |
|---|---|---|---|
| S0：inst_class 的循环（inst.cpp:1500-1595） | 先全部 extends、后本地组件 | 所有类 | 只在"本地组件写在 extends 前、用到被 extends 修饰改过的继承类"时影响结果，底线多为 SKIP 守卫或老错（§6.9） |
| flatten.cpp:5945 / :5953、:7725（B-1 / B-2） | 不定型 break | cond_B / cond_B2 | 必 `inst_exp(Break)` → SKIP |
| inst.cpp:1673-1677（R） | 清 `extends_mods` | 非空且 cond_RA | 必在 :1806 SKIP |
| inst.cpp:2205-2213（CM-1） | 收新形态类修饰 | 非 plain 且 `redecl_only_class_mod` + cond_CM1 | flatten 走到时 :7543 必 SKIP；走不到的三种由条件排除 |
| F7 门（CM-2 / RICM） | 嵌套 / redecl-only 形态进 `ext_cmods` | 对应谓词 + cond_CM2 | 必在 :1574 SKIP |
| inst.cpp:631-633（CM-3）、:441-443（CM-4） | 改走 `inst_package` | redecl-only / const-literal；第五版加 plain | 新形态必 SKIP；plain 形态见 F7 已知风险 |
| inst.cpp:448 后（CM-5） | 读链上节点的新形态 | 链上有新形态 | 新形态只在放开的模型里存在 |
| F7：inst.cpp:1500-1582 + :1311-1318 + inst_class 形参 | 类修饰预挂、转发 | 所有 extends 类修饰 | 底线有输出的会变，只许变成与 omc 一致 |
| F1 inst.cpp:885-921 + 1500-1582；F2 flatten.cpp:15738-15781；F5（F7 第 1 步）；F6 inst.cpp:1229-1256 | 老路线补齐 | 底线**有输出**的模型也会进入 | 同上 |
| F4 flatten.cpp:7659 起 | 被丢弃属性的核查 | 任一层带 startOrigin，或标量的 unbounded / String fixed 胜出值是字面量 | 底线 AI 变两边都拒（TAU01-04 / 06、RV48、RF4-RF9） |
| inst.cpp:624 / :2184 / :1675 / inst_component / 根实例末尾（P） | 挂起、丢弃、flush、组件栈 | 撞 partial 且根阶段栈非空 | 必 SKIP |
| S5：PP（§6.8） | 进类钩子、检查实例、表达式遍历、根路径 RELAXED | 经限定名进类 | 底线 AI 变两边都拒（47 个）；其余不许变 |

**规则 (a) 例外清单**（照 OMC 移植、变化后与 omc 逐字节一致的才允许；逐个列名记账；底线均为 ymc_base_5c7 实跑）：
- **FAIL → PASS（34 个）**：
  - S0 + F7：OR1；
  - F7：A68fCM20、EQ05、A68fYLP01、A68fYLP02、RV22、A68fXST04、A68fXST05、A68fXST06、RW3（第六版，N4）、RW7、RW8（定稿，N4 的组件修饰路线）；
  - F5：A68fYLP03、A68fYLP04；
  - F1：A68fXSB01、EQ04、EQ06、PX01、PX02、Q04、RV11、RV12、RV15、RV17、RV18、RV19、RV20、RV21，以及 Q05（S0 在时）；
  - F2：EQ09、CMX14、RV13、RV16；
  - F6：Q07（S0 放弃时 Q05 也归 F6）。
- **ACCEPTS-INVALID → 两边都拒**：
  - F4：A68fTAU01-04、A68fTAU06、RV48、RF4、RF5、RF6、RF7、RF8、RF9（第六版加 RF4-RF9）；
  - S5：§6.8 列名的 47 个。
- 三语料里另有被这些改动的：逐个对 omc，变化后与 omc 一致（PASS，或两边都拒）才算，补进账；有一个不是 → 撤掉那一项（连带撤项见下）。
- MSL 底线 0 FAIL、0 AI：S0、F 系列与 S5 若改动任何 MSL 输出，必是 PASS→其它，按退路撤。

**分阶段实现**（让 §12 b 能把变化归到具体一项；每个阶段、子阶段都对上一步的构建做一次差分）。主代理定的顺序：老路线补齐全部排在放开之前；F1 并进 S1a（定稿），因为 S1a 的 CM-3 / CM-4 plain 门本身就放开底线 SKIP 的 const15 形态。

| 阶段 | 内容 | 对谁差分 | 退路 |
|---|---|---|---|
| S0 | 实例化序（§6.9） | B0 | 整项撤，再问用户是否改窄做法（用户批准的退路） |
| S1a | F7（预挂、转发、rid 带类修饰、CM-3 / CM-4 的 plain 门）+ F5（F7 第 1 步里的丢弃判断）+ F1 | S0 | F7 先退到只对新写法预挂（连同 rid 拷贝的 plain 部分），仍不行整项撤（用户批准）；F1 单撤见下 |
| S1b | F2 | S1a | 只撤 F2 |
| S1c | F6 | S1b | 只撤 F6 |
| S1d | 放开 B、R、CM-1、CM-2（嵌套形态进 F7 门）、CM-3、CM-4、CM-5 | S1c | 按 e 在放开入口收窄 |
| S2 | F4 | S1d | 只撤 F4 |
| S3 | RICM（redecl-only 形态进 F7 门） | S2 | 整项放弃 |
| S4 | P 组 | S3 | 整项放弃 |
| S5 | PP 族（依赖 S4 的组件栈 / pending，S4 放弃时 S5 自带，见 §6.8） | S4 | 整项放弃 |

**逐阶段核"放开模型底线 SKIP → FAIL"**（定稿重核）：
- S0：不放开任何东西。守卫拦住的 SKIP 会先变老错（OR7 / OR19：同一模型把本地组件挪到 extends 后，在 B0 上就是这个 FAIL）——不是放开模型，照豁免；F1 在 S1a，S1a 结束应清零。
- S1a：plain 门放开 const15 形态，F7 / F5 / F1 同时在位，RVC3、XST04-06、const15 这类不出 FAIL。仍有两类要等后面的子阶段（已实跑核过底线 SKIP、omc 值）：
  - 同时经 `B.i` 和 `E.i` 取值（G1：`x = B.i`、`y = E.i`，omc 都是 4）：`y` 要 F2，S1b 才对；
  - 定义层先用过本地类、又经 `B.i` 取值（G2：`A.E pre; A a1(B(i = 4));`，A 里 `E e; Real z = B.i;`，omc `a1.e.i = 4`）：`a1.e` 要 F6，S1c 才对。
  - 这两类照豁免：只记账、不撤；S1c 结束时必须清零。也可以把 F2 / F6 一并并进 S1a 免掉豁免，实现时任选，§12 b 的清零点不变。
- S1b / S1c：只补老路线，不新放开。
- S1d：F7 / F5 / F1 / F2 / F6 都已在位，放开的模型只许变 PASS / 两拒 / SKIP。
- S2：F4 只改被丢弃属性，不放开。
- S3：RICM 放开 redecl-only 形态，F1 已在位（RVC4 / RVC5 的类 extends 由 F1 收到重声明）。
- S4 / S5：退路本来就包含"底线 SKIP 变 FAIL / AI / CRASH"。
- 清零点：S1c 结束时对 B0 复核，上面三类（OR7 / OR19、G1 类、G2 类）以及其它任何"底线 SKIP → FAIL"必须全部清零，否则按 e。

**依赖与连带撤项**：
- F4：独立。
- F5：与 F7 同在 S1a。只撤 F5 的丢弃规则时，YLP03 / 04 的输出若与 B0 不同（挂到了顶替进来的节点上），按 F7 一起处理。
- S0：撤 S0 → 再问用户是否让 S4 / S5 改用 §6.9 的窄做法；OR1 回底线 FAIL；Q05 改由 F6 补。
- S1a 内部：F7、F5、F1 同一阶段，差分出问题时按项定位（例外清单按项列名）；撤 F7 见下，单撤 F1 见下。
- F7：CM-2、RICM、F1 的类修饰子修饰、F5、F6 的触发都建在它上面。先退到只对新写法预挂（用户批准），rid 拷贝里 plain 形态的部分一起撤：这时 F7 列名的底线变化回到 B0，S1d 里 CM-4 的 const15 放开要排除"基类里有已实例化的、类 extends 或经别名用到被改类的组件"的写法（报用户定）。整项撤 → 连带撤 CM-2、RICM、F5（ModClass6、RICM 两个目标退出）。
- F1：F2 的 inst 侧、F6 的 Q05 / Q07 都靠它。撤 F1 → 连带撤 F2、F6；S1a 的 plain 门放开的 const15 写法里、以及 S1d 里"本地类 extends 被改类"的放开写法要在放开条件里排除（报用户定）。
- F2：撤 F2 → S1a / S1d 里经 `E.i` 这类限定名取值的放开写法要排除（报用户定）。
- F6：见 §6.6 F6 的退路。
- "排除写法"都与规则 (c) 冲突，所以凡要排除的都报用户定。

### 6.8 PP 族：照 omc 的整包实例化补 partial 检查（阶段 S5；用户 2026-11-11 定本桶做）

**目标**：底线 ACCEPTS-INVALID 的 PP 族改成和 omc 一样拒绝；底线有输出且 omc 照收的一个不变。

**落点**：实例层，用 ymc 真实的实例化，**不在 flatten 的 SCode 层近似**。旧 68f 第五、六轮在 SCode 层扫描：
- 重声明按名字记、传给所有基类；
- 扫到 omc 根本不进的类；
- 结果连着两轮 PASS→SKIP 退步（A68fPT / PU / PV 族）。

**PP-A：进类钩子 `Instantiator::pp_enter(NodeId cls, PpWhy why)`**——一一对应 omc 的每一次 instPackage 调用（§3.5-1）。
- 语义：
  - cls 经短类链到长类；
  - 若 partial（且不豁免）→ PP 错误。这一步底线的多数入口已有 SKIP，保持原样；只有新增入口才新报。
  - 否则，若 cls 不在 `pp_checked_` → 记入，建检查实例（PP-B），在上面跑 PP-C。
  - 发现的 PP 错误按 PP-E 报。
- `why` 区分四类，各有不同豁免：
  - 类名路径：自引用第一步豁免；
  - extends 基类名：自引用第一步豁免，另外 PartialModelicaServices 免查；
  - cref / 函数名：无豁免（VPQ06）；
  - import。
- **调用点**（新代码只调钩子，原查找结果、返回值、原有 SKIP 都不改）：
  1. inst `lookup_class_name`（inst.cpp:597-660）逐段进类处（i ≥ 1 的 `entered`）；expand 的基类名调用（inst.cpp:885）带"基类名"标记。
  2. inst 的 import：
     - `import_class`（inst.cpp:195 起）解析限定 import 时，路径上的各类；
     - 类展开（`partial_inst` :656 起 / `expand` :850 起）时，对本类的非限定 import 目标立即调。NO_CONTEXT，不挂起。
  3. flatten 经限定名进类的各条路线：`resolve_qual_const`（:9062）、`eval_qual_path`（:9112）、`resolve_class_path`（:16263）、`resolve_fn`（:16451，函数名）、`import_lookup`（:8967，限定 import）。第五版：这五处只往队列里记，不当场检查（PP-G）。每进一个类：
     - 实例层的类 → 用 `node_out` 给出的节点；
     - 定义层的类 → 记定义路径，flatten 结束后再用 `instr_->class_def_node(定义路径)`（inst.h:46）取定义节点（它会 expand，不能在 flatten 期调）；
     - 都经 `instr_->pp_enqueue` 记队列，flatten 结束后由 `pp_enter` 检查。
- **自引用规则**（只用于第 1 类调用点的第一步）：首段经词法简单名查找（不经 import）找到的类，其定义元素就是书写处所在的类或它的某个词法外层类 → 豁免。
  - 依据：NFLookup.mo:597 / :608-625；类型在定义节点上展开（短类 `expandClassDerived` 在定义节点上做，scope 为定义节点自身）。
  - 行为以探针为准：ZSR01-06 / WSS01-06 豁免；WSB01 经 import 不豁免；VSR02 / 03、VSW04 第一步豁免、下一步照报。
  - omc 节点相等的确切机制待查（§19-8）。

**PP-B：检查实例**
- 用现有 `instantiate`（inst.cpp:1210 起）实例化 cls：定义层用定义节点，实例层用那个节点。走的是 ymc 真实的实例化，extends 修饰、类重声明、短类修饰都照常生效。这正是避开旧 68f "按名字记重声明"误判的关键（VPT / UPU / UPV 族底线 PASS、omc 收）。
- 检查模式下，组件类型路径撞 partial（inst.cpp:624、:2184）记为 PP 错误，不是 SKIP。
- 新增 `checkPartialComponent` 等价（NFInst.mo:2000-2002 / :2120-2125）：检查模式下 inst_component 遇到组件类型解析到的类（链底）是 partial、组件不是 `outer`、类型不是函数 → PP 错误。只在检查模式下做；模型里的组件仍由 flatten 的 `bad_type_path` 管，底线不变。
- **沙盒**：检查实例化对全局状态的写入，结束时恢复，保证不改正常查找的结果。
  - 回滚（进入时保存、退出时还原）：
    - inst 层：`pkg_cache_`、`I_.package_instances`、`I_.applied_redecls`、`I_.applied_class_mods`、`I_.class_mods`、`I_.class_modded`、`I_.redeclared_classes`、`I_.replaced_in`、`I_.redecl_cc`、`I_.final_class_mod_comps`、`I_.dup_kept`、`I_.class_extends_target`、`I_.class_extends_via_mod`；
    - Instantiator：`settled_classes_`、`short_settled_`、`instances_`、`inst_redecls_`、`redecl_orig_`、`redecl_class_nodes_`、`mod_redecl_nodes_`、`unsupported_` 及原因；
    - 检查中被 `partial_inst` / `expand` 改过状态的**定义节点**的 `Class::state` / `tree`（改动前记下）。`instance_class` 依据定义节点状态决定克隆时是否重展开（inst.cpp:1177-1187），不还原会改变之后的克隆。
  - 第五版补（评审第 6 条）：
    - `synth_cap_`（inst.cpp:144：`elems.size() + 4096`，判在 :667 / :1794 / :2354）：SCode 合成元素只增不减、按键缓存（`ce_ext_elem_`、`redecl_elems_`、`dims_elems_`），而且超出 reserve 的 add 会搬家、让各处持有的 SElement 引用悬垂，不能回滚。做法：检查模式下要合成**新**元素（三处判预算的地方）→ 按预算截断处理（停止这次检查、不报错，走 PP-F 的截断规则）；键命中的已有元素照用。这样检查实例不消耗正常实例化的预算，不会让之后的正常实例化撞 "synthesized element budget"、PASS 变 SKIP。
    - `depth_`（:1214，上限 200）：进入检查时保存、退出时还原（RAII，退出时断言相等）；检查自己的递归深度另计（见下面的预算）。
    - `short_active_`（inst.h:153，through_short_classes 的递归守卫）：进入检查时保存并清空（免得把外层正在解析的短类误判成递归定义），退出时还原。
  - 保留：arena 增长（检查实例不可达）、`pp_checked_`、PP 错误记录。
  - 检查中 ymc 自己未做的构造引发的 `mark_unsupported` 一律丢弃。检查实例不产出结果，丢弃只会让那一块的 PP 检查漏掉（保持底线 AI），不会把底线 PASS 变 SKIP。
  - **预算（第四版定：接受，必须是确定性的）**：
    - 单次检查（含递归进的类与被调函数）新建的实例节点数、递归深度各有上限；超过就停止这次检查、不报错。
    - 计数确定性：数 `I_.nodes` 的增量与递归层数，不按时间。同一输入每次结果相同，与机器快慢、并发无关。
    - 代码里写 `ponytail:` 注释，写明上限数值与升级路径。升级路径：65c 把查找迁到实例树后，包实例本身就会被真正用到，检查与真实实例化合一，不再需要单独预算。
    - 上限按 MSL 实测定：取 MSL 全部模型里单次检查的最大计数，再留余量，写进 §16。
    - 理由：漏报只是保持底线本来的 ACCEPTS-INVALID，不是退步；误报会把底线 PASS 变成拒绝，正是规则 (a) 禁止的。所以宁漏报不误报。

**PP-C：表达式实例化步骤（omc instExpressions 的等价物；inst 里新增的只读遍历，只查名、不定型、不求值、不产出表达式）**
- 范围同 NFInst.mo:2450-2525：本类与各层 extends 基类实例的组件。每个组件看：
  - 绑定（`comp.binding.raw_exp`，作用域 `binding.scope`）；
  - 维（定义的 `attr.dims` / `type.dims`，以及短类链拼上的维，各带书写作用域）；
  - 条件；
  - 内建属性修饰的绑定（声明修饰、extends_mods、derived_mods、cc_mods 里的属性子修饰；A68fUMB09）；
  - 组件的类实例递归（记录 A68fUMB10 / 模型类型的组件）；
  - 本类及递归到的组件类的方程 / 算法段（A68fUMB11）。
- 遇到限定 cref `A.B.c`：首段在书写作用域里按词法简单名查（`lookup_simple_class` / 本地组件，inst.cpp:153 起）。首段是类 → 沿路径逐段 `pp_enter(…, Cref)`；遇到组件段即停。
- 遇到调用：函数名同 cref 处理。解析到用户函数时，照 `instFunction3` 把函数类做一次检查实例化（形参 / 局部的类型路径按 PP-B 查），再对函数的绑定与算法体做 PP-C。函数按节点记入 `pp_checked_`，防递归。依据 PP01、PP05。
- 内建函数、内建枚举（`size`、`StateSelect.prefer`）不进类（PP11）。

- **PP-C 做不到的（第五版补，只会少报、保持底线 AI，不误报）**：
  - 记录构造器：omc 的 instExpressions 对记录类型做 `instComplexType`（NFInst.mo:2523 → :2593-2614）→ `instRecordConstructor`（:2608 → :2616-2639）；派生记录在 EXPANDED_DERIVED 支（:2527-2538，基类 instExpressions :2529、构造器 :2538）。PP-C 不跟进构造器。
  - 外部对象的构造 / 析构函数：`instExternalObjectStructors`（:1237 → :1289-1310）。PP-C 不跟进。
  - modifyExtends 在实例化期按名字重查基类（:1401 `lookupBaseClassName`）：ymc 的基类名只在 expand 时查一次（inst.cpp:885，钩子挂在那里）。omc 第一次进类也在 expand，同一个包第二次进入只命中缓存，所以只在两次语境不同时会少报。

**PP-D：import**
- 限定 import 用到时，路径上逐段 `pp_enter`（A68fVLK11 / VLK14）。
- 非限定 import 在类展开时立即 `pp_enter` 目标（PP03、A68fVLK10），不受 REDECLARED 挂起（NFImport.mo:163）。

**PP-E：报错与 REDECLARED**
- PP 错误报成真错误：新增 `Instantiator::report_error(SourceLoc, std::string)`，转 `diags_.error`。
  - 第四版定：只有判定条件逐条对到 OMC 文件:行的，才报真错误，限下面三种：
    - 进类撞 partial（NFLookup.mo:703 / :772 / :979 → NFLookupState.mo:323-336）；
    - 组件类型本身 partial（NFInst.mo:2000-2002 → :2120-2125）；
    - 上面两种在 PP-C 遍历、被调函数检查里发现的。
  - 拿不准的不报真错误，包括自引用规则以外的边界（§19-8），以及检查中撞到 ymc 未做的构造（预算内丢弃，PP-2）。模型层原有入口的 SKIP 照旧。
  - SKIP 的意思是"这一块 ymc 没做"。这里是照 OMC 移植了同一判定，所以报成"两边都拒"。
  - 驱动按现有逻辑退出 1（main.cpp:318 / :362），得到"两边都拒"。flatten 后若同时有 SKIP，仍以 66 为先（:320-324）。
  - flatten 之后有错误时，驱动不打印模型（driver 归 Claude 写，顺手改）。
- 第六版（N6）：根阶段的 PP 错误一律先记、不当场报（组件栈空的也记），根阶段结束 flush 时统一判；每条带一个递增的事件序号。
- 根阶段、组件栈非空时，PP 错误进 S4 的 pending（加种类字段 = PP）：
  - 被 extends 重声明换掉的原组件里的照丢（PP06、PR01）；
  - flush 时 PP 种类报真错误，不是 SKIP；
  - 非限定 import 触发的不进 pending；
  - flatten 期的进类（cref / 函数名 / flatten 的类路径）在根阶段之后：记队列，flatten 结束后统一检查、直接报（PP-G）。
- 依赖：PP-E 用 S4 的组件栈与 pending。S4 若按退路放弃，S5 自带同一套组件栈 / pending（只收 PP 错误），其风险计入 S5 的退路。

**PP-F：根类路径的 RELAXED 检查（第五版补，评审第 3 条）**
- OMC：§3.5-9（NFInst.mo:347-389，:358 RELAXED，:362 `lookupClassName`；instPackage :1365-1368）。
- ymc 现状：`instantiate_root_path`（inst.cpp:2730-2738）经 `lookup_class_name`（:2732）查根类全名。第四版卡在 lookup_class_name 逐段挂钩子，会按普通语境检查 `Lib.Ex` → RVR 的 Lib.Ex.M / M2 底线 PASS 变拒。MSL 的 500 个都经 `--class=Modelica.…` 进入，这是 S5 整项放弃的直接风险。
- 落点：
  - `instantiate_root_path` 查根类全名期间置 RELAXED 标记（RAII）。期间逐段进的包（根类本身那一段不算，omc 也不对它 instPackage），以及从这些检查实例递归进入的包，都按 RELAXED 检查：记入 `pp_checked_`、不报错、不挂 pending。
  - 单段根类名（`instantiate_root`）没有前缀，不涉及。
- **截断规则**：RELAXED 语境，或在 REDECLARED 的对应物里（根阶段组件栈上有之后被 extends 重声明换掉的组件），检查若被预算截断，没走到的包不在 `pp_checked_` 里，之后普通语境进入会报出 omc 不报的错（omc 那时已把它们缓存）。所以这种截断生效后，它之后的 PP 错误一律不报（宁漏报不误报）。普通语境的截断不影响：没走到的包之后再进入时，omc 同样会报。
- **怎么判、从哪算起**（第六版，N6）：截断当时还不知道栈上的组件之后会不会被换掉，所以：
  - 截断时记一条截断事件：事件序号、当时的组件栈拷贝、是否 RELAXED；
  - RELAXED 的截断当场生效；
  - 其余的截断，在丢 pending 的那一处（inst.cpp:1675 前，§6.3-4）若被换掉的组件 `bc` 在它记下的栈里 → 生效；根阶段结束 flush 时仍没生效的 → 作废（当普通截断）；
  - "之后的 PP 错误" = 事件序号大于这次截断的 PP 错误，包括 pending 里还没 flush 的、根阶段之后队列检查报的。根阶段的 PP 错误按上面一律先记到 flush，所以 flush 时每次截断生效与否都已定。
- 验收：RVR 三例进 `pk-p55r-pp`（Lib.Ex.M / M2 期望 omc 输出，Lib.M3 期望两拒）；MSL 专项 500 PASS 不变（§12 b）。

**PP-G：flatten 期的进类改记队列（第五版补，评审第 6 条）**
- 问题：flatten 的五处钩子若当场建检查实例，会往 arena 加节点；调用方此时若持有 `INode&` / `Class&` / `Component&`，arena 扩容后引用悬垂。现有在 flatten 期加节点的只有 `pkg_instance` 回调（flatten.cpp:16308-16316）一处，五处新钩子的调用方都没按这个前提审过。
- 做法：五处只调 `instr_->pp_enqueue({实例层节点 | 定义路径, why})`，不调 lookup、不 instantiate，flatten 期 arena 不因 S5 增长。flatten 返回后、驱动打印之前，依次取定义节点（定义路径的经 `class_def_node`）并 `pp_enter`；这时已在根阶段之后，都是普通语境，有错直接报。
- 第六版（N5）：队列处理**整段**在检查沙盒、丢弃 unsupported 的模式下跑，只认 PP 错误：
  - `class_def_node` 走 `lookup_class_name`，会 expand，也可能 `mark_unsupported`（inst.cpp:616-617 "lookup into modified short class"、:623-624 "lookup in partial class"）；这些一律丢弃，取不到定义节点的那一条跳过；
  - 驱动在 flatten 之后只看 `fr.unsupported`（main.cpp:318-324），队列处理不得改它，也不得让 `instr.unsupported()` 留下置位；
  - 沙盒退出时同 PP-B 还原各表与定义节点状态，只保留 `pp_checked_` 与 PP 错误。
- 与当场检查等价：flatten 期全是普通语境，检查的集合与先后无关（先检查的记入 `pp_checked_`，后来的命中即跳过，报错集合相同）；只有报错在 stderr 上的次序会变，不比较。
- 验收：ASan（§12 d-3）照常；`pp_enqueue` 只往 vector 里压值，不取任何 arena 引用。

**改动边界（S5 范围，实现不得越出）**
1. 新代码：`pp_enter`、`pp_enqueue` 与队列处理、RELAXED 标记、检查实例沙盒、PP-C 遍历、`report_error`。建议放新文件 `src/inst/pp_check.cpp`，加文件后要重新 configure。
2. 既有函数里只加"调钩子"的一行：
   - inst：`lookup_class_name` 逐段处、expand 基类名处、`import_class`、partial_inst / expand 的非限定 import 处；
   - flatten：`resolve_qual_const` / `eval_qual_path` / `resolve_class_path` / `resolve_fn` / `import_lookup` 进类处（只记队列）；
   - `instantiate_root_path`：RELAXED 标记的 RAII；
   - 驱动：flatten 返回后处理队列、有错不打印模型（driver 归 Claude 写）；
   - 不改这些函数的查找结果、返回值、原有 SKIP。
3. inst_component：只在检查模式下加 partial 组件类型核查；:624 / :2184 只在检查模式下改记 PP 错误（检查模式外维持 S4 之后的行为）。
4. 写到的全局表：
   - 沙盒内的全部回滚（上列）；
   - 持久的只有 `pp_checked_`（节点集）、S4 的 pending（加种类字段）、`diags_`（错误）；
   - arena 只增不减。

**规则 (a) 下底线会变的用例（ACCEPTS-INVALID → 两边都拒，omc 都已核过是拒绝）**
- 旧评审探针（KNOWN 里登记的 24 条 PP 全在内），40 个：
  - A68fXPK04、XPK05、YCP01、YCP02、ZPP01、ZPP02；
  - A68fWPP01-07、WSS07、WSS08、WSB01；
  - A68fVLK01、VLK02、VLK03、VLK06、VLK09、VLK10、VLK11、VLK14、VLK16；
  - A68fVPQ06、VSR02、VSR03、VSW04、VPT10；
  - A68fUPU06、UPU07、UPU09、UPU21、UMB07-12。
- 本卡新探针，4 个：PR02、PP01、PP03、PP09。
- 第 3 步评审探针，3 个：RV01（要 S0：Base2 先于 port 实例化）、RV02、RVR 的 `Lib.M3`。合计 47 个。
- 底线 SKIP → 两边都拒（不受 (a) 约束，照记）：PP05、PP07、PP08，以及 A68fXPK01 / 02 / 08 等。
- 三语料里另有 ACCEPTS-INVALID → 两边都拒的：逐个对 omc（omc 须拒），补记账。
- KNOWN 另 10 条的归属：
  - 属 PP、已列在上面：A68fVLK03、VLK10、VLK11、VLK14、VPQ06、VSR03；
  - 不属 PP、仍 KNOWN：A68fVSW01（protected 访问）、VRF03（记录命名）、VTC02 / VTD05（函数变量类型链的属性名）。

**必须不变的底线 PASS（控制组）**
- 旧评审探针：A68fZSR01-06、WSS01-06、WPG01 / 05 / 06、VPP08-10、VPT01-03 / 05 / 07-09、VMC02、UPU01 / 02 / 04 / 05 / 11-13 / 16-18、UPV01-03 / 05-08（UPV04 不存在，第四版误列）、UMB01 / 06、TTP01-04。
- 第 3 步评审探针：RV03、RV04（要 S0）、RVR 的 `Lib.Ex.M`、`Lib.Ex.M2`（要 PP-F）。
- 本卡新探针：PR01、PP02、PP04、PP06、PP10、PP11、PP12。
- 三语料全部底线 PASS。MSL 底线 PASS 的 500 个尤其要紧：MSL 到处经限定名进 Modelica.Units / Constants / Math / Media，每个都会触发 pp_enter。

**会让底线 PASS 变化或 SKIP 的风险**
1. 误报：自引用规则、PartialModelicaServices、REDECLARED（依赖 pending）、缓存先后、短类链 partial 的判法与 omc 不一致 → 底线 PASS 变成"ymc 拒"。
2. 沙盒漏回滚：检查实例化写坏了正常查找要用的缓存 / 定义节点状态 → 底线 PASS 的输出变。
3. 性能：MSL 大包（Media）的检查实例化耗时 → 扫描超时。
4. 检查模式下罕见构造触发断言 → CRASH。
5. flatten 的进类钩子调到 omc 不做 instPackage 的地方（枚举字面量、内建、经组件的 cref）→ 误报。
6. 第五版补：
   - 根类路径没按 RELAXED 检查 → MSL 等经 `--class=A.B.C` 进入的底线 PASS 变拒（PP-F 处理）；
   - 检查实例消耗 `synth_cap_` → 之后正常实例化 PASS 变 SKIP（PP-B 处理）；
   - `depth_` / `short_active_` 被检查带脏（PP-B 处理）；
   - flatten 期加节点让调用方引用悬垂 → CRASH（PP-G 处理，ASan 验收）；
   - 实例化次序与 omc 不同 → 缓存语境不同 → 误报 / 漏报（S0 处理；S0 放弃时用 §6.9 窄做法）。

**退路（照 RICM / partial 组）**
- S5 是单独一个阶段，排在 S1-S4 之后。
- S5 对 S4 构建做 §12 b 机械比对。底线有输出（退出 0）的用例里，只要有一个不在上面"会变"的名单里（三语料补记、对 omc 核过 omc 拒的也算在名单里）却变了——无论是底线 PASS 变拒、变 SKIP、变 CRASH、输出变，还是 S5 让扫描出现超时——S5 整项放弃，不修补。
- 第五版加：底线 SKIP 的用例变成 FAIL / AI / CRASH，同样触发整项放弃（S5 与 S4 的组件栈、pending 交织，这类变化说明语境判错）。
- S1-S4 不受影响。

**精确做仍做不到的（单列；第四版均已按规则定，不再交用户）**
- PP-1：instPackage 报的非 partial 错误（§3.5-7：缺元素、NON_ENCAPSULATED、protected 访问 A68fVSW01、encapsulated 边界 A68fWSB02、`checkClass`）。
  - **第四版定：不进本桶，仍 KNOWN**。依据是已定的补齐口径：只补消费本桶放开数据的路线，无关的老错仍 KNOWN。这些错误不来自本桶放开的数据，也不属 PP（partial）族。
  - 另一个原因：要全报，就得把检查实例化里 ymc 的所有核查都当成 omc 错误；但 ymc 的 inst 核查多数报的是 SKIP（未做），不是错误，两者分不清。
- PP-2：检查实例化里 ymc 未做的构造被丢弃，那一块里的 partial 漏报（保持底线 AI）。
- PP-3：预算截断后递归进的类、被调函数漏报。
- PP-4：被调函数的 `derivative` / `inverse` 注解函数（omc 在 instFunction2 :484-486 也会实例化）不跟进。
- PP-5：PP-C 不跟进的记录构造器、外部对象构造 / 析构、实例化期重查基类名（PP-C 末，第五版补）。
- PP-6：RELAXED / REDECLARED 语境的检查被截断后，本次运行之后的 PP 错误一律不报（PP-F）。

### 6.9 S0：实例化序照 omc（先全部 extends，后本地组件）——第五版新增；用户 11-11 批准精确做法

**问题**（评审第 2 条，§3.3-6）：omc 先实例化全部 extends、再实例化本地组件（NFInst.mo:1181-1190，递归同样，:1489-1497）；ymc 的 inst_class 按声明序交错（inst.cpp:1500-1595）。包实例第一次在哪种语境建决定之后报不报，次序不同 → S4 / S5 与 omc 分歧：
- RV05（底线 SKIP，omc 拒）：ymc 先走 port（原组件，挂起语境）建好 P 的缓存，Base2 再命中缓存不挂起 → 按第四版卡 S4 变 AI；
- RV03 / RV04（底线 PASS，omc 收）：ymc 先在 t 里普通语境进 P → 按第四版卡 S5 报错，PASS 变拒；
- RV01（底线 AI，omc 拒）：ymc 先走 port，P 在挂起语境里检查过，Base2 不再报 → S5 漏报；
- RV06 / RV07（底线 SKIP，omc 收）。
- 同一次序问题也出在类修饰与继承类上：本地组件写在 extends 之前、类型是被 extends 修饰改过的继承类时，底线在 classes_via_mod 守卫拦成 SKIP（OR2 / OR4 / OR7 / OR19，"inherited class seen through modifier / redeclare"），或是老错（OR1：A 里 `B2 b2;` 写在 `extends A0` 之前，外层改 A0 的 B，omc `b2.x = 2.0`，底线 1.0）。

**精确做法（推荐）**：inst_class 的组件循环拆成两遍。
1. 第一遍只处理 extends（Ref 节点）：实例化基类、施加 extends 修饰、adopt 继承类、记 `flat.exts`，基类组件存起来（拷贝）；
2. 第二遍按声明序：extends 处拼入存好的基类组件，本地组件照旧克隆 + `inst_component`。
- `flat.components` 的次序不变（omc 输出也是声明序：RV03 `t` 在 `port.q` 前、OR11），段序（`flat.exts`）不变；`redeclare_local_components`、`dedup_components` 在两遍之后，不变。
- 每层基类实例自己也走 inst_class，所以递归同样先 extends 后组件，与 omc 一致。

**精确性**：只差一处。omc 先建全部基类的类树（`ClassTree.instantiate`），再逐条实例化各基类的组件；ymc 是逐条 extends"建树 + 实例化组件"。基类组件查名走基类自己的作用域，不看派生类后面那条 extends 的树；extends 基类名在 expand 时就解析（omc 也在 expand），所以进包的语境不受影响。
- 被 extends 修饰重声明的组件：omc 在它的位置先实例化原组件（REDECLARED）、紧接着建新组件（NFInst.mo:1894-1907）；ymc 的原组件在基类实例里按位置实例化，新组件要等基类实例返回后才建（inst.cpp:1675）。两者之间的兄弟组件、新组件的子树都是普通语境，只有原组件子树是 REDECLARED，所以各包第一次进入的语境与 omc 相同。

**底线影响评估**：
- 底线有输出的模型，次序只在"本地组件写在 extends 之前、且它用到的继承类被 extends 修饰改过"时影响结果。这些底线要么被守卫拦成 SKIP（OR2 / OR4 / OR7 / OR19），要么是老错（OR1）。用到未改的继承类（OR9-OR13：记录、类型、函数、带修饰但不相干的 extends）底线 PASS，克隆与定义节点结果相同，不会变。
- 包缓存的语境只在 S4 / S5 里有意义；底线不分语境。
- 节点编号的次序会变。已核：flatten 对 `package_instances` 只做成员判断（flatten.cpp:15774 / 15942 / 16881 / 20087），排序都按名字（:28914-28916 等），组件按 `flat.components` 次序遍历，不依赖编号。
- SKIP 的原因可能换成另一个（先撞到哪个），退出码不变。
- S0 单独上时，守卫拦住的 SKIP 有的会先变成老错（OR7 / OR19：E 在实例里 extends 被改的 B，要 F1 才对），S1a（F1 在内）结束时变 PASS。§12 b 对这类"中途 SKIP→FAIL"有专门一条。
- 风险：某处依赖"本地组件先于 extends 实例化"的副作用 → 底线 PASS 变化。由 §12 b 机械比对兜底。已知的几处（第六版，N7）：
  - `settled_classes_` 的写入（inst.cpp:2216 本地组件按单段名定类型时写；:345 顺短类链时写）：S0 让本地组件的写入挪到 extends 之后，而 `instance_class`（:1182）按它决定克隆时要不要重展开。基类实例化时原本看得到的 settled 标记，现在可能还没写 → 克隆方式不同。
  - `short_settled_` / `check_short_settled`（写在 :373，查在 :326 / :2149）：同理，写与查的先后会变，可能多报或少报 SKIP。
  - 只在旧次序下才触发的守卫：例如 :1982 的 "redeclared class seen from nested class"。复评 SA5（D 里 `W w;` 写在 `extends A` 之前，外层重声明 Y）底线在这里 SKIP，S0 后 W 取到 adopt 进来的类、应变 PASS（omc `d.w.y.v = 3.0`）；对照 SB5（次序调换）底线 PASS，必须不变。
  - 复评用 5 对"本地组件写在 extends 前 / 后"的探针（s0a：SA1-SA5 / SB1-SB5）比底线两种次序的输出，没找到底线 PASS 会被改变的写法；SA5 / SB5 进 `pk-p55r`。

**退路**：S0 对 B0 做 §12 b 比对，外加 MSL 专项（500 PASS、耗时在噪声带内）。底线退出 0 的用例只要有一个变得与 omc 不同（含 PASS 变 SKIP / 拒 / CRASH），S0 整项放弃，S4 / S5 改用下面的窄做法。

**窄做法**（S0 放弃时用，评审给出）：
- S4：根阶段普通语境命中"挂起状态下建成"的包实例缓存 → SKIP；flatten 期命中照放，PR01 不受影响。RV05 → SKIP（底线就 SKIP），RV06 / RV07 → SKIP。
- S5：根阶段某个包只要在任何地方以 REDECLARED 语境进入过，就不报它的 PP 错。RV03 / RV04 保持 PASS，RV01 保持 AI（漏报）。
- 覆盖：不误报、不出新的 AI；但对放开模型是收窄（RV06 / RV07 这类 omc 收的仍 SKIP，RV01 这类仍 AI），与规则 (c) 的"照 OMC 补齐"冲突，选它要用户定。

**建议：精确做法。**
- 改动集中在 inst_class 一处，次序与 omc 相同，S4 / S5 的缓存语境因此精确；
- F7 的完整性也要它（OR1：基类里写在 extends 之前的组件要看到转发来的类修饰）；
- 窄做法只能保证不出错，放开模型里 omc 收的写法仍 SKIP。

**改动边界**：只改 inst_class 的循环结构（拆两遍），每个 extends、每个组件的处理代码不动。用户已批准（todo「用户已批准的越界改动」：S0 作为最先的实现阶段，机械比对或 MSL 专项不过就整项撤，再问用户是否改窄做法）。

## 7. 用户已定 / 仍需用户定

**已定（2026-11-11）**：
1. GenerationOfFMUs 归 68e（68e 已收走，§19-1 交接保留）。
2. RICM 做，带退路（第五版经 F7 预挂，§6.5）。
3. partial 组做，改共用的 `inst_component`（只加组件栈）与 :624 由停改挂起，退路已写（§6.3）。
4. 规则 (c) 补充：漏考虑的老路线照 OMC 补齐（§6.6）。
5. 规则 (a) 例外放宽：照 OMC 移植、变化后与 omc 逐字节一致的都允许，逐个列名（§6.7）。
6. 老路线补齐范围：只补消费本桶放开数据的路线（§6.6 口径）。
7. PP 族本桶做（§6.8，阶段 S5）。
8. （第四版）instPackage 报的非 partial 错误不进本桶，仍 KNOWN（§6.8 PP-1）。
9. （第四版）检查实例化设确定性预算，宁漏报不误报；验收加 MSL 500 PASS 与扫描耗时不变（§6.8 PP-B、§12 b）。
10. （第四版）F4 与 PP 报真错误、退出 1，只限条件逐条对到 OMC 文件:行的判定（§6.6 F4、§6.8 PP-E）。
11. （第四版）丙-2 / 丙-4 现在补（F1 只看直接点名的节点、F6）。
12. （第五版，用户）F 系列按类修饰触发、不分新旧写法，底线已支持的普通类修饰也走新代码；批准的改动点照 todo（§6.6 乙开头）；评审第 6 条关闭。
13. （第五版，主代理按已定规则）F4 定型不了的静默保持底线（不 SKIP、不报错）；startOrigin 照 OMC 报真错误；数组不进 F4；TAU05、RV47 记 KNOWN。
14. （用户 11-11，todo）S0 用精确做法，最先实现，不过就整项撤、再问是否改窄做法。
15. （用户 11-11，todo）F7 批准：改 instantiate 的 cmods 支（转发、头插改追加）、inst_class 加 cmods 形参、删事后挂；退路先退到只对新写法预挂，仍不行整项撤。
16. （第六版，主代理）阶段顺序：F1 / F2 / F6 挪到放开之前（§6.7，N3）；N4 补上（F7 第 6 步，RW3 列名）。

**仍需用户定**：
- 目前没有。
- 条件性：S0 撤掉后 S4 / S5 是否改窄做法；F7（退到只对新写法预挂之后）、F1、F2、F6 任一须撤时，依赖它的放开写法要在放开条件里排除、保持 SKIP。这些与规则 (c) 冲突，到时报用户定（§6.7 连带撤项）。

## 8. 输入 → 输出、不变量

- 输入：SCode + Absyn（break 是 `ExpKind::Break`，parser.cpp:638）、inst 实例树。
- 输出：FlatModel，dump 与 omc 1.25.0 逐字节一致。
- 不变量：
  - B-1 / B-2 放开后 `ExpKind::Break` 不进 `inst_exp`；
  - 根实例化返回时 `redecl_pending_` 与组件栈为空（`YMC_ASSERT`）；
  - 新形态类修饰只挂在 `instantiate` 克隆出的类节点上（断言 `in_instance_context`）；
  - inst_class 第二遍开始时，本类全部 extends 已处理完（S0，断言）；
  - `class_mods` 只挂在 instantiate 刚克隆出的类节点（或由它拷出的 rid）上，且挂在该实例任何组件实例化之前（F7，断言：挂时该实例尚未建 FlatTree）；`class_mods[node]` 外层在前；
  - F1 施加 `class_mods[ext_named]`：组件子修饰逆序、类修饰与重声明子修饰正序（N2）；
  - 检查模式进出前后 `depth_`、`short_active_` 相等，检查模式下不合成新的 SCode 元素（S5，断言）。

## 9. 数据结构变更

- `src/inst/inst.h`（`Instantiator` 私有成员）：
  - `std::vector<NodeId> comp_stack_;`
  - `struct RedeclPending { const char* why; std::string detail; std::vector<NodeId> stack; }; std::vector<RedeclPending> redecl_pending_;`
  - `bool root_phase_ = false;`
  - `int pkg_depth_ = 0;`
  - `std::unordered_set<std::uint32_t> new_cmods_applied_, new_cmods_refused_;`
  - 谓词 `redecl_only_class_mod` / `nested_class_mod` / `const_literal_class_mod` / `ra_drop_ok`；
  - 公开只读查询 `bool bases_class_modded(NodeId cls)`（F2）；
  - S5：`std::unordered_set<std::uint32_t> pp_checked_;`、检查模式标记与深度计数、`struct PpSandbox`（RAII：进入时保存 §6.8 PP-B 所列各表与被改的定义节点类状态，退出时还原）、`RedeclPending` 加 `kind` 字段（Partial / Pp）；
  - 公开：`void pp_enter(NodeId cls, PpWhy why)`、`void report_error(SourceLoc, std::string)`（F4 与 PP 共用）。
- `src/inst/inode.h`：
  - `ClassTree` 加 `std::vector<NodeId> ext_named;`（与 `exts` 平行：extends 名直接查到的节点，F1 / F2）。
- `src/inst/inst.h`：
  - `inst_class` 加形参 `const std::vector<ClassMod>* cmods = nullptr`（F7：转发目标不是本地类的类修饰）；
  - `instantiate` 不加形参（第四版 F6 的 `refresh_locals` 由 F7 的 cmods 代替）；
  - S5：`pp_enqueue(...)` 与队列 `std::vector<PpPending> pp_queue_`（实例层节点或定义路径 + why），RELAXED 标记，"截断后全部不报"标记。
- `src/flat/flatten.cpp`：`collect_attributes` / `plan_attributes` 加 `bool break_ok = false`；小工具 `is_break_exp`；S5 的进类钩子调用（只调 `instr_->pp_enter`）。
- 新文件（建议）：`src/inst/pp_check.cpp`（S5）。
- `modifier.h`、SCode、Absyn：不改。

## 10. 不做

- **break**：参数、常量（含包常量及其引用）、记录、数组、非 Real、fixed / stateSelect、函数形参缺省；在 inst 层识别 break。
- **类修饰**：
  - 非 Model 类；非字面量值；protected；final / each；继承来的目标类；组件数组；
  - 短类修饰 / 约束修饰 / 重声明元素里的类修饰；带修饰的重声明；混写；
  - 嵌套类修饰当组件类型用；经挂嵌套类修饰的类直接查 `B.C`；
  - 包缓存陈旧本身（丙-3）。
- **partial**：组件修饰 / 短类修饰里的重声明、constrainedby、PartialModelicaServices 的 :624 豁免（模型层 SKIP 照旧）。
- **PP**：instPackage 报的非 partial 错误（§6.8 PP-1）；被调函数的 derivative / inverse 注解函数（PP-4）。
- **Bug2727 族**：嵌套、final / each、内建类型组件、组件修饰路线、数组。
- **F4**：数组组件上被丢弃属性的核查（RV47 KNOWN）；cref / 调用 / 表达式值的定型（A68fTAU05 KNOWN）。
- **PP-C**：记录构造器、外部对象构造 / 析构、实例化期重查基类名（§6.8 PP-5）。
- **其它**：GenerationOfFMUs 及 derivative / inverse 引用函数（归 68e）；函数体记录构造器的实例命名、DUP 留存（65b）；A68fUTU 族（函数变量类型链属性，KNOWN）。

## 11. 旧评审探针在新方案下的落点

567 个旧评审探针已对 omc 1.25.0、B0、旧 68f 终版跑完：`$S/redo/68f/allp/<集>.txt`。B0 合计：PASS 128、SKIP 255、SKIP(omc 拒) 74、BOTH-REJECT 9、FAIL 38、AI 63。

| 族（前缀 A68f） | B0 | 新方案落点 |
|---|---|---|
| BR / XBK13 / YBG01 / 04 / 08（break） | SKIP 41 + omc 拒 7 | Real 标量变量的放开（BR01 / 03 / 05 / 06 / 08 / 13 / 16 / 18 / 23 / 24 / 31 / 32 / 36 / 44 / 45 / 47 / 50 / 51 / 54 等），须与 omc 一致；BR29 / BR37 撞上与去掉 break 的变体相同的 SKIP（BV29 / BV37）；其余照旧 SKIP |
| XBK / TBC / YBC / VDB13 | SKIP | 照旧 SKIP |
| VDB / WXB / VRB / UDC / XBK08 | PASS | 不受影响 |
| TAU | AI | F4 → 两边都拒：TAU01-04 / 06（例外清单）；TAU05（cref 值）KNOWN |
| UTU | AI | KNOWN（函数变量路线，不消费本桶数据） |
| RA / YRG / RAX | SKIP 5 | 按 cond_RA：RA03、RAX05-07 放开；YRG01、RAX01-03 / 08 / 09 照旧 |
| RC / XRC / PR | SKIP 17 + omc 拒 8 | extends 路线的（RC01 / 06 / 08 / 11 / 17 / 18 / 19、PR06 / 10 / 12）放开；组件修饰 / 短类 / constrainedby / PartialModelicaServices 照旧 SKIP；omc 拒的照旧 SKIP |
| XPK01 / 02 / 08 | SKIP(omc 拒) | 照旧 |
| PP 族（XPK04 / 05、YCP、ZPP、WPP、WSS07 / 08、WSB01、VLK、VPQ06、VSR02 / 03、VSW04、VPT10、UPU06 / 07 / 09 / 21、UMB07-12） | AI | S5 → 两边都拒（例外清单，§6.8） |
| VSW01、WSB02 | AI | 非 partial 的 instPackage 错误，KNOWN（§6.8 PP-1） |
| PR01 与 ZSR / WSS01-06 / WPG / VPP / VPT / VMC02 / UPU / UPV / UMB01 / 06 / TTP 的 PASS | PASS | S5 控制组，必须不变（变一个则 S5 整项放弃） |
| CM（类修饰） | SKIP 30 | 满足谓词的放开（CM01 / 02 / 10 / 28 / 35；CM03 / 04 / 05 / 09 / 14 / 24 / 39 中满足者；CM07 / 08 / 18 / 19 / 23 中满足者）；CM06 / 11 / 12 / 21 / 22 / 25 / 27 / 36-38 / 40 照旧 |
| CM20 | FAIL | F7 → PASS（例外清单） |
| XSB01 / XSB02 / XSB03 | FAIL / SKIP / SKIP | F1：XSB01 → PASS（例外清单）；XSB02 / 03 放开后须 PASS |
| YLP01 / 02 / 03 / 04 | FAIL | F7 / F5 → PASS（例外清单） |
| XST04-06 | FAIL | F7（预挂 + CM-3 / CM-4 收 plain 形态）→ PASS（例外清单） |
| XST01-03、XSC、YPT、WPR、VPR、XPD、XCT | 混合 | 不满足谓词的照旧；B0 老错照旧 KNOWN |
| YFN / ZFN / ZRS / WCG / RW / TF / OR | SKIP / FAIL | 非 Model，照旧 |
| TIM / URN / URP / URQ / URW / VRF / VRC / VRN | SKIP / FAIL | 不碰（65b / 68e） |

**第 3 步评审探针**（`$S/redo/68f-review/`，本卡已复跑，输出在 `pout5/`）：

| 探针 | 底线（5c7） | 新方案落点 |
|---|---|---|
| RV01 / RV02 | AI | S5 → 两边都拒（RV01 要 S0） |
| RV03 / RV04 | PASS | S5 控制组（要 S0） |
| RV05 | SKIP（omc 拒） | S0 后 S4 仍 SKIP，S5 两边都拒 |
| RV06 / RV07 | SKIP | S0 + S4 → PASS |
| RV10、RV35 | PASS | 不变 |
| RV11-13、RV15-22 | FAIL | F7 / F1 / F2 → PASS（例外清单） |
| RV14 | SKIP | 须 PASS 或 SKIP |
| RV31-34 | SKIP | 照旧 SKIP（定义层经包实例取值、限定函数调用，65b / 65c） |
| RV40-46、RV49 | PASS | F4 后必须不变 |
| RV47 | AI | 数组不进 F4，KNOWN |
| RV48 | AI | F4 → 两边都拒 |
| RVC2-5 | SKIP | 放开后须 PASS（F7 + F1 + CM-2 / RICM / CM-4） |
| RVR Lib.Ex.M / M2 | PASS | S5 控制组（要 PP-F） |
| RVR Lib.M3 | AI | S5 → 两边都拒 |
| RW3 | FAIL | F7 第 6 步 → PASS（例外清单） |
| RW1 / RW2 / RW4 | SKIP | 须 PASS 或 SKIP（omc 6 / `b3.c.x = 2.0` / 5） |
| RW5 / RW6 | SKIP | 须 PASS 或 SKIP（omc `d.y = 5.0`） |
| RW7 / RW8 | FAIL | F7 第 6 步 → PASS（例外清单；组件修饰路线） |
| G1 / G2（本卡 g1） | SKIP | S1a 后可暂为 FAIL（豁免），S1b（F2）/ S1c（F6）后须 PASS |
| RF1-RF3 | PASS | F4 控制组，必须不变 |
| RF4-RF9 | AI | F4 → 两边都拒（例外清单；RF6-RF9 是本卡 f6 补的） |
| SA1 / SB1、SA2 / SB2、SA4 / SB4 | SKIP（两种次序同） | 照旧 |
| SA3 / SB3 | PASS | 不变 |
| SA5 | SKIP | S0 后须 PASS（omc `d.w.y.v = 3.0`） |
| SB5 | PASS | 不变 |

入库规则（todo）：重做版与 B0 输出相同的老错不入库，转交 65b / 65c；入库的只要 PASS / 两拒 / SKIP。

## 12. 验收（与 OMC 对比，§7.4 第 2 步 a–e）

`NEW` = 新二进制，`B0` = 重做起点提交的构建。omc 用 PATH 上的 1.25.0，240 秒超时；门在跑时不并发。

**a. 目标逐字节**
```
python -I $S/redo/68f/prep.py
cd $S/redo/68f/cases && for f in *.mo; do b=${f%.mo}; timeout 240 omc $f > $b.omc; NEW $f > $b.new; cmp <(tr -d '\r' < $b.new) <(tr -d '\r' < $b.omc) && echo "$b SAME"; done
```
- 期望：11 个 SAME。
- Bug2727 以 omc 1.25.0 实测为准；它与文件 Result 的分歧记进 `docs/open-questions.md` 分歧账本（门的 `--oracle-live` 遇到它也要能在账本里查到）。

**b. 底线不变（分阶段做，见 §6.7）**
```
python -I -X utf8 $S/pks68f/diffbin.py --a <上一阶段或 B0> --b <本阶段> --jobs 2 tests/probes/*.probes <旧评审 8 集> <新集 pk-p55r*.probes>
bash $S/rv5/corpus.sh 68fr NEW          # 三语料对 B0 的变化（基线 json 换成 B0 的）
```
期望：
- B0 退出 0 的模型，NEW 的退出码与 stdout 全部相同。例外只有 §6.7 例外清单里点名的（以及三语料补记、对 omc 核过的同类），每个都要逐条对上 omc：FAIL→PASS 的 NEW 输出与 omc 逐字节一致；AI→两边都拒的 NEW 退出 1、omc 拒。
- **S0（实例化序）阶段**：对 B0 比。底线退出 0 的变化只许是例外清单里归 S0 的（目前无，OR1 要 S0 + F7）；有一个变得与 omc 不同 → S0 整项放弃，S4 / S5 改窄做法（报用户定）。
- **S1a-S1d、S2 各阶段**：对上一步比，底线退出 0 的变化只许是例外清单里归该项的；不是 → 按 §6.7 连带撤项。S1a 里 plain 门放开的底线 SKIP 模型只许变 PASS / 两拒 / SKIP，或落在 §6.7 列的两类豁免（G1 类等 F2、G2 类等 F6）；S1d（放开）时 F 系列都已在位，底线 SKIP 的放开模型只许变 PASS / 两拒 / SKIP。
- **S3（RICM）阶段**：差分里底线有输出的用例只要变了一个，或有一个底线 SKIP 的用例变成 FAIL / AI / CRASH → RICM 整项放弃，不修补。
- **S4（P 组）阶段**：同上 → P 组整项放弃，不修补（第五版加 SKIP→FAIL / AI / CRASH 触发，RV05 这类）。
- **S5（PP）阶段**：差分里底线有输出的用例只要有一个不在 §6.8 名单里却变了（含 PASS→拒 / SKIP / CRASH / 超时）→ S5 整项放弃，不修补。
- **S0 与 S1a-S1c 的 MSL 专项**：同下面 S5 的两条（500 PASS、耗时在噪声带内），S0 对 B0 比，S1a-S1c 各对上一步比。S0 不过 → S0 整项撤；S1a-S1c 不过 → 定位到项，按 §6.7 撤（F7 先退到只对新写法预挂）。
- **S5 的 MSL 专项**（第四版）：`python tools/msl_sweep.py --ymc <S5 构建> --jobs 6 --json …`。
  - 底线 PASS 的 500 个必须全部仍 PASS；
  - 扫描总耗时不变：同机、同并发，先把 S4 构建跑两遍定出噪声带，S5 的耗时不得超过两遍里较大者的 1.1 倍；
  - 任一条不满足 → 按退路 S5 整项放弃。
- 差分里 B0 退出 66 的变化逐个对 omc：允许 SKIP→PASS / 两拒 / SKIP；SKIP→FAIL / AI / CRASH 按 e（S3-S5 直接触发整项放弃，见上）。
- **中途 SKIP→FAIL**（定稿）：三类只记账、不撤——
  - S0 到 S1a：守卫拦住的 SKIP 先变老错（OR7 / OR19 这类：同一模型把本地组件挪到 extends 之后，在 B0 上就是这个 FAIL）；
  - S1a 到 S1b：plain 门放开、又要 F2 的（G1 类）；
  - S1a 到 S1c：plain 门放开、又要 F6 的（G2 类）。
  - S1c 结束时对 B0 复核，所有"底线 SKIP → FAIL"必须全部清零（变 PASS、两拒或 SKIP），否则按 e。
- 三语料变化：
  - scodeinst：9 个目标 SKIP→PASS；
  - testsuite：11 个目标 SKIP→PASS，外加例外清单里落在 testsuite 的；
  - MSL 无变化；
  - 无未列名的 PASS→非 PASS。

**c. 全部探针集**
```
python tools/probe_recheck.py --ymc NEW --jobs 2
# 旧评审 8 集：照 $S/redo/68f/runall.py 换 NEW 重跑，与 allp/ 的 B0 结果逐模型比
```
- 期望：tests/probes 全部 FAIL / ACCEPTS-INVALID / CRASH = 0（KNOWN.txt 除外）。
- 旧评审集：每个模型为 PASS / 两拒 / SKIP，或与 B0 逐字节相同的老错。§11 标"→ PASS""→ 两边都拒"的必须如此；S5 控制组必须仍 PASS。
- KNOWN.txt：S5 通过后删掉 24 条 PP，以及 A68fVLK03 / VLK10 / VLK11 / VLK14 / VPQ06 / VSR03；F4 通过后无对应条目可删（TAU 未登记）；A68fTAU05、RV47 入库时按 KNOWN 登记。

**d. 过门**（CLAUDE.md 四条）
1. `python tools/flat_sweep.py --cases-from docs/research/s3-easy-slice.json --oracle-live`：FAIL 0，不一致 0。
2. `python tools/flat_sweep.py --oracle-live`：无新增真 FAIL。
3. 三套 `ctest` 全绿、零警告、ASan 0。
4. 更新文档：
   - ModClass4、RedeclareClass2、RedeclareMod11、RedeclareMod12 加进 slice；
   - **Bug2727 分歧写进 `docs/open-questions.md` 分歧账本**；
   - S0、F1、F2、F4-F7、S5 改变的底线输出（例外清单）写进 progress 与 open-questions 账本。

**e. 评审结果处理**（照规范 §7.4 第 2 步 e、todo (c)）
- 本桶新代码算错的 → 在新路线入口收窄为 SKIP。
- 放开后原 SKIP 的模型出错、根因是本卡漏考虑的老路线（不在 §6.6 里） → 回第 1-3 步补学 OMC、补卡、再评审，再实现；不收窄、不只登记 KNOWN；仍受 b 约束。
- B0 PASS → 非 PASS → 撤造成它的改动。
- 与放开无关的 B0 老错 → KNOWN + open-questions。
- RICM / P 组的 b 差分有变化 → 按退路整项放弃。
- 收口：对抗评审一轮 + 核对评审一轮。
- 实现中某目标又撞别的 not_implemented 点：停下报告，不加宽。

## 13. 单测计划（期望只来自 omc 实跑）

加在 `tests/unit/flat/flatten_test.cpp`，期望串逐字取自 omc 输出：
- BM1-3（`cases/`）；BV41、BV43、A68fBR36。
- ModClass4、ModClass6、RICM、const15（`cases/`）；CMX13、CMY09、CMX09。
- RedeclareMod11、RedeclareClass2（`cases/`）；PR10。
- A68fRA03、RAX05。
- F7：PX04、A68fCM20、RVC3、RV22、A68fXST04；F1：PX01、PX03、A68fXSB02、Q04、RVC2、RV17，以及不施加的 Q02；F2：EQ09、RV13；F5：A68fYLP04、OR17、OR18；F6：Q07、Q06。
- S0：OR1（要 F7）、OR2、OR4（期望 omc 输出）。
- F4：A68fTAU01、RV48、RF6、RF8（期望：报错、退出 1——omc 拒）；RV40、RV41、RV43、RF1、RF2（期望 omc 输出，保持底线）。
- F7 第 6 步：RW3（期望 `b.i = 6`）、RW7（期望 `d.b.i = 6`）；F1 次序：RV17（期望 6）。
- S5：A68fZPP01、VLK10、VPQ06、PP01、RV01（期望：报错——omc 拒）；A68fZSR01、VPT01、PR01、PP06、RV03（期望：omc 输出）。RVR 三例要库加载，放进 pk-p55r-pp，不写单测。
- 条件外的"保持 SKIP"不写单测。

## 14. 新探针集计划

- `tests/probes/pk-p55r.probes`（前缀 `R68f`）：
  - 本卡的 br1 / br2 / sx1 / pr1 / cm1 / cm2 / ra1 / eq1 / px1 全部；
  - 每个放开条件与每条补齐（F7、F1、F2、F4-F6）的边界两侧各一；
  - 每组都带调换声明次序、外层再套一层的变体；
  - q1 全部（Q01-Q08，F1 / F6 的正反面）；
  - 第五版：评审 rv3 / rv5 / rv6 / rv7 / rv8 全部（RV10-RV22、RV31-RV35、RVC2-RVC5）；o1-o3 全部（OR1-OR19：实例化序、预挂转发、F5 两种顶替）；
  - 第六版：复评 rw1（RW1-RW4）、rf1（RF1-RF5）、s0a（SA1-SA5 / SB1-SB5），本卡 f6（RF6-RF9）；
  - 定稿：复评 rw2（RW5-RW8），本卡 g1（G1、G2：S1a 中间态与清零）；另补：中间层元素级重声明再被更外层重声明（rid 的 rid 带类修饰）、F1 三层 extends 各给一条组件修饰（核逆序施加）；
  - F7 加：外层改继承三层的类；同一条 extends 修饰同时改两个继承来源不同的类；转发遇到两条 extends 都有同名类；类修饰带短类代码不收的写法（组件修饰带子修饰）又经被改类查名（F7 已知风险的探针）；
  - F1 加：经两层短类别名被 extends；别名自身与链下节点同时挂类修饰；别名带包前缀；
  - F6 加：EQ04 形态前面加定义层引用（第 2 种）；定义层用过后在多个实例里用；类重声明形态；settled 情形（根类、单段名）对照；
  - P 组加"原组件子树里建的包实例被新组件复用"（PR01 形）、"原组件子树里施加的 SMod 在别处被拒"。
- `tests/probes/pk-p55r-adv.probes`（前缀 `R68fV`）：对抗评审手写。
- `tests/probes/pk-p55r-pp.probes`（前缀 `R68fP`，S5 自检）：
  - pp1 全部，加上旧评审 PP 相关探针改名（AI 那批改作期望两拒，控制组期望 PASS）；
  - 补自引用的正反面（经 import 别名、经继承到自身、cref 自引用）；
  - 补每条进类路线（类型路径 / 基类名 / cref / 函数名 / 限定与非限定 import）各一正一反；
  - 补 REDECLARED 缓存先后（先在原组件里进、后正常进；反过来）；
  - 补被调函数的形参类型 / 函数体、记录字段绑定、属性修饰绑定、模型类的方程段；
  - 补一个大包（模拟 Media 规模）核预算。
  - 第五版：评审 rv1 / rv2（RV01-RV07）与 RVR 三例（库加载形态，用 `--modelica-path` + `--load-model` 跑）；rv4（RV40-RV49）进 `pk-p55r.probes` 的 F4 段；
  - 补：根路径三段以上、根路径上的包在检查中再进别的包（RELAXED 递归）；RELAXED 检查被预算截断后普通语境再进同一包（核"截断后全部不报"）；flatten 期经限定名进类的五条路线各一（核队列处理）。
- gf1（GenerationOfFMUs 邻近写法）移交 68e。

## 15. 方案评审（第 3 步填）

- 第一次评审（11-11，第四版）：不通过。七条意见与第五版处理：
  1. F1 补不到"事后挂"的类修饰（RVC2 / RVC3）→ 新增 F7 预挂（§6.6 F7、§3.4-10），F3 删去，CM-2 / RICM 改经 F7；XST04-06 随之补齐。
  2. 实例化序不同（RV01-RV07）→ §6.9 精确做法 S0（建议）与窄做法并列，交用户定；S3 / S4 / S5 退路加"底线 SKIP 变 FAIL / AI / CRASH"。
  3. 漏了 lookupRootClass 的 RELAXED → §3.5-9、§6.8 PP-F。
  4. F4 → 只核字面量与 startOrigin，数组不进，定型无副作用（§6.6 F4）。
  5. S2 拆子阶段 → §6.7（F7 连同 F5 提前到 S1a；S2.1 F4 → S2.2 F1 → S2.3 F2 → S2.4 F6），连带撤项写明。第六版按 N3 再调，见下面第二次评审。
  6. 沙盒与 C++ 风险 → §6.8 PP-B（synth_cap_、depth_、short_active_）、PP-G（flatten 期改队列）。
  7. 低：PP-C 做不到的、S4 继续实例化的 SKIP 原因与 CRASH、UPV04 订正 → §6.8 PP-C 末 / PP-5、§6.3-7、§6.8 控制组。
  - 评审"F1-F6 改共用路径未列入 todo"一条，随用户 11-11 决定关闭。
- 第二次评审（11-11，第五版）：有条件通过，上轮七条全部关闭。必改三处与注记四条，第六版处理：
  - N1 F4 只核胜出来源 → §6.6 F4（照属性值的选法取最高优先、带值的来源；startOrigin 任何一层出现都报；引文订正为 NFInst.mo:1547、NFBuiltin.mo:165 / :209；Real 属性表补 uncertain）。RF1-RF3 进控制组；RF4-RF9 进例外清单（RF6-RF9 是本卡补的 f6，omc 拒、底线 AI 已实跑）。
  - N2 多条类修饰的施加次序 → §6.6 F1（组件子修饰逆序、类修饰与重声明正序）、§8 不变量。
  - N3 阶段顺序（主代理定）→ §6.7 阶段表：S0 → S1a F7 + F5 → S1b F1 → S1c F2 → S1d F6 → S1e 放开 → S2 F4 → S3-S5（定稿再改：F1 并进 S1a，见第三次快速核对）。
  - N4 RW3（主代理定补上）→ §3.4-8 末（omc 依据 NFInst.mo:1735-1737 / :1787、:1163 / :1170 先于 :1173、:1672-1674）、§6.6 F7 第 6 步；RW3 FAIL→PASS 列名，RW1 / RW2 / RW4 进探针。
  - N5 队列处理在沙盒里跑 → §6.8 PP-G。
  - N6 截断规则按事件判 → §6.8 PP-E、PP-F。
  - N7 S0 风险点名 → §6.9 风险（settled_classes_、short_settled_、:1982 守卫，SA5 / SB5）。
- 第三次快速核对（11-11，第六版）：通过，改三处后开工，不再评：
  - N3：F1 并进 S1a（S1a = F7 + F5 + F1）；阶段表、依赖、连带撤项、§12 b 随之改；逐阶段重核"放开模型 SKIP → FAIL"，剩 G1 类（等 F2）、G2 类（等 F6）两类，与 OR7 / OR19 一起豁免，S1c 结束清零（§6.7）。
  - N4 影响面：组件修饰路线 RW7 / RW8 FAIL→PASS 列名，RW5 / RW6 进探针（§6.6 F7 第 6 步、§11）。
  - F7 退路连带撤 rid 拷贝里 plain 形态的部分（§6.6 F7 退路、§6.7）。

## 16. 调试

- 新 SKIP 子原因：`class modifier form`、`class modifier across instances`、`(deferred)`。
- 不新增 dump。
- S5：检查实例的预算上限（新建节点数 / 递归深度，确定性计数）实现时按 MSL 实测定，写在这里；代码里用 `ponytail:` 注释写同样的数值与升级路径（§6.8 PP-B）。排查时可临时打印每次 `pp_enter` 的类路径与新建节点数（不入库）。

## 17. C++ 要点

- arena 纪律：遍历 `SMod::subs` 先拷贝（inst.cpp:1556 写法）；不跨调用持引用。
- 组件栈、`pkg_depth_` 用 RAII。
- `std::erase_if` 删 pending，pending 存栈的拷贝。
- S0 的两遍：第一遍把每条 extends 的基类组件存成 `std::vector<NodeId>`（拷贝），第二遍拼接；不跨 arena add 持引用（instantiate 会扩 arena）。
- F7 的 `ext_cmods` 存 `ClassMod` 值（名字、SMod id、作用域节点），不存引用；`member_def` 返回 id，用前不缓存 `SElement&`。
- S5 的沙盒、RELAXED 标记、`depth_` / `short_active_` 保存都用 RAII，异常路径（早 return）也还原。
- 两套编译器零警告。

## 18. 完成定义

- §12 a-d 全过；ASan 干净。
- 方案评审与对抗 + 核对评审按 e 收口。
- 文档：open-questions（Bug2727 分歧；KNOWN 条目）、slice、probes README、progress（含 S0、F1、F2、F4-F7 改变的底线输出）。
- lessons 更新。

## 19. 待查

1. （移交 68e）GenerationOfFMUs：根因 flatten.cpp:11487；放开点 `emit_referenced_function` :14787-14789。依据 NFFunctionDerivative.mo:77 / :257、NFFunction.mo:343；探针 `pout/GF01-GF10`。
2. ModClass4 放开后，flatten SCode 层核查经 `B.C` 落到被顶替的原类时是否撞 `redeclared_check`。
3. P 组挂起后继续实例化原组件子树，是否因别的原因 SKIP。
4. RA04、YRG02 是否满足 cond_RA。
5. 组件栈之外（extends 名展开）撞 partial 时 omc 是否放过；本卡按"照报"处理。
6. （第四版已核）F1 / F2 依赖克隆在实例里重新展开：omc 行为见 q1 Q05-Q07，由 F6 保证。
7. （第五版作废：F3 删去。）
8. omc `isSelfReference`（NFLookup.mo:608-625，按 instanceParent 链 refEqual）在"经 extends 进到自身"时为真的确切机制：本卡的豁免规则来自探针（ZSR / WSS 豁免、WSB01 不豁免、VSR / VSW04 第二步照报），实现前补探针核"经两层 extends""经短类别名"到自身的情形。
9. 第一次进入某包的先后：omc 的 instExpressions 在实例化之后，定型时还会再查名。若一个包第一次在 flatten 期被 cref 进入、而 omc 的第一次是在 REDECLARED 实例化期——缓存先后是否与 ymc 一致，需补探针核。
10. 检查实例化的预算上限值。
11. F6 的名字集 N 里"def_node 自己的 extends 修饰"一项，在 `instantiate` 开头展开 def_node 之后判；class extends 合成的 extends 是否也要算，实现时核。
12. 丙-2 的不对称（extends 经别名只读被点名节点的修饰，组件类型经别名读整条链）只核了一层别名。两层别名、别名带包前缀的情形，实现前补探针核（RV10 / RV11 已核两层别名：点名 B3 才生效）。
13. S0 的影响面：实现后先跑三语料对 B0 的差分（不带别的改动），按 §12 b 判；若出现 PASS 变化，先看 §6.9 点名的几处（settled_classes_、short_settled_、只在旧次序下触发的守卫）。
14. F7 转发的"先到先得"：两条 extends 都给出同名类时，omc 的重复元素检查（`ClassTree.checkDuplicates`）可能报错或要求相同；实现前补探针核。
15. PP-F：MSL 的 `Modelica` 顶层包在 RELAXED 下检查的计数，作为预算上限的参照之一。
16. N4：rid 是从 `base_redecls` 里的节点拷出的；同一个节点会一路传给各层基类实例，每层各拷一份 rid，类修饰随之各拷一份。实现时核"中间层重声明再被更外层重声明"时，外层的类修饰该落在哪一个 rid 上（§14 补的探针）。

## 20. 实现订正（第 4 步，2026-11-12，实现子代理；用户决定后重写）

**用户决定（2026-11-12，todo「68e / 68f 收敛方案」已写）**
1. F7 已知风险：不把 XF721 / XF722（`B.C c` + `extends A(B(x(start = 1)))`，底线碰巧对，XF724 同形态取错值）列进例外，执行 F7 退路第一步：只对新形态预挂（CM-2 嵌套、RICM 只含类重声明）；底线 plain 形态恢复事后挂（底线 :1556-1581 + `apply_class_mod`）；CM-3 / CM-4 的 plain 门撤；rid 只拷新形态条目，拷完没有条目不标 `class_modded`。
2. const15 族整个不放：const15 退出 68f，**目标 11 → 10**（§1 的第 7 个划掉）。"经 extends 类修饰改基类本地类的常量、再经限定名取值"一律保持底线 SKIP：CM-3 / CM-4 的 const-literal 门与 plain 门都不做；只含类重声明的门照卡保留给 ModClass4 / RICM。未采用"排除基类里有组件用到被改类的写法、其余放开"。记 docs/open-questions.md TBD-16。

**阶段（实际）**：S0 → S1a4（F7 只新形态 + F5 + F1 + F2 + F6，F2 / F6 按 §6.7 并进 S1a）→ S1d → S2 → S3 → S4 → S5（**整项撤**，见下）。终版 = S4 态。S1a / S1a2（plain 也预挂、plain 门、S1a2 的门收窄）作废：plain 门整个撤了，S1a2 对它的收窄（只收 `名字 = 字面量`、public、种类相容）随之无意义，代码已删。S1a3 是中间版（early 守卫过宽，RV18 B0 FAIL→SKIP），改成 S1a4。

**F7 退路第一步之后 F1 / F2 / F6 的口径（实现补，规则 (c)"本桶引入的错 → 入口收窄"）**
- 事后挂的类修饰（底线 plain 形态的 extends 类修饰、inst_package 短类修饰挂的类修饰，记在 `posthoc_cmods_`）挂上时基类实例里先建的组件已经读不到；F1 若读它们，只让后建的那部分变（XF716：A 里的 e 不变、派生类的 e3 变，B0 FAIL 换成另一种 FAIL）。所以 F1 / F2 / F6 只读预挂的条目（组件修饰路线的、新形态的；`preattached_class_mods`），事后挂的照底线不读。
- S0 让"写在 extends 之前、类型名首段在定义类树里是 classes_via_mod 的本地组件"晚于 extends 实例化，底线在这里撞 `tree_class` 守卫 SKIP（OR7 / OR19）；F7 不再预挂 plain 形态后这类会变 FAIL。收窄：这类组件的子树里 F1 遇到事后挂的条目 → SKIP（原因同底线 "inherited class seen through modifier / redeclare"）。**（对抗评审后由收窄 N1 取代，early 守卫已删，见下。）**

**重算后的规则 (a) 例外清单（S0 + S1a4，已对 B0 机械差分核过；对抗评审收窄后的终版清单见后面"终版对 B0"）**
- FAIL → PASS（保留 5 个，与 omc 逐字节一致）：A68fXSB01、EQ06、PX02、RV15（F1，组件修饰路线预挂的类修饰）、Q07（F6）。
- 回到 B0（原列名、现与 B0 逐字节相同，不再是例外）：
  - F7：OR1、A68fCM20、EQ05、A68fYLP01、A68fYLP02、RV22、A68fXST04、A68fXST05、A68fXST06、RW3、RW7、RW8；
  - F5：A68fYLP03、A68fYLP04；
  - F1（依赖预挂）：EQ04、PX01、Q04、RV11、RV12、RV17、RV18、RV19、RV20、RV21、Q05（收窄 N1 后 EQ04 / PX01 / Q04 / Q05 / RV11 / RV12 / RV17-19 改为底线 FAIL → SKIP，RV20 / RV21 仍与 B0 相同）；
  - F2：EQ09、CMX14、RV13、RV16（收窄 N1 后 EQ09 / RV13 / RV16 改为底线 FAIL → SKIP，CMX14 仍与 B0 相同）。**订正**：原写"F2 全回 B0"不对——F2 走组件修饰路线的写法仍有底线 FAIL → PASS（评审 F705、Q006，与 omc 一致，列在下面）。
- 底线 SKIP → PASS（S0）：OR2、OR4、A68cIH02、R68cIH2、SA5。**（核对评审后由收窄 N4 退回底线 SKIP，见下。）**
- F4（S2）的 AI → 两拒名单照 §6.6，**补一个漏列的**：pk-p55f/A68fUTU08（`type T = Real(startOrigin = "x"); T x`，omc 拒）。§6.6 F4 把它和 UTU02 / 04 / 05（函数变量那条路）归在一起记"仍 KNOWN"，实际它走的是 F4 第 1 条（类型上的 startOrigin 不在 Real 属性表，NFInst.mo:1547），S2 后 AI → 两拒，照规则 (a)"AI → 两拒逐个列名"补进。
- S5 撤了，§6.7 / §6.8 的 S5 AI → 两拒名单作废（PP 族照底线 AI）。

**目标与验收（去掉 const15 后）**
- §1 目标 10 个：BreakModifier1-3、ModClass4、ModClass6、RedeclareInClassModification、RedeclareClass2、RedeclareMod11、RedeclareMod12、Bug2727。§12 a 期望 10 个 SAME；§12 b 三语料：scodeinst 8 个目标 SKIP→PASS、testsuite 10 个；§12 d-4 slice 不加 const15。

**S5 整项撤（§6.8 退路 / §12 b 触发，不修补）**
- 草稿先修了三处控制组误报（照 omc 定的口径：检查模式里 :624 第一步自引用不报——ZSR / WSS；flatten 的 `resolve_class_path` 只在 `resolve_fn` 里排检查——VMC02，它也被 `collect_outer_needs` 等 SCode 分析当工具用；检查里已置 unsupported 后不记 PP 错误、extends / 组件修饰改类而 ymc 不施加的形态先置 unsupported——UPU16）。之后对 S4 的机械差分：探针 88 个 AI → 两拒（全在名单里；名单里只 A68fXPK05 仍 AI）+ pk-p50/XPi19 底线 SKIP（omc 拒）→ 两拒；scodeinst / testsuite 0 变化。
- 但 MSL `Modelica.Electrical.Digital.Examples.RAM` 底线 PASS → 拒（"lookup in partial class MemoryBase"）：DLATRAM `extends Interfaces.MemoryBase`、算法里不带限定地调继承来的 `getMemory` / `address`，flatten 期进类钩子按函数的定义路径进了 partial 的 MemoryBase；omc 经实例里继承来的元素取函数，不对 MemoryBase 做 instPackage。最小形 pk-p55r-pp/R68fPRAM01（omc 收、S4 收、S5 草稿拒）。底线 PASS 变拒不在名单里 → S5 整项放弃。源码回 S4 备份，删 `src/inst/pp_check.cpp`；PP 族记 docs/open-questions.md TBD-18（转 65b / 65c：查找迁移到实例树后函数经继承元素取）。
- 跟着改：pk-p55r 去掉 PR02（188 个）；pk-p55r-pp 只留 PASS / SKIP 的控制组与语境用例（64 个）+ R68fPRAM01；KNOWN 去掉 R68fPXPK05；单测去掉 flatten_68f_pp_ok / pp_reject（剩 8 个 flatten_68f_*）；`flatten_src_driver` 不调 PP 队列。

**对抗评审后的收窄（2026-11-13，规则 (c) 第一条：本桶引入的 FAIL → 在新路线入口收窄为 SKIP，不写新的识别逻辑；评审材料 scratchpad/redo/68f-adv/）**
- **N1**（评审 1 类-A，12 个：S006、T002、T003、T005、M002-M006、E005、E007、E008）。形态：派生类的本地类 `model E extends B; end E; E e;` 写在 `extends A(B(x = 2))` 之前；S0 改序后底线的 tree_class 守卫不再触发，F1 只读预挂条目，plain 类修饰是事后挂的、被丢，e.x 取未改值（omc 2.0，S4 终版 1.0）。根因同 TBD-16（plain 不预挂，用户定不做）。改动点：inst.cpp `inst_class` 的 F1，被点名节点上有事后挂条目（`named_posthoc`）就 SKIP，不再看早写深度；S1a4 的 early 守卫（`early_comp_depth_` / EarlyGuard）被它覆盖，删掉。代价：底线 FAIL → SKIP 的 F706、M009、T001、EQ04、EQ09、PX01、Q04、Q05、RV11、RV12、RV13、RV16、RV17、RV18、RV19、XF716、XF717（规则 (a) 允许，列名）。
- **N2**（评审 1 类-B，3 个：F709、V011、M013）。形态：内层新写法预挂（CM-2 嵌套或 RICM 重声明）+ 外层 plain 修饰事后挂到同一个类节点；已建好的消费者读不到（F1 只读预挂；apply_class_mod 补不到经短类别名的组件，M013）。改动点：inst.cpp `inst_class` 事后挂分支，目标节点 `preattached_class_mods` 非空 → SKIP。这些模型底线都 SKIP；顺带 H028、M012（S4 终版 PASS，底线 SKIP）回到 SKIP。
- **N3**（评审 2 类，7 个：Z001、Z002、Z004、Z006、Z010、Z013、Y008：底线 FAIL → 另一种 FAIL）。形态：组件修饰路线的类修饰 `A a(B(x = q))`，A 里 `model B2 extends B; end B2; B2 b2;`；F1 把条目里的组件子修饰当 extends 修饰施加（apply_extends_modifier），flatten 对 extends 修饰的绑定按继承组件所在实例的前缀解析（`a.b2.x = a.b2.q`，omc `q`；Z002 里 B 恰有 q 时静默引用错）。改动点：inst.cpp F1 循环里，组件子修饰（逐层子修饰）的绑定只要不是"字面量及其一元 / 二元运算、括号、字面量数组 / 矩阵、`= break`"（`literal_only_mod` / `literal_tree`）→ SKIP（任何 cref——含常量、time——函数调用、重声明都算要查名）。字面量照施加（N3X07 / 08 / 09 / 14 / 16 / 17、Y017 `= break` PASS）。
- **N3 顺查**（自写 18 个 n3x 探针，三方实跑）：F2（`Real y = E.i` 经包实例取常量）与 F6（Q07 形态的本地类克隆重展开）读的都是常量，常量按写处作用域求值，S4 终版本来就对（N3X01-06 / N3X18 当时 PASS）；它们取值时同样经 E 的 inst_class 走 F1，所以 N3 一并收成 SKIP（按"含常量也 SKIP"的口径）。F1 转发的类子修饰（`A a(B(C(z = q)))`，经派生链修饰带作用域）底线、S4 都是 SKIP（N3X12）。属性子修饰带查名绑定 `A a(B(x(start = q)))`（N3X13）S4 是新 FAIL，N3 已收成 SKIP。没有别的组件修饰路线消费点有这个问题。
- 10 个目标与列名的 FAIL → PASS（XSB01 / EQ06 / PX02 / RV15 / Q07）绑定都是字面量，不受 N3 影响，照旧 PASS。
- 单测：flatten_68f_narrow_posthoc_named（N1，S006）、flatten_68f_narrow_pre_and_posthoc（N2，V011）、flatten_68f_narrow_nonliteral_binding（N3，Z001 SKIP + N3X07 字面量照施加，期望串取 omc 实跑）。

**核对评审后的收窄 N4（2026-11-13，规则 (c) 第一条；核对评审材料 scratchpad/redo/68f-ver/）**
- 问题（核对评审 VN1C、VC01、VC04、VC07、VC08、VD01、VD02、VE01）：底线里写在某条 extends 之前、类型是"经被修饰的 extends 继承来的类"的本地组件（`B b0; extends A(B(x = 2));`）按声明序先实例化，实例类树里那条 extends 要 adopt 的类项还是定义里的、带 classes_via_mod 标记，查到就撞 `tree_class` 的守卫（"inherited class seen through modifier / redeclare"），类型是还没 adopt 的继承嵌套类、所在实例带类重声明的撞 `inst_component` 的守卫（"redeclared class seen from nested class"，SA5），整个模型 SKIP。S0 把它挪到全部 extends 之后，守卫不再触发，模型被放出来，别处的底线老错露出来：VN1C / VC01 / VC07 / VC08 / VD02 底线 SKIP → FAIL（基类里 `model E extends B; E e;` / `model B2 = B; B2 c;`，M010 族，TBD-16；N1 的 named_posthoc 为假抓不到），VC04 / VD01 / VE01 底线 SKIP（omc 拒）→ AI（`Integer i(unbounded = true)`，L008 族）。stage_S0 起就这样。原 :1902-1903 的注释"N1 照旧 SKIP"不对，已改。
- 改动点（inst.cpp `inst_class`）：第一遍每条 extends 的 adopt 前后对比实例类树，记下换掉的类项（下标、换进来的克隆、换之前在不在 classes_via_mod）；第二遍实例化写在某条 extends 之前的本地组件时，把它后面那些 extends 的 adopt 抹掉的 classes_via_mod 标记重新标上、换进来的克隆记进 `pending_adopted_`（`inst_component` 的嵌套类守卫按"父亲还不是实例"判），实例化完再撤——即复原底线守卫此时看到的状态，守卫条件本身照底线不动，查到的类仍是 adopt 进来的那份（S016 / T009 / T012-T018 / M001 这些不靠守卫的 S0 收益不动，照旧 PASS）。
- 实测（对 N3b 41 个变化，差分文件 impl/diff_N4_N3b.txt）：核对评审的 8 个全部 SKIP，与底线逐字节相同（rc、输出、原因串）；S0 带来的同形态底线 SKIP → PASS 退回底线 SKIP：OR2、OR4、A68cIH02、R68cIH2、SA5、CMX07、CMX08、评审 S007、S008、S021、T004、T006、T020、W006、E003、E004、M007、M008、核对评审 VN1D、VN1E、VC03、VC05、VNXA（从收益里划掉）；SKIP 原因回到底线：OR3、OR7、OR19、A68cIH06、A68cIH26、T007、E001、V016、F712、VN1L。41 个变化在 N4 后全部与 B0 逐字节相同。10 个目标照旧 PASS，底线 PASS 无变化。
- 单测：flatten_68f_narrow_early_comp（VC04、VE01 → unsupported）；flatten_68f_inst_order 原用 OR2 / OR4（N4 后照底线 SKIP），改用 S016（期望串取 omc 实跑）。

**终版对 B0（S0 + S1a4 + S1d + S2 + S3 + S4 + N1-N4）的规则 (a) 例外清单**（比较集 20396 个 = 原比较集 19858 + 对抗评审 22 集 410 + n3x 18 + 核对评审 vn* 9 集 85（vn4 / vn4b 19 个为 N4 复核时补，单独对 B0 差分：只 VG03 一处变化）+ 自写 x1-x3 25；差分文件 impl/diff_N4_B0.txt 与 impl/resv4_*.json）
- **在全部 20396 个比较模型上**：底线 PASS 变化 0；底线 SKIP → FAIL / AI / CRASH 0（核对评审的 vn* 也在内）。
- FAIL → PASS（与 omc 逐字节一致）：
  - F1 / F6 原列名 5 个：A68fXSB01、EQ06、PX02、RV15、Q07；
  - S0 族 11 个（评审 3 类 10 个 + 核对评审 VG03）：S016、T009、T012-T018、M001、VG03（`D d(redeclare model Y = Z);`，D 里本地类 H 的组件写在 `extends A` 之前，底线 1.0 FAIL、不撞守卫，S0 起与 omc 一致 3.0，N4 不退）；
  - F1 / F2 组件修饰路线族（评审 3 类，实测）：F705、F707、G003-G007、G009、G010、G013、G014、G016-G018、G025、U004、U005、Q006、Y002、Y005、Y006、Y010、Y012、Y013、Y016、Y017；自写 XF714、XF718（x1，此前漏列）；N3 顺查 N3X07、N3X08、N3X09、N3X14、N3X16、N3X17；核对评审 VN3B-VN3G、VN3J、VN3K、VN3N-VN3P、VN3T、VN3U、VNXD、VNXF（字面量绑定）。评审列的 Z005、Z007、Z008、Z012（绑定是 time / 常量 / 包常量）收窄后改记 FAIL → SKIP。
- AI → 两拒：F4 原列名 TAU01-04、TAU06、RV48、RF4-RF9、UTU08；评审 3 类 21 个：L001、L006、L007、L009、L011、L012、L014、L016-L019、L022-L026、L029、L033、L034、Y018、Y019。
- AI → SKIP（错输出变如实未实现，同口径）：Y001、Y004、Y009、VN3M（each 数组绑定）。
- FAIL → SKIP：N1 的 F706、M009、T001、EQ04、EQ09、PX01、Q04、Q05、RV11-13、RV16-19、XF716、XF717、VN1A、VN1J、VN1K、VN1N；N3 的 Y008、Z001、Z002、Z004、Z005、Z006、Z007、Z008、Z010、Z012、Z013、N3X01-06、N3X10、N3X11、N3X13、N3X15、N3X18、VN3A、VN3H、VN3I、VN3L、VN3S、VN3Y、VNXB、VNXE。
- 底线 SKIP → PASS 178 个、SKIP → SKIP（原因变）46 个、SKIP（omc 拒）不变 27 个、SKIP（omc 拒）→ 两拒 1 个（L028），名字见差分文件。合计 424（FAIL → PASS 69 = 原 68 + VG03）。
- 底线老错没变的（评审 Y003、N002、N004、L002、L008、M010、T010、T011、PB05、PB07、P007、W004；核对评审 VN1B、VC02、VC06、VF11、VF14、VG04）不入库：M010 / T010 / T011 / VN1B / VC02 → TBD-16，L002 / L008 / VC06 / VF11 / VF14 / VG04（`Integer i(unbounded = true)` 老 AI）→ TBD-17，PB05 / PB07 / P007 → TBD-18，Y003 / N002 / N004 / W004 → 65b / 65c。入库：pk-p55r 202、pk-p55r-adv 416、pk-p55r-ver 79（新）、pk-p55r-pp 65。
- 开放问题编号（集成时主代理定，2026-11-13）：68f 先提交，保留 TBD-16 / 17 / 18；68e 的两条顺延为 TBD-19 / 20。

**其它**
- XF721 / XF722 在 S1a4 回到底线 PASS（自写探针 x2；XF724 / XF725 回底线 FAIL）。
- §13 单测计划里 const15、CMX13 / CMY09 / CMX09（const15 族）、F7 / F2 的单测（PX04、CM20、RVC3、RV22、XST04、EQ09、RV13、RW3、RW7、RV17）不写（行为回到底线）；保留 F1 的 PX01→改 PX02、A68fXSB02 视 S1d 结果、F6 的 Q07。
