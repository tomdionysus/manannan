//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The query engine, query octets in and response octets out. Clauses proved:
// - RFC 1034 4.3.2 steps 2, 3a, 3b, 3c and 6: the zone, answers, CNAMEs,
//   referrals with glue, names not found, and additional addresses.
// - RFC 1034 3.6.2: CNAME chains, stopped by a limit and by a loop.
// - RFC 1035 4.1.1: AA, TC, RD copied, RA never set, the ID copied; and
//   REFUSED, NOTIMP and FORMERR.
// - RFC 2181 9: an RRset sent whole or not at all; TC only when what is
//   left out is required.
// - RFC 2308 2.1, 2.2 and 3: NXDOMAIN and NODATA with the SOA, at the
//   negative TTL.
// - RFC 4035 3.1.6: CD copied.
// - RFC 4343: names found in any case, the question echoed as asked.
// - RFC 4592 3.3.1 and 4.1: wildcards at the closest encloser, synthesized
//   under the name asked.
// - RFC 6604 2.1: the RCODE of a chain is the last name's.
// - RFC 6891 6.1.1, 6.1.3, 6.2.3 and 6.2.5: OPT echoed only when asked, DO
//   copied, BADVERS, and the UDP size limit.
// - RFC 8020: an empty non-terminal is NODATA, not NXDOMAIN.
// - RFC 8482 4.2: ANY answered with one RRset.
// - RFC 9471 3: glue below the cut is required; other glue is not.

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fixtures/dns_bytes.h"
#include "fixtures/dns_zone.h"
#include "manannan/dns/engine.h"
#include "manannan/dns/writer.h"

namespace {
using manannan::dns::Catalog;
using manannan::dns::Edns;
using manannan::dns::EngineLimits;
using manannan::dns::Message;
using manannan::dns::parse_message;
using manannan::dns::ParseError;
using manannan::dns::Question;
using manannan::dns::Record;
using manannan::dns::respond;
using manannan::dns::Transport;
using manannan::dns::Writer;
using manannan::dns::Zone;
using manannan::test::a_rdata;
using manannan::test::aaaa_rdata;
using manannan::test::dns_name;
using manannan::test::mx_rdata;
using manannan::test::name_rdata;
using manannan::test::record;
using manannan::test::soa_rdata;
using manannan::test::srv_rdata;
namespace rr_type = manannan::dns::rr_type;
namespace rcode = manannan::dns::rcode;

std::vector<Record> example_com() {
  std::vector<Record> records{
      record("example.com", rr_type::soa, 3600, soa_rdata("ns1.example.com", "admin.example.com", 300)),
      record("example.com", rr_type::ns, 3600, name_rdata("ns1.example.com")),
      record("example.com", rr_type::ns, 3600, name_rdata("ns2.example.com")),
      record("example.com", rr_type::mx, 3600, mx_rdata(10, "mail.example.com")),
      record("example.com", rr_type::mx, 3600, mx_rdata(20, "mail.example.net")),
      record("ns1.example.com", rr_type::a, 3600, a_rdata(1)),
      record("ns1.example.com", rr_type::aaaa, 3600, aaaa_rdata(1)),
      record("ns2.example.com", rr_type::a, 3600, a_rdata(2)),
      record("www.example.com", rr_type::a, 60, a_rdata(10)),
      record("www.example.com", rr_type::a, 60, a_rdata(11)),
      record("mail.example.com", rr_type::a, 60, a_rdata(20)),
      record("alias.example.com", rr_type::cname, 60, name_rdata("www.example.com")),
      record("chain1.example.com", rr_type::cname, 60, name_rdata("chain2.example.com")),
      record("chain2.example.com", rr_type::cname, 60, name_rdata("www.example.com")),
      record("loop1.example.com", rr_type::cname, 60, name_rdata("loop2.example.com")),
      record("loop2.example.com", rr_type::cname, 60, name_rdata("loop1.example.com")),
      record("out.example.com", rr_type::cname, 60, name_rdata("www.example.net")),
      record("gone.example.com", rr_type::cname, 60, name_rdata("missing.example.com")),
      record("cross.example.com", rr_type::cname, 60, name_rdata("www.example.org")),
      record("intochild.example.com", rr_type::cname, 60, name_rdata("host.child.example.com")),
      record("*.wild.example.com", rr_type::a, 60, a_rdata(30)),
      record("deep.a.b.example.com", rr_type::a, 60, a_rdata(31)),
      record("child.example.com", rr_type::ns, 3600, name_rdata("ns.child.example.com")),
      record("child.example.com", rr_type::ns, 3600, name_rdata("ns1.example.com")),
      record("child.example.com", rr_type::ns, 3600, name_rdata("ns.example.net")),
      record("ns.child.example.com", rr_type::a, 3600, a_rdata(40)),
      record("_sip._udp.example.com", rr_type::srv, 60, srv_rdata(0, 0, 5060, "sip.example.com")),
      record("sip.example.com", rr_type::a, 60, a_rdata(50)),
      record("bigmx.example.com", rr_type::mx, 60, mx_rdata(10, "big.example.com")),
      record("bigchild.example.com", rr_type::ns, 60, name_rdata("ns.bigchild.example.com")),
  };
  // Forty addresses: 640 octets of answer, more than 512.
  for (std::uint8_t i = 0; i < 40; ++i) {
    records.push_back(record("big.example.com", rr_type::a, 60, a_rdata(i)));
    records.push_back(record("ns.bigchild.example.com", rr_type::a, 60, a_rdata(i)));
  }
  // A hundred: 1600 octets, more than 1232.
  for (std::uint8_t i = 0; i < 100; ++i) records.push_back(record("huge.example.com", rr_type::a, 60, a_rdata(i)));
  return records;
}

const Catalog& catalog() {
  static const Catalog catalog = [] {
    Catalog built;
    built.add(std::make_shared<const Zone>(dns_name("example.com"), example_com()));
    built.add(std::make_shared<const Zone>(
        dns_name("example.org"),
        std::vector<Record>{record("example.org", rr_type::soa, 600, soa_rdata("ns.example.org", "admin.example.org", 60)),
                            record("example.org", rr_type::ns, 600, name_rdata("ns.example.org")), record("www.example.org", rr_type::a, 60, a_rdata(99))}));
    return built;
  }();
  return catalog;
}

struct Ask {
  std::string name;
  std::uint16_t type = rr_type::a;
  std::uint16_t klass = manannan::dns::rr_class::in;
  std::optional<Edns> edns{};
  std::uint8_t opcode = 0;
  bool cd = false;
};

std::vector<std::uint8_t> query_octets(const Ask& ask) {
  Writer writer;
  writer.header().id = 0x5151;
  writer.header().rd = true;
  writer.header().cd = ask.cd;
  writer.header().opcode = ask.opcode;
  EXPECT_TRUE(writer.add_question(Question{dns_name(ask.name), ask.type, ask.klass}));
  if (ask.edns) {
    EXPECT_TRUE(writer.add_edns(*ask.edns));
  }
  return writer.finish();
}

Message answer_bytes(const std::vector<std::uint8_t>& query, Transport transport = Transport::udp, const EngineLimits& limits = {}) {
  const std::vector<std::uint8_t> response = respond(catalog(), query, transport, limits);
  Message message;
  EXPECT_EQ(parse_message(response, message), ParseError::none);
  EXPECT_LE(response.size(), transport == Transport::udp ? 1232u : 65535u);
  return message;
}

Message ask(const Ask& request, Transport transport = Transport::udp, const EngineLimits& limits = {}) {
  return answer_bytes(query_octets(request), transport, limits);
}

std::vector<std::string> owners(const std::vector<Record>& records) {
  std::vector<std::string> names;
  for (const Record& r : records) names.push_back(r.name.to_text() + " " + std::to_string(r.type));
  return names;
}

TEST(DnsEngine, AnswersRfc1034Section4_3_2Step3a) {
  const Message m = ask({"www.example.com"});
  EXPECT_EQ(m.header.id, 0x5151);
  EXPECT_TRUE(m.header.qr);
  EXPECT_TRUE(m.header.aa);
  EXPECT_TRUE(m.header.rd);
  EXPECT_FALSE(m.header.ra);
  EXPECT_FALSE(m.header.tc);
  EXPECT_EQ(m.header.rcode, rcode::noerror);
  ASSERT_EQ(m.questions.size(), 1u);
  ASSERT_EQ(m.answers.size(), 2u);
  EXPECT_EQ(m.answers[0].rdata, a_rdata(10));
  EXPECT_EQ(m.answers[1].rdata, a_rdata(11));
  EXPECT_EQ(m.answers[0].ttl, 60u);
  EXPECT_TRUE(m.authorities.empty());
  EXPECT_FALSE(m.edns);  // none asked for (RFC 6891 6.1.1)
}

TEST(DnsEngine, AnyCaseRfc4343) {
  const Message m = ask({"WwW.ExAmPlE.CoM"});
  EXPECT_EQ(m.questions[0].name.to_text(), "WwW.ExAmPlE.CoM.");
  EXPECT_EQ(m.answers.size(), 2u);
}

TEST(DnsEngine, NameErrorRfc2308Section2_1) {
  const Message m = ask({"nothing.example.com"});
  EXPECT_TRUE(m.header.aa);
  EXPECT_EQ(m.header.rcode, rcode::nxdomain);
  EXPECT_TRUE(m.answers.empty());
  ASSERT_EQ(m.authorities.size(), 1u);
  EXPECT_EQ(m.authorities[0].type, rr_type::soa);
  EXPECT_EQ(m.authorities[0].name, dns_name("example.com"));
  EXPECT_EQ(m.authorities[0].ttl, 300u);  // RFC 2308 3
}

TEST(DnsEngine, NoDataRfc2308Section2_2) {
  for (const auto& [name, type] : std::vector<std::pair<std::string, std::uint16_t>>{{"www.example.com", rr_type::aaaa}, {"b.example.com", rr_type::a}}) {
    const Message m = ask({name, type});  // b.example.com is an empty non-terminal (RFC 8020)
    EXPECT_EQ(m.header.rcode, rcode::noerror) << name;
    EXPECT_TRUE(m.answers.empty()) << name;
    ASSERT_EQ(m.authorities.size(), 1u) << name;
    EXPECT_EQ(m.authorities[0].type, rr_type::soa) << name;
  }
}

TEST(DnsEngine, CnameRfc1034Section3_6_2) {
  Message m = ask({"alias.example.com"});
  EXPECT_EQ(owners(m.answers), (std::vector<std::string>{"alias.example.com. 5", "www.example.com. 1", "www.example.com. 1"}));
  EXPECT_TRUE(m.header.aa);
  m = ask({"alias.example.com", rr_type::cname});
  EXPECT_EQ(owners(m.answers), (std::vector<std::string>{"alias.example.com. 5"}));
  m = ask({"chain1.example.com"});
  EXPECT_EQ(owners(m.answers), (std::vector<std::string>{"chain1.example.com. 5", "chain2.example.com. 5", "www.example.com. 1", "www.example.com. 1"}));
}

TEST(DnsEngine, CnameChainsEnd) {
  Message m = ask({"loop1.example.com"});
  EXPECT_EQ(owners(m.answers), (std::vector<std::string>{"loop1.example.com. 5", "loop2.example.com. 5"}));
  EXPECT_EQ(m.header.rcode, rcode::noerror);
  EngineLimits one;
  one.max_cname_chain = 1;
  m = ask({"chain1.example.com"}, Transport::udp, one);
  EXPECT_EQ(owners(m.answers), (std::vector<std::string>{"chain1.example.com. 5"}));
}

TEST(DnsEngine, CnameTargetsRfc6604Section2_1) {
  // Out of the catalog: the CNAME alone.
  Message m = ask({"out.example.com"});
  EXPECT_EQ(owners(m.answers), (std::vector<std::string>{"out.example.com. 5"}));
  EXPECT_EQ(m.header.rcode, rcode::noerror);
  EXPECT_TRUE(m.authorities.empty());
  // To a missing name: NXDOMAIN, for the target.
  m = ask({"gone.example.com"});
  EXPECT_EQ(owners(m.answers), (std::vector<std::string>{"gone.example.com. 5"}));
  EXPECT_EQ(m.header.rcode, rcode::nxdomain);
  EXPECT_EQ(m.authorities[0].type, rr_type::soa);
  // Into another zone served here.
  m = ask({"cross.example.com"});
  EXPECT_EQ(owners(m.answers), (std::vector<std::string>{"cross.example.com. 5", "www.example.org. 1"}));
  m = ask({"cross.example.com", rr_type::aaaa});
  EXPECT_EQ(m.authorities[0].name, dns_name("example.org"));
  EXPECT_EQ(m.authorities[0].ttl, 60u);
  // Into a delegation: the CNAME alone, and no referral.
  m = ask({"intochild.example.com"});
  EXPECT_EQ(owners(m.answers), (std::vector<std::string>{"intochild.example.com. 5"}));
  EXPECT_TRUE(m.authorities.empty());
  EXPECT_TRUE(m.header.aa);
}

TEST(DnsEngine, WildcardsRfc4592Section3_3_1) {
  Message m = ask({"Host.wild.example.com"});
  ASSERT_EQ(m.answers.size(), 1u);
  EXPECT_EQ(m.answers[0].name.to_text(), "Host.wild.example.com.");
  EXPECT_EQ(m.answers[0].rdata, a_rdata(30));
  m = ask({"a.b.wild.example.com"});
  EXPECT_EQ(owners(m.answers), (std::vector<std::string>{"a.b.wild.example.com. 1"}));
  m = ask({"*.wild.example.com"});
  EXPECT_EQ(owners(m.answers), (std::vector<std::string>{"*.wild.example.com. 1"}));
  m = ask({"host.wild.example.com", rr_type::aaaa});
  EXPECT_TRUE(m.answers.empty());
  EXPECT_EQ(m.header.rcode, rcode::noerror);
  // The wildcard covers only names below wild.example.com, which itself exists.
  m = ask({"wild.example.com"});
  EXPECT_EQ(m.header.rcode, rcode::noerror);
  EXPECT_TRUE(m.answers.empty());
  // A name under an existing name that has no wildcard is not covered (RFC 4592 4.1).
  m = ask({"x.deep.a.b.example.com"});
  EXPECT_EQ(m.header.rcode, rcode::nxdomain);
}

TEST(DnsEngine, ReferralsRfc1034Section4_3_2Step3b) {
  for (const char* name : {"child.example.com", "host.child.example.com", "ns.child.example.com"}) {
    const Message m = ask({name});
    EXPECT_FALSE(m.header.aa) << name;
    EXPECT_EQ(m.header.rcode, rcode::noerror) << name;
    EXPECT_TRUE(m.answers.empty()) << name;
    EXPECT_EQ(owners(m.authorities), (std::vector<std::string>{"child.example.com. 2", "child.example.com. 2", "child.example.com. 2"})) << name;
    // Glue below the cut, and the sibling's address held in this zone; none for the name elsewhere.
    EXPECT_EQ(owners(m.additionals), (std::vector<std::string>{"ns.child.example.com. 1", "ns1.example.com. 1", "ns1.example.com. 28"})) << name;
  }
}

TEST(DnsEngine, AdditionalAddressesRfc1034Section4_3_2Step6) {
  Message m = ask({"example.com", rr_type::ns});
  EXPECT_EQ(m.answers.size(), 2u);
  EXPECT_EQ(owners(m.additionals), (std::vector<std::string>{"ns1.example.com. 1", "ns1.example.com. 28", "ns2.example.com. 1"}));
  m = ask({"example.com", rr_type::mx});
  EXPECT_EQ(owners(m.additionals), (std::vector<std::string>{"mail.example.com. 1"}));
  m = ask({"_sip._udp.example.com", rr_type::srv});
  EXPECT_EQ(owners(m.additionals), (std::vector<std::string>{"sip.example.com. 1"}));
  // Nothing for a target below a cut.
  m = ask({"child.example.com", rr_type::ns});
  EXPECT_FALSE(m.header.aa);  // a referral, even when NS is asked for
}

TEST(DnsEngine, AnyRfc8482Section4_2) {
  Message m = ask({"example.com", rr_type::any});
  ASSERT_EQ(m.answers.size(), 2u);  // the NS RRset, the lowest type
  EXPECT_EQ(m.answers[0].type, rr_type::ns);
  m = ask({"b.example.com", rr_type::any});
  EXPECT_TRUE(m.answers.empty());
  EXPECT_EQ(m.authorities[0].type, rr_type::soa);
}

TEST(DnsEngine, RefusesWhatItDoesNotServe) {
  EXPECT_EQ(ask({"www.example.net"}).header.rcode, rcode::refused);
  EXPECT_FALSE(ask({"www.example.net"}).header.aa);
  EXPECT_EQ(ask({"www.example.com", rr_type::a, 3}).header.rcode, rcode::refused);  // class CH
  EXPECT_EQ(ask({"example.com", 252}).header.rcode, rcode::refused);                // AXFR
  EXPECT_EQ(ask({"example.com", 251}).header.rcode, rcode::refused);                // IXFR
  const Message m = ask({"www.example.com", rr_type::a, manannan::dns::rr_class::in, std::nullopt, 2});
  EXPECT_EQ(m.header.rcode, rcode::notimp);
  EXPECT_EQ(m.header.opcode, 2);
  EXPECT_EQ(m.questions.size(), 1u);
}

TEST(DnsEngine, FormatErrorsRfc1035Section4_1_1) {
  using manannan::test::DnsBytes;
  // No question, and two.
  Message m = answer_bytes(DnsBytes().header(0x7777, 0x0100, 0).bytes());
  EXPECT_EQ(m.header.rcode, rcode::formerr);
  EXPECT_EQ(m.header.id, 0x7777);
  DnsBytes two;
  two.header(1, 0, 2).name("a.example.com").u16(1).u16(1).name("b.example.com").u16(1).u16(1);
  EXPECT_EQ(answer_bytes(two.bytes()).header.rcode, rcode::formerr);
  // Unreadable: a pointer to itself; the ID still answered.
  m = answer_bytes(DnsBytes().header(0x0102, 0x0100, 1).pointer(12).u16(1).u16(1).bytes());
  EXPECT_EQ(m.header.rcode, rcode::formerr);
  EXPECT_EQ(m.header.id, 0x0102);
  EXPECT_TRUE(m.questions.empty());
  // More records than a query needs.
  EXPECT_EQ(answer_bytes(DnsBytes().header(1, 0, 17).bytes()).header.rcode, rcode::formerr);
}

TEST(DnsEngine, SendsNothingToNonQueries) {
  EXPECT_TRUE(respond(catalog(), std::vector<std::uint8_t>(11, 0), Transport::udp).empty());
  std::vector<std::uint8_t> response = query_octets({"www.example.com"});
  response[2] |= 0x80;  // QR: a response, never answered
  EXPECT_TRUE(respond(catalog(), response, Transport::udp).empty());
}

TEST(DnsEngine, EdnsRfc6891Section6_1) {
  Message m = ask({"www.example.com", rr_type::a, manannan::dns::rr_class::in, Edns{.udp_payload_size = 4096, .dnssec_ok = true}});
  ASSERT_TRUE(m.edns);
  EXPECT_EQ(m.edns->udp_payload_size, 1232);
  EXPECT_TRUE(m.edns->dnssec_ok);  // RFC 3225 3
  EXPECT_EQ(m.edns->version, 0);
  m = ask({"www.example.com", rr_type::a, manannan::dns::rr_class::in, Edns{.udp_payload_size = 1232}});
  EXPECT_FALSE(m.edns->dnssec_ok);
  // A version this server does not speak (RFC 6891 6.1.3).
  m = ask({"www.example.com", rr_type::a, manannan::dns::rr_class::in, Edns{.udp_payload_size = 1232, .version = 1}});
  EXPECT_EQ(manannan::dns::full_rcode(m), rcode::badvers);
  EXPECT_TRUE(m.answers.empty());
  EXPECT_EQ(m.edns->version, 0);
}

TEST(DnsEngine, CdCopiedRfc4035Section3_1_6) {
  EXPECT_TRUE(ask({"www.example.com", rr_type::a, manannan::dns::rr_class::in, std::nullopt, 0, true}).header.cd);
  EXPECT_FALSE(ask({"www.example.com"}).header.cd);
}

TEST(DnsEngine, TruncationRfc2181Section9) {
  // Without EDNS, 512 octets: the forty-address RRset does not fit, and is sent not at all.
  Message m = ask({"big.example.com"});
  EXPECT_TRUE(m.header.tc);
  EXPECT_TRUE(m.answers.empty());
  // With EDNS it fits; over TCP, anything does.
  m = ask({"big.example.com", rr_type::a, manannan::dns::rr_class::in, Edns{.udp_payload_size = 4096}});
  EXPECT_FALSE(m.header.tc);
  EXPECT_EQ(m.answers.size(), 40u);
  // A client's size below 512 is taken as 512 (RFC 6891 6.2.5); above ours, as ours.
  m = ask({"huge.example.com", rr_type::a, manannan::dns::rr_class::in, Edns{.udp_payload_size = 100}});
  EXPECT_TRUE(m.header.tc);
  EXPECT_TRUE(m.edns);  // the OPT record always has room
  m = ask({"huge.example.com", rr_type::a, manannan::dns::rr_class::in, Edns{.udp_payload_size = 65535}});
  EXPECT_TRUE(m.header.tc);
  m = ask({"huge.example.com"}, Transport::tcp);
  EXPECT_FALSE(m.header.tc);
  EXPECT_EQ(m.answers.size(), 100u);
}

TEST(DnsEngine, TruncationOnlyForRequiredDataRfc9471Section3) {
  // Addresses for an MX are a courtesy: left out without TC.
  Message m = ask({"bigmx.example.com", rr_type::mx});
  EXPECT_FALSE(m.header.tc);
  EXPECT_EQ(m.answers.size(), 1u);
  EXPECT_TRUE(m.additionals.empty());
  // Glue below a cut is required: TC when it does not fit.
  m = ask({"host.bigchild.example.com"});
  EXPECT_TRUE(m.header.tc);
  EXPECT_EQ(m.authorities.size(), 1u);
  EXPECT_TRUE(m.additionals.empty());
}
}  // namespace
