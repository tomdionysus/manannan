//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// A zone as the query engine reads it: immutable once built, checked as it
// is built, and shared between readers without locks. A Catalog is the set
// of zones one snapshot serves; a newer snapshot replaces it whole.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "manannan/dns/message.h"
#include "manannan/dns/name.h"

namespace manannan::dns {

// The records of one owner, type and class (RFC 2181 5).
struct RRset {
  Name owner;
  std::uint16_t type = 0;
  std::uint16_t klass = 0;
  std::uint32_t ttl = 0;
  std::vector<std::vector<std::uint8_t>> rdatas;
};

class Zone {
 public:
  // An owner name in the zone. One with no RRsets is an empty non-terminal:
  // it exists because names below it do (RFC 8020).
  struct Node {
    std::vector<RRset> rrsets;  // ordered by type

    const RRset* get(std::uint16_t type) const;
  };

  // Builds the zone from its records, in class IN. Throws
  // std::invalid_argument, naming the fault, unless: every owner is at or
  // below `origin`; the apex holds exactly one SOA and at least one NS; no
  // CNAME shares its owner with other data or sits at the apex (RFC 1034
  // 3.6.2, RFC 2181 10.1); every record's RDATA is well formed for the types
  // that hold names. The RRset's TTL is the lowest of its records' (RFC 2181
  // 5.2), and a repeated record is kept once (RFC 2181 5).
  Zone(const Name& origin, const std::vector<Record>& records);
  Zone(const Zone&) = delete;
  Zone& operator=(const Zone&) = delete;

  const Name& origin() const { return _origin; }
  const RRset& soa() const { return *_soa; }
  // The TTL of a negative answer: the lower of the SOA's TTL and its MINIMUM (RFC 2308 3).
  std::uint32_t negative_ttl() const;
  // The node at `name`, in any case, or nullptr when no such name exists.
  const Node* find(const Name& name) const;

 private:
  Name _origin;
  std::unordered_map<std::string, Node> _nodes;  // by lowered wire name
  const RRset* _soa = nullptr;
};

class Catalog {
 public:
  void add(std::shared_ptr<const Zone> zone);
  // The zone whose origin is the longest suffix of `name`, or nullptr.
  const Zone* find(const Name& name) const;

 private:
  std::vector<std::shared_ptr<const Zone>> _zones;
};

}  // namespace manannan::dns
