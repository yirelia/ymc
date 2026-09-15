// 任务卡 T-S0-01 的验收测试。你的 symbol.h / symbol.cpp 必须让这个文件编译通过且全部 ok。
// 读这个文件 = 读接口的"行为规格"。每个测试上面的注释说明它在检查哪条不变量。
#include "ymc_test.h"

#include "base/symbol.h"

#include <cstdint>
#include <string>
#include <unordered_set>

using ymc::Interner;
using ymc::Symbol;

// I-1  默认构造的 Symbol 是无效的
YMC_TEST(symbol_default_is_invalid) {
  Symbol s;
  YMC_CHECK(!s.is_valid());
  YMC_CHECK_EQ(s.id, Symbol::kInvalid);
}

// I-2  同一文本 → 同一 Symbol；不同文本 → 不同 Symbol
YMC_TEST(intern_same_text_same_symbol) {
  Interner in;
  Symbol a = in.intern("der");
  Symbol b = in.intern("der");
  Symbol c = in.intern("pre");
  YMC_CHECK(a == b);
  YMC_CHECK(!(a == c));
  YMC_CHECK(a.is_valid());
}

// I-3  id 从 0 开始、按首次出现顺序连续分配
YMC_TEST(intern_ids_are_sequential) {
  Interner in;
  YMC_CHECK_EQ(in.intern("x").id, 0u);
  YMC_CHECK_EQ(in.intern("y").id, 1u);
  YMC_CHECK_EQ(in.intern("x").id, 0u);   // 重复不分配新 id
  YMC_CHECK_EQ(in.intern("z").id, 2u);
  YMC_CHECK_EQ(in.size(), std::size_t{3});
}

// I-4  str() 还原原文；输入是调用方的临时缓冲区也没关系（Interner 必须自己拷贝一份）
YMC_TEST(str_roundtrip_copies_input) {
  Interner in;
  Symbol s;
  {
    std::string temp = "Modelica.Blocks.Continuous.Integrator";   // 作用域结束后销毁
    s = in.intern(temp);
  }
  YMC_CHECK_EQ(in.str(s), std::string_view("Modelica.Blocks.Continuous.Integrator"));
}

// I-5  lookup 不插入
YMC_TEST(lookup_does_not_insert) {
  Interner in;
  YMC_CHECK(!in.lookup("nothing").has_value());
  YMC_CHECK_EQ(in.size(), std::size_t{0});
  Symbol s = in.intern("time");
  YMC_CHECK(in.lookup("time").has_value());
  YMC_CHECK(in.lookup("time").value() == s);
}

// I-6  空串是合法的符号
YMC_TEST(empty_string_is_a_symbol) {
  Interner in;
  Symbol e = in.intern("");
  YMC_CHECK(e.is_valid());
  YMC_CHECK_EQ(in.str(e).size(), std::size_t{0});
}

// I-7  str() 返回的 string_view 在 Interner 生命周期内永远有效——
//      插入十万个新符号之后，最早拿到的 view 仍指向正确内容。
//      （如果你用 std::vector<std::string> 存字符串，这个测试会随机崩溃或读到垃圾：见任务卡"坑 #1"）
YMC_TEST(views_stay_valid_after_many_inserts) {
  Interner in;
  Symbol first = in.intern("first_symbol");
  std::string_view view_before = in.str(first);
  for (int i = 0; i < 100000; ++i) {
    (void)in.intern("sym_" + std::to_string(i));   // 显式丢弃 [[nodiscard]] 返回值
  }
  YMC_CHECK_EQ(view_before, std::string_view("first_symbol"));
  YMC_CHECK_EQ(in.str(first), std::string_view("first_symbol"));
  YMC_CHECK_EQ(in.size(), std::size_t{100001});
}

// I-8  Symbol 可以当 unordered_set / unordered_map 的 key（需要 std::hash<Symbol>）
YMC_TEST(symbol_is_hashable) {
  Interner in;
  std::unordered_set<Symbol> set;
  set.insert(in.intern("a"));
  set.insert(in.intern("b"));
  set.insert(in.intern("a"));
  YMC_CHECK_EQ(set.size(), std::size_t{2});
  YMC_CHECK(set.count(in.intern("a")) == 1);
}

// I-9  Interner 不可拷贝（编译期性质，用 type trait 检查）
YMC_TEST(interner_is_not_copyable) {
  YMC_CHECK(!std::is_copy_constructible_v<Interner>);
  YMC_CHECK(!std::is_copy_assignable_v<Interner>);
}

YMC_TEST_MAIN
