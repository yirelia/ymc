# R10 — MLS 3.6 包→文件系统映射 / MODELICAPATH / package.order / 版本处理 原文要点

- 来源 PDF：`C:\Program Files\Dymola 2025x Refresh 1\Documentation\ModelicaSpec36.pdf`（元数据标题 "Modelica® Language Specification version 3.6"，pdfTeX 2023-03-09，共 366 页）。
- 读取方式：Read 工具无法渲染该 PDF（环境缺 pdftoppm），改用 PyMuPDF 抽取同一文件的文本层逐页阅读；以下英文原句均照抄抽取文本（仅去掉换行）。
- 页码约定：**"PDF 第 N 页" = 阅读器物理页（1 起）；页脚印刷页码 = N − 1**（已用书签核对：书签 "13 Packages" → 物理页 208，页脚印 207）。
- 注意：任务卡里写的 "13.4.1 单文件 / 13.4.2 目录 / 13.4.3 MODELICAPATH" 与 3.6 实际编号不同。3.6 实际为：**13.3 MODELICAPATH、13.4.1 目录、13.4.2 单文件、13.4.3 within**。下文一律用 3.6 实际编号。

## 章节定位（节号 → PDF 页 / 印刷页）

| 节 | 标题 | PDF 页（印刷页） |
|---|---|---|
| 4.5 | Class Declarations（class-definition 文法，含 `encapsulated`） | 48（47） |
| 4.5.1 | Short Class Definitions（encapsulated 短类定义） | 49（48） |
| 4.6 | Specialized Classes（package 只能含类与常量） | 51（50） |
| 5.3.1 | Simple Name Lookup（encapsulated 截断查找） | 66（65） |
| 5.3.3 | Global Name Lookup（`.A` 全局查找） | 67（66） |
| 13 | Packages（章首） | 208（207） |
| 13.2 / 13.2.1 / 13.2.2 | Importing Definitions / Lookup of Imported Names / Rules for Import-Clauses | 208–209（207–208） |
| 13.3 | The Modelica Library Path – MODELICAPATH | 210（209） |
| 13.4 | File System Mapping of Package/Class（UTF-8 / BOM） | 211（210） |
| 13.4.1 | Directory Hierarchy Mapping（package.mo / package.order） | 211（210） |
| 13.4.2 | Single File Mapping | 211（210） |
| 13.4.3 | The within Clause | 211–212（210–211） |
| 13.5 | External Resources（`modelica://` URI） | 212（211） |
| 18.8 / 18.8.1 / 18.8.2 | Versions / Version Numbering / Version Handling | 288（287） |
| 18.8.2.1 | Conversion Rules | 289–292（288–291） |
| 18.8.3 | Versions in the File System | 292（291） |
| 18.8.4 | Version Date and Build Information | 292–293（291–292） |
| 18.9 / 18.9.1 / 18.9.2 | Access Control / Protection of Classes / Licensing | 294–297（293–296） |
| A.2.1 | Stored Definition – Within（文法） | 302（301） |
| D.3 | Modelica 3.4 变更记录（package.order / 多类文件的澄清来源） | 333（332） |

## 逐条原文要点（英文原句 + 中文）

### 1. 单文件映射：`X.mo` 与 `within`

- MLS 3.6 §13.4.2（PDF 第 211 页）："When mapping a package or class-hierarchy to a file (e.g., the file A.mo), that file shall only define a single class A with a name matching the name of the nonstructured entity. In a file hierarchy the files shall have the extension .mo."
  → 文件 `A.mo` 只能定义一个类，且类名必须等于文件基名；扩展名必须是 `.mo`。
- §13.4.2（PDF 211）："A .mo file defining more than one class cannot be part of the mapping to file-structure and it is an error if it is loaded from the MODELICAPATH."
  → 多类文件不属于文件系统映射；**从 MODELICAPATH 加载多类文件是错误**（文法 A.2.1 本身允许一个 stored-definition 含多个 class-definition，所以用户显式打开的多类文件不在此禁令内——这是推论，规范未明说）。
- §13.4.3（PDF 211–212）：语法 `within [ packageprefixname ] ";"`。"A non-top-level entity shall begin with a within-clause which for the class defined in the entity specifies the location in the Modelica class hierarchy. A top-level class may contain a within-clause with no name. For a sub-entity of an enclosing structured entity, the within-clause shall designate the class of the enclosing entity; and this class must exist and must not have been defined using a short class definition."
  → 非顶层条目必须以 within 开头且指向**包围目录对应的类**；该类必须存在、不能是短类定义；顶层类可以写空 `within;`。
- §13.4.3 示例（PDF 212）：`within Modelica.Mechanics; package Rotational` → 全名 `Modelica.Mechanics.Rotational`，"formed by concatenating the packageprefixname with the short name of the package"。
- A.2.1（PDF 302）文法："stored-definition : [ within [ name ] ";" ] { [ final ] class-definition ";" }"
  → within 可省略、可无名；每个 class-definition 前可有 `final`。
- §13.4（PDF 211）编码："Each Modelica file in the file-system is stored in UTF-8 format ... A deprecated feature is that the file may start with the UTF-8 encoded BOM (byte order mark; 0xef 0xbb 0xbf); this is treated as white-space in the grammar. Since the use of BOM is deprecated, tools can ignore any BOM when reading, and it is recommended to never write it."

### 2. 目录映射：`package.mo` 与 `package.order`

- §13.4.1（PDF 211）："A directory shall contain a node, the file package.mo. The node shall contain a stored-definition that defines a class A with a name matching the name of the structured entity."
  → 目录必须有 `package.mo`，其中定义与目录同名的类。
- §13.4.1（PDF 211）："[The node typically contains documentation and graphical information for a package, but may also contain additional elements of the class A.]"
  → package.mo 除包头外可以直接含该类的其它元素（类、常量）。
- §13.4.1（PDF 211）："A directory may also contain one or more sub-entities (directories or files). The sub-entities are mapped as elements of the class defined by their enclosing structured entity. Two sub-entities shall not define classes with identical names"
  示例："If directory A contains the three files package.mo, B.mo and C.mo, the classes defined are A, A.B, and A.C." / "A directory shall not contain both the sub-directory A and the file A.mo."
  → 子目录/子文件 = 该类的元素；同名冲突（`A/` 与 `A.mo` 并存）= 违反 shall。
- §13.4.1（PDF 211）package.order 全文："In order to preserve the order of sub-entities it is advisable to create a file package.order where each line contains the name of one class or constant (using its Modelica IDENT form). If a package.order is present when reading a structured entity the classes and constants are added in this order; if the contents does not exactly match the classes and constants in the package, the resulting order is tool specific and a warning may be given. Classes and constants that are stored in package.mo are also present in package.order but their relative order should be identical to the one in package.mo (this ensures that the relative order between classes and constants stored in different ways is preserved)."
  中文逐点：
  - **可选**（"advisable"），不是必需。
  - 每行一个名字，用 Modelica IDENT 形式（含引号标识符时带 `'…'`）。
  - 存在时：类与常量**按该顺序加入**，顺序即元素顺序。
  - 应**精确匹配**包内全部类与常量（包括写在 package.mo 里的常量和类，也包括子包——子包也是类）；不匹配（少列/多列）→ 顺序 tool specific，**可**给警告；不是错误。
  - package.mo 内元素在 package.order 中的相对顺序应与 package.mo 内一致。
  - 空行、注释、编码、行尾：规范未提。
- §4.6（PDF 51）："package – May only contain declarations of classes and constants. Enhanced to allow import of elements of packages."；§13 章首（PDF 208）："Parameters and variables cannot be declared in a package."
  → package.order 的条目集合 = 类 ∪ 常量，与包允许的元素集合一致。
- D.3（PDF 333）变更记录："Clarified 'structured entity' to be 'directory', section 13.4. Ticket #922." / "Clarified handling of incorrect package.order, section 13.4.1. Ticket #1858." / "Restricted use of files with multiple classes, section 13.4.2. Ticket #1854."（均为 3.4 引入）

### 3. MODELICAPATH

- §13.3（PDF 210）："The top-level scope implicitly contains a number of classes stored externally. If a top-level name is not found at global scope, a Modelica translator shall look up additional classes in an ordered list of library roots, called MODELICAPATH."
  → 有序根列表；只在顶层名在全局作用域找不到时触发（按需加载）。
- §13.3（PDF 210）分隔符："[The implementation of MODELICAPATH is tool dependent. In order that a user can work in parallel with different Modelica tools, it is advisable to not have this list as environment variable, but as a setting in the respective tool. Since MODELICAPATH is tool dependent, it is not specified in which way the list of library roots is stored. Typically, on a Windows system MODELICAPATH is a string with path names separated by ';' whereas on a Linux system it is a string with path names separated by a ':'.]"
  → 分隔符**未规范化**：仅"typically" Windows `;`、Linux `:`。
- §13.3（PDF 210）："In addition a tool may define an internal list of libraries, since it is in general not advisable for a program installation to modify global environment variables. The version information for a library (as defined in section 18.8) may also be used during this search to search for a specific version of the library (e.g., if Modelica library version 2.2 is needed and the first directory in MODELICAPATH contain Modelica library version 2.1, whereas the second directory contains Modelica version 2.2, then Modelica library version 2.2 is loaded from the second directory.)."
  → 版本信息**可以**（may）参与搜索：需要 2.2 时跳过含 2.1 的前一根目录。这是唯一的"按 uses 选版本"依据，且是 may。
- §13.3（PDF 210）搜索顺序与不回溯："[The first part of the path A.B.C (i.e., A) is located by searching the ordered list of roots in MODELICAPATH. If no root contains A the lookup fails. If A has been found in one of the roots, the rest of the path is located in A; if that fails, the entire lookup fails without searching for A in any of the remaining roots in MODELICAPATH.]"
- §13.3 示例（PDF 210）："For the search to succeed, there must be a subdirectory MyLib or a file MyLib.mo in one of the directories mentioned in the MODELICAPATH. If there is no such subdirectory or file, the lookup fails." … "This subdirectory must have a file package.mo containing a definition of the package MyLib"
  示例 MODELICAPATH = "C:\library;C:\lib1;C:\lib2"，根可为目录 `Modelica/` 或单文件 `ComplexNumbers.mo`。
- §18.8.3（PDF 292）同名多版本的命名："A top-level class, IDENT, with version VERSION-NUMBER can be stored in one of the following ways in a directory given in the MODELICAPATH:" 四种：`IDENT ".mo"`（Modelica.mo）/ `IDENT " " VERSION-NUMBER ".mo"`（Modelica 2.1.mo）/ 目录 `IDENT` 含 package.mo（Modelica/package.mo）/ 目录 `IDENT " " VERSION-NUMBER` 含 package.mo（Modelica 2.1/package.mo）。"This allows a tool to access multiple versions of the same package."
  → 名字与版本之间**恰一个空格**；VERSION-NUMBER 本身可含空格（预发布形式，见 §18.8.1）。
- 选择规则（"newest"/"highest"）：**未找到**（全文无 newest / latest version / highest version）。大小写敏感性：**未找到**（§13.3/13.4 无任何说明；§13.5 只说 URI scheme 名不区分大小写）。

### 4. 版本处理 §18.8

- §18.8（PDF 288）："A top-level package or model can specify the version of top-level classes it uses, its own version number, and if possible how to convert from previous versions. This can be used by a tool to guarantee that consistent versions are used, and if possible to upgrade usage from an earlier version to a current one."
- §18.8.1（PDF 288）版本号三种形式：
  - Main release：`""" UNSIGNED-INTEGER { "." UNSIGNED-INTEGER } """`，例 "2.1"（**段数不限**，x.y.z 只是常见）。
  - Pre-release：`""" UNSIGNED-INTEGER { "." UNSIGNED-INTEGER } " " {S-CHAR} """`，例 "2.1 Beta 1"（主版本 + 一个空格 + 任意串）。
  - Un-ordered：`""" NON-DIGIT {S-CHAR} """`，例 "Test 1"。
  - 排序："The main release versions are ordered using the hierarchical numerical names, and follow the corresponding pre-release versions. The pre-release versions of the same main release version are internally ordered alphabetically."
    → 数值分段比较；"2.1" 排在 "2.1 Beta 1" 之后（正式 > 预发布）；同主版本的预发布按字母序。
- §18.8.2（PDF 288）："In a top-level class, the version number and the dependency to earlier versions of this class are defined using one or more of the following annotations:"
  - `version = CURRENT-VERSION-NUMBER`："Defines the version number of the model or package. All classes within this top-level class have this version number."
  - `conversion(noneFromVersion = VERSION-NUMBER)`："Defines that user models using the VERSION-NUMBER can be upgraded to the CURRENT-VERSION-NUMBER of the current class without any changes."
  - `conversion(from(version = Versions, [to=VERSION-NUMBER,] Convert))`，"where Versions is VERSION-NUMBER | {VERSION-NUMBER, VERSION-NUMBER, . . .} and Convert is script=". . ." | change={conversionRule(), . . ., conversionRule()}"；"Defines that user models using the VERSION-NUMBER or any of the given VERSION-NUMBER can be upgraded to the given VERSION-NUMBER (if the to-tag is missing this is the CURRENT-VERSION-NUMBER) of the current class by applying the given conversion rules. The script consists of an unordered sequence of conversionRule(); (and optionally Modelica comments)." "[The to-tag is added for clarity and optionally allows a tool to convert in multiple steps.]"
  - `uses(IDENT (version = VERSION-NUMBER [, versionBuild=INTEGER] [, dateModified=STRING] ) )`："Defines that classes within this top-level class uses version VERSION-NUMBER of classes within the top-level class IDENT."
  - "The annotations uses and conversion may contain several different sub-entries."（一个 uses 可列多个库；一个 conversion 可含多个 noneFromVersion/from）
- §18.8.2 示例（PDF 288–289）：Modelica `version = "3.1"`，`conversion(noneFromVersion = "3.1 Beta 1", noneFromVersion = "3.1 Beta 2", from(version = {"2.1", "2.2", "2.2.1"}, script = "convertTo3.mos"), from(version = "1.5", script = "convertFromModelica1_5.mos"))`；model A `uses(Modelica(version = "1.5"))`，model B `uses(Modelica(version = "3.1 Beta 1"))`。"In this example the model A uses an older version of the Modelica library and can be upgraded using the given script, and model B uses an older version of the Modelica library but no changes are required when upgrading."
- §18.8.2.1（PDF 289）转换规则函数 convertClass / convertClassIf / convertElement / convertModifiers / convertMessage："The calls of these functions do not directly convert, instead they define conversion rules" … "The order between the function calls does not matter, instead the longer paths (in terms of number of hierarchical names) are used first as indicated below, and it is an error if there are any ambiguities." "All of these convert-functions only use inheritance among user models, and not in the library that is used for the conversion"。（S2 只需能解析/保留这些注解；执行转换是 GUI/脚本层）
- §18.8.4（PDF 292–293）四个附加注解的声明串：`String versionDate "UTC date of first version build (in format: YYYY-MM-DD)"`；`Integer versionBuild "Larger number is a more recent maintenance update"`；`String dateModified "UTC date and time of the latest change to the package in the following format (with one space between date and time): YYYY-MM-DD hh:mm:ssZ"`；`String revisionId "Revision identifier of the version management system used to manage this library. ..."`。
- §18.8.4（PDF 293）versionBuild 语义与**唯一的"不匹配"规定**："versionBuild is the optional build number of the library. When a new version is released versionBuild should be omitted or versionBuild = 1. ... There are no conversions between the same versions with different build numbers. Two releases of a library with the same version but different versionBuild are in general assumed to be compatible. In special cases, the uses-clause of a model may specify versionBuild and/or dateModified. In such a case the tool is expected to give a warning if there is a mismatch between library and model."
  → 只有 versionBuild/dateModified 不匹配才"expected to give a warning"；`version` 不匹配时的行为规范**未规定**。
- §18.8.4（PDF 293）："The versionBuild and dateModified annotations can also be specified in the uses annotation (together with the version number). [It is recommended that tools do not automatically store versionBuild and dateModified in the uses annotation.]"；dateModified："[The intention is that a Modelica tool updates this annotation whenever the package or part of it was modified and is saved on persistent storage ...]"

### 5. 其它与加载相关

- import 形式（§13.2，PDF 208–209）：`import definitionname;` / `import packagename.definitionname;` / `import packagename.{def1, ..., defn};` / `import packagename.*;` / `import shortname = definitionname;` / `import shortname = packagename.definitionname;`。"It is the only way of referring to definitions declared in some other package for use inside an encapsulated package or class."
- §13.2.1（PDF 209）："lookup starts with the lexical lookup of the first part of the name (A) at the top level." "Qualified import-clauses may only refer to packages or elements of packages, i.e., in import A.B.C; or import D = A.B.C;, A.B must be a package. Unqualified import-clauses may only import from packages, i.e., in import A.B.*;, A.B must be a package." "[In import A; the class A can be any class which is an element of the unnamed top-level package.]"
- §13.2.2（PDF 209）六条规则：not inherited；不是命名元素（不能被 modifier/redeclare 改）；顺序无关；只能从 package 导入（但 package 和 class 都可含 import）；"An imported package or definition should always be referred to by its fully qualified name in the import-clause."；"Multiple qualified import-clauses shall not have the same import name (see section 5.3.1)."
- import 触发按需加载（§13.3 示例，PDF 210）："During lookup we first try to find a package MyLib corresponding to the first part of the name in the import-statement. It is not found in the top-level scope since it has not previously been loaded into the environment. Since the name was not found in the top-level scope the search continues in the directories in the MODELICAPATH in the specified order."
- encapsulated（§5.3.1，PDF 66）："A class declared with the keyword encapsulated (see class-definition in the grammar) is called an encapsulated class. By restricting name lookup inside a restricted class in ways defined in this chapter, the meaning of the class is made independent of where it is placed in a package hierarchy." "... until a match is found or an enclosing class is encapsulated. In the latter case the lookup stops except for the predefined types, functions and operators defined in this specification. For these cases the lookup continues in the global scope, where they are defined."
  §5.3.3（PDF 67）：`.A` 全局查找 "is possible even if the class is encapsulated and import-clauses are not used for this."
  §4.5.1（PDF 49）："if the short class definition is declared as encapsulated, then the type-specifier and modifiers follow the rules for encapsulated classes and cannot be looked up in the enclosing scope."
  → encapsulated 只影响名字查找；对文件加载无任何规定（未找到）。
- Protection（§18.9.1，PDF 294–295）：`annotation(Protection(access = Access.documentation));`，`Access = enumeration(hide, icon, documentation, diagram, nonPackageText, nonPackageDuplicate, packageText, packageDuplicate)`。"if a class is encrypted and no Protection annotation is defined, the access annotation has the default value Access.documentation"。"The access annotation holds for the respective class and all classes that are hierarchically on a lower level, unless overridden by a Protection annotation with access. Overriding access=Access.hide or access=Access.packageDuplicate has no effect." §18.9（PDF 294）："Obfuscation and encryption are not standardized." "An encrypted package that has the Protection annotation is read-only"。
  §18.9.2（PDF 296–297）Licensing：`Protection(features={...}, License(libraryKey=..., licenseFile=...))`；授权文件是 "a Modelica package without classes" 带 `Authorization(...)` 注解（libraryKey / licensor / license[:]{licensee, id[:], features[:], startDate, expirationDate, operations[:]}）。
- `.moe`：**未找到**（全文检索无 ".moe"）。
- `modelica://`（§13.5，PDF 212）："Any Modelica-scheme URI containing a slash after the package-name is interpreted as a reference to a resource. The authority portion of the URI is interpreted as a fully qualified package name and the path portion of the URI is interpreted as the path (relative to the package) of the resource." "The first part of the path shall not be the name of a class in the package given by the authority." 目录存储时 `modelica://A/Resources/C.jpg` → `A/Resources/C.jpg`；带版本目录时 "modelica://Modelica.Mechanics/C.jpg is Modelica 3.2.1/Mechanics/C.jpg – regardless of whether Modelica.Mechanics is stored in Modelica 3.2.1/package.mo, Modelica 3.2.1/Mechanics.mo, or Modelica 3.2.1/Mechanics/package.mo."；单文件包 `A.mo` 的资源与 A.mo 同目录（"not recommended"）。引号标识符中的 `'` 与保留字符需百分号编码。

## 未找到（全文检索 + 精读相关页均无）

1. 文件/目录名与类名匹配的大小写规则。
2. MODELICAPATH 内同名多版本（`Modelica 3.2.1/`、`Modelica 4.0.0/`）的默认选择规则（无 "newest/highest"）；只有 §13.3 "版本信息 may 参与搜索" 的示例。
3. `uses(...(version=...))` 不匹配时的规范行为（警告/错误/自动转换）；只有 versionBuild/dateModified 不匹配 → 期望警告。
4. 同一顶层类内多个 `uses` 指向同一库不同版本的冲突处理。
5. package.order 缺失时的默认顺序、空行/注释/行尾/编码、非类非常量条目。
6. `.moe`/加密文件格式（"Obfuscation and encryption are not standardized"）。
7. `encapsulated` 对加载的影响。
8. 目录下未被引用的 .mo 是否必须加载（规范只描述"按名查找→找不到→搜 MODELICAPATH"，未规定懒加载或全量加载）。
9. `within` 与实际目录不一致时的诊断级别（只有 shall，无 error/warning 措辞）。

## 对 ymc 的含义

**MUST（规范 shall / is an error，测试可直接引用）**
1. 目录 ⇒ 必含 `package.mo`，其 stored-definition 定义与目录同名的类；子目录/子文件是该类的元素；两个子条目同名（含 `A/` 与 `A.mo` 并存）→ 违规。（§13.4.1，PDF 211）
2. `X.mo` ⇒ 只定义一个类且名为 X；从 MODELICAPATH 加载的多类文件 → error；扩展名 `.mo`。（§13.4.2，PDF 211）
3. 非顶层文件必须以 `within <包围类全名>;` 开头，该类必须存在且非短类定义；顶层允许 `within;`。（§13.4.3，PDF 211–212）
4. UTF-8；开头 BOM 视作空白、可忽略。（§13.4，PDF 211）
5. `package.order` 存在时按其顺序加入类与常量；不匹配 → 顺序自定 + 可警告（不得报 error）。（§13.4.1，PDF 211）
6. MODELICAPATH 是有序根列表；顶层名找不到才搜；在某根找到 A 后其余部分只在 A 内找，失败不回溯其它根。（§13.3，PDF 210）
7. 必须识别 §18.8.3 四种存储形式，`IDENT␠VERSION` 单空格分隔；VERSION 可含空格（预发布）。（PDF 292）
8. 版本号三形式与排序（主版本数值分段、正式 > 同号预发布、预发布按字母序）。（§18.8.1，PDF 288）
9. import 六形式；首段从顶层查；`A.B` 必须是 package；六条规则（不继承、非命名元素、顺序无关、同名 import 冲突）。（§13.2–13.2.2，PDF 208–209）
10. encapsulated 截断名字查找，预定义类型/函数/运算符与 `.A` 例外。（§5.3.1/§5.3.3，PDF 66–67）
11. `uses` 里的 versionBuild/dateModified 与库不一致 → 警告。（§18.8.4，PDF 293）
12. S2 需能解析并保留 `version / conversion(noneFromVersion, from(version, to, script|change)) / uses(...) / versionDate / versionBuild / dateModified / revisionId / Protection(...)` 注解结构（§18.8.2、§18.8.4、§18.9）。

**工具自定（规范明写 tool dependent / tool specific / may / 未规定 → 写 ADR 或 open-questions）**
- MODELICAPATH 的存储与分隔符（建议 Windows `;`、Linux `:`，且建议做成工具设置而非仅环境变量）；可附加内部库列表。（§13.3）
- package.order 不匹配时的具体顺序与是否警告；缺失时的顺序（建议：按目录名排序并记录，OMC 行为另测）。
- 同名多版本目录的选择策略（建议：有 `uses` 时按 §13.3 示例选匹配版本；无 `uses` 时 ymc 自定并记入 ADR）。
- `uses` 的 `version` 不匹配的处理（警告/自动转换/拒绝）；多 uses 冲突。
- 文件名大小写匹配策略（规范未说；Windows 文件系统不区分大小写，需自定并记入 open-questions）。
- 加密/混淆格式（.moe 等）完全不标准化；ymc 可不支持。
- 是否懒加载：规范只描述按名查找，ymc 可自选（懒加载符合 §13.3 描述的"未加载→再搜"流程）。
