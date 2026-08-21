#pragma once

#include <string>
#include <vector>

#include "judge/compiler.h"
#include "judge/runner.h"
#include "models/submission.h"
#include "models/testcase.h"

namespace oj::judge {

// 判题总结果（对应 SPEC 4.4：总体状态 + 逐用例状态 + 时间/内存统计）。
struct JudgeResult {
  oj::models::SubmissionStatus status = oj::models::SubmissionStatus::kAc;
  long long time_ms = 0;    // 总耗时（各用例 wall 时间之和，FR-3.4）
  long long memory_kb = 0;  // 内存峰值（各用例峰值取最大，FR-3.4）
  std::vector<CaseResult> cases;  // 逐用例结果（按用例顺序，FR-3.4）
  CompileResult compile;    // 编译结果；CE 时 error_log 为完整 g++ 日志（FR-3.3）
};

// 判题编排入口（对应 SPEC 4.4 判题内核流程）：
//   1. 全局信号量 P()（全局同时判题数 ≤ kJudgeMaxConcurrent，FR-3.5，防死循环打满线程）
//   2. g++ 编译（失败 → CE，含完整编译日志；超时兜底 → CE）
//   3. 逐用例运行比对（runner），首个失败用例的类型决定总体状态；全部通过 → AC
//   4. getrusage 汇总时间/内存 → 返回结果（信号量在返回前 V()）
//
// 说明：信号量持有跨越编译与运行全程，保证「多路死循环提交」最多同时占用
// kJudgeMaxConcurrent 个判题槽位，其余请求排队等待（SPEC 5「并发提交」）。
JudgeResult Judge(const std::string& code,
                  const std::vector<oj::models::TestCase>& testcases);

// 逐用例状态 → 中文显示名（结果页逐用例表展示）。
const char* CaseStatusLabel(CaseResult::Status status);

}  // namespace oj::judge
