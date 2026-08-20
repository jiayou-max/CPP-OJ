#include "logger.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>

#include "config.h"

namespace oj {
namespace {

// 日志全局状态（单进程）：错误日志文件流 + 保护锁。
// 服务为多线程（线程池）处理请求，所有读写都持有 g_mutex 串行化。
std::ofstream g_error_file;
std::mutex g_mutex;
std::string g_error_log_path = kErrorLogPath;  // 空串表示关闭文件日志

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "DEBUG";
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarn:
      return "WARN";
    case LogLevel::kError:
      return "ERROR";
  }
  return "UNKNOWN";
}

// 打开错误日志文件（追加模式）。失败时降级为仅控制台，并直接打 stderr 提示
// （此处不能用 Log()，避免在持有 g_mutex 时递归加锁）。
void OpenErrorFile() {
  if (g_error_log_path.empty()) {
    return;
  }
  g_error_file.open(g_error_log_path, std::ios::app);
  if (!g_error_file.is_open()) {
    std::fprintf(stderr,
                 "[ERROR] 无法打开日志文件: %s（错误日志将仅输出到控制台）\n",
                 g_error_log_path.c_str());
    g_error_log_path.clear();
  }
}

// 当前时间戳，形如 "2026-08-20 21:33:05"。localtime_r 保证线程安全。
std::string CurrentTimestamp() {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  char buf[32] = {0};
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

}  // namespace

void LogInit(const std::string& error_log_path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_error_log_path = error_log_path;
  g_error_file.close();
  OpenErrorFile();
}

void Log(LogLevel level, const std::string& message) {
  std::lock_guard<std::mutex> lock(g_mutex);

  const std::string line = "[" + CurrentTimestamp() + "] [" + LevelName(level) +
                           "] " + message + "\n";
  std::fputs(line.c_str(), stdout);
  std::fflush(stdout);

  if (level >= LogLevel::kWarn) {
    if (!g_error_file.is_open()) {
      OpenErrorFile();  // 懒打开：首个 WARN/ERROR 时才创建文件
    }
    if (g_error_file.is_open()) {
      g_error_file << line;
      g_error_file.flush();
    }
  }
}

void LogDebug(const std::string& message) { Log(LogLevel::kDebug, message); }
void LogInfo(const std::string& message) { Log(LogLevel::kInfo, message); }
void LogWarn(const std::string& message) { Log(LogLevel::kWarn, message); }
void LogError(const std::string& message) { Log(LogLevel::kError, message); }

}  // namespace oj
