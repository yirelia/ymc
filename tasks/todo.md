# tasks/todo.md — 当前计划

对照《docs/spec/ymc-需求规格与RFC.md》。本轮 = M0 基建（Claude 实现；编译器核心由用户手写）。

## M0：三方对比基建（进行中）

- [x] `tests/omc/omc_diff.py` — omc-diff 复刻（FR-002）；单测 14 + 对拍 omc-diff.exe 10 组全一致
- [x] `tests/omc/run.py` — rtest 运行器 + 阶段门 + self-check（FR-001/003/004/050/060）
      - [x] self-check：对 OMC 自身，判定与 omc-diff.exe 26/26 一致（scoping）
- [x] `tests/omc/cflags_map.json` + `stages.json`（FR-003/050）
- [x] `tools/resultio.py` — .mat/.csv/.mat.xz 读取（FR-010）；val() 与 OMC 逐位一致
- [x] `tools/matcompare.py` — tube/classic/csvtube（FR-011/012/013）；单测 9 + 对拍 OMC diffSimulationResults 3 组一致
- [x] `tools/varnorm.py` — 变量名归一化与交集（FR-014）
- [x] `tools/structcompare.py` — checkModel/backenddaeinfo/bltdump/Base Modelica 解析（FR-015）
- [x] `tools/dymola_driver.py` — 无窗口驱动 + 翻译日志统计解析（FR-020/021）；stats 解析已单测
- [x] `tools/refgen.py` — 参考生成 + manifest + 过期（FR-030/031）
- [x] `tools/report.py` — report.md + 台账 ledger + 待办块（FR-040/041/042）
- [x] `tools/gate_engineering.py` — 工程门 G3（FR-062）
- [x] `tools/probe/*.mo` — C-PROBE 探针（Linear/Nonlinear/Event）
- [x] `docs/open-questions.md` — 待验证活文档（TR-003）
- [ ] 首次 git 提交（TR-001，**由用户做**）：S0 骨架 + 本轮基建 + Spec
- [ ] 安装 Dymola wheel 到 Python 3.11 后跑 `dymola_driver.py --smoke`（需用户执行）
- [ ] 在若干 MSL 模型上实跑 Dymola，取样 getLastError 文本，固化 stats 解析正则（TBD-02）

## 待用户拍板（阻塞后续里程碑，不阻塞 M0）

- [ ] TBD-05：ymc loadModel 是否复刻 OMC 版本回退映射（3.2.x→3.2.3、2.2.2→4.0.0）
- [ ] TBD-08：各阶段 G1/G2 通过率阈值、S7 退出阈值
- [ ] TBD-11：Dymola 语料优先级（默认 C-MSL-EX→C-MAPLIB→提取 .mo）
- [ ] TBD-12：erroneous 用例错误文本对齐深度

## S0 核心（应用户要求由 Claude 写"第一个版本"，2026-09-14）

- [x] `src/base/symbol.h` + `symbol.cpp`（T-S0-01）：Interner/Symbol；`symbol_test` 9 项全过
- [x] `src/base/arena.h`（T-S0-02）：Arena/Id；`arena_test` 10 项全过
- [x] `docs/adr/001-symbol-interning.md`、`002-arena-id.md`
- 两套编译器（gcc-debug、msvc-debug）零警告，ctest 3/3 通过（含 smoke）

## S1 第一个版本（应用户要求由 Claude 写，2026-09-15）

- [x] `src/base/loc.h`（SourceLoc）、`src/base/diag.h`（Diagnostics）
- [x] `src/parse/token.h` + `lexer.{h,cpp}`：完整 token 集 + 手写词法器（关键字/Q-IDENT/数字/字符串/注释/全部算子）
- [x] `src/absyn/absyn.h`：Absyn 树（Exp/Cref/Modification/ClassDef 用 Arena+Id；元素/方程值语义）
- [x] `src/parse/parser.{h,cpp}`：递归下降 + 分层优先级表达式；错误恢复 + 进度保证
- [x] `src/absyn/unparse.{h,cpp}`：优先级感知反解析（round-trip 幂等）
- [x] `src/driver/main.cpp`：`ymc <f>` / `--dump-after=parse` / `--check-syntax` / `--dump-tokens`
- [x] `tests/unit/parse/parser_test.cpp`：词法/结构/round-trip 幂等/优先级/connect/if/数组
- 两套编译器零警告，ctest 4/4 通过
## S1 v2（2026-09-15）——方案 §5 S1 验收 ①② 达成

- [x] 树扩展：元素/方程/语句进 Arena（复合构造经 Id 嵌套）；新增 Matrix/Tuple/Reduction/Subscript/Break 表达式、命名实参、`f(i).y`、`function f(...)` 部分应用
- [x] 类：短类定义、枚举（含 `(:)`）、`class extends`、`der(f, x)` 偏导类、external 段、`initial algorithm`
- [x] 元素：redeclare/replaceable/inner/outer、constrainedby（兼容 2.x 的 `extends` 写法）、条件组件、`import A.{c, e = d}`、注解全部保留
- [x] 方程/语句：if/for/when 方程；赋值/元组赋值（含空位 `(, x)`）/调用/if/for/while/when/break/return 语句
- [x] 修饰：redeclare/replaceable 元素修饰（`component_clause1` 单声明）、`= break`、`break connect(a, b)`
- [x] 词法：`.13E2`、`$` 标识符（OMC 兼容）；`import A.*` 接受 `.*` 记号
- [x] `tools/parse_sweep.py`：整目录 `--check-syntax` + `--roundtrip` 幂等扫描，按 status 分桶，TIMEOUT/CRASH 为门
- **扫描结果（全部 --roundtrip）**：

  | 语料 | 文件 | status=correct OK | RT-FAIL | TIMEOUT | CRASH |
  |---|---|---|---|---|---|
  | flattening/modelica | 2628 | **1919/1919 (100%)** | 0 | 0 | 0 |
  | MSL 4.0.0+maint.om | 2508 | 2508/2508 | 0 | 0 | 0 |
  | simulation/modelica | 383 | 120/120（全部 383 OK） | 0 | 0 | 0 |

  flattening 剩 5 个 ERR 全是 `incorrect`/`none`：`x[1]=2` 修饰名带下标（OMC 文法 Modelica.g:955 同样在语法层拒绝）、algorithm 里的 connect、PDEModelica 扩展（范围外）。
- 两套编译器零警告，ctest 4/4（parser_test 含 11 组 round-trip 用例）
## S1 收尾（2026-09-16）——方案 §5 S1 验收 ①②③ 全部达成

- [x] **验收 ③ 错误位置对齐 OMC**：`tools/parse_error_align.py` 对 `openmodelica/parser/*.mo` + 3 个 flattening 用例，
  从 `// Result:` 取 OMC 期望位置比对起点 → **23/23（100%）**，0 误报；13 个无错文件均 OK；9 个 MetaModelica 跳过
- [x] 诊断框架改为 OMC 格式 `[file:sl:sc-el:ec:writable] Error: msg`（供 L1/L3 文本比对复用）；EOF 列记 0
- [x] 复刻 OMC 的语法层检查（位置到列）：首尾类名不同（报在类头名字）、`end;` 缺名、算法段 `=`、方程段 `:=`、
  元组赋值右侧非调用、方程段单独非调用表达式、`{}`、import 前导 `.`、类型前缀顺序/唯一、修饰名用关键字、
  嵌套 `else if` 提示、顶层类缺 `;`；无前缀元素首组件名缺失报在元素起点（ANTLR 回溯约定）
- [x] `--std=X.Y`（pure/impure 3.3 起才是保留字）；`--log=parse` 规则入口跟踪；`--files-from` 单进程批量
- [x] `tools/parse_sweep.py` 按目录 Makefile 的 `FAILINGTESTFILES` 单独分桶（OMC 自己跑不过的不进门）
- [x] 单测 `error_positions_match_omc`（16 条断言）；ctest 4/4：gcc-debug / gcc-release / msvc-debug / msvc-asan 全绿零警告
- [x] **ASan 全量**：ASan 版 ymc 跑 flattening 2628 文件含 round-trip，0 报告（149 s）
- [x] 性能基线 `docs/perf-baseline.md`：Release 整库 MSL 解析 1.02 s（320 k 行/s）；omc loadModel 2.22 s
- [x] 文档：`docs/s1-文法对照表.md`（MLS 附录 B ↔ 解析函数 ↔ Absyn，含 OMC 偏离 MLS 清单）、`docs/adr/003-absyn-representation.md`
- **最终扫描**：flattening `correct` **1843/1843**（另 150 个 OMC 自标 failing 中 149 OK、1 个与 OMC 同位置同错）、MSL 4.0.0 2508/2508、
  **MSL 3.2.3 212/212、Buildings 9.1.0 4425/4425**、simulation/modelica 383/383；0 RT-FAIL / TIMEOUT / CRASH
- [x] 修正：`assert` 不是保留字（MLS B.1 / OMC 词法器均无），改为普通标识符——Buildings 里 `Assert assert(...)` 组件名曾报错；保留字表已与 OMC `BaseModelica_Lexer.g` 逐词对比
- 语言版本：解析器实现 MLS 3.6 文法（3.x 的超集）；`--std=<3.3` 时 pure/impure 退回标识符；老库（MSL 3.2.3、msl22 用例）解析正常
- 已知不做：PDEModelica / MetaModelica 扩展；`--std` 除 pure/impure 外的版本差异

## S2 第一个版本（完成，2026-09-16 ~ 09-17）——方案 §5 S2

依据（不臆想）：研究底稿 `docs/research/R8`（OMC ClassLoader/loadModel/getClassNames）、`R9`（SCode.mo / AbsynToSCode.mo / SCodeDump）、
`R10`（MLS 3.6 §13.4、§18.8）+ 探针 `docs/research/probe-{r8,r9,s2}/`。决策：ADR-004（dump 逐字对齐 OMC）、ADR-005（库查找与版本选择）。

### 装载器（`src/scode/{version,loader}.{h,cpp}`、`src/absyn/absyn_util.{h,cpp}`）
- [x] 版本号（MLS §18.8.1 三形式 + OMC 的 `+build` / semver `-prerelease`）、四种存储形式、MODELICAPATH 有序根
      （`--modelica-path` > `OPENMODELICALIBRARY` > `MODELICAPATH` > `%APPDATA%/.openmodelica/libraries`）
- [x] 版本选择：复刻 OMC 两层机制的**结果**，不读 index.json（ADR-005）；22 个判别探针（Modelica 13 + Foo 9）固化成单测
- [x] `package.order` 精确语义（名单逐项消费、常量整块、未列出追加、列出不存在则整份作废、无 order 时"外部子项逆字典序 + 自身元素在末"）
- [x] 错误路径对齐 OMC：一文件一类、类名/within 不符（仅大小写 → Warning）、`package.mo` 须为长类、包内 `X.mo`+`X/` 并存、
      任一子文件失败 → 整库失败；诊断文案逐字取自 `Error.mo` / R8 探针原文
- [x] `uses`：只认 `Name(version="s")`；装载失败只给 Note、父库成功（依赖自己的 Error 保留，同 OMC）；已装载版本不同按 conversion 注解分级
- [x] 门：`tools/scode_compare.py` 三套库 PASS——MSL 4.0.0 **6427/6427**、MSL 3.2.3 **6180/6180**、Buildings 9.1.0 **5965/5965**，
      装载的库集合、类集合、类顺序都与 OMC 一致，0 诊断
- 分工记录：错误路径与 `package.order` 的对齐由 **Codex** 按任务卡 `docs/tasks/T-S2-03-loader-omc-alignment.md` 实现（只动了卡里允许的 4 个文件）；
  Claude 验收时改了三处：去掉"uses 依赖先在隔离树里试装一遍"（整库被解析两次、且 OMC 并不回滚错误）、版本选择补上"无 conversion 历史的库直接放宽"
  与 semver 预发布、清理未用参数与过期注释

### SCode（`src/scode/{scode.h,absyn_to_scode,scode_dump}`、`src/base/strings.h`）
- [x] IR：Element（Class/Component/Extends/Import）、ClassDef（Parts/ClassExtends/Derived/Enumeration/Pder）、Mod（NOMOD = 无效 Id）、
      Prefixes/Attributes/Restriction(+purity)、Equation/Statement（含 Assert/Terminate/Reinit/NoRetCall）；表达式复用 Absyn
- [x] 翻译的归一化全部照 `AbsynToSCode.mo`：组件列表拆分、可见性进前缀、**多个 equation 段反序**、`x.y = z` → `x(y = z)`、组导入拆分、
      if 方程摊平 elseif、多迭代器 for 嵌套、空修饰规范化（注解顶层保留 NOMOD 子修饰）、注释串解转义、
      external 段注解与类注解合并（**安装版 1.25.0 行为，HEAD 已改**）
- [x] dump 版式照 `SCodeDumpTpl.tpl` 并用探针原文固化单测；driver `--dump-after=scode [--class=Q.N]`
- [x] 为了能逐字对齐，S1 的 Absyn 改了四处（ADR-004）：括号入树、Real 字面量与字符串保留原文、`elseif` 标记、类注解合并 / 段边界 / purity / operator function
- [x] 门：`tools/scode_dump_compare.py`（对 OMC `list(C, exportKind=SCode)` 逐行 diff）最后一轮（新种子 11/12/13）——MSL 4.0.0 **200/200**、
      MSL 3.2.3 **120/120**、Buildings 9.1.0 **150/150**；S1 回归：flattening 1843/1843、MSL 2508/2508 round-trip 0 失败，错误位置对齐 23/23
- 对拍工具归一化、ymc **不复刻**的三类 OMC dump 失真：多行字符串续行被模板按缩进重排；if 表达式 ≥2 个 elseif 被折行；
  `list(A.B)` 在 A 同时含 `extends B;` 与嵌套类 B 时返回 extends 子句
- 性能（`docs/perf-baseline.md`）：release 装载 MSL 1.70 s、Buildings 链 4.85 s；SCode 翻译 + 整库 dump 只多 0.18 s / 0.50 s

### 工程门
- [x] gcc-debug / gcc-release / msvc-debug 零警告，ctest 7/7（loader_test 31 例、scode_test 6 例、parser_test 含 OMC `ryu_to_hr` 向量）
- [x] msvc-asan：ctest 7/7；ASan 版 ymc 对 MSL 3.2.3 做"装载 → SCode → 整库 dump"（8.8 万行）0 报告

### 遗留 / 已知不做
- `package.encoding`、加密库 `.moc`（只给 Warning）、`requireExactVersion`、OMC 的 `Case` 夹具（Windows 大小写不敏感导致的重复类，属 OMC 缺陷）
- `getClassNames(recursive=true, includeConstants=true)` 在 OMC 里是坏的（R8 结论 12），不能当 oracle
- S1 的 `--dump-after=parse` 现在与 OMC 的 Absyn dump 同一套表达式文本，但**类/元素层的版式**（注解折行等）还没对齐 `list(X)`——需要时再做

## S3 实例化 / 类型 / 压平（进行中，2026-09-17 起）——方案 §5 S3

### 验收集重排（2026-09-17 普查，事实）
方案 §5 S3 表里给各子阶段列的验收目录（scoping / packages / declarations / extends / modification / redeclare / types / arrays…）
**几乎全是旧前端用例**（文件内 `annotation(__OpenModelica_commandLineOptions="-d=-newInst")`）：flattening/modelica 下 status=correct 的
新前端用例 954 个，其中 **927 个在 `scodeinst/`**；旧前端用例 938 个，按方案 §6.2 不做 L1。
所以 S3 的 L1 门 = `scodeinst`（按用例名前缀分桶：Ceval 117、Func 97、Function 32、Redeclare 31、Inner 29、If 34、Record 28、Dim 22、
Class 21、Operation 25、Import 12、Extends 12、mod/Mod/Merge 24 …），外加 398 个 incorrect/其它用例对拍错误文案。
旧前端目录以后可以"去掉注解后在安装版 omc（新前端）上重生成期望"当作第二语料（`tools/refgen.py`），不急。

特征普查（正则粗筛源码部分）：927 个里用到数组 404、function 191、algorithm 151、extends 139、record 113、for 97、redeclare 92、
条件组件 89、connect 69、if 方程 44、when 21、external 23、stream/expandable/clock 各约 20。
**不含这些"硬特征"的有 220 个**（`docs/research/s3-easy-slice.json`）——这就是第一个竖切片的目标集。

### 做法：先打通竖切片，再按失败桶加宽（同 S1 的扫描驱动）
ymc 对不支持的特性以**退出码 66 + `ymc: not implemented: <特性>`** 退出，运行器记 SKIP；扫描结果分 PASS / FAIL（输出错了 = bug）/ SKIP（还没做 = 进度）。
任何时候 FAIL 必须为 0 才算一个可提交的状态。

- [x] 基建：`tests/omc/run.py` 改成单次运行 + 退出码 66 → SKIP、识别文件内旧前端注解；`tools/flat_sweep.py`（并行、按前缀分桶、首个差异）
- [x] **研究底稿完成（2026-09-18，5 份共约 4300 行 + 227 探针）**：`docs/research/R11`（实例化管线 + InstNode/ClassTree/Component 数据结构 + arena+Id 落法）、
      `R12`（名字查找：两族入口、LookupTree、LookupState、双父链、import）、`R13`（压平 DAE-dump 版式：变量行/属性顺序/表达式括号优先级表）、
      `R14`（类型/variability/编译期求值/化简 + 各趟硬顺序）、`R15`（Modifier arena/合并/SPLIT_PROXY/extends/redeclare/条件组件）
- [x] **架构定稿：`docs/adr/006-s3-inst-flat-architecture.md`**——四层 IR（inst 树 / 类型化 Expr / FlatModel / DAE-dump 文本）各 arena+Id；
      INode 双父链；Modifier 独立 arena + 结构共享；类型化管线硬顺序；压平打印器用优先级表加括号（区别于 S1/S2 unparse），Real 复用 `real_literal_text`；
      v1 竖切片边界（标量最小闭环，排除数组/函数/redeclare/条件组件/package 常量）
- [x] **落地步骤 1**（2026-09-18）：`src/inst/type.h`（Type：enum kind + 载荷，扁平 ARRAY；Dimension；`type_promotable`）、
      `src/inst/expr.h`（类型化 Expr：arena+Id+kind，与 Absyn::Exp 并存不互转；ExprCref/BinOp/RelOp/LogOp/Variability）。g++ -Werror 零警告
- [x] **落地步骤 2**（2026-09-18）：`src/inst/modifier.h`（Binding 状态机 + Modifier 三态 + ModifierArena，Nomod 哨兵 Id 0，
      子修饰按字符串字典序、比较归修饰 pass）、`src/inst/inode.h`（INode/InstNodeType/Class/ClassTree/Component + 双父链 +
      `Inst` 容器拥有全部 arena，EMPTY 哨兵 Id 0；SlotId 间接层按 ponytail 推迟到 redeclare/数组竖切片）。
      g++ -Werror 零警告；MSVC 校验等第一个 .cpp 接入 CMake 时做（现无 .cpp 引用，构建暂不编）
- [x] **输出侧先行（2026-09-18，落地顺序步骤 5 的 dump 部分，提前做为具体对拍目标）**：
      `src/flat/flat.h`（FlatModel/FlatVariable/FlatEquation/FlatStatement，标量 + 普通等式/赋值，enum 给全）、
      `src/flat/expr_dump.{h,cpp}`（类型化 Expr → 文本：R13 §4.1 间距 + §4.2 优先级表加括号 + CAST + Real 复用 real_literal_text）、
      `src/flat/flat_dump.{h,cpp}`（FlatModel → DAE 版式：R13 §2 段序 / §3 变量行 / §6 方程）。
      `tests/unit/flat/flat_dump_test.cpp` 手搭 FlatModel/Expr 逐字对照 R13（含 §4.2 括号 20 例）。
      接入 CMake（src/inst+src/flat 进 ymc_frontend）；两套编译器 **8/8 通过、零警告**。R13 已够细，自己写了没交 Codex
- [x] **落地步骤 3 第一里程碑（2026-09-18）**：`src/inst/inst.{h,cpp}`（Instantiator）+ `tests/unit/inst/inst_test.cpp`：
      make_top_node（内建 Real/Integer/Boolean/String + 顶层类节点）、partial_inst（建 ClassTree、组件/extends Ref 占位、保序）、
      expand（解析 extends、递归 expand 基类、本地组件进 lookup、环检测占位态）、instantiate（克隆 = 新 ClassId 断共享）+
      inst_class（建 FlatTree：递归组件 + extends 就地展开 + protected extends 改可见性）、inst_component（内建标量→叶 / 用户类→复合、
      绑定留 Raw）、最小 lookup_class_name（内建短路 + 沿词法外层链）。**8 例单测**：标量组件、绑定留 Raw、单/多层继承就地展开、
      复合组件、protected extends、每个使用点私有克隆、缺基类报错。三套编译器 **9/9 零警告**，ASan 0 报告。
      **未做**（后续子步）：修饰合并、短类、内建属性、inner/outer、redeclare、数组；lookup 还是最小版（未做 LookupState/复合名/import）
- [x] **落地步骤 3-4：instExpressions + flatten（2026-09-18）**：`src/flat/flatten.{h,cpp}`——
      instExpressions（`inst_exp`：Absyn 绑定 → 类型化 Expr、cref 解析 + 前缀改写、整型→实型字面量折叠 / 非字面量插 Cast）+
      flatten（`flatten_class`：带前缀 DFS，叶组件→FlatVariable、复合组件递归、protected 上下文传播、顶层方向）。
      driver：`ymc X.mo` 默认压平（同 `omc X.mo`，取最后一个顶层类），`--dump-after=flat`；v1 未实现构造 → 退 66（记 SKIP）。
      `tests/unit/flat/flatten_test.cpp` 6 例逐字对照 omc 实测。
- [x] **S3.1 门达成：easy slice 220 例 FAIL=0**（2026-09-18）：`tools/flat_sweep.py --cases-from docs/research/s3-easy-slice.json`
      → **15 PASS / 205 SKIP / 0 FAIL / 0 崩溃**。每例要么与 `omc X.mo` 逐字一致，要么干净退 66。
      三套编译器 10/10 零警告，ASan 0 报告（含递归 InnerOuter12 的深度守卫）。
      SKIP 分布 = v1 未做的特性清单（后续子步的加宽目标）：函数/ceval 60、属性修饰 26、方程/算法 22、短类/枚举 21、
      inner/outer 15、extends 修饰 13、常量求值/跨包引用、连接器、import、去重…
- [x] **加宽①方程段（Equals）**（2026-09-18）：flatten 收 `equation`/`initial equation` 段里的纯等式方程（`SEqKind::Equals`），
      两侧走 inst_exp（cref 解析 + 前缀 + int→real 强转），其余方程形态（connect/if/for/when/assert…）标 unsupported。
      顺带修两个真 bug：(1) Windows stdout 文本模式把 `\n` 翻成 `\r\n`，与 omc 的 LF 逐字节不同 → `_setmode(stdout, _O_BINARY)`；
      (2) expr_dump 的 `* / ^` 漏了两侧空格（OMC ExpressionDump.mo:112-116 全带空格）→ 补上并改正自证的单测。
      `__OpenModelica_commandLineOptions`（evaluateAllParameters/mergeComponents）会重写整个结果 → 标 unsupported。
      → **21 PASS / 199 SKIP / 0 FAIL**；三套编译器 10/10 零警告，ASan 0 报告。
- [x] **加宽②内建属性 start/unit**（2026-09-18）：标量叶组件的内建属性修饰 `x(start=…, unit=…, min=…, stateSelect=…)`
      → 有序属性表（R13 §4.2，按类型属性序产出，与书写序无关）+ Real 属性 int→real 强转（`to_real` 补折 `-<intlit>`）+
      枚举字面量值（`StateSelect.never`/`E.two` 直接建 EnumLit，绕过跨作用域 cref 检查）。丢弃 Real.unbounded/String.fixed。
      复合组件上的修饰（`B b(x=1.0)`）→ 标 "modifier merge"（下一桶）；无绑定 parameter 带 start → 标 unsupported（参数默认值规则）。
      → **22 PASS / 198 SKIP / 0 FAIL**（StateSelect1 新过；机制另用探针 5 类型逐字对拍 omc）；三套编译器 10/10 零警告，ASan 0 报告。
      注：BuiltinAttribute1 的 Real/Int/Bool/String 行已正确，但整例被其枚举类型组件 `E e`（短类）挡在 SKIP —— 机制已就绪，
      待"短类塌陷"桶补上枚举类型组件后自然转 PASS；Each4/BreakModifier3 的子组件属性也复用本机制，待"修饰合并"桶。
- [x] **加宽③修饰合并**（2026-09-18）：复合组件修饰沿 flatten DFS 逐层剥离传播（R15 §3.3：无全局环境，每层按名拆子修饰）。
      支持：外层实例修饰覆盖声明默认（`A a(x=2.0)` > `Real x=1.0`）、嵌套 `b(a(m(z=…)))`、重名并集（mod4 两个 `a(...)`）、
      final 沿子树传播（`final A a` / `A a(final x=…)`）、each 在标量上无害、修饰值在书写作用域解析（ModScope1 `a(x=x)`→`c.a.x=c.x`）、
      多段本地 cref `a.x` 解析到叶子取正确类型（避免误 Cast，Visibility2）。SKIP：嵌套类修饰 `a(B(...))`、redeclare/break、
      整体绑定 `A a=A(...)`、双作用域叠加。ClassMod1/BreakModifier/ceval 仍 SKIP。
      → **31 PASS / 187 SKIP / 0 FAIL**（218 例）；mod/Each/Final/Mod/Visibility 桶转正；三套编译器 10/10 零警告，ASan 0 报告，+3 单测。
      注：FinalParameter2/3 从 slice 剔除并记入 open-questions 分歧账本 —— 文件 `// Result:` 是较新 OMC 生成（内联 final 参数 `y=3.0`），
      安装版 omc 1.25.0（本项目 oracle）实测 `y=a.x` 不内联，ymc 与 1.25.0 逐字一致。
- [ ] **下一子步（按 SKIP 桶加宽，每步对拍）**：短类塌陷（`type V=Real(unit=…)` 派生/枚举类型组件，23 例，也解锁 BuiltinAttribute1）→
      evalConstants（常量内联、参数默认值）→ 完整 lookup（复合名/import/内建枚举）→ inner/outer → 函数/ceval → 数组
- [ ] S3.1 门：easy slice 220 例 FAIL=0，PASS 数记入本节；错误用例（incorrect）里查找/修饰类的文案对拍
- [ ] 之后按桶加宽：3c 数组（Dim/Array/Subscript/Size/For…）→ 3e 函数与求值（Func/Function/Ceval/Record/External）→ 3d connect
      （Connect/Expandable/Stream/Cardinality）→ 3b 余下的 Redeclare/Constraining → when/if/算法 → 3g Base Modelica + MSL Examples

## 下一里程碑

- S3 门 + 结构比对接 Dymola（spec FR-052）。
- S1 后续可选：S1 任务卡（若用户接手手写）；`--dump-after=parse` 的 `.absyn` 文本回读（方案 §2.1 契约，S3 前不急）。
- M3：S3 门 + 结构比对接 Dymola。
