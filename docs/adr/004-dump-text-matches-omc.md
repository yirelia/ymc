# ADR-004 Absyn / SCode 的文本 dump 逐字对齐 OMC：括号、字面量、字符串都保留源码写法

- 日期：2026-09-17
- 状态：已接受（取代 ADR-003 决定 3"不存源码括号"）

## 背景
S2 的验收对象从"自己和自己比"（S1 的 round-trip 幂等）变成"和 OMC 比"：`tools/scode_dump_compare.py` 把
`ymc --dump-after=scode --class=C` 与 OMC `list(C, exportKind=OpenModelica.Scripting.ExportKind.SCode)` 逐行 diff。
第一次抽样 40 个 MSL 类只有 24 个一致，差异全在**表达式文本**：`k * u` 对 `k*u`、`1000.0` 对 `1e3`、
`-0.0105248` 对 `-1.05248e-2`、`if C > -1e40` 对 `if (C > -1e40)`。原因是 S1 的 Absyn 丢了源码写法
（ADR-003 决定 3），反解析再按自己的规则重建。

## 决定
1. **源码括号入树**：`(e)` 解析成单元素 `Tuple`；`(e)[subs]` 解析成 `Subscript(e)`，反解析总打印 `(e)[subs]`。
2. **反解析不再按优先级增删括号**，运算符间距照 OMC：`+ - .+ .-`、关系与逻辑运算符两侧留空格，
   `* / ^ .* ./ .^` 不留，一元 `+ -` 紧贴，`not` 后一个空格。S1 的优先级表删除。
3. **Real 字面量保留源码拼写**（`Exp::sval`）；只有手工构造、没有拼写的节点才用 `real_literal_text()`
   （与 OMC `realString` 相同的最短往返规则，供 S3 以后打印求值结果）。
4. **字符串保留原文**（转义序列不解）：词法器不再解转义。只有 SCode 的**注释串**在翻译时解转义、dump 时转义回去
   （`src/base/strings.h`）。
5. **`elseif` 与 `else if` 在树里可区分**（`Exp::is_elseif`），反解析分别打印。
6. SCode dump 的版式（段顺序、`protected` 标签行、PARTS 类前后空行、空类体单行、类注解位置、隐藏的注解名单、
   不打印修饰的字符串注释、`pure/impure function`）逐条照 `SCodeDumpTpl.tpl`，并用探针输出固化成单测。
7. **三类 OMC dump 失真不复刻**，由对比工具归一化并在工具 docstring 里写明：
   多行字符串续行被模板引擎按缩进重排（改变了字符串内容）；if 表达式 ≥2 个 `elseif` 时模板在子句间插换行；
   `list(A.B)` 在 A 同时有 `extends B;` 与嵌套类 B 时返回 extends 子句。

## 依据
- 1：OMC `Parser/Modelica.g:1856-1861` 把 `(e)` 建成 `Absyn.TUPLE({e})`，`:1851` 把 `(e)[subs]` 建成 `SUBSCRIPTED_EXP`。
- 2：`Template/AbsynDumpTpl.tpl:857-864` 的 `dumpOperand` 里加括号逻辑被注释掉，只打印树里已有的括号；
  运算符文本见 `:1049-1075 dumpOperator`。
- 3：`FrontEnd/Absyn.mo:713-715`：`REAL` 存 `String`，注释写明"为了 unparse 不改变用户的显示偏好"。
  `realString`：`SimulationRuntime/c/meta/realString.c:76-84` + `3rdParty/ryu/ryu/om_format.c ryu_to_hr`（其自带测试向量已抄进 `parser_test`）。
- 4：`Parser/BaseModelica_Lexer.g:338` 的 `STRING_GUTS` 原样进 `Absyn.STRING`；`AbsynToSCode.mo:1474/1496/1502` 只对注释调
  `System.unescapedString`；`SCodeDumpTpl.tpl:918` 对注释调 `System.escapedString(cmt, false)`。
- 5：`AbsynDumpTpl.tpl` 的 `dumpIfExp` / `dumpElseIfExp`（`IFEXP.elseIfBranch` 是列表）。
- 6：探针 `docs/research/probe-r9/`、`docs/research/probe-s2/{layout,extann,emptymod}.mos` 在 omc 1.25.0 上的实跑输出。
- 7：同上探针 + MSL/Buildings 抽样；`dumpElseIfExp` 的 `separator="\n"`。
- 结果（最后一轮，每轮换随机种子）：MSL 4.0.0 抽样 200 类、MSL 3.2.3 120 类、Buildings 9.1.0 150 类全部逐行一致；
  此前各轮暴露的差异类别都已补探针并固化成单测。S1 的幂等扫描（flattening 1843 + MSL 2508）仍然 0 失败。

## 后果
- 得到：Absyn dump 与 SCode dump 都有了外部 oracle；表达式打印器只有一份，两种 dump 共用；代码比原来少（优先级表没了）。
- 放弃：**手工构造的 Absyn 树要自己负责加括号**（加一个单元素 `Tuple`），否则打印出来的文本可能回读成另一棵树。
  S3 以后的 IR 有各自的 dump，不受影响。
- 放弃：词法器不再给出解转义后的字符串值；S3 求值字符串字面量时要调 `unescape_modelica_string`。
- 何时重看：若以后需要"与源码无关的规范化输出"（格式化工具、哈希去重），届时另写一个按优先级补括号的打印器，不改这一个。
