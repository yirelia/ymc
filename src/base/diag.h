// 诊断（方案 §3.4 第 6 条、§4.1）：模型错误一律走 Diagnostics，不用异常。
// v1 最小实现：收集 + 排序打印，格式接近 OMC 的 "[file:line:col] Error: msg"。
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "base/loc.h"

namespace ymc {

enum class DiagLevel : std::uint8_t { Note, Warning, Error };

struct Diagnostic {
  DiagLevel level = DiagLevel::Error;
  std::string msg;
  SourceLoc loc;
};

class Diagnostics {
 public:
  void report(DiagLevel level, SourceLoc loc, std::string msg) {
    items_.push_back({level, std::move(msg), loc});
    if (level == DiagLevel::Error) ++error_count_;
  }
  void error(SourceLoc loc, std::string msg) {
    report(DiagLevel::Error, loc, std::move(msg));
  }

  [[nodiscard]] bool has_errors() const noexcept { return error_count_ > 0; }
  [[nodiscard]] std::size_t error_count() const noexcept { return error_count_; }
  [[nodiscard]] const std::vector<Diagnostic>& items() const noexcept { return items_; }

  // 打印到给定流（用 FILE* 以免拖入 <iostream>）。
  void print(std::FILE* out) const {
    for (const Diagnostic& d : items_) {
      const char* lvl = d.level == DiagLevel::Error ? "Error"
                        : d.level == DiagLevel::Warning ? "Warning" : "Note";
      std::fprintf(out, "[%u:%u:%u] %s: %s\n", d.loc.line, d.loc.col, d.loc.len, lvl,
                   d.msg.c_str());
    }
  }

 private:
  std::vector<Diagnostic> items_;
  std::size_t error_count_ = 0;
};

}  // namespace ymc
