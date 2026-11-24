//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Zones and the catalog. Clauses proved:
// - RFC 1034 3.6.2 and RFC 2181 10.1: a CNAME has no other data beside it.
// - RFC 1035 5.2: one SOA at the apex, and NS there.
// - RFC 2181 5 and 5.2: an RRset holds each record once, at one TTL, the
//   lowest given.
// - RFC 2308 3: a negative answer's TTL is the lower of the SOA's TTL and
//   its MINIMUM.
// - RFC 6895 3.1: meta-types are never held in a zone.
// - RFC 8020: the names between an owner and the apex exist, without data.
// - RFC 1034 4.3.2 step 2: the zone for a name is the nearest enclosing one.

#include "fixtures/dns_zone.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "manannan/dns/zone.h"

namespace {
using manannan::dns::Catalog;
using manannan::dns::Record;
using manannan::dns::Zone;
using manannan::test::a_rdata;
using manannan::test::dns_name;
using manannan::test::name_rdata;
using manannan::test::record;
using manannan::test::soa_rdata;
namespace rr_type = manannan::dns::rr_type;

std::vector<Record> apex(const char* origin = "example.com", std::uint32_t soa_ttl = 3600, std::uint32_t minimum = 300) {
  const std::string o(origin);
  return {record(o, rr_type::soa, soa_ttl, soa_rdata("ns1." + o, "admin." + o, minimum)), record(o, rr_type::ns, 3600, name_rdata("ns1." + o))};
}

std::vector<Record> with(std::vector<Record> records, std::vector<Record> more) {
  records.insert(records.end(), more.begin(), more.end());
  return records;
}

void expect_refused(const std::vector<Record>& records, const std::string& fragment) {
  try {
    Zone zone(dns_name("example.com"), records);
    ADD_FAILURE() << "built a zone that should be refused: " << fragment;
  } catch (const std::invalid_argument& e) {
    EXPECT_NE(std::string(e.what()).find(fragment), std::string::npos) << e.what();
  }
}

TEST(DnsZone, FindsNamesIgnoringCase) {
  const Zone zone(dns_name("example.com"), with(apex(), {record("www.example.com", rr_type::a, 60, a_rdata(1))}));
  EXPECT_EQ(zone.origin(), dns_name("example.com"));
  ASSERT_NE(zone.find(dns_name("WWW.Example.COM")), nullptr);
  EXPECT_NE(zone.find(dns_name("WWW.Example.COM"))->get(rr_type::a), nullptr);
  EXPECT_EQ(zone.find(dns_name("WWW.Example.COM"))->get(rr_type::aaaa), nullptr);
  EXPECT_EQ(zone.find(dns_name("ftp.example.com")), nullptr);
  EXPECT_EQ(zone.soa().type, rr_type::soa);
}

TEST(DnsZone, EmptyNonTerminalsExistRfc8020) {
  const Zone zone(dns_name("example.com"), with(apex(), {record("a.b.c.example.com", rr_type::a, 60, a_rdata(1))}));
  for (const char* name : {"b.c.example.com", "c.example.com"}) {
    ASSERT_NE(zone.find(dns_name(name)), nullptr) << name;
    EXPECT_TRUE(zone.find(dns_name(name))->rrsets.empty()) << name;
  }
  EXPECT_EQ(zone.find(dns_name("x.c.example.com")), nullptr);
}

TEST(DnsZone, RRsetsRfc2181Section5) {
  const Zone zone(
      dns_name("example.com"),
      with(apex(), {record("www.example.com", rr_type::aaaa, 60, manannan::test::aaaa_rdata(1)), record("www.example.com", rr_type::a, 300, a_rdata(1)),
                    record("www.example.com", rr_type::a, 60, a_rdata(2)), record("WWW.example.com", rr_type::a, 120, a_rdata(1))}));
  const Zone::Node* node = zone.find(dns_name("www.example.com"));
  ASSERT_EQ(node->rrsets.size(), 2u);
  EXPECT_EQ(node->rrsets[0].type, rr_type::a);  // ordered by type
  EXPECT_EQ(node->rrsets[1].type, rr_type::aaaa);
  EXPECT_EQ(node->rrsets[0].ttl, 60u);
  EXPECT_EQ(node->rrsets[0].rdatas.size(), 2u);
}

TEST(DnsZone, NegativeTtlRfc2308Section3) {
  EXPECT_EQ(Zone(dns_name("example.com"), apex("example.com", 3600, 300)).negative_ttl(), 300u);
  EXPECT_EQ(Zone(dns_name("example.com"), apex("example.com", 60, 300)).negative_ttl(), 60u);
}

TEST(DnsZone, RefusesWhatCannotBeServed) {
  Record chaos = record("www.example.com", rr_type::a, 60, a_rdata(1));
  chaos.klass = 3;
  expect_refused(with(apex(), {chaos}), "class is not IN");
  expect_refused(with(apex(), {record("www.example.net", rr_type::a, 60, a_rdata(1))}), "outside the zone example.com.");
  expect_refused(with(apex(), {record("www.example.com", rr_type::opt, 60, {})}), "type 41");
  expect_refused(with(apex(), {record("www.example.com", rr_type::any, 60, {})}), "type 255");
  expect_refused(with(apex(), {record("www.example.com", 128, 60, {})}), "type 128");
  expect_refused(with(apex(), {record("www.example.com", rr_type::a, 60, {1, 2, 3})}), "malformed RDATA");
  expect_refused(with(apex(), {record("www.example.com", rr_type::ns, 60, {0xc0, 0x0c})}), "malformed RDATA");
}

TEST(DnsZone, RefusesABadApexRfc1035Section5_2) {
  expect_refused({record("example.com", rr_type::ns, 60, name_rdata("ns1.example.com"))}, "exactly one SOA");
  std::vector<Record> two_soas = apex();
  two_soas.push_back(record("example.com", rr_type::soa, 60, soa_rdata("ns2.example.com", "admin.example.com", 300)));
  expect_refused(two_soas, "exactly one SOA");
  expect_refused({apex().front()}, "needs NS");
  expect_refused({}, "exactly one SOA");
}

TEST(DnsZone, RefusesCnameBesideOtherDataRfc2181Section10_1) {
  expect_refused(
      with(apex(), {record("www.example.com", rr_type::cname, 60, name_rdata("host.example.com")), record("www.example.com", rr_type::a, 60, a_rdata(1))}),
      "CNAME cannot share");
  expect_refused(with(apex(), {record("example.com", rr_type::cname, 60, name_rdata("host.example.com"))}), "CNAME cannot share");
}

TEST(DnsCatalog, NearestEnclosingZoneRfc1034Section4_3_2) {
  Catalog catalog;
  EXPECT_EQ(catalog.find(dns_name("www.example.com")), nullptr);
  catalog.add(std::make_shared<const Zone>(dns_name("example.com"), apex("example.com")));
  catalog.add(std::make_shared<const Zone>(dns_name("sub.example.com"), apex("sub.example.com")));
  catalog.add(std::make_shared<const Zone>(dns_name("example.org"), apex("example.org")));
  EXPECT_EQ(catalog.find(dns_name("www.example.com"))->origin(), dns_name("example.com"));
  EXPECT_EQ(catalog.find(dns_name("www.SUB.example.com"))->origin(), dns_name("sub.example.com"));
  EXPECT_EQ(catalog.find(dns_name("sub.example.com"))->origin(), dns_name("sub.example.com"));
  EXPECT_EQ(catalog.find(dns_name("example.org"))->origin(), dns_name("example.org"));
  EXPECT_EQ(catalog.find(dns_name("example.net")), nullptr);
  EXPECT_EQ(catalog.find(dns_name("com")), nullptr);
  EXPECT_THROW(catalog.add(std::make_shared<const Zone>(dns_name("EXAMPLE.com"), apex("example.com"))), std::invalid_argument);
}
}  // namespace
