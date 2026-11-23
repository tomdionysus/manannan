//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/daemon/clock.h"

#include <sys/select.h>

#include <cerrno>
#include <ctime>
#include <system_error>

namespace manannan::daemon {

Clock::time_point SystemClock::now() { return std::chrono::steady_clock::now(); }

void SystemClock::wait_until(time_point deadline, const sigset_t& wait_mask) {
  const auto remaining = deadline - now();
  if (remaining <= time_point::duration::zero()) return;
  const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(remaining);
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(nanoseconds);
  const timespec timeout{static_cast<std::time_t>(seconds.count()), static_cast<long>((nanoseconds - seconds).count())};
  if (pselect(0, nullptr, nullptr, nullptr, &timeout, &wait_mask) < 0 && errno != EINTR)
    throw std::system_error(errno, std::generic_category(), "pselect");  // LCOV_EXCL_LINE
}

}  // namespace manannan::daemon
