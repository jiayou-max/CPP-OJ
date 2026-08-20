#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "db/db.h"

namespace {

// 连接池测试依赖本机可用的 MySQL（SPEC A.1）。若不可达则跳过，
// 保证无 DB 的 CI / 开发环境构建与测试也能通过。
class DbPoolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    cfg_ = oj::db::DbConfig();
    oj::db::ConnectionPool probe(cfg_);
    {
      auto c = probe.Acquire();
      reachable_ = static_cast<bool>(c);
    }
    if (!reachable_) {
      GTEST_SKIP() << "本机 MySQL 不可达，跳过连接池测试";
    }
  }

  oj::db::DbConfig cfg_;
  bool reachable_ = false;
};

// 获取连接应返回可用句柄：mysql_ping 通过，池统计同步更新。
TEST_F(DbPoolTest, AcquireReturnsUsableConnection) {
  oj::db::ConnectionPool pool(cfg_);
  {
    auto c = pool.Acquire();
    ASSERT_TRUE(c);
    EXPECT_EQ(0, mysql_ping(c.get()));
    EXPECT_EQ(1u, pool.total());
    EXPECT_EQ(0u, pool.idle());
  }
  // 析构即归还：重新回到空闲池。
  EXPECT_EQ(1u, pool.total());
  EXPECT_EQ(1u, pool.idle());
}

// 归还后的连接应被复用（不反复新建），返回同一底层 MYSQL*。
TEST_F(DbPoolTest, AcquireReusesIdleConnection) {
  oj::db::ConnectionPool pool(cfg_);
  MYSQL* first = nullptr;
  {
    auto c = pool.Acquire();
    ASSERT_TRUE(c);
    first = c.get();
  }
  auto c2 = pool.Acquire();
  ASSERT_TRUE(c2);
  EXPECT_EQ(first, c2.get());
}

// 池满且超过等待时限 → 返回空句柄；连接归还后恢复可获取。
TEST_F(DbPoolTest, AcquireTimesOutWhenPoolFull) {
  oj::db::DbConfig cfg = cfg_;
  cfg.max_connections = 1;
  cfg.acquire_timeout_ms = 200;

  oj::db::ConnectionPool pool(cfg);
  {
    auto held = pool.Acquire();
    ASSERT_TRUE(held);
    auto second = pool.Acquire();  // 池满 + 超时
    EXPECT_FALSE(second);
  }
  auto again = pool.Acquire();
  EXPECT_TRUE(again);
}

// 并发获取：连接数不超过上限，所有请求最终都能拿到连接（信号量式限流语义）。
TEST_F(DbPoolTest, ConcurrentAcquireRespectsLimit) {
  oj::db::DbConfig cfg = cfg_;
  cfg.max_connections = 3;

  oj::db::ConnectionPool pool(cfg);
  constexpr int kThreads = 8;
  constexpr int kIterations = 5;
  std::atomic<int> acquired{0};
  std::atomic<int> failed{0};

  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&] {
      for (int j = 0; j < kIterations; ++j) {
        auto c = pool.Acquire();
        if (c) {
          acquired.fetch_add(1);
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } else {
          failed.fetch_add(1);
        }
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(0, failed.load()) << "并发下不应有获取失败（默认超时足够宽裕）";
  EXPECT_EQ(kThreads * kIterations, acquired.load());
  EXPECT_EQ(cfg.max_connections, pool.total());
  EXPECT_EQ(cfg.max_connections, pool.idle());
}

// 全局单例：DbInit 预检通过后 DbPool 可用；重复 DbInit 幂等。
TEST_F(DbPoolTest, DbInitCreatesGlobalPool) {
  if (oj::db::DbPool() == nullptr) {
    ASSERT_TRUE(oj::db::DbInit(cfg_));
  }
  EXPECT_NE(nullptr, oj::db::DbPool());
  EXPECT_TRUE(oj::db::DbInit(cfg_));  // 已初始化 → 幂等返回 true
}

}  // namespace
