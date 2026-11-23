//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Any bytes as the check IP service's body: the reader returns an IPv4
// address or refuses with std::runtime_error, and does nothing else.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "check_ip_body.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > manannan::check_ip::max_body_bytes) return 0;
  try {
    (void)manannan::check_ip::parse_body(std::string(reinterpret_cast<const char*>(data), size));
  } catch (const std::runtime_error&) {
  }
  return 0;
}
