// 库版本号与文件系统存储形式（S2 装载器用）。
//
// 依据：MLS 3.6 §18.8.1 Version Numbering（三种形式与排序）、§18.8.3 Versions in the File System
//（四种存储形式：IDENT.mo | IDENT␠VERSION.mo | IDENT/ | IDENT␠VERSION/，名字与版本之间恰一个空格，
// VERSION 本身可含空格）。OMC 的库目录名形如 `Modelica 4.0.0+maint.om`：'+' 后是构建元数据，
// 不在 MLS 里；这里按 semver 风格接受并忽略其排序作用。`-beta.1` 这样的 semver 预发布后缀同样接受
//（对齐 OMC runtime/systemimpl.c:2063-2098 splitVersion：'+…' 视为无后缀，'-…' 保留为后缀）。
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace ymc {

struct Version {
  std::vector<int> nums;   // 主版本数值段（"2.1" → {2,1}）；空 = 无序版本（"Test 1"）
  std::string prerelease;  // 预发布后缀：MLS 形式 "2.1 Beta 1" → "Beta 1"；semver 形式 "3.0.0-beta.1" → "beta.1"；空 = 正式版
  std::string build;       // '+' 之后的构建元数据（"4.0.0+maint.om" → "maint.om"）；不参与排序
  std::string raw;

  [[nodiscard]] bool is_unordered() const noexcept { return nums.empty(); }
  [[nodiscard]] bool is_prerelease() const noexcept { return !nums.empty() && !prerelease.empty(); }
};

// 解析 VERSION-NUMBER（§18.8.1）；任何字符串都能解析（不合形式的归为无序版本）。
[[nodiscard]] Version parse_version(std::string_view s);

// §18.8.1 排序：主版本按数值分段比较（短的用 0 补齐，"2.1" == "2.1.0"）；同主版本时正式版 > 预发布；
// 同主版本的预发布按字母序。无序版本参与时按 raw 字典序（工具自定，记 ADR）。返回 <0 / 0 / >0。
[[nodiscard]] int compare_versions(const Version& a, const Version& b);

// 主版本相同（忽略预发布与构建元数据）：用于 uses(Modelica(version="4.0.0")) 匹配 "4.0.0+maint.om"。
[[nodiscard]] bool same_main_version(const Version& a, const Version& b);

// §18.8.3 的存储形式：从目录名或文件名解析出库名与可选版本。
struct LibraryEntry {
  std::string name;        // IDENT
  bool has_version = false;
  Version version;
  bool is_file = false;    // true = `X.mo` / `X 1.2.mo`；false = 目录
};

// filename 是目录名（is_dir=true）或文件名（.mo）。不是合法条目返回 false（如非 .mo 文件、空名）。
[[nodiscard]] bool parse_library_entry_name(std::string_view filename, bool is_dir, LibraryEntry& out);

}  // namespace ymc
