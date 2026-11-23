//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "route53_response.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

namespace manannan::aws {
namespace {
// The text of the first <tag>text</tag> in element whose text is not empty
// and holds no '<'.
std::optional<std::string_view> first_text(std::string_view element, std::string_view open, std::string_view close) {
  for (auto start = element.find(open); start != std::string_view::npos; start = element.find(open, start + 1)) {
    const auto text = start + open.size();
    const auto end = element.find('<', text);
    if (end != std::string_view::npos && end > text && element.substr(end, close.size()) == close) return element.substr(text, end - text);
  }
  return std::nullopt;
}

// The text of the first <ResourceRecord>, whitespace, <Value>text</Value>.
std::optional<std::string_view> first_value(std::string_view set) {
  constexpr std::string_view record = "<ResourceRecord>";
  constexpr std::string_view open = "<Value>";
  constexpr std::string_view close = "</Value>";
  for (auto start = set.find(record); start != std::string_view::npos; start = set.find(record, start + 1)) {
    auto at = start + record.size();
    while (at < set.size() && std::isspace(static_cast<unsigned char>(set[at]))) ++at;
    if (set.substr(at, open.size()) != open) continue;
    const auto text = at + open.size();
    const auto end = set.find('<', text);
    if (end != std::string_view::npos && end > text && set.substr(end, close.size()) == close) return set.substr(text, end - text);
  }
  return std::nullopt;
}

bool is_octal(char character) { return character >= '0' && character <= '7'; }
}  // namespace

std::optional<std::string> find_a_record(const std::string& response, const std::string& fqdn) {
  constexpr std::string_view open = "<ResourceRecordSet>";
  constexpr std::string_view close = "</ResourceRecordSet>";
  const std::string_view text = response;
  for (auto start = text.find(open); start != std::string_view::npos;) {
    const auto body = start + open.size();
    const auto end = text.find(close, body);
    if (end == std::string_view::npos) break;
    const auto set = text.substr(body, end - body);
    const auto name = first_text(set, "<Name>", "</Name>");
    if (name && decode_dns_name(xml_unescape(std::string(*name))) == fqdn && set.find("<Type>A</Type>") != std::string_view::npos)
      if (const auto value = first_value(set)) return xml_unescape(std::string(*value));
    start = text.find(open, end + close.size());
  }
  return std::nullopt;
}

std::string xml_unescape(std::string value) {
  const std::vector<std::pair<std::string, std::string>> replacements{{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}};
  for (const auto& [encoded, plain] : replacements) {
    std::size_t position = 0;
    while ((position = value.find(encoded, position)) != std::string::npos) {
      value.replace(position, encoded.size(), plain);
      position += plain.size();
    }
  }
  return value;
}

std::string decode_dns_name(const std::string& value) {
  std::string result;
  for (std::size_t i = 0; i < value.size(); ++i) {
    // A byte is at most \377.
    if (value[i] == '\\' && i + 3 < value.size() && value[i + 1] >= '0' && value[i + 1] <= '3' && is_octal(value[i + 2]) && is_octal(value[i + 3])) {
      result += static_cast<char>((value[i + 1] - '0') * 64 + (value[i + 2] - '0') * 8 + (value[i + 3] - '0'));
      i += 3;
    } else {
      result += value[i];
    }
  }
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return result;
}

}  // namespace manannan::aws
