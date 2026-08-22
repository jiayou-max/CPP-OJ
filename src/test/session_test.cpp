#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "auth/session.h"

namespace {

using oj::auth::Session;
using oj::auth::SessionConfig;
using oj::auth::SessionManager;

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

// 建一个短 lifetime 的会话管理器，便于在测试中模拟过期。
SessionManager ShortLifetimeManager(int lifetime_ms) {
  SessionConfig cfg;
  cfg.lifetime = std::chrono::milliseconds(lifetime_ms);
  return SessionManager(cfg);
}

// 创建、读取会话；读操作带滑动续期。
TEST(SessionTest, CreateAndGetSession) {
  SessionManager mgr;
  const std::string token = mgr.CreateSession(42, "root", "admin");
  ASSERT_FALSE(token.empty());

  Session s;
  ASSERT_TRUE(mgr.GetSession(token, s));
  EXPECT_EQ(42, s.user_id);
  EXPECT_EQ("root", s.username);
  EXPECT_EQ("admin", s.role);

  // 同一 token 可重复读取（滑动续期不失效）。
  Session s2;
  EXPECT_TRUE(mgr.GetSession(token, s2));
  EXPECT_EQ(42, s2.user_id);
}

// token：64 位小写 hex（32 字节 CSPRNG），且每次创建不重复。
TEST(SessionTest, TokenIsRandomHex) {
  SessionManager mgr;
  const std::string a = mgr.CreateSession(1, "alice", "user");
  const std::string b = mgr.CreateSession(2, "bob", "user");
  ASSERT_FALSE(a.empty());
  ASSERT_FALSE(b.empty());

  EXPECT_EQ(64u, a.size());
  EXPECT_TRUE(IsLowerHex(a));
  EXPECT_NE(a, b);
}

// 未知 token 返回 false，且 out 不被修改。
TEST(SessionTest, GetUnknownTokenReturnsFalse) {
  SessionManager mgr;
  Session s;
  s.user_id = 99;
  EXPECT_FALSE(mgr.GetSession("no-such-token", s));
  EXPECT_EQ(99, s.user_id);
}

// 登出后会话不可再取。
TEST(SessionTest, DestroyRemovesSession) {
  SessionManager mgr;
  const std::string token = mgr.CreateSession(7, "carol", "user");
  ASSERT_FALSE(token.empty());
  EXPECT_EQ(1u, mgr.Count());

  mgr.DestroySession(token);
  Session s;
  EXPECT_FALSE(mgr.GetSession(token, s));
  EXPECT_EQ(0u, mgr.Count());

  mgr.DestroySession(token);  // 销毁不存在的会话应静默
}

// 过期会话被拒绝，并在 GetSession 时顺势清理。
TEST(SessionTest, ExpiredSessionRejectedAndCleaned) {
  SessionManager mgr = ShortLifetimeManager(50);
  const std::string token = mgr.CreateSession(1, "alice", "user");
  ASSERT_FALSE(token.empty());
  EXPECT_EQ(1u, mgr.Count());

  std::this_thread::sleep_for(std::chrono::milliseconds(80));

  Session s;
  EXPECT_FALSE(mgr.GetSession(token, s));
  EXPECT_EQ(0u, mgr.Count());
}

// 滑动续期：未过期的合法访问会刷新基准，避免会话过早失效。
TEST(SessionTest, SlidingExpirationRefreshes) {
  SessionManager mgr = ShortLifetimeManager(200);
  const std::string token = mgr.CreateSession(1, "alice", "user");
  ASSERT_FALSE(token.empty());

  for (int i = 0; i < 3; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Session s;
    ASSERT_TRUE(mgr.GetSession(token, s));  // 每次访问都续期
  }
  EXPECT_EQ(1u, mgr.Count());
}

// CleanupExpired 只清除过期会话，存活的保留。
TEST(SessionTest, CleanupExpiredKeepsFresh) {
  SessionManager mgr = ShortLifetimeManager(300);
  const std::string stale = mgr.CreateSession(1, "alice", "user");
  const std::string fresh = mgr.CreateSession(2, "bob", "user");
  ASSERT_FALSE(stale.empty());
  ASSERT_FALSE(fresh.empty());

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  Session s;
  ASSERT_TRUE(mgr.GetSession(fresh, s));  // 刷新 fresh 的到期基准
  std::this_thread::sleep_for(std::chrono::milliseconds(250));

  EXPECT_EQ(1u, mgr.CleanupExpired());    // 仅 stale 过期
  EXPECT_EQ(1u, mgr.Count());
  EXPECT_TRUE(mgr.GetSession(fresh, s));
  EXPECT_FALSE(mgr.GetSession(stale, s));
}

// 多线程并发创建/读取/销毁不应崩溃，且所有创建出的会话均可被读取。
TEST(SessionTest, ConcurrentAccessIsThreadSafe) {
  SessionManager mgr;
  constexpr int kThreads = 8;
  constexpr int kPerThread = 50;

  std::vector<std::thread> workers;
  std::vector<std::string> tokens(kThreads * kPerThread);
  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&mgr, &tokens, t]() {
      for (int i = 0; i < kPerThread; ++i) {
        tokens[t * kPerThread + i] =
            mgr.CreateSession(t * 1000 + i, "u" + std::to_string(t * 1000 + i), "user");
      }
    });
  }
  for (auto& w : workers) {
    w.join();
  }

  EXPECT_EQ(kThreads * kPerThread, static_cast<int>(mgr.Count()));
  for (const std::string& token : tokens) {
    Session s;
    EXPECT_TRUE(mgr.GetSession(token, s));
  }
}

// Set-Cookie 头：携带 token 并附安全标志（HttpOnly / SameSite / Path）。
TEST(SessionTest, BuildSetCookieContainsFlags) {
  SessionManager mgr;
  const std::string header = mgr.BuildSetCookie("abc123");
  EXPECT_NE(std::string::npos, header.find("oj_session=abc123"));
  EXPECT_NE(std::string::npos, header.find("HttpOnly"));
  EXPECT_NE(std::string::npos, header.find("SameSite=Lax"));
  EXPECT_NE(std::string::npos, header.find("Path=/"));
}

// 登出清 Cookie：值为空，Max-Age=0，且不泄露原 token。
TEST(SessionTest, BuildSetCookieExpireClears) {
  SessionManager mgr;
  const std::string header = mgr.BuildSetCookie("abc123", true);
  EXPECT_NE(std::string::npos, header.find("oj_session="));
  EXPECT_NE(std::string::npos, header.find("Max-Age=0"));
  EXPECT_EQ(std::string::npos, header.find("abc123"));
}

// Cookie 头解析：支持多个 cookie、分隔空白、空值；拒绝前缀/后缀误匹配。
TEST(SessionTest, ParseCookie) {
  const std::string name = "oj_session";
  EXPECT_EQ("tok123", SessionManager::ParseCookie("oj_session=tok123", name));
  EXPECT_EQ("tok123", SessionManager::ParseCookie("oj_session=tok123; other=1", name));
  EXPECT_EQ("tok123", SessionManager::ParseCookie("other=1; oj_session=tok123", name));
  EXPECT_EQ("tok123", SessionManager::ParseCookie("a=1; oj_session = tok123 ; b=2", name));
  EXPECT_EQ("", SessionManager::ParseCookie("", name));
  EXPECT_EQ("", SessionManager::ParseCookie("myoj_session=tok123", name));
  EXPECT_EQ("", SessionManager::ParseCookie("oj_sessionx=tok123", name));
  EXPECT_EQ("", SessionManager::ParseCookie("other=1", name));
}

// 全局单例（SessionInit / Sessions）：幂等初始化，创建读取销毁闭环。
TEST(SessionTest, GlobalSingletonRoundTrip) {
  ASSERT_TRUE(oj::auth::SessionInit());
  ASSERT_NE(nullptr, oj::auth::Sessions());
  ASSERT_TRUE(oj::auth::SessionInit());  // 幂等

  const std::string token = oj::auth::Sessions()->CreateSession(1, "alice", "user");
  ASSERT_FALSE(token.empty());
  Session s;
  EXPECT_TRUE(oj::auth::Sessions()->GetSession(token, s));
  EXPECT_EQ(1, s.user_id);

  oj::auth::Sessions()->DestroySession(token);
  EXPECT_FALSE(oj::auth::Sessions()->GetSession(token, s));
}

}  // namespace
