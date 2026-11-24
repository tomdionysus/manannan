//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Builds a DNS message within a size limit. Questions and records are added
// section by section, in order; one that would pass the limit is refused
// and leaves the message as it was, so the caller decides what truncation
// means (RFC 2181 9) and sets TC itself. Owner and question names are
// compressed (RFC 1035 4.1.4); RDATA is written as held, uncompressed,
// which every reader accepts.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "manannan/dns/message.h"

namespace manannan::dns {

class Writer {
 public:
  enum class Section { question, answer, authority, additional };

  // Throws std::invalid_argument unless header_octets <= max_size <= max_message_octets.
  explicit Writer(std::size_t max_size = max_message_octets);

  // The header written by finish(); counts are filled in there.
  Header& header() { return _header; }

  // Each returns false, changing nothing, when the addition would not fit.
  // Adding to a section before the last one used throws std::logic_error,
  // as does a second OPT record.
  bool add_question(const Question& question);
  bool add_record(Section section, const Record& record);
  // The OPT record, in the additional section (RFC 6891 6.1).
  bool add_edns(const Edns& edns);
  // Keeps `octets` free of questions and records, for the OPT record that
  // add_edns writes last; add_edns itself may use them.
  void reserve(std::size_t octets) { _reserved = octets; }

  // A point to return to, so that several records go in together or not at all.
  struct Mark {
    std::size_t size;
    std::size_t names;
    std::array<std::uint16_t, 4> counts;
  };
  Mark mark() const { return Mark{_out.size(), _names_added.size(), _counts}; }
  void rollback(const Mark& mark);

  std::size_t size() const { return _out.size(); }
  std::vector<std::uint8_t> finish();

 private:
  void enter(Section section);
  void put16(std::uint16_t value);
  void put32(std::uint32_t value);
  void put_name(const Name& name);
  bool commit_or_rollback(std::size_t mark, std::size_t names_mark, std::size_t reserved);

  std::size_t _max_size;
  std::size_t _reserved = 0;
  Header _header;
  std::vector<std::uint8_t> _out;
  Section _section = Section::question;
  std::array<std::uint16_t, 4> _counts{};
  bool _has_edns = false;
  // Lowered uncompressed suffixes already written, and where.
  std::unordered_map<std::string, std::uint16_t> _names;
  std::vector<std::string> _names_added;
};

// The whole message, OPT last, or nullopt when it would not fit in `max_size`.
std::optional<std::vector<std::uint8_t>> write_message(const Message& message, std::size_t max_size = max_message_octets);

}  // namespace manannan::dns
