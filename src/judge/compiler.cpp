#include "judge/compiler.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>

#include "config.h"
#include "logger.h"

namespace oj::judge {

namespace {

// 递归读取文件全部内容；失败返回空串。
std::string ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return "";
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// 向文件写入内容；成功返回 true。
bool WriteFile(const std::string& path, const std::string& content) {
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    return false;
  }
  f << content;
  f.flush();
  return f.good();
}

}  // namespace

Compiler::~Compiler() { Cleanup(); }

CompileResult Compiler::Compile(const std::string& source_code) {
  CompileResult result;
  Cleanup();  // 幂等：复用实例时先清理旧目录

  // 1) 创建随机临时目录（mkdtemp 就地改写模板，SPEC 5「随机临时目录避免互相干扰」）。
  std::string tmpl = kJudgeTempDirTemplate;
  if (char* dir = mkdtemp(tmpl.data()); dir != nullptr) {
    work_dir_ = tmpl;
  } else {
    result.error_log = "创建临时目录失败: " + std::string(std::strerror(errno));
    LogError("judge::Compiler: " + result.error_log);
    return result;
  }
  binary_path_ = work_dir_ + "/main";
  const std::string source_path = work_dir_ + "/main.cpp";
  const std::string log_path = work_dir_ + "/compile.log";

  // 2) 写入用户代码到 main.cpp。
  if (!WriteFile(source_path, source_code)) {
    result.error_log = "写入 main.cpp 失败";
    LogError("judge::Compiler: " + result.error_log + " (" + work_dir_ + ")");
    return result;
  }

  // 3) fork + 子进程降为 g++ 编译，stderr 重定向到 compile.log（CE 日志捕获）。
  const int log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (log_fd < 0) {
    result.error_log = "创建 compile.log 失败: " + std::string(std::strerror(errno));
    LogError("judge::Compiler: " + result.error_log);
    return result;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(log_fd);
    result.error_log = "fork 失败: " + std::string(std::strerror(errno));
    LogError("judge::Compiler: " + result.error_log);
    return result;
  }

  // 父进程也主动 setpgid，避免与子进程 setpgid(0,0) 之间的竞态：
  // 若父进程在子进程 setpgid 前先超时，仍能对整个进程组发信号。
  if (setpgid(pid, pid) != 0) {
    LogWarn("judge::Compiler: setpgid 失败: " + std::string(std::strerror(errno)));
  }

  if (pid == 0) {
    // 子进程：纳入独立进程组，便于父进程超时后整组终止
    // （g++ 会派生子进程 cc1plus/as，若只 kill 父进程会遗留孤儿进程）。
    setpgid(0, 0);
    // stdout 丢弃，stderr → compile.log，随后 exec g++。
    close(log_fd);
    const int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
      close(devnull);
    }
    const int err_out = open(log_path.c_str(), O_WRONLY | O_APPEND);
    if (err_out >= 0) {
      dup2(err_out, STDERR_FILENO);
      close(err_out);
    }
    if (chdir(work_dir_.c_str()) != 0) {
      _exit(126);
    }
    execlp(kCompilerBinary, kCompilerBinary, kCompilerFlags, "-o", "main", "main.cpp",
           static_cast<char*>(nullptr));
    // exec 失败（如 g++ 未安装）：向 compile.log 写入错误后退出。
    std::string err = std::string("无法执行 ") + kCompilerBinary + ": " +
                      std::strerror(errno) + "\n";
    if (write(STDERR_FILENO, err.data(), err.size()) < 0) {
      // 忽略：仅尽力写入 CE 日志
    }
    _exit(127);
  }

  // 4) 父进程：等待编译结束，带超时兜底（SPEC 5 / D8：编译 10s 兜底 → CE）。
  close(log_fd);
  int status = 0;
  bool timed_out = false;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(compile_timeout_sec_);
  while (true) {
    const pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      break;  // 编译已结束
    }
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      result.error_log = "waitpid 失败: " + std::string(std::strerror(errno));
      LogError("judge::Compiler: " + result.error_log);
      kill(-pid, SIGKILL);  // 终止整个编译进程组
      waitpid(pid, &status, 0);
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      // 编译超时：强制终止整个进程组（含 g++ 派生的 cc1plus/as 子进程，
      // 避免遗留孤儿进程占用 CPU）。
      kill(-pid, SIGKILL);
      waitpid(pid, &status, 0);
      timed_out = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // 5) 读取 CE 日志并判定结果。
  const std::string log = ReadFile(log_path);
  if (timed_out) {
    result.timeout = true;
    result.error_log = "编译超时（超过 " + std::to_string(compile_timeout_sec_) +
                       "s），已强制终止。\n" + log;
    LogWarn("judge::Compiler: 编译超时，已终止子进程 pid=" + std::to_string(pid));
    return result;
  }

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    result.ok = true;
    LogDebug("judge::Compiler: 编译成功 " + work_dir_);
  } else {
    result.ok = false;
    result.error_log = log;
    if (result.error_log.empty()) {
      result.error_log = "编译失败（无 stderr 输出）";
    }
    LogDebug("judge::Compiler: 编译失败，退出码 " + std::to_string(WEXITSTATUS(status)));
  }
  return result;
}

void Compiler::Cleanup() {
  if (work_dir_.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove_all(work_dir_, ec);
  if (ec) {
    LogWarn("judge::Compiler: 清理临时目录失败 " + work_dir_ + ": " + ec.message());
  }
  work_dir_.clear();
  binary_path_.clear();
}

}  // namespace oj::judge
