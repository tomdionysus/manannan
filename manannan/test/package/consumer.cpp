//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// What a plugin author compiles against the installed manannan-dev headers: a
// source plugin and its descriptor. Run with the expected version, it checks
// the installed version header and the plugin ABI, and that the plugin works.
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "manannan/plugin.h"
#include "manannan/version.h"

namespace {
class Fixed final : public manannan::SourcePlugin {
 public:
  std::string get_value() override { return "192.0.2.1"; }
};
manannan::Plugin* create(const YAML::Node&, std::shared_ptr<manannan::loggers::Logger>) { return new Fixed; }
void destroy(manannan::Plugin* plugin) { delete plugin; }
const manannan::PluginDescriptor descriptor{manannan::plugin_abi_version, manannan::PluginType::source, "fixed", "source.fixed", create, destroy};
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: consumer <expected version>\n";
    return 2;
  }
  if (std::strcmp(manannan::version, argv[1]) != 0) {
    std::cerr << "installed version " << manannan::version << ", expected " << argv[1] << "\n";
    return 1;
  }
  if (descriptor.abi_version != 2) {
    std::cerr << "plugin ABI " << descriptor.abi_version << ", expected 2\n";
    return 1;
  }
  auto* plugin = descriptor.create(YAML::Node(), nullptr);
  const auto value = static_cast<manannan::SourcePlugin*>(plugin)->get_value();
  descriptor.destroy(plugin);
  if (value != "192.0.2.1") return 1;
  std::cout << "manannan " << manannan::version << ", plugin ABI " << descriptor.abi_version << ": consumer OK\n";
  return 0;
}
