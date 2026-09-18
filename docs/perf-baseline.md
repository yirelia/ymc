# 性能基线（NFR-003：先测基线，再定阈值）

每个阶段一节。数字来自本机实测（Windows 11，TDM-GCC 10.3；OMC 1.25.0 安装版），
命令与脚本可复跑。阈值在拿到基线后再定，不预设。

## S1 词法 + 语法（2026-09-16）

工具：`python tools/bench_parse.py --ymc build/gcc-release/ymc.exe --runs 3`（取 3 次最小值）。
语料：OMC 自带 `Modelica 4.0.0+maint.om` 全量，2508 个 `.mo`，327 400 行，14.5 MB。
ymc 侧是**单进程**顺序解析全部文件（`--files-from`），每个文件独立 Interner/Arena，含读文件；
OMC 侧是 `omc bench.mos` 执行 `loadModel(Modelica, {"4.0.0"})`，含进程启动、库发现、解析与装载，
不是纯解析，只能当量级参照。

| 构建 | 耗时（min of 3） | 吞吐 |
|---|---|---|
| ymc `gcc-release`（-O2 -g） | **1.02 s** | 320 k 行/s，14.1 MB/s |
| ymc `gcc-debug`（-O0 -g3，迭代器调试） | 2.90 s | 113 k 行/s，5.0 MB/s |
| omc 1.25.0 `loadModel(Modelica 4.0.0)` | 2.22 s | —（含装载与启动） |

结论：S1 不是性能瓶颈；Release 下整库解析 1 秒级。Debug 慢 2.8×，主要是 `-O0` 与 arena/vector
的边界断言（方案 §3.4 第 12 条：Release 也保留 `YMC_ASSERT`，这部分开销已含在 1.02 s 里）。

建议阈值（待用户拍板，见 spec TBD-06）：S1 整库解析 Release ≤ 3 s（留 3× 余量）；进入 S3 后再测
压平耗时（方案 §8 #5 用工作区 omc 批量 `instantiateModel` 计时做对照）。

工程门同日结果（供对照）：`msvc-asan` 构建 + ctest 4/4；ASan 版 `ymc.exe` 跑 flattening/modelica 全量
2628 文件（含 round-trip，约 7 900 次进程调用）149 s，`ASAN_OPTIONS=exitcode=99` 下 0 个 sanitizer 报告。

## S2 库装载（2026-09-16）

命令：`ymc --load-model=<P> --version=<V> --dump-class-names`（含 MODELICAPATH 扫描、候选库注解元数据读取、
按 package.order 递归装载、`uses` 递归装载、类名枚举），3 次取最小值。

| 构建 | Modelica 4.0.0（+Complex、ModelicaServices） | Buildings 9.1.0（+MSL 4.0.0 链） |
|---|---|---|
| ymc `gcc-release` | **1.63 s** | **4.5 s** |
| ymc `gcc-debug` | 3.63 s | 10.16 s |
| omc 1.25.0 `loadModel(Modelica 4.0.0)`（含启动） | 2.22 s（同 S1 表） | — |

对照 S1 纯解析 1.02 s：装载多出的 0.6 s 主要是候选库根文件的元数据读取（每个候选的 package.mo 各解析一次，
MSL 有 3.2.3 与 4.0.0 两个候选）与选中库根文件的二次解析；S3 前不优化，若成瓶颈再缓存元数据。

## S2 SCode 翻译 + dump（2026-09-17）

命令：`ymc --load-model=<P> --version=<V> --dump-after=scode`（装载 + 全部顶层库 Absyn→SCode + 整库文本 dump 到 stdout），
`gcc-release`，Python `perf_counter` 计整个进程，3 次取最小值。

| 库 | 只装载 | 装载 + SCode + dump | SCode 阶段增量 | dump 行数 |
|---|---|---|---|---|
| Modelica 4.0.0（+Complex、ModelicaServices） | 1.68 s | **1.86 s** | 0.18 s | 90 832 |
| Buildings 9.1.0（+MSL 4.0.0 链） | 4.78 s | **5.28 s** | 0.50 s | 192 923 |

结论：SCode 翻译与 dump 合计不到装载时间的 1/8，S2 的耗时仍由解析主导；`tools/scode_dump_compare.py` 每个类起一个进程
（每次都重新装库），抽样 200 类约 6 分钟——若以后要全量对拍，给 driver 加"一次装载、多类 dump"的批量模式即可，现在不做。
