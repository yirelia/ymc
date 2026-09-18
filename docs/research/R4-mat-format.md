# R4：OMC 结果文件 .mat（MAT v4 / Dymola 兼容）写出布局与变量过滤规则

> 只读研究，基线为工作区 HEAD 源码。每条结论附「文件:行号」并引原文关键行。
> 关键文件（均在 `C:\dev\OpenModelica\OMCompiler\SimulationRuntime\c\`）：
> - 写出：`simulation/results/simulation_result_mat4.cpp`（62KB，简称 mat4.cpp）
> - 底层 MAT v4：`simulation/results/MatVer4.cpp` / `MatVer4.h`
> - 读取端印证：`util/read_matlab4.c` / `read_matlab4.h`
> - CSV 写出：`simulation/results/simulation_result_csv.cpp`（注意扩展名是 `.cpp` 不是 `.c`）
> - 过滤：`simulation/simulation_input_xml.c`、`simulation/simulation_runtime.cpp`、`util/simulation_options.c`
> 注意：HEAD 版 mat4 写出器已被重写，使用显式 `DataInfo` 结构与 `writeMatrix_matVer4`，但产出的仍是经典 binTrans/Dymola 布局。

## 结论摘要（≤8 条）

1. **6 个矩阵，固定顺序**：`Aclass, name, description, dataInfo, data_1, data_2`（读取端硬编码此顺序，read_matlab4.c:336）。全部按 MAT v4 列主序（Fortran order）写出。
2. **Aclass = 4×11 CHAR**，转置打包存储，解码后 4 行为：`Atrajectory` / `1.1` / （空行）/ `binTrans`（mat4.cpp:531-532）。第 4 行 `binTrans` 决定"变量沿列存储"的转置格式。
3. **name / description = maxLen×nSignals CHAR**；**dataInfo = 4×nSignals INT32**；每个变量占一列（mat4.cpp:854,862,1210）。
4. **dataInfo 四行**依次为 `channel(0=time,1=data_1,2=data_2)`、`index(1 基，负号=取反别名)`、`interpolation(0=线性)`、`extrapolation(-1/0/1)`（结构体注释 mat4.cpp:89-115）。
5. **data_1 = nData1×2 DOUBLE**（列0=start，列1=stop）；**第 1 行(1基)保留给 [tStart, tStop]**；时不变量(参数/常量)存此（mat4.cpp:1265-1269,1359）。**data_2 = nData2×nPoints DOUBLE**，time 是第 1 个序列(行)，逐个时间点追加为一列（mat4.cpp:1373-1375,1491）。
6. **过滤开关是每变量 `filterOutput` 布尔**：受 `isProtected`、`HideResult` 注解控制，可被 `-emit_protected` / `-ignoreHideResult` 反选（simulation_input_xml.c:627 `shouldFilterOutput`）；再叠加 `-variableFilter` 正则（simulation_runtime.cpp:346 `initializeOutputFilter`）。
7. **`-r` 是结果文件名，不是过滤器**；变量过滤 simflag 是 `-variableFilter`；单精度 `-single`；增量刷头 `-mat_sync`（simulation_options.c:47,142,165,178）。String 变量不写入 .mat（计数函数只处理 real/int/bool）。
8. **CSV**：首行 `"time"`，值格式 real=`,%.16g` int/bool=`,%i`，time 无前导逗号 `%.16g`（simulation_result_csv.cpp:118-121,131,195）。

---

## 逐项事实（带锚点）

### Q1 矩阵名、顺序、维度、类型、转置、Aclass 原文

**矩阵顺序（读取端硬编码，印证写出顺序）** read_matlab4.c:336
```
static const char *matrixNames[6]={"Aclass","name","description","dataInfo","data_1","data_2"};
```

**底层写出与类型编码** MatVer4.h:39-43（类型枚举值）
```
MatVer4Type_DOUBLE = 0, MatVer4Type_SINGLE = 10,
MatVer4Type_INT32 = 20, MatVer4Type_CHAR = 51
```
类型字节大小 MatVer4.cpp:65-77：DOUBLE=8, SINGLE=4, INT32=4, CHAR=1。
写头含大小端 MatVer4.cpp:107 `header.type = (isBigEndian() ? 1000 : 0) + type;`（小端为上列值；即 MAT v4 MOPT 编码）。
MatVer4Header 五字段 MatVer4.h:46-52：`type, mrows, ncols, imagf, namelen`（imagf 恒 0）。
写序 MatVer4.cpp:113-117：先 header，再 name（namelen=strlen+1），再 `fwrite(matrixData, size, rows*cols, file)`（内存序=列主序）。

**Aclass = 4 行 × 11 列 CHAR，转置打包** mat4.cpp:526-532
```
// Dimensions: 4 x 11 ... Data Type: 8-bit, unsigned integer
const char Aclass[] = "A1\0bt.\0ir1\0na\0\0Tj\0\0re\0\0ac\0\0nt\0\0so\0\0\0r\0\0\0y\0\0\0";
writeMatrix_matVer4(matData->pFile, "Aclass", 4, 11, Aclass, MatVer4Type_CHAR);
```
按 4×11 列主序解码（本机验证）得 4 行：
- row0 = `Atrajectory`
- row1 = `1.1`（其余补 `\0`）
- row2 = 空（全 `\0`）
- row3 = `binTrans`（其余补 `\0`）

读取端印证 read_matlab4.c:52-53,381-382,394-405：`binTrans_char="binTrans"`, `binNormal_char="binNormal"`；`if(hdr.mrows != 4)...` `if(hdr.ncols != 11)...`；只在 `if(k==3)` 行判定 `binTrans`(=1) 或 `binNormal`(=0)。**OMC 恒写 binTrans**。

**name / description / dataInfo 维度** mat4.cpp:854,862,1210
```
writeMatrix_matVer4(..., "name", maxLengthName, matData->nSignals, name, MatVer4Type_CHAR);
writeMatrix_matVer4(..., "description", maxLengthDesc, matData->nSignals, description, MatVer4Type_CHAR);
writeMatrix_matVer4(..., "dataInfo", 4, matData->nSignals, dataInfo, MatVer4Type_INT32);
```
即 name=maxLenName×nSignals、description=maxLenDesc×nSignals、dataInfo=4×nSignals；列主序下"每个变量=一列"（转置）。读取端 read_matlab4.c:488-494 印证：`isParam = tmp[k*hdr.mrows]`、`index = tmp[k*hdr.mrows+1]`（k 遍历列/变量，mrows=4）。

**信号总顺序**（name/dataInfo/data 三者一致）：time → [$cpuTime] → [$solverSteps] → real 变量 → [灵敏度参数] → int 变量 → bool 变量 → real 参数 → int 参数 → bool 参数 → real 别名 → int 别名 → bool 别名。见计数 mat4.cpp:200-219（起始 `nSignals=1` 即 time，cpuTime/solverSteps 各条件 +1）与 emit mat4.cpp:1410 起。**String 不进 .mat**。

### Q2 dataInfo 四列含义、别名/负号、data_1/data_2 规则、time 位置

**DataInfo 结构体四字段原注释** mat4.cpp:89-115
```
/* Channel: 0=time, 1=data_1 (time-invariant), 2=data_2 (time-variant) */  channel_t channel;
/* 1 based variable index ... A negative values is a negated alias. */      int32_t index;
/* Interpolation: 0 = linear interpolation. */                             interpolation_t interpolation;
/* Extrapolation ... -1 = not defined outside, 0 = keep first/last, 1 = linear */ extrapolation_t extrapolation;
```
枚举值 mat4.cpp:71-86：channel {TIME=0, TIME_INVARIANT=1, TIME_VARIANT=2}；interpolation {LINEAR=0}；extrapolation {NOT_ALLOWED=-1, CONSTANT=0, LINEAR=1}。
写出为 4 行 = [channel, index, interpolation, extrapolation]（struct 内存布局即列，读取端按 `tmp[k*4+0]=isParam`、`tmp[k*4+1]=index` 解，read_matlab4.c:489-490）。注：读取端把 channel 字段当 `isParam`（==1 时为 data_1 参数）。

**time 的 dataInfo** mat4.cpp:907-910
```
dataInfo[cur].channel = CHANNEL_TIME;  dataInfo[cur].index = ++index_time_variant;  // =1
... extrapolation = EXTRAPOLATION_NOT_ALLOWED;
```
即 time：channel=0，data_2 中 index=1（第 1 个序列/行），外推=不允许。

**负号/取反别名编码** mat4.cpp:1092-1095（real 别名，integer 同 1134-1137）
```
if (modelData->realAlias[arrayIdx].negate) { dataInfo[cur].index = -dataInfo[cur].index; }
```
即"取反别名"用 index 取负表示（与结构体注释一致）。**Bool 取反别名例外**：不用负号，而新开一个 data_2 序列 mat4.cpp:1163-1171
```
if (...booleanAlias...negate) { dataInfo[cur].channel = CHANNEL_TIME_VARIANT; dataInfo[cur].index = ++index_time_variant; ... }
```

**data_1（时不变量：参数/常量）= nData1×2** mat4.cpp:1265-1269,1359
```
void *data_1 = malloc(size * matData->nData1 * 2);
WRITE_REAL_VALUE(data_1, cur, startTime); WRITE_REAL_VALUE(data_1, cur + matData->nData1, stopTime); cur++;
...
writeMatrix_matVer4(..., "data_1", matData->nData1, 2, data_1, matData->type);
```
即 2 列（列0=start 值，列1=stop 值，列主序 `[全部start..][全部stop..]`）；**第 1 行(cur=0)保留写 [tStart, tStop]**，故 `index_time_invariant` 从 1 起（mat4.cpp:887），真正参数从 data_1 第 2 行开始。同一变量 start==stop（参数在 1247/1277 两列写同值）。读取端 read_matlab4.c:507-516：binTrans 下 `nparam=hdr.mrows`，要求 `ncols==2||1`。

**data_2（时变量）= nData2×nPoints** mat4.cpp:1373-1375,1491,1496/1527
```
matData->data2HdrPos = ftell(matData->pFile);
writeMatrix_matVer4(..., "data_2", matData->nData2, 0, NULL, matData->type);   // 初始 0 列
...
fwrite(matData->data_2, sizeofMatVer4Type(matData->type), matData->nData2, matData->pFile);  // 每次 emit 追加 1 列
updateHeader_matVer4(..., data2HdrPos, "data_2", matData->nData2, matData->nEmits, ...);      // 收尾/‑mat_sync 回填 ncols
```
即 nData2 行(序列) × nPoints 列(时间点)，流式追加每列 nData2 个 double，结束或每 `-mat_sync` 个点回填列数。读取端 read_matlab4.c:551-558：binTrans 下 `nrows=ncols`(点数)、`nvar=mrows`(序列数)。nData1/nData2 定义 mat4.cpp:1195-1196 = `index_time_invariant` / `index_time_variant`。

### Q3 哪些变量写入 / 被过滤

**每变量 `filterOutput` 标志**贯穿 mat4 计数(mat4.cpp:290 等)与 emit(mat4.cpp:1422 `!filterOutput && !time_unvarying`)、csv。`time_unvarying` 决定进 data_1 还是 data_2（mat4.cpp:939）。

**`shouldFilterOutput` 判定** simulation_input_xml.c:615-660（函数体 627 起），头注释：
```
* - we filter if isProtected (protected variables)
* - we filter if annotation(HideResult=true)
* - we emit (remove filtering) if !encrypted && emitProtected && isProtected
* - we emit (remove filtering) if ignoreHideResult && annotation(HideResult=true)
```
读 `isProtected`/`hideResult`/`isEncrypted` 三个 XML 属性（630-636）；`isProtected==true`→过滤(640)；`hideResult==true`→过滤(644)；`!encrypted && FLAG_EMIT_PROTECTED && isProtected`→反选(649-650)；`FLAG_IGNORE_HIDERESULT && hideResult`→反选(654)。赋值点 simulation_input_xml.c:712,725,738,751（变量）与 1010（别名）。

**`-variableFilter` 正则叠加** simulation_runtime.cpp:346-369（`initializeOutputFilter`）
```
string tmp = ("^(" + string(variableFilter) + ")$");
if(0 == strcmp(filter, ".*")) { ... return; }   // 默认 .* 全输出，直接跳过
...
modelData->realVarsData[i].filterOutput = regexec(&myregex, ...info.name, ...) != 0;  // 名字不匹配则过滤
```
调用点 simulation_runtime.cpp:751，实参 `simData->simulationInfo->variableFilter`。仅对 `filterOutput` 尚为 0 者叠加过滤（不会反选已被 protected 过滤者）。

**simflag 定义** util/simulation_options.c
```
:46 "mat_sync"   :47 "emit_protected"   :74 "ignoreHideResult"
:142 "r"         :165 "single"          :178 "variableFilter"
:202 "emits protected variables to the result-file"
:297 "value specifies a new result file than the default Model_res.mat"   // -r = 结果文件名，非过滤
:320 "output in single precision"      :333 "sets variableFilter"
```

**`$cse` / `$DER` 未在运行时过滤**：在 `results/` 与 `simulation_runtime.cpp`、`simulation_input_xml.c` grep `$cse`/`$DER`/`cse` 均无命中（本机验证，空结果）→ 这类内部变量的可见性由上游（前端/代码生成阶段的 variableFilter 或 protected/hideResult 标注）决定，运行时只按 filterOutput 执行。

### Q4 CSV 头行与数值格式

值格式 simulation_result_csv.cpp:118-121
```
const char* format = ",%.16g"; const char* formatint = ",%i";
const char* formatbool = ",%i"; const char* formatstring = ",\"%s\"";
```
emit：time 首列无前导逗号 `fprintf(fout, "%.16g", timeValue)`（:131），随后 real(:135)、int(:137)、bool(:139)，**string 被注释掉**(:140-141)；别名取反写 `-value`（real :150 / int :157 / bool 用 `==1?0:1` :164）；行尾 `\n`(:173)。
头行 simulation_result_csv.cpp:189-234
```
const char* format = ",\"%s\"";
fprintf(fout, "\"time\"");            // :195
if(...) fprintf(fout, format, "$cpuTime");   // :197
... 每个未过滤 real/int/bool 变量与非参数别名写 ,"名字" ... fprintf(fout, "\n");  // :234
```
即首行形如 `"time","var1","var2",...`（含 `$cpuTime` 当启用），数值行每列 `%.16g`/`%i`。

### Q5 目录内所有 "Dymola" 注释原句

`grep -rn Dymola`（results 目录 + read_matlab4.c/.h）命中：
- simulation_result_mat4.h:35 —「Specification of Dymosim's result file ... `http://.../Dymola5Manual.pdf`」（另 :33 引 MathWorks matfile_format.pdf）。
- read_matlab4.c:601 —「Convert an OpenModelica variable name into Dymola "der" style.」
- read_matlab4.c:603 —「... is of the form "der(X.Y)" in Dymola style ...」
- read_matlab4.c:626 —「return NULL; /* The Dymola name is the same as OMC */」
- read_matlab4.c:635 —「/* fprintf(stderr, "Dymola style %s -> %s\n", ...) */」
- read_matlab4.c:640 —「Convert a Dymola-style variable name into OpenModelica style.」
- read_matlab4.c:676 —「- attempts Dymola/OpenModelica style conversions」
- read_matlab4.c:692 —「if (res == NULL) { /* Try to convert the name to a Dymola name */」

（结果 `.cpp` 写出器与 csv 文件本体无 "Dymola" 字样；兼容性体现在布局与 der() 命名换算。）

---

## 未找到或待探针验证

1. **`$cse`/`$DER` 具体过滤位置**：运行时未过滤（已证空）。其在结果文件中的默认可见性由上游前端/后端设定，属 NF/NB 或代码生成模板范畴，不在本 R4 运行时文件内——如 ymc 需精确复刻默认隐藏集，建议另开任务查 `NFFrontEnd`/SimCode 的 variableFilter 默认值与 `$` 变量标注。
2. **data_1 同变量两列是否恒相等**：源码对参数在两列写同值（1319-1320 等），对时不变的 realVars 也写同值（1277-1278）；对"第 1 行 [tStart,tStop]"两列不同。建议用探针模型实跑一份 .mat，dump data_1 验证列0=列1（除首行）。
3. **name/description 的 `printArrayName` 数组下标展开格式**（如 `a[1,2]`）与转义细节未逐字节核对——若 ymc 要与 Dymola 变量名逐字符一致，需实跑对比 name 矩阵内容。
4. **CSV 分隔符/换行在 Windows 下是否 `\r\n`**：源码用 `"\n"`，实际取决于 `omc_fopen` 是否文本模式；待实跑确认字节。
5. 本报告基于工作区 HEAD 源码；安装版 omc 1.25.0 的旧 mat4 写出器实现可能不同（经典版直接写 2×n / (n+1)×steps 而非本 DataInfo 重写版），如需对齐安装版行为需另查其源码或实跑 dump。
