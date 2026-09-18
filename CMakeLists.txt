cmake_minimum_required(VERSION 3.25)
project(ymc LANGUAGES CXX)

# ---- 语言标准（方案 §3.1）----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)            # 用 -std=c++20 而不是 -std=gnu++20
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)    # 生成 compile_commands.json，给编辑器/clangd 用

# ---- 统一的警告开关（方案 §3.1 / §3.4 第 1 条）----
# INTERFACE 库不产生任何目标文件，只是一个"选项包"，谁链接它谁就继承这些编译选项。
add_library(ymc_warnings INTERFACE)
if(MSVC)
  target_compile_options(ymc_warnings INTERFACE
    /W4                         # 最高常用警告级别
    /permissive-                # 严格标准模式（关掉 MSVC 的历史宽松行为）
    /Zc:__cplusplus             # 让 __cplusplus 报真实值（MSVC 默认报 199711L）
    /we4062                     # switch(enum) 漏 case 且无 default → 报错（IR 穷尽检查靠它）
    /we4715                     # 有返回值的函数不是所有路径都 return → 报错
    /wd4458 /wd4459             # 成员/局部变量遮蔽的两个噪音警告先关掉（-Wshadow 的 MSVC 版太吵）
    /wd4127                     # "条件表达式是常量"——测试宏 YMC_CHECK(常量) 会触发，纯噪音
    /utf-8                      # 源码按 UTF-8 解释（注释里有中文）
  )
  # std::getenv 等标准函数在 MSVC 下默认报 C4996"不安全"，这是 MSVC 私货，不是标准问题；关掉它保持零警告
  target_compile_definitions(ymc_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(ymc_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    -Wshadow                    # 内层变量遮蔽外层变量——初学者最常见的 bug 来源之一
    -Werror=switch              # switch(enum) 漏 case → 直接报错（IR 穷尽检查靠它）
    -Werror=return-type         # 有返回值的函数走到末尾没 return → 报错
    -fno-omit-frame-pointer     # 让 gdb 的栈回溯可靠
  )
endif()

# ---- AddressSanitizer 开关（预设 msvc-asan 打开）----
# 不用覆盖 CMAKE_CXX_FLAGS 的方式：那样会把 CMake 给 MSVC 的默认 /EHsc 冲掉（C4530）。
option(YMC_ASAN "Build with AddressSanitizer" OFF)
if(YMC_ASAN AND MSVC)
  add_compile_options(/fsanitize=address)
  add_link_options(/INCREMENTAL:NO)       # ASan 元数据与增量链接不兼容（LNK4300）
elseif(YMC_ASAN)
  add_compile_options(-fsanitize=address)  # TDM-GCC 10 在 Windows 上不支持 ASan；留给以后的 clang 预设
  add_link_options(-fsanitize=address)
endif()

# ---- 基础设施库 src/base ----
# 现在 src/base 里还没有 .cpp（symbol.cpp 由你按任务卡 T-S0-01 创建）。
# 有 .cpp 时编成静态库；没有时只是一个"头文件目录"接口。
file(GLOB YMC_BASE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/base/*.cpp")
if(YMC_BASE_SOURCES)
  add_library(ymc_base STATIC ${YMC_BASE_SOURCES})
  target_include_directories(ymc_base PUBLIC "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(ymc_base PUBLIC ymc_warnings)
else()
  add_library(ymc_base INTERFACE)
  target_include_directories(ymc_base INTERFACE "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(ymc_base INTERFACE ymc_warnings)
endif()

# ---- 前端库 src/parse + src/absyn + src/scode + src/inst + src/flat
#      （S1：词法/语法/Absyn/unparse；S2：SCode/包加载；S3：实例化/压平）----
# 有源码时编成 ymc_frontend；没有时不建，由 if(TARGET) 门控链接。
file(GLOB YMC_FRONTEND_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/parse/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/absyn/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/scode/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/inst/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/flat/*.cpp")
if(YMC_FRONTEND_SOURCES)
  add_library(ymc_frontend STATIC ${YMC_FRONTEND_SOURCES})
  target_link_libraries(ymc_frontend PUBLIC ymc_base ymc_warnings)
endif()

# ---- 编译器可执行文件 ----
add_executable(ymc src/driver/main.cpp)
target_link_libraries(ymc PRIVATE ymc_base ymc_warnings)
if(TARGET ymc_frontend)
  target_link_libraries(ymc PRIVATE ymc_frontend)
endif()

# ---- 测试 ----
enable_testing()
add_subdirectory(tests)
