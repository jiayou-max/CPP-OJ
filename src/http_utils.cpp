#include "http_utils.h"

#include <cctype>
#include <limits>
#include <string>

namespace oj::http {

std::string HtmlEscape(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (unsigned char c : input) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out.push_back(static_cast<char>(c)); break;
    }
  }
  return out;
}

std::string Trim(const std::string& input) {
  std::size_t begin = 0;
  while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) {
    ++begin;
  }
  std::size_t end = input.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }
  return input.substr(begin, end - begin);
}

bool ParseId(const std::string& s, long long& out) {
  if (s.empty()) {
    return false;
  }
  // 仅允许纯十进制数字（前导 '+'/'-' 或空白一律拒绝）。
  for (unsigned char c : s) {
    if (!std::isdigit(c)) {
      return false;
    }
  }
  // 排除溢出：先按无符号累加并逐位检查上限。
  unsigned long long value = 0;
  const unsigned long long kMax = static_cast<unsigned long long>(
      std::numeric_limits<long long>::max());
  for (char c : s) {
    const unsigned int digit = static_cast<unsigned int>(c - '0');
    if (value > (kMax - digit) / 10) {
      return false;
    }
    value = value * 10 + digit;
  }
  out = static_cast<long long>(value);
  return true;
}

}  // namespace oj::http
