#include "views/template.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// 探测 www/ 根目录：优先仓库根，回退到 build 目录（ctest 从 build/ 运行）。
std::string WwwRoot() {
  for (const char* dir : {"./www", "../www"}) {
    if (std::filesystem::is_directory(dir)) {
      return dir;
    }
  }
  return "./www";  // 兜底：LoadTemplate 得到空串，便于排查挂载路径。
}

}  // namespace

namespace oj::views {

std::string LoadTemplate(const std::string& name) {
  static const std::string root = WwwRoot();

  std::ifstream in(root + "/" + name);
  if (!in) {
    return "";
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string ReplaceAll(const std::string& src, const std::string& key,
                       const std::string& value) {
  std::string out = src;
  for (std::string::size_type pos = 0;
       (pos = out.find(key, pos)) != std::string::npos;
       pos += value.size()) {
    out.replace(pos, key.size(), value);
  }
  return out;
}

}  // namespace oj::views
