// Plugins for the loader's and the daemon's tests, built once per variant
// from this file (see tests/CMakeLists.txt).
//
// test_source returns its configured 'value'. test_registry holds the
// configured 'records' in memory; its 'create' setting makes the factory
// throw a std::exception ("throw"), throw something else ("throw_int") or
// return nothing ("null"). While MANANNAN_TEST_CHANGED is set, the descriptor
// claims to be the other kind of plugin, as a library replaced after the scan
// would. The VARIANT_* macros build the malformed libraries the scan skips.

#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "manannan/plugin.h"

namespace {

class TestSource final : public manannan::SourcePlugin {
 public:
  explicit TestSource(const YAML::Node& config) : value_(config["value"].as<std::string>("")) {}
  std::string get_value() override { return value_; }

 private:
  std::string value_;
};

class TestRegistry final : public manannan::RegistryPlugin {
 public:
  explicit TestRegistry(const YAML::Node& config) {
    for (const auto& record : config["records"]) values_[record.as<std::string>()] = "";
  }
  std::vector<std::string> records() const override {
    std::vector<std::string> names;
    for (const auto& [name, value] : values_) names.push_back(name);
    return names;
  }
  std::string get_value(const std::string& record) override { return values_.at(record); }
  void set_value(const std::string& record, const std::string& value) override { values_.at(record) = value; }

 private:
  std::map<std::string, std::string> values_;
};

manannan::Plugin* create(const YAML::Node& config, std::shared_ptr<manannan::loggers::Logger> logger) {
  logger->debug("create()");
#if defined(TEST_PLUGIN_SOURCE)
  return new TestSource(config);
#else
  const auto mode = config["create"].as<std::string>("");
  if (mode == "throw") throw std::runtime_error("refused by configuration");
  if (mode == "throw_int") throw 7;
  if (mode == "null") return nullptr;
  return new TestRegistry(config);
#endif
}

void destroy(manannan::Plugin* plugin) { delete plugin; }

#if defined(TEST_PLUGIN_SOURCE)
constexpr auto kind = manannan::PluginType::source;
constexpr auto other_kind = manannan::PluginType::registry;
constexpr const char* root = "source." TEST_PLUGIN_NAME;
#else
constexpr auto kind = manannan::PluginType::registry;
constexpr auto other_kind = manannan::PluginType::source;
constexpr const char* root = "registry." TEST_PLUGIN_NAME;
#endif

#if defined(VARIANT_WRONG_ABI)
constexpr std::uint32_t abi = manannan::plugin_abi_version + 1;
#else
constexpr std::uint32_t abi = manannan::plugin_abi_version;
#endif

#if defined(VARIANT_NO_CREATE)
const manannan::PluginDescriptor descriptor{abi, kind, TEST_PLUGIN_NAME, root, nullptr, destroy};
#else
const manannan::PluginDescriptor descriptor{abi, kind, TEST_PLUGIN_NAME, root, create, destroy};
#endif
const manannan::PluginDescriptor changed{abi, other_kind, TEST_PLUGIN_NAME, root, create, destroy};

}  // namespace

#if !defined(VARIANT_NO_ENTRYPOINT)
extern "C" __attribute__((visibility("default"))) const manannan::PluginDescriptor* manannan_plugin_descriptor() {
#if defined(VARIANT_NULL_DESCRIPTOR)
  return nullptr;
#else
  return std::getenv("MANANNAN_TEST_CHANGED") ? &changed : &descriptor;
#endif
}
#endif
