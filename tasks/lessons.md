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

## 2026-10-02 — inst_exp 给数组字面量的是粗类型（标量 Real）：凡判"是不是数组 / 什么形状"一律走 expr_shape，别看 ty
- 模式：`inst_exp` 把 `{…}` 定成 `ty = Type::real()`（无维，"dump 不看"）。模型语境的 `op_type` / `scalarize` 直接看 `ty.is_scalar()`，于是
  `p + {1, 1, 1}` 被当成"数组 + 标量广播"，逐元素打成 `p[1] + {1, 1, 1}`——绑定、方程都中，静默错出很久，因为语料里恰好没有
  "cref 数组 ± 字面量数组"。直到做数组属性切分（`start = p + {1, 1, 1}`）逐行对拍才暴露；修完又顺带暴露多维展开打成平铺
  （ArrayAddSub1）、向量化调用被误折（FuncVectorization3）——先前都被同一个粗类型 SKIP 遮着。
- 规则：(1) 判数组 / 形状 / 秩用 `expr_shape`（看穿字面量、一元、if），不用 `Expr.ty`；新写处理类型化 Expr 的代码先问"字面量进来会
  怎样"。(2) 修掉一个"让路径提前 SKIP"的粗糙点后，下游从未跑过的代码会被首次执行——必须全量重扫（scodeinst + nf-live）再看
  新冒出的 FAIL，它们多半是早就写错、只是没被走到的分支。(3) 加宽时把每个新形态拆成"一行一个模型"逐行对拍（`tools/probe_lines.py`），
  比整模型 diff 更快定位哪一行错。

## 2026-10-02 — 探 omc 时别把 stderr 并进来当 oracle：成功运行的告警走 stderr，门只比 stdout
- 模式：探 WillowForest 时用 `omc X.mo > f 2>&1`，看到 "Components are deprecated in class." 告警行，以为 omc 的 flat 输出带告警、
  ymc 必须对普通 `class` SKIP，加了守卫——nf-live 当场 PASS 417 → 312（一百多个本来逐字节一致的 `class` 用例被误伤）。查
  nf_live_sweep / flat_sweep：两者都只比 **stdout**；omc 成功时告警打在 **stderr**（报错退出时才混进 stdout）。
- 规则：(1) 做 oracle 对拍的探针一律 `omc X.mo 2>/dev/null`（或分开存 stdout / stderr），与门的比较口径一致；看告警再单独看
  stderr。(2) 新加的"为告警而 SKIP"守卫，先在全量 nf-live 上看它误伤了多少原本 PASS 的用例——大批 PASS → SKIP 就是口径错了。

## 2026-10-02 — 新表达式形态要探"嵌在运算里"的位置；门跑着时不许动源码 / 重建
- 模式 1：59c 引入推导式 Reduction 时只探了"整个绑定 / 等式一侧"和 `sum(…)` 实参两种位置就提交了。共享的 `scalarize` 给
  Reduction 加了代入展开，于是 `x = 2 * {…}`、`y = -{…}`、`s = {…} * {1, 2, 3}` 都被逐元素展开——omc 对含数组调用的式子是
  **整体保留**并按数组算符打印（`Expression.hasArrayCall` 用的是 `contains`，任意深度）。语料里没有这种用例，门全绿，三个
  提交之后做向量化调研时才撞见（R1 / R2 探针）。
- 规则 1：给共享 helper（scalarize / simplify / expr_shape …）加一个新 kind 的分支前，列出它的**所有调用点**，每类位置各探
  一个模型（顶层、嵌在二元 / 一元运算里、作内建实参、作点积操作数、参数摊分）。没探过的位置默认拒绝：在 helper 里用"核实过
  的调用点才置位"的开关（`red_scalarize_ok_`）收口，而不是默认放行。
- 模式 2：59f 的门还在跑（切片扫描用 gcc-debug 的 ymc、随后 MSVC 两套重建），我就改了 flatten.cpp 并重建 gcc-debug——那次
  门验的已不是暂存的那份代码，结果作废、整门重跑。
- 规则 2：门启动后，到输出里出现 `BUILDS-DONE` 之前，**不编辑 src/、不跑任何 cmake --build**（探针只用已有二进制 + omc）。
  想到要改的东西先记下来，等 BUILDS-DONE 再动；动之前先 `cat` 一眼门的输出确认。

## 2026-10-02 — omc 的化简只走一遍：某些构造产出后"不再化简"，ymc 到处重复 simplify 会把它们折坏
- 模式：做向量化时顺手探 `sum(sin({1, 2, time}))`，ymc 出 `1.75… + sin(time)`，omc 出 `0.84… + 0.90… + sin(time)`。往下挖
  发现是一整族早就存在的错出（V4d / V4e 探针 56 行里 22 行不一致）：`sum({1, 2, time})` → omc `1.0 + 2.0 + time`、
  `sum({x[1], 0, x[2]})` → `x[1] + 0.0 + x[2]`、`sum(-x)` → `(-x[1]) + (-x[2]) + (-x[3])`、`sum({i for i in 1:3})` →
  `/*Real*/(1 + 2 + 3)`、`max({i for i in 1:3})` → `max(i for i in 1:3)`。原因：omc 的 simplifySumProduct 把各元素各自化简后
  直接用 `Expression.BINARY` 串成链，链本身不回头化简；Ceval 只在"实参化简后是字面量数组"时才把整个调用折成一个数。ymc 的
  做法是串完再 `simplify_expr(acc)`，而且 call() 里对常量数组一律 eval_reduction 整体求值——两处都比 omc 折得多。
- 规则：(1) 照 omc 的某个 simplifyXxx 实现时，逐句看它**对产物还做不做化简**（直接构造 `Expression.BINARY` = 不做；调
  `simplifyBinaryOp` = 做一层），不要默认"最后 simplify 一下总没错"。产物不该再化简的，用标记集合（`nofold_bins_`）让
  simplify_expr 原样放行。(2) "整体求值"（Ceval）的触发条件是**实参化简后是字面量**，不是"实参是常量表达式"；数组构造子
  （推导式、向量化调用、linspace）不是字面量。(3) 给一个内建加宽时，把它的实参形态成组探一遍（纯字面量 / 混合字面量 /
  变量 / 参数 / 推导式 / 嵌套调用 / 一元负 / 逐元素运算），一行一个模型——这次 7 组 206 行探针撞出的潜伏错比新功能还多。

## 2026-10-02 — "结构参数"会顺着裸引用绑定传染；从探针里读规则要多备几个对照组
- 模式：做 Evaluate = true 时探 `parameter Real q = p`（p 被求值）发现 omc 把 q 也打成 final、`x = q` 代成 `2.0`，而 `q = p * 2`
  只是绑定被求值。再探发现这条规则和 Evaluate 无关：`parameter Integer n = 2; parameter Integer m = n; Real y[n]` 里 m 同样
  final——ymc 此前一直出非 final 的 m（潜伏错）。第一版实现（"绑定是结构参数的 cref 就连带"）立刻把一个已核实的单测打挂：
  记录构造绑定 `p3 = … P(a = k)` 拆出的字段 `p3.a` 在 omc 里是求值后的字面量、不 final。
- 规则：(1) 从探针归纳规则时，同一现象至少备四类对照：表达式变体（`-p` / `p + 0` / `(p)`）、来源变体（声明绑定 / 实例修饰 /
  记录表达式拆出 / 数组字面量元素）、类型变体（Real 别名 Integer）、位置变体（声明序颠倒、传递链）。一类只探一个点就下结论，
  多半会把"别的来源的同形表达式"误伤。(2) 规则若依赖"源码里的写法"，就在 emit 时按 Absyn 记候选（alias_keys_），不要到后处理里
  凭类型化表达式的形状猜——类型化之后 `k` 和"拆出来的 k"长得一样。(3) 新规则先跑全部单测再跑探针：已核实的单测是最便宜的
  回归网，这次它在 30 秒内指出了规则的边界。

## 2026-10-02 — 上一桶归纳的规则是错的（别名连带"与声明序无关"）：旧探针没重跑、也没先读 1.25.0 自己的源码
- 模式：59l 收尾时顺手重跑 59j 的探针集，8 例 FAIL——59k 加的"绑定是结构参数裸引用的参数连带 final"把 `parameter Boolean pd =
  pt`（pt 只在 if 表达式选支时才被标结构）也打成了 final，omc 不 final。59k 当时只跑了自己的新探针和两套语料扫描，语料里
  没有这种写法，于是带病提交。往下挖发现规则本身不完整：1.25.0 的结构标记分三个阶段（实例化期 / 绑定定型期按声明序 /
  方程段与压平期），别名连带在**别名自己的绑定定型时**判，依赖连带只进"尚未定型"的绑定（`Binding.untypedExp`）。这些
  只凭探针很难归纳全，读 `git show v1.25.0:OMCompiler/Compiler/NFFrontEnd/NFStructural.mo`（379 行）十分钟就清楚了；而且
  工作区 HEAD 的同一个函数已经改成 `Binding.getExpOpt`（一路连带），照 HEAD 读会得出相反的结论。同一轮重跑还撞出三个
  更早的潜伏错：外层写的修饰表达式按被修饰实例的帧求值（`M m(v = c[i])` 取了 m.i 的值）、求值失败后结构标记不撤销
  （`if time > k` 把 k 标成 final）、if 表达式两支类型不相容被放行。
- 规则：(1) **每桶收尾重跑全部旧探针目录**（`tools/probe_recheck.py`，见 tests/probes/README），FAIL / ACCEPTS-INVALID 必须
  为 0（账本里的除外）——语料扫描只能证明"语料里的写法没坏"，探针才是已核实行为的回归网。(2) 归纳语义规则前先读
  **v1.25.0 标签**的源码（`cd C:/dev/OpenModelica && git show v1.25.0:<路径>`），注释里的行号也按该标签；HEAD 只用来
  理解意图，两者有出入时以 1.25.0 实测 + 标签源码为准。(3) 探针里出现"与次序无关"这类结论时，至少把标记来源换三种
  （实例化期的维 / 定型期的下标 / 方程段的 for 范围）、别名声明位置换前后两种再下结论。(4) 求值带副作用（打标记）的
  地方，失败路径必须撤销副作用；表达式在哪个实例里写的，就按哪个实例的帧求值。

## 2026-10-02 — 探针只铺了"顺的"元素形态：59o 的归约范围带着 20 多个潜伏 FAIL 提交了
- 模式：59q 做参数数组时顺手探了 `sum(x[i] for i in k)`，发现 omc 取值代入却不标结构参数；追到 v1.25.0 的 `foldReduction2`
  才看到展开前有一步 `Ceval.evalExp(range)`。照这条规则回头一探，59o 提交的归约代码在"范围元素是含变量的运算式 / 转型 /
  调用 / 外层迭代变量"上全错（`sum(v * t for v in {2 * t, 3})` omc 是整个保留，ymc 展开了）——59o 的 255 行探针里范围只有
  `1:3`、`{1.5, 2.5}`、`x[1:2]` 这几种，元素不是字面量就是裸引用。同一轮还撞出：标量化后的 `true or b[1]` 不能再化简（已有
  单测拦住了）、omc 的标量化把空 if 分支全删（改语义）、for 迭代变量出现在 fill 的维 / 范围里时 omc 根本不在定型期求值。
- 规则：(1) 操作数是"数组 / 范围"的特性，探针按**元素形态矩阵**铺：字面量 / 参数 / 参数式 / 变量裸引用 / 含变量的运算式 /
  转型（Integer 进 Real 数组）/ 调用 / 外层迭代变量，每格至少一个；只铺顺的那几格等于没探。(2) 凡是"编译期求值"的规则，都
  在 for 方程、for 语句、嵌套归约里各放一遍——迭代变量会改变 omc 求值的时机（定型期循环还没展开）。(3) 读 omc 源码要读到
  被调函数里面：`simplifyReduction2` 本身看不出求值，求值藏在它调的 `foldReduction2` 里。(4) 新加化简规则先看它和"先化简
  后标量化"的次序是否相容：标量化产生的节点要打标记，后续 simplify / clone 都得带着走。

## 2026-10-02 — 换一个语料目录就又撞出错：对拍语料要尽量铺开，而不是只守着两套
- 模式：59r 收尾后顺手把 testsuite 的 simulation/modelica（那里的 .mo 由 .mos 驱动仿真，没有 `// Result:`）交给 ymc 与
  实跑 omc 对拍，379 个模型里 1 个 FAIL（Integer / Real 混合的 for 范围，omc 打 `1.0:n`）；把 flattening/modelica 里**没有
  注解**的 137 个也跑一遍，又出 3 个 ACCEPTS-INVALID（负底数的非整数幂折成了 NaN、extends 经 replaceable 类被放行）。这些
  写法 scodeinst 和 nf-live 两套语料里都没有，探针也没想到去铺。
- 规则：(1) 任何装着 Modelica 模型的目录都是实跑 oracle 的语料——`tools/nf_live_sweep.py --all --dir <目录>`；新拿到一个
  目录先扫一遍，FAIL / ACCEPTS-INVALID 清零后把它加进门。(2) "数值上不合法的常量运算"（负底数幂、除零、定义域外的内建）
  是单独一类：omc 的化简遇到求不出的就原样留下、只在必须求值的地方（constant 绑定）报错，ymc 的折叠要逐个算子对齐，
  不能让 C++ 的 `std::pow` / 除法自己算出 NaN / inf 打出来。

## 2026-10-02 — 门的输出没读完就提交了：ASan 扫描那一行写着 CRASH 2
- 模式：59t 的门跑完，我用一条命令 `tail` 看了最后三行、紧接着就 `git commit`——而最后一行（ASan 版 ymc 扫整个 testsuite）
  正是 `CRASH 2`。事后查明是两个 60 万 / 190 万字节的大模型在 ASan 版下并行跑时超了 30 秒的时限（单独跑 15 秒、干净退出、
  没有 ASan 报告），加长时限重跑 CRASH 0，提交本身没问题——但这是碰巧。
- 规则：(1) 门的输出**先完整读一遍、逐行对基线**，确认无误后再用**单独的一条命令**提交；查看与提交不写在同一条命令里。
  (2) ASan 版的整 testsuite 扫描带 `--timeout 180`（ASan 慢 10–20 倍，默认 30 秒会把大模型判成超时）。(3) 门里任何一个
  数字与基线不同（哪怕只是 SKIP 少了 2 个），先解释清楚再往下走。

## 2026-10-02 — `a < b + c` 打错了括号：打印器的规则从来没有被"算符两两组合"探过
- 模式：做函数体的逐元素算符时去读 omc 的 shouldParenthesize，才发现 ymc 的括号规则写反了一处（优先级相等时该看
  操作数自己可不可结合，写成了看外层算符）。标量算符里两种写法只在"加法出现在关系式右边"时结果不同——
  `a < b + c` 被打成 `a < (b + c)`。这么基本的式子，scodeinst、nf-live、2600 多个探针里一次都没出现过（关系式右边全是
  字面量或单个引用）。
- 规则：(1) 打印器 / 化简器这类"规则表"性质的代码，探针要按**规则表的维度**穷举（算符 × 算符 × 左右位置），而不是等
  语料碰巧撞上；一张 60 行的模型就能铺完（tests/probes/pp.probes）。(2) 从 omc 抄规则时逐字对照条件里的每个实参
  （`isAssociativeExp(inOperand)` vs 外层算符），抄完立刻找一个能区分两种读法的例子去实跑。

## 2026-10-02 — ASan 全绿不等于没有悬垂引用：arena 扩容要"正好踩到"才暴露
- 模式：flatten 里有 17 处往 Inst 的 arena（absyn 的 exps / crefs、scode 的 mods）加节点，而 inst_exp 及其上层栈帧一路
  拿着 `const Exp& e`。`base/arena.h` 文件头就写着"不要持有引用跨越一次 add()"，我在 flatten 里却一直这么干。三套语料
  + 2800 个探针在 ASan 下从没报过——vector 扩容只发生在"合成节点时余量正好用完"的那一刻，小模型合成的节点少、永远
  落在余量里。这次是想给 `array(…)` 加一处合成、回头看既有的合成点才发现的；造一个 400 元素的记录数组字段展开
  （`r.x + r.x`），ASan 立刻报 heap-use-after-free。
- 规则：(1) 往别人（Inst）的 arena 里加节点只走 `synth_*`（预留内不扩容）；新增合成点先问"此刻栈上有没有这个 arena 的
  引用"。(2) "容量 / 规模触发"的错靠语料撞不出来：每加一条按元素个数合成 / 展开的路径，就配一个**大尺寸**探针
  （几百个元素）在 ASan 版下跑一次。(3) 占位返回值不能随手给 0 号节点——它可能正是触发这次合成的式子，调用方接着
  解析它就无穷递归（U4 探针栈溢出）；占位要选"再处理一遍也不会回到这里"的节点。

## 2026-10-02 — 给规则表接第二个用户时，用同一张探针矩阵回头探第一个用户
- 模式：做算法段的整数组赋值时，把"数组绑定整体保留的形态"那张表抽出来共用，探针矩阵是冲着赋值语句铺的。顺手把同样的
  右侧写成绑定再探一遍，结果绑定路径（早就在门里的老代码）冒出 4 类潜伏错：`Real y[2] = z * 1` 打成 `y = z`、`m .^ 2` 的
  类型、字面量里的 Integer 数组元素的转型、带步长切片的终点。它们在 scodeinst / 整个 testsuite / 3400 个探针里都没出现过
  ——当初做绑定时探针只铺了"算符种类"，没铺"恒等元 / 混合类型 / 步长取不到终点"这些边角。
- 规则：(1) 抽公共函数、给老规则表接新调用方时，新矩阵对**新旧两个调用方各跑一遍**（同一行改写成绑定 / 等式 / 语句三种
  位置）；老调用方冒出来的不一致算本桶的活。(2) 探针矩阵的列除了"形态"还要有"恒等元 / 零 / 自身"（`x * 1`、`0 * x`、
  `x - x`、`x .^ 1`）与"混合类型"（Integer 进 Real）两列——化简与转型的次序只有这两列能照出来。

## 2026-10-02 — 放开一个 SKIP 原因，等于第一次让它后面的代码见光
- 模式：59y 放开 when 条件的白名单、修掉"函数体 assert 一律 SKIP"之后，一批用例从 SKIP 直接变成 FAIL / ACCEPTS-INVALID：
  函数形参的注释整个没打、单元素推导式没展开、日文标识符该被词法器拒绝。这些代码早就在，只是这些用例一直被前面的
  SKIP 挡着，从没跑到那里——SKIP 数下降的同时，"首次被执行的旧代码"在上升。
- 规则：(1) 放开 SKIP 的桶，预算里要留出"修后面露出来的错"的份；门里 SKIP → FAIL 的每一例都当作一条新线索按规则表去铺
  探针（这次由 3 个用例铺出 4 类潜伏错、200 多行探针），而不是只把那一例修绿。(2) 排桶先看 `nf_live_sweep.py
  --skip-oracle` 的真余量：SKIP 里一半是 omc 也拒绝的，做出来只是 BOTH-REJECT。(3) 写补丁脚本一律用 Write 工具落盘
  再跑——本会话三次把含反斜杠的 Python 补丁塞进 bash heredoc，`\\n` 被吃成真换行、写坏了 C++ 源文件；heredoc 只用于
  不含反斜杠 / 引号的内容。

## 2026-10-03 — 给值表开一条新的查找路，旧路上的"把关"不会自动跟过来
- 模式：60b 让维 / for 范围经实例路径（`a.j`、`a.p`）查已发射变量的值，第一版只管"查得到"。把同一个值铺到别的编译期
  语境（下标、条件组件、属性、归约范围）再探，冒出三类把关缺口：protected 元素照样读得到（类型化路径有这道检查，按 key
  查表的路径没有——而且常量这条路早就漏着，`constant Integer k = f.p` 一直被放行）；`Evaluate = false` 的参数被照常求值
  （本地参数的老路径也一样错，omc 根本不求）；结构标记只标了被引用的那个元素（omc 标的是整个组件）。另外五个"看变率"
  的判定都只看 cref 的首段，`a.p` 被当成连续量——`y[a.p]` 的下标原样留着，是悄悄打错而不是 SKIP。
- 规则：(1) 新查找路上线前，列出旧路径在"查到值"之外还做了什么（可见性、Evaluate、变率、标记的粒度、求值次序），
  逐条给新路配探针；同一批探针也对旧路跑一遍。(2) 判定函数只看 cref 首段的地方，多段名一来就是错分类——加多段的
  用法之前先 grep 这类"首段判定"，要么改成沿路径判，要么对多段名显式 SKIP。(3) 探针里要有"值相同、但 omc 不许求值 /
  不许访问"的行（protected、Evaluate = false、fixed = false），只铺"能求出来的"永远照不出 ACCEPTS-INVALID。

## 2026-10-03 — 放开的 SKIP 原来还兼着别的岗；标志位只传到一半，等于没有这个标志位
- 模式：(1) 60e 把"带下标的一般表达式"接进类型化路径后，探针回归冒出 4 例 ACCEPTS-INVALID——`f(x)[1]`、`size(m)[1]`
  是 omc 的语法错，ymc 的解析器把它和 `(f(x))[1]` 解析成同一个节点，此前全靠 flatten 里那条"其它基一律 SKIP"兜着；
  放开 SKIP 的同时把这道兜底也拆了。(2) 追一个"参差组件数组"的 ACCEPTS-INVALID 时顺手换成 `constant` 数组再试，
  牵出一族既有错：`eval_reduction` 把 `allow_param` 写死成 true，所有"不许取参数值"的求值都能经 `sum(p)` 读到参数
  ——`parameter Real q = f(sum(p))` 被整个折成字面量（FAIL，60d 及以前一直如此）；另一头，constant 的绑定只在结构
  位置（下标、范围、fill 个数）引用参数时，类型化路径的结构求值把参数吃成字面量，绕过了"constant 的绑定须是字面量"
  这道检查（omc 报绑定变率错）。这两处在 scodeinst / 整个 testsuite / 5500 个探针里都没露过头。
- 规则：(1) 放开一个 SKIP 之前先问"它除了挡未实现，还挡着什么"——解析器比 omc 宽的地方（T-S3-08 这类还没落地的
  任务卡），flatten 的 SKIP 就是唯一的防线；放开时把防线按节点来源留一份（合成的收、书写的照拒），或等解析器补上。
  (2) 带限制标志的求值函数（allow_param、used）每加 / 每查一个帮手，grep 一遍"调用里把标志写死成字面量"的地方
  （`eval_scode(…, true)`）：每一处要么注明这个位置为什么天然是结构位置（下标），要么照调用方的传。
  (3) "求出来是字面量"不等于"变率是常量"：变率检查做在**书写的**式子上，不能做在求值 / 化简之后。
  (4) 定位到一个 ACCEPTS-INVALID 后，把同一行换个变率前缀（Real → parameter → constant）各跑一遍——这次的两族既有错
  都是这样换出来的。

## 2026-10-03 — 从源码读来的打印格式，没有探针走到的那一半是猜的；规则表缺"外层语境的变率"这一列
- 模式：(1) 60f 第一次产出带两个下标的 `{…}[n, 1]`，dump 打成 `[n,1]`——expr_dump 里那条注释引的是
  ExpressionDump.printExp2Str（"下标以 , 无空格相连"），实际压平输出走的是模板 ExpressionDumpTpl（", "）。单下标时两者
  一样，所以一直没露；注释写得有出处、有行号，看着很可信。(2) 归约的 S2 / S3 规则是 59 系列用绑定、for 方程铺出来的，
  for 方程的迭代变量会被代成字面量，于是"常量变率的归约都能求出值"成了默认前提；for 语句的迭代变量是常量变率却没有
  值，omc 求值失败后把归约原样留着——ymc 照 S3 展开，60e 及以前一直是 FAIL，只是 constant 数组的符号下标被 SKIP 挡着的
  那部分这次才露出来（又一次"放开 SKIP 露出后面的旧错"）。
- 规则：(1) 从 omc 源码读来的格式规则，凡是带"列表 / 多个 / 嵌套"的，至少各配一个探针（1 个、2 个、嵌套）再写进
  dump；注释里的出处要是**实际走的那条打印路**（flat 输出走 Tpl 模板，不是 ExpressionDump）。(2) 归约 / 推导式 /
  if 这类"先看变率再决定怎么化简"的规则，探针矩阵要有一列"外层语境"：for 方程（迭代变量有值）、for 语句（常量变率、
  无值）、范围带参数 / 运行时量的 for、数组构造子、嵌套归约——同一个体在这几种语境里走的分支不同。(3) 每放开一类
  SKIP，把它后面第一次被执行的规则各挑一条，在 60 个探针以内先扫一遍（这次 c10 的一行 FAIL 牵出 13 行旧 FAIL）。

## 2026-10-03 — 推迟到收尾才做的求值，丢的是"在哪个实例里"
- 模式：connect 实参一直推迟到 run() 末尾才展开（为了先知道哪些组件被条件移除），展开时要求的维、下标在那时已经没有
  实例帧：维里的参数按声明的缺省值求。`A a(M = 3)` 里的 `C p[M]; connect(p, q)` 只连了 2 对——静默的错，变量是按 3
  发射的，只有方程少了。语料里没有这种写法，探针铺"维取决于实例修饰"这一列才照出来（60g 的 n2）。
- 规则：(1) 凡是"先攒起来、最后统一处理"的东西，攒的时候就把依赖实例上下文的部分求完（或者记下发射时已经求好的值，
  如 array_shapes_），收尾只做与实例无关的组合。(2) 新开一条推迟处理的路，探针矩阵加一列"同一个类的两个实例取不同
  修饰"（`A a1(M = 3), a2(M = 1)`、`A a[2](M = {2, 3})`）。(3) 结构标记的时机要对照 omc 是在实例化期（updateImplicit
  VariabilityEq、markExpSize）还是定型期标的——放错阶段，结构别名的连带就错（这次 AO1 先 FAIL 了一次）。

## 2026-10-05 — 放开一条 SKIP 之前，先看它挡着的 omc 报错用例（第三次"放开 SKIP 露出后面的错"）
- 模式：短类别名到长类此前整条 SKIP（"short class aliasing composite"），顺带挡住了 partial / 受限类型 / inner-outer /
  重复修饰 / 记录折叠路这些检查——61b 第一版放开后，flat_sweep 冒出 6 个 FAIL，nf-live `--all` 冒出 3 个 FAIL + 8 个
  ACCEPTS-INVALID。其中 5 个在放开前就看得到：60g 的 `--skip-oracle` 全景里，这个 SKIP 原因下 omc_ok = False 的用例。按
  "同一特性换容器"铺的邻近探针又照出三处与短类无关的旧错（记录缺省值里的 time、replaceable 的 partial 类型、折叠记录
  丢字段注释）。
- 规则：(1) 放开一个 SKIP 原因之前，先从 `nf_live_sweep.py --all --skip-oracle` 的结果里列出该原因下 omc 报错的用例，
  逐个写清"放开后由哪条检查接住"，没有检查接的先补检查——这一步比事后看回归便宜。(2) 间接层（短类、别名、import）只带
  名字和修饰：放开时把链底长类在直接写法下会被查的性质（partial、受限类型、inner/outer、前缀、修饰合法性）在间接层上
  逐条重查。(3) 邻近探针按"同一特性换容器"铺：继承来的 / 嵌套实例里的 / 记录 / 连接器 / 包里定义的 / 数组。

## 2026-10-05 — 任务卡里写的"omc 行为"，实现前要重新实跑
- 模式：T-S3-09（Claude 10-02 写的卡）第 2 步说"反斜杠后不是合法转义字符 → 报错"，依据是读 BaseModelica_Lexer.g 的
  SESCAPE 规则。61c 实现前把卡里每条行为各配一个探针实跑：`'a\qb'` 在 omc 1.25.0 只出告警 "Lexer treating \ as \\"
  并照收——词法器的错误恢复动作改写了语法规则的字面意思。照卡实现会多出一类"omc 收、ymc 拒"（REJECTS-VALID）。
- 规则：(1) 从语法 / 词法规则文件读来的"报错"结论，必须有探针实跑确认（动作代码、错误恢复会改写规则）；(2) 卡里列的每一条
  行为在动手前各配一个探针重跑，结果和卡不符就先改卡再写码；(3) 报错位置也照实跑结果写单测（这次 omc 的词法错报在
  坏字符的**下一列**，不是坏字符本身）。

## 2026-10-06 — 放开一个 SKIP 原因前，先 grep 单测里断言它的地方
- 模式：61i 放开 constrainedby 的重声明，61h 的单测 inst_extends_redeclare_and_dropped_final 里还断言 RDS2 要 SKIP
  （`"redeclare form"`），正式门的四套 ctest 全红，门白跑一轮。另一处：放开"复合组件的 extends 修饰"后，旧探针 rx-r1 的
  R124（`extends M(redeclare B a, a(p = 9))`，omc 报重复修饰）变成 ACCEPTS-INVALID——它此前是被那条 SKIP 顺带接住的。
- 规则：(1) 放开 / 改名一个 SKIP 原因前，`grep -rn "<原因>" tests/` 找出断言它的单测，按实跑结果改成正向期望再跑门；
  (2) 放开前用临时构建把**全部**旧探针跑一遍（`probe_recheck.py --ymc <临时构建>`），旧探针里"被这条 SKIP 顺带接住的
  错误用例"会现形，先补检查。

## 2026-10-06 — 让更多东西"求得出值"时，查一遍原先靠"求不出"兜底的检查
- 模式：61n 让多段参数引用（`fp.a`）顺绑定链求得出值，if 方程就折掉了——可"条件参数是否固定"的检查只对帧求值记下的单段参数
  （`used`）跑，多段引用此前全靠"求不出 → SKIP"兜底。补的邻近探针一跑：fixed = false / Evaluate = false 在绑定链上的 4 个
  从 SKIP 变成 FAIL（p60 / p62 / p63 / p66）；整个 testsuite 里 RecordBinding16 也从 SKIP 变 FAIL（omc 求值时把中间那层记录
  的绑定换成求得的值）。顺藤摸出一个早就有的错：for 范围求到声明在后的参数，omc 打求得的字面量（p90）。
- 规则：(1) 放宽求值（新增一条能求出值的路）前，列出所有"求值成功后才走"的后续检查（结构 / 固定 / Evaluate / 标记），确认
  它们对新路一样生效；(2) 新路的邻近探针要带上会让后续检查翻转的修饰（fixed = false、Evaluate = false、声明在后、记录引用 /
  构造 / 嵌套记录）；(3) omc 的求值有副作用（evalComponentBinding 存回求得的值），"声明在前 / 在后"各配一个探针。
- 同桶又犯了上一条：IF3 单测断言 `b = c > 1` 要 SKIP（"if condition folds via parameter"），固定检查顺绑定走之后它与 omc 逐字节
  一致了，正式门的 ctest 才红。没 grep，是因为这次不是"删掉一个 SKIP 原因"，而是"改了触发它的判定"。补规则：(4) 改了任何
  SKIP 的判定条件都按"放开"处理——precheck 先跑临时构建的全部单测（scratchpad/precheck.sh 已加 ctest 一步），再跑扫描。

## 2026-10-06 — 模仿 omc"求值存回"时，先问求值发生在流水线的哪一步
- 模式：61t 的 IMC_YD 只剩一处不同（`aimc.statorCoreParameters.m` omc 打 3）。缩出 D40 后先在症状处补了一刀（被"发射前求值"
  换成字面量的别名，另记别名目标），探针全过。再补对照行才看出根因：omc 只在**定型期**的求值（维、条件……）把求得的值
  存回组件；Evaluate / 结构参数的绑定是压平之后 EvalConstants 才求，那时变量早已压平，存回不影响输出。ymc 的
  "touched"规则对所有求值一视同仁——T20/T43 这类此前就错（`d.scp.m` 打 3，omc `d.m`），症状补丁反倒把它盖住了。
- 规则：(1) 补"omc 求值副作用"一类规则前，先查 omc 在哪一步求值（Typing 的 typeDimension / typeIfEquation 是定型期；
  NFFlatten / NFEvalConstants 是压平期），探针各配一行"维里用"与"Evaluate 里用"的对照；(2) 症状补丁过了探针也别收：补一行
  "可疑机制会造成可见差异"的对照（目标不是结构参数、值不同、声明前后各一），修好根因后把症状补丁删掉再跑一遍；(3) 新
  探针矩阵顺带暴露的旧错若不属本桶（Evaluate = false 的声明次序），移出探针包、记进 todo 的下一桶，别让它卡门也别丢。

## 2026-10-06 — 探针矩阵要有"继承 + 派生类同名遮蔽"这一列
- 模式：61t 的探针顺手写了 `model B parameter Real p = q; end B; model R parameter Real q = 3; extends B; end R;`，omc 报
  "Variable q not found in scope B"，ymc 照收。顺着查出继承来的声明（绑定 / 维 / 属性 / 嵌套修饰 / 中间类的 extends 修饰）一律
  在派生类实例里查名：派生类自己的同名元素遮住了基类包里的常量（静默错）。inst 早就给每条绑定记了"写它的类实例"
  （Binding::scope，R12 §2.4），flatten 没用——几千个语料用例里派生类恰好都没有同名元素，所以一直没露出来。
- 规则：(1) 任何"在某作用域里解析名字"的特性，探针都配一行"声明从别的包里的基类继承来、派生类再声明一个同名元素"（参数、
  常量、类型、函数各一），多层继承（中间类在第三个包）再一行；(2) 上游结构里已有的元数据（作用域、来历、书写处）下游没用到时，
  先查是不是漏用，别另起一套补丁（此前的 decl_scope_ 只补了限定名和函数名两处）。

## 2026-10-07 — 照搬 omc 的判定函数前，先问它跑的时候表达式定型了没有
- 模式：61v 照 NFStructural.isExpressionNotFixed 把"不纯 / 外部函数的调用"当不固定，precheck 里 CevalBinding10（`pure`
  external 函数、Evaluate 参数）从在账的 SKIP 变成 ACCEPTS-INVALID。读 NFCall.isExternal 才看出：Evaluate 的判定跑在
  updateImplicitVariability（定型之前），未定型调用的 isExternal 看的是还没实例化的函数类，恒为假——外部函数在那一刻"认不出"。
  同桶另一处：类入口预扫维时把求值拒掉的 Evaluate = false 参数也记进了 final_params（那个 used 集给 if 方程认"非结构"用），
  反把它标成了结构参数。
- 规则：(1) 搬 omc 的谓词时记下它在流水线的哪一步跑、那时输入是什么形态（未定型 / 已定型 / 已求值），谓词里对类型、函数属性
  的判断在未定型时可能恒真或恒假，配探针核实；(2) 一个集合被多个调用方当不同用途（"用到的参数" vs "结构参数"）时，给新的
  写入路径分清是谁在用，别顺手记进去。

## 2026-10-07 — 拒绝 / 限制也要有出处；改名字重定向，探针要有"数组元素前缀"和"声明次序"两列
- 模式：62a 先给复合 outer 加了"inner 的类须是 outer 声明类型或其派生"的检查（MLS §5.4 说 inner 应是子类型），没 grep omc——
  v1.25.0 NF 里根本没有这道检查，不相干的类照样压平（i40 / i42），这条限制把 omc 能压平的例子判成 SKIP。同桶补探针时又露出
  标量 outer 早就有的两处静默错：inner 在数组元素实例里（`C c[2]` 打成 `c.x`，omc `c[1].x`）、引用写在 outer 声明前面（打成
  `b.x`）——此前的探针只有"inner 在根上、引用写在后面"。precheck 里 BioChem extra_cellularTotal 成了 ACCEPTS-INVALID：把根类的
  inner 删掉再跑 omc，告警直接点名了找不到 inner 的那个 outer（行号），顺着才查到 omc 查类名时会把穿过的类整个实例化。
- 规则：(1) 新加的拒绝 / SKIP 条件和语义规则一样要出处（MLS 只说"应当"、omc 不查时以 omc 为准）；(2) 名字重定向一类的特性，
  探针矩阵固定带"目标在数组元素实例里""引用写在声明前面""从上层穿过子实例引用"三列；(3) 库用例的 ACCEPTS-INVALID 先改
  模型让 omc 的诊断点名肇事元素（删掉 inner / 改名），再缩成探针。

## 2026-10-07 — 放开一个构造时，绑定的每种来历各配一行探针
- 模式：62b 放开 Evaluate 参数数组时探针只配了"书写的绑定 / 实例修饰 / 组件数组分发"，没配"值来自外层记录整体绑定拆出的
  字段"（`pr = F(v = …)`）。scodeinst DimUnknown12 正是这一种（omc 不 final），当时恰因别的参数求不出值而 SKIP，62c 修好向前
  引用后才露成 FAIL。标量那边早有 origin 0 / 1 / 2 三种来历的分支（emit_variable、key_not_fixed），数组照搬时漏了。
- 规则：(1) 把标量的规则推广到数组 / 记录字段时，先列出标量实现里按"来历"分的支（origin、rec_bind、by_cref…），数组每支一行
  探针，不支持的支显式 SKIP；(2) 一个桶修好"求不出值"一类的通路后，precheck 里 SKIP→FAIL 的用例先查是不是更早的桶的潜伏错。

## 2026-10-07 — 把"节点"放宽成更弱的一种时，逐个查它的消费方
- 模式：62d 让记录构造调用在模型里没有实例时也能定型，Complex 类型的 complex_node 退到作用域树里的类定义节点。record_match
  逐字段比时直接取 `trees[node_class(n).tree].components`——类定义节点可能还没 partial_inst，树号无效 → arena 越界断言
  （scodeinst RecordOrder1，precheck 的 testsuite 段报 CRASH）。
- 规则：(1) 一个字段从"总是 A 类对象"放宽成"A 或 B"时，grep 它的全部读取点，逐个写明对 B 的处理（这次 11 处 complex_node，
  只有 record_match 碰树）；(2) 探针矩阵配一行"只有新形态、没有旧形态"的模型（没有实例、只有构造调用），并让它走一遍比较 /
  转换这类会深入结构的路径。

## 2026-10-07 — 同一构造的两条通路要并成一条；`f().begin()` 配 `f().end()` 是两个临时对象
- 模式：(1) 记录组件绑定成"返回记录的常量调用"时另有一条折叠通路（emit_record_fields）直接发字段，从没合并字段的属性——
  `parameter R r = g()` 打成 `r.x = 1`，omc `r.x(unit = "m") = 1.0`；构造调用 `R r = R(1)` 走的是另一条带属性的通路，两条各自
  演化。62e 加数组字段时才露出来。(2) 写 `ScodeScope(scode_scope_of(ci).begin() + 1, scode_scope_of(ci).end())`：两次调用返回
  两个不同的临时 vector，迭代器配不成一个区间——未定义行为，单测里抛了 "cannot create std::vector larger than max_size()"。
- 规则：(1) 给一种构造补功能时先查它有几条通路（grep 同一 SKIP 原因 / 同一类数据的发射点），能合成"等价的输入"喂给主通路的
  就合并（这次把记录值合成字段修饰），删掉旁路——合并后旁路原本绕开的检查（Evaluate、final 字段的修饰）会重新起作用，precheck 里逐条核对；(2) C++：要用某个函数返回的容器的迭代器区间，先存进局部变量再取 begin / end。

## 2026-10-07 — omc 的打印形态不是化简：toDAE 才做的变换不能烤进还会被展开的树
- 模式：62f 把含数组调用的 `2 * f(time)` 照 NFOperator.toDAE 换位成 `f(time) * 2.0`，起初在化简时（sym_binary 里调
  fn_array_form）就建成换过位的节点。`sum(2 * {i * time for i in 1:2})` 的实参先化简、再由 sum 逐元素展开，取到的元素成了
  `time * 2.0`（omc `2.0 * time`）——omc 的换位发生在转 DAE 打印时，展开用的是定型时的次序。单测 R2 当场红了。
- 规则：照 omc 实现一个形态前先分清它出自哪一步——SimplifyExp / ExpandExp（改树，后面的步骤看得见）还是 toDAE / DAEDump
  （只影响打印）。后者在 ymc 里只能做成打印标记（ew_binaries / product_binaries / swapped_binaries）或在发射前最后一步做，
  不能改节点本身；探针要带"外层再展开"的组合（sum / product / 下标取元素）才测得出来。

## 2026-10-07 — 新加的节点标记要查一遍所有克隆 / 改写节点的地方
- 模式：62f 把数组运算的打印形态记成节点标记（swapped / ew / product / 书写的算符），探针与门都过了；62i 才发现压平末尾
  代入结构参数时 clone_subst 克隆出的新节点不带这些标记——`parameter Integer k = 2; … x = k * f(time)` 打成 `2.0 * f(time)`
  （omc `f(time) * 2.0`），静默错。探针里没有"符号形态 + 结构参数"的组合。
- 规则：凡是按节点 id 记的旁路标记（集合 / map），加的时候 grep 所有会复制 / 克隆 Expr 节点的函数（clone_subst、subst_slots、
  simplify 的重建…），逐个决定标记要不要跟着走；探针要带"该形态 + 压平末尾代入"（结构参数、for 迭代变量）的组合。

## 2026-10-07 — 模仿 omc 在修饰上做的事，要按合并后的修饰取值
- 模式：fixed 属性里参数的预扫（omc 把 `fixed = T0fixed` 的 T0fixed 标成结构参数）只看组件声明自己的修饰；外层
  `cylinder2(T(fixed = true))` 盖过它时 ymc 照样把 cylinder2.T0fixed 打成 final（omc 不标，它标的是合并后修饰里的值），
  静默错。探针 / testsuite 都没有"外层改掉该属性"的组合，直到 MSL FluidHeatFlow TestCylinder 才露。
- 规则：凡是复刻 omc 对某个修饰 / 属性做的事（标结构参数、求值、判 final），都按合并后的修饰（外来 > extends > 声明）取值；
  探针要带"外来修饰 / extends 修饰改掉该属性"的组合。

## 2026-10-08 — omc 先定型全部分支、压平时才选支：没选中的分支也有副作用
- 模式：ymc 对结构条件为假的 if 方程分支直接丢弃不看；omc 先定型整条 if 方程（typeIfEquation）、压平时才选支
  （flattenIfEquation），定型的副作用——Ceval 求值存回参数绑定（evalComponentBinding）、Structural.markExp 标结构参数——在
  没选中的分支里照样发生。MSL Clocked EventClock 的 `y_clock.solverMethod` 因此打成字面量（ymc 打引用，静默错，扫 MSL 才露）。
- 规则：复刻 omc 的定型期行为时，问一句"被丢掉的分支 / 被删的方程定型过没有"；有副作用的定型逻辑（求值存回、结构标记）在
  丢弃的分支里也要跑（dropped_branch_effects），做不到就 SKIP。

## 2026-10-08 — 标量化 / 取元素之后再化简不是无害的
- 模式：参数数组的 if 绑定逐元素摊开时，ymc 对每个元素再跑一遍 simplify，把 `if b then 1.0 else 1.0` 收拢成 `1.0`、
  `if b then true else false` 收拢成 `b`；omc 取元素是 applySubscriptIf（NFExpression.mo:1653-1683），取完不再化简，照打
  原样。静默错一直在，BooleanTable 的真值表（相邻行同值）才露出来。
- 规则：omc 是"先整体化简、后取元素 / 标量化"的地方，ymc 取元素后的再化简只能是幂等的整理；会改形态的化简规则（simplifyIf
  的两条收拢、布尔 / 算术的合并）对取元素造出的节点要关掉（split_ifs_ 这类标记），探针带"两支在某个元素处相同"的组合。

## 2026-10-11 — 快检只看 FAIL = 0 不够：PASS → SKIP 会被新组的 PASS 盖住
- 模式：63s 给修饰里重声明的链加了个守卫（原声明的维写在哪不知道 → SKIP），快检的探针总数只多了 SKIP 1、PASS 被新探针组
  的 27 个 PASS 盖住，没细看就进了门；门的逐组比对才露出 rf-r3 的 R33 从 PASS 退成 SKIP（字面量维在哪求都一样，守卫过宽）。
- 规则：快检也跑逐组比对（precheck.sh 已加：只许多出新增的组）；新加守卫（mark_unsupported）时先想一遍已经 PASS 的写法里有
  没有它会误伤的（这里：字面量维），守卫只拦真会出错的那一角。

## 2026-10-11 — 保守护栏要贴着语义触发点：PASS → SKIP 清单逐条当回退看
- 模式：64a 给"被移除的条件组件里的结构标记"加 SKIP 护栏，触发条件写成"修饰绑定的目标在类里任何子组件的修饰 /
  条件里出现过"，`x(start = x_start)` 这类与结构无关的传递全中——precheck 里 MSL 52 个、testsuite 3 个 PASS → SKIP，
  FAIL 仍是 0、门不会红。
- 规则：护栏只拦 omc 真会产生差异的那个语义位置（这里：维 / 条件 / fixed / stateSelect，经子组件修饰递归），不拦
  "名字出现过"；precheck 的 MSL / testsuite 变化清单里每一条 PASS → SKIP 都当回退处理，不是只看 FAIL = 0。

## 2026-10-11 — 逐组比对的正则只认带横杠的组名，rec / alg / v4 这类组的回退看不见
- 模式：precheck.sh 的逐组 diff 用 `[a-z0-9]+-[a-z0-9]+` 认组行，不带横杠的组（rec、alg、v4、misc …，十几个）整组漏比；
  64c 的 rec/V3 是 FAIL 才在总数行露出来，换成 PASS → SKIP 就静默过去了。
- 规则：按"组名 + 空格 + 判定词"认组行（precheck.sh 已改）；门后手工逐组 diff 用同一个正则。

## 2026-10-13 — 放开一条新查找路径，下游的旧静默错会跟着放出来
- 模式：65a-2 让重声明的包（`redeclare package Medium = P.Water`）在 flatten 里查得到，PK9 转 PASS；但 `Medium.BaseProperties`
  的 class extends 隐式基类一直取共享的基类定义（绕过了 classes_via_mod 守卫），基类里 `Real X[nX]` 读声明值 1，而 omc 取
  extends 实例里的那份（`extends PartialMedium(nX = 2)` → 2 个）。直接写 `P.Water.BaseProperties` 早就静默错（PKX3），只是没有
  探针带"基类体读被 extends 修饰改过的常量"；新路径又把它引到 Medium 用法上（PKX4）。门不会红：语料里没有这个组合。
- 规则：放开新路径（查名、类重声明、实例化）前，探针要带"沿途被修饰 / 重声明改过的东西在下游被读到"的组合（维、绑定、
  基类体、函数体各一）；每个绕开既有守卫的取法（这里：class extends 目标直接取基类树）都要问一遍守卫的理由是否同样适用。
