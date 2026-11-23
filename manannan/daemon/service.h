//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The service loop: read the source, bring every record of every registry to
// its value, and do it again every cadence.
//
// Retries follow the self-healing disciplines. A record that fails, or the
// source, waits cadence x 2^(n-1) after its nth failure in a row, up to 64
// cadences. At the budget of 8 failures it is parked, which the log says once,
// with what wakes it: it is still tried every 64 cadences, so it recovers on
// its own; a new source value (the event that could change its answer) tries
// it at once; and SIGHUP, the operator's action, tries everything at once. A
// success clears the count and says so.
#pragma once
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "manannan/daemon/clock.h"
#include "manannan/loggers/logger.h"
#include "manannan/plugin.h"
namespace manannan::daemon {
struct Registry {
  std::string name;
  RegistryPlugin* plugin;
};

// "1h20m", "30s", "250ms": a duration for the log.
std::string describe(std::chrono::milliseconds duration);

class Service {
 public:
  static constexpr unsigned retry_budget = 8;
  static constexpr long long max_backoff_factor = 64;

  Service(SourcePlugin& source, std::vector<Registry> registries, std::chrono::milliseconds cadence, Clock& clock, std::shared_ptr<loggers::Logger> logger);
  // Reconciles at once and then every cadence until SIGINT or SIGTERM;
  // SIGHUP retries everything at once. Leaves the caller's signal mask and
  // handlers as it found them.
  int run();
  // Reads the source once and brings every record of every registry that is
  // not waiting for a retry up to date. A failure on one record is logged and
  // does not affect the others. Returns the number of records that failed.
  std::size_t reconcile();
  // The wait after the nth failure in a row.
  std::chrono::milliseconds backoff(unsigned failures) const;

 private:
  struct Retry {
    unsigned failures = 0;
    Clock::time_point next{};
  };
  std::string retry_note(unsigned failures, std::chrono::milliseconds delay) const;

  SourcePlugin& _source;
  std::vector<Registry> _registries;
  std::chrono::milliseconds _cadence;
  Clock& _clock;
  std::shared_ptr<loggers::Logger> _logger;
  std::map<std::string, Retry> _retries;
  std::string _last_desired;
};
}  // namespace manannan::daemon
