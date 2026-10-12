# ymc — 自研 Modelica 编译器（C++）项目说明

## 这是什么
用 C++20 自研的 Modelica 编译器。对标 Dymola，参考实现是 OMC（源码在 `C:\dev\OpenModelica`，新前端 `NFFrontEnd/` + 新后端 `NBackEnd/`，基线提交 `3ceb0c9d06`），验收对象是 OMC `testsuite/`。

## 分工（硬约束，来自用户）
- **核心代码由用户手写**：`src/` 下除 `base/assert.h` 与 `driver/` 外的一切（解析、实例化、后端算法、代码生成）、`runtime/`。Claude **不写**这些，只给：任务卡（接口 + 步骤 + 坑位）、测试、构建/基建、dump/diff 工具、探针模型、算法讲解、评审。
- 用户 C++ 基础弱，正在干中学：任务卡要有大量指导，评审要解释"为什么"。
- 评审清单：来源标注（MLS §/OMC 文件:行/探针）齐全？方案 §3.4 编码 15 条、§3.5 可调试 12 条逐条？不变量断言？注释写的是"为什么"？两套编译器零警告？ASan 绿？

## 禁止臆想
任何语义结论只允许三种来源：MLS 3.6 章节号 / OMC 函数（文件:行，引用前重新 grep）/ 探针模型在 OMC 上实跑。写不出来源 → 记入 `docs/open-questions.md`，不写代码猜。

## 文档地图
- `自研编译器技术方案-C++.md`：规范（约束→原则、验收 L1/L2/L3、工程规范、S0–S8、任务卡模板、风险）
- `ymc-设计总览.html`：设计地图 + 难点账本
- `docs/omc-导读/`：OMC 源码导读 8 篇；`docs/tasks/`：任务卡；`docs/cpp/`：C++ 学习材料；`docs/adr/`：不可逆决定
- `tasks/todo.md`：当前计划；`tasks/lessons.md`：踩坑规则（每次被纠正后追加）

## 构建与验证
```
cmake --preset gcc-debug && cmake --build --preset gcc-debug && ctest --preset gcc-debug     # 默认
cmake --preset msvc && cmake --build --preset msvc-debug && ctest --preset msvc-debug        # VS 调试
cmake --preset msvc-asan && cmake --build --preset msvc-asan && ctest --preset msvc-asan     # ASan
```
新增 `.cpp`/`.h` 后要重新 configure（`file(GLOB … CONFIGURE_DEPENDS)` + `if(EXISTS)` 门控）。两套编译器都必须零警告。

## 任务收尾必须与 OMC 对拍（硬性，来自用户）
**每一个任务/加宽桶结束后，都必须把结果和 OMC 做对比**——不是凭印象说"应该对了"，而是实跑对拍。收尾清单逐条过：
1. **门扫描 + 实跑 oracle 对拍**（权威）：
   ```
   python tools/flat_sweep.py --cases-from docs/research/s3-easy-slice.json --oracle-live
   ```
   要求：`FAIL 0`（对 testsuite 文件 `// Result:`）**且** `oracle=live` 段 `不一致 0`（每个 PASS 用例与**实跑 omc 1.25.0** 逐字节一致）。
   `--oracle-live` 会把每个 PASS 再和实跑安装版 omc 1.25.0 逐字节比；只有文件 Result 与 1.25.0 本身分歧的用例（omc 自身报错/不同版本行为）才允许落在 `omc 自身报错/超时` 桶，且**必须**在 `docs/open-questions.md` 分歧账本里有记录（ymc 一律以 **1.25.0 实测**为准，不以文件为准）。
2. **横切特性对广语料**：改动若涉及 cref/表达式/折叠/数组等横切逻辑，还要 `python tools/flat_sweep.py --oracle-live`（全 scodeinst）确认**无新增真 FAIL**（curated slice 不含未做特性的坑，见 lessons）。
3. **三套编译器 + ASan**：gcc-debug / msvc-debug / msvc-asan 各 `ctest` 全绿、零警告、ASan 0 报告。
4. 新 PASS 用例加进 `docs/research/s3-easy-slice.json` 固化进门；单测期望值必须来自 oracle（探针实跑或引 MLS/OMC 行号），不能照自己实现写（否则自证，见 lessons）。

只有这四条都过，任务才算"验证完成"，才能自行提交（提交流程见用户记忆 ymc-commit-workflow）。

## 跑 OMC 做 oracle 的坑
- 安装版 `omc`（1.25.0，在 PATH）的新后端无指标约简；工作区版 `C:\dev\OpenModelica\build_cmake\install_cmake\bin\omc.exe`（= 源码 HEAD）对 index-3 + StateSelect 模型会无限循环并写 GB 级输出，**必须带超时**。
- 项目前缀统一为 `ymc` / `YMC_`；OMC 自己的 `MMC_` 是 MetaModelica Compiler，别混。
