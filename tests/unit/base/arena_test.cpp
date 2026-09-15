// 任务卡 T-S0-02 的验收测试。你的 arena.h 必须让这个文件编译通过且全部 ok。
#include "ymc_test.h"

#include "base/arena.h"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>

using ymc::Arena;
using ymc::Id;

// 两个只在测试里用的"IR 节点"类型
struct Expr { int value = 0; };
struct Var  { std::string name; };

// A-1  默认 Id 无效
YMC_TEST(id_default_is_invalid) {
  Id<Expr> id;
  YMC_CHECK(!id.is_valid());
  YMC_CHECK_EQ(id.raw, Id<Expr>::kInvalid);
}

// A-2  add 返回 0,1,2,…；size 跟着长
YMC_TEST(add_returns_sequential_ids) {
  Arena<Expr> a;
  Id<Expr> i0 = a.add(Expr{10});
  Id<Expr> i1 = a.add(Expr{20});
  Id<Expr> i2 = a.add(Expr{30});
  YMC_CHECK_EQ(i0.raw, 0u);
  YMC_CHECK_EQ(i1.raw, 1u);
  YMC_CHECK_EQ(i2.raw, 2u);
  YMC_CHECK_EQ(a.size(), std::size_t{3});
  YMC_CHECK(i0.is_valid());
}

// A-3  operator[] 读、写都要能用；const Arena 只能读
YMC_TEST(index_read_and_write) {
  Arena<Expr> a;
  Id<Expr> id = a.add(Expr{1});
  YMC_CHECK_EQ(a[id].value, 1);
  a[id].value = 42;
  YMC_CHECK_EQ(a[id].value, 42);
  const Arena<Expr>& ca = a;           // 通过 const 引用只能调 const 版本的 operator[]
  YMC_CHECK_EQ(ca[id].value, 42);
}

// A-4  contains：范围内为真，范围外、无效 id 为假
YMC_TEST(contains) {
  Arena<Var> a;
  Id<Var> id = a.add(Var{"x"});
  YMC_CHECK(a.contains(id));
  YMC_CHECK(!a.contains(Id<Var>{}));
  YMC_CHECK(!a.contains(Id<Var>::from_index(5)));
}

// A-5  强类型：Id<Expr> 和 Id<Var> 是两个不同类型，不能互相赋值（编译期性质）
YMC_TEST(ids_of_different_types_are_different_types) {
  YMC_CHECK(!(std::is_same_v<Id<Expr>, Id<Var>>));
  YMC_CHECK(!(std::is_convertible_v<Id<Expr>, Id<Var>>));
  YMC_CHECK(!(std::is_convertible_v<Id<Expr>, std::uint32_t>));   // 也不能悄悄变成整数
}

// A-6  from_index / index 互逆；用它遍历
YMC_TEST(iterate_with_from_index) {
  Arena<Expr> a;
  for (int v = 0; v < 5; ++v) (void)a.add(Expr{v});
  int sum = 0;
  for (std::uint32_t i = 0; i < a.size(); ++i) {
    Id<Expr> id = Id<Expr>::from_index(i);
    YMC_CHECK_EQ(id.index(), i);
    sum += a[id].value;
  }
  YMC_CHECK_EQ(sum, 0 + 1 + 2 + 3 + 4);
}

// A-7  reserve 不改变 size，但之后添加不会失败
YMC_TEST(reserve_does_not_change_size) {
  Arena<Expr> a;
  a.reserve(1000);
  YMC_CHECK_EQ(a.size(), std::size_t{0});
  for (int v = 0; v < 1000; ++v) (void)a.add(Expr{v});
  YMC_CHECK_EQ(a.size(), std::size_t{1000});
}

// A-8  只能移动、不能拷贝的元素类型也能放进 Arena（要求 add 按值接收并 std::move 进容器）
YMC_TEST(move_only_element_type) {
  Arena<std::unique_ptr<int>> a;
  Id<std::unique_ptr<int>> id = a.add(std::make_unique<int>(7));
  YMC_CHECK_EQ(*a[id], 7);
}

// A-9  Id 可哈希，可以当 unordered_set 的 key
YMC_TEST(id_is_hashable) {
  std::unordered_set<Id<Expr>> set;
  set.insert(Id<Expr>::from_index(1));
  set.insert(Id<Expr>::from_index(2));
  set.insert(Id<Expr>::from_index(1));
  YMC_CHECK_EQ(set.size(), std::size_t{2});
}

// A-10 百万级插入不出问题（性能只是顺带，主要看 uint32 索引和 vector 扩容都正常）
YMC_TEST(million_inserts) {
  Arena<Expr> a;
  for (int v = 0; v < 1'000'000; ++v) (void)a.add(Expr{v});
  YMC_CHECK_EQ(a.size(), std::size_t{1'000'000});
  YMC_CHECK_EQ(a[Id<Expr>::from_index(999'999)].value, 999'999);
}

YMC_TEST_MAIN
