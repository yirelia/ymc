# R1：OMC 数值比对算法的精确规则

来源约定：`Tubes.c` = `C:\dev\OpenModelica\OMCompiler\Compiler\runtime\SimulationResultsCmpTubes.c`；`Cmp.c` = 同目录 `SimulationResultsCmp.c`；`SR.c` = 同目录 `SimulationResults.c`；`Builtin.mo` = `OMCompiler\Compiler\FrontEnd\ModelicaBuiltin.mo`；`Ceval.mo` = `OMCompiler\Compiler\Script\CevalScriptBackend.mo`；`mat4.c` = `OMCompiler\SimulationRuntime\c\util\read_matlab4.c`；`MT.mos` = `testsuite\simulation\libraries\common\ModelTesting.mos`。探针 = 用 PATH 中的 `omc 1.25.0` 实跑（`OpenModelica v1.25.0 (64-bit)`），模型与脚本在 scratchpad `probe-r1\`。源码是工作区 HEAD，探针是 1.25.0 安装版，两者在这几个文件上未逐行核对差异（见"待验证"）。

注：任务里指定的 `Script/SimulationResults.mo` 不存在，实际文件是 `OMCompiler\Compiler\Util\SimulationResults.mo`（`find` 结果）。

## 结论摘要

1. `diffSimulationResults` 默认值：`relTol = 1e-3`、`relTolDiffMinMax = 1e-4`、`rangeDelta = 0.002`、`vars = fill("",0)`、`keepEqualResults = false`（Builtin.mo:3363-3372）。但 testsuite 标准流程传入的是 `relTol := 0.01`（ModelTestingDefaults.mos:11 → MT.mos:195），其余两项用默认。
2. diff 算法是 ITI "csv-compare" 的 tube 算法移植（Tubes.c:1-6）：以**参考曲线**为中心生成上下管道；把实际曲线**线性插值到参考时间轴**；管道再与 `ref ± max(|ref|·relTol, |max−min|·relTolDiffMinMax)` 取并集（fmax/fmin，不是相加）；参考轴上任一非事件点的插值实际值落在管道外即该变量失败。
3. `rangeDelta` 的几何意义：时间方向的平移量 `delta = rangeDelta·(tStop−tStart)`（Tubes.c:219），并按局部斜率 m 折算成竖直半宽 `delta·sqrt(m²+S²)`，`S = 4·(max−min)/(tStop−tStart)`（Tubes.c:229）。由此竖直半宽 ≥ `4·rangeDelta·(max−min)` = 默认 0.8 % 信号幅值，**远大于 relTol=1e-3**；探针证实 relTol=1e-9 仍判 Equal，且 t=0 处 high/low = 1.008/0.992。
4. `relTolDiffMinMax` 的几何意义：绝对 y 容差 = `|max−min|·relTolDiffMinMax`（max/min 取参考信号全程；Tubes.c:650）；全零信号时退化为 `relTolDiffMinMax²`。
5. 事件点（参考轴上相邻时间相等的点）**不计入误差**（`error[i]=NAN`，Tubes.c:582-592），只按 `10×` 容差画显示带；实际曲线在事件处取右极限（Tubes.c:379-380）。
6. 返回 `(success, failVars)`，打印形如 `(true, {})` / `(false, {"h"})`；**变量缺失不导致失败**，只有 Error+Warning（Compare.mos:41-44，探针 E）。实际文件比参考长 → 截断后可通过；比参考短 → 末值保持并判失败（探针 F/F2）。
7. `rangeDelta = 0` 会让 omc 1.25.0 **段错误**（探针 3/3b，exit 139）；代码原因：Tubes.c:612 `withTubes = 0 == rangeDelta` 走 `skipCalculateTubes`（不分配 `mh/i0h/…`），而 Tubes.c:818-826 `if (withTubes) GC_free(priv->mh)…` 释放未分配指针。
8. `compareSimulationResults`（已弃用，Error.mo:1295-1296）默认 `relTol=0.01, absTol=0.0001`（Builtin.mo:3337-3347）；判据是**单一绝对阈值** `average = relTol·mean(|ref|) + absTol`（Cmp.c:248），逐实际点与最近/插值参考点比 `|d−dr| > average`；返回 `{"Files Equal!"}` 或 `{"Files not Equal!", var…}`（Cmp.c:790-793）；变量缺失 → Not Equal。
9. `val(x,t)`：mat 为二分查找 + 线性插值，精确命中事件重复时刻时取**右极限**（mat4.c:977-985 "use the right limit"）；plt 为顺序扫描 + 线性插值，精确命中时同样取最后一个重复行（右极限）；csv **未实现**（返回 NaN + "val() not implemented for plot format: CSV"）。t 超出 [startTime, stopTime] → NaN + 错误。
10. 结果文件格式只按后缀判定：`.mat`→MATLAB4（`omc_new_matlab4_reader`）、`.plt`→PLT（`ptolemyio.cpp`）、`.csv`→CSV（`read_csv.c:241`）；其他后缀报 "Unknown result-file suffix"（SR.c:109-116）。时间变量名接受 `time` 或 `Time`（Cmp.c:575-587）。

## 逐项事实

### 1. API 签名与默认值

- Builtin.mo:3363-3372 `diffSimulationResults`：
  `input Real relTol = 1e-3 "y tolerance";` / `input Real relTolDiffMinMax = 1e-4 "y tolerance based on the difference between the maximum and minimum of the signal";` / `input Real rangeDelta = 0.002 "x tolerance";` / `input Boolean keepEqualResults = false;` / `output Boolean success` / `output String[:] failVars`。
- Builtin.mo:3337-3347 `compareSimulationResults`：`input Real relTol = 0.01; input Real absTol = 0.0001; input String[:] vars = fill("",0); output String[:] result;` 并标注 `version="Deprecated"`。
- Builtin.mo:3382-3395 `diffSimulationResultsHtml(var, actualFile, expectedFile, relTol=1e-3, relTolDiffMinMax=1e-4, rangeDelta=0.002)` → `output String html`。
- Builtin.mo:3416-3421 `val`：`input VariableName var; input Real timePoint = 0.0; input String fileName = "<default>"`；文档："For parameters, any time may be given. For variables the startTime<=time<=stopTime needs to hold." / "On error, nan ... is returned".
- Builtin.mo:3282-3290 `readSimulationResult(filename, variables, size = 0)` → `Real result[:,:]`；3292-3298 `readSimulationResultSize(fileName)` → `Integer sz`（"number of intervals that are present in the output file"）。
- Builtin.mo:3350-3361 `deltaSimulationResults(filename, reffilename, method "1norm, 2norm, maxerr", vars)` → `Real`。
- 弃用提示原文 Error.mo:1295-1296：`"'%1' is deprecated. It is recommended to use '%2' instead."`；Ceval.mo:2336 `Error.addMessage(Error.DEPRECATED_API_CALL, {"compareSimulationResults", "diffSimulationResults"})`。

### 2. 调用链

- Ceval.mo:2373-2384 `diffSimulationResults` 分支 → `SimulationResults.diffSimulationResults(Testsuite.isRunning(),filename,filename_1,filename2,reltol,reltolDiffMinMax,rangeDelta,vars_1,b)` → 返回 `Values.TUPLE({Values.BOOL(b),v1})`。参考文件路径先经 `Testsuite.friendlyPath`（Util/Testsuite.mo:99，仅在 testsuite 下给相对路径加 `../`）。
- Util/SimulationResults.mo:124-137 → `external "C" res=SimulationResults_diffSimulationResults(...)`；SimulationResults_omc.c:90-93 → `SimulationResultsCmp_compareResults(0, …, refTol, /*abstol*/0, reltolDiffMaxMin, rangeDelta, vars, keepEqualResults, success, /*isHtml*/0, NULL)`。
- SimulationResults_omc.c:79-82 `compareSimulationResults` → `SimulationResultsCmp_compareResults(1, …, refTol, absTol, /*reltolDiffMaxMin*/0, /*rangeDelta*/0, vars, 0, NULL, 0, NULL)`。
- Cmp.c:759-763 分派：`isHtml` → `cmpDataTubes(...,1,htmlOut)`；`isResultCmp` → `cmpData(...)`（旧算法）；否则 → `cmpDataTubes(...,0,0)`（diff 算法）。

### 3. 公共前处理（Cmp.c `SimulationResultsCmp_compareResults` 592-810）

- 打开两个文件失败 → `*success=0`、返回空表（Cmp.c:615-629）。
- vars 为空 → 用**参考文件**的全部非别名变量（Cmp.c:643-645 `cmpvars = getVars(allvarsref,&ncmpvars)`；`readVarsFilterAliases` SR.c:317-346 跳过 `index<=0` 的负别名和重复 index）。
- 时间变量名 Cmp.c:575-587：默认 `"time"`，若变量表里有 `"Time"` 则用之。
- 终止时间检查 Cmp.c:686-694：`if (fabs(time.data[time.n-1]-timeref.data[timeref.n-1]) > reltol*fabs(timeref.data[timeref.n-1]))` → 仅**警告** `"Resultfile and Reference have different end time points!\nReffile[%d]=%f\nFile[%d]=%f\n"`（探针 F 原文：`Reffile[106]=1.000000 / File[56]=0.500000`）。
- 起始重复点 Cmp.c:697-698、752-756：统计文件开头时间相等的连续行数 `offset`，把这些行的数据改写为最后一个重复行的值（"adjust initial data points"）。
- 变量名去双引号 Cmp.c:713 `if (var[j] !='\"' )`。
- 某变量在参考或实际文件中读不到 → `ngetfailedvars++; continue`，同时 Warning `"Get data of variable %s from file %s failed!"`（Cmp.c:723-750）。

### 4. diffSimulationResults 算法（Tubes.c `cmpDataTubes` 610-833）

逐步：

1. Tubes.c:612 `int withTubes = 0 == rangeDelta;`（**变量名与语义相反**：rangeDelta==0 时为真，走无管道路径）。
2. Tubes.c:617 事件识别的时间容差：`xabstol = (tRefEnd − tRef0) · (withTubes ? rangeDelta : 1e-3) / fmax(nActual, nRef)`。即 rangeDelta≠0 时 `xabstol = 跨度·1e-3/点数`；rangeDelta==0 时 `xabstol = 0`。
3. Tubes.c:637 在**参考曲线**上建管道：`priv = withTubes ? skipCalculateTubes(ref…) : calculateTubes(ref.time, ref.values, ref.size, rangeDelta)`。`skipCalculateTubes`（171-188）只把 `yHigh=yLow=y` 复制一份，不分配 `mh/i0h/i1h/ml/i0l/i1l/xHigh/xLow`。
4. `calculateTubes`（191-325）：
   - 219 `priv->delta = r * (priv->tStop - priv->tStart);`（r = rangeDelta）
   - 229 `priv->S = fabs(4 * (priv->max - priv->min) / (fabs(priv->tStop - priv->tStart)));`；231 下限 `0.0004/(tStop−tStart)`。
   - 首段（274-281）：`xHigh = x2 − delta; yHigh = y2 − m·delta + delta·sqrt(m²+S²)`；`xLow = x2 − delta; yLow = y2 − m·delta − delta·sqrt(m²+S²)`。即管道 = 参考折线沿时间平移 ±delta，再沿 y 偏移 ±delta·sqrt(m²+S²)。后续段由 `generateHighTube/LowTube`（51-169）按斜率变化合并/求交。
   - 末端（300-323）：管道延长到 `tStop + delta`。
   - **推论**（由 219/229/276 三行）：竖直半宽 ≥ `delta·S = 4·rangeDelta·(max−min)`，默认 = 0.8 %·(max−min)，与 relTol 无关。探针 D（BB2 vs BB，默认参数，`keepEqualResults=true`）生成的 `BB_dD.h.csv` 第 2 行：`0,1,1,1.00800060145382,0.991999398546181,,1`（max−min≈1 → ±0.008）。
5. Tubes.c:641 `calibrated_values = calibrateValues(ref.time, actual.time, actual.values, &n, actual.size, xabstol)`：把实际曲线线性插值到参考时间点上（`calibrateValues` 341-387，`linearInterpolation` 327-338）。
   - 359-362：若实际时间轴已越过参考末点 → "Avoid extrapolation by cutting the sequence"，`*nsource = i+1` 截断（只比较到实际文件结束处；探针 F2：实际 1.0 s vs 参考 0.5 s → `(true, {})` 仅警告）。
   - 372-375：`while ((x1 <= x) && ((j + 1) < ntarget))` 推进；实际文件比参考短时 j 停在末尾，末两行时间相同 → `linearInterpolation` 的 `almostEqualRelativeAndAbs(x1,x0,0,xabstol)` 分支返回 `y0`，即**末值保持**（探针 F：实际 0.5 s vs 参考 1.0 s → `(false, {"v", "h"})`，csv 末行 `1,0.7109…,0.1817…`）。
   - 379-380：`/* Previous value was the left limit of the event; use the right limit! */ interpolatedValues[i] = y1;`。
6. Tubes.c:644-645：把管道上下折线同样插值到参考时间轴得到 `high[]`、`low[]`。
7. Tubes.c:650 `abstol = (max−min == 0 && max < reltolDiffMaxMin²) ? reltolDiffMaxMin² : fabs((max−min)·reltolDiffMaxMin)`。
8. Tubes.c:651-652 + 547-559 `addRelativeTolerance`：`high[i] = fmax(ref[i] + fmax(|ref[i]|·reltol, abstol), high[i])`；`low[i] = fmin(ref[i] − fmax(|ref[i]|·reltol, abstol), low[i])`。三种容差取**最大者**，不叠加。
9. Tubes.c:653 + 573-607 `validate`：对 i in [0,n)：
   - `isEvent = (i>0 && |t_i − t_{i−1}| ≤ xabstol) || (i+1<n && |t_i − t_{i+1}| ≤ xabstol)`（用 `almostEqualRelativeAndAbs(…,0,xabstol)`，Cmp.c:155-166：`diff <= abstol || diff <= max(|a|,|b|)·reltol`）。
   - 事件点（582-592）：`tol = fmax(abstol*10, fmax(|refv|,|val|)*reltol*10)`；`high = (lastStepError ? refv : max(refv,val)) + tol`，low 对称；`error[i] = NAN` → **不计失败**。探针 D csv 事件行：`0.451523641007943,-1.0002e-010,-1.0002e-010,0.000999999900077754,-0.00100000010012229,,`（=±10·1e-4）。
   - 非事件点（594-604）：`calibrated < low` 或 `> high` → `error[i] = 越界量; isdifferent++`。
   - 有任一越界 → 返回 error 数组，否则 NULL。
10. 结果收集 Tubes.c:758-763：`if (error) { cmpdiffvars[vardiffindx++] = varname; *diffLst = cons(varname, …) }`。Cmp.c:797 `*success = ((ddf.n == 0) && (vardiffindx == 0));`（diff 路径 ddf 恒空，因此 success ⇔ 无变量越界；**变量缺失不影响**）。
11. 输出文件 Tubes.c:711-720：仅当 `error || keepEqualResults` 时写 `"%s.%s.csv"`（prefix.var.csv），表头 731 `time,reference,actual,high,low,error,actual (raw)`；`isHtml` 时生成 dygraph HTML（661-710、767-786）。
12. 打印格式：Ceval.mo:2382-2384 元组；testsuite 期望 `(true, {})`（Compare.mos:41、51；omsimulator/testSynchronousFMU_01.mos 末尾）。探针 A2（rangeDelta=1e-9, relTol=1e-9, MinMax=1e-9）→ `(false, {"h"})`；C2（1e-3/1e-4/1e-9）→ `(false, {"h"})`；C3（1e-3/1e-2/1e-9）→ `(true, {})`；默认参数（含 rangeDelta=0.002）即使 relTol=MinMax=1e-9 → `(true, {})`。
13. **rangeDelta=0 崩溃**：Tubes.c:818-826 `if (withTubes) { GC_free(priv->mh); GC_free(priv->i0h); … GC_free(priv->xLow); }` 释放 `skipCalculateTubes` 从未分配的成员。探针 3（`diffSimulationResults("BB_res.mat","BB_res.mat","BB_dZ",1e-3,1e-4,0.0,{"h"})`）与 3b（`diffSimulationResultsHtml(…,0.0)`）均 `Segmentation fault`，exit 139。
14. 被注释掉的步骤：Tubes.c:631-634、638 `removeUneventfulPoints`、`mergeTimelines` 均为注释，**不参与**当前算法。

### 5. testsuite 标准流程实际使用的参数

- ModelTestingDefaults.mos:9 `diffAlgorithm := OpenModelicaModelTesting.DiffAlgorithm.diffSimulationResults;`、:11 `relTol := 0.01;`、:12 `absTol := 0.0001;`、:13 `compareVars := fill("",0);`、:17 `outputFormat :="mat";`。
- MT.mos:195 `(res,failVars) := OpenModelica.Scripting.diffSimulationResults(resultFile,referenceFile,csvFile+"diff.",relTol,vars=compareVars);` → 实际 `relTol=0.01`，`relTolDiffMinMax=1e-4`、`rangeDelta=0.002` 取默认；`absTol` 在 diff 路径**未使用**。
- MT.mos:185 旧算法分支 `compareSimulationResults(resultFile,referenceFile,csvFile,relTol,absTol,compareVars)`，187 以 `res[1] == "Files Equal!"` 判通过。
- MT.mos:207 通过时打印 `"Files Equal!\n"`；210 失败打印 `"Failed variables: " + …`，并为每个失败变量写 `diffSimulationResultsHtml` 报告（225-229）。
- 统计（`grep -rl`，`testsuite\simulation\libraries`）：805 个 `.mos` 走 `runScript(modelTesting)`；其中 12 个覆盖 `relTol :=`（0.02×7、0.015×2、0.01×3），3 个覆盖 `absTol :=`，7 个覆盖 `diffAlgorithm :=`。
- 全 testsuite：含 `diffSimulationResults` 的 `.mos` 208 个；含 `compareSimulationResults` 778 个；含 `compareVars` 932 个；期望输出含 `"Files Equal!"` 1527 个、`"Files not Equal"` 27 个。

### 6. compareSimulationResults（旧算法，Cmp.c `cmpData` 222-550）

- 阈值 Cmp.c:241-248：`average = mean(|refdata|)`（全程平均）→ `average = reltol*fabs(average)+abstol;`。此后所有比较都是 `|d − dr|` 与这个**常量**比。
- 参考点对齐 Cmp.c:259-281：对每个实际点 (t,d)，推进 j 使 `tr ≥ t`；若推进过且 `(fabs((t-tr)/tr) > reltol) || (fabs(t-tr) > fabs(t-reftime->data[j-1]))` 则退回一格（取更近的参考点）。
- 事件（Cmp.c:294-430）：实际相邻时间 `almostEqualWithDefaultTolerance`（Cmp.c:74-75：`DOUBLEEQUAL_TOTAL 1e-10`、`DOUBLEEQUAL_REL 1e-5`）即视为事件；取 `d_left`（首个）与 `d_right`（最后一个重复行）；在参考里先向前（317 `t_event = t + t*reltol*0.1`）后向后（377 `t − t*reltol*0.1`）找事件；找到则 `|d_left−dr_left| < average && |d_right−dr_right| < average` → `continue`（通过）。
- 插值 Cmp.c:436 `if (fabs(t-tr) > 0.00001) interpolate = 1;`（**绝对** 1e-5 秒）；441-489 在参考相邻点间线性插值，跳过重复时间点。
- 判据 Cmp.c:502-514：`if (err > average)` → 若下一参考行时间相同则改用其值再试（505-511）；仍 `≥ average` → `isdifferent = 1` 并记录到 ddf。
- 返回 Cmp.c:785-793：`if ((ddf.n > 0) || (ngetfailedvars > 0) || vardiffindx > 0)` → `{"Files not Equal!", var…}` + Warning `"Files not Equal"`；否则 `{"Files Equal!"}`。变量缺失 → Not Equal（Compare.mos:45-49；探针 H）。
- 日志 csv Cmp.c:552-573：分号分隔，头四行 `"Generated by OpenModelica";;;;;` / `"Compared Files";;;"absolute tolerance";%.15g;relative tolerance;%.15g` / 两个文件名 / `"Name";"Time";"DataPoint";"RefTime";"RefDataPoint";"absolute error";"relative error";interpolate;`（探针 I 的 `BB_cmpI.csv` 逐字一致）。
- 探针：BB2(37 步) vs BB(100 步)，`0.01, 0.0001` → `{"Files Equal!"}`；`1e-9, 1e-9` → `{"Files not Equal!", "h"}`（v 分段线性，插值零误差；h 二次，插值误差 ~1e-4 超阈值）。

### 7. val(x, t)

- Ceval.mo:2559-2572：`fileName="<default>"` 时读脚本变量 `currentSimulationResult`；否则用给定文件；两者均 → `SimulationResults.val(filename,varNameStr,timeStamp)`（Util/SimulationResults.mo:51-57 → `SimulationResults_val`）。
- SR.c:158-230 `SimulationResultsImpl__val`：
  - MATLAB4（166-186）：`omc_matlab4_find_var` 找不到 → NaN + `"%s not found in %s"`；`omc_matlab4_val` 失败 → NaN + `"%s not defined at time %s (startTime=%s, stopTime=%s)."`。
  - mat4.c:1070-1104 `omc_matlab4_val`：参数直接返回 `params[index-1]`（负 index 取负）；`time > stopTime || time < startTime` → NaN, return 1；否则 `find_closest_points(time, timeColumn, nrows, &i1,&w1,&i2,&w2)`，`*res = w1*y1 + w2*y2`（线性插值）。
  - mat4.c:970-1005 `find_closest_points`：二分查找；精确命中 → `/* If we have events (multiple identical time stamps), use the right limit */ while(mid < max && vec[mid] == vec[mid+1]) mid++;`，权重 1/0；未命中 → 相邻两点线性权重。
  - PLT（188-226）：`fseek` 到头，逐行找 `"DataSet: %s\n"`；`while (fscanf(...,"%lg, %lg\n",&t,&v) == 2) { if (t > timeStamp) break; pt=t; pv=v; }`；218-223 `/* Linear interpolation */ if ((t-pt) == 0.0) return v; w1 = (timeStamp - pt)/(t-pt); return pv*w2 + v*w1;`。精确命中重复时刻时 pv 停在最后一个重复行 → 右极限。
  - CSV（227-230，default 分支）：`"val() not implemented for plot format: %s\n"` → NaN。
- 探针（mat，事件行 `t=0.45152364100794257` 两行 v=−4.42945/3.9865，再两行 `t+5e-11`）：`val(v,t_ev)=3.9865`（右）、`val(v,t_ev−1e-6)=−4.42944`、`val(v,t_ev+1e-6)=3.98649`；`h(0.005)=0.999755` = `(h(0)+h(0.01))/2 = (1+0.999509)/2`，线性插值成立；`val(h,2.0)` → NaN + `Error: h not defined at time 2 (startTime=0, stopTime=1).`；csv → NaN + `Error: val() not implemented for plot format: CSV`。
- 探针（plt）：plt 文本写 16 位有效数字 `0.4515236410079426`，与 mat 的 double `0.45152364100794257` **不是同一个 double**（`0.4515236410079426 == tv[1,47]` → false）。用 plt 文本原值查询 → `3.9865`（右极限）；用 17 位值查询（比 plt 解析值小 1 ulp）→ `−4.42945`（落在事件之前，插值几乎等于左值）。mat 上 `0.4515236410079425/26/27` → 左/右/右。

### 8. 结果文件格式与读取入口

- SR.c:58-64 `enum { UNKNOWN_PLOT=0, MATLAB4, PLT, CSV }`；`PlotFormatStr[] = {"Unknown","MATLAB4","PLT","CSV"}`。
- SR.c:93-156 `SimulationResultsImpl__openFile`：100-106 同名文件缓存（"Super cache"，mtime 只在非 MinGW/MSVC 记录，150-153）；109-116 按后缀：`.mat`→`omc_new_matlab4_reader`（120）、`.plt`→`omc_fopen`（127）、`.csv`→`read_csv`（135，`SimulationRuntime\c\util\read_csv.c:241`，支持首行 `"sep=` 指定分隔符 260-266）。
- SR.c:232-260 `readSimulationResultSize`：mat `matReader.nrows`；plt `read_ptolemy_dataset_size`（`runtime\ptolemyio.cpp:163`）；csv `csvReader->numsteps`（`read_csv.c:302` = 单元格数/列数）。探针：mat/csv/plt 均 106（100 区间 + 事件 4 行 + 末尾重复 2 行）。
- SR.c:347-421 `readDataset`：mat 逐变量 `omc_matlab4_read_vals`（参数展开为常量列）；plt `read_ptolemy_dataset`（`ptolemyio.cpp:62`）；csv `read_csv_dataset`（`read_csv.c:308`，按列名 `strcmp`）。变量不存在 → `"Could not read variable %s in file %s."`。
- 比较时两个文件各用独立全局 reader（Cmp.c:77-78 `simresglob_c` / `simresglob_ref`），可跨格式比较（探针：csv vs mat → `(true, {})`）。
- 附：OMC 1.25.0 在事件处写 4 行：`(t_ev, 左值)、(t_ev, 右值)、(t_ev+5e-11, 右值)×2`，终止时刻写 2 行（探针 mat/csv/plt 一致）。这是运行时输出行为，非比对算法的一部分，但比对算法的事件识别依赖它。

### 9. deltaSimulationResults（附）

- Cmp.c:817-994：`"1norm"|"2norm"|"maxerr"`，未知方法 → Warning 并用 1-norm（838-850）；实际文件必须**起点 ≤ 参考起点且终点 ≥ 参考终点**，否则 Error 返回 −1（898-905）；`deltaData`（173-220）在每个参考时间点上把实际值线性插值后累加 `|valRef−val|`（或平方和开方 / 最大值），各变量求和。

## 未找到或待探针验证

1. 工作区源码（HEAD）与安装版 1.25.0 在 `SimulationResultsCmp*.c`、`read_matlab4.c` 上是否逐行一致：未核对（探针结论与源码推演全部吻合，但 `rangeDelta=0` 崩溃是否在 HEAD 已修复未验证——HEAD 源码 Tubes.c:818-826 仍存在该释放逻辑）。
2. `generateHighTube/LowTube`（Tubes.c:51-169）中段合并/求交的精确几何（"3.2.6.x" 编号对应的 ITI 文档）未在本仓库找到原始规范文档；本文只给出首段公式与由此推出的下界。
3. `SR.c:150-153` 在 Windows（MinGW/MSVC）下不记录 mtime，`Super cache` 是否会在同一 omc 会话中返回被覆盖文件的旧内容：未探针（`closeSimulationResultFile` 文档 Builtin.mo:3431-3439 暗示 Windows 有此问题）。
4. PLT 的 `val` 在**非精确命中**且落在事件 4 行之间时的取值（探针只测了 ±1 ulp）：未穷举。
5. `Testsuite.isRunning()` 对比对结果的影响：只见于错误信息里用 `basename`（Cmp.c:731、744），未见影响判据；未验证 partest 环境。
6. `rtest` 脚本如何把 `// Result:` 期望文本与实际输出做文本比对（是否忽略浮点尾数）：本次未读 `testsuite/rtest`（任务范围外）。

## 对规格的建议

1. ymc 若要"与 OMC testsuite 同判"，比对器应实现 tube 算法而不是纯 relTol/absTol：默认容差事实上由 `rangeDelta=0.002` 主导（竖直半宽 ≥ 0.8 % 幅值），纯 y 容差实现会把 OMC 判 Equal 的结果判 Not Equal（探针 C2 vs 默认）。
2. 规格里明确"参考轴主导"：只在**参考文件的时间点**上比较；实际文件可比参考长（截断）不可比参考短（末值保持→失败）；事件点不计误差；变量缺失只告警。
3. 规格里禁止 `rangeDelta=0`（OMC 段错误），若需要"无管道"模式用极小正值（探针用 1e-9 可行）。
4. `val()` 规格：事件重复时刻取右极限；线性插值；超出 [start, stop] 报错；CSV 不支持。ymc 若实现自己的结果读取器，写 mat 时时间列必须保留完整 double（plt 的 16 位文本会破坏精确命中）。
5. 若 ymc 只输出 csv：OMC 侧 `diffSimulationResults` 可直接读 csv（探针 csv vs mat 通过），但 `val()` 不能用 csv，testsuite 的 `ModelTesting.mos:180` 也优先找 `_res.mat` 再回退 `_res.csv`。
