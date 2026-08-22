#include <string>

#include <gtest/gtest.h>

#include "auth/middleware.h"
#include "auth/session.h"
#include "httplib.h"

namespace {

// require_login / require_admin 中间件测试（不依赖数据库）。
// 直接构造 httplib::Request/Response，走全局会话单例。
class MiddlewareTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(oj::auth::SessionInit());
    mgr_ = oj::auth::Sessions();
    ASSERT_NE(nullptr, mgr_);
  }

  // 构造带登录 Cookie 的请求（user_id/role）。
  httplib::Request RequestWithSession(long long user_id,
                                      const std::string& role) const {
    const std::string token =
        mgr_->CreateSession(user_id, "user_" + std::to_string(user_id), role);
    httplib::Request req;
    req.set_header("Cookie", "oj_session=" + token);
    return req;
  }

  httplib::Request AnonymousRequest() const {
    httplib::Request req;
    req.set_header("Cookie", "");
    return req;
  }

  oj::auth::SessionManager* mgr_ = nullptr;
};

// 未登录调用 require_login → 302 重定向 /login（FR-1.5），返回 false。
TEST_F(MiddlewareTest, RequireLoginRedirectsWhenAnonymous) {
  httplib::Response res;
  oj::auth::Session out;
  EXPECT_FALSE(oj::auth::RequireLogin(AnonymousRequest(), res, out));
  EXPECT_EQ(302, res.status);
  EXPECT_EQ("/login", res.get_header_value("Location"));
}

// 合法会话通过 require_login，且 out 被填充（session.user_id 供落库）。
TEST_F(MiddlewareTest, RequireLoginAcceptsValidSession) {
  httplib::Response res;
  oj::auth::Session out;
  EXPECT_TRUE(oj::auth::RequireLogin(RequestWithSession(7, "user"), res, out));
  EXPECT_EQ(7, out.user_id);
  EXPECT_EQ("user", out.role);
}

// 伪造/过期 token → 同样按未登录处理（302），不进入业务逻辑。
TEST_F(MiddlewareTest, RequireLoginRejectsGarbageToken) {
  httplib::Request req;
  req.set_header("Cookie", "oj_session=not-a-real-token");
  httplib::Response res;
  oj::auth::Session out;
  EXPECT_FALSE(oj::auth::RequireLogin(req, res, out));
  EXPECT_EQ(302, res.status);
}

// admin 角色通过 require_admin（FR-4.1）。
TEST_F(MiddlewareTest, RequireAdminAcceptsAdminRole) {
  httplib::Response res;
  oj::auth::Session out;
  EXPECT_TRUE(oj::auth::RequireAdmin(RequestWithSession(1, "admin"), res, out));
  EXPECT_EQ("admin", out.role);
}

// 普通用户访问 admin 资源 → 403 错误页（A5）。
TEST_F(MiddlewareTest, RequireAdminReturns403ForUser) {
  httplib::Response res;
  oj::auth::Session out;
  EXPECT_FALSE(oj::auth::RequireAdmin(RequestWithSession(2, "user"), res, out));
  EXPECT_EQ(403, res.status);
  EXPECT_NE(std::string::npos, res.body.find("访问被拒绝"));
}

// 未登录调用 require_admin → 302 重定向 /login。
TEST_F(MiddlewareTest, RequireAdminRedirectsWhenAnonymous) {
  httplib::Response res;
  oj::auth::Session out;
  EXPECT_FALSE(oj::auth::RequireAdmin(AnonymousRequest(), res, out));
  EXPECT_EQ(302, res.status);
  EXPECT_EQ("/login", res.get_header_value("Location"));
}

// GetLoginSession：合法会话 true；伪造 token / 无 Cookie false 且 out 不修改。
TEST_F(MiddlewareTest, GetLoginSession) {
  oj::auth::Session out;
  out.user_id = 99;
  EXPECT_TRUE(oj::auth::GetLoginSession(RequestWithSession(5, "user"), out));
  EXPECT_EQ(5, out.user_id);

  EXPECT_FALSE(oj::auth::GetLoginSession(AnonymousRequest(), out));
  EXPECT_EQ(5, out.user_id);  // 失败时 out 不被修改

  httplib::Request req;
  req.set_header("Cookie", "oj_session=bad-token");
  EXPECT_FALSE(oj::auth::GetLoginSession(req, out));
  EXPECT_EQ(5, out.user_id);
}

}  // namespace
