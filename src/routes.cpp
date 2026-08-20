#include "routes.h"

#include "views/render.h"

namespace oj {

void RegisterRoutes(httplib::Server& svr) {
  svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
    const std::string body =
        "<h1>Hello, OJ!</h1>\n"
        "<p>cpp-httplib SSR 已跑通。</p>\n"
        "<ul>\n"
        "  <li><a href=\"/problems\">题库列表</a> (待阶段 3)</li>\n"
        "  <li><a href=\"/login\">登录</a> (待阶段 2)</li>\n"
        "</ul>\n";
    res.set_content(views::Layout("Hello, OJ!", body), "text/html; charset=utf-8");
  });

  svr.Get("/hello", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("<h1>Hello World from cpp-httplib</h1>", "text/html; charset=utf-8");
  });
}

}  // namespace oj
