//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "manannan/daemon/application.h"
#include "manannan/loggers/logger_stdio.h"

int main(int argc, char** argv) {
  return manannan::daemon::run_application(std::vector<std::string>(argv + 1, argv + argc),
                                           std::make_shared<manannan::loggers::LoggerStdIO>(manannan::loggers::INFO), std::cout, std::cerr);
}
