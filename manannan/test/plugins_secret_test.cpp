//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The Secret type: it cannot be printed or become a string unasked; it takes
// its value and cleanses the string it came from; copies and moves keep the
// value, and a moved-from secret is empty.

#include <gtest/gtest.h>

#include <concepts>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "secret.h"

namespace {
using manannan::aws::Secret;

template <typename T>
concept Streamable = requires(std::ostream& out, const T& value) { out << value; };

static_assert(!Streamable<Secret>, "a secret must not be printable");
static_assert(!std::is_convertible_v<Secret, std::string>, "a secret must not become a string");
static_assert(!std::is_convertible_v<std::string, Secret>, "a string becomes a secret only explicitly");

TEST(Secret, HoldsItsValueAndCleansesTheSource) {
  std::string source = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
  const Secret secret(std::move(source));
  EXPECT_EQ(secret.reveal(), "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY");
  EXPECT_FALSE(secret.empty());
  EXPECT_TRUE(source.empty());  // NOLINT(bugprone-use-after-move): the constructor promises it
}

TEST(Secret, IsEmptyByDefaultAndFromAnEmptyString) {
  EXPECT_TRUE(Secret().empty());
  EXPECT_EQ(Secret().reveal(), "");
  EXPECT_TRUE(Secret(std::string()).empty());
}

TEST(Secret, CopiesAndMovesKeepTheValue) {
  Secret original(std::string("value"));
  Secret copy(original);
  EXPECT_EQ(copy.reveal(), "value");
  Secret assigned(std::string("other"));
  assigned = original;
  EXPECT_EQ(assigned.reveal(), "value");
  auto& self = assigned;
  assigned = self;
  EXPECT_EQ(assigned.reveal(), "value");

  Secret moved(std::move(copy));
  EXPECT_EQ(moved.reveal(), "value");
  EXPECT_TRUE(copy.empty());  // NOLINT(bugprone-use-after-move): moved-from is empty by contract
  Secret target(std::string("other"));
  target = std::move(moved);
  EXPECT_EQ(target.reveal(), "value");
  EXPECT_TRUE(moved.empty());  // NOLINT(bugprone-use-after-move)
  auto& same = target;
  target = std::move(same);
  EXPECT_EQ(target.reveal(), "value");
}

TEST(Secret, HoldsAValueLongerThanAPage) {
  const std::string long_value(70000, 'k');
  const Secret secret{std::string(long_value)};
  EXPECT_EQ(secret.reveal(), long_value);
}
}  // namespace
