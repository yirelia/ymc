# tasks/progress.md — 任务进度账（本地离线记录，便于追溯）

用户要求（2026-11-05）：所有任务都在本地记录进度和完成状态。约定：
- 主代理维护本文件，每个任务一节：开始、关键进展、卡点、完成状态；随该任务的提交一起提交。
- 状态只用这几种：排队 / 进行中 / 等门 / 已提交 / 已推送 / 搁置（写原因）。
- 记事实和数字（门的结果、提交号、worktree、子代理 id），不写推测；子代理的工作笔记在会话临时目录，任务结束时把要紧的结论摘进本节。
- 计划和完成摘要仍在 `tasks/todo.md`，踩坑规则在 `tasks/lessons.md`；本文件记的是过程。

## 当前状态（2026-11-13）

| 任务 | 状态 | 执行 | 下一步 |
|---|---|---|---|
| 68e 函数调用的压平名与内建 print | 已提交（门 68ef 全绿），随 68f 一起推送 | — | — |
| 68f 修饰 / 重声明 / 短类 / extends 的剩余 SKIP | 已提交（门 68ef 全绿），随 68e 一起推送 | — | — |
| S5a-2e 后端 Alias 模块 | 已推送 132bab3 | — | — |
| s1fix 修饰名带下标的错误位置 + testsuite 全扫错误位置 | 已推送 b73b1c1（break 去留待用户定 TBD-13） | — | — |
| S1 / S2 专项复验 + 加进 gate | 完成：s12.sh gate 版已接进 gate.sh（门 i2 里 S1 / S2 全绿） | 主代理 | 随下一道门生效 |

## 日志

### 2026-11-05
- S5a-2d（Simplify 1）已提交 9bd9f02 并推送（c52a3a8..9bd9f02，用户批准）。
- 68e：子代理完成；评审手写对抗探针 pk-p54a（29 个）干净；precheck 的变化逐条解释过（testsuite 10 个 SKIP→PASS、MSL GenerationOfFMUs、12 个 bdae print 用例）。
  补丁合进主检出，门 m68e 已排队。预期：testsuite 1918→1928，MSL 500→501。
- S5a-2e：子代理完成实现（solveLinear + Differentiate、Tarjan 序、常量别名变参数），开发语料 1081 个模型 PASS 1025，生成探针 420 个 PASS；precheck 中途到 Simplify 1 段 FAIL 0、CRASH 0。
- 68f：42 个候选（20 个在 scodeinst）；选了三个根因：嵌套类的类修饰、break 修饰、partial 类查找延到重声明之后。
  子代理两次 ECONNRESET，按笔记恢复。中途数：pk-p55 154 个，PASS 100 / 两拒 29 / SKIP 25 / FAIL 0；
  顺带修了基线 FAIL 3 个（CM20、RC28、RC29）和基线误收 3 个（RC23、RD05、RE04）。
- S1 / S2：用户问"S0-S2 是否稳定成熟"。核对：验收后碰 S0–S2 代码的提交 9 个、真修 S1 / S2 的 3 处（61c、62q、68c）；
  四个专项工具（parse_sweep / parse_error_align / scode_compare / scode_dump_compare）不在 gate 里，9 月后没全量重跑。
  用户同意排上复验并把便宜的加进 gate。脚本 s12.sh（full / gate 两档）写好，full 已排队。
- S1 / S2 全量复验（14:26–15:33，ymc = 主检出 gcc-debug，HEAD 9bd9f02），和 9 月验收数一致：
  - round-trip：flattening 2628 个（correct 1843/1843，ERR 8），simulation 383 个（correct 120/120，ERR 1），MSL 4.0.0 2508/2508、MSL 3.2.3 212/212、Buildings 9.1.0 4425/4425；RT-FAIL / TIMEOUT / CRASH 全 0。
  - 语法错误位置：23/23，MISMATCH / MISSING / SPURIOUS 0。
  - 类名集合与顺序：MSL 4.0.0、MSL 3.2.3、Buildings 9.1.0 全 PASS（缺 0、多 0）。
  - SCode dump 抽样（新种子 21 / 22 / 23）：MSL 4.0.0 200/200、MSL 3.2.3 119/119（另 1 个是 OMC list() 的已知怪癖）、Buildings 150/150。
- 核对 flattening 的 8 个 ERR：6 个与 omc 首错起点一致；PDEModelicaTest 是范围外扩展；
  ArrayModification1 / 2（修饰名带下标）omc 1.25.0 实跑报在名字起点 12:7 / 13:13，ymc 报在 `[` 上晚一列，且连带 16 条错
  （OMC 规则 Modelica.g:908-922 v1.25.0）。parse_error_align 默认语料没收这两个，9 月的 23/23 没覆盖到。
  派小桶 s1fix：修这一处、兄弟形态对拍、把错误位置对齐扩到整个 testsuite（做成能进门的开关）。
- 用户（18:0x）："后续门测试校验通过就推送，不要在问我"——此后门绿 + 提交后直接推送，推送区间记在本文件；门没绿或没跑的不推，不 force-push。
- 20:39 68f 子代理交报告：42 个候选放开 11 个（BreakModifier1-3、RedeclareMod11 / 12、RedeclareClass2、ModClass4 / 6、const15、
  RedeclareInClassModification、Bug2727），pk-p55 154 个（PASS 102 / 两拒 29 / SKIP 23，无 FAIL），基线 vs 终版 6040 个探针只 3 个 SKIP→PASS；
  修了基线 FAIL 3（CM20、RC28、RC29）、误收 3（RC23、RD05、RE04）；单测 4 组 28 条；新 PASS 的 scodeinst 6 个待进 slice
  （RedeclareMod11 / 12、RedeclareClass2、ModClass4 / 6、const15）。但 precheck LOCK-TIMEOUT：锁被 m68e 门占着（15:33 起），排了 6 小时。
- 卡点：扫描锁。门一次约 6.5 小时（15:33 起：构建 ~1.5 h，前端扫描 ~0.5 h，后端 5 段 gcc ~1 h，ASan 前端 + 后端 5 段 ~3 h）。
  处理：lock.sh 等锁上限 6 h → 24 h；m68e 门结束后把 gate.sh 的后端 ASan 改成只跑最后一段（--dump-after 最后一段会走完前面所有模块），
  同时接上 S1 / S2 的 gate 版；68f 不再单跑 precheck，评审后直接合进主检出过正式门（门是 precheck 的超集）。
- 20:4x 派对抗评审子代理评 68f（不看实现者结论，只拿 diff + omc 实跑找反例，写 pk-p55a）。
- 21:1x 评审结果：pk-p55a 58 个，68f 上 FAIL 22 / ACCEPTS-INVALID 13（其中 68f 引入 FAIL 16 + AI 8，基线就有 FAIL 6 + AI 5）。
  8 族：flatten 期建的包实例挂起的 partial 错不报；包实例缓存陈旧；applied_class_mods 按 SMod 全局记账串实例；
  类修饰对兄弟类 `B3 extends B` 不生效；包实例上类修饰的值作用域错；嵌套类修饰不查 protected / 重复；break 打到包常量、模型常量后引用处仍折声明值。
  主代理复现 BK01（omc `Real a.y = /*Real*/(a.B.i)`，ymc68f `3.0`）、SC01（omc `4.0`，ymc68f `10.0`），属实。交回实现子代理：能照源码做对的做对，否则收回 SKIP；
  基线的 11 个也至少收成 SKIP。教训（提交时进 lessons）：实现者自己的 154 个探针 0 FAIL，独立对抗评审 58 个找出 35 个错——每个桶都要独立对抗评审。
- 21:2x 据此给 68e 补一次独立对抗评审（68e 只有主代理手写的 pk-p54a 29 个）：68e 的提交压到评审结束（下一道门本来就要凌晨才排得上，不耽误）。
  以后 Alias、s1fix 合入前也各派一次。
- 21:3x 68e 评审结果：pk-p54b 61 个，68e 上 FAIL 19 / AI 29（68e 引入 FAIL 16 + AI 27，9bd9f02 就有 FAIL 3 + AI 2）。6 族：
  实例类里的函数短类别名前缀倒推错；派生类同名类元素没顶替基类的（NFClassTree addInheritedElementConflict）；print 被组件 / 类 / 包名遮蔽仍当内建；
  Evaluate 参数调带 print 的函数；根类外的函数引用根类里的记录类型；实例类里包内函数调同包兄弟按词法拼名。主代理复现 X01（omc `M.c.m`，68e `M.m`）、X26（omc 报错，68e 照打），属实。
  处理：68e 不提交；停掉门 m68e（它验的是要被改掉的版本），按进程树结束 gate 的 bash / python / ymc，手动释放 heavy.lock；
  s1fix 的 precheck 21:36 拿到锁。68e 交回原实现子代理返工（要求同 68f：做对或收回 SKIP，三组探针 0 FAIL / 0 AI）。
  合并顺序：新 68e → 复评 → 主检出换成新 68e → 68f（以原 68e 为底）用 git merge-file 三方合并 → 复评 → 正式门。
- 23:2x 用户："跑完当前任务后，先停止，等我下一步指令"。在跑的四件（68e 返工、68f 返工、s1fix precheck、Alias 的 alias 段扫描）跑完收报告、记进本文件，
  之后不再起新任务（不派复评、不合入、不跑门、不提交），等用户指令。
  此刻：s1fix precheck 前端扫描与 lower 段全绿、后端各段在跑；68f 返工完成在等全探针差分（diff3）；68e 返工 F4 已过，在处理派生类同名顶替的剩余形态；
  Alias 的 alias 段扫描排在 s1fix precheck 之后。
- 23:3x 68f 返工报告：评审 8 族 + 基线 11 个全部 0 FAIL / 0 AI。做对 2 处（CT06 约束修饰里的类修饰 NFInst.mo:1902-1907；
  flatten 回调建的包实例出调用前就报挂起的 partial 错）；其余收回保守 SKIP（包实例已缓存后再挂类修饰、refused_class_mods 记"别处没施加"、
  extends 到挂了类修饰的基类、包实例上带名字的类修饰值、常量上的 break）；嵌套类修饰查 protected / 同层重复（NFInst.mo:1575-1583、NFModifier.mo:711-752）。
  计数：pk-p55 PASS 102 / 两拒 29 / SKIP 23（不变）；pk-p55a PASS 12 / 两拒 18 / SKIP 28 / FAIL 0 / AI 0（主动收回 SC02-04、BK11-12 四个 PASS）；
  42 目标仍 11 PASS；全部探针差分 19067 个只 3 个 SKIP→PASS（A68cIH03 / IH04、R117，已核 omc）；ctest 16/16，gcc / MSVC 零警告；单测新增合计 41 条。
  待留意：SC01 只收成 SKIP，根因没定位；const_type_partial 多解析一次类型路径，正式门看 MSL 有无 PASS→SKIP；refused_class_mods 全局记账会多 SKIP。
- 11-06 s1fix 报告，precheck 干净（PRE-END-s1fix；testsuite 只 2 个 SKIP→BOTH-REJECT：RedeclareComponentInvalidDims1、InvalidPartialFunction，别的段 changed 0）。
  修了 5 处与 omc 1.25.0 不一致：修饰名带下标（Modelica.g:908-922，报完不再连带）；修饰里 redeclare 组件类型不许带维（:956-958）；
  部分函数应用只许作函数实参 / 数组构造元素（:1527-1536 等）；第 1 行语法 / 词法诊断列号整体左移一列（antlr3inputstream.c:222）；
  引号名首字符非法报在坏字符本身。新开关 `parse_error_align.py --testsuite-scan [--all]`：3885 个文件全交 omc，MATCH 32 / MISMATCH 0 / SPURIOUS 0 /
  MISSING 0 / KNOWN 27 / OK 3826，热缓存 61 s（基线 ymc：MISMATCH 3、MISSING 2）。单测新增 62 条断言语句；ctest 16/16，gcc / MSVC 零警告；
  parse_sweep flattening correct 1843/1843（ERR 8→9，多的是 RedeclareComponentInvalidDims1，与 omc 同位置拒）、MSL 2508/2508。
  **待用户定**：KNOWN 27 = `break` 组件 / `break connect` 修饰（BreakComponent*、BreakConnect*）。omc 1.25.0 文法没有（Modelica.g:893-899，报 Missing token ')'），
  OMC e92e6c7c66（2025-09-26）才加，首见 v1.26.0；ymc 自 S1 v2 起支持。按 CLAUDE.md 以 1.25.0 为准应在语法层拒绝；open-questions 分歧账本里没有这条。
  （`= break` 绑定形式 1.25.0 有，Modelica.g:840-845，68f 用的是这个，不受影响。）
- 11-06 68e 返工报告：pk-p54（并入自补 RW 后 166）PASS 122 / SKIP 27 / 两拒 17；pk-p54a 25 / 2 / 2；pk-p54b PASS 22 / SKIP 7 / 两拒 32；三组 0 FAIL / 0 AI。
  F1 别名前缀做对（FnHome 在取名处定前缀，不再从 qname 倒推）；F6 包内兄弟调用做对；F2 派生类同名元素收 SKIP（inst_name_clash，依据 NFClassTree :1976-2049）；
  F3 所有内建名被组件 / 非函数类占用 → SKIP（lookupFunctionName :193-214）；F4 Ceval 碰 print → SKIP，Evaluate 求值不看后标纯度（updateImplicitVariability
  :3890-3936、NFCall.mo:591），X47 做对；F5 记录锚定类须在引用函数的作用域链上。基线 5 个：X47 / X59 做对，X17 SKIP，X23 / X38 两拒；
  自补探针还挖出基线 FAIL RW01 / RW17 / RW18（继承来的方程调同名函数），现 SKIP。全部前端探针差分 19148 个：旧组只 11 个 SKIP→PASS（已核 omc），无 PASS→其它。
  47 个目标仍 10 PASS。单测新增 7 条；ctest 15/15，gcc / MSVC 零警告。待留意：F3 守卫对所有内建生效，正式门看 MSL 有无 PASS→SKIP。
- 11-06 02:1x S5a-2e Alias 报告：(Alias) 段与 omc 1.25.0 逐字节一致；语料 a PASS 69（非恒等 30）、b PASS 960（非恒等 477）、探针 17635 个 PASS 16698
  （新集 s5a2e 584 个，PASS 568 全非恒等），三组 FAIL / SKIP / CRASH / leak 全 0。本段 no-section（a 5、b 45、探针 253）逐个配对到 omc NBAlias 内部错。
  NBAlias 11 块全移植（找简单方程、建集合、Tarjan 第三遍序的因果化、solveLinear + 线性求导、选代表、属性合并及其怪癖、pre 例外、applySimple、收尾、打印）；
  评审要看：fixValues 里 solveBody 导数为 0 不失败（NBSolve.mo:604-606，NBAlias.mo:820 不看 status）→ solve(strict=false) 只在 fix_values 用。
  EqnData 加 equations 视图（Simplify 2 用），Discrete 桶空位查清无漏网。bdae_sweep 语料模式也读 census rc 记 omc-exit。单测 alias_test 5 个；ctest 17/17，零警告。
  precheck 跑在 fixValues 修正之前（修正只动 alias.cpp，precheck 覆盖的段走不到），修正后重跑了构建、ctest、MSVC 和 alias_sweep。
  主代理待办：ADR-008 §6 Q3 加 S5a-2e 段、普查表 Alias 行（a 30 / b 477）、todo、lessons 两条（被调函数失败要看调用方查不查 status；撑过 4096 拿崩溃截断的参考段）。
- 11-06 02:13 四件全部跑完（68e 返工、68f 返工、s1fix、Alias），扫描锁空闲，没有在跑的后台任务。按用户指令停下，等下一步指令。
  主检出仍是旧 68e 的未提交改动（门 m68e 已停、未提交），没动。

### 2026-11-10
- 用户："继续下一步"。break 组件 / break connect 的去留用户还没定，先维持现状（parse 扫描里记 KNOWN），不动。
- 会话重启后临时目录换了（新的在 C:\Users\yangrui\AppData\Local\Temp\claude\c05ed9e74614feaf\…），旧的 C:\Users\yangrui\AppData\Local\Temp\claude\C--dev-ymc\4e49f17e-…\scratchpad
  还在，门脚本、基线、各桶笔记都在里面，照常用。
- 集成：四个桶合进主检出（$S/integ/integrate.sh）。flatten.cpp 以旧 68e 为底、新 68e 与 68f 三方合并无冲突；flatten_test.cpp 唯一冲突是两边都在末尾追加测试组，两段都留；
  tests/probes/README.md、tools/README.md 逐行合；其余各桶独有文件整体拷贝。合计 24 个文件 +1843 / −190，新文件 alias.{h,cpp}、alias_test.cpp、s5a2e.probes、pk-p54/54a/54b/55/55a.probes。
  主检出原来的旧 68e 三个文件备份在 $S/integ/base/。gcc-debug 配置 + 构建 + ctest 后台在跑。
- 门脚本（无门在跑时改，备份在 $S/integ/bak/）：gate.sh 加 Alias 段（gcc + ASan），后端 ASan 只留 lower 与 alias 两段（原来 6 段都跑），构建后接 `s12.sh gate`；
  s12.sh gate 版加全 testsuite 语法错误位置扫描（parse_error_align --testsuite-scan --all，omc 缓存从 s1fix worktree 拷到主检出 .cache/）；precheck.sh 加 Alias 段。
- 四份对抗评审说明写好（$S/rv2/：common.txt + b68e / b68f / bs1 / bal），等集成版构建完拿同一个 ymc 并行评审：68e、68f 复评，s1fix、Alias 首评。
- 15:3x 集成版 gcc-debug 构建零警告、ctest 17/17；ymc 拷到 $S/integ/ymc_integ.exe。四个评审子代理已并行启动（各自并发 ≤ 2，不拿锁）；
  msvc-debug / msvc-asan / msvc-asan-rel 预构建 + ctest 后台在跑（$S/integ/msvc.log）。
- MSVC 三套预构建零警告，ctest 各 17/17。四套编译器都绿，等四份评审。
- s1fix 评审（pk-s1a 52 个）：本桶没引入压平层 FAIL / AI / CRASH；9bd9f02 上同类写法的 9 个 AI 集成版全变两拒。集成版变差 2 例（U03 / U04：第 1 行未闭合引号名 / 字符串，
  omc 报在 EOF，ymc 报在记号起点，左移一列后不再碰巧对上）；9bd9f02 就有的 AI 8 个（外部函数调用实参、修饰名前导点、引号名里非法转义、`1e999` 溢出、指数不完整）。
  交回 s1fix 子代理在其 worktree 补修（只碰 s1fix 自己的文件），修完再拷进主检出。BOM、只用 CR 换行两族基线就有、testsuite 无用例，记未决。
- 68e 复评（pk-p54c 59 个）：集成版 FAIL 35 / AI 16；本桶引入 FAIL 30 / AI 5，其中 10 个是回归（9bd9f02 PASS → 返工版 FAIL）。
  F7 经顶层类路径（import C.Q.g、限定 C.R、别名 = C.Q.g、redeclare = C.f2、extends C.g）到达实例类自己的元素：omc 用类路径命名，ymc 拼实例前缀；
  F8 内建名遮蔽漏网（枚举类型名、for 迭代变量；归约 / 同步算子在遮蔽检查之前接走调用，后者旧有）；F9 嵌套实例类调外层实例类的函数（旧有）。
  教训：两轮都是"先铺开实例前缀、再按反例补洞"，补不完。第三轮改白名单：新命名只用在已证明与 omc 一致的路线（函数体里无限定名、词法在 fn_home_.cls 里找到、不经 import），
  其余一律退回 9bd9f02 行为或 SKIP；要求核对没有任何 9bd9f02 的 PASS 变成别的。交回 68e 子代理。主代理复现 A68eY01（omc 与 9bd9f02 `C.Q.g`，集成版 `M.c.Q.g`），属实。
- 68f 复评（pk-p55b 60 个）：点名的返工守卫都没找到反例；但本桶引入 FAIL 11 / AI 9，9bd9f02 就有 16 个。FN 类修饰改函数类 / 记录类（发射函数和构造器不读 class_mods）；
  PT 包常量绑定不核类型（本桶开路、基线的洞）；BC 字段缺省被 break 去掉后构造器缺实参（同上）；旧有 CP、LP。主代理复现 FN（`A a(B(f(k = 2)))`：omc k = 2.0，集成版 1.0，9bd9f02 SKIP）。
  交回 68f 子代理，同样按白名单原则：目标是函数 / 记录类的类修饰一律 SKIP，包常量补类型检查，构造器缺实参拒绝。
- Alias 评审（s5a2e-adv 60 个 + 随机 220 个）：主线全对、FAIL 0；本桶引入 CRASH 9、leak 8。族 1 String 拼接集合：omc 在 createReplacementRules 对每个被换变量
  solveBody 时 makeOne(String) 失败无段（NBAlias.mo:817-821），ymc 的 fix_values 无属性就跳过、照打；族 2 自指方程再接简单方程：causalize 匹配失败 stop 后
  replacement_rules 没查 unsupported，fix_values 里 repl_.at 抛 std::out_of_range（主代理复现属实）。交回 Alias 子代理修。
- 现状：四个桶都在返工 / 补修（68e 第三轮、68f 第三轮、s1fix 补修、Alias 补修），主检出的集成版不动；全部回来后重新集成 → 复评 → 正式门。
- 16:1x 用户："执行完成后等我指令再确定是否进入下一步"。四个返工跑完收报告、记进本文件后停下，不重新集成、不派复评、不跑门，等用户指令。
  此刻：68e 白名单改完 F7 / F8 在构建；68f FN 白名单（class_mod_kind_ok）、PT、BC、CP、LP、RG04 都已改；s1fix 读完 OMC 源码在改；Alias 刚接手。
- 68e 第三轮报告（白名单）：实例前缀只用在"函数体里的名字首段按词法查到、落在 fn_home_.cls 或它里面的包、途中不经 import"这一条路线（FnHome.named；emit_function 再核到达方式：
  不经短类别名、函数体词法在 home.cls 里、不经 extends 拿基类的体），其余退回 9bd9f02 老规则或 SKIP。F7 经 import / 限定 C.R / 别名 / redeclare / extends 到达的按类路径或 SKIP；
  import 穿过 protected 段 SKIP（NFLookupState.checkProtection :442-467）；F8 遮蔽检查挪到 call() 开头、认迭代变量、枚举同名也算（assert / reinit / terminate 除外）；F9 嵌套实例类调外层 → SKIP。
  四组探针（pk-p54 166 / p54a 29 / p54b 61 / p54c 59）全部 0 FAIL / 0 AI；全部前端探针差分 19207 个：没有任何 9bd9f02 的 PASS 变成别的，旧组只 11 个 SKIP→PASS；
  47 目标仍 10 PASS；ctest 15/15，gcc / MSVC 零警告。白名单收回的：pk-p54b X01-X08 / X10 / X11（别名）、X56、pk-p54 RW04 → SKIP。待留意：F8 对所有内建名生效，门看 MSL。
- s1fix 补修报告：U03 / U04 等未闭合记号改报 EOF 位置（antlr3inputstream.c:249、antlr3lexer.c:258-264）；外部函数调用照 Modelica.g:484-489 重写；修饰名前导点报错（:910、1889-1893）；
  引号名转义照 BaseModelica_Lexer.g:389-405；实数上溢报 Overflow（:1779-1782），下溢换 0.0；指数不完整报错（:447-449）；部分应用只收命名实参、无后缀（:1558-1565）；
  lexerError 分两个旗（error_at 报一条后 omc_aborted_；四处无条件报的 error_unconditional），词法错只报第一条。pk-s1a PASS 24 / 两拒 28，0 FAIL / AI；
  --testsuite-scan --all MISMATCH / SPURIOUS / MISSING 0（KNOWN 27 break）；parse_sweep correct 1843/1843、MSL 2508/2508；探针差分 35995 个只有 AI→两拒 17（pk-s1a）与两拒内部变化，
  无 PASS / SKIP 变化；单测 YMC_CHECK 105→226；ctest 16/16，零警告。拷进主检出时 src/parse/ 5 个文件要一起拷（token.h 的 kLexerErrorMark）。
  未决（基线就有）：BOM、只用 CR 换行、`function der` 当类名（DUQ4）、`--std=3.2` 不带 --strict 的 pure、break 去留。
- 68f 第三轮报告（白名单）：类修饰按目标类种类判定（class_mod_kind_ok，5 个调用点）：model / class / block / connector / expandable / type / package 照常；
  record / operator record / function 只挂上，flatten 一旦解析到这些类的调用或构造器就 SKIP（class_modded_callables）；其余 SKIP。PT 包常量值与声明类型不符 SKIP（const_value_mismatch）；
  BC 构造器缺实参无缺省 SKIP（NFFunction.mo:1164 UNFILLED_SLOT）；CP 改递归 type_path_partial；LP01/02 经短类别名声明的组件 SKIP（type_chain）；LP03/04 类修饰目标被类重声明顶替 SKIP；RG04 恢复 PASS。
  pk-p55（补 RW 8 个，共 162）PASS 107 / 两拒 29 / SKIP 26；pk-p55a PASS 12 / 两拒 18 / SKIP 28；pk-p55b PASS 23 / 两拒 17 / SKIP 20；三组 0 FAIL / 0 AI。
  全部探针差分 19067 个：没有任何 9bd9f02 的 PASS 变成别的；133 个不同里 130 个归 68e（与 68e 底逐字节同），68f 自己 3 个 SKIP→PASS。42 目标仍 11 PASS（9 个 scodeinst 按文件 Result 也过）。
  ctest 16/16，gcc / MSVC 零警告；本桶单测合计 57 条。RW06 碰巧对的现在保守 SKIP。
- Alias 补修报告：族 1 String 拼接集合照 NBAlias.mo:817-821 / :1414-1420 无条件 solve / 求导，String 求导 stop → SKIP（原 leak 8 → omc 无段、ymc SKIP）；
  族 2 causalize 后补 unsupported 检查（其余 stop 点逐个核过无漏查），照 VariablePointers.fromList（NBVariable.mo:1738-1784）去重，去重后方程多于变量 → SKIP
  （PFPlusExternal / match_pf_fair 没移植，不猜；原 CRASH 9 → SKIP）。顺带修回归 LwaTCast2：simplify.cpp number() 认 CAST(Real) / UNARY 按 Ceval 求值（NFSimplifyExp.mo:1470-1479）。
  s5a2e 589 个 PASS 573；s5a2e-adv 60 个 PASS 28 / 无段 20 / SKIP 12，0 FAIL / CRASH / leak；alias_sweep a PASS 69、b PASS 960、全部 bdae 探针 17700 个 PASS 16731，FAIL / CRASH / leak 0
  （b 一轮里 8 个 60 s 超时是机器忙，空闲时重跑恢复）。alias_test 5→8 个；ctest 17/17，零警告。未决：方程多于变量的 PFPlusExternal 匹配。
- 四个返工 / 补修全部完成（68e 第三轮、68f 第三轮、s1fix、Alias），按用户指令停下，等用户决定是否进入下一步（重新集成 → 复评 → 正式门 → 提交推送）。
  主检出仍是上一次的集成版（未提交），没动。
- 用户："继续下一步"。第二次集成（$S/integ/integrate2.sh）：flatten.cpp 以旧 68e 为底、68e 第三轮与 68f 第三轮三方合并无冲突；flatten_test.cpp 末尾追加冲突照旧两段都留；
  其余各桶文件整体拷贝（Alias 多了 simplify.cpp 与 s5a2e-adv.probes，s1fix 多了 pk-s1a.probes，68e 多 pk-p54c，68f 多 pk-p55b）。25 个文件 +2435 / −226。
  四套构建 + ctest 后台在跑（$S/integ/build2.log）。之后：68e、68f 各派一次复评（集成版二进制）；s1fix、Alias 由主代理核对评审探针集；然后正式门。
- 第二次集成：四套构建零警告，ctest 各 17/17；ymc 拷到 $S/integ/ymc_integ2.exe。8 个新前端探针集（pk-s1a、pk-p54/54a/54b/54c、pk-p55/55a/55b，647 个）在集成版上
  0 FAIL / AI / CRASH，计数与各子代理报告逐一相同（合并没改变行为）。68e、68f 第三轮复评已派（$S/rv3/b68e.txt、b68f.txt；重点：白名单外是否真退回 9bd9f02、新守卫的误伤与漏网）。
- 用户："继续"。正式门 i2 与复评并行开跑（`gate.sh i2` + 8 个新探针集，结果 $S/gatei2.out）：复评干净则直接用这道门提交，有问题修完重跑，这道门先暴露全语料层面的问题。
  门跑着期间主检出不动。提交计划：四个桶各一个提交，68e 用 W1 的 flatten.cpp / flatten_test.cpp（只含 68e），68f 再换成合并后的最终版；文档等复评结论再写。
- 22:1x 第三轮复评结果：
  - 68e（pk-p54d 63 个）：**白名单守住**，FAIL / AI / CRASH 0。退步 15 个（9bd9f02 PASS → SKIP）：size 被占用（omc 不查名直接造 SIZE，NFCall.mo:2300 / 2321）9 个；
    外层非包类被当成实例（`D.C c` 经类路径，omc 用类路径）4 个；相同的重复函数元素 2 个。交回 68e 第四轮修退步。
  - 68f（pk-p55c 53 个）：class_modded_callables 漏网，本桶引入 FAIL 22 / AI 7——函数 extends 链（collect_fn_sig 不查）、记录短类链中间层（build_record_ctor_mod 只查两端）；
    退步 7 个，其中自引用 partial 包 6 个（NFLookup.isSelfReference，MSL Media 可能常见）。主代理复现 FN01 属实。交回 68f 第四轮：守卫挪到所有路线必经的发射点、沿整条定义链查；SR 族必须做对。
  - 门 i2 继续跑（构建全绿、S1 / S2 全绿、slice PASS 697→702 / 实跑不一致 0），拿全语料信息；终版要在第四轮之后重跑门。
- 22:2x 主会话进程退出过一次：门 i2 在探针段被中断（进程已无，留下的 heavy.lock 手动释放），68e、68f 第四轮的构建也断了，已 SendMessage 让两者按笔记接着做。
  门 i2 不重跑（终版要在第四轮后重跑），改在锁下对集成版 v2 跑 precheck i2p（无 ASan，约 3 小时）拿全语料信息，重点看 MSL / testsuite 有无 PASS→SKIP。
- 11-11 precheck i2p（集成版 v2，全语料，无 ASan）结果：**无任何退步**。探针 19539 个 0 FAIL / AI / CRASH，逐组只有 SKIP→PASS 与 8 个新组；
  scodeinst SKIP→PASS 9（42 目标里的 9 个）；testsuite nf-live PASS 1928→1939、变化 23 个全是 SKIP→PASS / SKIP→两拒，AI 3 为老账；MSL 514 个 PASS 500 不变；
  bdae 五段（含新 alias 段：a PASS 69、b PASS 960、探针 PASS 16743）全部 FAIL / CRASH 0，变化只有 SKIP / 未接纳 → PASS 与新探针。等 68e、68f 第四轮。
- 68e 第四轮报告：三族退步全修。Z-SIZE 照 NFCall.mo:2300/2321/2325/2399-2410 size / array 不查名（looked_up_builtin）；OUTER-CLASS 收窄为外层类正是 home 实例上层实例的类，类路径到达退回老规则；
  DUP 复刻 NFClassTree.addInheritedElementConflict 的留存规则（omc_kept_element）+ 保守同形判定（class_same_shape），其余 SKIP。五组 pk-p54..54d 0 FAIL / 0 AI，pk-p54d PASS 36 / 两拒 24 / SKIP 3；
  全部前端探针 9bd9f02 对终版没有 PASS 变别的；47 个 testsuite 目标 10 PASS。主代理在终版二进制上复跑 pk-p54d 计数相同。第四轮复评已派（$S/rv4/b68e.txt，68e 单独二进制）。
- 68e 第四轮复评（pk-p54e 57 个，$S/rv4/68e/）：Z-SIZE 放开无新错、OUTER-CLASS 收窄无 FAIL / AI；但 DUP 同形豁免（class_same_shape）暴露 FAIL 15 + AI 1（不比字符串注释、组件类型名在两份所在类里解析不同、
  所在类 import 不在类树、看不到 extends 修饰里的 redeclare；r3 全 SKIP，9bd9f02 同样错）；退步 11 个（带 algorithm / annotation / 短类的相同重复函数、相同重复包、`u+1` 对 `u+1.0`、
  经类路径到达的 C 调 D 的函数）；9bd9f02 就有的：SIZE-USERFN 6 FAIL（omc 按首段名 size / array 直接造内建，NFCall.mo:2300/2321/2325）、非函数重复元素 2 FAIL + 2 AI。
  主代理复现 W01 / W12 / W15 / W17（FAIL）与 W30 / W34（PASS→SKIP）属实。交回 68e 第五轮：DUP 改为按 omc 留下的那份（kept）在它自己的作用域发射，算不准就 SKIP，不再比形状；
  SIZE 照首段名规则做对；非函数重复按 NFClassTree 规则做对或 SKIP；W27-W37 必须变回 PASS。
- 68f 第四轮报告：守卫挪到发射必经点（class_modded_hit：collect_fn_sig 及其 to_long、long_class_of 每层、record_fields_ex、record_class_thru_alias、oprec_ctor_fns），链上任一层挂类修饰就 SKIP；
  只改属性的记录类修饰不记名单（修回 pk-p30 XM41）；SR 自引用照 NFLookup.isSelfReference 免查首段 partial（SR01-06 PASS）；PP 限定类及基类组件类型全查 partial（NFInst.instPackage）；
  TF01 短类链每层并修饰；顺带修 XBK12（外层 extends 修饰覆盖 break）。pk-p55 PASS 107、55a PASS 13、55b PASS 23、55c PASS 16 / 两拒 9 / SKIP 28，四组 0 FAIL / 0 AI；
  全部探针差分新组外无新差异，9bd9f02 PASS 只丢自己新组里的 RW06 / ZOR01（保守 SKIP）；42 目标 11 PASS。主代理复跑 pk-p55c 计数相同。第四轮复评已派（$S/rv4/b68f.txt）。
- 68f 第四轮复评（pk-p55d 58 个，$S/rv4/68f/）：发射点守卫约 45 种写法未被绕过，MSL 17 个常见写法 + 10 个整例子无退步。问题：PKG-PROT 6 AI（本桶引入：包类修饰改 protected 常量，omc NF_MODIFY_PROTECTED）；
  RC-NAME 4 FAIL（记录构造器调用名按全局第一个实例取，9bd9f02 就有，本桶放开后多暴露）；退步 9 个（SR 短类链进 cannot_enter 无自引用豁免 6、break 被外层组件修饰 / 包链覆盖 2、函数形参类型类修饰 break 1）；
  9bd9f02 就有的 AI 8（自引用豁免对 import / encapsulated 放太宽、PP 只查最后一段限定类、TF02）+ FAIL 1（RC05）。主代理复现 PR01 / RC01 / SS01 / XB01 / SB01 属实。交回 68f 第五轮。
- 68e 第五轮报告：DUP 删掉形状比较，改为 omc_kept_status（Same / Replace / Unknown），Replace 按 omc 留下的那份在它自己的作用域发射（NFClassTree.replaceDuplicates），Unknown SKIP；
  OUTER-CLASS 看首段本地命中层是否实例；SIZE / ARRAY 按首段名一律内建（call_head，含 `size.f`、前导点）；非函数重复：表达式查到 Replace 的类 SKIP、前缀不同 SKIP。改动只在 flatten.cpp。
  六组 pk-p54..54e 0 FAIL / 0 AI，pk-p54e PASS 44 / SKIP 7 / 两拒 6，W27-W37 全 PASS；全前端差分 9bd9f02 PASS 无丢失；47 目标 10 PASS。主代理复跑 pk-p54d / 54e 计数相同。第五轮复评已派（$S/rv5/b68e.txt）。
- 68f 第五轮报告：PKG-PROT 照 NFInst.mo:1579-1581 只查挂在长类包节点自己身上的类修饰（PG01-06 保持 PASS）；RC-NAME 按调用处实例的类节点命名（RC05 老错一并改对）；SS / XB / TF01 退步全回 PASS；
  SB 自引用加"按类名找到且非 import"与 encapsulated 判定；PP 每次进类扫整个类（enter_comp_types_partial，QuietLookup 内跑）；TF02 拒。pk-p55d PASS 33 / 两拒 18 / SKIP 7，五组 0 FAIL / 0 AI；
  全差分新组外无新差异，9bd9f02 PASS 只丢已知 RW06 / ZOR01；42 目标 11 PASS。主代理复跑 pk-p55d 计数相同。第五轮复评已派（$S/rv5/b68f.txt）；68e、68f 第五轮单桶二进制的全语料信息扫在锁下跑（$S/rv5/corpus_*.out）。
- 68e 第五轮复评（pk-p54f 51 个）：Replace 发射本身做对，但"留哪份"模型错——omc expandExtends 先 join 基类自己的 dup 条目、addInheritedElementConflict 对已在 dup 条目里的新条目不比索引（F1，9bd9f02 PASS → FAIL 8）；
  前缀冲突 SKIP 太宽、CompTypeLookup 只挂 5 处（SKIP 退步 13）；9bd9f02 就有的：Unknown 不拦常量求值、函数输入缺省里的 size 不折、array 命名实参 AI。主代理复现 V01 / V29 / V33 / V24 / V37 属实。
  交回 68e 第六轮：逐行移植 omc 类树 dup 逻辑算 kept（不再近似），前缀检查照 Class.isIdentical 精确条件，组件类型 / extends 名在共同入口放行；要求报告前自造兄弟形态探针过一遍。
- 68f 第五轮复评（pk-p55e 56 个）：PKG-PROT / RC-NAME 模型层 / SR / PP 入口 / break 都确认对；单桶全语料扫 scodeinst / MSL（+1）/ testsuite 无退步。问题：函数体里记录构造器仍按第一个实例命名 4 FAIL；
  SKIP 退步 21（PP 扫描沿 SCode 看不到重声明 10、常量声明处 break 被外层盖回 5、break 被修饰重声明盖回 3、类型链属性检查不认 unbounded / fixed 5）；扫描漏网的老 AI 12。主代理复现 RC03 / PT01 / DB01 / RB03 / TA12 属实。
- **处理原则调整（主代理）**：已五轮，每轮修老错加的守卫都带出新退步。改为：本桶引入的 FAIL / AI 与 9bd9f02 PASS 退步必须修；9bd9f02 就有的老错能精确修才修，否则登记 tests/probes/KNOWN.txt + docs/open-questions.md 账目，留后续桶。
  68f 第六轮、68e 第六轮（补充）都按此执行。
- 第五轮单桶全语料扫（$S/rv5/corpus_68f.out / corpus_68e.out）：68f 无退步（MSL +1 GenerationOfFMUs）；68e 退步 3 个——MSL Fluid BranchingDynamicPipes、NonCircularPipes 与 testsuite Ticket4258b PASS→SKIP，
  都是前缀冲突守卫（tree_dup_prefix_clash，复评 F2）误伤，已转给 68e 第六轮，必须回 PASS。
- 68f 第六轮报告：函数体记录构造器按 fn_home_ 实例命名（RC03 / RF05-07 PASS）；PP 扫描带重声明名单（PT / PP10 / MC02 回 PASS）；声明处常量 break 并入 const_breaks 挂起 / 撤销（DB 回 PASS）、
  redeclare_component 撤 const_breaks（RB 回 PASS）；类型链属性检查认 unbounded / startOrigin / fixed（TA 回 PASS）；老 AI 精确修 3 个（LK01 / 02 / 06，绑定扫描），其余 10 个登记 KNOWN + open-questions「探针登记的老错」。
  pk-p55e PASS 34 / 两拒 11 / SKIP 1 / KNOWN 10，六组除 KNOWN 外 0 FAIL / 0 AI；全差分新组外无新差异；42 目标 11 PASS。主代理复跑 pk-p55e 计数相同。第六轮复评已派（$S/rv6/b68f.txt），单桶全语料扫在锁下跑。
  集成注意：68f 也改了 docs/open-questions.md 与 tests/probes/KNOWN.txt，68e 若也改，集成时以 HEAD 为底三方合并。
- 68f 第六轮单桶全语料扫（$S/rv5/corpus_68f6.out）：scodeinst PASS 707、MSL PASS 501、testsuite PASS 1939，变化全是 SKIP→PASS，无退步。
- 68f 第六轮复评（pk-p55f 60 个）：函数体记录构造器多段名（`A.R`，omc 打定义路径）与嵌套包记录（a2.Fs.R）各错，本桶引入 FAIL 7；PP 重声明名单只挡类型路径首段、按名字记，SKIP 退步 11；
  包里任一常量挂 break 就整包 SKIP，退步 6；startOrigin 放行名单是错的（NFBuiltin Real 没有 startOrigin）。主代理复现 URP08 / URW01 / PU02 / DC01 / TU02 属实。
  交回 68f 第七轮：**撤掉第五、六轮只为收 9bd9f02 老 AI 加的 SCode 层 PP 整类扫描**（两轮都在近似 omc instPackage、每轮带出退步），对应老 AI 登记 KNOWN；保留守住本桶自己放开路线的守卫；修 F1 / F2 / R2 / startOrigin。
- 68e 第六轮报告（2026-11-11）：DUP 不再近似，把 omc NFClassTree 的重复条目逻辑逐行移植成 OmcTree（fromSCode / expand / expandExtends / addInheritedElementConflict / replaceDuplicates / checkDuplicates）；
  三种查名语境（表达式 = 全替换后、组件类型 = 本层替换前、extends 名 = 原槽）；前缀冲突按 Class.isIdentical 只比两份都展开过的长类；eval_user_call 遇换份 / 无法判定 SKIP；size 缺省槽折叠；array() 命名实参 / 无实参拒。
  pk-p54..54f 七组 0 FAIL / 0 AI，V01-08 / V17-20 / V28-36 PASS；自查 pk-p54g 50 个仅 1 AI（R6B06，9bd9f02 老错，登记 KNOWN + open-questions「68e 留账」）；三个语料退步单跑回 PASS；全前端差分无 PASS 丢失。
  主代理抽查 pk-p54f / 54g（final5）计数与报告相同、0 FAIL / AI / CRASH。第六轮复评已派（$S/rv6/b68e.txt，探针 pk-p54h），单桶全语料扫（corpus_68e6）排在锁后。
- 68f 第七轮报告（2026-11-11）：撤掉第五、六轮 SCode 层整类扫描（enter_comp_types_partial / comp_types_partial / dims_cref_partial 及四处挂钩）与第三轮"同类别组件也查"，对应 24 个老 AI 登记 KNOWN（共 34 条，open-questions 加行，留"查名统一走实例"后续桶）；
  保留 type_path_partial（被引用常量自身类型路径）及其精化。record_ctor_call 函数体分支前移：首段在 fn_home_ 内 → fn_home_path_ + 段，首段在外的多段名 → 定义路径（F1 / F2 7 个 FAIL → PASS）；
  包常量 break 改在 lower_pkg_comp 真引用时才 SKIP（R2 6 个回 PASS）；startOrigin 从函数变量与模型变量两路都去掉、unbounded 只认 Real、丢弃属性值须 Boolean（TU02/04/05/08 两拒）。
  七组除 KNOWN 外 0 FAIL / AI / CRASH；9bd9f02 PASS 只丢已知保守 SKIP RW06 / ZOR01；42 目标 11 PASS；删 8 条靠扫描的单测断言、新增第 10 组 6 断言。
  主代理抽查 pk-p55e / 55f（final7）计数与报告相同。第七轮复评已派（$S/rv7/b68f.txt，探针 pk-p55g），单桶全语料扫（corpus_68f7）排在 68e6 之后。
- 用户纠正（2026-11-11）："方案总是在反复，重新修"。主代理复盘根因写进 lessons（修法挂在共用路径、老错修不修中途改口、没有收口条件、SCode 近似 omc 实例树查名），
  定三条不再改的规则 (a)(b)(c)（tasks/todo.md「68e / 68f 收敛方案」）。用户选定：**两桶都从 9bd9f02 重做**，W1 / W2 只作参考。
- 同时回来的两份复评（旧方案最后一轮，结果只作参考 / 回归床）：
  - 68e 第六轮（pk-p54h 53 个）：本桶无 FAIL / AI，但 9bd9f02 PASS → SKIP 11 个（经两条继承路径的函数做元素级重声明 10 个、重复记录类构造 1 个）；老错 FAIL 11 / AI 9。
  - 68f 第七轮（pk-p55g 60 个）：本桶 FAIL 26 / AI 2——函数体记录构造器经 import 命名错（打成实例路径 / 别名段）、包常量 break 后包内类体的非限定引用取了声明值（break 检查只挂在 lower_pkg_comp）；
    模型变量路径 unbounded / fixed 值类型不查是老 AI。无 9bd9f02 PASS 退步。
- 单桶全语料扫：68e 第六轮（corpus_68e6）testsuite SKIP→PASS 10，与第一版 68e 完全同一批，scodeinst / MSL 无变化；68f 第七轮（corpus_68f7，含旧 68e 底）scodeinst +9、MSL +1、testsuite +21，均无退步。
  这 22 个 SKIP→PASS 就是重做的目标清单。
- 用户（2026-11-11）定做事与验证流程五步：学 OMC 处理逻辑 → 开发方案 + 与 OMC 对比的验收标准 → 独立 agent 评审方案防偏移 → 评审通过再开发、验证 → 经验沉淀。
  已写进 `自研编译器技术方案-C++.md` §7.4（§7.1 任务卡模板同步加字段）、记忆 ymc-five-step-workflow、lessons（2026-11-11 五条）；todo「68e / 68f 收敛方案」改为按五步执行。
- 用户同意（2026-11-11 "OK"）：先单独提交 s1fix、Alias；68e / 68f 开第 1、2 步。
  - 主检出改成 HEAD + s1fix + Alias（$S/integ/prep_s1al.sh）：集成版 v2 的 42 个改动 / 未跟踪文件先整份备份到 $S/integ/bak_v2/；flatten / inst / flatten_test 恢复 HEAD 内容，pk-p54* / pk-p55* 移出；
    s1fix、Alias 文件按 worktree 重拷，两个 README 照旧合并。正式门 `gate.sh s1al pk-s1a` 已在后台跑（结果 $S/gates1al.out）。
  - 提交用文档已写：ADR-008 §6 Q3（S5a-2e）段、普查表 Alias 行（a 30 / b 477）、todo（s1fix、S5a-2e）、open-questions（TBD-13 break 去留、TBD-14 s1fix 老差异、TBD-15 PFPlusExternal）、lessons 两条（status、4096）。
  - 68e、68f 第 1、2 步（学 OMC 逻辑 + 任务卡）子代理已派（通用说明 $S/redo/plan_common.txt，产出 $S/redo/68e/T-S3-68e-redo.md、$S/redo/68f/T-S3-68f-redo.md），不改仓库、omc 串行跑以免干扰门。
- 68e 重做第 1、2 步完成（任务卡 $S/redo/68e/T-S3-68e-redo.md，探针 112 个三方实跑）：10 个目标 9 个能在规则内做——AssertTest4-7（实例类里的 external 用户函数 print，放开点 try_emit_user_call）、
  IntegerTest（顶层内建 print，放开点 builtin_call 的 !bf 支）、4 个函数变量记录类型（放开点 emit_function，rec_flat_name 返回空之后）。
  越界：AlgorithmNoRetCall（函数体里内建 print 要把外层函数后标 impure，NFFunction.mo:1645-1682，必须改共用纯度判定，会让 9bd9f02 PASS 的常量求值变 SKIP）——建议放弃、留纯度桶。
  协调：MSL GenerationOfFMUs 走 derivative 注解 → fn_flat_name，是 68e 的路线，todo 原列在 68f。两项等 68f 报告回来一起请用户定。
- 68f 重做第 1、2 步完成（任务卡 $S/redo/68f/T-S3-68f-redo.md；新探针 8 组 + 旧评审 567 个三方实跑）：11 个 testsuite 目标都核实是 68f 自己放开的（底线与旧 68e 都 SKIP）。
  规则内能做 8 个：BreakModifier1-3（flatten emit_variable / plan_attributes）、Bug2727（inst.cpp:1673 extends 路线清 extends_mods）、ModClass4、ModClass6、const15（inst_package 缓存未命中支）、GenerationOfFMUs。
  越界待用户定：GenerationOfFMUs 与 68e AssertTest 同根（flatten.cpp:11487）；RICM 新代码须放在 SKIP 点之前（门控与 SKIP 条件等价）；RedeclareClass2 / Mod11 / Mod12 要改共用 inst_component（组件栈记账、:624 由停改挂起）；
  规则 (c) 对"放开后的模型错在老路线上"的读法。另：Bug2727 omc 1.25.0 输出与文件 Result 不同，落地时记分歧账；底线二进制换成 s1fix / Alias 提交后的构建。
- 用户定（2026-11-11）：① AlgorithmNoRetCall 做，纯度判定与常量求值的纯度检查两处共用路径一起改；② GenerationOfFMUs 归 68e（注解路线多一个放开点，与 AssertTest 同一套条件）；
  ③ 68f 的 RICM 与 partial 组（RedeclareClass2 / Mod11 / Mod12）都做，预设退路：机械比对里底线有输出的用例一个变了就整项放弃、不修补；
  ④ 规则 (c) 补充："如果是之前未考虑到的，将之前漏掉的内容都补充上"——放开后的模型错在方案漏考虑的老路线上，照 OMC 补齐（回第 1–3 步补方案、再评审），不收窄、不只登记。已写进 todo 规则 (c) 与规范 §7.4 第 2 步 e。
  两张任务卡交回原规划子代理补改，改完派独立 agent 评审（第 3 步）。
- 68f 任务卡补改完成（11 个目标；§6.6 老路线分三类：甲 放开条件已排除 / 乙 本卡补齐 F1-F5 / 丙 补不了）。F1 extends 挂了类修饰的类（类节点修饰在外层，实测 PX01-03）、F2 限定名进"基类挂了类修饰"的类走 pkg_instance、
  F3 类修饰补到经短类别名声明的组件、F4 break 变量被丢弃属性的类型核查（门控）、F5 类修饰挂到被外层重声明顶替的类上丢弃。RICM / partial 组改动边界与退路写入 §6.5 / §6.3，分 S1-S4 四阶段实现便于差分归因。
  待用户定：F1/F2/F3/F5 会把 13 个底线 FAIL 变 PASS（规则 (a) 例外）；老路线范围口径；PP 族（要 65c 的查找迁移 + instExpressions，建议交 65c）；ACCEPTS-INVALID→拒绝是否算允许的变化。
- 用户定（2026-11-11，68f 三问）：① 规则 (a) 例外放宽为"照 OMC 移植、变化后与 omc 一致的都允许"（FAIL→PASS、AI→两拒，逐个列名；FAIL→SKIP 主代理按旧口径补注为进步）；② 老路线补齐范围 = 只补消费本桶放开数据的路线，无关老错仍 KNOWN；
  ③ **PP 族本桶做**（主代理原建议交 65c）：经限定名进类建包实例、包实例化时查 partial、补 omc 的 instExpressions 步骤。68f 任务卡再交规划子代理补这一族（第 1、2 步）。
- 68e 任务卡补改完成（11 个目标）：AlgorithmNoRetCall 用"新口径"纯度（只多内建 print 来源，旧 fn_marked_impure 不动）+ eval_user_call 纯度门按语境二分（Ceval 语境旧口径、化简语境新口径），
  配套 print_only_impure_ 与求值器走到 print 判 SKIP；13 个底线 PASS 必须保持，11 个点名 FAIL→PASS；放开点 G（derivative 注解路线）与 A 共用 sibling_fn_name；老路线表 15 行。
  主代理按规则定：U05（final 参数，底线 FAIL）仍错就 KNOWN；U16 / :31691 看旧后标的老差异记进后续纯度桶。68e 任务卡送第 3 步独立评审。
- 68f 任务卡第三版（PP 族进本桶，作阶段 S5 排最后，落实例层：pp_enter 钩子挂在 omc 调 instPackage 的各处、沙盒检查实例化后还原全局表、只读的 instExpressions 等价遍历、import 两种时机、真错误报出；
  预计 44 个底线 AI→两拒 + F1-F5 的 13 个 FAIL→PASS + TAU01-06 AI→两拒，逐个列名；阶段 S1-S5 各自对上一阶段构建差分，S3 / S4 / S5 各带整项放弃的退路）。
  主代理按已定规则处理其四个待定项：非 partial 的 instPackage 错误不进本桶（无关老错 KNOWN）；检查实例化设确定性预算、宁漏报不误报（漏报不退步、误报违反 (a)）；照 OMC 移植的判定报真错误（拿不准仍 SKIP）；
  丙-2 / 丙-4 两处静默错风险按规则 (c) 现在补进方案，精确补不了再报用户。交规划子代理出第四版。
- 68e 任务卡独立评审（$S/redo/68e-review/，探针 r1-r4 共 46 个）：**不通过**。
  ① Q-D2 用 allow_param || ceval_ext_ 判 Ceval 语境不对：常量数组两条路线、记录常量 :1658 / :1799 / :28384、eval_const_value 的类型化路径在 omc 都是 Ceval（NFEvalConstants.mo:112-146），按卡会拒折 → U23 等底线 PASS 退步；拒折后发射被调函数的 SKIP 收不回。
  ② sibling_fn_name 挡不住"派生类声明同名函数顶替基类里的兄弟函数"（omc 克隆后父节点是组件实例，NFClassTree.mo:521-523），RV3A1 / A6 / G1 会打错函数体。
  ③ omc 对形参 / 局部 / 输出绑定和维的纯度是深遍历（NFFunction.mo:2230-2256、NFBinding.mo:683-697），卡按浅遍历会把 SKIP 变 FAIL（RV1Q05 / Q12）。
  ④ todo 没同步用户决定（主代理疏漏，已补，lessons 加一条）；Q-D5（结构参数判定等三个读者）、Q-D6（求值器走到 print 判 SKIP）超出已批准的两处共用路径，需用户明示同意。
  主代理按已定范围口径定③取"只对 print 来源深遍历"（声明 impure 的老错不在本桶）。待用户答 Q-D5 / Q-D6 后交回 68e 规划子代理改卡，再评审。
- 门 s1al（HEAD + s1fix + Alias，$S/gates1al.out）**全绿**：四套构建 ctest 17/17、零警告；S1 / S2 专项全绿（parse-scan MISMATCH 0，KNOWN 27）；slice PASS 697 FAIL 0、oracle-live 不一致 0；
  探针 18944 无 FAIL / AI / CRASH；scodeinst / MSL 变化 0，testsuite 只 2 个 SKIP→两拒；bdae 六段 FAIL / CRASH 0，探针变化只是新集 s5a2e / s5a2e-adv（649）；ASan 与 gcc 全部相同；pk-s1a 0 FAIL / AI。
  提交三个：s1fix、Alias、docs（五步流程 / 收敛方案 / 进度账），随后推送。
- 推送 9bd9f02..5c7cd5b（b73b1c1 s1fix、132bab3 Alias、5c7cd5b docs）。基线滚到门 s1al 的结果（sweep / ts / msl / bdae / bnd / fa / ei / s1 / al 的 *_cur.json）。
  重做底线二进制 = HEAD 5c7cd5b 的 gcc-debug 构建，拷到 $S/redo/ymc_base_5c7.exe（flatten / inst 与 9bd9f02 相同）。
- 用户同意（2026-11-11）68e 纯度配套的两处共用改动：Q-D5（结构参数判定等三个读者跳过只因 print 才 impure 的函数）、Q-D6（函数体求值器走到内建 print 判 SKIP）。已进 todo 已批准清单。
- 68e 任务卡交回规划子代理按评审意见改（第 2 步返工）：Q-D2 按"给哪个分量的绑定求值"判语境、新 RAII 标志、深度 0 入口逐个归类；sibling_fn_name 收窄到组件自己的类实例；新口径对 print 来源深遍历；r1-r3 并入验收。
- 68f 任务卡第四版（丙-2 / 丙-4 都能精确补：F1 改为只读被 extends 直接点名的节点的修饰，新增 F6 本地类在挂了类修饰的实例里重新展开；例外清单 16 个 FAIL→PASS）送第 3 步独立评审。
- 68e 任务卡 v3（Q-D2 改为位置三类 C / S / L + 新 RAII 标志 ceval_owner_，记录绑定按绑定变率整段归 Ceval（NFFlatten.mo:961）；条件 3' 要求组件自己的类实例；Q1 对 print 来源深遍历；Q-D6 改不粘；深度 0 入口 67 处全表归类）。
  主代理按已定规则处理其待定项：试发射兜底只在新纯度检查拒折后运行，暂归已批准范围、请复评判断；L 类收尾 SKIP 是如实未实现、不违反 (c)；U05 KNOWN。v3 送原评审员复评。
- 68f 任务卡第四版独立评审（$S/redo/68f-review/，rv1-rv8 + RVR）：**不通过**。严重三条：① F1 补不到"实例化基类之后才挂"的 extends 类修饰（RVC2 / RVC3 放开后新 FAIL）；
  ② omc 先实例化 extends 再实例化本地组件（NFInst.mo:1181-1190），ymc 按声明序，包实例缓存的语境不同 → RV05 新 AI、RV03 / RV04 底线 PASS 变拒；③ 漏了 lookupRootClass 的 RELAXED 语境（NFInst.mo:362），根路径上的包不报 PP，按卡 MSL 会退步。
  中：F4 定型不了就 SKIP 会退步（RV40-46 / 49）、数组与 startOrigin 没写；S2 依赖没拆开无法归因；F1-F6 改动面超出已批准范围；沙盒漏 synth_cap_ / depth_ / short_active_，flatten 期 pp_enter 有悬垂引用风险。
  用户定（11-11）：F1-F6 按类修饰触发、不分新旧，改动点进 todo 已批准清单。交回 68f 规划子代理改卡。
- 68e 任务卡 v3 复评：**有条件通过**——上轮四条主体已关；剩 4 处都在纯度部分（试发射兜底复原不全会残留空体函数、L 类收尾即时判误伤、C 类 owner 过宽、深遍历漏混合来源），且试发射兜底超出批准范围。
  用户定（11-11）：**不批准试发射兜底，放弃 AlgorithmNoRetCall**，纯度改动（Q-D1 / D2 / D5 / D6）全部撤出 68e，归纯度桶（todo 已改，评审材料留作复用）。68e 剩 10 个目标：AssertTest4-7、IntegerTest、4 个记录类型、GenerationOfFMUs。
  交规划子代理出 v4（删 Q 节与 P 的函数体扩展），评审员快速核对后开工。
- 68f 任务卡第五版（新增 F7 类修饰预挂、删 F3 / chain_layers；新增 S0 实例化与 omc 同序；PP-F 根路径 RELAXED；F4 定型失败静默照底线、数组不进、startOrigin 报真错；阶段 S0 → S1a / S1b → S2.1-2.4 → S3 → S4 → S5，依赖与连带撤项写明；
  沙盒补 synth_cap_ / depth_ / short_active_，flatten 期钩子改排队、返回后统一检查；例外 FAIL→PASS 31、PP AI→两拒 47）。用户批准 S0 精确做法与 F7（已进 todo）。送原评审员复评。
- 68e 任务卡 v4（10 个目标，纯度全删，P 只在模型语境；新增 G0：走过放开点且本次解析过的函数体含内建 print 就 SKIP，防 RV4P10 类放开后 FAIL）。送原评审员快速核对（重点 G0 与删得是否干净）。
- 68e 任务卡 v4 快速核对：**不通过**，3 处小改——G0 要认 `.print`（RV8G3）、G0 要从 enc_fns_ 做静态调用传递闭包（RV9G6）、A / G 在 E 的绑定 / 维深处调到不纯兄弟时不放（RV8G2；照 OMC 补齐要改纯度，用户已排除，故收窄）；
  B / G 放开模型里与放开无关的深处 impure 调用记 KNOWN。纯度删得干净、AssertTest 的 impure 打法、10 个目标不受 G0 影响、例外清单只剩 C10 族都已核过。交规划子代理出 v5。
- 68e 任务卡 v5（G0 认 `.print`、G0 改为从 enc_fns_ 出发的静态调用传递扫描 check_print_reach（包在 LookupSnapshot 里，放在 check_enclosing_refs 前一行）、sibling_fn_name 条件 7（E 的绑定 / 维 / 记录字段深处调到不纯兄弟不放）；
  operator record 'constructor' 在模型里直接调被折叠的情形留作已知漏洞）。送评审员快速核对，并请其判断该漏洞能否构造出静默 FAIL、能否不碰共用路径补上（G0 起点加模型方程 / 绑定的静态调用）。
- 68f 任务卡第五版复评：**有条件通过**——上轮七条全关（F7 预挂次序、F5、RVC2 / RVC3 / XST04-06、S0 推演 RV01-07 与 5 对前后序探针、PP-F、沙盒、拆阶段、范围）。
  开工前必改：N1 F4 只核胜出来源的值（RF1-RF3 底线 PASS 会被误拒）；N2 class_mods 多条时组件子修饰逆序施加；N3 主代理定把 F1 / F2 挪到 S1b 之前，避免放开模型中途 FAIL。
  注记：N4 RW3（重声明链上类修饰不跟到 rid）主代理定按规则 (c) 补上；N5 队列处理在丢弃模式下跑；N6 截断判定时机；N7 S0 风险点名。交规划子代理出第六版，之后评审员快速核对。
- 68e 任务卡 v5 核对：**不通过**，3 处——P 把 encapsulated 类里的 print 当内建（NFLookup.mo:885-887，RVEP1 / 2 会误收）；operator record constructor 起点漏洞能造出静默 FAIL（RVAD1）；LookupSnapshot 复原不了 Instantiator 的粘性 unsupported。
  G0 连着两轮被找出漏洞，根子是沿 resolve_fn 追踪传递闭包（有漏路径、有副作用）。主代理定改成全程序静态扫描：放开过的模型里，已加载程序的全部函数元素只读扫一遍，任一内建 print 就 SKIP，不调 resolve_fn、不碰 Instantiator。交规划子代理出 v6（含 MSL 扫描核实）。
- 68f 任务卡第六版（N1 F4 只核胜出来源、startOrigin 任何一层都报；N2 组件子修饰逆序施加；N3 新阶段表 S0 → S1a F7+F5 → S1b F1 → S1c F2 → S1d F6 → S1e 放开 → S2 F4 → S3 → S4 → S5；
  N4 补上：rid 拷 class_mods / class_modded（NFInst.mo:1735-1737 / :1787），RW3 FAIL→PASS；例外 FAIL→PASS 32、F4 AI→两拒加 RF4-RF9）。送评审员快速核对。
- 68f 任务卡第六版快速核对：**通过**（N1 / N2 / N4 / N5-N7 已解决，RF6-RF9 复跑底线 AI、omc 拒）。开工前改一处写法：S1a 的 plain 门会放开 const15 形态而 F1 在 S1b，RVC3 会中途 FAIL——主代理定 F1 并进 S1a。
  另补：N4 还会让 RW7 / RW8（组件修饰路线）FAIL→PASS，列进例外；F7 退路连带撤 N4 的 plain 拷贝。交规划子代理出定稿，之后开第 4 步实现。
- 68e 任务卡 v6（print_kind 三值纯语法判定、G0 = scan_program_print 全程序只读扫描、条件 5 / 7 改只读、LookupSnapshot 删除；MSL 4.0.0 不带限定的 print 81 处全经 import Streams.print，无内建 print；10 个目标不受影响）。送评审员快速核对。
  子代理误在 F:\Git\ 写了临时文件 tmp_ag.txt（删除被安全检查拦下），已请用户手动删除，主代理不代删。
- 68f 任务卡定稿（1208 行；阶段 S0 → S1a F7+F5+F1 → S1b F2 → S1c F6 → S1d 放开 → S2 F4 → S3 RICM → S4 P 组 → S5 PP；例外 FAIL→PASS 34、AI→两拒 59；中途豁免 OR7 / OR19 / G1 / G2，S1c 结束清零）。
  第 4 步实现已派（通用说明 $S/redo/impl_common.txt，isolation worktree，阶段二进制 $S/redo/68f/stage_*.exe）。
- 68e 任务卡 v6 快速核对：**通过**（print_kind 判定顺序不误伤 AssertTest、IntegerTest 能放；G0 覆盖全部评审探针、MSL 抽查无误挡；条件 7 / 5 不挡 AssertTest / GenerationOfFMUs；条件 3 是重复调用）。
  两条小建议主代理定实现时采纳：条件 7(b) 改为兄弟首输出书写类型不是 Real 就不放；核 MSL 在 program 里的嵌套形态。第 4 步实现已派（isolation worktree，阶段二进制 $S/redo/68e/stage_*.exe）。
- 68e 第 4 步实现完成（11-12）：10 目标全部与实跑 omc 逐字节一致；阶段 B+G0 → P → A → G 每阶段机械差分，底线有输出的变化只有例外清单的 C10（FAIL→PASS）；
  终版对底线探针 20013 个变 251（248 SKIP→PASS、2 FAIL→PASS、1 SKIP→FAIL）；scodeinst 0 变、MSL 只 GenerationOfFMUs、testsuite 只 9 目标。gcc / MSVC 0 警告，ctest 17/17。
  实现订正 6 条（条件 7(a) 补属性修饰、extends 经 H 类名限定路径收窄等，均只少放）；新探针 pk-p54r 225 个；与底线输出相同的老错按 todo"不入库"记 TBD-16。
  唯一 SKIP→FAIL：A68eIN58（`c.f` 的 f 继承自组件类的基类，体内兄弟调用走老类路径打 `B.g`，omc 打 `…c.g`；只调 c.f 的变体底线就 FAIL）。
  主代理定性：与放开点 A 同一机制（组件路线函数体里的兄弟调用命名），卡漏了继承情形 → 规则 (c) 第二条照 OMC 补齐；补卡交原规划代理，之后原评审员核，再交实现者。
- 68f 第 4 步停在 S1a（11-12）：S0 对 B0 探针 12 处变化、底线退出 0 无变化、三语料 0 变，不撤；S1a（F7+F5+F1+F2+F6）差分里 plain 门放开的底线 SKIP 出 AI / FAIL，按 e 收窄成 S1a2，例外 34 个兑现。
  F7 已知风险兑现：自写探针 XF721 / XF722 底线碰巧 PASS → SKIP（同形态 XF724 底线 FAIL）。
  用户定：**执行 F7 退路第一步**（不列例外）；随之 **const15 族整个不放**，const15 移出 68f（目标 11 → 10），不采用部分排除的收窄。已写进 todo，交实现者重做 S1a3 后继续。
- 68e A2 补充卡（规划代理，11-12）：fn_flat_name 类路径支末尾加只读判定 inherited_sibling_name（C0–C9），预测底线退出 0 变化只 IN51 / IN56 / IN59 / Z54（FAIL→PASS）+ 目标 IN58、V01，语料 0 变；交原评审员独立评审。
- 68e A2 评审（11-12）：**有条件通过**——OMC 依据逐行核对无误、C0–C8 下反例都没打出与 omc 不同的名字、插入点只碰底线本来就返回类路径名的分支；
  条件：① A2 与"老路线补齐范围"口径冲突，需用户确认 → **用户批准 A2**；② 扫描只认调用形式，漏了兄弟函数当函数值（RVA2F1）→ 交规划代理补扫、更新预测清单，之后交实现者。
- 68e A2 补充卡 v2（11-12）：扫描改为整词出现（含函数值实参），快照命中 53→123，新增 70 个都是记录类型名、A2 无名可改；预测清单不变（IN51 / IN56 / IN59 / Z54 + 目标 IN58，语料 0 变），新增评审探针 RVA2F1 / RVA2W1 FAIL→PASS（模拟改名与 omc 逐字节）。
  交 68e 实现者做 stage_A2（对 ymc_final 机械差分，清单外变化整项退回），终版另存 ymc_final2.exe。
- 68e stage_A2 实现完成（11-12）：fn_flat_name 类路径支加 inherited_sibling_name；对 ymc_final 探针 20137 个变 37 个，全部 FAIL→PASS、全在卡 §6 清单内（IN51 / IN56 / IN58 / IN59 / Z54 及副本、a2 / a2b / r13 新探针、V01），清单外 0；三语料 changed 0。
  pk-p54r 225 → 280，KNOWN 加 IN70 / IN73 / A2R04；单测 flatten_68e_redo_a2；gcc / MSVC 0 警告、ctest 17/17。终版 $S/redo/68e/ymc_final2.exe。派独立对抗评审（只给 diff、前后二进制、卡与规则）。
- 68e 对抗评审（11-12，独立代理，217 个探针三方实跑）：无 CRASH、无底线 PASS→非 PASS。
  ① 1 类 XE01 族 8 个：A / G 放开的函数体里调"被实例化的外层类"里的函数（`S.h`，omc `XE01.s.h`），走老类路径支 → SKIP→FAIL；去掉兄弟调用的 XE01b 底线就 FAIL（老错暴露），与 A2 同族（omc 实例树查名）。
  ② 2 类 7 个：底线退 0 FAIL → 另一个 FAIL（变化部分与 omc 一致，残差是负零 / impure 标记 / print 折叠等已登记老错），规则 (a) 字面算退步。
  ③ 3 类记账漏项：B 的同名内建回落 FAIL→PASS（XB30-32 / 34 / 41）、G0 FAIL→SKIP（XF06 / XB33）、A2 新形态 FAIL→PASS 25 个——按现有规则逐个列名。
  ①② 需用户定（规则边界），已报用户。
- 68f 第 4 步实现完成（11-12）：阶段 S0 → S1a4（F7 只对新写法预挂 + F5 / F1 / F2 / F6；S1a3 的 early 守卫过宽、RV18 B0 FAIL→SKIP，收窄成与底线同条件）→ S1d → S2（F4）→ S3（RICM）→ S4（partial 组），每阶段对上阶段机械差分，均不撤。
  S5（PP 族）撤：MSL `Modelica.Electrical.Digital.Examples.RAM` 底线 PASS → 被拒（DLATRAM 不限定调继承来的 getMemory，S5 按定义路径进 partial MemoryBase；omc 经实例继承元素取函数）。照规则不修，源码回 S4，最小形入库 pk-p55r-pp/R68fPRAM01，记 TBD-18 转 65b / 65c。
  终版对 B0：探针 19858 个 130 处变化——FAIL→PASS 5（A68fXSB01、EQ06、PX02、RV15、Q07）、AI→两拒 13（F4 名单 12 + UTU08）、SKIP→PASS 86、只换 SKIP 原因 13；无底线 PASS 变化、无新 FAIL / AI / CRASH。三语料：scodeinst 698→706、testsuite 1918→1928（都只是目标）、MSL 500→500。gcc / MSVC 零警告，ctest 17/17；slice 门 FAIL 0、oracle=live 不一致 0。
  UTU08（`type T = Real(startOrigin = "x")`）补列：主代理三方核过——omc 拒（Modified element startOrigin not found in class Real，NFInst.mo:1547 MISSING_MODIFIED_ELEMENT）、底线收、终版拒，属规则 (a) AI→两拒逐个列名。新开放问题 TBD-16（const15 族）/ 17（F4 没做的两种）/ 18（PP 族）——与 68e worktree 的 TBD-16 撞号，集成时 68f 这三条顺延改号（连带 KNOWN.txt / 卡 / 探针 README 的引用）。
  对抗评审已派（说明 $S/redo/brief_adv_68f.txt，笔记 $S/redo/68f-adv/）。
- 68f 对抗评审（11-12，独立代理，22 集 410 个探针三方实跑；主代理抽查 S006 / V011 / Z001 属实）：无 CRASH、无底线 PASS→非 PASS、const15 仍 SKIP。
  1 类-A 12 个（S006 等）：派生类本地类 `E extends B` 的组件写在 `extends A(B(x=2))` 之前，S0 改序后底线 tree_class 守卫不触发，F1 只读预挂、plain 事后挂被丢 → SKIP→FAIL（根因 = TBD-16，用户已定不做 plain 预挂）。
  1 类-B 3 个（F709 / V011 / M013）：同一类节点既有新写法预挂、又被外层 plain 事后挂，早建消费者读不到 → SKIP→FAIL。
  2 类 7 个（Z001 等）：F1 把组件修饰路线条目的子修饰当 extends 修饰施加，绑定按 a.b2 前缀解析（`a.b2.q`，omc `q`）→ 底线 FAIL 变另一个 FAIL。
  3 类约 70 个记账漏项（S0 族 FAIL→PASS 10、F1 / F2 组件修饰路线 FAIL→PASS 约 35、F4 AI→两拒 21、AI→SKIP 3）。
  主代理按规则 (c) 第一条定收窄（2 类的错输出也是本桶引入，收窄后为 FAIL→SKIP，(a) 允许）：N1 F1 遇事后挂条目即 SKIP；N2 事后挂点目标已有预挂条目即 SKIP；N3 组件修饰路线子修饰绑定含查名（cref / 调用）即 SKIP。AI→SKIP 按"错输出变如实未实现"同 FAIL→SKIP 口径列名（主代理补注，用户可否决）。已交实现者，之后核对评审。
- 68f 收窄轮完成（11-13）：N1（F1 遇事后挂条目即 SKIP，取代并删掉 S1a4 的 early 守卫）、N2（事后挂点目标已有预挂条目即 SKIP）、N3（组件子修饰绑定 literal_only_mod：字面量及其运算 / 括号 / 字面量数组 / `= break` 照施加，其余 SKIP）。实现者自定两处（括号与 `= break` 算不查名；删 early 守卫），主代理认可。
  评审 1 / 2 类 22 个全成 SKIP；N2 附带 H028 / M012 由 S4 PASS 回到底线 SKIP（底线就 SKIP，不违规则）。终版对 B0 探针 20286 个：底线 PASS 变化 0、SKIP→FAIL / AI / CRASH 0；底线退 0 的变化只有 FAIL→PASS 51、AI→两拒 43、FAIL→SKIP 42、AI→SKIP 3，全部列名进卡 §20。
  三语料：scodeinst 706（只 8 目标）、testsuite 1928（只 10 目标）、MSL 500 不变；gcc / MSVC 零警告、ctest 17/17、slice FAIL 0 / oracle=live 不一致 0；probe_recheck 683 个无 FAIL / AI / CRASH（KNOWN 2）。新探针集 pk-p55r（202）、pk-p55r-adv（416）。
  核对评审已派（说明 $S/redo/brief_ver_68f.txt，工作目录 $S/redo/68f-ver/）：独立重算对 B0 的机械差分 + 三收窄点贴边探针。
- 68f 核对评审（11-13，独立代理）：机械差分独立重算与实现者自报逐项相同（410 处变化），§20 四类例外 125 个名字全准，N1 / N2 / N3 贴边探针无漏网无误伤，omc 缓存抽查 38/38 一致。
  不通过 1 条（S0，stage_S0 起就有）：写在 extends 前、类型经被修饰 extends 继承来的本地组件（`B b0; extends A(B(x=2));`），底线先实例化撞 tree_class 守卫整模型 SKIP；S0 改序后守卫不触发，模型放出来，别处底线老错露出——基类里 `E extends B` / `B2 = B` 读不到事后挂（TBD-16）→ SKIP→FAIL 5 个（VN1C VC01 VC07 VC08 VD02）；无关老 AI（`Integer i(unbounded=true)`）→ SKIP→AI 3 个（VC04 VD01 VE01）。
  主代理按规则 (c) 第一条定 N4：S0 第二遍入口对"声明在 extends 前"的本地组件照底线守卫条件 mark_unsupported（原因串同底线），不写新识别逻辑；S0 的同形态 SKIP→PASS（OR2 / OR4 / A68cIH02 / R68cIH2 / SA5 等）随之退回底线 SKIP。目标掉 PASS 或底线 PASS 变化即停报。已交实现者；完成后请核对评审员只复核 N4。
- 68f N4 完成（11-13）：复原底线状态让守卫照原样触发——第一遍记下各 extends 的 adopt 换掉的类项与 via_mod 标记，第二遍实例化 extends 前的本地组件时重标并记 pending_adopted_，实例化完撤销；守卫条件不变。
  核对评审点名的 8 个全部 SKIP、与底线逐字节相同；对 N3b 41 处变化全成与 B0 相同的 SKIP（S0 同形态收益 23 个退回底线 SKIP，含 OR2 / OR4 / A68cIH02 / R68cIH2 / SA5）；10 目标仍 PASS，S0 族 FAIL→PASS 10 个保留。
  终版对 B0（20377 个）：FAIL→PASS 68、AI→两拒 43、FAIL→SKIP 56、AI→SKIP 4，其余为底线 SKIP 侧；底线 PASS 变化 0、SKIP→FAIL / AI / CRASH 0。三语料只目标变；零警告、ctest 17/17、slice FAIL 0 / oracle 不一致 0；probe_recheck 746 个无 FAIL / AI / CRASH。新探针集 pk-p55r-ver（63）。已请核对评审员只复核 N4。
- 68f N4 复核通过（11-13）：N3b→N4 在 21137 个模型上 84 处变化全是与 B0 逐字节相同的 SKIP；终版对 B0 分类逐项与实现者相同；底线 PASS 变化 0、SKIP→FAIL / AI / CRASH 0；N4 贴边 19 个（泄漏 / 两条 extends / 深处守卫 / 嵌套 / 重复实例化）18 个与底线同；代码核对撤销在所有出口执行（src/inst 无 throw），守卫只多一项模拟"父亲还不是实例"。
  记账 1 项：VG03（S0 族，底线 FAIL→PASS 与 omc 一致）补列 §20；vn4 / vn4b 探针并进 pk-p55r-ver。已交实现者。68f 第 4 步收口。
- 68f 记账完成（11-13）：§20 S0 族补 VG03（11 个），终版对 B0 合计 424、FAIL→PASS 69；pk-p55r-ver 63→79（并进 vn4 / vn4b 16 个，老 AI VF11 / VF14 / VG04 不收转 TBD-17）；probe_recheck 79 个 PASS 29 / 两拒 4 / SKIP 46，无 FAIL / AI / CRASH。源码与终版二进制不变。
- 用户 11-13 回"A"，按两件都取推荐项 1A 2A 执行（todo「68e / 68f 收敛方案」已记）：
  1A：XE01 族 8 个照 omc 实例树查名规则统一补一次（A3），A2 收编进同一判定；之后核对评审再找出的变种一律入口收窄成 SKIP，留给 65b。
  2A：规则 (a) 例外扩一条——底线 FAIL → 另一个 FAIL，变化部分与 omc 逐字节一致、残差逐个对到已登记老错的，按名字记账（XF01-XF05、XB35 / XB36）。
  A3 第 1–2 步已派规划代理（产出 $S/redo/68e/A3/T-S3-68e-redo-A3.md）；之后独立评审 → 实现者（worktree agent-a604f892029abe22b）→ 68e 核对评审 → 与 68f 集成。
- 用户 11-13 更正决定 2 为 B（"2 选择B"），即 1A 2B：规则 (a) 不加 FAIL→FAIL′ 例外；A2 作为共用路径改动整项撤掉，C10 同名回落那一批在放开点入口撤掉；
  A3 照 omc 实例树查名统一修 XE01 族与 IN58 类，但按规则 (b) 只在这次压平走过 68e 放开点时生效（底线有输出的模型逐字节不变）。todo 已改，规划代理已收到更正。
- 68e A3 补充卡 v1 完成（11-13，$S/redo/68e/A3/T-S3-68e-redo-A3.md，新探针 a3x 27 / a3y 5 / a3c 7 三方实跑）：
  - XE01 族 8 个全能 PASS：fn_flat_name 类路径支末尾用统一判定 X / Y 支，照作用域链上的实例节点 scope_path 拼名，A2 收编为 Y 支。
  - IN58 类名字先于放开落定，放开点入口 SKIP；XE08 / A3X20 / A3X33 由 G1 收窄；A2、C10 回落撤回 B0 字节。
  - 门控：blocked_ 加 released_68e_，B0 退出 0 的模型 0 变化。对 final2 预测：回 B0 104、FAIL→PASS 21、FAIL→SKIP 11、PASS→SKIP 7（B0 都 SKIP）。
  - 主代理按规则定 §9 越界五条（卡 §15）：
    - 不重跑压平；
    - 不加 call() 帧栈，用 FB 超集退路；
    - G1 扫全部 enc_fns_（规则 (c) 第一条）；
    - 别处回落暴露登记 KNOWN（老路线补齐范围）；
    - 缺实例树数据的一律 U → SKIP（1A 止损）。
  - 第 3 步独立评审已派（$S/redo/68e/A3/brief_review_a3.txt，工作目录 review/）。
- 68e A3 卡第 3 步独立评审（11-13，$S/redo/68e/A3/review/，探针 rv_g / x / c / i / j / k / l 三方实跑）：**有条件通过**。
  OMC 依据 O1-O9 核对正确；XE01 族推导成立；final2 对 B0 的退 0 变化与 §7.1 逐个对上。
  必须先改 4 处：
  1. FB 超集漏 import 改名，RVG01-04 会改 B0 退 0 的输出；
  2. X0 不成立又带实例层时误判 N，RVL01 / 02 仍 FAIL；
  3. 拒放的内建同名兄弟照旧回落，RVC01 / RVX01 FAIL，要求 A / G / B 判定点不论放不放都判 FB；
  4. G1 漏记录字段修饰与多段记录类型，RVI01 / 03 FAIL。
  同步改 5-8：raw_typing_ 门控、N 的论述与 RVX07 KNOWN、§15 进 todo（主代理已补）、rv_* 并入验收。
  全部按规则 (c) 第一条（收窄 / 入口判定）处理，交回规划代理改 v2（意见转录 review/REPORT.md），之后原评审员快速核对。
- 68e A3 卡 v2（11-13）：评审 1-9 全改。
  - FB 超集加改名 import 别名项；X0 不成立且有实例层判 U；A / G / B 判定点不论放不放都判 FB；G1 补记录字段修饰与多段类型逐段下降；新计数 raw_pass_ 门控。
  - 新门控探针 a3g 5 个。对 final2 预测：回 B0 110、FAIL→PASS 26、FAIL→SKIP 22、PASS→SKIP 7；对 B0 退 0 变化 0。
  - 已交原评审员快速核对。
- 68e A3 卡 v2 快速核对（11-13）：不通过只差 1 条——"X0 不成立 → U"写宽（含传入 scope，模型语境会一律 U、置 blocked_ 挡掉正确放开）；改完不再评。
  主代理直接改成 v2.1：只在函数体语境且 ds 本身有实例层时 U；采纳建议 raw 遍在 released_68e_ 已为真时照常放（卡 §16）。评审 1-4 全部收住、10 目标与 B0 退 0 零变化论证成立。
- 68e A3 第 4 步实现已派（$S/redo/68e/A3/brief_impl_a3.txt；worktree agent-a604f892029abe22b 上改；阶段 A3a 撤 A2 + 门控 + FB → A3b 统一改名 → A3c G1；终版 ymc_final3.exe）。
- 68e A3 实现完成（11-13，ymc_final3.exe；阶段 A3a / A3b / A3c 各自对上一阶段差分全部归项）：
  - 对 B0：全部 20766 个探针里 B0 退 0 的变化 0（主代理独立重算：只有 pk-p34 URZ* 7 个是输出里的临时目录名噪声，规范后 0）；B0 退 66 而新 FAIL / AI / CRASH 0。
  - 对 final2：变 167，与卡 §7.1 逐个吻合（A 节 110 回 B0 字节，B 节 56）；清单外 1 个 XA62（H 的兄弟函数叫 sample，A 判定点 FB 命中 → blocked_，PASS→SKIP，与 B0 逐字节同）。
    主代理认可：B0 本是 SKIP，合规则 (a)，是决定 2B「FB 超集多拒只少放」的直接后果，补进卡 §17 清单（PASS→SKIP 共 8）。
  - 卡 §7.1 三个待核 A3X09 / RVK03 / RVX07 都是 PASS（RVX07 入库，不进 KNOWN）。语料对 5c7 只变 10 个目标（scodeinst 0、MSL 1、testsuite 9）；gcc 0 警告、ctest 17/17、MSVC /W4 改过的 TU 0 个 C 警告。
  - 入库 pk-p54r 280 → 326（去 31：27 个回 B0 FAIL 的 A2 条目 + R68eC10 + KNOWN 3；追加 77），probe_recheck 无 FAIL / AI / CRASH；open-questions 68e 的 TBD-16 ③ 改 G1 口径、④ A2 撤后回 B0 FAIL 的条目转 65b、新 TBD-17 记 C10 族老错（集成时顺延为 TBD-19 / 20）。
- 68e 核对评审已派（$S/redo/brief_ver_68e.txt）：门控 / 收窄边界、对 B0 规则 (a) 差分重算、对 final2 预测清单逐个核。
- 68e 核对评审（11-13，独立代理，137 个边界探针三方实跑）：**通过**。
  - 四个放开点门控、FB 判定、a3_classify、G1 位置都对。对 B0 重算（21092 个）：B0 退 0 变化 0；B0 非 0 而有输出的 635 个实跑 omc 全 PASS；omc 缓存 729 个逐个比对一致。
  - 对 final2 的 167 个与卡 §7.1 + §17 逐个相同，清单外 0。卡 §7.1 两处标题数笔误（FAIL→PASS 名单 25、FAIL→SKIP 21）已订正（卡 §18）。
  - raw 遍"只有这一遍才到得了放开点"的写法 12 种都没构造出来，那条门控只能读代码确认。
  - 边界探针 128 个入库 pk-p54r-ver（前缀 R68eV；PASS 57 / 两拒 13 / SKIP 58）；与底线同输出的老错 9 个不入库：VF20a / 21a / 23a / 30a / 31a、VR06 记 C10 族那条，VG07-09 记纯度那条 ③。
- 68e / 68f 集成（11-13，$S/redo/integ.py）：68f 先拷进主仓库、保留 TBD-16 / 17 / 18；68e 三方合并（flatten.cpp 干净合并；单测、open-questions、探针 README 都是同处追加，两边都留），68e 的 TBD-16 / 17 顺延为 TBD-19 / 20。
  gcc-debug 零警告、ctest 17/17。组合检查已起（$S/redo/integ_check.sh：B0 / 68e final3 / 68f final / 集成版四个二进制跑同一批探针，判集成版变化 = 两桶变化的并集）。
- 组合检查（$S/redo/integ_chk/run.out）：22503 个探针，68e 单独变 644、68f 单独变 520、两桶同时变 0；集成版逐个等于预期，不一致 0。
- 门 68ef（集成树，$S/gate68ef.out）**全绿**：
  - 四套构建 ctest 17/17；S1 / S2 专项全绿（parse-scan MISMATCH 0，KNOWN 27）。
  - slice PASS 705 FAIL 0、oracle-live 不一致 0；探针 20160 无 FAIL / AI / CRASH，新集 1216 个 ASan 复查也干净。
  - scodeinst 只变 68f 的 8 个；testsuite 只变 19 个目标（68f 10 + 68e 9）；MSL 只变 GenerationOfFMUs。
  - bdae 六段 FAIL / CRASH 0，变化只是探针里 11 个含 print 的模型 SKIP / 未准入→PASS（68e 放开 print 的直接结果）；ASan 与 gcc 全部相同。
- 提交两个：先 68f（共用文件按 68f worktree 版本暂存，不动工作区），再 68e + tasks 文档。
