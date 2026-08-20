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

add_executable(oj_server
  src/main.cpp
  src/routes.cpp
  src/views/render.cpp
)

target_include_directories(oj_server PRIVATE src ${HTTPLIB_INCLUDE_DIR})

# ---------- 单元测试 (gtest, src/test/) ----------
option(OJ_BUILD_TESTS "构建单元测试" ON)
if(OJ_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)

  add_executable(oj_tests
    src/test/render_test.cpp
    src/test/routes_test.cpp
    src/routes.cpp
    src/views/render.cpp
  )

  target_include_directories(oj_tests PRIVATE src ${HTTPLIB_INCLUDE_DIR})
  target_link_libraries(oj_tests PRIVATE GTest::gtest GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(oj_tests)
endif()
