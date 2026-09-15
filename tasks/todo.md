# tasks/todo.md — 当前计划

对照《docs/spec/ymc-需求规格与RFC.md》。本轮 = M0 基建（Claude 实现；编译器核心由用户手写）。

## M0：三方对比基建（进行中）

- [x] `tests/omc/omc_diff.py` — omc-diff 复刻（FR-002）；单测 14 + 对拍 omc-diff.exe 10 组全一致
- [x] `tests/omc/run.py` — rtest 运行器 + 阶段门 + self-check（FR-001/003/004/050/060）
      - [x] self-check：对 OMC 自身，判定与 omc-diff.exe 26/26 一致（scoping）
- [x] `tests/omc/cflags_map.json` + `stages.json`（FR-003/050）
- [x] `tools/resultio.py` — .mat/.csv/.mat.xz 读取（FR-010）；val() 与 OMC 逐位一致
- [x] `tools/matcompare.py` — tube/classic/csvtube（FR-011/012/013）；单测 9 + 对拍 OMC diffSimulationResults 3 组一致
- [x] `tools/varnorm.py` — 变量名归一化与交集（FR-014）
- [x] `tools/structcompare.py` — checkModel/backenddaeinfo/bltdump/Base Modelica 解析（FR-015）
- [x] `tools/dymola_driver.py` — 无窗口驱动 + 翻译日志统计解析（FR-020/021）；stats 解析已单测
- [x] `tools/refgen.py` — 参考生成 + manifest + 过期（FR-030/031）
- [x] `tools/report.py` — report.md + 台账 ledger + 待办块（FR-040/041/042）
- [x] `tools/gate_engineering.py` — 工程门 G3（FR-062）
- [x] `tools/probe/*.mo` — C-PROBE 探针（Linear/Nonlinear/Event）
- [x] `docs/open-questions.md` — 待验证活文档（TR-003）
- [ ] 首次 git 提交（TR-001，**由用户做**）：S0 骨架 + 本轮基建 + Spec
- [ ] 安装 Dymola wheel 到 Python 3.11 后跑 `dymola_driver.py --smoke`（需用户执行）
- [ ] 在若干 MSL 模型上实跑 Dymola，取样 getLastError 文本，固化 stats 解析正则（TBD-02）

## 待用户拍板（阻塞后续里程碑，不阻塞 M0）

- [ ] TBD-05：ymc loadModel 是否复刻 OMC 版本回退映射（3.2.x→3.2.3、2.2.2→4.0.0）
- [ ] TBD-08：各阶段 G1/G2 通过率阈值、S7 退出阈值
- [ ] TBD-11：Dymola 语料优先级（默认 C-MSL-EX→C-MAPLIB→提取 .mo）
- [ ] TBD-12：erroneous 用例错误文本对齐深度

## S0 核心（应用户要求由 Claude 写"第一个版本"，2026-09-14）

- [x] `src/base/symbol.h` + `symbol.cpp`（T-S0-01）：Interner/Symbol；`symbol_test` 9 项全过
- [x] `src/base/arena.h`（T-S0-02）：Arena/Id；`arena_test` 10 项全过
- [x] `docs/adr/001-symbol-interning.md`、`002-arena-id.md`
- 两套编译器（gcc-debug、msvc-debug）零警告，ctest 3/3 通过（含 smoke）

## S1 第一个版本（应用户要求由 Claude 写，2026-09-15）

- [x] `src/base/loc.h`（SourceLoc）、`src/base/diag.h`（Diagnostics）
- [x] `src/parse/token.h` + `lexer.{h,cpp}`：完整 token 集 + 手写词法器（关键字/Q-IDENT/数字/字符串/注释/全部算子）
- [x] `src/absyn/absyn.h`：Absyn 树（Exp/Cref/Modification/ClassDef 用 Arena+Id；元素/方程值语义）
- [x] `src/parse/parser.{h,cpp}`：递归下降 + 分层优先级表达式；错误恢复 + 进度保证
- [x] `src/absyn/unparse.{h,cpp}`：优先级感知反解析（round-trip 幂等）
- [x] `src/driver/main.cpp`：`ymc <f>` / `--dump-after=parse` / `--check-syntax` / `--dump-tokens`
- [x] `tests/unit/parse/parser_test.cpp`：词法/结构/round-trip 幂等/优先级/connect/if/数组
- 两套编译器零警告，ctest 4/4 通过
- **v1 解析覆盖（无诊断率）**：scoping 22/26、declarations 97/100、equations 50/57、arrays 158/184、operators 47/56；无死循环
- **v1 尚不支持**（给诊断并恢复，留待 v2）：algorithm/external 段的语义、if/for/when 方程、短类定义、枚举、矩阵 `[ ]`、归约 `for`、redeclare/replaceable 元素

## 下一里程碑

- S1 v2：补 algorithm 段 + if/for/when 方程 + 矩阵 + 枚举 + 短类定义，把 flattening/modelica 全量解析零错误（方案 §5 S1 验收 ①②）。
- M3：S3 门 + 结构比对接 Dymola。
