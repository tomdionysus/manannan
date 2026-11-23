//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "aws_sigv4.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>

namespace manannan::aws {
namespace {
std::string hex(const unsigned char* data, std::size_t size) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < size; ++i) out << std::setw(2) << static_cast<unsigned>(data[i]);
  return out.str();
}

std::string sha256(const std::string& value) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest);
  return hex(digest, sizeof(digest));
}

// A buffer of key material, cleansed when it goes.
struct KeyBuffer {
  std::vector<unsigned char> bytes;
  KeyBuffer() = default;
  KeyBuffer(const KeyBuffer&) = delete;
  KeyBuffer& operator=(const KeyBuffer&) = delete;
  ~KeyBuffer() { OPENSSL_cleanse(bytes.data(), bytes.size()); }
};

// Replaces key with HMAC-SHA256(key, value).
void hmac_in_place(KeyBuffer& key, const std::string& value) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  HMAC(EVP_sha256(), key.bytes.data(), static_cast<int>(key.bytes.size()), reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest, &size);
  OPENSSL_cleanse(key.bytes.data(), key.bytes.size());
  key.bytes.assign(digest, digest + size);
  OPENSSL_cleanse(digest, sizeof(digest));
}

std::string format_utc(std::chrono::system_clock::time_point time, const char* format) {
  const auto seconds = std::chrono::system_clock::to_time_t(time);
  std::tm utc{};
  gmtime_r(&seconds, &utc);
  std::ostringstream out;
  out << std::put_time(&utc, format);
  return out.str();
}
}  // namespace

std::string url_encode(const std::string& value) {
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setfill('0');
  for (const unsigned char character : value) {
    if (std::isalnum(character) || character == '-' || character == '_' || character == '.' || character == '~')
      out << character;
    else
      out << '%' << std::setw(2) << static_cast<unsigned>(character);
  }
  return out.str();
}

SignedRequest sign_v4(const std::string& method, const std::string& host, const std::string& path, const std::string& query, const std::string& payload,
                      const Credentials& credentials, const std::string& region, const std::string& service, std::chrono::system_clock::time_point now) {
  const auto date_time = format_utc(now, "%Y%m%dT%H%M%SZ");
  const auto date = format_utc(now, "%Y%m%d");
  const auto payload_hash = sha256(payload);
  std::string headers = "host:" + host + "\nx-amz-content-sha256:" + payload_hash + "\nx-amz-date:" + date_time + "\n";
  std::string signed_headers = "host;x-amz-content-sha256;x-amz-date";
  if (!credentials.session_token.empty()) {
    headers += "x-amz-security-token:" + std::string(credentials.session_token.reveal()) + "\n";
    signed_headers += ";x-amz-security-token";
  }
  const auto canonical = method + "\n" + path + "\n" + query + "\n" + headers + "\n" + signed_headers + "\n" + payload_hash;
  const auto scope = date + "/" + region + "/" + service + "/aws4_request";
  const auto string_to_sign = "AWS4-HMAC-SHA256\n" + date_time + "\n" + scope + "\n" + sha256(canonical);
  KeyBuffer key;
  const std::string_view prefix = "AWS4";
  const auto secret = credentials.secret_access_key.reveal();
  key.bytes.reserve(prefix.size() + secret.size());
  key.bytes.insert(key.bytes.end(), prefix.begin(), prefix.end());
  key.bytes.insert(key.bytes.end(), secret.begin(), secret.end());
  hmac_in_place(key, date);
  hmac_in_place(key, region);
  hmac_in_place(key, service);
  hmac_in_place(key, "aws4_request");
  hmac_in_place(key, string_to_sign);  // the signature
  return {"AWS4-HMAC-SHA256 Credential=" + credentials.access_key_id + "/" + scope + ", SignedHeaders=" + signed_headers +
              ", Signature=" + hex(key.bytes.data(), key.bytes.size()),
          date_time, payload_hash};
}
}  // namespace manannan::aws
