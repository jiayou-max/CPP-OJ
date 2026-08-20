#include "auth/password.h"

#include <cstdio>
#include <string>

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace oj::auth {

namespace {

// 字节数组 → 小写十六进制串（libssl 依赖，SPEC A.1）。
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

std::string Sha256Hex(const std::string& input) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_len = 0;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (ctx == nullptr) {
    return {};
  }
  const int ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
                 EVP_DigestUpdate(ctx, input.data(), input.size()) == 1 &&
                 EVP_DigestFinal_ex(ctx, digest, &digest_len) == 1;
  EVP_MD_CTX_free(ctx);
  if (ok != 1) {
    return {};
  }
  return HexEncode(digest, digest_len);
}

}  // namespace

std::string GenerateSalt() {
  unsigned char bytes[16];
  if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
    // 回退 /dev/urandom（OpenSSL RAND 初始化异常等极端情况）。
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (f == nullptr) {
      return {};
    }
    const bool ok = std::fread(bytes, 1, sizeof(bytes), f) == sizeof(bytes);
    std::fclose(f);
    if (!ok) {
      return {};
    }
  }
  return HexEncode(bytes, sizeof(bytes));
}

std::string HashPassword(const std::string& password, const std::string& salt) {
  return Sha256Hex(salt + password);
}

bool VerifyPassword(const std::string& password, const std::string& salt,
                    const std::string& expected_hash) {
  const std::string hash = HashPassword(password, salt);
  if (hash.empty()) {
    return false;
  }
  return hash == expected_hash;
}

}  // namespace oj::auth
