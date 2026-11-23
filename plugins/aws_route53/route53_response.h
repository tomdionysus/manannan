//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The reader of Route 53's ListResourceRecordSets response, which is bytes
// from outside the process: it scans for record sets without recursion or
// backtracking, takes no more than the response it is given (the plugin caps
// that at max_response_bytes), and never throws on what it does not
// understand. A fuzz target drives it (manannan/fuzz/route53_response_fuzz.cpp).
#pragma once
#include <cstddef>
#include <optional>
#include <string>

namespace manannan::aws {

// The most of a Route 53 response the plugin reads: a page of one record set
// is well under a kilobyte.
inline constexpr std::size_t max_response_bytes = 1 << 20;

// The value of the first A record set named fqdn (lower case, with its
// trailing dot), or nothing when there is none.
std::optional<std::string> find_a_record(const std::string& response, const std::string& fqdn);

// XML's five predefined entities decoded, everything else left as it is.
std::string xml_unescape(std::string value);

// Route 53's names: lower case, with bytes outside [a-z0-9-_.] written as
// three-digit octal escapes ("*" is "\052"). Anything that is not a valid
// escape is kept as it is.
std::string decode_dns_name(const std::string& value);

}  // namespace manannan::aws
