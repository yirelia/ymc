// 编译期求值用的超越函数（sin / cos / … / exp / log / pow）：结果要与 omc 1.25.0 逐位一致。
//
// 为什么不直接用 <cmath>：omc（Windows 安装版）的编译期求值调的是 UCRT 的数学函数（libOpenModelicaCompiler.dll 从
// api-ms-win-crt-math-l1-1-0.dll 导入 cos / sin / exp / pow），而各家 libm 的超越函数末位常不同——TDM-GCC 链进的 libmingwex
// 自带一套（cos(4.188786666666666)：UCRT -0.5000030640984336、libmingwex -0.5000030640984338，真值几乎正好在两者中间；
// 抽样 1.1 万个点 78 个不同），连 msvcrt.dll 与 ucrtbase.dll 之间也有差（1.2 万个点 52 个）。MSVC 构建本就链 UCRT；MinGW 构建
// 在运行时从 ucrtbase.dll（Windows 10 起系统自带）取同名函数，取不到才退回 <cmath>（那时结果可能差一个 ulp）。
// sqrt / floor / ceil / fmod 是 IEEE 精确运算（结果唯一），各家一致，不经这里。
#pragma once

namespace ymc::crt {

double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double exp(double x);
double log(double x);
double log10(double x);
double pow(double x, double y);

}  // namespace ymc::crt
