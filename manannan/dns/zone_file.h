//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Zone files (RFC 1035 5), read and written. The reader takes untrusted
// text: bounded in size and record count, it reads $ORIGIN and $TTL (RFC
// 2308 4), refuses $INCLUDE, and knows A, AAAA, NS, CNAME, PTR, MX, SOA,
// TXT, SRV and CAA in presentation form, and every type in RFC 3597's
// generic form (TYPEnnn, \# length hex). Every fault is a
// std::invalid_argument naming its line. What the writer writes, the reader
// reads back to the same records.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "manannan/dns/message.h"
#include "manannan/dns/name.h"

namespace manannan::dns {

struct ZoneFileLimits {
  std::size_t max_octets = 16 * 1024 * 1024;
  std::size_t max_records = 1000000;
};

// The records in `text`, in class IN. Relative names are taken under
// `origin` until a $ORIGIN changes it.
std::vector<Record> parse_zone_file(std::string_view text, const Name& origin, const ZoneFileLimits& limits = {});

// One line of presentation form for `record`: an absolute owner, the TTL,
// IN, the type and the RDATA, in generic form where the RDATA is not well
// formed for its type.
std::string to_zone_text(const Record& record);

// "A", "MX", "TYPE65280" and so on, in any case.
std::optional<std::uint16_t> type_from_text(std::string_view text);
std::string type_to_text(std::uint16_t type);

}  // namespace manannan::dns
