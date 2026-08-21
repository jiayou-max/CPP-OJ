#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <unistd.h>

#include <gtest/gtest.h>

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

// 提交（阶段 3 判题内核）：匿名提交执行判题并渲染结果页，不再重定向登录。
// 数据库可用 → 判题流程；不可用 → 500 错误页（A14）。
TEST_F(ServerFixture, SubmitRunsJudgeOr500) {
  if (!db_ok_) {
    GTEST_SKIP() << "本机 MySQL 不可达，跳过判题提交路由测试";
  }

  const long long pid = InsertJudgeProblem();
  ASSERT_GT(pid, 0) << "插入题目/用例失败（请先执行 mysql -uroot < db/schema.sql）";

  // AC：正确代码 → 200 结果页，总体通过。
  const std::string ac_code =
      "#include <iostream>\n"
      "int main(){long long a,b;while(std::cin>>a>>b)std::cout<<(a+b);return 0;}";
  const std::string ac_path = "/problems/" + std::to_string(pid) + "/submit";
  auto ac = client_->Post(ac_path.c_str(), httplib::Params{{"code", ac_code}});
  ASSERT_TRUE(ac) << "POST submit(AC) 失败";
  EXPECT_EQ(200, ac->status);
  EXPECT_NE(std::string::npos, ac->body.find("判题结果"));
  EXPECT_NE(std::string::npos, ac->body.find("通过 (AC)"));
  EXPECT_NE(std::string::npos, ac->body.find("逐用例结果"));

  // CE：语法错误代码 → 200 结果页，展示编译错误 + g++ 日志。
  const std::string ce_code = "int main(){int x=;return 0;}";
  auto ce = client_->Post(ac_path.c_str(), httplib::Params{{"code", ce_code}});
  ASSERT_TRUE(ce) << "POST submit(CE) 失败";
  EXPECT_EQ(200, ce->status);
  EXPECT_NE(std::string::npos, ce->body.find("编译错误 (CE)"));
  EXPECT_NE(std::string::npos, ce->body.find("error:"));

  DeleteProblem(pid);
}

// 提交到不存在的题目：数据库可用 → 404「题目不存在」；不可用 → 500。
TEST_F(ServerFixture, SubmitMissingProblemReturns404Or500) {
  auto res = client_->Post("/problems/99999999/submit",
                           httplib::Params{{"code", "int main(){}"}});
  ASSERT_TRUE(res) << "POST submit 失败";
  if (db_ok_) {
    EXPECT_EQ(404, res->status);
    EXPECT_NE(std::string::npos, res->body.find("题目不存在"));
  } else {
    EXPECT_EQ(500, res->status);
  }
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
