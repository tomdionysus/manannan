//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/dns/message.h"

#include <array>
#include <utility>

namespace manannan::dns {
namespace {

std::uint16_t get16(std::span<const std::uint8_t> data, std::size_t pos) { return static_cast<std::uint16_t>(data[pos] << 8 | data[pos + 1]); }

std::uint32_t get32(std::span<const std::uint8_t> data, std::size_t pos) {
  return static_cast<std::uint32_t>(data[pos]) << 24 | static_cast<std::uint32_t>(data[pos + 1]) << 16 | static_cast<std::uint32_t>(data[pos + 2]) << 8 |
         data[pos + 3];
}

// Reads a possibly compressed name starting at `pos`, whose uncompressed
// part must end by `end`. Each pointer must point before the start of the
// labels that led to it (RFC 1035 4.1.4 says "prior occurrence"), so the
// walk always moves backwards and ends. On success `pos` is just past the
// name as it sits in the message.
ParseError read_name(std::span<const std::uint8_t> data, std::size_t& pos, std::size_t end, Name& out) {
  std::vector<std::uint8_t> wire;
  std::size_t at = pos;
  std::size_t limit = pos;
  bool jumped = false;
  while (true) {
    if (at >= end) return ParseError::truncated;
    const std::uint8_t length = data[at];
    if ((length & 0xc0) == 0xc0) {
      if (at + 1 >= end) return ParseError::truncated;
      const std::size_t target = static_cast<std::size_t>(length & 0x3f) << 8 | data[at + 1];
      if (target >= limit || target < header_octets) return ParseError::bad_pointer;
      if (!jumped) pos = at + 2;
      jumped = true;
      limit = target;
      at = target;
      end = data.size();
      continue;
    }
    if ((length & 0xc0) != 0) return ParseError::bad_label;
    if (at + 1 + length > end) return ParseError::truncated;
    // Room for this label, and for the root's octet after it.
    if (wire.size() + 1 + length + (length == 0 ? 0 : 1) > max_name_octets) return ParseError::name_too_long;
    wire.insert(wire.end(), data.begin() + static_cast<std::ptrdiff_t>(at), data.begin() + static_cast<std::ptrdiff_t>(at + 1 + length));
    at += 1 + length;
    if (length == 0) break;
  }
  if (!jumped) pos = at;
  out = *Name::from_wire(wire);
  return ParseError::none;
}

// The layout of RDATA that holds names, for the types RFC 3597 4 says a
// receiver decompresses: fixed octets, a name, and so on.
enum class Field : std::uint8_t { name, fixed2, fixed6, fixed20 };

std::span<const Field> layout(std::uint16_t type) {
  static constexpr std::array<Field, 1> one_name{Field::name};
  static constexpr std::array<Field, 2> two_names{Field::name, Field::name};
  static constexpr std::array<Field, 3> soa{Field::name, Field::name, Field::fixed20};
  static constexpr std::array<Field, 2> preference_name{Field::fixed2, Field::name};
  static constexpr std::array<Field, 2> srv{Field::fixed6, Field::name};
  switch (type) {
    case rr_type::ns:
    case rr_type::md:
    case rr_type::mf:
    case rr_type::cname:
    case rr_type::mb:
    case rr_type::mg:
    case rr_type::mr:
    case rr_type::ptr:
      return one_name;
    case rr_type::soa:
      return soa;
    case rr_type::minfo:
    case rr_type::rp:
      return two_names;
    case rr_type::mx:
    case rr_type::afsdb:
    case rr_type::rt:
      return preference_name;
    case rr_type::srv:
      return srv;
    default:
      return {};
  }
}

std::size_t fixed_octets(Field field) {
  switch (field) {
    case Field::fixed2:
      return 2;
    case Field::fixed6:
      return 6;
    default:
      return 20;
  }
}

ParseError read_rdata(std::span<const std::uint8_t> data, std::size_t pos, std::size_t end, std::uint16_t type, std::vector<std::uint8_t>& out) {
  const std::span<const Field> fields = layout(type);
  if (fields.empty()) {
    out.assign(data.begin() + static_cast<std::ptrdiff_t>(pos), data.begin() + static_cast<std::ptrdiff_t>(end));
    return ParseError::none;
  }
  for (const Field field : fields) {
    if (field == Field::name) {
      Name name;
      const ParseError error = read_name(data, pos, end, name);
      if (error == ParseError::truncated) return ParseError::bad_rdata;
      if (error != ParseError::none) return error;
      out.insert(out.end(), name.wire().begin(), name.wire().end());
      continue;
    }
    const std::size_t length = fixed_octets(field);
    if (pos + length > end) return ParseError::bad_rdata;
    out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(pos), data.begin() + static_cast<std::ptrdiff_t>(pos + length));
    pos += length;
  }
  return pos == end ? ParseError::none : ParseError::bad_rdata;
}

ParseError read_options(std::span<const std::uint8_t> data, std::size_t pos, std::size_t end, std::vector<EdnsOption>& out) {
  while (pos < end) {
    if (pos + 4 > end) return ParseError::bad_opt;
    const std::size_t length = get16(data, pos + 2);
    if (pos + 4 + length > end) return ParseError::bad_opt;
    out.push_back(EdnsOption{get16(data, pos), std::vector<std::uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(pos + 4),
                                                                         data.begin() + static_cast<std::ptrdiff_t>(pos + 4 + length))});
    pos += 4 + length;
  }
  return ParseError::none;
}

enum class Section { answer, authority, additional };

ParseError read_record(std::span<const std::uint8_t> data, std::size_t& pos, Section section, Message& message) {
  Record record;
  if (const ParseError error = read_name(data, pos, data.size(), record.name); error != ParseError::none) return error;
  if (pos + 10 > data.size()) return ParseError::truncated;
  record.type = get16(data, pos);
  record.klass = get16(data, pos + 2);
  record.ttl = get32(data, pos + 4);
  const std::size_t start = pos + 10;
  const std::size_t end = start + get16(data, pos + 8);
  if (end > data.size()) return ParseError::truncated;
  pos = end;

  if (record.type == rr_type::opt) {
    // RFC 6891 6.1.1: one OPT, in the additional section, owned by the root.
    if (section != Section::additional || message.edns || !record.name.is_root()) return ParseError::bad_opt;
    Edns edns;
    edns.udp_payload_size = record.klass;
    edns.extended_rcode = static_cast<std::uint8_t>(record.ttl >> 24);
    edns.version = static_cast<std::uint8_t>(record.ttl >> 16);
    edns.dnssec_ok = (record.ttl & 0x8000) != 0;
    edns.z = static_cast<std::uint16_t>(record.ttl & 0x7fff);
    if (const ParseError error = read_options(data, start, end, edns.options); error != ParseError::none) return error;
    message.edns = std::move(edns);
    return ParseError::none;
  }

  if (const ParseError error = read_rdata(data, start, end, record.type, record.rdata); error != ParseError::none) return error;
  std::vector<Record>& into = section == Section::answer ? message.answers : section == Section::authority ? message.authorities : message.additionals;
  into.push_back(std::move(record));
  return ParseError::none;
}

}  // namespace

std::uint16_t full_rcode(const Message& message) {
  const std::uint16_t high = message.edns ? static_cast<std::uint16_t>(message.edns->extended_rcode << 4) : 0;
  return static_cast<std::uint16_t>(high | message.header.rcode);
}

std::string_view describe(ParseError error) {
  switch (error) {
    case ParseError::none:
      return "no error";
    case ParseError::short_header:
      return "shorter than a header";
    case ParseError::too_long:
      return "longer than 65535 octets";
    case ParseError::truncated:
      return "data ends inside a field";
    case ParseError::bad_label:
      return "unknown label type";
    case ParseError::name_too_long:
      return "name longer than 255 octets";
    case ParseError::bad_pointer:
      return "compression pointer does not point back";
    case ParseError::too_many_records:
      return "too many records";
    case ParseError::bad_rdata:
      return "RDATA does not match its length";
    case ParseError::bad_opt:
      return "malformed or misplaced OPT record";
    case ParseError::trailing_octets:
      return "data after the last record";
  }
  return "unknown error";  // LCOV_EXCL_LINE
}

ParseError parse_message(std::span<const std::uint8_t> data, Message& out, const ParseLimits& limits) {
  out = Message{};
  if (data.size() < header_octets) return ParseError::short_header;
  Header& header = out.header;
  header.id = get16(data, 0);
  const std::uint8_t flags1 = data[2];
  const std::uint8_t flags2 = data[3];
  header.qr = (flags1 & 0x80) != 0;
  header.opcode = static_cast<std::uint8_t>((flags1 >> 3) & 0x0f);
  header.aa = (flags1 & 0x04) != 0;
  header.tc = (flags1 & 0x02) != 0;
  header.rd = (flags1 & 0x01) != 0;
  header.ra = (flags2 & 0x80) != 0;
  header.z = (flags2 & 0x40) != 0;
  header.ad = (flags2 & 0x20) != 0;
  header.cd = (flags2 & 0x10) != 0;
  header.rcode = static_cast<std::uint8_t>(flags2 & 0x0f);
  if (data.size() > max_message_octets) return ParseError::too_long;

  const std::size_t questions = get16(data, 4);
  const std::array<std::size_t, 3> counts{get16(data, 6), get16(data, 8), get16(data, 10)};
  if (questions + counts[0] + counts[1] + counts[2] > limits.max_records) return ParseError::too_many_records;

  std::size_t pos = header_octets;
  for (std::size_t i = 0; i < questions; ++i) {
    Question question;
    if (const ParseError error = read_name(data, pos, data.size(), question.name); error != ParseError::none) return error;
    if (pos + 4 > data.size()) return ParseError::truncated;
    question.type = get16(data, pos);
    question.klass = get16(data, pos + 2);
    pos += 4;
    out.questions.push_back(std::move(question));
  }
  constexpr std::array<Section, 3> sections{Section::answer, Section::authority, Section::additional};
  for (std::size_t s = 0; s < sections.size(); ++s) {
    for (std::size_t i = 0; i < counts[s]; ++i) {
      if (const ParseError error = read_record(data, pos, sections[s], out); error != ParseError::none) return error;
    }
  }
  return pos == data.size() ? ParseError::none : ParseError::trailing_octets;
}

}  // namespace manannan::dns
