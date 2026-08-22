#include <chrono>
#include <random>
#include <string>

#include <gtest/gtest.h>

#include "auth/password.h"
#include "db/db.h"
#include "db/db_query.h"
#include "models/user.h"
#include "services/user_service.h"

namespace {

// 用户服务测试依赖本机可用的 MySQL + db/schema.sql（SPEC A.1 / 4.3.1）。
// 不可达则跳过，保证无 DB 环境构建与测试也能通过（同 problem_service_test）。
class UserServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 与 main.cpp 一致：支持 OJ_DB_* 环境变量覆盖（SPEC 4.3.1）。
    const oj::db::DbConfig cfg = oj::db::DbConfigFromEnv();
    if (!oj::db::DbInit(cfg)) {
      GTEST_SKIP() << "本机 MySQL 不可达，跳过用户服务测试";
    }

    // 唯一用户名（合法字符：字母/数字/下划线），避免与既有数据/并行测试冲突。
    std::mt19937_64 rng(
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    username_ = "zz_test_user_" + std::to_string(rng());
    password_ = "test_secret_1";
  }

  void TearDown() override {
    if (username_.empty()) {
      return;
    }
    // 先删该用户提交（FK RESTRICT 保护提交记录），再删用户。
    oj::db::PreparedStatement dels(oj::db::DbPool()->Acquire(),
        "DELETE s FROM submissions s JOIN users u ON s.user_id=u.id WHERE u.username=?");
    if (dels.PrepareOk()) {
      dels.Bind(username_);
      dels.Execute();
    }
    oj::db::PreparedStatement delu(oj::db::DbPool()->Acquire(),
        "DELETE FROM users WHERE username=?");
    if (delu.PrepareOk()) {
      delu.Bind(username_);
      delu.Execute();
    }
  }

  std::string username_;
  std::string password_;
};

// 注册成功：返回 kOk 且填充 id/username/role=user（FR-1.4）。
TEST_F(UserServiceTest, RegisterCreatesUser) {
  oj::models::User user;
  const auto status = oj::services::RegisterUser(username_, password_, user);
  ASSERT_EQ(oj::services::UserStatus::kOk, status);
  EXPECT_GT(user.id, 0);
  EXPECT_EQ(username_, user.username);
  EXPECT_EQ("user", user.role);
}

// 重复用户名注册被拒，返回 kDuplicateName（FR-1.1 / A3）。
TEST_F(UserServiceTest, DuplicateRegistrationRejected) {
  oj::models::User first;
  ASSERT_EQ(oj::services::UserStatus::kOk,
            oj::services::RegisterUser(username_, password_, first));

  oj::models::User dup;
  EXPECT_EQ(oj::services::UserStatus::kDuplicateName,
            oj::services::RegisterUser(username_, password_, dup));
}

// 正确密码登录成功，返回的 id/role 与注册一致。
TEST_F(UserServiceTest, LoginWithCorrectPassword) {
  oj::models::User registered;
  ASSERT_EQ(oj::services::UserStatus::kOk,
            oj::services::RegisterUser(username_, password_, registered));

  oj::models::User user;
  const auto status = oj::services::LoginUser(username_, password_, user);
  ASSERT_EQ(oj::services::UserStatus::kOk, status);
  EXPECT_EQ(registered.id, user.id);
  EXPECT_EQ(username_, user.username);
  EXPECT_EQ("user", user.role);
}

// 错误密码登录被拒（kBadCredentials）。
TEST_F(UserServiceTest, LoginRejectsWrongPassword) {
  oj::models::User ignored;
  ASSERT_EQ(oj::services::UserStatus::kOk,
            oj::services::RegisterUser(username_, password_, ignored));

  oj::models::User out;
  EXPECT_EQ(oj::services::UserStatus::kBadCredentials,
            oj::services::LoginUser(username_, "wrong_password", out));
}

// 不存在的用户名登录被拒（kBadCredentials，防用户名枚举）。
TEST_F(UserServiceTest, LoginUnknownUsername) {
  oj::models::User out;
  EXPECT_EQ(oj::services::UserStatus::kBadCredentials,
            oj::services::LoginUser("zz_no_such_user_" + username_, password_, out));
}

// 参数非法：空用户名/非法字符/超长用户名/过短密码 → kInvalidInput（注册）。
// 登录侧非法格式统一 kBadCredentials（不区分账号是否存在）。
TEST_F(UserServiceTest, InvalidInputsRejected) {
  oj::models::User out;
  EXPECT_EQ(oj::services::UserStatus::kInvalidInput,
            oj::services::RegisterUser("", password_, out));
  EXPECT_EQ(oj::services::UserStatus::kInvalidInput,
            oj::services::RegisterUser("bad name!", password_, out));
  EXPECT_EQ(oj::services::UserStatus::kInvalidInput,
            oj::services::RegisterUser(username_, "short", out));
  EXPECT_EQ(oj::services::UserStatus::kInvalidInput,
            oj::services::RegisterUser(std::string(65, 'a'), password_, out));

  EXPECT_EQ(oj::services::UserStatus::kBadCredentials,
            oj::services::LoginUser("bad name!", password_, out));
  EXPECT_EQ(oj::services::UserStatus::kBadCredentials,
            oj::services::LoginUser(username_, "", out));
}

// 用户名/密码白名单校验（与 db/init_admin.cpp 白名单一致）。
TEST_F(UserServiceTest, UsernameAndPasswordValidation) {
  EXPECT_TRUE(oj::services::IsValidUsername("abc_123"));
  EXPECT_TRUE(oj::services::IsValidUsername("A"));
  EXPECT_FALSE(oj::services::IsValidUsername(""));
  EXPECT_FALSE(oj::services::IsValidUsername("has space"));
  EXPECT_FALSE(oj::services::IsValidUsername(std::string(65, 'a')));

  EXPECT_TRUE(oj::services::IsValidPassword("123456"));
  EXPECT_TRUE(oj::services::IsValidPassword(std::string(64, 'x')));
  EXPECT_FALSE(oj::services::IsValidPassword(""));
  EXPECT_FALSE(oj::services::IsValidPassword("12345"));
  EXPECT_FALSE(oj::services::IsValidPassword(std::string(65, 'x')));
}

// 落库密码必须是加盐哈希（sha256(salt+password) 64 位 hex），绝非明文（NFR-3）。
TEST_F(UserServiceTest, RegisteredPasswordIsSaltedHash) {
  oj::models::User user;
  ASSERT_EQ(oj::services::UserStatus::kOk,
            oj::services::RegisterUser(username_, password_, user));

  oj::db::PreparedStatement stmt(oj::db::DbPool()->Acquire(),
      "SELECT password, salt FROM users WHERE id=?");
  ASSERT_TRUE(stmt.PrepareOk());
  stmt.Bind(user.id);
  ASSERT_TRUE(stmt.Execute());
  ASSERT_TRUE(stmt.Fetch());

  const std::string stored_hash = stmt.GetString(0);
  const std::string stored_salt = stmt.GetString(1);
  EXPECT_EQ(64u, stored_hash.size());
  EXPECT_EQ(32u, stored_salt.size());
  EXPECT_NE(password_, stored_hash);
  EXPECT_EQ(stored_hash, oj::auth::HashPassword(password_, stored_salt));
}

}  // namespace
