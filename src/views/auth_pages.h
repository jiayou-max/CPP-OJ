#pragma once

#include <string>

namespace oj::views {

// 登录表单页（阶段 4，FR-1.2 / SPEC 4.5：GET /login）。
// error 非空时在表单上方渲染错误提示（A14「非法参数 → 表单错误提示」）。
// username 非空时导航栏显示登录态；缺省为未登录导航。
std::string LoginPage(const std::string& error,
                      const std::string& username = std::string());

// 注册表单页（阶段 4，FR-1.1：GET /register）。
std::string RegisterPage(const std::string& error,
                         const std::string& username = std::string());

}  // namespace oj::views
