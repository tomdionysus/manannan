// Cadence parsing: an integer and one of ms, s, m or h, positive and small
// enough to add to a steady_clock time point.

#include "manannan/duration.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace {
using namespace std::chrono_literals;
using manannan::parse_duration;

TEST(Duration, ParsesEachUnit) {
  EXPECT_EQ(parse_duration("250ms"), 250ms);
  EXPECT_EQ(parse_duration("30s"), 30s);
  EXPECT_EQ(parse_duration("5m"), 5min);
  EXPECT_EQ(parse_duration("2h"), 2h);
}

TEST(Duration, RefusesMalformedValues) {
  for (const auto* value : {"", "5", "m", "-5m", "5d", "5 m", "0s"}) EXPECT_THROW((void)parse_duration(value), std::invalid_argument) << value;
}

TEST(Duration, RefusesValuesTheClockCannotHold) {
  EXPECT_THROW((void)parse_duration("18446744073709551615ms"), std::invalid_argument);
  EXPECT_THROW((void)parse_duration("9999999999999999h"), std::invalid_argument);
  EXPECT_THROW((void)parse_duration("99999999999999999999ms"), std::out_of_range);
}
}  // namespace
