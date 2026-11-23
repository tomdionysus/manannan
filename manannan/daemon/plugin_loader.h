#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "manannan/loggers/logger.h"
#include "manannan/plugin.h"
namespace manannan::daemon {
class LoadedPlugin {
 public:
  LoadedPlugin(void* library, const PluginDescriptor* descriptor, Plugin* instance);
  ~LoadedPlugin();
  LoadedPlugin(const LoadedPlugin&) = delete;
  LoadedPlugin& operator=(const LoadedPlugin&) = delete;
  LoadedPlugin(LoadedPlugin&& other) noexcept;
  LoadedPlugin& operator=(LoadedPlugin&& other) noexcept;
  const PluginDescriptor& descriptor() const;
  Plugin& instance() const;

 private:
  void close();
  void* _library{};
  const PluginDescriptor* _descriptor{};
  Plugin* _instance{};
};
class PluginLoader {
 public:
  explicit PluginLoader(std::shared_ptr<loggers::Logger> logger);
  void scan(const std::vector<std::filesystem::path>& directories);
  LoadedPlugin create(PluginType type, const std::string& name, const YAML::Node& document) const;

 private:
  // Only the path is kept: the descriptor's strings live in the library, which
  // is closed again after discovery.
  std::map<std::pair<PluginType, std::string>, std::filesystem::path> _discovered;
  std::shared_ptr<loggers::Logger> _logger;
};
}  // namespace manannan::daemon
