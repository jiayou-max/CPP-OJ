#pragma once

#include <string>

namespace oj::views {

// 导航栏右侧链接（layout.html 的 {{nav_links}} 插槽，对应登录态）：
//   username 为空 → 未登录：登录 / 注册；
//   否则 → 用户名 + 登出按钮（POST /logout）。用户名经 HTML 转义（NFR-3）。
std::string NavLinks(const std::string& username);

// 页面骨架：读取 www/layout.html 模板，填充 {{title}} / {{body}} / {{nav_links}}
// 后输出（SPEC 4.6 views/render.h）。全站样式引用 /static/css/style.css，
// 静态资源由路由层 set_mount_point("/static", www/) 提供。
// 无 nav_links 参数的重载使用「未登录」导航（登录 / 注册）。
std::string Layout(const std::string& title, const std::string& body);
std::string Layout(const std::string& title, const std::string& body,
                   const std::string& nav_links);

}  // namespace oj::views
