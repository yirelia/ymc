# 待验证清单（活文档）

方案 §3.6 / §8 与需求规格 §10 TBD 的合并。每条：问题 / 验证方法 / 影响 / 状态。
研究底稿在 scratchpad `R1..R7-*.md`（结论已并入需求规格与本仓库工具）。

## 需用户拍板

| ID | 问题 | 下一步 | 状态 |
|---|---|---|---|
| TBD-05 | ymc loadModel 是否复刻 OMC 版本回退映射（{"3.2.1"}→3.2.3+maint、{"2.2.2"}→4.0.0+maint，R7§5 实证） | 用户决定；先只支持 4.0.0，其余标 skip:msl-version | 待定 |
| TBD-08 | 各阶段 G1/G2 通过率阈值、S7 退出阈值 | 用户定；stages.json 现留 null | 待定 |
| TBD-11 | Dymola 语料优先级 | 默认 C-MSL-EX→C-MAPLIB→提取 .mo | 默认执行 |
| TBD-12 | erroneous 错误文本对齐深度 | 默认位置一致 Must、文本一致 Should | 默认执行 |
| TBD-10 | 里程碑时间估算 | 用户告知每周投入 | 待定 |

## 需探针/实跑验证

| ID | 问题 | 验证方法 | 影响 |
|---|---|---|---|
| TBD-02 | Dymola getLastError 统计文本格式（仅有 CoupledClutches 一例，R3§4） | 对若干 MSL 模型实跑 Dymola 取样，固化 parse_translation_stats 正则 | S3/S5 Dymola 结构对比 |
| TBD-03 | Dymola `Advanced.*`（EliminateAliasVariables/OutputModelicaCode）是否存在 | 在 Dymola 里 `list(variables={"Advanced.*"})` | S3/S5 |
| TBD-07 | dsmodel.mof 能否经 Dymola Python 接口取得（文档未暴露，R3§7） | runTestCases(generateReferenceTranslationStructure=True) 或读 User Manual 2C | S3 结构对比来源 |
| TBD-09 | 1.25.0 libSimulationRuntimeC 头/ABI 是否够（方案 §8 #7） | 用 OMC 生成的 Demo.c 手工 clang 链接一次 | S6 |
| TBD-13 | 工作区 omc 与安装版在比对/读取源码是否一致（R1§未找到1） | 需要时 diff 两版对应 .c | S6/S7 |
| TBD-14 | MAP-LIB v4.0.0 是否含 .mat（抽样仅见 .csv，R5） | 克隆 v4.0.0 分支核实 | S6 |
| — | omc 版本回退映射函数位置（R7§未找到3） | grep OMC 源码 conversion(noneFromVersion/from) | TBD-05 |
| — | Blocks/Tables.mo 去 Include= 后 OMC 如何找头文件（R7§未找到） | 读 OMC codegen | 外部函数 S3e/S6 |
| — | R4：$cse/$DER 默认可见性、name 数组下标逐字节、CSV 换行 \r\n？ | 实跑 dump 一份 .mat 探针 | resultio 精度 |

## Oracle 分歧账本（testsuite `// Result:` vs 安装版 omc 1.25.0）

testsuite 的 `// Result:` 由某版 OMC 生成，与项目 oracle（安装版 omc 1.25.0）行为不一致时记此。
ymc 以 **omc 1.25.0 实测**为准；这些用例从 `docs/research/s3-easy-slice.json`（文件 Result 对拍）剔除，避免误记 FAIL。

| 用例 | 文件 `// Result:` | omc 1.25.0 实测 | 结论 |
|---|---|---|---|
| flattening/.../FinalParameter2.mo | `Real y = 3.0;`（final 参数内联） | `Real y = a.x;`（不内联） | 1.25.0 默认 DAE dump 不内联 final parameter；ymc 与 1.25.0 逐字一致。较新 OMC 才内联。 |
| flattening/.../FinalParameter3.mo | `Real y = 3.0;` | `Real y = a.x;` | 同上。 |

## 已解决（并入工具/规格）

- omc-diff 模糊比对规则（方案 §8 #1）→ `tests/omc/omc_diff.py`，对拍 omc-diff.exe 一致。
- ModelTesting 框架（方案 §8 #3）→ R2§E 已解析（relTol=0.01 等）。
- diffSimulationResults tube 算法 → `tools/matcompare.py`，对拍 OMC 一致；默认容差由 rangeDelta 主导。
- .mat v4 布局 → `tools/resultio.py`，val() 与 OMC 逐位一致。
- Dymola 无窗口接口 → `tools/dymola_driver.py`（R3）。
- MSL 4.0.0 三方语料：OMC 与 Dymola 各 2508 .mo，70 个内容不同（R7§3，需列 known-nonequiv 清单）；ModelicaServices 整包排除。
