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
- [ ] S3.1 门：easy slice 220 例 FAIL=0，PASS 数记入本节；错误用例（incorrect）里查找/修饰类的文案对拍
- [ ] 之后按桶加宽：3c 数组（Dim/Array/Subscript/Size/For…）→ 3e 函数与求值（Func/Function/Ceval/Record/External）→ 3d connect
      （Connect/Expandable/Stream/Cardinality）→ 3b 余下的 Redeclare/Constraining → when/if/算法 → 3g Base Modelica + MSL Examples

## 下一里程碑

- S3 门 + 结构比对接 Dymola（spec FR-052）。
- S1 后续可选：S1 任务卡（若用户接手手写）；`--dump-after=parse` 的 `.absyn` 文本回读（方案 §2.1 契约，S3 前不急）。
- M3：S3 门 + 结构比对接 Dymola。
