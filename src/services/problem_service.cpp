#include "services/problem_service.h"

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

}  // namespace oj::services
