//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The configuration. YAML is the source of truth: config.yaml, then
// secrets.yaml merged over it (maps merge key by key; anything else
// replaces). Then any scalar setting in them, and log.level and log.format
// always, can be overridden: the key a.b_c by the environment variable
// MANANNAN_A_B_C, and over that by the flag --a-b-c.
#pragma once
#include <yaml-cpp/yaml.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "manannan/loggers/logger.h"
#include "manannan/loggers/logger_stdio.h"
namespace manannan::daemon {

struct Config {
  YAML::Node document;  // the merged settings, which each plugin reads its block from
  std::vector<std::filesystem::path> plugin_directories;
  std::string source_plugin;
  std::vector<std::string> registry_plugins;
  std::chrono::milliseconds cadence;
  loggers::LogLevel log_level{loggers::INFO};
  loggers::LogFormat log_format{loggers::LogFormat::text};
};

// Flag names without the leading "--", with their values, in order.
using Flags = std::vector<std::pair<std::string, std::string>>;

// Reads the configuration and, when given, the secrets file, which must be
// readable by its owner only; then applies the environment and the flags.
Config load_config(const std::filesystem::path& config, const std::optional<std::filesystem::path>& secrets, const Flags& flags = {});

loggers::LogLevel parse_log_level(std::string value);
loggers::LogFormat parse_log_format(std::string value);
YAML::Node resolve_yaml_root(const YAML::Node& document, const std::string& route);

// The flag and environment variable that override the setting at a dotted path.
std::string flag_name(const std::string& path);
std::string environment_name(const std::string& path);

}  // namespace manannan::daemon
