#include "manannan/service.h"
#include <cerrno>
#include <csignal>
#include <stdexcept>
#include <ctime>
#include <system_error>
#include <sys/select.h>
namespace manannan {
namespace {
volatile std::sig_atomic_t shutdown_requested{};

extern "C" void request_shutdown(int) {
  shutdown_requested = 1;
}

timespec to_timespec(std::chrono::steady_clock::duration duration) {
  const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(nanoseconds);
  return {static_cast<std::time_t>(seconds.count()), static_cast<long>((nanoseconds - seconds).count())};
}
}  // namespace
Service::Service(SourcePlugin& source, std::vector<Registry> registries, std::chrono::milliseconds cadence,
                 std::shared_ptr<loggers::Logger> logger)
    : source_private(source), registries_private(std::move(registries)), cadence_private(cadence),
      logger_private(std::move(logger)) {}
std::size_t Service::reconcile() {
  logger_private->debug("loading value from source");
  const auto desired = source_private.get_value();
  if (desired.empty()) throw std::runtime_error("source returned an empty value");
  logger_private->debug("source value is " + desired);
  std::size_t failures = 0;
  for (const auto& registry : registries_private) {
    for (const auto& record : registry.plugin->records()) {
      const auto label = registry.name + ":" + record;
      try {
        const auto registered = registry.plugin->get_value(record);
        if (registered == desired) { logger_private->debug(label + " is current: " + desired); continue; }
        logger_private->debug(label + " is '" + registered + "'; setting " + desired);
        registry.plugin->set_value(record, desired);
        logger_private->info("set " + label + " to " + desired);
      } catch (const std::exception& error) {
        ++failures;
        logger_private->error(label + ": " + error.what());
      }
    }
  }
  return failures;
}
int Service::run() {
  shutdown_requested = 0;
  struct sigaction action {};
  action.sa_handler = request_shutdown;
  sigemptyset(&action.sa_mask);
  struct sigaction previous_interrupt {};
  struct sigaction previous_terminate {};
  if (sigaction(SIGINT, &action, &previous_interrupt) < 0 || sigaction(SIGTERM, &action, &previous_terminate) < 0)
    throw std::system_error(errno, std::generic_category());  // LCOV_EXCL_LINE

  // The shutdown signals are blocked except inside pselect, so one that
  // arrives between the flag check and the wait still ends the wait.
  sigset_t shutdown_signals;
  sigemptyset(&shutdown_signals);
  sigaddset(&shutdown_signals, SIGINT);
  sigaddset(&shutdown_signals, SIGTERM);
  sigset_t wait_mask;
  if (sigprocmask(SIG_BLOCK, &shutdown_signals, &wait_mask) < 0)
    throw std::system_error(errno, std::generic_category());  // LCOV_EXCL_LINE
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
      logger_private->error(error.what());
    }
  };
  attempt();

  auto next_run = std::chrono::steady_clock::now() + cadence_private;
  while (!shutdown_requested) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_run) {
      attempt();
      next_run = std::chrono::steady_clock::now() + cadence_private;
      continue;
    }
    const auto timeout = to_timespec(next_run - now);
    if (pselect(0, nullptr, nullptr, nullptr, &timeout, &wait_mask) < 0 && errno != EINTR) {
      const auto error = errno;              // LCOV_EXCL_LINE
      restore();                             // LCOV_EXCL_LINE
      throw std::system_error(error, std::generic_category());  // LCOV_EXCL_LINE
    }
  }
  restore();
  logger_private->info("shutting down");
  return 0;
}
}  // namespace manannan
