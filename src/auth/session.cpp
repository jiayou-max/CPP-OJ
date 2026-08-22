#include "auth/session.h"

#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <openssl/rand.h>

#include "http_utils.h"

namespace oj::auth {

namespace {

// 会话 token 随机字节数 → 64 位小写 hex（32 字节 CSPRNG，防猜解/碰撞）。
constexpr std::size_t kSessionTokenBytes = 32;

// 字节数组 → 小写十六进制串（与 auth/password.cpp 同约定，用于会话 token）。
std::string HexEncode(const unsigned char* data, std::size_t len) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (std::size_t i = 0; i < len; ++i) {
    out.push_back(kHex[data[i] >> 4]);
    out.push_back(kHex[data[i] & 0x0f]);
  }
  return out;
}

// 生成 CSPRNG 随机 hex token：优先 OpenSSL RAND_bytes，失败回退 /dev/urandom
// （与 GenerateSalt 一致的健壮性策略）。失败返回空串。
std::string GenerateToken(std::size_t bytes) {
  if (bytes == 0) {
    return {};
  }
  std::string buf(bytes, '\0');
  if (RAND_bytes(reinterpret_cast<unsigned char*>(&buf[0]),
                 static_cast<int>(bytes)) != 1) {
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (f == nullptr) {
      return {};
    }
    const bool ok = std::fread(&buf[0], 1, bytes, f) == bytes;
    std::fclose(f);
    if (!ok) {
      return {};
    }
  }
  return HexEncode(reinterpret_cast<const unsigned char*>(buf.data()), buf.size());
}

std::mutex g_session_mutex;                              // 保护全局单例指针
std::unique_ptr<SessionManager> g_sessions;

}  // namespace

SessionManager::SessionManager(SessionConfig config) : config_(std::move(config)) {}

std::string SessionManager::CreateSession(long long user_id,
                                          const std::string& username,
                                          const std::string& role) {
  CleanupExpired();  // 惰性清理，避免过期会话累积

  const std::string token = GenerateToken(kSessionTokenBytes);
  if (token.empty()) {
    return {};
  }

  Session s;
  s.user_id = user_id;
  s.username = username;
  s.role = role;
  s.last_access = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[token] = std::move(s);
  }
  return token;
}

bool SessionManager::GetSession(const std::string& session_id, Session& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return false;
  }
  if (IsExpired(it->second)) {
    sessions_.erase(it);
    return false;
  }
  // 滑动续期：每次合法访问刷新过期基准。
  it->second.last_access = std::chrono::steady_clock::now();
  out = it->second;
  return true;
}

void SessionManager::DestroySession(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.erase(session_id);
}

std::size_t SessionManager::CleanupExpired() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t removed = 0;
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (IsExpired(it->second)) {
      it = sessions_.erase(it);
      ++removed;
    } else {
      ++it;
    }
  }
  return removed;
}

std::size_t SessionManager::Count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.size();
}

std::string SessionManager::BuildSetCookie(const std::string& session_id,
                                           bool expire) const {
  std::string value = config_.cookie_name + "=";
  if (!expire) {
    value += session_id;
  }
  // HttpOnly 防 JS 窃取；SameSite=Lax 防跨站携带；Path=/ 全站生效。
  value += "; Path=/; HttpOnly; SameSite=Lax";
  if (expire) {
    value += "; Max-Age=0";
  }
  return value;
}

std::string SessionManager::ParseCookie(const std::string& cookie_header,
                                        const std::string& cookie_name) {
  std::size_t pos = 0;
  while (pos < cookie_header.size()) {
    const std::size_t name_start = cookie_header.find(cookie_name, pos);
    if (name_start == std::string::npos) {
      return {};
    }
    // cookie 名必须作为完整分段出现：前面是行首 / ';' / 空白，防止
    // "myoj_session" / "oj_sessionx" 之类被误匹配。
    const bool boundary_ok =
        name_start == 0 ||
        cookie_header[name_start - 1] == ';' ||
        std::isspace(static_cast<unsigned char>(cookie_header[name_start - 1]));
    if (boundary_ok) {
      // 名称后允许空白再跟 '='（宽容处理不规范的客户端/拼接头）。
      std::size_t cursor = name_start + cookie_name.size();
      while (cursor < cookie_header.size() &&
             std::isspace(static_cast<unsigned char>(cookie_header[cursor]))) {
        ++cursor;
      }
      if (cursor < cookie_header.size() && cookie_header[cursor] == '=') {
        const std::size_t value_start = cursor + 1;
        std::size_t value_end = cookie_header.find(';', value_start);
        if (value_end == std::string::npos) {
          value_end = cookie_header.size();
        }
        return oj::http::Trim(
            cookie_header.substr(value_start, value_end - value_start));
      }
    }
    pos = name_start + 1;
  }
  return {};
}

bool SessionManager::IsExpired(const Session& s) const {
  const auto now = std::chrono::steady_clock::now();
  return now - s.last_access > config_.lifetime;
}

bool SessionInit(SessionConfig config) {
  std::lock_guard<std::mutex> lock(g_session_mutex);
  if (g_sessions) {
    return true;  // 幂等
  }
  g_sessions = std::make_unique<SessionManager>(std::move(config));
  return g_sessions != nullptr;
}

SessionManager* Sessions() {
  std::lock_guard<std::mutex> lock(g_session_mutex);
  return g_sessions.get();
}

}  // namespace oj::auth
