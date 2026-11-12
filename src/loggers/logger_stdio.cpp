// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3.
#include "manannan/loggers/logger_stdio.h"

#include <iostream>

#include "manannan/util.h"

namespace manannan::loggers {

LoggerStdIO::LoggerStdIO(LogLevel log_level) : log_level_private(log_level) {}
void LoggerStdIO::debug(const std::string& log) {
  if (log_level_private > DEBUG) return;
  std::lock_guard lock(mutex_private);
  std::cout << get_zulu_time() << " [DEBUG] " << log << std::endl;
}
void LoggerStdIO::info(const std::string& log) {
  if (log_level_private > INFO) return;
  std::lock_guard lock(mutex_private);
  std::cout << get_zulu_time() << " [INFO ] " << log << std::endl;
}
void LoggerStdIO::warn(const std::string& log) {
  if (log_level_private > WARN) return;
  std::lock_guard lock(mutex_private);
  std::cout << get_zulu_time() << " [WARN ] " << log << std::endl;
}
void LoggerStdIO::error(const std::string& log) {
  std::lock_guard lock(mutex_private);
  std::cerr << get_zulu_time() << " [ERROR] " << log << std::endl;
}
void LoggerStdIO::raw(const std::string& log) {
  std::lock_guard lock(mutex_private);
  std::cout << get_zulu_time() << " [-----] " << log << std::endl;
}
std::shared_ptr<Logger> LoggerStdIO::base_logger() { return nullptr; }
void LoggerStdIO::set_level(LogLevel level) { log_level_private = level; }

}  // namespace manannan::loggers
