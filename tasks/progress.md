# tasks/progress.md — 任务进度账（本地离线记录，便于追溯）

用户要求（2026-11-05）：所有任务都在本地记录进度和完成状态。约定：
- 主代理维护本文件，每个任务一节：开始、关键进展、卡点、完成状态；随该任务的提交一起提交。
- 状态只用这几种：排队 / 进行中 / 等门 / 已提交 / 已推送 / 搁置（写原因）。
- 记事实和数字（门的结果、提交号、worktree、子代理 id），不写推测；子代理的工作笔记在会话临时目录，任务结束时把要紧的结论摘进本节。
- 计划和完成摘要仍在 `tasks/todo.md`，踩坑规则在 `tasks/lessons.md`；本文件记的是过程。

## 当前状态（2026-11-11 10:00）

| 任务 | 状态 | 执行 | 下一步 |
|---|---|---|---|
| 68e 函数调用的压平名与内建 print | 排队：从 9bd9f02 重做（用户 11-11 选定；目标 10 个 testsuite） | 旧版 W1 `agent-a31f1c44ca4d7e1b0` 只作参考 | s1fix / Alias 提交后按 todo「68e / 68f 收敛方案」开工 |
| 68f 修饰 / 重声明 / 短类 / extends 的剩余 SKIP | 排队：从 9bd9f02 重做（用户 11-11 选定；目标 12 个） | 旧版 W2 `agent-a50f554362667d4c5` 只作参考 | 同上 |
| S5a-2e 后端 Alias 模块 | 等门：补修完成 | worktree `agent-a5dbe5f9368975a92` | 与另一桶单独过门 → 提交 → 推送（不再等 68e / 68f） |
| s1fix 修饰名带下标的错误位置 + testsuite 全扫错误位置 | 等门：补修完成 | worktree `agent-afeb631e73fae5ed7` | 与另一桶单独过门 → 提交 → 推送（不再等 68e / 68f） |
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
