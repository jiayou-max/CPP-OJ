#pragma once

#include <string>

namespace oj::views {

// 读取 www/ 目录下的模板文件内容；找不到目录或文件时返回空串。
// www 根目录按「仓库根(./www) → build 目录(../www)」顺序自动探测，
// 与路由层 MountStatic 的静态资源路径回退逻辑保持一致。
std::string LoadTemplate(const std::string& name);

// 将 src 中出现的所有 key 原样替换为 value（含空串）。
// 占位符约定为 {{key}}；替换值须由调用方负责 HTML 转义。
std::string ReplaceAll(const std::string& src, const std::string& key,
                       const std::string& value);

}  // namespace oj::views
