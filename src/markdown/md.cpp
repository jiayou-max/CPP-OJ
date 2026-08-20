#include "markdown/md.h"

#include <md4c-html.h>

namespace oj::markdown {

namespace {

// md4c 回调：把产出的 HTML 片段追加到调用方提供的输出缓冲区。
void AppendOutput(const MD_CHAR* chunk, MD_SIZE size, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(chunk, size);
}

}  // namespace

std::string ToHtml(const std::string& markdown) {
  std::string html;
  if (md_html(markdown.data(), static_cast<MD_SIZE>(markdown.size()),
              &AppendOutput, &html, MD_DIALECT_GITHUB, 0) != 0) {
    return "<p>（Markdown 渲染失败）</p>";
  }
  return html;
}

}  // namespace oj::markdown
