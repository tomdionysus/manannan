#include "manannan/util.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
namespace manannan {
std::string get_zulu_time() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  gmtime_r(&time, &utc);
  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}
}  // namespace manannan
