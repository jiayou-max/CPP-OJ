#include "views/auth_pages.h"

#include <string>

#include "http_utils.h"
#include "views/render.h"
#include "views/template.h"

namespace oj::views {

namespace {

// 表单错误提示块（error 为空串 → 不渲染）。用户输入一律 HTML 转义防 XSS（NFR-3）。
std::string ErrorHtml(const std::string& error) {
  if (error.empty()) {
    return "";
  }
  return "<div class=\"form-error\">" + oj::http::HtmlEscape(error) + "</div>";
}

}  // namespace

std::string LoginPage(const std::string& error, const std::string& username) {
  std::string body = LoadTemplate("login.html");
  body = ReplaceAll(body, "{{error_html}}", ErrorHtml(error));
  return Layout("登录 - CPP OJ", body, NavLinks(username));
}

std::string RegisterPage(const std::string& error, const std::string& username) {
  std::string body = LoadTemplate("register.html");
  body = ReplaceAll(body, "{{error_html}}", ErrorHtml(error));
  return Layout("注册 - CPP OJ", body, NavLinks(username));
}

}  // namespace oj::views
