// S2 基础工具测试：版本号（MLS §18.8.1/§18.8.3）、Absyn 找类/插类（§13.4.1/§13.4.3）、
// 多文件解析入口 parse_stored_definition（A.2.1）。
#include "ymc_test.h"

#include "absyn/absyn.h"
#include "absyn/absyn_util.h"
#include "base/diag.h"
#include "base/symbol.h"
#include "parse/lexer.h"
#include "parse/parser.h"
#include "scode/version.h"

#include <string>

using namespace ymc;

// ---- 版本号 ----
YMC_TEST(version_parse_forms) {
  Version a = parse_version("4.0.0");
  YMC_CHECK(a.nums == std::vector<int>({4, 0, 0}));
  YMC_CHECK(a.prerelease.empty() && a.build.empty() && !a.is_unordered());

  Version p = parse_version("3.2.1 Beta 1");  // §18.8.1 pre-release
  YMC_CHECK(p.nums == std::vector<int>({3, 2, 1}));
  YMC_CHECK_EQ(p.prerelease, std::string("Beta 1"));
  YMC_CHECK(p.is_prerelease());

  Version m = parse_version("4.0.0+maint.om");  // OMC 目录名
  YMC_CHECK(m.nums == std::vector<int>({4, 0, 0}));
  YMC_CHECK_EQ(m.build, std::string("maint.om"));
  YMC_CHECK(m.prerelease.empty());

  Version u = parse_version("Test 1");  // §18.8.1 un-ordered
  YMC_CHECK(u.is_unordered());
  YMC_CHECK(parse_version("1.2b").is_unordered());  // 不合三种形式
}

YMC_TEST(version_ordering) {
  YMC_CHECK(compare_versions(parse_version("3.2.3"), parse_version("4.0.0")) < 0);
  YMC_CHECK(compare_versions(parse_version("2.1"), parse_version("2.1.0")) == 0);
  YMC_CHECK(compare_versions(parse_version("2.1"), parse_version("2.1 Beta 1")) > 0);   // 正式 > 预发布
  YMC_CHECK(compare_versions(parse_version("2.1 Alpha"), parse_version("2.1 Beta")) < 0);  // 字母序
  YMC_CHECK(compare_versions(parse_version("4.0.0+maint.om"), parse_version("4.0.0")) == 0);  // build 不排序
  YMC_CHECK(same_main_version(parse_version("4.0.0+maint.om"), parse_version("4.0.0")));
  YMC_CHECK(!same_main_version(parse_version("3.2.3"), parse_version("3.2.1")));
}

YMC_TEST(library_entry_names) {
  LibraryEntry e;
  YMC_CHECK(parse_library_entry_name("Modelica 4.0.0+maint.om", true, e));
  YMC_CHECK_EQ(e.name, std::string("Modelica"));
  YMC_CHECK(e.has_version && e.version.nums == std::vector<int>({4, 0, 0}) && !e.is_file);

  YMC_CHECK(parse_library_entry_name("Modelica.mo", false, e));
  YMC_CHECK_EQ(e.name, std::string("Modelica"));
  YMC_CHECK(!e.has_version && e.is_file);

  YMC_CHECK(parse_library_entry_name("ComplexNumbers 2.1.mo", false, e));  // §18.8.3 第二种
  YMC_CHECK_EQ(e.name, std::string("ComplexNumbers"));
  YMC_CHECK(e.has_version && e.version.nums == std::vector<int>({2, 1}) && e.is_file);

  YMC_CHECK(parse_library_entry_name("Modelica 3.2.1 Beta 1", true, e));  // VERSION 含空格
  YMC_CHECK_EQ(e.version.prerelease, std::string("Beta 1"));

  YMC_CHECK(!parse_library_entry_name("readme.txt", false, e));  // 非 .mo
  YMC_CHECK(!parse_library_entry_name(" 1.0", true, e));         // 空名
}

// ---- Absyn 找类 / 插类 / stored-definition ----
static Id<ClassDef> parse_into(Absyn& a, Diagnostics& d, const char* src, StoredDefinition* sd_out = nullptr) {
  Lexer lx(src, 0, a.interner, d);
  std::vector<Token> toks = lx.tokenize();
  Parser p(toks, a, d);
  StoredDefinition sd = p.parse_stored_definition();
  if (sd_out) *sd_out = sd;
  return sd.classes.empty() ? Id<ClassDef>{} : sd.classes[0];
}

YMC_TEST(stored_definition_does_not_touch_top) {
  Interner in;
  Diagnostics d;
  Absyn a(in);
  StoredDefinition sd;
  Id<ClassDef> c = parse_into(a, d, "within A.B;\nmodel C\nend C;\nmodel D\nend D;\n", &sd);
  YMC_CHECK(!d.has_errors());
  YMC_CHECK(c.is_valid());
  YMC_CHECK(sd.has_within);
  YMC_CHECK_EQ(sd.within_path.size(), std::size_t{2});
  YMC_CHECK_EQ(in.str(sd.within_path[1]), std::string_view("B"));
  YMC_CHECK_EQ(sd.classes.size(), std::size_t{2});
  YMC_CHECK_EQ(a.top.size(), std::size_t{0});  // 多文件入口不碰 top
}

YMC_TEST(find_and_insert_class) {
  Interner in;
  Diagnostics d;
  Absyn a(in);
  Id<ClassDef> P = parse_into(a, d, "package P\n  model A\n  end A;\n  type T = Real;\nend P;\n");
  YMC_CHECK(!d.has_errors() && P.is_valid());
  a.top.push_back(P);
  std::vector<Symbol> pP{in.intern("P")}, pPA{in.intern("P"), in.intern("A")},
      pPB{in.intern("P"), in.intern("B")}, pPT{in.intern("P"), in.intern("T")};
  YMC_CHECK(find_class(a, pP) == P);
  YMC_CHECK(find_class(a, pPA).is_valid());
  YMC_CHECK(!find_class(a, pPB).is_valid());
  YMC_CHECK_EQ(qualified_name(a, pPA), std::string("P.A"));

  // 模拟子文件 within P; model B end B;
  Id<ClassDef> B = parse_into(a, d, "within P;\nmodel B\nend B;\n");
  YMC_CHECK(insert_class(a, P, B) == InsertResult::Ok);
  YMC_CHECK(find_class(a, pPB) == B);
  YMC_CHECK(insert_class(a, P, B) == InsertResult::DuplicateName);  // §13.4.1 同名
  Id<ClassDef> T = find_class(a, pPT);
  YMC_CHECK(T.is_valid());
  YMC_CHECK(insert_class(a, T, B) == InsertResult::ParentIsShortClass);  // §13.4.3 短类不能当包围类
}

YMC_TEST_MAIN
