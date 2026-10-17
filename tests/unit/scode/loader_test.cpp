// S2 装载器测试：在临时目录里搭一个小库，覆盖 MLS §13.3/§13.4.1/§13.4.2/§13.4.3/§18.8.3 与 uses。
#include "ymc_test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "absyn/absyn.h"
#include "absyn/absyn_util.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "scode/loader.h"

namespace fs = std::filesystem;
using namespace ymc;

namespace {

void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f << text;
}

// 临时库布局：
//   Lib 1.0.0/package.mo          package Lib  constant Real k = 1; end Lib;
//   Lib 1.0.0/package.order       B, k, A      （故意把 B 放在 A 前面、常量夹中间）
//   Lib 1.0.0/A.mo                within Lib; model A end A;
//   Lib 1.0.0/B/package.mo        within Lib; package B end B;
//   Lib 1.0.0/B/C.mo              within Lib.B; model C end C;
//   Lib 1.0.0/B/Resources/x.txt   非结构化目录，应忽略
//   Lib 2.0.0/package.mo          package Lib annotation(uses(Single(version = "1.0"))); end Lib;
//   Single.mo                     package Single model X end X; end Single;
fs::path make_fixture() {
  fs::path root = fs::temp_directory_path() / "ymc_loader_test_fixture";
  fs::remove_all(root);
  put(root / "Lib 1.0.0" / "package.mo",
      "package Lib\n  constant Real k = 1;\n"
      "  annotation(version = \"1.0.0\", conversion(noneFromVersion = \"0.9\", noneFromVersion = \"0.8\","
      " from(version = {\"0.5\", \"0.6\"}, script = \"x.mos\"), from(version = \"0.4\", script = \"y.mos\")));\n"
      "end Lib;\n");
  put(root / "Lib 1.0.0" / "package.order", "B\r\nk\r\nA\r\n");
  put(root / "Lib 1.0.0" / "A.mo", "within Lib;\nmodel A\nend A;\n");
  put(root / "Lib 1.0.0" / "B" / "package.mo", "within Lib;\npackage B\nend B;\n");
  put(root / "Lib 1.0.0" / "B" / "C.mo", "within Lib.B;\nmodel C\nend C;\n");
  put(root / "Lib 1.0.0" / "B" / "Resources" / "x.txt", "not modelica\n");
  put(root / "Lib 2.0.0" / "package.mo",
      "package Lib\n  annotation(uses(Single(version = \"1.0\")));\nend Lib;\n");
  put(root / "Single.mo", "package Single\n  model X\n  end X;\nend Single;\n");
  return root;
}

fs::path r8_fixture() { return fs::path(YMC_TEST_FIXTURES) / "loader-r8"; }

bool has_diag(const Diagnostics& diagnostics, DiagLevel level, std::string_view text) {
  for (const Diagnostic& diagnostic : diagnostics.items())
    if (diagnostic.level == level && diagnostic.msg.find(text) != std::string::npos) return true;
  return false;
}

std::vector<std::string> class_names(const Absyn& absyn, Id<ClassDef> root,
                                     bool include_protected = false) {
  std::vector<std::string> names;
  std::string prefix(absyn.interner.str(absyn.classes[root].name));
  collect_class_names(absyn, root, prefix + ".", include_protected, names);
  for (std::string& name : names) name.erase(0, prefix.size() + 1);
  names.erase(std::remove_if(names.begin(), names.end(),
                             [](const std::string& name) { return name.find('.') != std::string::npos; }),
              names.end());
  return names;
}

LoaderOptions r8_options() {
  LoaderOptions options;
  options.roots = {r8_fixture()};
  return options;
}

struct R8Loader {
  Interner interner;
  Diagnostics diagnostics;
  Absyn absyn;
  Loader loader;

  R8Loader() : absyn(interner), loader(absyn, diagnostics, r8_options()) {}
};

}  // namespace

YMC_TEST(load_versioned_dir_library_with_package_order) {
  fs::path root = make_fixture();
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions opt;
  opt.roots = {root};
  Loader L(a, d, opt);
  Id<ClassDef> lib = L.load_library("Lib", "1.0.0");
  YMC_CHECK(lib.is_valid());
  YMC_CHECK(!d.has_errors());
  YMC_CHECK_EQ(a.top.size(), std::size_t{1});
  std::vector<std::string> names;
  collect_class_names(a, lib, "Lib.", true, names);
  // package.order 说 B 在 A 前；B 的子孙紧跟 B（先序）
  YMC_CHECK_EQ(names.size(), std::size_t{3});
  YMC_CHECK_EQ(names[0], std::string("Lib.B"));
  YMC_CHECK_EQ(names[1], std::string("Lib.B.C"));
  YMC_CHECK_EQ(names[2], std::string("Lib.A"));
  // 常量 k 也在元素表里且按 package.order 排在 B 与 A 之间
  const ClassDef& c = a.classes[lib];
  YMC_CHECK_EQ(c.elements.size(), std::size_t{3});
  YMC_CHECK(a.elems[c.elements[0]].kind == ElementKind::ClassDef);   // B
  YMC_CHECK(a.elems[c.elements[1]].kind == ElementKind::Component);  // k
  YMC_CHECK(a.elems[c.elements[2]].kind == ElementKind::ClassDef);   // A
  YMC_CHECK_EQ(L.loaded().size(), std::size_t{1});
  YMC_CHECK(L.loaded()[0].version.nums == std::vector<int>({1, 0, 0}));
  fs::remove_all(root);
}

YMC_TEST(default_version_is_highest_and_uses_is_followed) {
  fs::path root = make_fixture();
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions opt;
  opt.roots = {fs::path("Z:/does/not/exist"), root};  // 不存在的根被跳过（§13.3 有序根列表）
  Loader L(a, d, opt);
  Id<ClassDef> lib = L.load_library("Lib");  // 未指定版本 → 2.0.0
  YMC_CHECK(lib.is_valid());
  YMC_CHECK(!d.has_errors());
  YMC_CHECK(L.loaded()[0].version.nums == std::vector<int>({2, 0, 0}));
  // uses(Single(version="1.0")) 被递归装载：Single.mo 是单文件库（§18.8.3 第一种形式）
  YMC_CHECK_EQ(L.loaded().size(), std::size_t{2});
  YMC_CHECK_EQ(L.loaded()[1].name, std::string("Single"));
  std::vector<Symbol> p{in.intern("Single"), in.intern("X")};
  YMC_CHECK(find_class(a, p).is_valid());
  YMC_CHECK_EQ(a.top.size(), std::size_t{2});
  fs::remove_all(root);
}

YMC_TEST(missing_version_and_missing_library_report_errors) {
  fs::path root = make_fixture();
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions opt;
  opt.roots = {root};
  Loader L(a, d, opt);
  YMC_CHECK(!L.load_library("Lib", "3.0.0").is_valid());  // 没有这个版本
  YMC_CHECK(d.has_errors());
  YMC_CHECK(d.items()[0].msg.find("Failed to load package Lib (3.0.0)") == 0);
  YMC_CHECK(!L.load_library("Nope").is_valid());
  fs::remove_all(root);
}

YMC_TEST(within_mismatch_fails_the_whole_library) {
  fs::path root = fs::temp_directory_path() / "ymc_loader_test_fixture2";
  fs::remove_all(root);
  put(root / "W" / "package.mo", "package W\nend W;\n");
  put(root / "W" / "A.mo", "within Wrong;\nmodel A\nend A;\n");
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions opt;
  opt.roots = {root};
  Loader L(a, d, opt);
  Id<ClassDef> w = L.load_library("W");
  YMC_CHECK(!w.is_valid());
  YMC_CHECK(has_diag(d, DiagLevel::Error, "Expected the package to have within W; but got within Wrong;."));
  YMC_CHECK(has_diag(d, DiagLevel::Error, "Failed to load package W (default)"));
  YMC_CHECK(a.top.empty());
  YMC_CHECK(L.loaded().empty());
  fs::remove_all(root);
}

// MLS §18.8.2：version / conversion(noneFromVersion, from(version=… | {…})) / uses 的元数据读取
YMC_TEST(library_meta_from_annotation) {
  fs::path root = make_fixture();
  Interner in;
  LibraryMeta m = read_library_meta(root / "Lib 1.0.0" / "package.mo", in);
  YMC_CHECK_EQ(m.version, std::string("1.0.0"));
  YMC_CHECK(m.none_from == std::vector<std::string>({"0.9", "0.8"}));
  YMC_CHECK(m.convert_from == std::vector<std::string>({"0.5", "0.6", "0.4"}));
  YMC_CHECK(m.uses.empty());
  LibraryMeta u = read_library_meta(root / "Lib 2.0.0" / "package.mo", in);
  YMC_CHECK_EQ(u.uses.size(), std::size_t{1});
  YMC_CHECK_EQ(u.uses[0].first, std::string("Single"));
  YMC_CHECK_EQ(u.uses[0].second, std::string("1.0"));
  YMC_CHECK(u.version.empty());
  LibraryMeta none = read_library_meta(root / "nope.mo", in);
  YMC_CHECK(none.version.empty() && none.uses.empty());
  fs::remove_all(root);
}

// 版本选择：复刻本机 MSL 的注解结构（探针 2026-09-16 的 13 个用例，见 loader.cpp 规则说明）
//   Lib 3.2.3: noneFromVersion 3.2.2/3.2.1/3.2/3.1/3.0.1/3.0, from(2.1/2.2/2.2.1/2.2.2)
//   Lib 4.0.0: from({3.0,3.0.1,3.1,3.2,3.2.1,3.2.2,3.2.3})
YMC_TEST(version_selection_matches_omc_probes) {
  fs::path root = fs::temp_directory_path() / "ymc_loader_test_versions";
  fs::remove_all(root);
  put(root / "Lib 3.2.3+maint.om" / "package.mo",
      "package Lib\n  annotation(version = \"3.2.3\", conversion(noneFromVersion = \"3.2.2\", "
      "noneFromVersion = \"3.2.1\", noneFromVersion = \"3.2\", noneFromVersion = \"3.1\", "
      "noneFromVersion = \"3.0.1\", noneFromVersion = \"3.0\", "
      "from(version = \"2.1\", script = \"a.mos\"), from(version = \"2.2\", script = \"a.mos\"), "
      "from(version = \"2.2.1\", script = \"a.mos\"), from(version = \"2.2.2\", script = \"a.mos\")));\n"
      "end Lib;\n");
  put(root / "Lib 4.0.0+maint.om" / "package.mo",
      "package Lib\n  annotation(version = \"4.0.0\", conversion(from(version = {\"3.0\", \"3.0.1\", \"3.1\", "
      "\"3.2\", \"3.2.1\", \"3.2.2\", \"3.2.3\"}, script = \"b.mos\")));\nend Lib;\n");
  auto pick = [&](const char* req) -> std::string {
    Interner in;
    Diagnostics d;
    Absyn a(in);
    LoaderOptions opt;
    opt.roots = {root};
    opt.follow_uses = false;
    Loader L(a, d, opt);
    Id<ClassDef> c = L.load_library("Lib", req);
    if (!c.is_valid()) return "FAIL";
    return L.loaded()[0].version.raw;
  };
  YMC_CHECK_EQ(pick("3.2.3"), std::string("3.2.3+maint.om"));   // 种类 0
  YMC_CHECK_EQ(pick("4.0"), std::string("4.0.0+maint.om"));     // 数值相等
  YMC_CHECK_EQ(pick("3.2.2"), std::string("3.2.3+maint.om"));   // 种类 1 优先于 4.0.0 的 from
  YMC_CHECK_EQ(pick("3.0"), std::string("3.2.3+maint.om"));
  YMC_CHECK_EQ(pick("2.2.2"), std::string("4.0.0+maint.om"));   // 种类 2 链式 → 最高
  YMC_CHECK_EQ(pick("2.1"), std::string("4.0.0+maint.om"));
  YMC_CHECK_EQ(pick("3.2.4"), std::string("FAIL"));             // 不是已知发行版 → 失败，虽有更新的库
  YMC_CHECK_EQ(pick("3.3"), std::string("FAIL"));
  YMC_CHECK_EQ(pick("2.0"), std::string("FAIL"));
  YMC_CHECK_EQ(pick(""), std::string("4.0.0+maint.om"));        // 默认最高
  fs::remove_all(root);
}

// 前缀逐级放宽（systemimpl.c :2229-2251）：已知发行版 3.1.5 本地没有；3.2.3 与 4.0.0 都 ≥ 它，
// OMC 先试"前 1 级相等"（3.x）→ 取 3.2.3，而不是全局最高 4.0.0。
YMC_TEST(version_prefix_relaxation_prefers_closest) {
  fs::path root = fs::temp_directory_path() / "ymc_loader_test_prefix";
  fs::remove_all(root);
  put(root / "Lib 3.2.3" / "package.mo",
      "package Lib\n  annotation(version = \"3.2.3\", conversion(from(version = \"3.1.5\", script = \"a.mos\")));\nend Lib;\n");
  put(root / "Lib 4.0.0" / "package.mo",
      "package Lib\n  annotation(version = \"4.0.0\", conversion(from(version = \"3.2.3\", script = \"b.mos\")));\nend Lib;\n");
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions opt;
  opt.roots = {root};
  opt.follow_uses = false;
  Loader L(a, d, opt);
  YMC_CHECK(L.load_library("Lib", "3.1.5").is_valid());
  YMC_CHECK_EQ(L.loaded()[0].version.raw, std::string("3.2.3"));
  fs::remove_all(root);
}

YMC_TEST(split_modelica_path_windows) {
  std::vector<fs::path> r = Loader::split_modelica_path("C:/a;\"D:/b c\"; ;E:/d");
  YMC_CHECK_EQ(r.size(), std::size_t{3});
  YMC_CHECK_EQ(r[1].generic_string(), std::string("D:/b c"));
}

YMC_TEST(r8_part_appends_unlisted_child) {
  R8Loader f;
  Id<ClassDef> root = f.loader.load_library("Part");
  YMC_CHECK(root.is_valid());
  YMC_CHECK(class_names(f.absyn, root) == std::vector<std::string>({"B", "P", "A"}));
  YMC_CHECK(has_diag(f.diagnostics, DiagLevel::Warning,
                     "The package.order file does not list all .mo files and directories"));
}

YMC_TEST(r8_good_merges_constants_classes_and_files) {
  R8Loader f;
  Id<ClassDef> root = f.loader.load_library("Good");
  YMC_CHECK(root.is_valid());
  YMC_CHECK(class_names(f.absyn, root) == std::vector<std::string>({"Z", "M", "A"}));
  YMC_CHECK(f.diagnostics.items().empty());
}

YMC_TEST(r8_missing_order_entry_discards_the_order_file) {
  R8Loader f;
  Id<ClassDef> root = f.loader.load_library("Miss");
  YMC_CHECK(root.is_valid());
  YMC_CHECK(class_names(f.absyn, root) == std::vector<std::string>({"B", "A", "P"}));
  YMC_CHECK(has_diag(f.diagnostics, DiagLevel::Warning, "Missing was referenced in the package.order file"));
  YMC_CHECK(!has_diag(f.diagnostics, DiagLevel::Warning,
                      "The package.order file does not list all .mo files and directories"));
}

YMC_TEST(r8_noord2_uses_reverse_external_then_inline_order) {
  R8Loader f;
  Id<ClassDef> root = f.loader.load_library("NoOrd2");
  YMC_CHECK(root.is_valid());
  YMC_CHECK(class_names(f.absyn, root) ==
            std::vector<std::string>({"C", "B", "A", "P1", "Q1"}));
  YMC_CHECK(f.diagnostics.items().empty());
}

YMC_TEST(r8_noorder_preserves_protected_flag) {
  R8Loader f;
  Id<ClassDef> root = f.loader.load_library("NoOrder");
  YMC_CHECK(root.is_valid());
  YMC_CHECK(class_names(f.absyn, root, true) ==
            std::vector<std::string>({"Sub", "C", "B", "A", "P1", "P2"}));
  YMC_CHECK(f.diagnostics.items().empty());
}

YMC_TEST(r8_baz_discards_order_after_inline_is_consumed_as_external) {
  R8Loader f;
  Id<ClassDef> root = f.loader.load_library("Baz");
  YMC_CHECK(root.is_valid());
  YMC_CHECK(class_names(f.absyn, root) == std::vector<std::string>({"C", "B", "A", "Inline"}));
  YMC_CHECK(has_diag(f.diagnostics, DiagLevel::Warning,
                     "Inline was referenced in the package.order file, but was not found"));
  YMC_CHECK(has_diag(f.diagnostics, DiagLevel::Warning, "Got element k2 that was not referenced"));
  YMC_CHECK(!f.diagnostics.has_errors());
}

YMC_TEST(r8_sp_trims_whitespace_and_empty_lines) {
  R8Loader f;
  Id<ClassDef> root = f.loader.load_library("Sp");
  YMC_CHECK(root.is_valid());
  YMC_CHECK(class_names(f.absyn, root) == std::vector<std::string>({"A"}));
  YMC_CHECK(f.diagnostics.items().empty());
}

YMC_TEST(r8_qux_wrong_within_fails_atomically) {
  R8Loader f;
  YMC_CHECK(!f.loader.load_library("Qux").is_valid());
  YMC_CHECK(has_diag(f.diagnostics, DiagLevel::Error,
                     "Expected the package to have within Qux; but got within Nope;."));
  YMC_CHECK(has_diag(f.diagnostics, DiagLevel::Error, "Failed to load package Qux (default)"));
  YMC_CHECK(f.absyn.top.empty());
  YMC_CHECK(f.loader.loaded().empty());
}

YMC_TEST(r8_wrong_wrong_within_fails_atomically) {
  R8Loader f;
  YMC_CHECK(!f.loader.load_library("Wrong").is_valid());
  YMC_CHECK(has_diag(f.diagnostics, DiagLevel::Error,
                     "Expected the package to have within Wrong; but got within Nope;."));
  YMC_CHECK(f.absyn.top.empty());
  YMC_CHECK(f.loader.loaded().empty());
}

YMC_TEST(r8_duplicate_child_file_and_directory_fails) {
  R8Loader f;
  YMC_CHECK(!f.loader.load_library("Dupin").is_valid());
  YMC_CHECK(has_diag(f.diagnostics, DiagLevel::Error, "The same class is defined in multiple files:"));
  YMC_CHECK(f.absyn.top.empty());
}

YMC_TEST(r8_two_classes_in_one_library_file_fails) {
  R8Loader f;
  YMC_CHECK(!f.loader.load_library("Two").is_valid());
  YMC_CHECK(has_diag(f.diagnostics, DiagLevel::Error,
                     "exactly one package, but found the following classes: Two, Extra."));
  YMC_CHECK(f.absyn.top.empty());
}

YMC_TEST(r8_bom_file_loads_without_diagnostic) {
  R8Loader f;
  YMC_CHECK(f.loader.load_library("Bom").is_valid());
  YMC_CHECK(f.diagnostics.items().empty());
}

YMC_TEST(r8_bar_single_file_loads_without_diagnostic) {
  R8Loader f;
  YMC_CHECK(f.loader.load_library("Bar").is_valid());
  YMC_CHECK(f.diagnostics.items().empty());
}

YMC_TEST(r8_top_level_equal_version_uses_later_sorted_entry) {
  R8Loader f;
  YMC_CHECK(f.loader.load_library("Dup").is_valid());
  YMC_CHECK(f.diagnostics.items().empty());
  YMC_CHECK_EQ(f.loader.loaded().size(), std::size_t{1});
  YMC_CHECK_EQ(f.loader.loaded()[0].path.filename().generic_string(), std::string("Dup.mo"));
}

// Foo 不在 OMC 的包索引里，也没有任何 conversion 注解：请求原样进 C 层，做前缀逐级放宽（R8 "判别探针" 表，omc 1.25.0 实测）。
YMC_TEST(r8_foo_selection_matches_omc_c_layer) {
  auto pick = [](std::string_view requested) {
    R8Loader f;
    Id<ClassDef> root = f.loader.load_library("Foo", requested);
    return root.is_valid() ? f.loader.loaded()[0].path.filename().generic_string() : std::string("FAIL");
  };
  YMC_CHECK_EQ(pick(""), std::string("Foo 3.0.0-beta.1"));
  YMC_CHECK_EQ(pick("1"), std::string("Foo 1.0.0"));
  YMC_CHECK_EQ(pick("2.0"), std::string("Foo 2.0.0"));
  YMC_CHECK_EQ(pick("1.5"), std::string("Foo 3.0.0-beta.1"));         // j=0：≥ 1.5 的最高者
  YMC_CHECK_EQ(pick("0.5"), std::string("Foo 3.0.0-beta.1"));
  YMC_CHECK_EQ(pick("3.0.0"), std::string("Foo 3.0.0-beta.1"));       // 6 级全等，条目后缀 "-…" 合法
  YMC_CHECK_EQ(pick("3.0.0-beta"), std::string("Foo 3.0.0-beta.1"));  // 后缀前缀匹配
  YMC_CHECK_EQ(pick("3.0.0-rc"), std::string("FAIL"));
  YMC_CHECK_EQ(pick("abc"), std::string("FAIL"));
}

// uses 指向一个畸形库：依赖自己的 Error 留在诊断里（OMC 的 try/else 不回滚错误缓冲，CevalScript.mo:502-545），
// 外加一条 "Skipped loading" Note；父库照常成功，畸形依赖不进 top / loaded()。
YMC_TEST(uses_malformed_dependency_keeps_its_errors_but_parent_loads) {
  fs::path root = fs::temp_directory_path() / "ymc_loader_test_bad_dep";
  fs::remove_all(root);
  put(root / "Par.mo", "package Par\n  annotation(uses(BadDep(version = \"1.0\")));\nend Par;\n");
  put(root / "BadDep 1.0" / "package.mo", "package BadDep\nend BadDep;\n");
  put(root / "BadDep 1.0" / "X.mo", "within Nope;\nmodel X\nend X;\n");
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions opt;
  opt.roots = {root};
  Loader L(a, d, opt);
  Id<ClassDef> par = L.load_library("Par");
  YMC_CHECK(par.is_valid());
  YMC_CHECK_EQ(a.top.size(), std::size_t{1});
  YMC_CHECK_EQ(L.loaded().size(), std::size_t{1});
  bool saw_within_error = false, saw_skip_note = false, saw_failed_error = false;
  for (const Diagnostic& x : d.items()) {
    if (x.level == DiagLevel::Error && x.msg.find("Expected the package to have within BadDep;") != std::string::npos)
      saw_within_error = true;
    if (x.level == DiagLevel::Note && x.msg.find("Skipped loading package BadDep (1.0)") != std::string::npos)
      saw_skip_note = true;
    if (x.msg.find("Failed to load package BadDep") != std::string::npos) saw_failed_error = true;
  }
  YMC_CHECK(saw_within_error);
  YMC_CHECK(saw_skip_note);
  YMC_CHECK(!saw_failed_error);  // "Failed to load" 只属于显式（forceLoad）装载
}

YMC_TEST(r8_uses_missing_dependency_is_only_a_note) {
  R8Loader f;
  Id<ClassDef> root = f.loader.load_library("UsesTest");
  YMC_CHECK(root.is_valid());
  YMC_CHECK(!f.diagnostics.has_errors());
  YMC_CHECK(has_diag(f.diagnostics, DiagLevel::Note,
                     "Skipped loading package Missing (1.0) using MODELICAPATH"));
  YMC_CHECK_EQ(f.loader.loaded().size(), std::size_t{2});
  YMC_CHECK_EQ(f.loader.loaded()[1].name, std::string("Foo"));
}

YMC_TEST(uses_version_compatibility_uses_loaded_library_annotations) {
  fs::path root = make_fixture();
  put(root / "UsesNone.mo", "package UsesNone annotation(uses(Lib(version=\"0.9\"))); end UsesNone;\n");
  put(root / "UsesConvert.mo", "package UsesConvert annotation(uses(Lib(version=\"0.5\"))); end UsesConvert;\n");
  put(root / "UsesNewer.mo", "package UsesNewer annotation(uses(Lib(version=\"2.0\"))); end UsesNewer;\n");
  put(root / "UsesOlder.mo", "package UsesOlder annotation(uses(Lib(version=\"0.7\"))); end UsesOlder;\n");
  put(root / "UsesSame.mo", "package UsesSame annotation(uses(Lib(version=\"1.0\"))); end UsesSame;\n");
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions options;
  options.roots = {root};
  Loader loader(a, d, options);
  YMC_CHECK(loader.load_library("Lib", "1.0").is_valid());
  YMC_CHECK(loader.load_library("UsesNone").is_valid());
  YMC_CHECK(loader.load_library("UsesConvert").is_valid());
  YMC_CHECK(loader.load_library("UsesNewer").is_valid());
  YMC_CHECK(loader.load_library("UsesOlder").is_valid());
  std::size_t diagnostics_before_same = d.items().size();
  YMC_CHECK(loader.load_library("UsesSame").is_valid());
  YMC_CHECK_EQ(d.items().size(), diagnostics_before_same);
  YMC_CHECK(has_diag(d, DiagLevel::Note, "fully compatible without conversion script needed"));
  YMC_CHECK(has_diag(d, DiagLevel::Warning, "only compatible with a conversion script"));
  YMC_CHECK(has_diag(d, DiagLevel::Warning, "2.0 is newer than 1.0.0"));
  YMC_CHECK(has_diag(d, DiagLevel::Warning, "0.7 is older than 1.0.0"));
  fs::remove_all(root);
}

YMC_TEST(uses_non_string_version_warns_and_loads_default) {
  fs::path root = fs::temp_directory_path() / "ymc_loader_uses_default";
  fs::remove_all(root);
  put(root / "Dep 1.0" / "package.mo", "package Dep annotation(version=\"1.0\"); end Dep;\n");
  put(root / "Parent.mo", "package Parent annotation(uses(Dep(version=1), Bare, Dep(extra=1, version=\"1\"))); end Parent;\n");
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions options;
  options.roots = {root};
  Loader loader(a, d, options);
  YMC_CHECK(loader.load_library("Parent").is_valid());
  YMC_CHECK(!d.has_errors());
  YMC_CHECK(has_diag(d, DiagLevel::Warning, "Uses-annotation is missing version for library Dep"));
  YMC_CHECK_EQ(loader.loaded().size(), std::size_t{2});
  fs::remove_all(root);
}

YMC_TEST(package_mo_short_class_fails) {
  fs::path root = fs::temp_directory_path() / "ymc_loader_short_package";
  fs::remove_all(root);
  put(root / "Short" / "package.mo", "package Short = Base;\n");
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions options;
  options.roots = {root};
  Loader loader(a, d, options);
  YMC_CHECK(!loader.load_library("Short").is_valid());
  YMC_CHECK(has_diag(d, DiagLevel::Error, "needs to be based on class parts"));
  fs::remove_all(root);
}

YMC_TEST(file_name_case_only_mismatch_warns_and_continues) {
  fs::path root = fs::temp_directory_path() / "ymc_loader_name_case";
  fs::remove_all(root);
  put(root / "lower.mo", "package Lower end Lower;\n");
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions options;
  options.roots = {root};
  Loader loader(a, d, options);
  YMC_CHECK(loader.load_library("lower").is_valid());
  YMC_CHECK(!d.has_errors());
  YMC_CHECK(has_diag(d, DiagLevel::Warning, "Expected the package to have name lower, but got Lower"));
  fs::remove_all(root);
}

YMC_TEST(within_case_only_mismatch_warns_and_continues) {
  fs::path root = fs::temp_directory_path() / "ymc_loader_within_case";
  fs::remove_all(root);
  put(root / "Upper" / "package.mo", "package Upper end Upper;\n");
  put(root / "Upper" / "A.mo", "within upper; model A end A;\n");
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions options;
  options.roots = {root};
  Loader loader(a, d, options);
  YMC_CHECK(loader.load_library("Upper").is_valid());
  YMC_CHECK(!d.has_errors());
  YMC_CHECK(has_diag(d, DiagLevel::Warning,
                     "Expected the package to have within Upper; but got within upper;"));
  fs::remove_all(root);
}

YMC_TEST(encrypted_package_moc_is_skipped_with_warning) {
  fs::path root = fs::temp_directory_path() / "ymc_loader_encrypted";
  fs::remove_all(root);
  put(root / "Secret" / "package.moc", "encrypted\n");
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions options;
  options.roots = {root};
  Loader loader(a, d, options);
  YMC_CHECK(!loader.load_library("Secret").is_valid());
  YMC_CHECK(has_diag(d, DiagLevel::Warning, "Encrypted Modelica library is not supported"));
  fs::remove_all(root);
}

// Case 故意不测：OMC 1.25.0 的 {A,A} 结果依赖 Windows 大小写不敏感文件系统，是已知缺陷。

YMC_TEST(class_files_record_top_level_sources) {
  // 顶层类的来源文件记进 Absyn::class_files（omc 的 URI 映射取它的目录：SymbolTable.updateUriMapping，SymbolTable.mo:568-599
  // v1.25.0）：目录形式的库记它的 package.mo、单文件库记 X.mo 本身。omc 1.25.0 实跑的对应形态（2026-10-17，OPENMODELICALIBRARY
  // 指向临时库根的手工探针 URZX）：`modelica://UrzDir/none.txt` → `<库根>/UrzDir 1.0/none.txt`、`modelica://UrzLib/none.txt` →
  // `<库根>/none.txt`（目录由 base/omc_uri.h 的 omc_source_dir 从这里记的文件求）。
  fs::path root = fs::temp_directory_path() / "ymc_loader_class_files";
  fs::remove_all(root);
  put(root / "UrzDir 1.0" / "package.mo", "package UrzDir\nend UrzDir;\n");
  put(root / "UrzLib.mo", "package UrzLib\nend UrzLib;\n");
  Interner in;
  Diagnostics d;
  Absyn a(in);
  LoaderOptions options;
  options.roots = {root};
  Loader loader(a, d, options);
  YMC_CHECK(loader.load_library("UrzDir").is_valid());
  YMC_CHECK(loader.load_library("UrzLib").is_valid());
  YMC_CHECK_EQ(a.class_files[in.intern("UrzDir")], (root / "UrzDir 1.0" / "package.mo").generic_string());
  YMC_CHECK_EQ(a.class_files[in.intern("UrzLib")], (root / "UrzLib.mo").generic_string());
  fs::remove_all(root);
}

YMC_TEST_MAIN
