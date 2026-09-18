# T-S2-03 库装载器对齐 OMC（ClassLoader 行为、错误路径、package.order 精确语义）

> 执行者：Codex（用户授权）。验收者：Claude。日期：2026-09-17。
> 先读：`CLAUDE.md`（项目规则）、`docs/research/R8-omc-classloader.md`（本卡的全部依据）、
> `src/scode/loader.h`、`src/scode/loader.cpp`、`tests/unit/scode/loader_test.cpp`、`src/scode/version.h`。

## 目标

`src/scode/loader.{h,cpp}` 目前已通过 S2 装载门（MSL 4.0.0 / MSL 3.2.3 / Buildings 9.1.0 的类名集合与顺序与 OMC 完全一致），
但**错误路径和畸形库**的行为是我凭 MLS 字面写的，与 OMC 不一致；代码里还留着 `[R8]` 占位标记。
本卡把这些行为改成与 OMC 1.25.0 一致，并把每条规则固化成单测。**不要动已经通过的正常路径的结果。**

## 硬性规则（违反即验收不通过）

1. **不臆想**：每条语义只能来自 (a) `docs/research/R8-omc-classloader.md`，(b) OMC 源码
   `C:/dev/OpenModelica/OMCompiler/Compiler/...`（引用 `文件:行`，引用前重新 grep 确认行号），(c) 在安装版 `omc`（1.25.0，在 PATH）上实跑探针。
   oracle 是**安装版 1.25.0**，不是源码 HEAD；两者不一致时以实跑为准并在注释里写明。写不出来源的行为不要实现，记到 `docs/open-questions.md`。
2. 只改这些文件：`src/scode/loader.h`、`src/scode/loader.cpp`、`src/scode/version.{h,cpp}`（确有需要时）、
   `tests/unit/scode/loader_test.cpp`、`tests/CMakeLists.txt`（只为给 loader_test 传夹具路径）、`src/driver/main.cpp`（只在诊断输出确需调整时）。
   **不要改** `src/parse/`、`src/absyn/`、`src/scode/scode*.{h,cpp}`、`src/scode/absyn_to_scode.*`、`tools/`、`docs/`（`docs/open-questions.md` 除外）、`tasks/`。不要 `git commit`。
3. 编码规范：C++20；IR 里不用 `std::variant`/继承/`shared_ptr`；模型/库错误走 `Diagnostics`，不抛异常；`switch` 穷尽不写 `default`；
   注释写"为什么"并带来源锚点；中文注释风格与现有文件一致；两套编译器（gcc-debug、msvc-debug）**零警告**。
4. 诊断文本对齐 OMC 原文（R8 里有逐字探针原文；其余从 `Compiler/Util/Error.mo` 取模板）。OMC 的 NOTIFICATION → `DiagLevel::Note`。

## 要对齐的行为（R8 对应条目见括号）

### 1. MODELICAPATH 默认根（R8 结论 1）
`Loader::default_roots()` 顺序：环境变量 `OPENMODELICALIBRARY`（OMC 唯一读取的变量，`runtime/settingsimpl.c:227-256`）→
环境变量 `MODELICAPATH`（MLS §13.3；OMC 不读，这是 ymc 有意的超集，注释里写明）→ `%APPDATA%/.openmodelica/libraries`。都用 `;` 分隔。

### 2. 顶层条目扫描与同版本并存（R8 结论 7、10）
- 顶层 `Name` / `Name ver` 目录须含 `package.mo`；文件 `Name.mo` / `Name ver.mo`。目录里只有 `package.moc`（加密）→ 跳过并给 Warning（ymc 不支持加密库）。
- 顶层 `Dup.mo` 与 `Dup/package.mo` 并存：**不报错**，版本相等时**枚举顺序靠后者胜**（`systemimpl.c:2201-2209` `modelicaPathEntryVersionGreater` 相等也返回 1；探针 p4 装的是 `Dup.mo`）。
  `choose_version` 里"同版本取先到者"要改成取后到者；枚举顺序用目录项名的字典序（显式排序，别依赖文件系统）。

### 3. 文件级校验（R8 结论 11，`FrontEnd/ClassLoader.mo:434-487 parsePackageFile`）
- 文件必须恰好一个类，否则 ERROR：`Modelica library files should contain exactly one package, but found the following classes: Two, Extra.`，该文件装载失败。
- 类名 ≠ 期望名：仅大小写不同 → WARNING `Expected the package to have name a, but got A. Proceeding since only the case of the names are different.` 并继续；否则 ERROR 并失败。
- `within` ≠ 期望（期望 = 包围类全名；顶层库期望 `within ;` 或无 within）：仅大小写不同 → WARNING；否则 ERROR
  `Expected the package to have within Qux; but got within Nope;.` 并失败。
- **任何子文件失败 → 整个库装载失败**（OMC 的 `fail()` 一路上抛）：`load_library` 返回 invalid，并追加
  `Failed to load package Qux (default) using MODELICAPATH <roots>.`（未指定版本时括号里是 `default`）。失败的库不得进入 `Absyn::top` 与 `loaded()`。
- `package.mo` 必须是长类定义（PARTS），短类定义 → 失败（`ClassLoader.mo:323-326` 附近，自己核对行号与文案）。

### 4. package.order（R8 结论 8、9 与 "§4 package.order 精确语义"，`ClassLoader.mo:497-570, 598-628, 642-763`）
- 读法：按行、trim、去空行；重复名 → WARNING（`PACKAGE_ORDER_DUPLICATES` 的文案）。
- 与 `package.mo` 元素的合并：照 `getPackageContentNamesinParts/Elts` 的算法移植（名单按序消费；名单头等于元素名（类或 constant 组件）则该元素就位；
  否则名单头是外部文件；名单耗尽仍有类/常量元素 → WARNING 并接在后面；一行多常量须在名单中连续；非类/非组件元素与方程段原位保留）。
  文案与各 WARNING 名称见 R8，逐条核对 `Error.mo`。
- 未列出的子文件/子目录 → WARNING（探针原文：`The package.order file does not list all .mo files and directories (containing package.mo) present in its directory.\nMissing names are:\n\tA`），追加到**末尾**。
- 列出但不存在（既不是 package.mo 元素，也没有 `X/package.mo` 或 `X.mo`）→ WARNING
  `Missing was referenced in the package.order file, but was not found in package.mo, Missing/package.mo or Missing.mo.`，然后**整份 package.order 作废**，退回"无 package.order"的默认顺序（包仍装载成功；此时"未列出"警告不再出现）。
- 无 `package.order` 的默认顺序：外部子项（子目录 + `.mo`）按名字**逆字典序**，然后才是 `package.mo` 自身的元素（探针：`NoOrd2` → `{C, B, A, P1, Q1}`；`NoOrder` → `{Sub, C, B, A, P1}`）。
  现实现是"自身元素在前 + 子项正序"，要改。
- 包内 `X.mo` 与 `X/package.mo` 并存 → ERROR `The same class is defined in multiple files: <p1>, <p2>.` 且该包（从而整个库）装载失败（探针 `Dupin`）。

### 5. uses（R8 结论 5、6 与 "§6 uses 注解"）
- 只认 `Name(version = "s")`——**恰好一个 `version` 参数**（`Script/Interactive.mo:7511-7548`）。`version=` 不是字符串 → WARNING（`USES_MISSING_VERSION` 文案）并按 `default` 装；
  裸 `NoVer` 或多参数形式**静默跳过**。
- uses 触发的装载失败只是 `DiagLevel::Note`：`Skipped loading package Missing (1.0) using MODELICAPATH <roots> (uses-annotation may be wrong).`，**不让父库失败**，也不报 Error。
  （显式 `load_library` 失败仍是 Error。实现上给内部装载函数加一个 `force_load` 参数即可。）
- 目标包已装载但版本不同：按已装包的 `conversion` 注解分级（`Script/CevalScript.mo:548-614 checkValidVersion`）：版本相同静默；`noneFromVersion` 命中 → Note；
  `from(version)` 命中 → Warning；无注解 → 按新旧给 Warning。四条文案的探针原文都在 R8 "§6"，逐字对齐。
- OMC `loadModel` 默认 `notify=false`：成功经 uses 自动装载**不打印任何消息**，保持现状。

### 6. 清理
把 `loader.h` / `loader.cpp` 里所有 `[R8]` 占位替换成真实锚点（`文件:行`，重新 grep 过的）。`choose_version` 的版本选择逻辑已对齐 OMC 两层机制并有 13 例单测，**不要改它的判定结果**（第 2 条的"同版本取后到者"除外）。

## 测试（写进 `tests/unit/scode/loader_test.cpp`）

夹具：`tests/fixtures/loader-r8/` 是 R8 探针用的真实目录树（原样拷贝；OMC 1.25.0 对它的实测结果见 R8 "§8 探针 2" 与 "§4"）。
在 `tests/CMakeLists.txt` 给 `loader_test` 加 `target_compile_definitions(loader_test PRIVATE YMC_TEST_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")`，测试里用它定位夹具（只读，不要往里写）。
每个用例 = 一个 `YMC_TEST`，断言**类名顺序**（用 `collect_class_names`）与**诊断的级别 + 文本关键片段**：

| 库 | OMC 实测（R8） |
|---|---|
| `Part`（order `B,P`，A 未列）| `{B, P, A}` + 未列出 Warning |
| `Good`（order `Z,k1,k2,M,A`）| `{Z, M, A}`，无诊断 |
| `Miss`（order `P,A,Missing,B`）| `{B, A, P}` + "Missing was referenced…" Warning，且**没有**"未列出"Warning |
| `NoOrd2` | `{C, B, A, P1, Q1}` |
| `NoOrder` | `{Sub, C, B, A, P1}`（含 protected 类时 `include_protected=true`）|
| `Baz` | `{C, B, A, Inline}`；诊断见 R8 "§8"（`Inline` 先被当外部文件消费 → 存在性检查失败 → order 作废）|
| `Sp`（order 带空白与空行）| 正常，无诊断 |
| `Qux`（`Bad.mo` 的 within 写错）| 装载失败，两条 Error（文案见上）|
| `Wrong` | 装载失败（自己读夹具确认是哪一类错误，并与 R8 对照）|
| `Dupin` | 装载失败，"The same class is defined in multiple files" |
| `Two.mo` | 装载失败，"exactly one package" |
| `Bom.mo`、`Bar.mo` | 成功，无诊断 |
| `Dup`（顶层 `Dup.mo` + `Dup/`）| 成功、无诊断、装的是 `Dup.mo`（看 `loaded()[i].path`）|
| `Foo`（1.0.0 / 2.0.0 / 3.0.0-beta.1）| `""`→3.0.0-beta.1；`"1"`→1.0.0；`"1.5"`、`"0.5"`→3.0.0-beta.1；`"2.0"`→2.0.0；`"3.0.0"`→3.0.0-beta.1；`"3.0.0-beta"`→3.0.0-beta.1；`"3.0.0-rc"`、`"abc"`→失败。**若现实现与此不符，先停下来在最终报告里说明，不要为了过这个表去改 `choose_version` 的既有 13 例行为** |
| `UsesTest.mo` 等 uses 夹具 | 按 R8 "§6"：缺失依赖只给 Note、库本身成功 |
| `Case` | **跳过**：结果 `{A, A}` 依赖 Windows 文件系统大小写不敏感，属 OMC 缺陷，不复刻；在测试文件里留一行注释说明 |

现有用例里与新行为冲突的断言（如"within 不一致只告警"）要按新行为改，不能删测试了事。有疑问的夹具可以自己写 `.mos` 在 `omc` 上重跑确认（脚本样例：`docs/research/probe-r8/p1.mos`–`p5.mos`）。

## 验收命令（全部要过；最终报告里贴每条的结果摘要）

```
cmake --preset gcc-debug && cmake --build --preset gcc-debug && ctest --preset gcc-debug
cmake --preset msvc && cmake --build --preset msvc-debug && ctest --preset msvc-debug
cmake --build --preset gcc-release
python tools/scode_compare.py --ymc build/gcc-release/ymc.exe --package Modelica --version 4.0.0
python tools/scode_compare.py --ymc build/gcc-release/ymc.exe --package Modelica --version 3.2.3
python tools/scode_compare.py --ymc build/gcc-release/ymc.exe --package Buildings --version 9.1.0
```
- 构建输出里 `warning` 计数必须为 0（两套编译器）。
- 三个 `scode_compare` 必须仍是 PASS（集合与顺序都一致）。如果对齐 OMC 之后三套真库冒出了 Warning，逐条在 `omc` 上核实 OMC 是否也给同样的 Warning（`loadModel(...); getErrorString();`），把结论写进最终报告。

## 最终报告（写到 stdout 即可）

1. 改了哪些文件、每条行为对应的 OMC 锚点；2. 每条验收命令的结果；3. 与 R8 或本卡不一致、或无法确认来源而**没有**实现的点；4. 你认为任务卡本身写错的地方。

---

## 验收记录（Claude，2026-09-17）

执行：`codex exec -s workspace-write`（gpt-5.6-sol，reasoning=high），两段会话共约 23 万 token。第一段只出计划就停下等"确认实施"，
用 `codex exec resume <session-id>` 续上后一次做完。

**通过的部分**
- 只改了卡里允许的 4 个文件（对照交接前快照逐文件 diff 确认）；没有 commit；`[R8]` 占位全部换成了重新 grep 过的 `文件:行`。
- 第 1–6 节的行为全部实现并各有单测（loader_test 8 → 30 例）；诊断文案与 R8 探针原文逐字一致。
- 如实报告了卡里的两处错误：`Foo` 表与"不改 choose_version"冲突（没有硬改，留给验收者）；`NoOrder` 期望漏了 protected 类 `P2`。
- 验收命令全过：gcc-debug / msvc-debug / gcc-release 零警告，ctest 7/7，三套库类名对拍 PASS。

**验收时改掉的部分**
1. `uses` 依赖被装载两遍：Codex 为了不让畸形依赖的 Error 出现在父库的诊断里，先在一棵隔离的 Absyn 里试装一遍，成功了再正式装。
   代价是 Buildings 的依赖（整个 MSL）被解析两次；而它引用的 `CevalScript.mo:502-545` 是不回滚错误缓冲的 try/else，OMC 里那些 Error 本来就留着。
   改成直接装、失败补 Note，并加了单测 `uses_malformed_dependency_keeps_its_errors_but_parent_loads`。装载耗时回到 1.70 s / 4.85 s。
2. `Foo` 的 4 个版本用例：根因是我原来的版本选择只对"已知发行版"放宽，而 OMC 对不在索引里的库直接放宽；另外 `3.0.0-beta.1`
   被 `parse_version` 当成无序版本。补了"无 conversion 历史 → 直接放宽"、semver 预发布解析、带后缀请求的前缀匹配（ADR-005）。
3. `place_into` 留了两个 `(void)` 掉的参数——删掉；`choose_version` 注释里的 scratchpad 路径、ADR 编号过期——改掉。

**给下一张 Codex 任务卡的改进**：prompt 里写明"非交互批处理，不要停下等确认"；卡里的期望表先自己在 oracle 上核一遍再交出去；
"不许改某函数"这类约束要写清楚与期望表冲突时谁优先。
