//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#pragma once

#include <string>

#include "manannan/loggers/logger.h"

namespace manannan::loggers {

class LoggerScoped : public Logger {
 public:
  LoggerScoped(std::string scope, std::shared_ptr<Logger> logger);
  void debug(const std::string& log) override;
  void info(const std::string& log) override;
  void warn(const std::string& log) override;
  void error(const std::string& log) override;
  void raw(const std::string& log) override;
  std::shared_ptr<Logger> base_logger() override;
  void set_level(LogLevel level) override;

 private:
  std::string _scope;
  std::shared_ptr<Logger> _logger;
};

}  // namespace manannan::loggers
