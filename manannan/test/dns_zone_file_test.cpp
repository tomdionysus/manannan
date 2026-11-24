//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Zone files, read and written. Clauses proved:
// - RFC 1035 5.1: entries, the blank owner, @, relative names under the
//   origin, parentheses across lines, comments, quoted strings, \X and \DDD.
// - RFC 1035 5.1: $ORIGIN; $INCLUDE refused.
// - RFC 2308 4: $TTL as the default TTL.
// - RFC 2181 8: TTLs up to 2^31 - 1.
// - RFC 1035 3.3 and 3.4, RFC 3596 2.4, RFC 2782, RFC 8659 4.1: the
//   presentation forms of A, AAAA, NS, CNAME, PTR, MX, SOA, TXT, SRV, CAA.
// - RFC 3597 5: TYPEnnn and the generic \# form, for any type.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "fixtures/dns_zone.h"
#include "manannan/dns/zone_file.h"

namespace {
using manannan::dns::parse_zone_file;
using manannan::dns::Record;
using manannan::dns::to_zone_text;
using manannan::dns::type_from_text;
using manannan::dns::type_to_text;
using manannan::dns::ZoneFileLimits;
using manannan::test::a_rdata;
using manannan::test::aaaa_rdata;
using manannan::test::dns_name;
using manannan::test::mx_rdata;
using manannan::test::name_rdata;
using manannan::test::record;
using manannan::test::srv_rdata;
namespace rr_type = manannan::dns::rr_type;

std::vector<Record> parse(const std::string& text, const char* origin = "example.com") { return parse_zone_file(text, dns_name(origin)); }

void expect_refused(const std::string& text, const std::string& fragment, const ZoneFileLimits& limits = {}) {
  try {
    (void)parse_zone_file(text, dns_name("example.com"), limits);
    ADD_FAILURE() << "read a zone file that should be refused: " << fragment;
  } catch (const std::invalid_argument& e) {
    EXPECT_NE(std::string(e.what()).find(fragment), std::string::npos) << e.what();
  }
}

const char* const sample = R"($TTL 1h
$ORIGIN example.com.
@   IN  SOA ns1 admin.example.com. (
            2026112401 ; serial
            2h         ; refresh
            15m        ; retry
            2w         ; expire
            300 )      ; minimum
    IN  NS  ns1
        NS  ns2.example.com.
    300 MX  10 mail
ns1     A     192.0.2.1
        AAAA  2001:db8::1
www 60 IN A 192.0.2.10
    IN 60 A 192.0.2.11
txt     TXT   "hello world" plain "with \"quotes\" and \\" "\000\255"
_sip._udp SRV 0 5 5060 sip
alias   CNAME www
1.2     PTR   www.example.com.
@       CAA   0 issue "ca.example.net"
$ORIGIN sub.example.com.
host    A     192.0.2.20
)";

TEST(DnsZoneFile, ReadsEntriesRfc1035Section5_1) {
  const std::vector<Record> records = parse(sample);
  std::vector<std::uint8_t> soa = name_rdata("ns1.example.com");
  const std::vector<std::uint8_t> admin = name_rdata("admin.example.com");
  soa.insert(soa.end(), admin.begin(), admin.end());
  for (const std::uint32_t value : {2026112401u, 7200u, 900u, 1209600u, 300u}) manannan::test::put32(soa, value);
  const std::vector<std::uint8_t> txt = [] {
    std::vector<std::uint8_t> out;
    for (const std::string& s : {std::string("hello world"), std::string("plain"), std::string("with \"quotes\" and \\"), std::string("\0\xff", 2)}) {
      out.push_back(static_cast<std::uint8_t>(s.size()));
      out.insert(out.end(), s.begin(), s.end());
    }
    return out;
  }();
  const std::vector<Record> expected{
      record("example.com", rr_type::soa, 3600, soa),
      record("example.com", rr_type::ns, 3600, name_rdata("ns1.example.com")),
      record("example.com", rr_type::ns, 3600, name_rdata("ns2.example.com")),
      record("example.com", rr_type::mx, 300, mx_rdata(10, "mail.example.com")),
      record("ns1.example.com", rr_type::a, 3600, a_rdata(1)),
      record("ns1.example.com", rr_type::aaaa, 3600, aaaa_rdata(1)),
      record("www.example.com", rr_type::a, 60, a_rdata(10)),
      record("www.example.com", rr_type::a, 60, a_rdata(11)),
      record("txt.example.com", rr_type::txt, 3600, txt),
      record("_sip._udp.example.com", rr_type::srv, 3600, srv_rdata(0, 5, 5060, "sip.example.com")),
      record("alias.example.com", rr_type::cname, 3600, name_rdata("www.example.com")),
      record("1.2.example.com", rr_type::ptr, 3600, name_rdata("www.example.com")),
      record("example.com", rr_type::caa, 3600, {0, 5, 'i', 's', 's', 'u', 'e', 'c', 'a', '.', 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'n', 'e', 't'}),
      record("host.sub.example.com", rr_type::a, 3600, a_rdata(20)),
  };
  ASSERT_EQ(records.size(), expected.size());
  for (std::size_t i = 0; i < records.size(); ++i) EXPECT_EQ(records[i], expected[i]) << i << ": " << to_zone_text(records[i]);
}

TEST(DnsZoneFile, TtlsRfc2308Section4) {
  // Without $TTL, a record takes the last TTL stated.
  std::vector<Record> records = parse("a 100 A 192.0.2.1\nb A 192.0.2.2\n");
  EXPECT_EQ(records[1].ttl, 100u);
  // $TTL wins over the last TTL stated.
  records = parse("a 100 A 192.0.2.1\n$TTL 50\nb A 192.0.2.2\n");
  EXPECT_EQ(records[1].ttl, 50u);
  records = parse("a 1w2d3h4m5s A 192.0.2.1\nb 1H30 A 192.0.2.1\nc 2147483647 A 192.0.2.1\n");
  EXPECT_EQ(records[0].ttl, 604800u + 2 * 86400u + 3 * 3600u + 4 * 60u + 5u);
  EXPECT_EQ(records[1].ttl, 3630u);
  EXPECT_EQ(records[2].ttl, 2147483647u);
  expect_refused("a A 192.0.2.1\n", "line 1: no TTL");
  expect_refused("a 2147483648 A 192.0.2.1\n", "TTL out of range");
  expect_refused("a 9999999999999 A 192.0.2.1\n", "TTL out of range");
  expect_refused("a 3000000w A 192.0.2.1\n", "TTL out of range");
  expect_refused("a 2147483647s1 A 192.0.2.1\n", "TTL out of range");
  expect_refused("a 5x A 192.0.2.1\n", "bad TTL");
  expect_refused("$TTL h\n", "bad TTL");
  expect_refused("a 1hh A 192.0.2.1\n", "bad TTL");
  expect_refused("$TTL \"5\"\n", "bad TTL");
}

TEST(DnsZoneFile, NamesRfc1035Section5_1) {
  std::vector<Record> records = parse("$TTL 1\n@ A 192.0.2.1\nWWW A 192.0.2.1\nabs.example.net. A 192.0.2.1\ndot\\. A 192.0.2.1\nback\\\\. A 192.0.2.1\n");
  EXPECT_EQ(records[0].name, dns_name("example.com"));
  EXPECT_EQ(records[1].name.to_text(), "WWW.example.com.");
  EXPECT_EQ(records[2].name, dns_name("abs.example.net"));
  EXPECT_EQ(records[3].name.to_text(), "dot\\..example.com.");  // an escaped dot is part of the label
  EXPECT_EQ(records[4].name.to_text(), "back\\\\.");            // an escaped backslash, then the final dot
  records = parse("$TTL 1\nwww A 192.0.2.1\n", ".");
  EXPECT_EQ(records[0].name, dns_name("www"));
  records = parse("$TTL 1\n$ORIGIN sub\nwww A 192.0.2.1\n");  // a relative $ORIGIN
  EXPECT_EQ(records[0].name, dns_name("www.sub.example.com"));
  expect_refused("$TTL 1\n\"quoted\" A 192.0.2.1\n", "line 2: a name cannot be quoted");
  expect_refused("$TTL 1\n" + std::string(64, 'a') + " A 192.0.2.1\n", "bad name");
  expect_refused("$TTL 1\n  A 192.0.2.1\n", "no owner to carry over");
}

TEST(DnsZoneFile, DirectivesRfc1035Section5_1) {
  expect_refused("$INCLUDE other.zone\n", "$INCLUDE is not supported");
  expect_refused("$include other.zone\n", "$INCLUDE is not supported");
  expect_refused("$TTL 1 2\n", "$TTL takes one value");
  expect_refused("$ORIGIN\n", "$ORIGIN takes one value");
  expect_refused("$GENERATE 1-2\n", "unknown directive $GENERATE");
}

TEST(DnsZoneFile, LexicalFaults) {
  expect_refused("$TTL 1\na A (192.0.2.1\n", "line 3: unclosed parenthesis");
  expect_refused("$TTL 1\na A ((192.0.2.1))\n", "line 2: nested parenthesis");
  expect_refused("$TTL 1\na A 192.0.2.1)\n", "unmatched parenthesis");
  expect_refused("$TTL 1\na TXT \"open\n", "unclosed quote");
  expect_refused("$TTL 1\na TXT \"open", "unclosed quote");
  expect_refused("$TTL 1\na TXT end\\\n", "a backslash ends the line");
  expect_refused("$TTL 1\na TXT end\\", "a backslash ends the line");
  // Comments and blank lines are nothing; escaped specials are characters.
  const std::vector<Record> records = parse("; comment\n\n   ; indented comment\n$TTL 1\na TXT semi\\;colon paren\\( \"quote\\\"\"\n");
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records[0].rdata, (std::vector<std::uint8_t>{10,  's', 'e', 'm', 'i', ';', 'c', 'o', 'l', 'o', 'n', 6,  'p',
                                                         'a', 'r', 'e', 'n', '(', 6,   'q', 'u', 'o', 't', 'e', '"'}));
}

TEST(DnsZoneFile, ClassAndType) {
  std::vector<Record> records = parse("$TTL 1\na in a 192.0.2.1\nb CLASS1 TYPE1 192.0.2.1\nc 5 in A 192.0.2.1\n");
  EXPECT_EQ(records.size(), 3u);
  EXPECT_EQ(records[1].type, rr_type::a);
  EXPECT_EQ(records[2].ttl, 5u);
  expect_refused("$TTL 1\na CH A 192.0.2.1\n", "unknown type or class CH");
  expect_refused("$TTL 1\na IN IN A 192.0.2.1\n", "unknown type or class IN");
  expect_refused("$TTL 1\na 5 5 A 192.0.2.1\n", "unknown type or class 5");
  expect_refused("$TTL 1\na 5 IN\n", "no type");
  expect_refused("$TTL 1\na \"A\" 192.0.2.1\n", "unknown type or class A");
  expect_refused("$TTL 1\na HINFO \"x\" \"y\"\n", "unknown type or class HINFO");
  expect_refused("$TTL 1\na TYPE13 \"x\" \"y\"\n", "TYPE13 is read only in the generic form");
}

TEST(DnsZoneFile, TypeNamesRfc3597Section5) {
  EXPECT_EQ(type_from_text("mx"), rr_type::mx);
  EXPECT_EQ(type_from_text("Caa"), rr_type::caa);
  EXPECT_EQ(type_from_text("TYPE65535"), 65535);
  EXPECT_EQ(type_from_text("type0"), 0);
  for (const char* bad : {"TYPE", "TYPE65536", "TYPE123456", "TYPE1x", "TYPX1", "BOGUS", ""}) EXPECT_FALSE(type_from_text(bad)) << bad;
  EXPECT_EQ(type_to_text(rr_type::srv), "SRV");
  EXPECT_EQ(type_to_text(65280), "TYPE65280");
}

TEST(DnsZoneFile, GenericFormRfc3597Section5) {
  std::vector<Record> records = parse("$TTL 1\na TYPE65280 \\# 4 0102 0304\nb A \\# 4 C0000201\nc TYPE99 \\# 0\n");
  EXPECT_EQ(records[0].rdata, (std::vector<std::uint8_t>{1, 2, 3, 4}));
  EXPECT_EQ(records[1].rdata, a_rdata(1));
  EXPECT_TRUE(records[2].rdata.empty());
  expect_refused("$TTL 1\na TYPE99 \\#\n", "\\# needs a length");
  expect_refused("$TTL 1\na TYPE99 \\# 2 012\n", "bad hex: 012");
  expect_refused("$TTL 1\na TYPE99 \\# 1 zz\n", "bad hex: zz");
  expect_refused("$TTL 1\na TYPE99 \\# 1 \"01\"\n", "bad hex");
  expect_refused("$TTL 1\na TYPE99 \\# 1 0102 03\n", "length does not match");
  expect_refused("$TTL 1\na TYPE99 \\# 3 0102\n", "length does not match");
  expect_refused("$TTL 1\na TYPE99 \\# 65536 00\n", "length out of range");
  expect_refused("$TTL 1\na A \\# 3 010203\n", "RDATA malformed for A");
}

TEST(DnsZoneFile, RdataFaults) {
  expect_refused("$TTL 1\na A 192.0.2\n", "bad address: 192.0.2");
  expect_refused("$TTL 1\na A \"192.0.2.1\"\n", "bad address");
  expect_refused("$TTL 1\na AAAA 192.0.2.1\n", "bad address");
  expect_refused("$TTL 1\na A 192.0.2.1 192.0.2.2\n", "A takes 1 fields");
  expect_refused("$TTL 1\na NS\n", "NS takes 1 fields");
  expect_refused("$TTL 1\na MX mail\n", "MX takes 2 fields");
  expect_refused("$TTL 1\na MX 65536 mail\n", "preference out of range");
  expect_refused("$TTL 1\na MX ten mail\n", "bad preference: ten");
  expect_refused("$TTL 1\na SRV 0 0 99999 sip\n", "SRV field out of range");
  expect_refused("$TTL 1\na SOA ns admin 1 2 3 4\n", "SOA takes 7 fields");
  expect_refused("$TTL 1\na SOA ns admin 99999999999 2 3 4 5\n", "bad serial");
  expect_refused("$TTL 1\na SOA ns admin 1 2 3 4 5x\n", "bad SOA timer");
  expect_refused("$TTL 1\na TXT\n", "TXT needs a string");
  expect_refused("$TTL 1\na TXT " + std::string(256, 'x') + "\n", "longer than 255 octets");
  expect_refused("$TTL 1\na TXT \\25\n", "\\DDD needs three digits");
  expect_refused("$TTL 1\na TXT \\2a5\n", "\\DDD needs three digits");
  expect_refused("$TTL 1\na TXT \\256\n", "\\DDD above 255");
  expect_refused("$TTL 1\na CAA 0 issue\n", "CAA takes 3 fields");
  expect_refused("$TTL 1\na CAA 256 issue x\n", "CAA flags out of range");
  expect_refused("$TTL 1\na CAA 0 is-sue x\n", "bad CAA tag");
  expect_refused("$TTL 1\na CAA 0 \"issue\" x\n", "bad CAA tag");
  std::string many = "$TTL 1\na TXT";
  for (int i = 0; i < 300; ++i) many += " " + std::string(255, 'x');
  expect_refused(many + "\n", "RDATA longer than 65535 octets");
}

TEST(DnsZoneFile, Bounds) {
  expect_refused("$TTL 1\na A 192.0.2.1\nb A 192.0.2.1\n", "line 3: more than 1 records", ZoneFileLimits{.max_records = 1});
  expect_refused(std::string(101, ';'), "longer than 100 octets", ZoneFileLimits{.max_octets = 100});
  EXPECT_TRUE(parse("").empty());
  EXPECT_TRUE(parse("\n\n; nothing\n").empty());
}

TEST(DnsZoneFile, WritesPresentationForm) {
  EXPECT_EQ(to_zone_text(record("www.example.com", rr_type::a, 60, a_rdata(1))), "www.example.com. 60 IN A 192.0.2.1");
  EXPECT_EQ(to_zone_text(record("www.example.com", rr_type::aaaa, 60, aaaa_rdata(1))), "www.example.com. 60 IN AAAA 2001:db8::1");
  EXPECT_EQ(to_zone_text(record("example.com", rr_type::mx, 1, mx_rdata(10, "mail.example.com"))), "example.com. 1 IN MX 10 mail.example.com.");
  EXPECT_EQ(to_zone_text(record("s.example.com", rr_type::srv, 1, srv_rdata(1, 2, 3, "t.example.com"))), "s.example.com. 1 IN SRV 1 2 3 t.example.com.");
  EXPECT_EQ(to_zone_text(record("example.com", rr_type::soa, 1, manannan::test::soa_rdata("ns.example.com", "admin.example.com", 300))),
            "example.com. 1 IN SOA ns.example.com. admin.example.com. 1 7200 900 1209600 300");
  EXPECT_EQ(to_zone_text(record("t.example.com", rr_type::txt, 1, {2, 'a', '"', 0})), "t.example.com. 1 IN TXT \"a\\\"\" \"\"");
  EXPECT_EQ(to_zone_text(record("t.example.com", rr_type::txt, 1, {3, '\\', 0x07, 0xff})), "t.example.com. 1 IN TXT \"\\\\\\007\\255\"");
  EXPECT_EQ(to_zone_text(record("c.example.com", rr_type::caa, 1, {128, 3, 't', 'a', 'g', 'v'})), "c.example.com. 1 IN CAA 128 tag \"v\"");
  // The generic form: unknown types, and RDATA malformed for its type.
  EXPECT_EQ(to_zone_text(record("x.example.com", 65280, 1, {0xab, 0x01})), "x.example.com. 1 IN TYPE65280 \\# 2 ab01");
  EXPECT_EQ(to_zone_text(record("x.example.com", rr_type::a, 1, {1, 2, 3})), "x.example.com. 1 IN A \\# 3 010203");
  EXPECT_EQ(to_zone_text(record("x.example.com", rr_type::txt, 1, {})), "x.example.com. 1 IN TXT \\# 0");
  EXPECT_EQ(to_zone_text(record("x.example.com", rr_type::txt, 1, {5, 'a'})), "x.example.com. 1 IN TXT \\# 2 0561");
  for (const std::vector<std::uint8_t>& caa :
       {std::vector<std::uint8_t>{0}, std::vector<std::uint8_t>{0, 0}, std::vector<std::uint8_t>{0, 5, 'a'}, std::vector<std::uint8_t>{0, 1, '-'}}) {
    EXPECT_NE(to_zone_text(record("x.example.com", rr_type::caa, 1, caa)).find("\\# "), std::string::npos);
  }
}

TEST(DnsZoneFile, WrittenRecordsReadBack) {
  const std::vector<Record> records = parse(sample);
  std::string text;
  for (const Record& r : records) text += to_zone_text(r) + "\n";
  EXPECT_EQ(parse(text, "."), records);
  // Names that need escapes in presentation form.
  const std::vector<Record> odd{record("\\$x.\\@.\\(\\).\\;\\\".\\032\\\\.example", rr_type::txt, 1, {0}), record("a\\.b.example", 65280, 2, {})};
  text.clear();
  for (const Record& r : odd) text += to_zone_text(r) + "\n";
  EXPECT_EQ(parse(text, "."), odd);
}
}  // namespace
