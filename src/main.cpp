#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "manannan/config.h"
#include "manannan/loggers/logger_stdio.h"
#include "manannan/plugin_loader.h"
#include "manannan/service.h"
int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "usage: manannan <config.yaml>\n"; return 2; }
  auto logger = std::make_shared<manannan::loggers::LoggerStdIO>(manannan::loggers::INFO);
  try {
    const auto config = manannan::load_config(argv[1]);
    logger->set_level(config.log_level);
    manannan::PluginLoader loader(logger);
    loader.scan(config.plugin_directories);
    // The loader has checked each descriptor's type. dynamic_cast is not used
    // because RTTI is not reliably shared with RTLD_LOCAL plugins (libc++ on
    // x86_64 compares type_info by address).
    auto source_loaded = loader.create(manannan::PluginType::source, config.source_plugin, config.document);
    auto& source = static_cast<manannan::SourcePlugin&>(source_loaded.instance());
    std::vector<manannan::LoadedPlugin> registries_loaded;
    std::vector<manannan::Registry> registries;
    for (const auto& name : config.registry_plugins) {
      registries_loaded.push_back(loader.create(manannan::PluginType::registry, name, config.document));
      auto* registry = static_cast<manannan::RegistryPlugin*>(&registries_loaded.back().instance());
      const auto records = registry->records();
      if (records.empty()) throw std::runtime_error("registry " + name + " has no records configured");
      for (const auto& record : records) logger->info("managing " + name + ":" + record);
      registries.push_back({name, registry});
    }
    logger->debug("Manannan started");
    return manannan::Service(source, std::move(registries), config.cadence, logger).run();
  } catch (const std::exception& error) { logger->error(error.what()); return 1; }
}
