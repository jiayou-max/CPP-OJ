#include <cstdio>
#include <string>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include "judge/judge.h"
#include "models/submission.h"
#include "models/testcase.h"

namespace {

using oj::judge::CaseResult;
using oj::judge::Judge;
using oj::models::SubmissionStatus;
using oj::models::TestCase;

// 判题编排测试依赖本机 g++。缺失则跳过。
bool GxxAvailable() { return ::access("/usr/bin/g++", X_OK) == 0; }

TestCase MakeCase(int order, const std::string& input, const std::string& expected) {
  TestCase tc;
  tc.order = order;
  tc.input = input;
  tc.expected = expected;
  return tc;
}

// AC 标准解：A+B，两组用例全部通过 → 总体 AC，逐用例全 kOk。
TEST(JudgeTest, AllCasesPassIsAc) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过判题测试";
  }
  const std::string code = R"(
#include <iostream>
int main() {
  long long a, b;
  while (std::cin >> a >> b) std::cout << (a + b) << "\n";
  return 0;
}
)";
  std::vector<TestCase> cases = {
      MakeCase(1, "1 2\n", "3\n"),
      MakeCase(2, "10 20\n", "30\n"),
      MakeCase(3, "-5 7\n", "2\n"),
  };
  const auto r = Judge(code, cases);
  EXPECT_EQ(SubmissionStatus::kAc, r.status);
  ASSERT_EQ(3u, r.cases.size());
  for (const auto& c : r.cases) {
    EXPECT_EQ(CaseResult::Status::kOk, c.status);
    EXPECT_GE(c.time_ms, 1);
  }
  // 统计：总耗时 ≥ 0，内存峰值 ≥ 0。
  EXPECT_GE(r.time_ms, 1);
  EXPECT_GE(r.memory_kb, 0);
}

// WA：中间某组用例答案错 → 总体 WA，且能看到是第几个用例失败（FR-3.4 / A7）。
TEST(JudgeTest, WrongCaseMakesWa) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过判题测试";
  }
  const std::string code = R"(
#include <iostream>
int main() {
  long long a, b;
  while (std::cin >> a >> b) std::cout << (a + b) << "\n";
  return 0;
}
)";
  std::vector<TestCase> cases = {
      MakeCase(1, "1 2\n", "3\n"),
      MakeCase(2, "10 20\n", "99\n"),  // 第 2 组答案错
      MakeCase(3, "-5 7\n", "2\n"),
  };
  const auto r = Judge(code, cases);
  EXPECT_EQ(SubmissionStatus::kWa, r.status);
  ASSERT_EQ(3u, r.cases.size());
  EXPECT_EQ(CaseResult::Status::kOk, r.cases[0].status);
  EXPECT_EQ(CaseResult::Status::kWrongAnswer, r.cases[1].status);
  EXPECT_EQ(2, r.cases[1].case_index);
  EXPECT_EQ(CaseResult::Status::kOk, r.cases[2].status);
}

// CE：语法错误代码 → 总体 CE，且含完整 g++ 编译日志（FR-3.3 / A9）。
TEST(JudgeTest, SyntaxErrorIsCeWithLog) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过判题测试";
  }
  const std::string code = "int main() { int x = ; return 0; }";
  std::vector<TestCase> cases = {MakeCase(1, "", "")};
  const auto r = Judge(code, cases);
  EXPECT_EQ(SubmissionStatus::kCe, r.status);
  EXPECT_FALSE(r.compile.ok);
  EXPECT_TRUE(r.cases.empty()) << "CE 不应进入逐用例运行";
  EXPECT_NE(std::string::npos, r.compile.error_log.find("error:"));
}

// 空代码走正常编译流程 → CE（SPEC 5「空白/语法错误代码」）。
TEST(JudgeTest, BlankCodeIsCe) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过判题测试";
  }
  const auto r = Judge("", {MakeCase(1, "", "")});
  EXPECT_EQ(SubmissionStatus::kCe, r.status);
}

// 死循环代码 → 总体 TLE（有界返回，不卡死调用线程）。
TEST(JudgeTest, InfiniteLoopIsTle) {
  if (!GxxAvailable()) {
    GTEST_SKIP() << "g++ 不可用，跳过判题测试";
  }
  const auto r = Judge("int main() { for (;;) {} }", {MakeCase(1, "", "")});
  EXPECT_EQ(SubmissionStatus::kTle, r.status);
  ASSERT_EQ(1u, r.cases.size());
  EXPECT_EQ(CaseResult::Status::kTimeLimit, r.cases[0].status);
}

// 状态映射：SubmissionStatusName/Label 与库 ENUM 名一致。
TEST(JudgeTest, StatusNameMapping) {
  EXPECT_STREQ("ac", oj::models::SubmissionStatusName(SubmissionStatus::kAc));
  EXPECT_STREQ("wa", oj::models::SubmissionStatusName(SubmissionStatus::kWa));
  EXPECT_STREQ("tle", oj::models::SubmissionStatusName(SubmissionStatus::kTle));
  EXPECT_STREQ("mle", oj::models::SubmissionStatusName(SubmissionStatus::kMle));
  EXPECT_STREQ("re", oj::models::SubmissionStatusName(SubmissionStatus::kRe));
  EXPECT_STREQ("ce", oj::models::SubmissionStatusName(SubmissionStatus::kCe));
  EXPECT_EQ(SubmissionStatus::kWa, oj::models::SubmissionStatusFromName("wa"));
}

}  // namespace
