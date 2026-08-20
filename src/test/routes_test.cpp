#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "httplib.h"
#include "routes.h"

namespace {

// 在随机空闲端口启动真实 HTTP 服务，用 httplib::Client 端到端验证路由。
class ServerFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    oj::RegisterRoutes(svr_);

    const int port = svr_.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0) << "bind_to_any_port 失败";

    thread_ = std::thread([this]() { svr_.listen_after_bind(); });
    client_ = std::make_unique<httplib::Client>("http://127.0.0.1:" + std::to_string(port));

    // 等待服务进入监听状态（最多 ~2s）
    bool ready = false;
    for (int i = 0; i < 200; ++i) {
      if (auto res = client_->Get("/hello")) {
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

  httplib::Server svr_;
  std::thread thread_;
  std::unique_ptr<httplib::Client> client_;
};

// 首页 GET / 应返回 200 + SSR HTML：
// 验证状态码、Content-Type 头，以及页面是否包含标题/DOCTYPE/正文关键内容，
// 确认路由能端到端产出完整页面（而非空响应或错误页）。
TEST_F(ServerFixture, RootReturns200HtmlSsr) {
  auto res = client_->Get("/");
  ASSERT_TRUE(res) << "GET / 失败: " << httplib::to_string(res.error());
  EXPECT_EQ(200, res->status);

  EXPECT_EQ("text/html; charset=utf-8", res->get_header_value("Content-Type"));

  EXPECT_NE(std::string::npos, res->body.find("Hello, OJ!"));
  EXPECT_NE(std::string::npos, res->body.find("<!DOCTYPE html>"));
  EXPECT_NE(std::string::npos, res->body.find("cpp-httplib SSR 已跑通。"));
}

// GET /hello 是阶段 0 的 Hello World 探针路由：
// 验证其返回 200 且正文包含 "Hello World from cpp-httplib"。
TEST_F(ServerFixture, HelloRouteReturnsHelloWorld) {
  auto res = client_->Get("/hello");
  ASSERT_TRUE(res) << "GET /hello 失败: " << httplib::to_string(res.error());
  EXPECT_EQ(200, res->status);
  EXPECT_NE(std::string::npos, res->body.find("Hello World from cpp-httplib"));
}

// 未注册的路径应返回 404，防止幽灵路由被意外吞掉（SPEC FR-2.3 异常路由）。
TEST_F(ServerFixture, UnknownPathReturns404) {
  auto res = client_->Get("/does-not-exist");
  ASSERT_TRUE(res) << "GET /does-not-exist 失败: " << httplib::to_string(res.error());
  EXPECT_EQ(404, res->status);
}

// 连续多次请求首页都应稳定返回 200，
// 验证路由处理器无状态污染、服务在线程池下可重复响应。
TEST_F(ServerFixture, RootIsIdempotent) {
  for (int i = 0; i < 3; ++i) {
    auto res = client_->Get("/");
    ASSERT_TRUE(res) << "第 " << i << " 次 GET / 失败";
    EXPECT_EQ(200, res->status);
  }
}

}  // namespace
