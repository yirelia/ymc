# ADR-002 IR 用 Arena + 强类型 Id，不用指针

- 日期：2026-09-14
- 状态：已接受

## 背景
IR 节点（表达式、变量、方程……）之间要互相引用。用裸指针互指在有 GC 的语言里可行
（OMC 就是），但 C++ 没有 GC，指针互指 = 手工管理生命周期 = 野指针与所有权混乱。

## 决定
- 所有 IR 节点存在 `Arena<T>`（内部 `std::vector<T>`）里，节点间引用一律用 `Id<T>`
  （4 字节强类型整数），不用指针。
- `Id<T>` 无到整数的隐式转换；"整数变 Id"只能显式走 `from_index`（可 grep）。
- `add(T)` 按值 + `std::move` 入容器，故只可移动类型（`unique_ptr`）也能放。
- 不做删除/回收：id 永不复用是**设计**，不是缺功能。

## 依据
- 动机：OMC 的 `NBVariable.mo:88 VariablePointer = Pointer<Variable>` 靠 GC；C++ 里
  arena+Id 一次性回答"谁拥有节点"——arena 拥有一切，别人只拿号。成熟先例：LLVM `SlotIndex`、
  rustc 的 arena+NodeId、游戏引擎 ECS。（方案 §3.4 第 1–2 条、§4.1。）
- 强类型 Id：`Id<Expr>` 与 `Id<Var>` 是不同类型、互不转换、也不隐式变整数（测试 A-5 用
  `is_same_v/is_convertible_v` 固化），避免"把 Expr 的号当 Var 的号用"这类 bug。
- **引用失效实验**（任务卡 T-S0-02 要求实测）：`T& r = a[a.add(...)]` 后再循环 add 上万次，
  `&items_[0]` 在 vector 扩容后改变，`r` 悬垂。由此定下纪律：**持有 Id，不持有引用跨越一次
  add()**。Id 是值语义，扩容后依然有效。

## 后果
- 得到：确定的所有权（arena）；稳定的跨扩容引用（Id）；强类型防混号；可放 move-only 类型。
- 放弃：`operator[]` 每次一次边界断言（Debug）；不支持删除节点（arena 只增）；持有裸引用
  跨 add 是未定义行为——靠纪律+断言防。
- 何时重看：若某 IR 需要真正删除节点或稳定的原地引用，再考虑分代 id 或 stable_vector。
