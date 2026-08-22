#pragma once

#include <string>
#include <vector>

#include "models/problem.h"
#include "models/testcase.h"

namespace oj::views {

// 管理后台：题目列表 + 编辑/删除（FR-4.1 / FR-4.3）。
// 每行含「编辑」链接与「删除」表单（模板 JS 二次确认后 POST 物理删除）。
// username / role 用于导航栏登录态与管理入口展示。
std::string AdminProblemListPage(const std::vector<oj::models::Problem>& problems,
                                 const std::string& username = std::string(),
                                 const std::string& role = std::string());

// 管理后台：新增题目表单（FR-4.2：标题/描述(Markdown)/难度/多组用例）。
// error 非空时在表单上方渲染错误提示（校验失败回填）。
std::string AdminProblemNewPage(const std::string& error,
                                const std::string& username = std::string(),
                                const std::string& role = std::string());

// 管理后台：编辑题目表单（阶段 5 扩展：修改标题/描述/难度/用例）。
// 表单预填当前题目字段与既有测试用例；error 非空时渲染错误提示（校验失败回填）。
std::string AdminProblemEditPage(const oj::models::Problem& problem,
                                 const std::vector<oj::models::TestCase>& testcases,
                                 const std::string& error,
                                 const std::string& username = std::string(),
                                 const std::string& role = std::string());

}  // namespace oj::views
