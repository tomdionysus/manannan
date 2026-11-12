#pragma once
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "manannan/loggers/logger.h"

namespace manannan {
// The daemon: reads the configuration named by the single argument, loads the
// source and registry plugins and runs the service until SIGINT or SIGTERM.
// "--version" prints the version. Returns the process exit status: 0 after a
// clean shutdown, 1 when the configuration or a plugin is refused, 2 on a
// usage error.
int run_application(const std::vector<std::string>& arguments, std::shared_ptr<loggers::Logger> logger, std::ostream& out, std::ostream& err);
}  // namespace manannan
