// Arena 与强类型 Id（任务卡 T-S0-02）——本项目 IR 的内存模型（方案 §3.4 第 1–2 条、§4.1）。
//
// 为什么用 Arena+Id 而不是指针：OMC 的 IR 用可变指针互指（NBVariable.mo:88
// VariablePointer = Pointer<Variable>），靠 GC 回收。C++ 没有 GC，指针互指 = 手工管理生命周期
// = 野指针风险。Arena 一次性回答"谁拥有节点"：arena 拥有一切，别人只拿号（Id）。
// 同类先例：LLVM SlotIndex、rustc 的 arena + NodeId、游戏引擎 ECS。
//
// 存储分块（ADR-007 第 2 阶段 65a 起）：每块 kChunk 个元素、块内容量一次预留够，add() 只在末块里 push_back、满了另开一块
// ——已有元素**永不搬家**，拿到的 T& 跨 add() 依然有效。此前是单个 std::vector，扩容使引用失效（"会踩的第一个大坑"）；
// 压平期要按需实例化包（OMC 的 Lookup.lookupLocalName 进类前 Inst.instPackage，NFLookup.mo:699-700 v1.25.0），flatten 里
// 到处持着节点引用，不能再要求"引用不跨 add"。纪律照旧推荐持有 Id（值语义、可存可比可哈希），引用只是不再是雷。
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
  // 返回新 id = 旧 size；id 永不复用。不搬家：末块容量预留满 kChunk，块内 push_back 不重新分配（块对象本身随外层 vector 移动
  // 时，元素缓冲区不动）。
  [[nodiscard]] Id<T> add(T value) {
    if (chunks_.empty() || chunks_.back().size() == kChunk) {
      chunks_.emplace_back();
      chunks_.back().reserve(kChunk);
    }
    chunks_.back().push_back(std::move(value));
    return Id<T>::from_index(static_cast<std::uint32_t>(size_++));
  }

  [[nodiscard]] T& operator[](Id<T> id) {
    YMC_ASSERT(contains(id), "Arena: id out of range");
    return chunks_[id.raw >> kShift][id.raw & kMask];
  }
  [[nodiscard]] const T& operator[](Id<T> id) const {
    YMC_ASSERT(contains(id), "Arena: id out of range");
    return chunks_[id.raw >> kShift][id.raw & kMask];
  }

  [[nodiscard]] bool contains(Id<T> id) const noexcept {
    return id.is_valid() && id.raw < size_;
  }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  // 分块存储不搬家，无需预留（保留接口：调用方此前为"引用不失效"预留的，现在是空操作）。
  void reserve(std::size_t) {}

  // 遍历标准写法（暂无迭代器；用到再加 IdRange）：
  //   for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(a.size()); ++i) {
  //     Id<T> id = Id<T>::from_index(i); ... a[id] ...
  //   }

 private:
  static constexpr std::uint32_t kShift = 10;
  static constexpr std::size_t kChunk = std::size_t{1} << kShift;
  static constexpr std::uint32_t kMask = (1u << kShift) - 1;
  std::vector<std::vector<T>> chunks_;   // 每块容量 = kChunk（见 add）
  std::size_t size_ = 0;
};

}  // namespace ymc

// Id 可哈希（A-9）。特化写在 std 里。
template <class T>
struct std::hash<ymc::Id<T>> {
  std::size_t operator()(ymc::Id<T> id) const noexcept {
    return std::hash<std::uint32_t>{}(id.raw);
  }
};
