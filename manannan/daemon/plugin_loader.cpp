#include "manannan/daemon/plugin_loader.h"

#include <dlfcn.h>

#include <set>
#include <stdexcept>

#include "manannan/daemon/config.h"
#include "manannan/loggers/logger_scoped.h"
namespace manannan::daemon {
LoadedPlugin::LoadedPlugin(void* library, const PluginDescriptor* descriptor, Plugin* instance)
    : _library(library), _descriptor(descriptor), _instance(instance) {}
LoadedPlugin::~LoadedPlugin() { close(); }
LoadedPlugin::LoadedPlugin(LoadedPlugin&& other) noexcept : _library(other._library), _descriptor(other._descriptor), _instance(other._instance) {
  other._library = nullptr;
  other._descriptor = nullptr;
  other._instance = nullptr;
}
LoadedPlugin& LoadedPlugin::operator=(LoadedPlugin&& other) noexcept {
  if (this != &other) {
    close();
    _library = other._library;
    _descriptor = other._descriptor;
    _instance = other._instance;
    other._library = nullptr;
    other._descriptor = nullptr;
    other._instance = nullptr;
  }
  return *this;
}
void LoadedPlugin::close() {
  if (_instance && _descriptor) _descriptor->destroy(_instance);
  _instance = nullptr;
  _descriptor = nullptr;
  if (_library) dlclose(_library);
  _library = nullptr;
}
const PluginDescriptor& LoadedPlugin::descriptor() const { return *_descriptor; }
Plugin& LoadedPlugin::instance() const { return *_instance; }

PluginLoader::PluginLoader(std::shared_ptr<loggers::Logger> logger) : _logger(std::move(logger)) {}
void PluginLoader::scan(const std::vector<std::filesystem::path>& directories) {
  std::set<std::filesystem::path> scanned;
  for (const auto& directory : directories) {
    _logger->debug("scanning plugin directory " + directory.string());
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
      _logger->warn("plugin directory unavailable: " + directory.string());
      continue;
    }
    const auto canonical = std::filesystem::weakly_canonical(directory, error);
    if (!scanned.insert(error ? directory : canonical).second) {
      _logger->debug("already scanned " + directory.string());
      continue;
    }
    std::filesystem::directory_iterator entries(directory, error);
    if (error) {
      _logger->warn("cannot read plugin directory " + directory.string() + ": " + error.message());
      continue;
    }
    for (const auto& entry : entries) {
      if (!entry.is_regular_file(error) || entry.path().extension() != ".so") continue;
      void* library = dlopen(entry.path().c_str(), RTLD_NOW | RTLD_LOCAL);
      if (!library) {
        _logger->warn("cannot inspect " + entry.path().string() + ": " + dlerror());
        continue;
      }
      dlerror();
      auto get_descriptor = reinterpret_cast<GetPluginDescriptor>(dlsym(library, MANANNAN_PLUGIN_ENTRYPOINT));
      const char* error_message = dlerror();
      if (error_message || !get_descriptor) {
        dlclose(library);
        continue;
      }
      const auto* descriptor = get_descriptor();
      if (!descriptor || descriptor->abi_version != plugin_abi_version || !descriptor->name || !descriptor->yaml_root || !descriptor->create ||
          !descriptor->destroy) {
        _logger->warn("invalid plugin: " + entry.path().string());
        dlclose(library);
        continue;
      }
      // The descriptor's strings live in the library: copy them before it is closed.
      const auto key = std::make_pair(descriptor->type, std::string(descriptor->name));
      if (_discovered.contains(key)) {
        dlclose(library);
        throw std::runtime_error("duplicate plugin: " + key.second);
      }
      _discovered.emplace(key, entry.path());
      _logger->debug("discovered plugin " + key.second);
      dlclose(library);
    }
  }
}
LoadedPlugin PluginLoader::create(PluginType type, const std::string& name, const YAML::Node& document) const {
  const auto found = _discovered.find({type, name});
  if (found == _discovered.end()) throw std::runtime_error("plugin not found: " + name);
  _logger->debug("loading plugin " + name + " from " + found->second.string());
  void* library = dlopen(found->second.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!library) throw std::runtime_error("cannot load plugin " + name + ": " + dlerror());
  auto get_descriptor = reinterpret_cast<GetPluginDescriptor>(dlsym(library, MANANNAN_PLUGIN_ENTRYPOINT));
  const auto* descriptor = get_descriptor ? get_descriptor() : nullptr;
  if (!descriptor || descriptor->abi_version != plugin_abi_version || descriptor->type != type || !descriptor->name || name != descriptor->name ||
      !descriptor->yaml_root || !descriptor->create || !descriptor->destroy) {
    dlclose(library);
    throw std::runtime_error("plugin changed while loading: " + name);
  }
  auto node = resolve_yaml_root(document, descriptor->yaml_root);
  if (!node) {
    const std::string root = descriptor->yaml_root;
    dlclose(library);
    throw std::runtime_error("plugin YAML root missing: " + root);
  }
  auto scoped_logger = std::make_shared<loggers::LoggerScoped>(name, _logger);
  Plugin* instance = nullptr;
  // An exception thrown by the plugin may have its type information and
  // destructor inside the plugin, so it must be finished with before dlclose.
  std::string failure = "factory returned no object";
  try {
    instance = descriptor->create(node, scoped_logger);
  } catch (const std::exception& error) {
    failure = error.what();
  } catch (...) {
    failure = "unknown exception";
  }
  if (!instance) {
    dlclose(library);
    throw std::runtime_error("cannot create plugin " + name + ": " + failure);
  }
  _logger->info("loaded plugin " + name);
  return LoadedPlugin(library, descriptor, instance);
}
}  // namespace manannan::daemon
