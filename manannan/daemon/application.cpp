//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "manannan/daemon/application.h"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>

#include "manannan/daemon/clock.h"
#include "manannan/daemon/config.h"
#include "manannan/daemon/migrate.h"
#include "manannan/daemon/plugin_loader.h"
#include "manannan/daemon/service.h"
#include "manannan/loggers/logger_stdio.h"
#include "manannan/version.h"

namespace manannan::daemon {
namespace {
constexpr const char* usage =
    "usage: manannan [--config <config.yaml>] [--secrets <secrets.yaml>] [--<setting> <value>]...\n"
    "       manannan migrate <manannan.yaml> <environment> <config.yaml> <secrets.yaml>\n"
    "       manannan --version\n";

struct Options {
  std::filesystem::path config{MANANNAN_DEFAULT_CONFIG};
  std::optional<std::filesystem::path> secrets;
  Flags flags;
};

// --name value or --name=value; false on anything else.
bool parse_options(const std::vector<std::string>& arguments, Options& options) {
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const auto& argument = arguments[index];
    if (!argument.starts_with("--") || argument.size() == 2) return false;
    std::string name = argument.substr(2);
    std::string value;
    if (const auto equals = name.find('='); equals != std::string::npos) {
      value = name.substr(equals + 1);
      name.erase(equals);
    } else {
      if (++index == arguments.size()) return false;
      value = arguments[index];
    }
    if (name == "config")
      options.config = value;
    else if (name == "secrets")
      options.secrets = value;
    else
      options.flags.emplace_back(name, value);
  }
  // The installed secrets file is read when it is there; one that is named must be.
  if (!options.secrets && std::filesystem::exists(MANANNAN_DEFAULT_SECRETS)) options.secrets = MANANNAN_DEFAULT_SECRETS;
  return true;
}
}  // namespace

int run_application(const std::vector<std::string>& arguments, std::shared_ptr<loggers::Logger> logger, std::ostream& out, std::ostream& err) {
  if (arguments.size() == 1 && arguments[0] == "--version") {
    out << "manannan " << version << "\n";
    return 0;
  }
  if (!arguments.empty() && arguments[0] == "migrate") {
    if (arguments.size() != 5) {
      err << usage;
      return 2;
    }
    try {
      migrate(arguments[1], arguments[2], arguments[3], arguments[4]);
      out << "migrated " << arguments[1] << " and " << arguments[2] << " to " << arguments[3] << " and " << arguments[4] << "\n";
      return 0;
    } catch (const std::exception& error) {
      err << "manannan migrate: " << error.what() << "\n";
      return 1;
    }
  }
  Options options;
  if (!parse_options(arguments, options)) {
    err << usage;
    return 2;
  }
  try {
    const auto config = load_config(options.config, options.secrets, options.flags);
    logger->set_level(config.log_level);
    if (auto stdio = std::dynamic_pointer_cast<loggers::LoggerStdIO>(logger)) stdio->set_format(config.log_format);
    PluginLoader loader(logger);
    loader.scan(config.plugin_directories);
    // The loader has checked each descriptor's type. dynamic_cast is not used
    // because RTTI is not reliably shared with RTLD_LOCAL plugins (libc++ on
    // x86_64 compares type_info by address).
    auto source_loaded = loader.create(PluginType::source, config.source_plugin, config.document);
    auto& source = static_cast<SourcePlugin&>(source_loaded.instance());
    std::vector<LoadedPlugin> registries_loaded;
    std::vector<Registry> registries;
    for (const auto& name : config.registry_plugins) {
      registries_loaded.push_back(loader.create(PluginType::registry, name, config.document));
      auto* registry = static_cast<RegistryPlugin*>(&registries_loaded.back().instance());
      const auto records = registry->records();
      if (records.empty()) throw std::runtime_error("registry " + name + " has no records configured");
      for (const auto& record : records) logger->info("managing " + name + ":" + record);
      registries.push_back({name, registry});
    }
    logger->info("Manannan " + std::string(version) + " started");
    SystemClock clock;
    return Service(source, std::move(registries), config.cadence, clock, logger).run();
  } catch (const std::exception& error) {
    logger->error(error.what());
    return 1;
  }
}
}  // namespace manannan::daemon
