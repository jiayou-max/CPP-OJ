#pragma once

#include <string>

namespace oj::views {

// 页面骨架：读取 www/layout.html 模板，填充 {{title}} / {{body}} 后输出
// （SPEC 4.6 views/render.h）。全站样式引用 /static/css/style.css，
// 静态资源由路由层 set_mount_point("/static", www/) 提供。
std::string Layout(const std::string& title, const std::string& body);

}  // namespace oj::views
