#include "manannan/plugin_loader.h"
#include <dlfcn.h>
#include <set>
#include <stdexcept>
#include "manannan/config.h"
#include "manannan/loggers/logger_scoped.h"
namespace manannan {
LoadedPlugin::LoadedPlugin(void* library, const PluginDescriptor* descriptor, Plugin* instance)
    : library_private(library), descriptor_private(descriptor), instance_private(instance) {}
LoadedPlugin::~LoadedPlugin() { close(); }
LoadedPlugin::LoadedPlugin(LoadedPlugin&& other) noexcept
    : library_private(other.library_private), descriptor_private(other.descriptor_private), instance_private(other.instance_private) {
  other.library_private = nullptr; other.descriptor_private = nullptr; other.instance_private = nullptr;
}
LoadedPlugin& LoadedPlugin::operator=(LoadedPlugin&& other) noexcept {
  if (this != &other) { close(); library_private = other.library_private; descriptor_private = other.descriptor_private;
    instance_private = other.instance_private; other.library_private = nullptr; other.descriptor_private = nullptr; other.instance_private = nullptr; }
  return *this;
}
void LoadedPlugin::close() {
  if (instance_private && descriptor_private) descriptor_private->destroy(instance_private);
  instance_private = nullptr; descriptor_private = nullptr;
  if (library_private) dlclose(library_private);
  library_private = nullptr;
}
const PluginDescriptor& LoadedPlugin::descriptor() const { return *descriptor_private; }
Plugin& LoadedPlugin::instance() const { return *instance_private; }

PluginLoader::PluginLoader(std::shared_ptr<loggers::Logger> logger) : logger_private(std::move(logger)) {}
void PluginLoader::scan(const std::vector<std::filesystem::path>& directories) {
  std::set<std::filesystem::path> scanned;
  for (const auto& directory : directories) {
    logger_private->debug("scanning plugin directory " + directory.string());
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) { logger_private->warn("plugin directory unavailable: " + directory.string()); continue; }
    const auto canonical = std::filesystem::weakly_canonical(directory, error);
    if (!scanned.insert(error ? directory : canonical).second) { logger_private->debug("already scanned " + directory.string()); continue; }
    std::filesystem::directory_iterator entries(directory, error);
    if (error) { logger_private->warn("cannot read plugin directory " + directory.string() + ": " + error.message()); continue; }
    for (const auto& entry : entries) {
      if (!entry.is_regular_file(error) || entry.path().extension() != ".so") continue;
      void* library = dlopen(entry.path().c_str(), RTLD_NOW | RTLD_LOCAL);
      if (!library) { logger_private->warn("cannot inspect " + entry.path().string() + ": " + dlerror()); continue; }
      dlerror();
      auto get_descriptor = reinterpret_cast<GetPluginDescriptor>(dlsym(library, MANANNAN_PLUGIN_ENTRYPOINT));
      const char* error_message = dlerror();
      if (error_message || !get_descriptor) { dlclose(library); continue; }
      const auto* descriptor = get_descriptor();
      if (!descriptor || descriptor->abi_version != plugin_abi_version || !descriptor->name || !descriptor->yaml_root ||
          !descriptor->create || !descriptor->destroy) { logger_private->warn("invalid plugin: " + entry.path().string()); dlclose(library); continue; }
      const auto key = std::make_pair(descriptor->type, std::string(descriptor->name));
      if (discovered_private.contains(key)) { dlclose(library); throw std::runtime_error("duplicate plugin: " + std::string(descriptor->name)); }
      discovered_private.emplace(key, entry.path());
      logger_private->debug("discovered plugin " + std::string(descriptor->name));
      dlclose(library);
    }
  }
}
LoadedPlugin PluginLoader::create(PluginType type, const std::string& name, const YAML::Node& document) const {
  const auto found = discovered_private.find({type, name});
  if (found == discovered_private.end()) throw std::runtime_error("plugin not found: " + name);
  logger_private->debug("loading plugin " + name + " from " + found->second.string());
  void* library = dlopen(found->second.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!library) throw std::runtime_error("cannot load plugin " + name + ": " + dlerror());
  auto get_descriptor = reinterpret_cast<GetPluginDescriptor>(dlsym(library, MANANNAN_PLUGIN_ENTRYPOINT));
  const auto* descriptor = get_descriptor ? get_descriptor() : nullptr;
  if (!descriptor || descriptor->abi_version != plugin_abi_version || descriptor->type != type ||
      !descriptor->name || name != descriptor->name || !descriptor->yaml_root || !descriptor->create ||
      !descriptor->destroy) {
    dlclose(library);
    throw std::runtime_error("plugin changed while loading: " + name);
  }
  auto node = resolve_yaml_root(document, descriptor->yaml_root);
  if (!node) { const std::string root = descriptor->yaml_root; dlclose(library); throw std::runtime_error("plugin YAML root missing: " + root); }
  auto scoped_logger = std::make_shared<loggers::LoggerScoped>(name, logger_private);
  Plugin* instance = nullptr;
  // An exception thrown by the plugin may have its type information and
  // destructor inside the plugin, so it must be finished with before dlclose.
  std::string failure = "factory returned no object";
  try { instance = descriptor->create(node, scoped_logger); }
  catch (const std::exception& error) { failure = error.what(); }
  catch (...) { failure = "unknown exception"; }
  if (!instance) { dlclose(library); throw std::runtime_error("cannot create plugin " + name + ": " + failure); }
  logger_private->info("loaded plugin " + name);
  return LoadedPlugin(library, descriptor, instance);
}
}
