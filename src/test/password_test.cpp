#include <set>
#include <string>

#include <gtest/gtest.h>

#include "auth/password.h"

namespace {

using oj::auth::GenerateSalt;
using oj::auth::HashPassword;
using oj::auth::VerifyPassword;

// 盐与哈希的格式约定（users.salt VARCHAR(32) / users.password VARCHAR(255)）。
bool IsLowerHex(const std::string& s) {
  if (s.empty()) {
    return false;
  }
  for (unsigned char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

TEST(PasswordTest, GenerateSaltHasExpectedShape) {
  const std::string salt = GenerateSalt();
  EXPECT_EQ(32u, salt.size());        // 16 字节 → 32 位 hex
  EXPECT_TRUE(IsLowerHex(salt));
}

// 随机盐不应重复（同一毫秒内多次调用也不应碰撞，抽样 100 个检查唯一性）。
TEST(PasswordTest, GenerateSaltIsUnique) {
  constexpr int kCount = 100;
  std::set<std::string> seen;
  for (int i = 0; i < kCount; ++i) {
    seen.insert(GenerateSalt());
  }
  EXPECT_EQ(kCount, static_cast<int>(seen.size()));
}

// 哈希：64 位 hex、确定性；盐不同则哈希不同。
TEST(PasswordTest, HashPasswordDeterministicAndSalted) {
  const std::string salt = GenerateSalt();
  const std::string hash = HashPassword("s3cret", salt);

  EXPECT_EQ(64u, hash.size());
  EXPECT_TRUE(IsLowerHex(hash));
  EXPECT_EQ(hash, HashPassword("s3cret", salt));  // 确定性

  const std::string other_salt = GenerateSalt();
  ASSERT_NE(salt, other_salt);
  EXPECT_NE(hash, HashPassword("s3cret", other_salt));  // 加盐隔离

  EXPECT_NE(hash, HashPassword("s3cretX", salt));  // 密码不同则哈希不同
}

// 校验：正确密码通过，错误密码拒绝。
TEST(PasswordTest, VerifyPassword) {
  const std::string salt = GenerateSalt();
  const std::string hash = HashPassword("correct horse", salt);

  EXPECT_TRUE(VerifyPassword("correct horse", salt, hash));
  EXPECT_FALSE(VerifyPassword("wrong password", salt, hash));
  EXPECT_FALSE(VerifyPassword("correct horse", "different_salt_123", hash));
}

// 空盐/空密码的边界行为：不崩溃；校验仍按「重算哈希比对」的语义工作，
// 是否拒绝空密码属于注册/登录层的业务校验，不在此模块强制。
TEST(PasswordTest, EmptyInputsAreWellDefined) {
  const std::string hash = HashPassword("", "");
  EXPECT_EQ(64u, hash.size());
  EXPECT_TRUE(IsLowerHex(hash));
  EXPECT_TRUE(VerifyPassword("", "", hash));  // 语义一致（重算比对）
  EXPECT_FALSE(VerifyPassword("", "", "not-a-hash"));
}

}  // namespace
