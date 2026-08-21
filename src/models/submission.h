#pragma once

#include <string>

namespace oj::models {

// 判题总体状态（submissions.status ENUM，SPEC 4.3 / FR-3.3）。
enum class SubmissionStatus {
  kAc,  // 全部用例通过
  kWa,  // 答案错误
  kTle, // 超时
  kMle, // 内存超限
  kRe,  // 运行错误
  kCe,  // 编译错误
};

// 状态 → 库中 ENUM 小写名（ac/wa/tle/mle/re/ce，submissions.status）。
const char* SubmissionStatusName(SubmissionStatus status);

// 状态 → 中文显示名（结果页展示）。
const char* SubmissionStatusLabel(SubmissionStatus status);

// 小写名 → 状态；非法值返回 kAc。
SubmissionStatus SubmissionStatusFromName(const std::string& name);

// 提交记录（submissions 表一行，对应 SPEC 4.3 / FR-5.1）。
// 只存最终总体状态与资源统计，逐用例详情不落库（仅当次页面展示，SPEC 4.3 说明）。
struct Submission {
  long long id = 0;
  long long user_id = 0;
  long long problem_id = 0;
  std::string code;
  SubmissionStatus status = SubmissionStatus::kAc;
  long long time_ms = 0;   // 本次实际运行用时（各用例 wall 之和）
  long long memory_kb = 0; // 内存峰值（各用例峰值取最大）
};

}  // namespace oj::models
