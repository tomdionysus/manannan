//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#pragma once
#include <chrono>
#include <string>

#include "secret.h"

// AWS Signature Version 4 for a single request, signing the host, the payload
// hash, the date and, when present, the session token.
namespace manannan::aws {
struct Credentials {
  std::string access_key_id;  // an identifier, not a secret: it is in every request
  Secret secret_access_key;
  Secret session_token;
};
struct SignedRequest {
  std::string authorization;
  std::string amz_date;
  std::string content_sha256;
};
SignedRequest sign_v4(const std::string& method, const std::string& host, const std::string& path, const std::string& query, const std::string& payload,
                      const Credentials& credentials, const std::string& region, const std::string& service, std::chrono::system_clock::time_point now);
// Percent-encodes everything outside RFC 3986's unreserved characters.
std::string url_encode(const std::string& value);
}  // namespace manannan::aws
