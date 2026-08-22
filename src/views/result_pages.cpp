#include "views/result_pages.h"

#include <string>

#include "http_utils.h"
#include "judge/judge.h"
#include "models/submission.h"
#include "views/render.h"
#include "views/template.h"

namespace oj::views {

namespace {

// 逐用例状态 → 总体状态枚举（仅用于结果页取状态 CSS class/名，与判题汇总无关）。
oj::models::SubmissionStatus CaseToSubmission(
    oj::judge::CaseResult::Status status) {
  switch (status) {
    case oj::judge::CaseResult::Status::kOk: return oj::models::SubmissionStatus::kAc;
    case oj::judge::CaseResult::Status::kWrongAnswer: return oj::models::SubmissionStatus::kWa;
    case oj::judge::CaseResult::Status::kTimeLimit: return oj::models::SubmissionStatus::kTle;
    case oj::judge::CaseResult::Status::kMemoryLimit: return oj::models::SubmissionStatus::kMle;
    case oj::judge::CaseResult::Status::kRuntimeError: return oj::models::SubmissionStatus::kRe;
  }
  return oj::models::SubmissionStatus::kAc;
}

// 逐用例表格行（用例序号 + 状态 + 时间 + 内存）。
std::string CaseRow(const oj::judge::CaseResult& c) {
  const auto sub = CaseToSubmission(c.status);
  return "<tr>"
         "<td>#" + std::to_string(c.case_index) + "</td>"
         "<td><span class=\"case-status case-" +
             oj::models::SubmissionStatusName(sub) + "\">" +
             oj::judge::CaseStatusLabel(c.status) + "</span></td>"
         "<td>" + std::to_string(c.time_ms) + " ms</td>"
         "<td>" + std::to_string(c.memory_kb) + " KB</td>"
         "</tr>\n";
}

}  // namespace

std::string JudgeResultPage(long long problem_id, const std::string& problem_title,
                            const oj::judge::JudgeResult& result,
                            const std::string& username) {
  std::string body = LoadTemplate("result.html");
  body = ReplaceAll(body, "{{status_name}}",
                    oj::models::SubmissionStatusName(result.status));
  body = ReplaceAll(body, "{{status_label}}",
                    oj::models::SubmissionStatusLabel(result.status));
  body = ReplaceAll(body, "{{time_ms}}", std::to_string(result.time_ms));
  body = ReplaceAll(body, "{{memory_kb}}", std::to_string(result.memory_kb));
  body = ReplaceAll(body, "{{problem_id}}", std::to_string(problem_id));

  std::string rows;
  for (const auto& c : result.cases) {
    rows += CaseRow(c);
  }
  body = ReplaceAll(body, "{{case_rows}}", rows);

  // CE：展示完整 g++ 编译日志（FR-3.3，含「编译超时」标注）。
  std::string compile_log;
  if (result.status == oj::models::SubmissionStatus::kCe) {
    std::string title = "编译错误日志";
    std::string log = result.compile.error_log;
    if (result.compile.timeout) {
      title = "编译超时";
      log = result.compile.error_log;  // Compiler 已在日志中标注超时说明
    }
    compile_log = "<h2>" + title + "</h2>"
                  "<pre class=\"compile-log\">" +
                  oj::http::HtmlEscape(log) + "</pre>";
  }
  body = ReplaceAll(body, "{{compile_log_html}}", compile_log);

  const std::string title = "判题结果：" + problem_title + " - CPP OJ";
  return Layout(title, body, NavLinks(username));
}

}  // namespace oj::views
