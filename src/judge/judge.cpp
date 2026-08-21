#include "judge/judge.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "config.h"
#include "logger.h"

namespace oj::judge {

namespace {

// 计数信号量（C++17 无 std::counting_semaphore，用 mutex + cv 实现）。
// 全局同时判题数 ≤ kJudgeMaxConcurrent（SPEC 4.4 / FR-3.5）。
class Semaphore {
 public:
  explicit Semaphore(int count) : count_(count) {}

  void acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return count_ > 0; });
    --count_;
  }

  void release() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++count_;
    cv_.notify_one();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int count_;
};

// RAII：acquire 于构造，release 于析构（含异常路径）。
class SemaphoreGuard {
 public:
  explicit SemaphoreGuard(Semaphore& sem) : sem_(sem) { sem_.acquire(); }
  ~SemaphoreGuard() { sem_.release(); }

  SemaphoreGuard(const SemaphoreGuard&) = delete;
  SemaphoreGuard& operator=(const SemaphoreGuard&) = delete;

 private:
  Semaphore& sem_;
};

// 全局判题信号量（进程级，任意线程/请求共享，FR-3.5）。
Semaphore& JudgeSemaphore() {
  static Semaphore sem(static_cast<int>(kJudgeMaxConcurrent));
  return sem;
}

// 逐用例状态 → 总体状态（按首个失败用例定最终状态，SPEC 4.4 汇总规则）。
oj::models::SubmissionStatus MapCaseToSubmission(CaseResult::Status case_status) {
  switch (case_status) {
    case CaseResult::Status::kOk: return oj::models::SubmissionStatus::kAc;
    case CaseResult::Status::kWrongAnswer: return oj::models::SubmissionStatus::kWa;
    case CaseResult::Status::kTimeLimit: return oj::models::SubmissionStatus::kTle;
    case CaseResult::Status::kMemoryLimit: return oj::models::SubmissionStatus::kMle;
    case CaseResult::Status::kRuntimeError: return oj::models::SubmissionStatus::kRe;
  }
  return oj::models::SubmissionStatus::kAc;
}

}  // namespace

const char* CaseStatusLabel(CaseResult::Status status) {
  switch (status) {
    case CaseResult::Status::kOk: return "通过";
    case CaseResult::Status::kWrongAnswer: return "答案错误";
    case CaseResult::Status::kTimeLimit: return "超时";
    case CaseResult::Status::kMemoryLimit: return "内存超限";
    case CaseResult::Status::kRuntimeError: return "运行错误";
  }
  return "通过";
}

JudgeResult Judge(const std::string& code,
                  const std::vector<oj::models::TestCase>& testcases) {
  SemaphoreGuard guard(JudgeSemaphore());

  JudgeResult result;

  // 编译阶段：失败 → CE（含完整 g++ 日志 / 编译超时标注），不再运行。
  Compiler compiler;
  result.compile = compiler.Compile(code);
  if (!result.compile.ok) {
    result.status = oj::models::SubmissionStatus::kCe;
    LogDebug("judge::Judge: 编译失败，最终状态 CE");
    return result;
  }

  // 逐用例运行：记录每个用例的时间/内存，首个失败类型定总体状态。
  long long total_ms = 0;
  long long max_mem_kb = 0;
  bool failed = false;
  result.cases.reserve(testcases.size());

  for (std::size_t i = 0; i < testcases.size(); ++i) {
    CaseResult case_result = RunOneCase(
        compiler.binary_path(), compiler.work_dir(), testcases[i].input,
        testcases[i].expected);
    case_result.case_index = static_cast<int>(i + 1);
    total_ms += case_result.time_ms;
    max_mem_kb = std::max(max_mem_kb, case_result.memory_kb);
    result.cases.push_back(case_result);

    if (!failed && case_result.status != CaseResult::Status::kOk) {
      failed = true;
      result.status = MapCaseToSubmission(case_result.status);
    }
  }

  result.time_ms = total_ms;
  result.memory_kb = max_mem_kb;
  if (!failed) {
    result.status = oj::models::SubmissionStatus::kAc;
  }

  LogDebug("judge::Judge: 完成 " + std::to_string(testcases.size()) + " 个用例，状态 " +
           oj::models::SubmissionStatusName(result.status));
  return result;
}

}  // namespace oj::judge
