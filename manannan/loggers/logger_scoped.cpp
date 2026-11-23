//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/loggers/logger_scoped.h"

namespace manannan::loggers {

LoggerScoped::LoggerScoped(std::string scope, std::shared_ptr<Logger> logger) : _scope(std::move(scope)), _logger(std::move(logger)) {}
void LoggerScoped::debug(const std::string& log) { _logger->debug("(" + _scope + ") " + log); }
void LoggerScoped::info(const std::string& log) { _logger->info("(" + _scope + ") " + log); }
void LoggerScoped::warn(const std::string& log) { _logger->warn("(" + _scope + ") " + log); }
void LoggerScoped::error(const std::string& log) { _logger->error("(" + _scope + ") " + log); }
void LoggerScoped::raw(const std::string& log) { _logger->raw("(" + _scope + ") " + log); }
std::shared_ptr<Logger> LoggerScoped::base_logger() { return _logger; }
void LoggerScoped::set_level(LogLevel level) {
  auto base = base_logger();
  if (base) base->set_level(level);
}

}  // namespace manannan::loggers
