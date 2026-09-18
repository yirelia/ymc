# R8：OMC 按 MODELICAPATH 加载库的机制（ClassLoader / loadModel / 版本选择 / package.order / uses）

> 源码根 `C:\dev\OpenModelica\OMCompiler\Compiler\`（下文路径相对于此，HEAD=3ceb0c9d06）。探针：安装版 omc 1.25.0，脚本与临时库在 scratchpad `probe-r8\`（p1–p5.mos，`lib\`）。
> 版本一致性：`git diff v1.25.0 HEAD` 显示 `ClassLoader.mo` 仅许可证头变动，`systemimpl.c` 的 `getLoadModelPath*` 区段无改动；下文行号为 HEAD，行为已用 1.25.0 实测。

## 结论摘要

1. **`MODELICAPATH` 环境变量不被 omc 读取**。`runtime/settingsimpl.c:227-256 SettingsImpl__getModelicaPath`：已 set→用之；否则 `getenv("OPENMODELICALIBRARY")`；否则 `"%s/.openmodelica/libraries/", homePath`（Windows home=`getenv("APPDATA")`，:189-192）。全树无 `getenv("MODELICAPATH")`。探针：`getModelicaPath()`→`"C:/Users/yangrui/AppData/Roaming/.openmodelica/libraries/"`，而 `getEnvironmentVar("MODELICAPATH")`→`"D:\\modelica\\modelica-buildings"` 被无视。错误文案里的 "MODELICAPATH" 只是措辞。
2. 版本选择是**两层**：①`ClassLoader.loadClassFromMps`（FrontEnd/ClassLoader.mo:152-166）用 `PackageManagement.versionsThatProvideTheWanted`（Script/PackageManagement.mo:352-371）按 **index.json 的 `provides`** 把请求版本**替换**成候选串列表（库不在索引/无索引则保留原串）；②`System_getLoadModelPath`（runtime/systemimpl.c:2304-2327）按候选顺序做目录名匹配：先"最长版本前缀相等 ∧ 版本≥请求 ∧ 取最高"逐级放宽到 0 级前缀（:2224-2244），再精确匹配（:2246-2268）。假说 A/B 都不对；conversion 注解不参与选库。
3. 索引层有一个"陷阱"：`SemanticVersion.compare`（Util/SemanticVersion.mo:120-122）规定 **任一方为 0.0.0 即相等**，而索引里 `trunk`/`master` 被 `parse(..., nonsemverAsZeroZeroZero=true)` 成 0.0.0 → 它们"提供"任何版本。所以对索引内的库，凡没有已发布版本 `provides` 的请求（3.2.4、3.3、2.0、1.0）候选=`{trunk,master}` → 本地无此目录 → 失败，C 层前缀放宽根本没机会跑；而 2.2.2 成功是因为候选变成 `2.2.2+maint.om` 后 C 层 0 级放宽取到 4.0.0。
4. 优先级列表 `{"a","b"}` **先列者优先**（p1/p5：`{"3.2.3","4.0.0"}`→3.2.3，反之→4.0.0；Foo `{"9.9","1.0.0"}`→1.0.0）。`requireExactVersion=true` 跳过索引层且 C 层只做 6 级全等（`3.2`≡`3.2.0.0.0.0`，:2224），并**传递给 uses 递归**（p1：exact 装 3.2.3 后 Complex 3.2.3 被 "Skipped"）。
5. `uses` 在**每个包装载完成后立刻递归**（Script/CevalScript.mo:531-534），`forceLoad=false`：找不到只发 NOTIFICATION `NOTIFY_LOAD_MODEL_FAILED`；装到不同版本**无任何消息**（选库不比对 uses 版本）；只有 `notify=true` 才发 `NOTIFY_LOAD_MODEL_DUE_TO_USES`。`loadModel` 默认 `notify=false`，`loadFile` 默认 `notify=true`（FrontEnd/ModelicaBuiltin.mo:2244 / :1139）。
6. 目标包**已在程序中**时才比版本：`checkModelLoaded`→`checkValidVersion`（CevalScript.mo:548-614）用已装包的 `conversion(noneFromVersion/from)` 分级：NOTIFICATION（noneFromVersion 命中）/ WARNING（from 命中 / 无注解且新于或旧于）；显式 `loadModel`（forceLoad=true）则直接重装覆盖。
7. 顶层候选条目（systemimpl.c:2110-2135，匹配条件在 :2120）：目录项名 = `Name`、`Name<空格>ver`（目录须含 `package.mo`/`package.moc`）或文件 `Name.mo`/`Name ver.mo`；`splitVersion` :2063-2098 取最多 6 级整数，余串以 `+` 开头视为无后缀（`+maint.om` 等价于无后缀），`-beta.1` 之类保留为 extra。
8. `package.order`（ClassLoader.mo:497-570）：逐行 trim、去空行；未列出者 → WARNING `PACKAGE_ORDER_FILE_NOT_COMPLETE` 并**追加到末尾**；列出但不存在 → WARNING `PACKAGE_ORDER_FILE_NOT_FOUND` 后 `fail()`，被外层 `try` 捕获 → **整个 order 文件被丢弃**，退回"无 order 文件"路径（包仍装载成功）。
9. 无 `package.order` 时顺序 = 外部子项（子目录+`.mo`）按 `Util.strcmpBool` 排序后**逆字典序**，然后才是 `package.mo` 自身元素（p4：`NoOrd2`→`{C, B, A, P1, Q1}`）。
10. 同一子名既有 `X.mo` 又有 `X/package.mo` → ERROR `PACKAGE_DUPLICATE_CHILDREN` 且 `assertionOrAddSourceMessage` 对 ERROR 级 **fail**（Util/Error.mo:1732-1746）→ 包装载失败。顶层 MODELICAPATH 里 `Dup.mo`+`Dup/package.mo` 则**无报错**，目录枚举中靠后者胜（`modelicaPathEntryVersionGreater` 相等也返回 1，:2201-2209；p4 装了 `Dup.mo`）。
11. 文件内类名/within 校验在 `parsePackageFile`（:434-487）：文件须恰好一个类（`LIBRARY_ONE_PACKAGE_PER_FILE`，ERROR）；类名≠期望：仅大小写异→WARNING 继续，否则 ERROR+fail；`within`≠期望：仅大小写异→WARNING，否则 ERROR `LIBRARY_UNEXPECTED_WITHIN`+fail → **整个库装载失败**（p2 `Qux`、p4 `Wrong`）。BOM 由语法接受（`Parser/Modelica.g:227 BOM? (within=within_clause SEMICOLON)?`，p4 `Bom.mo` 正常）；加密扩展名是 `.moc`，**源码无 `.moe`**。
12. `getClassNames`：顶层顺序=装载顺序逆序（`updateProgram2` 头插，Script/InteractiveUtil.mo:1390-1394）；递归为先序且首项是 root（InteractiveUtil.mo:3580-3586）；`includeConstants=true` 与 `recursive=true` 同用是**坏的**——常量当作类路径查找失败 → `LOOKUP_ERROR` 且该分支返回 `{}` 把已累计结果清空（:3597-3598 `then (inPath,{})`），624 = 最后一个失败常量 `Modelica.Constants.T_zero` 之后遍历到的 Icons(33)+Units(591)。

## 版本选择算法

### 调用链
`loadModel(X,{v…})`（CevalScript.mo:1327-1349，`forceLoad=true, checkUses=true`）→ `loadModel` :450-472 → `loadModel1` :474-547 → `ClassLoader.loadClass` :90-135（`System.strtok(mp, Autoconf.groupDelimiter)` 拆分）→ `loadClassFromMps` :137-208 → `System.getLoadModelPath` :174 → `Config.setLanguageStandardFromMSL(name)` :201（Util/Config.mo:484-516，按 `Modelica <ver>` 目录名切换语言标准）→ `loadClassFromMp` :210-269。

### 第 1 层：候选扩展（ClassLoader.mo:152-166）
```
if not requireExactVersion:
  if prios=={} : versions := versionsThatProvideTheWanted(id,"default")
  else for v in listReverse(prios):
         vp := versionsThatProvideTheWanted(id, v)
         versions := (vp=={} ? v::versions : vp ++ versions)      # 请求串被 vp 整体替换
else versions := prios
```
`versionsThatProvideTheWanted`（PackageManagement.mo:352-371）：`getPackageIndex` :282-319 读 `<home>/.openmodelica/libraries/index.json`（用户库目录不在 MODELICAPATH 内则返回空 :298-304）；对 `libs[id].versions` 每个 `V`，`providesExpectedVersion(V, provides, wanted)` :165-200 —— `for v in V::provides` 用 `SemanticVersion.compare(parse(v,nonsemverAsZeroZeroZero=true), wanted, comparePrerelease=isPrerelease(wanted)) == 0`；"default"/"" 匹配一切。结果按 `compareVersionsAndSupportLevel` :220-241 排序（`List.sort` 配"小于"函数 ⇒ **降序**：support 高者先、非预发布先、版本高先）。
本机 index.json（2025-11-21）片段：`Modelica 3.2.3+maint.om provides=[3.0.0,3.0.1,3.1.0,3.2.0,3.2.1,3.2.2]`、`4.0.0+maint.om provides=[]`、`4.1.0+maint.om provides=[4.0.0]`、`2.2.2+maint.om provides=[2.1.0,2.2.0,2.2.1]`、`trunk/master provides=[]`（但按结论 3 匹配一切）；`Complex 4.0.0+maint.om provides=[1.0.0,1.1.0,3.2.1,3.2.2,3.2.3]`；`ModelicaServices 4.0.0+maint.om provides=[1.0.0,1.1.0,1.2.0,3.2.1,3.2.2,3.2.3]`。

### 第 2 层：`System_getLoadModelPath`（systemimpl.c:2304-2327）
1. `getAllModelicaPaths` :2110-2184：对每个 mp 目录 `readdir`，条目满足 `strncmp(name,d_name,nlen)==0 && d_name[nlen] ∈ {'\0',' ','.'}`；`Name<空格>…` 时 `splitVersion` → `version[6]` + `versionExtra`。
2. 按候选顺序（:2309-2323）首个命中即返回：
   - `"default"` → `getLoadModelPathFromDefaultTarget` :2272-2302：extra 为空或以 `+`/`-` 开头的最高版本；没有则任意最高。
   - 其他 → `getLoadModelPathFromSingleTarget` :2211-2270：(a) 请求以数字开头且无 extra 时 `for j=6..(exact?6:0)`：取 {前 j 级相等 ∧ (j==6 ∨ 条目≥请求) ∧ extra ∈ {"", "+…", "-…"}} 的最高者，该级命中即返回；(b) 否则/落空：逐条目精确 6 级相等且 `strncmp(entryExtra, wantExtra, strlen(wantExtra))==0`（前缀匹配；请求全 0 时仅比 extra，条目 extra 可去前导 `-`）。
3. 全落空 → `MMC_THROW()` :2326 → `loadClassFromMps` :175-199 依索引生成 `installPackage(...)` 提示（NOTIFICATION `NOTIFY_PKG_FOUND`）→ `fail()` → `loadModel1` :536-545：forceLoad → ERROR `LOAD_MODEL_FAILED` "Failed to load package %s (%s) using MODELICAPATH %s."，否则 NOTIFICATION `NOTIFY_LOAD_MODEL_FAILED`。

### 判别探针（本机只有 Modelica 3.2.3+maint.om / 4.0.0+maint.om）
| 请求 | 第 1 层候选（头部） | 第 2 层结果 | 实测（p1/p3） |
|---|---|---|---|
| `3.2.4`、`3.3`、`2.0`、`1.0` | `{trunk, master}` | 非数字→(b) extra 前缀不匹配 | **失败**，提示 `installPackage(Modelica, "trunk", exactMatch=true)` |
| `3.0`、`3`、`3.2.1` | `3.2.3+maint.om, 3.2.2+…, …` | 6 级全等 | 3.2.3 |
| `4.0` | `4.1.0+maint.om, 4.0.0+maint.om, …` | 4.1.0 无 → 下一候选全等 | 4.0.0 |
| `2.2.2`、`2.1` | `2.2.2+maint.om, …` | j=0 级放宽取最高 | 4.0.0 |
| `4.1.0` | `4.1.0+maint.om, 4.2.0-dev, …` | 无 ≥4.1.0 | 失败 |
| `3.2.3+maint.om`、`{"default"}`、`{""}`、`loadModel(Modelica)` | — | 全等 / default | 3.2.3 / 4.0.0 / 4.0.0 / 4.0.0 |
| exact `3.2.1`、exact `3.2` | 原串 | 仅 j=6 | 失败 |
| `Complex 3.2.3` | `4.1.0+maint.om, 4.0.0+maint.om, …` | 全等 | 4.0.0 |

不在索引的库（p2/p5 `Foo 1.0.0 / 2.0.0 / 3.0.0-beta.1`）直接验证 C 层：`loadModel(Foo)`→3.0.0-beta.1（`-` 前缀算发布）；`{"1"}`→1.0.0（j=1 前缀 1 且 ≥）；`{"1.5"}`、`{"0.5"}`→3.0.0-beta.1（j=0 取最高）；`{"2.0"}`→2.0.0；`{"3.0.0"}`（含 exact）→3.0.0-beta.1；`{"3.0.0-beta"}`→3.0.0-beta.1（extra 前缀）；`{"3.0.0-rc"}`、`{"abc"}`、`{"1.0.0.0.0.0.0"}`（7 级）→失败；`{"1.0.0","2.0.0"}`→1.0.0。

## 逐项事实

### 1. MODELICAPATH 默认值与来源
- 见结论 1。`setModelicaPath("")` 清空回默认（settingsimpl.c:291-300）；`getModelicaPath` 会 `putenv("OPENMODELICALIBRARY=…")` :254。用户库/索引路径：PackageManagement.mo:777-787；安装缓存 `share/omlibrary/cache/index.json` :798-808。
- 本机 `%APPDATA%\.openmodelica\libraries\`：`Buildings 9.1.0`、`Complex 4.0.0+maint.om`、`Modelica 3.2.3+maint.om`、`Modelica 4.0.0+maint.om`、`ModelicaReference 4.0.0+maint.om`、`ModelicaServices 4.0.0+maint.om`、`ObsoleteModelica4 4.0.0+maint.om`、`index.json`。

### 2. loadModel / loadFile / getSourceFile 入口
- `CevalScript.loadFile` :321-372：参数为目录 → 追加 `/package.mo` :335-337；不可读 → ERROR `LOAD_FILE_FAILED` :339-347；文件名是 `package.mo|.moc` → 从**父目录名**拆 `cname` 与版本串（:350-356）并临时把 `realpath(dir/..)` 前置到 MODELICAPATH，`loadModel(…, onlyCheckFirstModelicaPath=true, pathToFile=…)` → `loadModel1` :516-522 直接 `loadClassFromMp(…, isDir=true)`（p2：`loadFile(".../Foo 1.0.0/package.mo")` 与 `loadFile(".../Foo 1.0.0")` 均装 1.0.0）；普通文件 → `Parser.parse` :359，`allowWithin=false` 时顶层 within 必须为 TOP :361-363（`checkTopClassWithin` :421-432），重复顶层类 → ERROR `DOUBLE_DECLARATION_OF_ELEMENTS` :369-371，再 `checkUsesAndUpdateProgram` :373。
- `loadModel` case :1327-1349：`languageStandard` 非空时临时切换 std；`getSourceFile` :766-770 → Script/Interactive.mo:3299-3324 `AbsynUtil.classFilename`，找不到返回 `""`。

### 3. 目录布局 / within / 编码 / 加密
- 目录库 `loadClassFromMp` :246-267：读 `path/name/package.encoding`（OM 扩展）否则用传入编码，默认 `"UTF-8"` :250；非 testsuite 且 `noProc()!=1` 时 `getAllFilesFromDirectory` :271-287 收集 `package.mo`+`*.mo`（子目录仅含 `package.mo` 者递归）→ `Parser.parallelParseFiles` 预解析成哈希表 :260-262；再 `loadCompletePackageFromMp` :289-341（`package.mo` 缺失 → 内部错误 :323-326；`package.mo` 必须是 PARTS）。单文件库 :236-244 `parsePackageFile(expectPackage=false, within=TOP)`。
- 子项期望 within = `joinWithinPath(父 within, IDENT(id))` :330-331；子项加载 `loadCompletePackageFromMp2` :369-432：先看 `mp/id/package.mo`（目录）否则 `mp/id.mo`，都无 → 内部错误 fail。
- 探针原文：`[…/Qux/Bad.mo:2:1-2:18:writable] Error: Expected the package to have within Qux; but got within Nope;.` + `Error: Failed to load package Qux (default) …`；`[…/Two.mo:0:0-0:0:readonly] Error: Modelica library files should contain exactly one package, but found the following classes: Two, Extra.`；`Case/A.mo` 被 order 写成 `a` → `Warning: Expected the package to have name a, but got A. Proceeding since only the case of the names are different.`。
- `.moc`/`package.moc` 走 `encrypted=true` 分支（`Parser.startLibraryVendorExecutable` :252-258），grep 全 Compiler 无 `.moe`。

### 4. package.order 精确语义（`getPackageContentNames` :497-570）
- 读法 :519-524：`strtok(contents,"\n")` → `trimWhitespace` → 去空行（p4 `Sp` 的 `"  A  \n\n"` 正常）；重复 → WARNING `PACKAGE_ORDER_DUPLICATES`。
- 与 `package.mo` 元素合并 `getPackageContentNamesinParts/Elts` :642-733：名单按序消费；名单头==元素名（类或 `constant` 组件）→ 该元素按名单位置放置；不等 → 名单头视作外部文件 `CLASSLOAD`；名单耗尽仍有类/常量元素 → WARNING `FOUND_ELEMENT_NOT_IN_ORDER_FILE` 并插回名单；元素已作为 CLASSLOAD 加入过又在 package.mo 中出现 → WARNING `PACKAGE_MO_NOT_IN_ORDER`；一行多常量须在 order 中连续否则 WARNING `ORDER_FILE_COMPONENTS` :735-763。非类/组件元素与 equation 等 part 原位保留。
- 存在性检查 `checkPackageOrderFilesExist` :598-628：`mp/name` 目录或 `mp/name.mo` 皆无 → 大小写不敏感找 `.mo`：找到 → WARNING `PACKAGE_ORDER_CASE_SENSITIVE` 并改名；否则 WARNING `PACKAGE_ORDER_FILE_NOT_FOUND` + `fail()`（:617-618）。**Windows 文件系统大小写不敏感，`regularFileExists("a.mo")` 为真**，故 p4 `Case` 未走此分支而是既按 `a` 加载又把 `A` 当未列出追加 → `getClassNames(Case)`=`{A, A}`（重复类）。
- 顺序结果：`po` 为逆序，`List.fold4(reverseOrder, loadCompletePackageFromMp2 …)` 用 `mergeBefore` 头插 :335,:348-367。存在 order：`po = po2(未列出) ++ po1` → 未列出者排最后。探针 `Part`（order `B,P`，A 未列）→ `{B, P, A}` + `[…/Part/package.order:0:0-0:0:readonly] Warning: The package.order file does not list all .mo files and directories (containing package.mo) present in its directory.\nMissing names are:\n\tA`。`Good`（order `Z,k1,k2,M,A`）→ `{Z, M, A}`（含常量时 k1,k2 在 M 前）。
- 列出但不存在：`Miss`（order `P,A,Missing,B`）→ 仅 `Warning: Missing was referenced in the package.order file, but was not found in package.mo, Missing/package.mo or Missing.mo.`，结果 `{B, A, P}`（= 无 order 顺序；未列出警告也不再出现，因整个 try 块被放弃 :517-551→:552-561）。
- 无 order 文件 :552-561：`sort(subdirs++mofiles, strcmpBool)`（`strcmpBool = stringCompare>0`，Util/Util.mo:1025-1030）+ 查重 → `po = classParts ++ CLASSLOAD…` → 结果**逆字典序，package.mo 元素在末**：`NoOrd2`→`{C, B, A, P1, Q1}`；`NoOrder`（含子目录 Sub 与 protected 类）→`{Sub, C, B, A, P1}`。
- 目录枚举原语 `System_moFiles/subDirectories`（runtime/System_omc.c:470-483 / 588-618）：Windows 用 `FindFirstFileW` 逐项 `cons`（**无排序**），Unix `scandir` 无比较函数；顺序由文件系统决定，只有无 order 分支显式排序。
- 文件名带空格：顶层由 `getAllModelicaPaths` 拆 `Name ver`；包内子项直接拼 `mp/id(.mo)`，行只 trim 首尾 → **无**对包内空格名的特殊处理（未探针）。

### 5. "Failed to insert class X within Y"
- `Error.INSERT_CLASS`（Util/Error.mo:505-506，ID 213）由 `InteractiveUtil.insertClassInProgram` :2703-2751 的兜底 case 产生：`updateProgram2` :1364-1398 对 `within W` 的程序调用它，`getClassInProgram(W 的首段, p)` 失败即报错（列出当前所有类）并 fail。触发：`loadFile` 一个 `within Foo;` 文件而 `Foo` 未装载。探针：`Error: Failed to insert class Orphan within Foo;\n\n the available classes were:\n`（空程序）；先 `loadModel(Foo)` 再 loadFile → 成功，`getClassNames(Foo)`=`{Orphan}`。特例：`within OpenModelica.…` 静默忽略 :2737。

### 6. uses 注解
- 提取 `Interactive.getUsesAnnotation` Script/Interactive.mo:7454-7475（仅顶层类）；`getUsesAnnotationString2` :7511-7548 只认 `Name(version="s")`（恰好一个 `version` 参数）；`version=` 非字符串 → WARNING `USES_MISSING_VERSION` 用 `"default"`；**裸 `NoVer` 或多参数形式被静默跳过**（p5 `uses(... NoVer)` 无任何消息）。
- 递归：`loadModel1` :532-534 装完即 `getUsesAnnotationOrDefault(pnew)` → `loadModel(…, forceLoad=false, …)`，深度优先（成功通知 :524，失败 :540-544）；`loadFile` 经 `checkUsesAndUpdateProgram` :434-448 同理，`uses=false` 关闭（p5：`{UsesTest}`）。
- 探针原文（p1/p5）：`Notification: Automatically loaded package Complex 4.0.0 due to uses annotation from Modelica.`；`Notification: Skipped loading package Missing (1.0) using MODELICAPATH … (uses-annotation may be wrong).`；已装 Complex 4.0.0 再装 Modelica 3.2.3 → `Notification: Modelica requested package Complex of version 3.2.3. Complex 4.0.0 is used instead which states that it is fully compatible without conversion script needed.`；已装 Modelica 4.0.0 再 `loadFile(uses323.mo)` → `Warning: UsesOld requested package Modelica of version 3.2.3. Modelica 4.0.0 is used instead which states that it is only compatible with a conversion script. Use convertPackageToLibrary(UsesOld, Modelica, "4.0.0") …`；`uses(Modelica 4.1.0)` 对已装 4.0.0 → `Warning: Requested package Modelica of version 4.1.0, but this package was already loaded with version 4.0.0. There are no conversion annotations for this version but 4.1.0 is newer …`；Foo 2.0.0 已装、uses 1.0.0 → `…1.0.0 is older than 2.0.0, so the libraries are probably incompatible.`。

### 7. getClassNames
- `CevalScript.getClassNames` :3190-3237：`builtin=true` → `p := updateProgram(p, FBuiltin.getInitialFunctions())`，即把 `ModelicaBuiltin.mo` 的 97 个顶层项（`StateSelect, Uncertainty, Clock, ExternalObject, der, initial, terminal, AssertionLevel, assert, ceil, …`）并入（探针 `size(getClassNames(builtin=true))`=97，空程序）；`class_=AllLoadedClasses` 非递归 → `getTopClassnames`（=程序 classes 顺序）；递归 → `getClassNamesRecursive`（InteractiveUtil.mo:3558-3599）`acc = pp::acc`（:3582）后对 `getClassnamesInClassList` 每个子名递归，末尾 `listReverseInPlace` → 先序、root 首项（探针 `{Modelica, Modelica.UsersGuide, Modelica.UsersGuide.Overview}`）；`sort=true` → `List.sort(paths, pathGe)`；`qualified` 仅对非递归指定类加前缀。
- `showProtected` 控制 `getClassnamesInParts` 是否进 PROTECTED 段 :1496-1537；`includeConstants` 在 `getClassnamesInElts` :1539-1580 把 `variability=CONST` 的组件名一并列出（`Modelica.Constants`：0 类 / 22 常量；`Modelica.Units.SI` 527/527）。
- 顶层顺序探针：装 Bar,Foo,Baz → `{Baz, Foo, Bar}`；装 Complex, Modelica（自动带 ModelicaServices）→ `{ModelicaServices, Modelica, Complex}`，再装 ModelicaReference → 前插。
- 624 机制见结论 12（`getErrorString` 出现 1846 条 `Error: Class Modelica.….m not found in scope <TOP>.`）。

### 8. 探针 2 原始布局与结果（`probe-r8\lib`，`setModelicaPath` 指向后）
`Foo 1.0.0/`、`Foo 2.0.0/`、`Foo 3.0.0-beta.1/`（各 `package.mo` 含 `annotation(version=…)`）、`Bar.mo`、`Baz/{package.mo(k1,k2,Inline), A.mo, B.mo, C/package.mo, package.order=C,Missing,k2,Inline,A,k1}`、`Qux/Bad.mo(within Nope)`、`Dup.mo`+`Dup/package.mo`、`Two.mo`、`Bom.mo`(EF BB BF 头)、`Part`、`Miss`、`Case`、`NoOrd2`、`Dupin/{X.mo, X/package.mo}`、`Sp`。
- `Baz`：装载成功，`getClassNames(Baz)`=`{C, B, A, Inline}`，含常量 `{C, B, A, k1, k2, Inline}`；消息：3×`Warning: Elements in the package.mo-file need to be in the same relative order as the package.order file. Got element named k2/Inline …`、2×`Warning: Got element k2/Inline that was not referenced in the package.order file.`、`Warning: Inline was referenced in the package.order file, but was not found in package.mo, Inline/package.mo or Inline.mo.`（`Inline` 先被当外部文件消费，随后存在性检查失败 → 整个 order 被丢弃 → 退回逆字典序；`Missing` 从未被报告）。
- `Dupin` → `Error: The same class is defined in multiple files: …/Dupin/X.mo, …/Dupin/X/package.mo.` + `Error: Failed to load package Dupin (default) …`。
- `Bar.mo` 单文件 → 成功；`Bom.mo` → 成功；`Dup` → 装 `Dup.mo` 无消息。

## 未找到或待验证
- 包内子项名含空格（`Sub Name.mo`）的行为：源码无特殊处理，未探针。
- Windows 下 `System_moFiles` 返回顺序（`FindFirstFileW` 枚举序，NTFS 通常按名排序但未保证）；对 `Part`/`Case` 里"未列出者"的相对顺序仅在 1 个元素时验证。
- `PACKAGE_ORDER_CASE_SENSITIVE` 在 Windows 不可触发（FS 大小写不敏感），Linux 行为未验证。
- `package.encoding` 文件与非 UTF-8 编码解析（`Parser.parse(filename, encoding)`）未探针。
- `Error.failOnErrorMsg` 只按消息级别判断（Error.mo:1745 文档），未逐条核对 WARNING 级 `assertionOrAddSourceMessage` 不 fail（探针行为与此一致）。
- `index.json` 缺失/用户库目录不在 MODELICAPATH 时第 1 层退化为原串（PackageManagement.mo:298-304），未单独探针（`Foo` 案例等价覆盖"库不在索引"）。

## 对 ymc S2 加载器的建议
1. **路径来源**：只读 `OPENMODELICALIBRARY`/显式 `setModelicaPath`，忽略 `MODELICAPATH`；默认 `%APPDATA%/.openmodelica/libraries/`；分隔符 Windows `;`；输出路径统一正斜杠（与 OMC 的 `getSourceFile` 可 diff）。
2. **条目扫描**（对齐 systemimpl.c:2106）：`Name`、`Name ver`（目录须含 `package.mo`）、`Name.mo`、`Name ver.mo`；`splitVersion` 6 级整数 + extra；`+…` 视为无 extra；保留 `-…`。
3. **版本选择接口**：`resolve(name, prios: list<string>, exact: bool) -> (dir, entry, isDir)`。规则：prios 顺序优先；`"default"` → extra ∈ {"", "+", "-"} 的最高版；数字请求 → `for j=6..(exact?6:0)`：前 j 级相等 ∧ (j==6 ∨ ≥请求) ∧ extra 合法 → 最高者；否则精确 6 级相等 + extra 前缀匹配。**是否复刻 index.json 第 1 层**（含 trunk=0.0.0 匹配一切的陷阱）应作 ADR：建议 ymc 不引入索引层（无索引时 OMC 本身也退化为纯 C 层），但在 spec TBD-05 记录 OMC 有索引时 3.2.4/3.3/2.0 失败而 2.2.2 成功的差异，diff 工具把这些用例列为"预期不一致"。
4. **uses**：装载完成后立即递归（深度优先），`forceLoad=false`；找不到 → 通知级消息；已装载同名包 → 按已装包 conversion 注解分级（相等静默 / noneFromVersion 通知 / from 警告 / 无注解按新旧警告）；显式 loadModel 重装覆盖；uses 只认 `Name(version="s")` 单参数形式。
5. **package.order**：trim/去空行/查重；顺序合并算法照抄 `getPackageContentNamesinElts` 的五个 case；未列出→警告+末尾追加；列出不存在→警告+**整份丢弃退回默认序**（可考虑更合理的"跳过该行"但需在 diff 中标注为 ymc 差异）；默认序=逆字典序+自身元素在末（若 ymc 改用正序需登记为已知不一致）；`X.mo` 与 `X/` 并存→错误并放弃该包。
6. **文件级校验**：一文件一类；类名/within 与目录推导值比对，大小写不同仅警告；within 错误使**整库**失败；`package.mo` 须为 PARTS；接受 BOM，默认 UTF-8，支持 `package.encoding`；`.moc` 直接报"不支持加密库"。
7. **符号表更新**：顶层头插（新装在前）、同名替换；`within W` 插入时 `W` 不存在 → 报 "Failed to insert class" 并列出现有顶层类。
8. **getClassNames 对拍**：实现先序+root 首项；`includeConstants` 只在非递归或叶级使用，递归+常量的 OMC 结果不可作 oracle（记入难点账本）。
