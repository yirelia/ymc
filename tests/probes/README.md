# 探针回归集

每个 `.probes` 文件是一组为核实 omc 1.25.0 行为写的小模型——各加宽桶探针的原文。**期望值不入库**：

```
python tools/probe_recheck.py            # 全部；--show 5 打印差异，<集名…> 只跑部分
```

把每个模型同时交给 ymc 与实跑的 omc 1.25.0，逐字节对拍 stdout（判定口径同 `tools/flat_sweep.py`）。

## 规矩

- **每桶收尾必跑**，FAIL / ACCEPTS-INVALID / CRASH 必须为 0。语料扫描（flat_sweep / nf_live_sweep）只能证明"语料里的
  写法没坏"；探针才是已核实行为的回归网（来由见 `tasks/lessons.md` 2026-10-02 那条：59k 的别名规则把 59j 的探针打坏
  8 例，当时没重跑）。
- 确属 omc 1.25.0 自身的毛病、ymc 有意不跟的：在 `KNOWN.txt` 里登记一行 `<集>/<模型>  # 原因`，并且
  `docs/open-questions.md` 的分歧账本里要有对应条目。
- 新桶的探针：草稿区里核实后追加成一个新包 `<桶>-<主题>.probes`。格式：首行 `//// set: 名字 — 说明`，每个模型一段
  `//// model: 名字` + Modelica 源码（omc / ymc 压平文件里**最后一个类**，辅助类写在前面）。
- 不放 OMC testsuite 的拷贝（那是语料，走 sweep），不放 OMC 源码摘录。

## 现有的集

| 集 | 模型数 | 主题 |
|---|---|---|
| `alg` | 4 | 算法段 |
| `ao-a1` | 20 | 结构标记：别名与声明序 |
| `ao-a2` | 18 | 结构标记：实例化期 / 定型期 / 方程段 |
| `ao-a3` | 15 | 结构标记：依赖连带 |
| `ao-a4` | 12 | size(切片) |
| `ao-a5` | 18 | 结构标记：子组件 / 属性 / when |
| `ao-a6` | 8 | 外层写的修饰表达式的求值帧 |
| `ao-a7` | 8 | 求值帧（续） |
| `ao-ut` | 9 | 结构标记（单测原文） |
| `arrattr-split-c2` | 8 | 切分（续） |
| `arrattr-split-m1` | 12 | 切分（续） |
| `arrattr-split` | 11 | 属性的按元素切分 |
| `arrattr` | 45 | 数组组件的属性修饰 |
| `ba-p` | 36 | pre / change 的实参规则 |
| `ba-p2` | 36 | pre / change（续） |
| `ba-q` | 12 | 杂项 |
| `ba-r` | 29 | 数组上的 der / pre |
| `ba-s` | 22 | Boolean 数组表达式 |
| `ba-t` | 33 | Boolean 数组表达式（续） |
| `ba-u` | 53 | Boolean 数组 / 关系 |
| `ba-w` | 47 | 数组 if 表达式 |
| `ba-z` | 37 | 数组 if 按结构参数选支 |
| `cc53` | 8 | 杂项 |
| `ce54` | 9 | 常量求值 |
| `empty2` | 15 | 空数组（续） |
| `ev-m1` | 15 | Evaluate 注解 |
| `ev-m2` | 24 | 结构参数的别名连带 |
| `ev-m3` | 16 | 别名连带（续） |
| `ev-m4` | 10 | 别名连带（修饰 / 记录） |
| `ev-m5` | 6 | 别名连带（数组） |
| `exp52` | 12 | 表达式 |
| `fa-ut` | 8 | 同上（单测原文） |
| `fa` | 43 | 函数注解、derivative / inverse、空 algorithm 段 |
| `fn55` | 18 | 用户函数发射 |
| `imp` | 3 | import |
| `misc` | 176 | 各桶零散探针（早期，按文件名看主题） |
| `nf56` | 7 | nf-live 语料带出的杂项 |
| `r3` | 20 | 杂项 |
| `rec` | 98 | 记录：构造 / 绑定 / 字段 / 变率下沉 |
| `v4-a` | 30 | 同上（分组 a） |
| `v4-b` | 33 | 同上（分组 b） |
| `v4-c` | 58 | 同上（分组 c） |
| `v4-d` | 30 | 同上（分组 d） |
| `v4-e` | 26 | 同上（分组 e） |
| `v4-f` | 20 | 同上（分组 f） |
| `v4-g` | 9 | 同上（分组 g） |
| `v4` | 50 | 向量化调用、sum / product / min / max |
| `sy-s1` | 32 | 同步算子：Clock 构造与钟控 when |
| `sy-s2` | 70 | 同步算子：sample / hold / noClock / previous |
| `sy-s3` | 67 | 同步算子：backSample / shiftSample / subSample / superSample / interval / firstTick |
| `sy-s4` | 47 | 同步算子：Clock 实参形态、结构参数代入、钟控 when 的体 |
| `sy-s5` | 43 | 同步算子：其它语境 |
| `sy-ut` | 6 | 同步算子（单测原文） |
| `rd-r1` | 59 | min / max：两个标量、一个数组 |
| `rd-r2` | 64 | 归约式：常量 / 非常量 |
| `rd-r3` | 49 | 归约式：长度 0 / 1、变率、范围形态 |
| `rd-r4` | 47 | min / max：数组实参的形态 |
| `rd-r5` | 41 | 归约式与 min / max：其它语境 |
| `rd-ut` | 6 | 归约式与 min / max（单测原文） |
| `rc` | 7 | 记录构造绑定里依赖别的字段的缺省 |
| `ap-p1` | 29 | 参数数组进结构语境：下标 / 维 / 条件 |
| `ap-p2` | 27 | 参数数组：归约范围、整体引用、代入后的 assert |
| `ap-p3` | 16 | 方程 assert 条件恒真 |
| `ap-p4` | 32 | and / or 的吸收律 |
| `ap-p5` | 37 | 代入后成字面量的 if 条件（方程 / 语句）、assert(true) 的各语境 |
| `ap-p6` | 40 | sum / product 的范围是参数数组 |
| `ap-p7` | 23 | 体为空的 if 分支 |
| `ap-p8` | 30 | 归约范围的元素是含变量的式子 |
| `ap-p9` | 30 | 归约范围的保留形态 |
| `ap-p10` | 17 | 归约范围引用外层迭代变量 |
| `ap-p11` | 15 | 归约范围里的转型 / 参数算术 |
| `ap-ff` | 21 | fill 的维 / 范围里有 for 迭代变量 |
| `ap-ee` | 10 | 段的内容被删空 |
| `ap-ut` | 11 | 参数数组、吸收律、收尾折叠、归约范围（单测原文） |
| `dm-s1` | 51 | 整段范围切片 `x[1:3]` → `x[:]`：各种形态与语境 |
| `dm-s2` | 22 | when / if 体里的切片、下标数少于维数的切片 |
| `dm-s3` | 53 | 维里的 size(`:` 数组) / end / 类型名的 size |
| `dm-s4` | 33 | `:` 维由修饰给、继承来的组件、组件数组 |
| `dm-s5` | 34 | when 方程体里的数组等式 |
| `dm-misc` | 20 | 维 / 切片的零散探针（D / E / X） |
| `dm-ut` | 6 | 维里的 size、整段切片、when 体数组等式（单测原文） |
| `dm-s6` | 28 | Integer / Real 混合的范围（for 语句、函数体） |
| `dm-s7` | 42 | 求不出的常量运算：负底数的非整数幂、0 的负数次幂、定义域外的内建 |
| `dm-ut2` | 5 | 范围配型、非法幂、extends 经 replaceable 类（单测原文） |
| `drv-misc` | 4 | 次正规 Real 字面量的打印、`within` 子句（单文件压平） |
| `nr-n1` | 43 | 无返回值的调用：方程 / 算法语句 / 函数体、缺省实参、各语境 |
| `nr-n2` | 39 | 无返回值的调用：常量实参、impure / external、非法形态 |
| `nr-c1` | 16 | 常量实参的调用：函数体里有 assert / 调用语句 |
| `ga-g1` | 46 | 数组算术式作函数实参 |
| `ga-g2` | 42 | 数组内建 / 切片作函数实参 |
| `ga-misc` | 3 | 函数实参 / 函数体数组运算的零散探针 |
| `ga-ut` | 5 | 无返回值调用、数组实参展开（单测原文） |
| `fa-f1` | 65 | 函数体里的数组运算（点号算符、标量 * 数组、已知维的展开） |
| `fa-f2` | 70 | 函数体里的数组运算：Integer 数组、矩阵、嵌套、sum / max 的实参 |
| `fa-f3` | 51 | 函数体里的数组运算：取负改写、类型错、恒等元、控制流里的语句 |
| `fa-f4` | 40 | 函数体里的数组运算：Integer 数组的整体转型 |
| `fb-ut` | 4 | 括号规则、函数体数组运算（单测原文） |
| `pp` | 2 | 表达式打印的括号规则：算符两两组合 |
| `es-e1` | 125 | 标量化简规则表：恒等元、取负、常量折叠、if、关系与逻辑 |
| `es-e2` | 83 | 实参全是字面量的内建：求值 / 原样保留 |
| `es-e3` | 52 | semiLinear / smooth / noEvent / edge / homotopy / delay / der 的化简 |
| `es-e4` | 38 | array(…) 与 String(…)、字符串拼接 |
| `es-e5` | 66 | array(…) 调用写法：绑定、等式、求值、归约、运算；字符串拼接链 |
| `es-e6` | 26 | array(…) 与 {…} 的对照：函数体、算法段、size / max / for 范围 |
| `es-e7` | 12 | 模型算法段里的整数组赋值（未做，SKIP 基线） |
| `es-e8` | 87 | edge / change / semiLinear / smooth 在 when、for、函数体、向量化里的各种形态 |
| `es-e9` | 91 | pre / change / edge 的常量与结构参数实参；min / max / fill / cat / transpose 的化简 |
| `es-ut` | 4 | smooth / semiLinear / edge、array(…) 与字符串拼接（单测原文） |
| `uaf` | 1 | 合成 Absyn 节点时 arena 不扩容（400 个元素的记录数组字段展开；修复前 ASan 报 use-after-free） |
| `wa-w1` | 63 | 模型算法段的整数组赋值：右侧的各种形态（字面量、整取、算术、内建、if、切片、类型） |
| `wa-w2` | 78 | 整数组赋值：Integer → Real、嵌套语句、类型错、左侧切片、记录字段、函数调用 |
| `wa-w3` | 40 | 数组运算的恒等化简次序（先展开后化简）：绑定与赋值；字面量里的数组值元素 |
| `wa-w4` | 24 | 带步长的切片：步长 / 终点照写 |
| `wa-w5` | 108 | 整数组赋值：元素类型、Boolean 运算、for / when / if 里的左侧形态、常量与参数；`.^` 的类型 |
| `wa-ut` | 4 | 整数组赋值与数组绑定的整体保留形态（单测原文） |
| `wc-c1` | 65 | when 方程的条件形态：关系 / 逻辑 / 内建调用 / initial / sample / 条件向量 |
| `wc-c2` | 65 | when 语句的条件形态（同 c1 的条件） |
| `wc-c3` | 41 | elsewhen 分支的条件形态 |
| `wc-c4` | 63 | when 条件：terminal、用户函数、noEvent、枚举 / 字符串关系、条件向量的元素、类型错 |
| `wc-c5` | 72 | when 体：terminate / assert、数组与切片的 reinit、切片与元素作左侧、for 里的 when、解出变量的绑定 |
| `wc-c6` | 32 | assert / terminate 的消息：方程里不化简、语句里化简；各种位置 |
| `wc-c7` | 58 | assert 的消息与 level：结构参数、String 的实参、AssertionLevel 字面量与表达式、函数体 |
| `wc-ut` | 9 | when 条件与体、assert / terminate、数组构造子、注释（单测原文） |
| `cmp-k1` | 37 | 数组构造子：只有一个元素的范围、数组字面量取迭代下标、等式 / 语句 / 归约里的形态 |
| `cmp-k2` | 47 | 数组构造子：数组字面量范围、带步长的范围、多迭代变量、嵌套、各种体 |
| `fcm-f1` | 22 | 函数形参 / 局部 / 输出的字符串注释，各种函数形态 |
| `fcm-f2` | 18 | 类注释的转义、别名 / 继承函数的注释 |
| `fcm-f3` | 25 | 变量 / 方程 / 语句注释与字符串字面量里的转义序列 |
| `fcm-n1` | 3 | 多行注释的续行缩进（变量 1 格、方程 / 语句跟所在块）；函数与类注释 |
| `sz-s1` | 59 | size / ndims 的实参形态：字面量、算术式、切片、内建；维号的形态；维与常量里的 size |
| `sz-s2` | 53 | size / ndims：非常量维号、for / when / if 里、函数体里的 size(u) 与 ndims |
| `fs-t1` | 59 | 函数体里的数组切片：`:`、范围、end、下标数组；sum / 取负 / 乘；左侧切片 |
| `fs-t2` | 72 | 函数体切片：向量 / 矩阵积、Integer 切片、控制流、多维、下标向量变量、fill |
| `fs-t3` | 68 | 函数体：Integer 数组进 Real 的转型、fill / zeros / ones 的展开、各种积 |
| `fs-t4` | 17 | 函数体里的数组 if 表达式：分支转型、逐元素展开 |
| `fs-ut` | 5 | size 与函数体切片（单测原文） |
| `sm-m1` | 57 | 状态机算子：initialState / transition / activeState / ticksInState / timeInState 的各种实参与位置 |
| `sm-m2` | 49 | 状态机算子：状态实参的种类（记录 / 连接器 / 实例数组 / 嵌套实例）、具名实参、类型错、当表达式用 |
| `sm-ut` | 3 | 状态机算子（单测原文） |
| `ir-i1` | 39 | 隐式迭代范围：for 语句 / for 方程 / 归约 / 推导式的基本形态，维长不等与没有用处的报错 |
| `ir-i2` | 74 | 隐式迭代范围（模型）：if / when / while 里、参数维、`:` 维、各实例各推各的、记录数组、遮蔽、枚举 / Boolean 维 |
| `ir-i3` | 41 | 隐式迭代范围（函数体）：`:` 维 / size 维 / 表达式维 / 整数维的相等判定与取末，嵌套同名迭代变量 |
| `ir-s1` | 22 | 留成符号的下标的化简：for 语句 / 归约 / 推导式里的 `x[i + 0]`、`x[1 * i]` |
| `ir-e1` | 23 | 枚举 / Boolean 维上的迭代（隐式与显式范围）：1.25.0 的打印形态（ymc 未做，全 SKIP） |
| `ir-ut` | 13 | 隐式迭代范围、符号下标、经实例路径的维、多迭代推导式（单测原文） |
| `nd-d1` | 41 | 维里的子实例常量 / 参数 `a.j`、`a.p`、`c[2].j`、记录参数字段、子实例数组的 size、ndims、矩阵字面量 |
| `nd-d2` | 48 | 实例路径的值在别的编译期语境：下标、条件组件、if 方程、属性、归约范围、fill、整组结构标记、protected、Evaluate |
| `nd-d3` | 33 | 求值器里的矩阵字面量（拼接、混排类型）与 ndims |
| `nd-c1` | 18 | 推导式绑定：`:` 维、多迭代变量、参数 / 常量 / 连续变量 |
| `nd-c2` | 6 | 推导式绑定的形状与声明维不符（omc 类型错） |
| `nd-c3` | 27 | 多迭代推导式绑定：三重、参数范围、嵌套、依赖范围、空范围、String、修饰里 |
| `nd-p1` | 16 | protected 元素经实例路径的访问；`Evaluate = false` 的参数在各编译期语境 |
| `nd-v1` | 20 | ndims / 单实参 size 的变率（参数）：constant 绑定报错，Evaluate = true |
| `en-t1` | 31 | 类型维（枚举 / Boolean）数组：元素 / 整体引用、数组方程；for 方程在类型名 / 标签范围 / 标签数组上迭代 |
| `en-t2` | 42 | for 语句、归约、推导式在枚举 / Boolean 上迭代：保留形态、类型不符的下标、Integer(i)、参数 / 常量数组 |
| `en-t3` | 32 | 空的标签范围、`:` 维由带类型维的绑定定、类型维的切片 / end、size / ndims |
| `en-t4` | 33 | 类型维的切片 `y[E.one:E.two]`、`y[{E.three, E.one}]`、`y[end]`：绑定 / 等式 / 语句 / 多维 / 类型不符 |
| `en-t5` | 53 | 类型维数组在别的语境：der / when / reinit / if、数组运算、函数实参、子实例 / 记录字段、属性、Boolean 运行时下标 |
| `en-ut` | 7 | 类型维数组与枚举 / Boolean 上的迭代（单测原文） |
| `ca-c1` | 59 | 复合数组的元素引用（绑定 / sum / 等式右侧）：洞、切片、数组字段、多维、多层、越界、size |
| `ca-c2` | 31 | 复合数组引用在语句里的形态（array(a[$i1].x …) 未做）、符号下标 `a[i].x`、运行时下标 `a[n].x` |
| `ca-c3` | 40 | 复合数组引用在等式左侧、if / when / assert、修饰、参数维、子实例里的复合数组、记录数组 |
| `ca-c4` | 15 | der / pre / abs / noEvent 作用在复合数组取字段上 |
| `ca-ut` | 4 | 复合数组的元素引用（单测原文） |
| `md-m1` | 44 | 组件数组上的数组值修饰：绑定的各种形态（cref / 运算 / fill / 切片 / 推导式 / if / 用户函数 / 属性 / 多维 / 嵌套一层） |
| `md-m2` | 24 | 修饰字面量里 Integer 与 Real 混排（数组类型取切分那一层末元素的） |
| `md-m3` | 52 | 修饰绑定的更多形态：start 属性的混排、带运算的字面量、Integer 参数数组、常量数组、连续量、final、子模型里 |
| `md-m4` | 36 | 书写的 `(E)[i]`（运算 / 切片 / 调用 / 推导式 / 范围作基，越界，符号下标）与推导式取元素后的下标化简 |
| `md-m5` | 15 | 分发下来的参数值作结构量用（if 方程条件、维、条件组件、fixed） |
| `md-m6` | 18 | 逐元素的参数作维：各元素同维 / 参差（参数数组、复合子数组、for 范围、下标、条件组件） |
| `md-m7` | 18 | 参差的带绑定数组（omc 内部错）及邻近形态：方程 / 算法 / 推导式 / 多维 / 离散 / 嵌在子模型里 |
| `md-m8` | 30 | 参差守卫的矩阵：两层、参差的复合父、`:` 维、含 0、start 属性、外层修饰给的绑定、二维组件数组 |
| `md-m9` | 12 | 组件数组里的 constant / parameter 数组绑定引用逐元素的参数 |
| `md-m10` | 19 | constant 数组的绑定引用 parameter（fill / ones / 推导式 / `:` 维；size 的例外） |
| `md-m11` | 32 | constant 绑定里只在结构位置出现的 parameter（下标、范围、fill 个数、归约；size 的例外） |
| `md-m12` | 28 | constant 绑定引用 parameter 的其它语境：子实例、修饰、组件数组、记录、实例路径、迭代变量遮蔽、函数局部 |
| `md-m13` | 26 | 归约实参里的参数值（`f(sum(p))` 不求值）、下标 / 范围里的结构参数、记录的函数 / 构造绑定 |
| `md-m14` | 28 | constant 绑定变率守卫的误伤面：size 的各种写法、Evaluate / final 参数、子实例里的常量 |
| `md-ut` | 10 | 组件数组上的数组值修饰 / 参差守卫 / constant 绑定变率（单测原文） |
| `cx-c1` | 37 | constant 数组的切片 / 整取 / 参数下标（绑定、等式、语句） |
| `cx-c2` | 40 | constant 数组的运行时下标、参数下标、保留着的迭代变量 |
| `cx-c3` | 40 | 别的常量来源：包常量、子实例里的、记录字段、类型维、枚举 / Boolean / String 元素、越界、空数组、函数体 |
| `cx-c4` | 36 | 常量绑定的形态（fill / ones 的各元素相等、推导式、linspace、运算、别名、修饰）配运行时下标 |
| `cx-c5` | 34 | 数组值的运行时下标（`m[n]`、`m[:, n]`、`x[{n, 1}]`、留着的切片）在各语境 + 各元素相等的传播 |
| `cx-c6` | 29 | parameter / constant 绑定与属性值里被化简掉的变量引用（omc 报变率错） |
| `cx-c7` | 54 | 运行时下标的其它语境：if / assert / 函数实参 / for / while / 归约 / 属性 / 条件 / 维 / 嵌套下标 / 运算 |
| `cx-c8` | 44 | 模型结构（子实例、修饰、继承、记录数组、包记录、外层常量）与 Integer / Boolean / String / 各元素相等的常量数组 |
| `cx-c9` | 37 | end / 负步长 / 三维 / 修饰里的切片 / 大数组 / protected / fixed = false 与 Evaluate = false 的参数下标 |
| `cx-c10` | 18 | 函数体里的局部常量数组、初始段、when / if 语句、for 语句里的归约 |
| `cx-c11` | 20 | for 语句里对常量数组的归约：体的变率决定展开还是保留 |
| `cx-c12` | 20 | 常量变率、体里带外层迭代变量的归约（不带数组）：for 语句 / 数组构造子 / 嵌套归约 |
| `cx-c13` | 25 | 外层迭代变量的变率（范围是常量 / 参数 / 运行时量）与归约的展开 / 保留 |
| `cx-ut` | 11 | constant 数组的下标引用 / 书写绑定上的变率检查 / 常量归约（单测原文） |
| `cn-n1` | 30 | connect 实参的下标形态：迭代变量算式、参数、`end`、切片、`:`、多维、for / if 里、子模型数组 |
| `cn-n2` | 6 | connect 涉及的数组维取决于实例修饰的参数（`A a(M = 3)` 里的 `C p[M]`） |
| `cn-n3` | 40 | 深段的维取决于子实例参数（`ports[nPorts]`）、更多下标形态、越界 / 运行时 / Real / fixed = false 下标、短类连接器数组 |
| `cn-n4` | 30 | 子模型里 / 继承 / 条件组件 / expandable / cardinality / stream 里的下标；参差实例（`a[2](M = {2, 3})`）；零长度 |
| `cn-n5` | 12 | connect 下标里的参数与结构别名（实例化期标记：声明在前 / 在后的别名都 final） |
| `cn-ut` | 4 | connect 实参的表达式下标（单测原文） |
| `sl-s1` | 24 | 模型里的数组切片：空切片、运行时界的切片（语句目标 / 右侧 / sum）、`end` 算式、for 语句里的切片 |
| `sl-s2` | 12 | 函数体里运行时界的切片与 `end`（输入 `:` 维、输出 / 局部的表达式维） |
| `sl-s3` | 11 | 函数体下标里 `end` 取书写的维：`size(x, 1)`、`n`、`2 * n`、`size(x, 1) + 1`、二维、嵌套在调用里 |
| `sl-ut` | 1 | 函数体 `end` 取书写的维（单测原文） |
| `ih-e1` | 26 | 继承来的方程 / 算法段：多重 / 多层继承、extends 修饰、初始段、when、connect、组件数组、protected |
| `im-i1` | 43 | 类型名 / 基类名经 import：限定 / 重命名 / 组导入 / `.*`、冲突、遮蔽、不被继承、对嵌套类可见、encapsulated |
| `im-x1` | 15 | extends 的基类名从扩展类自身查：本地嵌套类、路径前缀、replaceable 基类、自引用、继承来的类 |
| `im-ut` | 3 | import / extends 查找 / 继承段（单测原文） |
| `sc-s1` | 27 | 短类别名到长类：修饰 / 多层别名 / 连接器 / 记录 / 包里定义 / final / 数组维 / type 别名 |
| `sc-s2` | 40 | 短类别名到长类：继承来的、嵌套实例里的、记录 / 连接器 / 前缀、包里的多层别名、修饰值的作用域、final 交互 |
| `sc-s3` | 32 | 短类别名到长类：记录缺省构造里的 time、整体绑定 + 短类修饰、partial / 受限类型 / inner-outer / 重复修饰 |
| `sc-t` | 11 | 记录构造缺省值里的 time、经短类别名调构造、replaceable 的 partial 类型、折叠记录字段的注释 |
| `sc-ut` | 4 | 短类别名到长类（单测原文） |
| `ps-p1` | 25 | 下标只能跟在括号式后面：构造子 / 矩阵 / 调用 / 元组 / 两段下标，绑定 / 算法 / if 式里，及合法的括号写法 |
| `ps-l1` | 14 | 带引号标识符的字符集：非 ASCII、空、反引号、转义（含 omc 只告警的非法转义）、ASCII 标点 |
| `ps-ut` | 10 | 下标 / Q-IDENT 的报错位置（单测原文） |
| `al-a1` | 25 | 隐式装库：表达式 / 函数调用 / import / extends / 类型里的 Modelica.…、ModelicaServices，同名本地元素、查不到的库名 |
| `fq-f1` | 30 | 属性修饰上的 final：类型里 / 组件声明里 / each、外层（实例 / extends / 短类 / 记录字段）再改 final 属性、MSL 的 SI 类型 |
| `fq-f2` | 25 | 数组组件的类型链修饰（每元素同值）+ MSL 的 SI 类型（Position[3]、Angle、Temperature、NonSI…） |
| `fq-t` | 10 | extends 属性修饰的优先级、隐式装库按文件的 uses 注解选版本、继承来的连接器分量序 |
| `fq-ut` | 3 | final 属性 / 数组派生类型 / extends 属性修饰（单测原文） |
| `ss-s1` | 29 | 内建枚举 StateSelect / AssertionLevel 当组件类型、stateSelect 里的参数（声明 / 嵌套修饰 / extends 修饰 / if 式 / each）、MSL 的 Inertia / Mass |
| `lc-c1` | 14 | 短类连接器：继承来的组件、限定类型名（包里的 / MSL Blocks.Interfaces 的 Real / Boolean / Integer 输入输出） |
| `lc-b` | 9 | MSL 的 Rotational / Blocks 部件互连（定位 connect operand shape 用的最小模型） |
| `ss-ut` | 3 | 内建枚举 + 库里的连接器（单测原文） |
| `rx-r1` | 25 | extends 修饰里的组件重声明：约束修饰（原声明的修饰）、叶 / 复合、绑定与注释的来源、非 replaceable、constrainedby、数组、多层 |
| `rx-f` | 24 | final 修饰在连接器里经短类定型的成员上被丢掉（mergeDerivedAttributes）及保留 final 的对照；零取负的符号 |
| `rx-m` | 3 | 重声明的最小模型（含 MSL 的 SineVoltage） |
| `rx-a` | 6 | 类型带 absoluteValue = false 时组件注释换成类型的；重声明的类型名先撞上组件 |
| `rx-ut` | 3 | extends 重声明 + 丢 final（单测原文） |
| `ec-e1` | 22 | extends 修饰改继承来的复合组件：与声明修饰叠加、final、多层继承、实例修饰再叠、数组 each、记录、protected |
| `ec-e2` | 28 | extends / 实例修饰叠到复合组件：改 final 元素（叶也算）、`final c(…)`、各层作用域、复合数组切分、整体绑定 + 属性、短类 |
| `ec-e3` | 20 | 多层修饰各带作用域：三层实例修饰、结构参数、条件组件、嵌套记录、each、内层 final 被外层改 |
| `ec-e4` | 19 | 顶层 / 包里短类的修饰在全局 / 包作用域解析（含结构参数） |
| `ec-ut` | 3 | 修饰各层带作用域 + 改 final 元素（单测原文） |
| `rf-r1` | 24 | extends 修饰里的重声明形态：constrainedby、条件、维、前缀合并与冲突、final、多层、先被 extends 修饰改过 |
| `rf-r2` | 18 | 重声明形态补充：未重声明时的 constrainedby、多层带内层 constrainedby、输出 / 流前缀、维 + 绑定、each、条件连接器 |
| `rf-r3` | 16 | 重声明形态：作复合组件的类型、外层实例修饰再叠、final 冲突、多层带维、protected、Evaluate 注解随原声明 |
| `rf-ev` | 12 | 外层组件的 Evaluate = true 传给子树里的参数（parentEval） |
| `rf-rb` | 6 | 记录整体绑定拆出的字段绑定不是修饰层：final 字段不触发 final 冲突 |
| `rf-ut` | 4 | 重声明形态 + parentEval + 前缀冲突 / 重复修饰（单测原文） |
| `fv-f1` | 12 | 函数变量的类型是短类链 / 带属性修饰：属性的打印顺序、final 不打、局部、Integer 短类、枚举形参（未做） |
| `fv-f2` | 12 | 函数变量属性：数组铺开（已知维 / 未知维 / 二维）、each、引用别的形参、输出 start、局部绑定、Boolean / String 短类 |
| `fv-f3` | 10 | 函数变量属性：表达式维的 fill、二维未知维、多层短类 + 声明修饰、属性名不对（omc 报错）、数组值属性 |
| `fv-ut` | 2 | 函数变量属性（单测原文） |
| `fc-c1` | 8 | 函数体里的推导式 / 归约：运行时范围保留、array(…) 写法、min / max / product、局部绑定里的推导式 |
| `fc-c2` | 10 | 函数体归约：字面量范围展开、多迭代变量嵌套、嵌套推导式、外层 for 变量作范围、数组字面量范围 |
| `fc-c3` | 12 | 函数体归约补充：min / max 字面量范围、空范围、单元素、步长、Integer 体、if 体、下标式 |
| `fc-c4` | 6 | 推导式的维：Integer 体转 Real 时整体转型打印的表达式维（getRangeTypeInt） |
| `fc-ut` | 2 | 函数体归约 / 推导式（单测原文） |
| `fc-ev` | 6 | 用户函数的编译期求值：函数体里的推导式 / 归约式 `sum(3 * i for i in 1:5)`、多输出调用（testsuite ForIterator1 的拆件） |
| `ue-u1` | 12 | 用户枚举参数的结构求值：Evaluate、if 条件、修饰改值、包里的枚举、Integer()、别名、初始方程、维、属性、无绑定、常量 |
| `ue-u2` | 10 | 用户枚举结构求值：MSL 式 Init / LimiterHomotopy、模型里定义的枚举（未做）、extends 改值、枚举数组（未做）、记录字段 |
| `ue-u3` | 4 | 枚举字面量经 import / 全名：类型路径规范成导入目标的全名 |
| `ue-ut` | 2 | 用户枚举结构参数（单测原文） |
| `sm-s1` | 6 | 结构标记：未定型绑定里 size(a, k) 的实参、if 表达式没选中的那支、矩阵构造套 size（1.25.0 怪癖，SKIP） |
| `sm-s2` | 6 | 结构标记：size 在哪儿会连带标记它的实参（矩阵构造 vs 普通实参 / if 条件） |
| `sm-s3` | 8 | 结构标记：if 表达式没选中的那支（维 / elseif / Evaluate / if 方程条件 / 绑定链 / for 范围） |
| `xb-x1` | 11 | 长类型 extends 内建类型（Icons.TypeReal 一类）：属性、后续短类、Integer / Boolean / String、数组、函数变量 |
| `xb-x2` | 10 | 短类带维（`type Axis = Real[3](…)`）：each / 切分的类型修饰、组件再加维、二维、长类型作底、非字面量维（未做） |
| `xb-x3` | 5 | 函数变量的类型带维：each 照标量、标量类型修饰按维铺开、数组类型修饰照原样 |
| `xb-x4` | 7 | 长类型 extends 带修饰的短类（1.25.0 丢修饰，SKIP）/ extends 只 extends 的长类型（照留）/ type 里有 protected（omc 报错） |
| `xb-ut` | 2 | extends 内建类型 + 类型维（单测原文） |
| `rp-p1` | 12 | 结构求值里的记录字段参数（if 条件 / 维 / for 范围 / 绑定里引用 `r.a`、嵌套记录、Evaluate = false） |
| `rp-p2` | 8 | 结构求值顺着参数绑定链：记录字段的整体绑定 / 修饰绑定、只标被引用的本身、维、`q = a.p` |
| `rp-p3` | 3 | 更像 MSL 电机：嵌套记录字段 + 记录整体绑定 + final 修饰 + 带 final 派生字段的记录 |
| `rp-p4` | 4 | 记录整体绑定盖掉字段修饰的绑定：final 留在字段上、each 的绑定一并盖掉 |
| `rp-p5` | 10 | if 条件里的参数声明在后（向前引用）：修饰 / 记录 / 嵌套实例 / Evaluate = false / fixed = false（含声明在前的对照） |
| `rp-p6` | 9 | isExpressionNotFixed 经记录字段：fixed = false / Evaluate = false 在绑定链上、记录引用查记录本身 |
| `rp-p7` | 8 | 求值副作用：声明在后的参数被结构求值求过 → 打求得的字面量；声明在前的照旧 |
| `rp-p8` | 4 | 非结构条件之后再引用同一参数（不留结构标记、不代入） |
| `rp-p9` | 5 | if 方程以外的方程段结构求值（for 范围 / if 表达式 / 维 / Evaluate）碰到声明在后的参数 |
| `rp-pa` | 13 | 记录构造整体绑定查整个调用、嵌套记录 / 记录数组整体绑定、Evaluate = false 的记录、撤销求值副作用 |
| `rp-ut` | 4 | 参数绑定链的结构求值 / 求值副作用 / 不固定条件（单测原文） |
| `lh-l1` | 7 | 枚举字面量的类型名在外层包里（if 式 / if 方程 / Evaluate / 修饰 / 变量 / 更深的包 / 继承来的方程） |
| `lh-l2` | 10 | 同上换位置：Integer()、修饰值、when、记录缺省、for 枚举范围、根类在包里、外层模型里的枚举（SKIP）、函数、数组 |
| `pd-d1` | 12 | when 里被赋值的 Real 在 when 外 pre / change（方程先后、跨实例、数组元素、算法 when、if / for 里的 when、整体赋值的记录） |
| `lh-ut` | 2 | 外层包里的枚举类型 + when 赋值变量的 pre（单测原文） |
| `cs-c1` | 12 | 复合引用穿过子实例里参数维的数组（`p.pin.v`、`p.pin[2].v`、`q.p.pin.v`、sum / 方程里、声明在后的子实例） |
| `cs-c2` | 8 | 同上：参差的实例数组（SKIP）、实例数组整体、子实例修饰里的引用、维随声明在后的参数、数组运算、connect（SKIP） |
| `ev-e1` | 4 | Evaluate = true 的参数顺记录整体绑定链求值（记录参数声明在前 / 在后、外层实例里） |
| `rt-t1` | 5 | 记录引用整体绑定到另一个记录类型（字段次序不同 / 相同、参数值、变量记录）：按字段引用拆 |
| `rt-t2` | 5 | 同上，含记录数组字段（参数维 / 字面量维、嵌套实例里再绑） |
| `rt-t3` | 4 | final 修饰的记录引用绑定到字段次序不同的另一个记录类型（标量记录：照常拆） |
| `rt-t4` | 4 | final + 次序不同 + 记录数组字段：omc 求值成字面量（SKIP）；嵌套标量记录 / 不 final / 次序一致照常拆 |
| `cs-ut` | 2 | 参数维数组的复合引用 + Evaluate 顺向前的记录绑定（单测原文） |
| `ce-q1` | 6 | 元素是数组的推导式（参数数组 / 参数维 / 常量维 / 子模型里的矩阵乘向量 / 三维 / 变量数组 SKIP） |
| `ce-q2` | 7 | 编译期求值的超越函数（sin … log10、atan2、^）与 UCRT 逐位一致 + 推导式的形状（嵌套推导式 SKIP、形状不符报错） |
| `ce-ut` | 2 | 超越函数折叠 + 元素是数组的推导式（单测原文） |
| `pf-f1` | 4 | 参数数组绑到结构实参的用户函数调用（Evaluate / 维带来的结构参数、再参与矩阵 / 推导式） |
| `pf-f2` | 7 | 参数绑用户函数调用：字面量 / 非结构 / 结构实参，标量与数组，表达式里的数组调用（SKIP），变量数组 |
| `pf-f3` | 4 | 实参不是结构参数的整调用绑定（omc 不标量化，SKIP）/ 表达式里（SKIP）/ Evaluate / 维带来的结构参数 |
| `pf-ut` | 1 | 参数绑到结构实参的用户函数调用（单测原文） |
| `ac-a1` | 8 | 参数数组绑定里嵌在运算中的数组值调用（取负 / 乘参数 / 结构与非结构实参 / 子模型 / 相减；方程里的 SKIP） |
| `ac-a2` | 5 | 同上：取负、乘常数、减字面量数组，结构实参整体求值后的零元素 -0.0，两次取负 |
| `ac-a3` | 6 | 取负的零：`-(0.0)`、字面量数组取负（零元素 -0.0，全零数组原样）、乘参数、方程里乘 time |
| `ac-ut` | 2 | 数组值调用在参数数组绑定里 + 取负的零（单测原文） |
| `sp-s1` | 7 | 连接器里的子连接器数组 `Pin pin[m]`：整体 connect、子模型间、单个元素对 Pin、实例修饰的维、Plug 数组 |
| `sp-s2` | 16 | 同上：两对不同维、维不符（都拒）、二维 / 成员带维、Bus 里的 Plug 数组、stream / 空数组（SKIP）、链式、pin 整体对 Pin 数组、切片、300 元素 |
| `fd-d1` | 5 | 函数形参的维只引用函数常量 / 包常量：打成字面量维；引用输入的照留 |
| `fd-d2` | 3 | 同上：extends 图标函数、import 常量、作为修饰绑定的调用 |
| `st-d3` | 3 | 记录参数里 Evaluate 的 final 字段：整体绑定前后声明、GcRef 代入 |
| `st-d4` | 3 | Evaluate 链经记录整体绑定走到声明在后的记录：别名连带的 d.scp.m、mm.scp.m 代入 |
| `st-d5` | 4 | 外层整体绑定盖过内层字段修饰（final / 非 final），Evaluate 与否 |
| `st-t1` | 16 | Evaluate 链上的别名连带：标量、括号、final / constant 目标、子实例、链式 |
| `st-t2` | 14 | 同上：记录整体绑定、两个实例、方程段 / 维 才标的目标、Real / Boolean / 枚举 / 数组 |
| `st-t3` | 9 | 记录字段求值取哪个绑定：外层整体绑定盖过字段修饰（同层 / 跨层），维里的求值 |
| `st-t4` | 6 | Evaluate 求值碰到声明在后的参数：值照用、绑定不存回（d.scp.m 照打 d.m） |
| `st-t5` | 7 | 同上，对照维的求值（存回 `s.k = 2`）与 Evaluate 的（`s.k = j + 1`） |
| `sp-ut` | 3 | 子连接器数组 connect + 函数常量维 + Evaluate 不存回向前的绑定（单测原文） |
| `lx-v1` | 8 | 继承来的声明在基类作用域查名：派生类同名参数遮不住包常量、基类引用派生类才有的名字（都拒）、方程段、extends 修饰 |
| `lx-v2` | 10 | 同上：多层继承（中间类在别的包）、类重声明（SKIP）、属性 / start / 嵌套组件修饰、维与 fill |
| `lx-v3` | 20 | 同上：多层 extends 修饰（start / 绑定 / 嵌套）、函数、枚举、记录构造、数组常量、条件、组件实例 / 子模型、each start、维、连接器、constant |
| `lx-v4` | 16 | 同上：重声明、实例修饰、短类修饰、记录字段修饰、组件类型、中间类参数、fixed、派生类在后声明、数组维被引用、protected |
| `lx-ut` | 4 | 继承来的声明在基类作用域查名（单测原文） |
| `vo-u1` | 20 | Evaluate 的结构判定按访问序：Evaluate = false 在前 / 在后、链上、维、子实例、if 式、等式段 |
| `vo-u2` | 23 | 同上：maxDepth = 4（三层求值、四层不求，常量 / 先访问的 Evaluate 截断）、fixed = false、无绑定 / start、数组、函数、子实例修饰、外层 Evaluate |
| `vo-u3` | 18 | 同上：extends 的访问序、记录字段（修饰 / 整体绑定 / fixed）、记录自己的 Evaluate = false、子实例、if 式、size 越维 |
| `vo-u4` | 15 | 同上：不纯函数、纯函数实参、size(x, 1)、两支之一走满四层、维连带后的 Evaluate、子实例修饰链、fixed = true |
| `vo-r1` | 14 | 记录整体绑定链上的 Evaluate：引用链三层求值 / 四层不求、构造实参、字段修饰链、无绑定记录 |
| `vo-r2` | 7 | 外层组件 Evaluate（模型 / 记录）与 Evaluate = false 的次序、维与 Evaluate 混合 |
| `vo-fx` | 7 | 带 fixed 属性（true / false / 常量 / 多属性）的参数在 Evaluate 链上都不固定；维照常连带 |
| `vo-pd` | 2 | 61t 留下的两个：Evaluate = false 声明在后（链中段 / 末端） |
| `vo-ut` | 5 | Evaluate 的访问序 / 深度 / fixed 属性（单测原文） |
| `xo-e1` | 16 | 外部对象变量：final 丢掉、protected、默认 / 命名构造实参、传给外部函数、子实例 / 记录里、别名、参数实参、input、注释、等式、包里的类 |
| `xo-e2` | 4 | 空的 if 方程（各分支都空）整条丢掉 |
| `xo-e3` | 5 | if 方程 / if 式的条件参数顺绑定到外部函数调用：定型期认外部函数 → 不固定，条件照留 |
| `xo-e4` | 16 | 外部对象的非法用法：算术、比较、传给 Real 形参、实参 / 绑定类型不符、两种外部对象相等、der、修饰、多余 / 命名不符的构造实参、if 式绑定 |
| `xo-ut` | 3 | 外部对象变量与构造 / 析构、连接、非法元素（单测原文） |
| `du-d1` | 20 | 继承来的同名元素：绑定不同 / 整数与实数字面量相等、类型不同、变率 / 属性 / 维 / 注释 / 条件 / 方向 / 可见性不比、本地在前、实例修饰、记录类型不同、组件与类同名、连接器、extends 修饰改成相同 / 不同 |
| `du-ut` | 2 | 继承来的同名元素去重 / 不相同报错（单测原文） |
| `sd-w1` | 7 | 子实例里参数维数组的整体引用：顶层 / 块里 / final n = n、u[:] 绑定、sum、等式 |
| `sd-w2` | 3 | 同上：维经 extends 修饰 `nout = size(k, 1)` / `= m` 一路传到连接器 |
| `sd-w3` | 3 | 引用处有同名参数 n（与子实例的维不同）：整体引用 / sum / 等式 |
| `sd-w4` | 8 | 子实例数组的下标越界（按实例的维判）、向前引用、组件数组元素、类型维、二维、两层子实例 |
| `sd-w5` | 6 | 向量化内建 / 用户函数在等式、绑定里（两元素保留 array(…) 形态） |
| `sd-w6` | 8 | 只向量化一维长 1：收成单元素数组、逐元素标量化（内建 / 用户函数、绑定、sum、算法）；两维不收 |
| `sd-ut` | 1 | 子实例数组按自己的维（单测原文） |
| `us-s1` | 16 | 无绑定的固定参数拿 start 当绑定：标量 / Integer / fixed = true / start 是参数引用 / Evaluate / 数组 / each（SKIP）/ 子实例 / 外来修饰 / 维 / Boolean / 类型带的 start（不做）/ String / 记录字段（不做）/ 注释 / 常量 |
| `us-s2` | 6 | 同上：记录字段（直接 / 外来修饰 / 嵌套在模型里）不做、连接器与两层模型照做 |
| `us-s3` | 14 | 同上：extends 修饰的 start、fixed = false、类型带 start 的数组、记录数组字段、别的参数 / fill / 维用它、外来改 start / 改绑定、min、time 当 start（拒）、有绑定、start 链、连接器里的维 |
| `us-ut` | 3 | start 当绑定（单测原文：外来修饰、维、记录字段；顶层；Evaluate） |
| `io-ref` | 24 | 复合 outer 的引用：绑定 / 方程 / 嵌套两层 `d.c.b.a.x` / 两个使用者 / inner 是派生类 / 不相干的类（拒）/ 数组元素 / 记录 / 最近的 inner / 参数与 Evaluate / if 条件 / der / pre / 算法段 / 缺 inner（SKIP）/ outer 带修饰（拒）/ 经实例调函数（SKIP） |
| `io-type` | 8 | 同上：inner 的类与 outer 声明类型不相干也照样压平（omc 不比类型）、经 outer 访问派生类独有的元素；inner 在数组元素实例里（`C c[2]`，前缀下标照抄）、outer 维用 inner 的参数 |
| `io-conn` | 12 | 经复合 outer 的 connect：outer 连接器本身 / 两个使用者 / inner 在数组元素里 / 根上也连 / 两端都经 outer / 连接器数组元素 / for 里连；短类型 / 短类连接器的 outer；内层 inner 遮住外层；for 迭代变量当下标 |
| `io-order` | 10 | 声明次序：引用写在 outer 声明前面（复合 / 标量 / 维）、根上先引用子实例里的 outer（SKIP）；根类 extends 来的 inner、嵌套 inner |
| `io-detach` | 16 | omc 查名穿过类时把它整个实例化（instPackage），里面的 outer 沿词法外层找不到 inner 就生成：类型名 `P.M.J` / extends `M.Base` / 类里自引 `A.NestedA`（InnerOuter13）→ TOP_LEVEL_OUTER 或多出变量（SKIP）；简单名、经包、常量与函数引用不触发 |
| `io-ut` | 2 | 复合 inner/outer（单测原文：两层使用者 + if 条件里的参数；不相干的 inner 类） |
| `ea-e1` | 12 | Evaluate = true 的参数数组：字面量绑定 / 整个数组别名 / 子实例修饰 / 别的参数用它 / 当维 / each 属性 / 无绑定（拒）/ time 绑定（拒）/ Boolean 选支 / 二维 / 依赖参数连带 final / 注释 |
| `ea-e2` | 6 | 对照：不带 Evaluate 的结构参数数组（维、if 条件、引用代入）；标量 Evaluate 的函数绑定、依赖 Evaluate = false |
| `ea-e3` | 15 | 同上：数组类型带 `each final unit`（World 的 n）/ 整体引用 `g * n` / fill / zeros / 用户函数 / 外层组件 Evaluate / Integer 维 / if 表达式条件 / 依赖 Evaluate = false、fixed = false（不固定）/ each fixed（SKIP）/ sum（SKIP）/ 数组方程 / `:` 维 / String |
| `ea-e4` | 10 | 同上：记录参数 Evaluate 的数组字段 / 别名数组 / 元素引用别的数组 / 组件数组的修饰分发 / if 绑定里用 / 标量积 / 无绑定（拒）/ 只有 start / protected / 整体数组方程 |
| `ea-e6` | 19 | 同上：依赖 Evaluate = false / fixed = false 的整个数组、向前引用的参数 / 整个数组、子实例参数、for 范围、子实例维、extends 修饰、经 outer 的整个数组、identity；经子实例的整个参数数组（`b.a`，求值 / 维里 sum） |
| `ea-ut` | 4 | Evaluate 参数数组（单测原文：World 式 Axis、依赖 Evaluate = false、声明在后的别名、经子实例） |
| `cc-c1` | 15 | 条件组件的条件：字符串比较（`<>` / `==`，start 当绑定、子实例修饰）；redeclare 进来的连接器里的条件（多层 extends 修饰传值）；条件经修饰引用声明在后的记录字段（第二遍）/ 声明在后的参数 / 记录参数修饰 |
| `rm-r1` | 16 | rem 内联成 `x - div(x, y) * y`：time / 参数式 / Integer 离散 / 算法段 / 函数体 / 负数 / 嵌套；字面量折叠、Evaluate 参数、数组实参（SKIP）、关系式里、integer()、for 方程 |
| `gi-g1` | 10 | getInstanceName()：根类自己 / 子实例 / 组件数组 / 方程 / 包里的类 / 传给外部函数；多维下标（`a[1, 1]`）/ extends / 字符串拼接 / 传给用户函数求值（SKIP）/ assert 消息 |
| `ea-e8` | 4 | Evaluate 记录组件的数组字段：值来自外层记录整体绑定（`pr = F(v = …)`，omc 不 final → SKIP）/ 字段自己的修饰（final） |
| `cc-ut` | 4 | 单测原文：字符串条件、向前记录字段条件、rem 内联、getInstanceName |
| `rcf-r` | 19 | 记录构造函数与函数里的记录字段：数组字段 / extends 来的字段（两层）/ 短类型字段（不打 unit）/ 字段缺省（数组）/ 引用别的字段的维（`Real[n] b`）/ constant 字段（照样是输入）/ 混合类型；模型里没有实例的构造调用、`r.T[1, 2]`、整个数组字段、矩阵乘、全缺省的常量调用（omc 求值，SKIP）、函数返回带数组字段的记录（SKIP） |
| `rcf-t` | 21 | 函数体里的 transpose / identity：`transpose(T) * v`、`M := transpose(T)`、`v * transpose(T)`、矩阵乘、模型语境对照；identity(3) * a、identity(n)（n 是输入，调用处求值）、记录值的常量函数（SKIP）；记录类型转换 `r1 = R2(…)`（SKIP，此前崩溃）/ 不同记录类实参 / final 字段（SKIP）；短类记录类型 `record R = BaseR(x = 2)` 的变量传给函数（omc 发射 R 的构造函数，SKIP——此前静默错） |
| `rcf-ut` | 2 | 单测原文：extends + 短类型数组字段的构造、记录实例实参 + transpose / identity 展开 |
| `rv-v1` | 16 | 记录值的常量函数调用：方程里 `r = nullR()` → 构造调用（字段字面量）、参数绑定摊到字段（数组字段逐元素）、标量字段；数组字段的记录值（位置 / 命名构造、identity）；折叠路的字段属性（短类型 / 字段修饰的 unit，此前静默丢）、注释；final 字段（按构造求好的值）、Evaluate 记录组件的折叠（不 final）、嵌套记录字段的折叠 |
| `rv-s1` | 6 | 函数体里 smooth 的数组实参（if 表达式两支数组、整个向量）、cross / skew 展开（向量实参、与矩阵乘混用） |
| `rv-ie` | 7 | 记录构造缺省里两支长短不一的 if 式 / 切片 `a[1:end-1]`：单独出现 1.25.0 都接受（两者同在的 scodeinst IfExpression13 1.25.0 拒、在账）；函数输入缺省同理 |
| `rv-ut` | 2 | 单测原文：空基类 extends 的记录 + 记录值常量调用（方程 / 参数绑定）、短类型字段属性 + smooth / cross |
| `ax-a1` | 8 | 模型里嵌在运算中的数组值调用（等式 / Var 绑定整体保留）：`{1.0, 2.0, 3.0} + f(time)`、标量挪右 `f(time) * 2.0`、积不展开 `m * f(time)`、Var 绑定成方程 `x = a + f(time)`、sum / cross 里的数组调用 |
| `ax-b1` | 32 | 同上的各算符形态：`/ ./ .^ .- .+ .*` 的点号与换位、`.-` 改 `.+ -1.0`、`* 1` 不化简、Integer 运算数提实型（字面量逐叶 / 引用与调用整体 CAST）、不含数组调用的运算数展开成字面量（`a / 2` → `{a[1] / 2.0, …}`）、标量积、等式两侧 Integer / Real 整体转 |
| `ax-c1` | 33 | 内建包着数组调用（向量化 abs / sin → SKIP，cross / transpose 展开）、用户函数实参里的 `g(f(time) * 2.0)`、零元与负号化简（`f(time) + {0, 0, 0}` → `f(time)`、`-f(time) + a` → `a - f(time)`、`./` 的负号怪癖）、e - e（SKIP）、Var 绑定、der、when 体（SKIP）、if 分支、非法形态 |
| `ax-d1` | 9 | vector 作函数实参：矩阵构造 / 数组字面量实参压成向量字面量，引用实参 omc 留 `vector(x)`（SKIP） |
| `ax-e1` | 4 | 组件数组上推导式修饰按元素代入迭代变量（体是数组 / 含数组调用 / vector 字面量实参）：`s[1].r = r0 + f({…})` |
| `ax-f1` | 20 | 推导式（omc 的数组构造调用）嵌在运算里：外层 sum / product 按定型次序逐元素展开、点号与换位只是打印形态、下标取元素 / 向量化内建（SKIP） |
| `ax-g1` | 3 | 实参是结构参数的数组调用（omc 化简时先求值成字面量、等式照常标量化）：一侧整个是它 `y = so(m)` → 逐元素；嵌在运算里的 SKIP |
| `ax-ut` | 8 | 单测原文：AX1-AX3（各形态）+ 由 SKIP 改正向的 R1a / R1b / R2s / R2 / M |
| `cf-p1` | 9 | 组件里的函数调用：普通函数 / 短类别名带修饰（`w.acc(time)` → `P10.w.acc(…)`）、嵌套组件 `a.w.acc`、常量修饰、显式实参、两次调用共一个定义、参数数组修饰（Evaluate）、经 outer 调 inner 的（数组实例里）；调用名带下标（双拒）、redeclare（SKIP） |
| `cf-p2` | 8 | 同上：两个组件各一个定义、修饰引用变量（定义里原样 `w.v`）、模型内函数别名的缺省求值（`gg = 3.0`，调用处补 `g`）、多层 outer、命名实参、修饰式 `g * k`、参数绑定 / 算法段里的调用 |
| `cf-p3` | 9 | 别名 / constrainedby 的注释、无绑定参数、继承来的函数类（SKIP）、枚举类型的函数输入（标量 / 数组、缺省、返回枚举的常量调用） |
| `cf-q1` | 6 | 函数输入的短类型属性数组、枚举类型的函数变量与函数体里的枚举字面量、组件函数（CF1 / CF2）、函数体引用派生类型的包常量（QT1，`Modelica.Constants.T_zero` 那种）、MultiBody World 的复刻（`g * normalizeWithAssert(n)`，n 是 Evaluate 的参数数组，未做 → SKIP） |
| `cf-ut` | 4 | 单测原文：组件函数 + inner/outer + 变量缺省、模型内别名缺省求值 + 参数数组、枚举函数变量、派生类型包常量 |
| `sk-h1` | 11 | 结构参数的绑定里矩阵构造套 size(a, k)：a 一并成结构参数（KinematicPTP2 式的 `nout = max([size(deltaq, 1); …])`，修饰给 / 默认 / 引用别的参数、二维、非 final 的 n）；a 的 `:` 维由 `2:size(table, 2)` 这类绑定推（CombiTimeTable 的 columns）；a 是变量 / 向前引用（SKIP） |
| `sk-ut` | 3 | 单测原文：KinematicPTP2 式（修饰给的 deltaq）、columns = 2:size(table, 2)、数组声明在 size() 引用处之后 |
| `wr-w1` | 8 | 结构参数数组（size-in-matrix 连带 / Evaluate）的整体引用：用户函数 / 外部函数实参、数组运算、调用嵌在运算里、sum 的实参（SKIP）、Var 绑定、`:` 维由区间绑定推出的 columns |
| `wr-v1` | 12 | Evaluate 参数数组 n 的整体引用：等式 / 绑定里 `nrm(n)`、`nrm(n) * time`、`g * nrm(n)`（代值求值后逐元素 / 展开）、标量积、sum（SKIP）、外部函数实参 `fe({0.0, -1.0, 0.0})`、参数数组别名、参数绑定里的调用 |
| `wr-w2` | 4 | `:` 维由 `2:size(table, 2)` 推出的数组：外部函数实参、在 nout 之前 / 之后定型、Integer 数组绑定 |
| `wr-m1` | 8 | 结构参数代入后打印标记跟着走（`x = k * f(time)` → `f(time) * 2.0`、`.- k` → `.+ -2.0`）、函数体 if 分支里的 zeros / fill 展开、APS2（整体引用代成 `{1, 2}`）、外部对象的向前引用（CombiTimeTable 的 tableID 式）、`:` 维数组的向前引用（size 里 / 等式里，Var 绑定里的 SKIP）、整体代入后 min / max 求值（`max(k) * time` → `2.0 * time`） |
| `wr-ut` | 4 | 单测原文 |
| `ej-e1` | 4 | 枚举属性值是式子：参数引用（`start = y0`、外层 `m.y0`）、if 式、包常量（`start = P.c0` → `P.E.b`）、extends 修饰里的；值是 Integer / 别的枚举类型（omc Type mismatch，SKIP） |
| `ej-w1` | 12 | when 条件里的 initial()（checkWhenInitial）：标量条件里嵌着的告警（SKIP）；条件向量有一个元素是 initial() 本身或不含 initial() 就不告警（`{initial(), change(x) and not initial()}`）、全都嵌着的告警；算法 when 同样 |
| `ej-i1` | 7 | 初始算法给参数赋值（fixed = false / 有绑定的、数组参数整体 / 元素）；普通算法里的参数目标、常量目标、when 外的 discrete Real（omc 报错）；when 外的 Integer / Boolean 目标 |
| `ej-c1` | 6 | 连接器里的参数 / 常量记录成员按字段拆：各字段相等断言（嵌套记录、Integer 字段、三方连接、连接器数组 + each final）；Var 记录里的参数字段 |
| `ej-f1` | 3 | fixed 被外来修饰 / extends 修饰盖过时，类里 `fixed = T0fixed` 的 T0fixed 不标 final；外层参数给的 fixed 照标 |
| `ej-a1` | 5 | 组件数组各实例的算法段（目标 `s[1].x :=`）：for 体元素目标、整数组目标、嵌套组件数组、when 目标的连续绑定（omc 报错）、初始算法的参数目标 |
| `ej-k1` | 17 | 枚举维常量表的运行时下标：`t[a, b]` → `{{…}, …}[a, b]`、已知前导下标取元素（`t[L.X, b]` → `{2, 3, 4, 1}[b]`）、已知尾随下标照写（`[a, L.X]`）、整数维混排、包里的枚举类型 / import 别名 / 连接器短类的输入（MSL Digital 的 delayTable 形态）；Boolean 维、参数下标（SKIP） |
| `ej-ut` | 10 | 单测原文 |
| `dk-e1` | 2 | 枚举数组的属性值按元素摊分：`start = fill(L.U, n)`、`fill(p, n)`（p 是枚举参数）、each |
| `dk-q1` | 5 | 包里的枚举值真值表：矩阵 / 花括号 / 3 维 `[{{{…}}}]` 写法、import 别名 `T.OrTable[a, b]`、已知前导下标取元素、参数下标求值标 final、嵌套的表取元素照写；下标是别的枚举类型（omc 报错） |
| `dk-p1` | 2 | 变量 / 参数数组的枚举维上的运行时下标照写（`ResetMap[reset]`、`w[pre(reset)]`）；`Integer(pre(x))`、运行时 if 式的 Integer() 照写 |
| `dk-s1` | 4 | 短类连接器（`connector DigitalInput = input DigitalSignal`）数组整体连接：两层短类、partial block 的 MISO、类型路径首段是 import 别名（`D.Interfaces.DigitalInput x[n]`） |
| `dk-r1` | 2 | 枚举区间常量数组 `LogicValues[:] = L.U:L.O` 的 `:` 维、带运行时整数下标的取元素（`delay` 补第三实参）；参数枚举区间（SKIP） |
| `dk-ut` | 5 | 单测原文 |
| `ck-k1` | 11 | Clock 类型的变量与短类连接器（`connector ClockInput = input Clock`、三方连接、连接器里的 Clock 成员）、`sample(u, clock)` 的钟是组件引用、Clock 两实参构造第二实参求值存回参数（沿链）、没选中的 if 分支里的存回与同步算子计数实参的结构标记、`parameter Clock` |
| `ck-ut` | 4 | 单测原文 |
| `ct-t1` | 5 | 参数数组 if 绑定逐元素摊开后不收拢（`if b then 1.0 else 1.0`、`if b then true else false`）、矩阵里的推导式列（BooleanTable 的 `[table[1], 1.0; table, {mod(i + 1, 2.0) for i in 1:n}]`）、子组件修饰里 + 条件参数向前引用、写出来的内层 if 照常收拢、选中支引用条件参数（SKIP） |
| `ct-ut` | 2 | 单测原文 |
| `oc-p1` | 29 | 超定连接图：root / branch 摘除、环里压平序最先的连接断开出 equalityConstraint 方程（函数按 lhs 实例路径起名）、潜在根按优先级 + cref 字节序、isRoot / rooted（BFS 距离、最后一个 branch 定对端）、纯连接环 / 重复连接不断、结构 if / for 里的、非结构 if 里留裸名、Real[0] 输出消失；标量输出 / 类型超定 / 裸 rooted / 绑定里的（SKIP） |
| `oc-f1` | 60 | 超定连接图的随机模型（与 omc 差分核实过的 1040 个里取 60 个）：世界 / 潜在根 / 关节 / 三端关节 / 重复连接 |
| `ft-t1` | 2 | 函数体里的元组赋值 `(a, , c) := g(x)`：空位 `_`、数组输出目标、用前未赋值检查认元组目标 |
| `oc-ut` | 3 | 单测原文 |
| `pa-p1` | 14 | 参数数组整个绑定是实参求不成字面量的用户函数调用：不标量化、一行 `parameter Real[3] e(…) = f(n)`（组件数组里的、Integer / Boolean 输出、2 维）；带 min / max / nominal 的照逐元素 `f(n)[k]`；数组属性值整个打（引用 / if 原样、算术式展开成字面量）；要强转元素类型的属性值（SKIP）。单测取 1 / 4 / 7c |
| `op-p1` | 15 | outerProduct 留符号形态 `promote(a, 2) * transpose(promote(b, 2))`：函数体、等式 / Var 绑定、全字面量实参的 promote / transpose 求值、Integer 引用整体转型、外面的运算（标量换位、矩阵积、取负、除）、全零运算数（标量积折零、矩阵积不折）、常量调用求值；参数数组绑定 / 结构参数代入后成字面量 / Integer 运算实参（SKIP）。单测取 1 / 4d / 7f / 4b |
| `da-p1` | 10 | derivative 注解的修饰 order / noDerivative / zeroDerivative（不打印、引用的函数照样发射，含 MultiBody resolve2 式的记录输入与 cross 里的函数调用）；名字不是输入 / 未知属性 / order 写成式子或 Real（SKIP）。单测取 3 / 4 / FAS2 |
| `qe-p1` | 11 | 类注释里的转义随类级 annotation 走：没有 annotation 的长类存源码原文、打印再转义一遍（`\"` → `\\\"`、`\n` → `\\n`），有 annotation（空的、方程段里的）才解转义（`\n` 成真换行）；元素上的 annotation 不算。单测取 WCS4 / Wk / Wj |
| `vx-p1` | 6 | 向量化调用的实参是不含数组调用的运算式（`to_unit1(r - r_shape)`）：先按元素展开成字面量再取 `[$i1]`，参数数组绑定逐元素摊开；2 维、多实参、短类型 Axis 的参数 / input；Integer 元素（omc 整个取元素式转型）/ 含数组调用（SKIP）。单测取 1 / 5 |
| `te-p1` | 12 | 元组方程 / 函数体元组赋值的目标是整个数组的引用、空数组目标打 `_`、空数组实参打 `{}`（含函数局部，Blocks Filter 的 `(cr, _, _) := toHighestPowerOne(den1, {})`）；多输出调用的每个输出都求维、维式参数标结构；带属性修饰的空数组整体不出、属性形状 / each 不符（omc 报错，SKIP）；无绑定的空数组参数照核 fixed / start（U1–U5）。单测取 TE1 / TE2 / TE3 / EA1 |
| `rb-p1` | 112 | 记录类型组件带整体绑定（研究子任务 rp62r：116 个探针 + SPEC 16 条规则，去掉 omc 崩溃的 IN8 与要装 MSL 的 MS*）：构造调用 / 记录引用 / if 选支、final 参数记录的强制内联与放弃（多条语句、有局部、调用处顶层实参是内建调用）、tryEvalExp 成字面量与写回、求不出挪成初始方程（数组字段 fixed = false）、结构参数实参求值、Evaluate = false / external（SKIP）。单测取 BR6 / IA2 / SE1 / BR8 / FD4 / BR5 |
| `at-p1` | 9 | 数组变量属性值里的数组调用逐元素取 `f(p)[k]`（`2 * f(p)`、`f(p) + {…}`，参数 / 变量）；数组值的 fixed 结构求值后取元素、参数连带 final；数组 if 的分支留不住形态时，条件只读结构参数的照值选支（选中调用支成整体数组参数），非结构条件 SKIP；条件组件的条件经子实例 / outer 引用非字面量绑定的参数（顺绑定链求值、连带标结构）；参数数组的调用绑定实参是结构参数数组的照常逐元素求值。单测取 AT1 / AT2 / IB1 / IB2、CC4 / CC5 / RE1 |
| `ve-p1` | 41 | Evaluate / 结构参数数组的绑定是向量化调用（`to_unit1(r - r_shape)`、`f(a)`、`f(a, b)`、二维、带缺省实参）或书写的推导式：结构求值逐元素求出字面量；函数是否照打看 omc 压平变量的绑定是否还是推导式（Evaluate / 别的 Evaluate 参数读到 → 照打；维 / 条件在定型期求值过 → 不打；嵌在运算 / 外层调用里的被化简掉 → 不打）；压平期（if 方程条件、for 范围）才标的元素绑定照旧是符号式、只加 final；标量 / 非向量化的数组调用对照；记录绑定经 from_nxy 式函数（A 支）。单测取 VE8 / VE25 / VE23 / VE39 |
| `ad-p1` | 6 | 函数别名修饰给的缺省（`function acc = std(g = g * n, mu = mu)`，MultiBody World 的重力函数）引用的参数声明在调用者之后（向前引用，第二遍用第一遍的值）/ 之前；缺省里调用户函数（`g * nrm(n)`）；枚举输入；被引用参数的绑定非字面量（AD5 / AD6：omc 不存回组件，ymc 数组元素的非字面量绑定未做 → SKIP）。单测取 AD1 |
| `lt-p1` | 3 | 不标量化的数组参数（绑定是整个用户函数调用）上类型链派生、不带 each 的属性按元素抬成同形数组（SI 类型的 quantity / unit / displayUnit），组件上带 each 的打一个值；带 min / nominal 的照旧逐元素。单测取 LT2 |
| `dg-p1` | 16 | diagonal(v)：作用户函数实参、整个数组的等式 / 绑定 / 矩阵积里留调用（实参照化简后的形态：字面量叶统一类型、算术式逐元素展开 `{(m * 3.0 - k) / 12.0, …}`），实参全是字面量（含结构参数代入后）的求值成矩阵；参数数组摊分、sum 里逐元素取（对角 / 0.0）。单测取 DG6 / DG9 |
| `zs-p1` | 6 | 结构参数代入后数组运算的一侧成了全零字面量（`ta + rot(R, tb) - cross(r, f)`、r = 0）：照 simplifyBinarySub 消掉；推导式绑定的 Evaluate 数组参数在下标数组各元素相同时（`a = {0, 0, 0}`）调用当场求值、函数不打（VK3 / VK4），否则照打（VK1 / VK2）。单测取 ZS2 / VK3 |
| `ra-p1` | 3 | 参数数组是结构参数数组的裸 cref 别名（`Inner i(n = n)`、外层 n 带 Evaluate；`n = m`）：调用绑定 `e = nrm(n)` 求值成字面量、逐元素（MSL Rotor1D 的 `rotorWith3DEffects.e`）；非结构的照旧整体数组变量。单测取 RA1 |
| `ab-p1` | 12 | 别的数组内建作函数实参 / 在整个数组的运算里（transpose / identity 展开、vector 的非字面量实参 SKIP、symmetric、skew、cross、fill、outerProduct、`diagonal(a) + m`、`m * a * time`）对照 |
| `sx-p1` | 4 | 参数数组绑定是用户函数调用、实参当时折不成字面量（`nrm(cross(e, nnx))`、`nrm(2 * a)`）：本参数自己带 Evaluate（已是结构参数）→ 求值成字面量、逐元素；不带的照旧整体数组变量；矩阵构造 `[ex, ey] * fc` 里引用。单测取 SP4 |
| `fw-p1` | 3 | 条件组件的条件经 outer 读到声明在后的 world 参数（`sphereDiameter = world.defaultBodyDiameter = world.nominalLength / 9`），子实例里 / 修饰了 world：第二遍用第一遍的值，依赖一起标结构参数。单测取 CF1 |
| `sv-p1` | 2 | stateSelect 属性的参数引用被外来修饰盖掉（`inertia1(phi.stateSelect = StateSelect.always, …)`、嵌套 `d(j(phi(stateSelect = StateSelect.never)))`）：声明里的 `stateSelect = stateSelect` 不用、参数不标 final（testsuite mosfiles/Model2） |
| `rs-p1` | 10 | 短类修饰里的组件重声明（`record SalientInductance = Salient(redeclare SI.Inductance d, …)`，MSL FundamentalWave.Types / SI.Complex*）：施加到实例化出的记录的同名字段上，新类型的属性先打、原声明 / 重声明的修饰照留；同一短类修饰里同名的另一个子修饰 omc 报错。单测取 RS7 / RS8 |
| `or-p1` | 149 | operator record（MSL Complex 及其别名 SI.Complex* / ComplexInput / ComplexOutput）的全部 rp62x 探针：字段注释 / 属性、`Complex(…)` → `Complex.'constructor'.fromReal(…)`（常量实参求值成记录式）、记录类型的函数缓存（默认构造函数 + 'constructor'，别名按别名路径）、绑定求值 / 挪成方程、短类连接器别名的 connect；运算符重载、强制内联等仍 SKIP |
| `or-ut` | 4 | operator record 构造 / 函数缓存 / 连接器别名（单测原文，本地 operator record） |
| `oo-p1` | 7 | operator record 的边角：括号里的 if 记录绑定（MSL ComplexSISO 的 `uInternal = (if … then conj(u) else u)`）、写在声明之前的别名运算数（IdealACDCConverter 的 `vQS = pin_pQS.v - pin_nQS.v`）、常量构造绑定带字段修饰 / 别名类型、`-(-i1)`、模型里 operator record 常量的整体引用（`constant Complex j`） |
| `oo-ut` | 4 | 记录运算数的重载运算符（单测原文，本地 operator record）：精确匹配、隐式构造（`C(2.0, 0.0)` 求值、Integer 转型）、别名路径 `P.CV.'*'.multiply`、`-(-a)`、局部常量 |
| `fi-ut` | 1 | final parameter 记录绑定的强制内联（单测原文，本地 operator record + 单语句函数）：'constructor' 取输出的字段修饰（`re = re`）、`k * cexp(C(0, phi))` 内联化简成 `k * cos(phi)`、`p * p` → `p.re * p.re - p.im * p.im`，不留函数块 |
| `fi-p1` | 6 | 强制内联放行 FundamentalWave 电机之后暴露的两处：结构参数代入后算法段里的 `String(m)` 照常求值（FS2；方程里的 assert 消息不化简）；记录整体绑定写在数据记录之前、字段经别名链到 Evaluate 参数（`aimc(statorCoreParameters = aimcData.statorCoreParameters)`，T2；T1 / FM 是对照）。单测取 FS2 |
| `va-p1` | 4 | 记录数组的声明类型是带修饰的短类（SI.ComplexVoltage、`record S = R0(a(start = 3))`、`operator record CX = Complex(re(start = 1))`）：每个元素原样照收短类修饰；记录数组作记录形参的实参照样向量化（`abs(v)` → `array(Modelica.ComplexMath.abs(v[$i1]) for $i1 in 1:2)`）；连接器数组的字段 `p.v` 作实参仍 SKIP（VA4） |
| `va-ut` | 2 | 带修饰短类的记录数组（VA2）、记录数组作重载运算符的运算数（ORU10：`z1 + z2`、`k * z1`、`z1 * r` 逐元素）（单测原文） |
| `ry-p1` | 11 | 记录数组续：整体绑定是推导式 / fill（MSL ComplexMISO 的 `uInternal`、Sum 的 `k = fill(Complex(1, 0), nin)`）逐元素取；有元素拆不开（`conj(u[2])`、`a .* b`）→ 全部元素挪成方程、记录 cref 元素写成字段记录式（RY1 `ComplexInput(s.u[1].re, s.u[1].im)`）；向量 * 向量精确匹配 `Complex.'*'.scalarProduct`（数组字面量运算数 `{a[1], a[2]} * b` 同）；`.*` 逐元素；展开的连接器数组字段作运算数；组件声明修饰里只换类型的成员重声明（MSL 多相传感器 RY5、RY10）。RY8 / RY9 引用外层参数的重声明 SKIP，RY11 改 protected 成员两边都拒 |
| `ry-ut` | 2 | 记录数组整体绑定挪成逐元素方程 + scalarProduct + `.*`（RYU1）、组件声明修饰里只换类型的成员重声明（RYU2）（单测原文） |
| `rz-p1` | 15 | 函数里的记录（数组）：记录数组元素字段 `u[k].re` 与 `sum({…for k in 1:m})` → `sum(… for k in 1:m)`（MSL quasiRMS）；operator record 短类别名形参（`input Modelica.Units.SI.ComplexVoltage[:] v`，别名构造 / 类型函数随之打、运算符按别名命名，RZ4-RZ6）；推导式实参在函数体里向量化（MSL activePower RZ3、RZ9）；记录数组整体绑定的复合路径（`p.v`、`plug_p.pin.i`）与向前引用的 plug（RZ7 / RZ8）；返回记录数组的函数求值后拆字段（RZ10 多相电磁转换器、RZ11 / RZ12）、常量实参的记录函数元素（RZ13）、范围运算数与输出维里的调用（RZ14）、数组取负的 -0.0（RZ15） |
| `rz-ut` | 2 | 函数的记录形参（RZU1，本地 operator record + 短类别名 `P.CV`）、返回记录数组的函数求值（RZU2）（单测原文） |
| `xc-p1` | 15 | 外部 C 字符串函数（ModelicaStrings_substring / ModelicaStrings_length）的常量求值：常量绑定求、参数绑定照留调用（impure）；endIndex 越过串尾照 omc 取到串尾（XC3 / XC6）、-999、转义 / UTF-8 字节长度（XC4）；向前引用的常量（XC10 / XC11，MSL PartialBasicTransformer 的 `R1(start = …C1…)`）；经组件向前引用的常量内联（XC13 / XC15）；MSL Yd01 变压器（XC9） |
| `xc-ut` | 1 | 外部 C 字符串函数的常量求值（单测原文，本地声明的外部函数 `impure function substr`） |
| `rq-p1` | 6 | 记录类 `extends Base(field = …)` 的缺省：构造调用求值（RQ1 / RQ2 MSL FluidHeatFlow.Media.Water）、多层 extends + 属性子修饰 + 引用别的字段的缺省（RQ4）、带实参的构造（RQ6）；派生记录传给基类形参（RQ3）、extends 里的 final 修饰（RQ5）SKIP |
| `rq-ut` | 1 | 派生记录的构造函数（单测原文）：`input Real rho = 995.6`、多层 extends、构造调用求值与缺省补齐 |
| `tv-p1` | 8 | 函数体里的元素式内建向量化（MSL ToSpacePhasor）：一维局部数组上的 `cos(phi)` → `array(cos(phi[$i1]) for $i1 in 1:m)`（书写的维 / `size(x, 1)` / Integer 元素转型，TV1 / TV2 / TV7）、数组字面量体的推导式、标量乘行长未知 / 已知的数组字面量（TV5 / TV6）、`2 / m * x` 换位（TV3）、作调用输出维实参的 parameter 局部打 `final parameter`（TV4）；运算式实参的向量化（TV8）SKIP |
| `tv-ut` | 1 | 本地 ToSpacePhasor 复刻（单测原文）：`cos(+phi)` 向量化、数组字面量体的推导式、`{…} * 2.0 / /*Real*/(m)`、`final parameter` |
| `db-p1` | 14 | 被字面量条件剪掉的 if 分支里的越界下标：for 展开后的复合连接器数组 `inPort[i-1].occupied`（MSL StateGraph，DB1 / DB2）、Real 数组 / 书写的 `x[0]` / if 方程 / 常量数组 / elseif 链 / 中间段 / 数组值的 if（DB3–DB9）；活的支越界（DB10 SKIP、DB11 双拒）、剪掉的支里类型 / 名字错（DB12 / DB13 双拒）、条件靠下标才成结构参数（DB14 SKIP） |
| `db-ut` | 1 | 剪掉的 if 分支里的越界下标（单测原文）：复合连接器数组的 `inPort[i - 1].occupied`、Real 数组的 elseif 链 |
| `ip-p1` | 17 | 纯函数里"顶层"调 impure → omc 后标 impure（无返回值调用 / 赋值右侧整个是调用 / for 体 / while 条件 / 局部与输入缺省的绑定 / 元组赋值，传递且与声明先后无关）；嵌在运算里、被 Integer → Real 转型包住的不算，被求值函数体里嵌着的 impure 调用照常求值；常量绑定里顶层的 impure 调用（IPA / IPD）SKIP |
| `tw-p1` | 7 | when 体里的元组方程（保留、数组目标、elsewhen、空位 `_`、常量实参拆开）与模型语境；ModelicaStrings_hashString 的就地求值、折出来的 getInstanceName() 作实参（`automaticLocalSeed(getInstanceName())`） |
| `ip-ut` | 1 | 纯函数后标 impure（单测原文）：`impure function f`、常量实参调用照留、嵌着的 h 照常求值、when 里的元组方程 |
| `fr-p1` | 6 | 派生记录的构造调用求值、基类在别的包里（MSL FluxTubes 材料记录）：基类字段的缺省 / 短类类型（SI 属性）/ 数组、缺省在基类作用域里求、extends 修饰与构造实参（FRB1–FRB4）；非常量构造给基类型记录组件按字段拆开（FRB5 / FRB6）SKIP |
| `fr-ut` | 1 | 派生记录的构造调用求值、基类在别的包里（单测原文）：包常量 k、`B = 2 * mu_i`、extends 修饰 |
| `cr-p1` | 15 | 整个常量记录的引用作实参 → 记录式（无绑定 / 带绑定，CRL3 / CRL4）、字段引用（CRL1）；记录构造函数的外来基类字段（SI 类型、start 属性，CRL15）；Evaluate = true 的记录参数连带结构参数（CRL6）、只带字段修饰（CRL8）；final 记录的函数绑定求值（CRL7 / CRL10 / CRL11）、标量照留（CRL9）；读到的全固定的 final 记录绑定求值成字面量字段（CRL12–CRL14，63j 起）；模型里局部记录类作函数形参类型 SKIP（CRL2 / CRL5） |
| `cr-ut` | 1 | 整个常量记录的引用作实参 + Evaluate = true 的 final 记录参数由函数求值、x0 连带 final（单测原文） |
| `ws-p1` | 16 | 整个记录参数的编译期求值与结构记录：final 记录绑定里的记录参数实参（WS1–WS3）、Evaluate 记录绑定读整个记录 / 常量记录（WS4–WS7）、带 start 的字段才是结构参数（WS8–WS10、WS15 / WS16）、整体引用代成记录式（WS12 结构语境读过的、WS14 自己 Evaluate 且有整体绑定的）、记录引用整体绑定求值（WS13）；强制内联（WS11）SKIP |
| `ws-p2` | 6 | 函数体：字面量条件的 if 语句化简（WS17 / WS18）、只 extends 不加字段的派生记录实参（WS19；加字段 omc 报错 WS20）、元组赋值给记录字段（WS21）、模型里没有实例的输出记录类（WS22） |
| `eg-p1` | 16 | Evaluate = true 的记录参数 × 字段声明网格：整体绑定下只有带 start 的字段是结构参数，只带字段修饰的都是 |
| `ws-ut` | 1 | 整个记录参数作实参的编译期求值（单测原文）：元组赋值给记录字段、死的 if 语句、带 start 字段代入、mc 字段不 final |
| `il-p1` | 22 | 继承来的类 / 元素的查找：继承来的嵌套类作组件类型（IL1、多层 IL14、`A.B` IL16）、继承来的函数 / 常量 / 短类型（IL2，压平名 = 实例路径；根类继承 IL18）、同名继承类先到先得（IL13）、本地类盖过继承类（IL21）、改了非常量的 extends（IL15）、未改的外层常量（IL11 / IL12）、继承记录（IL20）；经改了基类常量的 extends 继承来的类（IL3 / IL7）、本类有元素级重声明时继承来的类（IL22）、外层实例上被改过的常量被嵌套类 / 模型内函数读到（IL5 / IL6 / IL9 / IL10，此前静默错）、经包路径访问的继承函数 / 枚举（IL4 / IL19）、继承函数体里调兄弟函数（IL17）SKIP；读外层 parameter（IL8）双拒 |
| `il-ut` | 2 | 继承来的类 / 函数 / 常量 / 短类型的查找（单测原文）：`ILU1.a.f`；外层实例上被改过的常量被嵌套类读到 SKIP（ILU2） |
| `sa-p1` | 20 | 根类是短类别名：`model R = A` / 带修饰与注释（SA1 / SA2）、两层别名链（SA3、经包里的别名 SA19）、connector 别名记录（SA4）、修饰引包常量（SA5）、类里的函数名取别名根 `SA6.f`（SA6）、注解（SA7）、结构参数（SA8）、block（SA10）、final 修饰（SA13）、getInstanceName（SA16）、记录字段修饰（SA17）、继承 + protected（SA20）、连接（SA26）；partial 别名 / 基类（SA9 / SA11）、重复修饰（SA22）、`each` 修饰（SA25）双拒；别名里的 redeclare（SA21）SKIP |
| `sa-ut` | 1 | 根类是短类别名（单测原文）：两层别名链的修饰（外层在前、final）、别名的注释 |
| `gn-p1` | 13 | 找不到 inner 的 outer：根类末尾生成同名 inner（GN1；带 extends GN2 / GN12、同名 outer 共用第一个 GN3、内建类型 GN4、连接器 + connect GN7、组件数组 GN8、missingInnerMessage GN9、部分子树有 inner GN10、两个生成的 inner 次序与生成次序相反 GN11、经 protected extends 继承的 outer GN13）；无值 parameter（GN5）、根类里已有同名元素（GN6）双拒 |
| `gn-ut` | 2 | 找不到 inner 的 outer 生成 inner（单测原文）：内建类型的 outer（GNU0）、两个生成的 inner 的次序（GNU1） |
| `cb-p1` | 12 | 没被重声明的 replaceable 元素的约束子句修饰：外来 > 声明 > 约束（CB1–CB3）、写在类里的参数（CB5）、继承来的（CB6）、复合 / 记录 / 数组组件（CB4 / CB10 / CB9）、2.x 的 `extends A(…)` 写法（CB8）、重声明后原约束修饰照样施加（CB11）、自带约束子句的重声明（CB12）；final 约束修饰被外来修饰改（CB7）双拒 |
| `cb-ut` | 1 | 约束子句修饰（单测原文）：外来 > 声明 > 约束；自带约束子句的重声明 |
| `kr-p1` | 22 | 派生类里的元素级类重声明：长 / 短 / 带修饰的新类（KR1 / KR2 / KR8）、多层基类（KR3 / KR23）、新类引派生类的常量（KR4）、不是 replaceable 的也照换（KR6）、派生类自己也用（KR7）、带修饰 / extends 修饰的继承组件（KR12 / KR17 / KR28）、两层重声明（KR14）、type（KR16）、根类（KR18）、两个（KR19）、组件数组（KR21）、派生类的嵌套类（KR22）、同一基类另有不重声明的实例（KR27）；基类的嵌套类体写到被换名（KR5 / KR26）、经 SCode 链查到被换的函数 / 记录（KR10 / KR11）SKIP |
| `kr-ut` | 1 | 派生类里的元素级类重声明（单测原文）：两层之下换成带修饰的短类，同一基类另有不重声明的实例 |
| `km-p1` | 18 | 修饰里的类重声明：extends 修饰（KM1；新类修饰引写修饰的类的参数 KM5）、组件声明修饰（KM2；被换类是基类继承来的 KM6；组件数组 / 数组组件 each KM7 / KM8；长形态 KM9；包里的新类 KM10；修饰引外层参数 KM11、嵌套实例里 KM12；与被换组件的修饰并用 KM17；约束子句 KM20）、短类修饰（KM3；包里的别名 KM15）、根类别名（KM14）；外层修饰里的（KM4 `d(a(redeclare …))`）、经组件数组且新类修饰引外层名（KM16）、经 SCode 链查到被换的函数（KM18）SKIP |
| `km-ut` | 1 | 修饰里的类重声明（单测原文）：extends 修饰（引写修饰的类的参数）、组件声明修饰、短类修饰 |
| `kx-p1` | 11 | class extends：新类隐式 extends 继承来的同名原类（原类元素在前）顶替基类实例里的原类（KX1）；class extends 的修饰施加到原类元素（KX2）；不带 redeclare 前缀（KX3）；目标经中间基类继承来（KX4）；原类自己有 extends（KX5）；派生类自己也用（KX6）；两层 class extends 串起来（KX8）；新增参数（KX7）；记录的（KX10）SKIP；函数的（KX9，两段算法 omc 报错）、找不到目标（KX11）双拒 |
| `kx-ut` | 1 | class extends（单测原文）：目标经中间基类继承来、class extends 的修饰施加到原类元素、派生类自己也用 |
| `iy-p1` | 26 | inner outer：对子层是 inner（子层 outer 连它自己）、经它查名（本层 / 从外面）落到外层 inner（IY1-IY4、IY11）；连接（IY21，inner 是别的类）、继承来的（IY23）、数组（IY24 / IY25）、constant 两头内联（IY26）、复合带方程（IY27）；外来修饰（IY8）；生成外层 inner（IY7）；根类里的 TOP_LEVEL_OUTER（IY6）；没有子层 outer 连它 omc 内部错误（IY5 / IY12 / IY13）；前缀漏进惰性实例化的 inner（IY14 / IY19 SKIP，IY15 / IY20 不漏）；经它求值的结构参数（复合的 IY10 照常求，标量的 IY9 SKIP）；纯 outer 连 constant 内联（IY16）、连嵌套 protected inner（IY17 / IY18） |
| `iy-ut` | 2 | inner outer（单测原文）：本层 / 从外面经它查名落到外层 inner、子层 outer 连它自己；constant 两头内联 |
| `el-p1` | 36 | 元素级组件重声明：留在本类的位置（ELR1 / ELR12）；原声明的修饰、改它的 extends 修饰不带，只带绑定（ELR2 / ELR4 / ELR15，对照修饰里的 ELR3）；外来修饰（ELR5）；条件 / 注释 / 维 / 前缀取原声明的（ELR6 / ELR7 / ELR13 / ELR14）；非 replaceable 同类型（ELR8）；复合（ELR10 / ELR17）、经中间基类（ELR11）、protected（ELR16 / ELR23）、自己的 constrainedby 修饰不并（ELR18）、条件引基类参数（ELR19）、再套修饰重声明（ELR21）、变率合并（ELR22）、带方程（ELR24）；omc 报错的：无继承元素（ELR9）、重声明的类的段里 / 从外面引用它（ELR20、ELRQ1-ELRQ8，绑定 ELRQ3 与基类的段 ELRQ4 可以）；取自原声明的条件 / 维在原声明处求（ELRS1-ELRS4） |
| `el-ut` | 2 | 元素级组件重声明（单测原文）：位置、只带绑定；原声明的条件在原处求（元素级与修饰里的两种） |
| `bx-p1` | 54 | MSL Batteries 一路：Integer 参数数组作下标（BX2 / BX5-BX7）；`:` 维向前引用、绑定引用已发射数组（BX8-BX19）；枚举参数经记录字段求值（BX20-BX28，模型里定义的枚举 BX22 SKIP）；整体绑定记录里字段的 Evaluate = true 只看 start（BX29 / BX30）；记录构造的记录类有继承来的字段（BX31-BX41）；复合数组修饰分发经记录数组中间段（BX42-BX44）；if 方程条件里继承来的记录字段不算固定（BX45-BX54） |
| `bx-ut` | 3 | 记录绑定求值规则（单测原文）：Evaluate 字段只看 start、参数数组作下标、继承来的字段照留 if 方程 |
| `dz-p1` | 24 | 组件数组修饰分发的结构参数别名连带：原式是 cref 才连带（整体 / 切片 / 括号 / 记录字段 / 两层分发 / 数组参数，DZ1 / DZ8 / DZ9 / DZ16 / DZ18 / DZ20）、数组字面量 / 负号 / 常量 / Evaluate = false 不连带（DZ3 / DZ10 / DZ11 / DZ15）、each（DZ4 / DZ17）、时刻按共享节点的定型序（DZ5-DZ7 / DZ19 / DZ21）、别名的别名（DZ12）、extends 修饰（DZ13）、MSL JointSSP 形态（DZ14）；负零：书写的 `-0.0` 不护零、混排数组取负、Real 目标的 `-{0, 1}`（DZ22-DZ24） |
| `dz-ut` | 3 | 分发别名连带与负零（单测原文） |
| `mx-p1` | 53 | TransferFunction 一路：矩阵构造的 0 行 / 0 列块只占维（MZ1-MZ13，含 MSL TransferFunction 单个 / 数组）；零长切片——方程整条不出、sum / 点积折 0.0、空范围不查越界（ESL1-ESL9）；组件数组里的数组方程与整条保留的数组调用方程（AP1-AP3）；向前引用的数组常量、elseif 链里形状不相容 / 同形的内层 if 由外层选支后按结构参数定（CL，CL13 / CL14 / CL24 omc 打怪式子、CL4 模型里定义的枚举 SKIP）；被移除的条件组件里维用到的参数经修饰绑定连带外面（CR1-CR3、MultiTerminalBox TB1-TB3） |
| `mx-ut` | 5 | 同上（单测原文） |
| `vb-p1` | 25 | 函数体里的向量化调用：整体 `u[$i1]` / 切片 `u[(1:n)[$i1]]` / `:` 位 `m[1,$i1]`，范围取实参那一维的尺寸式（已知 → 字面量、声明维式、切片长度 `max(size(u, 1) - k + 1, 0)`、未知 → `size(u, 1)`），二维嵌套、多实参、sum 里（VB1-VB15，VB7 模型侧维式 / VB10 嵌套向量化 / VB11 运算式实参 SKIP）；FFT：输出维取输入值、when 算法里元组赋值的整体数组目标（VFF）；函数输入缺省 `fill(0.0, 0)` → `{}`（VFL1，二维 VFL2 SKIP）；scodeinst FuncVectorization5（VFV5） |
| `vb-ut` | 4 | 同上（单测原文） |
| `vc-p1` | 11 | 模型语境向量化调用的范围切片实参：`y = array(g(x[(2:3)[$i1]]) for $i1 in 1:2)`（整条保留，VSL1 / VSL2）、带步长 `(1:2:3)[$i1]`（VSL5）、结构参数界代入 `(1:2)[$i1]`（VSL6）、二维 `m[(1:2)[$i1],$i2]`（VSL7）、运算里 `array(…) * 2.0`（VSL8）、sum 里逐项 `g(x[2]) + g(x[3])`（VSL9）；der / pre 的切片（VSL3 / VSL10）、previous 的切片在 cat 里逐元素（VSL4，MSL Clocked FIR）；书写成整段范围的切片照原范围 `w[(1:3)[$i1]]`（VSL11，函数体） |
| `vc-ut` | 4 | 同上（单测原文） |
| `ks-p1` | 5 | 常量数组在符号下标后跟 `:`：等式 / if 分支里逐元素 `b[1] = {{true, false}, …}[k + 1, 1]`（CSL1 / CSL2，MSL SVPWM）、绑定随运算展开（CSL3）、算法里整条留着是 omc 内部错（CSL4，ymc SKIP）；常量矩阵 `[…]` 绑定逐元素照打声明注释（CM1，此前丢注释） |
| `ks-ut` | 3 | 同上（单测原文） |
| `pk-p1` | 10 | ADR-007 第 2 阶段（A 实例树查找）的目标探针族，随 65a–65f 逐个转 PASS：Medium 式包重声明——组件类型 `Medium.BaseProperties`（PK1 / PK9）、常量 `Medium.nX` 与函数 `PK3.v.Medium.g`（实例路径命名，PK3）、包里继承来的类型 / 记录（PK4 / PK5）、两层 `redeclare package Medium = Medium`（PK6）；外层实例修饰（PK7）；继承函数的访问路径（PK8）；默认 partial 包（PK2，omc 报错）；递归短类（PKR1，omc 报错） |
| `pk-ut` | 3 | 进类查名顺短类链到底；组件修饰 / extends 修饰里的类修饰（单测原文） |
| `pk-p2` | 7 | partial 类里不许查名（lookupLocalName / lookupCrefInNode 的 isPartial 检查）：`P.PM.T x`（PKP1）、partial 模型 `A.B b`（PKP4）、表达式里的 `P.PM.c` / `P.PM.f(time)`（PKP5 / PKP6，此前 ymc 误接受）、没被重声明的 `replaceable package M = PM` 里的 `M.T` / `M.n`（PKP2 / PKP7，omc "contains the definition of a partial class"）→ omc 报错；重声明成非 partial 包的照常（PKP3，待 65a 后续步） |
| `pk-p3` | 6 | 重声明的包 / 类在实例树里（ADR-007 第 2 阶段 65a-2）：`redeclare package M = P.W` 后 `M.T` 当类型（两层 `redeclare package M = M`，PKN1）、`extends Base(redeclare package M = W)` 后派生类自己的 `M.T z`（PKN2）、嵌套模型 / 记录里用（PKN3 / PKN5）、嵌套模型的组件类型是被重声明的模型（PKN4，此前 inst SKIP）、重声明成短类别名包（PKN6） |
| `pk-p4` | 4 | 经改了常量的 extends 继承来的类当 class extends 的目标（`extends PartialMedium(nX = 2)` 后 `redeclare model extends BaseProperties`，基类里 `Real X[nX]`）：omc 取 extends 实例里的那份、X 有 2 个；ymc 此前直接用 `P.Water.BaseProperties` 时静默只出 X[1]（PKX3），现 SKIP，待包实例（65a-3）转 PASS |
| `pk-p5` | 6 | 经重声明的包查常量 / 函数（包实例，ADR-007 第 2 阶段 65a-3）：包里函数互调 `M.h2` → `M.h`（PKF1）、同一模型两个不同 Medium 各出一套函数 `PKF2.a.M.h` / `PKF2.b.M.h`（PKF2）、常量链 `nXi = nX - 1` 与嵌套包 `M.Sub.m` 当维（PKF3）、包函数调别的包的函数（PKF4）、记录类型输出 `PKF5.v.M.R`（PKF5，66c 起记录构造按实例路径命名 → PASS）、两层传递后当参数绑定（PKF6） |
| `pk-p6` | 5 | 类修饰（ADR-007 第 2 阶段 65a-4）：extends 修饰里改嵌套类型 `extends PM(Temperature(min = 273.15, …))`——组件修饰 > 类修饰 > 类型定义的修饰（PKC1）、直接用 `P.W.Temperature`（PKC2）、改过的包常量与类修饰一起落到记录字段（PKC3）、两层 extends 的类修饰叠加（PKC4）、修饰里引用包常量 `start = Tstart * 2`（PKC5） |
| `pk-p7` | 2 | 短类链修饰在类型定义处求值：包里 `type T = Real(min = Tmin)` 取包常量 5，模型自己另有 `Tmin = 7` 时此前静默打成 7.0（PKD1）、模型里没有同名的（PKD2，此前 SKIP） |
| `pk-p8` | 2 | 查名穿过重声明的包时扫它子树里的 outer（lookup_detaches_outer）：包里常量的类型不下钻（MLS §4.7）——`constant FC fc` 的记录类型经 `extends PM(redeclare replaceable record FC = …)` 继承来（PKO1）、`constant C cp_const` 的类型经 `extends PM(n = 2)` 继承来（PKO2），此前都 SKIP |
| `pk-p9` | 10 | MSL Media 包常量的长尾：`redeclare model extends BaseProperties(h(stateSelect = …))` 的 class extends 修饰也预扫 stateSelect（PKS1）；常量数组的空切片 `reference_X[1:nXi]`、nXi = 0（PKS3–PKS5）；`:` 维常量的 `size`（`nS = size(substanceNames, 1)`，绑定被 extends 修饰改过，PKS6 / PKS7 / PKS11）；String / Boolean 数组常量当值（PKS8）；空数组常量 `fill("", 0)` 按声明类型定型（PKS9 / PKS10） |
| `pk-p10` | 8 | Medium 长尾：记录常量（无绑定、按 `each` 修饰逐元素构造）的继承字段 `fluidConstants[1].molarMass`（PKM1–PKM3，`record TwoPhase extends Basic`）；String(Real) 编译期求值的指数照 Windows 版 omc 补到 3 位（PKM4，`"1e-005"`）；class extends 修饰里的 stateSelect 经 inner 参数求值、inner 声明在前 / 后（PKM5–PKM7b，MSL ThreeTanks 的 `preferredMediumStates = (if energyDynamics == …)`） |
| `pk-p11` | 6 | Medium 函数：`redeclare function extends f` 的签名先收隐式基类（PKG1 包内不限定调用、PKG2 经包限定调用）；包实例里的记录类型 / 构造函数按实例路径命名（PKG2、两个 Medium 各一份 PKG3）；函数变量类型的 extends 类修饰（PKG4）；extends 改过常量后基类函数的维 / 体（PKG5）；`redeclare record extends` 链的字段与函数体里的构造调用（PKG6） |
| `pk-p12` | 3 | class extends 记录继承来的字段按实例类树里的隐式基类解析（Instantiator::class_extends_base）：字段类型 `FixedPhase` 经带修饰的 extends 继承来（PKG7）、包常量 `h_default = specificEnthalpy_pT(…)` 编译期求值里的同一记录（PKG8），此前都撞定义层继承守卫；注解顶层的空修饰（`annotation(Evaluate, …)` 等，读注解处此前断言崩，PKG9） |
| `pk-p13` | 5 | MSL Fluid 端口数组：for 里逐个端口的方程（PKH1）；整体引用 `ports.C_outflow = fill(C, nPorts)`——C_outflow[nC] 而 nC = 0、nPorts = 0、fill 的值是空数组（PKH2）；连接器的空数组成员（含 stream）与 `assert(cardinality(ports[i]) <= 1, …)`（PKH3，不带 assert 的 PKH3b）；变量引用展开出的空数组当函数实参（`anyTrue(c.occupied)`、n = 0：omc 照留调用，PKH4） |
| `pk-p14` | 4 | MSL Fluid 端口与管道：端口数组上不带 each 的类重声明（`ports[nPorts](redeclare package Medium = Medium, m_flow(each start = 0))`，PKH5）；元素类型是 class extends 的复合数组 `Medium.BaseProperties[n] mediums`（PKH6）；空数组的字面量切片引用 `ports_mXi_flow[i, :]`、空 stream 数组成员当 actualStream / inStream 实参、`m_flow * actualStream(h)` 包 `smooth(0, …)`（PKH7）；actualStream 乘积的各种写法——调用在前、非关联的 cref、非 cref 因子、负号、flow 方向已知（PKH8） |
