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

// 判题状态图标（内联 SVG，样式见 result.html 的 .status-icon）。
// 深色科技风：每个状态一个语义化图标，作为颜色之外的指示（可访问性）。
std::string StatusIcon(oj::models::SubmissionStatus status) {
  const char* stroke = "stroke=\"currentColor\" stroke-width=\"2\" "
                       "stroke-linecap=\"round\" stroke-linejoin=\"round\"";
  switch (status) {
    case oj::models::SubmissionStatus::kAc:
      return "<svg viewBox=\"0 0 24 24\" fill=\"none\" " + std::string(stroke) +
             "><path d=\"M22 11.08V12a10 10 0 1 1-5.93-9.14\"/><polyline "
             "points=\"22 4 12 14.01 9 11.01\"/></svg>";
    case oj::models::SubmissionStatus::kWa:
      return "<svg viewBox=\"0 0 24 24\" fill=\"none\" " + std::string(stroke) +
             "><circle cx=\"12\" cy=\"12\" r=\"10\"/><line x1=\"15\" y1=\"9\" "
             "x2=\"9\" y2=\"15\"/><line x1=\"9\" y1=\"9\" x2=\"15\" y2=\"15\"/></svg>";
    case oj::models::SubmissionStatus::kTle:
      return "<svg viewBox=\"0 0 24 24\" fill=\"none\" " + std::string(stroke) +
             "><circle cx=\"12\" cy=\"12\" r=\"10\"/><polyline points=\"12 6 "
             "12 12 16 14\"/></svg>";
    case oj::models::SubmissionStatus::kMle:
      return "<svg viewBox=\"0 0 24 24\" fill=\"none\" " + std::string(stroke) +
             "><rect x=\"2\" y=\"2\" width=\"20\" height=\"8\" rx=\"2\" "
             "ry=\"2\"/><rect x=\"2\" y=\"14\" width=\"20\" height=\"8\" rx=\"2\" "
             "ry=\"2\"/><line x1=\"6\" y1=\"6\" x2=\"6.01\" y2=\"6\"/><line x1=\"6\" "
             "y1=\"18\" x2=\"6.01\" y2=\"18\"/></svg>";
    case oj::models::SubmissionStatus::kRe:
      return "<svg viewBox=\"0 0 24 24\" fill=\"none\" " + std::string(stroke) +
             "><circle cx=\"12\" cy=\"12\" r=\"10\"/><line x1=\"12\" y1=\"16\" "
             "x2=\"12\" y2=\"12\"/><line x1=\"12\" y1=\"8\" x2=\"12.01\" y2=\"8\"/></svg>";
    case oj::models::SubmissionStatus::kCe:
      return "<svg viewBox=\"0 0 24 24\" fill=\"none\" " + std::string(stroke) +
             "><path d=\"M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 "
             "1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z\"/><line x1=\"12\" y1=\"9\" "
             "x2=\"12\" y2=\"13\"/><line x1=\"12\" y1=\"17\" x2=\"12.01\" "
             "y2=\"17\"/></svg>";
  }
  return "";
}

}  // namespace

std::string JudgeResultPage(long long problem_id, const std::string& problem_title,
                            const oj::judge::JudgeResult& result,
                            const std::string& username,
                            const std::string& role) {
  std::string body = LoadTemplate("result.html");
  body = ReplaceAll(body, "{{status_name}}",
                    oj::models::SubmissionStatusName(result.status));
  body = ReplaceAll(body, "{{status_label}}",
                    oj::models::SubmissionStatusLabel(result.status));
  body = ReplaceAll(body, "{{status_icon}}", StatusIcon(result.status));
  body = ReplaceAll(body, "{{time_icon}}",
      "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" "
      "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
      "<circle cx=\"12\" cy=\"12\" r=\"10\"/><polyline points=\"12 6 12 12 16 14\"/></svg>");
  body = ReplaceAll(body, "{{mem_icon}}",
      "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" "
      "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
      "<rect x=\"2\" y=\"2\" width=\"20\" height=\"8\" rx=\"2\" ry=\"2\"/>"
      "<rect x=\"2\" y=\"14\" width=\"20\" height=\"8\" rx=\"2\" ry=\"2\"/>"
      "<line x1=\"6\" y1=\"6\" x2=\"6.01\" y2=\"6\"/><line x1=\"6\" y1=\"18\" "
      "x2=\"6.01\" y2=\"18\"/></svg>");
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
  return Layout(title, body, NavLinks(username, role));
}

}  // namespace oj::views
