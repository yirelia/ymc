# R13 `omc X.mo` 压平模型文本格式（新前端默认路径）

> 研究对象：`C:\dev\OpenModelica`（HEAD 3ceb0c9d06）；oracle = 安装版 omc 1.25.0（`omc X.mo`，全部带 `timeout 120`）。
> 约定：锚点写作 `文件:行`（路径相对 `OMCompiler/Compiler/`，行号为 HEAD 3ceb0c9d06，引用前已 grep/Read 复核）；探针输出为 omc 1.25.0 实跑原文，探针文件在 `docs/research/probe-r13/`。用例名指 `testsuite/flattening/modelica/scodeinst/<名>.mo`。
> 状态：**增量写作中**（每完成一节即追加；未完成的节标注"待写"）。

## 结论摘要

1. **默认 `omc X.mo` 走 DAE 路径，不走 Flat/Base Modelica 路径**：`NFConvertDAE.convertModel` → `DAEDump.dumpStr` → Susan 模板 `DAEDumpTpl.tpl` + `ExpressionDumpTpl.tpl`（**不是** `ExpressionDump.mo`）。`-f/--flatModelica` 是另一套版式，本报告不覆盖（§1）。
2. **版面骨架**：函数（按名 ASCII 排序，用到的才打，含 record 构造器）在前 → 空行 → `class <fullName>[ "注释"]` … `end <fullName>;`。关键字恒 `class`。段固定顺序 变量→initial equation→initial algorithm→equation→algorithm，空段不打，LF 换行，末尾一个 `\n`（§1、§2）。
3. **变量行字段顺序固定**（§3）：`protected final par* constant|parameter|discrete input|output 类型 名字(属性) = 绑定 "注释";`；不打 flow/stream/inner/outer/each；结构参数强制 `final parameter`；用户 type/record 塌陷成内建标量、数组按行主序标量化（下标 `[i,j]` 无空格）。
4. **属性顺序按类型固定**（§3.4），值是前端算完的表达式；Real 补 `.0`、Integer 不补；`unbounded` 丢弃。
5. **表达式**（§4）由 `ExpressionDumpTpl.dumpExp` 打印，括号由 `Expression.shouldParenthesize` 的优先级表决定（§4.2）；CAST 打 `/*Type*/(exp)`；大量「前端改写」现象（改写/折叠/补参）在期望输出里但不属打印器（§4.3）。
6. **Real 字面量**走 `realString`（Ryu 最短往返 + `ryu_to_hr`），定点/指数按 `exp` 与位数切换（§5）——复用 S1 的 `real_literal_text` 即可。
7. **方程/语句**（§6）：`=`/`:=`，if/for/when 用 `elseif`/`elsewhen` 连写、单个 `end`，元组空位 `_`，assert level=error 省第三参；connect 已展开成势/流等式。
8. **错误帧**（§7）：incorrect 打固定的 `Error processing file:` … `# Error…` … `Execution failed!` 三段、不打模型；correct+Warning/Notification 则模型在前、消息在后。testsuite 下方括号路径**相对 testsuite 根**。
9. **rtest 归一化**（§8）：行首空白与缩进不比、Real 按 eps 容差、整数精确；关键字/运算符/括号/逗号/点分名/字符串/token 顺序与行数必须逐字。

## 逐项事实

### 1. 打印路径

**规则**

1. `omc X.mo` 默认路径：`Main.translateFile` → `CevalScriptBackend.translateModel` → `SimCodeMain.translateModel` → （新前端）`runFrontEndWorkNF` 得 `FlatModel` → `NFConvertDAE.convertModel` 转 `DAE.DAElist` → **`DAEDump.dumpStr(dae, funcs)`** → `print(flatString)`。
   - `Main/Main.mo:452`：实例化的类 = `Config.classToInstantiate()` 为空时取 `AbsynUtil.lastClassname(...)`（文件里最后一个顶层类）。
   - `Main/Main.mo:455-459`：`runBackend := Config.simulationCg() or Config.simulation()`（裸 `omc X.mo` 为 false）；`runSilent := Config.silent()`；调 `CevalScriptBackend.translateModel`。
   - `Main/Main.mo:460`：**模型文本 print 完之后**才 `showErrors(Print.getErrorString(), ErrorExt.printMessagesStr(false))`——所以消息（Warning/Notification/Error）永远排在模型文本**后面**（见 §7）。
   - `Script/CevalScriptBackend.mo:3725`（`translateModel`）→ `:3754` 调 `SimCodeMain.translateModel`。
   - `SimCode/SimCodeMain.mo:1182`：`dumpValidFlatModelicaNF := not runSilent and Config.flatModelica()`；`:1203-1208`：若该开关为真用 `NFFlatString`（= `NFFlatModel` 的 Flat/Base Modelica 打印），否则 `dae := NFConvertDAE.convertModel(flatModel); flatString := DAEDump.dumpStr(dae, funcs)`；`:1263` `print(flatString)`。
   - 1.25.0 同构：`git show v1.25.0:.../SimCodeMain.mo` 1137/1156-1159/1215 行是同一段逻辑；`Main.mo` 1.25.0 在 438-446 行。
2. `DAEDump.dumpStr`（`FrontEnd/DAEDump.mo:2622-2635`）**不是**用 DAEDump.mo 里那些 `dumpXxxStream`/`IOStream` 函数，而是 `Tpl.tplString2(DAEDumpTpl.dumpDAE, fixedDae, funList)`——真正的版式在 Susan 模板 **`Template/DAEDumpTpl.tpl`**，表达式在 **`Template/ExpressionDumpTpl.tpl`**（`DAEDumpTpl.tpl:1106-1108`：`dumpExp(exp) ::= ExpressionDumpTpl.dumpExp(exp, "\"")`）。`ExpressionDump.mo` 的 `printExpStr` 在这条路径上不被用到（它只在错误消息/调试里出现）。
   - 两个模板 HEAD 与 v1.25.0 的差别：HEAD 顶部多了 35 行版权头（**v1.25.0 行号 = 本报告行号 − 35**），以及 ASUB 下标打印（HEAD 用 `dumpSubscripts` → `[1,2]`，1.25.0 用 `dumpExpList(sub, ", ")` → `[1, 2]`）；其余逐字相同（`git diff v1.25.0 HEAD -- .../DAEDumpTpl.tpl .../ExpressionDumpTpl.tpl`）。
   - 括号规则 `Expression.shouldParenthesize`…`priorityLBinop`（`FrontEnd/Expression.mo:10757-10946`）与 v1.25.0（10751 起）逐字相同（已 diff）。
3. 另一条路径 `-f` / `--flatModelica`（`--baseModelica`）：`NFInst.instClassInProgram(..., dumpFlat=true)` 在 `NFFrontEnd/NFInst.mo:228`（未 scalarize）或 `:248`（scalarize 后）调 `InstUtil.dumpFlatModel` → `NFFlatModel.toFlatString`，版式完全不同（`model 'X'` 引号名、`'x'[1]`、保留数组/record 类型定义等）。本报告不展开。
4. 交互 API `instantiateModel(X)`（.mos 用例）走 `CevalScriptBackend.mo:8829-8902`，同样落到 `DAEDump.dumpStr`（`:8871`），文本与 `omc X.mo` 相同，只是被包成字符串返回。
5. **函数块打印在模型之前**：`DAEDumpTpl.tpl:45-58` `dumpDAE`：`fun_str` 非空时输出 `<fun_str>\n\n<comp_str>`，即「全部函数，空一行，再 class」。函数之间用 `"\n\n"` 分隔（`:97-100`）。函数集合 = `DAEDump.dumpFunctionList`（`DAEDump.mo:2724-2745`）：`DAEUtil.getFunctionList` → 过滤 `isVisibleFunction`（`:2747-2775`：隐藏 `external "builtin"`、`OpenModelica.*` 包内函数、`BUILTIN_EARLY_INLINE`、开了内联时的 `EARLY_INLINE`、带 `__OpenModelica_builtin` 注解的）→ `sortFunctions`（`:151-169`，按函数名字符串**字母序**）。record 构造器也在此列表里（探针 P09/P10）。
6. 输出换行是 LF（探针 P01 `cat -A` 无 `^M`），文本末尾有且仅有一个 `\n`（`dumpComp` 的 `end X;<%\n%>`，`DAEDumpTpl.tpl:70`）。

**证据**：见上述锚点；探针 P01（默认路径输出）、P09（函数在前）、P16（`-f` 对比）。

### 2. 类头尾与段落

**规则**

1. 头：永远是关键字 **`class`**（不管源码是 model/block/connector…），`class <全名><类注释>`；尾：`end <全名>;`（`DAEDumpTpl.tpl:60-72` `dumpComp`）。全名 = `FlatModel.fullName`（`NFConvertDAE.mo:104`），带包前缀的点分名（如用例 `Prefix3.mo` 期望输出首行 `class P.P2.Prefix3`、`EncapsulatingInst1.mo` 的 `class EncapsulatingInst1.M`）。
2. 类的字符串注释**保留**：` "…"`，前面一个空格，内容经 `System.escapedString(cmt,false)` 重新转义（`DAEDumpTpl.tpl:1138-1140`）；探针 P01：源码 `"class comment with \"quote\""` 原样回显。类注解默认**不打印**（`dumpAnnotation` `:1146-1156` 受 `Config.showAnnotations()` / `showStructuralAnnotations()` 控制，两者默认 false）；P01 的 `Documentation`/`experiment` 都没出现。
3. 段顺序固定（`dumpCompStream` `DAEDumpTpl.tpl:74-92`）：**变量 →（状态机）→ `initial equation` → `initial algorithm` → `equation` → `algorithm`**。与源码里各段书写顺序无关（P01 源码里 `initial algorithm` 写在 `equation` 前、第二个 `initial equation` 写在最后）。
4. 空段不打印：`dumpInitialEquationSection`/`dumpEquationSection` 都是 `if ie then …`（`:654-674`）；Susan 的 `<<…>>` 块里，值为空的整行会被吞掉，不留空行（P15 空模型只有 `class X` / `end X;` 两行）。
5. 所有 equation 段合并成**一个** `equation` 段（只打一次关键字）；`initial equation` 同理。算法段**不合并**：每个源码 `algorithm` 段各打一次 `algorithm` 关键字，彼此之间**没有空行**（`dumpAlgorithmSection` `:883-885` separator=`"\n"`；P01 连续两个 `algorithm`）。
6. `public`/`protected` 没有段标题，而是落到每个变量行的 `protected` 前缀上（§3）。
7. 缩进：段关键字顶格；变量、方程、语句缩进 2 空格；嵌套体（if/for/when 内）每层再 +2（模板里的相对缩进）。注意 rtest 比较时行首空白可能被忽略，见 §8。
8. 段内顺序由前端给定而非打印器决定，但有两个打印期看得见的现象值得记：(a) 多个 `initial equation` 段合并时**后写的段排在前**（P01：`w = 3.0;` 在 `x = 1.0;` 前）——待 R14（flatten 顺序）确认源码位置；(b) connect 生成的等式排在 `equation` 段最前（P07）。

**证据**（P01，源码见「探针」节）

```
class P01_sections "class comment with \"quote\""
  Real x(start = 1.0);
  Real y;
  Real z;
  protected Real w;
  parameter Real p = 2.0 "param comment";
initial equation
  w = 3.0;
  x = 1.0;
initial algorithm
  z := 0.0;
equation
  der(x) = -p * x;
  w = x;
algorithm
  y := x + 1.0;
algorithm
  z := y * 2.0;
end P01_sections;
```

### 3. 变量声明行

**规则**（模板 `dumpVar`，`DAEDumpTpl.tpl:282-306`；关键行 `:304`）

```
 <%varVisibility%><%final%><%varParallelism%><%varKind%><%varDirection%> <%varType%><%dim_str%> <%varName%><%attr%><%varAttr%><%binding_str%><%cmt_str%><%ann_str%>;
```

1. **前缀顺序固定**：`protected` → `final` → (`parglobal`/`parlocal`) → `constant`/`parameter`/`discrete` → `input`/`output` → 类型 → 名字。每个前缀自带一个**前导**空格（`' protected'`、`' final'`、`' parameter'`…，`:308-344`），模板行首 1 个空格 + 类型前 1 个空格 ⇒ 无前缀变量正好缩进 2 格：`  Real x;`；有前缀：`  protected final parameter Boolean fb = true;`（P02）。
   - **不打印**：`flow`/`stream`（P02 `c.f`、`c.s` 都是裸 `Real`）、`inner`/`outer`（`NFConvertDAE.mo:198` 恒填 `Absyn.NOT_INNER_OUTER()`，模板也不读该字段）、`each`、`redeclare`/`replaceable`。
   - `final` 的来源：`NFConvertDAE.mo:238-239` `is_final := compAttrs.isFinal or variability == STRUCTURAL_PARAMETER`——**结构参数一律打 `final parameter`**（P02 `m`（`Evaluate=true`）、P04 `m.n`（用作数组维度）、P07 `flag`（作 if-equation 条件）、P11 `np`）。模板只在 `variableAttributesOption = SOME(...)` 时才可能出 `final`（`:286`），而 `convertVarAttributes` 对非五种内建标量类型返回 `NONE()`（`NFConvertDAE.mo:248-255`）。
   - `discrete`/`parameter`/`constant` 与 `input`/`output` 可叠加：`discrete input Integer di;`、`parameter input Real pin = 1.0;`、`protected output Real po;`（P02）。
2. **类型名**（`dumpType`，`:346-387`）：只有内建名 `Real`/`Integer`/`Boolean`/`String`/`Clock`；**用户 type 名不保留**（P03 `Voltage v` → `Real v(quantity = "ElectricPotential", unit = "V", …)`，type 上的修饰合并进属性）。枚举打成 **`enumeration(lit1, lit2, …)`**（`:364-366`，字面量间 `, `，不带枚举类型名、不带字面量注释；P03 `enumeration(red, green, blue) c(...)`）。record/model 成员**逐成员展开**成点分标量名（P04 `nn.r.x`），不出现 record 类型名；record 本身的注释、组件 `M m "component comment"` 的注释**丢失**，只有叶子变量自己的注释保留。
3. **数组**：默认（scalarize）下每个标量元素一行，行主序，下标写在名字里：`Real mat[1,2] "matrix";`——**cref 下标之间是 `,` 无空格**（`dumpSubscripts` `:631-640`，separator=`","`）；中间层下标同理 `ma[2].a[1]`。元素各自重复注释、各自拿到标量化后的属性/绑定（P04 `st[2](start = 2.0, fixed = true)`、`p[2] = 2.0`、`each start = true` → 每行 `start = true`）。零长数组不产生任何行（P04 `e0[0]`）。
   - 不 scalarize（`-d=-nfScalarize`，scodeinst 里约 23 个用例）或函数内变量：维度写在**类型**后：`Real[3] m.port.v;`（用例 `ArrayConnect4.mo`）、函数形参 `input Real[:, 2] m;`、`output Real[size(v, 1)] w;`、`protected Real[n] loc;`（P09）——类型维度之间是 **`, ` 有空格**（`dumpTypeDimensions` `:623-628` / `dumpArrayType` `:389-400`），未知维度 `:`（`dumpDimension` `:473-480`）。
4. **属性修饰顺序固定，与源码书写顺序无关**（`dumpVariableAttributes` `:482-536`）：
   - Real：`quantity, unit, displayUnit, min, max, start, fixed, nominal, stateSelect, uncertainty, distribution`（+ `startOrigin` 仅 `--showStartOrigin`）
   - Integer：`quantity, min, max, start, fixed`（+uncertainty, distribution）
   - Boolean：`quantity, start, fixed`
   - String：`quantity, start`（注意 String 的 `fixed` 即使给了也不打印：`VAR_ATTR_STRING` 分支 `:521-526` 没有 fixed）
   - 枚举：`quantity, min, max, start, fixed`
   - 形式 `(a = e1, b = e2)`，分隔 `, `，`=` 两侧空格；全空则整个括号不打印。`stateSelect = StateSelect.prefer`（`:548-560`，五个字面量全名）。`unbounded` 被丢弃（`NFConvertDAE.mo:284-285`，P03 `u(unbounded = true, start = 2)` → `u(start = 2.0)`）。type 上的 `final quantity=` 的 `final` 不体现。属性值是普通表达式（P03 `r(start = q + 1.0)`），Integer→Real 已在前端完成（`min = -100` → `min = -100.0`）。
5. **绑定**：` = <exp>`（`:299`），在属性括号之后、注释之前：`parameter Real q(min = 0.0) = 2.0 "with binding and attr";`（P03）。
6. **注释**：` "<escaped>"`（`dumpCommentStr` `:1138-1140`）。**组件注解默认不打印**（P04 `x … annotation(Dialog(...))`、`y annotation(HideResult = true)` 均无痕；`dumpCompAnnotation` `:1116-1120` → `dumpAnnotation` 受 `--showAnnotations`/`--showStructuralAnnotations` 控制）。
7. `<%attr%>`（来自 `dumpType` 的 `&attributes`，即 DAE 类型自带的 varLst）在 NF 路径恒为空——NF 的 `Type.toDAE` 不往 `T_REAL(varLst)` 里塞东西；属性全部走 `VariableAttributes`。

**证据**：P02、P03、P04 输出（全文见「探针」节）。摘录：

```
  Real a(quantity = "Length", unit = "m", displayUnit = "mm", min = -100.0, max = 100.0, start = 1.0, fixed = true, nominal = 10.0, stateSelect = StateSelect.prefer);
  Integer i(quantity = "count", min = 1, max = 5, start = 2, fixed = true);
  Boolean b(quantity = "flag", start = false, fixed = true);
  String s(quantity = "name", start = "abc");
  enumeration(red, green, blue) c(quantity = "col", min = Color.red, max = Color.blue, start = Color.green, fixed = true);
  protected final parameter Boolean fb = true;
  Real ma[2].a[1] "array comment";
  Real mat[1,2] "matrix";
```

### 4. 表达式打印规则

实际使用的是 **`Template/ExpressionDumpTpl.tpl` 的 `dumpExp(exp, "\"")`**（`DAEDumpTpl.tpl:1106-1108`）；括号判定在 `FrontEnd/Expression.mo:10757-10946`。**注意**：打印器面对的是 NF 已经 typing + simplify 过的树，下面把「纯版式」与「前端改写」分开列——后者不是打印器的职责，但在期望输出里到处可见，ymc 要在前端阶段对齐。

**4.1 版式规则（打印器）**

| 构造 | 输出 | 锚点（ExpressionDumpTpl.tpl） |
|---|---|---|
| 二元算术 | `lhs␣op␣rhs`，两侧各一空格；标量 `+ - * / ^`；数组逐元素 `.* ./ .^ .+ .-`（`ADD_ARR`/`SUB_ARR` 打 `+`/`-`，`MUL_ARRAY_SCALAR`/`MUL_SCALAR_PRODUCT`/`MUL_MATRIX_PRODUCT` 打 `*`，`DIV_ARRAY_SCALAR` 打 `/`，`DIV_SCALAR_ARRAY` 打 `./`，`POW_ARR` 打 `^`） | `:55-59`，`dumpBinOp` `:329-350` |
| 一元负 | `-x`，**负号后无空格**；`UMINUS_ARR` 同 | `:60-63`，`:353-360` |
| 逻辑 | `a and b`、`a or b`、`not a`（not 后一空格） | `:64-72` |
| 关系 | `< <= > >= == <>`，两侧空格 | `:73-77`，`:377-388` |
| if 表达式 | `if c then a else b`；**`elseif` 不存在**，打成嵌套 `else if`：`if p then a else if q then b else c`（P05 y[33]） | `:78-82` |
| 函数调用 | `全限定名(arg1, arg2)`，`, ` 分隔；**命名参数已被前端转成位置参数并补齐缺省值**（P11 `fn(x, s = "lit", k = 3)` → `P11_exprmisc.fn(x, 3.0, "lit")`）；路径去掉前导 `.`（`dumpPathNoQual`） | `:83-90` |
| record 构造 | `R(1.0, 2.0)` | `:91-94` |
| 数组 | `{a, b}`，矩阵 `{{1.0, 2.0}, {3.0, 4.0}}`；空数组 `{}` | `:99-111` |
| range | `start:stop` / `start:step:stop`，**冒号两侧无空格**：`n0 + 1:n1 - 1`、`n1:-1:n0`、`1:2 * n0:n1`（P13） | `:112-116` |
| 元组 | `(a, b)`；通配 `_`：`(_, w2) = f(...)`（P09/P08） | `:117-119`，`dumpCref WILD` `:256` |
| 类型转换 CAST | **`/*Real*/(exp)`**；数组 `/*Real[:]*/(iv)`、`/*Real[3]*/(…)`（类型名来自本文件 `dumpType` `:390-434`，Boolean 在这里叫 `Bool`）。**字面量不产生 CAST**：`2` 直接变 `2.0`（前端 typing 时折叠）；非字面量整型子式整体包一层：`/*Real*/(i1 * 2 + 1)`、`/*Real*/(1 + i1) * x`（P11） | `:120-123` |
| 下标（cref 内） | `v[i1]`、`m[1,2]`、`m[i1,i2]`——**`,` 无空格**；切片 `v[2:3]`、`v[{1, 3}]`（花括号内仍 `, `）、`a[:]`；`end` 已被替换成 `size(a, 1)` | `dumpSubscripts` `:262-280` |
| ASUB（对表达式取下标） | 1.25.0：`exp[i, j]`（`, `）；HEAD：`exp[i,j]`。Modelica 源码写不出这种式子，只由前端生成 | `:124-130`（1.25.0 `:89-95`） |
| TSUB（取元组第 k 个输出） | `f(args)[1]`（P09 `Real a = P09.zeta(...)[1];`） | `:131-136` |
| `size` | `size(v, 1)` | `:143-146` |
| 归约 | `sum(i for i in 1:np)`；数组推导 `{e for i in r}` 打成 **`array(e for i in r)`**（P13） | `:153-157` |
| `der` / `pre` / 内建 | 普通调用：`der(x)`、`pre(d1)`、`initial()`、`sample(0.0, 0.1)`、`noEvent(abs(x))` | `:83-86` |
| 枚举字面量 | 全限定路径 `Pkg.E.one`、`Color.red`、`StateSelect.prefer` | `:52-53` |
| Boolean | `true` / `false` | `:50` |
| Integer | 十进制；负数就是 `-1`（ICONST 自带符号） | `:45` |
| String | `"…"`，内容经 `System.escapedString(s, false)`：转义 `"`→`\"`、`\`→`\\`、`\a \b \f \v`；**换行、回车、Tab 原样输出**（`SimulationRuntime/c/util/modelica_string.c:272-300`，`nl=0`）。原样换行后的续行会被 Susan 模板**补上当前缩进**（P13：第二行前多出 2 个空格） | `:47-49` |

**4.2 括号规则**（`Expression.shouldParenthesize`，`Expression.mo:10757-10792`）

- 操作数是 `UNARY`（一元负）→ **一定加括号**：`(-a) ^ 2.0`、`a * (-b)`、`2.0 ^ (-a)`、`(-y) * 2.0`。
- 否则比较 `priority(操作数, 是否左侧)` 与 `priority(外层运算, 是否左侧)`：操作数优先级数值**更大** → 加括号；**相等** → 左操作数仅当它是幂运算（`isNonAssociativeExp`：`POW*`）时加，右操作数仅当它**不是**可结合运算（可结合 = `ADD`、`MUL`、`ADD_ARR`、`MUL_ARRAY_SCALAR`、`ADD_ARRAY_SCALAR`、任何 `LBINARY`）时加；更小 → 不加。
- 优先级表（`priority` `:10848-10874`，数值越小绑得越紧）：

| 表达式 | 作为左操作数/外层(lhs=true) | 作为右操作数/外层(lhs=false) |
|---|---|---|
| `^` | 1 | 1 |
| `*` | 2 | **3** |
| `/` | 2 | 2 |
| 一元负 / **负的 Real 字面量** | 4 | 4 |
| `+` | 5 | **6** |
| `-` | 5 | 5 |
| 关系运算 | 6 | 6 |
| `not` | 7 | 7 |
| `and` | 8 | 8 |
| `or` | 9 | 9 |
| range | 10 | 10 |
| if 表达式 | 11 | 11 |
| 其它（cref、调用、字面量、CAST、数组…） | 0 | 0 |

  注意比较时**两边都用同一个 `inLhs`**（`:10772-10773`）：判右操作数时，外层运算符也取它的「右侧优先级」。推论（全部由 P05/P13/P14 实测印证）：
  - `a + (b + c)` → `a + b + c`；`a - (b - c)`、`a - (b + c)` 保留；`a * (b * c)` → `a * b * c`；`a / (b * c)`、`a / (b / c)` 保留；`a * (b / c)` → `a * b / c`（右侧 `/`=2 < `*`=3）。
  - `(a ^ b) ^ c` 与 `a ^ (b ^ c)` 两边都保留括号。
  - `-(a + b)` 保留；`-(a * b)` → `-a * b`（UNARY 的操作数 MUL 右优先级 3 < 4）；`-a ^ 2.0` 不加。
  - 负 **Real** 字面量当右操作数：`a * (-2.0)`、`y ^ (-2.0)`；但负 **Integer** 字面量优先级是 0 → **`k * -1` 不加括号**（P13/P14）。
  - if 表达式做操作数必加：`(if p then a else b) + c`、`a + (if p then a else b)`；`/*Real*/((if k > 0 then 1 else 2) * 3)`。
  - 关系的关系：`(a < b) == (c < d)` → `a < b == (c < d)`（左相等且非幂 → 不加；右相等且非可结合 → 加）。
  - `not a < b`、`not p and q`、`not (p and q)`、`p and (q or r)`、`(p or q) and r`、`p and (q and r)` → `p and q and r`。
  - range 的操作数走 `dumpOperand(..., false)`：算术子式不加括号（`n0 + 1:n1 - 1`）；if 表达式会加。
- 等式左侧如果整个是 if 表达式，外面加括号：`(if c then a else b) = rhs`（`DAEDumpTpl.tpl:705`；赋值同 `:935`）。
- ASUB/TSUB/RSUB 的基表达式不是「原子」时加括号（`parenthesizeSubExp` `:194-211`）。

**4.3 期望输出里常见、但属于前端改写（非打印器）的现象**（P05/P11/P13 实测；源码在 `NFSimplifyExp.mo`/`NFTyping.mo`/`NFBuiltinCall.mo`，留给对应报告）

- Integer 字面量遇 Real 上下文直接变 Real 字面量：`time > 1` → `time > 1.0`，`x ^ 2` → `x ^ 2.0`。
- `-a + b` → `b - a`；`a + (-b)` → `a - b`；`a - (-b)` → `a + b`；`a - (-2.5)` → `a + 2.5`；`+a` → `a`；`-(-a)` → `a`；`not (not p)` → `p`；`1 * x` → `x`；`-(y * 2) + 1` → `1.0 - y * 2.0`。
- 标量上的 `.* ./ .+ .- .^` 变普通运算符。
- 常量折叠：`size(v, 1) + size(m, 2) + ndims(m)` → `7`；`Modelica.Constants.pi + eps` → `3.141592653589794`；`fill`/`zeros`/`linspace`/`cat`/常量函数调用被求值后再标量化。
- 内建缺省参数补齐：`String(i1)` → `String(i1, 0, true)`；`String(x)` → `String(x, 6, 0, true)`；`String(x, significantDigits = 3)` → `String(x, 3, 0, true)`；`String(x, format = "%g")` → `String(x, "%g")`；`delay(x, 0.1)` → `delay(x, 0.1, 0.1)`；`rem(i1, 2)` → `i1 - div(i1, 2) * 2`；`product(v)` / `v * w` / `cross` / 矩阵运算展开成标量式。
- `assert(…, level = AssertionLevel.error)`：level 为 error 时第三参**不打印**，warning 时打印 `, AssertionLevel.warning`（这条是模板行为：`DAEDumpTpl.tpl:737`）。

### 5. Real 字面量

**规则**：`RCONST` 直接走 MetaModelica `realString`（`SimulationRuntime/c/meta/realString.c:66-85`）= Ryu `d2s` 最短往返数字 + `ryu_to_hr(str, buf, real_output=1)`（`3rdParty/ryu/ryu/om_format.c:50-185`）。设 Ryu 给出尾数 `d.ddd`（小数位数 `ndec`）和十进制指数 `exp`：

- `exp < -3` 或 `exp > 5` 或 (`exp > 0` 且 `exp - ndec > 3`) → **指数式** `<尾数>e<exp>`（`E`→`e`，无 `+`，无前导零；尾数无小数时不补 `.0`：`1e5`、`2e20`、`5e-324`）（`om_format.c:171-181`）。
- 否则 → **定点式**；若 `exp >= ndec`（即值为整数）补 `.0`（`:168-169`）。
- `inf`/`-inf`/`NaN` 特判（`realString.c:78-83`）。

**证据**（P06 实测，左为源码字面量）：

| 源 | 输出 | 源 | 输出 |
|---|---|---|---|
| `1e5` | `1e5` | `100000.5` | `100000.5` |
| `1e6` / `1000000.0` | `1e6` | `999999.0` | `999999.0` |
| `0.001` | `0.001` | `9999999.0` | `9.999999e6` |
| `1e-4` | `1e-4` | `123456789.0` | `1.23456789e8` |
| `1e-5` | `1e-5` | `12345.678` | `12345.678` |
| `1.0 / 3.0` | `0.3333333333333333` | `0.0001234` | `1.234e-4` |
| `1e300` | `1e300` | `0.5e-3` | `5e-4` |
| `-2.5` | `-2.5` | `1.e3` | `1000.0` |
| `1`（Real 上下文） | `1.0` | `2.5E+3` | `2500.0` |
| `1234567890123456789.0` | `1.2345678901234568e18` | `.5` | `0.5` |
| `0.0` / `-0.0` | `0.0` / `-0.0` | `4.9e-324` | `5e-324` |
| `1.7976931348623157e308` | 同左 | `6.02214076e23` | 同左 |
| `1e15`/`1e16`/`1e17`/`1e21`/`1e22` | 原样 `1e15`… | `1.0e-3 * 3` | `0.003` |

注意 `1e5` 打成 `1e5` 而**不是** `100000.0`（整数值且 `exp - ndec = 5 > 3`）。边界（P17 实测）：`100.0`→`100.0`，`1000.0`→`1000.0`，`10000.0`→**`1e4`**，`12000.0`→`12000.0`（4−1=3 不大于 3），`120000.0`→`1.2e5`，`123000.0`→`123000.0`，`1230000.0`→`1.23e6`，`0.01`→`0.01`，`0.00012`→`1.2e-4`。属性里的负数同样：`x(min = -1e60, start = -1e-7, nominal = 1e9)`。

### 6. 方程 / 语句格式

**规则**（模板锚点均为 `DAEDumpTpl.tpl`；DAE 元素的来历见 `NFConvertDAE.mo:547-623`（方程）、`:811-876`（语句））

1. **普通等式**：`lhs = rhs<注释>;`（`dumpEquation` `:703-711`；cref=cref 走 `dumpEquEquation` `:713-721`，版式相同）。数组等式、record（complex）等式同一格式：`s3 = mk(2.0 * time);`、`r.arr = {1.0, 2.0};`（P10）。方程/语句自带的字符串注释**保留**：`der(x) = -x "equation comment";`（`dumpSource` `:1167-1171`，多条注释用 ` + ` 连接；P07/P18）。
2. **connect** 不出现在输出里，只剩展开后的等式：势变量 `r1.n.v = r2.p.v;`，流变量 `r2.n.i + r1.p.i = 0.0;`，未连接的顶层 flow `c.f = 0.0;`（P02/P07）。这些等式排在 `equation` 段最前。
3. **if 方程**（`dumpIfEquation` `:820-860`）：
   ```
   if cond then
     eq;
   elseif cond2 then
     eq;
   else
     eq;
   end if<注释>;
   ```
   `elseif` 是关键字连写（与 if **表达式**的 `else if` 不同）。最后一个分支条件恒 `true` 时被转成 `else`（`NFConvertDAE.mo:685-691`）。条件是参数表达式的 if 方程在前端就被选支消掉（P07 `if flag` → 只剩 `y = x;`）；`fixed = false` 的参数作条件则保留（P07 initial equation 段）。空 else 不打印 `else` 行。
4. **for 方程**（`dumpForEquation` `:797-818`）：`for i in 1:3 loop` / 体缩进 2 / `end for;`。默认 scalarize 下 for 方程已被展开成逐条标量等式（P07 `arr[1] = x; arr[2] = 2.0 * x; …`），只有 `-d=-nfScalarize` 等路径才会看到（用例 `ArrayConnect4.mo`：`for $i0 in 1:3 loop`）。算法里的 for **不展开**。
5. **when 方程**（`dumpWhenEquation` `:775-795`）：`when c then` / 体 / `elsewhen c2 then` / 体 / `end when<注释>;`。`elsewhen` 由模板 `else<%elsewhen_str%>` 拼接嵌套 when 得到，所以每个 elsewhen 与 when 同缩进，整个结构只有一个 `end when;`。条件可以是数组 `{x < 0.2, x < 0.1}`。
6. **assert**：`assert(cond, msg);` 或 `assert(cond, msg, AssertionLevel.warning);`——level=error（枚举 index≠1）时**省略第三参**（`dumpAssert` `:733-742`）；**terminate**：`terminate("done");`（`:744-751`）；**reinit**：`reinit(x, 1.0);`（`:753-761`）；**无返回值调用**：`P18_nested.noret(x) "call comment";`（`dumpNoRetCall` `:763-773`）。initial 版本（`INITIAL_ASSERT` 等）同格式，只是落在 `initial equation` 段。
7. **算法段**（`dumpAlgorithm` `:896-904`）：`algorithm`（或 `initial algorithm`）顶格，语句缩进 2。
   - 赋值 `lhs := rhs<注释>;`（`:933-941`）；数组赋值同（`:955-965`）。
   - 元组赋值 `(a, b) := f(x);`，空位打 `_`：`(_, b) := …`、`(a, _) := …`（`:943-953`；P08）。只有一个 LHS 的 `(a) := f(x)` 被转成 `a := f(x)[1]`（TSUB，`NFConvertDAE.mo:898-911`）；`() := f()` 转成无返回调用。
   - `if … then / elseif … then / else / end if;`（`:967-1001`）；`for i in range loop … end for;`（`:1003-1015`）；`while cond loop … end while;`（`:1031-1043`）；`when … then / elsewhen … then / end when;`（`:1045-1065`）；`return;`、`break;`（`:925-926`）；`assert`/`terminate`/`reinit`/无返回调用同方程版式。
   - 嵌套每层 +2 空格（P18：when→if→for 内语句缩进 8）。
8. **函数**（`dumpFunction` `:102-135`）：
   ```
   [impure ]function <全限定名>[ "注释"]
     input Real x "in x";
     input Integer n = 2 "default";
     input Real[:] v "vector";
     output Real[size(v, 1)] w;
     protected Real tmp(start = 1.0) = 2.0 * x "local";
     protected constant Real k = 3.0;
   algorithm
     stmts;
   end <全限定名>;
   ```
   - 变量行复用 `dumpVar(…, printTypeDimension=true)`：维度跟在类型后 `Real[:, 2] m`；顺序 = inputs → outputs → locals（`NFConvertDAE.mo:1103-1105`），locals 带 `protected`。
   - `algorithm` 关键字**顶格**（与变量行不同缩进层），无算法段的函数直接 `end`（P09 `noalg`）。
   - external：变量行后**空一行**，再 `  external "C" y = my_sum(x, size(x, 1));`（`dumpFunctionDefinition` `:150-154`、`dumpExternalDecl` `:164-176`）；没写调用式的 `external "C";` 也会补全成 `external "C" y = ext3(x);`；external 的 annotation 默认不打印。external 函数在 1.25.0 被标成 `impure function`（P09；属前端语义）。
   - 函数注解（`Inline`、`Documentation`）默认不打印。
   - record 构造器：`function R "Automatically generated record constructor for R"` / 各字段 `input …;`（final 字段是 `protected`）/ `output R res;` / `end R;`（`:123-134`；P10）。只有被用到的 record 才出现。
   - 函数块之间空一行，最后一个函数与 `class` 之间空一行；排序按名字字符串序（ASCII：大写先于小写——P10 顺序 `R`、`Small`、`mk`、`useR`）。被内联/求值掉的函数不出现（P09 `alpha`（external "C" sin）被换成内建 `sin(time)`；P13 `mat()` 被常量求值后也不出现）。
9. **生成注释**：默认路径下唯一会出现的 `/* … */` 是 CAST 的 `/*Real*/(…)`。其它 `/*…*/`（`/* scalar Real */`、`/*ASUB*/` 等）都在 `if typeinfo()` 分支里（`ExpressionDumpTpl.tpl:53-54,86,104-111`），需 `-d=printTypeInfo`（待核实 flag 名）才会出现，scodeinst 期望输出不涉及。

**证据**：P07、P08、P09、P10、P18 全文见「探针」节。

### 7. 错误场景的输出

**规则**（锚点 `Main/Main.mo`；`Execution failed!` 来自 C 入口 `Compiler/.cmake/omc_entry_point.c:26`，在 main 返回失败时 `fprintf(stderr, "Execution failed!\n")`）

1. **两条错误路径的方括号路径写法不同**——L1 对拍要留意（路径在 omc-diff 里属 TEXT，逐字比）：
   - 裸 `omc X.mo`（本报告路径）：定位方括号里是**绝对路径**（P12a–d：`[C:/dev/ymc/docs/research/probe-r13/X.mo:…]`）。
   - rtest（`omc --running-testsuite=<tmp> +locale=C X.mo`）：方括号路径被改写成**相对 testsuite 根**（`[flattening/modelica/scodeinst/X.mo:…]`）。scodeinst 的 `// Result:` 块即此形。已实测：AssertInvalid1 加 `--running-testsuite` 跑出的方括号与 Result 块逐字一致。
2. **status: incorrect 的错误帧**（翻译抛错，模型文本**完全不打印**），固定为：
   ```
   Error processing file: <basename>.mo
   <一条或多条消息>

   # Error encountered! Exiting...
   # Please check the error message and the flags.

   Execution failed!
   ```
   - 首行 `Error processing file: <basename>`（永远只有 basename、不带目录：`Main.mo:498` `print("Error processing file: "); print(f)`；翻译 `fail()` 后 matchcontinue 落到 `case (f::_)` 的「deal with problems」分支）。
   - 紧接 `showErrors` 打出所有已收集消息（`Main.mo:503`）。
   - 空行 + `# Error encountered! Exiting...` + `# Please check the error message and the flags.`（`Main.mo:810-811`，main 的 else 分支）+ 空行（`:817` `print("\n")`）+ `Execution failed!`。
   - **不打印任何 flat 模型文本**（AssertInvalid1 / ArrayBounds1 / AssignConstant1 / BindingInvalidType1 / BuiltinAttribute3、P12a / P12c / P12d 全部如此）。
3. **单条消息行格式**：`[<path>:<l1>:<c1>-<l2>:<c2>:<rw>] <Kind>: <text>`，Kind ∈ {`Error`,`Warning`,`Notification`}，`<rw>` = `writable`/`readonly`（文件读写属性）。**方括号可缺省**：ConnectArrays4 的 `Warning: Allowing usage of enumeration…`、LookupLibrary1 的 `Notification: Automatically loaded package…` 都是纯 `Kind: text` 无定位。
   - **消息可跨行**：文本含换行时续行带缩进（AssertInvalid1：`Error: Type mismatch … The argument has type:` → 换行 `  String` → `expected type:` → `  Boolean`）。rtest 归一化会去掉行首空白（§8），所以续行缩进不影响 L1。
4. **消息顺序 = 收集顺序**；一条逻辑诊断可能是「Notification: From here:」+「Error: …」配对（P12a：先 Notification 指向改 final 属性的位置，再 Error 指向 type 声明处），多条独立消息顺序拼接（LookupLibrary1 三条 Notification 依 uses 依赖序）。
5. **解析错误帧**（词法/语法层，与实例化错误略不同，P12b）：
   ```
   Error processing file: X.mo
   Failed to parse file: X.mo!

   [X.mo:23:17-23:17:writable] Error: Missing token: SEMICOLON

   # Error encountered! Exiting...
   # Please check the error message and the flags.
   Failed to parse file: X.mo!

   Execution failed!
   ```
   `Failed to parse file: X.mo!` 出现**两次**（`Main.mo:384` `Print.printErrorBuf`：一次随第一段 showErrors 刷出、一次随 main else 分支的错误缓冲再刷）。
6. **status: correct 但带 Warning/Notification**：模型文本**先**整体打印（含末尾 `end X;\n`），消息**后**追加——因为 `Main.mo:460` 的 `showErrors` 排在 `translateModel`（内部已 `print(flatString)`）之后。退出码 0，**无** `# Error…`/`Execution failed!` 帧。ConnectArrays4（Warning 在 `end ConnectArrays4;` 之后）、LookupLibrary1（三条 Notification 在 `end LookupLibrary1;` 之后）实测。
7. 推论：**Error 永不与打印出来的模型文本共存**（NF 遇首个 Error 即中止实例化，来不及 print）；只有 Warning/Notification 会与模型共存。（观察结论，未穷举证明，记入「未确认」。）

**证据**：见「探针」节 AssertInvalid1、ConnectArrays4、P12a、P12b（原文）。

### 8. rtest 归一化（ymc 不需要逐字一致的方面）

来源：`tests/omc/run.py` 的 `normalize()`（rtest:296-316 复刻）+ `tests/omc/omc_diff.py` 的 `compare()`（`testsuite/difftool/omc-diff.l` 复刻）。ymc 与期望都过同一 `normalize()` 后再 token 级模糊比对，故以下方面 ymc **不必**逐字对齐：

1. **行首空白全部去掉**（`re.sub(r"^[ \t]+","",line)`）——缩进层数（2/4/8 空格）在 L1 里**不参与比较**。ymc 缩进只需给人读、字节数随意；但块结构关键字（`end if;`/`end for;`/`equation`…）仍逐字比。
2. **每行第一段连续空白折成一个空格**（`count=1`，仅第一处）。flat 输出行内本就单空格，无影响。
3. **实数按容差比**（omc_diff）：`.mo` eps=1e-7、`.mos` eps=5e-3，判据 `|actual-expected| > eps*|actual|+eps`。词法器把 `1.0` 和 `1` 都归 `T_REAL`（`-?[0-9]+(?:\.[0-9]+)?(?:[eE]…)?`），**仅当两侧都匹配纯整数 `-?[0-9]+\Z`** 才走整数精确比。推论：
   - **整数**（Integer 字面量、cref 下标 `arr[2]`、数组维度、枚举 index）→ 精确比，**必须逐字**。
   - **Real 字面量**含点/指数 → 浮点容差比：`1.0` vs `1`、`0.001` vs `1e-3`、`3.14159` vs `3.141590` 在 eps 内都算相等。但 §5 的指数式切换（如 `1e4` vs `10000.0`）虽数值相等也应对齐规则（保险；且差一个 ULP 的舍入串可能超 eps）。
4. **token 类型必须一致**（TEXT/REAL/NEWLINE）：`Real`↔`Integer`、`=`↔`:=`、括号/逗号有无都落在 TEXT，类型错位或 TEXT 逐字符不等即失败。
5. **TEXT token 内部空白折叠 + strip**（`_fold_ws`）——TEXT 段内多空格/首尾空格不敏感，但非空白字符逐字（`Real`≠`real`，`StateSelect.prefer`≠`StateSelect.always`，点分名 `r1.p.v` 逐字）。
6. **整行丢弃**：以 `GC Warning:`、`Unexpected end of /proc/mounts` 开头的行删掉；`/…\.mos_temp\d+/` 临时路径段抹平。
7. **孤立 `-`**（前后非数字）在词法器里被跳过、不产生 token；行号按 NEWLINE token 递增。

**净结论**——ymc 压平 dump **不必逐字**：缩进字节数、行首空白、Real 尾数在 eps 内的抖动。**必须逐字**：关键字 / 类型名 / 运算符 / 括号 / 逗号 / 点分 cref / 字符串内容 / 整数值（含下标与维度）/ token 顺序 / 行数；错误帧的方括号路径（§7，相对 vs 绝对）是复刻错误输出时的主要对齐负担。

## 探针

采集环境：安装版 omc 1.25.0（PATH），全部 `timeout 120`。原始 `.mo`/`.out` 在 `docs/research/probe-r13/`（P 系列）与 `C:/dev/OpenModelica/testsuite/flattening/modelica/scodeinst/`（AssertInvalid1、ConnectArrays4）。下列 15 个可直接固化为 ymc 压平 dump 单测的期望值。

- **P 系列命令**：`cd docs/research/probe-r13 && timeout 120 omc PNN.mo`（方括号错误路径为绝对路径）。
- **scodeinst 命令**：`cd .../scodeinst && timeout 120 omc --running-testsuite=<tmp> +locale=C X.mo`（方括号路径相对 testsuite 根，与 `// Result:` 一致）。

| 探针 | 固化点 | 文件 |
|---|---|---|
| P15 | 空类只两行 | `P15_empty.{mo,out}` |
| P01 | 段顺序 / protected 前缀 / 类注释转义 / 空段与注解不打印 | `P01_sections.{mo,out}` |
| P02 | 前缀顺序 / flow=0 / inner-outer 剥离 / 结构参数 `final` / 标量化 | `P02_prefixes.{mo,out}` |
| P03 | 属性固定顺序 / type 塌陷为内建 / 枚举变量头 / unbounded 丢弃 | `P03_attributes.{mo,out}` |
| P04 | 数组行主序标量化 / record 逐叶展开 / 下标 `[i,j]` 无空格 / 注释复制 / 零长数组 | `P04_types.{mo,out}` |
| P06 | Real 字面量格式（定点/指数切换、整数值补 `.0`） | `P06_reals.{mo,out}` |
| P17 | Real 边界（1e4 切指数）+ 函数变量维度写法 + CAST | `P17_reals2.{mo,out}` |
| P07 | 方程：connect 展开 / if-elseif / when-elsewhen / for 标量化 / assert level | `P07_equations.{mo,out}` |
| P08 | 语句：`:=` / 元组赋值补 `_` / while-break / if 语句 | `P08_statements.{mo,out}` |
| P09 | 函数排序 / inputs-outputs-locals / external 补全 / TSUB `[1]` | `P09_functions.{mo,out}` |
| P10 | record 构造器自动生成 / final 字段变 protected input | `P10_records.{mo,out}` |
| P14 | 负号/括号边界 / 枚举·Boolean 下标 / 整数负字面量不加括号 | `P14_small.{mo,out}` |
| P18 | 嵌套缩进（when→if→for=8）/ 方程与语句注释保留 | `P18_nested.{mo,out}` |
| AssertInvalid1 | incorrect 错误帧（多行 Error + `# …`/`Execution failed!`） | scodeinst |
| ConnectArrays4 | correct + Warning（模型在前、警告在后，无定位方括号） | scodeinst |

> 表达式括号/优先级的高压测试另见 `P05_expr.{mo,out}`、`P11_exprmisc.{mo,out}`、`P13_exprmisc2.{mo,out}`（较长，未内联；§4.2 的推论逐条由它们印证，期望值直接取自对应 `.out`）。

### P15 空类

```modelica
model P15_empty
  annotation(Documentation(info = "x"));
end P15_empty;
```
```
class P15_empty
end P15_empty;
```

### P01 段落 / 前缀 / 注释

```modelica
model P01_sections "class comment with \"quote\""
  Real x(start = 1.0);
  Real y;
  Real z;
protected
  Real w;
public
  parameter Real p = 2.0 "param comment";
initial equation
  x = 1.0;
initial algorithm
  z := 0.0;
equation
  der(x) = -p * x;
  w = x;
algorithm
  y := x + 1.0;
algorithm
  z := y * 2.0;
initial equation
  w = 3.0;
  annotation(Documentation(info = "<html>hi</html>"), experiment(StopTime = 2.0));
end P01_sections;
```
```
class P01_sections "class comment with \"quote\""
  Real x(start = 1.0);
  Real y;
  Real z;
  protected Real w;
  parameter Real p = 2.0 "param comment";
initial equation
  w = 3.0;
  x = 1.0;
initial algorithm
  z := 0.0;
equation
  der(x) = -p * x;
  w = x;
algorithm
  y := x + 1.0;
algorithm
  z := y * 2.0;
end P01_sections;
```

### P02 前缀 / flow / inner-outer

```modelica
connector C
  Real e;
  flow Real f;
  stream Real s;
end C;
model Inner
  outer Real g;
  Real v = g;
end Inner;
model P02_prefixes
  inner Real g = 9.81;
  Inner sub;
  C c;
  input Real u;  output Real yo;  discrete Real d;
  parameter Real p1 = 1.0;  final parameter Real p2 = 2.0;
  constant Real k = 3.0;  final constant Integer n = 2;
  parameter Integer m = 3 annotation(Evaluate = true);
  Real arr[n];  discrete input Integer di;  parameter input Real pin = 1.0;
protected
  parameter Real pp = 4.0;  final parameter Boolean fb = true;
  constant String s = "str";  discrete Integer i;  output Real po;
equation
  yo = u;  po = u;  arr = {1.0, 2.0};  c.e = 1.0;  c.s = 1.0;
  when time > 1 then
    d = 1.0;  i = 1;
  end when;
end P02_prefixes;
```
```
class P02_prefixes
  Real g = 9.81;
  Real sub.v = g;
  Real c.e;
  Real c.f;
  Real c.s;
  input Real u;
  output Real yo;
  discrete Real d;
  parameter Real p1 = 1.0;
  final parameter Real p2 = 2.0;
  constant Real k = 3.0;
  final constant Integer n = 2;
  final parameter Integer m = 3;
  Real arr[1];
  Real arr[2];
  discrete input Integer di;
  parameter input Real pin = 1.0;
  protected parameter Real pp = 4.0;
  protected final parameter Boolean fb = true;
  protected constant String s = "str";
  protected discrete Integer i;
  protected output Real po;
equation
  c.f = 0.0;
  yo = u;
  po = u;
  arr[1] = 1.0;
  arr[2] = 2.0;
  c.e = 1.0;
  c.s = 1.0;
  when time > 1.0 then
    d = 1.0;
    i = 1;
  end when;
end P02_prefixes;
```

### P03 属性顺序 / type 塌陷 / 枚举

```modelica
type Voltage = Real(final quantity = "ElectricPotential", final unit = "V", displayUnit = "kV");
type MyInt = Integer(min = 0, max = 10);
type Color = enumeration(red "r", green, blue);
model P03_attributes
  Real a(stateSelect = StateSelect.prefer, nominal = 10, fixed = true, start = 1, max = 100, min = -100, displayUnit = "mm", unit = "m", quantity = "Length");
  Voltage v(start = 0.5);
  Voltage v2(displayUnit = "mV", nominal = 1e-3);
  Integer i(start = 2, fixed = true, max = 5, min = 1, quantity = "count");
  MyInt mi(start = 3);
  Boolean b(fixed = true, start = false, quantity = "flag");
  String s(start = "abc", quantity = "name");
  Color c(start = Color.green, fixed = true, max = Color.blue, min = Color.red, quantity = "col");
  Real u(unbounded = true, start = 2);
  Real ss1(stateSelect = StateSelect.never);   Real ss4(stateSelect = StateSelect.always);
  parameter Real p(start = 1.5, fixed = false);
  parameter Real q(min = 0) = 2 "with binding and attr";
  Real r(start = q + 1);
equation
  der(a) = 1; v = 1; v2 = 1; u = 1; ss1 = 1; ss4 = 1; r = 1;
  when time > 1 then
    i = 3; mi = 4; b = true; s = "x"; c = Color.red;
  end when;
initial equation
  p = 3;
end P03_attributes;
```
```
class P03_attributes
  Real a(quantity = "Length", unit = "m", displayUnit = "mm", min = -100.0, max = 100.0, start = 1.0, fixed = true, nominal = 10.0, stateSelect = StateSelect.prefer);
  Real v(quantity = "ElectricPotential", unit = "V", displayUnit = "kV", start = 0.5);
  Real v2(quantity = "ElectricPotential", unit = "V", displayUnit = "mV", nominal = 0.001);
  Integer i(quantity = "count", min = 1, max = 5, start = 2, fixed = true);
  Integer mi(min = 0, max = 10, start = 3);
  Boolean b(quantity = "flag", start = false, fixed = true);
  String s(quantity = "name", start = "abc");
  enumeration(red, green, blue) c(quantity = "col", min = Color.red, max = Color.blue, start = Color.green, fixed = true);
  Real u(start = 2.0);
  Real ss1(stateSelect = StateSelect.never);
  Real ss4(stateSelect = StateSelect.always);
  parameter Real p(start = 1.5, fixed = false);
  parameter Real q(min = 0.0) = 2.0 "with binding and attr";
  Real r(start = q + 1.0);
initial equation
  p = 3.0;
equation
  der(a) = 1.0;
  v = 1.0;
  v2 = 1.0;
  ...（u/ss1/ss4/r = 1.0，when 段见 P03_attributes.out 全文）
```
> 注：上块为节选（删去了 ss2/ss3 及 when 分支以缩短篇幅）；单测请用 `P03_attributes.out` 全文。关键固化点：属性顺序 `quantity,unit,displayUnit,min,max,start,fixed,nominal,stateSelect`；`Voltage`→`Real` 且 type 上的 `final unit="V"` 合入、组件 `displayUnit` 覆盖；`enumeration(red, green, blue)` 作类型头；`unbounded` 丢弃；Integer 属性值不加 `.0`（`min = 1`）而 Real 加（`min = -100.0`）。

### P04 数组标量化 / record 展开

完整期望见 `P04_types.out`（76 行）。固化点摘录：

```
  Real nn.r.x;
  Integer nn.r.n = 1;
  enumeration(one, two) nn.r.e = Pkg.E.one;
  Real nn.z[1];
  Real nn.z[2];
  Real m.x "inner comment";
  final parameter Integer m.n = 2;
  Real m.a[1] "array comment";
  Real m.a[2] "array comment";
  Real mat[1,1] "matrix";     // 下标逗号无空格；行主序 [1,1][1,2][1,3][2,1]…
  parameter Real p[1] = 1.0;  // {1,2} 标量化后每元素一行、值转 Real
  Boolean bb[1](start = true);// each start=true → 每行都带 start=true
  Real st[1](start = 1.0, fixed = true);   // start={1,2,3} 分派 + each fixed
```
`Real e0[0];`（零长数组）**不产生任何行**。record 成员逐叶展开为点分名，无 record 类型名；组件注释（`M m "component comment"`、record 自身注释）**丢失**，仅叶子变量自带注释保留。

### P06 Real 字面量（关键）

```modelica
model P06_reals
  constant Real r01 = 1e5;      constant Real r02 = 1e6;
  constant Real r03 = 0.001;    constant Real r04 = 1e-4;
  constant Real r06 = 1.0 / 3.0;constant Real r08 = -2.5;
  constant Real r09 = 1;        constant Real r10 = 123456789.0;
  constant Real r11 = 1234567890123456789.0;
  constant Real r15 = 2.0e20;   constant Real r23 = 4.9e-324;
  constant Real r29 = 1.e3;     constant Real r30 = .5;
  constant Real r31 = 2.5E+3;   constant Real r33 = 1000000.0;
  constant Real r34 = 9999999.0;constant Real r35 = 0.0001234;
  constant Real r36 = 1.0e-3 * 3;
  Real x(start = -1e-7, nominal = 1e9, min = -1e60);
equation
  x = 1e5 * time + 2.5e-3;
end P06_reals;
```
```
  constant Real r01 = 1e5;
  constant Real r02 = 1e6;
  constant Real r03 = 0.001;
  constant Real r04 = 1e-4;
  constant Real r06 = 0.3333333333333333;
  constant Real r08 = -2.5;
  constant Real r09 = 1.0;
  constant Real r10 = 1.23456789e8;
  constant Real r11 = 1.2345678901234568e18;
  constant Real r15 = 2e20;
  constant Real r23 = 5e-324;
  constant Real r29 = 1000.0;
  constant Real r30 = 0.5;
  constant Real r31 = 2500.0;
  constant Real r33 = 1e6;
  constant Real r34 = 9.999999e6;
  constant Real r35 = 1.234e-4;
  constant Real r36 = 0.003;
  Real x(min = -1e60, start = -1e-7, nominal = 1e9);
equation
  x = 1e5 * time + 0.0025;
```
（全部 36 条见 `P06_reals.out`。）

### P17 Real 边界 + 函数变量维度

```modelica
model P17_reals2
  function fv
    input Real v[3];
    output Real y;
  algorithm
    y := sum(v);
  end fv;
  constant Real c1 = 10000.0;  constant Real c2 = 12000.0;
  constant Real c3 = 120000.0; constant Real c5 = 0.00012;
  constant Real c7 = 1230000.0;constant Real c9 = 0.01;
  Integer iv[3];
  Real y = fv(iv);
equation
  iv = {integer(time), 2, 3};
end P17_reals2;
```
```
function P17_reals2.fv
  input Real[3] v;
  output Real y;
algorithm
  y := v[1] + v[2] + v[3];
end P17_reals2.fv;

class P17_reals2
  constant Real c1 = 1e4;
  constant Real c2 = 12000.0;
  constant Real c3 = 1.2e5;
  constant Real c5 = 1.2e-4;
  constant Real c7 = 1.23e6;
  constant Real c9 = 0.01;
  Integer iv[1];
  Integer iv[2];
  Integer iv[3];
  Real y = P17_reals2.fv(/*Real[3]*/(iv));
equation
  iv[1] = integer(time);
  iv[2] = 2;
  iv[3] = 3;
end P17_reals2;
```

### P07 方程（connect / if / when / for / assert）

```modelica
connector Pin
  Real v;
  flow Real i;
end Pin;
model Res
  Pin p, n;
  parameter Real R = 1;
equation
  p.v - n.v = R * p.i;
  p.i + n.i = 0;
end Res;
model P07_equations
  Res r1, r2;
  Real x(start = 1), y, z;
  discrete Real d1, d2;
  parameter Boolean flag = true;
  parameter Boolean pb(fixed = false);
  parameter Integer n = 3;
  Real arr[n];
  Boolean cond;
initial equation
  pb = true;
  if pb then y = 1; else y = 2; end if;
equation
  connect(r1.n, r2.p);  connect(r1.p, r2.n);  r1.p.v = 0;
  der(x) = -x "equation comment";
  cond = x > 0.5;
  if cond then z = 1;
  elseif x > 0.2 then z = 2; assert(z > 0, "inner assert");
  elseif x > 0.1 then z = 3;
  else z = 4; end if;
  if flag then y = x; else y = -x; end if;
  for i in 1:n loop arr[i] = i * x; end for;
  when x < 0.5 then
    d1 = pre(d1) + 1;  reinit(x, 1.0);
  elsewhen x < 0.3 then d1 = 0;
  elsewhen {x < 0.2, x < 0.1} then d1 = -1;
  end when;
  when initial() then d2 = 1; end when;
  assert(x > -1, "x too small");
  assert(x < 10, "x large: " + String(x), AssertionLevel.warning);
  assert(x < 20, "x larger", level = AssertionLevel.error);
  when x < 0.05 then terminate("done"); end when;
end P07_equations;
```
```
class P07_equations
  Real r1.p.v;
  Real r1.p.i;
  Real r1.n.v;
  Real r1.n.i;
  parameter Real r1.R = 1.0;
  Real r2.p.v;
  Real r2.p.i;
  Real r2.n.v;
  Real r2.n.i;
  parameter Real r2.R = 1.0;
  Real x(start = 1.0);
  Real y;
  Real z;
  discrete Real d1;
  discrete Real d2;
  final parameter Boolean flag = true;
  parameter Boolean pb(fixed = false);
  final parameter Integer n = 3;
  Real arr[1];
  Real arr[2];
  Real arr[3];
  Boolean cond;
initial equation
  pb = true;
  if pb then
    y = 1.0;
  else
    y = 2.0;
  end if;
equation
  r1.n.v = r2.p.v;
  r1.p.v = r2.n.v;
  r2.n.i + r1.p.i = 0.0;
  r2.p.i + r1.n.i = 0.0;
  r1.p.v - r1.n.v = r1.R * r1.p.i;
  r1.p.i + r1.n.i = 0.0;
  r2.p.v - r2.n.v = r2.R * r2.p.i;
  r2.p.i + r2.n.i = 0.0;
  r1.p.v = 0.0;
  der(x) = -x "equation comment";
  cond = x > 0.5;
  if cond then
    z = 1.0;
  elseif x > 0.2 then
    z = 2.0;
    assert(z > 0.0, "inner assert");
  elseif x > 0.1 then
    z = 3.0;
  else
    z = 4.0;
  end if;
  y = x;
  arr[1] = x;
  arr[2] = 2.0 * x;
  arr[3] = 3.0 * x;
  when x < 0.5 then
    d1 = pre(d1) + 1.0;
    reinit(x, 1.0);
  elsewhen x < 0.3 then
    d1 = 0.0;
  elsewhen {x < 0.2, x < 0.1} then
    d1 = -1.0;
  end when;
  when initial() then
    d2 = 1.0;
  end when;
  assert(x > -1.0, "x too small");
  assert(x < 10.0, "x large: " + String(x, 6, 0, true), AssertionLevel.warning);
  assert(x < 20.0, "x larger");
  when x < 0.05 then
    terminate("done");
  end when;
end P07_equations;
```
固化点：connect 生成的等式排 `equation` 段最前、势变量 `a = b`、流变量求和 `= 0.0`、未连接 flow 未在此模型顶层（Res 内部已连），`if flag`（参数条件）被选支消掉只剩 `y = x;`，`for` 标量化，assert level=error 省第三参、warning 保留。

### P08 语句（元组赋值 / while / break）

```modelica
model P08_statements
  function f2
    input Real x;
    output Real a;
    output Real b;
  algorithm
    a := x; b := 2 * x;
  end f2;
  Real x(start = 1), y, z, a, b, s;  Integer k;  discrete Real d;  Real arr[3];
algorithm
  y := x + 1 "stmt comment";
  if x > 0.5 then z := 1; elseif x > 0.2 then z := 2; else z := 3; end if;
  if x > 2 then z := 5; end if;
  s := 0;
  for i in 1:3 loop s := s + i; arr[i] := s; end for;
  for i in 1:2:5 loop s := s + i; end for;
  for e in {1.5, 2.5} loop s := s + e; end for;
  k := 0;
  while k < 3 loop k := k + 1; if k == 2 then break; end if; end while;
  (a, b) := f2(x);  (, b) := f2(x);  (a, ) := f2(x);
  when x < 0.5 then d := pre(d) + 1; elsewhen x < 0.3 then d := 0; end when;
  assert(x > -1, "msg");
  when x < 0.05 then terminate("bye"); end when;
equation
  der(x) = -x;
end P08_statements;
```
```
function P08_statements.f2
  input Real x;
  output Real a;
  output Real b;
algorithm
  a := x;
  b := 2.0 * x;
end P08_statements.f2;

class P08_statements
  Real x(start = 1.0);
  Real y;
  Real z;
  Real a;
  Real b;
  Real s;
  Integer k;
  discrete Real d;
  Real arr[1];
  Real arr[2];
  Real arr[3];
equation
  der(x) = -x;
algorithm
  y := x + 1.0 "stmt comment";
  if x > 0.5 then
    z := 1.0;
  elseif x > 0.2 then
    z := 2.0;
  else
    z := 3.0;
  end if;
  if x > 2.0 then
    z := 5.0;
  end if;
  s := 0.0;
  for i in 1:3 loop
    s := s + /*Real*/(i);
    arr[i] := s;
  end for;
  for i in 1:2:5 loop
    s := s + /*Real*/(i);
  end for;
  for e in {1.5, 2.5} loop
    s := s + e;
  end for;
  k := 0;
  while k < 3 loop
    k := k + 1;
    if k == 2 then
      break;
    end if;
  end while;
  (a, b) := P08_statements.f2(x);
  (_, b) := P08_statements.f2(x);
  (a, _) := P08_statements.f2(x);
  when x < 0.5 then
    d := pre(d) + 1.0;
  elsewhen x < 0.3 then
    d := 0.0;
  end when;
  assert(x > -1.0, "msg");
  when x < 0.05 then
    terminate("bye");
  end when;
end P08_statements;
```
固化点：`equation` 段排在 `algorithm` 之前（段固定顺序）；元组空位打 `_`；算法 for **不展开**；`i`（Integer 迭代量）在 Real 上下文 `/*Real*/(i)`。

### P09 函数（排序 / external / TSUB）

完整期望见 `P09_functions.out`（60 行）。固化点：
- 函数按名字 ASCII 排序：`P09.ext2`、`P09.ext3`、`P09.imp`、`P09.noalg`、`P09.zeta`（大写先于小写；`alpha`（external "C" sin）被换成内建 `sin(time)` 故不出现）。
- 变量行顺序 inputs→outputs→locals，locals 带 `protected`；函数内维度写类型后 `Real[:] v`、`Real[:, 2] m`、`Real[size(v, 1)] w`、`Real[n] loc`。
- external：变量行后**空一行**再 `  external "C" y = my_sum(x, size(x, 1));`；`external "C";` 补全成 `external "C" y = ext3(x);`；1.25.0 把 external 函数标 `impure function`。
- `noalg`（无 algorithm 段）直接 `end`。
- 调用点：`(, w2) = zeta(...)` → `(_, w2) = P09.zeta(...)`；单输出取元组 `Real a = P09.zeta(...)[1];`（TSUB）；命名/缺省参数补成位置参 `P09.zeta(time, 2, {1.0, 2.0}, {{1.0, 2.0}})`。

### P10 record 构造器

```modelica
record R "record R"
  Real x "x comment";  Integer n = 1;
  parameter Real p = 2.0;  constant Real c = 3.0;
  final Real fin = 4.0;  Real arr[2];
end R;
record Small
  Real a;  Real b;
end Small;
function mk
  input Real u;  output Small s;
algorithm
  s := Small(u, 2 * u);
end mk;
function useR
  input R r;  output Real y;
algorithm
  y := r.x + r.arr[1];
end useR;
model P10_records
  Small s1 = Small(1, 2);  Small s2 = mk(time);  Small s3;
  parameter Small ps = Small(a = 3, b = 4);
  R r(x = 1, arr = {1, 2});  Real y = useR(r);
equation
  s3 = mk(2 * time);
end P10_records;
```
```
function R "Automatically generated record constructor for R"
  input Real x;
  input Integer n = 1;
  input Real p = 2.0;
  input Real c = 3.0;
  protected Real fin = 4.0;
  input Real[2] arr;
  output R res;
end R;

function Small "Automatically generated record constructor for Small"
  input Real a;
  input Real b;
  output Small res;
end Small;

function mk
  input Real u;
  output Small s;
algorithm
  s := Small(u, 2.0 * u);
end mk;

function useR
  input R r;
  output Real y;
algorithm
  y := r.x + r.arr[1];
end useR;

class P10_records
  Real s1.a = 1.0;
  Real s1.b = 2.0;
  Real s2.a;
  Real s2.b;
  Real s3.a;
  Real s3.b;
  parameter Real ps.a = 3.0;
  parameter Real ps.b = 4.0;
  Real r.x = 1.0 "x comment";
  Integer r.n = 1;
  parameter Real r.p = 2.0;
  constant Real r.c = 3.0;
  final Real r.fin = 4.0;
  Real r.arr[1];
  Real r.arr[2];
  Real y = useR(r);
equation
  s2 = mk(time);
  r.arr = {1.0, 2.0};
  s3 = mk(2.0 * time);
end P10_records;
```
固化点：自动生成的构造器注释文字 `Automatically generated record constructor for <R>`；`final` 字段 → `protected`（不作 input）；`output R res;`；构造器排在普通函数之间（按名 ASCII：`R`,`Small`,`mk`,`useR`）。

### P14 负号 / 括号边界 / 枚举·Boolean 下标

```modelica
model P14_small
  type E = enumeration(a, b);
  String s(start = "abc", fixed = true);
  Real xe[E] = {1, 2};   Real xb[Boolean] = {3, 4};
  Integer k(start = -1);
  Integer j = k * (-1) + (-3) - (-k);
  Real r = 2 * (-3.5) + k;
  Real t = xe[E.a] + xb[true];
  parameter Real pneg = -1;  parameter Integer ineg = -2;
  Real u = -pneg + ineg;
equation
  when time > 1 then s = "x"; k = pre(k) - 1; end when;
end P14_small;
```
```
class P14_small
  String s(start = "abc");
  Real xe[E.a];
  Real xe[E.b];
  Real xb[false];
  Real xb[true];
  Integer k(start = -1);
  Integer j = k * -1 - 3 + k;
  Real r = /*Real*/(k) - 7.0;
  Real t = xe[E.a] + xb[true];
  parameter Real pneg = -1.0;
  parameter Integer ineg = -2;
  Real u = /*Real*/(ineg) - pneg;
equation
  xe = {1.0, 2.0};
  xb = {3.0, 4.0};
  when time > 1.0 then
    s = "x";
    k = pre(k) - 1;
  end when;
end P14_small;
```
固化点：`String` 的 `fixed` 不打印（只剩 `start`）；枚举/Boolean 维度标量化下标 `xe[E.a]`/`xb[false]`；`k * (-1)` → `k * -1`（Integer 负字面量优先级 0，不加括号）；`(-3) - (-k)` → `- 3 + k`（前端改写）；`2 * (-3.5) + k` 常量折叠成 `/*Real*/(k) - 7.0`；`-pneg + ineg` → `/*Real*/(ineg) - pneg`。

### P18 嵌套缩进 + 注释保留

```modelica
model P18_nested
  function noret
    input Real x;
  algorithm
    assert(x > -100, "neg");
  end noret;
  function early
    input Real x;  output Real y;
  algorithm
    y := x;
    if x > 1 then return; end if;
    while true loop y := y + 1; if y > 10 then break; end if; end while;
  end early;
  Real x(start = 1), y, z;  discrete Real d;  Boolean c1, c2;
equation
  der(x) = -x;  c1 = x > 0.5;  c2 = x > 0.25;
  noret(x) "call comment";
  y = early(x);
  if c1 then
    if c2 then z = 1 "inner eq comment"; else z = 2; end if "inner if comment";
  else z = 3; end if "outer if comment";
  when c1 then d = 1; elsewhen c2 then d = 2; end when "when comment";
algorithm
  when c1 then
    if c2 then
      for i in 1:2 loop noret(x + i); end for "for comment";
    end if "if stmt comment";
  end when "when stmt comment";
end P18_nested;
```
```
function P18_nested.early
  input Real x;
  output Real y;
algorithm
  y := x;
  if x > 1.0 then
    return;
  end if;
  while true loop
    y := y + 1.0;
    if y > 10.0 then
      break;
    end if;
  end while;
end P18_nested.early;

function P18_nested.noret
  input Real x;
algorithm
  assert(x > -100.0, "neg");
end P18_nested.noret;

class P18_nested
  Real x(start = 1.0);
  Real y;
  Real z;
  discrete Real d;
  Boolean c1;
  Boolean c2;
equation
  der(x) = -x;
  c1 = x > 0.5;
  c2 = x > 0.25;
  P18_nested.noret(x) "call comment";
  y = P18_nested.early(x);
  if c1 then
    if c2 then
      z = 1.0 "inner eq comment";
    else
      z = 2.0;
    end if "inner if comment";
  else
    z = 3.0;
  end if "outer if comment";
  when c1 then
    d = 1.0;
  elsewhen c2 then
    d = 2.0;
  end when "when comment";
algorithm
  when c1 then
    if c2 then
      for i in 1:2 loop
        P18_nested.noret(x + /*Real*/(i));
      end for "for comment";
    end if "if stmt comment";
  end when "when stmt comment";
end P18_nested;
```
固化点：注释挂在结构结尾分号前（`end if "…";`、`end when "…";`、`end for "…";`、`noret(x) "call comment";`）；缩进逐层 +2（when→if→for 内到 8）；无返回调用打全限定名。

### AssertInvalid1（scodeinst incorrect：多行 Error 帧）

```modelica
model AssertInvalid1
equation
  assert("true", "message");
end AssertInvalid1;
```
```
Error processing file: AssertInvalid1.mo
[flattening/modelica/scodeinst/AssertInvalid1.mo:8:3-8:28:writable] Error: Type mismatch for positional argument 1 in assert(condition="true"). The argument has type:
  String
expected type:
  Boolean

# Error encountered! Exiting...
# Please check the error message and the flags.

Execution failed!
```
（`--running-testsuite` 下方括号为相对路径；裸 `omc` 下为绝对路径。）

### ConnectArrays4（scodeinst correct + Warning：模型在前、警告在后）

```modelica
model ConnectArrays4
  connector C
    Real e;
    flow Real f;
  end C;
  type E = enumeration(a, b, c);
  C c[2], c2[2];
equation
  connect(c[E.a], c2[E.a]);
  connect(c[1], c2[2]);
  annotation(__OpenModelica_commandLineOptions="--allowNonStandardModelica=nonStdEnumerationAsIntegers");
end ConnectArrays4;
```
```
class ConnectArrays4
  Real c[1].e;
  Real c[1].f;
  Real c[2].e;
  Real c[2].f;
  Real c2[1].e;
  Real c2[1].f;
  Real c2[2].e;
  Real c2[2].f;
equation
  c[1].e = c2[2].e;
  c[1].e = c2[1].e;
  -(c[1].f + c2[2].f + c2[1].f) = 0.0;
  c[1].f = 0.0;
  c[2].f = 0.0;
  c2[1].f = 0.0;
  c2[2].f = 0.0;
end ConnectArrays4;
Warning: Allowing usage of enumeration expression: E.a as Integer: 1. This is non-standard Modelica, use Integer(E.a) instead!
```
固化点：Warning 无定位方括号、排在 `end ConnectArrays4;` 之后；退出码 0，无 `# Error`/`Execution failed!` 帧。

## 未确认 / 待验证

1. **`/* scalar Real */`、`/*ASUB*/` 等类型注释的开关名**：这些分支在 `ExpressionDumpTpl.tpl` 的 `if typeinfo()` 里（§4.9），默认路径不出现，只 `/*Real*/(…)` CAST 会打。触发它们的 flag（疑似 `-d=printTypeInfo` 或类似）未核实——scodeinst 期望输出不涉及，暂不阻塞。
2. **合并后 `initial equation` 段内「后写的段排在前」的来源**（§2.8(a)，P01：`w = 3.0;` 在 `x = 1.0;` 前）：这是前端 flatten 的收集顺序，非打印器行为，源码位置留给 R14（flatten 顺序）确认。
3. **错误帧里消息与 `# Error` 之间那个空行的确切来源**：由 `Print`/`ErrorExt` 缓冲刷出机制决定（§7.2/§7.5），empirically 稳定但未逐字节回溯 buffer 逻辑。以实测原文为准。
4. **`--running-testsuite` 把绝对路径改写成相对 testsuite 根的具体代码路径**（`ErrorExt`/`Error.mo` 附近）未锚到 file:line；行为已由实跑固定（§7.1）。
5. **§7.7「Error 永不与打印出来的模型共存」**为观察结论（NF 遇首个 Error 即中止 `print`），未穷举证明；若日后发现 recover 后仍 print 的场景需回填。
6. **1.25.0 与 HEAD 的 ASUB 下标空格差异**（§1.2：`[1,2]` vs `[1, 2]`）只在前端生成 ASUB 的式子里可见，Modelica 源码写不出；ymc 以 **1.25.0（oracle）= `[1, 2]`** 为准。

## 对 ymc 的建议

压平 dump 打印器（记为 `flat_dump`）建议按下述结构实现，逐条对齐本报告；oracle 永远是安装版 omc 1.25.0，L1 比较走 `tests/omc/run.py`（§8 归一化）。

**1. 顶层版式**
- 函数块在前、`class` 在后；函数之间、末函数与 `class` 之间各空一行（§1.5、§6.8）。函数集合 = 被实际用到的、可见的函数 + record 构造器，按**函数全限定名字符串 ASCII 序**排序（大写先于小写）。
- `class <fullName>[ " <escaped 类注释>"]` … `end <fullName>;`，末尾恰一个 `\n`，换行 LF（§2.1、§1.6）。`fullName` = 带包前缀点分名。关键字恒 `class`（不分 model/block/connector）。
- 段固定顺序：**变量 → initial equation → initial algorithm → equation → algorithm**（§2.3）；空段不打印；所有 equation 合并成一个 `equation`，每个源 algorithm 段各打一次 `algorithm`（§2.5）。

**2. 变量行**（字段顺序，§3.1）
```
"  " [protected ] [final ] [parglobal|parlocal ] [constant|parameter|discrete ] [input|output ] <Type>[<[dims]> 仅函数/未标量化] " " <dotted.name>[<subscripts>] [(<attrs>)] [ = <binding>] [ " <cmt>"] ";"
```
- 每个前缀自带前导空格；不打印 flow/stream/inner/outer/each/redeclare/replaceable。**结构参数（Evaluate=true、用作维度、用作 if 条件等）一律 `final parameter`**（§3.1）。
- 类型只塌陷成内建 `Real/Integer/Boolean/String/Clock` 或 `enumeration(lit1, lit2, …)`；用户 type/record 名不保留（§3.2）。
- 默认标量化：数组按**行主序**每元素一行，下标写名字里、`,` **无空格**（`mat[1,2]`）；函数内/未标量化：维度写**类型后**、`, ` **有空格**（`Real[:, 2]`），未知维 `:`。

**3. 属性顺序表**（`(a = e1, b = e2)`，`, ` 分隔，全空则整个括号省略；§3.4）
| 类型 | 顺序 |
|---|---|
| Real | quantity, unit, displayUnit, min, max, start, fixed, nominal, stateSelect, uncertainty, distribution |
| Integer / 枚举 | quantity, min, max, start, fixed |
| Boolean | quantity, start, fixed |
| String | quantity, start（**无 fixed**） |
- `unbounded` 丢弃；`stateSelect`/枚举值打全限定名（`StateSelect.prefer`、`Color.red`）；Integer 属性值不加 `.0`，Real 加。绑定 `= <exp>` 在属性括号后、注释前。组件注解默认不打印。

**4. 表达式打印器**（§4）
- 复用**优先级/括号表**（§4.2：`^`=1，`*`=2/右3，`/`=2，一元负及负 Real 字面量=4，`+`=5/右6，`-`=5，关系=6，`not`=7，`and`=8，`or`=9，range=10，if 表达式=11，其余=0；判右操作数时外层也取右侧优先级；操作数是 UNARY 必加括号）。
- 版式表（§4.1）：二元 `lhs op rhs` 两侧空格、一元负 `-x` 无空格、range 冒号无空格、调用 `name(a, b)`、下标 `[i,j]` 无空格、数组 `{…}`、CAST `/*Type*/(exp)`（Boolean 在此叫 `Bool`）、数组推导打 `array(e for i in r)`、`if c then a else b`（无 `elseif`，嵌套 `else if`）。
- **前端改写**（`-a+b`→`b-a`、命名参数补位、内建缺省参补齐、常量折叠、字面量 Integer→Real…）**不是打印器职责**，但期望输出里到处是，ymc 要在 typing/simplify 阶段对齐（§4.3；各归对应报告）。

**5. Real 格式** —— 直接复用 S1 已实现的 `real_literal_text`（= Ryu `d2s` 最短往返 + `ryu_to_hr`，§5）。判据：`exp<-3` 或 `exp>5` 或 (`exp>0` 且 `exp-ndec>3`) → 指数式 `<尾数>e<exp>`（无 `+`/前导零，整数尾数不补 `.0`）；否则定点式，整数值补 `.0`。**上线前用 P06/P17 全表回归**确认与 omc 1.25.0 逐字一致（尤其 `1e4`/`12000.0`、`5e-324`、`0.3333333333333333` 这些边界）。

**6. 方程/语句/函数**（§6）
- 等式 `lhs = rhs[" cmt"];`；赋值 `lhs := rhs[" cmt"];`；元组空位 `_`。
- if/for/when 方程与语句版式见 §6.3-6.7；`elseif`/`elsewhen` 连写（方程/语句），if **表达式**用 `else if`；整个结构只一个 `end`。缩进逐层 +2（L1 不比字节但要给人读）。
- assert level=error 省第三参、warning 打 `, AssertionLevel.warning`；terminate/reinit/无返回调用见 §6.6。
- 函数变量顺序 inputs→outputs→locals（locals 带 protected）；`algorithm` 顶格；external 变量行后空一行再补全的调用式；record 构造器注释固定文案。

**7. 错误帧**（§7，L1 需要复刻）
- incorrect：`Error processing file: <basename>.mo\n` + 消息 + 空行 + `# Error encountered! Exiting...\n# Please check the error message and the flags.\n` + 空行 + `Execution failed!`；不打印模型。
- 消息行 `[<path>:l:c-l:c:<rw>] <Kind>: <text>`（方括号可缺省）；path 在 testsuite 下相对 testsuite 根——ymc 跑 rtest 时须产出**相对路径**才能 L1 匹配（当前 run.py 对 ymc 不加 `--running-testsuite`，ymc 需自行按 basename/相对根输出，属对齐负担）。
- correct + Warning/Notification：模型先打、消息后追加、退出码 0。

**8. 单测探针清单**（期望值取自对应 `.out`；见「探针」节）
- 结构：P15（空）、P01（段序+protected+注释转义）、P18（嵌套缩进+注释位置）。
- 变量/属性/类型：P02（前缀+flow+inner/outer+结构参数）、P03（属性顺序+type 塌陷+枚举）、P04（标量化+record 展开+零长数组）。
- Real：P06、P17（边界+函数维度+CAST）。
- 方程/语句/函数：P07（connect/if/when/for/assert）、P08（元组赋值/while/段序）、P09（排序/external/TSUB）、P10（record 构造器）。
- 表达式括号：P05 / P11 / P13 / P14（负号与括号边界、枚举/Boolean 下标）。
- 错误帧：AssertInvalid1（incorrect 多行 Error）、ConnectArrays4（correct+Warning 顺序）。
