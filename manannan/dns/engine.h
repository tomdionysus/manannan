//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The authoritative query engine: one query's octets in, the response's
// octets out, from a Catalog that is only read. It never waits, takes no
// locks and keeps no state between queries. No recursion: RD is copied and
// RA never set.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "manannan/dns/message.h"
#include "manannan/dns/zone.h"

namespace manannan::dns {

enum class Transport { udp, tcp };

struct EngineLimits {
  // The UDP payload advertised, and the most sent over UDP whatever a client
  // advertises (RFC 6891 6.2.5); 1232 avoids fragmentation.
  std::uint16_t udp_payload = 1232;
  // CNAMEs followed for one query before the answer stops (RFC 1034 3.6.2).
  std::size_t max_cname_chain = 8;
  // A query holds a question and an OPT record, and little more.
  ParseLimits parse{.max_records = 16};
};

// The response to `query`, or nothing when none should be sent: to fewer
// octets than a header, or to a message that is itself a response.
std::vector<std::uint8_t> respond(const Catalog& catalog, std::span<const std::uint8_t> query, Transport transport, const EngineLimits& limits = {});

}  // namespace manannan::dns
