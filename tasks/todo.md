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
- [x] **加宽④短类塌陷**（2026-09-18）：inst 里把 `type V = Base(mods)` 顺链解析到内建/枚举底（R15 EXPANDED_DERIVED），
      累积属性修饰（存 Component.derived_mods，外层派生优先）+ 累积前缀属性（constant/input/… 沿链，组件自身 > 靠近用处的派生）。
      枚举类型组件塌成 `enumeration(lits)`（enum_literals 取自底层 ENUM 类）；枚举绑定 `E.one`/`P.E.one` 建 EnumLit 打全路径。
      flatten：derived_mods 作为最低优先属性来源并入 collect_attributes；多段本地 cref 解析到叶子取正确类型。
      SKIP：短类别名到长类 `model M = P.M`（牵出常量内联）、extends 短类。
      → **43 PASS / 174 SKIP / 0 FAIL**（217 例，含 BuiltinAttribute1 解锁）；三套编译器 10/10 零警告，ASan 0 报告，+3 单测。
      注：OperationRelational1 从 slice 剔除记入分歧账本 —— flat 模型与 omc 1.25.0 逐字一致，仅文件 Result 多两行 `==` on Real 弃用告警（ymc 尚无诊断）。
- [x] **加宽⑤extends 修饰**（2026-09-18）：`extends A(x=1)` 的修饰在 inst_class 拼接基类组件时合并进继承组件
      （apply_extends_modifier）：绑定覆盖 + 属性（存 derived_mods，靠近派生的优先，沿继承链累积 PropagateExtends）+ final。
      修饰命名非组件（嵌套类型 mod9 `A(MyReal(start=y))`）→ 标 unsupported（类修饰未做）。SKIP：extends 内建/短类、重复元素、复合目标。
      顺带修单测助手 flatten_src：未实现判定要合并 inst+flatten 两阶段（原来漏 Instantiator.unsupported()，见 lessons）。
      → **47 PASS / 170 SKIP / 0 FAIL**（217 例）；三套编译器 10/10 零警告，ASan 0 报告，+3 单测。
- [x] **加宽⑥inner/outer**（2026-09-18）：标量 inner/outer 连接（R11 §5）。flatten DFS 带 inner 作用域栈（外层+本层 inner），
      outer 组件不产变量、按名连到最近外层 inner，记重定向表；cref 解析（首段或多段叶子）命中 outer → 换成 inner 全名（不加前缀）。
      inst：outer 只解析标量类型、不实例化子树（避免 `outer B b` 自引用递归）。深嵌套（InnerOuter2）也过。
      SKIP：复合 outer（`outer A a`）、无匹配 inner。→ **54 PASS / 163 SKIP / 0 FAIL**（217 例）；三套编译器 10/10 零警告，ASan 0 报告，+2 单测。
      注：InnerOuter13 反向分歧记账本（1.25.0 报错、文件+较新 OMC 接受，ymc 按文件 PASS）。
- [x] **加宽⑦数组（字面量维标量化）**（2026-09-18）：叶数组按字面量维度行主序标量化成逐元素变量行（`x[1]…`，IR 加
      FlatVariable.subscripts + dump `[i,j]`）；连续数组绑定 `= {…}` → 数组方程 `x = {1.0,…}`（omc 默认不摊到每元素）；
      下标引用 `x[2]` 保留（cref 下标 + expr_dump `[..]`）。**honest-SKIP 守卫**（对全 scodeinst 1325 例验，数组 FAIL 34→0）：
      参数/常量数组绑定（omc 摊每元素）、整体数组引用（数组运算/方程）、非字面量维、越界/类型不符绑定/下标（ymc 无验证 pass）、
      派生类型带维度 `type V=Real[3]`（维在 derived_attr.dims）、复合数组。
      → easy-slice **55 PASS / 163 SKIP / 0 FAIL**（218 例，加 Subscript1）；三套编译器 10/10 零警告，ASan 0 报告，+4 单测。
- [x] **加宽⑧evalConstants 常量折叠**（2026-09-18）：类型化 Expr 上的 simplify_expr —— 折叠 Int/Real 算术、Int/Real 关系、
      逻辑、Cast、if（常量条件），部分折叠（`x+(1+2)`→`x+3.0`）；constant cref 内联成值（eval_const_value 递归 + 环检测，
      同作用域常量，bindings1 前向引用也过）。应用到每个绑定/方程/属性。**final parameter 不内联**（omc 1.25.0 保留 `y=x`，
      分歧账本 +FinalParameter1）。顺带修：括号子表达式（S1 存成 1 元 Tuple）在 inst_exp 解开——原来任何带括号的表达式都 SKIP。
      SKIP：枚举/布尔/字符串关系（需序数）、内建函数 ceval、跨作用域/package 常量、记录。
      → easy-slice **59 PASS / 158 SKIP / 0 FAIL**（217 例）；全 scodeinst 无新增 FAIL（PASS 56→62）；三套编译器 10/10 零警告，ASan 0 报告，+5 单测。
- [x] **加宽⑨跨作用域常量（enclosing-scope）**（2026-09-18）：cref 本层 find_local 未命中时，沿词法 parent_scope 链找同名
      constant 组件（lookup_const_upscope，从 SCODE 读可变性——def-only 组件 attributes 未填），命中就 eval + 内联（const10 的 `j = i`）。
      eval_const_value 加 decl_scope 参数 + SCODE 绑定回退（跨作用域常量是 def-only，comps.binding 未设）。
      **踩坑**：`empty_node()=raw0` 也 `is_valid()`，判"找到没"必须用 `!nodes[x].is_empty()`——原来 12 个带 time/import 的用例全崩（lessons）。
      SKIP：package 限定名 `P.A.B.i`（const11/14/16/17）、package redeclare 修饰 `package P1=P(i=2)`（ih2/3）——要完整 package lookup。
      → easy-slice **60 PASS / 157 SKIP / 0 FAIL**；oracle=live 59 逐字节一致；全 scodeinst 无新增 FAIL（PASS 62→63）；三套编译器 10/10 零警告，ASan 0，+1 单测。
- [x] **加宽⑩标量内建函数**（2026-09-18）：白名单 Call 定型/符号打印 + 数值字面量常量折叠；覆盖数学函数、
      `abs/sign/integer/div/mod/rem/max/min` 与 `der/pre/initial/terminal/edge/change/sample/smooth/semiLinear/homotopy`。
      参数/常量 `der` 按 ParameterDer 折为 `0.0`；混合数值二元实参提升到 Real；非法常量域、除零、错误 smooth 类型/变率诚实 SKIP。
      SKIP：`Integer(enum)`（Expr 尚无 ordinal）、`delay`/`noEvent`（任务卡“恒符号”与 testsuite 规范化/去包装语义冲突）、
      数组/命名/限定/用户函数。新增 `FuncBuiltinSample.mo` 门用例与 2 个单测。
      → easy-slice **103 PASS / 115 SKIP / 0 FAIL**（218 例）；oracle=live **102 一致 / 0 不一致 / 1 已登记 OMC 错误**；
      全 scodeinst：改前既有 FAIL 91 → 改后 **71**（内建函数净修好 20 个广语料用例），`Func` 18 PASS / 0 FAIL，live 105/0 不一致；
      gcc-debug / msvc-debug / msvc-asan 均 **10/10**，零警告，ASan 0 报告。
      **实现由 Codex 按任务卡 `docs/tasks/T-S3-02-builtin-functions.md` 完成，Claude 验收修 3 处**（der 揭出的既有缺口）：
      (1) if 表达式分支类型统一 `if c then der(x) else 0`→`else 0.0`（IfExpression17/InnerOuter3）；
      (2) 复合/记录组件带整体声明绑定 `P.Clock(1)` → SKIP（Clock6，记录构造未做）；
      (3) `inner outer` 组合前缀 → SKIP（InnerOuter3）。IfExpression17 记入分歧账本（1.25.0 不做 final 参数求值/if 化简，ymc 与 1.25.0 一致）。
- [x] **加宽⑪枚举/布尔关系折叠**（2026-09-18）：EnumLit 补 1 基序数（Expr.enum_index；扫作用域 SCODE 元素找 enumeration 类，
      cref 也认 `E.lit` 建 EnumLit）；simplify_expr 关系折叠扩到枚举（序数）、布尔（false<true）、字符串（字典序），
      加常量逻辑折叠（去掉 rel_or_logic 的过时 SKIP，符号 `a and b` 照打）、`Integer(enum)`→序数。
      ceval3 的 `fixed = <参数>` 需参数求值（omc 求值成字面量 + 标 final）→ 加 `fixed` 非字面量 SKIP 守卫，避免退成 FAIL。
      → easy-slice **106 PASS / 0 FAIL**（CevalRelation1/Logical1/IntegerEnum1 转正）；oracle=live 105 逐字节一致；
      全 scodeinst PASS 106→109，FAIL 71 不变（无新增真 FAIL）；三套编译器 10/10 零警告，ASan 0，+3 单测。
- [x] **加宽⑫package 限定常量**（2026-09-18）：限定名 `P.A.B.c` 在 **SCODE 作用域链**上解析（不经 inst 树，因 package
      常量常未实例化）+ 值语义求值（CVal：字面量/限定常量引用/算术）。作用域链内→外查（支持遮蔽 const17）、前导点从顶层
      （const14）、下钻嵌套包。cref 多段名先试限定常量、再退 SKIP。
      → easy-slice **108 PASS / 0 FAIL**（const14/17 转正 + 常见 `P.c` 模式）；oracle=live 107 逐字节一致；
      全 scodeinst PASS 109→111，FAIL 71 不变（无新增真 FAIL）；三套编译器 10/10 零警告，ASan 0，+2 单测。
      SKIP：const11/16（跨包 `extends P.M`）、ih2/3（package redeclare `package P1=P(i=2)`）——各需另一特性。
- [x] **加宽⑬参数求值/final（结构参数）**（2026-09-18）：参数用在 `fixed=` 属性 → 求值成字面量 + 该参数及其依赖参数标
      `final parameter`、绑定也求值成字面量（ceval3：`fixed = init_x`→`true`，init_x/p1/p2 全 final）。
      eval_scode 扩到结构模式（allow_param + 收集用到的参数 + 逻辑/关系）；flatten_class 预扫各组件 fixed 收集 final 参数集，
      emit_variable 据此标 final + 求值绑定；collect_attributes 对 fixed 做结构求值。
      → easy-slice **109 PASS / 0 FAIL**；oracle=live 108 逐字节一致；全 scodeinst PASS 111→112，FAIL 71 不变；三编译器 10/10，ASan 0，+1 单测。
      注：`start=a`、`y=b` 等非结构上下文的参数**不**求值/不 final（探针确认）；数组维度参数（Dim1）留给数组进阶桶。
- [x] **加宽⑭跨包 extends**（2026-09-19）：基本 `extends P.Base`（限定基类名）本来就靠 lookup_class_name 走通；本桶修**继承成员的
      词法作用域**——inst_class 拼接时把继承组件的 parent_scope 设成基类实例，使其限定名/常量按**基类作用域**解析
      （const16 的 `P.P.i` 在 P.M 里命中嵌套 P.P 的遮蔽，而非派生类）。flatten 记 decl_scope_ 供 cref 限定解析用。
      → easy-slice **110 PASS / 0 FAIL**（const16 转正）；oracle=live 109 逐字节一致；全 scodeinst PASS 112→113，FAIL 71 不变；三编译器 10/10，ASan 0，+1 单测。
      SKIP：const11（还叠加组件修饰 `A a(j=3)` + 成员访问 `a.j`）、import-extends（ExtendImport，需 import 解析）、外部对象/replaceable extends。
- [x] **加宽⑮数组进阶（维度求值）**（2026-09-19）：数组维度支持参数/常量/size()/算术求值（不再只字面量）——eval_dim 走 eval_scode
      结构模式；`Real x[n]`（n 参数→3 + n 标 final，Dim1）、`Real y[size(x,1)]`（size 求值，DimSize1）。eval_scode 加 size(arr,d)
      解析组件维度、按声明类型强转常量值（Real n=3→3.0；Integer=Real 收窄视为类型错→SKIP，ceval5）。
      → easy-slice **120 PASS / 0 FAIL**（+10：DimSize1/2、dim1/7/18、const2/3/5、ceval1、inst3）；oracle=live 119 逐字节一致；
      全 scodeinst PASS 113→123，FAIL 71 不变；三编译器 10/10，ASan 0，+1 单测。
      未做（留后续数组桶）：参数/常量数组绑定摊元素（BindingArray8，多维+cref 元素）、复合数组 `A a[2]`、数组运算/矩阵积。
- [x] **加宽⑯connect（未连接 flow=0）**（2026-09-19）：普通连接器当复合组件摊开成员（`C c`→`c.e`/`c.f`），连接器内未连接
      flow 变量补 `name = 0.0`（R13 §6-2，排在本类方程后）——含 connect() 的模型仍在 collect_equation 整体 SKIP。
      → easy-slice **121 PASS / 0 FAIL**（+1 Connect1）；oracle=live 120 逐字节一致；全 scodeinst FAIL 71 不变；三编译器 10/10，ASan 0，+2 单测。
      诚实 SKIP（各需另一特性）：connect() 方程展开（Connect2+，连接集/inside-outside 符号）、expandable connector、stream、
      连接器成员方向 inside/outside 保留（Direction1）、`flow C c` 前缀下沉复合成员（conn9）、flow 用在非连接器（NonConnectorFlow1，omc 只告警）。
- [x] **加宽⑰记录（构造整体绑定）**（2026-09-19）：记录构造 `R r = R(4.0, 5.0)` 翻成类修饰 `R r(x=4.0, y=5.0)`
      （build_record_ctor_mod：位置实参按声明序、命名实参按名匹配字段，合成 SMod 走既有修饰机制；缺省实参保留字段默认）。
      → easy-slice **124 PASS / 0 FAIL**（+3 RecordBinding1/Clock5/Clock6）；oracle=live 123 逐字节一致；
      全 scodeinst PASS 124→127、FAIL 71→70（0 新增，含 Clock6 从整体绑定 SKIP 转 PASS）；三编译器 10/10，ASan 0，+3 单测。
      诚实 SKIP（各需另一特性）：`constant/parameter R r` 变率下沉成员 + 常量字段引用求值（CevalRecord3/5）、
      记录数组 `R r[2]`（复合数组）、记录 extends（RecordExtends）、构造绑定叠加字段修饰、算法段里的 `r := R(...)`（RecordConstructor1）。
- [x] **加宽⑱connect() 连接集展开（2-连接器）**（2026-09-19）：单条 connect、根层、两个标量连接器——按名匹配成员，
      potential 相等（A 声明序）在前、flow 求和在后；符号 inside=+/outside=−，both-inside 项序反转（复刻 OMC DisjointSets
      seed 反序），both-outside `-(a+b)`、混合 `pos-neg`；flow=0 精化：outside 恒 =0、inside 仅未连接时 =0。
      resolve_connector 把 connect 实参解析成 flat 路径+face+标量成员表。
      → easy-slice **127 PASS / 0 FAIL**（+3 Connect2/Connect5/ConnectDiffOrder1）；oracle=live 逐字节一致；
      全 scodeinst PASS 127→130、FAIL 70 不变（0 新增）；三编译器 10/10，ASan 0，+3 单测；Connect3 分歧记账本。
      诚实 SKIP（各需另一特性）：多条 connect（连接集 union-find、元素序是 OMC 内部产物，算法已记 open-questions）、
      connect 在子模型（前缀化）、数组连接器/成员、嵌套连接器 `c1.c2`/深层 `b.a.c`、parameter/constant 成员（连接生成 assert 相等）、stream、expandable。
- [x] **加宽⑲条件组件 `x if cond`**（2026-09-19）：cond 求值成常量 Bool（MLS §4.4.5）——true 保留（抹条件）、false 移除
      组件（不产变量/不递归 + 记路径）；条件里的参数变结构 final（Condition3，声明处作用域求值兼容继承）；引用它的
      connect 整条丢弃（Condition5，丢后若只剩一条即走单连接引擎）。守卫：方程/绑定引用已移除组件 → SKIP（omc 报错，
      ConditionInvalidContext）、数值组件绑 String/Bool → SKIP（类型错，ConditionInvalidBinding1）、被移除名又被 emit（redeclare
      一个 replaceable-conditional 成员）→ SKIP（RedeclareElementCondition1/2/3）。
      → easy-slice **136 PASS / 0 FAIL**（+9：Condition1/2/3/4/5/10、ConditionInvalidBinding2、DimNegative2、PartialFunction2）；
      oracle=live 逐字节一致；全 scodeinst PASS 130→139、FAIL 70→69（0 新增，反修一例）；三编译器 10/10，ASan 0，+4 单测。
      诚实 SKIP：条件里数组组件（Condition7/8，复合数组）、嵌套条件连接器成员（Condition9）、redeclare+条件语义。
- [x] **加宽⑳数组绑定摊元素（sub-bucket 1/3）**（2026-09-19）：参数/常量数组绑定摊成逐元素绑定（`x[i]=lit`），Var 数组仍留
      单条数组方程（Subscript1）。array_scalar_elem 逐维下降 Array 字面量到标量；矩阵行含 cref 数组 → 剩余下标补到末段成
      `x[i]` 元素引用（BindingArray8 的 `y[1,j]=x[j]`）。constant 元素须求值成字面量。
      → easy-slice **137 PASS / 0 FAIL**（+1 BindingArray8）；oracle=live 逐字节一致；全 scodeinst PASS 139→140、FAIL 69 不变；
      三编译器 10/10，ASan 0，改 1 旧单测 + 1 新单测。纯字面量 param 数组本身 testsuite 无独立用例（多被 `:` 维/复合数组卡住）。
      诚实 SKIP（下个 sub-bucket）：复合数组 `A a[2]`（80 例，payoff 最大）、`:` 维绑定推断、constant 数组元素下标内联、数组运算。
- [x] **加宽㉑复合数组标量化（sub-bucket 2/3，phase 2a）**（2026-09-19）：`A a[2]` 逐元素以 `a[i]` 前缀递归展开成员
      （`a[1].x` `a[2].x`）。名字模型加逐段下标 name_subs（与 name 平行，末段叶数组仍用 subscripts）；PSubs 穿过
      flatten_class/emit_*/collect_equation，cref 前缀改写读成员 cur_psubs_ → 元素内引用带段下标（`a[i].z=a[i].x`）。
      去重比较加 name_subs。守卫：flatten_class 入口 `if unsupported return`（大数组早停）、复合数组元素数 >1024 SKIP
      （ArrayConnect3 cells[1000,100] 爆量）、表达式里显式下标引用复合数组元素 `a[3].x` 保守 SKIP（越界校验未做，ArrayBounds2）。
      → easy-slice **141 PASS / 0 FAIL**（+4 Prefix1/2、bindings4、eq6）；oracle=live 逐字节一致；全 scodeinst PASS 140→144、
      FAIL 69 不变（0 新增）；三编译器 10/10，ASan 0，+2 单测。**phase 2a 只做无修饰**；数组修饰分发 `a[3](x={…})` 留 phase 2b。
- [x] **加宽㉒复合数组修饰分发（sub-bucket 2/3，phase 2b）**（2026-09-19）：组件自身声明修饰 `A a[3](x={1,2,3})` 逐元素分发
      （distribute_mod：每个子修饰绑定按元素下标 aidx 逐维下降取标量 → `a[i].x=vi`；`each` 子修饰对每元素保持原值），
      合成 SMod 走既有修饰机制。外来/派生修饰的数组分发、子修饰带嵌套属性（`x(start=…)`）仍 SKIP。
      → easy-slice **145 PASS / 0 FAIL**（+4 BindingArray1/2、eq4、eq7）；oracle=live 逐字节一致；全 scodeinst PASS 144→148、
      FAIL 69 不变（0 新增）；三编译器 10/10，ASan 0，+1 单测。
      诚实 SKIP：修饰提供值 + 跨成员常量求值（CevalBinding3 的 `z=x+y` 需 eval 解析修饰值，ymc eval_const_value 只看节点绑定）。
      **sub-bucket 2 完成**；剩 sub-bucket 3 数组 connect（连接器数组 + for-connect + 连接集）暂缓（记 open-questions）。
- [x] **加宽㉓用户函数常量折叠**（2026-09-19）：`f(3.0)` 解析到 function 类 → 解释函数体求常量（无函数块发射）。
      eval_user_call：绑定实参（位置/命名/缺省）到输入 → 局部环境 locals_（eval_scode 单名 cref 先查它）→ 解释 output 绑定
      `output T y=expr` + 直线赋值算法 `y:=expr`（exec_stmt）→ 读单输出、按声明类型强转。call() 与 eval_scode Call 都先试折叠。
      守卫（各为 omc 报错/须发射 → SKIP）：impure 函数（须发射 ImpureCall1）、函数注解 inverse/derivative（告警
      FunctionInverseInvalid1/2）、公开非-io 变量（FunctionNonInputOutputParameter）、给输入赋值（AssignInput1）、多输出/外部/短类。
      → easy-slice **152 PASS / 0 FAIL**（+7：CevalFunc1、FuncSimple、FuncDefaultArg1/5、FuncLocals、FuncWildcard、ceval4）；
      oracle=live 逐字节一致；全 scodeinst PASS 148→155、FAIL 69 不变（0 新增）；三编译器 10/10，ASan 0，+3 单测。
      诚实 SKIP（下个函数增量）：if/for/while/return 控制流（CevalFuncFor/If/While）、数组/记录参数与返回、函数块发射（非常量实参调用）。
- [x] **加宽㉔用户函数控制流**（2026-09-19）：函数体解释加 if/elseif/else、for（range `a:b`/`a:step:b` 展开）、while、return、break
      （exec_stmt 返回 Flow 信号，exec_body 传播；eval_range 求常量整数序列）。for/while 迭代上限 100000（omc evalLoopLimit）、
      递归靠 eval_scode depth≤100——超限放弃折叠（→ SKIP，omc 也报 loop/recursion limit）。守卫：顶层 break（不在循环内）→ SKIP（Break2）。
      → easy-slice **159 PASS / 0 FAIL**（+7：CevalFuncIf1、CevalFuncFor1/2、CevalFuncWhile1、Break1、Return1、FunctionRecursive1）；
      oracle=live 逐字节一致；全 scodeinst PASS 155→162、FAIL 69 不变（0 新增）；三编译器 10/10，ASan 0，+1 单测。
      诚实 SKIP：when/assert/terminate 语句、数组/记录参数与返回、函数块发射（非常量实参调用）。
- [x] **加宽㉕函数返回记录常量折叠**（2026-09-19）：CVal 加 Record 变体（shared_ptr + COW set_field）。函数解释器：output
      记录用字段修饰 `r(x=x,y=y)` 建 Record、算法字段赋值 `r.x:=x` 增量建 Record；`constant/parameter R r = f(…)` 若折成
      Record 常量 → emit_record_fields 直接发射 `constant Real r.x=1.0` 等（继承 r 变率/final，绕开"变率复合" SKIP）。
      → easy-slice **161 PASS / 0 FAIL**（+2 CevalFuncRecord1/2）；oracle=live 逐字节一致；全 scodeinst PASS 162→164、FAIL 69 不变；
      三编译器 10/10，ASan 0，+1 单测。诚实 SKIP：符号函数求值（CevalFuncRecord3 `f(r1)` 字段未赋值 → `r2.x=r1.x`）、
      数组值/`:` 维推断（CevalFuncArray）、嵌套记录/数组字段、`constant R r = R(…)` 记录构造的常量传播（CevalRecord3/5 还需字段引用求值）。
- [x] **加宽㉖常量数组规约（sum/product/min/max）**（2026-09-19）：CVal 加 Array 变体（shared_ptr）。eval_scode 数组字面量
      `{…}`（可嵌套）→ CVal::Array；eval_reduction 把单数组实参的 sum/product/min/max 摊平后规约成标量（Int→Int、含 Real→Real；
      min/max 保留胜出元素类型）。call() 与 eval_scode Call 都先试规约折叠。顺带修 for 隐式范围 `for i loop`（无 in range）
      崩溃 → 干净 SKIP（ImplicitRangeAlg2；eval_range 守 invalid）。
      → easy-slice **165 PASS / 0 FAIL**（+4 CevalSum1、CevalProduct1、CevalMin2、CevalMax2）；oracle=live 逐字节一致；
      全 scodeinst PASS 164→168、FAIL 69 不变（0 新增）；三编译器 10/10，ASan 0，+1 单测。
      诚实 SKIP（下个数组增量）：带迭代器规约 `sum(e for i in …)`（Reduction；CevalReduction 还需多迭代器/空范围默认/bool·enum，暂缓）、
      数组函数值 + `:` 维推断（CevalFuncArray）、数组方程/运算。
- [x] **加宽㉗for 方程展开**（2026-09-19）：`for i in RANGE loop … end for` 常量范围（`a:b`/`a:step:b`/整数数组）逐值展开体方程
      （collect_equation For；loop_vars_ 迭代变量→值）。迭代变量作值 `=i` → 内联字面量（cref）、作下标 `x[i]` → `x[1]`
      （subscript_int 解析下标）、嵌套依赖范围 `i+1:i+3`（eval_scode 也读 loop_vars_）。守卫：非常量范围/隐式范围/遮蔽同名
      迭代变量（omc 告警，ForEquationShadow1）→ SKIP。
      → easy-slice **172 PASS / 0 FAIL**（+7 ForEquation1–7）；oracle=live 逐字节一致；全 scodeinst PASS 168→175、FAIL 69 不变
      （0 新增）；三编译器 10/10，ASan 0，+2 单测。诚实 SKIP：for 语句（算法段）、if/when 方程、非常量下标、数组方程。
- [x] **加宽㉘assert 方程**（2026-09-19）：`assert(cond, msg[, level])` → FlatEqKind::Assert（flat_dump 已支持）；默认
      level=AssertionLevel.error 省略（比 eq.c 与 scode.assertion_level_error 哨兵）。参数类型校验（omc 报错 → SKIP）：
      cond 须 Boolean、msg 须 String、显式 level 须枚举（AssertInvalid1/2/3）。
      → easy-slice **174 PASS / 0 FAIL**（+2 Assert1/2）；oracle=live 逐字节一致；全 scodeinst PASS 175→177、FAIL 69 不变
      （0 新增）；三编译器 10/10，ASan 0，+1 单测。诚实 SKIP：显式非默认 level（AssertionLevel.warning 等，inst 未解析内建枚举，Assert3）。
- [x] **加宽㉙terminate 方程**（2026-09-19）：`terminate(msg)` → FlatEqKind::Terminate；msg 须 String（omc 报错 →
      SKIP，TerminateInvalid1）。→ easy-slice **175 PASS / 0 FAIL**（+1 Terminate1）；oracle=live 逐字节一致；全 scodeinst
      PASS 177→178、FAIL 69 不变（0 新增）；三编译器 10/10，ASan 0，+1 单测。reinit 留待 when 方程（Reinit1 在 when 里）。
- [x] **加宽㉚when 方程 + reinit**（2026-09-19，并行子agent出任务卡）：`when/elsewhen … then …` → FlatEqKind::When（flat_dump 已支持）。
      条件白名单（标量 Boolean：关系[操作数非调用]/Boolean 变量/initial()）；体只做标量 Equals + reinit（目标须连续 Real 变量）；
      所有分支须解同一组 LHS（When7/VariableMismatch）；initial 段禁 when。顺带补 der 校验：der 作用于离散变量或非-Real（被插 Cast）
      → SKIP（FuncBuiltinDerInvalid1/2，之前被 when SKIP 掩盖）。
      → easy-slice **183 PASS / 0 FAIL**（+8 When1/4/5/6、Reinit1、WhenCondition1/3、WhenInitial2）；oracle=live 逐字节一致；
      全 scodeinst PASS 178→186、FAIL 69 不变（0 新增）；三编译器 10/10，ASan 0，+2 单测。
      诚实 SKIP：数组/Clock/not-initial/pre/noEvent 条件、数组体、嵌套 when、when 在算法段、ReinitInvalid（离散/参/常/数组目标）。
- [x] **加宽㉛noEvent 常量折叠**（2026-09-19，子agent survey出卡）：`noEvent(常量)` 脱壳成字面量、符号参数保留 `noEvent(...)`。
      守卫：Boolean 变量绑定到符号 noEvent(…)（noEvent 抑制事件→连续，绑离散是错误）→ SKIP（NoEvent1；窄化到 noEvent-Call，
      不误伤合法 `Boolean b = x>1`）。→ easy-slice **184 PASS / 0 FAIL**（+1 CevalNoEvent1）；oracle=live 逐字节一致；
      全 scodeinst PASS 186→187、FAIL 69 不变（0 新增/0 丢失）；三编译器 10/10，ASan 0，+1 单测。
      子agent 另确认：tan/asin/exp/log/… 等标量内建早已在表内；String()/pure()/枚举构造器需另加基建，暂缓。
- [x] **加宽㉜if 方程**（2026-09-19，子agent出卡）：常量条件求值折叠（真分支选中：无前置→内联体、有前置→成 else；假分支丢弃），
      否则发射 flat if（FlatEqKind::If，flat_dump 已支持）。collect_if_body 快照法把分支体搬进独立方程序列（in_if_ 禁分支体 connect，
      save/restore 支持嵌套）；cond_is_runtime 判运行时条件（time/Var/Discrete/残留调用）；只折 used 为空的纯字面量/迭代变量条件
      （参数条件 omc 标 final、ymc if 路径不标 → SKIP）。守卫：非 Boolean 条件类型 → SKIP（IfEquationInvalidCond1）。
      → easy-slice **191 PASS / 0 FAIL**（+7：IfEquation1/2/7、IfEquationEval1/2/3、NoEvent2 连带）；oracle=live 逐字节一致；
      全 scodeinst PASS 187→194、FAIL 69 不变（0 新增/0 丢失）；三编译器 10/10，ASan 0，+1 单测。
      诚实 SKIP：枚举/下标 cref 条件（ymc eval 未做）、参数条件、firstTick、复合数组体、if 在 when（IfEquationImpure1）。
- [x] **加宽㉝模型级算法段 CORE**（2026-09-22，并行子agent出卡）：删整类 SKIP("algorithms")，flatten_class 末尾照方程序收集算法段（子先本类后）。
      collect_algorithm→collect_statement：v1 只做标量赋值 `cref := expr`——LHS 须解析成 Cref（常量已内联成字面量→非 Cref→SKIP）、无下标
      （`x[i]:=` 属 for 层）、目标须已发射的连续变量 Var（给参/常/离散赋值是 omc 错误→SKIP）；RHS 数组/元组→SKIP；整型→实型强制（`x:=1`→`x:=1.0`）。
      连带补组件声明注释（emit_variable 标量路径 `Real x "c";`，dumper 已支持）→ Comment1 过。其余语句 kind（for/if/when/while/assert/…）诚实 SKIP。
      → easy-slice **195 PASS / 0 FAIL**（+4：Algorithm1/2/3、Comment1）；oracle=live 逐字节一致；全 scodeinst PASS 194→198、FAIL 69 不变（0 新增）；
      三编译器 10/10，ASan 0，+2 单测。FOR 层（保留 for 循环 + 符号迭代/下标 + Range 机制）留待下一桶。
- [x] **加宽㉞数组竖切片（Type 地基 Option B + 逐元素数组方程 Tier A/B）**（2026-09-22，用户定 Option B，并行子agent出卡）：
      Type 补 boxed `array_elem`（shared_ptr<Type>；不变量 kind==Array⟺!dims.empty()⟺array_elem!=null）+ array_of/element_type（对标 NFType.ARRAY/liftArrayLeft）。
      cref 整体数组引用 `x` 提升成 ARRAY 类型（维须字面量）；binary/unary/rel_or_logic 传播维度（gate is_array()，标量路不变）；
      scalarize(expr,idx) 逐下标标量化（cref 补末段下标、二元/一元/逻辑重建、Int→Real 逐元素）；collect_equation 两侧同形数组→行优先枚举+逐元素标量等式。
      守卫（保 FAIL=0）：非数值元素（记录数组/运算符重载 OperatorOverloadArrayOp*）、矩阵乘（* 两侧数组）、矩阵幂（^ 含数组 OperationPow2/3）、
      除法（/ ./ 操作数提实型 Tier C 暂缓 OperationDiv*）、数组关系、复合数组前缀（cur_psubs_ 非空 eq2/eq3）→ 一律 SKIP。
      → easy-slice 204 PASS/0 FAIL（+9：OperationLogicalUnary1/Unary1/LogicalBinary1/Mul1/MulEW1/PowEW1/AddEW1/SubEW1、eq8）；
      oracle=live 逐字节一致；全 scodeinst PASS 198→207、FAIL 69 不变；三编译器 10/10、ASan 0；+3 单测（1 桩改成标量化断言）。
      Tier C（Div 操作数提实型）、矩阵/切片/数组 connect、数组-of-记录 留待后续。
- [x] **加宽㉟数组方程 Tier C（`/` 恒实除）**（2026-09-22）：修正 Modelica `/` 语义——整型操作数也提实型
      （`i/j` → `/*Real*/(i) / /*Real*/(j)`，MLS §10.6.1），标量 binary()、array_binary、scalarize 三处同改（op==Div → real）。
      顺带修了标量 `/` 潜伏 bug（原产 `/*Real*/(i/j)` 整除后转型，与 omc 不符）。array_binary 放开 `/`：数组÷标量广播逐元素、
      数组÷数组（非点号）仍 SKIP。→ easy-slice 206 PASS/0 FAIL（+2：OperationDiv1、OperationDivEW1）；oracle=live 逐字节一致；
      全 scodeinst PASS 207→209、FAIL 69 不变；三编译器 10/10、ASan 0；+1 单测。矩阵/切片/数组 connect、非字面量维（×99）留待后续。
- [x] **加宽㊱`:` 维从数组构造子推断（CevalArray 系）**（2026-09-22，用户定子桶，子agent两次 ECONNRESET 后自行落地）：
      发现"非字面量维 ×99"非单一特性——eval_dim 已能折叠常量/参数/size/算术维；真正卡住的 `:` 维分四簇（数组常量绑定推断、类型维 Bool/enum、`:`+矩阵/reduction、omc 报错）。本桶做第一簇。
      emit_array 重排——先备维表达式，绑定就绪后解析各维；`:`（ExpKind::Colon）维用 binding_dim_size 从绑定构造子形状逐层推断（矩形，行优先）。
      is_array_ctor 认 `{…}`（ExpKind::Array）与 `array(…)`（Call，MLS §10.4.1.1 等价，元素同在 .args），接进绑定检查/摊分/形状匹配三处；
      `array(e for i in r)` 是 ExpKind::Reduction → 不认（正确 SKIP）。守卫：顶层 input/output 数组带绑定 omc 丢方向+绑定（标量不丢，探针实测）
      quirk 未复刻 → SKIP（修 TopLevelInputs2 回归）。→ easy-slice 209 PASS/0 FAIL（+3：CevalArray1、DimUnknown1、DimUnknown6）；
      oracle=live 逐字节一致；全 scodeinst PASS 209→212、FAIL 69 不变；三编译器 10/10、ASan 0；+1 单测。
      非纯 `:` 用例（常量数组元素折叠、array(…for…) reduction、复合记录数组、size/矩阵）仍 SKIP，各是独立后续特性。
- [x] **加宽㊲常量数组下标元素折叠**（2026-09-22，并行子agent出卡，穷举 1325 文件）：
      cref() const 内联点加 eval_const_array_element——constant 数组绑定求值成 CVal::Array，逐层按（常量）下标取标量
      （行主序，对标 NFCeval subscriptBinding/applySubscripts）。严格 gate SVariability::Const（参数数组 omc 保留符号 x[2]，已正确 PASS，不动）。
      复用 eval_scode（绑定→数组、下标→整/loop_vars_）、coerce_cval、literal_of。守卫（保 FAIL=0）：非常量下标（x[n]→omc 留 {…}[n]）、
      越界、切片/整取/部分下标留数组、构造子/函数/记录数组绑定 → 一律 invalid→SKIP。→ easy-slice 212 PASS/0 FAIL
      （+3：CevalArrayConstant1/3、SubscriptCevalIndex1）；oracle=live 逐字节一致；全 scodeinst PASS 212→215、FAIL 69 不变；
      三编译器 10/10、ASan 0；+2 单测（N-D 行主序/表达式内下标/整→实；非常量下标 SKIP）。end 下标、array(…for…) 构造子求值、记录数组元素留后续。
- [x] **加宽㊳非构造子数组绑定**（2026-09-22，并行子agent穷举出卡）：emit_array 放开 `Real x[N] = <非{}构造子>`。
      类型化绑定一次（inst_exp→数组 Expr），same_dims 形状守卫 + cond_is_runtime 变率守卫（param/const 绑更高变率=omc 报错→SKIP）、
      discrete 数组绑定→SKIP。Var：整取同型 cref→`x = y`（NFScalarize ARRAY_EQUALITY 不标量化）、逐元素算子→展开 `{op(y[i])…}`（NFExpandExp）；
      param/const：逐元素 scalarize（`q[i]=p[i]`、`/*Real*/(ip[i])`）。守卫（保 FAIL=0）：整取 Int→Real 数组 cast（A6，omc 出 `/*Real[3]*/(yi)`，ymc 无数组 cast）、
      切片（inst_exp 拒 range）、常量数组 RHS（omc 内联，ymc 无）、标量 RHS 缺 each、shape 不符、Call/Range → 一律 SKIP。
      → easy-slice 213 PASS/0 FAIL（+1：bindings7）；oracle=live 逐字节一致；全 scodeinst PASS 215→216、FAIL 69 不变；三编译器 10/10、ASan 0；+1 单测（A1/A2/B1/B6）。
      （testsuite 纯非构造子数组绑定用例稀少，多为合成探针；数组 cast / 切片 / 常量数组绑定留后续。）
- [x] **加宽㊴type-typed 维（x[Boolean]/x[E]）**（2026-09-22，用户定"参考OMC实现走"=Option B）：
      先 refactor 377fc67——FlatVariable 子下标 int64→Id<Expr>（子下标=表达式，对标 NFSubscript；纯表示层，行为不变）。
      再 emit_array 每维建 dim_labels：classify_type_dim 认 Boolean（→[false,true] BoolLit）、本地枚举 type E（find_enum_def→[E.lit…] EnumLit）、
      builtin StateSelect（MLS §3.8.3 固定 5 值）；否则整数维 IntLit 1..n。末段下标从行主序 idx 取该维标签；dump 走 dump_expr。
      连带修空数组潜伏 UB（某维=0 时旧 idx_to_exprs 越界；omc 整体丢弃空数组=独立特性 → SKIP EmptyArray*/含 0 维矩阵）。
      → easy-slice 216 PASS/0 FAIL（+3：BooleanDim、enum3、enum8）；oracle=live 逐字节一致；全 scodeinst PASS 216→219、FAIL 69 不变；
      三编译器 10/10、ASan 0；+1 单测。限定/别名枚举维、复合数组 type 维、type-维变量在方程被引用 留后续（现 SKIP）。
- [x] **加宽㊵算法段 for 语句（KEPT loop，increment 1）**（2026-09-22，并行子agent穷举出卡）：承 algorithm CORE。
      for-语句保留不展开（对标 NFFlatten.flattenStatement:2308，与 for-方程展开相反）。inst_exp 补 ExpKind::Range；
      stmt_iters_ 注册符号迭代变量（元素类型，不内联/不加前缀，区别于 loop_vars_ 内联）；cref 符号迭代变量早返回 + 符号下标 x[i] 免越界检查；
      collect_statement 认 SStmtKind::For（collect_for_statement：范围类型化 + 体翻译 + save/restore 嵌套）+ 数组元素赋值目标 x[i]（find_array_flat_var）。
      范围结构保留（2:2 不塌、{1,2,3} 不转 1:5）、/*Real*/(i) 逐元素强制、a.x 前缀但 i 不前缀。
      → easy-slice 221 PASS/0 FAIL（+5：ForStatement1/2/3/Array/Prefix）；oracle=live 逐字节一致；全 scodeinst PASS 219→224、FAIL 69 不变；三编译器 10/10、ASan 0；+1 单测。
      隐式范围（`for i loop`，ImplicitRangeAlg1）留 increment 2；if/while/when/assert/terminate/reinit 语句全是 omc 错误或需正交特性 → 维持 SKIP。
- [x] **加宽㊶复合组件变率下沉 + 常量记录成员折叠**（2026-09-22，并行子agent穷举出卡）：Half A + Half B 耦合（单做 A 会让 CevalRecord1/3/5 翻 FAIL）。
      Half A：ModCtx.var_floor + var_min（OMC variabilityMin = ymc 枚举取序数大者）下沉到叶 emit_variable（var_kind 用有效变率）；
      is_plain_data_record 只放行纯数据记录（成员全标量 Var 非 final 非数组非嵌套 Component）——排除会 FAIL 的 final 字段（RecordBinding6/7）等。
      Half B：cref 变率门改用**根**组件（r1.x 的 r1，非叶字段自身）；记录成员引用 eval_const_record_member（Option A：复用已发射常量叶子的字面量，依赖声明序）。
      → easy-slice 224 PASS/0 FAIL（+3：CevalRecord1/3/5）；oracle=live 逐字节一致；全 scodeinst PASS 224→227、FAIL 69 不变；
      三编译器 10/10、ASan 0；+1 单测（1 桩 SKIP 改成 PASS 断言）。整体记录复制 r2=r1、参数成员折叠、记录数组、Evaluate/fixed 留后续（现 SKIP）。
- [x] **加宽㊷函数发射地基（专用 FlatFunction，increment 1）**（2026-09-23，用户定"专用 FlatFunction"，并行子agent穷举出卡）：
      发射被调用户函数 + 保留符号调用（对标 NFFlatten.collectFunctions / NFFlatModel.appendStream，函数块 class 之前）。
      数据模型：flat.h 加 FlatFuncParam/FlatFunction + FlatModel.functions。dumper function_block（维在类型上、per-line protected、in/out 变率剥离）。
      call() 折不成常量 → try_emit_user_call：解析函数 + emit_function（纯标量 in/out、绑定定义输出、无 protected 局部/算法段）+ 建符号 Call（限定名）。
      func_params_ 令函数体 cref 符号解析成裸名；closure 经递归 inst_exp 自然收集。工具修复 flat_sweep.py:124 认 stdout 以 function 开头。
      守卫（保 FAIL=0）：partial/external/impure/带注解、多输出（元组）、递归（emitting_funcs_）、输出无绑定（omc 告警 FunctionUnitialized*）、
      非标量形参、省略默认实参、命名实参、数组实参 → 一律 SKIP。
      → easy-slice 225 PASS/0 FAIL（+1：FunctionParamVariability）；oracle=live 逐字节一致；全 scodeinst PASS 227→228、FAIL 69 不变；三编译器 10/10、ASan 0；+1 单测。
      protected 局部 + 算法体 + 依赖重排（FunctionCompOrder1）、限定名嵌套函数、默认实参展开、数组/记录/多输出 留后续（现 SKIP）。
- [x] **加宽㊸函数发射 increment 2（protected 局部 + 算法体 + 依赖重排）**（2026-09-23）：承 f3654ce。
      emit_function 收 protected 局部（拓扑排序：绑定引用的局部先出，x1=x2,x2=x → x2,x1）+ 算法段（collect_func_statement：标量赋值，目标须 func_params_ 名）。
      守卫（保 FAIL=0）：只发射顶层函数（I_.scode.program 成员；嵌套函数需限定名 → SKIP FuncInnerParam/FunctionUnitialized*）、
      输出须赋值（绑定或算法 LHS）、函数体引用未解析裸名（time/模型变量 → omc 报错 Time3）→ SKIP。
      → easy-slice 226 PASS/0 FAIL（+1：FunctionCompOrder1）；oracle=live 逐字节一致；全 scodeinst PASS 228→229、FAIL 69 不变；三编译器 10/10、ASan 0；+1 单测。
      嵌套限定名、默认实参展开、数组/记录/多输出、if/for/while 函数体 留后续。
- [x] **加宽㊹数组 connect（flatten.cpp，不相交多 connect + for-connect）**（2026-09-23，用户定"我推 array-connect（flatten.cpp）"）：
      P0 连接器数组标量化：复合数组分支放行普通 Connector（只 SKIP expandable），逐元素 `c[i]` 递归摊成员（标量→变量、flow→flow_vars_ 带下标）。
      P1 resolve_connector 收下标：末段数组连接器下标逐维选标量元素（下标数==维数、常量整型，subscript_int 求值）；ConnInfo/FlowVar/member_cref/conn_key 全部带逐段下标。
      P2 for-connect：collect 时快照 loop_vars_ 存进 ConnectStmt，process_connects 解析该 connect 前还原（`c1[i]` 的 i 在 run() 末尾已清空）。
      P3 不相交多 connect：conn_key（含下标）去重，任一连接器出现 >1 次 → SKIP（共享端点要连接集并查）；逐 connect 交替发射 pot 在前 flow 在后。
      守卫（保 FAIL=0）：整体数组连接 `connect(c1,c2)`（Connect4）、数组成员、宿主段带下标 `m[j].c`、共享端点、非常量下标 → 一律 SKIP。
      → easy-slice 227 PASS/0 FAIL（+1：ForConnect1）；oracle=live 逐字节一致；全 scodeinst PASS 229→230、FAIL 69 不变；三编译器 10/10、ASan 0；+1 单测（多 connect 桩改成共享端点 SKIP + 不相交 PASS 两个）。
      整体数组连接（Connect4：连接器维 + 数组成员双重展开）、共享端点连接集并查（Connect2 传递/ConnectArrays1 切片）留后续（现 SKIP）。
- [x] **加宽㊺数组 connect increment 2（整体数组连接 + 数组成员）**（2026-09-27，用户定"依据 OMC 源码启动"）：承 4bc9a43。
      OMC 链（NFFlatten.mo:2433 resolveConnections）：collectConnections → makeConnectors（ExpandExp.expand + Connector.fromExp，
      NFConnections.mo:209-235）把整体数组实参展成元素列表、makeConnections 左右**按位**配对（:200-206）→ Connections.split
      （Connector.splitImpl 先 potential 后 flow、数组成员保留，NFConnector.mo:344-346/363）→ Connections.scalarize
      （默认 keepSingleConnectedArrays=false → 数组成员全摊，:285-288；Connector.scalarize 行主序，:266-268）→ generateEquations。
      三探针实跑 1.25.0 定序：3×2（元素→pot→flow→成员元素）、2-D 整体连接行主序 c[1,1],c[1,2],c[2,1],c[2,2]、元素连接+2-D 成员。
      ymc：resolve_connector 改返元素列表（整体数组 → 行主序展开，decl_cls 作用域求维）；ConnMember 带 dims；member_cref/flow_sum
      加成员元素下标；process_connects 按位配对（元素数不等 → SKIP）+ 成员 do/next_index 逐元素（dims 不等 → SKIP）。
      emit_array 补连接器守卫（stream/方向/连接器外 flow，与 emit_variable 同）+ flow 元素攒 flow_vars_（末段下标放 subs 末槽）——
      此前连接器里 `flow Real f[2]` 走 emit_array 根本不进 flow_vars_（无 `f[k]=0`），是本增量补的洞。
      → easy-slice 228 PASS/0 FAIL（+1：Connect4）；oracle=live 逐字节一致；全 scodeinst PASS 230→231、FAIL 69 不变、SKIP 1026→1025
      （守卫未翻转任何 PASS）；三编译器 10/10、ASan 0；+1 单测。共享端点连接集并查（Connect2 传递 / ConnectArrays1 切片）、
      部分下标/切片实参、inside 数组连接器的 connected_inside_ 仍按 conn_key（已含下标）留后续验证。
- [x] **加宽㊻connect 成员序（潜在 bug）+ 嵌套连接器 + 条件成员**（2026-09-27，"依据 OMC 源码启动"）：承 a94b3ff。
      先把连接集顺序从源码推全并用 12 探针逐字节验证，写成 docs/research/connect-sets-order.md（DisjointSets 插入序/union 秩规则/
      extractSets 根序/generateEquations 前插/List.mapFlat 块内反转；共享端点做并查集时照此复刻）。数据：testsuite 目前**没有**
      被共享端点卡住的用例，卡住的 5 个合法 connect 用例要的是嵌套连接器/条件成员/短类连接器+切片。
      **潜在 bug（探针 YmcOrder.Flat 揭露）**：连接方程成员序不是声明序而是每组**按名升序**（makeConnectorType 遍历
      ClassTree.enumerateComponents = 按名 LookupTree 折叠，NFTyping.mo:339-354 / NFClassTree.mo:1418-1444；cons/reverse 链净效果升序）；
      既有 PASS 用例成员恰好按序声明所以没暴露。变量行与 flow 边界 =0 仍按声明序。
      ymc：connector_leaves（对标 Connector.split → splitImpl 递归，NFConnector.mo:328-393）：每层 potential 升序（子连接器就地展开）→
      flow 升序，ConnMember.path 多段；member_cref/flow_sum 收成员路径；配对按路径；条件为假成员（任一侧）整对丢弃
      （Connection.split，NFConnection.mo:68，member_removed 逐段前缀查 removed_）；不相交判定降到**标量叶**（leaf_key）——
      Condition6 三条 connect 共享连接器 c2 但叶不相交；connected_inside_ 改记已配对的 inside flow 叶（flow=0 发射处按叶精确查，
      不再按连接器路径/前缀猜）。emit 序按叶表顺序直接发（不再全局 pot/flow 分桶——嵌套时是每层分组，ConnectDiffOrder2）。
      → easy-slice 231 PASS/0 FAIL（+3：Condition6/Condition9/ConnectDiffOrder2）；oracle=live 逐字节一致；全 scodeinst PASS 231→234、
      FAIL 69 不变、SKIP 1025→1022；三编译器 10/10、ASan 0；+4 单测（升序/嵌套/条件成员/全移除空类，全部 omc 1.25.0 实跑来源）。
      留后续：短类连接器 `connector InReal = input Real` + 切片实参（ConnectArrays2/3）、记录成员（splitImpl :354-360）、
      共享端点并查集（cardinality/stream 前置）。
- [x] **加宽㊼短类连接器 + 任意段下标/切片实参**（2026-09-27）：承 adeda22。
      短类连接器 `connector InReal = input Real`：组件本身即唯一标量叶（splitImpl 非 COMPLEX 分支，NFConnector.mo:366）；
      class_inst 已塌到内建（inst.cpp:448），故按声明类型名在声明类作用域链找元素（find_class_elem，同 find_enum_def 走法），
      须 restriction=connector 的 Derived 短类（`type T = input Real` 非连接器 → nullopt）。
      元素展开改成 ExpandExp.expandCref 语义：每段（含宿主段 `b1[1:3].x[1:3]`）下标按 Subscript.expandList 展开——整数/迭代变量、
      范围/数组按自身顺序（eval_range，含 a:step:b）、缺省后续维取全部（NFSubscript.mo:1081-1103）；各段各维行主序求积、首段最外
      （expandCref3/4，NFExpandExp.mo:208-255）；越界/非常量下标 → SKIP（ConnectArraysInvalidSubscript1 保持 SKIP）。
      探针实跑 1.25.0：YmcSlice.Partial（c1[1]→c1[1,1],c1[1,2]）/Desc（3:-1:1 按范围序配对）/Step（1:2:4）逐字节。
      **分歧账本 +2**：InvalidConnectorDirection1/2（status incorrect，文件期望 `Cannot connect input component … to non-input …`）
      ——该错误只在旧前端 InstSection.mo:4012 抛，1.25.0 接受并产出，ymc 与 1.25.0 逐字节一致 → 记入 docs/open-questions.md，
      sweep 按文件记 FAIL：**全 scodeinst FAIL 基线 69→71（无真 FAIL；账本内 FinalParameter1/2/3、IfExpression17、
      OperationRelational1 同样计在 FAIL 里）**。
      → easy-slice 233 PASS/0 FAIL（+2：ConnectArrays2/ConnectArrays3）；oracle=live 逐字节一致；全 scodeinst PASS 234→236、
      SKIP 1022→1018；三编译器 10/10、ASan 0；+2 单测（短类+切片、降序范围）。
      留后续：短类 flow 连接器在模型层（`connector F = flow Real; F f;` → emit_* 的 in_conn 判定会 SKIP "flow outside connector"）、
      参数下标 `c[p]`（subscript_int 只认整数/迭代变量）、记录成员、共享端点并查集。
- [x] **加宽㊽方向保留规则 + 连接器里的记录成员（flow 下沉）**（2026-09-27）：承 e82bc45。
      方向：`Variable.removeNonTopLevelDirection`（NFVariable.mo:466-493）——所有前缀组件都是连接器或 input 组件才保留
      input/output（顶层变量、顶层连接器成员 `c.e`、顶层 input 组件成员），否则抹掉；ModCtx 加 keep_dir 沿复合递归下沉
      （两处 flatten_class 递归点），emit_variable/emit_array 改用它，去掉 "connector direction" SKIP 守卫（Direction1）。
      记录成员：连接器里 `R e` / `flow R f`——ModCtx 加 in_conn（连接器内部，含记录字段）与 flow_inherited（`flow R f` 字段
      全部继承 flow，ConnectorType.merge，NFAttributes.mo:229-234），emit_* 的 in_conn 不再靠 enclosing_is_connector；
      connector_leaves 收记录成员 → record_leaves 按字段**声明序**拆叶（splitImpl 记录分支 ClassTree.getComponents = 数组，
      NFConnector.mo:354-360；YmcRecConn 探针 r2,r1——与连接器成员按名排序不同）。`flow C c`（conn9，omc 另有不平衡告警行）仍 SKIP。
      **分歧账本 +2**：InvalidConnectorDirection3/4（同 1/2：方向不匹配错误只在旧前端查，1.25.0 接受），ymc 与 1.25.0
      逐字节一致 → FAIL 基线 71→73。
      → easy-slice 235 PASS/0 FAIL（+2：Direction1 由 SKIP 翻 PASS、ConnectComplex1 新入）；oracle=live 逐字节一致；
      全 scodeinst PASS 236→238、SKIP 1018→1014；三编译器 10/10、ASan 0；+3 单测（方向规则、未连接 flow 记录、记录成员相连）。
      留后续：记录字段自带 flow/stream 前缀、复合数组上的 flow 前缀、`flow C c` 子连接器（需告警文本）、cardinality
      （CardinalityTable + if/assert 常量折叠）、expandable、stream。
- [x] **加宽㊾连接集忠实实现（DisjointSets 序）+ 连接方程前置 + 子模型内 connect**（2026-09-27，S1–S3；S4 cardinality 另开）：
      背景：两处已确认潜在偏差（docs/research/connect-sets-order.md 补充节）：(1) 集合输出序 = 最终根节点下标反序，
      "不相交简化"在 inside↔outside 且 outside 先声明时出错（探针 OrderTrap）；(2) 连接方程须置于模型自身方程之前
      （NFFlatten.mo:2497，探针 EqPlace）。
      - [x] S1 连接方程前置：process_connects 的全部产出 insert 到 model.equations 头部（initial 段不动）。
      - [x] S2 DisjointSets（process_connects 重写）：节点 = 标量叶 × 面（key 加 `#i/#o`，Connector.isEqual 含 face）；
            先按 flow_vars_ 反序预插全部 flow 叶（INSIDE），再按配对自然序**整体反转**合并（cons + List.mapFlat 块内反转
            的净效果），每对 lhs 先 rhs 后；union 按 Util/DisjointSets.mo:256-287 的**代码**（注释相反）；extractSets 根按
            节点序编号、集合内为插入序；输出集合号反序：potential head=其余，flow 单元素 `=0.0`、多元素
            `in… - out… / -(out…)`。旧的 flow=0 发射循环、connected_inside_、flow_sum、leaf_key 去重 SKIP 全部删除；
            共享端点（Connect2 传递、星形）自然支持。
      - [x] S3 子模型内 connect：ConnectStmt 补 psubs；面在书写类判定（resolve_connector 本地路径长度==1 → outside，
            对标 firstNonScope），随后前缀 ++ 本地路径（复合数组元素带下标）；references_removed 也按前缀全路径查。
      → 16 个独立探针（12 集合序 + OrderTrap/EqPlace + SubConn/ArrSub）逐字节；easy-slice 235 PASS/0 FAIL **不变**、
        全 scodeinst 238/73/1014 **不变**（既有输出全部不动，只修潜在偏差）；oracle=live 逐字节一致；三编译器 10/10、
        ASan 0；单测 −1（共享端点 SKIP 桩）+6（链式共享端点、OrderTrap、EqPlace、子模型面、数组元素前缀、四元混合面求和）。
- [x] **加宽㊿cardinality（㊾ 的 S4）+ 方向前缀冲突守卫**（2026-09-27）：承 990bf78。
      cardinality：call() 拦截，实参经 resolve_connector 须恰好 1 个标量连接器元素（类名/数组 → SKIP，CardinalityInvalidArg1/2），
      且只允许在 if 方程/assert 的条件里（card_allowed_，其余 SKIP，CardinalityInvalidContext1/2）；保持符号 Call（实参 =
      前缀化连接器 cref）。process_connects 在 split 前按连接器元素建计数表（CardinalityTable.fromConnections，
      NFCardinalityTable.mo:54-92）；随后 evaluate_cardinality 就地替换成 IntLit（evaluateCardinality :94-103，缺省 0），
      只重化简被替换的表达式，再按 NFSimplifyModel 折叠：assert(true) 删（:215-219）、if 真分支首个则内联/否则成 else、
      假分支丢弃（simplifyIfEqBranches :510-573）。find_class_elem 补查顶层 program（Cardinality3 顶层短类连接器）。
      方向前缀冲突守卫：新暴露 conn8（`connector C = input Real; output C c3`）→ omc 报错 Prefixes.mergeDirection
      （NFPrefixes.mo:588-604，派生链 allowSame）；conflicting_direction 沿短类链累积方向、不同即 SKIP。
      顺带把 ClassAttributes4 / type4 / type5（status incorrect，1.25.0 实跑同样报错）从真 FAIL 变 SKIP。
      正确归宿是 inst.cpp 短类塌陷的属性合并（:436-438）——留给用户接手时移入（任务卡：mergeDirection 语义 + 这四个用例）。
      → easy-slice 239 PASS/0 FAIL（+4：Cardinality1 由 SKIP 翻 PASS、Cardinality2/3/4 新入）；oracle=live 逐字节一致；
      全 scodeinst PASS 238→242、**FAIL 73→70**（conn8 先冒出后守住；ClassAttributes4/type4/type5 −3 真 FAIL）、SKIP 1014→1013；
      三编译器 10/10、ASan 0；+6 单测（cardinality ×5 含上下文守卫、方向冲突守卫 ×1）。
- [ ] **加宽(51)stream 连接器**（2026-09-27 规划；用户定"上 stream 连接器，依据 OMC 源码"）：
      源：NFConnectEquations.mo generateStreamEquations :423-474 / streamEquationGeneral :476-497 / streamSumEquationExp
      :499-551（sumMap 前插）/ makePositiveMaxCall :673-711（阈值 = FLOW_THRESHOLD×nominal）/ makeInStreamDivCall :713-729 /
      evaluateInStream :814-841 + generateInStreamExp :843-900（按 **INSIDE 面**查集合；isNoFlowMinMax 精简）/
      evaluateActualStream :978-1016 + evaluateFlowDirection :1044-1076 / associatedFlowCref :1106-1126。
      - [x] A：stream 叶（splitImpl :346，flows 之后按名升序，同层唯一 flow）进 DisjointSets 自成集合；stream 集方程；
            inStream/actualStream 先留符号（args = stream cref + 关联 flow cref），连接解析后按集合求值（字面量 min/max/nominal）。
            守卫：非 stream / 非 cref 实参、非字面量下标、连接器 flow 数 ≠ 1、flow*actualStream（smooth 包裹）、算法段里的
            stream 算子 → SKIP。目标：InStreamUnconnected/TwoInside/TwoOutside/InsideOutside、ExtendConnector1、ActualStream、
            ActualStreamMinMax1、ConnectStream1、InStreamNominalThreshold、InStreamPipeline（+10）。
            **完成（2026-09-27）**：连接集节点表改为成员 cs_*（SetNode 带 is_stream / name_key / 关联 flow cref+key）；
            emit_stream_set 按 generateStreamEquations/streamEquationGeneral；eval_instream 按 INSIDE 面查集合 + isNoFlowMinMax
            精简（查询者本身按名保留）；stream_sum 复刻 streamSumEquationExp（sumMap 前插、PositiveMax 阈值 1e-7×nominal、
            inStreamDiv 包裹）；eval_actualstream 按 evaluateFlowDirection。10 例首轮即逐字节；9 个 incorrect 全 SKIP。
            → easy-slice 249 PASS/0 FAIL（+10）；全 scodeinst PASS 242→252、FAIL 70 不变、SKIP 1013→1003；
            oracle=live 逐字节一致；三编译器 10/10、ASan 0；+5 单测。
      - [x] B：参数值属性/下标的结构化 final 副作用（isNoFlow 的 Structural.markExp）、if 同分支化简
            （ActualStreamMinMax2、ActualStreamVariability2）。**完成（2026-09-27）**：simplifyIf 三条规则全局移植
            （NFSimplifyExp.mo:1311-1345：两支相等 → 该支；true/false → cond；false/true → not cond，logicNegate；
            新增 expr_equal = Expression.isEqual）；isNoFlow 对参数值属性非破坏性求值并记结构参数（evaluateFlowDirection 仍见
            未折叠原式 → 不定）；stream 算子实参改自走段解析，本地参数下标 `c[n]` 取值并记结构参数；apply_structural_params
            统一 final + 属性折叠，结构参数在他处（绑定/方程/算法）被引用 → SKIP（全局 EvalConstants 未做）。
            → easy-slice 251 PASS/0 FAIL（+2）；全 scodeinst PASS 252→254、FAIL 70 不变、SKIP 1003→1001（simplifyIf 全局化
            未改动任何其它用例）；三编译器 10/10、ASan 0；+3 单测（含 IfSimp 探针）。
      - [ ] InStreamReduction1：`sum(inStream(c[i].s) for i in 1:n)` 需归约符号展开（evaluateOperatorReductionExp →
            ExpandExp.expandArrayConstructor，:750-791；sum 数组 → 加法链），属归约特性，另开。
      - [ ] 潜在偏差（本桶探针发现）：属性里的 if 表达式整数分支 ymc 打 `/*Real*/(if c then 0 else -1)`，omc 逐分支
            提升为 `if c then 0.0 else -1.0`（matchTypes 对 IF 分支逐支 cast）——当前无用例触发，属性 Real 化应改用逐分支 coerce。
- [ ] **加宽(52)expandable 连接器**（2026-09-27 规划；用户定"上 expandable，依据 OMC 源码，然后自动往下执行"）：
      源：NFExpandableConnectors.mo elaborate :71-122（sortConnections :126-171 → 独立 DisjointSets（全 OUTSIDE 面）：
      expandable↔expandable + 嵌套同名 expandable 成员（:173-237）、未声明元素↔其父 expandable（:239-282）→ 每集并集
      （按末段名去重）：已声明者 markComponentPresent（:334-355，连子孙），未声明者建虚变量（createVirtualVariables :431-455：
      前插、复合类型按声明序递归、注释 "virtual variable in expandable connector"、AUGMENTED）→ 连接表重排 expandable/
      undeclared/normal（:113-115）→ NFFlatten.mo:2464 删掉仍 potentially present 的变量 → collectFlows（虚 flow 另加 OUTSIDE，
      NFConnections.mo:158-163）。NFAttributes.mo:412-416：expandable 直属成员标 POTENTIALLY_PRESENT、禁 flow/stream；
      splitImpl2 :388 跳过未 present 成员；checkConnectorForm（NFTyping.mo:3215-3229）除首段外须全是连接器（pp 成员算）。
      - [x] A：标量/复合虚元素、已声明 pp 成员、expandable↔expandable（含嵌套同名）、未连接 pp 删除、前插序、连接重排、
            虚 flow 的 IN+OUT、连接器链 `c1.c2…` 面规则。目标：ExpandableConnector1/2/3/4/5/6/7/8/10/11/12/14、Complex1、Flow2、
            Present1/2、expconn4（+ expconn1/2/3/5：文件无 Result（FAILREASON）、1.25.0 能出模型 → 对齐 1.25.0 记账本）。
            守卫：flow 直属 expandable（Flow1）、两侧都未声明（NonDecl1/expconn6/7）、expandable↔非 expandable（Invalid…1）、
            虚元素下再虚（NonDecl3）、类名实参（NonDecl4）、虚元素为数组 → SKIP。
            **完成（2026-09-28）**：flatten 期按 ModCtx::pp_chain 记每个 pp 变量的节点链；resolve_connector 泛化为逐段走
            （除首段外皆须连接器 = checkConnectorForm，面 = crefFace；端种类 Normal/Expandable/Declared/Virtual）；
            elaborate_expandable 复刻 sortConnections → 独立 DisjointSets 关联图（嵌套同名 expandable 升序递归）→ 逐集增广
            （exp_set = UnorderedSet 桶序，stringHashDjb2 按 **32 位 long**（Windows 1.25.0）+ 13 桶/rehash；markComponentPresent
            按**节点**，数组元素共用）→ 虚变量按 complexComponents 声明序逐个前插 → 连接重排 [普通, 未声明, expandable] →
            删未 present 变量、重建 flow 表（虚 flow 先 OUTSIDE 后 INSIDE 预插入）。探针（均与 1.25.0 逐字节）：YmcExpHash1/2
            （桶序 / 32 位）、YmcExpRehash1（15 名 rehash）、YmcExpNode1（按节点标记）、YmcExpTwoSets1（根序编号）、
            YmcExpElem1（数组元素作类型源）、YmcExpNestedVirt1、YmcExpDeclOther1、YmcConnChain1（`c1.c2` 链；omc 另有连接器
            不平衡告警，ymc 无告警）。ExpandableConnector13（原列 B）无需数组，本增量即过；ExpandableConnector5 卡在旧的
            "顶层方向数组带绑定"守卫（与 expandable 无关，另开）；ExpandableConnector9/14 属 B。
            → easy-slice 267 PASS/0 FAIL（+16）；全 scodeinst PASS 254→270、FAIL 70→73（ExpandableConnector12 −1、expconn1/2/3/5
            +4 入账本：文件无 Result、ymc 与 1.25.0 逐字节）、SKIP 1001→982；oracle=live 不一致 0；三编译器 10/10、ASan 0
            （另以 ASan 版 ymc 跑 40 个 expandable 用例/探针：0 报告、输出与 gcc 版一致）；+8 单测。
      - [x] B：expandable 数组（ExpandableConnector9/14）。**完成（2026-09-28）**：放开复合数组 / 连接端的 expandable 数组
            守卫（逐元素同标量 expandable，下标进虚变量名与关联图 key）；增广类型按节点"最后一次生效"（:428，数组元素共用节点），
            去掉原先"各次名表不同 → SKIP"的保守守卫——omc 照样按最后一次拆 E–E，输出引用未建的虚变量（YmcExpArr3 探针
            `ec[2].x = ec2[2].x`），ymc 逐字节复刻；YmcExpArr1/2（各元素虚成员不同）亦一致。
            → easy-slice 269 PASS/0 FAIL（+2）；全 scodeinst PASS 270→272、FAIL 73 不变、SKIP 982→980；oracle=live 不一致 0；
            +1 单测。
      - [ ] 余项（未做，均 SKIP）：数组型虚元素（`connect(ec.x, y)` 整体数组 / 切片另一端，omc 保持未展开的数组连接）、
            pp 数组成员作连接端、嵌套 expandable 数组的同名配对（addNestedExpandableConnectorsToSets 对数组型成员）、
            虚元素类型含数组 / stream / 非 Real 叶、经 pp 连接器下探的未声明端（`ec.c.e`，父被强标 EXPANDABLE）。
      - [ ] ExpandableConnector5：`RealOutput y[2] = zeros(size(y, 1))` 顶层方向数组带绑定 → omc 保留 `output` 并把绑定
            改成方程 `y = {0.0, 0.0}`；现守卫 "top-level directional array with binding"（TopLevelInputs2 形态不同）需细分。
- [ ] **加宽(53)connect 收尾：参数 / 常量连接器**（2026-09-28 规划；expandable 完成后按"自动往下执行"自选：connect 区
      剩余的有效用例里唯一自洽的一块，其余卡在 inst 域 short-class aliasing / 数组元素引用 / 归约）：
      源：NFConnectEquations.generatePotentialEquations :202-236（集首 `Connector.variability(c1) > PARAMETER` → 相等方程，
      否则逐个 makeEqualityAssert :290-341：Real → `assert(abs(a - b) <= 0.0, "Connected constants/parameters must be equal")`，
      其它类型 → `assert(a == b, …)`，数组 → for 包裹）；常量由 EvalConstants 折叠（ConstantConnector2 → `assert(false, …)`）。
      - [x] 放开连接器叶的 parameter/constant 标量 potential 成员（connector_leaves / 短类连接器 `connector C = parameter Real`）；
            集首变率 ≤ parameter → assert；常量绑定字面量代入后化简。目标：ConstantConnector1/2/3。
            守卫：非 Var 的 flow/stream、数组成员（for 包裹）、集内变率混杂 → SKIP（探针定）。
            **完成（2026-09-28）**：探针定下的语义——变率只看集首（YmcCCMix1：parameter 集首连连续变量也出断言，不 SKIP）；
            非 Real 用 `==`（YmcCCInt1）；constant 代入字面量后化简，`assert(true, …)` 删（YmcCCEq1/YmcCCConst3）；连接已逐元素
            标量化故无 for 包裹。集节点 name_key 与 var_key 同编码 → 发射时直接查变量变率/绑定（set_var），ConnMember 不加字段。
            放开后暴露既有缺口：omc 对无绑定、fixed 且无 start 的 parameter 报 UNBOUND_PARAMETER_ERROR（NFFlatten.verifyBinding
            :812-875），ymc 原先照印 → 在 emit_variable / emit_array 补上（→ SKIP），根治 UnboundParameter1（FAIL→SKIP）并挡住
            InvalidComplexConnectorType1/2、InvalidVariability1/2。源码 HEAD 的 constant 无绑定报错 1.25.0 不报（YmcUnbConst1）→ 不查，
            记 open-questions。
            → easy-slice 272 PASS/0 FAIL（+3）；全 scodeinst PASS 272→275、**FAIL 73→72**、SKIP 980→978；oracle=live 不一致 0；
            +3 单测。
- [ ] **加宽(54)常量数组求值（Ceval 数组）**（2026-09-28 规划；按 SKIP 直方图自选：有效用例里 "non-literal array dimension"
      50 例居首，其中约半数是 `constant T x[:] = <数组常量表达式>`）：
      源：NFCeval.evalBinaryAdd/Sub/Mul/Div/Pow :1010-1307、ScalarArray/ArrayScalar :1309-1351、MulVectorMatrix /
      MulMatrixVector / ScalarProduct / MatrixProduct :1353-1484、PowMatrix :1486-1540、evalUnaryMinus :1559-1580、
      evalRange/evalRangeExp/evalRangeReal :815-938、evalBuiltinZeros/Ones/Fill/Identity（:3102/2745/2431/2458）。
      常量值与修饰：ymc 的修饰在 flatten 期（ModCtx）才落到变量上 → 已发射常量的**最终值**才是修饰后的值（CevalBinding2
      `A a(x = 1.0)` 里 `z = x + y` 现 SKIP 即因此）。
      - [x] A：已发射常量值表 const_vals_（var_key → CVal，数组存整体 + 逐元素）；cref() 的 constant 分支与 eval_scode 的 cref
            先查它（前缀 + 段下标，修饰感知）；eval_scode 加数组二元（逐元素 / 标量广播 / 向量·向量 / 矩阵·向量 / 向量·矩阵 /
            矩阵·矩阵 / 矩阵幂）、数组取负、String `+`、范围（Integer / Real / Boolean）、zeros/ones/fill/identity；
            emit_array 的 constant 绑定改走值求值（`:` 维取值形状、逐元素字面量、空数组整体不出）；标量 constant 绑定 inst_exp
            折不成字面量时回退值求值。目标：CevalAdd1/Sub1/Mul1、RealRangeBinding1、CevalMatrixVectorMul1/VectorMatrixMul1/
            MatrixPow1/MatrixProduct1/VectorProduct1、CevalZeros1/Ones1/Fill1/Identity1、CevalBinding2。
            **完成（2026-09-28）**：CVal 加 eshape（空数组保全形状，`ones(0, 3)` 转置 / 矩阵积要用）；cval_binary 按 Absyn 算子
            + 值维数分派（evalBinaryOp_dispatch）；范围复刻 evalRangeReal 的首尾原值（`1.0:0.3:2.0` → …, 1.6, 2.0，P54c 探针实证
            omc 的这一怪癖）；constant 标量改为先值求值、失败再走类型化路径；函数体求值期关掉书写处前缀。全部 15 个目标首轮即与
            1.25.0 逐字节，另白捡 CevalBinding3（数组元素的修饰分发）、CevalFuncArray1、Size4。
            → easy-slice 289 PASS/0 FAIL（+17）；全 scodeinst PASS 275→292、FAIL 72 不变、SKIP 978→961；oracle=live 不一致 0；
            三编译器 10/10、ASan 0（ASan 版 ymc 跑 148 个 Ceval/Range/Size 文件：0 报告、与 gcc 版输出一致）；+3 单测。
      - [x] B：transpose/cross/skew/vector/linspace/diagonal/scalar/der(常量) 等内建（CevalTranspose1/Cross1/Skew1/Vector1/
            Linspace1/Diagonal1/Scalar1/Der2）。**完成（2026-09-28）**：NFCeval 专用求值器（transpose 换前两维、vector 摊平、
            diagonal 零取元素类型、scalar 逐层取唯一元素、der → 同形 Real 0、skew）+ Modelica 层内建按函数体（cross、linspace，
            NFModelicaBuiltin.mo:353-362 / :305-316）；cval_transpose 推广到维数 ≥ 2。8 例首轮即逐字节。
            → easy-slice 297 PASS/0 FAIL（+8）；全 scodeinst PASS 292→300、FAIL 72 不变、SKIP 961→953；oracle=live 不一致 0；
            +1 单测（P54d 探针）。
      - [x] C：数组推导式 `{i-j for i in …}`（CevalArrayConstructor2）、返回数组的用户函数（CevalFuncArray*/FuncSubscript*）。
            **完成（2026-09-28）**：推导式按 MLS §10.4.2.1（最右迭代器最外维，evalArrayConstructor2）；函数解释器补 Undef
            占位（未赋值数组输出 / 局部，NFEvalFunction.buildBinding :342-365）、输出维依赖输入、protected 局部初始化、下标 / 切片
            读写（写时复制）、数组逐元素强转；标量内建数学函数进值求值（复用 simplify_expr 的折叠 + 数组向量化）。
            CevalFuncArray6 需 `annotation(Evaluate=true)` 参数求值（另一特性）→ 仍 SKIP。
            → easy-slice 305 PASS/0 FAIL（+8，含白捡 CevalArrayConstructor1）；全 scodeinst PASS 300→308、FAIL 72 不变、
            SKIP 953→945；oracle=live 不一致 0；+1 单测（P54e 探针）。
      - [ ] 余项：`annotation(Evaluate=true)` 参数（CevalFuncArray6）；常量数组整取引用进类型化路径（CevalArrayConstant2
            `{1.0, 2.0, 3.0}[n]`）；记录数组常量（CevalRecordArray*，卡在 "variability on composite array"）。
- [ ] **加宽(55)用户函数**（2026-09-28 规划；用户定"上用户函数，依据 OMC 源码"）：有效用例里 function 相关 SKIP 约 60 例
      （"function: <名>" 中的用户函数 + qualified function call / tuple / named argument 等）。源：NFEvalFunction（求值：
      createArgumentMap :250-295、buildBinding :342-365、buildRecordBinding :386-434）、NFFunction（签名 / 实参匹配 /
      默认实参填充）、DAEDump.dumpFunctionList :2724-2745（按全名 stringCompare 排序、隐藏 builtin 等）+ DAEDumpTpl
      dumpFunction :102-135（`impure `、`external "C" …;`、记录构造函数 "Automatically generated record constructor for R"）。
      1.25.0 与文件分歧：ImplicitTrailingSubscript1/2（1.25.0 不补尾随 `:`）、CevalBinding10（1.25.0 报错）→ 以 1.25.0 为准。
      - [x] A 折叠补全：函数解析（包限定名 `P.f`、短类别名 `function f = P.f` 链、函数体按**定义处**作用域）；函数 extends
            （成员就地展开、同名去重、基类算法在前）；if 表达式求值（递归 fac）；记录（构造调用 `R(2)`、protected 记录局部
            `R r(x = x)` + 字段默认值互引 `y = x`、记录输入缺省取字段默认）；多输出取首输出；隐式 for 范围（`for i loop`
            由 `x[i]` 推 1:size(x, k)）；模型内嵌函数的函数体可见调用实例的常量（func1）；常量叶经模型实例引用内联（`a1.w`）。
            目标：CevalFuncRecursive1、FunctionRecursive2、FuncExtends、FunctionExtends1、FuncDuplicateParams1、ExtendsShort2、
            CevalFuncRecord7、FunctionRecordArg2、RecordBinding8、FuncDefaultArg4、FunctionMultiOutput3、ImplicitRangeAlg2、func1。
            **完成（2026-09-28）**：resolve_fn（类路径逐段下降 + 别名链，函数体作用域 = 定义处）、collect_fn_sig（extends 就地
            展开 / 同名去重 / 链上算法段至多一个，YmcFnExtOrder / YmcFnExtAlg 探针）、record_value（buildRecordBinding：给定 >
            修饰 > 字段默认，函数变量缺值填 Undef）、输出与局部按依赖就绪反复初始化（createArgumentMap 的代换语义）、多输出取首、
            隐式范围（deduceIterationRange）、if 表达式；typed 路 call() 只接标量折叠值（原先数组 / 记录值会被 literal_of 退成 0）。
            13 个目标首轮逐字节，另白捡 TupleOperation3；中途 CevalFuncRecord2 因 `output R r` 无默认一度回退（缺值须填 EMPTY 而
            非失败）已修。RecordBinding18 由 SKIP 变"FAIL"：ymc 与 1.25.0 stdout 逐字节（折叠成 0.785…），文件是较新 OMC 的符号
            结果 → 入账本。
            → easy-slice 319 PASS/0 FAIL（+14）；全 scodeinst PASS 308→322、FAIL 72→73（RecordBinding18 入账本）、SKIP 945→930；
            oracle=live 不一致 0；+3 单测。
      - [x] B-i 发射主干：函数按全名排序；压平名；默认实参填进调用；数组形参（维打在类型上）；protected parameter 局部；
            多输出函数在表达式里 `f(x)[1]`；external 函数。
            **完成（2026-09-28）**：
            · 压平名（lookupFunction :323-350 + scopeList）：顶层 / 包 → 类路径（`P.g`、`P2.g2`）；调用实例链上某实例的类里
              找到 → 根名 + 该层前缀（`R.a.h`、`R.b.a.h`、继承的 `R.a2.h`）；根类 → `R.f`。YmcFnName1/2/3 探针逐字节。
            · 实参（fillArgs / fillDefaultSlot2 / evaluateSlotCref）：位置 + 命名入槽；缺省 = 输入绑定里引用别的输入处代换成实参
              （`f(a)` → `f(a, 2.0 * a, 3)`）；别名修饰 `function g = f(r = x)` 的默认按模型语境（`g(1.0, x)`）；按形参类型
              强转（`g(/*Real*/(n))`）。YmcFnEmit1 探针逐字节。
            · 类型化路径只折常量实参的调用（参数实参不求值：YmcFnPar1 `g(p)` 保留，此前 ymc 错折成 2.0）。
            · 函数体（NFInstContext.FUNCTION）：形参 / 局部 = 裸名 cref、包常量内联、局部常量保留（YmcFnEmit1）；语句
              assign / if / for / while / return / break（循环外 break → SKIP，Break2）/ assert；局部拓扑序（sortLocals 的
              FIFO Kahn）；用前未赋值 / 输出未赋值告警（checkUseBeforeAssign）→ SKIP；纯函数调 impure → 告警 → SKIP（ImpureCall1）。
            · external：纯度按 makeAttributes :2626-2633（自带 external 段 = impure，继承来的不算）；显式调用照印、隐式调用按
              makeDefaultExternalCall（名 = external 段所在类、数组后跟各维 size）。size(a, k)（typeSize：已知维 → 字面量）。
            · collectFunctions：只留最终模型用到的函数（被结构参数求值掉的调用不留块，ceval4）。
            · `annotation(Evaluate = true)` 参数未做 → SKIP（CevalBinding10 账本要求保持 SKIP；CevalRecord6 由错出 FAIL → SKIP）。
            · 工具：flat_sweep 的 oracle-live 把 `impure function` 开头的 stdout 误判成 omc 报错 → 修正。
            · ASan 抓到一处 arena 悬垂引用（赋值目标类型按引用传进会 add 的 fn_exp）→ 改按值；gcc 未暴露。
            → easy-slice 335 PASS / 0 FAIL（+16）；全 scodeinst PASS 322→338、FAIL 73→72；oracle=live 不一致 0；
            gcc / msvc / msvc-asan 10/10、零警告；ASan 版 ymc 跑全部 1335 个文件 + 探针：仅 inst7 既有断言（已另开任务）；+4 单测。
      - [x] B-ii 函数体里的数组 + 模型语境的数组调用。**完成（2026-09-28）**：
            · 函数语境（符号形态、不标量化）：数组字面量按形状给类型；`*` 两侧皆数组 = 向量 / 矩阵积（checkBinaryOperationMul
              :1019-1093，未知 / 表达式维与任何维相等）；逐元素运算同秩相容维；Integer 运算数按 matchExpressions 提实型。
            · 内建：`scalar(a)` 展开数组字面量（typeScalarCall :1036-1085）；`fill` / `zeros` / `ones` 打成 fill（typeFillCall2，
              Real 目标把强转推进填充值，NFCall.typeCast :1361-1367）；sum / product / min / max(数组) 符号保留。
            · 模型语境：返回数组的调用只许是整个绑定 / 等式一侧（NFScalarize :343-346 整体保留 `x = f(n)`；嵌在运算里须逐元素
              展开，未做 → SKIP——此前 `f(n) * {1, 1}` 会错出）；调用处输出维按 evaluateCallTypeDim 只代换输入，求不出留未知；
              常量数组调用求值成字面量（FuncUnknownDim2）。
            · YmcFnArr2 探针逐字节；数组推导式在函数里（omc 打 `array(e for i in r)`）未做。
            → easy-slice 340 PASS / 0 FAIL（+5）；全 scodeinst PASS 338→343、FAIL 72→74（ImplicitTrailingSubscript1/2：ymc 与
            1.25.0 逐字节、文件是较新 OMC 的补尾随 `:` 形态，账本早有预记，已改为"已按 1.25.0"）；oracle=live 不一致 0；
            gcc / msvc / msvc-asan 10/10、零警告；ASan 版 ymc 跑 1341 个文件 + 探针 0 报告；+2 单测。
            未做（另列）：DimUnknown16（结构参数要编译期调外部 C 函数 strlen）。
      - [x] C-i 函数里的记录。**完成（2026-09-28）**：记录类型的形参 / 局部按类型名打印（`input R r`），构造函数块随之发射
            （Record.instDefaultConstructor + DAEDumpTpl RECORD_CONSTRUCTOR：字段逐个 `input <类型> <名>[ = 默认];`，1.25.0 连
            constant 字段也打 input，`output <末段名> res;`，YmcRec2 探针）；collectFunctions 按可达函数的形参类型收构造函数；
            记录局部的字段修饰不打印、只进排序依赖（FunctionCompOrder4 `lx` 先于 `r`）；函数体里 `r.x` 字段引用；调用处记录
            实参须同一记录类的整体引用。
            → easy-slice 342 PASS / 0 FAIL（+2）；全 scodeinst PASS 343→345、FAIL 74（不变，均入账本）；oracle=live 不一致 0；
            gcc / msvc / msvc-asan 10/10、零警告；ASan 版 ymc 跑 1343 个文件 + 探针：仅 inst7 既有断言（中止路径里输入法 DLL
            的报告，已另开任务）；+1 单测。
      - [ ] C-ii 模型语境的记录等式 / 记录值调用：`r1 = R2(1.0, 2.0)` → `r1 = R1(2.0, 1.0)`（NFTypeCheck.matchComplexTypes
            :1835-1935 按字段名重排 + typeCastRecord；R2 的构造函数为何仍被收集待追 EvalConstants / collectFunctions），
            `c = C(1.0)`（operator record 默认构造）。目标：RecordOrder1、OperatorOverloadConstructorDefault1。
            记录绑定（`R r = R(…)` 在函数里）、记录数组 / 嵌套记录字段、记录输出的调用也在此项。
      - [ ] 另列（非本桶或须用户侧）：`$overload(F, G)` 的实参表在 SCode 里丢失（ymc 打印成 `function OV = $overload;`）→
            解析 / SCode 属用户域，任务卡 `docs/tasks/T-S3-03-overload-scode.md`（2026-09-28 出）；短类链方向前缀冲突下沉到
            inst，任务卡 `docs/tasks/T-S3-04-derived-direction-merge.md`（同日出）；函数型实参（FunctionalArg*）、向量化（FuncVectorization*）、运算符重载、枚举转换
            函数、函数 derivative/inverse 注解、带告警的 FunctionUnitialized*。
- [ ] **S3 往下走（2026-10-01，用户定"先把 S3 往下走"）**。先量后做：
  - [x] 测量基建 `tools/nf_live_sweep.py`：flattening/modelica 里 1106 个纯 `-d=-newInst` 旧前端用例，去注解后
        ymc 与实跑 omc 1.25.0（新前端）逐字节比 stdout（omc 的告警在 stderr，不进比较）。首测：PASS 259 / FAIL 30 /
        ACCEPTS-INVALID 60（omc 报错、ymc 照常出结果）/ CRASH 6 / SKIP 751。→ 96 例 ymc 静默出错，比"未实现"更要紧。
  - [ ] **(56) 正确性：nf-live 的 96 例**——逐个按根因修（修不了的改为 SKIP，绝不静默错出）。依据各条后注：
    - 静默漏洞（影响面最大）：单段名解析不到（非 `time`）不再当内建保留（Constant8 `N`、Import6 `mu_0` 被原样打印）；
      继承元素的绑定按声明处词法作用域找跨作用域常量（Constant8）；零长度复合数组不出元素（ZeroDimUnconnectedFlow1/2）；
      组件数组的绑定修饰 `Foo[2] foo(enabled = {true, false})` 未分配却用了默认值（ConnectArrayCond2）。
    - 结构参数（NFEvalConstants.evaluateExpTraverser :196-230：常量与结构参数的引用一律代入值）：`for i in 1:n`
      （n 因作维度而结构化）→ `1:5`（SumForLoop）；range 表达式化简（ForLoopHideVariable `1:4 + 1` → `1:5`）。
    - 方向 / 变率下沉：`input InputConnector ic` → `input Real ic.r`（InputDecl*/OutputDecl*）；记录字段自身
      constant / parameter 比外层更严时保留（RecordVariability、RecordConstructors2）。
    - 表达式化简（NFSimplifyExp）：`x - (-1)` → `x + 1.0`、`±1 * x`、`(-a) + b` → `b - a`（VanDerPol、SimplifyBinary）、
      `(-e1) / e2` → `e1 / (-e2)`；`^` 两侧提实型（Pow）；字符串常量拼接（StringConcatenation）。
    - 打印：等式左侧 if 表达式加括号（DAEDumpTpl.dumpEquation :705，LhsIf）；字符串里换行后补模板缩进（StringLiterals）。
    - 函数：external "C" 的已知数学函数按内建处理（SCodeUtil.isBuiltinFunction :2525-2560，Function12 / LogCall1）；
      函数体里已知维数组运算展开（ElementWiseMultiplication）；折叠里 size(局部数组)（PolynomialEvaluator1/3）。
    - 记录：等式 / 绑定里出现整记录 cref → 收构造函数（collectExpFuncs CREF → collectTypeFuncs，EquationComponent1-4、
      RecordAssignment）；参数记录绑定到参数实参的函数调用不折叠（ConstructParameters2 → initial equation + fixed=false）。
    - 连接：带字符串注释的 connect（ConnectTheSameConnector，源合并规则未核实 → SKIP）。
    - ACCEPTS-INVALID 60 例：按 omc 错误种类加校验（重复声明、重复修饰、受限类 connector/record 的段、根类为
      package/function、protected 访问 / 修饰、非法 redeclare、final 覆盖、绑定类型不符、包里非常量变量……），
      未做的一律 SKIP。CRASH：`x.start`（YMC_ASSERT，与 inst7 同根）。
    - 门：nf-live 加入收尾清单（FAIL / ACCEPTS-INVALID / CRASH 只许减不许增，剩余入账本）。
    - **完成（2026-10-01）**：
      · 结构参数（新的实例帧机制 frame_value）：条件 / 维度 / fixed / for 范围等编译期求值按"外来修饰绑定 > 声明绑定"求值，
        用到的参数记为结构参数，apply_structural_params 统一打 final、绑定代入值、所有引用处代入并化简（替代原先"结构参数
        被引用 → SKIP"）。YmcCond1/2：`Foo foo(enabled = true)` 此前被当成默认 false 删掉了 foo.x。
      · 条件移除按元素（键带逐段下标；connect / 引用检查同步），Condition8 / ConnectArrayCond2 转 PASS。
      · 单段名：继承元素绑定按声明处词法作用域经 SCODE 找常量（Constant8）；其余解析不到（非 time）→ SKIP（Import6 等此前
        原样打印名字）。零长度复合数组不出元素。方向按 mergeComponentAttributes 下传（YmcDir1）。记录字段变率取
        variabilityMin、记录函数绑定只折常量实参（RecordVariability / RecordConstructors2；ConstructParameters2 改 SKIP）。
      · NFSimplifyExp 二元 / 一元规则、`^` 提实型、Expression.typeCast 推进 if 两支 / 一元负号、字符串拼接、range 化简；
        打印：等式左侧 if 加括号、多行字符串续行补缩进（YmcSimp1 / YmcStr1 探针）。
      · 函数：external "C"/"builtin" 的已知数学函数按内建（"C" 不折叠）；函数体里已知维数组运算展开（ExpandExp）；折叠里
        size(局部数组)；等式里整记录引用收构造函数。
      · 校验（omc 报错 → SKIP，不再静默出结果）：重复声明 / 重复修饰、受限类的段与可见性、记录 / 连接器成员前缀、根类
        package / function / partial、改 final / protected、访问 protected、元素级 redeclare、constrainedby、partial / package /
        无基类 type / enumeration(:) 组件类型、encapsulated 查找边界、extends 被继承类遮蔽、discrete Real 不在 when、
        属性绑定变率与类型、绑定 / 等式类型不符、非数值算术、函数 protected 输入输出、`f(i).y`、函数外 Real ==（告警）、
        outer 元素带修饰、不带类型前缀的枚举字面量、内建类型组件的 `x.start`（原 YMC_ASSERT 崩溃）。driver：testsuite 透传
        的 omc 选项（`-i=` 等）按未实现退 66。
      → nf-live：PASS 259→289、FAIL 30→0、ACCEPTS-INVALID 60→1（ConnectArrayCond 入账本）、CRASH 6→0（3 例两边都拒绝）；
        全 scodeinst：PASS 345→355、FAIL 74→16（余下均为账本内 1.25.0 分歧 / 错误文案）；easy-slice 449 例 355 PASS / 0 FAIL；
        oracle=live 不一致 0；gcc / msvc / msvc-asan 10/10、零警告；ASan 版 ymc 跑 1382 个文件 + 探针 0 报告；+5 单测。
  - [x] (57a) 继承段：flatten 侧 `collect_inherited_sections`（本类段在前、extends 逆序递归，NFInst.instExpressions 的
        Sections.join 头插）已就绪；inst 记录基类实例 + 去守卫属用户域 → 任务卡 `docs/tasks/T-S3-05-extends-sections.md`
        （本地临时打 inst 两处实测：scodeinst +1、nf-live +4，未提交 inst.cpp）。顺带补参数 / 常量绑连续量的变率校验
        （DisturbedResistance1）。门全绿（2026-10-01）。
  - [ ] (57) 错误输出：OMC 错误帧（`Error processing file` … `Execution failed!`）+ 最常见的错误文案与位置，把 incorrect 用例变成
        可对拍（scodeinst 386 + nf-live 约 270）。**2026-10-01 评估**：错误种类长尾（单种最多 7 例），且要逐元素复刻 OMC 的源码
        区间约定 → 性价比低于 correct 用例加宽，后排。
  - [ ] (58+) 按 SKIP 族加宽：数组（150）→ redeclare / 类修饰（64）→ 记录 / 复合绑定（50）→ 查找 / import（49）→ 继承（38）→ …
    - [x] (58a) 数组属性修饰按元素切分（2026-10-02）：Modifier.propagate 的 SPLIT_PROXY 语义（非 each 子修饰逐元素取值、
          each 整棵同值、复合数组修饰递归切到叶属性）；字面量 / 矩阵逐元素下降，类型化数组值逐元素标量化；`:` 维无绑定
          取 start 的形状；无值 `each start` 当没写；维不符 / 缺 each / 标量组件给数组值 → SKIP。顺带修出 3 处既有静默错：
          ① 模型语境里数组字面量作运算数被当标量（`p + {1, 1, 1}` 打成 `p[1] + {1, 1, 1}`）；② 多维逐元素展开打成平铺
          （ArrayAddSub1）；③ 复合数组修饰的混型字面量（`p = {1, 2, 3.0}`，1.25.0 逐元素各自类型化 → `a[1].p = 1`）。
          另两处 1.25.0 缺陷改 SKIP 并入账本（混型切分字面量；函数求值原地改写 cref 实参数组 FunctionBubblesort）；
          向量化调用 `f({1, 2, 3})` 不再常量折叠（FuncVectorization3）。
          → 全 scodeinst PASS 355→365（FAIL 16 不变、oracle=live 不一致 0）；nf-live PASS 289→302（FAIL 0、ACCEPTS-INVALID 1、
          CRASH 0）；easy-slice 459 例 365 PASS / 0 FAIL；gcc / msvc / msvc-asan 10/10、零警告；+3 单测（148）。
          未做（仍 SKIP）：派生类型修饰的 fill 维（origin ≠ parent）、`fill` / `zeros` / 范围 / 推导式作属性值、非字面量的枚举数组属性值。
    - [x] (58b) 非字面量维（2026-10-02）：`:` 维取类型化绑定的维（deduceDimensionFromExp，NFTyping.mo:781-826；函数调用
          输出维在调用处求出）；枚举类型维 / 枚举字面量打枚举类的 scopePath（makeEnumerationType NFInst.mo:627-638：根类里
          → `E.a`、包里 → `P.E3.x`、组件 m 的类里 → `m.F.u`——此前一律打书写名，组件类里声明的枚举是静默错出），顶层 / 包 /
          多段名枚举维可用；Evaluate 注解的参数数组同标量 SKIP（CevalFuncArray6）。
          → 全 scodeinst PASS 365→369、nf-live 302→311（FAIL 0）；+2 单测（150）。
          余下 "non-literal array dimension"（约 45）多要求值器能力：带下标的参数数组 `n[2]`、修饰感知的包 / 组件常量
          （const1 `P.i` / `a.j`）、`max([n, m])`、记录参数 `c.n`、`size(E, 1)`、`b[end]`、Integer / Boolean 范围维、
          数组逻辑运算的形状——后排。
    - [x] (58c) 矩阵构造 `[a, b; c, d]`（2026-10-02）：typeMatrix / typeMatrixComma + makeCatExp 的语义（元素提升到 2 维、
          行内按列 / 行间按行拼接、转公共类型）直接展开成嵌套数组字面量（向量元素按列：`[a, a]` → `{{a[1], a[1]}, …}`）；
          数组方程的分派与形状检查改走 expr_shape（嵌套字面量 `m = {{x, 1}, {2, y}}` 此前因粗类型 SKIP）。
          → 全 scodeinst PASS 369→370、nf-live 311→322（FAIL 0）；+1 单测（151）。余下矩阵用例卡在 fill / transpose /
          symmetric / cat / diagonal 等内建、切片下标、函数向量化。
    - [x] (58d) 下标求值（2026-10-02）：常量表达式下标（for 展开后的 `a[i - 1]`、常量 `x[N]`）与 `end` 求成字面量；参数
          变率的下标按结构参数求值（typeSubscripts → Structural.markSubscript，NFTyping.mo:2122-2127：参数打 final、
          下标代入值，修饰感知）；算法 for 里引用迭代变量的下标（`a[i - 1]`）保持符号、不静态越界检查，其中含参数的
          （`x[p[i]]` → omc 打 `x[{1, 2, 3}[i]]`）→ SKIP。
          → 全 scodeinst PASS 370→371、nf-live 322→332（FAIL 0）；+1 单测（152）。余下 "out of range" 主要是切片
          （`x[2:3]`、`a[{1, 3}]`、`y[:, 1]`）与运行时变量下标（`r[c, n, c]` 的 n）。
    - [x] (58e) 切片下标（2026-10-02）：`:` / `a:b` / `a:s:b` / `{i, j}`（含 `end`、结构参数边界）求出选中下标表，引用类型带
          切片维；`:` 用 Wild 表示、下标位置打印成 `:`（Subscript.WHOLE）。连续数组绑定保持整体（`y = x[2:3]`、
          `w = z[1,:]`），参数绑定 / 方程按切片映射标量化（SL2 探针）。
          → 全 scodeinst PASS 371→374、nf-live 332→339（FAIL 0）；+1 单测（153）。
    - [x] (58f) 模型语境的数组内建（2026-10-02）：fill / zeros / ones / identity / diagonal / transpose / vector 维已知时展开成
          数组字面量（参数维按结构参数求值），sum / product 展开成元素运算链（B2 探针）；求值器补一参 `size(a)`（Scalar 的
          `f(fill(1, 3))` 常量折叠成 3.0，此前被 fill 的 SKIP 遮住）。
          → 全 scodeinst PASS 374→384、nf-live 339→349（FAIL 0）；+1 单测（154）。未做：`max(a)` / `min(a)` 数组实参的符号调用、
          `linspace` 的 `array(… for i in …)` 形。
    - [x] (58g) 向量 / 矩阵积与归约（2026-10-02）：模型语境 `*` 两侧皆数组按 checkBinaryOperationMul 的四种形状展开成积和
          （k 升序、左结合，C3 探针）；`sum / product(e for i in r)` 逐值代入连成运算链；参数 / 常量数组的推导式绑定逐元素
          代入迭代变量（`:` 维取范围长度，C4 探针）。连续数组的推导式绑定（omc 保留 `array(… for …)`）仍 SKIP。
          → 全 scodeinst PASS 384→391、nf-live 349→368（FAIL 0）；+1 单测（155，矩阵积旧的 SKIP 断言改为对拍输出）。
    - [x] (58h) 模型算法段的控制语句（2026-10-02）：if / while / break（须在循环里）/ when（不嵌套、不在 if / 循环 /
          initial algorithm 里，体内可赋 discrete）/ assert；if 按 simplifyIfStmtBranches 化简常量条件（AlgorithmElseOpt），
          体为空的 for 删去；for 语句范围里的参数是结构参数（SumVectorForIf `1:n` → `1:5`）。reinit 在算法段 omc 报错 → SKIP。
          → nf-live PASS 368→382（FAIL 0）、scodeinst 不变；+1 单测（156）。
    - [x] (58i) when 条件（2026-10-02）：sample(a, b) / change(x) / edge(b)、条件向量 `{c1, c2}`、Boolean 数组 cref 按元素展开
          成条件向量（`when b` → `when {b[1], b[2], b[3]}`，W1 探针）；方程与算法的 when 共用。not initial()（omc 告警）仍 SKIP。
          → 全 scodeinst PASS 391→394、nf-live 382→388（FAIL 0）；+1 单测（157）。
    - [x] (58j) 元组方程 / 元组赋值（2026-10-02）：`(a, , c) := f(…)` / `(a, b) = f(…)` 实参全常量 → 用户函数求值后按
          simplifyTupleElement 逐元素拆开（空位跳过）；否则保留元组形、空位打 `_`（T1 / T2 探针）。求值器可取全部输出。
          → 全 scodeinst PASS 394→398、nf-live 388→397（FAIL 0）；+1 单测（158）。
    - [x] (58k) import（表达式侧，2026-10-02）：flatten 的 SCODE 名字解析（find_scode_element_at）在每层本地元素之后查该层
          import（限定 / 重命名 / 组导入优先于 `.*`，同档两项 = 冲突用到即 SKIP，目标从顶层解析，不被继承）；经 import 的常量
          在目标包的作用域求值、函数按真实路径发射（`A.B.f`）。顺带：本地组件绑定里的名字按 enclosing 的词法链解析（此前
          只查顶层）。类型名 / 基类名经 import 属 inst → 任务卡 `docs/tasks/T-S3-06-imports.md`。
          → 全 scodeinst PASS 398→403、nf-live 397→400（FAIL 0）；+1 单测（159）。
    - [x] (58l) parameter / constant 记录与记录数组（2026-10-02）：变率前缀按 variabilityMin 下沉到继承字段、数组字段（emit_array
          此前不看下沉的 var_floor）、嵌套记录、自带 parameter / final 的字段（R1 / R2 探针）；记录数组元素引用 `r[1].x`
          （首段常量 / end / 结构参数下标、在界内；越界一律 SKIP——1.25.0 对标量末段不查界、对带下标末段报错，不一致）。
          → 全 scodeinst PASS 403→405、nf-live 400→401（FAIL 0）；单测 160（复合数组元素引用旧的 SKIP 断言改为对拍）。
          记录族余下多卡在记录绑定（构造 / 函数返回记录 / 记录数组绑定）、final 覆盖、Evaluate 注解。
    - [x] (58m) String(x, …)（2026-10-02，MLS §3.7.2 四个重载）：实参按重载补齐缺省打印（`String(x, 6, 0, true)`、
          `String(n, 0, true)`、`String(x, "6.2f")`）；全常量 → 求值成字符串（%g 有效数字、minimumLength / leftJustified 补空格、
          format、枚举名）；Real 结果带指数 → SKIP（1.25.0 是 Windows CRT 三位指数，与工具链相关）。MSL 的 assert 文案常用。
          → 全 scodeinst PASS 405→408、nf-live 401→406（FAIL 0）；+1 单测（161）。
    - [x] (58n) delay（2026-10-02）：两参形按 delay2 早内联成 `delay(e, d, d)`（NFModelicaBuiltin.mo:1105-1119），delayTime
          ≤ parameter 且求值为 0 → 只留 e（simplifyDelay，NFSimplifyExp.mo:577-595；判 0 的求值不标结构参数）；parameter 形参给了
          变量 → SKIP。→ 全 scodeinst PASS 408→409（FuncBuiltinDelay，open-questions 一条随之了结一半）、nf-live 406→411；
          +1 单测（162）。
    - [x] (58o) 记录 cref 整体绑定 + 字段修饰合并（2026-10-02）：`R r2 = r1` 按 splitRecordCref（NFFlatten.mo:1027-1053）
          拆成字段 cref 修饰 `r2(x = r1.x, …)`（源须是本作用域记录标量、含目标全部字段；常量源经字段常量折叠 = evalExp 效果）；
          整体绑定 + 字段修饰：字段绑定一律取整体绑定（含构造缺省实参，RM2/RM4 探针），字段属性合并（同作用域并入，异作用域
          SKIP）；顺带修掉记录常量折叠路丢字段属性（`R d(y(min = 0)) = R(4, 5)` 曾丢 min）。不同记录类的等式按
          matchComplexTypes 分类：同名同序原样，要 typeCastRecord → SKIP（RecordOrder2 由此从 SKIP 掉成 FAIL 的口子堵上）。
          if 式记录绑定（参数条件选支 + 标结构）留后续。→ 全 scodeinst PASS 409→412、nf-live 411→413（FAIL 0）；+2 单测（164）。
    - [x] (58p) 复合数组外来修饰 + 记录数组整体绑定（2026-10-02）：外来修饰（`b(a(x={…}))`）同自身修饰逐元素切分（作用域 =
          修饰所在实例）；记录数组整体绑定 `R ra[2] = {r1, R(3, 4)}` / `= rs` 逐元素取第 i 个（字面量下降 / 数组 cref 补下标）
          再按标量记录绑定拆字段，`:` 维按绑定字面量推，字段属性合并。顺带修两处静默错：复合数组元素里 Var 数组绑定移成
          方程时左侧丢逐段下标（`a.x = …` → `a[1].x = …`，CA1 探针）；记录构造只比末段名（同名异包 `P2.S(…)` 绑给 `P1.S`
          会错用目标类缺省，CI2 探针）→ 改为解析到同一记录类。→ 全 scodeinst PASS 412→419（FAIL 16 不变）、nf-live 413；
          +2 单测（166）。记录数组跨元素取字段 `r.x`（CevalRecordArray1/5）留后续。
    - [x] (58q) 复合数组跨元素取字段（2026-10-02）：`r.x` / 前几维带标量下标的 `a[1].x`（x 为标量内建字段）按 ExpandExp
          展开成元素数组 `{r[1].x, r[2].x}`（元素经 cref() 解析：越界 / 结构参数下标 final / 常量折叠复用），下游 Var 绑定
          `a = {…}`、参数逐元素、`sum(r.y)`、数组方程标量化都与 omc 一致（XR1 / XR2 探针）。→ 全 scodeinst PASS 419→424
          （CevalBinding4、CevalRecordArray1/3/5、const4；FAIL 16 不变）、nf-live 413；+1 单测（167）。叶是数组 / 复合字段、
          更深的段带下标（EquationQualified1）留后续。
    - [x] (58r) if 方程结构条件（2026-10-02）：参数条件且此前各支都已折掉 = 结构条件（typeIfEquation NFTyping.mo:3581-3590），
          参数标 final、引用处代入、按值选支（flattenIfEquation NFFlatten.mo:1975-2010；帧求值本就记结构参数）；前面有运行时
          分支 / Evaluate=false / fixed=false 的参数条件原样保留为分支条件（IF2、EvaluateFalse1）。条件参数须字面量绑定：
          omc 的传递标记在绑定已被 Ceval 缓存（CEVAL_BINDING，getExpOpt 不返回）时中断，依赖求值次序（IF3 `b = c > 1` → c
          不 final，而维度 `n = m` → m final）→ 不猜，SKIP。→ 全 scodeinst PASS 424→427（EvaluateFalse1、IfConnect1/2）、
          nf-live 413→417（EquationIf1/2/3、IfEquation）；+1 单测（168）。
    - [x] (58s) 取下标式 + 运行时下标 + 数组元素注释（2026-10-02）：`(e)[i]`（ExpKind::Subscript）——基是 cref → 下标并进 cref
          （`(x)[n]` → `x[n]`），一维字面量数组 + 字面量下标 → 取元素，运行时下标 → ASUB `{1.0, 2.0, 3.0}[n]`（转型推进元素；
          打印按 ExpressionDump ASUB :703-714，下标 "," 无空格）；cref 的运行时整型下标保留符号（`x[n]`、`m[1,n]`、
          `x[n + 1]`，不做静态越界）；数组元素逐个带声明注释（WillowForest 曾被运行时下标遮住，放行后暴露）。→ 全 scodeinst
          PASS 427→431（End5、Subscript6、SubscriptedExp1/3）、nf-live 417→420（Summation、SubscriptEval、WillowForest）；
          +1 单测（169）。教训：探针别 `2>&1`（成功时 omc 告警在 stderr，门只比 stdout，见 lessons）。
    - [x] (58t) constant 数组整体内联 + `:` 维数组被引用（2026-10-02）：整体 constant 数组引用（`x = a`、`a .+ b`、`sum(a)`、
          `a * {1, 2, 3}`）取已发射值内联成带真维类型的数组字面量再折叠（CE3 探针）；`:` 维数组（`Real x[:] = {…}`）被引用时
          取已发射元素的形状（单段本地 cref、须先发射；SubscriptedExp4 `(x)[n]`、CD1 `2 * x`）。→ 全 scodeinst PASS 431→432、
          nf-live 420→424（ArrayAddition / Division / Multiplication / Subtraction）；+1 单测（170）。另：无绑定参数取 start
          当绑定（updateParameterBinding）已核实语义但 omc 同时告警、文件比对要告警行 → 仍 SKIP，告警输出需元素源码范围
          （parser，用户域），记入 open-questions。
    - [x] (58u) cat + 数组字面量绑定的表达式叶（2026-10-02）：`cat(k, A1, …)` 维已知时展开成数组字面量（k 常量、同秩、除
          第 k 维外同形，有 Real 实参提 Real；CT1/CT3 探针）；数组字面量绑定的叶允许 cref / 调用 / 运算等表达式（`{time, 2, 3}`、
          `{k, 3}`），定型后逐叶核类型（Real ← Integer/Real，其余同类；不符 = omc "Array types mismatch" → SKIP，BL1）。
          → 全 scodeinst PASS 432（Subscript5 由 SKIP 转为账本内分歧：文件补尾随 `:`，1.25.0 不补，FAIL 基线 16→17）、nf-live
          424→431（AppendElement、Concat3、ConcatArr1/2/4、MatrixMulVector、BoolArrayTest）；+1 单测（171）。linspace（omc 打成
          `array(… for i in 1:n)` 推导式）留后续。
    - [x] (58v) 字面量范围绑定（2026-10-02）：起 / 步 / 止都是数值字面量的范围有形状（getRangeTypeInt `max(intDiv(stop - start,
          step) + 1, 0)` / getRangeTypeReal `realRangeSize`，NFTypeCheck.mo:2758-2833、Util.mo:1324-1333；Range 的 ty 仍是元素
          类型供迭代变量用，形状由 expr_shape 另算）；Var 数组整体保留 `x = 1:5`（Real 数组起 / 步 / 止转 Real：`r = 1.0:3.0`），
          参数逐元素 = 起 + (i - 1)·步。→ 全 scodeinst PASS 432→433（DimUnknown17）、nf-live 431→437（Array1、ArrayRange、
          Range1/2、Operators、ConditionalComponent）；+1 单测（172）。
    - [x] (58w) 限定名常量路径（2026-10-02）：`P.cr.r`、`P.cr[1].r1.r`、`P.x[2]` 沿路径取值（新 eval_qual_path：前几段按类
          下降，首个组件须 constant，余段按下标 / 记录字段取，复用 cval_path）；无绑定的记录常量 / 记录常量数组由组件修饰 +
          字段缺省构造（const_comp_value / record_value：each 原样、非 each 逐元素切分、嵌套构造修饰）；包常量数组整体引用
          内联成数组字面量（原先 literal_of 对数组值返回 0，幸被形状核对挡成 SKIP）。QP1 探针逐字节一致。→ 全 scodeinst PASS
          433→436（CevalRecordArray2、PackageConstant3/4）、nf-live 437→440（RecordConstant1/2/3）；+1 单测（173）。
    - [x] (58x) 隐式 for 范围 + 隐式尾随 `:` + 非 fixed 参数条件（2026-10-02）：`for i loop` 的范围按 deduceIterationRange
          （NFTyping.mo:3826-3936：体里 i 恰作下标处的各 (数组, 维) 维长须相同 → 1:n）；下标少于维数的数组引用（`x[i]` 引 2 维 x）
          = 隐式尾随 `:`，引用类型接上余维、标量化补末尾下标，打印仍只带给出的下标（1.25.0）；求不出值但引用 fixed = false 参数的
          if 条件 = 非结构条件，原样保留（isExpressionNotFixed，IfEquation8）。→ 全 scodeinst PASS 436→441（IfEquation8、
          ImplicitRangeEq1/2/3、Size3）、nf-live 440→442（SubscriptsFill1、EquationFor6）；+1 单测（174）。
    - [x] (58y) 记录构造调用 + 拆不开的记录绑定挪成方程（2026-10-02）：`R(a, b)` / `R(x = a)` 在表达式里打成对构造函数的
          调用（缺省实参用字段声明值补齐、Integer → Real，RC1 探针 `R(x = time)` → `R(time, 2.0, 3)`；只做纯数据记录、记录类须有
          实例节点，否则 SKIP）并登记构造函数块（ensure_record_ctor，与 collect_record_ctors 共用；返回记录的调用也收构造函数）；
          拆不开的记录整体绑定（运行时 if、返回记录的函数调用）挪成方程 `r = …`（≤ parameter 进 initial equation），字段一律无
          绑定、parameter 字段 fixed = false（NFFlatten.mo:994-1004 / :730-735，ModCtx::unbound_fields）；参数条件的 if 记录绑定
          按 splitRecordCref 的 IF 分支选支 + 标结构参数（:1061-1074，RE1 探针 k final、`p3.a = 2.0`）。条件求不出（如依赖外部
          函数，CevalBinding9）——omc 也求不出时保留 if 挪方程，但 ymc 分不清"omc 也求不出"与"ymc 求值器不够"→ SKIP。
          → 全 scodeinst PASS 441（不变；多例 SKIP 原因前移到函数体记录 / 无实例记录）、nf-live 442→443（RecordConstructors）；
          +1 单测（175）。下一步：函数体里的记录输出赋值 / 字段赋值（RF1/RF2 探针）。
    - [x] (58z) 返回记录的函数（2026-10-02）：模型里的记录值调用放行（类型取输出记录类的实例节点；多输出 / 无实例 → SKIP），
          record_def_of 认返回记录的调用；函数体允许整记录赋值 `r := R(…)`（核记录类相同）与记录输出 / 局部的字段赋值
          `r.a := x`。挪方程只在绑定有运行时 / 参数变率且非 final 时做：全常量（omc Ceval 求值）/ final parameter（omc 内联再求值，
          RecordBinding18 账本）→ 仍 SKIP。RF1 / RF2 探针逐字节一致。→ 全 scodeinst PASS 441（不变）、nf-live 443→445
          （ConstructParameters2、ComplexNumbers）；+1 单测（176）。
    - [x] (59a) 记录的 final 字段 + 记录变率下沉进求值（2026-10-02）：记录构造的输入 = 非 protected、非 final 字段
          （NFRecord.collectRecordParam :260-283；`R(1.0, 3.0)` 越过 `final constant Real y`）；记录实例里引用兄弟字段时按
          variabilityMin 下沉后的变率判"高变率绑定"（refs_runtime_var 带 floor）与帧求值（frame_value 用帧 ctx 的 var_floor）——
          `constant R r` 里声明为 `Real x` 的字段是 constant（RecordBinding7 `y = x / 2.0` → 0.5）。→ 全 scodeinst PASS 441→443
          （RecordBinding6/7）、nf-live 445；+1 单测（177）。
    - [x] (59b) 内建的命名实参 + homotopy 实参提 Real（2026-10-02）：homotopy(actual, simplified) / semiLinear(x, positiveSlope,
          negativeSlope) 的命名实参按 NFModelicaBuiltin.mo 形参序重排成位置实参（smooth 的命名实参 1.25.0 报错，不放行）；homotopy
          按声明 `input Real … output Real` 实参提 Real（原表 Keep，`homotopy(x, 1)` 曾错印整数）。→ 全 scodeinst PASS 443→444
          （FuncBuiltinHomotopy）、nf-live 445；+1 单测（178）。
    - [x] (59c) 推导式表达式 `array(e for i in r)` + smooth 的 Integer 实参转型（2026-10-02）：模型语境的 `{e for i in r, …}`
          不再 SKIP——保留成 Reduction（字段约定 call_name = {array, 迭代变量}、a = 体、b = 范围，不改 inst 的 expr.h），最右迭代变量
          在外层、迭代变量在体里作符号引用、参数范围按结构参数求值打字面量；Var 绑定 / 等式整体保留（同数组构造调用），Integer
          元素对 Real 整体转型 `/*Real[2, 3]*/(…)`（expr_dump 新增 Reduction 打印与带维数组转型；simplify 不再把数组转型降成标量
          转型）；sum 等逐元素用时代入迭代变量（scalarize）。smooth 的标量 Integer expr 插 `/*Real*/`（原 SKIP）。AC2 / AC3 / BT1
          探针逐字节一致。→ 全 scodeinst PASS 444→445（DimUnknown9）、nf-live 445；+1 单测（179）。
    - [x] (59d) linspace 早内联成推导式（2026-10-02）：照 NFModelicaBuiltin.mo:305-316 的 EarlyInline 体 `{x1 + (x2-x1)*(i-1)/(n-1)
          for i in 1:n}` 建 Reduction 再化简（CT1 / LS1 探针逐字节一致，含 `-1 + …` 化成 `… - 1.0`）；n 须求成 ≥ 2 的结构常量，
          否则 / 命名实参 / 迭代变量名 i 被外层占用 → SKIP。函数体里的 linspace（Vectorizable7，n 为输入）仍 SKIP。→ 全 scodeinst
          PASS 445→446（FuncBuiltinLinspace）、nf-live 445→449（Linspace1/2、ConstructFunc、InitialReduction）；+1 单测（180）。
    - [x] (59e) 空数组（2026-10-02）：维为 0 的变量不出行（形状记进 empty_shapes_，供 `:` 维引用 / size()）；类型为空数组的方程 /
          赋值整条删去（NFSimplifyModel.mo:254 / :413），被删式子里新登记了函数 → SKIP（omc 函数收自化简后的模型）；空数组变量的
          引用换成缺省值 `{}` / `{{}, {}}`，下标引用成 `{}[m - 1]`（NFInstUtil.replaceEmptyArraysExp :164-205）；fill / zeros /
          ones 的 0 维产带全维类型的空字面量；空内维的向量 / 矩阵积 = 0（NFExpandExp.makeScalarProduct :796-799）；空 sum /
          product = 0 / 1（NFSimplifyExp.mo:464-465）；der 作用在连续 Real 空数组上透传（方程随后删去）。带属性修饰 / 字面量绑定
          / 记录数组 / pre 等未核实 → SKIP。E1–E15 探针：9 例一致、其余 SKIP（omc 报错的 4 例全 SKIP）。→ 全 scodeinst PASS
          446→451（EmptyArray1/2、FuncBuiltinSum/Product、OperationMatrixProduct3）、nf-live 449→464；+1 单测（181）。
    - [x] (59f) 向量 / 矩阵内建 cross / skew / outerProduct / symmetric / matrix / ndims（2026-10-02）：cross / skew 按
          NFModelicaBuiltin.mo:353-372 的 EarlyInline 体展开再化简（符号实参照式保留，AB2 探针）；outerProduct（1.25.0 对符号实参留
          `promote(…, 2)` 形）与 symmetric（非常量留调用）只做全字面量实参；matrix 标量 / 向量 / 矩阵 / 余维为 1 的字面量；ndims →
          秩。非字面量 Integer 实参、形状不对（omc 报错）→ SKIP。→ 全 scodeinst PASS 451→457（FuncBuiltinCross / Matrix /
          Ndims / OuterProduct / Skew / Symmetric）、nf-live 464→470（ArrayAlgebraFunc、DimSize、Matrix、Matrix2、Skew、
          Symmetric）；+1 单测（182）。
          **同批修 59c 的潜伏错**：推导式嵌在数组运算里（`x = 2 * {…}`、`y = -{…}`、点积 `{…} * {1, 2, 3}`）曾被 scalarize 逐元素
          展开，omc 是整体保留 + 数组算符打印（hasArrayCall = contains，NFExpression.mo:5440-5471；R1 / R2 探针）→ scalarize 的
          Reduction 支改成只在核实过的调用点展开（sum / product 实参、参数数组摊分），其余 SKIP。语料无该形态（两套扫描判定不变）；
          +1 单测（183）。数组算符的整体打印（`array(…) * 2.0` 等）留待后续。
    - [x] (59g) 向量化调用 + 一批探针撞出的潜伏错（2026-10-02）：
          **向量化**（NFCall.vectorizeCall :2908-2964）：用户函数 / 23 个初等内建（sin…atan2、mod、div、abs、sign、integer、
          homotopy、semiLinear…）的实参比形参多出前导维 → `array(F(x[$i1], …) for $i1 in 1:n)`，迭代变量按维序号每调用从 `$i1`
          起（1.25.0 实测；源码 HEAD 是全局计数）；实参形态 = 整体 / 带下标 / `:` 切片的 cref 与书写的字面量 `{…}[$i1]`；
          整体保留、sum 实参 / 参数摊分 / 属性摊分逐元素代入、作数组形参的实参原样。范围切片 / 运算式 / 嵌套向量化实参、维不
          一致、rem、嵌在数组运算里 → SKIP。`array(e for i in r)` 调用写法 = 推导式。
          **sum / product / min / max(数组) 重做**（V4d / V4e 探针 56 行，此前 22 行错出）：实参化简后是字面量数组才求值成一个
          字面量；否则 sum / product 出元素链且**链不再化简**（`1.0 + 2.0 + time`、`x[1] + 0.0 + x[2]`、`(-x[1]) + …`、
          `/*Real*/(1 + 2 + 3)`；nofold_bins_），min / max 的数组构造子实参改写成归约式 `max(e for i in r)`。call() 里的
          eval_reduction 整体求值只留给函数语境与直接写的归约式。
          **pre / change**：实参须是变量、when 之外不能是连续变量（NFBuiltinCall.typePreChangeCall :578-588；此前 P1 / P5 / P7
          三种 ACCEPTS-INVALID）。**函数体的 sum / product**：维全已知的整体局部数组展开成元素链（FuncVectorization2 的
          `o := b + a[1] + … + a[4]`），其余可展开形态 SKIP（此前错印 `sum(a .* b)`）。**字面量取字面量下标**化简成元素。
          **属性的推导式值**逐元素摊分（S16）。展开后成常量的用户函数调用 omc 会求值，未做 → SKIP。
          探针 7 组 206 行：151 PASS / 47 SKIP / 8 BOTH-REJECT / 0 FAIL / 0 ACCEPTS-INVALID。→ 全 scodeinst PASS 457→458
          （FuncMap1），另 6 例与 1.25.0 逐字节一致但文件 Result 是较新 OMC 的 `$i0` 命名（FuncVectorization1/2/3、
          FuncVectorizationBuiltin / CastConflict1 / Map1，记入分歧账本，FAIL 基线 17→23）；nf-live 470→472（Vectorizable5、
          ArrayReduce）；+4 单测（187）。
    - [x] (59h) 数组上的 der / pre + Boolean 数组表达式（2026-10-02）：
          **der / pre**（Call.isVectorizeable :1300-1318 对 der / pre / previous 为 false）：数组实参不向量化；显式等式逐元素
          标量化（`der(y) = -A * y + x` → `der(y[1]) = x[1] - (A[1,1] * y[1] + …)`、`v = pre(b) and b2` → `v[1] = pre(b[1]) and
          b2[1]`），绑定整体保留 `k1 = der(x)`、算术绑定逐元素展开 `{2.0 * der(x[1]), …}`；另一侧有向量化调用时整体保留。此前数组
          der 一律 SKIP，`v = pre(b)` 错成整体保留。edge 向量化、change 整体保留（Boolean 数组类型）。非 cref / Integer 数组的 der → SKIP。
          **Boolean 数组的 and / or / not**：绑定整体保留（`g3 = b and not b2`）、全字面量在定型时折成字面量数组、参数数组与显式
          等式逐元素（不化简 `true or b[1]`）；simplify 不再丢数组类型；形状按字面量看穿。标量与数组的 and / or、数组关系
          `{1, 2} < {2, 3}`（omc 类型错，此前 ACCEPTS-INVALID / 错当标量关系）→ SKIP；两个混合字面量、混合字面量的 not 的字面量叶
          （omc 的特殊化简 / 不化简）→ SKIP。**Boolean 范围** `false:true` 整体保留、空范围不出变量。
          **external 段注解的 derivative / inverse** → SKIP（omc 连带压平导数函数，NFFlatten.mo:2987-2995；ExternalFunctionAnno
          放行数组 der 后暴露）。探针 4 组 132 行：102 PASS / 0 FAIL / 0 ACCEPTS-INVALID。→ 全 scodeinst PASS 458→460
          （FuncBuiltinEdge2、OperationLogicalBinary2）、nf-live 472→475（ArrayAddition2、ArrayRemoveIndex1、BooleanRanges）；
          +2 单测（189）。数组 if 表达式（`i3 = if c then x else {…}` 整体 / 等式逐元素）留待下一桶。
    - [x] (59i) 数组 if 表达式（2026-10-02）：两支同形、元素同为 Boolean 或数值（有 Real 则全提 Real）。绑定整体保留、各支规整成
          omc 的打印形态（引用原样、字面量逐叶转、算术式逐元素展开成字面量、嵌套 if / 向量化调用原样：`i3 = if time > 1.0 then x
          else {2.0 * x[1], …}`）；显式等式 / 参数摊分 / sum 里逐元素（`der(y[1]) = if time > 1.0 then x[1] else -y[1]`）；`:` 维由
          分支形状推断；条件折成字面量时直接取一支（IfExpression10）。两支形状不同（结构参数条件求值）、Integer 引用配 Real、
          逻辑式分支、含数组调用的等式 → SKIP；数组 if 作属性值是 1.25.0 内部错 → SKIP。探针 V4w 45 行：35 PASS / 6 SKIP /
          4 BOTH-REJECT / 0 FAIL。→ 全 scodeinst PASS 460（不变）、nf-live 475→476（ConditionalArrayExpression2）；+1 单测（190）。
    - [x] (59j) 数组 if 两支维不同时按结构参数求值选支（2026-10-02）：if 是整个绑定 / 等式一侧（if_top_）、两支都是同秩数组、
          条件可求值（参数字面量绑定 / fixed、无 Evaluate = false）→ 取选中的那一支，条件里的参数标结构（`final parameter Boolean
          b = true` + `x = {1.0, 2.0}`；1.25.0 实测，源码 HEAD 的 typeIfExpression 对不相容分支直接报错）。1.25.0 的怪相一律 SKIP：
          嵌在运算里（类型错）、嵌在另一个 if 的分支里（原样留下不相容的 if）、选中的支引用参数（`fill(n, 2)` → `{2, 2}` 丢
          Real 转型）或是 Integer 算术式（IfExpression12 `x = {6, 6}`）、组件数组元素里（IfExpression6 内部错）、标量对数组（类型错）。
          探针 V4z 36 行：13 PASS / 14 SKIP / 9 BOTH-REJECT / 0 FAIL。→ 全 scodeinst PASS 460→463（IfExpression4 / 8 / 14）、
          nf-live 476（不变）；+1 单测（191）。
    - [x] (59k) Evaluate = true 的标量参数 + 结构参数的别名连带（2026-10-02）：
          **Evaluate = true**：标量参数当结构参数——final、绑定按本实例帧求值成字面量（修饰感知）、绑定里用到的参数连带 final、
          引用处代入并化简（`Real x = 2.0 * time`）；绑定依赖 Evaluate = false 的参数时 omc 不结构化（撤销标记）。求不出值 / 无
          绑定只有 start / 带 fixed 属性 / 数组参数 → SKIP。
          **别名连带**（修潜伏错，与 Evaluate 无关也会触发）：绑定在源码里就是某结构参数裸引用的参数连带成为结构参数
          （`parameter Integer n = 2; parameter Integer m = n; Real y[n]; Real x = m * time` → omc `final parameter Integer m = 2`、
          `x = 2.0 * time`；此前 ymc 出非 final 的 m）。传递、与声明序无关、实例修饰同样；参数数组的裸引用绑定整体连带
          （CevalRecordArray10）。表达式绑定 / 类型不同的别名 / 记录表达式拆出的字段（derived_mods_）/ 数组字面量元素不连带，
          只是被求值；组件数组修饰分发合成的裸引用、数组只部分连带 → SKIP。
          **simplify_expr 进数组字面量逐元素化简**（结构参数代入后 `{/*Real*/(3), …}` → `{3.0, …}`）。
          探针 EV m1–m5 71 个模型：63 PASS / 7 SKIP / 1 BOTH-REJECT / 0 FAIL。→ 全 scodeinst PASS 463→464
          （CevalRecordArray10）、nf-live 476（不变）；+1 单测（192）。数组参数的 Evaluate（整体引用代入 + 依赖标记）留待后续。
    - [x] (59l) 函数注解 + 未引用函数统一由 prune_functions 收走（2026-10-02）：
          **无影响的注解放行**：Inline / LateInline / InlineAfterIndexReduction / smoothOrder / Documentation / Icon / GenerateEvents /
          preferredView / obsolete / 厂商注解 / `__OpenModelica_Impure`（FA 探针 16 种与无注解逐字节相同）；其余 `__OpenModelica_*`
          （EarlyInline 把调用内联掉）仍 SKIP。**derivative / inverse**：被引用的函数连带发射（NFFlatten.flattenFunction
          :2987-2995；FlatFunction::linked，本函数留则它们也留，传递；external 段注解同样）；只做 `derivative = name` 与
          `inverse(input = call(简单实参))`，带选项（order / noDerivative / zeroDerivative）、指向不存在的函数 → SKIP。
          **空 algorithm 段**照打一行 `algorithm`（FlatFunction::has_algorithm）。**删掉四处"登记过函数就 SKIP"的守卫**（空数组
          绑定 / 方程 / 赋值、未选中的 if 分支）：omc 的函数收自化简后的模型（collectFunctions :418-429），prune_functions 已按
          可达性收。**顺带修潜伏错**：结构参数代入后实参全成字面量的用户函数调用 omc 会求值（simplifyCall :206，`m = g(n) + n`
          → `6.0`），ymc 无求值入口 → SKIP；克隆时丢了"不折叠"标记（external "C" 的 `g(n)` → omc `sin(2.0)`）→ 标记跟着走。
          探针 FA 20 + FD 11 + FE 7 + EA 2。→ 全 scodeinst PASS 464→467（FunctionDerivative1、FunctionInverse1/2）、nf-live
          476→484（Inline1–6、ArrayAssignEmpty、ExternalFunctionAnno）；+2 单测（194）。
          **重跑旧探针集发现 59k 的回归**（ba/z 8 例 FAIL）：别名连带不是与声明序无关——`parameter Boolean pd = pt` 在 pt 被
          if 表达式条件（定型期）标结构**之前**定型 → omc 不 final。下一桶（59m）修。
    - [x] (59m) 结构标记按 1.25.0 的三个阶段重做 + 三个潜伏错（2026-10-02）：
          **标记带时刻**（MarkPhase，源码按 v1.25.0 标签）：Early = 实例化期（维 / Evaluate / 条件 / fixed / 绑定里数组构造子的
          迭代范围）与 `:` 维数组的绑定；Binding = 绑定 / 属性定型期（按声明序，参数下标、参数范围、fill 维实参）；Late =
          方程 / 算法段与压平期（for 范围、if 表达式选支、if 方程条件）。**依赖连带**只进"尚未定型"的绑定（markComponent 的
          `Binding.untypedExp`）：Early 一路连带、Binding 看该参数发射了没有、Late 不连带（此前一律连带：`n = k; for i in 1:n`
          把 k 也打成 final）。**别名连带**在别名自己的绑定定型时判（checkComponentBindingVariability）：目标须在那之前已是
          结构参数（修 59k："与声明序无关"是错的，`m = k; k = n; Real y[n]` 的 m 不 final）。
          **求值帧跟书写处实例走**（FrameAt）：外层写的修饰表达式 `M m(v = c[i], x(fixed = b))` 此前按 m 里的同名参数取值
          （`m.v = c[2]`、`fixed = false`）。**求值事务**：顶层带参数求值失败时撤销途中的结构标记（`if time > k` 的 k 此前被
          标 final）。**if 表达式两支类型不相容** → SKIP（此前放行 `if c then 1 else "a"`）。`size(界非字面量的切片)` 1.25.0
          原样留下 → SKIP。探针 AO a1–a7 共 100 个模型：82 PASS / 18 SKIP / 0 FAIL；全部旧探针目录（约 1100 个模型）重跑 0 FAIL。
          → 全 scodeinst PASS 467、nf-live 484（均不变，无判定变化）；+2 单测（196）。
    - [x] (59n) 同步算子（MLS §16）的定型与打印（2026-10-02）：
          **Clock 构造**：`Clock()`；一个实参按类型选重载（Real 原样、Integer 补 `, 1`、Boolean 补 `, 0.0`）；两个实参时第二个
          求值成字面量（不标结构）；首参 omc 既不化简也不代入结构参数（`Clock(n, 1)`、`Clock(1.0 + 2.0)` 原样）→ 只收化简前后
          同形的首参，clone_subst 不进 Clock。**sample(u[, 钟])** → 补 `Clock()`，类型同 u（经典的 sample(start, interval)
          照旧）。**hold / noClock**（类型同 u）、**previous**（实参须是组件引用，数组按元素向量化）、**backSample /
          shiftSample / subSample / superSample**（计数类实参按结构参数求值成字面量，缺省 resolution = 1 / factor = 0，首参可以
          是钟）、**interval / firstTick**。命名实参按形参名重排。**钟控 when**：条件是钟时体按任意方程收（if / 元组 / 非赋值
          形等式），带 elsewhen → SKIP。返回数组的算子嵌在运算里、非引用的数组实参、Clock 类型的组件（inst 不认）→ SKIP。
          探针 SY s1–s5 254 行：206 PASS / 22 SKIP / 26 BOTH-REJECT / 0 FAIL。→ 全 scodeinst PASS 467→475（FuncBuiltinHold1 /
          Previous1 / SubSample1 / SuperSample1、IfEquation6、IfEquationImpure1、WhenClocked1、WhenClockedTupleCall1；
          FuncBuiltinPrevious2 / 3 与 1.25.0 一致但文件是 `$i0` → 账本，FAIL 基线 23→25）、nf-live 484→491（synchronous/ 下
          BackSample / Hold / NoClock / Previous / ShiftSample / SubSample / WhenClocks）；+1 单测（197）。
    - [x] (59o) 归约式 + min / max 的各种形态（2026-10-02）：
          **归约式** `sum / product / min / max(e for i in r, …)` 按 v1.25.0 的 simplifyReduction 重做（替掉只会单迭代变量整数
          范围的 reduction_chain）：单迭代变量长度 0 → 缺省值、长度 1 → 体代入；变率 ≤ 结构参数的求值；sum / product 各范围能
          展开就展开成链（最后写的迭代变量在最外层），否则保留；min / max 保留成归约式（多迭代变量打成嵌套、范围打成字面量
          范围 / 原样数组 `1:3`、`{1, 3}`、`x[1:2]`、`1:integer(time)`）。迭代变量类型 = 范围元素类型（Integer / Real /
          Boolean），范围可以是数组字面量 / 数组变量。**min / max(数组)**：整体 / 切片引用原样、算术式展开成字面量、恰一个
          元素取元素、字面量求值、空数组取类型极值（Real ±8.777798510069901e304、Boolean、枚举）。**min / max(a, b)** 支持
          Boolean / 枚举。Integer 极值随 omc 构建的整数位宽而变（本机 1073741823、文件 4611686018427387903）→ SKIP，记入
          open-questions 待定（挡住 CevalReduction1、FuncBuiltinMax / Min / Max2 / Min2、FuncBuiltinReduction 六例）。Real
          范围、枚举迭代变量、`vector(x)` 这类 omc 留调用的实参 → SKIP。
          **顺带修潜伏错**：记录构造绑定 `R t = R(a = p)` 里没给实参的字段，绑定引用别的字段时 omc 代入实参式
          （`t.b = 2.0 * p`、final 的 `t.y = p / 2.0`），ymc 此前出 `2.0 * t.a` → SKIP（constant 记录不受影响）；`to_real` 把
          Real 数组引用包成 `/*Real*/(x)`（`{x, x}` 的元素）→ 不再包。
          探针 RD r1–r5 255 行：228 PASS / 20 SKIP / 7 BOTH-REJECT / 0 FAIL。→ 全 scodeinst PASS 475→477（CevalMax1、
          CevalMin1）、nf-live 491→494（MaxArray、NonConstantReduction、Reductions）；+2 单测（199）。
    - [x] (59q) 参数数组的结构求值 + 吸收律 + 收尾折叠 + 归约范围求值（2026-10-02）：
          **参数数组**（内建类型）在结构语境（下标、维、条件组件的条件、if 条件、for 范围 …）被引用：求整个数组的值再取下标，
          标记是组件级的——所有元素一起 final（`parameter Integer idx[2] = {2, 1}; Real y = x[idx[1]]` → idx[1]、idx[2] 都
          final）。结构参数数组的整体 / 切片引用（omc 代入整个数组的值）未做 → SKIP。
          **吸收律**（simplifyLogicBinaryAnd / Or，NFSimplifyExp.mo:1178-1242 v1.25.0）：`true or e` → true、`false or e` → e、
          `true and e` → e、`false and e` → false（两序）；标量化出来的 and / or 不再化简（omc 先化简后标量化：
          `v[1] = true or b[1]`）。
          **收尾折叠**（原来只在 cardinality 代入后跑，现在总跑，方程 + 语句）：方程 assert 条件恒真 → 删（语句的保留）；结构
          参数代入后成字面量的 if 条件选支；体为空的末尾分支删去、if 方程全空整条删去；空 for 语句 / 空算法段删去。
          **归约范围**（修 59o 的潜伏错——当时的探针只铺了"元素是引用 / 字面量"的范围）：sum / product 展开前 omc 对范围做
          Ceval.evalExp（foldReduction2，NFExpression.mo:5885 v1.25.0）——参数式取值代入、不打结构标记（`sum(x[i] for i in
          k)` → `x[1] + x[2]`、k 不 final），变量 / 迭代变量的裸引用原样，元素是含变量的运算式 / 转型时求值失败 → 整个保留；
          保留形态的范围 = 化简后的范围（字面量逐叶转型 `{/*Real*/(m), 2.5}`、算术式逐元素展开 `{2.0 * x[1], …}`）。
          **不跟 / 未做 → SKIP**（账本）：omc 标量化删掉"其后还有非空分支"的空 if 分支（改了条件语义）；范围元素里含变量的
          调用（`der(t)` 求成 0 等怪相）；fill 的维 / 归约范围 / 推导式范围里有迭代变量（omc 定型期不求值、不标记，
          FuncBuiltinFill3 同因）；Real 下标（omc 报错）。
          探针 AP p1–p11 + ff + ee + ut 338 个：273 PASS / 55 SKIP / 10 BOTH-REJECT / 0 FAIL。→ 全 scodeinst PASS 477→480
          （IfEquation5、IfEquationEval4、dim19）、nf-live 494 不变；+3 单测（202）。
    - [x] (59r) 维里的 size / end、整段范围切片、when 体里的数组等式（2026-10-02）：
          **维**：`size(a, k)`，a 的第 k 维写的是 `:`（由绑定推断）→ 取 a 已发射的形状（a 须先于引用处发射）；size 只看类型，
          不把 a 标成结构参数。本数组的别的维可引用自己的 `:` 维（`parameter Real A[:, size(A, 1)]`，StateSpace 的写法：
          先定 `:` 维再定其余）。继承来的组件（声明在基类里）按求值帧的实例找。下标里的 `end`（`Real c[b[end]]`）。类型名的
          size（`size(E, 1)`、`size(Boolean, 1)`、`Real e[E]` 的 `size(e, 1)`）。
          **整段范围切片**（修潜伏错）：恰好是 `1:dim`（无步长 / 步长 1）的范围等价于 `:`（Subscript.simplifySlice，
          NFSubscript.mo:893-913 v1.25.0）——`y = x[1:3]` omc 打 `y = x[:]`，ymc 此前照写。下标数少于维数时 omc 拿末尾的维去
          比（simplifyList 的 List.lastN）：`m[1:2]`（m 是 [2, 3]）原样留下，照做；反过来部分切片恰等于末尾维时 omc 打成 `:`
          （改了含义）→ 不跟，SKIP（账本）。
          **when 方程体里的数组等式**（修潜伏错）：omc 逐元素标量化（scalarizeWhenEquation，NFScalarize.mo:401-424），ymc 此前
          把 `y = x` 整条留在 when 体里。含数组调用的等式未核实 → SKIP。
          探针 DM s1–s5 + misc + ut：见 tests/probes/dm-*.probes，0 FAIL。→ 全 scodeinst PASS 480→482（DimUnknown8、
          RecordBinding3）、nf-live 494→499（PolynomialEvaluatorA / B、QualifiedEnd、Modification12 / 13）；+2 单测（204）。
    - [x] (59s) 新对拍语料（simulation/modelica、flattening/modelica 无注解的）撞出的潜伏错（2026-10-02）：
          **工具**：`tools/nf_live_sweep.py --all` 把没有命令行选项注解的 .mo 也收进来（原样跑）→ 任何装着模型的目录都能当
          实跑 omc 的对拍语料。simulation/modelica 379 个模型：PASS 151 / FAIL 0 / ACCEPTS-INVALID 0；flattening/modelica
          全部 2506 个：PASS 1017 / FAIL 0 / ACCEPTS-INVALID 3（全在账本：ConnectArrayCond、DimUnknown17、InnerOuter13）。
          两条都进每桶收尾的门（FAIL / ACCEPTS-INVALID / CRASH 只许降）。
          **修**：(1) Integer / Real 混合的范围（Algorithm.mo `for i in 1:n`，n 是 Real）——omc 的 typeRange 把 Integer 的一方
          转成 Real（`1.0:n`、`/*Real*/(k + 1):n`）；有步长时范围类型取"起点原类型 × 步长"的结果，于是 `1:2:n` 打成
          `1.0:2:n`、迭代变量按 Integer 用。(2) 负底数配非整数指数的幂：omc 化简不求值、原样留下 `(-1.0) ^ 0.5`，ymc 此前
          折成 NaN；constant 绑定里要求值 → omc 报错（RealPow2），0 的负数次幂同样报错。(3) extends 的基类名路径上有
          replaceable 的类（InvalidReplaceableExtends3 / 4，omc 报错、ymc 此前放行）→ flatten 里先挡住（归 inst，T-S3-07）。
          探针 DM s6 / s7 / ut2：0 FAIL；+1 单测（205）。
    - [x] (59t) 整个 testsuite 当对拍语料 + 驱动 / 打印的两处潜伏错（2026-10-02）：
          `python tools/nf_live_sweep.py --all --dir <testsuite>`：3946 个 .mo（flattening 2506 + simulation 379 +
          openmodelica 793 + 其余），一分钟跑完，PASS 1234 / FAIL 0 / ACCEPTS-INVALID 3（账本）/ CRASH 0 —— 取代 59s 的两条
          分目录扫描，进每桶收尾的门。openmodelica 目录第一次跑出 1 FAIL + 16 ACCEPTS-INVALID：
          (1) 次正规 Real 字面量（parser/RealLiterals1）：omc 的 Ryu 打最短往返串 `5e-324`，ymc 从 15 位有效数字起试、打成
          `4.94065645841247e-324` → expr_dump 对次正规数从 1 位起逐位试。(2) `within P;`（P 非空）的文件单独压平：omc 报
          "Failed to insert class M within P"，ymc 此前无视 within 照常压平 → 驱动里拒绝（退出码 1）。
          探针 drv-misc 4 个；+1 单测（206）。
    - [x] (59u) 无返回值的调用 + 数组算术式实参的展开 + 函数解释器的 assert / 调用语句（2026-10-02）：
          **无返回值的调用** `f(args);`：方程（含 for / if / when 体、initial equation）、模型算法语句、函数体语句都留成
          调用（缺省 / 命名实参补齐、注释保留、带输出的函数也能当语句调）；实参全是常量的纯函数调用被 omc 求值掉、整条
          消失（NFSimplifyModel 的 NORETCALL 支），impure / external 的照留。语句位置的向量化调用
          （`array(f(x[$i1]) for $i1 in 1:3);`）未做 → SKIP。
          **数组算术式作函数实参**（修潜伏错，侧探撞出）：维已知时 omc 的化简按元素展开成字面量——`g(2 * x)` →
          `g({2.0 * x[1], 2.0 * x[2], 2.0 * x[3]})`，矩阵、参数数组、等式 / 语句 / when 体里同样；ymc 此前照写 `g(2 * x)`。
          `vector(x)` / `diagonal(x)` 作实参 omc 留成调用 → SKIP。
          **函数解释器**：认 assert（条件成立继续；不成立 → 求值失败）与调用语句（被调函数求值一遍，没有输出的也行）→
          `Real y = g(2)`（g 的体里有 assert / `f(u);`）折成字面量。实参全是字面量的纯函数调用若 ymc 没求出来而留了下来
          （omc 要么求值、要么求值失败照留，分不清）→ 一律 SKIP（此前只对结构参数代入出来的调用判）。常量值按形参的内建
          类型强转改成严格匹配（String 给 Real 形参 = 类型错）。
          **函数体里的数组运算**（FA 探针 f1 铺出 31 个潜伏 FAIL，下一桶逐条实现，这桶先挡住 → SKIP）：点号算符留点号
          （`u .* u`、`u .+ a`、`a .- u` …）、`标量 * 数组` 换成 `数组 * 标量`、数组自己减自己 omc 报错、已知维的标量积
          展成积和；已知维的取负按元素展开已做。会让 omc 出告警的写法照旧 SKIP（函数变量带 stream 前缀、求值时触发的
          warning 级 assert）。
          探针 NR n1 / n2 / c1、GA g1 / g2 / misc / ut、FA f1：0 FAIL。→ 全 scodeinst PASS 482→484（FuncUnknownDim1、
          FunctionNoOutput1）、整个 testsuite PASS 1234→1238；+2 单测（208）。
    - [x] (59v) 函数体里的数组运算 + 表达式括号规则（2026-10-02）：
          **括号规则**（修潜伏错）：优先级相等时右操作数看**它自己**可不可结合（Expression.shouldParenthesize :10751-10786
          v1.25.0），ymc 此前按外层算符判——`a < b + c` 被打成 `a < (b + c)`（关系式右边是加法这么基本的写法，语料与
          探针里竟然一个都没有）。不可结合的逐元素数组积 `.*`、向量 / 矩阵积作右操作数加括号（`u * (u .* u)`）。书写出来
          的双重否定 `not (not p)` → `p`（simplifyLogicUnary）。
          **函数体里的数组运算**（59u 先挡住的 31 + 个潜伏 FAIL 逐条实现）：维未知、没能按元素展开的按 DAE 算符类别定
          形——标量挪到右边（`2 * u` → `u * 2.0`、`a .* u` → `u * a`）、数组 .* / ./ 数组与 .^ 留点号、`a .+ u` → `u .+ a`、
          `u .- a` → `u .+ (-a)`（`u .- 2` → `u .+ -2.0`）、标量 .- / ./ 数组留点号、不做恒等化简（`u * 1.0`、`u .+ 0.0`）、
          Integer 数组整体转型 `/*Real[:]*/(n)`。维已知的照旧按元素展开（`a .- w`、`w .^ 2`、`-w`）。标量与数组之间不带
          点号的 + / -、标量 / 数组是类型错（此前放行）。FlatModel 记下打点号 / 不可结合的二元运算节点，dump 时查。
          未做 → SKIP：已知维的向量积展成积和、已知维的 Integer 数组逐元素转型、数组自己减自己（omc 报错）。
          探针 FA f1–f4、PP：0 FAIL。→ 全 scodeinst PASS 484→485（OperationSubEW2）、整个 testsuite PASS 1238→1239；
          +2 单测（210）。
    - [x] (59w) 内建调用的化简规则 + arena 悬垂引用修复（2026-10-02）：
          **内存安全（修潜伏错）**：flatten 途中往 Inst 的 absyn / scode arena 合成节点（补字面量下标的 cref、拆出的字段
          修饰），而 inst_exp 一路按引用拿着 `const Exp&`——arena 一扩容就悬垂。`R r[400]; Real y[400] = r.x + r.x;` 在
          ASan 下 binary() heap-use-after-free（此前两套语料 + 探针都没撞上：要正好在合成时用完 vector 的余量）。
          现在构造 Flattener 时给三个 arena 各预留 65536 个位置，合成只走 synth_exp / synth_cref / synth_mod，预留用完 →
          SKIP `too many synthesized nodes`（上限写在 ponytail 注释里；要去掉得把 Arena 换成不搬家的容器，base/ 归用户）。
          **化简规则**（ES 探针 e1–e9 共 571 行，按 simplifyBuiltinCall / evalBuiltinCall 的规则表铺）：`smooth(p, 字面量)` →
          字面量、`pre(字面量)` → 字面量（含常量引用、代入后的结构参数）；semiLinear：x 为零或两斜率为零 → 0.0，斜率
          相同 → `x * m1`（积不再化简：`a * 1.0`），实参全字面量 → 原样（Ceval 没有这一支）；edge 的实参须是变量
          （`edge(a > 0)` 此前放行）；向量化的 semiLinear 会触发化简的形态 → SKIP。
          **写法**：`array(a, b, …)` = `{a, b, …}`（求值与实例化两条路）；String + String 的结果类型是 String（三段以上的
          拼接此前 SKIP）。
          解析器放行了 omc 的语法错 `{…}[i]` / `f(x)[i]`（下标只能跟在括号式后面）→ 任务卡 T-S3-08（用户）。
          → 整个 testsuite PASS 1239→1241（gc2980、StringTest）；+3 单测（213）；探针 2837→3422。
    - [x] (59x) 模型算法段的整数组赋值 + 数组值的整体保留形态（2026-10-02）：
          **整数组 / 切片赋值** `y := <数组式>`、`y[1:2] := z`、`y[1, :] := z`（此前一律 SKIP `algorithm assignment to
          non-continuous variable`）：语句不拆（scalarizeStatement 只下到 for / if / when / while 的体里），右侧规整成整体保留的
          形态——把 Var 数组绑定方程的那张规则表抽成 whole_array_value 两边共用：整取 / 切片引用、if、向量化调用、数组调用、
          Boolean 的 and / or / not 原样；算术式逐元素展开；字面量逐叶转型、里面的数组值元素各自规整；范围 `1.0:3.0`。
          **共用之后撞出绑定路径的 4 类潜伏错**（WA 探针 w1–w5 共 308 行）：① 数组运算在数组层做了恒等化简（`Real y[2] =
          z * 1` 打成 `y = z`，omc 先展开再逐元素化简 → `{z[1], z[2]}`；`0 * z` 折成标量 0）；② `.^` 的结果类型留成 Integer
          （`m .^ 2` 打成 `/*Real*/(m[1] ^ 2)`，应为 `/*Real*/(m[1]) ^ 2.0`；`sum(m .^ 2)` 多套一层转型）；③ Real 数组字面量里
          的 Integer 数组元素打成标量转型 `/*Real*/(m)`（应为 `/*Real[2]*/(m)`）；④ 带步长的切片按选中的下标重算终点
          （`w[1:2:4]` 打成 `w[1:2:3]`）。另补：Integer 整取 / 切片进 Real 数组整体转型（绑定、赋值、if 分支）。
          未做 → SKIP：数组 if 被逐元素标量化且分支是 Boolean 运算（omc 打 `(not j)[1]`）、左侧切片含迭代变量 / 越界、
          字符串数组拼接、数组调用嵌在算术式里。
          → 整个 testsuite PASS 1241→1247（JoinThreeVectors2、ArrayIndex ×2、ArraySlice、AlgorithmArrayEqn、Bug1872）；
          +2 单测（215）；探针 3422→3739（新收的 317 个在上一提交的二进制上：FAIL 27、ACCEPTS-INVALID 1）。
    - [x] (59y) when 的条件与体、assert / terminate、数组构造子的化简、注释（2026-10-02）：
          先给 `tools/nf_live_sweep.py` 加了 `--skip-oracle`：SKIP 的用例也跑 omc，按"omc 能压平 / omc 也拒绝"分开计——
          1891 个 SKIP 里只有 912 个是真余量，排桶按它来（flat 侧最大的几项：限定名引用 19、非字面量维 17、限定名 / 具名
          实参的函数调用各 13、when 条件 13、整取引用 12）。本桶做 when：
          **when 条件**：白名单换成"任意标量 Boolean 式 / 条件向量，照化简后的式子打"（WC 探针 c1–c4）；只挡 initial() 不作
          整个条件（omc 告警）与 noEvent 包 Boolean 式（omc 报"不是离散时间表达式"）。
          **when 体**：assert / terminate；整个数组 / 元素 / 切片的 reinit（值走 whole_array_value）；数组元素、切片作左侧，
          各分支解出的变量按**元素**比（WhenVariableMismatch2）；算法段的 terminate 语句。when 解出 / 赋值的变量若声明绑定
          引用了变量或 time → omc 报"离散变量绑了连续的值"，此前放行。
          **assert / terminate**：方程里的消息 omc 不化简（`"a" + "b"`、`String(1 + 2, 0, true)` 原样），语句里的化简——此前
          一律化简；显式 level 字面量 `AssertionLevel.warning / error`（方程、语句、函数体）；函数体里的 assert 此前因为把
          缺省 level 当成"显式 level"而整个被挡（8 例）。
          **放开之后撞出的潜伏错**：① 函数形参 / 局部的字符串注释整个丢了、别名函数拿了被别名函数的注释；② 注释解了
          两遍转义（`"a \\n b"` 打出真的换行）、多行注释的续行缩进没补；③ 单迭代数组构造子：范围只有一个元素时 omc 展开
          成 `{体}`、体是"数组字面量取迭代下标"时按范围取元素、`for i in {1, 3}` 打成了 `1:3`、带步长的范围重算了终点；
          ④ 引号里带非 ASCII 字符的标识符 omc 词法器不收（flatten 先挡，根治在 lexer → 任务卡 T-S3-09）。
          未做 → SKIP：类注释里的引号 / 反斜杠（omc 二次转义）、when 体里的 if 方程、Integer 的 reinit、非字面量的 level。
          → 全 scodeinst PASS 485→488（Assert3、Reinit2、When8）、整个 testsuite PASS 1247→1269；+4 单测（219）；
          探针 3739→4296。
    - [x] (59z) size 的实参形态 + 函数体里的数组切片（2026-10-02）：
          **size / ndims**：实参的形状看穿数组字面量 / 范围的粗类型（`size({1, 2, 3}, 1)` → 3，此前字面量被当成"不是数组"）；
          维号不是常量时原样留调用（`size(m, d)`，参数不标结构）；求值路按构造子的嵌套层数（`Real y[size({1, 2, 3}, 1)]`）；
          函数体里 `size(2 * u, 1)` 的实参照常定形（→ `size(u * 2.0, 1)`）、维未知时 `size(u)` 整个留着（被运算 / sum /
          scalar 用到再按维展开）、`ndims(u)`。
          **函数体里的切片**（此前 `function array slice` 一律 SKIP；MSL 的函数里到处都是）：下标照写、留下一维——`:` 留
          原维，界是字面量的范围大小已知（`w[2:end]` → `w[2:3]`，恰是整段的打成 `w[:]`），否则未知（`u[1:k]`、
          `u[1:size(u, 1)]`）；下标向量 `w[{1, 3}]`、`u[idx]`；左侧切片 `v[1:2] := …`。已知大小的随运算按元素展开，未知的
          按算符定形。连带做了：已知大小的向量 / 矩阵积展成积和（59v 留下的 SKIP）、`sum` / `product` 的实参能展开就展开、
          `fill` 的展开、Integer 数组进 Real（赋值位置整体转型 `/*Real[2]*/(n2)`，作运算数逐个转）、数组 if 的分支转型与
          逐元素展开、混合字面量里的 Integer 叶转型（`{x, 2} .* n` → `2.0 * …`）。
          未做 → SKIP：切片上的向量化内建（omc 打 `abs(w[(1:2)[$i1]])`）、函数体里的 transpose、内层维未知的字面量取
          第 2 维（omc 报错）。
          → 全 scodeinst PASS 488→494（FuncBuiltinSize、Size2、const8、loop3、OperationUnary2、OperationLogicalUnary2）、
          整个 testsuite PASS 1269→1277；+2 单测（221）、改 2 个旧单测（原先断言 SKIP 的写法现在做了）；探针 4296→4629。
    - [x] (60a) 状态机算子 + 函数调用 SKIP 的子原因（2026-10-03）：
          **排桶工具**：用户函数调用没接住时（`function: f` / `qualified function call` / `named function argument` 这三个笼统
          原因共 60 多例）把卡在哪一步记成子原因，driver 打在 `[…]` 里（`function: f [record constructor]`），
          `nf_live_sweep --skip-oracle` 按它分组；主原因与单测不变。分出来最大的一组是状态机算子（12 例，原因写的是
          `function: initialState` 等）。
          **状态机算子**（MLS §17.1）：1.25.0 的新前端原样留调用——`initialState(s)`、`transition(from, to, condition,
          immediate = true, reset = true, synchronize = false, priority = 1)`（缺省实参补齐成 7 个位置实参、具名实参按名
          入槽）、`activeState(s)` → Boolean、`ticksInState()` → Integer、`timeInState()` → Real。状态实参是类实例的引用
          （沿实例树解析、带前缀打印 `w.w1`、实例数组取元素 `sa[1]`）；内建类型的变量 omc 报错。initialState / transition
          只能单独成句（方程、算法、if / when / for 体里都行）。
          未做 → SKIP：实例数组整体作状态（omc 打 `array(initialState(sa[$i1]) …)`）、记录实例作状态（omc 连带发射构造
          函数）、下标是参数的状态（结构参数）。
          → 整个 testsuite PASS 1277→1293（statemachines 下 15 例 + interactive_api_calls）；+1 单测（222）；探针
          4629→4738。
    - [x] (60b) 隐式迭代范围 + 数组维的求值缺口（2026-10-03）：
          **隐式迭代范围**（deduceIterationRange，NFTyping.mo:3735-3881 v1.25.0）：`for i loop …`（语句 / 方程）、
          `sum(x[i] for i)`、`{x[i] for i}`。按 omc 的次序收集体里迭代变量**恰**作下标的各处（iter_uses：赋值先左后右、
          for 先体后范围、表达式后序、cref 末段在前），相邻两处的维须相等，范围 = `1:末`、末取自最先收集到的那处：模型里
          各维已是整数 → `1:n`（语句打 `for i in 1:3 loop`，方程照常展开，保留的归约 / 推导式打 `for i in 1:3`）；函数体里
          `:` 维 → `1:size(u, 1)`、表达式维 → 维表达式（`Real w[k]` → `1:k`）、`size(u, 1)` 维与 u 本身算相等。合成的范围
          挂在 Reduction / for 的**拷贝**上（同一个类的不同实例推出的范围可以不同，arena 里的不改）。内层同名迭代变量的
          用处不算外层的。此前只有 for 方程一处、且体里有归约就 SKIP。
          **潜伏错**：留成符号的下标没化简（`for i in 1:3 loop y[i] := x[i + 0]` 打成 `x[i + 0]`，omc 是 `x[i]`；归约 /
          推导式里同）——显式范围也错，语料没碰到，隐式范围的探针带出来的。
          **维的求值缺口**：① 经实例路径引用的值——子实例的常量、**字面量绑定**的参数（`Real z[a.j]`、`a.p`、`c[2].j`、
          记录参数的字段 `r.n`）：按当前帧前缀查已发射变量的值；参数随之成结构参数，标记是组件级的（数组实例 / 参数数组
          的各元素一起 final）。子实例数组的 `size(a.v, 1)` 取已发射的形状（实例修饰改过的维也对）。② `ndims(a)`。③ 矩阵
          字面量 `[a, b; c, d]`（标量 / 向量 / 矩阵元素的拼接，`Real y[max([n, m])]`、`constant Integer M[2, 2] = [1, 2; 3, 4]`）。
          ④ 多迭代变量的推导式绑定（`parameter Integer s[:, :] = {i + j for i in 1:3, j in 1:2}`）：`:` 维取各范围长度、
          逐元素代入。
          **变率沿多段名下沉**：`a.p`（类实例里的参数）此前按首段 `a` 判成连续量——`parameter Integer k = a.p` 被当成
          更高变率的绑定（SKIP）、`y[a.p]` 的下标当运行时变量原样留着（**错**，omc 是 `y[2]` + `a.p` final）。五个判定
          （refs_runtime_var / refs_param_var / cond_is_runtime / absyn_refs_param / absyn_refs_plain_param）共用
          cref_path_var / absyn_cref_var。
          **探针带出的既有错**（都不在语料里）：`annotation(Evaluate = false)` 的参数被照常求值（omc 是
          NON_STRUCTURAL_PARAMETER：维留成 `Real[p] z`、下标留 `y[p]`，for 范围 / 条件组件 / fixed 报错）→ 现在不求值，
          命中即 SKIP / 拒绝；经实例路径读 protected 常量被放行（`constant Integer k = f.p`、`1:f.p`、`y[f.p]`）→ 拒绝；
          `constant Integer n = ndims(x)`、`constant Integer m[2] = size(x)` 被放行（omc：ndims / 单实参 size 总是参数
          变率，绑给 constant 报错）→ 拒绝；推导式绑定比声明维长（`parameter Integer s[2] = {i for i in 1:3}`）被放行 →
          拒绝。
          未做 → SKIP：枚举 / Boolean 维上的迭代（显式范围 `for i in E loop` 也没做，ImplicitRangeType1 / 2）、函数体里的
          归约 / 推导式（ForIterator1）、嵌套类经词法作用域引用外层实例的常量（PackageConstant6 `model B Real x[a.n]; end B`）、
          非字面量绑定的子实例参数（`A c(p = n); Real z[c.p]`）、前向引用的实例（`Real z[d.p]; A d;`）、`:` 维由函数调用
          绑定定（ArraySizeFromFunc `parameter Real x[:] = f(n)`）、when 方程体里的 for。
          → scodeinst 494→501 PASS（CevalRecord6、DimSize5、ImplicitRangeAlg1、ImplicitRangeExp1 / 2、SubscriptReduction1、
          const1 入 slice）；整个 testsuite PASS 1293→1308；+4 单测（226）；探针 4738→5159。
    - [x] (60c) 类型维（枚举 / Boolean）数组的引用 + 在枚举 / Boolean 上迭代（2026-10-03）：
          此前 `Real y[E]`、`y[Boolean]` 只能声明：任何引用（`y[E.one]`、整体 `y`、`sum(y)`）与任何类型范围上的 for / 归约都
          SKIP（60b 排桶时记下的"枚举 / Boolean 维上的迭代"）。
          **表示法**：类型维数组的元素下标在压平期一律按序数（IntLit：false 1、true 2，枚举字面量 = 序号）走——标量化、
          切片、展开这些下标的生成点都不用知道维的种类；key 编码（conn_key / expr_cref_key / emitted_shape）同样按序数；
          收尾 relabel_type_dims 按已发射变量的标签把全部 Cref 节点里落在类型维上的整数下标（含切片的界、下标数组的
          元素）换回标签。书写的标签下标在 cref 定型时核对类型（Boolean 维收 Boolean、枚举维收同一枚举）并转成序数；
          整数下标进类型维 / 标签进整数维 → omc 类型错，拒绝。
          **引用**：`y[E.two]`、`b[true]`、`y[end]`、切片 `y[E.one:E.two]` / `y[{E.three, E.one}]`、整体引用与数组运算 /
          数组方程 / 语句、constant 元素取值、子实例与记录字段里的类型维数组、Boolean 维上的运行时下标 `b[a > 1]`。
          **迭代**（iter_values / label_iter_values）：范围是类型名（`for i in E`、`for b in Boolean`）、标签范围
          （`E.one:E.three`、`false:true`）、标签数组；迭代变量的值是标签（loop_labels_，序数仍在 loop_vars_）。
          for 方程展开成 `y[E.one] = …`、`x[i] = i` → `x[E.one] = E.one`；for 语句保留（类型名打成 `{E.one, E.two, E.three}`，
          范围照写）；归约 sum / product 展开、max / min 保留；推导式保留 `array(y[i] for i in {…})`、参数 / 常量逐元素。
          类型维上的隐式范围 = 首末标签 `E.one:E.three`（Dimension.lowerBoundExp / endExp）。体里类型维上的符号下标须是
          同类型的迭代变量（Boolean 维收任何 Boolean 式）。
          **`:` 维的类型**：推导式里写类型名的范围定出来的是类型维（`Real z[:] = {… for i in E}` → `z[E.one]`…），标签范围
          给整数维；引用带类型维数组的绑定（`Real z[:] = y`）omc 也给类型维，未做 → SKIP。
          **潜伏错**：范围为空的 for 语句没删（`for i in 3:1 loop … end for;` 照打，omc 整条删去，simplifyStatement
          NFSimplifyModel.mo:312-333；整数范围同样错，函数体里也是）。
          未做 → SKIP：`:` 维由带类型维的绑定定、constant 类型维数组的切片 / end / 作维、枚举**变量**作下标、类型维的
          复合数组（`R r[E]`，EnumArrayMod1）、函数形参的类型维（`input Real u[E]`）、连接器里的类型维成员、StateSelect 维、
          空的标签推导式。
          → scodeinst 501→506 PASS（End1、ForEquationEnum1 / 2、ImplicitRangeType1 / 2 入 slice）；整个 testsuite PASS
          1308→1325；+4 单测（230）；探针 5159→5357。
    - [x] (60d) 复合数组的元素引用：洞 / 切片 / 数组字段 / 多层 / 符号下标（2026-10-03）：
          此前只有两种形态：首段带取尽各维的字面量下标（`a[3].x`）、两段式的"复合数组取标量字段"（`r.x`）；中间段带下标、
          切片 `a[:].x` / `a[1:2].x`、数组字段 `a.v`、多层 `b.q.x` 都 SKIP（`composite array element reference`，语料 9 例）。
          **展开**（expand_composite_path，omc 的 ExpandExp）：非末段的复合数组段有洞（没给下标 / 下标少于维数 / 切片）时，
          整条引用展开成全标量元素的嵌套数组——各段的洞按段序、维序逐层，末段是数组的也一并展开到标量（`a.v` →
          `{{a[1].v[1], a[1].v[2]}, …}`、`a[:, 2].x`、`b.q[2].x` → `{b[1].q[2].x, b[2].q[2].x}`）；元素合成全字面量下标的
          cref 交回 cref() 照常解析。首段的维在 enclosing 的作用域里求；更深的段只收字面量整数维（子实例里的参数维受实例
          修饰影响，求不准 → SKIP）。
          **中间段的标量下标**：`b[1].q[2].x`、`b.q[2].x`（此前只许首段带下标）。
          **符号下标**：复合数组段上保留着的迭代变量（for 语句 / 保留的归约、推导式）与运行时整型量原样留着——
          `for i in 1:3 loop s := s + a[i].x`、`max(a[i].x for i in 1:3)`、`Real z = a[n].x`（omc 不做越界检查）。
          **der**：`der(a.x)` 在绑定里整体保留 `der({a[1].x, a[2].x, a[3].x})`、等式里随等式逐元素。
          **潜伏错**：语句里原样留着的"复合数组取字段"（`y := a.x`）打成了数组字面量，omc 是
          `y := array(a[$i1].x for $i1 in 1:3)`（语句不标量化）——现在 SKIP（按展开出来的元素 cref 认；逐元素运算过的、
          被 sum 吃掉的照常）。
          未做 → SKIP：语句里的 `array(a[$i1].x …)` 形态、向量化调用的实参（`sin(a.x)` → `array(sin(a[$i1].x) …)`）、
          子实例里参数维的复合数组（`C c(n = 3)` 的 `c.q.x`）、零长度的复合数组、越界的字面量下标（omc 照打
          `a[4].x`，不跟）、参数字段的 der（omc 折成 0）、`size(a.x, 1)`。
          → scodeinst 506→507 PASS（EquationQualified1 入 slice）；整个 testsuite PASS 1325→1330（ArrayReduce2、MatrixEquation、
          QualifiedSlicing、LookupArray）；+1 单测（231）；探针 5357→5506。
    - [x] (60e) 组件数组上的数组值修饰 + 探针铺出来的四处潜伏错（2026-10-03）：
          此前复合数组的修饰只按**字面量**数组逐元素下降（`A a[3](k = {1, 2, 3})`），绑定是别的式子（`k = p`、`2 * p`、
          `fill(…)`、切片、推导式、if 式、用户函数）一律 SKIP（`composite array modifier distribution`）。
          **分发**：array_scalar_elem 的 general 档给落在一般式上的剩余下标合成 `(E)[i, j]`（Absyn Subscript，记入
          split_subscripts_）；subscripted_exp 的一般基把 E 整体定型后取该元素并化简（`(2 * p)[1]` → `2.0 * p[1]`、
          `(p[{3, 1, 2}])[1]` → `p[3]`、`(fill(2, 3))[1]` → `2.0`，推导式 / linspace 代入后化简），数组值的用户函数留成
          `f(p)[1]`；只取前几维的（数组字段 `v = fill(tt, 3, 2)`）下降数组字面量。split_mod_ok 对非字面量绑定按定型后的
          形状核对。字面量里 Integer / Real 混排：数组类型取切分那一层**末**元素的——末元素 Integer 时照常转型（收），
          末元素 Real（前面的 Integer 不转型）/ Real 元素带调用（多打一层 `/*Real*/`）不复刻。
          **书写的 `(E)[i]`** 走同一条路；基是调用 / 构造子 / 矩阵的不收（解析器分不清 `f(x)[1]` 与 `(f(x))[1]`，前者是
          omc 的语法错，任务卡 T-S3-08）。求值器补了 Subscript 的取值（分发下来的值作结构量用）。
          **潜伏错**（60d 及以前都在）：
          · 参差的组件数组：`X xs[2](n = {1, 3})`、X 里 `Real x[n] = fill(time, n)`——omc 内部错，ymc 放行 → SKIP
            （`ragged component array binding`；同维 / 无绑定 / 参数数组 / 参差的复合子数组照常）。
          · constant 的绑定只在结构位置引用参数（`c[n]`、`sum(1:n)`、`q[n] = fill(1.5, n)`、`R(c[n])`）：omc 报"绑定变率
            高于组件"（checkComponentBindingVariability），ymc 经结构求值把参数吃成字面量后放行 → 改在**书写的**绑定上查
            （absyn_refs_param：已知维的 `size(a, k)` 首参除外，归约迭代变量遮蔽同名参数）。
          · `eval_reduction` 把 allow_param 写死成 true：`parameter Real q = f(sum(p))` 被折成字面量、p 连带 final
            （FAIL），omc 留 `f(p[1] + p[2])` → 照调用方的传。
          · 推导式取元素后下标不化简（`sum({x[4 - i] for i in 1:3})` 打出 `x[4 - 1]`）→ clone_subst 的 fold_subs。
          未做 → SKIP：末元素 Real 的混排字面量、嵌在运算里的推导式（omc 留 `(array(…) .* p)[1]`）、书写的
          `(推导式)[k]`、长度不等的切片修饰（1.25.0 放行）、记录函数绑定里只在结构位置出现的参数（omc 挪成初始方程）、
          归约实参里范围 / fill 个数上的参数（`f(sum(1:n))`：此前靠上面那个漏洞碰巧对，现在 SKIP；要收得让这些结构位置
          自带事务地取参数值，typeRange NFTyping.mo:2352-2416 / typeFillCall2 NFBuiltinCall.mo:935-1001）。
          → scodeinst 507→508 PASS（IfExpression16 入 slice）；整个 testsuite PASS 1330→1331；+2 单测（233）；
            探针 5506→5898。
    - [x] (60f) constant 数组的切片 / 符号下标引用 + 书写绑定上的变率检查 + 常量归约的保留（2026-10-03）：
          此前 constant 数组只收"下标全是整数字面量"（`x[2]`）与整取（`x`）；切片、参数下标、运行时下标一律 SKIP
          （`constant evaluation`，语料 6 例），子实例里的（`a.c[n]`、`a.c[2:3]`）更是落到变量路上、打成对常量的引用（FAIL）。
          **引用换成值、下标应用上去**（const_array_ref / apply_const_subs，omc 的 Ceval.evalCref(evalSubscripts = false) +
          Expression.applySubscripts，NFExpression.mo:1242-1478）：求得出的下标（常量；参数按结构参数求值、打 final）取元素，
          `:` 对每个元素应用其余下标，展开的切片对每个下标各做一遍，运行时下标连同其后的留成 `{…}[n, 1]`——
          `x[2:3]` → `{2.0, 3.0}`、`m[:, 2]` → `{2.0, 5.0}`、`x[n]` → `{1.0, …}[n]`、`m[1, n]` → `{1.0, 2.0, 3.0}[n]`、
          `m[:, n]` → `{{…}[n], {…}[n]}`、`x[{n, 1}]` → `{{…}[n], 1.0}`。来源：本地常量、子实例里的、记录字段、包常量。
          **化简**（simplify 的 Subscript 支，simplifySubscriptedExp NFSimplifyExp.mo:1346-1379）："各元素相等"的字面量数组取
          符号下标 → 该元素（fill / ones 绑定的常量 `u[n]` → `1.5`，书写的 `({1.5, 1.5})[n]` 同）；打头的字面量下标部分应用。
          Integer 常量数组进 Real 语境：转型留在外面 `/*Real*/({3, 1, 2}[n])`（const_subscripts_ 标记）。
          **dump**：多下标的 ASUB 以 ", " 相连（ExpressionDumpTpl.tpl:89-95；此前照 ExpressionDump 写成 ","，没有探针走到过）。
          **潜伏错**（60e 及以前都在）：
          · parameter / constant 的绑定、内建属性值引用变量，而化简把它消掉（`0 * tt`、`tt - tt`、`if true then 1 else tt`、
            `false and tt > 1`）：omc 看化简前的绑定，报变率错；ymc 看化简后的 → 放行。改在书写的式子上查（binding_refs：
            实例继承的变率下限 scope_floor、size / ndims 首参除外），60e 的 constant-引用-parameter 检查并进同一个函数。
          · 变率 ≤ 结构参数的归约，体里有外层保留着的常量变率迭代变量（for 语句 / 数组构造子）：omc 求不出值就把归约原样
            留着（`/*Real*/(sum(i * j for j in 1:3))`），ymc 照 S3 展开了（FAIL）；min / max 则 SKIP → 现在都保留
            （var_iters_：迭代变量的变率 = 其范围定型时的变率）。
          · `x[n] - x[n]` 不折 0（expr_equal 不认 Subscript）。
          未做 → SKIP：没取尽各维的符号下标（`m[n]` → 数组值的 `{{…}, {…}}[n]`）、留着的切片（`x[n:n + 1]`）、符号下标后跟
          切片、越界的字面量下标（omc 照打 `{…}[7]`）、求不出值的参数下标（fixed = false：omc 打 final 而下标仍是符号）、
          记录数组的符号下标（`r[n].a`）、包里记录常量的数组字段、类型维上的运行时下标、函数体里的包常量。
          → scodeinst 508→512 PASS（CevalArrayConstant2、SubscriptCevalSlice1/2、SubscriptCevalWhole1 入 slice）；整个
            testsuite PASS 1331→1338（另 MatrixRowIndexing、VectorizeConstCref2D、SubScript3）；+3 单测（236）；
            探针 5898→6343。
    - [x] (60g) connect 实参的表达式下标 + 按实例的连接器数组形状（2026-10-03）：
          此前 connect 实参的下标只收整数字面量 / for 迭代变量本身 / 字面量范围；`p[i + 1]`、`p[N]`、`p[N - 1]`、`p[end]`、
          `p[2:end]`、`p[:]`、`p[:, 1]` 一律 SKIP（`connect operand shape`，语料 5 例，链式连接 `for i in 1:N - 1 loop
          connect(p[i], p[i + 1])` 就在其中）。
          **下标求值**（eval_subscript，与 60f 的常量数组下标共用）：常量 / 迭代变量的算式直接求，含参数的按结构参数求值
          （N 打 final），`end` = 该维大小，范围 / 下标数组逐个；connect 的下标在实例化期就标结构（updateImplicitVariabilityEq
          的 CONNECT 支，NFInst.mo:3982-3986）→ Early（`connect(c[i], d)` 让声明在前、在后的别名都 final）。
          **采集处解析**：实参此前推迟到 run() 末尾才展开，那时实例帧已不在——**潜伏错**：维里的参数按声明的缺省值求
          （`A a(M = 3)` 里的 `C p[M]; connect(p, q)` 只连了 2 对，60f 及以前都是 FAIL）。现在在 collect_equation 里解析，
          各段的维取发射时记下的实际形状（array_shapes_ / note_array_shape：复合数组、叶数组都记），`S s(nPorts = 2)` 的
          `s.ports[end]` 这类深段也对。
          零长度的整取（`C p[0], q[0]; connect(p, q)`）→ 空列表、不产生方程。
          未做 / 不跟 → SKIP：越界的下标（omc 照打不存在的 `p[4].e = q.e`）、往零长度数组里取元素（omc 打 `{}[1] = a.e`）、
          求不出值的参数下标（fixed = false：omc 打 final、下标留成符号）、经父路径整取参差实例的连接器数组（omc 内部错）、
          cardinality 实参带参数下标（omc 恒得 0）、expandable 里声明的数组成员（增量 B）。
          → scodeinst 512→513 PASS（InStreamArray 入 slice）；整个 testsuite PASS 1338→1343（Connect7 / 8、EquationFor4、
            expandable/07）；+1 单测（237）；探针 6343→6465。
    - [x] (60h) 函数体下标里的 `end` 取书写的维（2026-10-03）：
          **潜伏错**（探针铺出来的，60g 及以前都在）：函数体下标里的 `end` 一律打成 `size(数组, k)`；omc 取的是类型里的维——
          书写成表达式的维照写（`output Real y[size(x, 1)]` 的 `y[3:end] := …` → `y[3:size(x, 1)]`，`z[size(x, 1) + 1]` 的
          `z[end]` → `z[size(x, 1) + 1]`，`w[n]` 的 `w[end]` → `w[n]`），只有 `:` 维才是 `size(x, k)`。改由 fn_end_value 取
          fn_dim_exps_ 里的书写维（60b 为隐式范围记下的那张表）。
          顺带铺了模型里的切片（sl-s1）：运行时界的切片（`x[n:n + 1] := …`、`y := x[n:n + 1]`、`sum(x[1:n])`）、空切片
          （`ra[3:1]`）、`end` 算式的 `:` 维参数切片仍 SKIP（`array slice`，语料 3 例），留给后面的桶。
          → 语料无变化；+1 单测（238）；探针 6465→6513。
    - [x] (61a) inst：继承段 + extends 基类名查找 + import（任务卡 T-S3-05 / 06 / 07，2026-10-05）：
          用户暂不手写核心（"目前我不手写核心"），inst 侧的卡改由 Claude 实现。
          **T-S3-05**：inst_class 把基类实例按 extends 序记进实例类树的 exts、去掉 `extends with equations/algorithms` 守卫——
          flatten 的 collect_inherited_sections 早已就绪（段序：本类在前、各 extends 逆序在后，NFInst.mo:2511-2514 /
          NFSections.mo:192-209）。
          **T-S3-07**：extends 的基类名从扩展类**自身**查（NFInst.mo:775-776 `scope := InstNode.parent(ext)`，extends 节点的
          parent 是扩展类本身）——看得见本类的嵌套类（`package Modelica … extends Icons.Library`）；同时补"基类名各段须
          传递地非 replaceable"（checkReplaceableBaseClass，NFInst.mo:842-883）。
          **T-S3-06**：lookup_simple_class 每层在本地类之后查这一层的 import（import_class：限定 / 重命名 / 组导入优先于
          `.*`，同档两个同名项 = 冲突、用到才报错，目标从顶层查，import 不被继承——与 flatten 的 import_lookup 同一套规则）；
          顺带补上 encapsulated 查找边界（NFLookup.mo:465-470：本层找不到只剩内建类，Encapsulated3 由此不再被放过）。
          新探针 87 个（继承段 26 / import 43 / extends 查找 15 / 单测原文 3），无 FAIL / ACCEPTS-INVALID；60h 上其中
          1 例是 FAIL（本类嵌套的 D 与顶层 D 同名：extends 从外层查、错取了顶层的 D）。
          → scodeinst 513→520 PASS（ExtendSections、ExtendImport1、ImportNamed1/2、ImportQualified1、ImportUnqualified1/4
            入 slice）；整个 testsuite PASS 1343→1369（FAIL 0，ACCEPTS-INVALID 3 不变）；+1 单测（239）；探针 6513→6600。
          仍缺（下一批）：短类别名到长类（`model T = M`，short class aliasing composite，语料 36 例）、继承来的类不进查找表
          （`extends B; … extends D`，D 来自 B）、`extends` 短类、import 来的枚举类型（flatten 的 enumeration type path）。
    - [x] 任务卡 T-S3-07（Claude 实现于 (61a)，inst）：extends 基类名从扩展类**自身**查（inst.cpp:215 / :286 现从 parent_scope 起查，漏掉
          扩展类自己的嵌套类；NFInst.mo:780-781 `scope := InstNode.parent(ext)`，extends 节点的 parent 是扩展类本身）+ 基类须
          传递地非 replaceable 检查。`unresolved base class / component type` 147 例逐例归因（72 例首因是本 bug，22 import、
          25 预定义类、14 短类别名前缀、7 继承类查找…）记在卡里 → `docs/tasks/T-S3-07-extends-lookup-scope.md`。
    - [x] (61b) inst + flat：短类别名到长类（2026-10-05，用户暂不手写核心，Claude 实现）：
          `model B = A(k = 2); B b;`（MLS §4.5.1：短类定义 = 基类 + 修饰）不再 SKIP——inst 实例化链底的长类，链上各短类的
          修饰连同其定义所在的类记进组件（derived_mods / derived_scopes）；flatten 的复合组件分支把它们按最低优先级并进
          组件的修饰（外来的、组件自己的在前，外层短类在前），值在短类定义处解析（定义在组件所在的类 → 同组件自己的修饰；
          定义在包里 → 包作用域；别处 → SKIP）。随之补上被这条 SKIP 挡住的检查：
          ① 链底长类的性质：partial（PartialType2、packages/Import1/3/4 的 `model E = B.C(x = 6)`）、受限类型不一致
          （`class C2 = C`，Connect3 "c1 is not a valid connector"；`type B = A(…)` 1.25.0 整个丢掉 b）、inner/outer 的
          短类定义（InnerOuterClass1 等）→ SKIP；
          ② 短类修饰里的重复修饰（DuplicateMod7）、final 元素又被外层改（"final element override"）→ SKIP；
          ③ 记录整体绑定：短类修饰只留属性（构造是链底长类的，`record S = R(a = 5); S s = R(b = 4)` → s.a = 1.0）；
          折叠路（emit_record_fields 不看修饰）遇短类修饰不走（RedeclareRecordComponent1 丢了 redeclare 带来的 quantity）；
          经带修饰的别名调构造（`S(b = 4)`）不求值（别名修饰是构造缺省值，omc s.a = 5.0）。
          顺带修了探针揪出的三处旧错（与短类无关）：
          ④ 记录字段缺省值里的 time：omc 把记录类的缺省构造在函数上下文里定型（instRecordConstructor，NFInst.mo:2537-2539 /
          2606-2609；NFTyping.typeCref :1977-1985）→ "time is not allowed in a function"，ymc 此前照收 → SKIP；
          ⑤ replaceable 组件的类型本身是 partial：1.25.0 照样报错（bad_type_path 此前放过）；
          ⑥ 折叠出的常量 / 参数记录字段漏了声明注释（records/Derived1 `res.T = 1.0 "temperature"`）。
          另：没实例化过的组件（包常量等 CompDef）建出来就带声明的变率 / 方向 / 连接器前缀（包里的短类修饰引包常量时
          此前被当成变量 → 误报 "binding of higher variability"）。
          新探针 114 个（sc-s1 27 / sc-s2 40 / sc-s3 32 / sc-t 11 / 单测原文 4）：PASS 64、SKIP 25、BOTH-REJECT 25，无 FAIL /
          ACCEPTS-INVALID。
          → scodeinst 520→523 PASS（ExtendsShort3、RecordExtends1 入 slice；ExtendsShort1 原在 slice 里，SKIP → PASS）；
            整个 testsuite PASS 1369→1378（FAIL 0，ACCEPTS-INVALID 3 不变）；+1 单测（240）；探针 6600→6714。
          仍缺（下一批）：整个 testsuite 里原 "short class aliasing composite" 的 56 例，9 例 PASS，其余各归其因——受限类型
          不一致 9（多是 `connector RealInput = input MyReal`，MyReal 是 extends Real 的长 type 类，归 extends builtin type
          那簇）、inner/outer 类 6、partial 5（4 例 omc 报错；RedeclareClass2 的 partial 类被 redeclare 换掉，归 redeclare）、
          短类修饰作用域 5（短类定义在外层模型里）、类修饰 / redeclare 若干。
    - [x] (61c) parser + lexer：任务卡 T-S3-08 / T-S3-09（2026-10-05，用户暂不手写核心，Claude 实现）：
          **T-S3-08**：表达式后的下标只能跟在括号式后面（Modelica.g:1800-1806 只有 `LPAR … RPAR array_subscripts?` 一支，且
          只一段）——数组构造 / 矩阵 / 函数调用后不再调 parse_postfix，`[` 留给外层报缺分号（位置与 omc 的 "Missing token:
          SEMICOLON" 相同）；parse_postfix 由 while 改 if（`(e)[1][2]` 也是语法错）；元组后跟下标报 "Tuple expression can
          not be subscripted."（从 '(' 起）。此前 `{a, 1}[1]`、`array(a, 1)[1]` 在绑定 / 算法 / if 式里被静默收下
          （ACCEPTS-INVALID 4 例，PS 探针 p1）。flatten 那道"基是调用 / 构造子 / 矩阵的书写下标不收"的挡板随之撤掉：
          `(fill(2, 3))[1]`、`([1, 2; 3, 4])[2, 1]`、`(ones(n))[2]`、`(size(…))[2]`、`(f(time))[2]` 都与 1.25.0 一致。
          **T-S3-09**：Q-IDENT 按 QCHAR 收紧（BaseModelica_Lexer.g:430-436）：非 ASCII、反引号、制表符 → 词法错，报在坏
          字符的下一列（与 omc 同）；`''` → 报在第二个引号。**卡里第 2 步写错了**：非法转义 `'a\qb'` omc 1.25.0 只告警
          （"Lexer treating \ as \\"）照收，不是错误（PS 探针 L14）——实现照 omc，卡上已注明。flatten 的
          `non-ASCII identifier` 挡板撤掉。
          新探针 49 个（ps-p1 25 / ps-l1 14 / 单测原文 10）：在 61b 上 ACCEPTS-INVALID 10、SKIP 7，现 PASS 25 / BOTH-REJECT 24，
          无 FAIL / ACCEPTS-INVALID / SKIP；旧探针 md-m4 +5、sz-s2 +1 PASS（撤掉的挡板）。
          → scodeinst PASS 523 不变，FAIL 25→26：SubscriptedExp2（status incorrect，两边都在解析期拒绝、文案相同，差在 omc 的
            错误外框，记入分歧账本，同 ConnectAlgorithm）；整个 testsuite PASS 1378 不变（FAIL 0，ACCEPTS-INVALID 3 不变；
            SubscriptedExp2、JBouncingBall SKIP → BOTH-REJECT）；parse sweep 只这两例 OK → ERR，语法错位置一致率 23/23；
            +2 单测（parser_test 13→15）；探针 6714→6763。
    - [x] 任务卡 T-S3-08（Claude 实现于 (61c)，parser）：表达式后的下标只能跟在括号式后面——`{a, b}[2]`、`array(a, b)[1]`、`f(x)[1]`
          在 omc 1.25.0 是语法错（Modelica.g:1800 只有 `LPAR … RPAR array_subscripts?` 这一支），ymc 解析器 5 处多调了
          parse_postfix → `docs/tasks/T-S3-08-subscript-after-paren-only.md`。
    - [x] 任务卡 T-S3-09（Claude 实现于 (61c)，lexer）：带引号标识符的字符集按 OMC 词法收紧——`'höhe'` 这类引号里带非 ASCII 字符的
          标识符 omc 报词法错（BaseModelica_Lexer.g:430-436 的 QIDENT / QCHAR 只有 ASCII），ymc 的 lex_qident 任何字节都收
          → `docs/tasks/T-S3-09-qident-charset.md`。flatten 里有一道临时挡板（`non-ASCII identifier`）。
    - [x] (61d) driver：隐式装库 + inst 的 SKIP 子原因（2026-10-05）：
          **隐式装库**：omc 的 NFLookup.lookupSimpleName 查到顶层都没有的简单名 → loadLibrary(name) 按名从 MODELICAPATH 装默认
          版本（连带 uses 依赖）再查（NFLookup.mo:473-481 / :1162-1223）——`omc X.mo` 里直接用 `Modelica.Electrical…` 的 testsuite
          用例就是这样拿到 MSL 的。ymc 的实例化途中不能往 arena 里加东西，改在实例化之前：收集文件里各名字的首段（类型名、
          extends / import / constrainedby 路径、短类基类、表达式里的引用与调用），不是本文件顶层类、而 MODELICAPATH 上有同名
          库的先装上（S2 的 Loader）；根类改成装库前取的"文件里最后一个顶层类"。多装的库只多出顶层类，不改变内层查得到的名字。
          **子原因**：inst 的 `unresolved component type / base class` 带上查不到的名字（driver 打在 `[…]` 里）——据此把这 219 例
          归了因：MSL 未装 117（本桶解决装库，其后的首因多是 `final` 属性修饰 `attribute modifier`、extends redeclare）、
          ExternalObject 29、Clock 14、StateSelect / AssertionLevel 6、其余本地查找缺口（继承来的类、短类前缀…）。
          新探针 25 个（al-a1）：PASS 22 / SKIP 1（`Modelica.Units.SI.Voltage` 的 final 属性）/ BOTH-REJECT 2。
          → scodeinst 不变（523 / FAIL 26）；整个 testsuite PASS 1378→1389（FAIL 0，ACCEPTS-INVALID 3 不变：Modelica.Math /
            Constants 的函数与常量、MathematicalAspects 的 6 例、ClockedSource）；探针 6763→6788。
          **门的 ASan 扫描改用优化版**：Debug 版 ASan 装一次 MSL（2508 个文件、14 MB 源码）要 120 s（gcc-debug 3.8 s、msvc-debug
          14.6 s），语料扫描里隐式装库的用例全超时。新增构建 / 测试预设 `msvc-asan-rel`（同一构建目录的 RelWithDebInfo 配置，
          13.6 s；试过迭代器调试级别 1，129 s，不用）：ASan 语料扫描用它，单测两个配置都跑（Debug 版带完整迭代器检查）。
          下一步：属性修饰上的 `final`（MSL 的 SI 类型 `type Voltage = Real(final quantity = …, final unit = "V")`，把一大批 MSL
          用例挡在 `attribute modifier`）。
  - [ ] 3g：driver 从库里按名压平（`--load-model` + 类名）→ MSL Examples 压平通过率报告（S3 退出条件）。
- [ ] S3.1 门：easy slice 220 例 FAIL=0，PASS 数记入本节；错误用例（incorrect）里查找/修饰类的文案对拍
- [ ] 之后按桶加宽：3c 数组（Dim/Array/Subscript/Size/For…）→ 3e 函数与求值（Func/Function/Ceval/Record/External）→ 3d connect
      （Connect/Expandable/Stream/Cardinality）→ 3b 余下的 Redeclare/Constraining → when/if/算法 → 3g Base Modelica + MSL Examples

## 下一里程碑

- S3 门 + 结构比对接 Dymola（spec FR-052）。
- S1 后续可选：S1 任务卡（若用户接手手写）；`--dump-after=parse` 的 `.absyn` 文本回读（方案 §2.1 契约，S3 前不急）。
- M3：S3 门 + 结构比对接 Dymola。
