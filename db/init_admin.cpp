// =====================================================================
// 内置 admin 账号初始化工具（阶段 1，对应 SPEC 4.6 db/init_admin.cpp）
// ---------------------------------------------------------------------
// 作用：在 cpp_oj 库中创建（或重置）唯一内置管理员（FR-1.3，角色 admin
//       不可通过注册接口创建同名）。
// 密码以「随机盐 + sha256」哈希存储，方案与阶段 2 注册/登录共用
// （src/auth/password.*，注释里的全站约定）。
//
// 用法（先建库建表，再初始化管理员）:
//   mysql -uroot < db/schema.sql
//   ./build/oj_init_admin admin '你的密码'          # 指定用户名 + 密码
//   ./build/oj_init_admin '你的密码'                # 用户名默认 admin
//   ./build/oj_init_admin                          # 用户名/密码回退环境变量
//   OJ_ADMIN_USER=admin OJ_ADMIN_PASSWORD='xxx' ./build/oj_init_admin
//
// 幂等：重复执行会把该用户名密码重置为本次输入（也可用于忘记密码时重置）。
// 数据库连接参数取自 src/config.h，可用环境变量覆盖：
//   OJ_DB_HOST / OJ_DB_PORT / OJ_DB_USER / OJ_DB_PASSWORD
// =====================================================================

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <mysql/mysql.h>

#include "auth/password.h"
#include "config.h"
#include "db/db.h"
#include "logger.h"

namespace {

// 业务库名与 src/config.h kDbName 保持一致（建库/选库/检查均使用同一常量）。
constexpr const char* kDbName = oj::kDbName;

// 用户名仅允许字母/数字/下划线，长度 ≤ 64（与 users.username VARCHAR(64) 一致）。
bool IsValidUsername(const std::string& s) {
  if (s.empty() || s.size() > 64) {
    return false;
  }
  for (unsigned char c : s) {
    if (!(std::isalnum(c) || c == '_')) {
      return false;
    }
  }
  return true;
}

// 转义单条字符串，拼接进 SQL（本工具单条插入，配合 username 白名单校验即可）。
std::string Escape(MYSQL* mysql, const std::string& s) {
  std::string out(s.size() * 2 + 1, '\0');
  const unsigned long len =
      mysql_real_escape_string(mysql, &out[0], s.data(), s.size());
  out.resize(len);
  return out;
}

int Usage() {
  std::fprintf(stderr,
               "用法: oj_init_admin [用户名] [密码]\n"
               "  无参数    → 用户名/密码读环境变量 OJ_ADMIN_USER / OJ_ADMIN_PASSWORD（用户名默认 admin）\n"
               "  <密码>    → 用户名默认 admin\n"
               "  <用户名> <密码> → 指定用户名与密码\n"
               "  先执行: mysql -uroot < db/schema.sql\n");
  return 2;
}

int Run(const std::string& username, const std::string& password) {
  if (!IsValidUsername(username)) {
    oj::LogError("init_admin: 用户名非法（仅允许字母/数字/下划线，长度 1~64）：" + username);
    return 2;
  }
  if (password.empty()) {
    oj::LogError("init_admin: 密码不能为空");
    return 2;
  }

  // 生成随机盐 + 哈希（本模块定义全站密码存储方案）。
  const std::string salt = oj::auth::GenerateSalt();
  const std::string hash = oj::auth::HashPassword(password, salt);
  if (salt.empty() || hash.empty()) {
    oj::LogError("init_admin: 生成盐/哈希失败（随机源不可用）");
    return 1;
  }

  // 连接池：不预选库（保证建库前也能连接上），随后手动建库 + USE。
  // 默认连接参数取自 config.h，可用环境变量覆盖（config.h 注释：如需覆盖
  // 可改用环境变量/配置文件）：
  //   OJ_DB_HOST / OJ_DB_PORT / OJ_DB_USER / OJ_DB_PASSWORD
  oj::db::DbConfig cfg;
  cfg.database = "";
  if (const char* v = std::getenv("OJ_DB_HOST")) cfg.host = v;
  if (const char* v = std::getenv("OJ_DB_PORT")) cfg.port =
        static_cast<unsigned int>(std::atoi(v));
  if (const char* v = std::getenv("OJ_DB_USER")) cfg.user = v;
  if (const char* v = std::getenv("OJ_DB_PASSWORD")) cfg.password = v;
  oj::db::ConnectionPool pool(cfg);
  auto conn = pool.Acquire();
  if (!conn) {
    oj::LogError("init_admin: 无法连接 MySQL host=" + cfg.host +
             "（请确认 MySQL 已启动，或调整 src/config.h）");
    return 1;
  }
  MYSQL* mysql = conn.get();

  // 1) 确保数据库存在（幂等；与 schema.sql 的字符集/排序规则保持一致）。
  const char* kCreateDb =
      "CREATE DATABASE IF NOT EXISTS cpp_oj "
      "CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci";
  if (mysql_query(mysql, kCreateDb) != 0) {
    oj::LogError("init_admin: 创建数据库失败 err=" + std::string(mysql_error(mysql)));
    return 1;
  }
  if (mysql_select_db(mysql, kDbName) != 0) {
    oj::LogError("init_admin: 选择数据库 " + std::string(kDbName) +
             " 失败 err=" + std::string(mysql_error(mysql)));
    return 1;
  }

  // 2) 检查 users 表是否存在，引导先执行 schema.sql。
  const std::string check_table =
      "SELECT COUNT(*) FROM information_schema.tables "
      "WHERE table_schema = '" + std::string(kDbName) + "' AND table_name = 'users'";
  if (mysql_query(mysql, check_table.c_str()) != 0) {
    oj::LogError("init_admin: 检查 users 表失败 err=" + std::string(mysql_error(mysql)));
    return 1;
  }
  MYSQL_RES* res = mysql_store_result(mysql);
  if (res == nullptr) {
    oj::LogError("init_admin: 读取 users 表检查结果失败 err=" +
             std::string(mysql_error(mysql)));
    return 1;
  }
  MYSQL_ROW row = mysql_fetch_row(res);
  const bool table_exists = (row != nullptr && row[0] != nullptr && std::string(row[0]) == "1");
  mysql_free_result(res);
  if (!table_exists) {
    oj::LogError("init_admin: cpp_oj 库中不存在 users 表，请先执行: mysql -uroot < db/schema.sql");
    return 1;
  }

  // 3) UPSERT 管理员账号：用户名已存在则重置密码（幂等，重复执行 = 重置）。
  const std::string u = Escape(mysql, username);
  const std::string p = Escape(mysql, hash);
  const std::string s = Escape(mysql, salt);
  const std::string upsert =
      "INSERT INTO users (username, password, salt, role) "
      "VALUES ('" + u + "', '" + p + "', '" + s + "', 'admin') "
      "ON DUPLICATE KEY UPDATE password = '" + p + "', salt = '" + s + "'";
  if (mysql_query(mysql, upsert.c_str()) != 0) {
    oj::LogError("init_admin: 写入管理员账号失败 err=" + std::string(mysql_error(mysql)));
    return 1;
  }

  oj::LogInfo("init_admin: 管理员账号就绪 username=" + username +
          " role=admin（密码已加盐哈希落库）");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  // 参数形态：无参 / <username> <password> / <password>（兼容缺省用户名）。
  std::string username;
  std::string password;

  const char* env_user = std::getenv("OJ_ADMIN_USER");
  const char* env_pass = std::getenv("OJ_ADMIN_PASSWORD");

  if (argc == 1) {
    username = env_user != nullptr ? env_user : "admin";
    password = env_pass != nullptr ? env_pass : "";
  } else if (argc == 2) {
    username = "admin";
    password = argv[1];
  } else if (argc == 3) {
    username = argv[1];
    password = argv[2];
  } else {
    return Usage();
  }

  return Run(username, password);
}
