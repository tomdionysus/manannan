//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The daemon's logger: errors to standard error, everything else to standard
// output, one line each, as text ("<time> [LEVEL] message") or as a JSON
// object ({"time":..., "level":..., "message":...}).
#pragma once

#include <mutex>

#include "manannan/loggers/logger.h"

namespace manannan::loggers {

enum class LogFormat { text, json };

class LoggerStdIO : public Logger {
 public:
  explicit LoggerStdIO(LogLevel log_level, LogFormat format = LogFormat::text);
  void debug(const std::string& log) override;
  void info(const std::string& log) override;
  void warn(const std::string& log) override;
  void error(const std::string& log) override;
  void raw(const std::string& log) override;
  std::shared_ptr<Logger> base_logger() override;
  void set_level(LogLevel level) override;
  void set_format(LogFormat format);

 private:
  void write(std::ostream& stream, const char* text_label, const char* json_level, const std::string& log);

  LogLevel _log_level;
  LogFormat _format;
  std::mutex _mutex;
};

// The value as a JSON string literal, quotes included.
std::string json_string(const std::string& value);

}  // namespace manannan::loggers
