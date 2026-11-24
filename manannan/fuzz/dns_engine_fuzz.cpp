//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Any bytes as a query, over UDP and over TCP, to a small catalog with every
// kind of answer in it. A response, when there is one, must read back as a
// response to the same ID and fit its transport's limit; anything else aborts.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "manannan/dns/engine.h"

namespace {
using namespace manannan::dns;

Name name(std::string_view text) { return *Name::from_text(text); }

std::vector<std::uint8_t> with_suffix(std::vector<std::uint8_t> head, const Name& tail) {
  head.insert(head.end(), tail.wire().begin(), tail.wire().end());
  return head;
}

Record record(std::string_view owner, std::uint16_t type, std::vector<std::uint8_t> rdata) {
  return Record{name(owner), type, rr_class::in, 60, std::move(rdata)};
}

const Catalog& catalog() {
  static const Catalog catalog = [] {
    std::vector<std::uint8_t> soa = name("ns.f.test").wire();
    soa = with_suffix(soa, name("admin.f.test"));
    soa.resize(soa.size() + 20, 0);
    std::vector<Record> records{
        record("f.test", rr_type::soa, soa),
        record("f.test", rr_type::ns, name("ns.f.test").wire()),
        record("f.test", rr_type::mx, with_suffix({0, 10}, name("ns.f.test"))),
        record("ns.f.test", rr_type::a, {192, 0, 2, 1}),
        record("a.f.test", rr_type::cname, name("b.f.test").wire()),
        record("b.f.test", rr_type::cname, name("a.f.test").wire()),
        record("c.f.test", rr_type::cname, name("ns.f.test").wire()),
        record("*.w.f.test", rr_type::txt, {3, 'a', 'b', 'c'}),
        record("d.f.test", rr_type::ns, name("ns.d.f.test").wire()),
        record("ns.d.f.test", rr_type::a, {192, 0, 2, 2}),
    };
    for (std::uint8_t i = 0; i < 100; ++i) records.push_back(record("big.f.test", rr_type::a, {192, 0, 2, i}));
    Catalog built;
    built.add(std::make_shared<const Zone>(name("f.test"), records));
    return built;
  }();
  return catalog;
}

void check(std::span<const std::uint8_t> query, Transport transport, std::size_t limit) {
  const std::vector<std::uint8_t> response = respond(catalog(), query, transport);
  if (response.empty()) return;
  Message message;
  if (response.size() > limit || parse_message(response, message) != ParseError::none || !message.header.qr ||
      message.header.id != static_cast<std::uint16_t>(query[0] << 8 | query[1]))
    std::abort();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> query(data, size);
  check(query, Transport::udp, 1232);
  check(query, Transport::tcp, max_message_octets);
  return 0;
}
