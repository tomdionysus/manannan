//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Any text as a zone file. The reader accepts or refuses with
// std::invalid_argument, and does nothing else. What it accepts, the writer
// writes, and the reader reads back as the same records: a difference aborts.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "manannan/dns/zone_file.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  using namespace manannan::dns;
  const ZoneFileLimits limits{.max_octets = 65536, .max_records = 1000};
  std::vector<Record> records;
  try {
    records = parse_zone_file(std::string_view(reinterpret_cast<const char*>(data), size), *Name::from_text("fuzz.test"), limits);
  } catch (const std::invalid_argument&) {
    return 0;
  }
  std::string text;
  for (const Record& record : records) text += to_zone_text(record) + "\n";
  try {
    if (parse_zone_file(text, Name(), ZoneFileLimits{.max_octets = 1 << 30, .max_records = 1000}) != records) std::abort();
  } catch (const std::invalid_argument&) {
    std::abort();
  }
  return 0;
}
