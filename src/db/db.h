#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include <mysql/mysql.h>

#include "config.h"

namespace oj::db {

// MySQL 连接池配置（默认值来自 config.h，可整体覆盖后传入 DbInit / ConnectionPool）。
struct DbConfig {
  std::string host = kDbHost;
  unsigned int port = kDbPort;
  std::string user = kDbUser;
  std::string password = kDbPassword;
  std::string database = kDbName;          // 空串 = 不预选库
  unsigned int max_connections = kDbPoolSize;  // 池上限（含被占用中的连接）
  unsigned int acquire_timeout_ms = kDbAcquireTimeoutMs;  // 池满时等待上限
  unsigned int connect_timeout_ms = kDbConnectTimeoutMs;
  unsigned int read_timeout_ms = kDbReadTimeoutMs;
  unsigned int write_timeout_ms = kDbWriteTimeoutMs;
};

class ConnectionPool;

// 池化连接 RAII 句柄：持有期间独占该 MYSQL*，析构时自动归还连接池。
// Move-only（不能被拷贝，避免一个连接被多处同时使用）。
class PooledConnection {
 public:
  PooledConnection() = default;
  PooledConnection(ConnectionPool* pool, MYSQL* raw);
  ~PooledConnection();

  PooledConnection(const PooledConnection&) = delete;
  PooledConnection& operator=(const PooledConnection&) = delete;
  PooledConnection(PooledConnection&& other) noexcept;
  PooledConnection& operator=(PooledConnection&& other) noexcept;

  MYSQL* get() const { return raw_; }
  explicit operator bool() const { return raw_ != nullptr; }

 private:
  friend class ConnectionPool;
  void Release();

  ConnectionPool* pool_ = nullptr;
  MYSQL* raw_ = nullptr;
};

// 线程安全连接池（对应 SPEC 4.6 src/db/db.h：MySQL 连接管理 / 连接池）。
//
// 模型：HTTP 线程池中每个请求短暂持有 1 个连接（NFR-2 低并发）。
//   - Acquire()：优先复用空闲连接；不足且未达上限时新建；已达上限则阻塞等待，
//     等待超过 acquire_timeout_ms 返回空句柄（由调用方按 500/错误处理）。
//   - 归还：PooledConnection 析构自动归还并唤醒一个等待者。
//   - 健壮性：从池中取出的连接做 mysql_ping 存活检查，失效则关闭重建
//     （MySQL 长跑中可能断连/重启，NFR-6）。
class ConnectionPool {
 public:
  explicit ConnectionPool(DbConfig config);
  ~ConnectionPool();

  // 获取一个可用连接；失败（连不上 / 超时）返回空句柄。
  PooledConnection Acquire();

  // 池当前持有连接总数（含占用中）与空闲连接数，用于观察/日志。
  unsigned int total() const;
  unsigned int idle() const;

  const DbConfig& config() const { return config_; }

 private:
  friend class PooledConnection;
  MYSQL* Create();
  void Return(MYSQL* raw);
  bool PingOk(MYSQL* raw);

  const DbConfig config_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<MYSQL*> idle_;   // 空闲连接栈
  unsigned int total_ = 0;     // 已创建连接总数（含占用中）
};

// 全局连接池单例（进程启动时初始化一次，供 services 层复用）。
// DbInit 幂等：重复调用且已初始化时直接返回 true（忽略新配置）。
// 初始化时预建 1 个连接做可达性自检：MySQL 不可用则返回 false（配合启动自检，
// 对应 SPEC NFR-6 / A14「MySQL 不可用 → 报错」）。
bool DbInit(const DbConfig& config = DbConfig());

// 返回已初始化的全局连接池；未初始化或初始化失败返回 nullptr。
ConnectionPool* DbPool();

}  // namespace oj::db
