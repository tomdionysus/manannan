//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The reader of the check IP service's answer, which is bytes from outside the
// process: a dotted IPv4 address with surrounding whitespace, and nothing
// else. The plugin caps the body at max_body_bytes. A fuzz target drives it
// (manannan/fuzz/checkip_body_fuzz.cpp).
#pragma once
#include <cstddef>
#include <string>

namespace manannan::check_ip {

// An address with a line ending, and room for whitespace around it.
inline constexpr std::size_t max_body_bytes = 64;

// The address, trimmed; throws std::runtime_error for anything else.
std::string parse_body(const std::string& body);

}  // namespace manannan::check_ip
