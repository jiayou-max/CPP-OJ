#include "views/render.h"

#include <string>

#include "http_utils.h"
#include "views/template.h"

namespace oj::views {

std::string NavLinks(const std::string& username,
                     const std::string& role) {
  if (username.empty()) {
    return "<a href=\"/login\">登录</a>"
           "<a href=\"/register\">注册</a>";
  }
  std::string out;
  if (role == "admin") {
    out += "<a href=\"/admin/problems\">管理</a>";
  }
  out += "<span class=\"navbar-user\">" + oj::http::HtmlEscape(username) +
         "</span>"
         "<form action=\"/logout\" method=\"post\" class=\"navbar-logout\">"
         "<button type=\"submit\">登出</button>"
         "</form>";
  return out;
}

std::string Layout(const std::string& title, const std::string& body) {
  return Layout(title, body, NavLinks(""));
}

std::string Layout(const std::string& title, const std::string& body,
                   const std::string& nav_links) {
  const std::string html = LoadTemplate("layout.html");
  std::string out = ReplaceAll(html, "{{title}}", title);
  out = ReplaceAll(out, "{{body}}", body);
  out = ReplaceAll(out, "{{nav_links}}", nav_links);
  return out;
}

}  // namespace oj::views
