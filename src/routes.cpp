#include "routes.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "auth/middleware.h"
#include "auth/session.h"
#include "config.h"
#include "db/db.h"
#include "http_utils.h"
#include "judge/judge.h"
#include "logger.h"
#include "models/problem.h"
#include "models/testcase.h"
#include "services/problem_service.h"
#include "services/submission_service.h"
#include "services/user_service.h"
#include "views/admin_pages.h"
#include "views/auth_pages.h"
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

// 登录成功：创建会话 + 下发 Set-Cookie + 重定向题库列表（FR-1.2）。
// 会话/随机数不可用按 500 处理（main 启动自检已提前告警）。
void StartSessionAndRedirect(long long user_id, const std::string& username,
                             const std::string& role, httplib::Response& res) {
  oj::auth::SessionManager* mgr = oj::auth::Sessions();
  if (mgr == nullptr) {
    Render500(res);
    return;
  }
  const std::string token = mgr->CreateSession(user_id, username, role);
  if (token.empty()) {
    Render500(res);
    return;
  }
  res.set_header("Set-Cookie", mgr->BuildSetCookie(token));
  res.set_redirect("/problems", 302);
}

// 当前登录用户名（未登录为空串），用于导航栏展示登录态（用户名 + 登出）。
std::string CurrentUsername(const httplib::Request& req) {
  oj::auth::Session session;
  return oj::auth::GetLoginSession(req, session) ? session.username : "";
}

// 当前登录用户角色（未登录为空串），用于导航栏展示管理入口。
std::string CurrentRole(const httplib::Request& req) {
  oj::auth::Session session;
  return oj::auth::GetLoginSession(req, session) ? session.role : "";
}

// 已登录用户访问登录/注册页 → 直接回题库，避免重复登录表单。
bool RedirectIfLoggedIn(const httplib::Request& req, httplib::Response& res) {
  oj::auth::Session session;
  if (oj::auth::GetLoginSession(req, session)) {
    res.set_redirect("/problems", 302);
    return true;
  }
  return false;
}

// 解析表单里的多组测试用例：input[] / expected[] 同名字段按下标成对读取。
// 数量以较少的那个为准（前端 JS 保证成对提交，这里做兜底防御）。
std::vector<oj::models::TestCase> ParseTestcasesFromRequest(
    const httplib::Request& req) {
  std::vector<oj::models::TestCase> testcases;
  const std::size_t input_count = req.get_param_value_count("input[]");
  const std::size_t expected_count = req.get_param_value_count("expected[]");
  const std::size_t count = std::min(input_count, expected_count);
  testcases.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    oj::models::TestCase tc;
    tc.input = req.get_param_value("input[]", i);
    tc.expected = req.get_param_value("expected[]", i);
    testcases.push_back(std::move(tc));
  }
  return testcases;
}

// 管理后台新增/编辑共用的表单错误文案（FR-4.2 校验）。
constexpr const char* kAdminFormError =
    "请填写标题、Markdown 描述，并至少提供一组「输入/期望输出」均非空的测试用例。";

}  // namespace

void RegisterRoutes(httplib::Server& svr) {
  MountStatic(svr);

  // GET / → 落地页首页（阶段 5「大屏落地页」；原重定向 /problems 移除）。
  svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
    const std::string username = CurrentUsername(req);
    const std::string role = CurrentRole(req);
    // 统计失败（数据库不可用）由 LandingPage 渲染为 "--"，不阻塞页面。
    res.set_content(views::LandingPage(username, role,
                                       oj::services::CountProblems(),
                                       oj::services::CountUsers(),
                                       oj::services::CountSubmissions()),
                    "text/html; charset=utf-8");
  });

  // GET /problems → 题库列表（FR-2.1，游客可访问）。
  svr.Get("/problems", [](const httplib::Request& req, httplib::Response& res) {
    std::vector<oj::models::Problem> problems;
    if (!oj::services::ListProblems(problems)) {
      Render500(res);
      return;
    }
    res.set_content(views::ProblemListPage(problems, CurrentUsername(req),
                                           CurrentRole(req)),
                    "text/html; charset=utf-8");
  });

  // GET /problems/{id} → 题目详情 + 编辑器（FR-2.2，游客可访问）。
  svr.Get(R"(/problems/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    const std::string username = CurrentUsername(req);
    const std::string role = CurrentRole(req);
    long long id = 0;
    if (!oj::http::ParseId(req.matches[1].str(), id)) {
      res.status = 404;
      res.set_content(views::ErrorPage(404, "页面不存在", "请求的资源不存在。", username, role),
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
      res.set_content(views::ErrorPage(404, "题目不存在", "该题目已被移除或编号无效。", username, role),
                      "text/html; charset=utf-8");
      return;
    }

    res.set_content(views::ProblemDetailPage(problem, username, role),
                    "text/html; charset=utf-8");
  });

  // GET /login → 登录表单页（FR-1.2 / SPEC 4.5 路由表）。已登录直接回题库。
  svr.Get("/login", [](const httplib::Request& req, httplib::Response& res) {
    if (RedirectIfLoggedIn(req, res)) {
      return;
    }
    res.set_content(views::LoginPage(""), "text/html; charset=utf-8");
  });

  // GET /register → 注册表单页（FR-1.1）。
  svr.Get("/register", [](const httplib::Request& req, httplib::Response& res) {
    if (RedirectIfLoggedIn(req, res)) {
      return;
    }
    res.set_content(views::RegisterPage(""), "text/html; charset=utf-8");
  });

  // POST /register → 处理注册（FR-1.1）：校验 → 加盐哈希落库 → 自动登录。
  // FR-1.3：admin 由 oj_init_admin 创建，注册接口白名单 + 唯一键共同保证不可同名。
  svr.Post("/register", [](const httplib::Request& req, httplib::Response& res) {
    const std::string username = req.get_param_value("username");
    const std::string password = req.get_param_value("password");
    const std::string password2 = req.get_param_value("password2");

    // 表单级校验：两次密码须一致（密码格式校验由 RegisterUser 完成）。
    if (password != password2) {
      res.set_content(views::RegisterPage("两次输入的密码不一致。",
                                          CurrentUsername(req)),
                      "text/html; charset=utf-8");
      return;
    }

    oj::models::User user;
    const auto status = oj::services::RegisterUser(username, password, user);
    switch (status) {
      case oj::services::UserStatus::kOk:
        StartSessionAndRedirect(user.id, user.username, user.role, res);
        return;
      case oj::services::UserStatus::kInvalidInput:
        res.set_content(views::RegisterPage(
                            "用户名仅允许字母/数字/下划线（1~64 位），密码长度 6~64。",
                            CurrentUsername(req)),
                        "text/html; charset=utf-8");
        return;
      case oj::services::UserStatus::kDuplicateName:
        res.set_content(views::RegisterPage("用户名已存在，请换一个。", CurrentUsername(req)),
                        "text/html; charset=utf-8");
        return;
      default:
        Render500(res);
        return;
    }
  });

  // POST /login → 处理登录：校验 → 建会话 + Set-Cookie → 重定向题库。
  svr.Post("/login", [](const httplib::Request& req, httplib::Response& res) {
    const std::string username = req.get_param_value("username");
    const std::string password = req.get_param_value("password");

    oj::models::User user;
    const auto status = oj::services::LoginUser(username, password, user);
    switch (status) {
      case oj::services::UserStatus::kOk:
        StartSessionAndRedirect(user.id, user.username, user.role, res);
        return;
      case oj::services::UserStatus::kInvalidInput:
        res.set_content(views::LoginPage("用户名或密码格式不正确。", CurrentUsername(req)),
                        "text/html; charset=utf-8");
        return;
      case oj::services::UserStatus::kBadCredentials:
        res.set_content(views::LoginPage("用户名或密码错误。", CurrentUsername(req)),
                        "text/html; charset=utf-8");
        return;
      default:
        Render500(res);
        return;
    }
  });

  // POST /logout → 登出（FR-1.1）：销毁会话 + 清 Cookie + 回题库。
  svr.Post("/logout", [](const httplib::Request& req, httplib::Response& res) {
    oj::auth::Session session;
    if (!oj::auth::RequireLogin(req, res, session)) {
      return;
    }

    oj::auth::SessionManager* mgr = oj::auth::Sessions();
    const std::string token =
        mgr->ParseCookie(req.get_header_value("Cookie"), oj::kSessionCookieName);
    if (!token.empty()) {
      mgr->DestroySession(token);
    }
    res.set_header("Set-Cookie", mgr->BuildSetCookie("", /*expire=*/true));
    res.set_redirect("/problems", 302);
  });

  // POST /problems/{id}/submit → 判题提交（FR-2.2 / 阶段 3 判题内核）。
  // 阶段 4：未登录提交 → 重定向登录页（FR-1.5）；登录用户提交落库（FR-5.1）。
  svr.Post(R"(/problems/(\d+)/submit)", [](const httplib::Request& req,
                                           httplib::Response& res) {
    long long id = 0;
    if (!oj::http::ParseId(req.matches[1].str(), id)) {
      res.status = 404;
      res.set_content(views::ErrorPage(404, "页面不存在", "请求的资源不存在。",
                                       CurrentUsername(req), CurrentRole(req)),
                      "text/html; charset=utf-8");
      return;
    }

    // 登录校验：未登录 → 302 /login（FR-1.5）。
    oj::auth::Session session;
    if (!oj::auth::RequireLogin(req, res, session)) {
      return;
    }
    const long long current_user_id = session.user_id;

    const std::string code = req.get_param_value("code");

    oj::models::Problem problem;
    const auto status = oj::services::GetProblemById(id, problem);
    if (status == oj::services::ProblemQueryStatus::kDbError) {
      Render500(res);
      return;
    }
    if (status == oj::services::ProblemQueryStatus::kNotFound) {
      res.status = 404;
      res.set_content(views::ErrorPage(404, "题目不存在", "该题目已被移除或编号无效。",
                                       session.username, session.role),
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
      res.set_content(views::ErrorPage(500, "判题不可用", "该题目暂无测试用例，请联系管理员。",
                                       session.username, session.role),
                      "text/html; charset=utf-8");
      return;
    }

    // 同步判题（信号量限流，FR-3.5）：编译 + 逐用例运行 + 汇总。
    const oj::judge::JudgeResult result = oj::judge::Judge(code, testcases);

    // 登录用户判题结果落库（FR-5.1；落库失败仅记日志，不阻塞结果展示）。
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

    res.set_content(views::JudgeResultPage(id, problem.title, result,
                                           session.username, session.role),
                    "text/html; charset=utf-8");
  });

  // GET /admin/problems → 管理后台题目列表（FR-4.1，仅 admin；A5 权限校验）。
  svr.Get("/admin/problems", [](const httplib::Request& req,
                                httplib::Response& res) {
    oj::auth::Session session;
    if (!oj::auth::RequireAdmin(req, res, session)) {
      return;
    }

    std::vector<oj::models::Problem> problems;
    if (!oj::services::ListProblems(problems)) {
      Render500(res);
      return;
    }
    res.set_content(views::AdminProblemListPage(problems, session.username,
                                                session.role),
                    "text/html; charset=utf-8");
  });

  // GET /admin/problems/new → 新增题目表单（FR-4.2）。
  svr.Get("/admin/problems/new", [](const httplib::Request& req,
                                    httplib::Response& res) {
    oj::auth::Session session;
    if (!oj::auth::RequireAdmin(req, res, session)) {
      return;
    }
    res.set_content(views::AdminProblemNewPage("", session.username, session.role),
                    "text/html; charset=utf-8");
  });

  // POST /admin/problems → 创建题目（含测试用例，事务；FR-4.2）。
  svr.Post("/admin/problems", [](const httplib::Request& req,
                                 httplib::Response& res) {
    oj::auth::Session session;
    if (!oj::auth::RequireAdmin(req, res, session)) {
      return;
    }

    const std::string title = req.get_param_value("title");
    const std::string description = req.get_param_value("description");
    const std::string difficulty = req.get_param_value("difficulty");
    const auto testcases = ParseTestcasesFromRequest(req);

    long long problem_id = 0;
    const auto status = oj::services::CreateProblem(
        title, description, oj::models::DifficultyFromName(difficulty),
        testcases, problem_id);
    switch (status) {
      case oj::services::CreateProblemStatus::kOk:
        res.set_redirect("/admin/problems", 302);
        return;
      case oj::services::CreateProblemStatus::kInvalidInput:
        res.set_content(views::AdminProblemNewPage(kAdminFormError,
                                                   session.username,
                                                   session.role),
                        "text/html; charset=utf-8");
        return;
      default:
        Render500(res);
        return;
    }
  });

  // GET /admin/problems/{id}/edit → 编辑题目表单（预填现有字段与用例）。
  svr.Get(R"(/admin/problems/(\d+)/edit)", [](const httplib::Request& req,
                                              httplib::Response& res) {
    oj::auth::Session session;
    if (!oj::auth::RequireAdmin(req, res, session)) {
      return;
    }

    long long id = 0;
    if (!oj::http::ParseId(req.matches[1].str(), id)) {
      res.status = 404;
      res.set_content(views::ErrorPage(404, "页面不存在", "请求的资源不存在。",
                                       session.username, session.role),
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
      res.set_content(views::ErrorPage(404, "题目不存在", "该题目已被移除或编号无效。",
                                       session.username, session.role),
                      "text/html; charset=utf-8");
      return;
    }

    std::vector<oj::models::TestCase> testcases;
    if (!oj::services::ListTestcasesByProblem(id, testcases)) {
      Render500(res);
      return;
    }

    res.set_content(views::AdminProblemEditPage(problem, testcases, "",
                                                session.username, session.role),
                    "text/html; charset=utf-8");
  });

  // POST /admin/problems/{id}/edit → 保存题目修改（标题/描述/难度/用例，事务）。
  svr.Post(R"(/admin/problems/(\d+)/edit)", [](const httplib::Request& req,
                                               httplib::Response& res) {
    oj::auth::Session session;
    if (!oj::auth::RequireAdmin(req, res, session)) {
      return;
    }

    long long id = 0;
    if (!oj::http::ParseId(req.matches[1].str(), id)) {
      res.status = 404;
      res.set_content(views::ErrorPage(404, "页面不存在", "请求的资源不存在。",
                                       session.username, session.role),
                      "text/html; charset=utf-8");
      return;
    }

    const std::string title = req.get_param_value("title");
    const std::string description = req.get_param_value("description");
    const std::string difficulty = req.get_param_value("difficulty");
    const auto testcases = ParseTestcasesFromRequest(req);

    const auto status = oj::services::UpdateProblem(
        id, title, description, oj::models::DifficultyFromName(difficulty),
        testcases);
    switch (status) {
      case oj::services::UpdateProblemStatus::kOk:
        res.set_redirect("/admin/problems", 302);
        return;
      case oj::services::UpdateProblemStatus::kInvalidInput:
        // 回填用户提交的表单数据（不丢失已填写内容）+ 错误提示。
        {
          oj::models::Problem problem;
          problem.id = id;
          problem.title = title;
          problem.description = description;
          problem.difficulty = oj::models::DifficultyFromName(difficulty);
          res.set_content(views::AdminProblemEditPage(problem, testcases,
                                                      kAdminFormError,
                                                      session.username,
                                                      session.role),
                          "text/html; charset=utf-8");
        }
        return;
      case oj::services::UpdateProblemStatus::kNotFound:
        res.status = 404;
        res.set_content(views::ErrorPage(404, "题目不存在", "该题目已被移除或编号无效。",
                                         session.username, session.role),
                        "text/html; charset=utf-8");
        return;
      default:
        Render500(res);
        return;
    }
  });

  // POST /admin/problems/{id}/delete → 物理删除（FR-4.3，级联删用例；前端二次确认）。
  svr.Post(R"(/admin/problems/(\d+)/delete)", [](const httplib::Request& req,
                                                 httplib::Response& res) {
    oj::auth::Session session;
    if (!oj::auth::RequireAdmin(req, res, session)) {
      return;
    }

    long long id = 0;
    if (!oj::http::ParseId(req.matches[1].str(), id)) {
      res.status = 404;
      res.set_content(views::ErrorPage(404, "页面不存在", "请求的资源不存在。",
                                       session.username, session.role),
                      "text/html; charset=utf-8");
      return;
    }

    if (!oj::services::DeleteProblem(id)) {
      Render500(res);
      return;
    }
    res.set_redirect("/admin/problems", 302);
  });

  // 统一错误页：未匹配路由（404）、异常（500）等状态码 ≥ 400 时渲染。
  // 已在路由内显式 set_content 的错误页（如「题目不存在」）保持原样，不覆盖。
  svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
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
                                     httplib::detail::status_message(code),
                                     CurrentUsername(req), CurrentRole(req)),
                    "text/html; charset=utf-8");
  });
}

}  // namespace oj
