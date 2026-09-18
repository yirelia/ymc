// 库装载器实现（S2）。锚点见 loader.h 头注。
#include "scode/loader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "absyn/absyn_util.h"
#include "parse/lexer.h"
#include "parse/parser.h"

namespace fs = std::filesystem;

namespace ymc {

namespace {

bool read_text(const fs::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

std::string path_str(const fs::path& p) { return p.generic_string(); }

std::string ascii_lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool same_case_insensitive(std::string_view a, std::string_view b) {
  return ascii_lower(a) == ascii_lower(b);
}

std::string join_strings(const std::vector<std::string>& strings, std::string_view separator) {
  std::string out;
  for (const std::string& s : strings) {
    if (!out.empty()) out += separator;
    out += s;
  }
  return out;
}

std::string within_string(const Absyn& a, bool has_within, const std::vector<Symbol>& path) {
  if (!has_within || path.empty()) return "within ;";
  return "within " + qualified_name(a, path) + ";";
}

}  // namespace

// -------------------------------------------------------------- 根目录
std::vector<fs::path> Loader::split_modelica_path(std::string_view s) {
  // MLS §13.3：分隔符 tool dependent，"typically" Windows ';'、Linux ':'。本项目只在 Windows 上，用 ';'。
  std::vector<fs::path> out;
  std::size_t start = 0;
  while (start <= s.size()) {
    std::size_t end = s.find(';', start);
    if (end == std::string_view::npos) end = s.size();
    std::string_view piece = s.substr(start, end - start);
    while (!piece.empty() && (piece.front() == ' ' || piece.front() == '"')) piece.remove_prefix(1);
    while (!piece.empty() && (piece.back() == ' ' || piece.back() == '"')) piece.remove_suffix(1);
    if (!piece.empty()) out.emplace_back(std::string(piece));
    if (end == s.size()) break;
    start = end + 1;
  }
  return out;
}

std::vector<fs::path> Loader::default_roots() {
  std::vector<fs::path> roots;
  // OMC 唯一读取 OPENMODELICALIBRARY；未设置时回退用户库目录（settingsimpl.c:226-258）。
  if (const char* omlib = std::getenv("OPENMODELICALIBRARY")) {
    for (fs::path& p : split_modelica_path(omlib)) roots.push_back(std::move(p));
  }
  // MLS §13.3 使用 MODELICAPATH；OMC 不读取它，这是 ymc 为规范兼容保留的有意超集。
  if (const char* mp = std::getenv("MODELICAPATH")) {
    for (fs::path& p : split_modelica_path(mp)) roots.push_back(std::move(p));
  }
  if (const char* appdata = std::getenv("APPDATA")) {
    roots.push_back(fs::path(appdata) / ".openmodelica" / "libraries");
  }
  return roots;
}

// -------------------------------------------------------------- 候选与版本选择
void Loader::scan_root(const fs::path& root, std::string_view name, std::vector<Candidate>& out) {
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return;  // 不存在的根（如用户的 D:\modelica\...）静默跳过
  std::vector<fs::directory_entry> entries;
  for (const fs::directory_entry& de : fs::directory_iterator(root, ec)) {
    if (ec) break;
    entries.push_back(de);
  }
  // OMC 同版本候选取枚举靠后者（systemimpl.c:2201-2209）；先排序使结果不依赖文件系统。
  std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a,
                                                const fs::directory_entry& b) {
    return a.path().filename().generic_string() < b.path().filename().generic_string();
  });
  for (const fs::directory_entry& de : entries) {
    bool is_dir = de.is_directory(ec);
    LibraryEntry e;
    if (!parse_library_entry_name(de.path().filename().string(), is_dir, e)) continue;
    if (e.name != name) continue;
    if (is_dir) {
      if (!fs::exists(de.path() / "package.mo", ec)) {
        // OMC 会走加密解析分支（systemimpl.c:2130；ClassLoader.mo:321-328）；ymc 尚不支持 .moc。
        if (fs::exists(de.path() / "package.moc", ec))
          diags_.report(DiagLevel::Warning, SourceLoc{},
                        "Encrypted Modelica library is not supported: " +
                            path_str(de.path() / "package.moc") + ".");
        continue;
      }
    }
    Candidate c;
    c.entry = e;
    c.path = de.path();
    c.meta = read_library_meta(is_dir ? de.path() / "package.mo" : de.path(), a_.interner,
                               opt_.pure_impure_keywords);
    // OMC 只按条目名拆版本，不读取 annotation(version)（systemimpl.c:2063-2098,2110-2138）。
    c.version = e.has_version ? e.version : Version{};
    out.push_back(std::move(c));
  }
}

// 版本选择——OMC 机制（源码 + 13 个探针用例 + R8 的 Foo 系列探针，omc 1.25.0）：
//   第 1 层 ClassLoader.mo:151-165 → PackageManagement.versionsThatProvideTheWanted（:352-371）：
//     用 index.json 把请求 V 换成"等于 V 或其 provides 含 V"的版本名列表（providesExpectedVersion :165-200，
//     语义版本相等）；index 里 provides 恰等于该版本库的 conversion(noneFromVersion) 集合。
//   第 2 层 systemimpl.c getLoadModelPathFromSingleTarget（:2211-2270）：对每个候选名，在本地条目里按
//     "前 j 级版本相等（j 从 6 放宽到 0）且条目版本 ≥ 请求"找最高者，第一个有命中的 j 即返回；
//     splitVersion（:2063-2098）把 "+…" 后缀当作无后缀（4.0.0+maint.om ≡ 4.0.0）。
//   因此：3.2.1 → 3.2.3（noneFromVersion 含 3.2.1）；2.2.2/2.1 → 4.0.0（2.2.2 是已知发行版，本地没有 →
//     j 放宽到 0 取 ≥ 2.2.2 的最高）；3.2.4/3.3/2.0 → 失败：index 里没有这样的发行版，安装版 1.25.0 就不再
//     去本地匹配（工作区 HEAD 在 ClassLoader.mo:158 会把原始请求也送去第 2 层，那样会装 4.0.0——行为不同，
//     本项目以安装版 1.25.0 为 oracle）；4.1.0 → 失败（已知但本地无 ≥ 4.1.0 者）。
//   替换版本时 OMC 只发 NOTIFICATION（默认不显示）。
// ymc 不读 index.json：用本地各候选库注解重建同一信息——"等于/provides" ↔ version/noneFromVersion；
//   "已知发行版" ↔ 出现在任一候选的 version/noneFromVersion/from(version) 里（老发行版都在新库的转换表里）。
//   库不在 OMC 索引里时第 1 层不替换请求（ClassLoader.mo:152-166 的 `vp=={} ? v::versions`），原始请求直接进第 2 层
//   做前缀放宽（R8 的 Foo 探针：1.5 / 0.5 → 3.0.0-beta.1）。"库在不在索引里"ymc 用"是否有候选声明了 conversion 历史"近似：
//   一个都没有 → 当作不在索引里 → 直接放宽。近似的代价见 ADR-005。
//   第 2 层的两条细则也照抄：带预发布后缀的请求不放宽，只做"主版本全等 + 后缀前缀匹配"（:2246-2268；3.0.0-beta → 3.0.0-beta.1，
//   3.0.0-rc → 失败）；不带后缀的请求可以命中预发布条目（3.0.0 → 3.0.0-beta.1）。
//   探针脚本在 docs/research/probe-s2/fallback*.mos 与 docs/research/probe-r8/；决策记 ADR-005。
//   MLS §13.3 只说版本信息 "may" 参与搜索，§18.8 未规定回退。
const Loader::Candidate* Loader::choose_version(const std::vector<Candidate>& cands,
                                                std::string_view requested_version) const {
  if (cands.empty()) return nullptr;
  auto has_version_info = [](const Candidate& c) {
    return !c.version.is_unordered() || !c.version.raw.empty();
  };
  auto higher = [&](const Candidate& x, const Candidate& y) {  // x 版本高于 y？无版本号视为最低
    bool xv = has_version_info(x), yv = has_version_info(y);
    if (xv != yv) return xv;
    return compare_versions(x.version, y.version) > 0;
  };
  if (requested_version.empty()) {  // 未指定：最高版本（systemimpl.c getLoadModelPathFromDefaultTarget :2272-2302）
    const Candidate* best = nullptr;
    for (const Candidate& c : cands)
      if (!best || higher(c, *best) || compare_versions(c.version, best->version) == 0) best = &c;
    return best;
  }
  Version req = parse_version(requested_version);
  auto list_has = [&](const std::vector<std::string>& vs, const Version& v) {
    for (const std::string& s : vs)
      if (same_main_version(parse_version(s), v)) return true;
    return false;
  };
  // 种类 0：版本相同；种类 1：noneFromVersion 含请求（= index 的 provides）
  const Candidate* best = nullptr;
  int best_kind = 9;
  bool known_release = false;       // 请求是否为"已知发行版"（出现在任一候选的版本表里）
  bool family_has_history = false;  // 有候选声明了 conversion 历史 ≈ 这个库在 OMC 的索引里
  for (const Candidate& c : cands) {
    if (!c.meta.none_from.empty() || !c.meta.convert_from.empty()) family_has_history = true;
    int k = -1;
    // 请求带预发布后缀时，条目的后缀必须以它开头（systemimpl.c:2246-2268 的 strncmp）
    bool suffix_ok = req.prerelease.empty() ||
                     c.version.prerelease.compare(0, req.prerelease.size(), req.prerelease) == 0;
    if (same_main_version(c.version, req) && suffix_ok) k = 0;
    else if (list_has(c.meta.none_from, req)) k = 1;
    if (k >= 0 || list_has(c.meta.convert_from, req)) known_release = true;
    if (k >= 0 && (k < best_kind ||
                   (k == best_kind && (higher(c, *best) || compare_versions(c.version, best->version) == 0)))) {
      best = &c;
      best_kind = k;
    }
  }
  if (best) return best;
  // 种类 2：前缀逐级放宽 + 条目 ≥ 请求 + 取最高（systemimpl.c :2224-2244；只对不带后缀的数字请求做）。
  // 前提：请求是已知发行版但本地没有，或者这个库根本没有版本历史（≈ 不在 OMC 索引里，请求原样进第 2 层）。
  if ((known_release || !family_has_history) && !req.is_unordered() && req.prerelease.empty()) {
    const std::vector<int>& rn = req.nums;
    for (std::size_t j = 6; j-- > 0;) {  // j = 5..0：前 j 级相等（6 级全等已由种类 0 覆盖）
      const Candidate* hit = nullptr;
      for (const Candidate& c : cands) {
        if (!has_version_info(c) || c.version.is_unordered()) continue;
        bool prefix_eq = true;
        for (std::size_t i = 0; i < j; ++i) {
          int a = i < c.version.nums.size() ? c.version.nums[i] : 0;
          int b = i < rn.size() ? rn[i] : 0;
          if (a != b) { prefix_eq = false; break; }
        }
        if (!prefix_eq || compare_versions(c.version, req) < 0) continue;
        if (!hit || higher(c, *hit)) hit = &c;
      }
      if (hit) return hit;
    }
  }
  // 最后手段：完全没有版本信息的条目（Single.mo 且无 version 注解）——无从判断，接受（MLS §13.3 "may"）
  for (const Candidate& c : cands)
    if (!has_version_info(c)) return &c;
  return nullptr;  // 没有任何候选声明认识这个版本
}

// -------------------------------------------------------------- 装载入口
Id<ClassDef> Loader::load_library(std::string_view name, std::string_view requested_version) {
  return load_library_impl(name, requested_version, true, "");
}

Id<ClassDef> Loader::load_library_impl(std::string_view name, std::string_view requested_version,
                                       bool force_load, std::string_view request_origin) {
  for (const LoadedLibrary& l : loaded_) {
    if (l.name != name) continue;
    if (!force_load && !requested_version.empty()) {
      // uses 命中已装库时不重装，按已装库的 conversion 注解分级（CevalScript.mo:580-619）。
      LibraryMeta meta = library_meta(a_, l.cls);
      std::string actual = meta.version.empty() ? l.version.raw : meta.version;
      Version wanted_version = parse_version(requested_version);
      Version actual_version = parse_version(actual);
      if (!same_main_version(wanted_version, actual_version)) {
        auto has_version = [&](const std::vector<std::string>& versions) {
          return std::any_of(versions.begin(), versions.end(), [&](const std::string& version) {
            return same_main_version(parse_version(version), wanted_version);
          });
        };
        if (wanted_version.is_unordered() || actual_version.is_unordered()) {
          diags_.report(DiagLevel::Warning, SourceLoc{},
                        "Requested package " + std::string(name) + " of version " +
                            std::string(requested_version) + ", but this package was already loaded with version " +
                            actual + ". OpenModelica cannot reason about compatibility between the two packages since "
                                     "they are not semantic versions.");
        } else if (has_version(meta.none_from)) {
          diags_.report(DiagLevel::Note, SourceLoc{}, std::string(request_origin) + " requested package " +
                                                            std::string(name) + " of version " +
                                                            std::string(requested_version) + ". " +
                                                            std::string(name) + " " + actual +
                                                            " is used instead which states that it is fully compatible "
                                                            "without conversion script needed.");
        } else if (has_version(meta.convert_from)) {
          diags_.report(DiagLevel::Warning, SourceLoc{}, std::string(request_origin) + " requested package " +
                                                               std::string(name) + " of version " +
                                                               std::string(requested_version) + ". " +
                                                               std::string(name) + " " + actual +
                                                               " is used instead which states that it is only compatible "
                                                               "with a conversion script. Use convertPackageToLibrary(" +
                                                               std::string(request_origin) + ", " + std::string(name) +
                                                               ", \"" + actual +
                                                               "\") to run the conversion script or proceed with potential "
                                                               "issues as a result.");
        } else if (compare_versions(wanted_version, actual_version) > 0) {
          diags_.report(DiagLevel::Warning, SourceLoc{},
                        "Requested package " + std::string(name) + " of version " +
                            std::string(requested_version) + ", but this package was already loaded with version " +
                            actual + ". There are no conversion annotations for this version but " +
                            std::string(requested_version) + " is newer than " + actual +
                            ". There is a possibility that " + actual +
                            " remains backwards compatible, but it is not loaded so OpenModelica cannot verify this.");
        } else {
          diags_.report(DiagLevel::Warning, SourceLoc{},
                        "Requested package " + std::string(name) + " of version " +
                            std::string(requested_version) + ", but this package was already loaded with version " +
                            actual + ". There are no conversion annotations and " +
                            std::string(requested_version) + " is older than " + actual +
                            ", so the libraries are probably incompatible.");
        }
      }
    }
    return l.cls;
  }
  // uses 触发的装载（force_load=false）失败时：OMC 的 loadModel1 用 try/else 接住 fail()，只补一条
  // NOTIFICATION、父库照常成功（CevalScript.mo:502-545）；try/else 不回滚错误缓冲，所以畸形依赖自己报的
  // Error 仍然留在诊断里。这里照做：直接装进主树，失败就补 Note——不要为了"藏住错误"把依赖库装两遍
  //（Buildings 的依赖是整个 MSL，试装一遍就是多解析 30 万行）。
  // §13.3：按根顺序找；在某根找到候选后不再看后面的根
  std::vector<Candidate> cands;
  for (const fs::path& root : opt_.roots) {
    scan_root(root, name, cands);
    if (!cands.empty()) break;
  }
  std::string_view selection_version = requested_version == "default" ? std::string_view{} : requested_version;
  const Candidate* pick = choose_version(cands, selection_version);
  if (!pick) {
    std::string roots;
    for (const fs::path& r : opt_.roots) roots += (roots.empty() ? "" : ";") + path_str(r);
    std::string version = requested_version.empty() ? "default" : std::string(requested_version);
    std::string message = (force_load ? "Failed to load package " : "Skipped loading package ") +
                          std::string(name) + " (" + version + ") using MODELICAPATH " + roots;
    if (force_load)
      diags_.error(SourceLoc{}, message + ".");  // Error.mo:447-448 LOAD_MODEL_FAILED
    else
      diags_.report(DiagLevel::Note, SourceLoc{},
                    message + " (uses-annotation may be wrong).");  // Error.mo:1030-1031
    return Id<ClassDef>{};
  }
  Id<ClassDef> cls;
  std::vector<Symbol> empty_path;
  if (pick->entry.is_file)
    cls = load_file_entity(pick->path, pick->entry.name, Id<ClassDef>{}, empty_path);
  else
    cls = load_dir(pick->path, pick->entry.name, Id<ClassDef>{}, empty_path);
  if (!cls.is_valid()) {
    std::string roots;
    for (const fs::path& r : opt_.roots) roots += (roots.empty() ? "" : ";") + path_str(r);
    std::string version = requested_version.empty() ? "default" : std::string(requested_version);
    if (force_load)
      diags_.error(SourceLoc{}, "Failed to load package " + std::string(name) + " (" + version +
                                    ") using MODELICAPATH " + roots + ".");
    else
      diags_.report(DiagLevel::Note, SourceLoc{},
                    "Skipped loading package " + std::string(name) + " (" + version +
                        ") using MODELICAPATH " + roots + " (uses-annotation may be wrong).");
    return cls;
  }
  a_.top.push_back(cls);
  loaded_.push_back(LoadedLibrary{pick->entry.name, pick->entry.has_version, pick->entry.version,
                                  pick->path, cls});
  if (opt_.follow_uses) follow_uses(cls);
  return cls;
}

Id<ClassDef> Loader::load_path(const fs::path& path) {
  std::error_code ec;
  std::vector<Symbol> empty_path;
  Id<ClassDef> cls;
  if (fs::is_directory(path, ec)) {
    LibraryEntry e;
    if (!parse_library_entry_name(path.filename().string(), true, e)) {
      diags_.error(SourceLoc{}, "不是合法的库目录名: " + path_str(path));
      return Id<ClassDef>{};
    }
    cls = load_dir(path, e.name, Id<ClassDef>{}, empty_path);
  } else {
    // 显式打开的单文件：MLS A.2.1 允许多个类；这里只把第一个当顶层返回，全部加入 top
    StoredDefinition sd = parse_file(path, static_cast<std::uint32_t>(file_names_.size()));
    for (Id<ClassDef> id : sd.classes) a_.top.push_back(id);
    if (sd.classes.empty()) return Id<ClassDef>{};
    cls = sd.classes[0];
    loaded_.push_back(LoadedLibrary{std::string(a_.interner.str(a_.classes[cls].name)), false, Version{},
                                    path, cls});
    if (opt_.follow_uses) follow_uses(cls);
    return cls;
  }
  if (cls.is_valid()) {
    a_.top.push_back(cls);
    loaded_.push_back(LoadedLibrary{std::string(a_.interner.str(a_.classes[cls].name)), false, Version{},
                                    path, cls});
    if (opt_.follow_uses) follow_uses(cls);
  }
  return cls;
}

// -------------------------------------------------------------- 文件解析
StoredDefinition Loader::parse_file(const fs::path& file, std::uint32_t file_id) {
  file_names_.push_back(path_str(file));
  std::string src;
  if (!read_text(file, src)) {
    diags_.error(SourceLoc{file_id, 0, 0, 0}, "无法读取文件: " + path_str(file));
    return StoredDefinition{};
  }
  Lexer lx(src, file_id, a_.interner, diags_, opt_.pure_impure_keywords);
  std::vector<Token> toks = lx.tokenize();
  Parser p(toks, a_, diags_);
  return p.parse_stored_definition();
}

bool Loader::place_into(Id<ClassDef> cls, Id<ClassDef> parent, const std::vector<Symbol>& parent_path) {
  switch (insert_class(a_, parent, cls)) {
    case InsertResult::Ok:
      return true;
    case InsertResult::ParentIsShortClass:  // §13.4.3
      diags_.error(a_.classes[cls].loc, "Failed to insert class " +
                                            std::string(a_.interner.str(a_.classes[cls].name)) +
                                            " within " + qualified_name(a_, parent_path) +
                                            ": 包围类是短类定义");
      return false;
    case InsertResult::DuplicateName:  // §13.4.1
      diags_.error(a_.classes[cls].loc, "Failed to insert class " +
                                            std::string(a_.interner.str(a_.classes[cls].name)) +
                                            " within " + qualified_name(a_, parent_path) +
                                            ": 已有同名类");
      return false;
  }
  return false;
}

Id<ClassDef> Loader::load_file_entity(const fs::path& file, const std::string& expected_name,
                                      Id<ClassDef> parent, const std::vector<Symbol>& parent_path,
                                      bool expect_parts) {
  std::size_t errors_before = diags_.error_count();
  StoredDefinition sd = parse_file(file, static_cast<std::uint32_t>(file_names_.size()));
  if (diags_.error_count() != errors_before) return Id<ClassDef>{};
  if (sd.classes.size() != 1) {
    std::vector<std::string> names;
    for (Id<ClassDef> id : sd.classes) names.emplace_back(a_.interner.str(a_.classes[id].name));
    // ClassLoader.mo:452-462；Error.mo:547-548 LIBRARY_ONE_PACKAGE_PER_FILE。
    diags_.error(SourceLoc{}, "Modelica library files should contain exactly one package, but found the following "
                              "classes: " +
                                  join_strings(names, ", ") + ".");
    return Id<ClassDef>{};
  }
  Id<ClassDef> cls = sd.classes[0];
  std::string actual_name(a_.interner.str(a_.classes[cls].name));
  if (actual_name != expected_name) {
    if (same_case_insensitive(actual_name, expected_name)) {
      // ClassLoader.mo:466-472；Error.mo:783-784。
      diags_.report(DiagLevel::Warning, a_.classes[cls].loc,
                    "Expected the package to have name " + expected_name + ", but got " + actual_name +
                        ". Proceeding since only the case of the names are different.");
    } else {
      diags_.error(a_.classes[cls].loc,
                   "Expected the package to have name " + expected_name + ", but got " + actual_name + ".");
      return Id<ClassDef>{};
    }
  }
  if (expect_parts && a_.classes[cls].is_short) {
    // package.mo 必须是 PARTS（ClassLoader.mo:328-336,474-476；Error.mo:555-556）。
    diags_.error(a_.classes[cls].loc, expected_name +
                                          " is a package.mo-file and needs to be based on class parts (i.e. not class "
                                          "extends, derived class, or enumeration).");
    return Id<ClassDef>{};
  }

  bool same_within = parent_path.empty()
                         ? (!sd.has_within || sd.within_path.empty())
                         : (sd.has_within && sd.within_path == parent_path);
  if (!same_within) {
    bool same_case = sd.has_within && sd.within_path.size() == parent_path.size();
    for (std::size_t i = 0; same_case && i < parent_path.size(); ++i)
      same_case = same_case_insensitive(a_.interner.str(sd.within_path[i]),
                                        a_.interner.str(parent_path[i]));
    std::string expected_within = within_string(a_, !parent_path.empty(), parent_path);
    std::string actual_within = within_string(a_, sd.has_within, sd.within_path);
    if (same_case) {
      // ClassLoader.mo:477-485；Error.mo:803-804。
      diags_.report(DiagLevel::Warning, a_.classes[cls].loc,
                    "Expected the package to have " + expected_within + " but got " + actual_within +
                        " (ignoring the potential error; the class might have been inserted at an unexpected location).");
    } else {
      diags_.error(a_.classes[cls].loc,
                   "Expected the package to have " + expected_within + " but got " + actual_within + ".");
      return Id<ClassDef>{};
    }
  }
  if (parent.is_valid() && !place_into(cls, parent, parent_path)) return Id<ClassDef>{};
  return cls;
}

std::vector<std::string> Loader::read_package_order(const fs::path& dir) const {
  std::vector<std::string> names;
  std::string text;
  if (!read_text(dir / "package.order", text)) return names;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    std::size_t s = 0;
    while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
    line.erase(0, s);
    if (!line.empty()) names.push_back(line);  // ClassLoader.mo:519-523：trim 后去空行
  }
  return names;
}

Id<ClassDef> Loader::load_dir(const fs::path& dir, const std::string& expected_name,
                              Id<ClassDef> parent, const std::vector<Symbol>& parent_path) {
  std::error_code ec;
  fs::path pkg = dir / "package.mo";
  if (!fs::exists(pkg, ec)) {  // §13.4.1
    diags_.error(SourceLoc{}, "目录 " + path_str(dir) + " 缺少 package.mo");
    return Id<ClassDef>{};
  }
  // 1) package.mo：定义与目录同名的类
  Id<ClassDef> cls = load_file_entity(pkg, expected_name, parent, parent_path, true);
  if (!cls.is_valid()) return cls;
  std::vector<Symbol> my_path = parent_path;
  my_path.push_back(a_.classes[cls].name);
  const std::vector<Id<Element>> package_elements = a_.classes[cls].elements;

  // 2) 收集子条目：X/（含 package.mo）与 X.mo；同名并存违反 §13.4.1
  struct Sub {
    std::string name;
    fs::path path;
    bool is_dir;
  };
  std::vector<fs::directory_entry> entries;
  for (const fs::directory_entry& de : fs::directory_iterator(dir, ec)) {
    if (ec) break;
    entries.push_back(de);
  }
  std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a,
                                                const fs::directory_entry& b) {
    return a.path().filename().generic_string() < b.path().filename().generic_string();
  });
  std::vector<Sub> subs;
  for (const fs::directory_entry& de : entries) {
    std::string fname = de.path().filename().string();
    if (fname == "package.mo" || fname == "package.order") continue;
    bool is_dir = de.is_directory(ec);
    LibraryEntry e;
    if (!parse_library_entry_name(fname, is_dir, e) || e.has_version) continue;  // 子条目不带版本
    if (is_dir && !fs::exists(de.path() / "package.mo", ec)) continue;         // 非结构化目录（Resources/ 等）
    subs.push_back(Sub{e.name, de.path(), is_dir});
  }

  for (std::size_t i = 0; i < subs.size(); ++i) {
    for (std::size_t j = i + 1; j < subs.size(); ++j) {
      if (subs[i].name != subs[j].name) continue;
      const Sub& file_sub = subs[i].is_dir ? subs[j] : subs[i];
      const Sub& dir_sub = subs[i].is_dir ? subs[i] : subs[j];
      // ClassLoader.mo:533-538,553-560；Error.mo:998-999 PACKAGE_DUPLICATE_CHILDREN。
      diags_.error(SourceLoc{}, "The same class is defined in multiple files: " + path_str(file_sub.path) +
                                    ", " + path_str(dir_sub.path / "package.mo") + ".");
      return Id<ClassDef>{};
    }
  }

  // 3) 顺序完全复刻 ClassLoader.mo:497-570,642-763：名单逐项消费；失效时退回默认顺序。
  std::vector<std::string> order = read_package_order(dir);
  bool has_order = fs::is_regular_file(dir / "package.order", ec);
  enum class OrderedKind : std::uint8_t { Existing, External };
  struct OrderedItem {
    OrderedKind kind;
    Id<Element> element;
    std::string name;
  };
  std::vector<OrderedItem> layout;
  bool valid_order = has_order;

  if (has_order) {
    std::vector<std::string> sorted_order = order;
    std::sort(sorted_order.begin(), sorted_order.end());
    std::vector<std::string> duplicates;
    for (std::size_t i = 1; i < sorted_order.size(); ++i)
      if (sorted_order[i] == sorted_order[i - 1] &&
          (duplicates.empty() || duplicates.back() != sorted_order[i]))
        duplicates.push_back(sorted_order[i]);
    if (!duplicates.empty())
      diags_.report(DiagLevel::Warning, SourceLoc{},
                    "Found duplicate names in package.order file: " + join_strings(duplicates, ", ") + ".");

    std::vector<std::string> remaining = order;
    auto was_external = [&](std::string_view name) {
      return std::any_of(layout.begin(), layout.end(), [&](const OrderedItem& item) {
        return item.kind == OrderedKind::External && item.name == name;
      });
    };
    for (Id<Element> eid : package_elements) {
      const Element& element = a_.elems[eid];
      std::vector<std::string> names;
      if (element.kind == ElementKind::ClassDef) {
        names.emplace_back(a_.interner.str(a_.classes[element.cls].name));
      } else if (element.kind == ElementKind::Component &&
                 element.comp.prefix.var == TypePrefix::Variability::Constant) {
        for (const ComponentDecl& component : element.comp.comps)
          names.emplace_back(a_.interner.str(component.name));
      }
      if (names.empty()) {
        layout.push_back({OrderedKind::Existing, eid, {}});
        continue;
      }

      bool placed = false;
      while (!placed) {
        if (remaining.empty()) {
          if (was_external(names.front()))
            diags_.report(DiagLevel::Warning, element.loc,
                          "Elements in the package.mo-file need to be in the same relative order as the "
                          "package.order file. Got element named " +
                              names.front() +
                              " but it was already added because it was not the next element in the list at that time.");
          diags_.report(DiagLevel::Warning, element.loc,
                        "Got element " + names.front() + " that was not referenced in the package.order file.");
          remaining.insert(remaining.begin(), names.begin(), names.end());
        }

        bool match = remaining.size() >= names.size();
        for (std::size_t i = 0; match && i < names.size(); ++i) match = remaining[i] == names[i];
        if (match) {
          if (was_external(names.front()))
            diags_.report(DiagLevel::Warning, element.loc,
                          "Elements in the package.mo-file need to be in the same relative order as the "
                          "package.order file. Got element named " +
                              names.front() +
                              " but it was already added because it was not the next element in the list at that time.");
          layout.push_back({OrderedKind::Existing, eid, {}});
          remaining.erase(remaining.begin(), remaining.begin() + static_cast<std::ptrdiff_t>(names.size()));
          placed = true;
        } else {
          if (remaining.front() == names.front() && names.size() > 1)
            diags_.report(DiagLevel::Warning, element.loc,
                          "Components referenced in the package.order file must be moved in full chunks. Either split "
                          "the constants to different lines or make them subsequent in the package.order file.");
          layout.push_back({OrderedKind::External, Id<Element>{}, remaining.front()});
          remaining.erase(remaining.begin());
        }
      }
    }
    for (const std::string& name : remaining)
      layout.push_back({OrderedKind::External, Id<Element>{}, name});

    // OMC 内部列表是 reverse order（ClassLoader.mo:504,542-543），故先检查靠后的条目。
    for (auto rit = layout.rbegin(); rit != layout.rend(); ++rit) {
      const OrderedItem& item = *rit;
      if (item.kind != OrderedKind::External) continue;
      bool found = std::any_of(subs.begin(), subs.end(), [&](const Sub& sub) { return sub.name == item.name; });
      if (!found) {
        diags_.report(DiagLevel::Warning, SourceLoc{}, item.name +
                                                           " was referenced in the package.order file, but was not found "
                                                           "in package.mo, " +
                                                           item.name + "/package.mo or " + item.name + ".mo.");
        valid_order = false;
        break;
      }
    }

    if (valid_order) {
      std::vector<std::string> missing;
      for (const Sub& sub : subs)
        if (std::find(order.begin(), order.end(), sub.name) == order.end()) missing.push_back(sub.name);
      if (!missing.empty()) {
        diags_.report(DiagLevel::Warning, SourceLoc{},
                      "The package.order file does not list all .mo files and directories (containing package.mo) "
                      "present in its directory.\nMissing names are:\n\t" +
                          join_strings(missing, "\n\t"));
        for (const std::string& name : missing)
          layout.push_back({OrderedKind::External, Id<Element>{}, name});
      }
    }
  }

  if (!valid_order) {
    layout.clear();
    // 无 order：外部子项逆字典序，然后 package.mo 元素（ClassLoader.mo:552-562；探针 NoOrd2）。
    std::vector<Sub> descending = subs;
    std::sort(descending.begin(), descending.end(),
              [](const Sub& a, const Sub& b) { return a.name > b.name; });
    for (const Sub& sub : descending)
      layout.push_back({OrderedKind::External, Id<Element>{}, sub.name});
    for (Id<Element> eid : package_elements)
      layout.push_back({OrderedKind::Existing, eid, {}});
  }

  // 4) 外部子项的任一失败都使整个库失败（ClassLoader.mo:402-429 的 fail 向上传播）。
  std::vector<Id<Element>> result;
  result.reserve(layout.size());
  for (const OrderedItem& item : layout) {
    if (item.kind == OrderedKind::Existing) {
      result.push_back(item.element);
      continue;
    }
    auto it = std::find_if(subs.begin(), subs.end(), [&](const Sub& sub) { return sub.name == item.name; });
    if (it == subs.end()) return Id<ClassDef>{};
    Id<ClassDef> child = it->is_dir ? load_dir(it->path, it->name, cls, my_path)
                                    : load_file_entity(it->path, it->name, cls, my_path);
    if (!child.is_valid()) return Id<ClassDef>{};
    result.push_back(a_.classes[cls].elements.back());
  }
  a_.classes[cls].elements = std::move(result);
  return cls;
}

// -------------------------------------------------------------- 注解元数据（MLS §18.8.2）
namespace {

// 取 ModArg 的字符串绑定（"..."），不是字符串返回 false
bool string_binding(const Absyn& a, const ModArg& arg, std::string& out) {
  if (!arg.binding.is_valid()) return false;
  const Exp& e = a.exps[arg.binding];
  if (e.kind != ExpKind::StringLit) return false;
  out = std::string(a.interner.str(e.sval));
  return true;
}

// version = "x" 或 version = {"x", "y"}
void string_or_array_binding(const Absyn& a, const ModArg& arg, std::vector<std::string>& out) {
  std::string s;
  if (string_binding(a, arg, s)) {
    out.push_back(s);
    return;
  }
  if (!arg.binding.is_valid()) return;
  const Exp& e = a.exps[arg.binding];
  if (e.kind != ExpKind::Array) return;
  for (Id<Exp> el : e.args) {
    const Exp& x = a.exps[el];
    if (x.kind == ExpKind::StringLit) out.push_back(std::string(a.interner.str(x.sval)));
  }
}

bool arg_is(const Absyn& a, const ModArg& arg, const char* name) {
  return arg.path.size() == 1 && a.interner.str(arg.path[0]) == name;
}

}  // namespace

LibraryMeta library_meta(const Absyn& a, Id<ClassDef> cls) {
  LibraryMeta m;
  if (!cls.is_valid()) return m;
  Id<Modification> ann = a.classes[cls].annotation;
  if (!ann.is_valid()) return m;
  for (const ModArg& arg : a.mods[ann].args) {
    if (arg_is(a, arg, "version")) {
      string_binding(a, arg, m.version);
    } else if (arg_is(a, arg, "conversion") && arg.sub.is_valid()) {
      for (const ModArg& c : a.mods[arg.sub].args) {
        if (arg_is(a, c, "noneFromVersion")) {
          std::string v;
          if (string_binding(a, c, v)) m.none_from.push_back(v);
        } else if (arg_is(a, c, "from") && c.sub.is_valid()) {
          for (const ModArg& kv : a.mods[c.sub].args)
            if (arg_is(a, kv, "version")) string_or_array_binding(a, kv, m.convert_from);
        }
      }
    } else if (arg_is(a, arg, "uses") && arg.sub.is_valid()) {
      for (const ModArg& lib : a.mods[arg.sub].args) {
        // Interactive.mo:7511-7548：只认 Name(version=...) 且恰好一个参数；裸项/多参数静默跳过。
        if (lib.path.size() != 1 || !lib.sub.is_valid()) continue;
        const std::vector<ModArg>& args = a.mods[lib.sub].args;
        if (args.size() != 1 || !arg_is(a, args[0], "version")) continue;
        std::string ver;
        std::string name(a.interner.str(lib.path[0]));
        if (!string_binding(a, args[0], ver)) {
          ver = "default";
          m.uses_missing_version.push_back(name);
        }
        m.uses.emplace_back(std::move(name), std::move(ver));
      }
    }
  }
  return m;
}

LibraryMeta read_library_meta(const fs::path& root_file, Interner& interner, bool pure_impure_keywords) {
  std::string src;
  if (!read_text(root_file, src)) return LibraryMeta{};
  Diagnostics tmp_diags;       // 元数据读取不报错（真正装载时再报）
  Absyn tmp(interner);         // 临时树：候选库不一定被选中，不能进主树
  Lexer lx(src, 0, interner, tmp_diags, pure_impure_keywords);
  std::vector<Token> toks = lx.tokenize();
  Parser p(toks, tmp, tmp_diags);
  StoredDefinition sd = p.parse_stored_definition();
  if (sd.classes.empty()) return LibraryMeta{};
  return library_meta(tmp, sd.classes[0]);
}

// -------------------------------------------------------------- uses
void Loader::follow_uses(Id<ClassDef> top) {
  // 装载后立即递归、forceLoad=false（CevalScript.mo:529-544）。成功且 notify=false 时静默。
  LibraryMeta m = library_meta(a_, top);
  for (const std::string& name : m.uses_missing_version)
    diags_.report(DiagLevel::Warning, a_.classes[top].loc,
                  "Uses-annotation is missing version for library " + name +
                      ". Assuming the tool-specific version=\"default\".");  // Error.mo:1034-1035
  std::string origin(a_.interner.str(a_.classes[top].name));
  for (const auto& dependency : m.uses)
    load_library_impl(dependency.first, dependency.second, false, origin);
}

// -------------------------------------------------------------- 类名枚举
void collect_class_names(const Absyn& a, Id<ClassDef> root, const std::string& prefix,
                         bool include_protected, std::vector<std::string>& out) {
  const ClassDef& c = a.classes[root];
  for (Id<Element> eid : c.elements) {
    const Element& el = a.elems[eid];
    if (el.kind != ElementKind::ClassDef || !el.cls.is_valid()) continue;
    if (el.is_protected && !include_protected) continue;
    std::string name = prefix + std::string(a.interner.str(a.classes[el.cls].name));
    out.push_back(name);
    collect_class_names(a, el.cls, name + ".", include_protected, out);
  }
}

}  // namespace ymc
