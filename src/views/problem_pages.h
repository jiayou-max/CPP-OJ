#pragma once

#include <string>
#include <vector>

#include "models/problem.h"

namespace oj::views {

// 题库列表页（FR-2.1）：展示所有题目的编号/标题/难度。
std::string ProblemListPage(const std::vector<oj::models::Problem>& problems);

// 题目详情/做题页（FR-2.2）：Markdown 描述 + 代码编辑器 textarea + 提交表单。
std::string ProblemDetailPage(const oj::models::Problem& problem);

}  // namespace oj::views
