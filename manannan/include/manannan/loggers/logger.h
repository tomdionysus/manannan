// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3.
#pragma once

#include <memory>
#include <string>

namespace manannan::loggers {

enum LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

class Logger {
 public:
  virtual ~Logger() {}
  virtual void debug(const std::string& log) = 0;
  virtual void info(const std::string& log) = 0;
  virtual void warn(const std::string& log) = 0;
  virtual void error(const std::string& log) = 0;
  virtual void raw(const std::string& log) = 0;
  virtual void set_level(LogLevel level) = 0;
  virtual std::shared_ptr<Logger> base_logger() = 0;
};

}  // namespace manannan::loggers
