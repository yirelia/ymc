# 任务卡 T-S3-08：表达式后的下标只能跟在括号式后面

**执行者**：用户（手写核心：`src/parse/parser.cpp`）
**验收者**：Claude
**归属阶段**：S3（小卡，改动约 5 行）

---

## 0. 一句话目标
`{a, b, c}[2]`、`array(a, b)[1]`、`f(x)[1]`、`[1, 2; 3, 4][1, 2]` 在 omc 1.25.0 是**语法错**，ymc 的解析器却收下了
（ACCEPTS-INVALID，59w 的 ES 探针撞出来的）。只有带括号的 `({a, b, c})[2]` 才合法。

## 1. 依据（引用前重新 grep 行号）
- OMC 语法 `OMCompiler/Parser/Modelica.g`（v1.25.0 标签）`primary` 规则：
  - `:1800` `| LPAR el=output_expression_list[&tupleExpressionIsTuple] subs=array_subscripts?` —— **只有**这一支带可选下标，
    生成 `Absyn.SUBSCRIPTED_EXP(el, subs)`；
  - `:1803-1809` 括号里是元组（`(a, b)[1]`）时报 `Tuple expression can not be subscripted.`（`:1805`），否则 `:1809` 造节点；
  - `:1820` `LBRACK matrix_expression_list RBRACK`、`:1821` `LBRACE for_or_expression_list RBRACE`、`:1797`
    `component_reference__function_call` 三支后面都**没有** `array_subscripts`。
- 探针（omc 1.25.0 实跑，2026-10-02）：
  ```modelica
  model P1  Real a = time;  Real y = {a, 1, 2}[2];  end P1;          // Error: Missing token: SEMICOLON
  model P2  Real a = time;  Real y = array(a, 1)[1];  end P2;        // 同上
  model P3  Real a = time;  Real y = ({a, 1, 2})[2];  end P3;        // 合法 → Real y = 1.0;
  ```
  omc 的报错位置在 `[` 处（`[file:行:列-行:列:writable] Error: Missing token: SEMICOLON`）：声明 / 语句到 `}` 或 `)` 就
  结束了，下一个记号本该是 `;`。

## 2. 现状（改动锚点）
`src/parse/parser.cpp` 的 `parse_primary` 里有 6 处调 `parse_postfix`（吃掉后面的 `[...]` 链）：

| 行 | 所在分支 | omc 的行为 | 要不要留 |
|---|---|---|---|
| `:1172` | 空元组 `()` | 元组不能取下标 | 去掉 |
| `:1187` | 元组 `(a, b)` | `Tuple expression can not be subscripted.` | 去掉（或报这句错） |
| `:1192` | 括号式 `(e)` 后面跟 `[` | `SUBSCRIPTED_EXP` | **留** |
| `:1206` | 数组构造 `{…}` | 语法错 | 去掉 |
| `:1222` | 矩阵 `[…]` | 语法错 | 去掉 |
| `:1257` | 函数调用 `f(…)` | 语法错 | 去掉 |

`parse_postfix`（`:1264`）本身不用动。它是个 `while`——`(e)[1][2]` 这种连着两段下标 omc 也不收（语法里 `array_subscripts?`
只有一次），可以顺手把 `while` 改成 `if`，不改也只是多收一种没人写的形态。

## 3. 步骤
1. 把上表里"去掉"的 5 处 `return parse_postfix(mk(std::move(e)));` 改成 `return mk(std::move(e));`（变量名各处不同：`e` / `m`）。
2. 不需要自己报错：`[` 留在记号流里，外层（声明 / 语句的解析）下一步 `expect(TokenKind::Semicolon, …)` 自然会在 `[` 处
   报"缺分号"——和 omc 的 `Missing token: SEMICOLON` 是同一个位置。
3. （可选）`:1187` 元组后面若紧跟 `[`，报 `Tuple expression can not be subscripted.`，与 omc 文案一致。

## 4. 坑位
- **别动 `:1192`**：`(e)[subs]` 是合法的，而且这一支故意不包 Tuple（注释里写了原因：OMC 把 `(e)` 解析成 `TUPLE({e})`，
  带下标时则是 `SUBSCRIPTED_EXP(e, subs)`）。
- cref 自己的下标（`x[1]`、`a.b[2].c`）在 `parse_cref` 里，不经过 `parse_postfix`，不受影响。
- 调用结果的字段访问 `f(i).y`（`:1255` 的 `call_suffix`）是另一回事，omc 接受，保持原样。
- flatten 那边的 `subscripted_exp`（`src/flat/flatten.cpp`）已经只处理"基是 cref / 数组构造子"两种，改完解析器后它收到的
  永远是括号式的内容，不用跟着改。

## 5. 验收
- 上面 P1 / P2：ymc 报错退出（与 omc 同为拒绝，BOTH-REJECT）；P3 输出与 omc 逐字节一致。
- `ctest --preset gcc-debug`（解析器单测 + flatten 单测 `flatten_subscripted_and_runtime_subscripts`）全绿；
  `python tools/flat_sweep.py --cases-from docs/research/s3-easy-slice.json --oracle-live` FAIL 0（SubscriptedExp1/3/4 都是括号写法）；
  `python tools/nf_live_sweep.py --all --dir C:/dev/OpenModelica/testsuite` 的 ACCEPTS-INVALID 不增、PASS 不减。
- 做完后把 P1–P3 收进 `tests/probes/`（Claude 来做）。
