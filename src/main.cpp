#include <cstdlib>
#include <string>

#include "config.h"
#include "db/db.h"
#include "httplib.h"
#include "logger.h"
#include "routes.h"

int main() {
  oj::LogInit();

  // 连接参数取自 config.h（localhost:3306 / root / 免密），可用环境变量覆盖
  // （SPEC 4.3.1 / docs/deployment.md「连接参数覆盖」）：OJ_DB_HOST / OJ_DB_PORT /
  // OJ_DB_USER / OJ_DB_PASSWORD。测试与初始化工具复用 db::DbConfigFromEnv。
  const oj::db::DbConfig db_cfg = oj::db::DbConfigFromEnv();

  // 初始化 MySQL 连接池（阶段 2 题目浏览依赖）。失败不阻塞启动：
  // 路由层在无连接池时返回 500 错误页（SPEC A14「MySQL 不可用 → 500 页」），
  // 启动自检细化见阶段 6。
  if (oj::db::DbInit(db_cfg)) {
    oj::LogInfo("main: MySQL 连接池就绪");
  } else {
    oj::LogWarn("main: MySQL 连接池初始化失败，题目页面将返回 500（请检查 MySQL 与 db/schema.sql）");
  }

  httplib::Server svr;
  oj::RegisterRoutes(svr);

  // 每个请求打一条访问日志：方法 + 路径 + 状态码
  svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
    oj::LogInfo(req.method + " " + req.path + " -> " + std::to_string(res.status));
  });

  oj::LogInfo("OJ server listening on http://localhost:" + std::to_string(oj::kServerPort));
  return svr.listen("0.0.0.0", oj::kServerPort);
}
