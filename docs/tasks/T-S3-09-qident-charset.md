# 任务卡 T-S3-09：带引号标识符（Q-IDENT）的字符集按 OMC 词法收紧

**状态**：已完成（2026-10-05，加宽(61c)；用户暂不手写核心，由 Claude 实现并按本卡验收；**第 2 步更正**：非法转义（`\` 后不是 `'"\?abfnrtv`）omc 1.25.0 只告警 "Lexer treating \ as \\" 照收（PS 探针 L14 `'a\qb'`），不报错——实现照 omc）
**执行者**：用户（手写核心：`src/parse/lexer.cpp`）
**验收者**：Claude
**归属阶段**：S3（小卡，改动约 15 行）

---

## 0. 一句话目标
`Real 'höhe' = time;`、`model '跳ねるボール'` 这类**引号里带非 ASCII 字符**的标识符，omc 1.25.0 在词法阶段就报错，ymc 的
词法器却照单全收（ACCEPTS-INVALID；59y 放开 when 条件后，testsuite 的 `simulation/modelica/japaneselanguage/JBouncingBall.mo`
撞出来的）。flatten 里先加了一道临时挡板（`non-ASCII identifier` → SKIP），根治在词法器。

## 1. 依据（引用前重新 grep 行号）
- MLS 3.6 §2.3.1（Q-IDENT / Q-CHAR / S-ESCAPE 的定义）：`Q-CHAR` 只列了 ASCII 字符；非 ASCII 字符只能出现在字符串和
  注释里（testsuite 那个文件的头注释也写着 "Modelica does not allow quoted identifiers to be UTF-8 characters"）。
- OMC 词法 `OMCompiler/Parser/BaseModelica_Lexer.g`（v1.25.0 标签）：
  - `:430-431` `QIDENT : '\'' (QCHAR | SESCAPE) (QCHAR | SESCAPE)* '\'' ;` —— 引号里**至少一个**字符；
  - `:434-436` `QCHAR : (DIGIT | NONDIGIT | '!' | '#' | '$' | '%' | '&' | '(' | ')' | '*' | '+' | ',' | '-' | '.' | '/' | ':' |
    ';' | '<' | '>' | '=' | '?' | '@' | '[' | ']' | '^' | '{' | '}' | '|' | '~' | ' ' | '"' );`
  - `:439` `NONDIGIT : ('_' | 'a'..'z' | 'A'..'Z');`
  - `:389` `SESCAPE : '\\' ('\'' | '"' | '\\' | '?' | 'a' | 'b' | 'f' | 'n' | 'r' | 't' | 'v' …)` —— 反斜杠后只能跟这几个。
- 探针（omc 1.25.0 实跑，2026-10-02；文件要存成 UTF-8）：
  ```modelica
  model L1  Real 'höhe' = time;  end L1;                 // Error: Lexer got ''h…' but failed to recognize the rest: …
  model L2  Real höhe = time;  end L2;                   // Error: Lexer failed to recognize 'öhe = time'（ymc 已同样拒绝）
  model L3  Real 'a b!#' = time "höhe"; // höhe  end L3; // 合法：字符串、注释里可以有非 ASCII；引号里的空格 ! # 也合法
  ```

## 2. 现状（改动锚点）
`src/parse/lexer.cpp:140-154` `Lexer::lex_qident`：

```cpp
advance();  // 开头的 '
while (!at_end() && peek() != '\'') {
  if (peek() == '\\') advance();  // 转义：跳过反斜杠，下面再吃一个
  if (!at_end()) advance();
}
```

循环里对"吃进来的是什么字符"没有任何检查：任何字节（非 ASCII、制表符、换行、反引号 …）都收，反斜杠后面跟什么都行，
`''`（空）也收。

## 3. 步骤
1. 写一个文件内的小函数（放在匿名 namespace 里，和别的字符分类函数挨着）：
   ```cpp
   // Q-CHAR（BaseModelica_Lexer.g:434-436）：字母数字下划线 + 下面这串标点 + 空格 + 双引号。
   bool is_qchar(char c) {
     const auto u = static_cast<unsigned char>(c);
     return std::isalnum(u) || c == '_' || std::string_view("!#$%&()*+,-./:;<>=?@[]^{}|~ \"").find(c) != std::string_view::npos;
   }
   ```
   注意 `std::isalnum` 要传 `unsigned char`（传负的 `char` 是未定义行为——非 ASCII 字节在 `char` 有符号的平台上正是负数），
   而且它受 locale 影响；想彻底不依赖 locale 就手写 `('a' <= c && c <= 'z') || …`。
2. 循环体改成"逐个核对"：
   - 遇到 `\\`：看下一个字符在不在 `'"\\?abfnrtv` 里，在就两个一起吃掉；不在 → 报错。
   - 否则 `is_qchar(peek())` 为真就吃掉；为假 → 报错。
3. 报错用 `diags_.error(loc_from(start, sl, sc), "…")`，然后**照旧把这个字符吃掉继续扫到收尾的引号**（错误恢复：别让一个
   坏字符引出一串后续的假错误）。文案不必和 omc 逐字一致（错误输出对拍是另一个桶），位置大致对上即可：omc 对 L1 报
   `L1.mo:2:11-2:11`（坏字符 `ö` 的首字节在第 10 列）。
4. 收尾的引号吃掉之后，若引号之间一个字符都没有（`''`）→ 报错。

## 4. 坑位
- 字符串 `"…"`（`lex_string`）和注释**不要动**——那里本来就允许任意 UTF-8。
- UTF-8 的一个非 ASCII 字符是 2–4 个字节，每个字节都 ≥ 0x80。按字节报错会对一个汉字报 3 次；报第一次之后可以把后续的
  续字节（`(u & 0xC0) == 0x80`）一并跳过，只报一条。
- 词素仍然"连同两侧引号一起 intern"（`:152` 的注释），这一点不变——`'x'` 和 `x` 是两个不同的名字。
- BOM：文件开头的 `EF BB BF` 是另一回事（omc 接受带 BOM 的文件），不在本卡范围。

## 5. 验收
- L1：ymc 报错退出（与 omc 同为拒绝）；L2 照旧拒绝；L3 输出与 omc 逐字节一致。
- `ctest --preset gcc-debug` 全绿（词法器单测里若有 Q-IDENT 的用例，补两条：非 ASCII、非法转义）。
- `python tools/nf_live_sweep.py --all --dir C:/dev/OpenModelica/testsuite`：JBouncingBall 从 SKIP（`non-ASCII identifier`）变成
  BOTH-REJECT，ACCEPTS-INVALID 不增、PASS 不减。
- 做完后 flatten.cpp 里那道临时挡板（搜 `non-ASCII identifier`）由 Claude 删掉，L1–L3 收进 `tests/probes/`。
