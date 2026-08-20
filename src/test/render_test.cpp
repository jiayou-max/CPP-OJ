#include <string>

#include <gtest/gtest.h>

#include "views/render.h"

namespace {

// Layout() 必须输出 HTML5 文档声明，保证 SSR 页面以标准模式渲染。
TEST(RenderLayout, ContainsHtml5Doctype) {
  const std::string html = oj::views::Layout("t", "b");
  EXPECT_NE(std::string::npos, html.find("<!DOCTYPE html>"));
}

// Layout() 必须把传入的 title 放入 <head><title>，浏览器标签栏才会正确显示标题。
TEST(RenderLayout, ContainsTitle) {
  const std::string html = oj::views::Layout("My Title", "b");
  EXPECT_NE(std::string::npos, html.find("<title>My Title</title>"));
}

// Layout() 必须把 body 原样嵌入 <main>，页面主体内容才能被渲染出来。
TEST(RenderLayout, ContainsBodyContent) {
  const std::string html = oj::views::Layout("t", "<h1>hi</h1>");
  EXPECT_NE(std::string::npos, html.find("<main><h1>hi</h1></main>"));
}

// Layout() 输出的文档必须以 </html> 收尾，保证 HTML 结构闭合、可被浏览器正确解析。
TEST(RenderLayout, ClosesHtmlTag) {
  const std::string html = oj::views::Layout("t", "b");
  EXPECT_EQ(std::string("</html>\n"),
            html.substr(html.size() - std::string("</html>\n").size()));
}

// 边界情况：title 与 body 均为空时 Layout() 不应崩溃，
// 仍须输出完整文档骨架（含 <main></main> 空主体）。
TEST(RenderLayout, HandlesEmptyTitleAndBody) {
  const std::string html = oj::views::Layout("", "");
  EXPECT_NE(std::string::npos, html.find("<!DOCTYPE html>"));
  EXPECT_NE(std::string::npos, html.find("</html>"));
  EXPECT_NE(std::string::npos, html.find("<main></main>"));
}

// 行为锁定：阶段 0 尚未做 HTML 转义（body 由受控代码拼装），
// 因此 <script> 会被原样输出。此用例固化当前行为，
// 后续引入用户输入渲染时需补充转义（SPEC NFR-3），届时应更新该用例。
TEST(RenderLayout, EscapesNothingInBodyByDesign) {
  const std::string html = oj::views::Layout("t", "<script>alert(1)</script>");
  EXPECT_NE(std::string::npos, html.find("<script>alert(1)</script>"));
}

}  // namespace
