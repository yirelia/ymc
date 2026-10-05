# 任务卡 T-S3-05：继承来的方程 / 算法段——inst 记录基类实例

**状态**：已完成（2026-10-05，加宽(61a)；用户暂不手写核心，由 Claude 实现并按本卡验收）
**执行者**：用户（手写核心：`src/inst/inst.cpp`）
**验收者**：Claude
**归属阶段**：S3（flatten 侧已就绪，见下）

---

## 0. 一句话目标
基类带方程 / 算法的 extends（MSL 里几乎每个模型都是：`extends OnePort` 带 `v = p.v - n.v`）现在被 inst 直接标
`extends with equations/algorithms` → 整个模型 SKIP。flatten 侧的继承段收集已经写好（`collect_inherited_sections`），
只差 inst 把**基类实例**按 extends 序记进实例类树，并去掉这个守卫。

## 1. OMC 依据（引用前重新 grep 行号）
- 段的合并顺序：`NFInst.instExpressions` 先依次处理各 extends、再处理本类，每次 `Sections.join(new, accumulated)` 把新来的
  拼在**前面**（`NFInst.mo:2511-2514, 2533, 3205`；`NFSections.mo:192-209`）→ 结果 = 本类段在前，各 extends 的段按 extends
  **逆序**在后，每个 extends 内部递归同规则。研究底稿 `docs/research/R11-nf-inst-pipeline.md` §3.4 第 6 点（探针 P03）。
- 基类段里的名字在**基类**里解析（基类实例的类树 + 基类的词法作用域），前缀与派生实例相同。

## 2. 现状（改动锚点）
- `src/inst/inst.cpp` `Instantiator::inst_class`（约 280-330 行）：extends 就地展开时 `NodeId base_inst = instantiate(base_def,
  inst_node);` 把基类实例的组件拼进 `flat.components`，但 `base_inst` 本身没有记下来；随后（约 299-303 行）：
  ```cpp
  const SClassDef& bd = I_.scode.elems[bdef].def;
  if (!bd.equations.empty() || !bd.initial_equations.empty() || !bd.algorithms.empty() ||
      !bd.initial_algorithms.empty())
    mark_unsupported("extends with equations/algorithms");
  ```
- `src/inst/inode.h` `ClassTree::exts`：目前只在**定义**类树（ExpandedTree）里存 extends 节点；实例类树（FlatTree）里为空。
- `src/flat/flatten.cpp` `collect_inherited_sections`（flatten_class 末尾调用）：遍历实例类树的 `exts`（逆序），对每个基类实例
  收它自己的 initial equation / equation / initial algorithm / algorithm（`enclosing` = 基类实例），再递归它的 `exts`。`exts` 为空
  时无操作——所以现在是"就绪但未接通"。

## 3. 步骤
1. 在 `inst_class` 的 extends 分支里，`base_inst` 实例化之后加一行：`flat.exts.push_back(base_inst);`（按 extends 出现序；
   跳过的 extends——内建 / 短类 / 解析失败——不记）。
2. 删掉上面那段 `extends with equations/algorithms` 守卫。
3. 不用改 flatten。

## 4. 坑位
- `flat` 是局部 `ClassTree`，最后 `ic.tree = I_.trees.add(std::move(flat))`：`push_back` 要在 move 之前（照现有组件拼接的位置写
  即可）。
- 别在 `exts` 里放 Ref 节点或定义节点：flatten 要的是**实例化后的基类节点**（它的类树是 FlatTree、`def` 是基类 SElement）。
- 多层继承：基类实例自己的 `exts` 由递归的 `instantiate → inst_class` 记好，flatten 会递归，这里不用展开。

## 5. 验收（Claude 已在本地临时打上这两处改动实测，未提交 inst.cpp）
- 全 scodeinst：ExtendSections 由 SKIP 变 PASS，其余无变化；nf-live（`python tools/nf_live_sweep.py`）：PASS 289→293，同时暴露
  1 例参数绑连续变量未拒（DisturbedResistance1，flatten 已补校验）。其余带基类方程的用例仍被别的未实现特性挡着（import /
  redeclare / 短类别名……），MSL 模型主要受益于这一项。
- 门：`python tools/flat_sweep.py --cases-from docs/research/s3-easy-slice.json --oracle-live` FAIL 0 / 不一致 0；
  `python tools/flat_sweep.py --oracle-live` 无新增真 FAIL；`python tools/nf_live_sweep.py` 的 FAIL / ACCEPTS-INVALID / CRASH 不增；
  三套编译器 + ASan 全绿、零警告。
