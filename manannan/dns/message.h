//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// A DNS message (RFC 1035 4.1) and its parser. The parser reads untrusted
// bytes: it is bounded in size, record count, name length and compression,
// allocates no more than the input can justify, and reports what it refused
// as a ParseError rather than throwing. Names inside the RDATA of the types
// RFC 3597 4 lists are decompressed, so every Record's RDATA is
// self-contained and can be written into any message.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "manannan/dns/name.h"

namespace manannan::dns {

inline constexpr std::size_t header_octets = 12;
inline constexpr std::size_t max_message_octets = 65535;

// Record types and classes this code names; any other value passes through.
namespace rr_type {
inline constexpr std::uint16_t a = 1;
inline constexpr std::uint16_t ns = 2;
inline constexpr std::uint16_t md = 3;
inline constexpr std::uint16_t mf = 4;
inline constexpr std::uint16_t cname = 5;
inline constexpr std::uint16_t soa = 6;
inline constexpr std::uint16_t mb = 7;
inline constexpr std::uint16_t mg = 8;
inline constexpr std::uint16_t mr = 9;
inline constexpr std::uint16_t ptr = 12;
inline constexpr std::uint16_t minfo = 14;
inline constexpr std::uint16_t mx = 15;
inline constexpr std::uint16_t txt = 16;
inline constexpr std::uint16_t rp = 17;
inline constexpr std::uint16_t afsdb = 18;
inline constexpr std::uint16_t rt = 21;
inline constexpr std::uint16_t aaaa = 28;
inline constexpr std::uint16_t srv = 33;
inline constexpr std::uint16_t opt = 41;
inline constexpr std::uint16_t any = 255;
inline constexpr std::uint16_t caa = 257;
}  // namespace rr_type

namespace rr_class {
inline constexpr std::uint16_t in = 1;
}  // namespace rr_class

namespace rcode {
inline constexpr std::uint16_t noerror = 0;
inline constexpr std::uint16_t formerr = 1;
inline constexpr std::uint16_t servfail = 2;
inline constexpr std::uint16_t nxdomain = 3;
inline constexpr std::uint16_t notimp = 4;
inline constexpr std::uint16_t refused = 5;
inline constexpr std::uint16_t badvers = 16;  // RFC 6891 9
}  // namespace rcode

// RFC 1035 4.1.1, with AD and CD from RFC 4035 3.2. `rcode` is the low four
// bits; the high eight are in Edns::extended_rcode (RFC 6891 6.1.3).
struct Header {
  std::uint16_t id = 0;
  bool qr = false;
  std::uint8_t opcode = 0;
  bool aa = false;
  bool tc = false;
  bool rd = false;
  bool ra = false;
  bool z = false;
  bool ad = false;
  bool cd = false;
  std::uint8_t rcode = 0;

  friend bool operator==(const Header&, const Header&) = default;
};

struct Question {
  Name name;
  std::uint16_t type = 0;
  std::uint16_t klass = 0;

  friend bool operator==(const Question&, const Question&) = default;
};

struct Record {
  Name name;
  std::uint16_t type = 0;
  std::uint16_t klass = 0;
  std::uint32_t ttl = 0;
  std::vector<std::uint8_t> rdata;  // uncompressed

  friend bool operator==(const Record&, const Record&) = default;
};

struct EdnsOption {
  std::uint16_t code = 0;
  std::vector<std::uint8_t> data;

  friend bool operator==(const EdnsOption&, const EdnsOption&) = default;
};

// The OPT pseudo-record (RFC 6891 6.1), lifted out of the additional section.
struct Edns {
  std::uint16_t udp_payload_size = 0;
  std::uint8_t extended_rcode = 0;
  std::uint8_t version = 0;
  bool dnssec_ok = false;  // DO, RFC 3225
  std::uint16_t z = 0;     // the other fifteen flag bits, DO's bit clear
  std::vector<EdnsOption> options{};

  friend bool operator==(const Edns&, const Edns&) = default;
};

struct Message {
  Header header;
  std::vector<Question> questions;
  std::vector<Record> answers;
  std::vector<Record> authorities;
  std::vector<Record> additionals;  // without the OPT record
  std::optional<Edns> edns;

  friend bool operator==(const Message&, const Message&) = default;
};

// The full twelve-bit response code: the header's four bits and the OPT
// record's eight (RFC 6891 6.1.3).
std::uint16_t full_rcode(const Message& message);

// Whether uncompressed RDATA is well formed: an A holds four octets, an
// AAAA sixteen, and a type that holds names (RFC 3597 4) holds valid names
// and fixed fields that fill it exactly. Other types are taken as they are.
bool rdata_well_formed(std::uint16_t type, std::span<const std::uint8_t> rdata);
// The first name in a well-formed RDATA of a type that holds names: the
// target of NS, CNAME, PTR, MX and SRV, the primary server of SOA.
std::optional<Name> rdata_name(std::uint16_t type, std::span<const std::uint8_t> rdata);

enum class ParseError {
  none,
  short_header,      // fewer than twelve octets: nothing to answer
  too_long,          // more than 65535 octets
  truncated,         // the data ends inside a field
  bad_label,         // a label type other than 00 or 11 (RFC 6891 5)
  name_too_long,     // over 255 octets (RFC 1035 2.3.4)
  bad_pointer,       // a compression pointer that does not point strictly back
  too_many_records,  // over ParseLimits::max_records
  bad_rdata,         // RDATA whose names do not fill RDLENGTH exactly
  bad_opt,           // an OPT record misplaced, repeated or malformed
  trailing_octets,   // data after the last counted record
};

std::string_view describe(ParseError error);

struct ParseLimits {
  // Questions and records of every section together, OPT included.
  std::size_t max_records = 1024;
};

// Reads `data` into `out`. On any error but short_header, `out.header` holds
// the header as read, so the caller can answer FORMERR to its ID.
ParseError parse_message(std::span<const std::uint8_t> data, Message& out, const ParseLimits& limits = {});

}  // namespace manannan::dns
