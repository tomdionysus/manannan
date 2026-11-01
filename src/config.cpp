#include "manannan/config.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <utility>
#include "manannan/duration.h"
namespace manannan {
loggers::LogLevel parse_log_level(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  if (value == "debug") return loggers::DEBUG;
  if (value == "info") return loggers::INFO;
  if (value == "warn") return loggers::WARN;
  if (value == "error") return loggers::ERROR;
  throw std::runtime_error("invalid log level '" + value + "'; expected debug, info, warn, or error");
}

YAML::Node resolve_yaml_root(const YAML::Node& document, const std::string& route) {
  // A default-constructed YAML::Node is a defined null node, so "not found"
  // must be an explicitly undefined node for callers' !node checks to work.
  const YAML::Node missing(YAML::NodeType::Undefined);
  YAML::Node node = document;
  std::istringstream parts(route);
  std::string part;
  while (std::getline(parts, part, '.')) {
    if (part.empty() || !node || !node.IsMap()) return missing;
    // Index through a const reference so a missing key cannot be added to the document.
    const YAML::Node next = std::as_const(node)[part];
    if (!next) return missing;
    node.reset(next);
  }
  return node;
}
Config load_config(const std::filesystem::path& path) {
  Config config;
  config.document = YAML::LoadFile(path.string());
  const auto service = config.document["manannan"];
  if (!service || !service.IsMap()) throw std::runtime_error("missing manannan configuration root");
  config.cadence = parse_duration(service["cadence"].as<std::string>());
  for (const auto& directory : service["plugin_directories"])
    config.plugin_directories.emplace_back(directory.as<std::string>());
  if (config.plugin_directories.empty()) throw std::runtime_error("no plugin directories configured");
  const auto source = service["source"];
  if (!source || !source.IsScalar()) throw std::runtime_error("manannan.source must name a source plugin");
  config.source_plugin = source.as<std::string>();
  if (service["registry"])
    throw std::runtime_error("manannan.registry is not a setting; list registry plugins under manannan.registries");
  const auto registries = service["registries"];
  if (!registries || !registries.IsSequence() || registries.size() == 0)
    throw std::runtime_error("manannan.registries must list at least one registry plugin");
  for (const auto& registry : registries) {
    const auto name = registry.as<std::string>();
    if (std::find(config.registry_plugins.begin(), config.registry_plugins.end(), name) != config.registry_plugins.end())
      throw std::runtime_error("registry listed twice: " + name);
    config.registry_plugins.push_back(name);
  }
  config.log_level = parse_log_level(service["log_level"].as<std::string>("info"));
  if (const auto* environment_level = std::getenv("LOG_LEVEL"); environment_level && *environment_level)
    config.log_level = parse_log_level(environment_level);
  return config;
}
}
