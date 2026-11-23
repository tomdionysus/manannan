//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The service: each record of each registry is reconciled independently,
// unchanged records are not written, an empty source value is never
// published, and the loop runs at once and then every cadence on the injected
// clock until SIGINT or SIGTERM, leaving the caller's signal state as it
// found it, even when the wait fails. The system clock waits until its
// deadline or until a signal its wait mask unblocks.

#include <gtest/gtest.h>
#include <signal.h>

#include <chrono>
#include <csignal>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "fixtures/clock.h"
#include "fixtures/support.h"
#include "manannan/daemon/clock.h"
#include "manannan/daemon/service.h"

namespace {
using namespace std::chrono_literals;
using manannan::daemon::Service;
using manannan::daemon::SystemClock;
using manannan::test::contains;
using manannan::test::FakeClock;
using manannan::test::RecordingLogger;

class FakeSource final : public manannan::SourcePlugin {
 public:
  std::string get_value() override {
    ++calls;
    if (calls == signal_after) ::raise(signal_to_raise);
    if (calls == fail_on) throw std::runtime_error("source failed");
    return value;
  }
  std::string value = "192.0.2.10";
  int calls = 0;
  int fail_on = -1;
  int signal_after = -1;
  int signal_to_raise = SIGTERM;
};

class FakeRegistry final : public manannan::RegistryPlugin {
 public:
  std::vector<std::string> records() const override {
    std::vector<std::string> names;
    for (const auto& [name, value] : values) names.push_back(name);
    return names;
  }
  std::string get_value(const std::string& record) override {
    if (record == failing) throw std::runtime_error("lookup failed");
    return values.at(record);
  }
  void set_value(const std::string& record, const std::string& value) override {
    values.at(record) = value;
    ++writes;
  }
  std::map<std::string, std::string> values;
  std::string failing;
  int writes = 0;
};

// The process's signal mask and one signal's handler, to compare before and after.
struct SignalState {
  explicit SignalState(int signal) {
    sigprocmask(SIG_SETMASK, nullptr, &mask);
    sigaction(signal, nullptr, &action);
  }
  sigset_t mask{};
  struct sigaction action{};
};

void expect_restored(const SignalState& before, int signal) {
  const SignalState after(signal);
  EXPECT_EQ(sigismember(&after.mask, SIGTERM), sigismember(&before.mask, SIGTERM));
  EXPECT_EQ(sigismember(&after.mask, SIGINT), sigismember(&before.mask, SIGINT));
  EXPECT_EQ(after.action.sa_handler, before.action.sa_handler);
}

TEST(Service, ReconcilesEveryRecordIndependently) {
  auto logger = std::make_shared<RecordingLogger>();
  FakeSource source;
  FakeRegistry dns;
  dns.values = {{"a.example.com", "192.0.2.10"}, {"b.example.org", "192.0.2.1"}, {"c.example.net", ""}};
  FakeRegistry other;
  other.values = {{"d.example.com", "192.0.2.1"}};
  FakeClock clock;
  Service service(source, {{"dns", &dns}, {"other", &other}}, 5min, clock, logger);

  EXPECT_EQ(service.reconcile(), 0u);
  EXPECT_EQ(dns.values["b.example.org"], "192.0.2.10");
  EXPECT_EQ(dns.values["c.example.net"], "192.0.2.10");
  EXPECT_EQ(other.values["d.example.com"], "192.0.2.10");
  EXPECT_EQ(dns.writes, 2);
  EXPECT_EQ(other.writes, 1);
  EXPECT_TRUE(logger->logged(manannan::loggers::INFO, "set dns:b.example.org to 192.0.2.10"));

  EXPECT_EQ(service.reconcile(), 0u);
  EXPECT_EQ(dns.writes, 2);
  EXPECT_TRUE(logger->logged(manannan::loggers::DEBUG, "dns:a.example.com is current"));

  source.value = "192.0.2.20";
  dns.failing = "b.example.org";
  EXPECT_EQ(service.reconcile(), 1u);
  EXPECT_EQ(dns.values["a.example.com"], "192.0.2.20");
  EXPECT_EQ(dns.values["b.example.org"], "192.0.2.10");
  EXPECT_EQ(dns.values["c.example.net"], "192.0.2.20");
  EXPECT_EQ(other.values["d.example.com"], "192.0.2.20");
  const auto errors = logger->at(manannan::loggers::ERROR);
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_TRUE(contains(errors[0], "dns:b.example.org: lookup failed"));
}

TEST(Service, NeverPublishesAnEmptyValue) {
  auto logger = std::make_shared<RecordingLogger>();
  FakeSource source;
  source.value = "";
  FakeRegistry dns;
  dns.values = {{"a.example.com", "192.0.2.1"}};
  FakeClock clock;
  Service service(source, {{"dns", &dns}}, 5min, clock, logger);
  EXPECT_THROW((void)service.reconcile(), std::runtime_error);
  EXPECT_EQ(dns.values["a.example.com"], "192.0.2.1");
}

class ServiceRun : public ::testing::TestWithParam<int> {};

TEST_P(ServiceRun, RunsOnItsCadenceUntilASignalAndRestoresSignalState) {
  const SignalState before(GetParam());
  auto logger = std::make_shared<RecordingLogger>();
  FakeSource source;
  source.fail_on = 2;  // a failed cycle is logged and the loop carries on
  source.signal_after = 4;
  source.signal_to_raise = GetParam();
  FakeRegistry dns;
  dns.values = {{"a.example.com", ""}};
  FakeClock clock;
  const auto start = clock.now();
  Service service(source, {{"dns", &dns}}, 5min, clock, logger);

  EXPECT_EQ(service.run(), 0);
  // Once at start, then at each cadence; the signal raised during the fourth
  // cycle ends the wait that follows it.
  EXPECT_EQ(source.calls, 4);
  EXPECT_EQ(clock.deadlines, (std::vector<FakeClock::time_point>{start + 5min, start + 10min, start + 15min, start + 20min}));
  EXPECT_EQ(dns.values["a.example.com"], "192.0.2.10");
  EXPECT_TRUE(logger->logged(manannan::loggers::ERROR, "source failed"));
  EXPECT_TRUE(logger->logged(manannan::loggers::INFO, "shutting down"));
  expect_restored(before, GetParam());
}

INSTANTIATE_TEST_SUITE_P(Signals, ServiceRun, ::testing::Values(SIGTERM, SIGINT));

TEST(Service, CadenceRunsFromTheEndOfACycle) {
  auto logger = std::make_shared<RecordingLogger>();
  FakeSource source;
  source.signal_after = 2;
  FakeRegistry dns;
  dns.values = {{"a.example.com", ""}};
  FakeClock clock;
  const auto start = clock.now();
  // A slow first cycle: the next is a cadence after it ends, not after it began.
  class SlowRegistry final : public manannan::RegistryPlugin {
   public:
    explicit SlowRegistry(FakeClock& clock) : _clock(clock) {}
    std::vector<std::string> records() const override { return {"slow.example.com"}; }
    std::string get_value(const std::string&) override {
      _clock.advance(2min);
      return "192.0.2.10";
    }
    void set_value(const std::string&, const std::string&) override {}

   private:
    FakeClock& _clock;
  } slow(clock);
  Service service(source, {{"slow", &slow}}, 5min, clock, logger);
  EXPECT_EQ(service.run(), 0);
  ASSERT_FALSE(clock.deadlines.empty());
  EXPECT_EQ(clock.deadlines.front(), start + 7min);
}

TEST(Service, AFailedWaitRestoresSignalStateAndPropagates) {
  const SignalState before(SIGTERM);
  auto logger = std::make_shared<RecordingLogger>();
  FakeSource source;
  FakeRegistry dns;
  dns.values = {{"a.example.com", ""}};
  FakeClock clock;
  clock.fail_on_wait = 1;
  Service service(source, {{"dns", &dns}}, 5min, clock, logger);
  EXPECT_THROW((void)service.run(), std::runtime_error);
  expect_restored(before, SIGTERM);
}

TEST(SystemClock, ReturnsAtOnceForADeadlineThatHasPassed) {
  SystemClock clock;
  sigset_t mask;
  sigprocmask(SIG_SETMASK, nullptr, &mask);
  const auto start = clock.now();
  clock.wait_until(start - 1s, mask);
  EXPECT_GE(clock.now(), start);
  EXPECT_LT(clock.now() - start, 1s);
}

volatile std::sig_atomic_t woken{};
extern "C" void note_wake(int) { woken = 1; }

TEST(SystemClock, APendingSignalTheMaskUnblocksEndsTheWait) {
  struct sigaction action{};
  action.sa_handler = note_wake;
  sigemptyset(&action.sa_mask);
  struct sigaction previous_action{};
  ASSERT_EQ(sigaction(SIGUSR1, &action, &previous_action), 0);
  sigset_t usr1;
  sigemptyset(&usr1);
  sigaddset(&usr1, SIGUSR1);
  sigset_t wait_mask;
  ASSERT_EQ(sigprocmask(SIG_BLOCK, &usr1, &wait_mask), 0);
  woken = 0;
  ::raise(SIGUSR1);  // pending: blocked until the wait unblocks it

  SystemClock clock;
  const auto start = clock.now();
  clock.wait_until(start + 1h, wait_mask);
  EXPECT_EQ(woken, 1);
  EXPECT_LT(clock.now() - start, 1min);

  sigprocmask(SIG_SETMASK, &wait_mask, nullptr);
  sigaction(SIGUSR1, &previous_action, nullptr);
}
}  // namespace
