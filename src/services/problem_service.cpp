#include "services/problem_service.h"

#include <string>

#include "db/db.h"
#include "db/db_query.h"
#include "logger.h"
#include "models/problem.h"

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

}  // namespace oj::services
