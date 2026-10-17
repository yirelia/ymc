// omc 运行时的 OpenModelica.Scripting.uriToFilename（Windows 安装版 omc 1.25.0）：编译期求值时把 URI 换成本机路径。
//
// 用处：MSL 的 Modelica.Utilities.Files.loadResource → ModelicaServices.ExternalReferences.loadResource，体里就一句
// `fileReference := OpenModelica.Scripting.uriToFilename(uri)`；omc 化简时对字面量实参的调用求值（NFCeval.evalUriToFilename，
// NFCeval.mo:2932-2947 v1.25.0），压平结果里打出本机的绝对路径（MSL 示例 RAM / ReadRealMatrixFromFile / readRealParameterModel）。
//
// 依据：SimulationRuntime/c/util/utility.c v1.25.0——OpenModelica_uriToFilename_impl :307-402（编译器里经 utility.h:130 的宏调用，
// resourcesDir = NULL）、uriToFilenameRegularPaths :171-249、getIdent :271-278、OpenModelica_decode_uri_inplace :280-305。
// realpath（omc_msvc.c:374-382 的 MinGW 版 = _fullpath）与 omc_stat（omc_file.c:191-204 + longabspath :306-337：
// _wstat(GetFullPathNameW(p))）都落到 GetFullPathNameW。形态逐条核对过：探针 pk-p34（omc 1.25.0 实跑，2026-10-17）。
//
// 为什么自己做 GetFullPathName：libstdc++（TDM-GCC 10）的 std::filesystem::absolute 对已是绝对的路径原样返回（不换 '\\'、
// 不消 ".."），MSVC 的则调 GetFullPathNameW——两套编译器结果不同。这里只接"简单"路径（见 win_full_path），其规则两边一致、
// 与 GetFullPathNameW 相同；别的形态一律 nullopt（调用方 SKIP）。
//
// 文件系统由调用方注入（class_dir / path_stat / cwd）：本文件只管字符串规则，单测不碰磁盘也能覆盖每一支。
#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ymc {

// omc 的 C 缓冲区是 char buf[PATH_MAX]（omc_msvc.h:181 PATH_MAX = MAX_PATH = 260）：够长的路径它走另外的支（拼出的 modelica://
// 路径超长时返回空串 :385-387、类名段超长时报错 :366-369、longabspath 给超长路径加 `\\?\` 前缀并往 stdout 打告警），一律不接。
inline constexpr std::size_t kOmcPathMax = 260;

// GetFullPathNameW 对"简单"路径的结果：盘符绝对路径（`C:/…`、`C:\…`）或相对 cwd 的路径；'/' 换 '\\'、连续分隔符并一
//（探针 URZ46 `Resources//Data` → `Resources\Data`）、"." 段去掉、".." 段退一级不越过根（URZ45 / URZ91）、以分隔符结尾的保留
// 一个 '\\'（URZ23 `Utilities/` → `Utilities\`、URZ21 `./` → cwd + `\`）。cwd 是 GetCurrentDirectoryW 的形态（盘符绝对）。
// 别的形态 GetFullPathName 另有规矩，一律 nullopt：根相对 `\x` / UNC `\\srv`、盘符相对 `C:x`、段里有 `<>:"|?*` 或控制字符 /
// 非 ASCII（omc 的 UTF-8 与 ANSI 代码页换算未核实）、段尾是 '.' 或空格（GetFullPathName 会削掉，URZ100）、DOS 设备名（CON、
// NUL…，会变成 `\\.\NUL`）、结果过长。
inline std::optional<std::string> win_full_path(std::string_view p, std::string_view cwd) {
  auto sep = [](char c) { return c == '/' || c == '\\'; };
  auto drive_abs = [&](std::string_view s) {
    return s.size() >= 3 && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) && s[1] == ':' && sep(s[2]);
  };
  auto ok_segment = [](std::string_view s) {
    for (char c : s) {
      const unsigned char u = static_cast<unsigned char>(c);
      if (u < 0x20 || u > 0x7E || std::string_view("<>:\"|?*").find(c) != std::string_view::npos) return false;
    }
    if (s.back() == '.' || s.back() == ' ') return false;
    std::string base(s.substr(0, s.find('.')));
    while (!base.empty() && base.back() == ' ') base.pop_back();
    for (char& c : base) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    static const char* const kDevices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"};
    for (const char* d : kDevices)
      if (base == d) return false;
    return !(base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0) && base[3] >= '0' &&
             base[3] <= '9');
  };
  std::vector<std::string_view> segs;
  auto walk = [&](std::string_view s) {
    std::size_t b = 0;
    while (b < s.size()) {
      std::size_t e = b;
      while (e < s.size() && !sep(s[e])) ++e;
      const std::string_view seg = s.substr(b, e - b);
      if (seg == "..") {
        if (!segs.empty()) segs.pop_back();
      } else if (!seg.empty() && seg != ".") {
        if (!ok_segment(seg)) return false;
        segs.push_back(seg);
      }
      b = e + 1;
    }
    return true;
  };
  std::string root;
  if (drive_abs(p)) {
    root = std::string(p.substr(0, 2));
    if (!walk(p.substr(2))) return std::nullopt;
  } else {
    if (p.empty() || sep(p[0]) || p.find(':') != std::string_view::npos || !drive_abs(cwd)) return std::nullopt;
    root = std::string(cwd.substr(0, 2));
    if (!walk(cwd.substr(2)) || !walk(p)) return std::nullopt;
  }
  std::string out = root + "\\";
  for (std::size_t i = 0; i < segs.size(); ++i) out += (i == 0 ? "" : "\\") + std::string(segs[i]);
  if (!segs.empty() && sep(p.back())) out += '\\';
  if (out.size() + 1 >= kOmcPathMax) return std::nullopt;
  return out;
}

// URI 映射里的目录（SymbolTable.updateUriMapping，SymbolTable.mo:568-599 v1.25.0）：顶层类名 → System.dirname(info.fileName)，
// 文件名是解析时记下的 GetFullPathName 全路径、'\\' 换成 '/'（Parser.parsebuiltin，Parser.mo:119 的
// replaceWindowsBackSlashWithPathDelimiter(System.realpath(filename))）。file 是交给解析器的那个路径（库的 package.mo / X.mo、
// 命令行上的文件）。盘根下的文件（System.dirname 的形态未核实）→ nullopt。
inline std::optional<std::string> omc_source_dir(std::string_view file, std::string_view cwd) {
  std::optional<std::string> full = win_full_path(file, cwd);
  if (!full || full->back() == '\\') return std::nullopt;
  const std::size_t k = full->rfind('\\');
  if (k == std::string::npos || k <= 2) return std::nullopt;
  std::string dir = full->substr(0, k);
  std::replace(dir.begin(), dir.end(), '\\', '/');
  return dir;
}

struct OmcPathStat {
  bool exists = false;
  bool dir = false;
};
// class_dir：URI 里的顶层类名 → omc_source_dir 形态的目录（没装 / 不知道 → nullopt）。path_stat：win_full_path 形态、不以分隔符结尾的
// 路径 → 存在否 / 是目录否（判不了 → nullopt）。
using OmcClassDirFn = std::function<std::optional<std::string>(std::string_view)>;
using OmcStatFn = std::function<std::optional<OmcPathStat>(const std::string&)>;

// OpenModelica_uriToFilename_impl（resourcesDir = NULL）。nullopt = omc 自己求值失败（omc_assert → MMC_THROW：空串、取不到类名、
// 类没装、`..` 连写、不认识的 scheme——调用原样留着，探针 URZ18 / URZ22 / URZ41 / URZ43）或 ymc 不担保的形态（见 win_full_path）。
inline std::optional<std::string> omc_uri_to_filename(std::string_view uri, const OmcClassDirFn& class_dir,
                                                      const OmcStatFn& path_stat, std::string_view cwd) {
  if (uri.empty()) return std::nullopt;   // :315-318 "Malformed URI (got an empty string)"
  for (char c : uri)
    if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) return std::nullopt;
  auto ci_prefix = [&](std::string_view pre) {
    if (uri.size() < pre.size()) return false;
    for (std::size_t i = 0; i < pre.size(); ++i) {
      const char c = uri[i];
      if ((c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c) != pre[i]) return false;
    }
    return true;
  };
  const bool orig_slash = uri.back() == '/';
  // uriToFilenameRegularPaths（:171-249）：存在 → realpath 的结果（:202-225；目录且原 URI 以 '/' 结尾、结果不以 '/' 结尾时补 '/'，
  // :213-224——URZ23 `…\Utilities\/`、URZ24 `…maint.om/`）；不存在的绝对路径原样（:228-231，URZ2 / URZ79 的正斜杠形态）；不存在
  // 的相对路径接在 realpath("./") 后、中间补 '/'（:232-248，URZ21 `…\o1\/Resources/x.txt`）。以分隔符结尾的照去掉它再问存在否：
  // UCRT 的 _wstat 对"文件名后带分隔符"也说存在（URZ69 → `…\Test_RealMatrix_v4.mat\`，不是目录不补 '/'），msvcrt / MSVC STL 的
  // 判法各异，去掉后两套编译器一致。
  auto regular = [&](const std::string& path) -> std::optional<std::string> {
    const std::optional<std::string> full = win_full_path(path, cwd);
    if (!full) return std::nullopt;
    std::string probe = *full;
    if (probe.back() == '\\' && probe.size() > 3) probe.pop_back();
    const std::optional<OmcPathStat> st = path_stat(probe);
    if (!st) return std::nullopt;
    if (st->exists) {
      std::string r = *full;
      if (st->dir && r.back() != '/' && orig_slash) r += '/';
      return r;
    }
    const bool drive = path.size() > 2 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
                       path[1] == ':' && (path[2] == '/' || path[2] == '\\');
    if (path[0] == '/' || drive) return path.size() + 1 >= kOmcPathMax ? std::nullopt : std::optional<std::string>(path);
    std::optional<std::string> base = win_full_path("./", cwd);
    if (!base) return std::nullopt;
    if (base->back() != '/') *base += '/';
    if (base->size() + path.size() + 1 >= kOmcPathMax) return std::nullopt;
    return *base + path;
  };
  if (ci_prefix("modelica://")) {   // :324 scheme 不分大小写（strncasecmp，URZ17 `Modelica://`）
    std::string_view rest = uri.substr(11);
    std::size_t n = 0;
    while (n < rest.size() && rest[n] != '.' && rest[n] != '/') ++n;   // getIdent
    if (n == 0) return std::nullopt;   // :327-331 "Malformed URI (couldn't get a class name)"
    std::optional<std::string> dir = class_dir(rest.substr(0, n));
    if (!dir || dir->empty()) return std::nullopt;   // :332-336 "Failed to lookup URI (is the package loaded?)"（URZ18）
    // :350 类名之后的部分解码：'+' → 空格、"%XY" → 字节（URZ38 `a+b%41c` → `a bAc`、URZ92 的 `4.0.0+maint.om` → `4.0.0 maint.om`）。
    // 只接两位十六进制、解出可打印 ASCII 的：strtol 会吃空白 / 正负号、'%' 后只剩一个字符时越界读、"%00" 截断串——未核实。'%' 在
    // 串尾原样（`src[1]` 为 0 走 else 支，URZ103）。
    std::string r;
    const std::string_view enc = rest.substr(n);
    for (std::size_t i = 0; i < enc.size(); ++i) {
      if (enc[i] == '+') { r += ' '; continue; }
      if (enc[i] != '%' || i + 1 == enc.size()) { r += enc[i]; continue; }
      auto hex = [](char c) {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      };
      if (i + 2 >= enc.size() || hex(enc[i + 1]) < 0 || hex(enc[i + 2]) < 0) return std::nullopt;
      const int v = hex(enc[i + 1]) * 16 + hex(enc[i + 2]);
      if (v < 0x20 || v > 0x7E) return std::nullopt;
      r += static_cast<char>(v);
      i += 2;
    }
    // :352-379 `.A.B` 逐段：dir/A 是目录就进去，不是就停（这一段丢掉）——URZ39 `Modelica.Utilities/foo.txt` → `…/Utilities/
    // foo.txt`、URZ40 `Modelica.Blocks.Examples/x.txt`（Examples.mo 是文件）→ `…/Blocks/x.txt`；空段且后面还是 '.' = 连写的
    // `..` → 求值失败（:359-363，URZ41）。
    std::string_view s = r;
    while (!s.empty() && s.front() == '.') {
      s.remove_prefix(1);
      std::size_t k = 0;
      while (k < s.size() && s[k] != '.' && s[k] != '/') ++k;
      if (k == 0) {
        if (!s.empty() && s.front() == '.') return std::nullopt;
        break;
      }
      if (dir->size() + k + 1 >= kOmcPathMax) return std::nullopt;   // :366-369
      const std::string cand = *dir + "/" + std::string(s.substr(0, k));
      s.remove_prefix(k);
      const std::optional<std::string> full = win_full_path(cand, cwd);
      if (!full) return std::nullopt;
      const std::optional<OmcPathStat> st = path_stat(*full);
      if (!st) return std::nullopt;
      if (!st->exists || !st->dir) break;
      *dir = cand;
    }
    // :380 跳到第一个 '/' 之后；剩下的为空 → 目录本身（:381-384，URZ24 / URZ25），否则 目录 + '/' + 剩下的（:388-391）。
    while (!s.empty()) {
      const char c = s.front();
      s.remove_prefix(1);
      if (c == '/') break;
    }
    if (s.empty()) return regular(*dir);
    if (dir->size() + 1 + s.size() >= kOmcPathMax) return std::nullopt;   // :385-387
    return regular(*dir + "/" + std::string(s));
  }
  if (ci_prefix("file://")) return regular(std::string(uri.substr(7)));   // :393-395（URZ42 `file://C:/…`、URZ104 `FILE://`）
  if (uri.find("://") != std::string_view::npos) return std::nullopt;     // :396-399 "Unknown URI schema"（URZ43）
  return regular(std::string(uri));                                        // :401 普通路径（URZ20 / URZ21 / URZ44）
}

}  // namespace ymc
