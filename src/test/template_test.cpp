#include <string>

#include <gtest/gtest.h>

#include "views/template.h"

namespace {

// LoadTemplate 必须能从 www/ 目录读出 layout.html（服务端渲染的骨架模板）。
TEST(TemplateLoad, LoadsLayoutTemplate) {
  const std::string html = oj::views::LoadTemplate("layout.html");
  EXPECT_NE(std::string::npos, html.find("<!DOCTYPE html>"));
  EXPECT_NE(std::string::npos, html.find("{{title}}"));
  EXPECT_NE(std::string::npos, html.find("{{body}}"));
}

// 各页面模板文件应随源码存在，避免 SSR 渲染时退化成空 body。
TEST(TemplateLoad, LoadsPageTemplates) {
  EXPECT_NE(std::string::npos, oj::views::LoadTemplate("problems.html").find("{{rows}}"));
  EXPECT_NE(std::string::npos,
            oj::views::LoadTemplate("problem_detail.html").find("{{problem_title}}"));
  EXPECT_NE(std::string::npos, oj::views::LoadTemplate("error.html").find("{{code}}"));
}

// 不存在的模板返回空串而不是抛出异常。
TEST(TemplateLoad, MissingTemplateReturnsEmpty) {
  EXPECT_TRUE(oj::views::LoadTemplate("no_such_file.html").empty());
}

// ReplaceAll 全量替换：出现多次的占位符应全部被替换。
TEST(TemplateReplace, ReplacesAllOccurrences) {
  const std::string out =
      oj::views::ReplaceAll("a {{x}} b {{x}} c", "{{x}}", "1");
  EXPECT_EQ("a 1 b 1 c", out);
}

// ReplaceAll 把空值替换为删除：用于模板中可选插槽。
TEST(TemplateReplace, ReplacesWithEmptyValue) {
  const std::string out =
      oj::views::ReplaceAll("pre{{gone}}post", "{{gone}}", "");
  EXPECT_EQ("prepost", out);
}

// ReplaceAll 对不存在的 key 原样返回。
TEST(TemplateReplace, NoKeyNoChange) {
  const std::string src = "hello world";
  EXPECT_EQ(src, oj::views::ReplaceAll(src, "{{missing}}", "v"));
}

}  // namespace
