# 三方对比基建（M0）

Claude 实现的 oracle 对比工具（编译器核心由用户手写）。详见 `docs/spec/ymc-需求规格与RFC.md`。
全部 Python 3.11 + numpy/scipy；需外部 `xz`、OMC（安装版 1.25.0 + 工作区版）、Dymola 2025x（可选）。

## 运行器（tests/omc）

```
python tests/omc/run.py --self-check                          # 证明运行器正确（对拍 omc-diff.exe）
python tests/omc/run.py --tool=omc --dir <目录> --json out.json
python tests/omc/run.py --tool=ymc --stage=S1                 # 按 stages.json 跑某阶段
python tests/omc/run.py --print-cflags-histogram --dir <testsuite根>
```
- `omc_diff.py`：omc-diff 复刻（token 级）。`--tool` 切 ymc/omc；`--omc` 切 installed/workspace（工作区版带超时）。
- `cflags_map.json`：cflags→ymc 映射/skip 原因码。`stages.json`：阶段→语料→对比类型。

## S3 压平门

```
python tools/flat_sweep.py --cases-from docs/research/s3-easy-slice.json --oracle-live   # 门：FAIL 0、oracle=live 不一致 0
python tools/flat_sweep.py --oracle-live                                                 # 全 scodeinst（横切改动）
python tools/nf_live_sweep.py                                                            # 旧前端语料 vs 实跑 omc
python tools/nf_live_sweep.py --all --dir <testsuite>                                    # 门：整个 testsuite 的 .mo（3946 个）vs 实跑 omc
python tools/nf_live_sweep.py --all --dir <testsuite> --skip-oracle                      # 排桶：SKIP 原因按 omc 能否压平分开计（真余量）
#   函数调用没接住的 SKIP 带子原因：`function: f [record constructor]`（flatten 的 fn_fail，driver 打在方括号里）
python tools/probe_recheck.py                                                            # 探针回归（tests/probes，见其 README）
python tools/probe_lines.py probe.mo                                                     # 把一个探针模型逐行拆开对拍
```

## 比对器

```
python tools/matcompare.py --algo=tube --ref a.mat --actual b.mat     # = OMC diffSimulationResults
python tools/matcompare.py --algo=classic ...                        # = OMC compareSimulationResults
python tools/matcompare.py --algo=csvtube --ref-source=dymola ...    # csv-compare（Dymola/MAP-LIB）
python tools/resultio.py <file>.mat        # 读结果文件；val() 与 OMC 一致
python tools/structcompare.py --selftest   # 解析 checkModel/backenddaeinfo/bltdump/Base Modelica，及 NB 逐模块 dump（ADR-008 D4）
python tools/varnorm.py                    # 变量名归一化与交集
python -I tools/bdae_refgen.py extract --cases cases_a.json                    # S5 语料 a：NBackend/{basics,alias,event_handling} + equations/parameters
python -I tools/bdae_refgen.py sweep --from ts_cur.json --cases cases_b.json   # S5 语料 b：nf_live_sweep 的 PASS 记录（A1 证据）
python -I tools/bdae_refgen.py run --cases cases_a.json --out sig_a.jsonl --raw   # 跑 omc 1.25.0 新后端，存原文 + manifest 到 ymc-refs；不带 --raw 只留签名
python -I tools/bdae_refgen.py a1 --cases cases_a.json --out a1_a.jsonl --ymc <ymc.exe>   # 语料 a 的 A1
python -I tools/bdae_census.py sig.jsonl [--a1 a1.jsonl] [--strict]            # 准入普查：A2 / A3 / A4 表、累计准入表、(After Lowering) 直方图
python -I tools/bdae_refgen.py run --cases cases_b.json --out sig.jsonl --keep-sections   # 另把 lowering 和九个 pre 模块的段存到 ymc-refs 的 sections/<模块>/<id>.txt（语料 b 1857 个模型重跑 omc，在扫描锁下跑）
python -I tools/bdae_refgen.py sections --cases cases_a.json                   # 不跑 omc，从 <refs>/<id>/out.txt 切段写到同一处（语料 a）
python -I tools/bdae_sweep.py --corpus a --ymc <ymc.exe> [--json out.json]     # S5a 门：ymc --dump-after=lower 与 (After Lowering) 逐字节比（语料 a / b）
python -I tools/bdae_sweep.py --probes --ymc <ymc.exe>                         # 同上，tests/probes/bdae/*.probes 现跑 omc
python -I tools/bdae_sweep.py --corpus a --stage bindings --ymc <ymc.exe>      # --stage 选段（lower | bindings | functionalias | earlyinline），语料和 --probes 都能加；前提是上一段（NB 运行序）PASS，否则 NOT-ADMITTED <上一段>:<判定>，逐层套（functionalias 的 bindings:NOT-ADMITTED:lower:FAIL）
```

## OMC 哈希容器差分（S5b-0）

`src/bdae/omc_unordered.h` 与 omc 1.25.0 的三层对拍。OMC 源码固定读 tag v1.25.0，`--omc-src` 缺省 `C:/dev/OpenModelica`；oracle 只用 PATH 上的安装版 omc。

```
cmake --build <构建目录> -j 3 --target omc_hash_drv             # C++ 侧的驱动，不进默认构建和 ctest
python -I tools/omc_hash/diff.py --work <工作目录> --drv <构建目录>/tests/omc_hash_drv --hash   # L0：裸运行时的整数值（真 stringHashDjb2）
python -I tools/omc_hash/diff.py --work <工作目录> --drv <…> --seed 1 [--seed 2 …]              # L1：真容器（v1.25.0 原文由 omc 编译）差分，dump 带桶数
python -I tools/omc_hash/diff.py --work <工作目录> --drv <…> --ut                               # 单测 omc_unordered_test [容器] 用例的期望值来源
python -I tools/omc_hash/chk.py --drv <…> --run-dir <目录> --probes tests/probes/bdae/s5b0.probes   # L2：预测 vs NB 实跑（分区序、Full 邻接行、when 拆分）
python -I tools/omc_hash/chk.py --drv <…> --run-dir <目录> --list corpus.txt                    # 同上，语料；每行 名字<TAB>model.mo[<TAB>额外 omc 参数…]
```
`mkhsh.py` 备 oracle 目录，`gen_ops.py` 写操作脚本，都由 `diff.py` 调用，用法见各自文件头。`chk.py` 的 `Emit` / `scalar_row_ops` 是 `collectDependencies` union 树的已验证参考（只覆盖 SCALAR 方程），接 S5b 邻接时照它。
when 拆分有两种序：`split` 返回序（`$RES_SIM_k` 的 k 升序）和 dump 的打印序（再反一次，`typeList` 头插），说明在 `chk.py` 的 `check_when`。

## Dymola / 参考 / 报告

```
python tools/dymola_driver.py --smoke                     # 检测 Dymola 可用性（需先装 wheel）
python tools/refgen.py --oracle=omc-installed --models <.mo|目录> --refs-root C:\dev\ymc-refs
python tools/refgen.py ... --check                        # 过期检测（不生成）
python tools/report.py --in report.json --md report.md    # 渲染报告
python tools/report.py --in report.json --todo-block      # 可粘贴待办块
python tools/gate_engineering.py --presets gcc-debug      # 工程门 G3
```
安装 Dymola Python 接口：`py -3.11 -m pip install "C:\Program Files\Dymola 2025x Refresh 1\Modelica\Library\python_interface\dymola-2025.1-py3-none-any.whl"`

## 自测（无第三方框架）

```
python tests/omc/test_omc_diff.py --oracle
python tools/test_matcompare.py --oracle
python tools/{varnorm,structcompare,report,dymola_driver}.py --selftest
```
