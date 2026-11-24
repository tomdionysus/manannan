//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Any bytes as a DNS message. The parser accepts or refuses, and does nothing
// else. What it accepts, the writer writes, and the parser reads back as the
// same message: a difference aborts.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "manannan/dns/message.h"
#include "manannan/dns/writer.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  using namespace manannan::dns;
  Message message;
  if (parse_message(std::span<const std::uint8_t>(data, size), message) != ParseError::none) return 0;
  // Decompressed RDATA may no longer fit; that is not a fault.
  const auto written = write_message(message);
  if (!written) return 0;
  Message again;
  if (parse_message(*written, again) != ParseError::none || !(again == message)) std::abort();
  return 0;
}
