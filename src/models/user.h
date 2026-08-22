#pragma once

#include <string>

namespace oj::models {

// 用户（users 表一行，对应 SPEC 4.6 src/models/user.h）。
// 密码/盐不放模型：密码加盐哈希由 auth/password.* 统一封装（SPEC 4.3.1），
// 业务层（services/user_service）仅读写 id/username/role。
struct User {
  long long id = 0;
  std::string username;
  std::string role;  // "admin" / "user"（FR-1.4）
};

}  // namespace oj::models
