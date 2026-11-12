// The manannan configuration block: required settings, refusal of the old
// single-registry form and of duplicates, log level precedence, and dotted
// plugin roots that never modify the document.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "fixtures/support.h"
#include "manannan/config.h"

namespace {
using manannan::load_config;
using manannan::parse_log_level;
using manannan::resolve_yaml_root;
using manannan::test::contains;
using manannan::test::ScopedEnvironment;
using manannan::test::work_directory;
using manannan::test::write_file;

const std::string header =
    "manannan:\n  cadence: 5m\n  log_level: warn\n  source: test_source\n  plugin_directories: [/a, /b]\n";

std::string error_loading(const std::string& text) {
  const auto path = write_file(work_directory("config") / "manannan.yaml", text);
  try {
    (void)load_config(path);
  } catch (const std::exception& error) {
    return error.what();
  }
  return {};
}

TEST(Config, ParsesLogLevelsInAnyCase) {
  EXPECT_EQ(parse_log_level("debug"), manannan::loggers::DEBUG);
  EXPECT_EQ(parse_log_level("INFO"), manannan::loggers::INFO);
  EXPECT_EQ(parse_log_level("Warn"), manannan::loggers::WARN);
  EXPECT_EQ(parse_log_level("error"), manannan::loggers::ERROR);
  EXPECT_THROW((void)parse_log_level("verbose"), std::runtime_error);
}

TEST(Config, LoadsEverySetting) {
  ScopedEnvironment level("LOG_LEVEL", std::nullopt);
  const auto path = write_file(work_directory("config") / "manannan.yaml", header + "  registries: [first, second]\n");
  const auto config = load_config(path);
  EXPECT_EQ(config.cadence, std::chrono::minutes(5));
  EXPECT_EQ(config.log_level, manannan::loggers::WARN);
  EXPECT_EQ(config.source_plugin, "test_source");
  EXPECT_EQ(config.registry_plugins, (std::vector<std::string>{"first", "second"}));
  EXPECT_EQ(config.plugin_directories, (std::vector<std::filesystem::path>{"/a", "/b"}));
  EXPECT_TRUE(config.document["manannan"]);
}

TEST(Config, LogLevelDefaultsToInfo) {
  ScopedEnvironment level("LOG_LEVEL", std::nullopt);
  const auto path = write_file(work_directory("config") / "manannan.yaml",
                               "manannan:\n  cadence: 1s\n  source: s\n  plugin_directories: [.]\n  registries: [r]\n");
  EXPECT_EQ(load_config(path).log_level, manannan::loggers::INFO);
}

TEST(Config, LogLevelEnvironmentOverridesTheFileUnlessEmpty) {
  const auto path = write_file(work_directory("config") / "manannan.yaml", header + "  registries: [first]\n");
  {
    ScopedEnvironment level("LOG_LEVEL", "debug");
    EXPECT_EQ(load_config(path).log_level, manannan::loggers::DEBUG);
  }
  {
    ScopedEnvironment level("LOG_LEVEL", "");
    EXPECT_EQ(load_config(path).log_level, manannan::loggers::WARN);
  }
  {
    ScopedEnvironment level("LOG_LEVEL", "loud");
    EXPECT_THROW((void)load_config(path), std::runtime_error);
  }
}

TEST(Config, RefusesMissingOrMalformedSettings) {
  ScopedEnvironment level("LOG_LEVEL", std::nullopt);
  EXPECT_TRUE(contains(error_loading("other: 1\n"), "manannan configuration root"));
  EXPECT_TRUE(contains(error_loading("manannan: 5\n"), "manannan configuration root"));
  EXPECT_TRUE(contains(error_loading("manannan:\n  cadence: 5x\n"), "unit"));
  EXPECT_TRUE(contains(error_loading("manannan:\n  cadence: 5m\n  plugin_directories: []\n"), "plugin directories"));
  EXPECT_TRUE(contains(error_loading("manannan:\n  cadence: 5m\n  plugin_directories: [.]\n  source: [a]\n"),
                       "manannan.source"));
  EXPECT_TRUE(contains(error_loading(header + "  registry: first\n"), "manannan.registries"));
  EXPECT_TRUE(contains(error_loading(header), "at least one registry"));
  EXPECT_TRUE(contains(error_loading(header + "  registries: []\n"), "at least one registry"));
  EXPECT_TRUE(contains(error_loading(header + "  registries: first\n"), "at least one registry"));
  EXPECT_TRUE(contains(error_loading(header + "  registries: [first, first]\n"), "listed twice: first"));
  EXPECT_TRUE(contains(error_loading("manannan:\n  cadence: 1s\n  log_level: loud\n  source: s\n  plugin_directories: [.]\n"
                                     "  registries: [a]\n"),
                       "invalid log level"));
  EXPECT_FALSE(error_loading("manannan: [").empty());
}

TEST(Config, ResolvesDottedRootsWithoutChangingTheDocument) {
  const auto document = YAML::Load("source:\n  amazon_check_ip:\n    timeout: 10s\n  empty:\n  scalar: 3\n");
  EXPECT_EQ(resolve_yaml_root(document, "source.amazon_check_ip")["timeout"].as<std::string>(), "10s");
  EXPECT_TRUE(resolve_yaml_root(document, "source.empty"));
  EXPECT_FALSE(resolve_yaml_root(document, "registry.missing"));
  EXPECT_FALSE(resolve_yaml_root(document, "source.missing"));
  EXPECT_FALSE(resolve_yaml_root(document, "source..amazon_check_ip"));
  EXPECT_FALSE(resolve_yaml_root(document, "source.scalar.deeper"));
  EXPECT_FALSE(document["registry"]);
  EXPECT_EQ(document["source"].size(), 3u);
}
}  // namespace
