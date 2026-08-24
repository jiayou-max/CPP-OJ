// =====================================================================
// 数据库重置工具（构造接口/Web 自动化测试基线数据）
// ---------------------------------------------------------------------
// 作用：将 cpp_oj 库重置到自动化测试可用的干净基线状态：
//   1) 清理冗余内容：
//      - 清空 submissions（判题提交为累积测试数据，FR-5.1）
//      - 删除自动化测试残留用户（web_/judge_/curl_test_ 等前缀，
//        见 web自动化测试文档 §6.5/§7 与 接口自动化测试文档 §0）
//      - 删除自动化测试残留题目（webadmin% / <script>% 等标题，
//        级联删除其 testcases 与 submissions，FR-4.3）
//      - 保留内置管理员 admin 与真实业务用户（如 wuhu）
//   2) 构造接口自动化必要数据（与 web自动化测试文档 §2.1 /
//      接口自动化测试文档 §0 对齐）：
//      - 内置管理员 admin，默认密码 admin123（可覆盖）
//      - 题 5「两数之和」easy，2 组用例（判题 AC/WA/TLE/MLE/RE/CE 用题）
//      - 题 6「最长回文子串」medium，0 组用例（「判题不可用 500」场景）
//
// 用法（先建库建表，再重置）:
//   mysql -uroot < db/schema.sql
//   ./build/oj_db_reset                       # 管理员重置为 admin/admin123
//   ./build/oj_db_reset '新密码'              # 管理员 admin + 指定密码
//   ./build/oj_db_reset admin '新密码'        # 指定用户名与密码
//   OJ_ADMIN_USER=... OJ_ADMIN_PASSWORD=... ./build/oj_db_reset
//   ./build/oj_db_reset --dry-run             # 仅打印将执行的清理/写入，不实际执行
//
// 数据库连接参数取自 src/config.h，可用环境变量覆盖：
//   OJ_DB_HOST / OJ_DB_PORT / OJ_DB_USER / OJ_DB_PASSWORD
// 幂等：重复执行结果一致（删除目标已不存在，插入用 UPSERT / 先删后插）。
// =====================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <mysql/mysql.h>

#include "auth/password.h"
#include "config.h"
#include "db/db.h"
#include "logger.h"

namespace {

// 业务库名与 src/config.h kDbName 保持一致（建库/选库/检查均使用同一常量）。
constexpr const char* kDbName = oj::kDbName;

// ------------------------------------------------------------------
// 自动化测试必要数据（描述/用例文本与现库完全一致，保证重置后基线不变）
// ------------------------------------------------------------------

// 题 5「两数之和」描述（Markdown 原文，CRLF 行尾，与现库逐字节一致）。
constexpr const char* kProb5Desc =
    "## 题目\r\n\r\n"
    "给定一个整数数组 `nums` 和一个整数目标值 `target`，请你在该数组中找出 "
    "**和为目标值** 的那两个整数，并返回它们的数组下标。\r\n\r\n"
    "### 示例\r\n\r\n"
    "输入：`nums = [2,7,11,15], target = 9`\r\n\r\n"
    "输出：`[0,1]`";

// 题 6「最长回文子串」描述（0 组用例，用于「判题不可用 500」场景，SPEC 5）。
constexpr const char* kProb6Desc =
    "给你一个字符串 `s`，找到 `s` 中最长的回文子串。\n\n"
    "> 提示：可用中心扩展法。";

struct RequiredTestcase {
  int order;
  const char* input;
  const char* expected;
};

struct RequiredProblem {
  unsigned long long id;
  const char* title;
  const char* difficulty;
  const char* description;
  const RequiredTestcase* testcases;  // 为 nullptr 表示 0 组用例
  int testcase_count;
};

// 两数之和用例（输入格式 n / 数组 / target，输出 i j）。
const RequiredTestcase kProb5Testcases[] = {
    {1, "4\r\n2 7 11 15\r\n9", "0 1"},
    {2, "3\r\n3 2 4\r\n6", "1 2"},
};

// 接口自动化必需的题目集合（web自动化测试文档 §2.1 / 接口自动化测试文档 §0）。
const RequiredProblem kRequiredProblems[] = {
    {5, "两数之和", "easy", kProb5Desc, kProb5Testcases, 2},
    {6, "最长回文子串", "medium", kProb6Desc, nullptr, 0},
};

// 自动化测试残留用户前缀（web自动化测试文档 §6.5/§7 + 接口自动化测试文档 §0）。
// 这些用户名仅由自动化/人工测试产生，重置时删除；admin 与真实用户不受影响。
const char* const kTestUserPrefixes[] = {
    "web%",       // web_/webdup_/webuser_ 等注册类测试
    "judge%",     // judge_（判题测试用户，web自动化测试文档 §6.6）
    "login%",     // loginok_/loginbad_（登录类测试）
    "con_%",      // con_a_/con_b_（并发测试，web自动化测试文档 §6.9）
    "logout%",    // logout_/logout2_（登出测试）
    "redi%",      // redi_（已登录访问登录/注册页重定向测试）
    "mismatch%",  // mismatch_（两次密码不一致测试）
    "nounlink%",  // nounlink_（普通用户无管理入口测试）
    "nouser%",    // nouser_（防枚举-不存在用户测试）
    "curl_test%", // curl_test_（接口自动化测试文档 §3.1）
    "reg%",       // reg_/reg2_（接口自动化测试文档 §5 回归脚本）
    "submit%",    // submit_（接口自动化测试文档判题提交测试）
    "py_test%",   // Python 人工调试残留
    "dbg%",       // dbg_/dbg_ac_/dbg_fix_ 调试残留
    "flow%",      // flow_ 流程调试残留
    "fp%",        // fp_ 功能点调试残留
    "uat%",       // uat_ UAT 人工测试残留
};

// 自动化测试残留题目标题前缀（web自动化测试文档 §6.7/§6.8/§7）。
// 删除题目会级联删除其 testcases 与 submissions（CASCADE，FR-4.3）。
const char* const kTestProblemTitlePrefixes[] = {
    "webadmin%",       // 管理后台测试题（web自动化测试文档 §6.7）
    "<script>%",       // XSS 测试题（web自动化测试文档 §6.8，清理后自建自删）
    "zz_edit_target%", // 编辑功能调试残留
};

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

// 转义单条字符串，拼接进 SQL（本工具单条操作，配合白名单校验即可）。
std::string Escape(MYSQL* mysql, const std::string& s) {
  std::string out(s.size() * 2 + 1, '\0');
  const unsigned long len =
      mysql_real_escape_string(mysql, &out[0], s.data(), s.size());
  out.resize(len);
  return out;
}

// 执行一条 SQL；成功返回 true（可附带受影响行数）。失败返回 false 并填 err。
bool Exec(MYSQL* mysql, const std::string& sql, std::string* err,
          my_ulonglong* affected = nullptr) {
  if (mysql_query(mysql, sql.c_str()) != 0) {
    if (err != nullptr) {
      *err = std::string(mysql_error(mysql));
    }
    return false;
  }
  if (affected != nullptr) {
    *affected = mysql_affected_rows(mysql);
  }
  return true;
}

// 执行单列计数查询（如 SELECT COUNT(*)...）；失败返回 -1。
long long CountRows(MYSQL* mysql, const std::string& sql) {
  if (mysql_query(mysql, sql.c_str()) != 0) {
    return -1;
  }
  MYSQL_RES* res = mysql_store_result(mysql);
  if (res == nullptr) {
    return -1;
  }
  long long n = -1;
  if (MYSQL_ROW row = mysql_fetch_row(res); row != nullptr && row[0] != nullptr) {
    n = std::atoll(row[0]);
  }
  mysql_free_result(res);
  return n;
}

int Usage() {
  std::fprintf(stderr,
               "用法: oj_db_reset [--dry-run] [用户名 [密码]]\n"
               "  无参数           → 管理员重置为 admin/admin123\n"
               "  <密码>          → 管理员 admin + 指定密码\n"
               "  <用户名> <密码> → 指定管理员用户名与密码\n"
               "  --dry-run       → 仅打印将执行的重置操作，不实际执行\n"
               "  管理员用户名/密码也可用 OJ_ADMIN_USER / OJ_ADMIN_PASSWORD 环境变量覆盖\n"
               "  先执行: mysql -uroot < db/schema.sql\n"
               "  数据库连接参数用 OJ_DB_HOST / OJ_DB_PORT / OJ_DB_USER / OJ_DB_PASSWORD 覆盖\n");
  return 2;
}

// 检查四个业务表是否齐全；缺少任一表提示先执行 schema.sql。
bool CheckTablesExist(MYSQL* mysql) {
  const char* kTables[] = {"users", "problems", "testcases", "submissions"};
  for (const char* t : kTables) {
    const std::string sql =
        "SELECT COUNT(*) FROM information_schema.tables "
        "WHERE table_schema = '" + std::string(kDbName) + "' AND table_name = '" + t + "'";
    if (CountRows(mysql, sql) != 1) {
      oj::LogError("oj_db_reset: cpp_oj 库中不存在表 " + std::string(t) +
                   "，请先执行: mysql -uroot < db/schema.sql");
      return false;
    }
  }
  return true;
}

// 拼接「前缀 LIKE ... OR ...」的 WHERE 条件。
std::string BuildLikeOr(const char* const* prefixes, std::size_t count,
                        const char* column) {
  std::string out;
  for (std::size_t i = 0; i < count; ++i) {
    if (i > 0) {
      out += " OR ";
    }
    out += column;
    out += " LIKE '";
    out += prefixes[i];
    out += "'";
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  // ---- 参数解析：可选 --dry-run，以及可选的管理员用户名/密码 ----
  bool dry_run = false;
  std::string username;
  std::string password;
  const char* env_user = std::getenv("OJ_ADMIN_USER");
  const char* env_pass = std::getenv("OJ_ADMIN_PASSWORD");

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--dry-run") {
      dry_run = true;
    } else if (username.empty()) {
      username = arg;
    } else if (password.empty()) {
      password = arg;
    } else {
      return Usage();
    }
  }
  if (username.empty()) {
    username = env_user != nullptr ? env_user : "admin";
  }
  if (password.empty()) {
    password = env_pass != nullptr ? env_pass : "admin123";
  }

  if (!IsValidUsername(username)) {
    oj::LogError("oj_db_reset: 用户名非法（仅允许字母/数字/下划线，长度 1~64）：" + username);
    return 2;
  }
  if (password.empty()) {
    oj::LogError("oj_db_reset: 密码不能为空");
    return 2;
  }

  // 生成管理员随机盐 + 哈希（全站密码方案，src/auth/password.*）。
  const std::string admin_salt = oj::auth::GenerateSalt();
  const std::string admin_hash = oj::auth::HashPassword(password, admin_salt);
  if (admin_salt.empty() || admin_hash.empty()) {
    oj::LogError("oj_db_reset: 生成盐/哈希失败（随机源不可用）");
    return 1;
  }

  // ---- 连接 MySQL（不预选库，保证建库前也能连接上）----
  oj::db::DbConfig cfg;
  cfg.database = "";
  if (const char* v = std::getenv("OJ_DB_HOST")) cfg.host = v;
  if (const char* v = std::getenv("OJ_DB_PORT"))
    cfg.port = static_cast<unsigned int>(std::atoi(v));
  if (const char* v = std::getenv("OJ_DB_USER")) cfg.user = v;
  if (const char* v = std::getenv("OJ_DB_PASSWORD")) cfg.password = v;
  oj::db::ConnectionPool pool(cfg);
  auto conn = pool.Acquire();
  if (!conn) {
    oj::LogError("oj_db_reset: 无法连接 MySQL host=" + cfg.host +
                 "（请确认 MySQL 已启动，或调整 src/config.h）");
    return 1;
  }
  MYSQL* mysql = conn.get();

  // 确保数据库存在（与 schema.sql 字符集/排序规则一致）。
  const char* kCreateDb =
      "CREATE DATABASE IF NOT EXISTS cpp_oj "
      "CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci";
  std::string err;
  if (!Exec(mysql, kCreateDb, &err)) {
    oj::LogError("oj_db_reset: 创建数据库失败 err=" + err);
    return 1;
  }
  if (mysql_select_db(mysql, kDbName) != 0) {
    oj::LogError("oj_db_reset: 选择数据库 " + std::string(kDbName) +
                 " 失败 err=" + std::string(mysql_error(mysql)));
    return 1;
  }
  if (!CheckTablesExist(mysql)) {
    return 1;
  }

  const bool running = !dry_run;
  oj::LogInfo("oj_db_reset: " + std::string(dry_run ? "[dry-run] 预演重置 " : "重置 ") +
              "cpp_oj 库（管理员 " + username + "）");

  if (running) {
    if (!Exec(mysql, "START TRANSACTION", &err)) {
      oj::LogError("oj_db_reset: 开启事务失败 err=" + err);
      return 1;
    }
  }

  bool ok = true;

  // ---- 1) 清空判题提交（累积测试数据）----
  {
    const std::string sql = "DELETE FROM submissions";
    oj::LogInfo("oj_db_reset: 清理提交记录 -> " + sql);
    if (running) {
      my_ulonglong affected = 0;
      if (!Exec(mysql, sql, &err, &affected)) {
        oj::LogError("oj_db_reset: 清理 submissions 失败 err=" + err);
        ok = false;
      } else {
        oj::LogInfo("oj_db_reset:   已删除提交记录 " + std::to_string(affected) + " 条");
      }
    }
  }

  // ---- 2) 删除自动化测试残留用户（先删提交，避开 user FK RESTRICT）----
  if (ok) {
    const std::string cond = BuildLikeOr(kTestUserPrefixes,
        sizeof(kTestUserPrefixes) / sizeof(kTestUserPrefixes[0]), "username");
    const std::string sql = "DELETE FROM users WHERE " + cond;
    oj::LogInfo("oj_db_reset: 清理测试残留用户 -> " + sql);
    if (running) {
      my_ulonglong affected = 0;
      if (!Exec(mysql, sql, &err, &affected)) {
        oj::LogError("oj_db_reset: 清理测试残留用户失败 err=" + err);
        ok = false;
      } else {
        oj::LogInfo("oj_db_reset:   已删除测试残留用户 " + std::to_string(affected) + " 条");
      }
    }
  }

  // ---- 3) 删除自动化测试残留题目（级联删用例与提交）----
  if (ok) {
    const std::string cond = BuildLikeOr(kTestProblemTitlePrefixes,
        sizeof(kTestProblemTitlePrefixes) / sizeof(kTestProblemTitlePrefixes[0]), "title");
    const std::string sql = "DELETE FROM problems WHERE " + cond;
    oj::LogInfo("oj_db_reset: 清理测试残留题目 -> " + sql);
    if (running) {
      my_ulonglong affected = 0;
      if (!Exec(mysql, sql, &err, &affected)) {
        oj::LogError("oj_db_reset: 清理测试残留题目失败 err=" + err);
        ok = false;
      } else {
        oj::LogInfo("oj_db_reset:   已删除测试残留题目 " + std::to_string(affected) +
                    " 条（级联删除其用例与提交）");
      }
    }
  }

  // ---- 4) 重置内置管理员（UPSERT，重复执行 = 重置密码，FR-1.3）----
  if (ok) {
    const std::string u = Escape(mysql, username);
    const std::string p = Escape(mysql, admin_hash);
    const std::string s = Escape(mysql, admin_salt);
    const std::string sql =
        "INSERT INTO users (username, password, salt, role) "
        "VALUES ('" + u + "', '" + p + "', '" + s + "', 'admin') "
        "ON DUPLICATE KEY UPDATE password = '" + p + "', salt = '" + s + "', role = 'admin'";
    oj::LogInfo("oj_db_reset: 重置内置管理员 username=" + username +
                " role=admin（密码已加盐哈希落库）");
    if (running) {
      if (!Exec(mysql, sql, &err)) {
        oj::LogError("oj_db_reset: 写入管理员账号失败 err=" + err);
        ok = false;
      }
    }
  }

  // ---- 5) 构造接口自动化必需题目（UPSERT + 先删后插用例）----
  for (const RequiredProblem& prob : kRequiredProblems) {
    if (!ok) {
      break;
    }
    const std::string title = Escape(mysql, prob.title);
    const std::string desc = Escape(mysql, prob.description);
    const std::string diff = Escape(mysql, prob.difficulty);
    const std::string upsert =
        "INSERT INTO problems (id, title, description, difficulty) "
        "VALUES (" + std::to_string(prob.id) + ", '" + title + "', '" + desc + "', '" + diff + "') "
        "ON DUPLICATE KEY UPDATE title = '" + title + "', description = '" + desc +
        "', difficulty = '" + diff + "'";
    oj::LogInfo("oj_db_reset: 构造题目 id=" + std::to_string(prob.id) + " 「" + prob.title +
                "」难度 " + prob.difficulty);
    if (running && !Exec(mysql, upsert, &err)) {
      oj::LogError("oj_db_reset: 写入题目 id=" + std::to_string(prob.id) + " 失败 err=" + err);
      ok = false;
      break;
    }

    // 用例整体替换（编辑语义为整体替换，FR-4.2）：先删该题全部用例，再按需插入。
    const std::string del_tc =
        "DELETE FROM testcases WHERE problem_id = " + std::to_string(prob.id);
    oj::LogInfo("oj_db_reset:   替换用例 -> " + del_tc);
    if (running && !Exec(mysql, del_tc, &err)) {
      oj::LogError("oj_db_reset: 清理题目 id=" + std::to_string(prob.id) + " 用例失败 err=" + err);
      ok = false;
      break;
    }
    for (int i = 0; i < prob.testcase_count; ++i) {
      const RequiredTestcase& tc = prob.testcases[i];
      const std::string in = Escape(mysql, tc.input);
      const std::string ex = Escape(mysql, tc.expected);
      const std::string ins =
          "INSERT INTO testcases (problem_id, `order`, input, expected) "
          "VALUES (" + std::to_string(prob.id) + ", " + std::to_string(tc.order) +
          ", '" + in + "', '" + ex + "')";
      oj::LogInfo("oj_db_reset:   插入用例 order=" + std::to_string(tc.order));
      if (running && !Exec(mysql, ins, &err)) {
        oj::LogError("oj_db_reset: 插入用例失败 err=" + err);
        ok = false;
        break;
      }
    }
    if (!running || ok) {
      oj::LogInfo("oj_db_reset:   该题用例数 = " + std::to_string(prob.testcase_count));
    }
  }

  if (running) {
    if (ok) {
      if (!Exec(mysql, "COMMIT", &err)) {
        oj::LogError("oj_db_reset: 提交事务失败 err=" + err);
        ok = false;
      }
    } else {
      Exec(mysql, "ROLLBACK", &err);
      oj::LogError("oj_db_reset: 重置失败，已回滚（数据未改动）");
    }
  }
  if (!ok) {
    return 1;
  }

  // ---- 6) 重置结果汇总 ----
  if (running) {
    const long long users = CountRows(mysql, "SELECT COUNT(*) FROM users");
    const long long problems = CountRows(mysql, "SELECT COUNT(*) FROM problems");
    const long long submissions = CountRows(mysql, "SELECT COUNT(*) FROM submissions");
    const long long testcases = CountRows(mysql, "SELECT COUNT(*) FROM testcases");
    oj::LogInfo("oj_db_reset: 重置完成，当前状态：users=" + std::to_string(users) +
                " problems=" + std::to_string(problems) +
                " testcases=" + std::to_string(testcases) +
                " submissions=" + std::to_string(submissions));
    for (const RequiredProblem& prob : kRequiredProblems) {
      const long long tc = CountRows(
          mysql, "SELECT COUNT(*) FROM testcases WHERE problem_id = " + std::to_string(prob.id));
      oj::LogInfo("oj_db_reset:   题目 " + std::to_string(prob.id) + "「" + prob.title +
                  "」用例数 = " + std::to_string(tc) +
                  (prob.testcase_count == 0 ? "（无用例，用于「判题不可用 500」）" : ""));
    }
    oj::LogInfo("oj_db_reset: 完成。管理员 " + username + " 密码已重置；判题用题 5 / 题 6 就绪");
  } else {
    oj::LogInfo("oj_db_reset: [dry-run] 预演完成，未执行任何写入");
  }
  return 0;
}
