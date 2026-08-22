#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

#include "config.h"

namespace oj::auth {

// 会话配置（阶段 4 Session/Cookie 中间件，FR-1.2）。默认值取自 config.h，
// 单元测试可用短 lifetime 模拟过期行为。
struct SessionConfig {
  std::string cookie_name = kSessionCookieName;      // Cookie 名（全站统一）
  // 会话有效期（滑动续期：每次合法访问自动刷新基准）。毫秒粒度便于单测模拟过期，
  // 生产默认 7 天。
  std::chrono::milliseconds lifetime = std::chrono::hours(kSessionLifetimeHours);
};

// 会话内容（内存存储）。单进程 / 低并发（NFR-1），数据模型无 sessions 表，
// 不落库：服务重启 / 到期 / 注销即失效（个人练手场景，D1/D12）。
struct Session {
  long long user_id = 0;       // users.id（FR-1.5 提交落库用）
  std::string username;        // users.username（导航栏展示登录态）
  std::string role;            // "admin" / "user"（FR-1.4，登录时快照）
  std::chrono::steady_clock::time_point last_access;  // 滑动续期基准
};

// Session/Cookie 会话管理器（线程安全：cpp-httplib 线程池中可并发调用）。
//
// 典型用法：
//   auth::SessionInit();                              // main 启动时初始化一次
//   auto* mgr = auth::Sessions();
//   const std::string token = mgr->CreateSession(uid, username, role);  // → 下发 Set-Cookie
//   auth::Session s;
//   if (mgr->GetSession(token, s)) { /* 已登录，s.user_id / s.username / s.role */ }
//   mgr->DestroySession(token);                       // 登出
class SessionManager {
 public:
  explicit SessionManager(SessionConfig config = SessionConfig());

  // 新建会话并返回会话 token（即 Cookie 值）。生成随机数失败返回空串。
  // 内部先做一次惰性清理，避免长期运行累积过期会话。
  std::string CreateSession(long long user_id, const std::string& username,
                            const std::string& role);

  // 按 token 取会话并刷新最后访问时间（滑动续期）。不存在 / 已过期返回 false，
  // out 不被修改；已过期会话在此顺势清理。
  bool GetSession(const std::string& session_id, Session& out);

  // 主动销毁会话（登出 / 强制下线）。不存在时静默。
  void DestroySession(const std::string& session_id);

  // 清理过期会话，返回清除数量（Create/Get 惰性触发，也可由后台线程定时调用）。
  std::size_t CleanupExpired();

  // 当前存活会话数（观察 / 日志用）。
  std::size_t Count() const;

  // ---- Cookie 辅助（"Session/Cookie 中间件"）----
  // 生成 Set-Cookie 响应头值：HttpOnly + SameSite=Lax + Path=/（NFR-3 / D13，
  // 防 XSS 窃取会话；NFR-5 本机/局域网无 HTTPS，不加 Secure 标志）。
  // expire=true 输出清 Cookie 头（登出时使用，不携带原 token）。
  std::string BuildSetCookie(const std::string& session_id, bool expire = false) const;

  // 从请求 Cookie 头解析指定 cookie 名的值；未找到返回空串。
  static std::string ParseCookie(const std::string& cookie_header,
                                 const std::string& cookie_name);

  const SessionConfig& config() const { return config_; }

 private:
  bool IsExpired(const Session& s) const;

  const SessionConfig config_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Session> sessions_;  // token → 会话
};

// ---- 全局单例（风格同 db::DbPool，供路由 / middleware 复用）----
// 初始化全局会话管理器；幂等（重复调用忽略新配置）。随机数不可用返回 false。
bool SessionInit(SessionConfig config = SessionConfig());

// 获取全局会话管理器；未初始化返回 nullptr。
SessionManager* Sessions();

}  // namespace oj::auth
