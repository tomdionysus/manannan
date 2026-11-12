# Writing plugins

Manannan loads two kinds of plugin at startup:

- A **source** produces the value to publish, for example the public IP address.
- A **registry** holds one or more records that should match that value, for
  example DNS records.

Each plugin is a shared library (`.so`) in one of the directories listed under
`manannan.plugin_directories`.

## Contract

Include `manannan/plugin.h` (in the `manannan-dev` package, or installed to
`/usr/local/include` by a source install) and implement one of these
interfaces:

```cpp
class SourcePlugin : public Plugin {
  virtual std::string get_value() = 0;
};

class RegistryPlugin : public Plugin {
  virtual std::vector<std::string> records() const = 0;
  virtual std::string get_value(const std::string& record) = 0;  // "" when unset
  virtual void set_value(const std::string& record, const std::string& value) = 0;
};
```

Export a descriptor with C linkage:

```cpp
const manannan::PluginDescriptor descriptor{
    manannan::plugin_abi_version, manannan::PluginType::registry,
    "my_registry", "registry.my_registry", create, destroy};

extern "C" __attribute__((visibility("default")))
const manannan::PluginDescriptor* manannan_plugin_descriptor() { return &descriptor; }
```

- `name` is what users list under `manannan.source` or `manannan.registries`.
- `yaml_root` is a dotted path into the configuration file. Manannan passes the
  node found there to `create`. Startup fails if the node is missing.
- `create` receives that node and a logger that prefixes messages with the
  plugin name. It returns a new plugin object, or throws with a message
  explaining what is wrong.
- `destroy` deletes an object returned by `create`.

## Behaviour

- On each cycle Manannan calls the source once. It then calls `get_value` for
  every record of every registry, and `set_value` for records whose value differs.
- An exception from `get_value` or `set_value` is logged against that record
  and does not affect other records.
- Plugins are loaded with `RTLD_LOCAL` and stay loaded until shutdown.
- The interface uses C++ types, so a plugin must be built with the same
  compiler family, C++ standard library and yaml-cpp as Manannan.
- The current ABI version is 2. Plugins built for another version are skipped.

## Building

Build the plugin as a CMake `MODULE` library without the `lib` prefix, and
install it into the plugin directory. See `plugins/aws_route53/CMakeLists.txt`
for an example.
