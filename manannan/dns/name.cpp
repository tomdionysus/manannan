//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/dns/name.h"

#include <algorithm>
#include <utility>

namespace manannan::dns {
namespace {

std::uint8_t lower(std::uint8_t c) { return (c >= 'A' && c <= 'Z') ? static_cast<std::uint8_t>(c + ('a' - 'A')) : c; }

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Characters with a meaning in presentation form, written with a backslash.
bool is_special(std::uint8_t c) { return c == '.' || c == '\\' || c == '"' || c == '(' || c == ')' || c == ';' || c == '@' || c == '$'; }

bool equal_from(const std::vector<std::uint8_t>& a, std::size_t from, const std::vector<std::uint8_t>& b) {
  if (a.size() - from != b.size()) return false;
  // Length octets are below 64, so lowering them changes nothing.
  return std::equal(b.begin(), b.end(), a.begin() + static_cast<std::ptrdiff_t>(from), [](std::uint8_t x, std::uint8_t y) { return lower(x) == lower(y); });
}

}  // namespace

Name::Name() : _wire{0} {}

std::optional<Name> Name::from_wire(std::span<const std::uint8_t> wire) {
  if (wire.empty() || wire.size() > max_name_octets) return std::nullopt;
  std::size_t pos = 0;
  while (true) {
    const std::uint8_t length = wire[pos];
    if (length > max_label_octets) return std::nullopt;
    if (length == 0) {
      if (pos + 1 != wire.size()) return std::nullopt;
      return Name(std::vector<std::uint8_t>(wire.begin(), wire.end()));
    }
    pos += 1 + length;
    if (pos >= wire.size()) return std::nullopt;
  }
}

std::optional<Name> Name::from_text(std::string_view text) {
  if (text.empty()) return std::nullopt;
  if (text == ".") return Name();
  std::vector<std::uint8_t> wire;
  std::vector<std::uint8_t> label;
  bool ended = false;  // the text ended with a dot
  auto close_label = [&]() {
    if (label.empty() || label.size() > max_label_octets) return false;
    wire.push_back(static_cast<std::uint8_t>(label.size()));
    wire.insert(wire.end(), label.begin(), label.end());
    label.clear();
    return wire.size() + 1 <= max_name_octets;
  };
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '.') {
      if (!close_label()) return std::nullopt;
      ended = i + 1 == text.size();
      continue;
    }
    if (c != '\\') {
      label.push_back(static_cast<std::uint8_t>(c));
      continue;
    }
    if (++i == text.size()) return std::nullopt;
    if (!is_digit(text[i])) {
      label.push_back(static_cast<std::uint8_t>(text[i]));
      continue;
    }
    if (i + 2 >= text.size() || !is_digit(text[i + 1]) || !is_digit(text[i + 2])) return std::nullopt;
    const int value = (text[i] - '0') * 100 + (text[i + 1] - '0') * 10 + (text[i + 2] - '0');
    if (value > 255) return std::nullopt;
    label.push_back(static_cast<std::uint8_t>(value));
    i += 2;
  }
  if (!ended && !close_label()) return std::nullopt;
  wire.push_back(0);
  return Name(std::move(wire));
}

std::string Name::to_text() const {
  if (is_root()) return ".";
  std::string text;
  std::size_t pos = 0;
  while (_wire[pos] != 0) {
    const std::size_t end = pos + 1 + _wire[pos];
    for (std::size_t i = pos + 1; i < end; ++i) {
      const std::uint8_t c = _wire[i];
      if (c <= 0x20 || c >= 0x7f) {
        text += '\\';
        text += static_cast<char>('0' + c / 100);
        text += static_cast<char>('0' + c / 10 % 10);
        text += static_cast<char>('0' + c % 10);
      } else {
        if (is_special(c)) text += '\\';
        text += static_cast<char>(c);
      }
    }
    text += '.';
    pos = end;
  }
  return text;
}

std::size_t Name::label_count() const {
  std::size_t count = 0;
  for (std::size_t pos = 0; _wire[pos] != 0; pos += 1 + _wire[pos]) ++count;
  return count;
}

Name Name::lowered() const {
  std::vector<std::uint8_t> wire(_wire);
  std::transform(wire.begin(), wire.end(), wire.begin(), lower);
  return Name(std::move(wire));
}

bool Name::is_subdomain_of(const Name& parent) const {
  for (std::size_t pos = 0;; pos += 1 + _wire[pos]) {
    if (equal_from(_wire, pos, parent._wire)) return true;
    if (_wire[pos] == 0) return false;
  }
}

bool operator==(const Name& a, const Name& b) { return equal_from(a._wire, 0, b._wire); }

}  // namespace manannan::dns
