#pragma once

namespace oj {

// 全局配置（阶段 0 起逐步充实，对应 SPEC 4.6 src/config.h）
constexpr int kServerPort = 8080;

// 错误日志文件路径（相对进程工作目录，NFR-6：关键错误文件日志）
constexpr const char* kErrorLogPath = "oj_error.log";

// ---- MySQL 连接参数（阶段 0 连接池，对应 SPEC A.1 依赖清单）----
// 本机默认 root 免密部署；如需覆盖可改用环境变量/配置文件，见 docs/deployment.md。
constexpr const char* kDbHost = "localhost";     // "localhost" 走 unix socket（本机更常见）
constexpr unsigned int kDbPort = 3306;
constexpr const char* kDbUser = "root";
constexpr const char* kDbPassword = "";          // 空 = 免密
constexpr const char* kDbName = "";              // 空 = 不预选库（schema 建立后由业务指定）
constexpr unsigned int kDbPoolSize = 8;          // 连接池上限（含占用中，NFR-2 低并发足够）
constexpr unsigned int kDbAcquireTimeoutMs = 3000;  // 池满时等待空闲连接的上限
constexpr unsigned int kDbConnectTimeoutMs = 3000;
constexpr unsigned int kDbReadTimeoutMs = 5000;
constexpr unsigned int kDbWriteTimeoutMs = 5000;

}  // namespace oj
