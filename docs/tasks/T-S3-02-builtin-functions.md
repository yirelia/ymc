# 任务卡 T-S3-02：内建函数（标量）——符号打印 + 常量折叠

**执行者**：Codex（`codex exec`，非交互批处理，不要停下来等确认）
**验收者**：Claude
**归属阶段**：S3 flatten 加宽桶（承接 evalConstants，commit `d61d809` 之后）

---

## 0. 一句话目标
让 flatten 支持**标量内建函数**：非常量实参时按 `name(args)` 符号打印（`der(x)`、`sqrt(r1+r2)`），
全部实参是字面量时按 OMC 语义**折叠成常量**（`sqrt(25)`→`5.0`、`integer(-9.9)`→`-10`）。
用户自定义函数、数组/归约函数、命名实参一律 SKIP（退 66）。

## 1. 现状（改动锚点）
- `src/flat/flatten.cpp` 的 `call()`（约 739 行）现在无条件 `set_unsupported("function call / ceval")` 后仍构造 Call 占位。
- `src/flat/expr_dump.cpp` 的 `case ExprKind::Call`（约 177 行）**已能**打印 `dotted(call_name)(arg, arg, …)`——不用改 dump。
- `src/flat/flatten.cpp` 的 `simplify_expr()`（evalConstants，约 787 行起）已折叠 Int/Real 算术/关系/逻辑/Cast/if；
  Call 目前落到 `default: return id`（不折）。**本任务在这里加 Call 折叠**。
- `to_real(Id<Expr>)`、`lit_int/lit_real/lit_bool/lit_string`、`real_literal_text`（`src/absyn/unparse.h`，Ryu 最短）都已就绪，直接复用。
- Absyn `Exp`：Call 的实参在 `e.args`，命名实参名在 `e.arg_names`（与 args 平行，非空即命名实参）。函数名在 `e.cref`（`Cref.parts`）。

## 2. 范围

### 2.1 做（IN）——标量内建函数
分两类，都只处理**单段函数名**（`e.cref.parts.size()==1`）、**无命名实参**（`e.arg_names` 全空）、**标量实参**（实参 Expr 不是 `ExprKind::Array`）。

**A. 可折叠纯函数**（全实参字面量 → 算出常量；否则符号打印）。语义**逐字**照 OMC `NFCeval.mo`（下表标了 `evalBuiltin*` 与行号锚点，引用前重新 grep 确认）+ testsuite 实测：

| 名字 | 元数 | 实参域(强转) | 结果类型 | 折叠语义（`NFCeval.mo`） | 实测样例 |
|---|---|---|---|---|---|
| `abs` | 1 | 保留 | 同实参 | Int→abs(Int)，Real→abs(Real)（`evalBuiltinAbs`:2172） | `abs(-2.0)`→`2.0`，`abs(3)`→`3` |
| `sign` | 1 | **Real** | **Integer** | x>0→1, x<0→-1, else 0（`evalBuiltinSign`:2855） | `sign(-6)`→`-1`，`sign(4.0)`→`1` |
| `sqrt` | 1 | Real | Real | `sqrt`（`evalBuiltinSqrt`） | `sqrt(25)`→`5.0`，`sqrt(15.4)`→`3.924283374069717` |
| `sin`/`cos`/`tan` | 1 | Real | Real | C `sin/cos/tan` | `sin(1)`→`0.8414709848078965` |
| `asin`/`acos`/`atan` | 1 | Real | Real | C `asin/acos/atan` | — |
| `atan2` | 2 | Real,Real | Real | C `atan2(y,x)`（**注意参数序**，`evalBuiltinAtan2`:2087） | `atan2(3,2.0)` |
| `sinh`/`cosh`/`tanh` | 1 | Real | Real | C `sinh/cosh/tanh` | — |
| `exp` | 1 | Real | Real | C `exp` | `exp(4.5)` |
| `log` | 1 | Real | Real | C `log`（自然对数） | `log(1024)` |
| `log10` | 1 | Real | Real | C `log10` | `log10(250)` |
| `ceil` | 1 | Real | **Real** | `ceil`（`evalBuiltinCeil`） | `ceil(...)`→ Real |
| `floor` | 1 | Real | **Real** | `floor`（`evalBuiltinFloor`:2448） | `floor(4.25)`→`4.0`，`floor(-7.9)`→`-8.0` |
| `integer` | 1 | Real | **Integer** | **floor**（实测 `integer(-9.9)`→`-10`；OMC 源写 `realInt` 但**观测是 floor**，以实测为准，**任务卡里存疑于 open-questions**） | `integer(4.25)`→`4`，`integer(-9.9)`→`-10` |
| `div` | 2 | 保留(见注) | Real 若任一实参 Real，否则 Integer | **向零截断** `trunc(a/b)`（`evalBuiltinDiv`:2379；by-zero→报错，见 2.2） | `div(6,2)`→`3`，`div(-10,3)`→`-3`，`div(8,1.5)`→`5.0` |
| `mod` | 2 | 保留 | Real 若任一 Real，否则 Integer | `a - floor(a/b)*b`（`evalBuiltinMod`:2705；by-zero→报错） | `mod(10,3)`→`1`，`mod(-3,1.4)`→`1.1999999999999993` |
| `rem` | 2 | 保留 | Real 若任一 Real，否则 Integer | `a - div(a,b)*b`（`evalBuiltinRem`:2806；by-zero→报错） | 照 testsuite `CevalRem1` |
| `Integer`（大写，枚举转序数） | 1 | 枚举 | Integer | 枚举字面量的 1 基序数（`evalBuiltinIntegerEnum`:2102） | `Integer(E.two)`→`2` |
| `max`/`min` | **2 标量** | 保留 | 公共类型（任一 Real→Real） | 较大/较小值（`evalBuiltinMax/Min`；**仅 2 标量实参**，数组归约见 2.2） | `max(1,2)`→`2` |

- **div 实参域注**：`div(10, 3.0)`→`3.0`（混合 → Real 求值再判类型）。折叠时：若结果类型是 Real 就按 Real 算 `trunc(ax/bx)` 再 `lit_real`；否则按 Int 算 C `/`（向零截断）再 `lit_int`。
- **结果类型决定 Real 强转**：结果类型只影响**外层绑定/方程的强转**（已有逻辑：`Real z = <Integer 表达式>` 会 `to_real`）。函数节点自身 `ty` 要设对（下表结果类型列），否则外层强转会错/漏。

**B. 恒符号函数**（永不折叠，只符号打印；结果类型见表，用于外层强转）：

| 名字 | 元数 | 结果类型 | 备注/样例 |
|---|---|---|---|
| `der` | 1 | Real | `der(x)`→`der(x)`（`FuncBuiltinDer`） |
| `pre` | 1 | 同实参 | `pre(i)`（i:Integer）→ 外层 `Real z=pre(i)` 打 `/*Real*/(pre(i))`（`FuncBuiltinPre`） |
| `initial`/`terminal` | 0 | Boolean | `initial()`（`FuncBuiltinInitial1`/`Terminal1`） |
| `edge`/`change` | 1 | Boolean | — |
| `sample` | 2 | Boolean | — |
| `noEvent` | 1 | 同实参 | `NoEvent`：结果 = 实参类型 |
| `smooth` | 2 | 同第 2 实参 | `smooth(p, x)` → x 的类型 |
| `semiLinear` | 3 | Real | `FuncBuiltinSemiLinear` |
| `homotopy` | 2 | 同第 1 实参 | `homotopy(actual, simplified)` |
| `delay` | 2或3 | Real | `FuncBuiltinDelay` |

> 符号函数的实参：`der`/`sign`/数学函数把 Integer 实参**强转到 Real**（`sign(r1+r2)`→`sign(/*Real*/(r1 + r2))`）；
> `abs`/`pre`/`noEvent`/`max`/`min`/`mod`/`rem`/`div` **保留实参类型**（`abs(r1 - /*Real*/(r2))` 里的 cast 是 `-` 算符自身的，不是 abs 的）。
> 实现上：对"实参域=Real"的函数，构造 Call 前对每个实参 `to_real(arg)`；"保留"的不动。折叠路径同理（先按域强转再取字面量值）。

### 2.2 不做（SKIP，退 66，记 skip_reason）
- 命名实参（`e.arg_names` 任一非空）：`String(485, minimumLength=10)` → `set_unsupported("named function argument")`。
- 函数名不在上表（**用户自定义函数** / 数组·归约·构造类内建：`sum`/`product`/`min`/`max`(数组)/`String`/`cat`/`fill`/`array`/`zeros`/`ones`/`transpose`/`matrix`/`vector`/`scalar`/`diagonal`/`identity`/`cross`/`skew`/`symmetric`/`size`/`ndims`/`linspace`/`outerProduct`/`smooth`? 等）→ `set_unsupported("function: <name>")`。
- 任一实参是 `ExprKind::Array`（数组实参）→ `set_unsupported("array function argument")`。
- 多段函数名 `A.B.f(...)`（限定/包内函数）→ `set_unsupported("qualified function call")`。
- 折叠时 `div/mod/rem` 除数为 0 → **不折叠**（omc 报错，ymc 无验证 pass）：`set_unsupported("division by zero in constant")`，别产出错值。
- `max`/`min` 实参不是恰好 2 个标量 → SKIP（数组归约不做）。

## 3. 实现步骤

**Step 1 — 建内建表**（`flatten.cpp` 匿名命名空间，参考 `attr_table` 的写法）：
```cpp
enum class BFArg { Keep, Real };                 // 实参域
enum class BFRes { SameArg0, Real, Integer, Boolean, SameArg1, CommonNum, DivType };
struct BuiltinFn { const char* name; int arity; /*-1=可变*/ bool foldable; BFArg argdom; BFRes res; };
// 返回指针或 nullopt；名字查表。0 元数（initial/terminal）arity=0。
const BuiltinFn* lookup_builtin(std::string_view name);
```
- 数学/`sqrt`/`sign`/`der`：`argdom=Real`。`abs/pre/noEvent/max/min/mod/rem/div/integer/smooth/homotopy`：`argdom=Keep`（`integer` 折叠时内部按 Real 算，但符号打印保留？——**核对**：`integer(r1/r2)` 里 `r1/r2` 是 `/` 产生的 Real，integer 本身不再加 cast，所以 integer `argdom=Keep`）。
- 结果类型：见 2.1/2.2 表。`div/mod/rem`=`DivType`（任一 Real→Real）；`max/min`=`CommonNum`；`sign/integer`=Integer；`ceil/floor`=Real。

**Step 2 — 改 `call()`**：
1. 取函数名 `fn = crefs[e.cref]`；若 `parts.size()!=1` → SKIP("qualified function call")。
2. 若 `arg_names` 有非空 → SKIP("named function argument")。
3. `const BuiltinFn* bf = lookup_builtin(name)`；`bf==null` → SKIP("function: <name>")。
4. 元数不符（arity>=0 且 != args.size()）→ SKIP("function arity")。
5. 逐个 `inst_exp(arg)`；任一是 `Array` → SKIP("array function argument")。
6. 若 `argdom==Real`：对每个 arg `to_real(arg)`。
7. 构造 `Expr{Call, call_name={name}, args=…, ty=result_type(bf, args)}`，`add`。
   - `result_type`：SameArg0→args[0].ty；Real→Real；Integer→Integer；Boolean→Boolean；SameArg1→args[1].ty；CommonNum→(任一 Real?Real:Integer)；DivType 同 CommonNum。
   - **注意 arena**：`inst_exp`/`to_real` 会 add，别跨 add 持 `Expr&`；先把 arg id 收进 vector 再建节点。
8. **不要** `set_unsupported`（这是本任务的核心改动）。

**Step 3 — `simplify_expr` 加 Call 折叠**（在 `default` 之前加 `case ExprKind::Call`）：
1. 先 `simplify_expr` 每个 arg（拷 arg id 列表，避免跨 add 持引用）。
2. 查表；`!foldable` 或名字不在表 → 用简化后的 args 重建 Call 节点返回（保持符号，但子表达式已折叠，如 `sqrt(r1+r2)` 里 `r1+r2` 若含常量会折）。
3. `foldable` 且**所有 arg 是数值/枚举字面量**（`is_num_lit` 或 IntLit/RealLit；`Integer`(enum) 收 EnumLit）：按表算：
   - 先按 argdom/结果类型确定按 Real 还是 Int 算（`as_real(id)` 已有）。
   - 数学函数：`lit_real(std::func(as_real(a0)))`。
   - `abs`：Int→`lit_int(std::llabs(iv))`，Real→`lit_real(std::fabs(rv))`。
   - `sign`：`lit_int(v>0?1:v<0?-1:0)`（v=as_real）。
   - `floor/ceil`：`lit_real(std::floor/ceil(rv))`。`integer`：`lit_int((int64)std::floor(rv))`。
   - `div`：Real 型 `lit_real(std::trunc(ax/bx))`；Int 型：by-zero→SKIP，否则 `lit_int(ix/iy)`（C 截断）。
   - `mod`：`a - floor(a/b)*b`；Int 型判 by-zero。`rem`：`a - trunc(a/b)*b`。
   - `Integer`(enum)：EnumLit 的序数——**枚举字面量序数怎么取？** EnumLit 只存 `enum_path`（`{E,two}`），没存序数。v1 折 `Integer(E.two)`需要序数 → 若拿不到序数就 SKIP("enum ordinal")（放进 open-questions，后续桶补枚举序数）。**先 SKIP `Integer(enum)` 与枚举关系**，只做数值函数。
   - `max/min`：`lit`较大/较小，类型 CommonNum。
   - 非字面量（含枚举）→ 用简化 args 重建 Call 返回（符号）。
4. 重建 Call 时结果 `ty` 用原节点 `ty`（别丢类型）。

**Step 4 — 验收对拍 + 固化**（照 CLAUDE.md「任务收尾必须与 OMC 对拍」四条，逐条跑）：
- `python tools/flat_sweep.py --cases-from docs/research/s3-easy-slice.json --oracle-live`：`FAIL 0` 且 `oracle=live 不一致 0`。
- **横切**：本改动动 `call()`+`simplify_expr`（cref/表达式横切）→ 必跑 `python tools/flat_sweep.py --oracle-live`（全 scodeinst）确认**无新增真 FAIL**（对比改前 `git stash` 一次基线）。
- 三套编译器 `ctest` 全绿零警告 + ASan 0。
- 新 PASS 的 `FuncBuiltin*`/`Ceval*` 用例加进 `docs/research/s3-easy-slice.json`（一名一行，保持字母序）。
- `tests/unit/flat/flatten_test.cpp` 加单测，期望值**逐字取自实跑 omc 1.25.0**（探针），不许照实现写。至少覆盖：`sqrt(25)`→`5.0`、`sqrt(r1+r2)` 符号、`abs(-2.0)`/`abs(3)`、`integer(-9.9)`→`-10`、`div(-10,3)`→`-3`、`der(x)` 符号、`sign(r1+r2)` 符号带 Real 强转、by-zero `div` SKIP。

## 4. 坑位（务必逐条防）
1. **Real 格式化**：折叠出的 double 一律走 `real_literal_text`（Ryu 最短，如 `3.924283374069717`），别用 `std::to_string`。
2. **`integer` 是 floor 不是截断**：`integer(-9.9)=-10`。div/rem 才是向零截断。别搞反。
3. **arena 纪律**：`inst_exp`/`to_real`/`lit_*`/`simplify_expr` 都 add，会使 `Expr&` 失效。先把所有 arg id 收进 `std::vector<Id<Expr>>`，全部 add 完再建父节点；`simplify_expr` 里拷 `a0=exprs[id].a`、算子/类型等标量字段后再递归（照现有 `simplify_expr` 各 case 写法）。
4. **结果类型**：函数节点 `ty` 设错会让外层 `Real x = f(...)` 漏/错 `to_real`。`pre(i)`(Integer) 外层 `Real z` 要出 `/*Real*/(pre(i))`——靠 pre 结果类型=Integer + 现有 binding 强转。
5. **实参强转位置**：argdom=Real 的函数在**构造 Call 前**对每个 arg `to_real`（这样 `sign(r1+r2)` 得到 `sign(/*Real*/(r1+r2))`）；argdom=Keep 的不动。
6. **符号 vs 折叠是同一函数的两条路径**：`call()` 只负责建 Call + 定类型 + 实参强转，**不判常量**；折叠只在 `simplify_expr` 做（因为常量内联/子折叠后才知道 args 是不是字面量）。别在 `call()` 里试图折叠。
7. **by-zero**：折叠 `div/mod/rem` 前判除数字面量为 0 → SKIP，别 `lit_int(x/0)`（UB/崩）。
8. **别碰 dump**：`expr_dump` 的 Call 已够用；若发现某函数打印不对再说，默认不动。
9. **`der` 参数是 cref**：`der(x)` 的 x 走正常 cref（可能是 time 等）——不特殊处理。
10. **命名实参检测**：`e.arg_names` 是 `vector<Symbol>`，与 args 平行，元素**非 invalid** 即命名。空 vector 或全 invalid = 全位置实参。核对 `absyn_to_scode`/parser 里 arg_names 的填法（`grep`），确认"无命名实参"的判据。

## 5. 允许改的文件
- `src/flat/flatten.cpp`（`call()`、`simplify_expr` 的 Call case、内建表）。
- `tests/unit/flat/flatten_test.cpp`（加单测）。
- `docs/research/s3-easy-slice.json`（加新 PASS 用例）。
- `docs/open-questions.md`（`integer`=floor vs realInt 存疑、`Integer`(enum) 序数暂缺、任何 file-vs-1.25.0 分歧）。
- `tasks/todo.md`（勾掉本桶）、`tasks/lessons.md`（若踩坑）。
- **不改** `expr_dump.*`、`inst/*`、其它 pass，除非坑位 8/9 逼你改并说明理由。

## 6. 来源（不臆想）
- OMC `OMCompiler/Compiler/NFFrontEnd/NFCeval.mo`：`evalBuiltinCall` 分派 :2074-2151；各 `evalBuiltin*`（引用前重新 grep 取行号）。
- testsuite `flattening/modelica/scodeinst/`：`FuncBuiltin*`、`Ceval*`（`// Result:` 是参考，**但以实跑 omc 1.25.0 为准**，冲突记 open-questions）。
- 语义拿不准（如 `integer` 负数、`atan2` 参数序、`rem` 负数）→ 先 `omc probe.mo` 实跑取真值，别猜。

## 7. 交接给 Codex 的话术（prompt 要点）
「非交互批处理，读并实现 `docs/tasks/T-S3-02-builtin-functions.md`，只改第 5 节允许的文件；语义写不出 MLS/OMC:行/探针三种来源之一就 SKIP 并记 open-questions；收尾必须按 CLAUDE.md『任务收尾必须与 OMC 对拍』四条实跑对拍（含 `--oracle-live`），全绿才算完成；不要停下来等确认。」
