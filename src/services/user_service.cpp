#include "services/user_service.h"

#include <cctype>
#include <cstddef>
#include <string>

#include "auth/password.h"
#include "db/db.h"
#include "db/db_query.h"
#include "logger.h"
#include "models/user.h"

namespace oj::services {

namespace {

// 与 users.username VARCHAR(64) 及 db/init_admin.cpp 白名单保持一致。
constexpr std::size_t kMaxUsernameLen = 64;
constexpr std::size_t kMinPasswordLen = 6;
constexpr std::size_t kMaxPasswordLen = 64;

// 用户名是否已存在（注册去重；插入失败时用于唯一键冲突兜底）。
// 数据库不可用返回 false（调用方按各自的错误路径处理）。
bool UsernameExists(const std::string& username) {
  oj::db::ConnectionPool* pool = oj::db::DbPool();
  if (pool == nullptr) {
    return false;
  }
  auto conn = pool->Acquire();
  if (!conn) {
    return false;
  }

  oj::db::PreparedStatement stmt(std::move(conn),
      "SELECT id FROM users WHERE username=?");
  if (!stmt.PrepareOk()) {
    LogError("user_service: 预编译失败: " + stmt.Error());
    return false;
  }
  stmt.Bind(username);
  if (!stmt.Execute()) {
    LogError("user_service: 查询用户名失败 err=" + stmt.Error());
    return false;
  }
  return stmt.Fetch();
}

}  // namespace

bool IsValidUsername(const std::string& username) {
  if (username.empty() || username.size() > kMaxUsernameLen) {
    return false;
  }
  for (unsigned char c : username) {
    if (!(std::isalnum(c) || c == '_')) {
      return false;
    }
  }
  return true;
}

bool IsValidPassword(const std::string& password) {
  return password.size() >= kMinPasswordLen &&
         password.size() <= kMaxPasswordLen;
}

UserStatus RegisterUser(const std::string& username, const std::string& password,
                        oj::models::User& out) {
  if (!IsValidUsername(username) || !IsValidPassword(password)) {
    return UserStatus::kInvalidInput;
  }

  oj::db::ConnectionPool* pool = oj::db::DbPool();
  if (pool == nullptr) {
    LogError("user_service: 数据库连接池未初始化");
    return UserStatus::kDbError;
  }
  auto conn = pool->Acquire();
  if (!conn) {
    LogError("user_service: 获取数据库连接失败");
    return UserStatus::kDbError;
  }

  // 重复用户名：先查后插；并发注册同名时由 MySQL 唯一键兜底（见下方失败分支）。
  if (UsernameExists(username)) {
    return UserStatus::kDuplicateName;
  }

  const std::string salt = oj::auth::GenerateSalt();
  const std::string hash = oj::auth::HashPassword(password, salt);
  if (salt.empty() || hash.empty()) {
    LogError("user_service: 生成盐/哈希失败（随机源不可用）");
    return UserStatus::kDbError;
  }

  oj::db::PreparedStatement stmt(std::move(conn),
      "INSERT INTO users (username, password, salt, role) VALUES (?,?,?,'user')");
  if (!stmt.PrepareOk()) {
    LogError("user_service: 预编译失败: " + stmt.Error());
    return UserStatus::kDbError;
  }
  stmt.Bind(username);
  stmt.Bind(hash);
  stmt.Bind(salt);
  if (!stmt.Execute()) {
    LogError("user_service: 注册插入失败 username=" + username + " err=" + stmt.Error());
    // 并发注册同名：唯一键冲突。重查确认，避免误报数据库错误。
    if (UsernameExists(username)) {
      return UserStatus::kDuplicateName;
    }
    return UserStatus::kDbError;
  }

  const long long id = static_cast<long long>(stmt.InsertId());
  if (id <= 0) {
    LogError("user_service: 注册后无法取得自增 id username=" + username);
    return UserStatus::kDbError;
  }

  out.id = id;
  out.username = username;
  out.role = "user";
  return UserStatus::kOk;
}

UserStatus LoginUser(const std::string& username, const std::string& password,
                     oj::models::User& out) {
  if (!IsValidUsername(username) || password.empty()) {
    return UserStatus::kBadCredentials;
  }

  oj::db::ConnectionPool* pool = oj::db::DbPool();
  if (pool == nullptr) {
    LogError("user_service: 数据库连接池未初始化");
    return UserStatus::kDbError;
  }
  auto conn = pool->Acquire();
  if (!conn) {
    LogError("user_service: 获取数据库连接失败");
    return UserStatus::kDbError;
  }

  // SELECT 顺序与下列 GetString 下标对应：0 id, 1 username, 2 password, 3 salt, 4 role。
  oj::db::PreparedStatement stmt(std::move(conn),
      "SELECT id, username, password, salt, role FROM users WHERE username=?");
  if (!stmt.PrepareOk()) {
    LogError("user_service: 预编译失败: " + stmt.Error());
    return UserStatus::kDbError;
  }
  stmt.Bind(username);
  if (!stmt.Execute()) {
    LogError("user_service: 查询用户失败 username=" + username + " err=" + stmt.Error());
    return UserStatus::kDbError;
  }
  if (!stmt.Fetch()) {
    return UserStatus::kBadCredentials;  // 用户名不存在
  }

  // 重算 sha256(salt + password) 与库中哈希比对（SPEC 4.3.1 密码存储约定）。
  if (!oj::auth::VerifyPassword(password, stmt.GetString(3), stmt.GetString(2))) {
    return UserStatus::kBadCredentials;
  }

  out.id = std::stoll(stmt.GetString(0));
  out.username = stmt.GetString(1);
  out.role = stmt.GetString(4);
  return UserStatus::kOk;
}

}  // namespace oj::services
