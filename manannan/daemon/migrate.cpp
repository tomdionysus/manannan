//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/daemon/migrate.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace manannan::daemon {
namespace {
std::string trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r");
  if (first == std::string::npos) return {};
  return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

// KEY=VALUE lines as systemd's EnvironmentFile reads them: blank lines and
// comments skipped, an optional "export", and one pair of matching quotes.
std::map<std::string, std::string> read_environment(const std::filesystem::path& path) {
  std::map<std::string, std::string> variables;
  std::ifstream file(path);
  for (std::string line; std::getline(file, line);) {
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    if (line.starts_with("export ")) line = trim(line.substr(7));
    const auto equals = line.find('=');
    if (equals == std::string::npos || equals == 0) throw std::runtime_error(path.string() + ": cannot read the line '" + line.substr(0, equals) + "'");
    auto value = trim(line.substr(equals + 1));
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) value = value.substr(1, value.size() - 2);
    variables[trim(line.substr(0, equals))] = value;
  }
  return variables;
}

void write(const std::filesystem::path& path, const std::string& text, std::filesystem::perms mode) {
  const auto staged = std::filesystem::path(path.string() + ".migrating");
  {
    std::ofstream create(staged, std::ios::trunc);
    if (!create) throw std::runtime_error("cannot write " + staged.string());
  }
  // Restricted before the contents go in, so the secrets are never readable by others.
  std::filesystem::permissions(staged, mode, std::filesystem::perm_options::replace);
  std::ofstream file(staged, std::ios::trunc);
  file << text;
  file.close();
  if (!file) throw std::runtime_error("cannot write " + staged.string());  // LCOV_EXCL_LINE
  std::filesystem::rename(staged, path);
}

std::string emit(const YAML::Node& node) {
  YAML::Emitter out;
  out << node;
  return std::string(out.c_str()) + "\n";
}
}  // namespace

void migrate(const std::filesystem::path& old_config, const std::filesystem::path& environment, const std::filesystem::path& config,
             const std::filesystem::path& secrets) {
  YAML::Node document = YAML::Clone(YAML::LoadFile(old_config.string()));
  if (!document.IsMap() || !document["manannan"].IsMap()) throw std::runtime_error(old_config.string() + " is not a Manannan configuration");
  auto variables = read_environment(environment);

  YAML::Node secret_document(YAML::NodeType::Map);
  auto service = document["manannan"];
  if (service["log_level"]) {
    document["log"]["level"] = service["log_level"].as<std::string>();
    service.remove("log_level");
  }
  if (const auto level = variables.find("LOG_LEVEL"); level != variables.end()) {
    if (!level->second.empty()) document["log"]["level"] = level->second;
    variables.erase(level);
  }

  YAML::Node credentials(YAML::NodeType::Map);
  if (document["registry"].IsMap() && document["registry"]["aws_route53"].IsMap()) {
    auto route53 = document["registry"]["aws_route53"];
    if (route53["credentials"].IsMap()) {
      credentials = YAML::Clone(route53["credentials"]);
      route53.remove("credentials");
    }
  }
  for (const auto& [variable, key] : {std::pair{"AWS_ACCESS_KEY_ID", "access_key_id"}, std::pair{"AWS_SECRET_ACCESS_KEY", "secret_access_key"},
                                      std::pair{"AWS_SESSION_TOKEN", "session_token"}}) {
    const auto found = variables.find(variable);
    if (found == variables.end()) continue;
    if (!found->second.empty() && (!credentials[key] || credentials[key].as<std::string>().empty())) credentials[key] = found->second;
    variables.erase(found);
  }
  if (!variables.empty()) {
    std::string names;
    for (const auto& [name, value] : variables) names += (names.empty() ? "" : ", ") + name;
    throw std::runtime_error(environment.string() + " sets variables with no place in secrets.yaml: " + names +
                             "; move them by hand (a per-record credentials block goes in secrets.yaml)");
  }
  if (credentials.size() > 0) secret_document["registry"]["aws_route53"]["credentials"] = credentials;

  using std::filesystem::perms;
  write(config, emit(document), perms::owner_read | perms::owner_write | perms::group_read | perms::others_read);
  write(secrets,
        "# Manannan's secrets, merged over config.yaml. Readable by its owner only.\n" + (secret_document.size() > 0 ? emit(secret_document) : std::string()),
        perms::owner_read | perms::owner_write);
}

}  // namespace manannan::daemon
