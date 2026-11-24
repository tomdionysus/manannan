//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Records for test zones, with their RDATA built octet by octet.
#pragma once
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "manannan/dns/message.h"
#include "manannan/dns/name.h"

namespace manannan::test {

inline dns::Name dns_name(std::string_view text) { return *dns::Name::from_text(text); }

inline std::vector<std::uint8_t> a_rdata(std::uint8_t last) { return {192, 0, 2, last}; }

// 2001:db8::last
inline std::vector<std::uint8_t> aaaa_rdata(std::uint8_t last) {
  std::vector<std::uint8_t> rdata(16, 0);
  rdata[0] = 0x20;
  rdata[1] = 0x01;
  rdata[2] = 0x0d;
  rdata[3] = 0xb8;
  rdata[15] = last;
  return rdata;
}

inline std::vector<std::uint8_t> name_rdata(std::string_view target) { return dns_name(target).wire(); }

inline void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 8));
  out.push_back(static_cast<std::uint8_t>(value));
}

inline void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  put16(out, static_cast<std::uint16_t>(value >> 16));
  put16(out, static_cast<std::uint16_t>(value));
}

inline std::vector<std::uint8_t> mx_rdata(std::uint16_t preference, std::string_view exchange) {
  std::vector<std::uint8_t> rdata;
  put16(rdata, preference);
  const std::vector<std::uint8_t> name = name_rdata(exchange);
  rdata.insert(rdata.end(), name.begin(), name.end());
  return rdata;
}

inline std::vector<std::uint8_t> srv_rdata(std::uint16_t priority, std::uint16_t weight, std::uint16_t port, std::string_view target) {
  std::vector<std::uint8_t> rdata;
  put16(rdata, priority);
  put16(rdata, weight);
  put16(rdata, port);
  const std::vector<std::uint8_t> name = name_rdata(target);
  rdata.insert(rdata.end(), name.begin(), name.end());
  return rdata;
}

// An SOA with serial 1 and the given MINIMUM.
inline std::vector<std::uint8_t> soa_rdata(std::string_view mname, std::string_view rname, std::uint32_t minimum) {
  std::vector<std::uint8_t> rdata = name_rdata(mname);
  const std::vector<std::uint8_t> r = name_rdata(rname);
  rdata.insert(rdata.end(), r.begin(), r.end());
  for (const std::uint32_t value : {1u, 7200u, 900u, 1209600u, minimum}) put32(rdata, value);
  return rdata;
}

inline dns::Record record(std::string_view owner, std::uint16_t type, std::uint32_t ttl, std::vector<std::uint8_t> rdata) {
  return dns::Record{dns_name(owner), type, dns::rr_class::in, ttl, std::move(rdata)};
}

}  // namespace manannan::test
