# MSVC + /fsanitize=address 生成的 exe 依赖 clang_rt.asan_dynamic-x86_64.dll，
# 它躺在 cl.exe 同目录、不在 PATH 里；不处理的话 exe 直接启动失败（exit 0xC0000135）。
# 处理：构建后把 DLL 拷到 exe 旁边，并给 ctest 的运行环境加上那个目录。
set(YMC_ASAN_DLL "")
if(MSVC AND YMC_ASAN)
  get_filename_component(YMC_CL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
  if(EXISTS "${YMC_CL_DIR}/clang_rt.asan_dynamic-x86_64.dll")
    set(YMC_ASAN_DLL "${YMC_CL_DIR}/clang_rt.asan_dynamic-x86_64.dll")
  endif()
endif()

# 每个测试 = 一个可执行文件 + 一条 ctest 记录。
function(ymc_add_test name src)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE ymc_base ymc_warnings)
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")   # 让 #include "ymc_test.h" 能找到
  add_test(NAME ${name} COMMAND ${name})
  if(YMC_ASAN_DLL)
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${YMC_ASAN_DLL}" "$<TARGET_FILE_DIR:${name}>")
    set_tests_properties(${name} PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${YMC_CL_DIR}")
  endif()
endfunction()

# 永远存在的冒烟测试：证明工具链和测试框架本身能跑。
ymc_add_test(smoke_test unit/smoke_test.cpp)

# 下面两个测试只在你把对应头文件写出来之后才会出现（任务卡 T-S0-01 / T-S0-02）。
if(EXISTS "${CMAKE_SOURCE_DIR}/src/base/symbol.h")
  ymc_add_test(symbol_test unit/base/symbol_test.cpp)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/base/arena.h")
  ymc_add_test(arena_test unit/base/arena_test.cpp)
endif()

# 前端测试额外链接 ymc_frontend（S1 词法/语法/unparse）。
if(EXISTS "${CMAKE_SOURCE_DIR}/src/parse/parser.h")
  ymc_add_test(parser_test unit/parse/parser_test.cpp)
  if(TARGET ymc_frontend)
    target_link_libraries(parser_test PRIVATE ymc_frontend)
  endif()
endif()
