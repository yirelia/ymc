// 冒烟测试：只证明编译器、CMake、测试框架三者能一起工作。
#include "ymc_test.h"

#include <cstdint>
#include <string_view>
#include <vector>

YMC_TEST(toolchain_is_cxx20) {
  // __cplusplus 是编译器定义的宏。C++17 = 201703L；正式的 C++20 = 202002L，
  // 但 GCC 10.x 的 -std=c++20 仍报 201709L（它当时还叫 c++2a）。所以只检查"大于 C++17"。
  YMC_CHECK(__cplusplus > 201703L);
}

YMC_TEST(framework_check_eq_works) {
  std::vector<int> v{1, 2, 3};
  YMC_CHECK_EQ(v.size(), std::size_t{3});
  YMC_CHECK_EQ(std::string_view("abc").size(), std::size_t{3});
  YMC_CHECK_EQ(std::uint32_t{7} + 1u, 8u);
}

YMC_TEST_MAIN
