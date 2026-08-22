#include "views/error_pages.h"

#include <string>

#include "http_utils.h"
#include "views/render.h"
#include "views/template.h"

namespace oj::views {

std::string ErrorPage(int status_code, const std::string& title,
                      const std::string& message,
                      const std::string& username) {
  std::string body = LoadTemplate("error.html");
  body = ReplaceAll(body, "{{code}}", std::to_string(status_code));
  body = ReplaceAll(body, "{{title}}", oj::http::HtmlEscape(title));
  body = ReplaceAll(body, "{{message}}", oj::http::HtmlEscape(message));

  return Layout(std::to_string(status_code) + " - CPP OJ", body,
                NavLinks(username));
}

}  // namespace oj::views
