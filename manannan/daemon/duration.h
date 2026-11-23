//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#pragma once
#include <chrono>
#include <string>
namespace manannan::daemon {
std::chrono::milliseconds parse_duration(const std::string& value);
}  // namespace manannan::daemon
