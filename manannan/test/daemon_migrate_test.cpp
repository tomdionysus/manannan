//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The migration from 0.3: manannan.log_level and LOG_LEVEL become log.level;
// the base credentials leave the configuration for secrets.yaml, completed
// from AWS_* in the environment file as the plugin's fallback did; the
// environment file is read as systemd reads it; config.yaml is 0644 and
// secrets.yaml 0600; the result loads. A variable with no place, a line that
// is not KEY=VALUE, or a file that is not a Manannan configuration is
// refused and nothing is written.

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>

#include "fixtures/support.h"
#include "manannan/daemon/config.h"
#include "manannan/daemon/migrate.h"

namespace {
using manannan::daemon::load_config;
using manannan::daemon::migrate;
using manannan::test::contains;
using manannan::test::ScopedEnvironment;
using manannan::test::work_directory;
using manannan::test::write_file;
using std::filesystem::perms;

const std::string old_config =
    "manannan:\n  cadence: 5m\n  log_level: warn\n  source: amazon_check_ip\n  registries: [aws_route53]\n  plugin_directories: [/p]\n"
    "registry:\n  aws_route53:\n    ttl: 60\n    credentials: {access_key_id: AKIDFILE}\n    records:\n      - {name: a.example.com, hosted_zone_id: Z1}\n";

class Migrate : public ::testing::Test {
 protected:
  std::string refusal(const std::string& config_text, const std::string& environment_text) {
    write_file(directory / "manannan.yaml", config_text);
    write_file(directory / "environment", environment_text);
    try {
      migrate(directory / "manannan.yaml", directory / "environment", directory / "config.yaml", directory / "secrets.yaml");
    } catch (const std::exception& error) {
      EXPECT_FALSE(std::filesystem::exists(directory / "config.yaml"));
      EXPECT_FALSE(std::filesystem::exists(directory / "secrets.yaml"));
      return error.what();
    }
    return {};
  }

  std::filesystem::path directory = work_directory("migrate");
  ScopedEnvironment level{"MANANNAN_LOG_LEVEL", std::nullopt};
};

TEST_F(Migrate, MovesTheKeysToSecretsAndTheLogLevelToLog) {
  write_file(directory / "manannan.yaml", old_config);
  write_file(directory / "environment",
             "# keys\n\nAWS_ACCESS_KEY_ID=AKIDENV\nAWS_SECRET_ACCESS_KEY=\"s3cret\"\nexport AWS_SESSION_TOKEN='token'\nLOG_LEVEL=\n");
  migrate(directory / "manannan.yaml", directory / "environment", directory / "config.yaml", directory / "secrets.yaml");

  const auto config = YAML::LoadFile((directory / "config.yaml").string());
  EXPECT_FALSE(config["manannan"]["log_level"]);
  EXPECT_EQ(config["log"]["level"].as<std::string>(), "warn");
  EXPECT_FALSE(config["registry"]["aws_route53"]["credentials"]);
  EXPECT_EQ(config["registry"]["aws_route53"]["records"].size(), 1u);
  const auto secrets = YAML::LoadFile((directory / "secrets.yaml").string());
  const auto credentials = secrets["registry"]["aws_route53"]["credentials"];
  EXPECT_EQ(credentials["access_key_id"].as<std::string>(), "AKIDFILE");  // the file's value comes first
  EXPECT_EQ(credentials["secret_access_key"].as<std::string>(), "s3cret");
  EXPECT_EQ(credentials["session_token"].as<std::string>(), "token");

  EXPECT_EQ(std::filesystem::status(directory / "config.yaml").permissions() & perms::all,
            perms::owner_read | perms::owner_write | perms::group_read | perms::others_read);
  EXPECT_EQ(std::filesystem::status(directory / "secrets.yaml").permissions() & perms::all, perms::owner_read | perms::owner_write);

  const auto loaded = load_config(directory / "config.yaml", directory / "secrets.yaml");
  EXPECT_EQ(loaded.log_level, manannan::loggers::WARN);
  EXPECT_EQ(loaded.document["registry"]["aws_route53"]["credentials"]["secret_access_key"].as<std::string>(), "s3cret");
}

TEST_F(Migrate, LogLevelInTheEnvironmentWinsAndAMissingFileIsEmpty) {
  write_file(directory / "manannan.yaml", old_config);
  write_file(directory / "environment", "LOG_LEVEL=debug\n");
  migrate(directory / "manannan.yaml", directory / "environment", directory / "config.yaml", directory / "secrets.yaml");
  EXPECT_EQ(YAML::LoadFile((directory / "config.yaml").string())["log"]["level"].as<std::string>(), "debug");

  write_file(directory / "plain.yaml", "manannan:\n  cadence: 5m\n");
  migrate(directory / "plain.yaml", directory / "absent", directory / "config.yaml", directory / "secrets.yaml");
  EXPECT_FALSE(YAML::LoadFile((directory / "config.yaml").string())["log"]);
  EXPECT_TRUE(YAML::LoadFile((directory / "secrets.yaml").string()).IsNull());
}

TEST_F(Migrate, RefusesWhatItCannotPlace) {
  const auto unplaced = refusal(old_config, "AWS_ACCESS_KEY_ID=A\nOFFICE_KEY=x\nOTHER=y\n");
  EXPECT_TRUE(contains(unplaced, "no place in secrets.yaml: OFFICE_KEY, OTHER")) << unplaced;
  EXPECT_TRUE(contains(refusal(old_config, "just words\n"), "cannot read the line 'just words'"));
  EXPECT_TRUE(contains(refusal(old_config, "=value\n"), "cannot read the line"));
  EXPECT_TRUE(contains(refusal("other: 1\n", ""), "is not a Manannan configuration"));
  EXPECT_TRUE(contains(refusal("- a\n", ""), "is not a Manannan configuration"));
}
}  // namespace
