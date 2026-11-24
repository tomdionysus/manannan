//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The message parser, on octets built by hand. Clauses proved:
// - RFC 1035 4.1.1: the header, its flags and counts.
// - RFC 1035 4.1.2 and 4.1.3: questions and resource records.
// - RFC 1035 4.1.4: compression pointers, followed only backwards.
// - RFC 1035 2.3.4: names over 255 octets refused, however compressed.
// - RFC 3597 4: names inside RDATA decompressed for the RFC 1035 types and
//   those RFC 3597 lists; any other type's RDATA kept as it came.
// - RFC 4035 3.2: the AD and CD bits.
// - RFC 6891 5: label types 01 and 10 refused.
// - RFC 6891 6.1.1 to 6.1.3: one OPT record, in the additional section,
//   owned by the root; its payload size, extended RCODE, version, DO and
//   options.
// - RFC 3225 3: the DO bit.

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "fixtures/dns_bytes.h"
#include "manannan/dns/message.h"

namespace {
using manannan::dns::Edns;
using manannan::dns::Message;
using manannan::dns::Name;
using manannan::dns::parse_message;
using manannan::dns::ParseError;
using manannan::dns::ParseLimits;
using manannan::test::DnsBytes;
namespace rr_type = manannan::dns::rr_type;
namespace rr_class = manannan::dns::rr_class;

Name name(const char* text) { return *Name::from_text(text); }

ParseError parse(const DnsBytes& bytes, Message& message, const ParseLimits& limits = {}) { return parse_message(bytes.bytes(), message, limits); }

// A query for www.example.com A IN, whose name sits at offset 12 and whose
// "example.com" suffix sits at offset 16.
DnsBytes query(std::uint16_t an = 0, std::uint16_t ns = 0, std::uint16_t ar = 0) {
  DnsBytes bytes;
  bytes.header(0x1234, 0x0100, 1, an, ns, ar).name("www.example.com").u16(rr_type::a).u16(rr_class::in);
  return bytes;
}

TEST(DnsMessage, QueryRfc1035Section4_1_2) {
  Message message;
  ASSERT_EQ(parse(query(), message), ParseError::none);
  EXPECT_EQ(message.header.id, 0x1234);
  EXPECT_TRUE(message.header.rd);
  EXPECT_FALSE(message.header.qr);
  ASSERT_EQ(message.questions.size(), 1u);
  EXPECT_EQ(message.questions[0].name, name("www.example.com"));
  EXPECT_EQ(message.questions[0].type, rr_type::a);
  EXPECT_EQ(message.questions[0].klass, rr_class::in);
  EXPECT_TRUE(message.answers.empty());
  EXPECT_FALSE(message.edns);
}

TEST(DnsMessage, HeaderFlagsRfc1035Section4_1_1AndRfc4035Section3_2) {
  Message message;
  // QR, opcode 5, AA, TC, RD; RA, Z, AD, CD, RCODE 9.
  DnsBytes all;
  all.header(0xbeef, 0xaff9, 0);
  ASSERT_EQ(parse(all, message), ParseError::none);
  EXPECT_EQ(message.header.id, 0xbeef);
  EXPECT_TRUE(message.header.qr);
  EXPECT_EQ(message.header.opcode, 5);
  EXPECT_TRUE(message.header.aa);
  EXPECT_TRUE(message.header.tc);
  EXPECT_TRUE(message.header.rd);
  EXPECT_TRUE(message.header.ra);
  EXPECT_TRUE(message.header.z);
  EXPECT_TRUE(message.header.ad);
  EXPECT_TRUE(message.header.cd);
  EXPECT_EQ(message.header.rcode, 9);
  ASSERT_EQ(parse(DnsBytes().header(1, 0, 0), message), ParseError::none);
  EXPECT_EQ(message.header, manannan::dns::Header{.id = 1});
}

TEST(DnsMessage, ShortAndLongMessages) {
  Message message;
  message.header.id = 7;
  DnsBytes eleven;
  for (int i = 0; i < 11; ++i) eleven.u8(0xff);
  EXPECT_EQ(parse(eleven, message), ParseError::short_header);
  EXPECT_EQ(message.header.id, 0);  // reset

  std::vector<std::uint8_t> huge(manannan::dns::max_message_octets + 1, 0);
  huge[0] = 0xab;
  EXPECT_EQ(parse_message(huge, message), ParseError::too_long);
  EXPECT_EQ(message.header.id, 0xab00);
}

TEST(DnsMessage, KeepsTheHeaderWhenRefusing) {
  Message message;
  EXPECT_EQ(parse(DnsBytes().header(0x4242, 0x0100, 1).raw({3, 'w'}), message), ParseError::truncated);
  EXPECT_EQ(message.header.id, 0x4242);
  EXPECT_TRUE(message.header.rd);
}

TEST(DnsMessage, TruncatedFields) {
  Message message;
  EXPECT_EQ(parse(DnsBytes().header(1, 0, 1), message), ParseError::truncated);                                       // no question
  EXPECT_EQ(parse(DnsBytes().header(1, 0, 1).name("a").u16(1), message), ParseError::truncated);                      // half a question
  EXPECT_EQ(parse(DnsBytes().header(1, 0, 1).raw({0xc0}), message), ParseError::truncated);                           // half a pointer
  EXPECT_EQ(parse(query(1), message), ParseError::truncated);                                                         // no answer
  EXPECT_EQ(parse(query(1).pointer(12).u16(1).u16(1).u32(0), message), ParseError::truncated);                        // no RDLENGTH
  EXPECT_EQ(parse(query(1).pointer(12).u16(1).u16(1).u32(0).u16(4).raw({1, 2, 3}), message), ParseError::truncated);  // short RDATA
}

TEST(DnsMessage, CompressionRfc1035Section4_1_4) {
  Message message;
  // The answer's owner is a pointer to the question; the CNAME's target is
  // "mail" and a pointer to "example.com".
  DnsBytes bytes = query(1);
  bytes.pointer(12).u16(rr_type::cname).u16(rr_class::in).u32(300).u16(7).raw({4, 'm', 'a', 'i', 'l'}).pointer(16);
  ASSERT_EQ(parse(bytes, message), ParseError::none);
  ASSERT_EQ(message.answers.size(), 1u);
  EXPECT_EQ(message.answers[0].name, name("www.example.com"));
  EXPECT_EQ(message.answers[0].ttl, 300u);
  EXPECT_EQ(message.answers[0].rdata, name("mail.example.com").wire());
}

TEST(DnsMessage, PointerChainsMoveBackwards) {
  Message message;
  // A second question "x" + pointer to the first's suffix, then an answer
  // whose owner points at the second question: two jumps, each backwards.
  DnsBytes bytes;
  bytes.header(1, 0, 2, 1).name("www.example.com").u16(1).u16(1);  // offsets 12 to 32
  bytes.raw({1, 'x'}).pointer(16).u16(1).u16(1);                   // name at 33
  bytes.pointer(33).u16(1).u16(1).u32(0).u16(4).raw({192, 0, 2, 1});
  ASSERT_EQ(parse(bytes, message), ParseError::none);
  EXPECT_EQ(message.questions[1].name, name("x.example.com"));
  EXPECT_EQ(message.answers[0].name, name("x.example.com"));
  EXPECT_EQ(message.answers[0].rdata, (std::vector<std::uint8_t>{192, 0, 2, 1}));
}

TEST(DnsMessage, RefusesPointersThatDoNotPointBack) {
  Message message;
  EXPECT_EQ(parse(DnsBytes().header(1, 0, 1).pointer(12).u16(1).u16(1), message), ParseError::bad_pointer);        // to itself
  EXPECT_EQ(parse(DnsBytes().header(1, 0, 1).pointer(14).u8(0).u16(1).u16(1), message), ParseError::bad_pointer);  // forwards
  EXPECT_EQ(parse(DnsBytes().header(1, 0, 1).pointer(0).u16(1).u16(1), message), ParseError::bad_pointer);         // into the header
  // A loop: the second name's label is followed by a pointer back to itself.
  DnsBytes loop;
  loop.header(1, 0, 2).name("a").u16(1).u16(1).raw({1, 'b'}).pointer(19).u16(1).u16(1);
  EXPECT_EQ(parse(loop, message), ParseError::bad_pointer);
  // A pointer to a later name, though that name is well formed.
  DnsBytes pair;
  pair.header(1, 0, 2).raw({1, 'a'}).pointer(20).u16(1).u16(1).raw({1, 'b'}).pointer(12).u16(1).u16(1);
  EXPECT_EQ(parse(pair, message), ParseError::bad_pointer);
}

TEST(DnsMessage, RefusesExtendedLabelTypesRfc6891Section5) {
  Message message;
  EXPECT_EQ(parse(DnsBytes().header(1, 0, 1).raw({0x41, 0}).u16(1).u16(1), message), ParseError::bad_label);
  EXPECT_EQ(parse(DnsBytes().header(1, 0, 1).raw({0x80, 0}).u16(1).u16(1), message), ParseError::bad_label);
}

TEST(DnsMessage, RefusesNamesOver255OctetsRfc1035Section2_3_4) {
  Message message;
  const std::string label63(63, 'a');
  const std::string fits = label63 + "." + label63 + "." + label63 + "." + std::string(61, 'a');
  EXPECT_EQ(parse(DnsBytes().header(1, 0, 1).name(fits).u16(1).u16(1), message), ParseError::none);
  // Built from pieces by compression: a one-octet label ahead of the full name.
  DnsBytes longer;
  longer.header(1, 0, 2).name(fits).u16(1).u16(1);
  longer.u8(1).u8('b').pointer(12).u16(1).u16(1);
  EXPECT_EQ(parse(longer, message), ParseError::name_too_long);
}

TEST(DnsMessage, RecordCountLimit) {
  Message message;
  EXPECT_EQ(parse(query(), message, ParseLimits{.max_records = 0}), ParseError::too_many_records);
  EXPECT_EQ(parse(query(), message, ParseLimits{.max_records = 1}), ParseError::none);
  // Counts the header claims are checked before anything is read.
  EXPECT_EQ(parse(DnsBytes().header(1, 0, 0xffff, 0xffff, 0xffff, 0xffff), message), ParseError::too_many_records);
}

TEST(DnsMessage, RefusesTrailingOctets) {
  Message message;
  EXPECT_EQ(parse(query().u8(0), message), ParseError::trailing_octets);
}

TEST(DnsMessage, DecompressesRdataRfc3597Section4) {
  Message message;
  // SOA: two compressed names and twenty octets.
  DnsBytes soa = query(1);
  soa.pointer(16).u16(rr_type::soa).u16(1).u32(3600).u16(2 + 6 + 2 + 20).pointer(16).raw({5, 'a', 'd', 'm', 'i', 'n'}).pointer(16);
  for (std::uint8_t i = 0; i < 20; ++i) soa.u8(i);
  ASSERT_EQ(parse(soa, message), ParseError::none);
  std::vector<std::uint8_t> expected = name("example.com").wire();
  const std::vector<std::uint8_t> admin = name("admin.example.com").wire();
  expected.insert(expected.end(), admin.begin(), admin.end());
  for (std::uint8_t i = 0; i < 20; ++i) expected.push_back(i);
  EXPECT_EQ(message.answers[0].rdata, expected);

  // MX: a preference and a name.
  DnsBytes mx = query(1);
  mx.pointer(16).u16(rr_type::mx).u16(1).u32(0).u16(4).u16(10).pointer(12);
  ASSERT_EQ(parse(mx, message), ParseError::none);
  expected = {0, 10};
  const std::vector<std::uint8_t> www = name("www.example.com").wire();
  expected.insert(expected.end(), www.begin(), www.end());
  EXPECT_EQ(message.answers[0].rdata, expected);

  // SRV: priority, weight, port and a name.
  DnsBytes srv = query(1);
  srv.pointer(16).u16(rr_type::srv).u16(1).u32(0).u16(8).u16(1).u16(2).u16(53).pointer(16);
  ASSERT_EQ(parse(srv, message), ParseError::none);
  expected = {0, 1, 0, 2, 0, 53};
  const std::vector<std::uint8_t> example = name("example.com").wire();
  expected.insert(expected.end(), example.begin(), example.end());
  EXPECT_EQ(message.answers[0].rdata, expected);

  // Every type with names, one each.
  for (const std::uint16_t type : {rr_type::ns, rr_type::md, rr_type::mf, rr_type::cname, rr_type::mb, rr_type::mg, rr_type::mr, rr_type::ptr}) {
    DnsBytes one = query(1);
    one.pointer(16).u16(type).u16(1).u32(0).u16(2).pointer(12);
    ASSERT_EQ(parse(one, message), ParseError::none) << type;
    EXPECT_EQ(message.answers[0].rdata, www) << type;
  }
  for (const std::uint16_t type : {rr_type::minfo, rr_type::rp}) {
    DnsBytes two = query(1);
    two.pointer(16).u16(type).u16(1).u32(0).u16(4).pointer(12).pointer(16);
    ASSERT_EQ(parse(two, message), ParseError::none) << type;
    EXPECT_EQ(message.answers[0].rdata.size(), www.size() + example.size()) << type;
  }
  for (const std::uint16_t type : {rr_type::afsdb, rr_type::rt}) {
    DnsBytes pref = query(1);
    pref.pointer(16).u16(type).u16(1).u32(0).u16(4).u16(1).pointer(12);
    ASSERT_EQ(parse(pref, message), ParseError::none) << type;
    EXPECT_EQ(message.answers[0].rdata.size(), 2 + www.size()) << type;
  }
}

TEST(DnsMessage, KeepsUnknownRdataAsItCameRfc3597Section4) {
  Message message;
  // TXT and an unassigned type, holding octets that look like a pointer.
  for (const std::uint16_t type : {rr_type::txt, std::uint16_t{65280}}) {
    DnsBytes bytes = query(1);
    bytes.pointer(12).u16(type).u16(1).u32(0).u16(3).raw({2, 0xc0, 0x0c});
    ASSERT_EQ(parse(bytes, message), ParseError::none);
    EXPECT_EQ(message.answers[0].type, type);
    EXPECT_EQ(message.answers[0].rdata, (std::vector<std::uint8_t>{2, 0xc0, 0x0c}));
  }
  DnsBytes empty = query(1);
  empty.pointer(12).u16(rr_type::a).u16(1).u32(0).u16(0);
  ASSERT_EQ(parse(empty, message), ParseError::none);
  EXPECT_TRUE(message.answers[0].rdata.empty());
}

TEST(DnsMessage, RefusesRdataThatDoesNotMatchItsLength) {
  Message message;
  // A name that runs past RDLENGTH, then one that stops short of it.
  EXPECT_EQ(parse(query(1).pointer(12).u16(rr_type::ns).u16(1).u32(0).u16(1).pointer(12), message), ParseError::bad_rdata);
  EXPECT_EQ(parse(query(1).pointer(12).u16(rr_type::ns).u16(1).u32(0).u16(3).pointer(12).u8(0), message), ParseError::bad_rdata);
  EXPECT_EQ(parse(query(1).pointer(12).u16(rr_type::ns).u16(1).u32(0).u16(0), message), ParseError::bad_rdata);
  // Fixed fields that do not fit.
  EXPECT_EQ(parse(query(1).pointer(12).u16(rr_type::mx).u16(1).u32(0).u16(1).u8(0), message), ParseError::bad_rdata);
  EXPECT_EQ(parse(query(1).pointer(12).u16(rr_type::soa).u16(1).u32(0).u16(4).pointer(12).pointer(12), message), ParseError::bad_rdata);
  // Errors other than running out are passed on as they are: the RDATA, at
  // offset 45, points to itself.
  EXPECT_EQ(parse(query(1).pointer(12).u16(rr_type::ns).u16(1).u32(0).u16(2).pointer(45), message), ParseError::bad_pointer);
}

TEST(DnsMessage, EdnsRfc6891Section6_1) {
  Message message;
  DnsBytes bytes = query(0, 0, 1);
  // Payload 1232, extended RCODE 1, version 0, DO and one other flag; a
  // cookie option and an empty one.
  bytes.u8(0).u16(rr_type::opt).u16(1232).u32(0x01008001).u16(4 + 8 + 4).u16(10).u16(8);
  for (std::uint8_t i = 0; i < 8; ++i) bytes.u8(i);
  bytes.u16(12).u16(0);
  ASSERT_EQ(parse(bytes, message), ParseError::none);
  ASSERT_TRUE(message.edns);
  const Edns& edns = *message.edns;
  EXPECT_EQ(edns.udp_payload_size, 1232);
  EXPECT_EQ(edns.extended_rcode, 1);
  EXPECT_EQ(edns.version, 0);
  EXPECT_TRUE(edns.dnssec_ok);
  EXPECT_EQ(edns.z, 1);
  ASSERT_EQ(edns.options.size(), 2u);
  EXPECT_EQ(edns.options[0].code, 10);
  EXPECT_EQ(edns.options[0].data, (std::vector<std::uint8_t>{0, 1, 2, 3, 4, 5, 6, 7}));
  EXPECT_EQ(edns.options[1].code, 12);
  EXPECT_TRUE(edns.options[1].data.empty());
  EXPECT_TRUE(message.additionals.empty());
  // BADVERS is 16: four bits in the header, the rest in OPT (RFC 6891 6.1.3).
  EXPECT_EQ(manannan::dns::full_rcode(message), manannan::dns::rcode::badvers);
}

TEST(DnsMessage, RefusesMisplacedOrMalformedOptRfc6891Section6_1_1) {
  Message message;
  auto opt = [](DnsBytes bytes) { return bytes.u8(0).u16(rr_type::opt).u16(512).u32(0).u16(0); };
  EXPECT_EQ(parse(opt(query(1)), message), ParseError::bad_opt);                                                          // in the answer section
  EXPECT_EQ(parse(opt(query(0, 1)), message), ParseError::bad_opt);                                                       // in the authority section
  EXPECT_EQ(parse(opt(opt(query(0, 0, 2))), message), ParseError::bad_opt);                                               // twice
  EXPECT_EQ(parse(query(0, 0, 1).pointer(12).u16(rr_type::opt).u16(512).u32(0).u16(0), message), ParseError::bad_opt);    // not the root's
  EXPECT_EQ(parse(query(0, 0, 1).u8(0).u16(rr_type::opt).u16(512).u32(0).u16(2).u16(10), message), ParseError::bad_opt);  // half an option header
  EXPECT_EQ(parse(query(0, 0, 1).u8(0).u16(rr_type::opt).u16(512).u32(0).u16(5).u16(10).u16(2).u8(1), message), ParseError::bad_opt);  // short option
}

TEST(DnsMessage, OptBesideOtherAdditionalRecords) {
  Message message;
  DnsBytes bytes = query(0, 0, 2);
  bytes.u8(0).u16(rr_type::opt).u16(4096).u32(0).u16(0);
  bytes.pointer(12).u16(rr_type::a).u16(1).u32(60).u16(4).raw({192, 0, 2, 7});
  ASSERT_EQ(parse(bytes, message), ParseError::none);
  ASSERT_TRUE(message.edns);
  EXPECT_FALSE(message.edns->dnssec_ok);
  ASSERT_EQ(message.additionals.size(), 1u);
  EXPECT_EQ(manannan::dns::full_rcode(message), 0);
}

TEST(DnsMessage, AnswersAndAuthorities) {
  Message message;
  DnsBytes bytes = query(1, 1);
  bytes.pointer(12).u16(rr_type::a).u16(1).u32(60).u16(4).raw({192, 0, 2, 1});
  bytes.pointer(16).u16(rr_type::ns).u16(1).u32(60).u16(5).raw({2, 'n', 's'}).pointer(16);
  ASSERT_EQ(parse(bytes, message), ParseError::none);
  ASSERT_EQ(message.answers.size(), 1u);
  ASSERT_EQ(message.authorities.size(), 1u);
  EXPECT_EQ(message.authorities[0].rdata, name("ns.example.com").wire());
}

TEST(DnsMessage, DescribesEveryError) {
  std::set<std::string> seen;
  for (int e = static_cast<int>(ParseError::none); e <= static_cast<int>(ParseError::trailing_octets); ++e) {
    const std::string text(manannan::dns::describe(static_cast<ParseError>(e)));
    EXPECT_FALSE(text.empty());
    EXPECT_TRUE(seen.insert(text).second) << text;
  }
}
}  // namespace
