//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#pragma once
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "manannan/loggers/logger.h"

namespace manannan::daemon {
// The daemon: reads the configuration (--config, by default the installed
// config.yaml, and --secrets, by default the installed secrets.yaml when it
// exists), applies the environment and any other --setting flags, loads the
// source and registry plugins and runs the service until SIGINT or SIGTERM.
// "--version" prints the version; "migrate" converts a 0.3 installation's
// files. Returns the process exit status: 0 after a clean shutdown or a
// migration, 1 when the configuration, a plugin or a migration is refused, 2
// on a usage error.
int run_application(const std::vector<std::string>& arguments, std::shared_ptr<loggers::Logger> logger, std::ostream& out, std::ostream& err);
}  // namespace manannan::daemon
