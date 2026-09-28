# 任务卡 T-S3-03：`$overload(F, G)` 重载声明——解析 / Absyn / SCode 保留函数名表

**执行者**：用户（手写核心：`src/parse/`、`src/absyn/`、`src/scode/`）
**验收者**：Claude
**归属阶段**：S3 加宽(55)用户函数的"另列"项（flatten 侧的重载解析在本卡完成后由 Claude 接）

---

## 0. 一句话目标
`function OV = $overload(F, G);` 要在 Absyn / SCode 里保留重载函数名表 `{F, G}`。现在 ymc 把它当成短类
`OV = $overload` + 类修饰 `(F, G)` 解析，空修饰在 SCode 规范化时被丢掉，`--dump-after=scode` 打成
`function OV = $overload;`，名字表丢失，flatten 无从解析 `OV(1)`。

## 1. OMC 依据（引用前重新 grep 行号）
- 词法：`OMCompiler/Parser/BaseModelica_Lexer.g:250` —— `OVERLOAD : '$overload'; // OpenModelica extension`（单独的记号）。
- 语法：`OMCompiler/Parser/Modelica.g:401` —— `class_specifier2` 的一支 `EQUALS cs=overloading`；
  `:430-437` —— `overloading : OVERLOAD LPAR nl=name_list RPAR cmt=comment → Absyn__OVERLOAD(nl, cmt)`。
  `name_list` 是逗号分隔的**类路径**（可限定，如 `P.F`），不是修饰。
- Absyn：`Compiler/FrontEnd/Absyn.mo:168-171` —— `record OVERLOAD list<Path> functionNames; Option<Comment> comment;`。
- SCode：`Compiler/FrontEnd/AbsynToSCode.mo:476-481` —— `Absyn.OVERLOAD(pathLst, cmt) → SCode.OVERLOAD(pathLst)`，注释另转。
- 使用者（flatten 侧，本卡不做）：`NFFrontEnd/NFFunction.mo:456-466` —— `instFunction2` 的 `SCode.OVERLOAD` 分支逐个
  实例化名字表里的函数、把候选并进同一个函数缓存（调用时按实参匹配挑一个）。

## 2. 现状（改动锚点）
- `src/parse/lexer.cpp:11-12`：`$` 放宽成标识符字符，`$overload` 当普通标识符。
- `src/parse/parser.cpp:271` 附近：短类定义里已特判 `= der(f, x, y)`（偏导函数，存在 `absyn.h:282` 的 pder 字段）。
  `$overload` 照同一处特判即可。
- `src/scode/scode.h:141`：`enum class SClassDefKind { Parts, ClassExtends, Derived, Enumeration, Pder }`——缺 Overload。

## 3. 步骤
1. **解析**：在 `class_specifier2` 的 `=` 之后、走短类（derived）分支之前，若下一个记号是标识符 `$overload`
   且其后是 `(`：读 `name_list`（逗号分隔、每项一个可限定类路径，允许前导 `.`），再读 `)` 与 `comment`。
   其余照旧走 derived。
2. **Absyn**：仿 pder 在类定义上加一种 kind（如 `Overload`）+ `std::vector<Path> overload_names`；unparse 打回
   `$overload(F, G)`（round-trip 测试要过）。
3. **SCode**：`SClassDefKind::Overload` + `std::vector<SPath> overload_paths`（或复用现有路径类型）；absyn_to_scode
   原样搬名字表，注释照其它类定义转；scode_dump 打 `function OV = $overload(F, G);`（对照 OMC 的 SCodeDump）。
4. 不做：重载解析 / 候选挑选（flatten 侧，Claude 接）。

## 4. 坑位
- `$overload` 不能在词法里一律当关键字（别处 `$` 名字仍是标识符，如 `$cpuTime`），只在 `=` 后的这一处认。
- 名字表是**路径**不是修饰：`$overload(P.F, G)` 里的 `P.F` 要整段保留，不能被当成修饰名 `P` 带子修饰 `F`。
- 空修饰规范化（AbsynToSCode.mo:1714 的 NOMOD 语义）与本卡无关——改完后 `(F, G)` 不应再进修饰路径。

## 5. 测试与验收
- 单测（`tests/unit/parse/`、`tests/unit/scode/`）：`function OV = $overload(F, G);`、`$overload(P.F, .G)` 的 Absyn
  字段 + unparse round-trip；SCode kind 与名字表；scode_dump 文本。
- `ymc --dump-after=scode` 跑 testsuite `flattening/modelica/scodeinst/FuncOverloadSimple.mo` 打出
  `function OV = $overload(F, G);`。
- 三套编译器 + ASan 全绿、零警告；`python tools/flat_sweep.py --cases-from docs/research/s3-easy-slice.json
  --oracle-live` 仍 FAIL 0 / 不一致 0（本卡不应改变任何 flatten 结果）。
