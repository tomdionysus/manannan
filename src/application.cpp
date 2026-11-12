#include "manannan/application.h"

#include <stdexcept>
#include <utility>

#include "manannan/config.h"
#include "manannan/plugin_loader.h"
#include "manannan/service.h"
#include "manannan/version.h"

namespace manannan {
int run_application(const std::vector<std::string>& arguments, std::shared_ptr<loggers::Logger> logger, std::ostream& out, std::ostream& err) {
  if (arguments.size() == 1 && arguments[0] == "--version") {
    out << "manannan " << version << "\n";
    return 0;
  }
  if (arguments.size() != 1) {
    err << "usage: manannan <config.yaml>\n       manannan --version\n";
    return 2;
  }
  try {
    const auto config = load_config(arguments[0]);
    logger->set_level(config.log_level);
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
    return Service(source, std::move(registries), config.cadence, logger).run();
  } catch (const std::exception& error) {
    logger->error(error.what());
    return 1;
  }
}
}  // namespace manannan
