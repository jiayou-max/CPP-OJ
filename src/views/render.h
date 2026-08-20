#pragma once

#include <string>

namespace oj::views {

// 页面骨架：输出 HTML5 文档 + 基础布局，body 由各页面渲染后拼入（SPEC 4.6 views/render.h）
std::string Layout(const std::string& title, const std::string& body);

}  // namespace oj::views
