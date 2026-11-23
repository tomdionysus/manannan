//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Any bytes as a Route 53 ListResourceRecordSets response, and as a name and
// a value to decode: the reader must neither crash, nor throw, nor run
// without bound.
#include <cstddef>
#include <cstdint>
#include <string>

#include "route53_response.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > manannan::aws::max_response_bytes) return 0;
  const std::string input(reinterpret_cast<const char*>(data), size);
  (void)manannan::aws::find_a_record(input, "a.example.com.");
  (void)manannan::aws::decode_dns_name(input);
  (void)manannan::aws::xml_unescape(input);
  return 0;
}
