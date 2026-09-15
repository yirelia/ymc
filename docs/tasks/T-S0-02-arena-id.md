# T-S0-02 Arena 与强类型 Id

## 目标

一句话：所有 IR 节点（表达式、变量、方程……）都放在 `Arena<T>` 里，节点之间用 `Id<T>`（4 字节整数、带类型）互相引用，而不是指针。这一条决定了后面十万行代码的内存模型（方案 §3.4 第 1–2 条、§4.1）。

## 来源

- 动机：OMC 的 IR 用可变指针互指（`NBVariable.mo:88 VariablePointer = Pointer<Variable>`），依赖 GC。C++ 没有 GC，指针互指 = 手工管理生命周期 = 野指针。Arena + Id 把"谁拥有节点"这个问题一次性回答：**arena 拥有一切，别人只拿号**。
- 同样思路的成熟先例：LLVM 的 `SlotIndex`、Rust 编译器的 `NodeId`/arena、游戏引擎的 ECS。
- ADR：`docs/adr/002-arena-id.md`（你写：为什么不用指针、为什么 Id 带类型参数）。

## 输入 → 输出

- `add(value)`：把一个 `T` 移进 arena，返回它的 `Id<T>`。
- `arena[id]`：按 Id 取引用（读/写）。
- `contains(id)`、`size()`、`reserve(n)`。

## 接口（你在 `src/base/arena.h` 照着敲；模板，全部写在头里）

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>        // std::move
#include <vector>

#include "base/assert.h"

namespace ymc {

template <class T>
struct Id {
  static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
  std::uint32_t raw = kInvalid;

  [[nodiscard]] bool is_valid() const noexcept { return raw != kInvalid; }
  [[nodiscard]] std::uint32_t index() const noexcept;      // 前置条件：is_valid()
  [[nodiscard]] static Id from_index(std::uint32_t i) noexcept;
  bool operator==(const Id&) const = default;
};

template <class T>
class Arena {
 public:
  Arena() = default;
  // 拷贝 arena 是允许的（Id 是值，不会悬垂），但先别写：用到再说

  [[nodiscard]] Id<T> add(T value);                 // 返回新 id = 旧 size；id 永不复用；可能触发扩容 → 之前拿到的引用失效
  [[nodiscard]] T& operator[](Id<T> id);            // 前置条件：contains(id)
  [[nodiscard]] const T& operator[](Id<T> id) const;
  [[nodiscard]] bool contains(Id<T> id) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  void reserve(std::size_t n);

 private:
  std::vector<T> items_;
};

}  // namespace ymc

template <class T>
struct std::hash<ymc::Id<T>> {
  std::size_t operator()(ymc::Id<T> id) const noexcept { return std::hash<std::uint32_t>{}(id.raw); }
};
```

`Id` 故意**没有**从整数隐式转换的构造函数，也**没有**转成整数的操作：拿到一个 `Id<Expr>` 只能做三件事——传给 `Arena<Expr>`、比较、当 key。这是它比 `uint32_t` 安全的全部原因（测试 A-5）。

## 不变量

| # | 不变量 |
|---|---|
| A-1 | 默认 `Id` 无效 |
| A-2 | `add` 返回 0,1,2,…，`size` 同步增长 |
| A-3 | `operator[]` 可读可写；const arena 只读 |
| A-4 | `contains`：越界与无效 id 为假 |
| A-5 | `Id<A>` 与 `Id<B>` 是不同类型，互不转换，也不转成整数 |
| A-6 | `from_index(i).index() == i` |
| A-7 | `reserve` 不改 `size` |
| A-8 | 只移动类型（`unique_ptr`）可放入 |
| A-9 | `Id` 可哈希 |
| A-10 | 百万级插入正常 |

复杂度：`add` 均摊 O(1)，`operator[]` O(1)。

## 不做

- 不做删除/回收（id 永不复用是**设计**，不是缺功能）；不做迭代器（用 `from_index` 循环，第 3 步）；不做线程安全。

## 分步实施

**第 1 步：`Id`。** 只写 `struct Id`，`index()` 里放 `YMC_ASSERT(is_valid(), "Id is invalid")`；`from_index` 就是构造一个 `raw = i` 的 Id。写完先让 `arena_test` 里 A-1、A-5、A-6（前半）、A-9 通过——其余测试引用 `Arena`，可临时注释。
- 学习点：`static` 成员函数（不需要对象就能调，`Id<Expr>::from_index(3)`）；模板里 `Id` 在类内可以不写 `<T>`。
- 观察点：`print sizeof(ymc::Id<int>)` 应为 4。

**第 2 步：`Arena` 五个函数。** `add`：`items_.push_back(std::move(value)); return Id<T>::from_index(static_cast<std::uint32_t>(items_.size() - 1));`。`operator[]`：`YMC_ASSERT(contains(id), "Arena: id out of range"); return items_[id.raw];` const 版本同样（**两个版本都要写**，A-3 会检查 const 那条）。
- 学习点：为什么 `add` 参数是 `T` 而不是 `const T&`——A-8 用 `unique_ptr` 逼你写 `std::move`；试试把 `std::move` 去掉看编译错误长什么样，读懂它。
- 学习点：模板成员函数写在类内最省事；如果你想练类外定义，形式是 `template <class T> Id<T> Arena<T>::add(T value) { ... }`。

**第 3 步：遍历。** 本项目遍历 arena 的标准写法：

```cpp
for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(arena.size()); ++i) {
  Id<Expr> id = Id<Expr>::from_index(i);
  ...
}
```
写进 `arena.h` 顶部的注释里。（以后写 `IdRange` 让 `for (Id<Expr> id : arena.ids())` 能用——不是现在。）

**第 4 步：引用失效实验（必做）。** 写一个临时程序：`T& r = a[a.add(T{})];` 然后循环 `add` 一万次，再读 `r`。用 gdb 观察 `&items_[0]` 在扩容前后的变化。把观察写进 ADR-002。这就是方案里"持有 Id，不持有引用跨越 `add`"规则的物理证据。

**第 5 步：全绿。** `ctest --preset gcc-debug -R arena`。

## 坑

1. **`operator[]` 忘写 const 版本**：A-3 的 `const Arena<Expr>& ca = a; ca[id]` 编译错 `passing 'const Arena' as 'this' argument discards qualifiers`。
2. **`add` 用 `const T&` 参数**：A-8 编译错 `use of deleted function unique_ptr(const unique_ptr&)`。
3. **给 `Id` 加了 `Id(uint32_t)` 构造函数**：A-5 的 `is_convertible_v<Id<Expr>, uint32_t>` 不会因此为真，但 `is_convertible_v<uint32_t, Id<Expr>>` 会——不要加；用 `from_index`，让"整数变 Id"在代码里可 grep。
4. **`size_t` → `uint32_t` 不用 `static_cast`**：GCC 默认不警告，但方案要求显式转换，评审会打回。
5. **`std::hash<ymc::Id<T>>` 偏特化写在 `namespace ymc` 里面**：特化必须在 `std` 里（或用 `template <class T> struct std::hash<...>` 这种 C++17 起允许的限定名写法，接口里就是这么写的）。

## 验收

```
cmake --preset gcc-debug
cmake --build --preset gcc-debug
ctest --preset gcc-debug -R arena       # 10 tests, 0 failed
```

## 调试手段

gdb：`print a.items_.size()`、`print a.items_[3]`、`print &a.items_[0]`（观察扩容）。

## C++ 要点

类模板、模板成员函数、`static` 成员函数、按值参数 + `std::move`、const 重载、`static_cast`、`std::hash` 偏特化、`std::is_same_v / is_convertible_v`（测试里的 type trait，你只需要能读懂）。

## 完成定义

- [ ] `ctest -R arena` 全绿
- [ ] 引用失效实验做过，ADR-002 有记录
- [ ] 无编译警告
- [ ] 评审通过
