#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "manannan/loggers/logger.h"
#include "manannan/plugin.h"
namespace manannan::daemon {
struct Registry {
  std::string name;
  RegistryPlugin* plugin;
};
class Service {
 public:
  Service(SourcePlugin& source, std::vector<Registry> registries, std::chrono::milliseconds cadence, std::shared_ptr<loggers::Logger> logger);
  int run();
  // Reads the source once and brings every record of every registry up to
  // date. A failure on one record is logged and does not affect the others.
  // Returns the number of records that could not be reconciled.
  std::size_t reconcile();

 private:
  SourcePlugin& _source;
  std::vector<Registry> _registries;
  std::chrono::milliseconds _cadence;
  std::shared_ptr<loggers::Logger> _logger;
};
}  // namespace manannan::daemon
