#include "services/problem_service.h"

#include <cstddef>
#include <string>

#include "db/db.h"
#include "db/db_query.h"
#include "logger.h"
#include "models/problem.h"
#include "models/testcase.h"

namespace oj::services {

namespace {

// 从当前结果行解析一行 Problem（列顺序须与 SQL 保持一致）。
oj::models::Problem ParseProblemRow(const oj::db::PreparedStatement& stmt) {
  oj::models::Problem p;
  p.id = std::stoll(stmt.GetString(0));
  p.title = stmt.GetString(1);
  p.description = stmt.GetString(2);
  p.difficulty = oj::models::DifficultyFromName(stmt.GetString(3));
  return p;
}

// 从当前结果行解析一行 TestCase（列顺序须与 SQL 保持一致）。
oj::models::TestCase ParseTestCaseRow(const oj::db::PreparedStatement& stmt) {
  oj::models::TestCase tc;
  tc.id = std::stoll(stmt.GetString(0));
  tc.problem_id = std::stoll(stmt.GetString(1));
  tc.order = std::stoi(stmt.GetString(2));
  tc.input = stmt.GetString(3);
  tc.expected = stmt.GetString(4);
  return tc;
}

}  // namespace

ProblemQueryStatus GetProblemById(long long id, oj::models::Problem& out) {
  oj::db::ConnectionPool* pool = oj::db::DbPool();
  if (pool == nullptr) {
    LogError("problem_service: 数据库连接池未初始化");
    return ProblemQueryStatus::kDbError;
  }

  auto conn = pool->Acquire();
  if (!conn) {
    LogError("problem_service: 获取数据库连接失败");
    return ProblemQueryStatus::kDbError;
  }

  oj::db::PreparedStatement stmt(
      std::move(conn),
      "SELECT id, title, description, difficulty FROM problems WHERE id=?");
  if (!stmt.PrepareOk()) {
    LogError("problem_service: 预编译失败: " + stmt.Error());
    return ProblemQueryStatus::kDbError;
  }

  stmt.Bind(id);
  if (!stmt.Execute()) {
    LogError("problem_service: 查询题目失败 id=" + std::to_string(id) +
             " err=" + stmt.Error());
    return ProblemQueryStatus::kDbError;
  }

  if (!stmt.Fetch()) {
    return ProblemQueryStatus::kNotFound;
  }

  out = ParseProblemRow(stmt);
  return ProblemQueryStatus::kOk;
}

bool ListProblems(std::vector<oj::models::Problem>& out) {
  out.clear();

  oj::db::ConnectionPool* pool = oj::db::DbPool();
  if (pool == nullptr) {
    LogError("problem_service: 数据库连接池未初始化");
    return false;
  }

  auto conn = pool->Acquire();
  if (!conn) {
    LogError("problem_service: 获取数据库连接失败");
    return false;
  }

  oj::db::PreparedStatement stmt(
      std::move(conn),
      "SELECT id, title, description, difficulty FROM problems ORDER BY id");
  if (!stmt.PrepareOk()) {
    LogError("problem_service: 预编译失败: " + stmt.Error());
    return false;
  }

  if (!stmt.Execute()) {
    LogError("problem_service: 查询题目列表失败 err=" + stmt.Error());
    return false;
  }

  while (stmt.Fetch()) {
    out.push_back(ParseProblemRow(stmt));
  }
  return true;
}

bool ListTestcasesByProblem(long long problem_id,
                            std::vector<oj::models::TestCase>& out) {
  out.clear();

  oj::db::ConnectionPool* pool = oj::db::DbPool();
  if (pool == nullptr) {
    LogError("problem_service: 数据库连接池未初始化");
    return false;
  }

  auto conn = pool->Acquire();
  if (!conn) {
    LogError("problem_service: 获取数据库连接失败");
    return false;
  }

  // `order` 为 MySQL 保留字，查询时必须反引号（SPEC 4.3.1 注意事项）。
  oj::db::PreparedStatement stmt(
      std::move(conn),
      "SELECT id, problem_id, `order`, input, expected FROM testcases "
      "WHERE problem_id=? ORDER BY `order`");
  if (!stmt.PrepareOk()) {
    LogError("problem_service: 预编译失败: " + stmt.Error());
    return false;
  }

  stmt.Bind(problem_id);
  if (!stmt.Execute()) {
    LogError("problem_service: 查询用例失败 problem_id=" + std::to_string(problem_id) +
             " err=" + stmt.Error());
    return false;
  }

  while (stmt.Fetch()) {
    out.push_back(ParseTestCaseRow(stmt));
  }
  return true;
}

namespace {

// 返回 true 时回滚当前事务并返回 kDbError（统一失败路径，已打日志）。
bool Rollback(MYSQL* raw, const std::string& stage) {
  mysql_query(raw, "ROLLBACK");
  LogError("problem_service: 创建题目失败 @ " + stage);
  return false;
}

// 用例校验：至少一组，且每组 input/expected 均非空（FR-4.2 表单校验）。
bool ValidTestcases(const std::vector<oj::models::TestCase>& testcases) {
  if (testcases.empty()) {
    return false;
  }
  for (const auto& tc : testcases) {
    if (tc.input.empty() || tc.expected.empty()) {
      return false;
    }
  }
  return true;
}

}  // namespace

CreateProblemStatus CreateProblem(
    const std::string& title, const std::string& description,
    oj::models::Difficulty difficulty,
    const std::vector<oj::models::TestCase>& testcases, long long& out_id) {
  // 基础校验（表单错误提示路径，不触发事务）。
  if (title.empty() || title.size() > 255 || description.empty() ||
      !ValidTestcases(testcases)) {
    return CreateProblemStatus::kInvalidInput;
  }

  oj::db::ConnectionPool* pool = oj::db::DbPool();
  if (pool == nullptr) {
    LogError("problem_service: 数据库连接池未初始化");
    return CreateProblemStatus::kDbError;
  }
  auto conn = pool->Acquire();
  if (!conn) {
    LogError("problem_service: 获取数据库连接失败");
    return CreateProblemStatus::kDbError;
  }

  // 事务：题目 + 用例全部成功才 COMMIT，否则 ROLLBACK（SPEC 阶段 5）。
  if (mysql_query(conn.get(), "START TRANSACTION") != 0) {
    LogError("problem_service: START TRANSACTION 失败: " + std::string(mysql_error(conn.get())));
    return CreateProblemStatus::kDbError;
  }

  oj::db::PreparedStatement ins_prob(
      conn.get(),
      "INSERT INTO problems (title, description, difficulty) VALUES (?,?,?)");
  if (!ins_prob.PrepareOk()) {
    Rollback(conn.get(), "prepare problems");
    return CreateProblemStatus::kDbError;
  }
  ins_prob.Bind(title);
  ins_prob.Bind(description);
  ins_prob.Bind(oj::models::DifficultyName(difficulty));
  if (!ins_prob.Execute()) {
    Rollback(conn.get(), "insert problems");
    return CreateProblemStatus::kDbError;
  }
  const long long problem_id = static_cast<long long>(ins_prob.InsertId());
  if (problem_id <= 0) {
    Rollback(conn.get(), "insert id");
    return CreateProblemStatus::kDbError;
  }

  oj::db::PreparedStatement ins_tc(
      conn.get(),
      "INSERT INTO testcases (problem_id, `order`, input, expected) VALUES (?,?,?,?)");
  if (!ins_tc.PrepareOk()) {
    Rollback(conn.get(), "prepare testcases");
    return CreateProblemStatus::kDbError;
  }
  int order = 1;
  for (const auto& tc : testcases) {
    ins_tc.Bind(problem_id);
    ins_tc.Bind(static_cast<long long>(order));
    ins_tc.Bind(tc.input);
    ins_tc.Bind(tc.expected);
    if (!ins_tc.Execute()) {
      Rollback(conn.get(), "insert testcase order=" + std::to_string(order));
      return CreateProblemStatus::kDbError;
    }
    ++order;
  }

  if (mysql_query(conn.get(), "COMMIT") != 0) {
    Rollback(conn.get(), "commit");
    return CreateProblemStatus::kDbError;
  }

  out_id = problem_id;
  return CreateProblemStatus::kOk;
}

UpdateProblemStatus UpdateProblem(
    long long id, const std::string& title, const std::string& description,
    oj::models::Difficulty difficulty,
    const std::vector<oj::models::TestCase>& testcases) {
  // 基础校验（表单错误提示路径，不触发事务）。
  if (title.empty() || title.size() > 255 || description.empty() ||
      !ValidTestcases(testcases)) {
    return UpdateProblemStatus::kInvalidInput;
  }

  oj::db::ConnectionPool* pool = oj::db::DbPool();
  if (pool == nullptr) {
    LogError("problem_service: 数据库连接池未初始化");
    return UpdateProblemStatus::kDbError;
  }
  auto conn = pool->Acquire();
  if (!conn) {
    LogError("problem_service: 获取数据库连接失败");
    return UpdateProblemStatus::kDbError;
  }

  // 事务：UPDATE 题目 + 整体替换用例，任一步失败整体回滚（SPEC 阶段 5）。
  if (mysql_query(conn.get(), "START TRANSACTION") != 0) {
    LogError("problem_service: START TRANSACTION 失败: " + std::string(mysql_error(conn.get())));
    return UpdateProblemStatus::kDbError;
  }

  // 事务内校验题目存在性（不能依赖 UPDATE 的 AffectedRows 判存在：
  // 字段无变更时 MySQL 可能返回 0 行影响，与「不存在」混淆）。
  {
    oj::db::PreparedStatement exists(
        conn.get(), "SELECT id FROM problems WHERE id=?");
    if (!exists.PrepareOk()) {
      Rollback(conn.get(), "prepare exists");
      return UpdateProblemStatus::kDbError;
    }
    exists.Bind(id);
    if (!exists.Execute() || !exists.Fetch()) {
      Rollback(conn.get(), "check exists");
      return UpdateProblemStatus::kNotFound;
    }
  }

  oj::db::PreparedStatement upd(
      conn.get(),
      "UPDATE problems SET title=?, description=?, difficulty=? WHERE id=?");
  if (!upd.PrepareOk()) {
    Rollback(conn.get(), "prepare update");
    return UpdateProblemStatus::kDbError;
  }
  upd.Bind(title);
  upd.Bind(description);
  upd.Bind(oj::models::DifficultyName(difficulty));
  upd.Bind(id);
  if (!upd.Execute()) {
    Rollback(conn.get(), "update problems");
    return UpdateProblemStatus::kDbError;
  }

  oj::db::PreparedStatement del_tc(conn.get(),
                                   "DELETE FROM testcases WHERE problem_id=?");
  if (!del_tc.PrepareOk()) {
    Rollback(conn.get(), "prepare delete testcases");
    return UpdateProblemStatus::kDbError;
  }
  del_tc.Bind(id);
  if (!del_tc.Execute()) {
    Rollback(conn.get(), "delete testcases");
    return UpdateProblemStatus::kDbError;
  }

  oj::db::PreparedStatement ins_tc(
      conn.get(),
      "INSERT INTO testcases (problem_id, `order`, input, expected) VALUES (?,?,?,?)");
  if (!ins_tc.PrepareOk()) {
    Rollback(conn.get(), "prepare testcases");
    return UpdateProblemStatus::kDbError;
  }
  int order = 1;
  for (const auto& tc : testcases) {
    ins_tc.Bind(id);
    ins_tc.Bind(static_cast<long long>(order));
    ins_tc.Bind(tc.input);
    ins_tc.Bind(tc.expected);
    if (!ins_tc.Execute()) {
      Rollback(conn.get(), "insert testcase order=" + std::to_string(order));
      return UpdateProblemStatus::kDbError;
    }
    ++order;
  }

  if (mysql_query(conn.get(), "COMMIT") != 0) {
    Rollback(conn.get(), "commit");
    return UpdateProblemStatus::kDbError;
  }

  return UpdateProblemStatus::kOk;
}

bool DeleteProblem(long long id) {
  oj::db::ConnectionPool* pool = oj::db::DbPool();
  if (pool == nullptr) {
    LogError("problem_service: 数据库连接池未初始化");
    return false;
  }
  auto conn = pool->Acquire();
  if (!conn) {
    LogError("problem_service: 获取数据库连接失败");
    return false;
  }

  // testcases / submissions 通过外键 CASCADE 级联删除（SPEC 4.3.1）。
  oj::db::PreparedStatement stmt(std::move(conn),
                                 "DELETE FROM problems WHERE id=?");
  if (!stmt.PrepareOk()) {
    LogError("problem_service: 预编译失败: " + stmt.Error());
    return false;
  }
  stmt.Bind(id);
  if (!stmt.Execute()) {
    LogError("problem_service: 删除题目失败 id=" + std::to_string(id) +
             " err=" + stmt.Error());
    return false;
  }
  return true;
}

long long CountProblems() {
  oj::db::ConnectionPool* pool = oj::db::DbPool();
  if (pool == nullptr) {
    return -1;
  }
  auto conn = pool->Acquire();
  if (!conn) {
    return -1;
  }

  oj::db::PreparedStatement stmt(std::move(conn),
                                 "SELECT COUNT(*) FROM problems");
  if (!stmt.PrepareOk() || !stmt.Execute() || !stmt.Fetch()) {
    LogError("problem_service: 统计题目数失败");
    return -1;
  }
  return std::stoll(stmt.GetString(0));
}

}  // namespace oj::services
