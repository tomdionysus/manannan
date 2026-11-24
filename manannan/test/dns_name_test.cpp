//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Domain names. Clauses proved:
// - RFC 1035 2.3.4: labels of 63 octets or fewer, names of 255 or fewer.
// - RFC 1035 3.1: the wire form, length octets ending at the root's zero.
// - RFC 1035 5.1: presentation form, `\X` and `\DDD` escapes.
// - RFC 1034 3.1 and RFC 4343 3: comparison ignores ASCII case, and only
//   ASCII case; case is kept as given.
// - RFC 2181 11: any octet may appear in a label.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "manannan/dns/name.h"

namespace {
using manannan::dns::Name;

Name name(const char* text) { return *Name::from_text(text); }

TEST(DnsName, DefaultIsTheRoot) {
  const Name root;
  EXPECT_TRUE(root.is_root());
  EXPECT_EQ(root.wire(), std::vector<std::uint8_t>{0});
  EXPECT_EQ(root.to_text(), ".");
  EXPECT_EQ(root.label_count(), 0u);
  EXPECT_EQ(name("."), root);
}

TEST(DnsName, TextToWireRfc1035Section3_1) {
  const Name n = name("www.Example.com");
  const std::vector<std::uint8_t> wire{3, 'w', 'w', 'w', 7, 'E', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0};
  EXPECT_EQ(n.wire(), wire);
  EXPECT_EQ(n.label_count(), 3u);
  EXPECT_FALSE(n.is_root());
  EXPECT_EQ(n.to_text(), "www.Example.com.");
  EXPECT_EQ(name("www.Example.com."), n);
}

TEST(DnsName, RefusesMalformedText) {
  for (const char* text : {"", "..", "a..b", ".a", "a.b..", "\\", "a\\", "\\25", "\\2a5", "\\256"}) EXPECT_FALSE(Name::from_text(text)) << text;
}

TEST(DnsName, EscapesRfc1035Section5_1) {
  const Name dotted = name("a\\.b.c");
  EXPECT_EQ(dotted.label_count(), 2u);
  EXPECT_EQ(dotted.wire()[0], 3);
  EXPECT_EQ(dotted.to_text(), "a\\.b.c.");
  const Name octets = name("\\000\\032\\127\\255x.y");
  EXPECT_EQ(octets.wire(), (std::vector<std::uint8_t>{5, 0, 32, 127, 255, 'x', 1, 'y', 0}));
  EXPECT_EQ(octets.to_text(), "\\000\\032\\127\\255x.y.");
  const Name specials = name("\\\"\\(\\)\\;\\@\\$\\\\");
  EXPECT_EQ(specials.wire().size(), 9u);
  EXPECT_EQ(specials.to_text(), "\\\"\\(\\)\\;\\@\\$\\\\.");
  EXPECT_EQ(name(specials.to_text().c_str()), specials);
}

TEST(DnsName, LengthLimitsRfc1035Section2_3_4) {
  const std::string label63(63, 'a');
  EXPECT_TRUE(Name::from_text(label63));
  EXPECT_FALSE(Name::from_text(label63 + "a"));
  // Four labels of 63 make 4 * 64 + 1 = 257 octets; 61 in the last makes 255.
  const std::string four = label63 + "." + label63 + "." + label63 + "." + label63;
  EXPECT_FALSE(Name::from_text(four));
  const std::string fits = label63 + "." + label63 + "." + label63 + "." + std::string(61, 'a');
  ASSERT_TRUE(Name::from_text(fits));
  EXPECT_EQ(Name::from_text(fits)->wire().size(), 255u);
  EXPECT_FALSE(Name::from_text(fits + "a"));
}

TEST(DnsName, FromWireValidates) {
  EXPECT_TRUE(Name::from_wire(std::vector<std::uint8_t>{0}));
  EXPECT_TRUE(Name::from_wire(std::vector<std::uint8_t>{1, 'a', 0}));
  EXPECT_FALSE(Name::from_wire(std::vector<std::uint8_t>{}));
  EXPECT_FALSE(Name::from_wire(std::vector<std::uint8_t>{1, 'a'}));        // no root
  EXPECT_FALSE(Name::from_wire(std::vector<std::uint8_t>{1, 'a', 0, 0}));  // past the root
  EXPECT_FALSE(Name::from_wire(std::vector<std::uint8_t>{2, 'a', 0}));     // label overruns
  EXPECT_FALSE(Name::from_wire(std::vector<std::uint8_t>{64, 0}));         // label type 01
  std::vector<std::uint8_t> long_wire;
  for (int i = 0; i < 128; ++i) long_wire.insert(long_wire.end(), {1, 'a'});
  long_wire.push_back(0);
  EXPECT_FALSE(Name::from_wire(long_wire));  // 257 octets
}

TEST(DnsName, ComparesIgnoringAsciiCaseRfc4343) {
  EXPECT_EQ(name("WWW.example.COM"), name("www.EXAMPLE.com"));
  EXPECT_NE(name("www.example.com"), name("ww.example.com"));
  EXPECT_NE(name("a.b"), name("a.b.c"));
  // Only A to Z fold: 0xC4 and 0xE4 differ.
  EXPECT_NE(name("\\196"), name("\\228"));
  EXPECT_EQ(name("MiXeD").lowered().to_text(), "mixed.");
  EXPECT_EQ(name("MiXeD").to_text(), "MiXeD.");
}

TEST(DnsName, SubdomainRfc1034Section3_1) {
  EXPECT_TRUE(name("www.example.com").is_subdomain_of(name("example.com")));
  EXPECT_TRUE(name("www.example.com").is_subdomain_of(name("EXAMPLE.com")));
  EXPECT_TRUE(name("example.com").is_subdomain_of(name("example.com")));
  EXPECT_TRUE(name("example.com").is_subdomain_of(Name()));
  EXPECT_FALSE(name("example.com").is_subdomain_of(name("www.example.com")));
  EXPECT_FALSE(name("badexample.com").is_subdomain_of(name("example.com")));
  EXPECT_FALSE(Name().is_subdomain_of(name("com")));
}

TEST(DnsName, ParentAndPrefixed) {
  EXPECT_EQ(*name("www.example.com").parent(), name("example.com"));
  EXPECT_EQ(*name("com").parent(), Name());
  EXPECT_FALSE(Name().parent());
  EXPECT_EQ(*name("example.com").prefixed("*"), name("*.example.com"));
  EXPECT_EQ(*Name().prefixed("a"), name("a"));
  EXPECT_FALSE(name("example.com").prefixed(""));
  EXPECT_FALSE(name("example.com").prefixed(std::string(64, 'a')));
  const std::string label63(63, 'a');
  const Name full = name((label63 + "." + label63 + "." + label63 + "." + std::string(61, 'a')).c_str());
  EXPECT_FALSE(full.prefixed("b"));
  EXPECT_TRUE(full.parent()->prefixed(std::string(61, 'b')));
}

}  // namespace
