# ymc — 自研 Modelica 编译器

文档都在本仓库：总方案 `自研编译器技术方案-C++.md`（规范）、设计地图与难点账本 `ymc-设计总览.html`（根目录）、OMC 源码导读 8 篇 `docs/omc-导读/`、任务卡 `docs/tasks/`、踩坑规则 `tasks/lessons.md`。参考实现 OMC 的源码在 `C:\dev\OpenModelica`。

## 构建

你的机器上有两套能用的工具链，两套都已验证全绿：

**A. GCC（默认，命令行 + gdb）** —— CMake 3.29 + TDM-GCC 10.3 + mingw32-make，都在 PATH：

```
cmake --preset gcc-debug            # configure（改了 CMakeLists 或新增文件后重跑）
cmake --build --preset gcc-debug    # build
ctest --preset gcc-debug            # 全部测试
ctest --preset gcc-debug -R symbol  # 只跑名字含 symbol 的
build\gcc-debug\ymc.exe --help
```

**B. MSVC（Visual Studio 2022，图形调试器 + ASan）** —— 不需要手动跑 vcvars，VS 生成器自己会找 cl.exe：

```
cmake --preset msvc                 # configure，生成 build\msvc\ymc.sln
cmake --build --preset msvc-debug   # build（Debug 配置）
ctest --preset msvc-debug
cmake --preset msvc-asan && cmake --build --preset msvc-asan && ctest --preset msvc-asan   # 带 AddressSanitizer
```

ASan 版 exe 依赖 `clang_rt.asan_dynamic-x86_64.dll`，`tests/CMakeLists.txt` 会在构建后自动把它拷到 exe 旁边，所以可以直接双击/命令行运行；不需要在 VS 开发者命令行里跑。已验证它能抓 heap-buffer-overflow 并给出源码行号。

用 VS 调试：打开 `build\msvc\ymc.sln`，解决方案资源管理器里右键 `symbol_test` → "设为启动项目"，F5。断点、监视窗口、调用栈都是图形化的，比 gdb 好上手；建议手写阶段用 VS 调，命令行阶段用 gcc 预设做快速回归。

两套的编译警告规则等价（`-Werror=switch` ↔ `/we4062` 等），任何一套有警告都算不合格。

另：OMC 自带 clang/ninja（`C:\Program Files\OpenModelica1.25.0-64bit\tools\msys\ucrt64\bin`），以后需要 clang 特有功能时可加预设。

## 目录

```
src/base/     基础设施（Symbol/Interner、Arena/Id、assert、以后的 SourceLoc/Diagnostics）
src/driver/   命令行入口
tests/        自写的 60 行测试框架 + 单元测试
docs/tasks/   任务卡（一个手写任务一张）
docs/cpp/     C++ 学习材料（按阶段）
docs/adr/     不可逆决定的记录
```

## 分工（方案 §7.2）

核心代码（`src/` 下除 `base/assert.h` 与 `driver/` 外）由你手写；构建、测试框架、任务卡、评审由 Claude 提供。

## 当前任务

1. `docs/tasks/T-S0-01-symbol-interner.md`
2. `docs/tasks/T-S0-02-arena-id.md`

先读 `docs/cpp/00-basics-for-S0.md` §1–§3。
