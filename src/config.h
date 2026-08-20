#pragma once

namespace oj {

// 全局配置（阶段 0 起逐步充实，对应 SPEC 4.6 src/config.h）
constexpr int kServerPort = 8080;

// 错误日志文件路径（相对进程工作目录，NFR-6：关键错误文件日志）
constexpr const char* kErrorLogPath = "oj_error.log";

}  // namespace oj
