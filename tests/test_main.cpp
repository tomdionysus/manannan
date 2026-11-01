#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>
#include "manannan/config.h"
#include "manannan/duration.h"
#include "manannan/plugin_loader.h"
#include "manannan/service.h"

namespace {
int failures = 0;

// assert() is compiled out in Release builds, so checks must not depend on NDEBUG.
#define CHECK(condition)                                                             \
  do {                                                                               \
    if (!(condition)) {                                                              \
      std::cerr << __FILE__ << ":" << __LINE__ << ": check failed: " #condition "\n"; \
      ++failures;                                                                    \
    }                                                                                \
  } while (false)

template <typename Function>
bool throws(Function function) {
  try { function(); } catch (...) { return true; }
  return false;
}

template <typename Function>
std::string error_of(Function function) {
  try { function(); } catch (const std::exception& error) { return error.what(); }
  return {};
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

class QuietLogger final : public manannan::loggers::Logger {
 public:
  void debug(const std::string&) override {}
  void info(const std::string& log) override { infos.push_back(log); }
  void warn(const std::string&) override {}
  void error(const std::string& log) override { errors.push_back(log); }
  void raw(const std::string&) override {}
  void set_level(manannan::loggers::LogLevel) override {}
  std::shared_ptr<Logger> base_logger() override { return nullptr; }
  std::vector<std::string> infos;
  std::vector<std::string> errors;
};

class FakeSource final : public manannan::SourcePlugin {
 public:
  std::string get_value() override { return value; }
  std::string value = "192.0.2.10";
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

void write_file(const std::filesystem::path& path, const std::string& text) {
  std::ofstream file(path);
  file << text;
}

void test_duration() {
  using namespace std::chrono_literals;
  CHECK(manannan::parse_duration("250ms") == 250ms);
  CHECK(manannan::parse_duration("5m") == 5min);
  CHECK(manannan::parse_duration("2h") == 2h);
  CHECK(throws([] { (void)manannan::parse_duration("0s"); }));
  CHECK(throws([] { (void)manannan::parse_duration("5"); }));
  CHECK(throws([] { (void)manannan::parse_duration("m"); }));
  CHECK(throws([] { (void)manannan::parse_duration("5d"); }));
  CHECK(throws([] { (void)manannan::parse_duration("18446744073709551615ms"); }));
  CHECK(throws([] { (void)manannan::parse_duration("99999999999999999999ms"); }));
}

void test_config() {
  CHECK(manannan::parse_log_level("debug") == manannan::loggers::DEBUG);
  CHECK(manannan::parse_log_level("INFO") == manannan::loggers::INFO);
  CHECK(manannan::parse_log_level("warn") == manannan::loggers::WARN);
  CHECK(manannan::parse_log_level("error") == manannan::loggers::ERROR);
  CHECK(throws([] { (void)manannan::parse_log_level("verbose"); }));

  const auto path = std::filesystem::path("manannan-test-config.yaml");
  const std::string header = "manannan:\n  cadence: 5m\n  log_level: warn\n  source: test_source\n"
                             "  plugin_directories: [.]\n";
  write_file(path, header + "  registries: [first, second]\n");
  unsetenv("LOG_LEVEL");
  const auto config = manannan::load_config(path);
  CHECK(config.log_level == manannan::loggers::WARN);
  CHECK((config.registry_plugins == std::vector<std::string>{"first", "second"}));
  setenv("LOG_LEVEL", "debug", 1);
  CHECK(manannan::load_config(path).log_level == manannan::loggers::DEBUG);
  setenv("LOG_LEVEL", "", 1);
  CHECK(manannan::load_config(path).log_level == manannan::loggers::WARN);
  unsetenv("LOG_LEVEL");

  write_file(path, header + "  registry: first\n");
  CHECK(contains(error_of([&] { (void)manannan::load_config(path); }), "registries"));
  write_file(path, header + "  registries: []\n");
  CHECK(throws([&] { (void)manannan::load_config(path); }));
  write_file(path, header + "  registries: [first, first]\n");
  CHECK(contains(error_of([&] { (void)manannan::load_config(path); }), "twice"));
  std::filesystem::remove(path);

  const auto yaml = YAML::Load("source:\n  amazon_check_ip:\n    timeout: 10s\n  empty:\n");
  const auto node = manannan::resolve_yaml_root(yaml, "source.amazon_check_ip");
  CHECK(node["timeout"].as<std::string>() == "10s");
  CHECK(!manannan::resolve_yaml_root(yaml, "registry.missing"));
  CHECK(!manannan::resolve_yaml_root(yaml, "source.missing"));
  CHECK(!manannan::resolve_yaml_root(yaml, "source..amazon_check_ip"));
  CHECK(manannan::resolve_yaml_root(yaml, "source.empty"));
}

void test_service() {
  auto logger = std::make_shared<QuietLogger>();
  FakeSource source;
  FakeRegistry dns;
  dns.values = {{"a.example.com", "192.0.2.10"}, {"b.example.org", "192.0.2.1"}, {"c.example.net", ""}};
  FakeRegistry other;
  other.values = {{"d.example.com", "192.0.2.1"}};
  manannan::Service service(source, {{"dns", &dns}, {"other", &other}}, std::chrono::minutes(5), logger);

  CHECK(service.reconcile() == 0);
  CHECK(dns.values["a.example.com"] == "192.0.2.10");
  CHECK(dns.values["b.example.org"] == "192.0.2.10");
  CHECK(dns.values["c.example.net"] == "192.0.2.10");
  CHECK(other.values["d.example.com"] == "192.0.2.10");
  CHECK(dns.writes == 2);
  CHECK(other.writes == 1);

  // Nothing changes on a second pass.
  CHECK(service.reconcile() == 0);
  CHECK(dns.writes == 2);

  // One failing record does not stop the others.
  source.value = "192.0.2.20";
  dns.failing = "b.example.org";
  CHECK(service.reconcile() == 1);
  CHECK(dns.values["a.example.com"] == "192.0.2.20");
  CHECK(dns.values["b.example.org"] == "192.0.2.10");
  CHECK(dns.values["c.example.net"] == "192.0.2.20");
  CHECK(other.values["d.example.com"] == "192.0.2.20");
  CHECK(logger->errors.size() == 1 && contains(logger->errors[0], "dns:b.example.org"));

  // An empty source value is never written to any record.
  source.value = "";
  CHECK(throws([&] { (void)service.reconcile(); }));
  CHECK(other.values["d.example.com"] == "192.0.2.20");
}

std::vector<std::string> route53_records(const std::string& registry_yaml, std::string* error = nullptr) {
  const auto document = YAML::Load("registry:\n  aws_route53:\n" + registry_yaml);
  manannan::PluginLoader loader(std::make_shared<QuietLogger>());
  loader.scan({MANANNAN_TEST_ROUTE53_DIR});
  try {
    auto loaded = loader.create(manannan::PluginType::registry, "aws_route53", document);
    return static_cast<manannan::RegistryPlugin&>(loaded.instance()).records();
  } catch (const std::exception& failure) {
    if (error) *error = failure.what();
    return {};
  }
}

void test_route53_config() {
  unsetenv("AWS_ACCESS_KEY_ID");
  unsetenv("AWS_SECRET_ACCESS_KEY");
  unsetenv("AWS_SESSION_TOKEN");
  const std::string base = "    credentials: {access_key_id: AKIDBASE, secret_access_key: base}\n";
  std::string error;

  // Several records in several zones; names are normalised.
  const auto records = route53_records(base +
      "    records:\n"
      "      - {name: Macnessa.Macha.Network., hosted_zone_id: /hostedzone/Z111}\n"
      "      - {name: gbni.athenasip.org, hosted_zone_id: Z222, ttl: 300}\n", &error);
  CHECK(error.empty());
  CHECK((records == std::vector<std::string>{"macnessa.macha.network", "gbni.athenasip.org"}));

  // A record with its own credentials needs no base credentials.
  error.clear();
  CHECK(route53_records(
      "    records:\n"
      "      - name: gbni.athenasip.org\n"
      "        hosted_zone_id: Z222\n"
      "        credentials: {access_key_id: AKIDOWN, secret_access_key: own}\n", &error).size() == 1);
  CHECK(error.empty());

  // Without base credentials, a record that relies on them is rejected...
  error.clear();
  CHECK(route53_records("    records:\n      - {name: a.example.com, hosted_zone_id: Z1}\n", &error).empty());
  CHECK(contains(error, "a.example.com") && contains(error, "no credentials"));

  // ...unless the AWS environment variables supply them.
  setenv("AWS_ACCESS_KEY_ID", "AKIDENV", 1);
  setenv("AWS_SECRET_ACCESS_KEY", "env", 1);
  error.clear();
  CHECK(route53_records("    records:\n      - {name: a.example.com, hosted_zone_id: Z1}\n", &error).size() == 1);
  CHECK(error.empty());
  unsetenv("AWS_ACCESS_KEY_ID");
  unsetenv("AWS_SECRET_ACCESS_KEY");

  // Per-record credentials are complete in themselves; they never borrow from the base.
  error.clear();
  CHECK(route53_records(base +
      "    records:\n"
      "      - name: a.example.com\n"
      "        hosted_zone_id: Z1\n"
      "        credentials: {access_key_id: AKIDOWN}\n", &error).empty());
  CHECK(contains(error, "secret_access_key"));

  // ${NAME} reads from the environment, and an unset variable counts as missing.
  setenv("MANANNAN_TEST_KEY", "AKIDVAR", 1);
  setenv("MANANNAN_TEST_SECRET", "var", 1);
  error.clear();
  CHECK(route53_records(
      "    records:\n"
      "      - name: a.example.com\n"
      "        hosted_zone_id: Z1\n"
      "        credentials: {access_key_id: '${MANANNAN_TEST_KEY}', secret_access_key: '${MANANNAN_TEST_SECRET}'}\n",
      &error).size() == 1);
  CHECK(error.empty());
  unsetenv("MANANNAN_TEST_SECRET");
  error.clear();
  CHECK(route53_records(
      "    records:\n"
      "      - name: a.example.com\n"
      "        hosted_zone_id: Z1\n"
      "        credentials: {access_key_id: '${MANANNAN_TEST_KEY}', secret_access_key: '${MANANNAN_TEST_SECRET}'}\n",
      &error).empty());
  unsetenv("MANANNAN_TEST_KEY");

  // Configuration mistakes are reported, not ignored.
  error.clear();
  CHECK(route53_records(base + "    records:\n      - {name: a.example.com, hosted_zone_id: Z1}\n"
                               "      - {name: A.example.com., hosted_zone_id: Z2}\n", &error).empty());
  CHECK(contains(error, "twice"));
  error.clear();
  CHECK(route53_records(base + "    records:\n      - {name: a.example.com, hosted_zone: Z1}\n", &error).empty());
  CHECK(contains(error, "hosted_zone"));
  error.clear();
  CHECK(route53_records(base + "    records:\n      - {name: a.example.com, hosted_zone_id: Z1, ttl: 0}\n", &error).empty());
  CHECK(contains(error, "ttl"));
  error.clear();
  CHECK(route53_records(base + "    records: []\n", &error).empty());
  CHECK(contains(error, "records"));
  error.clear();
  CHECK(route53_records(base + "    hosted_zone_id: Z1\n    record_name: a.example.com\n", &error).empty());
  CHECK(contains(error, "records"));
}
}  // namespace

int main() {
  test_duration();
  test_config();
  test_service();
  test_route53_config();
  if (failures) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "all tests passed\n";
  return 0;
}
