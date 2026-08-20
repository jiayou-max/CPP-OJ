#pragma once

#include <string>

#include "config.h"

namespace oj {

// 日志级别（从低到高）。
enum class LogLevel {
  kDebug = 0,
  kInfo = 1,
  kWarn = 2,
  kError = 3,
};

// 初始化日志系统（幂等，进程启动时调用一次）。
// error_log_path 指定错误日志文件路径；传空串则关闭文件日志（仅控制台）。
// 未调用时默认使用 config.h 的 kErrorLogPath。
void LogInit(const std::string& error_log_path = kErrorLogPath);

// 输出一条日志（线程安全，格式：时间 [级别] 消息）：
//   - 所有级别同时打印到控制台 stdout；
//   - kWarn / kError 额外追加写入错误日志文件（NFR-6：关键错误文件日志）。
void Log(LogLevel level, const std::string& message);

void LogDebug(const std::string& message);
void LogInfo(const std::string& message);
void LogWarn(const std::string& message);
void LogError(const std::string& message);

}  // namespace oj
