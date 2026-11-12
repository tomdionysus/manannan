#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "manannan/loggers/logger.h"
#include "manannan/plugin.h"
namespace manannan {
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
  void* library_private{};
  const PluginDescriptor* descriptor_private{};
  Plugin* instance_private{};
};
class PluginLoader {
 public:
  explicit PluginLoader(std::shared_ptr<loggers::Logger> logger);
  void scan(const std::vector<std::filesystem::path>& directories);
  LoadedPlugin create(PluginType type, const std::string& name, const YAML::Node& document) const;

 private:
  // Only the path is kept: the descriptor's strings live in the library, which
  // is closed again after discovery.
  std::map<std::pair<PluginType, std::string>, std::filesystem::path> discovered_private;
  std::shared_ptr<loggers::Logger> logger_private;
};
}  // namespace manannan
