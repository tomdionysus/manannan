//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/loggers/logger_stdio.h"

#include <cstdio>
#include <iostream>

#include "manannan/loggers/util.h"

namespace manannan::loggers {

LoggerStdIO::LoggerStdIO(LogLevel log_level, LogFormat format) : _log_level(log_level), _format(format) {}
void LoggerStdIO::debug(const std::string& log) {
  if (_log_level > DEBUG) return;
  write(std::cout, "[DEBUG]", "debug", log);
}
void LoggerStdIO::info(const std::string& log) {
  if (_log_level > INFO) return;
  write(std::cout, "[INFO ]", "info", log);
}
void LoggerStdIO::warn(const std::string& log) {
  if (_log_level > WARN) return;
  write(std::cout, "[WARN ]", "warn", log);
}
void LoggerStdIO::error(const std::string& log) { write(std::cerr, "[ERROR]", "error", log); }
void LoggerStdIO::raw(const std::string& log) { write(std::cout, "[-----]", "raw", log); }
std::shared_ptr<Logger> LoggerStdIO::base_logger() { return nullptr; }
void LoggerStdIO::set_level(LogLevel level) { _log_level = level; }
void LoggerStdIO::set_format(LogFormat format) { _format = format; }

void LoggerStdIO::write(std::ostream& stream, const char* text_label, const char* json_level, const std::string& log) {
  std::lock_guard lock(_mutex);
  if (_format == LogFormat::json)
    stream << "{\"time\":\"" << get_zulu_time() << "\",\"level\":\"" << json_level << "\",\"message\":" << json_string(log) << "}" << std::endl;
  else
    stream << get_zulu_time() << " " << text_label << " " << log << std::endl;
}

std::string json_string(const std::string& value) {
  std::string out = "\"";
  for (const char character : value) {
    switch (character) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(character) < 0x20) {
          char escaped[7];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(character)));
          out += escaped;
        } else {
          out += character;
        }
    }
  }
  return out + "\"";
}

}  // namespace manannan::loggers
