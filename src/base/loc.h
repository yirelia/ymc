// 源码位置（方案 §3.5 第 1 条：每个 IR 节点带 SourceLoc，从 Absyn 一路传到生成的 C）。
//
// 只存 4 个 uint32：文件 id + 行 + 列 + 长度。文件 id 由外层的文件表分配（v1 先用 0）。
// 位置从 1 起（行、列），length 是 token/节点覆盖的字符数，便于错误下划线。
#pragma once
#include <cstdint>

namespace ymc {

struct SourceLoc {
  std::uint32_t file = 0;
  std::uint32_t line = 0;
  std::uint32_t col = 0;
  std::uint32_t len = 0;

  [[nodiscard]] bool is_valid() const noexcept { return line != 0; }
};

}  // namespace ymc
