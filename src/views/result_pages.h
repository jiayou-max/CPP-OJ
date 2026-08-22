#pragma once

#include <string>

#include "judge/judge.h"

namespace oj::views {

// 判题结果页（FR-3.4 / SPEC 4.4）：总体状态 + 逐用例状态 + 时间/内存统计；
// CE 时附完整 g++ 编译日志（FR-3.3）。
// username 非空时导航栏显示登录态；role 为 admin 时显示管理入口；缺省为未登录导航。
std::string JudgeResultPage(long long problem_id,
                            const std::string& problem_title,
                            const oj::judge::JudgeResult& result,
                            const std::string& username = std::string(),
                            const std::string& role = std::string());

}  // namespace oj::views
