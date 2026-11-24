//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// A domain name, held as its uncompressed wire form (RFC 1035 3.1): length
// octets and labels, ending with the root's zero octet. Every Name is
// absolute and valid: labels of 1 to 63 octets, 255 octets in all. Case is
// kept as given and ignored in comparison (RFC 1034 3.1, RFC 4343).
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace manannan::dns {

inline constexpr std::size_t max_name_octets = 255;
inline constexpr std::size_t max_label_octets = 63;

class Name {
 public:
  // The root.
  Name();

  // An uncompressed wire name, which must end exactly at the root's octet.
  static std::optional<Name> from_wire(std::span<const std::uint8_t> wire);
  // Presentation form, always taken as absolute: "example.com" and
  // "example.com." are the same name, "." is the root. `\X` stands for X and
  // `\DDD` for the octet DDD (RFC 1035 5.1).
  static std::optional<Name> from_text(std::string_view text);

  // Presentation form with the trailing dot; escapes what from_text reads back.
  std::string to_text() const;

  const std::vector<std::uint8_t>& wire() const { return _wire; }
  std::size_t label_count() const;
  bool is_root() const { return _wire.size() == 1; }
  // The same name with ASCII letters lowered (RFC 4343 3).
  Name lowered() const;
  // True when this name is `parent` or below it.
  bool is_subdomain_of(const Name& parent) const;

  friend bool operator==(const Name& a, const Name& b);

 private:
  explicit Name(std::vector<std::uint8_t> wire) : _wire(std::move(wire)) {}

  std::vector<std::uint8_t> _wire;
};

}  // namespace manannan::dns
