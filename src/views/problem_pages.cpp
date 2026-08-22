#include "views/problem_pages.h"

#include <string>

#include "http_utils.h"
#include "markdown/md.h"
#include "models/problem.h"
#include "views/render.h"
#include "views/template.h"

namespace oj::views {

namespace {

// 难度徽章 HTML（按难度着色，样式见 www/css/style.css）。
std::string DifficultyBadge(oj::models::Difficulty difficulty) {
  return "<span class=\"badge badge-" +
         std::string(oj::models::DifficultyName(difficulty)) + "\">" +
         oj::models::DifficultyLabel(difficulty) + "</span>";
}

// 代码编辑器预填的 C++ 模板（FR-2.2）。
constexpr const char* kCppTemplate = R"(#include <iostream>
using namespace std;

int main() {
    return 0;
}
)";

}  // namespace

std::string ProblemListPage(const std::vector<oj::models::Problem>& problems,
                            const std::string& username) {
  std::string rows;
  for (const auto& p : problems) {
    rows +=
        "<tr>"
        "<td class=\"col-id\">" + std::to_string(p.id) + "</td>"
        "<td><a class=\"problem-title\" href=\"/problems/" + std::to_string(p.id) +
            "\">" + oj::http::HtmlEscape(p.title) + "</a></td>"
        "<td class=\"col-diff\">" + DifficultyBadge(p.difficulty) + "</td>"
        "</tr>\n";
  }

  // 空题库与列表页共用题库标题，正文模板按是否有题二选一。
  const std::string body = problems.empty()
      ? LoadTemplate("problems_empty.html")
      : ReplaceAll(LoadTemplate("problems.html"), "{{rows}}", rows);
  return Layout("题库 - CPP OJ", body, NavLinks(username));
}

std::string ProblemDetailPage(const oj::models::Problem& problem,
                              const std::string& username) {
  const std::string markdown_html = oj::markdown::ToHtml(problem.description);

  std::string body = LoadTemplate("problem_detail.html");
  body = ReplaceAll(body, "{{problem_title}}",
                    oj::http::HtmlEscape(problem.title));
  body = ReplaceAll(body, "{{difficulty_badge}}",
                    DifficultyBadge(problem.difficulty));
  body = ReplaceAll(body, "{{description_html}}", markdown_html);
  body = ReplaceAll(body, "{{problem_id}}", std::to_string(problem.id));
  body = ReplaceAll(body, "{{cpp_template}}",
                    oj::http::HtmlEscape(kCppTemplate));

  return Layout(problem.title + " - CPP OJ", body, NavLinks(username));
}

}  // namespace oj::views
