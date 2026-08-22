#pragma once

#include <string>

#include "models/user.h"

namespace oj::services {

// 注册/登录业务层（阶段 4，对应 SPEC 4.6 src/services/user_service.h|.cpp，FR-1.1）。
// 密码加盐哈希统一走 src/auth/password.*（sha256 + salt），不得在此另起方案
// （SPEC 4.3.1「密码存储」：否则与既有 admin 密码互不兼容）。

// 用户名合法字符白名单（与 db/init_admin.cpp 一致）：字母/数字/下划线，长度 ≤ 64。
bool IsValidUsername(const std::string& username);

// 密码长度校验（基础档参数校验，D13）：长度 [6, 64]。
bool IsValidPassword(const std::string& password);

// 用户操作结果状态。
enum class UserStatus {
  kOk,             // 成功
  kInvalidInput,   // 用户名/密码格式非法
  kDuplicateName,  // 注册：用户名已存在（FR-1.1 / A3）
  kBadCredentials, // 登录：用户名不存在或密码错误（防用户名枚举）
  kDbError,        // 数据库不可用 / 执行失败（500）
};

// 注册（角色固定为 user，FR-1.4）。成功返回 kOk 并填充 out（id/username/role）。
// FR-1.3：admin 由 oj_init_admin 创建；注册接口白名单校验 + users.username
// 唯一键兜底，保证无法通过注册创建 admin 同名账号。
UserStatus RegisterUser(const std::string& username, const std::string& password,
                        oj::models::User& out);

// 登录：校验用户名/密码，成功返回 kOk 并填充 out（id/username/role）。
// 用户名不存在与密码错误统一返回 kBadCredentials（不泄露账号是否存在）。
UserStatus LoginUser(const std::string& username, const std::string& password,
                     oj::models::User& out);

// 用户总数（落地页统计用）。数据库不可用返回 -1。
long long CountUsers();

}  // namespace oj::services
