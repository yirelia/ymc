# R14 NF 类型检查、编译期求值与化简（决定压平文本里"表达式长什么样"）

> 研究对象：`C:\dev\OpenModelica`（HEAD 3ceb0c9d06）；oracle = 安装版 omc 1.25.0（`omc X.mo`，全部带 `timeout 120`）。
> 约定：锚点写作 `文件:行`（路径相对 `OMCompiler/Compiler/`，未写目录的 `NF*.mo` 均在 `NFFrontEnd/`；行号为 HEAD 3ceb0c9d06，引用前已 grep/Read 复核）；探针输出为 omc 1.25.0 实跑原文，探针文件在 `docs/research/probe-r14/`。用例名指 `testsuite/flattening/modelica/scodeinst/<名>.mo`。
> 状态：**增量写作中**（每完成一节即追加；未完成的节标注"待写"）。

## 结论摘要

压平文本里"表达式长什么样"由**四道工序**决定，顺序即语义（NFInst.mo:194–216）：`instExpressions` → `updateImplicitVariability`（把部分 parameter 标 structural/final）→ `typeClass`（三趟定型 + 维度/条件立即求值）→ `Flatten`（if 方程/条件组件裁剪）→ `EvalConstants`（把 constant/structural 的 cref **替换成值**，只替换不折叠）→ `SimplifyModel`（字面量折叠 + 局部代数化简）。要点：

1. **类型**：`ARRAY` 扁平（元素类型 + 维度向量，不嵌套）；唯一标准隐式转换是 `Integer→Real`，打印成 `/*Real*/(e)`，尽量下推到字面量、推不动才包 CAST。
2. **variability**：七档 `CONSTANT<STRUCTURAL_PARAMETER<PARAMETER<NON_STRUCTURAL_PARAMETER<DISCRETE<IMPLICITLY_DISCRETE<CONTINUOUS`，组件初始档**只由前缀**决定（Integer/Boolean 无前缀是 CONTINUOUS，**不是** discrete）；`STRUCTURAL_PARAMETER` 是"用于维度/条件/if 条件/Evaluate=true"事后标的，且**连带设 final**（输出里显示 `final parameter`）。
3. **求值/代入**：constant **总被**代入成值；structural parameter 尽力代入；普通 parameter **保留符号**。声明行绑定同理（constant/structural 行显示字面量，parameter 行保留符号）。
4. **化简**：纯局部单趟；字面量交给 `Ceval`，代数只做 `x*1/x+0/-(-x)/x^0` 等恒等式与短路/关系/if 折叠；**不做** `x+x→2*x`、不跨树收集常量（`2*x*3` 保留）。
5. **裁剪**：parameter 条件的 if 方程/条件组件在编译期选支/删除；分支方程数不等 NF 前端**不查**（留给后端）。

## 逐项事实

### 0. 总流水线（本报告各节在哪一趟发生）

`NFInst.instClassInProgram`（NFInst.mo:148–283）里与本题相关的顺序，**顺序本身就是语义**：

| 序 | 调用 | 锚点 | 作用 |
|---|---|---|---|
| 1 | `instExpressions` | NFInst.mo:194 | 绑定/维度/方程里的名字解析成 cref（未定型） |
| 2 | `updateImplicitVariability(inst_cls, Flags.isSet(Flags.EVAL_PARAM), context)` | NFInst.mo:198 | **在定型之前**把部分 parameter 标成 STRUCTURAL_PARAMETER（§2） |
| 3 | `Typing.typeClass` | NFInst.mo:202 | 三趟定型（本节）；期间维度/条件组件条件被**立即求值**，if 方程条件被判定是否 structural |
| 4 | `Flatten.flatten` | NFInst.mo:205 | 压平；`flattenIfEquation` 在这里对 structural 条件求值并裁剪分支（§6）；`verifyBinding` 报无绑定参数（§3） |
| 5 | `EvalConstants.evaluate` | NFInst.mo:206 | 把 constant / structural parameter 的 **cref 替换成值**（§3）——只替换，不折叠 |
| 6 | `UnitCheck.checkUnits` | NFInst.mo:211 | 默认关（`-d=frontEndUnitCheck` 默认 false，Flags.mo:482） |
| 7 | `SimplifyModel.simplify`（除非 `--noSimplify`） | NFInst.mo:214–216 | 字面量折叠与代数化简（§4） |
| 8 | `Package.collectConstants` / `collectFunctions` | NFInst.mo:221–224 | 没被替换掉的包常量补进模型 |
| 9 | `Scalarize.scalarize`（`-d=nfScalarize` 默认 true，Flags.mo:504） | NFInst.mo:235–236 | 数组拆标量 |
| 10 | `VerifyModel.verify` | NFInst.mo:262 | 压平后的合法性检查 |

用 `--noSimplify` 可以把第 5 步与第 7 步的效果分开看（探针 E01 对照组，见 §3.6）。

### 1. `NFTyping.typeClass` 三趟、类型表示、类型匹配

#### 1.1 三趟（NFTyping.mo:124–138）

```
typeClassType(cls, EMPTY_BINDING, ctx, cls)   // 根类自身的 Type
typeComponents(cls, ctx)      // 趟 1：每个组件的 Type（含维度求值、条件组件条件求值）
typeBindings(cls, ctx)        // 趟 2：绑定表达式定型 + 与组件类型匹配 + variability 检查 + 内建属性定型
typeClassSections(cls, ctx)   // 趟 3：方程 / 算法段
```

- **趟 1 `typeComponents`**（NFTyping.mo:140）→ 逐组件 `typeComponent`（:433）。对 `Type.UNTYPED(dimensions)` 的组件：
  1. `typeDimensions`（:588）→ `typeDimension`（:600）：维度表达式 `typeExp`，要求 variability ≤ PARAMETER（否则 `DIMENSION_NOT_KNOWN`，:654–656），然后**当场 `Ceval.tryEvalExpResizable` 求值**（:650–652）。`Real x[:]` 这种未知维度从绑定/`start` 推导（:692–759），并 `Structural.markExp` + `Ceval.evalExp`（:748–755）。
  2. `typeClassType`（:242）取得元素类型；`Type.liftArrayLeftList(ty, dims)` 把维度加在左边（:465）。短类定义 `type T = Real[3]` 的维度同样左加（:295–303）。
  3. 条件组件：`typeComponentCondition(c.condition, context, evaluate = true)`（:467–469）——必须是 Boolean（`IF_CONDITION_TYPE_ERROR`）、variability ≤ PARAMETER（`COMPONENT_CONDITION_VARIABILITY`，:1231–1235），然后 `Ceval.evalExp` 求值（:1242）。求值为 false → `is_deleted`，**不再给子组件定型**（:479）。
  4. 递归 `typeComponents(c.classInst)`（:484）。
- **趟 2 `typeBindings`**（:950）→ `typeComponentBinding`（:994）：被删除（条件为 false）或无效的组件直接跳过（:1017–1019）。对 `UNTYPED_BINDING`：`typeBinding`（:1168，就是对绑定表达式调 `typeExp`，结果装进 `TYPED_BINDING(exp, ty, var, purity, …)`）→ `TypeCheck.matchBinding`（:1033）→ `checkComponentBindingVariability`（:1036，见 §2）。内建属性（`TYPE_ATTRIBUTE` 组件，即 `start`/`min`…）走 `typeTypeAttribute`（:1110–1112，:1256，见 §5）。
- **趟 3 `typeClassSections`**（:2727）：基本类型的类跳过（:2740）；`Sections.map` 给四类段各自的 context（EQUATION / ALGORITHM，initial 段多加 INITIAL 位，:2746–2755）；再对每个组件递归 `typeComponentSections`（:2769–2771）。方程分派在 `typeEquation`（:3040）：等式 `typeEqualityEquation`（:3493）、`for` 的范围必须是 parameter 表达式（`typeIterator(..., structural = true)`，:3069，:562–565 报 `NON_PARAMETER_ITERATOR_RANGE`）、`if` 走 `typeIfEquation`（:3562，§6）。

依赖关系：趟 1 里求维度时可能需要还没定型的别的组件的绑定——`typeCref2` 对 cref 指向的组件**按需**调 `typeComponent`（:2041），`typeComponentBinding` 的第三个 case（:1087–1102）处理"组件还没定型但绑定要先定型"的情况。所以三趟不是严格分层，而是"三趟 + 按需递归 + 状态位（`ComponentState.Typed/TypeChecked`）防重入"；循环维度靠 `Dimension.UNTYPED(isProcessing = true)` 检出（:622–634）。

#### 1.2 类型表示（NFType.mo:68–144）

`INTEGER | REAL | STRING | BOOLEAN | CLOCK | ENUMERATION(typePath, literals) | ARRAY(elementType, list<Dimension>) | TUPLE | NORETCALL | UNKNOWN | COMPLEX(cls, complexTy) | FUNCTION | METABOXED | POLYMORPHIC | ANY | CONDITIONAL_ARRAY(trueType, falseType, matchedBranch) | UNTYPED(typeNode, dimensions)`。

- `ARRAY` 是**扁平**的：一个元素类型 + 维度列表，不嵌套（`liftArrayLeft`，NFType.mo:151–162）。
- `Dimension`（NFDimension.mo:57–93）：`RAW_DIM / UNTYPED(exp, isProcessing) / INTEGER(size, var) / BOOLEAN / ENUM(enumType) / EXP(exp, var) / RESIZABLE / UNKNOWN`。
- `COMPLEX` 的 `complexTy`：`CONNECTOR(pots, flows, streams)` / `EXPANDABLE_CONNECTOR` / `RECORD(constructor, fields, indexMap)` / `EXTERNAL_OBJECT` / `EXTENDS_TYPE`（NFTyping.mo:257–280）。
- `Type.isDiscrete`：INTEGER/STRING/BOOLEAN/ENUMERATION 及其数组（NFType.mo:1363–1377）——§2 的隐式离散靠它。

#### 1.3 绑定与声明类型的匹配：Integer→Real 怎么体现

入口 `TypeCheck.matchBinding`（NFTypeCheck.mo:2953）→ `matchTypes(actual, expected, exp, ALLOW_UNKNOWN)`（:1611）。类型构造子不同时进 `matchTypes_cast`（:2538）：**只有 `(INTEGER, REAL)` 一条标准隐式转换**（:2549–2553），调 `Expression.typeCast(exp, Real)`，MatchKind = CAST。（另有两条非标准 enum↔Integer，需 `--allowNonStandardModelica`，:2556–2569；Boolean→Real 只在 `-d=nfAPI` 下，仅 `matchExpressions_cast` 有，:1755。）

`Expression.typeCast`（NFExpression.mo:1180–1269）**尽量把转换下推到字面量，推不下去才包 CAST 节点**：

| 被转换的表达式 | 结果 | 锚点 |
|---|---|---|
| `INTEGER(v)` | 直接改成 `REAL(intReal(v))` | :1201–1202 |
| `ARRAY` | 逐元素 typeCast | :1223–1228 |
| `RANGE` | start/step/stop 各自 typeCast | :1230–1234 |
| `UNARY(-, e)` | 转操作数：`-(cast e)` | :1237–1241 |
| `IF` | 两个分支各自 typeCast，条件不动 | :1244–1251 |
| `CALL` | `Call.typeCast`（有特例） | :1254–1255 |
| `CAST(e)` | 覆盖旧 cast | :1258 |
| 其它（CREF、BINARY…） | `CAST(Real, e)` 整体包一层（`typeCastGeneric`，:1272–1281） | :1268 |

压平文本里 CAST 打印成 `/*Real*/(e)`（DAE 路径的打印）。注意错误消息用的 `Expression.toString` 在 `-d=nfAPI`（默认 **开**，Flags.mo:510）下**不打印 CAST**，只打印内层表达式，否则写作 `CAST(Real, e)`（NFExpression.mo:2267–2270）。探针 T01（1.25.0 实跑）：

```
Real a = 1;                      →  Real a = 1.0;
Real b = i;          (Integer i) →  Real b = /*Real*/(i);
Real d = k; (constant Integer 5) →  Real d = 5.0;            // 先 CAST(k)，§3 代入后 §4 simplifyCast 折叠
Real e = i + 1;                  →  Real e = /*Real*/(i + 1); // 整个 Integer 二元式包一层，不下推
Real f = i * 2.5;                →  Real f = /*Real*/(i) * 2.5;
Real g = i / 2;                  →  Real g = /*Real*/(i) / 2.0;
Real h = 7 / 2;                  →  Real h = 3.5;
Real m = n ^ 2;                  →  Real m = /*Real*/(n) ^ 2.0;
Real o = -i;                     →  Real o = -/*Real*/(i);
Real p = if a > 0 then i else n; →  Real p = if a > 0.0 then /*Real*/(i) else /*Real*/(n);
Real r[2] = {i, 2.0};            →  r = {/*Real*/(i), 2.0};
Real s = abs(i);                 →  Real s = /*Real*/(abs(i));  // abs 选了 Integer 重载，结果再 cast
Real t = sin(i);                 →  Real t = sin(/*Real*/(i));  // 实参匹配 Real 形参时 cast
Real u = max(i, n);              →  Real u = /*Real*/(max(i, n));
```

#### 1.4 二元/一元/逻辑/关系运算的类型推导（NFTypeCheck.mo）

`typeExp` 的 `BINARY` 分支（NFTyping.mo:1370–1377）：两边各自 `typeExp`（context 加 SUBEXPRESSION 位），再 `TypeCheck.checkBinaryOperation`（NFTypeCheck.mo:164）；结果 variability = `variabilityMax(var1, var2)`。

- **运算符重载入口**：任一操作数的元素类型是 `COMPLEX` → `checkOverloadedBinaryOperator`（:218；判定在 :180–182）。ymc 只做内建类型时，这里是唯一需要留桩的地方。
- `+`（:960）：`matchExpressions` 求公共类型，允许 Integer/Real/**String**；`-`（:990）只允许 Integer/Real。
- `*`（:1019）：标量×标量同 `+`；数组情形按 Op 细分（`MUL_SCALAR_ARRAY`、`SCALAR_PRODUCT`、`MATRIX_PRODUCT`…）。
- `/`（:1099）：**两边各自匹配到 Real**（:1117–1120），所以 `Integer / Integer` 结果是 Real，且两边的 Integer 字面量被改成 Real 字面量（`7 / 2` → `7.0 / 2.0` → 折叠 `3.5`）。
- `^`（:1156）：底数匹配到 Real；标量时指数也匹配到 Real（:1183），所以 `n ^ 2` → `/*Real*/(n) ^ 2.0`。
- `matchExpressions`（:1527）在两边类型构造子不同时走 `matchExpressions_cast`（:1713）：`(INTEGER, REAL)` 转左边（:1726–1730），`(REAL, INTEGER)` 转右边（:1746–1750）。
- 一元 `-`（:1308）：类型不变，要求 `Type.isNumeric`；一元 `+` **直接丢掉**（:1330）。
- `and/or`（:1397）、`not`（:1429）：要求 Boolean（元素）。
- 关系运算（:1454）：`matchExpressions` 后允许 Integer/Real/String/Boolean/Enumeration，结果 `BOOLEAN`；对 Real 的 `==`/`<>` 在模型里给 `WARNING_RELATION_ON_REAL`（`addStrictMessage`，:1489–1495）。关系式若含 continuous 操作数且不在 `noEvent` 里，variability 记为 DISCRETE（NFTyping.mo:1413–1417）。
- 所有这些失败时统一报 `UNRESOLVABLE_TYPE`（`printUnresolvableTypeError`，:1510–1525，§7）。
- if 表达式（NFTyping.mo:2674）：条件匹配 Boolean；两分支 `matchIfBranches` 求公共类型（Integer/Real 混合时 Integer 分支被 cast）；不兼容报 `TYPE_MISMATCH_IF_EXP`。
- 等式方程（NFTyping.mo:3493）：`matchExpressions(rhs, lhs)`，不兼容报 `EQUATION_TYPE_MISMATCH_ERROR`。

探针 T02：`Real y = x + i` → `x + /*Real*/(i)`；`Boolean b = x > i` → `x > /*Real*/(i)`；`String s = "a" + "b"` → `"ab"`；`String(i)` → `String(i, 0, true)`（默认实参被补齐打印）；`E.one == E.three` → `false`；`Integer(e1)` 保留。

### 2. variability 规则

#### 2.1 variability 的七档与来源

枚举 `Variability`（NFPrefixes.mo:285–292），从低到高**七档**（比较运算就用这个顺序）：

```
CONSTANT < STRUCTURAL_PARAMETER < PARAMETER < NON_STRUCTURAL_PARAMETER < DISCRETE < IMPLICITLY_DISCRETE < CONTINUOUS
```

- **组件的初始 variability 只来自前缀**：`variabilityFromSCode`（NFPrefixes.mo:411–419）——`constant`→CONSTANT，`parameter`→PARAMETER，`discrete`→DISCRETE，无前缀→CONTINUOUS。**类型不参与**：`STRUCTURAL_PARAMETER`/`NON_STRUCTURAL_PARAMETER` 都不是源级档位，是 `updateImplicitVariability` 事后改的（§2.3）；`IMPLICITLY_DISCRETE` 是 when 左值事后改的（§2.4）。
- **"Integer/Boolean/String 默认 discrete" 在 NF 里不成立**（探针 V15_default_discrete）：`Integer i = integer(x);`（`x` 连续、`integer(x)` 连续）**不报**高 variability 错——说明无前缀的 `Integer i` variability 是 **CONTINUOUS** 而非 DISCRETE。若它默认 discrete，绑定连续表达式会触发 `HIGHER_VARIABILITY_BINDING`。离散"值"的性质由**类型**单独刻画（`Type.isDiscrete`：INTEGER/STRING/BOOLEAN/ENUMERATION 及其数组，NFType.mo:1363–1377），与 variability 档位是两回事。

#### 2.2 表达式的 variability 计算

`typeExp` 逐节点算，取操作数上确界为主：

| 表达式 | variability | 锚点 |
|---|---|---|
| 字面量（INTEGER/REAL/BOOLEAN/STRING/枚举字面量） | CONSTANT | NFTyping typeExp 常量分支 |
| `cref` | 该组件当前的 variability（`ComponentRef.nodeVariability`） | NFTyping.mo:2041 附近 |
| `BINARY(e1,op,e2)` / 逻辑 / 一元 | `variabilityMax(var1, var2)` | NFTyping.mo:1370–1377 |
| 关系式（含 continuous 操作数且不在 `noEvent` 内） | **DISCRETE**（不是 continuous） | NFTyping.mo:1413–1417 |
| 函数调用 | 见形参/实参 variability 与函数纯度 | NFCall / NFBuiltinCall |

关系式降为 DISCRETE 这条：`Boolean b = x > 0.5;`（`x` 连续）里 `b` 只在事件时刻变，所以 `x>0.5` 记 DISCRETE 而非 continuous——这就是为什么把它绑到无前缀 Boolean（continuous 档）不报错（DISCRETE < CONTINUOUS，方向合法）。

#### 2.3 绑定 variability 高于声明 → 报错

趟 2 `typeComponentBinding` 里，绑定定型后调 `checkComponentBindingVariability`（NFTyping.mo:1036 附近）。若**绑定表达式的 variability 严格高于组件声明的 variability**，报 `HIGHER_VARIABILITY_BINDING`（Error.mo:339–340，id 130）：

```
Component %s of variability %s has binding %s of higher variability %s.
```

四个 `%s` = 组件名 / 声明档位串 / 绑定表达式（打引号）/ 绑定档位串；档位串由 `Variability` → 字面量映射（NFPrefixes.mo:467–470：CONSTANT→"constant"，三种 parameter 全打 "parameter"，DISCRETE→"discrete"，CONTINUOUS→"continuous"）。实测：

| 探针 | 声明 | 绑定 | omc 原文 |
|---|---|---|---|
| V01 | `parameter Real p` | `= x`（x 连续） | `Component p of variability parameter has binding 'x' of higher variability continuous.` |
| V02 | `constant Real c` | `= p`（p 参数） | `Component c of variability constant has binding 'p' of higher variability parameter.` |
| V11 | `discrete Real d` | `= x`（x 连续） | `Component d of variability discrete has binding 'x' of higher variability continuous.` |
| V05 | `Real y(start = x)` | start 是 parameter 档，`x` 连续 | `Component start of variability parameter has binding 'x' of higher variability continuous.` |

内建属性（`start`/`min`…）默认档位是 **parameter**，所以 V05 把连续量塞进 `start` 与 V01 同错。注意 V03 `Integer i = x > 0.5;` 报的是**类型**错（Boolean≠Integer，§7 `VARIABLE_BINDING_TYPE_MISMATCH`），不是 variability 错——类型检查在 variability 检查之前先拦下。

#### 2.4 `updateImplicitVariability`：谁被悄悄标成 structural / non-structural

在**定型之前**跑（NFInst.mo:198，`updateImplicitVariability(inst_cls, Flags.isSet(Flags.EVAL_PARAM), context)`；`EVAL_PARAM`=`-d=evaluateAllParameters` 默认 **false**，Flags.mo:220）。函数 NFInst.mo:3885，逐组件 `updateImplicitVariabilityComp`（:3928）做六件事：

1. **Evaluate=false 的 parameter** → 降为 `NON_STRUCTURAL_PARAMETER`（:3951–3957），从而躲开求值（§3）。
2. 否则判 `Structural.isStructuralComponent`（NFStructural.mo:55–112），真则 `markComponent`（:3960–3961）。`isStructuralComponent` 只对 **PARAMETER** 档且**（自身或某个父组件带 `Evaluate=true`）**返回真，且要求：`fixed`（非 `fixed=false`）、非 external object、有绑定（或 start）、且不依赖非 fixed 参数（`isBindingNotFixed`，深度≤4，:114–228）。→ 探针 E06/V13。
3. **用于数组维度的参数** → `markDimension`→`markExp`（:3967, NFStructural.mo:230–295）。→ E03/E16/V10/V12。
4. **决定某绑定数组尺寸的参数**（如 `fill(w,n)` 里的 `n`）→ `markExpSize`（:3972）。
5. **用于条件组件条件（`Real x if b;` 的 `b`）的参数** → `markExp`（:3977）。→ E04/E13。
6. **`fixed`/`stateSelect` 属性修饰里出现的参数** → `markExp`（TYPE_ATTRIBUTE 分支，:3987–3994）。

另有两处不在此函数、但同样产生 structural：**for 方程范围**（`typeIterator(..., structural=true)`，NFTyping.mo:2460、562–565，非参数报 `NON_PARAMETER_ITERATOR_RANGE`）；**if 方程条件**——若某分支条件是 parameter 且到此为止所有条件都是 parameter（或分支里含 `connect`），在 `typeIfEquation` 里被提升为 STRUCTURAL_PARAMETER（NFTyping.mo:3581–3586，§6）。`updateImplicitVariabilityEq` 的 IF 分支（NFInst.mo:4040–4051）**只递归分支体、不标条件**；when 左值则被标 IMPLICITLY_DISCRETE（`markImplicitWhenExp`，NFInst.mo:4144–4171）。

**关键副作用：`markComponent` 同时把组件设成 `final`**（NFStructural.mo:321–322：`setVariability(STRUCTURAL_PARAMETER)` + `setFinal(true)`），且 `NFConvertDAE` 把 STRUCTURAL_PARAMETER 一律当 final 打印（NFConvertDAE.mo:238–239）。这就是为什么压平输出里被求值的参数显示成 **`final parameter`**（探针 V10 的 `n`/`m`、E03/E16 的维度参数、V12 的 `a.n`、V13 的 `a1.k`）——`final` 不是用户写的，是"已被求值、值不可再改"的标记。`EVAL_PARAM` 打开时**所有** parameter 都变 structural，于是全部 `final` 且全部折叠（探针 E02.evalAll：`p/r/m` 全成字面量、`y=35.0`）。

### 3. 什么会被求值并代入压平结果

分两步、两个 pass：**先 `EvalConstants.evaluate`（NFInst.mo:206）把 cref 替换成值，再 `SimplifyModel.simplify`（:214）折叠**（§4）。§3 只讲"哪些 cref 被替换成值"。

#### 3.1 判定规则（NFEvalConstants.mo）

替换发生在 `evaluateExpTraverser` 的 CREF 分支（NFEvalConstants.mo:200–224）。设 `var = ComponentRef.nodeVariability(cref)`：

```
if var <= STRUCTURAL_PARAMETER and not isExternalObject(ty):
    if var > CONSTANT:            // 即 STRUCTURAL_PARAMETER
        try Ceval.evalCref(...) 在 checkpoint 里；失败则回滚、cref 原样保留（best-effort）
    else:                         // 即 CONSTANT
        Ceval.evalCref(...) 硬求值；失败即报错
// var >= PARAMETER 的 cref：完全不动
```

据此的**三分**：

| 组件类别（nodeVariability） | 在表达式里是否被替换成值 | 证据 |
|---|---|---|
| **CONSTANT** | **总是**（硬求值；无值/循环即报错） | E01（`PI`→3.14、`TWO_PI`→6.28 代入方程）、V14（`c[idx]`→2.0） |
| **STRUCTURAL_PARAMETER**（§2.4 被标的） | 尽力替换（可求值就换成字面量，否则保留符号） | E03/E16 维度参数、V10 `n/m`→2、V13 `a1.k`→1.0、E06 `a`→2.0 |
| **PARAMETER / NON_STRUCTURAL_PARAMETER / 更高** | **不替换**，保留 cref 符号 | E02（`p`/`r`/`m`/`n` 在方程里保留）、E06 `c/d/h`、E07 `a/b/c` |

对**声明行的绑定**，走 `evaluateVariable`→`evaluateBinding`（:96–155）：只有当组件本身是 `structural`（`var <= STRUCTURAL_PARAMETER`）时，绑定右侧才被求值+化简+（可能）Ceval；否则绑定右侧只做浅层 `evaluateExp`（把里面引用到的 constant/structural 换掉，但不动本变量档位）。所以：
- constant 声明行的绑定**显示为求值后的字面量**（E01：`constant Real TWO_PI = 6.28;`、`c3 = 16.09;`）。
- 普通 parameter 声明行的绑定**保留符号形式**，只把其中的 constant/structural 引用换掉（E02：`parameter Real p = 2.0 * q;` 里 `q` 是普通 param 保留；E07：`parameter Real c = a + 7.0;` 里 `k` 是 constant 被换成 7.0，`a` 是普通 param 保留）。
- structural parameter 声明行**显示为字面量**（V10：`final parameter Integer m = 2;`、E16：`final parameter Integer c = 6;`）。

#### 3.2 属性绑定的求值（`evaluateTypeAttribute`，NFEvalConstants.mo:157–173）

每条属性 `(name, binding)` 也过 `evaluateBinding`，但 `structural := name=="fixed" or name=="stateSelect"`——即 **`fixed`/`stateSelect` 当 structural（硬求成字面量），其余属性（`start`/`min`/`max`/`nominal`…）按 PARAMETER 档处理**（其中的 constant/structural 被代入，本身不强制求值）。探针：E01 `z(start = PI)`→`z(start = 3.14)`（PI 是 constant，代入）；E07 `x(final start = a)`→`x(start = a)`（`a` 普通 param，保留符号；注意属性上的 `final` 前缀在输出里被丢弃）；V12 `q(start = 2.5)` 原样。

#### 3.3 相关 flag 默认值

| flag | 默认 | 作用 |
|---|---|---|
| `-d=evaluateAllParameters`（`EVAL_PARAM`, Flags.mo:220） | **false** | 打开则**所有** parameter 变 structural → 全部求值折叠（探针 E02.evalAll） |
| `--evaluateStructuralParameters`（`EVALUATE_STRUCTURAL_PARAMETERS`, Flags.mo:1381） | **`all`** | `strictlyNecessary` 则 if 方程条件等"非必须"的结构参数不在前端求值（flattenIfEquation 的 `minimalEval` 支，NFFlatten.mo:1980） |
| `-d=nfScalarize`（`NF_SCALARIZE`, Flags.mo:504） | **true** | 影响 if 方程/维度是否在前端展开与求值 |

#### 3.4 编译期函数求值与数组下标

- **Modelica 函数调用**若实参可求值且组件档位允许，被 Ceval 展开成值：E17 `constant Real c = E17P.f(2);`→`c = 5.0;`、`parameter Real p = E17P.f(3);`→`p = 7.0;`；但 `parameter Real r = E17P.f(q);`（`q` 普通 param）**保留** `E17P.f(q)`；`Real x = E17P.f(4);`（x 连续，但实参是字面量）→`x = 9.0;`（走 §4 化简的内建/常量折叠，见 S02 f33 等），`Real y = E17P.f(x);`（x 变量）保留。
- **常量数组的常量下标**被求值：V14 `c[idx]`（idx 常量 2）→`2.0`、`c[pi]`（pi 结构参数 1）→`1.0`；但 `c[vi]`（vi 连续 Integer）→`{1.0,2.0,3.0}[vi]`（数组被代入成字面量数组，下标保留）。E17 `E17P.tbl[2]`→`20.0`、`tbl[vi]`→`{10.0,20.0,30.0}[vi]`。

#### 3.5 错误与边界

| 情形 | 结果 | 探针 / 锚点 |
|---|---|---|
| constant 无绑定，但只在**声明**里"用到自己"（`constant Real a; Real x = a;`） | **不报错**，`a` 原样保留 | E10（惊讶点：单纯引用未求值 constant 不触发） |
| constant 无绑定，被别的 constant 绑定或方程**用到需要其值** | 报 `UNBOUND_CONSTANT`（id 321）："Constant a is used without having been given a value."（附 "From here:" 定位到声明） | E10b |
| constant 无绑定用作维度 | 报 `STRUCTURAL_PARAMETER_OR_CONSTANT_WITH_NO_BINDING`（id 5033） | E10c |
| constant 循环依赖 | `CIRCULAR_PARAM`（id 125）："Variable 'b' has a cyclic dependency and has variability constant." | E09 |
| parameter 循环依赖（非 structural） | **不报错**，两行都保留符号（不求值就无所谓环） | E11 |
| structural/无绑定参数用作维度且无 start | `Could not evaluate structural parameter (or constant): n ... Array dimensions must be known at compile time.`（id 5033 文案变体） | E12 |
| parameter 无绑定、有 `start` | 用 start 当值，出 `UNBOUND_PARAMETER_WITH_START_VALUE_WARNING`（id 499） | E08/E13/V12 |
| parameter 无绑定、无 start、`fixed=true` | 报 `UNBOUND_PARAMETER_ERROR`（id 500）："Parameter p has neither value nor start value, and is fixed during initialization (fixed=true)." | E08b |
| structural/constant 求值时除零 | `DIVISION_BY_ZERO`（id 15）："Division by zero in 1.0 / 0.0" | E18（constant）/E19（维度 `div(4,0)`） |
| constant 表达式里的 `1/0` 但**该分支被常量 if 剪掉** | 不报（未被求值） | E20 `y = if useA then 1/0 else b` → `y = b`（useA=false） |

### 4. 化简规则（NFSimplifyModel / NFSimplifyExp）

`SimplifyModel.simplify`（NFInst.mo:214，`--noSimplify` 可关）遍历模型每个表达式调 `SimplifyExp.simplify`（NFSimplifyExp.mo:83–139）。**化简是纯局部、自底向上、单趟**：先化简子节点，再按顶层算子分派。**不做**跨整棵和/积的常量收集与重结合（见 §4.4）。

#### 4.1 字面量折叠（两操作数都是字面量 → 交给 Ceval）

`simplifyBinaryOp`（:971–1002）第一分支：`if isLiteral(exp1) and isLiteral(exp2) then Ceval.evalBinaryOp(...)`。关系式（`simplifyRelation`:1291–1305）、逻辑一元（`simplifyLogicUnary`:1270–1289）同理走 Ceval。探针 S01：`1+2`→`3.0`、`7/2`→`3.5`、`2^3`→`8.0`、`1-3`→`-2.0`、`-(2+3)`→`-5.0`、`10/4*2`→`5.0`、整数 `7*6-2`→`40`、`-(-3)`→`3`、整型溢出 `2147483647+1`→`2147483648`（**不检查溢出**）。**`1.0/0.0` 不折叠、原样保留**（S01 a14；除零只在"必须求值"的 constant/structural 语境报错，§3.5），浮点保持 IEEE 双精度（`0.1+0.2`→`0.30000000000000004`）。

#### 4.2 代数恒等式（非字面量，`simplifyBinaryAdd/Sub/Mul/Div/Pow`，:1005–1160）

| 规则 | 锚点 | 探针（S01） |
|---|---|---|
| `0+e=e`, `e+0=e` | :1011–1016 | b03/b04 → `x` |
| `0-e=-e`, `e-0=e`, `e-e=0` | :1039–1048 | b08→`x`, b09→`-x`, b13→`0.0` |
| `0*e=0`, `e*0=0`, `1*e=e`, `e*1=e`（仅字面量 0/1） | :1065–1085 | b01/b02→`x`, b05/b06→`0.0` |
| `e/1=e`, `e/(-1)=-e`, `0/e=0`（仅当 e≠0 可判定） | :1096–1110 | b10→`x`, b24→`-x`, b25 `0/x`→`0.0/x`（x 不可判非零，不化简）, b32 `x/0`→`x/0.0`（不折叠） |
| `e^0=1`, `e^1=e` | :1151–1158 | b11→`x`, b12→`1.0` |
| `-(-e)=e`（一元双负） | :1146–1147 | b07→`x` |
| 负号重组：`(-e1)+e2=e2-e1`、`e1+(-e2)=e1-e2`、`(-e1)+(-e2)=-(e1+e2)` 等 | :1017–1028, :1051–1060 | b23 `(-x)+(-x)`→`-(x + x)` |

#### 4.3 逻辑短路、关系、if、cast、内建

- **逻辑短路**（`simplifyLogicBinaryAnd/Or`，:1203–1268，不依赖字面另一侧）：`false and e=false`、`true and e=e`、`true or e=true`、`false or e=e`。S03：l01 `true and b`→`b`、l03 `false and b`→`false`、l05 `true or b`→`true`、l07 `not true`→`false`、l08 `not(not b)`→`b`；**不化简** `b and b`（l09 原样）、`b or not b`（l10 原样，不做矛盾律/排中律）。
- **关系折叠**（两侧字面量，:1301–1303）：S03 r01 `1<2`→`true`、r02 `2.5>=3`→`false`、r04 `"a"<"b"`→`true`、r05 字符串 `==`→`true`、r06 `true>false`→`true`、r12 `1<>2`→`true`；**枚举比较**：r07 `E.one<E.two`→`true`、r08（constant `ce==E.two`，先 §3 代入再折叠）→`true`；`r09`（`pe` 是 parameter）→保留 `pe == E.one`；r10 `x>1+2`→`x>3.0`（只折叠右子式）；r11 `x<x`→保留（不化简自比较）。
- **if 表达式**（`simplifyIf`，:1307–1345）：条件字面量 → 选中分支（S03 i01 `if true`→`x`，i02→`2.0*x`，i03 `if 1<2`→`1.0`）；两分支相等 → 取其一（i04 `if b then x else x`→`x`）；两分支是 Boolean 字面量 → `cond`/`not cond`（i05→`b`，i06→`not b`）；`elseif` 里常量条件被剪（i11 `elseif false`→只剩 `if b then 1.0 else 3.0`，i12 `elseif true`→`if b then 1.0 else 2.0`）；嵌套常量 if 被拍平（i08→`if x>0.0 then 1.0 else 3.0`）。
- **cast 折叠**（`simplifyCast`，:1347+）：`Real(INTEGER v)`→`REAL`、数组逐元素、`Real(Real)` 去壳。这解释 §1.3 里 `Real d = k`（k=constant 5）代入后 `/*Real*/(5)`→`5.0`。
- **内建函数常量折叠**（`simplifyCall`→`simplifyBuiltinCall`，:172–288，及经 §4.1 字面量实参走 Ceval）——探针 S02：`sin(0)`→`0.0`、`cos(0)`→`1.0`、`exp(1)`→`2.718…`、`sqrt(2)`→`1.414…`、`abs(-2.5)`→`2.5`、`abs(-3)`→`3`、`max(1,2.5)`→`2.5`、`min(3,4)`→`3`、`div(7,2)`→`3`、`mod(-7,2)`→`1`、`rem(-7,2)`→`-1`、`mod(5.5,2)`→`1.5`、`integer(3.7)`→`3`、`integer(-3.7)`→`-4`、`floor/ceil(2.5)`→`2.0/3.0`、`sign(-2.0)`→`-1`、`atan2(1,1)`→`0.785…`、`tanh(0.5)`→`0.462…`、`String(3)`→`"3"`、`String(2.5)`→`"2.5"`、`String(true)`→`"true"`、`String(E.two)`→`"two"`、`Integer(E.three)`→`3`、`sum({1,2,3})`→`6.0`、`product({2,3})`→`6.0`、`size({1,2,3},1)`→`3`、`max({1,5,3})`→`5.0`、`homotopy(x, 2*3)`→`homotopy(x, 6.0)`（第一实参保留、第二折叠）、`smooth(1, 2*3)`→`6.0`、`noEvent(1+2)`→`3.0`。**不折叠**：定义域外 `sqrt(-1)`→`sqrt(-1.0)`、`log(0)`→`log(0.0)`（保留，仿真期才报）；变量实参 `max(x,2)`→`max(x,2.0)`、`abs(x)`、`integer(x)`、`der(x)`、`initial()` 保留。`sin(x)+sin(0.0)*x`→`sin(x)`（`sin(0)=0` 折叠后 `0*x=0`、`sin(x)+0=sin(x)`，多规则级联）。

#### 4.4 明确"不化简"的（ymc 别自作聪明）

- `x+x` **不**变 `2*x`（S01 b14 原样）；`x*x` **不**变 `x^2`（b27）；`x/x` **不**变 `1`（b26，x 可能为 0）。
- **不跨树收集常量**：`2*x*3`（=`(2*x)*3`）→`2.0*x*3.0`（**不**成 `6*x`，b15）；`x*2*3`→`x*2.0*3.0`（b17）；`x+1+2`→`x+1.0+2.0`（b18）。只有当常量**相邻先结合**才折叠：`2*3*x`→`6.0*x`（b16）、`1+2+x`→`3.0+x`（b19）、`x+(1+2)`→`x+3.0`（b20）。这是左结合 + 局部化简的直接后果：`ymc` 若想做 `6*x` 需要额外的 flatten-multary + 常量收集趟（OMC 有 `simplifyMultary`/`ExpandExp`，但仅在 `-d=nfExpandOperations` 等路径启用，默认不走）。
- 关系自比较 `x<x`、逻辑 `b and b`/`b or not b` 不化简。

### 5. 内建属性的类型与默认值

#### 5.1 各内建类型的属性清单与类型

内建标量类型不是普通 Modelica 类，而是编译期硬编码的 `PARTIAL_BUILTIN`，其属性作为 `TYPE_ATTRIBUTE` 组件挂在类树里（NFBuiltin.mo）。**每种类型的属性名、类型、下标**：

| 类型 | 属性（`名:类型`，NFBuiltin.mo 锚点） |
|---|---|
| **Real**（:142–204） | `quantity:String`, `unit:String`, `displayUnit:String`, `min:Real`, `max:Real`, `start:Real`, `fixed:Boolean`, `nominal:Real`, `unbounded:Boolean`, `stateSelect:StateSelect`, `uncertainty:Uncertainty` |
| **Integer**（:218–247） | `quantity:String`, `min:Integer`, `max:Integer`, `start:Integer`, `fixed:Boolean` |
| **Boolean**（:262–279） | `quantity:String`, `start:Boolean`, `fixed:Boolean` |
| **String**（:296–317） | `quantity:String`, `start:String`, `fixed:Boolean` |
| **enumeration**（:320+，由 `ClassTree.fromEnumeration` 生成） | `quantity:String`, `min:E`, `max:E`, `start:E`, `fixed:Boolean` |

`StateSelect = enumeration(never, avoid, default, prefer, always)`（NFModelicaBuiltin.mo:36–42）；`Uncertainty = enumeration(given, sought, refine, propagate)`（:44–49）。

#### 5.2 默认值

NFBuiltin 里这些 `TYPE_ATTRIBUTE` 都是 `Modifier.NOMOD()`——**内建层不带默认绑定**，默认值来自 MLS 语义（MLS 3.6 §4.8.x / §3.7.2）：`start=0`（Real 0.0 / Integer 0 / Boolean false / String ""）、`fixed`=**true**（parameter/constant）或 false（其它）、`min=-∞`、`max=+∞`、`nominal` 无默认（未指定）、`unbounded=false`、`stateSelect=StateSelect.default`、`quantity=unit=displayUnit=""`。`fixed=true` 的默认在探针里以警告形式外显：E08/E13/V12 "…is fixed during initialization (fixed=true), using available start value…"。

#### 5.3 属性修饰的类型检查与 variability 要求

属性修饰按普通组件绑定定型（趟 2 `typeTypeAttribute`，NFTyping.mo:1110–1112、1256）：
- **类型**要匹配属性声明类型（`start` 是 Real → 塞 Boolean 报 §7 类型错）。
- **variability**：属性组件默认 **parameter** 档，所以 `start=<连续量>` 触发 `HIGHER_VARIABILITY_BINDING`（探针 V05：`Real y(start = x)` 报 "Component start of variability parameter has binding 'x' of higher variability continuous."）。

#### 5.4 属性在压平输出里的呈现

- **只有被显式修饰的属性才打印**，默认值不打印（V12 `a.q(start = 2.5)`；未设 `start` 的变量不带括号）。
- 属性绑定同样过 §3 求值 + §4 化简：constant/structural 引用被代入、字面量折叠（E01 `z(start = PI)`→`z(start = 3.14)`）；普通 parameter 引用保留符号（E07 `x(final start = a)`→`x(start = a)`）。
- **属性上的 `final` 前缀在输出里被丢弃**（E07：`final start = a` 打成 `start = a`）。
- `fixed`/`stateSelect` 被当 structural 硬求值成字面量（§3.2，NFEvalConstants.mo:167）。

### 6. if 方程 / 条件组件的编译期裁剪

#### 6.1 if 方程：条件是否 structural 决定是否裁剪

两步：**typing 期给条件定 variability**（`typeIfEquation`，NFTyping.mo:3562–3613），**flatten 期真正裁剪**（`flattenIfEquation`，NFFlatten.mo:1940–2070）。

- `typeIfEquation`（:3581–3586）：某分支条件是 **PARAMETER** 且**到此为止所有条件都 ≤ PARAMETER**（或该分支含 `connect`），就把它**提升为 STRUCTURAL_PARAMETER**；条件 variability > PARAMETER（连续/离散/或依赖非 fixed 参数）则标 `NONEXPANDABLE`，整支保留。
- `flattenIfEquation`（:1965–2070）：只有 `var <= STRUCTURAL_PARAMETER` 的条件才可能被求值。默认（`scalarize=true`、非新后端、`evaluateStructuralParameters=all`）走 `should_eval=true` 支（:1997–2000）→ `Ceval.tryEvalExp(cond)`；求值为 **true** → 选中该支、丢弃其余分支（:2022–2033）；求值为 **false** 的分支直接**丢弃**（:2035–2049）。`--evaluateStructuralParameters=strictlyNecessary`（`minimalEval`）则不求值，整个 if 方程保留（:1981–1984）。

探针：
- E05：`if flag then x=p else x=2*p`（flag=true param→structural）→ 压平只剩 `x = p;`；`if mode==1 … elseif mode==2 … else`（mode=2）→ 只剩 `y = 2.0;`；if 表达式 `z = if flag then p else -p`→`z = p`。
- E14：条件 `flag=false`，两分支方程数不等（then 有 2 条、else 有 1 条）——**照样裁剪、不报错**：选中 else 支得 `x = 2.0*p; y = 1.0;`，`if not flag`（=true）得 `y = time;`。
- E15：`flag=true annotation(Evaluate=false)` → §2.4 降为 NON_STRUCTURAL_PARAMETER（> STRUCTURAL_PARAMETER）→ **不裁剪**，if 方程**原样保留**在压平输出里。
- E20：`constant Boolean useA=false` → `if useA then assert(false,...)` 整支删除（连带 `assert(a>0,...)`，因为 `a` 分支被剪？实际是 useA 分支删，`assert(a>0)` 独立且 a>0 折叠为 true 被 §4 删；最终只剩 `assert(b>0.0, "b must be positive")`）。

#### 6.2 分支方程数不等：前端不查

`DIFFERENT_NO_EQUATION_IF_BRANCHES`（Error.mo:141–142，id 31）**只在旧前端/后端**引用，NF 前端（`typeIfEquation`/`NFVerifyModel`）里没有这条检查。探针 E21_ifeq_unequal_continuous（`if time>1 then x=1;y=2; else x=3;`，连续条件、分支数不等）**压平不报错**，if 方程原样保留，等到后端做等式配平。→ ymc 若在前端就想拦，需要自己加；OMC 不在这一层拦。

#### 6.3 条件组件（`Real x if cond;`）

- 条件必须 ≤ PARAMETER（否则 `COMPONENT_CONDITION_VARIABILITY`，§7；探针 V07）；条件里的参数被标 structural（§2.4 第 5 条）并求值。
- 条件求值为 **false** → 组件在实例化期就 `is_deleted`，**根本不给子组件定型**（NFTyping.mo:467–479），压平输出里该组件及其方程**完全消失**。探针 E04：`Real b if other`（other=false）→ 输出无 `b`；`a`（use=true）、`c`（c2=true）保留。
- **对已删除条件组件的引用**：若在非 connect 语境里引用一个条件组件，出 `CONDITIONAL_COMPONENT_INVALID_CONTEXT` **警告**（Error.mo:791–792，id 357，`WARNING`）。探针 E04：`a=1;` 与 `c = if use then 3 else 4;` 各出一条 "Conditional component 'a'/'c' is used in a non-connect context." 但**方程仍保留**（因为 a、c 未被删，只是警告"你在非 connect 处用了条件件"）。真正被删的组件（如 `b`）若被引用则是硬错误（其符号已不存在）。

### 7. 常见类型错误文案

消息定义在 `Util/Error.mo`（`id` 为 `ErrorTypes.MESSAGE(id,…)` 首参；模板里 `%s` 按位替换）。前端触发点在 `NFTyping.mo` / `NFTypeCheck.mo` / `NFEvalConstants.mo` / `NFFlatten.mo`。下表 15 条，均配 omc 1.25.0 实跑原文。

| # | 消息名 (id) | 模板（Error.mo:行） | 触发点 / 探针 |
|---|---|---|---|
| 1 | `HIGHER_VARIABILITY_BINDING` (130) | `Component %s of variability %s has binding %s of higher variability %s.`（:339–340） | 绑定 variability > 声明；NFTyping.mo:1036。V01/V02/V11/V05 |
| 2 | `VARIABLE_BINDING_TYPE_MISMATCH` (135) | `Type mismatch in binding %s = %s, expected subtype of %s, got type %s.`（:349–350） | `matchBinding` 失败；NFTyping.mo:1033。V03 |
| 3 | `UNRESOLVABLE_TYPE` (84) | `Cannot resolve type of expression %s. The operands have types %s in component %s.`（:247–248） | 二元/一元/关系无公共类型；NFTypeCheck.mo:1510–1525 |
| 4 | `EQUATION_TYPE_MISMATCH_ERROR` (45) | `Type mismatch in equation %s of type %s.`（:169–170） | 等式两侧类型不匹配；NFTyping.mo:3493 |
| 5 | `TYPE_MISMATCH_IF_EXP` (83) | `Type mismatch in if-expression in component %s. True branch: %s has type %s, false branch: %s has type %s.`（:245–246） | if 表达式两分支不兼容；NFTyping.mo:2674 |
| 6 | `IF_CONDITION_TYPE_ERROR` (10) | `Type error in conditional '%s'. Expected Boolean, got %s.`（:99–100） | if 方程/组件条件非 Boolean；NFTyping typeCondition |
| 7 | `ARG_TYPE_MISMATCH` (269) | `Type mismatch for positional argument %s in %s(%s=%s). The argument has type:\n  %s\nexpected type:\n  %s`（:617–618） | 函数实参类型不符；NFCall 参数匹配 |
| 8 | `NAMED_ARG_TYPE_MISMATCH` (268) | `Type mismatch for named argument in %s(%s=%s)…`（:615–616） | 具名实参类型不符 |
| 9 | `DIMENSION_NOT_KNOWN` (23) | `Dimensions must be parameter or constant expression (in %s).`（:125–126） | 维度 variability > parameter；NFTyping.mo:654–656。V06 |
| 10 | `COMPONENT_CONDITION_VARIABILITY` (100) | `Component condition must be parameter or constant expression (in %s).`（:279–280） | `if b` 条件 variability > parameter；NFTyping.mo:1231–1235。V07 |
| 11 | `NON_PARAMETER_ITERATOR_RANGE` (109) | `The iteration range %s is not a constant or parameter expression.`（:297–298） | for 范围非参数；NFTyping.mo:562–565。V08 |
| 12 | `UNBOUND_CONSTANT` (321) | `Constant %s is used without having been given a value.`（:719–720） | 无绑定 constant 被求值用到；NFEvalConstants。E10b |
| 13 | `UNBOUND_PARAMETER_ERROR` (500) | `Parameter %s has neither value nor start value, and is fixed during initialization (fixed=true).`（:931–932） | 无绑定无 start 的 fixed 参数；NFFlatten `verifyBinding`。E08b |
| 14 | `CIRCULAR_PARAM` (125) | `Variable '%s' has a cyclic dependency and has variability %s.`（:329–330） | constant/结构参数循环依赖求值。E09 |
| 15 | `STRUCTURAL_PARAMETER_OR_CONSTANT_WITH_NO_BINDING` (5033) | `Could not evaluate structural parameter (or constant): %s which gives dimensions of array: %s. Array dimensions must be known at compile time.`（:1219–1220） | 维度用的结构参数无值；NFTyping/NFFlatten。E12/E10c |
| 16 | `DIVISION_BY_ZERO` (15) | `Division by zero in %s / %s`（:109–110） | 编译期求值遇除零；Ceval。E18/E19 |

**警告类**（不中断）：`UNBOUND_PARAMETER_WITH_START_VALUE_WARNING`(499) "Parameter %s has no value…using…start value (start=%s)…"（E08/E13/V12）；`CONDITIONAL_COMPONENT_INVALID_CONTEXT`(357) "Conditional component '%s' is used in a non-connect context."（E04）；`WARNING_RELATION_ON_REAL`(509) "In relation %s, %s on Real operands is deprecated in non-function contexts."；`UNBOUND_PARAMETER_EVALUATE_TRUE`(596) "Parameter %s has annotation(Evaluate=true) and no binding."。

omc 原始输出（关键几条）：

```
# UNRESOLVABLE_TYPE（T04_unresolvable_string: Real x = "a" + 1;）
Error: Cannot resolve type of expression "a" + 1. The operands have types String, Integer in component <NO_COMPONENT>.

# UNRESOLVABLE_TYPE（T09_unresolvable_bool: Real x = true + 1;）
Error: Cannot resolve type of expression true + 1. The operands have types Boolean, Integer in component <NO_COMPONENT>.

# TYPE_MISMATCH_IF_EXP（T05_ifexp_mismatch: Real x = if time>0 then 1 else "a";）
Error: Type mismatch in if-expression in component . True branch: 1 has type Integer, false branch: "a" has type String.

# EQUATION_TYPE_MISMATCH_ERROR（T06_equation_mismatch: equation 1 = "a";）
Error: Type mismatch in equation 1 = "a" of type Integer = String.

# ARG_TYPE_MISMATCH（T07_arg_mismatch: Real x = sin("a");）
Error: Type mismatch for positional argument 1 in sin(x="a"). The argument has type:
  String
expected type:
  Real

# IF_CONDITION_TYPE_ERROR（T08_ifcond_type: if 1 then x = 1; end if;）
Error: Type error in conditional '1'. Expected Boolean, got Integer.

# VARIABLE_BINDING_TYPE_MISMATCH（V03: Integer i = x > 0.5;）
Error: Type mismatch in binding i = x > 0.5, expected subtype of Integer, got type Boolean.
```

## 探针

全部在 `docs/research/probe-r14/`（`<名>.mo` + `<名>.out`），命令一律 `timeout 120 omc <名>.mo`。下面挑 16 个最适合固化成 ymc 压平单测的"输入表达式 → 压平打印"对照。`⇒` 左为源码、右为 omc 1.25.0 压平输出（截取关键行）。

**T. 类型/隐式转换（Integer→Real）** — `T01_int_to_real.mo`
```
Real a = 1;        ⇒ Real a = 1.0;
Real b = i;        ⇒ Real b = /*Real*/(i);          (Integer i)
Real d = k;        ⇒ Real d = 5.0;                  (constant Integer k=5：先 CAST，代入折叠)
Real e = i + 1;    ⇒ Real e = /*Real*/(i + 1);      (整个整型子式外包 CAST)
Real g = i / 2;    ⇒ Real g = /*Real*/(i) / 2.0;    (除法两侧各匹配 Real)
Real h = 7 / 2;    ⇒ Real h = 3.5;
Real t = sin(i);   ⇒ Real t = sin(/*Real*/(i));     (实参 cast 进函数)
Real s = abs(i);   ⇒ Real s = /*Real*/(abs(i));     (整型重载结果再 cast)
```

**S1. 算术折叠 / 代数恒等** — `S01_arith_fold.mo`
```
1 + 2      ⇒ 3.0        7 / 2   ⇒ 3.5        2 ^ 3    ⇒ 8.0      -(2+3)  ⇒ -5.0
1.0/0.0    ⇒ 1.0/0.0   (不折叠)   2147483647+1 ⇒ 2147483648  (不查溢出)
x*1 ⇒ x    1*x ⇒ x     x+0 ⇒ x   0*x ⇒ 0.0   -(-x) ⇒ x   x^1 ⇒ x   x^0 ⇒ 1.0   x-x ⇒ 0.0
x+x ⇒ x+x  (不化简)     2*x*3 ⇒ 2.0*x*3.0 (不收集)   2*3*x ⇒ 6.0*x   1+2+x ⇒ 3.0+x
```

**S2. 内建函数折叠** — `S02_builtin_fold.mo`
```
sin(0)⇒0.0  cos(0)⇒1.0  abs(-3)⇒3  max(1,2.5)⇒2.5  div(7,2)⇒3  mod(-7,2)⇒1  rem(-7,2)⇒-1
integer(3.7)⇒3  integer(-3.7)⇒-4  String(2.5)⇒"2.5"  String(E.two)⇒"two"  Integer(E.three)⇒3
sum({1,2,3})⇒6.0  size({1,2,3},1)⇒3   sqrt(-1)⇒sqrt(-1.0) (保留)   max(x,2)⇒max(x,2.0) (保留)
sin(x)+sin(0.0)*x ⇒ sin(x)   (多规则级联)
```

**S3. 逻辑/关系/if** — `S03_logic_rel_if.mo`
```
true and b⇒b  false and b⇒false  not(not b)⇒b   b and b⇒b and b (不化简)
1<2⇒true  "a"<"b"⇒true  E.one<E.two⇒true   x>1+2⇒x>3.0   x<x⇒x<x (不化简)
if true then x else 2*x ⇒ x      if b then x else x ⇒ x     if b then true else false ⇒ b
if b then 1 elseif true then 2 else 3 ⇒ if b then 1.0 else 2.0
```

**E1. 常量链求值+代入** — `E01_const_chain.mo`
```
constant Real TWO_PI = 2*PI;  ⇒ constant Real TWO_PI = 6.28;
equation x = 2.0*PI;          ⇒ x = 6.28;      (常量 cref 代入方程)
```

**E2. 普通参数不代入** — `E02_param_chain.mo`
```
parameter Real p = 2*q;   ⇒ parameter Real p = 2.0 * q;   (q 保留符号)
equation x = p*time+2*3;  ⇒ x = p*time + 6.0;             (p 保留、6.0 折叠)
          y = r*m + n;    ⇒ y = r*/*Real*/(m) + /*Real*/(n);
```
对照 `--evaluateStructuralParameters=all -d=evaluateAllParameters`（`E02_param_chain.evalAll.out`）：`p⇒3.0, y⇒35.0` 全折叠。

**E3. 结构参数（维度）** — `E03_struct_dim.mo`
```
parameter Integer n=2, m=n+1;  ⇒ final parameter Integer n=2;  final parameter Integer m=3;
Real x[n];                     ⇒ Real x[1]; Real x[2];        (维度参数变 final 并求值)
```

**E5/E14/E15. if 方程裁剪** — `E05/E14/E15`
```
E05: if flag(=true param) then x=p else x=2*p        ⇒ x = p;
E14: 分支方程数不等 + flag=false                     ⇒ 选 else 支，不报错
E15: flag=true annotation(Evaluate=false)            ⇒ if 方程原样保留（未裁剪）
```

**E4. 条件组件** — `E04_struct_cond.mo`
```
Real b if other(=false);  ⇒ 输出无 b     Real a if use(=true);  ⇒ 保留 a
（引用条件件 a、c 于非 connect 语境 ⇒ 各出一条 CONDITIONAL_COMPONENT_INVALID_CONTEXT 警告）
```

**V13. Evaluate=true 在子组件** — `V13_eval_in_sub.mo`
```
A a2(k = top);  k 带 Evaluate=true, top=5  ⇒ final parameter Real a2.k = 5.0;  Real a2.y = 5.0*a2.g;
A a3(k=7, g=top); ⇒ a3.y = 35.0;   (k、g 都成 structural 后整式折叠)
```

**V14. 常量数组下标** — `V14_const_array.mo`
```
Real x = c[idx];  (constant c, idx=2)   ⇒ Real x = 2.0;
Real z = c[vi];   (vi 连续 Integer)      ⇒ Real z = {1.0,2.0,3.0}[vi];   (数组代入、下标保留)
```

**E17. 编译期函数求值** — `E17_func_eval.mo`
```
constant Real c = f(2);   ⇒ c = 5.0;      parameter Real r = f(q);  (q 普通 param) ⇒ r = E17P.f(q);
Real x = f(4);            ⇒ x = 9.0;      Real y = f(x);            ⇒ y = E17P.f(x);
```

**错误类**（`.out` 为报错原文）：
```
V01_higher_var_param   ⇒ Component p of variability parameter has binding 'x' of higher variability continuous.
V06_dim_var            ⇒ Dimensions must be parameter or constant expression (in n).
V07_cond_var           ⇒ Component condition must be parameter or constant expression (in b).
V08_for_range_var      ⇒ The iteration range 1:n is not a constant or parameter expression.
E08b_unbound_param_err ⇒ Parameter p has neither value nor start value, and is fixed during initialization (fixed=true).
E09_cyclic_const       ⇒ Variable 'b' has a cyclic dependency and has variability constant.
E10b_unbound_const_eq  ⇒ Constant a is used without having been given a value.
E18_div_zero_const     ⇒ Division by zero in 1.0 / 0.0
```

## 未确认 / 待验证

- **`simplifyBinaryDiv` 疑似笔误**（NFSimplifyExp.mo:1112）：负号分派 `match (Expression.isNegated(exp1), Expression.isNegated(exp1))` 两个参数都是 `exp1`（应有一个是 `exp2`）。探针 b24 `x/(-1)`→`-x` 走的是前面的 `isMinusOne` 支，没暴露此 bug；`x/(-y)` 是否漏掉负号提取未探。ymc 实现除法化简时别照抄这行。
- **"Integer/Boolean 默认离散"** 的 MLS 措辞（MLS §3.8.x discrete-time）与 OMC NF 的实现（variability 只来自前缀，探针 V15_default_discrete）不一致：OMC 把离散性放在类型层（`Type.isDiscrete`）而非 variability 档。ymc 采哪种建模，需在设计里定一次（建议跟 OMC，避免把类型和 variability 耦合）。
- **`should_eval` 分支矩阵**（flattenIfEquation，NFFlatten.mo:1975–2016）依赖 `scalarize`/`newBackend`/`minimalEval` 组合；本报告只验证了安装版 omc 的默认组合（scalarize=on、旧后端语义、all）。新后端（工作区 omc）下 if 方程条件"标 structural 但延后求值"的路径未跑（受基线无限循环限制）。
- 内建函数折叠的**完整白名单**（哪些 `builtin` 进 `simplifyBuiltinCall` vs 只在 Ceval 里折）未逐个枚举，S02 覆盖了常用 ~40 个；`delay/sample/pre/edge/previous` 等时间/时钟算子的折叠行为未探。
- `nominal` 无默认值时后端如何取（1.0？）不在前端可见，未验。

## 对 ymc 的建议

### 类型表示
- `Type` 用 **enum kind + 载荷**：`{ INTEGER, REAL, STRING, BOOLEAN, ENUMERATION(path, literals[]), ARRAY(elem, dims[]), COMPLEX(clsRef, complexKind), CLOCK, TUPLE, UNKNOWN, … }`。**`ARRAY` 保持扁平**（一个元素类型 + `vector<Dimension>`，绝不嵌套 array-of-array），`liftArrayLeft` 在左边加维度——跟 OMC 一致，压平打印/下标运算全靠这个不变量。
- `Dimension` 单独一个 sum type：`INTEGER(size, var) | EXP(exp, var) | UNKNOWN | BOOLEAN | ENUM(type)`，带 `isProcessing` 标志检循环维度（对应 NFDimension）。
- `enum kind` 用整数序保证 `Integer→Real` 之类的隐式提升可以用"kind 比较"快速判定；`COMPLEX` 留一个回调钩子（ymc 只做内建类型时是唯一需要留桩的分叉，见 §1.4）。

### 表达式挂类型 / variability 的位置
- 每个 `Expression` 节点挂 **`Type`**（定型后回填）；**variability 不必常驻节点**——OMC 是"要用时算"（`typeExp` 返回三元组 `(exp, type, variability)`）。ymc 可仿此：定型函数返回 `(typedExp, type, variability)`，只把 variability 存到需要检查的地方（组件、绑定）。
- **variability 七档 + 比较**照抄（§2.1）；组件档位=前缀映射，别掺类型。`STRUCTURAL_PARAMETER` 与 `final` 绑定：标结构参数时同时置 final（§2.4），压平打印才对得上 `final parameter`。
- 关系式（含连续操作数、非 `noEvent`）variability 降 DISCRETE 这条别忘（否则 `Boolean b = x>0.5` 的档位会算错）。

### 求值器需支持的值种类
- `Value` 至少覆盖：`Integer / Real / Boolean / String / Enum(index) / Array(递归) / Record(字段表)`。求值入口对应 `evalCref`（查组件绑定并递归）+ `evalBinaryOp/UnaryOp/RelationOp/LogicOp` + 内建函数表 + 用户函数解释器（E17 需要）。
- **两档强度**：`evalExp`（硬求值，失败报错——给 constant 和"必须知道"的维度用）与 `tryEvalExp`（checkpoint 包裹，失败回滚保留符号——给 structural parameter 用）。§3.1 的三分逻辑照抄。
- 除零、定义域外（`sqrt(-1)`）：求值器区分"编译期必须求值"（报 `DIVISION_BY_ZERO`）与"化简遇到"（保留符号，仿真期再说）。

### 化简规则表的组织
- 一个 `simplify(exp)` **自底向上单趟**：先递归化简子节点，再按顶层算子 `switch` 到 `simplifyAdd/Sub/Mul/Div/Pow/LogicAnd/LogicOr/Relation/If/Cast/Call`。
- **两操作数皆字面量 → 直接调求值器**（`evalBinaryOp` 等），这是折叠主力；其余是"代数恒等式"小规则集（§4.2 表逐条）。
- 明确**不实现**：`x+x→2*x`、跨树常量收集、自比较/矛盾律。想要 `6*x` 这种，另起一个可选的 "flatten-multary + 常量收集" 趟，默认关（对齐 OMC 默认不走 `ExpandExp`）。
- 化简规则表建议做成**数据驱动**（`(op, 左模式, 右模式) → 结果构造`）方便逐条对拍 §4 的探针。

### 各趟先后依赖（硬顺序）
```
解析 → 实例化(instExpressions) → updateImplicitVariability(标 structural/final)
     → 定型(typeClass 三趟；维度/条件组件条件在此立即求值)
     → flatten(if 方程/条件组件裁剪、无绑定参数校验)
     → evalConstants(cref→值，只替换)
     → simplify(折叠)               ← 可用 --noSimplify 与上一趟分离对拍
     → scalarize(数组拆标量)
```
关键依赖：**`updateImplicitVariability` 必须在定型前**（否则维度参数还没标 structural，定型时求不了值）；**`evalConstants` 在 `simplify` 前**（先把 cref 换成值，simplify 才有字面量可折）；**裁剪在求值后**才能拿到 `final parameter` 的确定值。用 `--noSimplify` 对照组（探针 E01.noSimplify）能把"替换"与"折叠"两步的效果分开验，ymc 应保留同样的调试开关。
