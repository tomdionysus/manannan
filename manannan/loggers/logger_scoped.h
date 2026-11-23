// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3.
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
  std::string scope_private;
  std::shared_ptr<Logger> logger_private;
};

}  // namespace manannan::loggers
