#include "routes.h"

#include <string>
#include <vector>

#include "config.h"
#include "db/db.h"
#include "http_utils.h"
#include "judge/judge.h"
#include "logger.h"
#include "models/problem.h"
#include "models/testcase.h"
#include "services/problem_service.h"
#include "services/submission_service.h"
#include "views/error_pages.h"
#include "views/problem_pages.h"
#include "views/render.h"
#include "views/result_pages.h"

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

  // POST /problems/{id}/submit → 判题提交（FR-2.2 / 阶段 3 判题内核）。
  //
  // 阶段 4 用户系统未落地：当前无会话鉴权，匿名提交仍执行完整判题并渲染结果页，
  // 但**不落库** submissions（用户决策：接入判题 + 匿名不落库）。阶段 4 将在此处
  // 加入 require_login 获取真实 user_id（FR-1.2 / FR-1.5），并启用落库。
  svr.Post(R"(/problems/(\d+)/submit)", [](const httplib::Request& req,
                                           httplib::Response& res) {
    long long id = 0;
    if (!oj::http::ParseId(req.matches[1].str(), id)) {
      res.status = 404;
      res.set_content(views::ErrorPage(404, "页面不存在", "请求的资源不存在。"),
                      "text/html; charset=utf-8");
      return;
    }

    // 阶段 4 占位：无登录用户 → 不落库。届时替换为会话鉴权取 user_id（FR-1.5）。
    const long long current_user_id = 0;

    const std::string code = req.get_param_value("code");

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

    std::vector<oj::models::TestCase> testcases;
    if (!oj::services::ListTestcasesByProblem(id, testcases)) {
      Render500(res);
      return;
    }
    if (testcases.empty()) {
      res.status = 500;
      res.set_content(views::ErrorPage(500, "判题不可用", "该题目暂无测试用例，请联系管理员。"),
                      "text/html; charset=utf-8");
      return;
    }

    // 同步判题（信号量限流，FR-3.5）：编译 + 逐用例运行 + 汇总。
    const oj::judge::JudgeResult result = oj::judge::Judge(code, testcases);

    // 有登录用户时才落库（阶段 4 前恒为匿名，不落库；落库失败仅记日志，不阻塞展示）。
    if (current_user_id > 0) {
      oj::models::Submission submission;
      submission.user_id = current_user_id;
      submission.problem_id = id;
      submission.code = code;
      submission.status = result.status;
      submission.time_ms = result.time_ms;
      submission.memory_kb = result.memory_kb;
      if (!oj::services::CreateSubmission(submission)) {
        LogError("routes: 提交落库失败（不阻塞结果展示）problem_id=" + std::to_string(id));
      }
    }

    res.set_content(views::JudgeResultPage(id, problem.title, result),
                    "text/html; charset=utf-8");
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
