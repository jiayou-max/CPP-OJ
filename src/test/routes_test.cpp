#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include <unistd.h>

#include <gtest/gtest.h>

#include "auth/session.h"
#include "db/db.h"
#include "db/db_query.h"
#include "httplib.h"
#include "routes.h"

namespace {

// 在随机空闲端口启动真实 HTTP 服务，用 httplib::Client 端到端验证路由。
// 题目相关路由依赖 MySQL：不可达时返回 500（SPEC A14），测试据此分支断言。
class ServerFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    oj::RegisterRoutes(svr_);

    // 数据库可达性：DbInit 幂等；失败时 DbPool() 为 nullptr（路由返回 500 页）。
    // 支持 OJ_DB_* 环境变量覆盖，与 main.cpp 一致（SPEC 4.3.1）。
    oj::db::DbInit(oj::db::DbConfigFromEnv());
    db_ok_ = oj::db::DbPool() != nullptr;

    // 全局会话管理器（阶段 4：submit/登录路由依赖；SessionInit 幂等）。
    ASSERT_TRUE(oj::auth::SessionInit());

    const int port = svr_.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0) << "bind_to_any_port 失败";

    thread_ = std::thread([this]() { svr_.listen_after_bind(); });
    client_ = std::make_unique<httplib::Client>("http://127.0.0.1:" + std::to_string(port));

    // 等待服务进入监听状态（最多 ~2s）。探针用 GET /problems：
    // 无论 MySQL 是否可用都会得到 HTTP 响应（200/500），只用于确认已监听。
    bool ready = false;
    for (int i = 0; i < 200; ++i) {
      if (auto res = client_->Get("/problems")) {
        ready = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(ready) << "服务在超时前未就绪";
  }

  void TearDown() override {
    svr_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  // 插入带一组 A+B 用例的题目，返回题目 id（失败返回 0）。
  long long InsertJudgeProblem() {
    oj::db::PreparedStatement ins(oj::db::DbPool()->Acquire(),
        "INSERT INTO problems (title, description, difficulty) VALUES (?,?,?)");
    if (!ins.PrepareOk()) return 0;
    ins.Bind(std::string("zz_judge_route_test"));
    ins.Bind(std::string("A+B"));
    ins.Bind(std::string("easy"));
    if (!ins.Execute()) return 0;
    const long long pid = static_cast<long long>(ins.InsertId());
    if (pid <= 0) return 0;

    oj::db::PreparedStatement tc(oj::db::DbPool()->Acquire(),
        "INSERT INTO testcases (problem_id, `order`, input, expected) VALUES (?,?,?,?)");
    if (!tc.PrepareOk()) { DeleteProblem(pid); return 0; }
    tc.Bind(pid);
    tc.Bind(1);
    tc.Bind(std::string("1 2\n"));
    tc.Bind(std::string("3\n"));
    if (!tc.Execute()) { DeleteProblem(pid); return 0; }
    return pid;
  }

  void DeleteProblem(long long pid) {
    if (pid <= 0) return;
    oj::db::PreparedStatement del(oj::db::DbPool()->Acquire(),
        "DELETE FROM problems WHERE id=?");
    if (!del.PrepareOk()) return;
    del.Bind(pid);
    del.Execute();
  }

  // 从响应 Set-Cookie 头提取会话 Cookie 值（形如 "oj_session=<token>"）；失败返回空串。
  std::string ExtractSessionCookie(const httplib::Response& res) {
    const std::string set_cookie = res.get_header_value("Set-Cookie");
    const std::string::size_type start = set_cookie.find("oj_session=");
    if (start == std::string::npos) {
      return {};
    }
    const std::string::size_type end = set_cookie.find(';', start);
    return set_cookie.substr(start, end == std::string::npos ? std::string::npos
                                                             : end - start);
  }

  // 注册新用户并返回其会话 Cookie 值；失败返回空串。
  std::string RegisterAndGetCookie(const std::string& username,
                                   const std::string& password) {
    auto res = client_->Post("/register",
        httplib::Params{{"username", username}, {"password", password}});
    if (!res) {
      return {};
    }
    return ExtractSessionCookie(*res);
  }

  // 删除测试用户及其提交记录（FK user RESTRICT 保护提交，须先删提交再删用户）。
  void DeleteUser(const std::string& username) {
    oj::db::PreparedStatement dels(oj::db::DbPool()->Acquire(),
        "DELETE s FROM submissions s JOIN users u ON s.user_id=u.id WHERE u.username=?");
    if (dels.PrepareOk()) {
      dels.Bind(username);
      dels.Execute();
    }
    oj::db::PreparedStatement delu(oj::db::DbPool()->Acquire(),
        "DELETE FROM users WHERE username=?");
    if (delu.PrepareOk()) {
      delu.Bind(username);
      delu.Execute();
    }
  }

  // 随机后缀，生成唯一用户名，避免与既有数据/并行测试冲突。
  std::string RandomSuffix() {
    std::mt19937_64 rng(
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return std::to_string(rng());
  }

  httplib::Server svr_;
  std::thread thread_;
  std::unique_ptr<httplib::Client> client_;
  bool db_ok_ = false;
};

// GET / → 302 重定向到 /problems（SPEC 4.5 路由表，阶段 0 首页已移除）。
TEST_F(ServerFixture, RootRedirectsToProblems) {
  auto res = client_->Get("/");
  ASSERT_TRUE(res) << "GET / 失败: " << httplib::to_string(res.error());
  EXPECT_EQ(302, res->status);
  EXPECT_EQ("/problems", res->get_header_value("Location"));
}

// GET /problems 题库列表：数据库可用 → 200 且为 SSR HTML；不可用 → 500 错误页。
TEST_F(ServerFixture, ProblemsListPageOr500) {
  auto res = client_->Get("/problems");
  ASSERT_TRUE(res) << "GET /problems 失败";
  EXPECT_EQ("text/html; charset=utf-8", res->get_header_value("Content-Type"));
  EXPECT_NE(std::string::npos, res->body.find("<!DOCTYPE html>"));

  if (db_ok_) {
    EXPECT_EQ(200, res->status);
    EXPECT_NE(std::string::npos, res->body.find("题库"));
  } else {
    EXPECT_EQ(500, res->status);
    EXPECT_NE(std::string::npos, res->body.find("500"));
  }
}

// 非法 id（非数字）不访问数据库，一律 404 错误页（FR-2.3 异常路由）。
TEST_F(ServerFixture, InvalidProblemIdReturns404) {
  auto res = client_->Get("/problems/not-a-number");
  ASSERT_TRUE(res) << "GET /problems/not-a-number 失败";
  EXPECT_EQ(404, res->status);
  EXPECT_NE(std::string::npos, res->body.find("404"));
  EXPECT_NE(std::string::npos, res->body.find("<!DOCTYPE html>"));
}

// 不存在的题目 id：数据库可用 → 404「题目不存在」；不可用 → 500。
TEST_F(ServerFixture, MissingProblemReturns404Or500) {
  auto res = client_->Get("/problems/99999999");
  ASSERT_TRUE(res) << "GET /problems/99999999 失败";

  if (db_ok_) {
    EXPECT_EQ(404, res->status);
    EXPECT_NE(std::string::npos, res->body.find("题目不存在"));
  } else {
    EXPECT_EQ(500, res->status);
  }
}

// 未登录提交 → 302 重定向登录页（FR-1.5，须先于题目是否存在等校验）。
TEST_F(ServerFixture, SubmitRequiresLogin) {
  auto res = client_->Post("/problems/99999999/submit",
                           httplib::Params{{"code", "int main(){}"}});
  ASSERT_TRUE(res) << "POST submit(匿名) 失败";
  EXPECT_EQ(302, res->status);
  EXPECT_EQ("/login", res->get_header_value("Location"));
}

// 登录态提交（阶段 3 判题内核）：AC + CE。
// 数据库可用 → 判题流程；不可用 → 500 错误页（A14）。
TEST_F(ServerFixture, SubmitRunsJudgeOr500) {
  if (!db_ok_) {
    GTEST_SKIP() << "本机 MySQL 不可达，跳过判题提交路由测试";
  }

  const long long pid = InsertJudgeProblem();
  ASSERT_GT(pid, 0) << "插入题目/用例失败（请先执行 mysql -uroot < db/schema.sql）";

  // 登录态提交：注册 → 取会话 Cookie → 带 Cookie 调用 submit。
  const std::string username = "zz_route_judge_" + RandomSuffix();
  const std::string password = "secret123";
  const std::string cookie = RegisterAndGetCookie(username, password);
  ASSERT_FALSE(cookie.empty()) << "注册失败，无法进行登录态提交";
  httplib::Headers headers;
  headers.emplace("Cookie", cookie);

  // AC：正确代码 → 200 结果页，总体通过。
  const std::string ac_code =
      "#include <iostream>\n"
      "int main(){long long a,b;while(std::cin>>a>>b)std::cout<<(a+b);return 0;}";
  const std::string ac_path = "/problems/" + std::to_string(pid) + "/submit";
  auto ac = client_->Post(ac_path.c_str(), headers, httplib::Params{{"code", ac_code}});
  ASSERT_TRUE(ac) << "POST submit(AC) 失败";
  EXPECT_EQ(200, ac->status);
  EXPECT_NE(std::string::npos, ac->body.find("判题结果"));
  EXPECT_NE(std::string::npos, ac->body.find("通过 (AC)"));
  EXPECT_NE(std::string::npos, ac->body.find("逐用例结果"));

  // CE：语法错误代码 → 200 结果页，展示编译错误 + g++ 日志。
  const std::string ce_code = "int main(){int x=;return 0;}";
  auto ce = client_->Post(ac_path.c_str(), headers, httplib::Params{{"code", ce_code}});
  ASSERT_TRUE(ce) << "POST submit(CE) 失败";
  EXPECT_EQ(200, ce->status);
  EXPECT_NE(std::string::npos, ce->body.find("编译错误 (CE)"));
  EXPECT_NE(std::string::npos, ce->body.find("error:"));

  DeleteProblem(pid);
  DeleteUser(username);
}

// 提交到不存在的题目：登录态 → 数据库可用返回 404「题目不存在」；不可用 → 500。
TEST_F(ServerFixture, SubmitMissingProblemReturns404Or500) {
  if (!db_ok_) {
    GTEST_SKIP() << "本机 MySQL 不可达，跳过缺题提交测试";
  }
  const std::string username = "zz_route_missing_" + RandomSuffix();
  const std::string cookie = RegisterAndGetCookie(username, "secret123");
  ASSERT_FALSE(cookie.empty()) << "注册失败";
  httplib::Headers headers;
  headers.emplace("Cookie", cookie);

  auto res = client_->Post("/problems/99999999/submit", headers,
                           httplib::Params{{"code", "int main(){}"}});
  ASSERT_TRUE(res) << "POST submit 失败";
  EXPECT_EQ(404, res->status);
  EXPECT_NE(std::string::npos, res->body.find("题目不存在"));

  DeleteUser(username);
}

// GET /login 与 GET /register 应返回 SSR 表单页（阶段 4 页面路由）。
TEST_F(ServerFixture, LoginAndRegisterPagesRender) {
  auto login = client_->Get("/login");
  ASSERT_TRUE(login) << "GET /login 失败";
  EXPECT_EQ(200, login->status);
  EXPECT_NE(std::string::npos,
            login->body.find("<form action=\"/login\" method=\"post\""));
  EXPECT_NE(std::string::npos, login->body.find("<!DOCTYPE html>"));

  auto reg = client_->Get("/register");
  ASSERT_TRUE(reg) << "GET /register 失败";
  EXPECT_EQ(200, reg->status);
  EXPECT_NE(std::string::npos,
            reg->body.find("<form action=\"/register\" method=\"post\""));
}

// 注册 → 自动登录（302 + Set-Cookie）→ 重复注册被拒（A3）→ 登录成功闭环。
TEST_F(ServerFixture, RegisterLoginLogoutFlow) {
  if (!db_ok_) {
    GTEST_SKIP() << "本机 MySQL 不可达，跳过注册/登录路由测试";
  }

  const std::string username = "zz_route_user_" + RandomSuffix();
  const std::string password = "secret123";

  // 注册成功 → 302 且下发会话 Cookie（自动登录）。
  auto reg = client_->Post("/register",
      httplib::Params{{"username", username}, {"password", password}});
  ASSERT_TRUE(reg) << "POST /register 失败";
  EXPECT_EQ(302, reg->status);
  const std::string cookie = ExtractSessionCookie(*reg);
  ASSERT_FALSE(cookie.empty()) << "注册未下发会话 Cookie";

  // 重复注册 → 200 表单页 + 明确错误提示，不落库（FR-1.1 / A3）。
  auto dup = client_->Post("/register",
      httplib::Params{{"username", username}, {"password", password}});
  ASSERT_TRUE(dup) << "POST /register(重复) 失败";
  EXPECT_EQ(200, dup->status);
  EXPECT_NE(std::string::npos, dup->body.find("用户名已存在"));

  // 已登录访问登录/注册页 → 直接回题库（避免重复登录）。
  httplib::Headers headers;
  headers.emplace("Cookie", cookie);
  auto back = client_->Get("/login", headers);
  ASSERT_TRUE(back) << "GET /login(已登录) 失败";
  EXPECT_EQ(302, back->status);
  EXPECT_EQ("/problems", back->get_header_value("Location"));

  // 错误密码登录 → 200 表单页 + 错误提示。
  auto bad = client_->Post("/login",
      httplib::Params{{"username", username}, {"password", "wrong_password"}});
  ASSERT_TRUE(bad) << "POST /login(错误密码) 失败";
  EXPECT_EQ(200, bad->status);
  EXPECT_NE(std::string::npos, bad->body.find("用户名或密码错误"));

  // 正确密码登录 → 302 + 新会话 Cookie。
  auto ok = client_->Post("/login",
      httplib::Params{{"username", username}, {"password", password}});
  ASSERT_TRUE(ok) << "POST /login(正确) 失败";
  EXPECT_EQ(302, ok->status);
  EXPECT_NE(std::string::npos, ok->get_header_value("Set-Cookie").find("oj_session="));

  // 登出 → 302 + 清 Cookie（Max-Age=0），原会话失效。
  auto out = client_->Post("/logout", headers, httplib::Params{});
  ASSERT_TRUE(out) << "POST /logout 失败";
  EXPECT_EQ(302, out->status);
  EXPECT_NE(std::string::npos, out->get_header_value("Set-Cookie").find("Max-Age=0"));

  // 未登录登出 → 302 重定向登录页。
  auto anon_out = client_->Post("/logout", httplib::Params{});
  ASSERT_TRUE(anon_out) << "POST /logout(匿名) 失败";
  EXPECT_EQ(302, anon_out->status);
  EXPECT_EQ("/login", anon_out->get_header_value("Location"));

  DeleteUser(username);
}

// 注册参数非法 → 200 表单页 + 错误提示（A14）。
TEST_F(ServerFixture, RegisterInvalidInputShowsError) {
  auto res = client_->Post("/register",
      httplib::Params{{"username", "bad name!"}, {"password", "x"}});
  ASSERT_TRUE(res) << "POST /register(非法) 失败";
  EXPECT_EQ(200, res->status);
  EXPECT_NE(std::string::npos, res->body.find("用户名仅允许字母/数字/下划线"));
}

// 导航栏登录态：游客显示登录/注册；登录后显示用户名 + 登出按钮。
TEST_F(ServerFixture, NavbarReflectsLoginState) {
  auto anon = client_->Get("/problems");
  ASSERT_TRUE(anon) << "GET /problems(游客) 失败";
  EXPECT_NE(std::string::npos, anon->body.find("href=\"/login\">登录</a>"));
  EXPECT_NE(std::string::npos, anon->body.find("href=\"/register\">注册</a>"));
  EXPECT_EQ(std::string::npos, anon->body.find("navbar-logout"));

  if (!db_ok_) {
    GTEST_SKIP() << "本机 MySQL 不可达，跳过登录态导航测试";
  }
  const std::string username = "zz_nav_user_" + RandomSuffix();
  const std::string cookie = RegisterAndGetCookie(username, "secret123");
  ASSERT_FALSE(cookie.empty()) << "注册失败";
  httplib::Headers headers;
  headers.emplace("Cookie", cookie);

  auto logged = client_->Get("/problems", headers);
  ASSERT_TRUE(logged) << "GET /problems(登录) 失败";
  EXPECT_NE(std::string::npos, logged->body.find(username));
  EXPECT_NE(std::string::npos, logged->body.find("navbar-logout"));
  EXPECT_NE(std::string::npos, logged->body.find("action=\"/logout\" method=\"post\""));
  EXPECT_EQ(std::string::npos, logged->body.find("href=\"/register\">注册</a>"));

  DeleteUser(username);
}

// 未注册的路径应返回 404 错误页（HTML），防止幽灵路由被意外吞掉。
TEST_F(ServerFixture, UnknownPathReturns404) {
  auto res = client_->Get("/does-not-exist");
  ASSERT_TRUE(res) << "GET /does-not-exist 失败: " << httplib::to_string(res.error());
  EXPECT_EQ(404, res->status);
  EXPECT_NE(std::string::npos, res->body.find("<!DOCTYPE html>"));
  EXPECT_NE(std::string::npos, res->body.find("404"));
}

// 连续多次请求首页都应稳定返回 302 重定向，验证路由无状态污染。
TEST_F(ServerFixture, RootIsIdempotent) {
  for (int i = 0; i < 3; ++i) {
    auto res = client_->Get("/");
    ASSERT_TRUE(res) << "第 " << i << " 次 GET / 失败";
    EXPECT_EQ(302, res->status);
  }
}

}  // namespace
