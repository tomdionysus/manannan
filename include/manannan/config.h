#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>
#include "manannan/loggers/logger.h"
namespace manannan {
struct Config {
  YAML::Node document;
  std::vector<std::filesystem::path> plugin_directories;
  std::string source_plugin;
  std::vector<std::string> registry_plugins;
  std::chrono::milliseconds cadence;
  loggers::LogLevel log_level{loggers::INFO};
};
Config load_config(const std::filesystem::path& path);
loggers::LogLevel parse_log_level(std::string value);
YAML::Node resolve_yaml_root(const YAML::Node& document, const std::string& route);
}
