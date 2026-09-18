# ADR-005 库查找与版本选择：复刻 OMC 1.25.0 的结果，但不读 index.json

- 日期：2026-09-17
- 状态：已接受（关闭 spec TBD-05）

## 背景
MSL 3.2.3 的 `uses` 要 Complex 3.2.3，本机只有 Complex 4.0.0，OMC 却静默装上了；同时 `loadModel(Modelica, {"3.2.4"})`
在装有 4.0.0 的机器上失败。MLS §13.3 只说版本信息 "may" 参与搜索，§18.8 没规定回退。ymc 的 L1/L3 验收要和 OMC 装到同一批库，
所以必须知道 OMC 的真实规则。

## 决定
1. **复刻 OMC 的两层选择结果**：第 1 层把请求版本换成"等于它或 provides 它"的候选；第 2 层在本地条目里按
   "前 j 级版本号相等（j 从 6 放宽到 0）且条目 ≥ 请求"取最高。未指定版本取最高；版本列表先列者优先。
2. **不读 OMC 的 `index.json`**：第 1 层需要的 provides 信息改从本地候选库根文件的注解重建——
   `version` / `conversion(noneFromVersion=…)` ↔ provides；`conversion(from(version=…))` 里出现过的版本 ↔ "已知发行版"。
   **"库在不在 OMC 索引里"用"本地是否有候选声明了 conversion 历史"近似**：OMC 对不在索引里的库不做第 1 层替换，
   原始请求直接进第 2 层放宽（`ClassLoader.mo:152-166` 的 `vp=={} ? v::versions`）；ymc 里，一个候选都没有
   `noneFromVersion` / `from(version)` 的库就走这条路（R8 的 `Foo` 探针：`1.5`、`0.5` → `3.0.0-beta.1`）。
3. **根目录来源**：`--modelica-path` > 环境变量 `OPENMODELICALIBRARY` > 环境变量 `MODELICAPATH` > `%APPDATA%/.openmodelica/libraries`。
4. **oracle 是安装版 omc 1.25.0，不是源码 HEAD**：两者行为不同时照安装版，并在代码注释里写明 HEAD 的差异。

## 依据
- 1：`FrontEnd/ClassLoader.mo:152-166` → `Script/PackageManagement.mo:352-371 versionsThatProvideTheWanted`、`:165-200 providesExpectedVersion`；
  `runtime/systemimpl.c:2211-2270 getLoadModelPathFromSingleTarget`、`:2272-2302`（default）、`:2063-2098 splitVersion`（`+…` 后缀视为无）。
  13 个判别探针（`docs/research/probe-s2/fallback*.mos`）与 R8 的 `Foo` 系列探针；全部固化在 `loader_test`。
- 2：本机 `index.json` 里 MSL / Complex / ModelicaServices 各版本的 `provides` 与对应库 `conversion(noneFromVersion)` 集合逐项相同
  （R8 "第 1 层"）。索引层还有一个陷阱：`trunk`/`master` 被解析成 0.0.0 而 `SemanticVersion.compare` 视 0.0.0 等于一切
  （`Util/SemanticVersion.mo:120-122`），导致 3.2.4 / 3.3 / 2.0 的候选变成 `{trunk, master}` 而失败——用注解重建时，
  "没有任何候选库认识这个版本 → 失败"给出同样的结果，不需要复刻那个陷阱本身。
- 3：OMC 只读 `OPENMODELICALIBRARY`（`runtime/settingsimpl.c:227-256`），全树没有 `getenv("MODELICAPATH")`；MLS §13.3 规定的是 `MODELICAPATH`。
  两个都认是有意的超集；对拍工具一律显式传 `--modelica-path`，不受环境影响。
- 4：实例——`translateAlternativeExternalAnnotation`：tag `v1.25.0` 的 `AbsynToSCode.mo:508-531` 无条件合并类注解进 external 段注解，
  HEAD `3ceb0c9d06` 的 `:514-561` 改成了白名单 + 仅当缺失时复制；探针 `docs/research/probe-s2/extann.mos` 印证安装版行为。
  查旧版源码的办法：`git -C C:/dev/OpenModelica show v1.25.0:<path>`。

## 后果
- 得到：三套库（MSL 4.0.0 / 3.2.3、Buildings 9.1.0）装载的库集合、类集合、类顺序与 OMC 完全一致；不依赖 OMC 的包管理器文件。
- 放弃：近似有两个方向的误差，都只影响"请求了一个本地没有的版本"这种边角——
  (a) 在 OMC 索引里但不带 conversion 注解的库：OMC 可能经 provides 装上某个版本或因索引陷阱失败，ymc 直接放宽；
  (b) 不在索引里却带 conversion 注解的私有库：OMC 放宽，ymc 对"没人认识的版本"报失败。
  遇到时在难点账本登记，不提前解决；真要逐字节一致就得读 `index.json`（需要一个 JSON 读取器，现在不值得）。
- semver 预发布后缀（`3.0.0-beta.1`）不在 MLS §18.8.1 里，为对齐 OMC 的 `splitVersion` 而接受；MLS 的空格形式（`2.1 Beta 1`）照旧。
- 放弃：`requireExactVersion`、`package.encoding`、加密库 `.moc` 暂不支持（加密库给 Warning）。
- 何时重看：用户升级 omc（oracle 版本变化）时重跑 `loader_test` 里的探针表；接入 Dymola 的库查找规则（spec FR-052）时。
