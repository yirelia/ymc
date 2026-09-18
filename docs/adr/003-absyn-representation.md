# ADR-003 Absyn 的表示：每类节点一个 Arena，胖 struct + Kind，不存括号

- 日期：2026-09-16
- 状态：已接受

## 背景
S1 要把 Modelica 源码变成一棵可反解析、可回读、可被 S2/S3 遍历的语法树。要在 C++ 里做到
"无 GC、无野指针、调试器里看得清、切换 case 漏分支能被编译器抓住"（方案 §3.4/§3.5）。

## 决定
1. **每类节点一个 `Arena<T>`，节点间用 `Id<T>` 互指**：`Exp/Cref/Modification/Element/Equation/
   Statement/ClassDef` 各自一个 arena（`Absyn` 拥有全部）。复合构造（if/for/when 方程与语句、嵌套类、
   修饰里的 redeclare 元素）用 `std::vector<Id<T>>` 嵌套，值类型里绝不递归包含自身。
2. **表达式是胖 struct + `ExpKind` + `switch`**（方案 §4.3）：字面量、`cref`、`op`、`a/b/c`、`args`
   各 kind 用自己那几个字段；不用 `std::variant`、不用继承。`-Werror=switch` 让每个遍历都穷尽。
3. ~~**不存源码括号**：`(a + b) * c` 的括号不入树；反解析按优先级补最少括号。~~ round-trip 的验收是
   "parse→unparse→parse→unparse 幂等"，不是"与原文 byte 相同"。
   **（2026-09-17 被 ADR-004 取代：括号、Real 字面量拼写、字符串原文都入树，反解析逐字对齐 OMC 的 Absyn dump。
   下面"依据 3"里"OMC 的 Dump.mo 按优先级重建括号"的说法是错的——`AbsynDumpTpl.tpl:857-864` 的加括号逻辑被注释掉了。）**
4. **命名实参用与 `args` 平行的 `arg_names`**（无效 Symbol = 位置实参），不造"命名实参"节点。
5. **矩阵的行复用 `Array` 节点**（`Matrix.args` 的每个元素是一个 `Array`），反解析时不打花括号。
6. **元组空位用空 `Tuple` 节点占位**（`(, x) := f()`），反解析时跳过。
7. **注解全部保留**为 `Id<Modification>`（类、组件、方程、语句、extends、import、枚举项、约束子句）。
8. **语义决定"以 OMC 实际接受的文法为准"**（见 `docs/s1-文法对照表.md` 末节）。

## 依据
- 1/2：方案 §3.4 第 1–2 条、§4.1、ADR-002（arena+Id 的所有权模型，引用失效实验）。递归值类型
  （`struct X { std::vector<X> kids; }`）在 MSVC/GCC 上虽可编译，但复制成本与调试可读性差；Id 嵌套
  一律 4 字节。
- 3：OMC 的 `Dump.mo` 同样按优先级重建括号；testsuite 的 `// Result:` 文本也是 OMC 反解析的产物，
  不保留原括号。幂等扫描（`tools/parse_sweep.py --roundtrip`）在 flattening 2628 + MSL 2508 文件上
  0 失败。`^` 不可链式（MLS `factor = primary [^ primary]`）是一次实测踩坑（`operators/Pow.mo`）。
- 4/5/6：都是"最小可反解析表示"，避免为了少数构造加节点类型；MLS §10.4（矩阵）、§12.4.1（命名实参）、
  §11.2.1.1（元组空位）。
- 7：S2 的 `uses`/版本解析、`Evaluate`、`derivative`、`Inline` 等都在注解里；S1 丢掉就要再解析一次。
- 8：方案 §3.6 三来源之一是"OMC 函数/文法"；验收对象是 OMC testsuite，MLS 字面与 OMC 冲突时以 OMC 为准。

## 后果
- 得到：确定的所有权与稳定 Id；穷尽 switch；小节点；注解可用；反解析幂等可验收。
- 放弃：无法逐字节复原源码（括号、空白、注释、段落顺序）；`Exp` 是胖结构（每个节点约 100 字节，
  含三个 vector）——S3 压平后的表达式量级下若成为内存热点，再考虑按 kind 拆分或用 `std::span`。
- 何时重看：S3 需要在树上做大规模改写（标量化、常量折叠）时，若 `Id` 嵌套的间接访问成为性能瓶颈；
  或需要保留源码格式（LSP/格式化工具）时。
