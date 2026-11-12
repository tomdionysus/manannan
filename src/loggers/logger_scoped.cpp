// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3.
#include "manannan/loggers/logger_scoped.h"

namespace manannan::loggers {

LoggerScoped::LoggerScoped(std::string scope, std::shared_ptr<Logger> logger) : scope_private(std::move(scope)), logger_private(std::move(logger)) {}
void LoggerScoped::debug(const std::string& log) { logger_private->debug("(" + scope_private + ") " + log); }
void LoggerScoped::info(const std::string& log) { logger_private->info("(" + scope_private + ") " + log); }
void LoggerScoped::warn(const std::string& log) { logger_private->warn("(" + scope_private + ") " + log); }
void LoggerScoped::error(const std::string& log) { logger_private->error("(" + scope_private + ") " + log); }
void LoggerScoped::raw(const std::string& log) { logger_private->raw("(" + scope_private + ") " + log); }
std::shared_ptr<Logger> LoggerScoped::base_logger() { return logger_private; }
void LoggerScoped::set_level(LogLevel level) {
  auto base = base_logger();
  if (base) base->set_level(level);
}

}  // namespace manannan::loggers
