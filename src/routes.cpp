#include "routes.h"

#include <string>
#include <vector>

#include "config.h"
#include "db/db.h"
#include "http_utils.h"
#include "logger.h"
#include "models/problem.h"
#include "services/problem_service.h"
#include "views/error_pages.h"
#include "views/problem_pages.h"
#include "views/render.h"

namespace oj {

namespace {

// 静态资源挂载（/static → www/）：优先按仓库根目录运行，回退到 build 目录。
void MountStatic(httplib::Server& svr) {
  if (!svr.set_mount_point("/static", "./www")) {
    svr.set_mount_point("/static", "../www");
  }
}

// 渲染 500 错误页（数据库不可用等，A14）。
void Render500(httplib::Response& res) {
  res.status = 500;
  res.set_content(views::ErrorPage(500, "服务器内部错误", "数据库暂不可用，请稍后重试。"),
                  "text/html; charset=utf-8");
}

}  // namespace

void RegisterRoutes(httplib::Server& svr) {
  MountStatic(svr);

  // GET / → 重定向题库列表（SPEC 4.5 路由表）。
  svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
    res.set_redirect("/problems", 302);
  });

  // GET /problems → 题库列表（FR-2.1，游客可访问）。
  svr.Get("/problems", [](const httplib::Request&, httplib::Response& res) {
    std::vector<oj::models::Problem> problems;
    if (!oj::services::ListProblems(problems)) {
      Render500(res);
      return;
    }
    res.set_content(views::ProblemListPage(problems), "text/html; charset=utf-8");
  });

  // GET /problems/{id} → 题目详情 + 编辑器（FR-2.2，游客可访问）。
  svr.Get(R"(/problems/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    long long id = 0;
    if (!oj::http::ParseId(req.matches[1].str(), id)) {
      res.status = 404;
      res.set_content(views::ErrorPage(404, "页面不存在", "请求的资源不存在。"),
                      "text/html; charset=utf-8");
      return;
    }

    oj::models::Problem problem;
    const auto status = oj::services::GetProblemById(id, problem);
    if (status == oj::services::ProblemQueryStatus::kDbError) {
      Render500(res);
      return;
    }
    if (status == oj::services::ProblemQueryStatus::kNotFound) {
      res.status = 404;
      res.set_content(views::ErrorPage(404, "题目不存在", "该题目已被移除或编号无效。"),
                      "text/html; charset=utf-8");
      return;
    }

    res.set_content(views::ProblemDetailPage(problem), "text/html; charset=utf-8");
  });

  // POST /problems/{id}/submit → 判题提交。
  // 阶段 2 仅落地详情页表单；判题内核（阶段 3）+ 用户系统（阶段 4）就绪后接入。
  // 未登录提交一律重定向登录页（FR-1.5）。
  svr.Post(R"(/problems/(\d+)/submit)", [](const httplib::Request& req,
                                           httplib::Response& res) {
    const long long id = std::stoll(req.matches[1].str());
    res.set_redirect("/login?return=/problems/" + std::to_string(id), 302);
  });

  // 统一错误页：未匹配路由（404）、异常（500）等状态码 ≥ 400 时渲染。
  // 已在路由内显式 set_content 的错误页（如「题目不存在」）保持原样，不覆盖。
  svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
    if (!res.body.empty()) {
      return;
    }
    const int code = res.status;
    std::string title;
    if (code == 404) {
      title = "页面不存在";
    } else if (code == 403) {
      title = "访问被拒绝";
    } else if (code >= 500) {
      title = "服务器内部错误";
    } else {
      title = httplib::detail::status_message(code);
    }
    res.set_content(views::ErrorPage(code, title,
                                     httplib::detail::status_message(code)),
                    "text/html; charset=utf-8");
  });
}

}  // namespace oj
