#include "services/submission_service.h"

#include <string>

#include "db/db.h"
#include "db/db_query.h"
#include "logger.h"
#include "models/submission.h"

namespace oj::services {

bool CreateSubmission(const oj::models::Submission& submission) {
  oj::db::ConnectionPool* pool = oj::db::DbPool();
  if (pool == nullptr) {
    LogError("submission_service: 数据库连接池未初始化");
    return false;
  }

  auto conn = pool->Acquire();
  if (!conn) {
    LogError("submission_service: 获取数据库连接失败");
    return false;
  }

  // status 以 ENUM 小写名落库（ac/wa/tle/mle/re/ce，SPEC 4.3）。
  oj::db::PreparedStatement stmt(
      std::move(conn),
      "INSERT INTO submissions (user_id, problem_id, code, status, time_ms, memory_kb) "
      "VALUES (?,?,?,?,?,?)");
  if (!stmt.PrepareOk()) {
    LogError("submission_service: 预编译失败: " + stmt.Error());
    return false;
  }

  stmt.Bind(submission.user_id);
  stmt.Bind(submission.problem_id);
  stmt.Bind(submission.code);
  stmt.Bind(std::string(oj::models::SubmissionStatusName(submission.status)));
  stmt.Bind(submission.time_ms);
  stmt.Bind(submission.memory_kb);
  if (!stmt.Execute()) {
    LogError("submission_service: 插入提交记录失败 err=" + stmt.Error());
    return false;
  }
  return true;
}

}  // namespace oj::services
