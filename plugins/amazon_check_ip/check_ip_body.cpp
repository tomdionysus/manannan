//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "check_ip_body.h"

#include <arpa/inet.h>

#include <stdexcept>

namespace manannan::check_ip {

std::string parse_body(const std::string& body) {
  const auto first = body.find_first_not_of(" \t\r\n");
  const auto last = body.find_last_not_of(" \t\r\n");
  const auto address = first == std::string::npos ? std::string{} : body.substr(first, last - first + 1);
  in_addr parsed{};
  if (inet_pton(AF_INET, address.c_str(), &parsed) != 1) throw std::runtime_error("check IP returned invalid IPv4 value");
  return address;
}

}  // namespace manannan::check_ip
