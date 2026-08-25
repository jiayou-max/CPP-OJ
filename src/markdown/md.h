#pragma once

#include <string>

namespace oj::markdown {

// Markdown → HTML（md4c 封装，对应 SPEC 4.6 src/markdown/md.h）。
//
// 使用 GFM 方言（表格 / 删除线 / 自动链接 / 任务列表等），并关闭 raw HTML
// 透传（MD_FLAG_NOHTML，防 XSS），输出仅为 <body> 内容片段（不含 HTML 头尾），
// 由调用方拼入页面。渲染失败时返回一段可见的错误占位 HTML。
std::string ToHtml(const std::string& markdown);

}  // namespace oj::markdown
