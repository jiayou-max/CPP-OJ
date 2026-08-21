#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

#include <unistd.h>

#include <gtest/gtest.h>

#include "judge/compiler.h"
#include "judge/runner.h"

namespace {

using oj::judge::CaseResult;
using oj::judge::Compiler;
using oj::judge::RunOneCase;

// 运行封装测试依赖本机 g++ + /proc（Linux）编译出可执行文件。g++ 缺失则跳过。
bool GxxAvailable() { return ::access("/usr/bin/g++", X_OK) == 0; }

// 编译一段源码并返回持有临时目录/可执行文件的 Compiler（失败返回 nullptr）。
// Compiler 禁拷贝/移动，故经 unique_ptr 持有。
std::unique_ptr<Compiler> CompileCode(const char* source) {
  auto c = std::make_unique<Compiler>();
  if (!c->Compile(source).ok) {
    return nullptr;
  }
  return c;
}

// AC：输出与 expected 一致。
TEST(RunnerTest, CorrectOutputIsOk) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过运行测试";
  }
  auto c = CompileCode(R"(
#include <iostream>
int main() {
  long long a, b;
  while (std::cin >> a >> b) std::cout << (a + b) << "\n";
  return 0;
}
)");
  ASSERT_NE(nullptr, c) << "测试源码编译失败";
  auto r = RunOneCase(c->binary_path(), c->work_dir(),
                      "1 2\n10 20\n", "3\n30\n", 1000, 64 * 1024);
  EXPECT_EQ(CaseResult::Status::kOk, r.status);
  EXPECT_GE(r.memory_kb, 0);
  EXPECT_GE(r.time_ms, 1);
}

// WA：输出内容与 expected 不一致（第 2 组答案错）。
TEST(RunnerTest, WrongOutputIsWrongAnswer) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过运行测试";
  }
  auto c = CompileCode(R"(
#include <iostream>
int main() {
  long long a, b;
  while (std::cin >> a >> b) std::cout << (a + b) << "\n";
  return 0;
}
)");
  ASSERT_NE(nullptr, c) << "测试源码编译失败";
  auto r = RunOneCase(c->binary_path(), c->work_dir(),
                      "1 2\n10 20\n", "3\n99\n", 1000, 64 * 1024);
  EXPECT_EQ(CaseResult::Status::kWrongAnswer, r.status);
}

// WA：输出行尾空白差异应被容忍（expected 无换行，输出有换行）。
TEST(RunnerTest, TrailingNewlineTolerated) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过运行测试";
  }
  auto c = CompileCode(R"(
#include <iostream>
int main() {
  std::cout << "hello\n";
  return 0;
}
)");
  ASSERT_NE(nullptr, c) << "测试源码编译失败";
  auto r = RunOneCase(c->binary_path(), c->work_dir(), "", "hello", 1000, 64 * 1024);
  EXPECT_EQ(CaseResult::Status::kOk, r.status);
}

// TLE：死循环代码应在 time_limit 内被终止并判定超时（SPEC 5：2s 强制 kill）。
TEST(RunnerTest, InfiniteLoopIsTimeout) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过运行测试";
  }
  auto c = CompileCode("int main() { for (;;) {} }");
  ASSERT_NE(nullptr, c) << "测试源码编译失败";
  const auto begin = std::chrono::steady_clock::now();
  auto r = RunOneCase(c->binary_path(), c->work_dir(), "", "", 500, 64 * 1024);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - begin).count();
  EXPECT_EQ(CaseResult::Status::kTimeLimit, r.status);
  // 有界返回：总耗时不应显著超过限时（< 限时 + 1s 余量）。
  EXPECT_LT(elapsed, 1500);
  EXPECT_GE(r.time_ms, 500);
}

// RE：除零 → SIGFPE 崩溃 → 运行错误。
TEST(RunnerTest, DivideByZeroIsRuntimeError) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过运行测试";
  }
  auto c = CompileCode(R"(
int main() {
  int a = 1, b = 0;
  volatile int x = a / b;
  return x;
}
)");
  ASSERT_NE(nullptr, c) << "测试源码编译失败";
  auto r = RunOneCase(c->binary_path(), c->work_dir(), "", "", 1000, 64 * 1024);
  EXPECT_EQ(CaseResult::Status::kRuntimeError, r.status);
}

// RE：非零退出码 → 运行错误。
TEST(RunnerTest, NonZeroExitIsRuntimeError) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过运行测试";
  }
  auto c = CompileCode("int main() { return 3; }");
  ASSERT_NE(nullptr, c) << "测试源码编译失败";
  auto r = RunOneCase(c->binary_path(), c->work_dir(), "", "", 1000, 64 * 1024);
  EXPECT_EQ(CaseResult::Status::kRuntimeError, r.status);
}

// MLE：逐步分配内存越过 memory_limit 应被判内存超限（父进程 /proc 轮询终止）。
TEST(RunnerTest, ExcessiveAllocationIsMemoryLimit) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过运行测试";
  }
  auto c = CompileCode(R"(
#include <vector>
int main() {
  std::vector<char> v;
  for (;;) {
    for (int i = 0; i < 1024; ++i) v.push_back('x');
  }
}
)");
  ASSERT_NE(nullptr, c) << "测试源码编译失败";
  auto r = RunOneCase(c->binary_path(), c->work_dir(), "", "", 3000, 32 * 1024);
  EXPECT_EQ(CaseResult::Status::kMemoryLimit, r.status);
}

// MLE：单次超大分配触发 RLIMIT_AS 硬上限，有界返回（bad_alloc 崩溃亦为 RE，
// 这里仅验证不被 TLE 误判且限时内有界）。
TEST(RunnerTest, MemoryLimitIsBounded) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过运行测试";
  }
  auto c = CompileCode(R"(
#include <vector>
int main() {
  std::vector<char> v(2000ull * 1024 * 1024, 'x');
  return static_cast<int>(v.size());
}
)");
  ASSERT_NE(nullptr, c) << "测试源码编译失败";
  auto r = RunOneCase(c->binary_path(), c->work_dir(), "", "", 1000, 16 * 1024);
  EXPECT_NE(CaseResult::Status::kTimeLimit, r.status);
  EXPECT_GE(r.time_ms, 1);
}

}  // namespace
