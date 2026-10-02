# 探针回归集

每个 `.probes` 文件是一组为核实 omc 1.25.0 行为写的小模型——各加宽桶探针的原文。**期望值不入库**：

```
python tools/probe_recheck.py            # 全部；--show 5 打印差异，<集名…> 只跑部分
```

把每个模型同时交给 ymc 与实跑的 omc 1.25.0，逐字节对拍 stdout（判定口径同 `tools/flat_sweep.py`）。

## 规矩

- **每桶收尾必跑**，FAIL / ACCEPTS-INVALID / CRASH 必须为 0。语料扫描（flat_sweep / nf_live_sweep）只能证明"语料里的
  写法没坏"；探针才是已核实行为的回归网（来由见 `tasks/lessons.md` 2026-10-02 那条：59k 的别名规则把 59j 的探针打坏
  8 例，当时没重跑）。
- 确属 omc 1.25.0 自身的毛病、ymc 有意不跟的：在 `KNOWN.txt` 里登记一行 `<集>/<模型>  # 原因`，并且
  `docs/open-questions.md` 的分歧账本里要有对应条目。
- 新桶的探针：草稿区里核实后追加成一个新包 `<桶>-<主题>.probes`。格式：首行 `//// set: 名字 — 说明`，每个模型一段
  `//// model: 名字` + Modelica 源码（omc / ymc 压平文件里**最后一个类**，辅助类写在前面）。
- 不放 OMC testsuite 的拷贝（那是语料，走 sweep），不放 OMC 源码摘录。

## 现有的集

| 集 | 模型数 | 主题 |
|---|---|---|
| `alg` | 4 | 算法段 |
| `ao-a1` | 20 | 结构标记：别名与声明序 |
| `ao-a2` | 18 | 结构标记：实例化期 / 定型期 / 方程段 |
| `ao-a3` | 15 | 结构标记：依赖连带 |
| `ao-a4` | 12 | size(切片) |
| `ao-a5` | 18 | 结构标记：子组件 / 属性 / when |
| `ao-a6` | 8 | 外层写的修饰表达式的求值帧 |
| `ao-a7` | 8 | 求值帧（续） |
| `ao-ut` | 9 | 结构标记（单测原文） |
| `arrattr-split-c2` | 8 | 切分（续） |
| `arrattr-split-m1` | 12 | 切分（续） |
| `arrattr-split` | 11 | 属性的按元素切分 |
| `arrattr` | 45 | 数组组件的属性修饰 |
| `ba-p` | 36 | pre / change 的实参规则 |
| `ba-p2` | 36 | pre / change（续） |
| `ba-q` | 12 | 杂项 |
| `ba-r` | 29 | 数组上的 der / pre |
| `ba-s` | 22 | Boolean 数组表达式 |
| `ba-t` | 33 | Boolean 数组表达式（续） |
| `ba-u` | 53 | Boolean 数组 / 关系 |
| `ba-w` | 47 | 数组 if 表达式 |
| `ba-z` | 37 | 数组 if 按结构参数选支 |
| `cc53` | 8 | 杂项 |
| `ce54` | 9 | 常量求值 |
| `empty2` | 15 | 空数组（续） |
| `ev-m1` | 15 | Evaluate 注解 |
| `ev-m2` | 24 | 结构参数的别名连带 |
| `ev-m3` | 16 | 别名连带（续） |
| `ev-m4` | 10 | 别名连带（修饰 / 记录） |
| `ev-m5` | 6 | 别名连带（数组） |
| `exp52` | 12 | 表达式 |
| `fa-ut` | 8 | 同上（单测原文） |
| `fa` | 43 | 函数注解、derivative / inverse、空 algorithm 段 |
| `fn55` | 18 | 用户函数发射 |
| `imp` | 3 | import |
| `misc` | 176 | 各桶零散探针（早期，按文件名看主题） |
| `nf56` | 7 | nf-live 语料带出的杂项 |
| `r3` | 20 | 杂项 |
| `rec` | 98 | 记录：构造 / 绑定 / 字段 / 变率下沉 |
| `v4-a` | 30 | 同上（分组 a） |
| `v4-b` | 33 | 同上（分组 b） |
| `v4-c` | 58 | 同上（分组 c） |
| `v4-d` | 30 | 同上（分组 d） |
| `v4-e` | 26 | 同上（分组 e） |
| `v4-f` | 20 | 同上（分组 f） |
| `v4-g` | 9 | 同上（分组 g） |
| `v4` | 50 | 向量化调用、sum / product / min / max |
| `sy-s1` | 32 | 同步算子：Clock 构造与钟控 when |
| `sy-s2` | 70 | 同步算子：sample / hold / noClock / previous |
| `sy-s3` | 67 | 同步算子：backSample / shiftSample / subSample / superSample / interval / firstTick |
| `sy-s4` | 47 | 同步算子：Clock 实参形态、结构参数代入、钟控 when 的体 |
| `sy-s5` | 43 | 同步算子：其它语境 |
| `sy-ut` | 6 | 同步算子（单测原文） |
