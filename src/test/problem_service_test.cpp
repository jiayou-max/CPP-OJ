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
    // 与 main.cpp 一致：支持 OJ_DB_* 环境变量覆盖（SPEC 4.3.1 连接参数覆盖）。
    const oj::db::DbConfig cfg = oj::db::DbConfigFromEnv();
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

// CreateProblem 应事务性写入题目 + 多组用例，并返回自增 id（FR-4.2）。
TEST_F(ProblemServiceTest, CreateProblemInsertsProblemAndCases) {
  std::vector<oj::models::TestCase> cases;
  oj::models::TestCase t1;
  t1.input = "1 2\n";
  t1.expected = "3\n";
  oj::models::TestCase t2;
  t2.input = "4 5\n";
  t2.expected = "9\n";
  cases.push_back(t1);
  cases.push_back(t2);

  long long created_id = 0;
  const auto status = oj::services::CreateProblem(
      "zz_created_problem", "# 求和", oj::models::Difficulty::kMedium, cases,
      created_id);
  ASSERT_EQ(oj::services::CreateProblemStatus::kOk, status);
  ASSERT_GT(created_id, 0);
  EXPECT_NE(id_, created_id);

  // 题目存在且难度/描述正确。
  oj::models::Problem p;
  ASSERT_EQ(oj::services::ProblemQueryStatus::kOk,
            oj::services::GetProblemById(created_id, p));
  EXPECT_EQ("zz_created_problem", p.title);
  EXPECT_EQ("# 求和", p.description);
  EXPECT_EQ(oj::models::Difficulty::kMedium, p.difficulty);

  // 用例按 order 1..2 写入。
  std::vector<oj::models::TestCase> out;
  ASSERT_TRUE(oj::services::ListTestcasesByProblem(created_id, out));
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ(1, out[0].order);
  EXPECT_EQ("1 2\n", out[0].input);
  EXPECT_EQ("3\n", out[0].expected);
  EXPECT_EQ(2, out[1].order);
  EXPECT_EQ("4 5\n", out[1].input);
  EXPECT_EQ("9\n", out[1].expected);

  // 清理。
  ASSERT_TRUE(oj::services::DeleteProblem(created_id));
}

// CreateProblem 参数校验：空标题 / 空用例 / 用例含空字段 → kInvalidInput，不落库。
TEST_F(ProblemServiceTest, CreateProblemValidatesInput) {
  std::vector<oj::models::TestCase> one_ok;
  oj::models::TestCase t;
  t.input = "1\n";
  t.expected = "2\n";
  one_ok.push_back(t);

  long long id = 0;
  // 空标题
  EXPECT_EQ(oj::services::CreateProblemStatus::kInvalidInput,
            oj::services::CreateProblem("", "desc", oj::models::Difficulty::kEasy,
                                        one_ok, id));
  // 空描述
  EXPECT_EQ(oj::services::CreateProblemStatus::kInvalidInput,
            oj::services::CreateProblem("t", "", oj::models::Difficulty::kEasy,
                                        one_ok, id));
  // 空用例列表
  EXPECT_EQ(oj::services::CreateProblemStatus::kInvalidInput,
            oj::services::CreateProblem("t", "desc", oj::models::Difficulty::kEasy,
                                        {}, id));
  // 用例期望输出为空
  std::vector<oj::models::TestCase> bad;
  oj::models::TestCase tb;
  tb.input = "1\n";
  tb.expected = "";
  bad.push_back(tb);
  EXPECT_EQ(oj::services::CreateProblemStatus::kInvalidInput,
            oj::services::CreateProblem("t", "desc", oj::models::Difficulty::kEasy,
                                        bad, id));

  EXPECT_EQ(0, id) << "校验失败不应产生自增 id";
}

// DeleteProblem 应物理删除题目并级联删除其测试用例（FR-4.3，SPEC 4.3.1）。
TEST_F(ProblemServiceTest, DeleteProblemCascadesTestcases) {
  std::vector<oj::models::TestCase> cases;
  oj::models::TestCase t;
  t.input = "1\n";
  t.expected = "1\n";
  cases.push_back(t);

  long long created_id = 0;
  ASSERT_EQ(oj::services::CreateProblemStatus::kOk,
            oj::services::CreateProblem("zz_to_delete", "d",
                                        oj::models::Difficulty::kEasy, cases,
                                        created_id));
  ASSERT_GT(created_id, 0);

  ASSERT_TRUE(oj::services::DeleteProblem(created_id));

  // 题目已不存在。
  oj::models::Problem p;
  EXPECT_EQ(oj::services::ProblemQueryStatus::kNotFound,
            oj::services::GetProblemById(created_id, p));

  // 用例已级联删除。
  std::vector<oj::models::TestCase> out;
  ASSERT_TRUE(oj::services::ListTestcasesByProblem(created_id, out));
  EXPECT_TRUE(out.empty());
}

// CountProblems 应返回非负值（数据库可用时 ≥ 当前题目数）。
TEST_F(ProblemServiceTest, CountProblemsNonNegative) {
  const long long count = oj::services::CountProblems();
  EXPECT_GE(count, 0);
}

// UpdateProblem 应整体更新题目字段并替换测试用例（阶段 5 编辑功能）。
TEST_F(ProblemServiceTest, UpdateProblemModifiesFieldsAndTestcases) {
  std::vector<oj::models::TestCase> original;
  oj::models::TestCase t0;
  t0.input = "1\n";
  t0.expected = "1\n";
  original.push_back(t0);

  long long created_id = 0;
  ASSERT_EQ(oj::services::CreateProblemStatus::kOk,
            oj::services::CreateProblem("zz_before_edit", "旧描述",
                                        oj::models::Difficulty::kEasy, original,
                                        created_id));
  ASSERT_GT(created_id, 0);

  // 编辑：新标题/描述/难度 + 两组新用例。
  std::vector<oj::models::TestCase> updated;
  oj::models::TestCase t1;
  t1.input = "2 3\n";
  t1.expected = "5\n";
  oj::models::TestCase t2;
  t2.input = "10 20\n";
  t2.expected = "30\n";
  updated.push_back(t1);
  updated.push_back(t2);

  EXPECT_EQ(oj::services::UpdateProblemStatus::kOk,
            oj::services::UpdateProblem(created_id, "zz_after_edit", "新描述",
                                        oj::models::Difficulty::kHard, updated));

  // 字段已更新。
  oj::models::Problem p;
  ASSERT_EQ(oj::services::ProblemQueryStatus::kOk,
            oj::services::GetProblemById(created_id, p));
  EXPECT_EQ("zz_after_edit", p.title);
  EXPECT_EQ("新描述", p.description);
  EXPECT_EQ(oj::models::Difficulty::kHard, p.difficulty);

  // 旧用例被整体替换为两组新用例。
  std::vector<oj::models::TestCase> out;
  ASSERT_TRUE(oj::services::ListTestcasesByProblem(created_id, out));
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ("2 3\n", out[0].input);
  EXPECT_EQ("5\n", out[0].expected);
  EXPECT_EQ("10 20\n", out[1].input);
  EXPECT_EQ("30\n", out[1].expected);

  ASSERT_TRUE(oj::services::DeleteProblem(created_id));
}

// UpdateProblem 参数校验：空标题 / 空用例 → kInvalidInput，题目保持不变。
TEST_F(ProblemServiceTest, UpdateProblemValidatesInput) {
  std::vector<oj::models::TestCase> one_ok;
  oj::models::TestCase t;
  t.input = "1\n";
  t.expected = "2\n";
  one_ok.push_back(t);

  // 空标题
  EXPECT_EQ(oj::services::UpdateProblemStatus::kInvalidInput,
            oj::services::UpdateProblem(id_, "", "desc",
                                        oj::models::Difficulty::kEasy, one_ok));
  // 空描述
  EXPECT_EQ(oj::services::UpdateProblemStatus::kInvalidInput,
            oj::services::UpdateProblem(id_, "t", "",
                                        oj::models::Difficulty::kEasy, one_ok));
  // 空用例列表
  EXPECT_EQ(oj::services::UpdateProblemStatus::kInvalidInput,
            oj::services::UpdateProblem(id_, "t", "desc",
                                        oj::models::Difficulty::kEasy, {}));

  // 原题字段未被修改。
  oj::models::Problem p;
  ASSERT_EQ(oj::services::ProblemQueryStatus::kOk,
            oj::services::GetProblemById(id_, p));
  EXPECT_EQ(title_, p.title);
}

// 更新不存在的题目 → kNotFound。
TEST_F(ProblemServiceTest, UpdateMissingProblemReturnsNotFound) {
  std::vector<oj::models::TestCase> one_ok;
  oj::models::TestCase t;
  t.input = "1\n";
  t.expected = "2\n";
  one_ok.push_back(t);

  EXPECT_EQ(oj::services::UpdateProblemStatus::kNotFound,
            oj::services::UpdateProblem(static_cast<long long>(-1), "t", "d",
                                        oj::models::Difficulty::kEasy, one_ok));
}

}  // namespace
