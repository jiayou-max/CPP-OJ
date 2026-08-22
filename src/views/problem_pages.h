#pragma once

#include <string>
#include <vector>

#include "models/problem.h"

namespace oj::views {

// 落地页首页（阶段 5「大屏落地页」，GET /）：品牌大屏 + 实时统计 + 终端演示。
// problem_count / user_count / submission_count 来自服务层统计，-1（数据库不可用）
// 渲染为 "--"。username 非空时导航栏显示登录态，role 为 admin 时显示管理入口。
std::string LandingPage(const std::string& username,
                        const std::string& role = std::string(),
                        long long problem_count = -1,
                        long long user_count = -1,
                        long long submission_count = -1);

// 题库列表页（FR-2.1）：展示所有题目的编号/标题/难度。
// username 非空时导航栏显示登录态；缺省为未登录导航。
std::string ProblemListPage(const std::vector<oj::models::Problem>& problems,
                            const std::string& username = std::string(),
                            const std::string& role = std::string());

// 题目详情/做题页（FR-2.2）：Markdown 描述 + ace.js 代码编辑器 + 提交表单。
std::string ProblemDetailPage(const oj::models::Problem& problem,
                              const std::string& username = std::string(),
                              const std::string& role = std::string());

}  // namespace oj::views
