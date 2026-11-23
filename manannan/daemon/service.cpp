//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/daemon/service.h"

#include <cerrno>
#include <csignal>
#include <stdexcept>
#include <system_error>
namespace manannan::daemon {
namespace {
volatile std::sig_atomic_t shutdown_requested{};

extern "C" void request_shutdown(int) { shutdown_requested = 1; }
}  // namespace
Service::Service(SourcePlugin& source, std::vector<Registry> registries, std::chrono::milliseconds cadence, Clock& clock,
                 std::shared_ptr<loggers::Logger> logger)
    : _source(source), _registries(std::move(registries)), _cadence(cadence), _clock(clock), _logger(std::move(logger)) {}
std::size_t Service::reconcile() {
  _logger->debug("loading value from source");
  const auto desired = _source.get_value();
  if (desired.empty()) throw std::runtime_error("source returned an empty value");
  _logger->debug("source value is " + desired);
  std::size_t failures = 0;
  for (const auto& registry : _registries) {
    for (const auto& record : registry.plugin->records()) {
      const auto label = registry.name + ":" + record;
      try {
        const auto registered = registry.plugin->get_value(record);
        if (registered == desired) {
          _logger->debug(label + " is current: " + desired);
          continue;
        }
        _logger->debug(label + " is '" + registered + "'; setting " + desired);
        registry.plugin->set_value(record, desired);
        _logger->info("set " + label + " to " + desired);
      } catch (const std::exception& error) {
        ++failures;
        _logger->error(label + ": " + error.what());
      }
    }
  }
  return failures;
}
int Service::run() {
  shutdown_requested = 0;
  struct sigaction action{};
  action.sa_handler = request_shutdown;
  sigemptyset(&action.sa_mask);
  struct sigaction previous_interrupt{};
  struct sigaction previous_terminate{};
  if (sigaction(SIGINT, &action, &previous_interrupt) < 0 || sigaction(SIGTERM, &action, &previous_terminate) < 0)
    throw std::system_error(errno, std::generic_category());  // LCOV_EXCL_LINE

  // The shutdown signals are blocked except inside the clock's wait, so one
  // that arrives between the flag check and the wait still ends the wait.
  sigset_t shutdown_signals;
  sigemptyset(&shutdown_signals);
  sigaddset(&shutdown_signals, SIGINT);
  sigaddset(&shutdown_signals, SIGTERM);
  sigset_t wait_mask;
  if (sigprocmask(SIG_BLOCK, &shutdown_signals, &wait_mask) < 0) throw std::system_error(errno, std::generic_category());  // LCOV_EXCL_LINE
  // The caller's mask and handlers are put back however the loop ends.
  const auto restore = [&] {
    sigprocmask(SIG_SETMASK, &wait_mask, nullptr);
    sigaction(SIGINT, &previous_interrupt, nullptr);
    sigaction(SIGTERM, &previous_terminate, nullptr);
  };

  const auto attempt = [this] {
    try {
      reconcile();
    } catch (const std::exception& error) {
      _logger->error(error.what());
    }
  };
  attempt();

  auto next_run = _clock.now() + _cadence;
  try {
    while (!shutdown_requested) {
      if (_clock.now() >= next_run) {
        attempt();
        next_run = _clock.now() + _cadence;
        continue;
      }
      _clock.wait_until(next_run, wait_mask);
    }
  } catch (...) {
    restore();
    throw;
  }
  restore();
  _logger->info("shutting down");
  return 0;
}
}  // namespace manannan::daemon
