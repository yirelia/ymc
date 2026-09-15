// 极简单元测试框架（方案 §3.2：不引第三方，60 行自己写）。
//
// 用法：
//   #include "ymc_test.h"
//   YMC_TEST(name_of_test) { YMC_CHECK(1 + 1 == 2); YMC_CHECK_EQ(x, 3); }
//   YMC_TEST_MAIN            // 每个测试文件末尾放一次
//
// 原理（也是一个 C++ 教学点）：
//   YMC_TEST 展开成"一个静态函数 + 一个静态对象"，静态对象的构造函数在 main() 之前运行，
//   把函数指针登记到一个全局列表里；main() 再逐个调用。这叫"静态注册"。
#pragma once
#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace ymc_test {

struct Case { const char* name; void (*fn)(); };

inline std::vector<Case>& cases() {
  static std::vector<Case> all;   // 函数内 static：第一次调用时构造，解决"全局对象初始化顺序"问题
  return all;
}

struct Registrar {
  Registrar(const char* name, void (*fn)()) { cases().push_back({name, fn}); }
};

struct Failure : std::exception {
  std::string text;
  Failure(const char* file, int line, const std::string& what) {
    std::ostringstream os;
    os << file << ":" << line << ": " << what;
    text = os.str();
  }
  const char* what() const noexcept override { return text.c_str(); }
};

inline int run_all() {
  int failed = 0;
  for (const Case& c : cases()) {
    try {
      c.fn();
      std::printf("  ok   %s\n", c.name);
    } catch (const Failure& f) {
      std::printf("  FAIL %s\n       %s\n", c.name, f.what());
      ++failed;
    } catch (const std::exception& e) {
      std::printf("  FAIL %s\n       unexpected exception: %s\n", c.name, e.what());
      ++failed;
    }
  }
  std::printf("%zu tests, %d failed\n", cases().size(), failed);
  return failed == 0 ? 0 : 1;   // 非零退出码 = ctest 判失败
}

template <class A, class B>
std::string describe_eq(const char* ea, const char* eb, const A& a, const B& b) {
  std::ostringstream os;
  os << "expected " << ea << " == " << eb << "\n         left:  " << a << "\n         right: " << b;
  return os.str();
}

}  // namespace ymc_test

#define YMC_TEST(name)                                                       \
  static void name();                                                        \
  static ::ymc_test::Registrar name##_registrar(#name, &name);               \
  static void name()

#define YMC_CHECK(cond)                                                      \
  do {                                                                       \
    if (!(cond)) throw ::ymc_test::Failure(__FILE__, __LINE__, "check failed: " #cond); \
  } while (0)

#define YMC_CHECK_EQ(a, b)                                                   \
  do {                                                                       \
    const auto& ymc_a_ = (a);                                                \
    const auto& ymc_b_ = (b);                                                \
    if (!(ymc_a_ == ymc_b_))                                                 \
      throw ::ymc_test::Failure(__FILE__, __LINE__, ::ymc_test::describe_eq(#a, #b, ymc_a_, ymc_b_)); \
  } while (0)

#define YMC_TEST_MAIN int main() { return ::ymc_test::run_all(); }
