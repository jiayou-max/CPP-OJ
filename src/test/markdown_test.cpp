#include <string>

#include <gtest/gtest.h>

#include "markdown/md.h"

namespace {

// 标题: `# Hello` 应渲染为 <h1>Hello</h1>，用于题目描述的大标题展示。
TEST(MarkdownToHtml, RendersAtxHeading) {
  EXPECT_EQ("<h1>Hello</h1>\n", oj::markdown::ToHtml("# Hello"));
}

// 行内强调: 粗体/斜体应转成 <strong>/<em>，题目描述中的强调能正确展示。
TEST(MarkdownToHtml, RendersEmphasis) {
  EXPECT_EQ("<p><strong>bold</strong> and <em>it</em></p>\n",
            oj::markdown::ToHtml("**bold** and *it*"));
}

// 围栏代码块: C++ 代码应保留在 <pre><code> 中，供题目示例/题解展示。
TEST(MarkdownToHtml, RendersFencedCodeBlock) {
  const std::string html = oj::markdown::ToHtml("```cpp\nint main() { return 0; }\n```");
  EXPECT_NE(std::string::npos, html.find("<pre><code class=\"language-cpp\">"));
  EXPECT_NE(std::string::npos, html.find("int main() { return 0; }"));
  EXPECT_NE(std::string::npos, html.find("</code></pre>"));
}

// 行内代码: `std::vector` 应保留在 <code> 中，代码标识不被误解析为强调。
TEST(MarkdownToHtml, RendersInlineCode) {
  const std::string html = oj::markdown::ToHtml("use `std::vector` here");
  EXPECT_NE(std::string::npos, html.find("<code>std::vector</code>"));
}

// 无序列表: 短横线列表应渲染为 <ul>/<li>。
TEST(MarkdownToHtml, RendersUnorderedList) {
  const std::string html = oj::markdown::ToHtml("- one\n- two");
  EXPECT_NE(std::string::npos, html.find("<ul>"));
  EXPECT_NE(std::string::npos, html.find("<li>one</li>"));
  EXPECT_NE(std::string::npos, html.find("<li>two</li>"));
}

// 链接: [text](url) 应渲染为 <a href="...">，题目描述中可引用外部资源。
TEST(MarkdownToHtml, RendersLink) {
  EXPECT_EQ("<p><a href=\"https://example.com\">oj</a></p>\n",
            oj::markdown::ToHtml("[oj](https://example.com)"));
}

// GFM 扩展: 表格应渲染为 <table>（本题库描述常用表格形式说明 I/O 示例）。
TEST(MarkdownToHtml, RendersGfmTable) {
  const std::string html = oj::markdown::ToHtml("| a | b |\n|---|---|\n| 1 | 2 |");
  EXPECT_NE(std::string::npos, html.find("<table>"));
  EXPECT_NE(std::string::npos, html.find("<th>a</th>"));
  EXPECT_NE(std::string::npos, html.find("<td>1</td>"));
}

// 边界情况: 空输入不应崩溃，渲染结果为空字符串即可。
TEST(MarkdownToHtml, HandlesEmptyInput) {
  EXPECT_EQ("", oj::markdown::ToHtml(""));
}

// 输出片段语义: 结果不含 HTML 文档骨架，仅为 <body> 内容片段（由 Layout 拼入页面）。
TEST(MarkdownToHtml, ReturnsBodyFragmentOnly) {
  const std::string html = oj::markdown::ToHtml("hi");
  EXPECT_EQ(std::string::npos, html.find("<!DOCTYPE html>"));
  EXPECT_EQ(std::string::npos, html.find("<html"));
}

}  // namespace
