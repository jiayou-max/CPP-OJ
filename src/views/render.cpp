#include "views/render.h"

namespace oj::views {

std::string Layout(const std::string& title, const std::string& body) {
  return
      "<!DOCTYPE html>\n"
      "<html lang=\"zh-CN\">\n"
      "<head>\n"
      "  <meta charset=\"utf-8\">\n"
      "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
      "  <title>" +
      title +
      "</title>\n"
      "</head>\n"
      "<body>\n"
      "  <main>" +
      body +
      "</main>\n"
      "</body>\n"
      "</html>\n";
}

}  // namespace oj::views
