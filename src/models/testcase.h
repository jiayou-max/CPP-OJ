#pragma once

#include <string>

namespace oj::models {

// 测试用例（testcases 表一行，对应 SPEC 4.3 数据模型 / FR-3.2）。
struct TestCase {
  long long id = 0;
  long long problem_id = 0;
  int order = 0;        // 用例序号（testcases.order，MySQL 保留字，SQL 中需反引号）
  std::string input;    // 用例输入（重定向到 stdin）
  std::string expected; // 期望输出（与 stdout 逐组比对）
};

}  // namespace oj::models
