#pragma once

#include <string>

#include "config.h"

namespace oj::judge {

// 单用例运行结果（对应 SPEC 4.4「运行封装」与 FR-3.4 逐用例状态 + 资源统计）。
struct CaseResult {
  enum class Status {
    kOk,           // 退出 0 且 stdout 与 expected 一致（该用例通过）
    kWrongAnswer,  // 退出 0 但 stdout 与 expected 不一致
    kTimeLimit,    // wall 超时（> time_limit_ms → 父进程 kill）
    kMemoryLimit,  // 内存超限（VmSize 轮询越过 memory_limit_kb → 父进程 kill）
    kRuntimeError, // 非零退出码 / 崩溃信号（除零、非法内存等）
  };

  Status status = Status::kOk;
  int case_index = 0;     // 用例序号（1-based，展示用）
  long long time_ms = 0;  // 本用例 wall time（毫秒）
  long long memory_kb = 0; // 本用例内存峰值（getrusage ru_maxrss，Linux 单位 KB）
};

// 运行封装：fork → setpgid → setuid(nobody) → chdir(临时目录) → setrlimit
// → exec，父进程轮询 wait4（超时 kill / 内存监控 / getrusage 逐用例统计）。
//
// 隔离与限制（NFR-4 / FR-3.6 / D8）：
//   - 低权限用户运行（setuid 需 root 启动；非 root 时 best-effort 并告警，测试可运行）
//   - RLIMIT_AS 硬上限 = kRunHardMemoryCapKb（保护宿主机，防单次超大分配）
//   - wall time 超时 → SIGKILL 整个进程组 → TLE（SPEC 5「死循环代码 2s 强制 kill」）
//   - /proc/<pid>/status VmSize 轮询 ≥ memory_limit_kb → SIGKILL → MLE
//   - RLIMIT_CPU 兜底（防父进程被卡时子进程穷尽 CPU）
//
// 输入/输出：用例 input 写入临时文件重定向为 stdin；stdout 写入输出文件供比对；
// stderr 丢弃（RE 详情以退出码/信号为准）。文件 fd 由父进程预先打开，
// 子进程 setuid 降权前 dup2 复用，避免权限问题。
//
// 就绪判定：父进程等待子进程 /proc/<pid>/stat 的 comm 变为判题可执行文件名后，
// 才开始计时与内存监控，排除 fork→exec 期间子进程继承父进程（可能很大）地址空间
// 造成的假 MLE/假超时。
CaseResult RunOneCase(const std::string& binary_path,
                      const std::string& work_dir,
                      const std::string& input,
                      const std::string& expected,
                      long long time_limit_ms = kRunTimeLimitMs,
                      long long memory_limit_kb = kMemoryLimitKb,
                      long long max_output_bytes = kMaxOutputBytes);

}  // namespace oj::judge
