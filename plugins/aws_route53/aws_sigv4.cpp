#include "aws_sigv4.h"

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

std::vector<unsigned char> hmac(const std::vector<unsigned char>& key, const std::string& value) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), reinterpret_cast<const unsigned char*>(value.data()),
       value.size(), digest, &size);
  return {digest, digest + size};
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

SignedRequest sign_v4(const std::string& method, const std::string& host, const std::string& path, const std::string& query,
                      const std::string& payload, const Credentials& credentials, const std::string& region,
                      const std::string& service, std::chrono::system_clock::time_point now) {
  const auto date_time = format_utc(now, "%Y%m%dT%H%M%SZ");
  const auto date = format_utc(now, "%Y%m%d");
  const auto payload_hash = sha256(payload);
  std::string headers = "host:" + host + "\nx-amz-content-sha256:" + payload_hash + "\nx-amz-date:" + date_time + "\n";
  std::string signed_headers = "host;x-amz-content-sha256;x-amz-date";
  if (!credentials.session_token.empty()) {
    headers += "x-amz-security-token:" + credentials.session_token + "\n";
    signed_headers += ";x-amz-security-token";
  }
  const auto canonical = method + "\n" + path + "\n" + query + "\n" + headers + "\n" + signed_headers + "\n" + payload_hash;
  const auto scope = date + "/" + region + "/" + service + "/aws4_request";
  const auto string_to_sign = "AWS4-HMAC-SHA256\n" + date_time + "\n" + scope + "\n" + sha256(canonical);
  const auto secret = "AWS4" + credentials.secret_access_key;
  auto key = hmac({secret.begin(), secret.end()}, date);
  key = hmac(key, region);
  key = hmac(key, service);
  key = hmac(key, "aws4_request");
  const auto signature = hmac(key, string_to_sign);
  return {"AWS4-HMAC-SHA256 Credential=" + credentials.access_key_id + "/" + scope + ", SignedHeaders=" + signed_headers +
              ", Signature=" + hex(signature.data(), signature.size()),
          date_time, payload_hash};
}
}  // namespace manannan::aws
