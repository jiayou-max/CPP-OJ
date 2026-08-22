#pragma once

#include "auth/session.h"
#include "httplib.h"

namespace oj::auth {

// 访问控制辅助（阶段 4，对应 SPEC 4.6 src/auth/middleware.h|.cpp，FR-1.5）。
// 用法：路由处理函数开头调用；返回 false 时中间件已写好响应（重定向/错误页），
// 路由直接 return 即可。
//
//   Session session;
//   if (!oj::auth::RequireLogin(req, res, session)) return;
//   // session.user_id / session.role 已填充

// 从请求 Cookie 解析并校验会话（滑动续期）。未登录/过期返回 false，out 不修改。
bool GetLoginSession(const httplib::Request& req, Session& out);

// require_login：已登录 → true 并填充 out；未登录 → 302 重定向 /login（FR-1.5）。
bool RequireLogin(const httplib::Request& req, httplib::Response& res, Session& out);

// require_admin：admin → true 并填充 out；未登录 → 302 重定向 /login；
// 已登录但非 admin → 403 错误页（FR-4.1 / A5）。
bool RequireAdmin(const httplib::Request& req, httplib::Response& res, Session& out);

}  // namespace oj::auth
