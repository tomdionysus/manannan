//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The message writer. Clauses proved:
// - RFC 1035 4.1.1: the header and counts as written.
// - RFC 1035 4.1.4: owner and question names compressed against earlier
//   names and suffixes, ignoring case (RFC 4343 3), and only to offsets a
//   pointer can hold.
// - RFC 2181 9: what does not fit is refused whole, leaving the message as
//   it was, for the caller to set TC.
// - RFC 6891 6.1.2 and 6.1.3: the OPT record's fields as written.
// Every message written here is read back by the parser and compared.

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "fixtures/dns_bytes.h"
#include "manannan/dns/message.h"
#include "manannan/dns/writer.h"

namespace {
using manannan::dns::Edns;
using manannan::dns::Header;
using manannan::dns::Message;
using manannan::dns::Name;
using manannan::dns::parse_message;
using manannan::dns::ParseError;
using manannan::dns::Question;
using manannan::dns::Record;
using manannan::dns::write_message;
using manannan::dns::Writer;
using manannan::test::DnsBytes;
namespace rr_type = manannan::dns::rr_type;
using Section = Writer::Section;

Name name(const char* text) { return *Name::from_text(text); }

Record a_record(const char* owner, std::uint8_t last) { return Record{name(owner), rr_type::a, 1, 60, {192, 0, 2, last}}; }

Message read_back(const std::vector<std::uint8_t>& bytes) {
  Message message;
  EXPECT_EQ(parse_message(bytes, message), ParseError::none);
  return message;
}

TEST(DnsWriter, HeaderAndCountsRfc1035Section4_1_1) {
  Writer writer;
  writer.header() =
      Header{.id = 0xbeef, .qr = true, .opcode = 5, .aa = true, .tc = true, .rd = true, .ra = true, .z = true, .ad = true, .cd = true, .rcode = 9};
  ASSERT_TRUE(writer.add_question(Question{name("a"), rr_type::a, 1}));
  ASSERT_TRUE(writer.add_record(Section::answer, a_record("a", 1)));
  ASSERT_TRUE(writer.add_record(Section::answer, a_record("a", 2)));
  ASSERT_TRUE(writer.add_record(Section::authority, a_record("a", 3)));
  ASSERT_TRUE(writer.add_record(Section::additional, a_record("a", 4)));
  const std::vector<std::uint8_t> bytes = writer.finish();
  EXPECT_EQ(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 12), (std::vector<std::uint8_t>{0xbe, 0xef, 0xaf, 0xf9, 0, 1, 0, 2, 0, 1, 0, 1}));
  const Message message = read_back(bytes);
  EXPECT_EQ(message.header, writer.header());
  EXPECT_EQ(message.answers.size(), 2u);
}

TEST(DnsWriter, CompressesNamesRfc1035Section4_1_4) {
  Writer writer;
  ASSERT_TRUE(writer.add_question(Question{name("www.example.com"), rr_type::a, 1}));  // name at 12, suffix at 16
  ASSERT_TRUE(writer.add_record(Section::answer, a_record("WWW.Example.COM", 1)));     // at 33
  ASSERT_TRUE(writer.add_record(Section::answer, a_record("mail.example.com", 2)));    // at 49
  const std::vector<std::uint8_t> bytes = writer.finish();
  EXPECT_EQ(bytes[33], 0xc0);
  EXPECT_EQ(bytes[34], 12);
  EXPECT_EQ(std::vector<std::uint8_t>(bytes.begin() + 49, bytes.begin() + 56), (std::vector<std::uint8_t>{4, 'm', 'a', 'i', 'l', 0xc0, 16}));
  EXPECT_EQ(bytes.size(), 56u + 14u);
  const Message message = read_back(bytes);
  EXPECT_EQ(message.answers[0].name, name("www.example.com"));
  EXPECT_EQ(message.answers[1].name, name("mail.example.com"));
}

TEST(DnsWriter, PointsOnlyWhereAPointerCanReach) {
  Writer writer;
  // A record that carries the message past offset 0x3fff.
  ASSERT_TRUE(writer.add_record(Section::answer, Record{Name(), 65280, 1, 0, std::vector<std::uint8_t>(0x4000, 0)}));
  ASSERT_EQ(writer.size(), 12u + 1 + 10 + 0x4000);
  // "late.example" lands beyond a pointer's reach, so its second use is written in full.
  ASSERT_TRUE(writer.add_record(Section::answer, Record{name("late.example"), 65280, 1, 0, {}}));
  ASSERT_TRUE(writer.add_record(Section::answer, Record{name("late.example"), 65280, 1, 0, {}}));
  EXPECT_EQ(writer.size(), 12u + 1 + 10 + 0x4000 + 2 * (14 + 10));
  const Message message = read_back(writer.finish());
  EXPECT_EQ(message.answers[2].name, name("late.example"));
}

TEST(DnsWriter, RefusesWhatDoesNotFitRfc2181Section9) {
  Writer writer(64);
  ASSERT_TRUE(writer.add_question(Question{name("www.example.com"), rr_type::a, 1}));
  ASSERT_EQ(writer.size(), 33u);
  EXPECT_FALSE(writer.add_record(Section::answer, Record{name("new.name"), 65280, 1, 0, std::vector<std::uint8_t>(40, 0)}));
  EXPECT_EQ(writer.size(), 33u);
  // The refused record's names are forgotten: this one is written in full,
  // not as a pointer into what was taken back.
  ASSERT_TRUE(writer.add_record(Section::answer, Record{name("new.name"), 65280, 1, 0, {1, 2, 3, 4}}));
  EXPECT_EQ(writer.size(), 57u);
  writer.header().tc = true;
  const Message message = read_back(writer.finish());
  EXPECT_TRUE(message.header.tc);
  ASSERT_EQ(message.answers.size(), 1u);
  EXPECT_EQ(message.answers[0].name, name("new.name"));
}

TEST(DnsWriter, RefusesOversizedFields) {
  Writer writer;
  EXPECT_FALSE(writer.add_record(Section::answer, Record{Name(), 65280, 1, 0, std::vector<std::uint8_t>(65536, 0)}));
  Edns edns;
  edns.options.push_back({1, std::vector<std::uint8_t>(65532, 0)});
  EXPECT_FALSE(writer.add_edns(edns));
  EXPECT_EQ(writer.size(), 12u);
}

TEST(DnsWriter, EdnsRfc6891Section6_1) {
  Writer writer;
  Edns edns{.udp_payload_size = 1232, .extended_rcode = 1, .version = 0, .dnssec_ok = true, .z = 0x0101, .options = {{10, {1, 2, 3, 4, 5, 6, 7, 8}}, {12, {}}}};
  ASSERT_TRUE(writer.add_edns(edns));
  const std::vector<std::uint8_t> bytes = writer.finish();
  DnsBytes expected;
  expected.header(0, 0, 0, 0, 0, 1).u8(0).u16(rr_type::opt).u16(1232).u32(0x01008101).u16(16).u16(10).u16(8).raw({1, 2, 3, 4, 5, 6, 7, 8}).u16(12).u16(0);
  EXPECT_EQ(bytes, expected.bytes());
  const Message message = read_back(bytes);
  ASSERT_TRUE(message.edns);
  EXPECT_EQ(*message.edns, edns);
}

TEST(DnsWriter, EdnsThatDoesNotFitCanBeTriedAgain) {
  Writer writer(12);
  EXPECT_FALSE(writer.add_edns(Edns{}));
  EXPECT_FALSE(writer.add_edns(Edns{}));
  EXPECT_EQ(writer.size(), 12u);
}

TEST(DnsWriter, SectionsInOrder) {
  Writer writer;
  ASSERT_TRUE(writer.add_record(Section::authority, a_record("a", 1)));
  EXPECT_THROW((void)writer.add_record(Section::answer, a_record("a", 1)), std::logic_error);
  EXPECT_THROW((void)writer.add_question(Question{name("a"), 1, 1}), std::logic_error);
  EXPECT_THROW((void)writer.add_record(Section::question, a_record("a", 1)), std::logic_error);
  ASSERT_TRUE(writer.add_edns(Edns{}));
  EXPECT_THROW((void)writer.add_edns(Edns{}), std::logic_error);
  ASSERT_TRUE(writer.add_record(Section::additional, a_record("a", 1)));
}

TEST(DnsWriter, SizeLimitMustHoldAHeader) {
  EXPECT_THROW(Writer(11), std::invalid_argument);
  EXPECT_THROW(Writer(manannan::dns::max_message_octets + 1), std::invalid_argument);
  EXPECT_EQ(Writer(12).finish().size(), 12u);
}

Message full_message() {
  Message message;
  message.header = Header{.id = 7, .qr = true, .aa = true, .rcode = 3};
  message.questions.push_back(Question{name("Www.Example.com"), rr_type::a, 1});
  message.answers.push_back(Record{name("www.example.com"), rr_type::cname, 1, 300, name("host.example.com").wire()});
  message.answers.push_back(a_record("host.example.com", 9));
  std::vector<std::uint8_t> soa = name("ns1.example.com").wire();
  const std::vector<std::uint8_t> admin = name("admin.example.com").wire();
  soa.insert(soa.end(), admin.begin(), admin.end());
  soa.resize(soa.size() + 20, 1);
  message.authorities.push_back(Record{name("example.com"), rr_type::soa, 1, 3600, soa});
  message.additionals.push_back(Record{name("ns1.example.com"), rr_type::aaaa, 1, 60, std::vector<std::uint8_t>(16, 0x20)});
  message.edns = Edns{.udp_payload_size = 1232, .dnssec_ok = true};
  return message;
}

TEST(DnsWriter, WholeMessagesRoundTrip) {
  const Message message = full_message();
  const auto bytes = write_message(message);
  ASSERT_TRUE(bytes);
  EXPECT_EQ(read_back(*bytes), message);
  // Without EDNS, and empty.
  Message plain = message;
  plain.edns.reset();
  EXPECT_EQ(read_back(*write_message(plain)), plain);
  EXPECT_EQ(read_back(*write_message(Message{})), Message{});
}

TEST(DnsWriter, WholeMessagesThatDoNotFit) {
  Message message = full_message();
  const std::size_t size = write_message(message)->size();
  EXPECT_FALSE(write_message(message, size - 1));  // the OPT record
  message.edns.reset();
  const std::size_t without_edns = write_message(message)->size();
  EXPECT_FALSE(write_message(message, without_edns - 1));  // an additional record
  EXPECT_FALSE(write_message(message, 12));                // the question
  message.questions.clear();
  EXPECT_FALSE(write_message(message, 12));  // an answer
}

TEST(DnsWriter, ReservesRoomForOpt) {
  Writer writer(64);
  writer.reserve(11);
  ASSERT_TRUE(writer.add_question(Question{name("www.example.com"), rr_type::a, 1}));            // 33 octets
  EXPECT_FALSE(writer.add_record(Section::answer, Record{name("new.name"), 65280, 1, 0, {1}}));  // 54, and 11 kept
  EXPECT_EQ(writer.size(), 33u);
  EXPECT_TRUE(writer.add_edns(Edns{}));  // the OPT record may use what is kept
  EXPECT_EQ(writer.size(), 44u);
}

TEST(DnsWriter, MarkAndRollback) {
  Writer writer;
  ASSERT_TRUE(writer.add_record(Section::answer, a_record("a.example", 1)));
  const Writer::Mark mark = writer.mark();
  ASSERT_TRUE(writer.add_record(Section::answer, a_record("b.example", 2)));
  ASSERT_TRUE(writer.add_record(Section::authority, a_record("c.example", 3)));
  writer.rollback(mark);
  EXPECT_EQ(writer.size(), mark.size);
  // The names written after the mark are forgotten, so c.example is written in full again.
  ASSERT_TRUE(writer.add_record(Section::additional, a_record("c.example", 4)));
  const Message message = read_back(writer.finish());
  EXPECT_EQ(message.answers.size(), 1u);
  EXPECT_TRUE(message.authorities.empty());
  ASSERT_EQ(message.additionals.size(), 1u);
  EXPECT_EQ(message.additionals[0].name, name("c.example"));
}

}  // namespace
