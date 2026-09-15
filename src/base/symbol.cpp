// Interner 成员实现（任务卡 T-S0-01）。
#include "base/symbol.h"

#include "base/assert.h"

namespace ymc {

Symbol Interner::intern(std::string_view text) {
  // 命中已有：直接返回，不新分配（I-2、I-3 的"重复不分配新 id"）。
  if (auto it = index_.find(text); it != index_.end()) {
    return Symbol{it->second};
  }
  // 未命中：先把原文拷进 storage_（deque 尾部，地址稳定），再用指向这份拷贝的 view 建索引。
  // 关键：index_ 的 key 必须指向 storage_.back()，绝不能是入参 text——text 可能指向调用方的
  // 临时缓冲区，函数返回后即失效（I-4 就是在验证这一点）。
  const std::uint32_t id = static_cast<std::uint32_t>(storage_.size());
  storage_.push_back(std::string(text));
  index_.emplace(std::string_view(storage_.back()), id);
  return Symbol{id};
}

std::string_view Interner::str(Symbol s) const {
  YMC_ASSERT(s.is_valid() && s.id < storage_.size(), "Symbol not from this Interner");
  return storage_[s.id];
}

std::optional<Symbol> Interner::lookup(std::string_view text) const {
  if (auto it = index_.find(text); it != index_.end()) {
    return Symbol{it->second};
  }
  return std::nullopt;
}

std::size_t Interner::size() const noexcept {
  // storage_ 是真相来源；两者应恒等。
  YMC_ASSERT(storage_.size() == index_.size(), "Interner storage/index size mismatch");
  return storage_.size();
}

}  // namespace ymc
