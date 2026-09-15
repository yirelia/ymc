# Lessons

## 2026-09-14 — 项目名 mmc 撞了 OMC 的 MMC（MetaModelica Compiler）
- 模式：我给新工程随手起了占位名 `mmc`，没问用户，也没查参考代码库里是否已占用。用户问"为啥 mmc"才发现 OMC 运行时到处是 `MMC_` 宏 / `mmc_mk_*`，并排读代码会混淆。改名成本在首次提交前最低，用户定名 `ymc`。
- 规则：**任何会渗透到命名空间/宏/目标名/目录的名字，先问用户，并 grep 参考代码库确认不撞名**。方案里标"待定"不等于可以先用。

## 2026-09-14 — Windows 上 shutil.move 整目录会半途失败
- 模式：目录被别的进程占着（终端/VS Code 打开在里面），`os.rename` 失败后 shutil 退化成 copytree + rmtree，rmtree 在顶层目录卡住，留下空的旧目录。
- 规则：Windows 上移目录前先假设它被占用；脚本要能"再跑一次"（幂等：新目录已存在就只做清理），跑完立刻列两个目录核对，不要相信一次 move 的返回。

## 2026-09-14 — CMake 预设里覆盖 CMAKE_CXX_FLAGS 会冲掉默认 /EHsc
- 模式：`msvc-asan` 预设直接写 `CMAKE_CXX_FLAGS=/fsanitize=address`，MSVC 默认的 `/EHsc /GR` 被替换，出现 C4530；ASan 又和 Debug 默认的 `/INCREMENTAL` 冲突出 LNK4300。之前说"三套预设零警告"时没逐条看 asan 那套的输出。
- 规则：预设只设开关型缓存变量（`YMC_ASAN=ON`），具体 flag 由 CMakeLists 里的 `option()` 分支用 `add_compile_options/add_link_options` 追加。宣称"零警告"前要 grep 每一套预设的构建输出。

## 2026-09-14 — OMC 结果对比基建的四个坑（M0 实现时踩到）
- **rtest 的相对路径靠 `--running-testsuite` + `OPENMODELICALIBRARY`**：直接 `omc file.mo` 的错误信息是绝对路径，与 `// Result:` 块里的 testsuite 相对路径不符。必须传 `--running-testsuite=<可写文件>` 且设 `OPENMODELICALIBRARY`，否则前者报错。运行器复刻 rtest 必须照做。
- **stderr 要合并进 stdout 保序**：`omc ... 2>&1` 是实时交错；Python 里 `capture_output` 后 `stdout+stderr` 拼接会打乱顺序（错误详情和 "# Error encountered" 位置颠倒）。用 `stderr=subprocess.STDOUT`。
- **testsuite 的 `// Result:` 块可能相对当前 omc 已过时**：scoping 里 4/26 用例，安装版和工作区版 omc 的真实输出都与 checked-in Result 不同（如新增 `function` 声明）。所以运行器自检不能要求"全 PASS"，正确判据是"我们的判定与真实 omc-diff.exe 对同一输出一致"。
- **tube 比对算法里 mergeTimelines/removeUneventfulPoints/addTargetEventTimes 在源码中被注释掉**：真实 diffSimulationResults 路径只做 calculateTubes(参考)→calibrate(实际+管道到参考轴)→addRelativeTolerance→validate。别照搬那三个死函数。默认容差由 `rangeDelta=0.002` 主导（竖直半宽≥0.8%幅值），不是 relTol。

## 2026-09-14 — 大工作流会撞会话额度，但 agent 产物已落盘
- 15-agent 的 Spec 工作流跑到一半撞 "session limit"，只有 2 个 agent 返回结构化结果进 cache，但另外 3 个研究 agent 其实已把 .md 写到 scratchpad（额度是在返回结构化输出前耗尽的）。
- 规则：长工作流的中间产物尽量让 agent 写文件；恢复时先 `ls` scratchpad 看哪些已完成，只补真正缺的（本例只需重跑 R4/R5），别整体 resume 重烧 13 个 agent。
