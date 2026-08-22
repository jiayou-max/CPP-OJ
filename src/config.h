#pragma once

namespace oj {

// 全局配置（阶段 0 起逐步充实，对应 SPEC 4.6 src/config.h）
constexpr int kServerPort = 8080;

// 错误日志文件路径（相对进程工作目录，NFR-6：关键错误文件日志）
constexpr const char* kErrorLogPath = "oj_error.log";

// ---- MySQL 连接参数（阶段 0 连接池，对应 SPEC A.1 依赖清单）----
// 本机默认 root 免密部署；如需覆盖可改用环境变量/配置文件，见 docs/deployment.md。
constexpr const char* kDbHost = "127.0.0.1";     // TCP 直连（localhost 走 /var/run/mysqld 的 3306 socket，会连错实例）
constexpr unsigned int kDbPort = 3307;           // 本机 3306 root 需密码；实际使用实例为 3307
constexpr const char* kDbUser = "root";
constexpr const char* kDbPassword = "";          // 空 = 免密
constexpr const char* kDbName = "cpp_oj";        // 业务库，由 db/schema.sql 创建（阶段 1）
constexpr unsigned int kDbPoolSize = 8;          // 连接池上限（含占用中，NFR-2 低并发足够）
constexpr unsigned int kDbAcquireTimeoutMs = 3000;  // 池满时等待空闲连接的上限
constexpr unsigned int kDbConnectTimeoutMs = 3000;
constexpr unsigned int kDbReadTimeoutMs = 5000;
constexpr unsigned int kDbWriteTimeoutMs = 5000;

// ---- 判题内核（阶段 3：编译器封装，对应 SPEC 4.4 / D8）----
constexpr const char* kCompilerBinary = "g++";      // 编译器可执行名（execlp 按 PATH 查找）
constexpr const char* kCompilerFlags = "-O2";       // g++ 编译选项（SPEC 4.4: g++ -O2 -o main）
constexpr const char* kJudgeTempDirTemplate = "/tmp/oj_judge_XXXXXX";  // 随机临时目录模板（mkdtemp）
constexpr unsigned int kCompileTimeoutSec = 10;     // 编译超时兜底（SPEC 5 / D8：>10s → CE 标注编译超时）

// ---- 判题运行（阶段 3：runner 运行封装，对应 SPEC 4.4 / FR-3.6 / D8）----
constexpr const char* kJudgeRunUser = "nobody";     // 降权运行用户（NFR-4，UID 65534；getpwnam 解析，缺省回退 65534）
constexpr long long kRunTimeLimitMs = 2000;         // 单用例 wall time 上限（FR-3.6 / D8：>2s → TLE）
constexpr long long kMemoryLimitKb = 256 * 1024;    // 单用例内存上限（FR-3.6 / D8：RLIMIT_AS 语义 → MLE）
constexpr long long kMemoryWatchIntervalMs = 5;     // 父进程内存监控轮询间隔（/proc/<pid>/status VmSize）
constexpr long long kRunHardMemoryCapKb = 512 * 1024; // RLIMIT_AS 硬上限：预留余量保护宿主机，防单次超大分配触发 OOM
constexpr long long kMaxOutputBytes = 8 * 1024 * 1024; // 单用例 stdout 上限（超出按 WA 计，防输出无限膨胀拖垮比对）
constexpr unsigned int kJudgeMaxConcurrent = 2;     // 全局同时判题数上限（信号量限流，SPEC 4.4 示例 N=2 / FR-3.5）

// ---- 会话 / Cookie（阶段 4：Session/Cookie 中间件，FR-1.2 / NFR-3 / D13）----
constexpr const char* kSessionCookieName = "oj_session";  // 登录会话 Cookie 名（全站统一）
constexpr int kSessionLifetimeHours = 24 * 7;             // 会话有效期（滑动续期，默认 7 天）

}  // namespace oj
