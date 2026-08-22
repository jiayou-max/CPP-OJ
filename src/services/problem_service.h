#pragma once

#include <vector>

#include "models/problem.h"
#include "models/testcase.h"

namespace oj::services {

// 题目查询结果状态（区分「不存在」与「数据库不可用」，路由据此渲染 404/500）。
enum class ProblemQueryStatus {
  kOk,       // 查询成功
  kNotFound, // 按 id 未找到（404）
  kDbError,  // 数据库不可用 / 查询失败（500）
};

// 创建题目结果状态（阶段 5：FR-4.2 新增题目）。
enum class CreateProblemStatus {
  kOk,          // 创建成功，out_id 为自增主键
  kInvalidInput, // 标题/描述/用例校验未通过（表单错误提示）
  kDbError,     // 数据库不可用 / 事务失败（500）
};

// 编辑题目结果状态（阶段 5：修改已有题目的标题/描述/难度/用例）。
enum class UpdateProblemStatus {
  kOk,          // 更新成功
  kInvalidInput, // 标题/描述/用例校验未通过（表单错误提示）
  kNotFound,    // 题目 id 不存在（404）
  kDbError,     // 数据库不可用 / 事务失败（500）
};

// 获取单题（含 Markdown 原文）。id 需已通过 http::ParseId 校验。
ProblemQueryStatus GetProblemById(long long id, oj::models::Problem& out);

// 获取全部题目（按 id 升序）。返回 false = 数据库不可用（已打错误日志）。
bool ListProblems(std::vector<oj::models::Problem>& out);

// 获取题目全部测试用例（按 `order` 升序，FR-3.2 逐组比对）。返回 false = 数据库不可用。
bool ListTestcasesByProblem(long long problem_id,
                            std::vector<oj::models::TestCase>& out);

// 创建题目（FR-4.2）：标题 + Markdown 描述 + 难度 + 多组用例，事务保证
// 「题目 + 用例」要么全部写入要么全部回滚（SPEC 阶段 5「事务：题目 + 用例」）。
// testcases 中的 order 字段将被忽略，按数组顺序自 1 起编号。
// 校验规则：标题非空 ≤255、描述非空、至少一组非空用例（input/expected 均非空）。
CreateProblemStatus CreateProblem(const std::string& title,
                                  const std::string& description,
                                  oj::models::Difficulty difficulty,
                                  const std::vector<oj::models::TestCase>& testcases,
                                  long long& out_id);

// 编辑题目（FR-4.2 扩展）：更新标题/描述/难度，并整体替换测试用例。
// 事务：UPDATE problems + DELETE 旧用例 + INSERT 新用例，任一步失败整体回滚。
// 校验规则与 CreateProblem 一致（标题非空 ≤255、描述非空、至少一组非空用例）。
// 题目不存在返回 kNotFound（事务内先校验存在性，防止误判为「无变更」）。
UpdateProblemStatus UpdateProblem(long long id, const std::string& title,
                                  const std::string& description,
                                  oj::models::Difficulty difficulty,
                                  const std::vector<oj::models::TestCase>& testcases);

// 物理删除题目（FR-4.3）：DELETE problems 级联删除 testcases 与 submissions
// （SPEC 4.3.1 外键删除规则）。返回 false = 数据库不可用。
bool DeleteProblem(long long id);

// 题目总数（落地页统计用）。数据库不可用返回 -1。
long long CountProblems();

}  // namespace oj::services
