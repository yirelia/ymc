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

# 前端测试额外链接 ymc_frontend（S1 词法/语法/unparse；S2 SCode/包加载）。
if(EXISTS "${CMAKE_SOURCE_DIR}/src/parse/parser.h")
  ymc_add_test(parser_test unit/parse/parser_test.cpp)
  if(TARGET ymc_frontend)
    target_link_libraries(parser_test PRIVATE ymc_frontend)
  endif()
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/scode/version.h")
  ymc_add_test(s2_util_test unit/scode/s2_util_test.cpp)
  if(TARGET ymc_frontend)
    target_link_libraries(s2_util_test PRIVATE ymc_frontend)
  endif()
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/scode/loader.h")
  ymc_add_test(loader_test unit/scode/loader_test.cpp)
  target_compile_definitions(loader_test PRIVATE YMC_TEST_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  if(TARGET ymc_frontend)
    target_link_libraries(loader_test PRIVATE ymc_frontend)
  endif()
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/src/scode/scode.h" AND EXISTS "${CMAKE_SOURCE_DIR}/tests/unit/scode/scode_test.cpp")
  ymc_add_test(scode_test unit/scode/scode_test.cpp)
  if(TARGET ymc_frontend)
    target_link_libraries(scode_test PRIVATE ymc_frontend)
  endif()
endif()
# S3：压平打印器（flat IR + 类型化表达式打印 + DAE 版式）。
if(EXISTS "${CMAKE_SOURCE_DIR}/src/flat/flat_dump.h" AND EXISTS "${CMAKE_SOURCE_DIR}/tests/unit/flat/flat_dump_test.cpp")
  ymc_add_test(flat_dump_test unit/flat/flat_dump_test.cpp)
  if(TARGET ymc_frontend)
    target_link_libraries(flat_dump_test PRIVATE ymc_frontend)
  endif()
endif()
# S3：实例化 pass（partialInst / expand / instClass）。
if(EXISTS "${CMAKE_SOURCE_DIR}/src/inst/inst.h" AND EXISTS "${CMAKE_SOURCE_DIR}/tests/unit/inst/inst_test.cpp")
  ymc_add_test(inst_test unit/inst/inst_test.cpp)
  if(TARGET ymc_frontend)
    target_link_libraries(inst_test PRIVATE ymc_frontend)
  endif()
endif()
# S3：压平端到端（instExpressions + flatten，对拍 omc X.mo）。
if(EXISTS "${CMAKE_SOURCE_DIR}/src/flat/flatten.h" AND EXISTS "${CMAKE_SOURCE_DIR}/tests/unit/flat/flatten_test.cpp")
  ymc_add_test(flatten_test unit/flat/flatten_test.cpp)
  if(TARGET ymc_frontend)
    target_link_libraries(flatten_test PRIVATE ymc_frontend)
  endif()
endif()
