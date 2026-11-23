//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/daemon/config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "manannan/daemon/duration.h"
namespace manannan::daemon {
namespace {
std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

std::vector<std::string> split(const std::string& path) {
  std::vector<std::string> parts;
  std::istringstream stream(path);
  for (std::string part; std::getline(stream, part, '.');) parts.push_back(part);
  return parts;
}

// Merges over into target, a map: maps key by key, anything else replaced.
// The merge is made in place: assigning a merged copy to the document lost
// its manannan root under test, by way of yaml-cpp's Node assignment.
void merge_into(YAML::Node target, const YAML::Node& over) {
  for (const auto& entry : over) {
    const auto key = entry.first.as<std::string>();
    if (target[key].IsMap() && entry.second.IsMap())
      merge_into(target[key], entry.second);
    else
      target[key] = YAML::Clone(entry.second);
  }
}

// The dotted paths of every scalar or empty setting reached through mappings.
void scalar_paths(const YAML::Node& node, const std::string& prefix, std::vector<std::string>& paths) {
  for (const auto& entry : node) {
    const auto path = prefix.empty() ? entry.first.as<std::string>() : prefix + "." + entry.first.as<std::string>();
    if (entry.second.IsMap())
      scalar_paths(entry.second, path, paths);
    else if (entry.second.IsScalar() || entry.second.IsNull())
      paths.push_back(path);
  }
}

void set(YAML::Node node, const std::vector<std::string>& parts, std::size_t index, const std::string& value) {
  if (index + 1 == parts.size()) {
    node[parts[index]] = value;
    return;
  }
  if (!node[parts[index]].IsMap()) node[parts[index]] = YAML::Node(YAML::NodeType::Map);
  set(node[parts[index]], parts, index + 1, value);
}

void apply_overrides(YAML::Node& document, const Flags& flags) {
  std::vector<std::string> paths;
  scalar_paths(document, "", paths);
  for (const char* always : {"log.level", "log.format"})
    if (std::find(paths.begin(), paths.end(), always) == paths.end()) paths.emplace_back(always);
  std::map<std::string, std::string> by_flag;
  for (const auto& path : paths) {
    const auto [found, added] = by_flag.emplace(flag_name(path), path);
    if (!added) throw std::runtime_error("settings " + found->second + " and " + path + " would share the flag --" + found->first);
  }
  for (const auto& path : paths)
    if (const auto* value = std::getenv(environment_name(path).c_str()); value && *value) set(document, split(path), 0, value);
  for (const auto& [name, value] : flags) {
    const auto found = by_flag.find(name);
    if (found == by_flag.end()) throw std::runtime_error("unknown setting --" + name);
    set(document, split(found->second), 0, value);
  }
}

YAML::Node load_secrets(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::status(path, error);
  if (error || !std::filesystem::exists(status)) throw std::runtime_error("cannot read secrets file " + path.string());
  using std::filesystem::perms;
  if ((status.permissions() & (perms::group_all | perms::others_all)) != perms::none)
    throw std::runtime_error("secrets file " + path.string() + " must be readable by its owner only (chmod 600)");
  const auto secrets = YAML::LoadFile(path.string());
  if (!secrets.IsNull() && !secrets.IsMap()) throw std::runtime_error("secrets file " + path.string() + " is not a mapping");
  return secrets;
}
}  // namespace

std::string flag_name(const std::string& path) {
  auto name = path;
  std::replace(name.begin(), name.end(), '.', '-');
  std::replace(name.begin(), name.end(), '_', '-');
  return name;
}

std::string environment_name(const std::string& path) {
  std::string name = "MANANNAN_";
  for (const unsigned char character : path) name += (character == '.' || character == '-') ? '_' : static_cast<char>(std::toupper(character));
  return name;
}

loggers::LogLevel parse_log_level(std::string value) {
  value = lowercase(std::move(value));
  if (value == "all" || value == "debug") return loggers::DEBUG;
  if (value == "info") return loggers::INFO;
  if (value == "warn") return loggers::WARN;
  if (value == "error") return loggers::ERROR;
  throw std::runtime_error("invalid log level '" + value + "'; expected all, debug, info, warn or error");
}

loggers::LogFormat parse_log_format(std::string value) {
  value = lowercase(std::move(value));
  if (value == "text") return loggers::LogFormat::text;
  if (value == "json") return loggers::LogFormat::json;
  throw std::runtime_error("invalid log format '" + value + "'; expected text or json");
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

Config load_config(const std::filesystem::path& path, const std::optional<std::filesystem::path>& secrets, const Flags& flags) {
  Config config;
  config.document = YAML::LoadFile(path.string());
  if (!config.document.IsMap()) throw std::runtime_error(path.string() + " is not a mapping");
  if (secrets) {
    const auto secret_document = load_secrets(*secrets);
    if (secret_document.IsMap()) merge_into(config.document, secret_document);
  }
  apply_overrides(config.document, flags);

  const auto& document = std::as_const(config.document);
  const auto service = document["manannan"];
  if (!service || !service.IsMap()) throw std::runtime_error("missing manannan configuration root");
  if (service["log_level"]) throw std::runtime_error("manannan.log_level is not a setting; set log.level");
  config.cadence = parse_duration(service["cadence"].as<std::string>());
  for (const auto& directory : service["plugin_directories"]) config.plugin_directories.emplace_back(directory.as<std::string>());
  if (config.plugin_directories.empty()) throw std::runtime_error("no plugin directories configured");
  const auto source = service["source"];
  if (!source || !source.IsScalar()) throw std::runtime_error("manannan.source must name a source plugin");
  config.source_plugin = source.as<std::string>();
  if (service["registry"]) throw std::runtime_error("manannan.registry is not a setting; list registry plugins under manannan.registries");
  const auto registries = service["registries"];
  if (!registries || !registries.IsSequence() || registries.size() == 0) throw std::runtime_error("manannan.registries must list at least one registry plugin");
  for (const auto& registry : registries) {
    const auto name = registry.as<std::string>();
    if (std::find(config.registry_plugins.begin(), config.registry_plugins.end(), name) != config.registry_plugins.end())
      throw std::runtime_error("registry listed twice: " + name);
    config.registry_plugins.push_back(name);
  }
  const auto log = document["log"];
  if (log.IsDefined()) {
    if (!log.IsMap()) throw std::runtime_error("log must be a mapping");
    config.log_level = parse_log_level(log["level"].as<std::string>("info"));
    config.log_format = parse_log_format(log["format"].as<std::string>("text"));
  }
  return config;
}

}  // namespace manannan::daemon
