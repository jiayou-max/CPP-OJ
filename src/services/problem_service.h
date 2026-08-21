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

// 获取单题（含 Markdown 原文）。id 需已通过 http::ParseId 校验。
ProblemQueryStatus GetProblemById(long long id, oj::models::Problem& out);

// 获取全部题目（按 id 升序）。返回 false = 数据库不可用（已打错误日志）。
bool ListProblems(std::vector<oj::models::Problem>& out);

// 获取题目全部测试用例（按 `order` 升序，FR-3.2 逐组比对）。返回 false = 数据库不可用。
bool ListTestcasesByProblem(long long problem_id,
                            std::vector<oj::models::TestCase>& out);

}  // namespace oj::services
