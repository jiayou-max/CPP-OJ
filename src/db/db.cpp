#include "db/db.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>

#include "logger.h"

namespace oj::db {

// ------------------------------------------------------------------
// PooledConnection：RAII 归还
// ------------------------------------------------------------------

PooledConnection::PooledConnection(ConnectionPool* pool, MYSQL* raw)
    : pool_(pool), raw_(raw) {}

PooledConnection::~PooledConnection() { Release(); }

PooledConnection::PooledConnection(PooledConnection&& other) noexcept
    : pool_(other.pool_), raw_(other.raw_) {
  other.pool_ = nullptr;
  other.raw_ = nullptr;
}

PooledConnection& PooledConnection::operator=(PooledConnection&& other) noexcept {
  if (this != &other) {
    Release();
    pool_ = other.pool_;
    raw_ = other.raw_;
    other.pool_ = nullptr;
    other.raw_ = nullptr;
  }
  return *this;
}

void PooledConnection::Release() {
  if (raw_ != nullptr) {
    pool_->Return(raw_);
    raw_ = nullptr;
    pool_ = nullptr;
  }
}

// ------------------------------------------------------------------
// ConnectionPool
// ------------------------------------------------------------------

ConnectionPool::ConnectionPool(DbConfig config) : config_(config) {}

ConnectionPool::~ConnectionPool() {
  // 仅在空闲连接在池中时关闭它们；被占用的连接由 PooledConnection 析构时归还，
  // 此时池已销毁属未定义行为 —— 因此池的生命周期必须覆盖所有使用者
  // （本项目以进程级单例使用，析构只在进程退出时触发）。
  std::lock_guard<std::mutex> lock(mutex_);
  for (MYSQL* raw : idle_) {
    mysql_close(raw);
  }
  idle_.clear();
  total_ = 0;
}

MYSQL* ConnectionPool::Create() {
  MYSQL* raw = mysql_init(nullptr);
  if (raw == nullptr) {
    LogError("db: mysql_init failed (内存不足)");
    return nullptr;
  }

  // 网络超时：连接/读写均设上限，避免慢网络卡死请求线程。
  const unsigned int connect_timeout = config_.connect_timeout_ms;
  const unsigned int read_timeout = config_.read_timeout_ms;
  const unsigned int write_timeout = config_.write_timeout_ms;
  mysql_options(raw, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
  mysql_options(raw, MYSQL_OPT_READ_TIMEOUT, &read_timeout);
  mysql_options(raw, MYSQL_OPT_WRITE_TIMEOUT, &write_timeout);

  MYSQL* result = mysql_real_connect(
      raw, config_.host.c_str(), config_.user.c_str(), config_.password.c_str(),
      config_.database.empty() ? nullptr : config_.database.c_str(),
      config_.port, nullptr, 0);

  if (result == nullptr) {
    LogError("db: 连接 MySQL 失败 host=" + config_.host + " user=" +
             config_.user + " err=" + mysql_error(raw));
    mysql_close(raw);
    return nullptr;
  }

  // 全库统一 utf8mb4（SPEC 4.3），与 schema 保持一致。
  if (mysql_set_character_set(raw, "utf8mb4") != 0) {
    LogWarn("db: 设置 utf8mb4 失败 err=" + std::string(mysql_error(raw)));
  }
  return raw;
}

// mysql_ping 检查连接是否仍可用；MySQL 8 已移除自动重连（MYSQL_OPT_RECONNECT），
// 失效连接直接关闭，由调用方重建。返回 0 表示存活。
bool ConnectionPool::PingOk(MYSQL* raw) { return mysql_ping(raw) == 0; }

PooledConnection ConnectionPool::Acquire() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    // 池已满：阻塞等待空闲连接归还，超时返回空句柄。
    while (idle_.empty() && total_ >= config_.max_connections) {
      if (cv_.wait_for(lock,
                       std::chrono::milliseconds(config_.acquire_timeout_ms)) ==
          std::cv_status::timeout) {
        LogError("db: 获取连接超时（池满 " + std::to_string(total_) +
                 "/" + std::to_string(config_.max_connections) + "）");
        return PooledConnection();
      }
    }

    MYSQL* raw = nullptr;
    if (!idle_.empty()) {
      raw = idle_.back();
      idle_.pop_back();
    } else {
      raw = Create();
      if (raw == nullptr) {
        return PooledConnection();  // 连不上 MySQL（已打 ERROR 日志）
      }
      ++total_;
    }

    // 复用连接前做存活检查；失效则关闭并继续取下一个（可能触发重建）。
    if (PingOk(raw)) {
      return PooledConnection(this, raw);
    }
    LogWarn("db: 复用连接已失效，关闭重建");
    mysql_close(raw);
    --total_;
  }
}

void ConnectionPool::Return(MYSQL* raw) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    idle_.push_back(raw);
  }
  cv_.notify_one();
}

unsigned int ConnectionPool::total() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_;
}

unsigned int ConnectionPool::idle() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<unsigned int>(idle_.size());
}

// ------------------------------------------------------------------
// 全局单例
// ------------------------------------------------------------------

namespace {

// 全局池指针（进程生命周期内只分配不释放，见 ConnectionPool 析构说明）。
std::mutex g_pool_mutex;
ConnectionPool* g_pool = nullptr;

}  // namespace

DbConfig DbConfigFromEnv() {
  DbConfig cfg;
  if (const char* v = std::getenv("OJ_DB_HOST")) cfg.host = v;
  if (const char* v = std::getenv("OJ_DB_PORT"))
    cfg.port = static_cast<unsigned int>(std::atoi(v));
  if (const char* v = std::getenv("OJ_DB_USER")) cfg.user = v;
  if (const char* v = std::getenv("OJ_DB_PASSWORD")) cfg.password = v;
  return cfg;
}

bool DbInit(const DbConfig& config) {
  std::lock_guard<std::mutex> lock(g_pool_mutex);
  if (g_pool != nullptr) {
    LogInfo("db: 连接池已初始化，忽略重复 DbInit");
    return true;
  }

  auto* pool = new ConnectionPool(config);

  // 预建 1 个连接做可达性自检：MySQL 不可用 → 启动即失败，避免运行期才暴露。
  {
    PooledConnection probe = pool->Acquire();
    if (!probe) {
      LogError("db: 初始化自检失败（MySQL 不可达），启动中止");
      delete pool;
      return false;
    }
    // probe 析构时自动归还，作为池中第一个空闲连接。
  }

  g_pool = pool;
  LogInfo("db: 连接池初始化完成 host=" + config.host + " pool_size=" +
          std::to_string(config.max_connections));
  return true;
}

ConnectionPool* DbPool() {
  std::lock_guard<std::mutex> lock(g_pool_mutex);
  return g_pool;
}

}  // namespace oj::db
