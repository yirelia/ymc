// 版本号解析与比较（MLS §18.8.1 / §18.8.3）。
#include "scode/version.h"

#include <cstdlib>

namespace ymc {

namespace {
bool is_digit(char c) { return c >= '0' && c <= '9'; }
}  // namespace

Version parse_version(std::string_view s) {
  Version v;
  v.raw = std::string(s);
  std::size_t i = 0;
  if (s.empty() || !is_digit(s[0])) return v;  // 无序版本（§18.8.1 第三种：NON-DIGIT 开头）
  // main release：UNSIGNED-INTEGER { "." UNSIGNED-INTEGER }
  for (;;) {
    int n = 0;
    std::size_t start = i;
    while (i < s.size() && is_digit(s[i])) {
      n = n * 10 + (s[i] - '0');
      ++i;
    }
    if (i == start) break;
    v.nums.push_back(n);
    if (i < s.size() && s[i] == '.' && i + 1 < s.size() && is_digit(s[i + 1])) {
      ++i;
      continue;
    }
    break;
  }
  // semver 预发布 '-…'（到 '+'、空格或结尾）：不在 MLS §18.8.1 里，但 OMC 的 splitVersion 把它当作合法后缀
  //（runtime/systemimpl.c:2063-2098；R8 探针目录 `Foo 3.0.0-beta.1`：loadModel(Foo) 装的就是它）。
  if (i < s.size() && s[i] == '-') {
    std::size_t j = i + 1;
    while (j < s.size() && s[j] != '+' && s[j] != ' ') ++j;
    v.prerelease = std::string(s.substr(i + 1, j - i - 1));
    i = j;
  }
  // 构建元数据 '+…'（到空格或结尾）：OMC 目录名 4.0.0+maint.om
  if (i < s.size() && s[i] == '+') {
    std::size_t j = i + 1;
    while (j < s.size() && s[j] != ' ') ++j;
    v.build = std::string(s.substr(i + 1, j - i - 1));
    i = j;
  }
  // pre-release（MLS 形式）：主版本 + 一个空格 + {S-CHAR}
  if (i < s.size() && s[i] == ' ' && v.prerelease.empty()) {
    v.prerelease = std::string(s.substr(i + 1));
    return v;
  }
  if (i != s.size()) {  // 主版本后跟了别的东西（如 "1.2b"）：不合 §18.8.1 三种形式 → 当无序版本
    v.nums.clear();
    v.build.clear();
  }
  return v;
}

int compare_versions(const Version& a, const Version& b) {
  if (a.is_unordered() || b.is_unordered()) {
    if (a.raw == b.raw) return 0;
    return a.raw < b.raw ? -1 : 1;  // 工具自定
  }
  std::size_t n = a.nums.size() > b.nums.size() ? a.nums.size() : b.nums.size();
  for (std::size_t i = 0; i < n; ++i) {
    int x = i < a.nums.size() ? a.nums[i] : 0;
    int y = i < b.nums.size() ? b.nums[i] : 0;
    if (x != y) return x < y ? -1 : 1;
  }
  // 同主版本：正式版 > 预发布；预发布之间按字母序（§18.8.1）
  if (a.prerelease.empty() != b.prerelease.empty()) return a.prerelease.empty() ? 1 : -1;
  if (a.prerelease == b.prerelease) return 0;
  return a.prerelease < b.prerelease ? -1 : 1;
}

bool same_main_version(const Version& a, const Version& b) {
  if (a.is_unordered() || b.is_unordered()) return a.raw == b.raw;
  std::size_t n = a.nums.size() > b.nums.size() ? a.nums.size() : b.nums.size();
  for (std::size_t i = 0; i < n; ++i) {
    int x = i < a.nums.size() ? a.nums[i] : 0;
    int y = i < b.nums.size() ? b.nums[i] : 0;
    if (x != y) return false;
  }
  return true;
}

bool parse_library_entry_name(std::string_view filename, bool is_dir, LibraryEntry& out) {
  out = LibraryEntry{};
  std::string_view s = filename;
  if (!is_dir) {
    if (s.size() < 4 || s.substr(s.size() - 3) != ".mo") return false;  // §13.4.2 扩展名必须是 .mo
    s.remove_suffix(3);
    out.is_file = true;
  }
  if (s.empty()) return false;
  std::size_t sp = s.find(' ');  // §18.8.3：IDENT 与 VERSION 之间恰一个空格；VERSION 可再含空格
  if (sp == std::string_view::npos) {
    out.name = std::string(s);
    return true;
  }
  if (sp == 0) return false;
  out.name = std::string(s.substr(0, sp));
  out.has_version = true;
  out.version = parse_version(s.substr(sp + 1));
  return true;
}

}  // namespace ymc
