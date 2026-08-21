#include "views/render.h"

#include "views/template.h"

namespace oj::views {

std::string Layout(const std::string& title, const std::string& body) {
  const std::string html = LoadTemplate("layout.html");
  return ReplaceAll(ReplaceAll(html, "{{title}}", title), "{{body}}", body);
}

}  // namespace oj::views
