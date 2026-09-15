# ymc — 自研 Modelica 编译器（C++）项目说明

## 这是什么
用 C++20 自研的 Modelica 编译器。对标 Dymola，参考实现是 OMC（源码在 `C:\dev\OpenModelica`，新前端 `NFFrontEnd/` + 新后端 `NBackEnd/`，基线提交 `3ceb0c9d06`），验收对象是 OMC `testsuite/`。

## 分工（硬约束，来自用户）
- **核心代码由用户手写**：`src/` 下除 `base/assert.h` 与 `driver/` 外的一切（解析、实例化、后端算法、代码生成）、`runtime/`。Claude **不写**这些，只给：任务卡（接口 + 步骤 + 坑位）、测试、构建/基建、dump/diff 工具、探针模型、算法讲解、评审。
- 用户 C++ 基础弱，正在干中学：任务卡要有大量指导，评审要解释"为什么"。
- 评审清单：来源标注（MLS §/OMC 文件:行/探针）齐全？方案 §3.4 编码 15 条、§3.5 可调试 12 条逐条？不变量断言？注释写的是"为什么"？两套编译器零警告？ASan 绿？

## 不臆想
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

## 跑 OMC 做 oracle 的坑
- 安装版 `omc`（1.25.0，在 PATH）的新后端无指标约简；工作区版 `C:\dev\OpenModelica\build_cmake\install_cmake\bin\omc.exe`（= 源码 HEAD）对 index-3 + StateSelect 模型会无限循环并写 GB 级输出，**必须带超时**。
- 项目前缀统一为 `ymc` / `YMC_`；OMC 自己的 `MMC_` 是 MetaModelica Compiler，别混。
