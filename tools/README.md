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

## 比对器

```
python tools/matcompare.py --algo=tube --ref a.mat --actual b.mat     # = OMC diffSimulationResults
python tools/matcompare.py --algo=classic ...                        # = OMC compareSimulationResults
python tools/matcompare.py --algo=csvtube --ref-source=dymola ...    # csv-compare（Dymola/MAP-LIB）
python tools/resultio.py <file>.mat        # 读结果文件；val() 与 OMC 一致
python tools/structcompare.py --selftest   # 解析 checkModel/backenddaeinfo/bltdump/Base Modelica
python tools/varnorm.py                    # 变量名归一化与交集
```

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
