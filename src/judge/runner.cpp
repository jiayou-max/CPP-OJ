#ifndef _GNU_SOURCE  // wait4 / setpgid 等声明（CMake 用 -std=c++17 非 gnu 模式）
#define _GNU_SOURCE  // mysqlclient 的 pkg-config cflags 可能已预定义，此处防重复定义告警
#endif

#include "judge/runner.h"

#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
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

// 读取 /proc/<pid>/status 中 VmSize（虚拟内存，KB）；读取失败返回 -1。
// 用途：父进程轮询判断子进程是否越过了内存上限 → MLE（RLIMIT_AS 语义）。
long long ReadVmSizeKb(pid_t pid) {
  std::ifstream f("/proc/" + std::to_string(pid) + "/status");
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("VmSize:", 0) == 0) {
      long long kb = 0;
      if (std::sscanf(line.c_str() + 7, "%lld", &kb) == 1) {
        return kb;
      }
      return -1;
    }
  }
  return -1;
}

// 读取 /proc/<pid>/stat 中括号内的 comm 字段（进程当前可执行名）。
// zombie 进程仍保留 exec 后的 comm，可据此确定子进程已完成 exec。
std::string ProcessComm(pid_t pid) {
  std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
  if (!f) {
    return "";
  }
  std::string line;
  if (!std::getline(f, line)) {
    return "";
  }
  const auto open = line.find('(');
  const auto close = line.find(')');
  if (open == std::string::npos || close == std::string::npos || close <= open) {
    return "";
  }
  return line.substr(open + 1, close - open - 1);
}

// 子进程降权到 kJudgeRunUser（nobody，UID 65534）。返回 0 成功；
// 非 root 或用户缺失时返回 -1（调用方仅告警，不阻断判题）。
int DropPrivileges() {
  uid_t target_uid = 65534;
  gid_t target_gid = 65534;
  if (struct passwd* pw = getpwnam(kJudgeRunUser); pw != nullptr) {
    target_uid = pw->pw_uid;
    target_gid = pw->pw_gid;
  }
  // 必须先 setgid 再 setuid（root 下 setuid 后即失权，无法再降 gid）。
  if (setgid(target_gid) != 0) {
    return -1;
  }
  if (setuid(target_uid) != 0) {
    return -1;
  }
  return 0;
}

// 剥离 output 末尾的空白（\r\n\t 空格），用于「宽松比对」：
// 只要输出逻辑内容与 expected 一致即视为通过，容忍行尾换行/空白差异。
std::string TrimTrailingWhitespace(const std::string& s) {
  std::string out = s;
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r' ||
                          out.back() == '\t' || out.back() == ' ')) {
    out.pop_back();
  }
  return out;
}

// 将子进程 wall time 统计从 duration 转毫秒（向下取整，最小 1ms）。
long long DurationMs(const std::chrono::steady_clock::time_point& begin) {
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - begin);
  return static_cast<long long>(elapsed.count());
}

}  // namespace

CaseResult RunOneCase(const std::string& binary_path, const std::string& work_dir,
                      const std::string& input, const std::string& expected,
                      long long time_limit_ms, long long memory_limit_kb,
                      long long max_output_bytes) {
  CaseResult result;
  if (binary_path.empty() || work_dir.empty()) {
    result.status = CaseResult::Status::kRuntimeError;
    LogError("judge::RunOneCase: binary_path/work_dir 为空");
    return result;
  }

  const std::string input_path = work_dir + "/run_input.txt";
  const std::string output_path = work_dir + "/run_output.txt";
  if (!WriteFile(input_path, input)) {
    result.status = CaseResult::Status::kRuntimeError;
    LogError("judge::RunOneCase: 写输入文件失败 " + input_path);
    return result;
  }

  // 保证 nobody（或测试用户）能遍历临时目录并执行/读取可执行文件：
  // mkdtemp 生成 0700 且 g++ 产出物所有者非 nobody。
  chmod(work_dir.c_str(), 0755);
  chmod(binary_path.c_str(), 0755);

  // 父进程预先打开 stdin/stdout/stderr 的 fd，子进程 setuid 降权后直接 dup2，
  // 避免 nobody 无权打开/截断 root 拥有的文件（fd 写入不受文件权限二次校验）。
  const int input_fd = open(input_path.c_str(), O_RDONLY);
  const int output_fd = open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  const int devnull = open("/dev/null", O_WRONLY);
  if (input_fd < 0 || output_fd < 0 || devnull < 0) {
    if (input_fd >= 0) close(input_fd);
    if (output_fd >= 0) close(output_fd);
    if (devnull >= 0) close(devnull);
    result.status = CaseResult::Status::kRuntimeError;
    LogError("judge::RunOneCase: 打开运行 fd 失败: " + std::string(std::strerror(errno)));
    return result;
  }

  // 就绪判定：父进程等待子进程 comm 变为判题可执行文件名（"main"），
  // 再启动计时与内存监控，排除 fork→exec 期间子进程仍是父进程地址空间副本
  // （父进程 VmSize 可能很大，如 gtest/mysqlclient 加载到数百 MB）导致的假 MLE/假超时。
  const pid_t pid = fork();
  if (pid < 0) {
    close(input_fd);
    close(output_fd);
    close(devnull);
    result.status = CaseResult::Status::kRuntimeError;
    LogError("judge::RunOneCase: fork 失败: " + std::string(std::strerror(errno)));
    return result;
  }

  // 父进程主动 setpgid，避免与子进程 setpgid(0,0) 竞态：超时后能对整个进程组发信号。
  if (setpgid(pid, pid) != 0) {
    LogWarn("judge::RunOneCase: setpgid 失败: " + std::string(std::strerror(errno)));
  }

  if (pid == 0) {
    // ---- 子进程：降权 + 资源限制 + exec ----
    setpgid(0, 0);  // 纳入独立进程组，父进程超时/内存超限可整组终止
    dup2(input_fd, STDIN_FILENO);
    dup2(output_fd, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(input_fd);
    close(output_fd);
    close(devnull);

    if (DropPrivileges() != 0 && geteuid() == 0) {
      // 仅 root 下失败才告警（非 root 环境为预期内的 best-effort，测试可运行）。
      LogWarn("judge::RunOneCase: 降权到 " + std::string(kJudgeRunUser) +
              " 失败: " + std::string(std::strerror(errno)) + "，继续以当前用户运行");
    }
    if (chdir(work_dir.c_str()) != 0) {
      _exit(126);
    }

    // 资源限制：先 setrlimit 再 exec（spec 4.4：setrlimit 后 exec ./main）。
    struct rlimit rl;
    // RLIMIT_AS 硬上限（保护宿主机，预留余量；精确 MLE 判定由父进程 VmSize 轮询负责）。
    rl.rlim_cur = static_cast<rlim_t>(kRunHardMemoryCapKb) * 1024;
    rl.rlim_max = rl.rlim_cur;
    if (setrlimit(RLIMIT_AS, &rl) != 0) {
      LogWarn("judge::RunOneCase: setrlimit RLIMIT_AS 失败: " + std::string(std::strerror(errno)));
    }
    // RLIMIT_CPU 兜底：wall 限时 + 1s，防父进程被卡时子进程穷尽 CPU。
    rl.rlim_cur = static_cast<rlim_t>(time_limit_ms / 1000) + 1;
    rl.rlim_max = rl.rlim_cur;
    if (setrlimit(RLIMIT_CPU, &rl) != 0) {
      LogWarn("judge::RunOneCase: setrlimit RLIMIT_CPU 失败: " + std::string(std::strerror(errno)));
    }
    // RLIMIT_FSIZE：单用例输出文件硬上限（防止输出无限膨胀占满磁盘）。
    rl.rlim_cur = static_cast<rlim_t>(max_output_bytes);
    rl.rlim_max = rl.rlim_cur;
    if (setrlimit(RLIMIT_FSIZE, &rl) != 0) {
      LogWarn("judge::RunOneCase: setrlimit RLIMIT_FSIZE 失败: " + std::string(std::strerror(errno)));
    }

    execl(binary_path.c_str(), binary_path.c_str(), static_cast<char*>(nullptr));
    _exit(127);  // exec 失败（如权限/格式错误）
  }

  // ---- 父进程：等待 exec 完成 → 监控循环 + 资源统计 ----
  close(input_fd);
  close(output_fd);
  close(devnull);

  // 等待子进程完成 exec（comm 变为 "main"）。预置 5s 兜底防无限阻塞。
  // 期间若子进程提前退出（如 chdir 失败 → _exit(126)），comm 仍为服务进程名，
  // wait4 返回 pid → 按运行错误处理。
  const std::string expected_comm =
      std::filesystem::path(binary_path).filename().string();
  int status = 0;
  struct rusage usage {};
  bool exec_done = false;
  {
    const auto exec_begin = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - exec_begin <
           std::chrono::seconds(5)) {
      if (ProcessComm(pid) == expected_comm) {
        exec_done = true;
        break;
      }
      const pid_t r = wait4(pid, &status, WNOHANG, &usage);
      if (r == pid) {
        // exec 前即退出：降权失败/目录不可达等 → RE。
        LogWarn("judge::RunOneCase: 子进程在 exec 前退出（comm=" + ProcessComm(pid) + "）");
        result.status = CaseResult::Status::kRuntimeError;
        result.time_ms = 1;
        result.memory_kb = static_cast<long long>(usage.ru_maxrss);
        return result;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  if (!exec_done) {
    LogError("judge::RunOneCase: 等待子进程 exec 超时（5s）");
    kill(-pid, SIGKILL);
    wait4(pid, &status, 0, &usage);
    result.status = CaseResult::Status::kRuntimeError;
    result.time_ms = 1;
    result.memory_kb = static_cast<long long>(usage.ru_maxrss);
    return result;
  }

  const auto begin = std::chrono::steady_clock::now();
  const auto deadline = begin + std::chrono::milliseconds(time_limit_ms);
  bool killed = false;
  CaseResult::Status verdict = CaseResult::Status::kOk;
  pid_t done = 0;

  while (true) {
    done = wait4(pid, &status, WNOHANG, &usage);
    if (done == pid) {
      break;  // 子进程已结束（正常退出或已被 kill）
    }
    if (done < 0) {
      if (errno == EINTR) {
        continue;
      }
      LogError("judge::RunOneCase: wait4 失败: " + std::string(std::strerror(errno)));
      kill(-pid, SIGKILL);
      wait4(pid, &status, 0, &usage);
      verdict = CaseResult::Status::kRuntimeError;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      // wall time 超时 → 终止整个进程组 → TLE（SPEC 5：死循环 2s 强制 kill）。
      kill(-pid, SIGKILL);
      wait4(pid, &status, 0, &usage);
      killed = true;
      verdict = CaseResult::Status::kTimeLimit;
      break;
    }
    const long long vm = ReadVmSizeKb(pid);
    if (vm >= memory_limit_kb) {
      // 内存超限 → MLE（VmSize 越过上限，轮询周期 kMemoryWatchIntervalMs）。
      kill(-pid, SIGKILL);
      wait4(pid, &status, 0, &usage);
      killed = true;
      verdict = CaseResult::Status::kMemoryLimit;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kMemoryWatchIntervalMs));
  }

  result.time_ms = killed ? std::max<long long>(time_limit_ms, DurationMs(begin))
                          : DurationMs(begin);
  // ru_maxrss 在 Linux 下单位 KB（SPEC FR-3.4 内存峰值）。
  result.memory_kb = static_cast<long long>(usage.ru_maxrss);
  if (result.time_ms < 1) {
    result.time_ms = 1;
  }

  if (verdict != CaseResult::Status::kOk) {
    result.status = verdict;
    return result;
  }

  // 子进程已正常结束：判定退出码/信号。
  if (WIFSIGNALED(status)) {
    result.status = CaseResult::Status::kRuntimeError;  // 崩溃信号（除零、段错误等 → RE）
    LogDebug("judge::RunOneCase: 子进程被信号 " + std::to_string(WTERMSIG(status)) +
             " 终止");
    return result;
  }
  if (!WIFEXITED(status)) {
    result.status = CaseResult::Status::kRuntimeError;
    return result;
  }
  const int exit_code = WEXITSTATUS(status);
  if (exit_code != 0) {
    result.status = CaseResult::Status::kRuntimeError;  // 非零退出 → RE
    return result;
  }

  // 退出 0：比对 stdout 与 expected（容忍行尾空白差异）。
  const std::string output = ReadFile(output_path);
  if (output.size() > static_cast<std::size_t>(max_output_bytes)) {
    result.status = CaseResult::Status::kWrongAnswer;  // 输出过大 → WA
    return result;
  }
  if (TrimTrailingWhitespace(output) != TrimTrailingWhitespace(expected)) {
    result.status = CaseResult::Status::kWrongAnswer;
    return result;
  }
  result.status = CaseResult::Status::kOk;
  return result;
}

}  // namespace oj::judge
