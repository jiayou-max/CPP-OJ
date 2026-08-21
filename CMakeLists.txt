cmake_minimum_required(VERSION 3.16)
project(cpp_oj VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# httplib v0.10.3 对 application/x-www-form-urlencoded 请求体限制为
# CPPHTTPLIB_REQUEST_URI_MAX_LENGTH（默认 8192 字节），判题提交的代码可能超过该值。
# 提升到 8MB，避免较长代码被 413 拒收（同时放宽请求行长度限制，无实际影响）。
add_compile_definitions(CPPHTTPLIB_REQUEST_URI_MAX_LENGTH=8388608)

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
# mysqlclient (MySQL 连接池，SPEC A.1 libmysqlclient-dev)
#   pkg-config 提供头文件路径与 -lmysqlclient，其私有依赖 openssl 一并带入。
# ------------------------------------------------------------------
pkg_check_modules(MYSQLCLIENT REQUIRED IMPORTED_TARGET mysqlclient)

# ------------------------------------------------------------------
# OpenSSL (SHA256 / CSPRNG 随机盐，SPEC A.1 libssl-dev)
#   mysqlclient 虽私有链接 openssl，但本项目 src/auth/password.* 直接调用
#   EVP_Digest / RAND_bytes，须显式 find + link OpenSSL::Crypto。
# ------------------------------------------------------------------
find_package(OpenSSL REQUIRED)

# ------------------------------------------------------------------
# 源码清单 (对应 SPEC 4.6 目录结构)
#   随阶段推进在此追加模块源文件（logger/db/auth/models/services/judge）。
#   单元测试复用 lib 源，不含 main.cpp（避免与 gtest_main 冲突）。
# ------------------------------------------------------------------
set(OJ_LIB_SOURCES
  src/logger.cpp
  src/routes.cpp
  src/http_utils.cpp
  src/views/render.cpp
  src/views/template.cpp
  src/views/problem_pages.cpp
  src/views/error_pages.cpp
  src/views/result_pages.cpp
  src/markdown/md.cpp
  src/db/db.cpp
  src/db/db_query.cpp
  src/models/problem.cpp
  src/models/submission.cpp
  src/services/problem_service.cpp
  src/services/submission_service.cpp
  src/auth/password.cpp
  src/auth/session.cpp
  src/judge/compiler.cpp
  src/judge/runner.cpp
  src/judge/judge.cpp
)

set(OJ_SERVER_SOURCES
  src/main.cpp
  ${OJ_LIB_SOURCES}
)

# 链接依赖（md4c 解析/渲染 + mysqlclient + openssl；cpp-httplib 为 header-only，
# 无链接库，除非回退到 apt 版才需 ${HTTPLIB_LINK_LIBS}）。
set(OJ_COMMON_LIBS
  PkgConfig::MD4C
  ${MD4C_HTML_LIBRARY}
  PkgConfig::MYSQLCLIENT
  OpenSSL::Crypto
)

# ---------- 目标: oj_server（HTTP 服务） ----------
add_executable(oj_server ${OJ_SERVER_SOURCES})
target_include_directories(oj_server PRIVATE src ${HTTPLIB_INCLUDE_DIR})
target_link_libraries(oj_server PRIVATE ${OJ_COMMON_LIBS} ${HTTPLIB_LINK_LIBS})

# ---------- 目标: oj_init_admin（阶段 1，内置 admin 初始化工具，见 db/init_admin.cpp） ----------
# 仅依赖 logger/db/password 三个最小源，不拖入 HTTP/渲染层。
set(OJ_INIT_ADMIN_SOURCES
  db/init_admin.cpp
  src/logger.cpp
  src/db/db.cpp
  src/auth/password.cpp
)
add_executable(oj_init_admin ${OJ_INIT_ADMIN_SOURCES})
target_include_directories(oj_init_admin PRIVATE src)
target_link_libraries(oj_init_admin PRIVATE
  PkgConfig::MYSQLCLIENT
  OpenSSL::Crypto)

# ---------- 单元测试 (gtest, src/test/) ----------
option(OJ_BUILD_TESTS "构建单元测试" ON)
if(OJ_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)

  add_executable(oj_tests
    src/test/render_test.cpp
    src/test/template_test.cpp
    src/test/routes_test.cpp
    src/test/markdown_test.cpp
    src/test/logger_test.cpp
    src/test/db_test.cpp
    src/test/password_test.cpp
    src/test/session_test.cpp
    src/test/problem_service_test.cpp
    src/test/compiler_test.cpp
    src/test/runner_test.cpp
    src/test/judge_test.cpp
    ${OJ_LIB_SOURCES}
  )

  target_include_directories(oj_tests PRIVATE src ${HTTPLIB_INCLUDE_DIR})
  target_link_libraries(oj_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    ${OJ_COMMON_LIBS}
    ${HTTPLIB_LINK_LIBS})

  include(GoogleTest)
  gtest_discover_tests(oj_tests)
endif()
