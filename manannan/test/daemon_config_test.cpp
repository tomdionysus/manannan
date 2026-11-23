//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The configuration: the manannan block's required settings, refusal of the
// old forms and of duplicates; log.level and log.format; secrets.yaml merged
// over config.yaml (maps by key, all else replaced), readable by its owner
// only; overrides of any scalar setting by MANANNAN_<KEY> and, over that, by
// --<key>, with unknown flags and clashing names refused; and dotted plugin
// roots that never modify the document.

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "fixtures/support.h"
#include "manannan/daemon/config.h"

namespace {
using manannan::daemon::environment_name;
using manannan::daemon::flag_name;
using manannan::daemon::Flags;
using manannan::daemon::load_config;
using manannan::daemon::parse_log_format;
using manannan::daemon::parse_log_level;
using manannan::daemon::resolve_yaml_root;
using manannan::loggers::LogFormat;
using manannan::test::contains;
using manannan::test::ScopedEnvironment;
using manannan::test::work_directory;
using manannan::test::write_file;

const std::string header = "manannan:\n  cadence: 5m\n  source: test_source\n  plugin_directories: [/a, /b]\n";
const std::string valid = header + "  registries: [first, second]\n";

// Every test starts from an environment without the log overrides.
class Config : public ::testing::Test {
 protected:
  std::filesystem::path file(const std::string& name, const std::string& text) { return write_file(directory / name, text); }
  std::filesystem::path secrets(const std::string& text) {
    const auto path = file("secrets.yaml", text);
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace);
    return path;
  }
  std::string error_loading(const std::string& text, const Flags& flags = {}) {
    try {
      (void)load_config(file("config.yaml", text), std::nullopt, flags);
    } catch (const std::exception& error) {
      return error.what();
    }
    return {};
  }

  std::filesystem::path directory = work_directory("config");
  ScopedEnvironment level{"MANANNAN_LOG_LEVEL", std::nullopt};
  ScopedEnvironment format{"MANANNAN_LOG_FORMAT", std::nullopt};
};

TEST_F(Config, ParsesLogLevelsAndFormatsInAnyCase) {
  EXPECT_EQ(parse_log_level("all"), manannan::loggers::DEBUG);
  EXPECT_EQ(parse_log_level("debug"), manannan::loggers::DEBUG);
  EXPECT_EQ(parse_log_level("INFO"), manannan::loggers::INFO);
  EXPECT_EQ(parse_log_level("Warn"), manannan::loggers::WARN);
  EXPECT_EQ(parse_log_level("error"), manannan::loggers::ERROR);
  EXPECT_THROW((void)parse_log_level("verbose"), std::runtime_error);
  EXPECT_EQ(parse_log_format("text"), LogFormat::text);
  EXPECT_EQ(parse_log_format("JSON"), LogFormat::json);
  EXPECT_THROW((void)parse_log_format("xml"), std::runtime_error);
}

TEST_F(Config, LoadsEverySetting) {
  const auto config = load_config(file("config.yaml", valid + "log:\n  level: warn\n  format: json\n"), std::nullopt);
  EXPECT_EQ(config.cadence, std::chrono::minutes(5));
  EXPECT_EQ(config.log_level, manannan::loggers::WARN);
  EXPECT_EQ(config.log_format, LogFormat::json);
  EXPECT_EQ(config.source_plugin, "test_source");
  EXPECT_EQ(config.registry_plugins, (std::vector<std::string>{"first", "second"}));
  EXPECT_EQ(config.plugin_directories, (std::vector<std::filesystem::path>{"/a", "/b"}));
  EXPECT_TRUE(config.document["manannan"]);
}

TEST_F(Config, LogsAtInfoAsTextByDefault) {
  const auto config = load_config(file("config.yaml", valid), std::nullopt);
  EXPECT_EQ(config.log_level, manannan::loggers::INFO);
  EXPECT_EQ(config.log_format, LogFormat::text);
}

TEST_F(Config, RefusesMissingOrMalformedSettings) {
  EXPECT_TRUE(contains(error_loading("- a\n"), "is not a mapping"));
  EXPECT_TRUE(contains(error_loading("other: 1\n"), "manannan configuration root"));
  EXPECT_TRUE(contains(error_loading("manannan: 5\n"), "manannan configuration root"));
  EXPECT_TRUE(contains(error_loading("manannan:\n  cadence: 5x\n"), "unit"));
  EXPECT_TRUE(contains(error_loading("manannan:\n  cadence: 5m\n  plugin_directories: []\n"), "plugin directories"));
  EXPECT_TRUE(contains(error_loading("manannan:\n  cadence: 5m\n  plugin_directories: [.]\n  source: [a]\n"), "manannan.source"));
  EXPECT_TRUE(contains(error_loading(header + "  registry: first\n"), "manannan.registries"));
  EXPECT_TRUE(contains(error_loading(header), "at least one registry"));
  EXPECT_TRUE(contains(error_loading(header + "  registries: []\n"), "at least one registry"));
  EXPECT_TRUE(contains(error_loading(header + "  registries: first\n"), "at least one registry"));
  EXPECT_TRUE(contains(error_loading(header + "  registries: [first, first]\n"), "listed twice: first"));
  EXPECT_TRUE(contains(error_loading(valid + "  log_level: debug\n"), "set log.level"));
  EXPECT_TRUE(contains(error_loading(valid + "log: 5\n"), "log must be a mapping"));
  EXPECT_TRUE(contains(error_loading(valid + "log:\n  level: loud\n"), "invalid log level"));
  EXPECT_TRUE(contains(error_loading(valid + "log:\n  format: xml\n"), "invalid log format"));
  EXPECT_FALSE(error_loading("manannan: [").empty());
}

TEST_F(Config, MergesTheSecretsOverTheConfiguration) {
  const auto config = load_config(file("config.yaml", valid + "registry:\n  first:\n    ttl: 60\n    records: [a, b]\n"),
                                  secrets("registry:\n  first:\n    credentials: {key: k}\n    records: [c]\n"));
  const auto first = config.document["registry"]["first"];
  EXPECT_EQ(first["ttl"].as<int>(), 60);
  EXPECT_EQ(first["credentials"]["key"].as<std::string>(), "k");
  ASSERT_EQ(first["records"].size(), 1u);  // a list is replaced, not merged
  EXPECT_EQ(first["records"][0].as<std::string>(), "c");
  EXPECT_EQ(load_config(file("config.yaml", valid), secrets("# nothing yet\n")).registry_plugins.size(), 2u);
}

TEST_F(Config, RefusesSecretsOthersCanReadOrThatAreMissingOrMalformed) {
  const auto config = file("config.yaml", valid);
  const auto readable = file("readable.yaml", "a: 1\n");
  std::filesystem::permissions(readable, std::filesystem::perms::owner_read | std::filesystem::perms::group_read, std::filesystem::perm_options::replace);
  try {
    (void)load_config(config, readable);
    FAIL();
  } catch (const std::runtime_error& error) {
    EXPECT_TRUE(contains(error.what(), "readable by its owner only")) << error.what();
  }
  EXPECT_THROW((void)load_config(config, directory / "absent.yaml"), std::runtime_error);
  EXPECT_THROW((void)load_config(config, secrets("- a\n")), std::runtime_error);
}

TEST_F(Config, NamesTheOverridesAfterTheSetting) {
  EXPECT_EQ(flag_name("registry.aws_route53.signing_region"), "registry-aws-route53-signing-region");
  EXPECT_EQ(environment_name("registry.aws_route53.signing_region"), "MANANNAN_REGISTRY_AWS_ROUTE53_SIGNING_REGION");
  EXPECT_EQ(environment_name("log.level"), "MANANNAN_LOG_LEVEL");
}

TEST_F(Config, TheEnvironmentOverridesTheFilesAndFlagsOverrideTheEnvironment) {
  const auto path = file("config.yaml", valid + "log:\n  level: warn\nregistry:\n  first:\n    ttl: 60\n    region:\n");
  {
    ScopedEnvironment debug("MANANNAN_LOG_LEVEL", "debug");
    ScopedEnvironment ttl("MANANNAN_REGISTRY_FIRST_TTL", "300");
    ScopedEnvironment region("MANANNAN_REGISTRY_FIRST_REGION", "eu-west-1");
    const auto config = load_config(path, std::nullopt);
    EXPECT_EQ(config.log_level, manannan::loggers::DEBUG);
    EXPECT_EQ(config.document["registry"]["first"]["ttl"].as<int>(), 300);
    EXPECT_EQ(config.document["registry"]["first"]["region"].as<std::string>(), "eu-west-1");
    const auto flagged = load_config(path, std::nullopt, {{"log-level", "error"}, {"registry-first-ttl", "30"}, {"manannan-cadence", "1s"}});
    EXPECT_EQ(flagged.log_level, manannan::loggers::ERROR);
    EXPECT_EQ(flagged.document["registry"]["first"]["ttl"].as<int>(), 30);
    EXPECT_EQ(flagged.cadence, std::chrono::seconds(1));
  }
  {
    ScopedEnvironment empty("MANANNAN_LOG_LEVEL", "");
    EXPECT_EQ(load_config(path, std::nullopt).log_level, manannan::loggers::WARN);
  }
  // log.level and log.format can be set when the files leave them out.
  const auto bare = file("bare.yaml", valid);
  ScopedEnvironment json("MANANNAN_LOG_FORMAT", "json");
  EXPECT_EQ(load_config(bare, std::nullopt).log_format, LogFormat::json);
  EXPECT_EQ(load_config(bare, std::nullopt, {{"log-level", "all"}}).log_level, manannan::loggers::DEBUG);
}

TEST_F(Config, RefusesUnknownFlagsAndClashingNames) {
  EXPECT_TRUE(contains(error_loading(valid, {{"registry-first-ttl", "30"}}), "unknown setting --registry-first-ttl"));
  EXPECT_TRUE(contains(error_loading(valid + "a_b:\n  c: 1\na:\n  b_c: 2\n"), "would share the flag --a-b-c"));
}

TEST_F(Config, ResolvesDottedRootsWithoutChangingTheDocument) {
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
