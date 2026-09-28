# 任务卡 T-S3-04：短类链的方向前缀冲突在 inst 里检查（mergeDirection）

**执行者**：用户（手写核心：`src/inst/inst.cpp`）
**验收者**：Claude
**归属阶段**：S3（替换 flatten 里的临时守卫）

---

## 0. 一句话目标
`type In = input Real; type Out = output In;` 这类短类链上方向前缀互相冲突时，OMC 在实例化时报 INVALID_TYPE_PREFIX；
ymc 现在由 flatten 里的临时守卫 `conflicting_direction()`（`src/flat/flatten.cpp` 约 2034 行，调用点约 329 行）
事后扫一遍 SCode 判 SKIP。应把检查放回 inst 的短类属性合并处，与 OMC 同一时机、同一规则。

## 1. OMC 依据（引用前重新 grep 行号）
- `NFFrontEnd/NFAttributes.mo:252-276` `mergeDerivedAttributes`：外层属性与短类内层属性合并时，
  `dir := Prefixes.mergeDirection(dir, innerAttr.direction, node, allowSame = true)`。
- `NFFrontEnd/NFPrefixes.mo:588-604` `mergeDirection`：一边是 NONE 取另一边；`allowSame` 且相同取之；否则
  `printPrefixError`（`:719-729`，Error.INVALID_TYPE_PREFIX，然后 fail）。
- 受影响用例：scodeinst 的 ClassAttributes4、conn8、type4、type5（现均因 flatten 守卫 SKIP，reason
  `conflicting direction prefix`）。

## 2. 现状（改动锚点）
- `src/inst/inst.cpp:437`：短类链循环里 `if (mdir == SDirection::Bidir && cd.derived_attr.dir != SDirection::Bidir)
  mdir = cd.derived_attr.dir;`——只"取第一个非缺省"，冲突时静默保留外层方向。
- 同一循环里的 variability / connector type 合并（:436、:438）不在本卡范围。

## 3. 步骤
1. 在 :437 处按 mergeDirection 改：`mdir` 与 `cd.derived_attr.dir` 都非 Bidir 且不同 → `mark_unsupported(...)`
   （ymc 尚无错误文案对拍，先按"omc 报错 → SKIP"处理；文案对拍是 S3.1 门的错误用例项）。相同 → 保留（allowSame）。
2. 组件自身方向 `el.attr.dir` 与链上第一个非缺省方向也要按同一规则比（OMC 是逐层合并，组件属性是最外层）。
3. 删掉 flatten 里的 `conflicting_direction()` 及其调用点（这一步 Claude 在验收时做：flatten 属 Claude 维护）。

## 4. 坑位
- 合并方向是"外层（组件 / 靠近用处的派生）在前"：注意循环从组件往基类走，外层是已累积的 `mdir`。
- `allowSame = true`：`input` 叠 `input` 合法，不能误报。
- 冲突判定只看方向；variability 用 `variabilityMin`（取更严），与本卡无关，不要顺手改。

## 5. 测试与验收
- 单测（`tests/unit/inst/`）：冲突链 → unsupported；同向叠加 → 不报、方向保留；单层 → 照旧。
- 上述 4 个用例仍为 SKIP（原因变为 inst 的新文案），其余不变：`python tools/flat_sweep.py --oracle-live`
  无新增 FAIL；easy-slice FAIL 0 / 不一致 0。
- 三套编译器 + ASan 全绿、零警告。
