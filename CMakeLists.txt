cmake_minimum_required(VERSION 3.16)
project(cpp_oj VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# ------------------------------------------------------------------
# cpp-httplib (header-only)
#   SPEC 4.6 / D4 硬性要求单文件集成：优先使用 vendored 的
#   third_party/httplib.h（v0.10.3，纯 header-only）。
#   回退到系统 libcpp-httplib-dev（/usr/include/httplib.h）：apt 版将
#   实现拆到 libcpp-httplib.so，非纯 header-only，必须额外链接 -lcpp-httplib。
# ------------------------------------------------------------------
find_path(HTTPLIB_INCLUDE_DIR httplib.h
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  NO_DEFAULT_PATH)

set(HTTPLIB_LINK_LIBS)
if(NOT HTTPLIB_INCLUDE_DIR)
  find_path(HTTPLIB_INCLUDE_DIR httplib.h PATHS /usr/include NO_DEFAULT_PATH)
  if(NOT HTTPLIB_INCLUDE_DIR)
    message(FATAL_ERROR
      "未找到 httplib.h。请保留 vendored third_party/httplib.h，"
      "或安装: sudo apt install -y libcpp-httplib-dev")
  endif()
  set(HTTPLIB_LINK_LIBS cpp-httplib)
endif()

# ------------------------------------------------------------------
# md4c (Markdown → HTML，NFR-7 / D4)
#   Ubuntu 将 md4c 拆成两个库：
#     - 解析器 (md_parse)     → libmd4c       （pkg-config: md4c）
#     - HTML 渲染器 (md_html) → libmd4c-html  （无 pkg-config，find_library）
#   渲染器需 libmd4c-html0-dev 提供 -l 符号链接，否则链接失败。
# ------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(MD4C REQUIRED IMPORTED_TARGET md4c)
find_library(MD4C_HTML_LIBRARY NAMES md4c-html)
if(NOT MD4C_HTML_LIBRARY)
  message(FATAL_ERROR
    "找不到 libmd4c-html（md_html 渲染器）。请安装: sudo apt install -y libmd4c-html0-dev")
endif()

# ------------------------------------------------------------------
# 源码清单 (对应 SPEC 4.6 目录结构)
#   随阶段推进在此追加模块源文件（logger/db/auth/models/services/judge）。
#   单元测试复用 lib 源，不含 main.cpp（避免与 gtest_main 冲突）。
# ------------------------------------------------------------------
set(OJ_LIB_SOURCES
  src/logger.cpp
  src/routes.cpp
  src/views/render.cpp
  src/markdown/md.cpp
)

set(OJ_SERVER_SOURCES
  src/main.cpp
  ${OJ_LIB_SOURCES}
)

# ---------- 目标: oj_server（HTTP 服务） ----------
add_executable(oj_server ${OJ_SERVER_SOURCES})
target_include_directories(oj_server PRIVATE src ${HTTPLIB_INCLUDE_DIR})
target_link_libraries(oj_server PRIVATE
  PkgConfig::MD4C
  ${MD4C_HTML_LIBRARY}
  ${HTTPLIB_LINK_LIBS})

# ---------- 单元测试 (gtest, src/test/) ----------
option(OJ_BUILD_TESTS "构建单元测试" ON)
if(OJ_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)

  add_executable(oj_tests
    src/test/render_test.cpp
    src/test/routes_test.cpp
    src/test/markdown_test.cpp
    src/test/logger_test.cpp
    ${OJ_LIB_SOURCES}
  )

  target_include_directories(oj_tests PRIVATE src ${HTTPLIB_INCLUDE_DIR})
  target_link_libraries(oj_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    PkgConfig::MD4C
    ${MD4C_HTML_LIBRARY}
    ${HTTPLIB_LINK_LIBS})

  include(GoogleTest)
  gtest_discover_tests(oj_tests)
endif()
