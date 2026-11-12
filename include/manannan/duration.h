#pragma once
#include <chrono>
#include <string>
namespace manannan {
std::chrono::milliseconds parse_duration(const std::string& value);
}
