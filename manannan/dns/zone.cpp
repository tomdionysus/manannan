//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/dns/zone.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace manannan::dns {
namespace {

std::string key(const Name& name) {
  const Name lowered = name.lowered();
  return std::string(lowered.wire().begin(), lowered.wire().end());
}

// Types that exist only in messages, never in a zone (RFC 6895 3.1).
bool is_meta_type(std::uint16_t type) { return type == rr_type::opt || (type >= 128 && type <= 255); }

[[noreturn]] void refuse(const Name& owner, const std::string& why) { throw std::invalid_argument(owner.to_text() + ": " + why); }

}  // namespace

const RRset* Zone::Node::get(std::uint16_t type) const {
  for (const RRset& rrset : rrsets) {
    if (rrset.type == type) return &rrset;
  }
  return nullptr;
}

Zone::Zone(const Name& origin, const std::vector<Record>& records) : _origin(origin) {
  for (const Record& record : records) {
    if (record.klass != rr_class::in) refuse(record.name, "class is not IN");
    if (!record.name.is_subdomain_of(origin)) refuse(record.name, "outside the zone " + origin.to_text());
    if (is_meta_type(record.type)) refuse(record.name, "type " + std::to_string(record.type) + " cannot be held in a zone");
    if (!rdata_well_formed(record.type, record.rdata)) refuse(record.name, "malformed RDATA for type " + std::to_string(record.type));

    std::vector<RRset>& rrsets = _nodes[key(record.name)].rrsets;
    auto at = std::lower_bound(rrsets.begin(), rrsets.end(), record.type, [](const RRset& rrset, std::uint16_t type) { return rrset.type < type; });
    if (at == rrsets.end() || at->type != record.type) at = rrsets.insert(at, RRset{record.name, record.type, record.klass, record.ttl, {}});
    at->ttl = std::min(at->ttl, record.ttl);
    if (std::find(at->rdatas.begin(), at->rdatas.end(), record.rdata) == at->rdatas.end()) at->rdatas.push_back(record.rdata);

    // Every name between the owner and the apex exists.
    for (std::optional<Name> above = record.name.parent(); above && above->is_subdomain_of(origin); above = above->parent()) _nodes.try_emplace(key(*above));
  }

  const Node& apex = _nodes[key(origin)];
  _soa = apex.get(rr_type::soa);
  if (_soa == nullptr || _soa->rdatas.size() != 1) refuse(origin, "the apex needs exactly one SOA record");
  if (apex.get(rr_type::ns) == nullptr) refuse(origin, "the apex needs NS records");
  for (const auto& [name, node] : _nodes) {
    if (node.get(rr_type::cname) != nullptr && node.rrsets.size() > 1) refuse(node.rrsets.front().owner, "a CNAME cannot share its owner with other data");
  }
}

std::uint32_t Zone::negative_ttl() const {
  const std::vector<std::uint8_t>& rdata = _soa->rdatas.front();
  const std::size_t at = rdata.size() - 4;
  const std::uint32_t minimum = static_cast<std::uint32_t>(rdata[at]) << 24 | static_cast<std::uint32_t>(rdata[at + 1]) << 16 |
                                static_cast<std::uint32_t>(rdata[at + 2]) << 8 | rdata[at + 3];
  return std::min(_soa->ttl, minimum);
}

const Zone::Node* Zone::find(const Name& name) const {
  const auto found = _nodes.find(key(name));
  return found == _nodes.end() ? nullptr : &found->second;
}

void Catalog::add(std::shared_ptr<const Zone> zone) {
  for (const auto& held : _zones) {
    if (held->origin() == zone->origin()) throw std::invalid_argument(zone->origin().to_text() + ": the zone is already served");
  }
  _zones.push_back(std::move(zone));
}

const Zone* Catalog::find(const Name& name) const {
  const Zone* best = nullptr;
  for (const auto& zone : _zones) {
    if (name.is_subdomain_of(zone->origin()) && (best == nullptr || zone->origin().label_count() > best->origin().label_count())) best = zone.get();
  }
  return best;
}

}  // namespace manannan::dns
