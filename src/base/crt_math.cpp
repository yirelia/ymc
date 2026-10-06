// 见 crt_math.h：MinGW 构建从 ucrtbase.dll 取 UCRT 的超越函数（与 omc 1.25.0 / MSVC 构建同一份实现），其余平台直接 <cmath>。
#include "base/crt_math.h"

#include <cmath>

#if defined(__MINGW32__)
#include <cstring>
#include <windows.h>
#endif

namespace ymc::crt {

#if defined(__MINGW32__)
namespace {
using F1 = double (*)(double);
using F2 = double (*)(double, double);

// ucrtbase.dll 里名为 name 的函数；取不到 → nullptr（调用处退回 <cmath>）。句柄不释放：进程内一直要用。
template <class F>
F ucrt_fn(const char* name) {
  static const HMODULE h = LoadLibraryA("ucrtbase.dll");
  if (!h) return nullptr;
  const FARPROC p = GetProcAddress(h, name);
  F f = nullptr;
  // FARPROC → 具体函数指针：memcpy 而非 reinterpret_cast（后者 gcc 报 -Wcast-function-type）。
  if (p) std::memcpy(&f, &p, sizeof f);
  return f;
}
}  // namespace

// 每个函数的指针在首次调用时取一次（函数内 static：C++11 起初始化线程安全）。
double sin(double x) { static const F1 f = ucrt_fn<F1>("sin"); return f ? f(x) : std::sin(x); }
double cos(double x) { static const F1 f = ucrt_fn<F1>("cos"); return f ? f(x) : std::cos(x); }
double tan(double x) { static const F1 f = ucrt_fn<F1>("tan"); return f ? f(x) : std::tan(x); }
double asin(double x) { static const F1 f = ucrt_fn<F1>("asin"); return f ? f(x) : std::asin(x); }
double acos(double x) { static const F1 f = ucrt_fn<F1>("acos"); return f ? f(x) : std::acos(x); }
double atan(double x) { static const F1 f = ucrt_fn<F1>("atan"); return f ? f(x) : std::atan(x); }
double atan2(double y, double x) { static const F2 f = ucrt_fn<F2>("atan2"); return f ? f(y, x) : std::atan2(y, x); }
double sinh(double x) { static const F1 f = ucrt_fn<F1>("sinh"); return f ? f(x) : std::sinh(x); }
double cosh(double x) { static const F1 f = ucrt_fn<F1>("cosh"); return f ? f(x) : std::cosh(x); }
double tanh(double x) { static const F1 f = ucrt_fn<F1>("tanh"); return f ? f(x) : std::tanh(x); }
double exp(double x) { static const F1 f = ucrt_fn<F1>("exp"); return f ? f(x) : std::exp(x); }
double log(double x) { static const F1 f = ucrt_fn<F1>("log"); return f ? f(x) : std::log(x); }
double log10(double x) { static const F1 f = ucrt_fn<F1>("log10"); return f ? f(x) : std::log10(x); }
double pow(double x, double y) { static const F2 f = ucrt_fn<F2>("pow"); return f ? f(x, y) : std::pow(x, y); }
#else
double sin(double x) { return std::sin(x); }
double cos(double x) { return std::cos(x); }
double tan(double x) { return std::tan(x); }
double asin(double x) { return std::asin(x); }
double acos(double x) { return std::acos(x); }
double atan(double x) { return std::atan(x); }
double atan2(double y, double x) { return std::atan2(y, x); }
double sinh(double x) { return std::sinh(x); }
double cosh(double x) { return std::cosh(x); }
double tanh(double x) { return std::tanh(x); }
double exp(double x) { return std::exp(x); }
double log(double x) { return std::log(x); }
double log10(double x) { return std::log10(x); }
double pow(double x, double y) { return std::pow(x, y); }
#endif

}  // namespace ymc::crt
