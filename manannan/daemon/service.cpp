//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/daemon/service.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <stdexcept>
#include <system_error>
namespace manannan::daemon {
namespace {
volatile std::sig_atomic_t shutdown_requested{};
volatile std::sig_atomic_t retry_requested{};

extern "C" void request_shutdown(int) { shutdown_requested = 1; }
extern "C" void request_retry(int) { retry_requested = 1; }
}  // namespace

std::string describe(std::chrono::milliseconds duration) {
  using namespace std::chrono;
  if (duration.count() <= 0) return "0ms";
  const auto h = duration_cast<hours>(duration);
  const auto m = duration_cast<minutes>(duration - h);
  const auto s = duration_cast<seconds>(duration - h - m);
  const auto ms = duration - h - m - s;
  std::string text;
  if (h.count()) text += std::to_string(h.count()) + "h";
  if (m.count()) text += std::to_string(m.count()) + "m";
  if (s.count()) text += std::to_string(s.count()) + "s";
  if (ms.count()) text += std::to_string(ms.count()) + "ms";
  return text;
}

Service::Service(SourcePlugin& source, std::vector<Registry> registries, std::chrono::milliseconds cadence, Clock& clock,
                 std::shared_ptr<loggers::Logger> logger)
    : _source(source), _registries(std::move(registries)), _cadence(cadence), _clock(clock), _logger(std::move(logger)) {}

std::chrono::milliseconds Service::backoff(unsigned failures) const {
  // A cadence too long to double safely is not stretched.
  if (_cadence > std::chrono::milliseconds::max() / (4 * max_backoff_factor)) return _cadence;
  const auto exponent = std::min(failures == 0 ? 0u : failures - 1, 31u);
  return _cadence * std::min<long long>(1LL << exponent, max_backoff_factor);
}

std::string Service::retry_note(unsigned failures, std::chrono::milliseconds delay) const {
  if (failures == retry_budget)
    return "; parked after " + std::to_string(failures) + " failures: retrying every " + describe(delay) + ", when the value changes, or at once on SIGHUP";
  return "; retrying in " + describe(delay);
}

std::size_t Service::reconcile() {
  _logger->debug("loading value from source");
  const auto desired = _source.get_value();
  if (desired.empty()) throw std::runtime_error("source returned an empty value");
  _logger->debug("source value is " + desired);
  // A new value is the event that could change a failing record's answer.
  if (desired != _last_desired) {
    for (auto& [label, retry] : _retries) retry.next = {};
    _last_desired = desired;
  }
  const auto now = _clock.now();
  std::size_t failures = 0;
  for (const auto& registry : _registries) {
    for (const auto& record : registry.plugin->records()) {
      const auto label = registry.name + ":" + record;
      const auto found = _retries.find(label);
      if (found != _retries.end() && now < found->second.next) {
        _logger->debug(label + " waits for its retry");
        continue;
      }
      try {
        const auto registered = registry.plugin->get_value(record);
        if (registered == desired) {
          _logger->debug(label + " is current: " + desired);
        } else {
          _logger->debug(label + " is '" + registered + "'; setting " + desired);
          registry.plugin->set_value(record, desired);
          _logger->info("set " + label + " to " + desired);
        }
        if (found != _retries.end()) {
          _logger->info(label + " recovered after " + std::to_string(found->second.failures) + " failures");
          _retries.erase(found);
        }
      } catch (const std::exception& error) {
        ++failures;
        auto& retry = _retries[label];
        ++retry.failures;
        const auto delay = backoff(retry.failures);
        retry.next = now + delay;
        _logger->error(label + ": " + error.what() + retry_note(retry.failures, delay));
      }
    }
  }
  return failures;
}

int Service::run() {
  shutdown_requested = 0;
  retry_requested = 0;
  struct sigaction stop{};
  stop.sa_handler = request_shutdown;
  sigemptyset(&stop.sa_mask);
  struct sigaction retry{};
  retry.sa_handler = request_retry;
  sigemptyset(&retry.sa_mask);
  struct sigaction previous_interrupt{};
  struct sigaction previous_terminate{};
  struct sigaction previous_hangup{};
  if (sigaction(SIGINT, &stop, &previous_interrupt) < 0 || sigaction(SIGTERM, &stop, &previous_terminate) < 0 ||
      sigaction(SIGHUP, &retry, &previous_hangup) < 0)
    throw std::system_error(errno, std::generic_category());  // LCOV_EXCL_LINE

  // The signals are blocked except inside the clock's wait, so one that
  // arrives between the flag checks and the wait still ends the wait.
  sigset_t handled;
  sigemptyset(&handled);
  sigaddset(&handled, SIGINT);
  sigaddset(&handled, SIGTERM);
  sigaddset(&handled, SIGHUP);
  sigset_t wait_mask;
  if (sigprocmask(SIG_BLOCK, &handled, &wait_mask) < 0) throw std::system_error(errno, std::generic_category());  // LCOV_EXCL_LINE
  // The caller's mask and handlers are put back however the loop ends.
  const auto restore = [&] {
    sigprocmask(SIG_SETMASK, &wait_mask, nullptr);
    sigaction(SIGINT, &previous_interrupt, nullptr);
    sigaction(SIGTERM, &previous_terminate, nullptr);
    sigaction(SIGHUP, &previous_hangup, nullptr);
  };

  // A failing source backs off and parks like a record; the next cycle is a
  // cadence after the last one ended, or the source's back-off.
  unsigned source_failures = 0;
  const auto attempt = [&] {
    try {
      reconcile();
      if (source_failures) _logger->info("the source recovered after " + std::to_string(source_failures) + " failures");
      source_failures = 0;
    } catch (const std::exception& error) {
      ++source_failures;
      _logger->error(error.what() + retry_note(source_failures, backoff(source_failures)));
    }
    return _clock.now() + (source_failures ? backoff(source_failures) : _cadence);
  };

  auto next_run = attempt();
  try {
    while (!shutdown_requested) {
      if (retry_requested) {
        retry_requested = 0;
        _logger->info("SIGHUP: retrying every record now");
        for (auto& [label, state] : _retries) state.next = {};
        next_run = attempt();
        continue;
      }
      if (_clock.now() >= next_run) {
        next_run = attempt();
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
