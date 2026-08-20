cmake_minimum_required(VERSION 3.16)
project(cpp_oj VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# cpp-httplib (header-only):
#   优先使用 vendored 的 third_party/httplib.h（纯 header-only，符合 SPEC 单文件集成）；
#   回退到系统 libcpp-httplib-dev（/usr/include/httplib.h）。
find_path(HTTPLIB_INCLUDE_DIR httplib.h
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        /usr/include
  NO_DEFAULT_PATH)

# md4c (Markdown → HTML):
#   Ubuntu 将解析器与 HTML 渲染器拆成两个库：
#     - 解析器 (md_parse)    → libmd4c        (包 libmd4c-dev)
#     - HTML 渲染器 (md_html) → libmd4c-html   (包 libmd4c-html0)
#   解析器头文件/链接参数通过 pkg-config 定位；渲染器无 pkg-config，
#   用 find_library 单独查找。任一缺失即报错并给出安装提示。
find_package(PkgConfig REQUIRED)
pkg_check_modules(MD4C REQUIRED IMPORTED_TARGET md4c)
# md4c-html 需要 dev 符号链接（由包 libmd4c-html0-dev 提供，内含 libmd4c-html.so）；
# 仅装 libmd4c-html0 运行时库无法被 -l 找到，需一并安装 dev 包。
find_library(MD4C_HTML_LIBRARY NAMES md4c-html)
if(NOT MD4C_HTML_LIBRARY)
  message(FATAL_ERROR "找不到 libmd4c-html（md_html 渲染器）。请安装: sudo apt install -y libmd4c-html0-dev")
endif()

add_executable(oj_server
  src/main.cpp
  src/routes.cpp
  src/views/render.cpp
  src/markdown/md.cpp
)

target_include_directories(oj_server PRIVATE src ${HTTPLIB_INCLUDE_DIR})
target_link_libraries(oj_server PRIVATE PkgConfig::MD4C ${MD4C_HTML_LIBRARY})

# ---------- 单元测试 (gtest, src/test/) ----------
option(OJ_BUILD_TESTS "构建单元测试" ON)
if(OJ_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)

  add_executable(oj_tests
    src/test/render_test.cpp
    src/test/routes_test.cpp
    src/test/markdown_test.cpp
    src/routes.cpp
    src/views/render.cpp
    src/markdown/md.cpp
  )

  target_include_directories(oj_tests PRIVATE src ${HTTPLIB_INCLUDE_DIR})
  target_link_libraries(oj_tests PRIVATE GTest::gtest GTest::gtest_main PkgConfig::MD4C ${MD4C_HTML_LIBRARY})

  include(GoogleTest)
  gtest_discover_tests(oj_tests)
endif()
