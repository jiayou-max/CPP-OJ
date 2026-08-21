#include <cstdio>
#include <fstream>
#include <string>

#include <unistd.h>

#include <gtest/gtest.h>

#include "judge/compiler.h"

namespace {

using oj::judge::Compiler;

// 编译封装测试依赖本机 g++（SPEC A.1 build-essential）。缺失则跳过。
bool GxxAvailable() { return ::access("/usr/bin/g++", X_OK) == 0; }

// 成功代码：标准 Hello World，编译后应能产出可执行文件。
const char* kValidCode = R"(
#include <iostream>
int main() {
  std::cout << "hello\n";
  return 0;
}
)";

// 语法错误代码：g++ 应输出 CE 日志。
const char* kInvalidCode = R"(
int main() {
  int x = ;
  return 0;
}
)";

TEST(CompilerTest, CompileValidCodeSucceeds) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过编译测试";
  }

  Compiler c;
  const auto r = c.Compile(kValidCode);
  EXPECT_TRUE(r.ok) << "合法代码应编译成功，日志: " << r.error_log;
  EXPECT_FALSE(r.timeout);

  // 产物应存在且可执行（binary_path 指向临时目录内的 main）。
  EXPECT_FALSE(c.work_dir().empty());
  EXPECT_EQ(0, ::access(c.binary_path().c_str(), F_OK));
  EXPECT_EQ(0, ::access(c.binary_path().c_str(), X_OK));
}

TEST(CompilerTest, CompileInvalidCodeCapturesCeLog) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过编译测试";
  }

  Compiler c;
  const auto r = c.Compile(kInvalidCode);
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.timeout);
  EXPECT_FALSE(r.error_log.empty()) << "CE 应包含完整 g++ stderr 日志";
  // 编译错误日志应含错误定位信息（error:），用于结果页展示。
  EXPECT_NE(std::string::npos, r.error_log.find("error:"));
}

// 编译超时兜底：以 #include 系统无限设备 /dev/zero 构造永不结束的预处理，
// 验证超时后强制终止并标注 timeout（SPEC 5 / D8：编译 10s 兜底 → CE 标注编译超时）。
TEST(CompilerTest, CompileHangTriggersTimeout) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过编译测试";
  }

  Compiler c;
  c.set_compile_timeout_sec(1);  // 缩短超时以加速测试
  const std::string hang_code = "#include \"/dev/zero\"\nint main() { return 0; }\n";
  const auto r = c.Compile(hang_code);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.timeout) << "死循环编译应触发超时兜底";
  EXPECT_NE(std::string::npos, r.error_log.find("编译超时"));
}

// Cleanup 幂等：清理后目录不存在，重复调用不崩溃。
TEST(CompilerTest, CleanupRemovesWorkDir) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过编译测试";
  }

  Compiler c;
  ASSERT_TRUE(c.Compile(kValidCode).ok);
  const std::string dir = c.work_dir();
  ASSERT_FALSE(dir.empty());
  ASSERT_EQ(0, ::access(dir.c_str(), F_OK));

  c.Cleanup();
  EXPECT_NE(0, ::access(dir.c_str(), F_OK)) << "Cleanup 后临时目录应已删除";
  c.Cleanup();  // 幂等，不崩溃
}

}  // namespace
