#pragma once

#include <string>

namespace oj::auth {

// 密码加盐哈希模块（SPEC 阶段 1「生成随机盐 + 哈希」，阶段 2 注册/登录共用）。
//
// 全站统一约定（变更须同步 db/schema.sql 与 db/init_admin.cpp 的注释）：
//   - salt      = 16 字节 CSPRNG 随机数 → 32 位十六进制小写串（users.salt VARCHAR(32)）
//   - hash      = sha256(salt + password) → 64 位十六进制小写串（users.password）
//   - 校验       = 重算 sha256(salt + password) 与库中 hash 比较
// 基础档安全（D13）：不要求 PBKDF2/bcrypt，但密码绝不明文入库。

// 生成随机盐；OpenSSL RAND 不可用时回退 /dev/urandom。失败返回空串。
std::string GenerateSalt();

// 计算 sha256(salt + password) 的 hex 串；失败返回空串。
std::string HashPassword(const std::string& password, const std::string& salt);

// 校验密码：重算哈希与 expected_hash 相等即通过。
bool VerifyPassword(const std::string& password, const std::string& salt,
                    const std::string& expected_hash);

}  // namespace oj::auth
