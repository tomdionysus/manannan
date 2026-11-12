// The service: each record of each registry is reconciled independently,
// unchanged records are not written, an empty source value is never
// published, and the loop runs on its cadence until SIGINT or SIGTERM,
// leaving the caller's signal state as it found it.

#include "manannan/service.h"

#include <gtest/gtest.h>
#include <signal.h>

#include <chrono>
#include <csignal>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "fixtures/support.h"

namespace {
using manannan::Service;
using manannan::test::contains;
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

TEST(Service, ReconcilesEveryRecordIndependently) {
  auto logger = std::make_shared<RecordingLogger>();
  FakeSource source;
  FakeRegistry dns;
  dns.values = {{"a.example.com", "192.0.2.10"}, {"b.example.org", "192.0.2.1"}, {"c.example.net", ""}};
  FakeRegistry other;
  other.values = {{"d.example.com", "192.0.2.1"}};
  Service service(source, {{"dns", &dns}, {"other", &other}}, std::chrono::minutes(5), logger);

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
  Service service(source, {{"dns", &dns}}, std::chrono::minutes(5), logger);
  EXPECT_THROW((void)service.reconcile(), std::runtime_error);
  EXPECT_EQ(dns.values["a.example.com"], "192.0.2.1");
}

class ServiceRun : public ::testing::TestWithParam<int> {};

TEST_P(ServiceRun, RunsOnItsCadenceUntilASignalAndRestoresSignalState) {
  sigset_t before;
  sigprocmask(SIG_SETMASK, nullptr, &before);
  struct sigaction handler_before{};
  sigaction(GetParam(), nullptr, &handler_before);

  auto logger = std::make_shared<RecordingLogger>();
  FakeSource source;
  source.fail_on = 2;  // a failed cycle is logged and the loop carries on
  source.signal_after = 4;
  source.signal_to_raise = GetParam();
  FakeRegistry dns;
  dns.values = {{"a.example.com", ""}};
  Service service(source, {{"dns", &dns}}, std::chrono::milliseconds(1), logger);

  EXPECT_EQ(service.run(), 0);
  EXPECT_EQ(source.calls, 4);
  EXPECT_EQ(dns.values["a.example.com"], "192.0.2.10");
  EXPECT_TRUE(logger->logged(manannan::loggers::ERROR, "source failed"));
  EXPECT_TRUE(logger->logged(manannan::loggers::INFO, "shutting down"));

  sigset_t after;
  sigprocmask(SIG_SETMASK, nullptr, &after);
  EXPECT_EQ(sigismember(&after, SIGTERM), sigismember(&before, SIGTERM));
  EXPECT_EQ(sigismember(&after, SIGINT), sigismember(&before, SIGINT));
  struct sigaction handler_after{};
  sigaction(GetParam(), nullptr, &handler_after);
  EXPECT_EQ(handler_after.sa_handler, handler_before.sa_handler);
}

INSTANTIATE_TEST_SUITE_P(Signals, ServiceRun, ::testing::Values(SIGTERM, SIGINT));
}  // namespace
