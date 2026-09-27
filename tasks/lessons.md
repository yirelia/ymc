# Lessons

## 2026-09-14 — 项目名 mmc 撞了 OMC 的 MMC（MetaModelica Compiler）
- 模式：我给新工程随手起了占位名 `mmc`，没问用户，也没查参考代码库里是否已占用。用户问"为啥 mmc"才发现 OMC 运行时到处是 `MMC_` 宏 / `mmc_mk_*`，并排读代码会混淆。改名成本在首次提交前最低，用户定名 `ymc`。
- 规则：**任何会渗透到命名空间/宏/目标名/目录的名字，先问用户，并 grep 参考代码库确认不撞名**。方案里标"待定"不等于可以先用。

## 2026-09-14 — Windows 上 shutil.move 整目录会半途失败
- 模式：目录被别的进程占着（终端/VS Code 打开在里面），`os.rename` 失败后 shutil 退化成 copytree + rmtree，rmtree 在顶层目录卡住，留下空的旧目录。
- 规则：Windows 上移目录前先假设它被占用；脚本要能"再跑一次"（幂等：新目录已存在就只做清理），跑完立刻列两个目录核对，不要相信一次 move 的返回。

## 2026-09-14 — CMake 预设里覆盖 CMAKE_CXX_FLAGS 会冲掉默认 /EHsc
- 模式：`msvc-asan` 预设直接写 `CMAKE_CXX_FLAGS=/fsanitize=address`，MSVC 默认的 `/EHsc /GR` 被替换，出现 C4530；ASan 又和 Debug 默认的 `/INCREMENTAL` 冲突出 LNK4300。之前说"三套预设零警告"时没逐条看 asan 那套的输出。
- 规则：预设只设开关型缓存变量（`YMC_ASAN=ON`），具体 flag 由 CMakeLists 里的 `option()` 分支用 `add_compile_options/add_link_options` 追加。宣称"零警告"前要 grep 每一套预设的构建输出。

## 2026-09-14 — OMC 结果对比基建的四个坑（M0 实现时踩到）
- **rtest 的相对路径靠 `--running-testsuite` + `OPENMODELICALIBRARY`**：直接 `omc file.mo` 的错误信息是绝对路径，与 `// Result:` 块里的 testsuite 相对路径不符。必须传 `--running-testsuite=<可写文件>` 且设 `OPENMODELICALIBRARY`，否则前者报错。运行器复刻 rtest 必须照做。
- **stderr 要合并进 stdout 保序**：`omc ... 2>&1` 是实时交错；Python 里 `capture_output` 后 `stdout+stderr` 拼接会打乱顺序（错误详情和 "# Error encountered" 位置颠倒）。用 `stderr=subprocess.STDOUT`。
- **testsuite 的 `// Result:` 块可能相对当前 omc 已过时**：scoping 里 4/26 用例，安装版和工作区版 omc 的真实输出都与 checked-in Result 不同（如新增 `function` 声明）。所以运行器自检不能要求"全 PASS"，正确判据是"我们的判定与真实 omc-diff.exe 对同一输出一致"。
- **tube 比对算法里 mergeTimelines/removeUneventfulPoints/addTargetEventTimes 在源码中被注释掉**：真实 diffSimulationResults 路径只做 calculateTubes(参考)→calibrate(实际+管道到参考轴)→addRelativeTolerance→validate。别照搬那三个死函数。默认容差由 `rangeDelta=0.002` 主导（竖直半宽≥0.8%幅值），不是 relTol。

## 2026-09-14 — 大工作流会撞会话额度，但 agent 产物已落盘
- 15-agent 的 Spec 工作流跑到一半撞 "session limit"，只有 2 个 agent 返回结构化结果进 cache，但另外 3 个研究 agent 其实已把 .md 写到 scratchpad（额度是在返回结构化输出前耗尽的）。
- 规则：长工作流的中间产物尽量让 agent 写文件；恢复时先 `ls` scratchpad 看哪些已完成，只补真正缺的（本例只需重跑 R4/R5），别整体 resume 重烧 13 个 agent。

## 2026-09-15 — 跳过"不支持的段"时没消费段关键字本身 → 空转死循环
- 模式：`external` 段的跳过分支先 `while(!is_section_boundary)` 再 advance，但 `is_section_boundary` 把 `external` 自己算作边界，循环体一次都不跑，组合循环反复回到同一记号，整目录扫描卡死。
- 规则：解析器里每个"列表/跳过"循环都要有**进度保证**——记录 `pos_`，一轮没前进就强制 advance 或 break 并报错；跳过某段前先吃掉引入该段的关键字。用带超时的整目录扫描（`tools/parse_sweep.py`）当回归，TIMEOUT 必须为 0。

## 2026-09-15 — 类级 annotation 可以出现在任何段中间
- 模式：MLS 只允许 annotation 在 composition 末尾，但 OMC 文法与 testsuite 允许它夹在元素/方程之间（Faculty5.mo、ElectrolytesT2Total.mo）。把它当"段边界"处理后，后续元素/方程落到组合循环的 default 分支报错。
- 规则：**以 OMC 实际接受的文法为准而非 MLS 字面**（本项目验收对象是 OMC testsuite）；组合循环记住当前段，处理完 annotation 回到该段继续。

## 2026-09-15 — 词法最大匹配吃掉了 `import A.*` 的 `.*`
- 模式：`.*` 是逐元素乘法记号，最大匹配把 `import A.B.*;` 切成 `A.B` + `.*`，语法层只等 `.` + `*`，16 个用例报错。
- 规则：凡是词法有多字符算子的地方，语法层要同时接受"合并记号"与"拆开记号"两种形态；新增算子记号时 grep 一遍所有用到其首字符的文法规则。

## 2026-09-15 — unparse 的括号规则要按"不可链式"处理 `^`
- 模式：把 `^` 当右结合打印，`(a^b)^c` 变成 `a ^ b ^ c`，而 MLS 的 `factor = primary [^ primary]` 不允许链式 `^`，回读失败（Pow.mo）。
- 规则：反解析的最少括号规则要以**文法的可解析性**为准，不是数学结合律；`parse_sweep.py --roundtrip` 的幂等扫描是唯一可靠的验收，不要靠手写样例。

## 2026-09-16 — testsuite 的 `status: correct` 不等于 OMC 自己能过
- 模式：`extends/GenericMicroCircuit.mo` 标 correct，却写了 `model TempResistor2 … end TempResistor;`；ymc 新加的首尾名检查报错后我先怀疑自己的检查。实跑安装版 omc：它在完全相同的位置（73:9）报同一个错——该文件在目录 Makefile 的 `FAILINGTESTFILES` 里，OMC 的 rtest 根本不跑它。
- 规则：**验收门只算 Makefile `TESTFILES` 里的用例，`FAILINGTESTFILES` 单独分桶**（`parse_sweep.py` 已按此实现）；ymc 与 OMC 不一致时，先用 oracle 实跑同一文件再下结论，不要只信文件头。

## 2026-09-16 — OMC（ANTLR 回溯）的错误位置有两套约定
- 模式：`Real 1x;` / `Real model;` OMC 报在元素起点（`Real`），而 `parameter Real (p3,p4)` / `Real x,y,;` 报在出错记号；原因是 ANTLR 回溯在"无前缀、以 IDENT 开头的元素"上整条无可行分支，位置回到决策点。
- 规则：对齐 OMC 位置时按**构造分类**写下约定并用 `tools/parse_error_align.py` 固化（23/23），不要逐个用例打补丁；新加语法检查后必须重跑 flattening/MSL 全量扫描，防止把 OMC 接受的文件误判。

## 2026-09-16 — 保留字别凭记忆定：`assert` 不是 Modelica 保留字
- 模式：S1 v1 写词法器时把 `assert` 当关键字（因为它像语句）。三套语料全绿都没暴露，直到扫 Buildings 9.1.0：`Assert assert(...)` 这样的组件名报错。MLS B.1 的保留字表没有 `assert`（它是内置函数，同 reinit/terminate/sample），OMC 词法器里也没有这个记号。
- 规则：保留字表只能来自 MLS B.1 / OMC `BaseModelica_Lexer.g`，写完用 `comm` 逐词对比（本次对比结果：ymc 多出的只有 `assert`）。第三方大库（Buildings 4425 文件）是比 MSL 更狠的语法覆盖，扫描门加进去。

## 2026-09-16 — 版本回退规则：两版直觉假说都错，13 个探针才定型
- 模式：MSL 3.2.3 的 uses 要 Complex 3.2.3，本机只有 4.0.0，OMC 却静默装上了。我先猜"有更新版就用最高的 ≥ 请求"，再猜"优先同 major"——两者都能解释已见的 5 个用例。加了 3.2.4/3.3/3.0/2.0/4.0/2.2.2/2.1/3.2.2 等判别用例后，两个假说都被推翻：3.2.4 在有 4.0.0 的情况下照样失败；真正决定的是候选库注解里的 `conversion(noneFromVersion/from)`（`from` 还可链式经其它候选）。
- 规则：**行为规则先设计判别用例再下结论**——写出两个以上候选假说，专挑它们预测不同的输入去探针，直到只剩一个；把全部探针用例固化成单测（`version_selection_matches_omc_probes`），源码锚点（R8）到位后只需核对机制，不必再猜。

## 2026-09-17 — "对齐 OMC 的 dump"不能靠读模板推断版式，要先跑探针拿原文
- 模式：SCode dump 第一版我照 `SCodeDumpTpl.tpl` 的阅读理解写，探针一比：空类体其实单行（`model M end M;`）、空行在 `protected` 标签**之前**、`pure function` 要打印、修饰的字符串注释不打印、注解顶层的空子修饰保留却不打印。Susan 模板的缩进/空项/分隔符语义靠读是读不准的。
- 规则：**每种 dump 版式先写一个覆盖面尽量大的探针模型在 omc 上实跑，把原文固化成单测的期望值**（`docs/research/probe-s2/*.mos`），再写打印器；模板源码只用来解释"为什么"，不用来预测"是什么"。抽样对拍（`tools/scode_dump_compare.py --sample N --seed k`）换种子多跑几轮，每轮的新差异类别再补探针。

## 2026-09-17 — 源码 HEAD 不是 oracle：安装版 1.25.0 与 HEAD 行为不同
- 模式：external 段注解的合并，我按 HEAD 源码（白名单、仅缺失时复制）实现，探针 6 种组合全不符；`git show v1.25.0:…AbsynToSCode.mo` 才看到安装版是无条件 `mergeSCodeOptAnn`。
- 规则：引用 OMC 源码解释行为前，先确认该函数在 oracle 版本里长什么样：`git -C C:/dev/OpenModelica show v1.25.0:<path> | sed -n …`；探针与 HEAD 源码冲突时以探针为准，并在注释里两边都写（ADR-005 决定 4）。

## 2026-09-17 — S1 的"最少括号 + %.17g"是自洽但错的：验收对象一换就暴露
- 模式：S1 只拿 round-trip 幂等当验收，于是 Absyn 丢括号、丢 Real 拼写、解转义字符串都"没问题"。S2 一和 OMC 逐行比，40 个类里 16 个不一致，全是表达式文本。OMC 的做法相反：`Absyn.REAL` 存字符串、`(e)` 建成单元素 TUPLE、dump 不补括号。ADR-003 里"OMC 的 Dump 按优先级重建括号"是我没核实的说法。
- 规则：**自洽性验收（幂等、自比）只能证明没崩，不能证明对**；每个阶段尽早接上外部 oracle 的逐字对比，哪怕只抽样。ADR 的"依据"里每一句关于 OMC 的陈述都要有 `文件:行`，没有就是臆想。

## 2026-09-17 — 单测输入必须是合法 Modelica：`a + not b`、`2 .* -x` 都不合文法
- 模式：给新打印器写期望值时顺手造了 `y = ({1,2})[1] + not b` 和 `(b*c)^2 .* -x`，解析器报错，我第一反应是怀疑解析器。MLS 文法里 `not` 只在 logical_factor 层、一元负号只在 arithmetic_expression 开头，`* -x` / `+ not b` 本来就不合法。
- 规则：测试失败先怀疑测试输入；造表达式用例前对一下 MLS B.2.7 的层次（`docs/s1-文法对照表.md`），拿不准就先喂给 `omc` 看它收不收。

## 2026-09-17 — Bash 工具的 heredoc 会把两个反斜杠折成一个：带转义的补丁脚本不要内联
- 模式：用 `python - <<'EOF'` 内联脚本往 C++ 源码里写"反斜杠 n"，脚本里写的双反斜杠到 Python 手里只剩一个，被当成换行符，落盘后字符串字面量里出现真换行，编译报 "missing terminating character"；连续三次修补都被同样吃掉（写这条教训时用 heredoc 追加，双反斜杠又被折了一次，实锤）。
- 规则：含反斜杠转义的内容一律用 Write/Edit 工具落盘（补丁脚本先 Write 成 .py 再执行）；heredoc 只用于不含双反斜杠的内容。

## 2026-09-17 — 把活交给 Codex（非交互 `codex exec`）的三条经验
- 先跑 1 分钟的冒烟任务验证沙箱：能否 ctest、调 omc、写系统临时目录、读仓库外的 OMC 源码（`-s workspace-write` 下都可以）。
- 任务卡写进仓库（`docs/tasks/T-S2-03-…md`）并把研究底稿、探针夹具拷进仓库（`docs/research/`、`tests/fixtures/`），prompt 只说"读并实现这张卡"；卡里写死允许改的文件清单、验收命令、"来源写不出就不实现"。
- Codex 会先出计划然后停下等"确认实施"——非交互模式没人回，它就正常退出（exit 0、零改动）。第一条 prompt 里就要写明"非交互批处理，不要停下来等确认"；已经停了就 `codex exec resume <session-id> "确认实施…"` 续上。交接前给工作树拍快照（仓库大量文件未提交，`git diff` 看不出它改了什么）。

## 2026-09-18 — arena 失效引用：持有 T& 跨越递归 add() = 写入丢失（inst_component 复合组件）
- 模式：`Component& comp = I_.comps[cid];` 拿到引用后，调 `instantiate(type)` 递归实例化组件类型——递归里往 `comps`/`nodes`/`classes`/`trees` arena 各 add 了元素，触发底层 vector 扩容搬家，`comp` 引用失效；随后 `comp.ty = Complex` 写到旧地址，值丢失。测试里"复合组件类型 == Complex"失败，但内建标量组件全对（那条不递归、不 add）。ASan 未必报（写的是仍属 vector 容量内的旧槽或已搬走的旧内存，取决于扩容时机）。
- 规则：**arena.h 头注那条"持有 Id 不持有引用跨越 add()"是硬纪律**。凡是函数中间会调用别的 pass / 递归实例化 / 任何 `arena.add`，就不能跨过它持引用：递归前用一个引用写"半成品"状态（值会随搬家保留），递归后**重新 `I_.arena[id]` 取引用**写最终值；或把结果先算进局部变量，最后统一回写。新写 inst/typing/flatten 的每个 pass 都按此审一遍——这是 arena 架构最容易反复踩的坑。

## 2026-09-18 — 压平门 FAIL=0 靠"诚实 SKIP"：不支持的构造要在崩溃/报错前拦下
- 模式：instExpressions+flatten 第一版能跑，但对 easy slice 220 例是 26 PASS / 155 FAIL——大量 FAIL 其实是 v1 没做的特性（常量求值、属性修饰、短类、枚举、连接器、inner/outer、跨包引用…），flatten 却硬产出了错误文本而不是标记"未实现"。更糟的是有的直接崩：`type MyReal extends Real`（Real 是内建、无 ClassTree → `trees[无效]` 断言崩）、`outer B b`（B 里又含 outer B → 无限递归实例化 → 栈溢出 rc=127）。这些都发生在 flatten 的"unsupported"检测之前（inst 阶段），检测根本没机会跑。
- 规则：**扫描门的三态是 PASS / FAIL（真 bug）/ SKIP（还没做）**，FAIL 必须只剩真 bug。要做到：(1) 每个 pass 都持有 `unsupported` 标记，碰到 v1 范围外的构造立刻标记、driver 退 66；(2) unsupported 检测要**在会崩/会报错的操作之前**——inst 阶段的（extends 内建、outer 自引用递归、未解析类型）就得在 inst 里标，不能等 flatten；(3) 任何递归实例化都要有**深度守卫**兜底（无限递归 = 崩溃 = 一定是 bug，哪怕成因是"没做 inner/outer 链接"）；(4) 宁可过度 SKIP（把某些本可 PASS 的也标了）也不要留一个 FAIL——SKIP 是进度、FAIL 是债。这样把 155 FAIL 一路压到 0，SKIP 分布正好成了下一步的加宽清单。

## 2026-09-18 — 单测拿"自己实现的输出"当期望值 = 自我确认，扫描门（真 oracle）才抓得住
- 模式：expr_dump 把 `* / ^` 打成无空格（`a*b`、`r1^r2`），flat_dump_test 的 20 条优先级用例期望值又是照着这个错实现手写的，于是单测全绿。真相是 OMC `ExpressionDump.mo:112-116` binopSymbol1 所有二元算符两侧都带空格（`" * "` `" / "` `" ^ "`），R13 §4.2 line126 本来也写对了（"两侧各一空格；标量 + - * / ^"）——是我实现时凭印象去了空格，又用自证的单测把错误锁死。只有 flat_sweep（逐字节比真 omc 输出）在 OperationPow1 上暴露 `r1^r2` vs `r1 ^ r2`。
- 规则：**打印器/格式化的期望值必须来自 oracle，不能来自自己的实现**。写 `*_dump` 单测时，每条期望字符串要么引 MLS/OMC 源码行号，要么先 `omc probe.mo` 实跑取真值粘进去，不能"我觉得应该长这样"。有 oracle 的阶段（S2 scode dump、S3 flat dump）优先靠 sweep/对拍守正确性，单测只锁"已对拍确认过"的样子——单测防回归，对拍防臆想。

## 2026-09-18 — 单测的流水线要和 driver 完全一致，否则漏判 inst 阶段的 SKIP
- 模式：加 extends 修饰后，`flatten_extends_modifier_on_nested_type_unsupported` 期望 SKIP 但单测判 uns=false。原因：`flatten_src` 助手只读了 `FlattenResult.unsupported`，没读 `Instantiator.unsupported()`——而 nested-type extends 修饰是在 **inst 阶段** mark 的。driver（main.cpp）两个都查，单测助手漏了一个，于是"未实现"被误判成"已支持并产出"。ymc 二进制本身行为正确（退 66），只是单测助手失真。
- 规则：**单测的端到端助手必须复刻 driver 的完整判定链**。unsupported 是 inst + flatten 两阶段的并集（`I.unsupported() || fr.unsupported`），助手要合并两者再返回。凡是"驱动器查了 N 个条件、助手只查了 M<N 个"的地方都会漏判——写助手时对着 driver 主循环逐条抄。

## 2026-09-18 — 横切特性（数组）的 FAIL=0 要对着"广语料"验，curated slice 会漏
- 模式：数组标量化的最小版只在 easy-slice（217 例，几乎没数组）上验，显示 FAIL=0，就以为没事。一扫全 scodeinst（1325 例）冒出 34 个数组 FAIL：参数数组绑定要摊到每元素（我只做连续数组的数组方程）、数组运算 `x+y`/矩阵积（整体数组引用我当标量处理）、越界/类型错绑定（omc 报错、ymc 无验证 pass 照产出）、派生类型带维度 `type V=Real[3]`（塌陷时把维度丢了，维度在 derived_attr.dims 不是 derived_type.dims）。都是"我这版不支持却硬产出错文本"。
- 规则：(1) 数组/求值这类**横切**特性，FAIL=0 必须对**广语料**（全目录）验，不能只看 curated slice——slice 是"已达标"集合，天然不含未做特性的坑。(2) 部分实现必须把不支持的分支**显式 SKIP**：参数数组绑定、整体数组引用（数组运算/方程）、非字面量维度、越界/类型不符绑定、派生类型维度——每个都在会产错文本的操作前拦。(3) ymc 无验证 pass，凡是 omc 靠类型/边界检查**报错**的用例（invalid model），ymc 只能靠"形状/类型/边界的最小检查"退成 SKIP，别硬产出。(4) 字段存哪要 grep 确认（derived 维度在 derived_attr.dims），别照名字猜。

## 2026-09-18 — empty_node()=raw 0 也 is_valid()：判"找到没"必须用 !nodes[x].is_empty()，不是 id.is_valid()
- 模式：跨作用域常量沿 parent_scope 链 find_local，未命中时 find_local 返回 `Inst::empty_node()`（= `NodeId::from_index(0)`，raw 0）。我用 `if (cc.is_valid())` 判是否命中——但 raw 0 的 `is_valid()` 返回 **true**（0 是合法下标，哨兵语义靠 arena 里的"空节点"体现，不是靠 id 无效）。于是空结果被当成命中，`eval_const_value(空节点, 未初始化 scope)` 一路 `comps[空].def` 越界 → `contains(id)` 断言崩。12 个用带 `time`/import 的用例全崩（它们的名字解析不到本地，触发跨作用域查找返回空）。
- 规则：**本仓库 NodeId 的"无效/空"是 raw 0，且 `is_valid()` 对 raw 0 返回 true**。判一个节点 id 是否真的指向实体，一律照 callers 的既有写法 `x.is_valid() && !I_.nodes[x].is_empty()`（或至少 `!I_.nodes[x].is_empty()`），**不能只用 `id.is_valid()`**。find_local 之类"未命中返回 empty_node()"的函数，其返回值必须这样判。新写任何走 parent_scope/instance_parent 链、或接收 find_* 返回值的代码都按此审。

## 2026-09-27 — 既有 PASS 集会遮住"顺序"类 bug：OMC 的排序/遍历规则要用"反序声明"探针专门戳
- 模式：connect 成员序我一直按声明序发，slice 里所有连接器成员恰好按名序声明（e,f / e1,e2,f1,f2），门一直是绿的。做嵌套连接器时读 makeConnectorType → ClassTree.enumerateComponents 才发现 OMC 是按名 LookupTree 折叠（净效果升序），写了 `Real e2; Real e1;` 探针一跑立刻暴露：连接方程 e1 在前。这类 bug 的特点：输出顺序来自 OMC 某个数据结构的遍历序（树 / cons 链 / 插入序），语料没覆盖"声明序 ≠ 该序"的形态就永远不会 FAIL。
- 规则：(1) 凡是 ymc 输出里存在"顺序"（成员、集合元素、方程序、变量序），必须去 OMC 源码找产生该序的**数据结构遍历**（LookupTree fold / cons+reverse 链 / UnorderedMap 插入序 / DisjointSets 根序），把链条推到底并记文件:行，不能拿"声明序"当默认。(2) 每条顺序规则至少一个**反序声明**探针（名字声明成与预期序相反）实跑 omc 逐字节验证再进单测；单元素/恰好有序的形态不算验证。(3) cons、`listReverse`、`List.mapFlat`（块内反转）、`listAppend(x, acc)` 前插——MetaModelica 这些习惯每个都翻一次序，推导时逐个数，最后用探针核对净效果（见 docs/research/connect-sets-order.md）。
