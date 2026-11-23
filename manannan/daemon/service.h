//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#pragma once
#include <chrono>
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
class Service {
 public:
  Service(SourcePlugin& source, std::vector<Registry> registries, std::chrono::milliseconds cadence, Clock& clock, std::shared_ptr<loggers::Logger> logger);
  // Reconciles at once and then every cadence until SIGINT or SIGTERM,
  // leaving the caller's signal mask and handlers as it found them.
  int run();
  // Reads the source once and brings every record of every registry up to
  // date. A failure on one record is logged and does not affect the others.
  // Returns the number of records that could not be reconciled.
  std::size_t reconcile();

 private:
  SourcePlugin& _source;
  std::vector<Registry> _registries;
  std::chrono::milliseconds _cadence;
  Clock& _clock;
  std::shared_ptr<loggers::Logger> _logger;
};
}  // namespace manannan::daemon
