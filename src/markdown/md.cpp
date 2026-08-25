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
  // MD_FLAG_NOHTML 禁止 raw HTML 块/行内标签原样透传（md4c 会将其按普通文本转义输出），
  // 防止题目描述等用户可控 Markdown 注入任意 HTML/脚本（XSS，NFR-3）。
  if (md_html(markdown.data(), static_cast<MD_SIZE>(markdown.size()),
              &AppendOutput, &html,
              MD_DIALECT_GITHUB | MD_FLAG_NOHTML, 0) != 0) {
    return "<p>（Markdown 渲染失败）</p>";
  }
  return html;
}

}  // namespace oj::markdown
