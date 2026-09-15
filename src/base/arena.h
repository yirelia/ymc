// Arena 与强类型 Id（任务卡 T-S0-02）——本项目 IR 的内存模型（方案 §3.4 第 1–2 条、§4.1）。
//
// 为什么用 Arena+Id 而不是指针：OMC 的 IR 用可变指针互指（NBVariable.mo:88
// VariablePointer = Pointer<Variable>），靠 GC 回收。C++ 没有 GC，指针互指 = 手工管理生命周期
// = 野指针风险。Arena 一次性回答"谁拥有节点"：arena 拥有一切，别人只拿号（Id）。
// 同类先例：LLVM SlotIndex、rustc 的 arena + NodeId、游戏引擎 ECS。
//
// 关键纪律（会踩的第一个大坑）：add() 可能触发底层 vector 扩容，使之前拿到的 T& 引用失效。
// 规范：持有 Id，不要持有引用跨越一次 add()。Id 是值语义，扩容后依然有效。
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>  // std::move
#include <vector>

#include "base/assert.h"

namespace ymc {

template <class T>
struct Id {
  static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
  std::uint32_t raw = kInvalid;

  [[nodiscard]] bool is_valid() const noexcept { return raw != kInvalid; }
  // 前置条件：is_valid()。故意不提供到整数的隐式转换——拿到 Id<Expr> 只能做三件事：
  // 传给 Arena<Expr>、比较、当 key（A-5）。"整数变 Id"必须显式走 from_index，可 grep。
  [[nodiscard]] std::uint32_t index() const noexcept {
    YMC_ASSERT(is_valid(), "Id is invalid");
    return raw;
  }
  [[nodiscard]] static Id from_index(std::uint32_t i) noexcept { return Id{i}; }
  bool operator==(const Id&) const = default;
};

template <class T>
class Arena {
 public:
  Arena() = default;

  // 按值接收 + std::move 进容器：这样只可移动的类型（unique_ptr）也能放进来（A-8）。
  // 返回新 id = 旧 size；id 永不复用。可能扩容 → 之前的引用失效（见文件头纪律）。
  [[nodiscard]] Id<T> add(T value) {
    items_.push_back(std::move(value));
    return Id<T>::from_index(static_cast<std::uint32_t>(items_.size() - 1));
  }

  [[nodiscard]] T& operator[](Id<T> id) {
    YMC_ASSERT(contains(id), "Arena: id out of range");
    return items_[id.raw];
  }
  [[nodiscard]] const T& operator[](Id<T> id) const {
    YMC_ASSERT(contains(id), "Arena: id out of range");
    return items_[id.raw];
  }

  [[nodiscard]] bool contains(Id<T> id) const noexcept {
    return id.is_valid() && id.raw < items_.size();
  }
  [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
  void reserve(std::size_t n) { items_.reserve(n); }

  // 遍历标准写法（暂无迭代器；用到再加 IdRange）：
  //   for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(a.size()); ++i) {
  //     Id<T> id = Id<T>::from_index(i); ... a[id] ...
  //   }

 private:
  std::vector<T> items_;
};

}  // namespace ymc

// Id 可哈希（A-9）。特化写在 std 里。
template <class T>
struct std::hash<ymc::Id<T>> {
  std::size_t operator()(ymc::Id<T> id) const noexcept {
    return std::hash<std::uint32_t>{}(id.raw);
  }
};
