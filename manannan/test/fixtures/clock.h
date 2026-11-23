//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#pragma once
// A clock the tests step. A wait moves time to its deadline at once and
// records it; like pselect, it lets through any signal the wait mask
// unblocks, so a shutdown signal raised before the wait ends the loop.
#include <signal.h>

#include <stdexcept>
#include <vector>

#include "manannan/daemon/clock.h"

namespace manannan::test {

class FakeClock final : public daemon::Clock {
 public:
  time_point now() override { return _now; }
  void wait_until(time_point deadline, const sigset_t& wait_mask) override {
    deadlines.push_back(deadline);
    if (static_cast<int>(deadlines.size()) == fail_on_wait) throw std::runtime_error("wait failed");
    if (deadline > _now) _now = deadline;
    sigset_t previous;
    sigprocmask(SIG_SETMASK, &wait_mask, &previous);
    sigprocmask(SIG_SETMASK, &previous, nullptr);
  }
  void advance(time_point::duration by) { _now += by; }

  std::vector<time_point> deadlines;
  int fail_on_wait = -1;

 private:
  time_point _now{};
};

}  // namespace manannan::test
