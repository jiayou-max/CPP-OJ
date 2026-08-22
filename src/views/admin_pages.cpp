#include "views/admin_pages.h"

#include <string>

#include "http_utils.h"
#include "models/problem.h"
#include "models/testcase.h"
#include "views/render.h"
#include "views/template.h"

namespace oj::views {

namespace {

// 难度徽章 HTML（与 problem_pages.cpp 一致，样式见 www/css/style.css）。
std::string DifficultyBadge(oj::models::Difficulty difficulty) {
  return "<span class=\"badge badge-" +
         std::string(oj::models::DifficultyName(difficulty)) + "\">" +
         oj::models::DifficultyLabel(difficulty) + "</span>";
}

// 表单错误提示块（error 为空串 → 不渲染）。用户输入一律 HTML 转义防 XSS（NFR-3）。
std::string ErrorHtml(const std::string& error) {
  if (error.empty()) {
    return "";
  }
  return "<div class=\"form-error\">" + oj::http::HtmlEscape(error) + "</div>";
}

// 难度下拉框的 selected 占位（仅匹配项填 "selected"，其余为空）。
std::string SelectedFor(oj::models::Difficulty difficulty,
                        oj::models::Difficulty target) {
  return difficulty == target ? "selected" : "";
}

// 单组测试用例表单 HTML（input/expected 文本区，内容 HTML 转义）。
std::string TestCaseGroupHtml(const std::string& input,
                              const std::string& expected) {
  return "<div class=\"tc-group\">"
         "<div class=\"tc-fields\">"
         "<div class=\"tc-field\"><label>输入</label>"
         "<textarea name=\"input[]\" rows=\"4\" spellcheck=\"false\">" +
         oj::http::HtmlEscape(input) + "</textarea></div>"
         "<div class=\"tc-field\"><label>期望输出</label>"
         "<textarea name=\"expected[]\" rows=\"4\" spellcheck=\"false\">" +
         oj::http::HtmlEscape(expected) + "</textarea></div>"
         "</div>"
         "<button type=\"button\" class=\"btn btn-sm btn-danger tc-remove\" hidden>移除</button>"
         "</div>";
}

// 新增 / 编辑共用的题目表单页（模板 www/admin_problem_form.html）。
// title/description/tc_groups 为用户可控内容，须 HTML 转义（NFR-3）。
std::string ProblemFormPage(const std::string& page_title,
                            const std::string& form_action,
                            const std::string& title_value,
                            const std::string& description_value,
                            oj::models::Difficulty difficulty,
                            const std::string& tc_groups,
                            const std::string& submit_label,
                            const std::string& error,
                            const std::string& username,
                            const std::string& role) {
  std::string body = LoadTemplate("admin_problem_form.html");
  body = ReplaceAll(body, "{{page_title}}", page_title);
  body = ReplaceAll(body, "{{form_action}}", form_action);
  body = ReplaceAll(body, "{{title_value}}",
                    oj::http::HtmlEscape(title_value));
  body = ReplaceAll(body, "{{description_value}}",
                    oj::http::HtmlEscape(description_value));
  body = ReplaceAll(body, "{{difficulty_easy}}",
                    SelectedFor(difficulty, oj::models::Difficulty::kEasy));
  body = ReplaceAll(body, "{{difficulty_medium}}",
                    SelectedFor(difficulty, oj::models::Difficulty::kMedium));
  body = ReplaceAll(body, "{{difficulty_hard}}",
                    SelectedFor(difficulty, oj::models::Difficulty::kHard));
  body = ReplaceAll(body, "{{tc_groups}}", tc_groups);
  body = ReplaceAll(body, "{{submit_label}}", submit_label);
  body = ReplaceAll(body, "{{error_html}}", ErrorHtml(error));

  return Layout(page_title + " - CPP OJ", body, NavLinks(username, role));
}

}  // namespace

std::string AdminProblemListPage(const std::vector<oj::models::Problem>& problems,
                                 const std::string& username,
                                 const std::string& role) {
  std::string list;
  if (problems.empty()) {
    list = "<p class=\"empty-hint\">题库暂无题目。点击右上角「新增题目」创建第一道题。</p>";
  } else {
    std::string rows;
    for (const auto& p : problems) {
      rows +=
          "<tr>"
          "<td class=\"col-id\">" + std::to_string(p.id) + "</td>"
          "<td><a class=\"problem-title\" href=\"/problems/" + std::to_string(p.id) +
              "\">" + oj::http::HtmlEscape(p.title) + "</a></td>"
          "<td class=\"col-diff\">" + DifficultyBadge(p.difficulty) + "</td>"
          "<td class=\"col-actions\">"
              "<span class=\"row-actions\">"
              "<a class=\"btn btn-sm\" href=\"/admin/problems/" +
              std::to_string(p.id) + "/edit\">编辑</a>"
              "<form class=\"admin-delete\" action=\"/admin/problems/" +
              std::to_string(p.id) + "/delete\" method=\"post\">"
              "<button type=\"submit\" class=\"btn btn-danger btn-sm\">删除</button>"
              "</form>"
              "</span>"
          "</td>"
          "</tr>\n";
    }
    list = "<table class=\"problem-table\">"
           "<thead><tr><th>#</th><th>标题</th><th>难度</th><th class=\"col-actions\">操作</th></tr></thead>"
           "<tbody>" + rows + "</tbody></table>";
  }

  std::string body = LoadTemplate("admin_problems.html");
  body = ReplaceAll(body, "{{error_html}}", "");
  body = ReplaceAll(body, "{{list_html}}", list);

  return Layout("题目管理 - CPP OJ", body, NavLinks(username, role));
}

std::string AdminProblemNewPage(const std::string& error,
                                const std::string& username,
                                const std::string& role) {
  // 新增：默认难度「简单」、空字段、两组空用例。
  std::string groups = TestCaseGroupHtml("", "") + TestCaseGroupHtml("", "");
  return ProblemFormPage("新增题目", "/admin/problems", "", "",
                         oj::models::Difficulty::kEasy, groups, "创建题目",
                         error, username, role);
}

std::string AdminProblemEditPage(const oj::models::Problem& problem,
                                 const std::vector<oj::models::TestCase>& testcases,
                                 const std::string& error,
                                 const std::string& username,
                                 const std::string& role) {
  std::string groups;
  for (const auto& tc : testcases) {
    groups += TestCaseGroupHtml(tc.input, tc.expected);
  }
  if (groups.empty()) {
    // 理论不出现（校验要求至少一组用例），兜底给一组空输入。
    groups = TestCaseGroupHtml("", "");
  }

  const std::string form_action =
      "/admin/problems/" + std::to_string(problem.id) + "/edit";
  return ProblemFormPage("编辑题目", form_action, problem.title,
                         problem.description, problem.difficulty, groups,
                         "保存修改", error, username, role);
}

}  // namespace oj::views
