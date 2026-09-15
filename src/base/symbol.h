// Symbol 与 Interner（符号表）——任务卡 T-S0-01。
//
// 为什么要 intern：编译器后端里"名字 → 索引"的哈希表到处都是（OMC 后端
// NBVariable.mo:1949 的 UnorderedMap<ComponentRef,Integer> 等）。若每次查找都哈希整个
// 字符串，代价随名字长度线性增长。intern 把"名字 → 4 字节整数"这一步只做一次，之后所有
// 比较/哈希/当 map key 都是整数操作（Symbol == 就是 uint32 ==）。
//
// 生命周期不变式（见 I-7）：str() 返回的 string_view 在 Interner 存活期间永远有效。
// 这要求存原文的容器在扩容时不搬动已有元素——用 std::deque（分块，不整体搬家），
// 不能用 std::vector<std::string>（扩容搬家 + 小字符串优化会让 view 悬垂）。
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ymc {

struct Symbol {
  static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
  std::uint32_t id = kInvalid;

  [[nodiscard]] bool is_valid() const noexcept { return id != kInvalid; }
  bool operator==(const Symbol&) const = default;
};

class Interner {
 public:
  Interner() = default;
  // 不可拷贝（I-9）：拷贝后新对象 index_ 里的 string_view 仍指向旧对象的 storage_，
  // 会悬垂。禁掉拷贝比"修好"它便宜。
  Interner(const Interner&) = delete;
  Interner& operator=(const Interner&) = delete;

  // 不存在则拷贝一份原文并分配下一个 id；已存在则返回原 Symbol。
  [[nodiscard]] Symbol intern(std::string_view text);
  // 前置条件：s 由本 Interner 产生。返回指向内部拷贝的视图（生命周期同 Interner）。
  [[nodiscard]] std::string_view str(Symbol s) const;
  // 只查不插。
  [[nodiscard]] std::optional<Symbol> lookup(std::string_view text) const;
  [[nodiscard]] std::size_t size() const noexcept;

 private:
  std::deque<std::string> storage_;                              // 第 i 个 = id 为 i 的原文（拥有字符）
  std::unordered_map<std::string_view, std::uint32_t> index_;    // 原文视图 → id；视图指向 storage_
};

}  // namespace ymc

// 让 Symbol 能当 unordered_map / unordered_set 的 key（I-8）。特化必须在 std 命名空间里，
// C++17 起允许用限定名 std::hash 直接写偏/全特化。
template <>
struct std::hash<ymc::Symbol> {
  std::size_t operator()(ymc::Symbol s) const noexcept {
    return std::hash<std::uint32_t>{}(s.id);
  }
};
