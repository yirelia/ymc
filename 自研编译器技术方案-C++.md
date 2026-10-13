# 自研 Modelica 编译器技术方案（C++）

- 基线：OMC 源码 `feat_OMEncryption` @ `3ceb0c9d06`（工作区 `build_cmake/install_cmake/bin/omc.exe` = `v1.27.0-dev-372-g3ceb0c9d06`）；安装版 `C:\Program Files\OpenModelica1.25.0-64bit`；MSL `C:\Users\yangrui\AppData\Roaming\.openmodelica\libraries\Modelica 4.0.0+maint.om`
- 规范：Modelica Language Specification 3.6（下文写作 MLS §x.y）
- 日期：2026-09-12（2026-09-14 连同全部相关文档移入代码仓库 `C:\dev\ymc`；OMC 源码留在 `C:\dev\OpenModelica` 仅作参考）

---

## 0. 基调与六条约束 → 设计原则

| 你的约束 | 设计原则 | 落地机制（本文对应章节） |
|---|---|---|
| 1 C++ 干中学 | 每个阶段只引入这个阶段**必须**的 C++ 特性；不用的特性明确禁用 | §3.4 编码规范、每阶段的"C++ 学习点" |
| 2 核心代码手写 | 分工写死：编译器核心（解析器、实例化、后端算法、代码生成）你写；基建（构建、测试运行器、oracle 脚本、dump/diff 工具）我写；我只提供**规格、骨架、评审**，不替你写核心 | §7 协作规范、每阶段"手写清单 / 我提供" |
| 3 OMC 能跑的用例全部支持 | 验收对象 = OMC `testsuite/`（3934 个 `.mos` + 2641 个 `.mo`），分三级验收；不在范围的用例**显式标 skip 并计数**，不是悄悄漏掉 | §1、§6 |
| 4 编译原理路线清晰、分阶段可落地 | 九个阶段，每阶段有可运行产物、验收命令、退出条件；阶段之间用**文本 IR**隔开，任何阶段可从文件单独启动 | §2、§5 |
| 5 不臆想、有落地规范 | 任何语义结论只允许三种来源：MLS 章节号 / OMC 函数（文件:行）/ 探针模型在 OMC 上实跑；写不出来源的就标"待验证"并写验证方法 | §3.6、§8 |
| 6 全部可调试 | 12 条可调试规范，从数据结构（稳定 Id、SourceLoc）到构建（ASan、natvis）到运行（dump-after、trace） | §3.5 |

---

## 1. 目标与验收总纲

### 1.1 对标对象的真实体量（2026-09-12 统计）

| 目录 | `.mos` | `.mo` | 说明 |
|---|---|---|---|
| `testsuite/simulation` | 2626 | 438 | 编译 + 仿真 + 数值比对；子目录按编译器特性分类（见 §5 各阶段） |
| `testsuite/openmodelica` | 1155 | 841 | 脚本 API、FMI、优化、接口——**大部分不在编译器核心范围** |
| `testsuite/flattening` | 153 | 2641 | 压平文本比对；`.mo` 文件自带 `// Result:` 期望块 |
| `testsuite/metamodelica` | 126 | 162 | MetaModelica 语言本身，**不在范围** |

状态分布（`.mos`）：`correct` 3711、`erroneous` 19、`incorrect` 10。

### 1.2 两个决定验收方式的事实

1. **2030 个 `.mos` 的 `// cflags:` 含 `-d=-newInst`**（钉在 OMC 旧前端上）。旧前端输出的压平文本和新前端不同（变量顺序、化简程度、注释）。自研编译器只实现一套语义（对齐新前端 `NFFrontEnd/`），所以这些用例**只做数值级验收**，不做文本级。
2. `// cflags: +simCodeTarget=Cpp` 316 个：C++ 运行时目标，**不做**，标 skip。`-d=newInst` 244、`--newBackend` 72、`+d=backenddaeinfo` 48 是主要的其它 flag。

### 1.3 三级验收

| 级别 | 什么算通过 | 适用 |
|---|---|---|
| **L1 文本一致** | 自研输出经归一化后与用例 `// Result:` 块一致（归一化规则 §6.2） | `flattening/**/*.mo`（2641）、`.mos` 中 `instantiateModel`（179 个文件） |
| **L2 数值一致** | 仿真结果 `.mat` 与 OMC 参考结果在容差内一致；`val(x, t)` 值一致 | `simulation/**/*.mos`（2626）；OMC 用例本身用 `compareSimulationResults`（旧）/ `diffSimulationResults`（新，75 个文件）/ `val`（700 个文件） |
| **L3 脚本一致** | `.mos` 逐语句输出与 `// Result:` 一致（`true`、`""`、record 打印格式……） | 所有 `.mos`；依赖 §6.3 的脚本 API 子集 |

### 1.4 明确排除（标 skip，计数）

`optimize`（61 个文件，动态优化）、`getModelInstance`（89，OMEdit 的 JSON 实例 API）、`buildModelFMU`/`importFMU`（115/53，放 S8）、`+simCodeTarget=Cpp`（316）、`hpcom`（19）、`parmodauto`（2）、`qss`（9）、`metamodelica/`（全部）、`openmodelica/` 下非编译器核心的接口测试（逐目录评估后列清单，S4 时定）。

### 1.5 MSL 回归

- 压平：`Modelica 4.0.0+maint.om` 下所有 `Examples` 模型 `instantiateModel` 与 OMC 文本 diff（L1）。
- 仿真：MAP-LIB 官方参考结果（外部仓库 `Modelica/MAP-LIB_ReferenceResults`，待克隆）做 L2。

---

## 2. 总体架构

### 2.1 分层与阶段间契约

```
 源码 .mo ──S1──▶ Absyn ──S2──▶ SCode ──S3──▶ FlatModel ──S5──▶ BackendDAE(因果化) ──S6──▶ SimCode ──▶ C 源码
              │            │             │                   │                        │
              ▼            ▼             ▼                   ▼                        ▼
           .absyn 文本   .scode 文本   Base Modelica 文本    .bdae 文本               （C 本身就是文本）
           (unparse)                   (MLS 附录 / OMC --baseModelicaOptions)  (对齐 OMC -d=bltdump 格式；块结构只在 --newBackend 下有，ADR-008)

 每个箭头 = 一个可单独运行的 pass：  ymc --from=<ir文件> --run=<pass> --dump-after=<pass>
```

**契约**：每种 IR 有且只有一种文本格式，同时有 `dump` 和 `parse`（至少 Base Modelica 和 `.bdae` 两种必须可 parse）。这样后端可以拿 OMC 产的 Base Modelica 文件启动，前端没写完也能开工；调试时可以把出错阶段的输入存成文件反复重放。

### 2.2 与 OMC 的两条接缝

1. **FlatModel / Base Modelica**：OMC `--baseModelicaOptions` 能把 NF 的 FlatModel 打成标准文本（`NFInst.mo:227-229 / :247-249` 调 `InstUtil.dumpFlatModel`）。自研前端的验收 oracle、自研后端的输入格式，都用它。
2. **运行时回调表 ABI**：`C:\Program Files\OpenModelica1.25.0-64bit\include\omc\c\openmodelica_func.h:56` `struct OpenModelicaGeneratedFunctionCallbacks`（`performSimulation` … `functionODE` :109、`functionAlgebraics` :113、`functionDAE` :117、`functionInitialEquations` :163 …）。S6 的代码生成填这张表、链接 OMC 的 `libSimulationRuntimeC`，就能借用 OMC 全部求解器/事件/结果文件做 L2 验收；S7 再逐块换成自研运行时。

### 2.3 进程模型

单可执行 `ymc`（名字待定）：`ymc file.mo`（等价 OMC `translateFile`，`Main.mo:391`）、`ymc script.mos`（S4 脚本模式）、`ymc --from/--run/--dump-after`（pass 隔离模式）。无守护进程、无 CORBA/ZMQ（OMEdit 接口不在范围）。

---

## 3. C++ 工程规范

### 3.1 工具链

| 项 | 决定 | 理由 |
|---|---|---|
| 语言标准 | **C++20**，禁 modules（工具链不稳） | designated initializers、`std::span`、`<=>`、concepts（限基建） |
| 编译器（2026-09-13 按本机实测定稿） | **默认 GCC**（TDM-GCC 10.3，命令行 + gdb，快速回归）；**MSVC**（VS 2022，图形调试器 + 原生 ASan）——两套都必须零警告；clang 在 OMC 自带的 MSYS2 UCRT64 里（`tools\msys\ucrt64\bin`），需要时再加预设 | 三套预设（`gcc-debug` / `msvc` / `msvc-asan`）已在 `C:\dev\ymc` 实证全绿 |
| 构建 | CMake 3.29 + `CMakePresets.json`；GCC 用 MinGW Makefiles，MSVC 用 VS 生成器（不需要 vcvars） | |
| Debug 预设 | GCC：`-O0 -g3 -fno-omit-frame-pointer -Werror=switch -Werror=return-type -Wall -Wextra -Wshadow`；MSVC：`/W4 /permissive- /Zc:__cplusplus /we4062 /we4715` | 两套的穷尽检查等价 |
| ASan 预设 | `msvc-asan`：`/fsanitize=address`，运行时 DLL 由 CMake 自动拷到 exe 旁；已验证能抓 heap-buffer-overflow | 每周至少全量跑一次 testsuite；UBSan 留给 clang 预设 |
| 格式/静态检查 | clang-format（固定配置）+ clang-tidy（`bugprone-*`, `performance-*`, `readability-*` 子集） | |

### 3.2 依赖白名单

非核心代码可用：`fmt`（格式化）、`doctest`（单测）、`cpptrace`（崩溃栈）。
**禁止**：Boost、LLVM/MLIR（S8 之前）、任何 CAS/符号库、任何解析器生成器（ANTLR/bison）、`std::regex` 于热路径。
运行时（C）可链：SUNDIALS、LAPACK/OpenBLAS、SuiteSparse(KLU)、ModelicaExternalC（S7）。

### 3.3 目录结构

```
ymc/
  CMakeLists.txt  CMakePresets.json  .clang-format  .clang-tidy
  src/
    base/      Symbol/Interner, Arena/Id, SourceLoc, Diagnostics, Logger, Dump 基础设施   ← 我写骨架，你可改
    absyn/     S1 语法树 + unparse
    parse/     S1 词法 + 递归下降
    scode/     S2 归一化 + 包加载
    inst/      S3 查找/实例化/修饰/类型/压平/编译期求值
    flat/      FlatModel + Base Modelica dump/parse
    bdae/      S5 变量/方程/分区/强连通分量 + 各 pass
    simcode/   S6 分桶
    codegen/   S6 C 打印器
    script/    S4 .mos 解释器子集
    driver/    CLI、pass 管理、dump-after
  runtime/     S7 自研 C 运行时（独立 CMake 目标）
  tests/
    unit/<pass>/   每个 pass 的最小模型 + 期望 dump（doctest + 文件快照）
    omc/           testsuite 运行器（Python）、omc-diff 复刻、.mat 比较器、skip 清单
  tools/         探针脚本、OMC 对照脚本
  docs/
    omc-导读/    OMC 源码导读 8 篇（参考实现的细读）
    tasks/       任务卡（一个手写任务一张）
    cpp/         C++ 学习材料（按阶段）
    adr/         不可逆决定的记录
  tasks/         todo.md（当前计划）、lessons.md（踩坑规则）
  ymc-设计总览.html          设计地图 + 难点账本
  自研编译器技术方案-C++.md   本文
```

### 3.4 编码规范（15 条，够用即止）

1. IR 数据 = `struct` + `enum class Kind` + `switch`（配 `-Werror=switch` 得到穷尽检查）；**不用**虚函数、RTTI、`dynamic_cast` 表示 IR。
2. IR 节点由 `Arena` 拥有，节点之间用 `Id<T>`（`uint32_t` 强类型）引用；**禁止** `shared_ptr` 出现在 IR 里，禁止裸 `new/delete`。
3. 服务对象（Interner、Diagnostics、各 pass）用 `std::unique_ptr` 或栈对象；一个 `Compilation` 上下文显式传参，**无全局可变状态**。
4. 标识符全部 intern 成 `Symbol`（`uint32_t`）；cref 同理（`CrefId`）；比较是整数比较。
5. 字符串：内部 `std::string_view` 指向 arena/interner；对外 API 才 `std::string`。
6. 异常只用于"编译器自身 bug"（`YMC_UNREACHABLE`）；模型错误一律走 `Diagnostics`，pass 返回后由 driver 决定是否继续。
7. `[[nodiscard]]` 标所有返回 Id/状态的函数；`const` 默认，可变性显式。
8. 容器：`std::vector` 优先；需要 map 时先问"输出顺序是否重要"——重要就用有序容器或插入序容器（§3.5 第 6 条）。
9. 每个 pass 一个头文件、一个入口函数 `run<Pass>(Compilation&, Input) -> Output`，无副作用地读输入、产新输出（允许原地改 arena，但要在 pass 文档里写明）。
10. 命名：类型 `PascalCase`，函数/变量 `snake_case`，常量 `kCamel`，枚举值 `PascalCase`；文件名 `snake_case.{h,cpp}`。
11. 头文件只放声明 + 小内联；模板只用于基建（`Arena<T>`、`Id<T>`），核心算法不写模板。
12. 断言：`YMC_ASSERT(cond, "msg with {}", args)` 在 Debug 生效、Release 保留（编译器正确性优先于速度）。
13. 注释写"为什么"，不写"做什么"；每个非平凡函数头注释引用来源（MLS §、OMC 文件:行）。
14. 一个 PR 一个 pass 或一个子功能；PR 描述附 dump 前后对比。
15. `auto` 只在类型显而易见（迭代器、`make_unique`）时用；公共 API 签名不写 `auto`。

### 3.5 可调试规范（12 条，全部强制）

1. **每个 IR 节点带 `SourceLoc`**（文件 Id、行、列、长度），从 Absyn 一路传到生成的 C（`#line` / `equationIndexes`）。
2. **稳定 Id**：Id 一旦分配永不复用；dump 里打印 Id，两次运行 Id 相同。
3. **`--dump-before/--dump-after=<pass>`**：任何 pass 前后可打印其 IR 文本；格式对齐 OMC 的 `-d=bltdump` 等，方便并排比。
4. **IR 文本可回读**（至少 Base Modelica、`.bdae`）：`--from=<file> --run=<pass>` 单独重放一个 pass。
5. **`--trace=<名字>`**：跟踪一个变量/方程穿过所有 pass（对齐 OMC `--debugFollowEquations`，`NBackendDAE.mo:284`）。
6. **确定性**：凡影响输出顺序的遍历，禁止走 `unordered_*`；哈希不带随机种子；同输入两次运行 byte-identical。
7. **Debug 构建**：`-O0 -g3`、迭代器调试、`-fno-omit-frame-pointer`；ASan 预设每周全量。
8. **natvis / pretty-printer**：`Symbol`、`Id<T>`、`Expr` 在 VS / lldb 里显示名字而不是整数（我提供 `.natvis` 和 lldb Python 脚本）。
9. **日志分类**：`--log=parse,inst,match,…`，每条日志带 pass 名和 SourceLoc。
10. **崩溃可定位**：`cpptrace` 打栈；`YMC_ASSERT` 失败打当前 pass、当前节点 Id 与 SourceLoc。
11. **测试粒度**：每个 pass 有 `tests/unit/<pass>/` 最小模型 + 期望 dump 快照；先写快照再写代码。
12. **pass 可幂等重跑**：同一 pass 对同一输入跑两次结果相同（方便在调试器里反复执行）。

### 3.6 不臆想规范

每条语义决定必须在代码注释或 `docs/adr/` 里标来源，三选一：

- `MLS §7.2.3`（规范章节）；
- `OMC NFInst.mo:1234 instantiateClass`（函数级引用，行号注明基线提交）；
- `probe: tools/probe/redeclare_order.mo` + OMC 输出片段（探针模型实跑）。

写不出来源 → 在 `docs/open-questions.md` 记"待验证 + 验证方法"，不得写代码"猜"。评审时首先查来源。

---

## 4. 核心数据结构（与 OMC 一一对应）

### 4.1 基础设施（S0，我写骨架）

```cpp
// base/symbol.h
struct Symbol { uint32_t id; auto operator<=>(const Symbol&) const = default; };
class Interner { public: Symbol intern(std::string_view); std::string_view str(Symbol) const; };

// base/arena.h
template<class T> struct Id { uint32_t raw; /* 强类型，禁止隐式转 int */ };
template<class T> class Arena { public: Id<T> add(T); T& operator[](Id<T>); const T& operator[](Id<T>) const; size_t size() const; };

// base/loc.h
struct SourceLoc { uint32_t file; uint32_t line, col, len; };

// base/diag.h
enum class DiagLevel { Note, Warning, Error };
struct Diagnostic { DiagLevel level; std::string code; std::string msg; SourceLoc loc; };
class Diagnostics { public: void report(Diagnostic); bool has_errors() const; /* 排序后打印，格式对齐 OMC 的 getErrorString */ };
```

### 4.2 IR 家族与 OMC 对应

| 自研 IR | OMC 对应（定义处） | 文本格式 | 说明 |
|---|---|---|---|
| `absyn::Program/Class/Element/Equation/Exp` | `FrontEnd/Absyn.mo`（PROGRAM :99、PARTS :147、COMPONENTS :353、EQUATIONITEM :435） | Modelica 源码（unparse） | 忠实源码，保留注释/注解/位置 |
| `scode::Program/Class/Element` | `FrontEnd/SCode.mo`（PARTS :193、CLASS :560、COMPONENT :571） | `.scode`（自定义，仅调试） | 语法糖归一：一个 `COMPONENT` 一个名字 |
| `inst::Node/Class/Component/Modifier` | `NFFrontEnd/NFInstNode.mo`、`NFClass.mo`、`NFComponent.mo`、`NFModifier.mo` | 无（树太大，只 dump 子树） | 可变共享树：arena + Id，父子/继承指针都是 Id |
| `flat::Model/Variable/Equation` | `NFFrontEnd/NFFlatModel.mo`、`NFVariable.mo`、`NFEquation.mo` | **Base Modelica** | S3 出口、S5 入口 |
| `bdae::Var/Eqn/Partition/StrongComponent` | `NBackEnd/Classes/NBVariable.mo`（VAR_DATA_SIM :2053，v1.25.0）、`NBEquation.mo`（Equation :899、EQ_DATA_SIM :4303）、`NBPartition.mo`（PARTITION :176）、`NBStrongComponent.mo`（:102–164） | `.bdae`（= NB 自己的 dump 版式，`--newBackend -d=optdaedump,bltdump`；见 ADR-008 D3） | 桶 = `std::vector<Id<Var>>` 视图 |
| `simcode::Block/SimVar` | `NSimCode/NSimStrongComponent.mo`（Block :94）、`NSimVar.mo` | 对齐 `-d=dumpSimCode` | |
| `Expr`（所有 IR 共用） | `NFExpression.mo` / `DAE.Exp` | Modelica 表达式文法 | arena 化、hash-consing（同结构同 Id）便于 CSE/别名 |

### 4.3 范式代码（S1 起所有 IR 照此写）

```cpp
enum class ExprKind : uint8_t { Integer, Real, Bool, String, Cref, Binary, Unary, Call, Array, Range, If, /*…*/ };
struct Expr {
  ExprKind kind; SourceLoc loc; TypeId type{};      // 公共头
  union { int64_t i; double r; bool b; Symbol s; };  // 字面量
  Id<Cref> cref{}; Op op{}; Id<Expr> lhs{}, rhs{};   // 各 kind 用自己那几个字段；不用 variant
  std::vector<Id<Expr>> args;                         // Call/Array
};
// 处理：switch (e.kind) { case ExprKind::Binary: … }  ← 漏 case 由 -Werror=switch 报错
```

不用 `std::variant`：调试器里看不清、编译错误难读；不用继承：数据不需要多态。等你写到第三个 IR 觉得字段太散时再重构，这是学习曲线的一部分，不预先抽象。

---

## 5. 分阶段路线

每阶段固定模板：**目标 / 范围与不做 / OMC 参考入口 / 产出 / 手写清单（你）/ 我提供 / 验收 / 调试手段 / C++ 学习点 / 退出条件 / 预估**。预估按一人全职，含学习时间。

### S0 工程基建（2 周）

- **目标**：空壳编译器能跑通"读文件 → 空 pass → dump"，测试运行器能跑 OMC 的一个用例并报 PASS/FAIL/SKIP。
- **产出**：`ymc --help`；`ymc --dump-after=parse foo.mo` 打印占位符；`python tests/omc/run.py testsuite/flattening/modelica/scoping` 输出统计。
- **手写清单（你）**：`Symbol/Interner`、`Arena/Id`（按 §4.1 接口，我给测试）。
- **我提供**：CMake/presets/clang-format/tidy、`Diagnostics`、`Logger`、dump 基础设施、testsuite 运行器（复刻 `testsuite/rtest`：解析 `// cflags:` `// status:`，运行，和 `// Result:`…`// endResult` 比对）、omc-diff 规则复刻（见 §8 待验证）、`.natvis`。
- **验收**：ASan 预设全绿；运行器对 OMC 自身跑 `flattening/modelica/scoping` 全 PASS（证明运行器正确）。
- **C++ 学习点**：翻译单元/头文件、RAII、`unique_ptr`、`string_view`、模板的最小用法（`Arena<T>`）。
- **退出条件**：上述验收 + §3.5 第 1/2/7/8/9/10 条基础设施到位。

### S1 词法 + 语法 → Absyn + unparse（3–4 周）

- **目标**：完整 Modelica 3.6 文法；AST 能反解析回等价源码。
- **范围**：MLS 附录 B 全部文法（含注解、`operator`、`stream`、`expandable`、同步算子、状态机语法）；**不做**语义。
- **OMC 参考**：文法 `Parser/Modelica.g`（起始 `stored_definition` :225；表达式 :1554；脚本语句 :2246）；词法 `Parser/Modelica_3_Lexer.g` + `BaseModelica_Lexer.g`；AST `FrontEnd/Absyn.mo`；反解析 `FrontEnd/Dump.mo`。
- **产出**：`ymc --dump-after=parse f.mo` 输出反解析文本；`ymc --check-syntax`。
- **手写清单**：词法器（手写状态机，注意 `.` 与数字、Q-IDENT、字符串转义、块注释嵌套不允许）、递归下降解析器（表达式优先级按 MLS §B.2.7 手写 Pratt 或分层递归）、Absyn 结构、unparse。
- **我提供**：文法逐条对照表（MLS 附录 B ↔ 你的函数名）、错误恢复策略说明、词法/语法用例集（从 testsuite 提取 + 我造的边角用例）。
- **验收**：① 解析 `testsuite/flattening` 全部 2641 个 `.mo` + MSL 4.0.0 全部文件（约 1200 个）零错误；② round-trip：parse → unparse → parse → unparse 两次输出 byte-identical；③ 对 `testsuite/openmodelica/parser`（若有，待查）的错误用例给出与 OMC 同位置的错误（位置一致即可，措辞不要求）。
- **调试**：`--dump-tokens`；解析器每个规则入口可 `--log=parse` 打印 "enter rule @loc"。
- **C++ 学习点**：递归、`enum class`、`std::vector` 生命周期、`string_view` 的悬垂陷阱、`std::optional`。
- **退出条件**：验收 ①②。

### S2 SCode 归一化 + 包加载（2 周）

- **目标**：Absyn → SCode；按 `MODELICAPATH` 加载库；`package.order`；`uses` 注解版本解析。
- **OMC 参考**：`FrontEnd/AbsynToSCode.mo:73 translateAbsyn2SCode`、`:114 translateClass`、`:1006 translateElement`；加载 `FrontEnd/ClassLoader.mo:823`；脚本 `loadFile/loadModel` 在 `Script/CevalScript.mo:361`。
- **产出**：`ymc --dump-after=scode`；`ymc --load-model=Modelica --version=4.0.0`。
- **手写清单**：SCode 结构、翻译、目录结构库加载（`.mo` 单文件 / 目录 + `package.mo` / `package.order`）、版本选择。
- **我提供**：`MODELICAPATH` 与 `uses` 解析规则（MLS §13.2.3、§18.8）对照表；库目录布局说明。
- **验收**：加载 MSL 4.0.0 + Complex + ModelicaServices 无错；SCode 元素计数与 OMC 一致（用 `-d=…` 或探针：OMC `getClassNames` 递归 vs 自研）。
- **C++ 学习点**：`<filesystem>`、移动语义（大树搬运）、const 正确性。

### S3 实例化 / 类型 / 压平 → FlatModel（4–6 个月，最重的阶段）

拆成七个子阶段，每个都有 OMC 入口和可验收的 testsuite 子集。

| 子阶段 | 内容 | OMC 入口 | 验收子集（L1） |
|---|---|---|---|
| **3a 查找与实例化骨架** | 作用域链、`import`、`encapsulated`、`inner/outer`；实例树（`InstNode`）；无数组、无修饰 | `NFInst.mo:148 instClassInProgram`、`:185 instantiateRootClass → instantiate/expand`、`NFLookup.mo` | `flattening/modelica/scoping`、`packages`、`declarations`（`.mo` 型） |
| **3b 修饰合并 + 继承 + redeclare** | 修饰沿继承链和组件树传播、`each`/`final`、`extends` 修饰、`replaceable/constrainedby`、`redeclare class/component`、条件组件 | `NFInst.mo:194 instExpressions`、`NFModifier.mo`、`NFInst` 的 `expand` | `extends`、`modification`、`redeclare`、`scodeinst` |
| **3c 类型与数组** | `NFTyping.mo:124 typeClass`（typeComponents :140 → typeBindings :950 → typeClassSections :2727）；维度推断、`:` 维度、切片、for 表达式、归约；类型兼容 | `NFTyping.mo`、`NFType.mo`、`NFDimension.mo` | `types`、`arrays`、`enums`、`operators`、`built-in-functions` |
| **3d connect** | flow 求和、stream（`inStream/actualStream` 展开公式 MLS §15.2）、expandable、过约束图（`Connections.root/branch`） | `NFFlatten.mo:2433 resolveConnections`（细读见 `NFFlatten-方程与连接导读.html`）、`NFOCConnectionGraph.mo`、`NFArrayConnections.mo` | `connectors`、`streams`、`expandable` |
| **3e 编译期求值与函数** | 常量折叠、结构参数、`if` 方程参数条件、Modelica 函数解释器（record/数组/循环/递归/`external "C"` 的白名单内置） | `NFEvalConstants.mo`、`NFCeval.mo`、`NFEvalFunction.mo`、`NFInst.mo:198 updateImplicitVariability` | `algorithms-functions`、`records`、`external-functions`、`external-objects`、`functions_eval`（simulation，L2） |
| **3f 压平 + 化简 + 标量化 + 校验** | 带前缀遍历（`NFFlatten.mo:320`，细读见 `NFFlatten-主流程导读.html`）、`SimplifyModel`（:215）、`Scalarize`（:236，本方案**先标量化**）、`VerifyModel`（:262）、平衡检查 | `NFFlatten.mo`、`NFSimplifyModel.mo`、`NFScalarize.mo`、`NFVerifyModel.mo` | `equations`、`others`、`mosfiles`（51 个 `.mos`，`instantiateModel`） |
| **3g Base Modelica 输出 + MSL 回归** | 文本格式对齐 OMC `--baseModelicaOptions`；可回读 | `NFInstUtil.dumpFlatModel`、`NFFlatModel.mo toFlatString` | MSL 4.0.0 全部 `Examples` 的 `instantiateModel` diff |

- **不做（放 S8）**：同步时钟、状态机、`nfScalarize=false` 的数组保持。
- **手写清单**：全部。
- **我提供**：每个子阶段一份规格（MLS 章节 ↔ OMC 函数 ↔ 你的接口）；归一化 diff 工具；探针模型库（每个规范角落一个最小模型 + OMC 输出）；评审。
- **验收**：上表子集 L1 全 PASS（`-d=-newInst` 用例除外）；`flattening/modelica` 2641 个 `.mo` 中，非旧前端用例全 PASS，旧前端用例记录"文本不同但语义等价"的比例。
- **调试**：`--dump-after=inst:<class>` 只 dump 某个类的子树；`--trace=a.b.c` 看一个组件从查找到压平的每一步；修饰合并单独 dump（对齐 OMC `-d=…` 的 modifier 打印）。
- **C++ 学习点**：哈希表设计（cref → Id）、`std::span`、算法库（`sort/lower_bound`）、迭代器失效（arena 扩容时的引用失效——这是你会踩的第一个大坑，规范：**持有 Id，不持有引用跨越 `add`**）。
- **退出条件**：验收 + MSL Examples 压平通过率报告。

### S4 脚本层：`.mos` 子集 + testsuite 对接（2–3 周，可与 S3 后半并行）

- **目标**：让 OMC 的 `.mos` 用例能原样跑。
- **依据（统计，出现在多少个 `.mos` 文件中）**：`getErrorString` 3269 · `simulate` 2068 · `loadFile` 1232 · `loadModel` 1136 · `loadString` 987 · `runScript` 952 · `setCommandLineOptions` 851 · `val` 700 · `getEnvironmentVar` 414 · `setDebugFlags` 270 · `echo` 213 · `instantiateModel` 179 · `system` 153 · `readFile` 142 · `setIndexReductionMethod` 139 · `setMatchingAlgorithm` 138 · `buildModel` 134 · `list` 116 · `setTearingMethod` 105 · `print` 76 · `diffSimulationResults` 75 · `checkModel` 69 · `readSimulationResult` 61 · `readSimulationResultSize` 49 · `diffModelicaFileListings` 34。
- **OMC 参考**：分派 `Script/CevalScriptBackend.mo:593 cevalInteractiveFunctions3`（`simulate` :1370、`buildModel` :5903、`instantiateModel` :1613、`compareSimulationResults` :2334、`diffSimulationResults` :2373）；语句文法 `Parser/Modelica.g:2246 interactive_stmt`。
- **产出**：`ymc script.mos` 逐语句打印结果，格式与 OMC 一致（`true`、`""`、`{...}`、record 打印）。
- **手写清单**：脚本语句解析（复用 S1 表达式解析器）、值系统（`Values`）、上面 25 个函数。
- **我提供**：每个函数的输出格式规格（从 OMC 实跑抓取）、`runScript` 所调用的 `ModelTesting*.mos` 框架内容分析（见 §8）。
- **验收**：`flattening/modelica/mosfiles`（51）L3 全 PASS；`simulation/modelica/others`（90）中不依赖后端的语句 PASS。
- **C++ 学习点**：小型解释器（值类型、环境）、`std::variant` 在这里可以用（Values 不是核心 IR，正好练手对比）。

### S5 后端：FlatModel → 因果化 BackendDAE（3–4 个月）

| 子阶段 | 内容 | OMC 入口（新后端为主，缺的看旧后端） | 验收子集（L2，需 S6 后跑通） |
|---|---|---|---|
| **5a lower + 预处理** | 变量/方程分箱（VAR_DATA_SIM 22 桶（v1.25.0）、EQ_DATA_SIM 8 桶）、Bindings、别名消除、DetectStates（`der(x)→$DER.x`）、Events（关系式→`$SEV/$TEV`） | `NBackendDAE.mo:259 lower`、`:505 lowerVariableData`、`:815 lowerEquationData`、`2_Pre/NBBindings.mo:50`、`NBAlias.mo:113`、`NBDetectStates.mo:71`、`NBEvents.mo:80`（v1.25.0）；旧后端别名 `BackEnd/RemoveSimpleEquations.mo` | `equations`、`parameters`、`commonSubExp`（40） |
| **5b 邻接 / 匹配 / 排序** | `Adjacency.Matrix`（FULL→FINAL，可解性靠偏导）、PF+ 匹配、Tarjan；分区 | `NBCausalize.mo:247 causalizePseudoArray`、`NBAdjacency.mo`（:81 strictness、:838 refine）、`NBMatching.mo:119 singular`、`NBSorting.mo:233 tarjan`、`NBPartitioning.mo:472`（细读见 `NFBackend-匹配与排序导读.html`） | `others`、`arrays`（68，标量化后） |
| **5c 指标约简** | Pantelides（MSSS 染色）+ 静态哑导数（分阶段匹配）；**动态状态选择**（新后端没有，看旧后端） | `NBResolveSingularities.mo:67 indexReduction`（细读见 `NB-指标约简导读.html`）；动态选择 `BackEnd/IndexReduction.mo`（`dynamicStateSelection`、StateSet） | `indexreduction`（8）、`start_value_selection`（8） |
| **5d 初始化** | 克隆 + `$START` + 参数变未知量 + 三阶段匹配 + 平衡 + homotopy λ=0 | `NBInitialization.mo:82`（细读见 `NB-初始化系统导读.html`）、`NBResolveSingularities.mo:344 balanceInitialization`、`NBCausalize.mo:262` | `initialization`（91） |
| **5e Solve / 撕裂 / 雅可比** | 符号求解（`solveSimple/solveLinear/solveUnique`）、Cellier 撕裂（新后端未实现，**看旧后端**）、符号微分、稀疏模式与着色 | `NBSolve.mo:92/:637`、`NBTearing.mo:138`（细读见 `NB-撕裂导读.html`）、**`BackEnd/Tearing.mo`**（Cellier/omcTearing）、`NBJacobian.mo:109/:1076`、`Util/NBDifferentiate.mo` | `tearing`（108）、`nonlinear_system`（61）、`linear_system`（16）、`resolveLoops`（13）、`jacobian`（1）、`inlineFunction`（26） |

- **不做（S8）**：数组保持、DAE 模式（`daemode` 13）、同步（`synchronous` 22/`synchronous_c` 18）、状态机（14）。
- **手写清单**：全部算法。
- **我提供**：每个算法的伪码 + OMC 实现的逐段注释 + 论文引用（Pantelides 1988、Tarjan 1972、Mattsson–Söderlind 1993 哑导数、Elmqvist–Otter 撕裂）；`.bdae` 格式规格；用 OMC `-d=bltdump,backenddaeinfo` 抓的参考 dump。
- **验收（S6 之前的结构级，ADR-008 D4 / D5）**：结构 oracle = 安装版 1.25.0 的 NB（`--newBackend`；默认的旧后端只作旁证）；准入模型上 5a 签名与 NB `(Events)` 段一致、5b 的块与分区内各序与 NB **逐位同序**（复刻 OMC 哈希容器遍历序）。NB 1.25.0 没有指标约简、不撕裂连续代数环 → 5c / 5e 尽早接 S6、以 L2 仿真数值为准（2026-10-13 用户裁定）。首批子集改为 `simulation/modelica/NBackend/{basics,alias,event_handling}` + equations / parameters 的准入模型。
- **调试**：`.bdae` dump/parse 让每个 pass 可从文件重放；`--trace=$RES_SIM_3`；邻接矩阵 dump（对齐 `-d=bltmatrixdump`）。
- **C++ 学习点**：图算法与位集、`std::sort` 自定义比较、递归深度（Tarjan 大模型要迭代版）、性能剖析（perf/VS Profiler）。

### S6 代码生成 C + 复用 OMC 运行时（1 个月）

- **目标**：生成 `<M>.c`、`_init.xml`、`_info.json`、makefile，填 OMC 回调表，链接 1.25.0 的 `libSimulationRuntimeC`，跑出 `_res.mat`。
- **OMC 参考**：ABI `openmodelica_func.h:56`；数据布局 `simulation_data.h`；文件切分 `SimCodeMain.mo:589–611`；模板 `Template/CodegenC.tpl`（`simulationFile` :1276、`equation_impl2` :5961、`equationSimpleAssign` :6229、`equationNonlinear` :6563、回调表 :1380、`#include perform_simulation.c.inc` :1339）、cref→数组下标 `CodegenCFunctions.tpl:4723/:4941/:7917`（细读见 `NB-Susan模板与代码生成导读.html`）；`_init.xml` `Template/SerializeInitXML`；`_info.json` `SerializeModelInfo`；makefile `CodegenC.tpl:6951`。
- **产出**：`ymc --build M.mo` → `M.exe`；`ymc script.mos` 里 `simulate/val/diffSimulationResults` 可用。
- **手写清单**：SimCode 分桶（`odeEquations/algebraicEquations/initialEquations/zeroCrossings/jacobians`）、变量下标分配（对齐 `NSimVar.mo:213 traverseCreate` 的顺序：状态→导数→代数→离散…）、C 打印器（AST→C，不用字符串模板）、init.xml/info.json 写出、makefile。
- **我提供**：ABI 逐字段规格（哪些可为 NULL）、OMC 生成代码的注释版样例（Demo/Loop/Init 三套已在手）、`.mat` v4 比较器、与 OMC 差分测试脚本。
- **验收**：`simulation/modelica/{others,equations,parameters,arrays,algorithms_functions,records,inheritances,enums,types,packages,declarations,asserts,built_in_functions}` L2 PASS；`events`（35）在借用 OMC 运行时的前提下 PASS。
- **C++ 学习点**：大文本生成的性能（`fmt::memory_buffer`，避免 `std::string +=` 二次方）、`<filesystem>`、进程调用。

### S7 自研 C 运行时（2–3 个月）

- **目标**：逐块替换 OMC 运行时，最终不依赖 `libSimulationRuntimeC`。
- **顺序**（每块替换后 testsuite 差分回归）：① 数据结构 + `_init.xml` 读取 + 参数覆盖；② MAT v4 写出（格式以 OMC `simulation_result_mat4.cpp` 为准）；③ 显式 Euler + 事件处理（过零检测、事件迭代、`pre`/`edge`/`change`、`sample`，语义 MLS §8.5、§3.8.3，OMC `solver/events.c`、`perform_simulation.c.inc:383`）；④ 初始化（`initialization.c:753/:281` 的流程、同伦）；⑤ 非线性求解器（Newton + 阻尼 + 同伦，参照 `nonlinearSolverNewton/Homotopy.c`）与线性（LAPACK/KLU）；⑥ SUNDIALS（CVODE/IDA）接入 + 与 DASSL 结果对照；⑦ 定步长/内联积分。
- **验收**：全部 S6 通过的用例在自研运行时上 PASS（差分：自研 vs OMC `.mat`）。
- **C 学习点**：C 与 C++ 的边界（`extern "C"`）、数值稳定性、浮点比较规范。

### S8 Dymola 级能力与外围（长期）

按价值排序：Cellier 撕裂（若 5e 没做完）→ 动态状态选择（StateSet + 运行时主元）→ 内联积分 → 同步时钟（`synchronous` 22+18）→ 状态机（14）→ DAE 模式（13）→ 数组保持（研究题）→ FMI 2/3 导出（`buildModelFMU` 115）→ 导入（`importFMU` 53，`fmi4c`）→ Python API → SEMLA 加密库 → LSP → GUI。每项到时单独出方案。

---

## 6. 测试与 oracle 基础设施

### 6.1 OMC 运行器协议（复刻 `testsuite/rtest`）

- 头部：`// name:`、`// keywords:`、`// status: correct|erroneous|incorrect`、`// cflags: <omc flags>`。
- `.mo` 用例：`omc [cflags] file.mo` = 实例化文件里最后一个类并打印（`Main.mo:391 translateFile`，`:452 lastClassname`）；`.mos` 用例：逐语句执行打印。
- 比对：stdout 与 `// Result:` … `// endResult` 之间的文本，经 `omc-diff` 模糊比对（浮点容差、空白）——**规则待复刻，见 §8**。
- 自研运行器（Python）读同样的头部，把 `cflags` 映射到 `ymc` 的 flag（映射表维护在 `tests/omc/cflags_map.json`；映射不到的 → skip 并计数）。

### 6.2 L1 文本归一化规则

对齐 OMC 新前端输出；规则文件 `tests/omc/normalize.md`，初版：去尾随空白；浮点按 `omc-diff` 同样容差；变量/方程顺序**不排序**（OMC 顺序是确定的，自研必须复现同一顺序——这本身是验收项，逼你把遍历顺序做对）；旧前端用例（`-d=-newInst`）不做 L1。

### 6.3 `.mos` 脚本 API 子集

§S4 的 25 个函数为第一批；每个函数的输出格式从 OMC 实跑抓取存为 `tests/omc/api/<fn>.expected`。`runScript` 指向的 `ModelTesting*.mos` 框架先分析再定（§8）。

### 6.4 数值比对

`.mat` v4 读取 + 逐变量插值比对，容差参数对齐 `diffSimulationResults(reltol, reltolDiffMinMax, rangeDelta)`（`CevalScriptBackend.mo:2373`）；MAP-LIB 参考结果接入放 S6 末。

### 6.5 差分测试

`tools/diff_omc.py M.mo`：同一模型分别过 OMC（工作区版）与 `ymc`，逐阶段并排 dump（Base Modelica / bltdump / backenddaeinfo / `.mat`），第一处不同即停。这是日常开发的主工具。

---

## 7. 协作规范

### 7.1 任务卡模板（每个手写任务一张，放 `docs/tasks/`）

```
# T-<阶段>-<序号> <标题>
目标：一句话
来源：MLS §… / OMC <文件:行> / probe <文件>
输入 → 输出：IR 类型、不变量（前置/后置条件）
数据结构变更：新增/修改的 struct/enum（列字段）
不做：明确排除
验收：命令 + 期望（testsuite 子集 / 单测快照）
调试：本任务新增的 dump/trace/日志
C++ 要点：本任务会用到的语言特性 + 我提前标出的坑
完成定义：验收通过 + ASan 干净 + 评审通过 + docs 更新
```

### 7.2 分工

- **你写**：`src/parse absyn scode inst flat bdae simcode codegen script` 全部核心代码、`runtime/` 全部。
- **我写**：`base/` 骨架（你可重写）、`tests/omc/` 运行器与工具、`tools/`、`.natvis`、CMake、每张任务卡、探针模型、评审意见、每阶段的算法讲解文档。
- **评审清单**：来源标注齐全？规范 §3.4/§3.5 逐条？dump 前后附上？新增单测快照？ASan 绿？

### 7.3 记录

- `docs/adr/NNN-*.md`：每个不可逆决定（IR 表示、内存策略、格式）一篇，含"当时的依据"。
- `tasks/lessons.md`：每次被评审打回或踩坑后一条规则（对应你的自我改进循环）。
- `docs/open-questions.md`：待验证清单（§8 的活文档）。

---

## 8. 风险与待验证清单（初版）

| # | 待验证 | 验证方法 | 影响阶段 |
|---|---|---|---|
| 1 | `omc-diff` 的模糊比对规则（浮点容差、空白、行序） | 读 `testsuite/omc-diff*` 源码（位置待找：`testsuite/` 或 `OMCompiler/`），跑几组已知差异 | S0 |
| 2 | 2030 个旧前端用例中有多少在数值级仍与新前端一致 | 用工作区 omc 分别 `-d=newInst` / `-d=-newInst` 跑 `simulation/`，统计 `.mat` 一致率 | S3/S6 |
| 3 | `runScript` 指向的 `ModelTesting*.mos` 框架用了哪些 API | grep `testsuite/**/ModelTesting*.mos`，统计函数 | S4 |
| 4 | `getEnvironmentVar` 依赖的环境变量清单 | grep 参数字面量 | S4 |
| 5 | OMC 压平 MSL Examples 的耗时基线（自研的性能目标） | 工作区 omc 批量 `instantiateModel` 计时 | S3g |
| 6 | ~~Windows 上双编译器 CMake 预设可行性~~ **已验证**（2026-09-13：GCC + MSVC + MSVC/ASan 三套预设全绿，见 `C:\dev\ymc\README.md`） | — | S0 |
| 7 | 1.25.0 `libSimulationRuntimeC` 的头文件与 ABI 是否足够（不需要 OMC 私有头） | 用 OMC 生成的 `Demo.c` 手工用 clang 编译链接一次 | S6 |
| 8 | 旧后端 `BackEnd/Tearing.mo` 的 Cellier 实现范围（strict/casual、`tearingSelect` 处理） | 读 `Tearing.mo:2149–2268、:3160–3216` | S5e |
| 9 | 动态状态选择的运行时接口（`stateset.c`、`STATE_SET_DATA`） | 读 `SimulationRuntime/c/simulation/solver/stateset.c` + `openmodelica_func.h:94` | S5c/S7 |
| 10 | MAP-LIB 参考结果的格式与覆盖范围 | 克隆仓库看 README | S6 |

---

## 9. 附录

### 9.1 OMC 入口速查（基线 `3ceb0c9d06`，均已 grep 核对）

- 总调度 `SimCode/SimCodeMain.mo:1143 translateModel`（NB 分叉 :1185 → `:1531`）；脚本 `Script/CevalScriptBackend.mo:593`；`Main/Main.mo:391 translateFile`
- 前端 `NFFrontEnd/NFInst.mo:148`（:185 实例化、:202 类型、:205 压平、:215 化简、:236 标量化、:262 校验）；`NFTyping.mo:124`；`NFFlatten.mo:320`（连接 :2433）
- 后端 `NBackEnd/Classes/NBackendDAE.mo:259 lower / :275 main / :358 applyModules / :410 dump 条件`；`NBCausalize.mo:89/:247/:262`；`NBMatching.mo:119`；`NBSorting.mo:233`；`NBResolveSingularities.mo:67/:344`；`NBInitialization.mo:82`；`NBTearing.mo:138/:403/:468`；`NBSolve.mo:92/:637`；`NBJacobian.mo:109/:1076`
- 桥 `NSimCode/NSimCode.mo:272/:493`；`NSimStrongComponent.mo:94/:640/:1068`；`NSimVar.mo:213`
- 模板 `Template/CodegenC.tpl:90/:1276/:1339/:1380/:4659/:5961/:6229/:6563`；`CodegenCFunctions.tpl:4723/:4941/:5154/:7917`
- 运行时 `simulation/solver/solver_main.c:599`；`perform_simulation.c.inc:383`；`initialization/initialization.c:753/:281`；ABI `include/omc/c/openmodelica_func.h:56`

### 9.2 命令速查

```
# 工作区 omc（与源码同提交；对 index-3 + StateSelect 模型会死循环，务必带超时）
$omc = "C:\dev\OpenModelica\build_cmake\install_cmake\bin\omc.exe"
& $omc --newBackend -d=bltdump,backenddaeinfo --simCodeTarget=None -s M.mo
& $omc --baseModelicaOptions=… --simCodeTarget=None -s M.mo      # Base Modelica 文本（选项待查 Flags.mo）
omc -d=dump M.mo                                                  # Absyn 反解析
omc script.mos                                                    # 脚本
# testsuite 单目录（需要 make + perl）
cd testsuite/flattening/modelica/scoping; make test
```

### 9.3 已有细读（系列，全部新前端/新后端；均在 `docs/omc-导读/`）

`OMC-编译全流程导读.html`、`NFFlatten-主流程导读.html`、`NFFlatten-方程与连接导读.html`、`NFBackend-匹配与排序导读.html`、`NB-指标约简导读.html`、`NB-撕裂导读.html`、`NB-初始化系统导读.html`、`NB-Susan模板与代码生成导读.html`。
