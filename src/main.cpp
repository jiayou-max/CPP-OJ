#include <string>

#include "config.h"
#include "httplib.h"
#include "logger.h"
#include "routes.h"

int main() {
  oj::LogInit();

  httplib::Server svr;
  oj::RegisterRoutes(svr);

  // 每个请求打一条访问日志：方法 + 路径 + 状态码
  svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
    oj::LogInfo(req.method + " " + req.path + " -> " + std::to_string(res.status));
  });

  oj::LogInfo("OJ server listening on http://localhost:" + std::to_string(oj::kServerPort));
  return svr.listen("0.0.0.0", oj::kServerPort);
}
