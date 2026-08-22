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
  EXPECT_NE(std::string::npos,
            html.find("<main class=\"container\"><h1>hi</h1></main>"));
}

// Layout() 输出的文档必须以 </html> 收尾，保证 HTML 结构闭合、可被浏览器正确解析。
TEST(RenderLayout, ClosesHtmlTag) {
  const std::string html = oj::views::Layout("t", "b");
  EXPECT_EQ(std::string("</html>\n"),
            html.substr(html.size() - std::string("</html>\n").size()));
}

// 边界情况：title 与 body 均为空时 Layout() 不应崩溃，
// 仍须输出完整文档骨架（含空主体）。
TEST(RenderLayout, HandlesEmptyTitleAndBody) {
  const std::string html = oj::views::Layout("", "");
  EXPECT_NE(std::string::npos, html.find("<!DOCTYPE html>"));
  EXPECT_NE(std::string::npos, html.find("</html>"));
  EXPECT_NE(std::string::npos, html.find("<main class=\"container\"></main>"));
}

// Layout() 必须引用全局样式表，否则所有页面（导航/表格/编辑器）都会失去基础样式。
TEST(RenderLayout, ContainsStylesheetLink) {
  const std::string html = oj::views::Layout("t", "b");
  EXPECT_NE(std::string::npos,
            html.find("<link rel=\"stylesheet\" href=\"/static/css/style.css\">"));
}

// Layout() 必须包含顶部导航：品牌名 + 题库/登录/注册链接（阶段 2 起全站导航）。
TEST(RenderLayout, ContainsNavbar) {
  const std::string html = oj::views::Layout("t", "b");
  EXPECT_NE(std::string::npos, html.find("class=\"navbar-brand\""));
  EXPECT_NE(std::string::npos, html.find("brand-mark"));
  EXPECT_NE(std::string::npos, html.find("href=\"/problems\">题库</a>"));
  EXPECT_NE(std::string::npos, html.find("href=\"/login\">登录</a>"));
  EXPECT_NE(std::string::npos, html.find("href=\"/register\">注册</a>"));
}

// 行为锁定：body 由受控代码拼装、未做 HTML 转义（转义发生在页面函数内部，
// 如 problem_pages 对标题/模板的 HtmlEscape），Layout 本身保持原样透传。
TEST(RenderLayout, EscapesNothingInBodyByDesign) {
  const std::string html = oj::views::Layout("t", "<script>alert(1)</script>");
  EXPECT_NE(std::string::npos, html.find("<script>alert(1)</script>"));
}

// 游客导航：NavLinks("") 只含登录/注册链接，不含登出（FR-1.2 游客可见性）。
TEST(RenderLayout, NavLinksAnonymousHasLoginAndRegister) {
  const std::string nav = oj::views::NavLinks("");
  EXPECT_NE(std::string::npos, nav.find("href=\"/login\">登录</a>"));
  EXPECT_NE(std::string::npos, nav.find("href=\"/register\">注册</a>"));
  EXPECT_EQ(std::string::npos, nav.find("logout"));
}

// 登录态导航：NavLinks 显示用户名 + 登出按钮（POST /logout），不显示登录/注册。
TEST(RenderLayout, NavLinksLoggedInShowsUsernameAndLogout) {
  const std::string nav = oj::views::NavLinks("alice");
  EXPECT_NE(std::string::npos, nav.find("alice"));
  EXPECT_NE(std::string::npos, nav.find("<form action=\"/logout\" method=\"post\""));
  EXPECT_NE(std::string::npos, nav.find(">登出</button>"));
  EXPECT_EQ(std::string::npos, nav.find("href=\"/login\""));
}

// 登录态导航：用户名中的特殊字符必须 HTML 转义（NFR-3 防 XSS）。
TEST(RenderLayout, NavLinksEscapesUsername) {
  const std::string nav = oj::views::NavLinks("<script>alert(1)</script>");
  EXPECT_EQ(std::string::npos, nav.find("<script>"));
  EXPECT_NE(std::string::npos, nav.find("&lt;script&gt;"));
}

// Layout 支持传入登录态导航：{{nav_links}} 插槽被正确替换。
TEST(RenderLayout, LayoutFillsNavLinksSlot) {
  const std::string html =
      oj::views::Layout("t", "b", oj::views::NavLinks("alice"));
  EXPECT_NE(std::string::npos, html.find(">登出</button>"));
  EXPECT_NE(std::string::npos, html.find("alice"));
  EXPECT_EQ(std::string::npos, html.find("href=\"/login\">登录</a>"));
}

// 缺省导航：Layout(title, body) 使用「未登录」导航（登录/注册），行为锁定。
TEST(RenderLayout, LayoutDefaultsToAnonymousNav) {
  const std::string html = oj::views::Layout("t", "b");
  EXPECT_NE(std::string::npos, html.find("href=\"/login\">登录</a>"));
  EXPECT_NE(std::string::npos, html.find("href=\"/register\">注册</a>"));
  EXPECT_EQ(std::string::npos, html.find("navbar-logout"));
}

}  // namespace
