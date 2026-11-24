//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Builds DNS messages octet by octet, so the parser's tests state exactly
// what is on the wire, independently of the writer.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "manannan/dns/name.h"

namespace manannan::test {

class DnsBytes {
 public:
  DnsBytes& u8(std::uint8_t value) {
    _bytes.push_back(value);
    return *this;
  }
  DnsBytes& u16(std::uint16_t value) { return u8(static_cast<std::uint8_t>(value >> 8)).u8(static_cast<std::uint8_t>(value)); }
  DnsBytes& u32(std::uint32_t value) { return u16(static_cast<std::uint16_t>(value >> 16)).u16(static_cast<std::uint16_t>(value)); }
  DnsBytes& raw(std::initializer_list<std::uint8_t> values) {
    _bytes.insert(_bytes.end(), values);
    return *this;
  }
  // An uncompressed name from presentation form.
  DnsBytes& name(std::string_view text) {
    const std::vector<std::uint8_t> wire = dns::Name::from_text(text)->wire();
    _bytes.insert(_bytes.end(), wire.begin(), wire.end());
    return *this;
  }
  DnsBytes& pointer(std::uint16_t offset) { return u16(static_cast<std::uint16_t>(0xc000 | offset)); }
  // A header with the given ID, flags and section counts.
  DnsBytes& header(std::uint16_t id, std::uint16_t flags, std::uint16_t qd, std::uint16_t an = 0, std::uint16_t ns = 0, std::uint16_t ar = 0) {
    return u16(id).u16(flags).u16(qd).u16(an).u16(ns).u16(ar);
  }
  std::size_t size() const { return _bytes.size(); }
  const std::vector<std::uint8_t>& bytes() const { return _bytes; }

 private:
  std::vector<std::uint8_t> _bytes;
};

}  // namespace manannan::test
