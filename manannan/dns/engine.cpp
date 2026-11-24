//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/dns/engine.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "manannan/dns/writer.h"

namespace manannan::dns {
namespace {

constexpr std::size_t opt_octets = 11;  // an OPT record with no options
constexpr std::uint16_t min_udp_payload = 512;
constexpr std::uint16_t qtype_ixfr = 251;
constexpr std::uint16_t qtype_axfr = 252;

// One RRset for the response, under `owner`, and whether leaving it out
// makes the response truncated.
struct Item {
  Writer::Section section;
  const RRset* rrset;
  Name owner;
  bool required;
  std::optional<std::uint32_t> ttl;  // in place of the RRset's
};

struct Resolution {
  std::uint16_t rcode = rcode::noerror;
  bool aa = false;
  std::vector<Item> items;

  bool has(const RRset* rrset) const {
    return std::any_of(items.begin(), items.end(), [rrset](const Item& item) { return item.rrset == rrset; });
  }
};

// The names from just below `origin` down to `name`, top first.
std::vector<Name> path_below(const Name& origin, const Name& name) {
  std::vector<Name> path;
  for (std::optional<Name> at = name; at && !(*at == origin); at = at->parent()) path.push_back(*at);
  std::reverse(path.begin(), path.end());
  return path;
}

// The node holding authoritative data at exactly `name`: none if the name is
// missing, or at or below a zone cut.
const Zone::Node* authoritative(const Catalog& catalog, const Name& name) {
  const Zone* zone = catalog.find(name);
  if (zone == nullptr) return nullptr;
  const Zone::Node* node = zone->find(zone->origin());
  for (const Name& step : path_below(zone->origin(), name)) {
    node = zone->find(step);
    if (node == nullptr || node->get(rr_type::ns) != nullptr) return nullptr;
  }
  return node;
}

// RFC 1034 4.3.2 step 6: addresses for the names an NS, MX or SRV answer
// gives, where this catalog holds them; left out if they do not fit.
void add_addresses_for(const Catalog& catalog, const RRset& rrset, Resolution& out) {
  if (rrset.type != rr_type::ns && rrset.type != rr_type::mx && rrset.type != rr_type::srv) return;
  for (const auto& rdata : rrset.rdatas) {
    const Name target = *rdata_name(rrset.type, rdata);
    const Zone::Node* node = authoritative(catalog, target);
    if (node == nullptr) continue;
    for (const std::uint16_t type : {rr_type::a, rr_type::aaaa}) {
      const RRset* address = node->get(type);
      if (address != nullptr && !out.has(address)) out.items.push_back(Item{Writer::Section::additional, address, address->owner, false, std::nullopt});
    }
  }
}

// A referral to the zone cut at `cut` (RFC 1034 4.3.2 step 3b). The NS
// RRset must fit; so must the glue below the cut (RFC 9471), while glue
// elsewhere in the zone is added if it fits.
void refer(const Zone& zone, const Name& cut, const Zone::Node& node, Resolution& out) {
  const RRset* ns = node.get(rr_type::ns);
  out.aa = false;
  out.items.push_back(Item{Writer::Section::authority, ns, cut, true, std::nullopt});
  for (const auto& rdata : ns->rdatas) {
    const Name target = *rdata_name(rr_type::ns, rdata);
    if (!target.is_subdomain_of(zone.origin())) continue;
    const Zone::Node* glue = zone.find(target);
    if (glue == nullptr) continue;
    for (const std::uint16_t type : {rr_type::a, rr_type::aaaa}) {
      const RRset* address = glue->get(type);
      if (address != nullptr && !out.has(address))
        out.items.push_back(Item{Writer::Section::additional, address, address->owner, target.is_subdomain_of(cut), std::nullopt});
    }
  }
}

void deny(const Zone& zone, std::uint16_t rcode, Resolution& out) {
  out.rcode = rcode;
  out.items.push_back(Item{Writer::Section::authority, &zone.soa(), zone.origin(), true, zone.negative_ttl()});
}

// RFC 1034 4.3.2 step 3, from the zone that holds `qname`, following CNAMEs
// through every zone the catalog holds. NXDOMAIN and NODATA answer for the
// last name of a chain (RFC 6604 2.1).
void resolve(const Catalog& catalog, Name qname, std::uint16_t qtype, const EngineLimits& limits, Resolution& out) {
  out.aa = true;
  std::vector<Name> seen{qname};
  while (true) {
    const Zone* zone = catalog.find(qname);
    if (zone == nullptr) return;  // a CNAME led out of the catalog
    const Zone::Node* node = zone->find(zone->origin());
    bool exact = true;
    Name closest = zone->origin();
    for (const Name& step : path_below(zone->origin(), qname)) {
      const Zone::Node* below = zone->find(step);
      if (below == nullptr) {
        exact = false;
        break;
      }
      node = below;
      closest = step;
      if (node->get(rr_type::ns) != nullptr) {
        // A referral answers the query itself, not the end of a chain.
        if (seen.size() == 1) refer(*zone, step, *node, out);
        return;
      }
    }
    if (!exact) {
      // RFC 4592 3.3.1: the source of synthesis is the wildcard at the closest encloser.
      const std::optional<Name> wildcard = closest.prefixed("*");
      node = wildcard ? zone->find(*wildcard) : nullptr;
      if (node == nullptr) return deny(*zone, rcode::nxdomain, out);
    }
    if (qtype == rr_type::any) {
      // RFC 8482 4.2: one RRset is enough.
      if (node->rrsets.empty()) return deny(*zone, rcode::noerror, out);
      out.items.push_back(Item{Writer::Section::answer, &node->rrsets.front(), qname, true, std::nullopt});
      return;
    }
    if (const RRset* rrset = node->get(qtype)) {
      out.items.push_back(Item{Writer::Section::answer, rrset, qname, true, std::nullopt});
      add_addresses_for(catalog, *rrset, out);
      return;
    }
    const RRset* cname = node->get(rr_type::cname);
    if (cname == nullptr) return deny(*zone, rcode::noerror, out);
    out.items.push_back(Item{Writer::Section::answer, cname, qname, true, std::nullopt});
    Name target = *rdata_name(rr_type::cname, cname->rdatas.front());
    if (seen.size() >= limits.max_cname_chain || std::find(seen.begin(), seen.end(), target) != seen.end()) return;
    seen.push_back(target);
    qname = std::move(target);
  }
}

// Writes the items RRset by RRset; a required one that does not fit sets
// TC and ends the response there (RFC 2181 9).
void write_items(std::vector<Item>& items, Writer& writer) {
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.section < b.section; });
  for (const Item& item : items) {
    const Writer::Mark mark = writer.mark();
    bool fits = true;
    for (const auto& rdata : item.rrset->rdatas) {
      fits = writer.add_record(item.section, Record{item.owner, item.rrset->type, item.rrset->klass, item.ttl.value_or(item.rrset->ttl), rdata});
      if (!fits) break;
    }
    if (fits) continue;
    writer.rollback(mark);
    if (item.required) {
      writer.header().tc = true;
      return;
    }
  }
}

}  // namespace

std::vector<std::uint8_t> respond(const Catalog& catalog, std::span<const std::uint8_t> query_octets, Transport transport, const EngineLimits& limits) {
  Message query;
  const ParseError error = parse_message(query_octets, query, limits.parse);
  if (error == ParseError::short_header || query.header.qr) return {};

  std::size_t size = max_message_octets;
  if (transport == Transport::udp) {
    size = min_udp_payload;
    if (query.edns) size = std::clamp(query.edns->udp_payload_size, min_udp_payload, std::max(limits.udp_payload, min_udp_payload));
  }
  Writer writer(size);
  Header& header = writer.header();
  header.id = query.header.id;
  header.qr = true;
  header.opcode = query.header.opcode;
  header.rd = query.header.rd;
  header.cd = query.header.cd;
  if (error != ParseError::none) {
    header.rcode = rcode::formerr;
    return writer.finish();
  }

  std::optional<Edns> edns;
  if (query.edns) {
    edns = Edns{.udp_payload_size = limits.udp_payload, .dnssec_ok = query.edns->dnssec_ok};
    writer.reserve(opt_octets);
  }
  auto finish = [&](std::uint16_t rcode) {
    header.rcode = static_cast<std::uint8_t>(rcode & 0x0f);
    if (edns) {
      edns->extended_rcode = static_cast<std::uint8_t>(rcode >> 4);
      writer.add_edns(*edns);
    }
    return writer.finish();
  };

  // RFC 6891 6.1.3: a version this server does not speak.
  if (query.edns && query.edns->version != 0) return finish(rcode::badvers);
  if (query.questions.size() != 1) return finish(rcode::formerr);
  const Question& question = query.questions.front();
  writer.add_question(question);  // always fits: a name is at most 255 octets
  if (query.header.opcode != 0) return finish(rcode::notimp);
  if (question.klass != rr_class::in || question.type == qtype_axfr || question.type == qtype_ixfr) return finish(rcode::refused);
  if (catalog.find(question.name) == nullptr) return finish(rcode::refused);

  Resolution resolution;
  resolve(catalog, question.name, question.type, limits, resolution);
  header.aa = resolution.aa;
  write_items(resolution.items, writer);
  return finish(resolution.rcode);
}

}  // namespace manannan::dns
