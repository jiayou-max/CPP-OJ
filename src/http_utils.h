#pragma once

#include <string>

namespace oj::http {

// HTML 转义：& < > " ' → 实体（NFR-3 防 XSS，D13）。
// 渲染任何用户可控内容（题目标题/描述、用户名、提交代码等）前都必须调用。
std::string HtmlEscape(const std::string& input);

// 去除首尾空白（注册/登录等表单输入的基础校验辅助）。
std::string Trim(const std::string& input);

// 解析题目/资源 id：仅接受十进制非负整数且不越界（long long 范围）。
// 非法输入返回 false，out 不修改。
bool ParseId(const std::string& s, long long& out);

}  // namespace oj::http
