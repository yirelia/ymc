// 断言（方案 §3.4 第 12 条）：Debug 和 Release 都生效——编译器的正确性优先于速度。
//
// 用法：
//   YMC_ASSERT(id.raw < items_.size(), "Arena index out of range");
//   YMC_UNREACHABLE("unhandled ExprKind");
//
// 教学点：这是本项目允许的少数几个宏之一。宏在预处理阶段做纯文本替换，
// #cond 会把条件表达式变成字符串，__FILE__/__LINE__ 由编译器填入位置。
// do { ... } while (0) 是让宏在 if/else 里当一条语句用的惯用法。
#pragma once
#include <cstdio>
#include <cstdlib>

#define YMC_ASSERT(cond, msg)                                                          \
  do {                                                                                 \
    if (!(cond)) {                                                                     \
      std::fprintf(stderr, "YMC_ASSERT failed: %s\n  %s\n  at %s:%d\n", #cond, (msg),  \
                   __FILE__, __LINE__);                                                \
      std::abort();   /* abort() 而不是 exit()：调试器会停在这里，能看到完整调用栈 */   \
    }                                                                                  \
  } while (0)

#define YMC_UNREACHABLE(msg)                                                           \
  do {                                                                                 \
    std::fprintf(stderr, "YMC_UNREACHABLE: %s\n  at %s:%d\n", (msg), __FILE__, __LINE__); \
    std::abort();                                                                      \
  } while (0)
