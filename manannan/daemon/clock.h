//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Time for the service loop, injected so its tests step time instead of
// sleeping. now() is monotonic. wait_until() returns at the deadline, or
// sooner when a signal is delivered: the caller blocks its shutdown signals
// and passes the mask to wait under, in which they are unblocked, so a signal
// that arrived before the wait still ends it.
#pragma once
#include <signal.h>

#include <chrono>

namespace manannan::daemon {

class Clock {
 public:
  using time_point = std::chrono::steady_clock::time_point;
  virtual ~Clock() = default;
  virtual time_point now() = 0;
  virtual void wait_until(time_point deadline, const sigset_t& wait_mask) = 0;
};

// The steady clock, waiting in pselect.
class SystemClock final : public Clock {
 public:
  time_point now() override;
  void wait_until(time_point deadline, const sigset_t& wait_mask) override;
};

}  // namespace manannan::daemon
