// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3.
#pragma once

#include <mutex>

#include "manannan/loggers/logger.h"

namespace manannan::loggers {

class LoggerStdIO : public Logger {
 public:
  explicit LoggerStdIO(LogLevel log_level);
  void debug(const std::string& log) override;
  void info(const std::string& log) override;
  void warn(const std::string& log) override;
  void error(const std::string& log) override;
  void raw(const std::string& log) override;
  std::shared_ptr<Logger> base_logger() override;
  void set_level(LogLevel level) override;

 private:
  LogLevel log_level_private;
  std::mutex mutex_private;
};

}  // namespace manannan::loggers
