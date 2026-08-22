#include "auth/middleware.h"

#include <string>

#include "auth/session.h"
#include "config.h"
#include "httplib.h"
#include "views/error_pages.h"

namespace oj::auth {

bool GetLoginSession(const httplib::Request& req, Session& out) {
  SessionManager* mgr = Sessions();
  if (mgr == nullptr) {
    return false;
  }
  const std::string token =
      mgr->ParseCookie(req.get_header_value("Cookie"), kSessionCookieName);
  if (token.empty()) {
    return false;
  }
  return mgr->GetSession(token, out);
}

bool RequireLogin(const httplib::Request& req, httplib::Response& res,
                  Session& out) {
  if (GetLoginSession(req, out)) {
    return true;
  }
  res.set_redirect("/login", 302);
  return false;
}

bool RequireAdmin(const httplib::Request& req, httplib::Response& res,
                  Session& out) {
  if (!GetLoginSession(req, out)) {
    res.set_redirect("/login", 302);
    return false;
  }
  if (out.role != "admin") {
    res.status = 403;
    res.set_content(views::ErrorPage(403, "访问被拒绝", "需要管理员权限。"),
                    "text/html; charset=utf-8");
    return false;
  }
  return true;
}

}  // namespace oj::auth
