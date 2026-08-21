#include <chrono>
#include <random>
#include <string>

#include <gtest/gtest.h>

#include "db/db.h"
#include "db/db_query.h"
#include "services/problem_service.h"

namespace {

// 题目服务测试依赖本机可用的 MySQL + db/schema.sql（SPEC A.1 / 4.3.1）。
// 不可达（或 problems 表缺失）则跳过，保证无 DB 环境构建与测试也能通过。
class ProblemServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    oj::db::DbConfig cfg;
    if (!oj::db::DbInit(cfg)) {
      GTEST_SKIP() << "本机 MySQL 不可达，跳过题目服务测试";
    }

    // 唯一题目标题，避免与既有数据/并行测试冲突。
    std::mt19937_64 rng(
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    title_ = "zz_test_problem_" + std::to_string(rng());

    if (!InsertProblem()) {
      GTEST_SKIP() << "problems 表缺失或插入失败（请先执行 mysql -uroot < db/schema.sql），跳过题目服务测试";
    }
  }

  void TearDown() override {
    DeleteProblem();
  }

  bool InsertProblem() {
    oj::db::PreparedStatement ins(oj::db::DbPool()->Acquire(),
        "INSERT INTO problems (title, description, difficulty) VALUES (?,?,?)");
    if (!ins.PrepareOk()) {
      return false;
    }
    ins.Bind(title_);
    ins.Bind(std::string("# 测试题目\n\n求和函数。"));
    ins.Bind(std::string("hard"));
    if (!ins.Execute()) {
      return false;
    }
    id_ = static_cast<long long>(ins.InsertId());
    return id_ > 0;
  }

  void DeleteProblem() {
    if (id_ <= 0) {
      return;
    }
    oj::db::PreparedStatement del(oj::db::DbPool()->Acquire(),
        "DELETE FROM problems WHERE id=?");
    if (!del.PrepareOk()) {
      return;
    }
    del.Bind(id_);
    del.Execute();
  }

  std::string title_;
  long long id_ = 0;
};

// GetProblemById 应返回刚插入题目的完整字段（含 Markdown 原文与难度）。
TEST_F(ProblemServiceTest, GetProblemByIdReturnsInserted) {
  oj::models::Problem p;
  const auto status = oj::services::GetProblemById(id_, p);
  ASSERT_EQ(oj::services::ProblemQueryStatus::kOk, status);
  EXPECT_EQ(id_, p.id);
  EXPECT_EQ(title_, p.title);
  EXPECT_EQ("# 测试题目\n\n求和函数。", p.description);
  EXPECT_EQ(oj::models::Difficulty::kHard, p.difficulty);
}

// ListProblems 应包含刚插入的题目，且结果按 id 升序、难度解析正确。
TEST_F(ProblemServiceTest, ListProblemsContainsInserted) {
  std::vector<oj::models::Problem> problems;
  ASSERT_TRUE(oj::services::ListProblems(problems));

  bool found = false;
  for (const auto& p : problems) {
    if (p.id == id_) {
      found = true;
      EXPECT_EQ(title_, p.title);
      EXPECT_EQ(oj::models::Difficulty::kHard, p.difficulty);
    }
  }
  EXPECT_TRUE(found) << "ListProblems 未包含刚插入的题目 id=" << id_;

  // 升序校验（抽查相邻项）。
  for (std::size_t i = 1; i < problems.size(); ++i) {
    EXPECT_LT(problems[i - 1].id, problems[i].id);
  }
}

// 不存在的 id → kNotFound（区别于数据库错误）。
TEST_F(ProblemServiceTest, GetMissingProblemReturnsNotFound) {
  oj::models::Problem p;
  const auto status = oj::services::GetProblemById(static_cast<long long>(-1), p);
  EXPECT_EQ(oj::services::ProblemQueryStatus::kNotFound, status);
}

// PreparedStatement 参数绑定：字符串 + 64 位整数同时验证，
// 且不存在的 id 不应命中任何行。
TEST_F(ProblemServiceTest, PreparedStatementLongLongBind) {
  oj::db::PreparedStatement stmt(
      oj::db::DbPool()->Acquire(),
      "SELECT title FROM problems WHERE id=?");
  ASSERT_TRUE(stmt.PrepareOk());

  stmt.Bind(static_cast<long long>(-1));
  ASSERT_TRUE(stmt.Execute());
  EXPECT_FALSE(stmt.Fetch()) << "不存在的 id 不应有结果行";

  // 复用语句绑定真实 id 应返回标题。
  stmt.Bind(id_);
  ASSERT_TRUE(stmt.Execute());
  ASSERT_TRUE(stmt.Fetch());
  EXPECT_EQ(title_, stmt.GetString(0));
  EXPECT_EQ(1u, stmt.ColumnCount());
}

}  // namespace
