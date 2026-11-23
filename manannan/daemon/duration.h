#pragma once
#include <chrono>
#include <string>
namespace manannan::daemon {
std::chrono::milliseconds parse_duration(const std::string& value);
}  // namespace manannan::daemon
