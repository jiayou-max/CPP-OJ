#pragma once

#include <string>

namespace oj::views {

// 统一错误页（404 / 403 / 500，SPEC 阶段 2）。
// status_code: HTTP 状态码；title: 页面主标题；message: 面向用户的说明文字。
std::string ErrorPage(int status_code, const std::string& title,
                      const std::string& message);

}  // namespace oj::views
