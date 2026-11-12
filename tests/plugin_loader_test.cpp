// The plugin loader: discovery skips what is not a plugin of this ABI and
// refuses two plugins of one kind and name; creation re-checks the library,
// needs the plugin's configuration root, reports a factory's failure in full,
// and unloads the library when the plugin is released.

#include "manannan/plugin_loader.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "fixtures/support.h"

namespace {
using manannan::LoadedPlugin;
using manannan::PluginLoader;
using manannan::PluginType;
using manannan::test::contains;
using manannan::test::RecordingLogger;
using manannan::test::ScopedEnvironment;
using manannan::test::test_plugin_dir;
using manannan::test::work_directory;
using manannan::test::write_file;

const auto good = test_plugin_dir / "good";
const auto registry_document = YAML::Load("registry:\n  test_registry:\n    records: [a, b]\n");

std::string creation_error(const PluginLoader& loader, PluginType type, const std::string& name, const YAML::Node& document) {
  try {
    (void)loader.create(type, name, document);
  } catch (const std::exception& error) {
    return error.what();
  }
  return {};
}

TEST(PluginLoader, CreatesADiscoveredPluginOfTheRightKind) {
  auto logger = std::make_shared<RecordingLogger>();
  PluginLoader loader(logger);
  loader.scan({good});
  auto loaded = loader.create(PluginType::registry, "test_registry", registry_document);
  EXPECT_STREQ(loaded.descriptor().name, "test_registry");
  EXPECT_EQ(loaded.descriptor().type, PluginType::registry);
  EXPECT_EQ(static_cast<manannan::RegistryPlugin&>(loaded.instance()).records(), (std::vector<std::string>{"a", "b"}));
  EXPECT_TRUE(logger->logged(manannan::loggers::INFO, "loaded plugin test_registry"));
  EXPECT_TRUE(logger->logged(manannan::loggers::DEBUG, "(test_registry) create()"));
}

TEST(PluginLoader, FindsPluginsOnlyByKindAndName) {
  PluginLoader loader(std::make_shared<RecordingLogger>());
  loader.scan({good});
  EXPECT_TRUE(contains(creation_error(loader, PluginType::source, "test_registry", registry_document), "not found"));
  EXPECT_TRUE(contains(creation_error(loader, PluginType::registry, "absent", registry_document), "not found: absent"));
}

TEST(PluginLoader, MovesOwnershipOfALoadedPlugin) {
  PluginLoader loader(std::make_shared<RecordingLogger>());
  loader.scan({good});
  auto first = loader.create(PluginType::registry, "test_registry", registry_document);
  auto* instance = &first.instance();
  LoadedPlugin moved(std::move(first));
  EXPECT_EQ(&moved.instance(), instance);
  auto other = loader.create(PluginType::source, "test_source", YAML::Load("source:\n  test_source: {value: x}\n"));
  other = std::move(moved);
  EXPECT_EQ(&other.instance(), instance);
  auto& self = other;
  other = std::move(self);
  EXPECT_EQ(&other.instance(), instance);
}

TEST(PluginLoader, SkipsDirectoriesItCannotUseAndScansEachOnce) {
  auto logger = std::make_shared<RecordingLogger>();
  PluginLoader loader(logger);
  const auto work = work_directory("loader-skip");
  write_file(work / "file", "");
  loader.scan({work / "absent", work / "file", good, good / "." / "", good});
  EXPECT_TRUE(logger->logged(manannan::loggers::WARN, "plugin directory unavailable: " + (work / "absent").string()));
  EXPECT_TRUE(logger->logged(manannan::loggers::WARN, "plugin directory unavailable: " + (work / "file").string()));
  EXPECT_TRUE(logger->logged(manannan::loggers::DEBUG, "already scanned"));
  EXPECT_NO_THROW((void)loader.create(PluginType::registry, "test_registry", registry_document));
}

TEST(PluginLoader, SkipsFilesThatAreNotPluginsOfThisAbi) {
  auto logger = std::make_shared<RecordingLogger>();
  PluginLoader loader(logger);
  const auto work = work_directory("loader-invalid");
  write_file(work / "garbage.so", "not a library");
  write_file(work / "notes.txt", "");
  std::filesystem::create_directory(work / "directory.so");
  loader.scan({work, test_plugin_dir / "no_entrypoint", test_plugin_dir / "null_descriptor", test_plugin_dir / "wrong_abi", test_plugin_dir / "no_create"});
  EXPECT_TRUE(logger->logged(manannan::loggers::WARN, "cannot inspect " + (work / "garbage.so").string()));
  EXPECT_EQ(logger->at(manannan::loggers::WARN).size(), 4u);  // garbage.so and three invalid descriptors
  EXPECT_TRUE(logger->logged(manannan::loggers::WARN, "invalid plugin: "));
  for (const auto type : {PluginType::source, PluginType::registry})
    EXPECT_TRUE(contains(creation_error(loader, type, "broken", registry_document), "not found"));
}

TEST(PluginLoader, RefusesTwoPluginsOfOneKindAndName) {
  PluginLoader loader(std::make_shared<RecordingLogger>());
  try {
    loader.scan({test_plugin_dir / "duplicate"});
    FAIL() << "expected a duplicate plugin error";
  } catch (const std::runtime_error& error) {
    EXPECT_STREQ(error.what(), "duplicate plugin: test_source");
  }
}

TEST(PluginLoader, NeedsThePluginsConfigurationRoot) {
  PluginLoader loader(std::make_shared<RecordingLogger>());
  loader.scan({good});
  EXPECT_EQ(creation_error(loader, PluginType::registry, "test_registry", YAML::Load("source: {}\n")), "plugin YAML root missing: registry.test_registry");
}

TEST(PluginLoader, ReportsWhyAFactoryFailed) {
  PluginLoader loader(std::make_shared<RecordingLogger>());
  loader.scan({good});
  const auto with = [](const std::string& mode) { return YAML::Load("registry:\n  test_registry:\n    create: " + mode + "\n"); };
  EXPECT_EQ(creation_error(loader, PluginType::registry, "test_registry", with("throw")), "cannot create plugin test_registry: refused by configuration");
  EXPECT_EQ(creation_error(loader, PluginType::registry, "test_registry", with("throw_int")), "cannot create plugin test_registry: unknown exception");
  EXPECT_EQ(creation_error(loader, PluginType::registry, "test_registry", with("null")), "cannot create plugin test_registry: factory returned no object");
}

TEST(PluginLoader, RechecksALibraryThatChangedAfterTheScan) {
  PluginLoader loader(std::make_shared<RecordingLogger>());
  loader.scan({good});
  ScopedEnvironment changed("MANANNAN_TEST_CHANGED", "1");
  EXPECT_EQ(creation_error(loader, PluginType::registry, "test_registry", registry_document), "plugin changed while loading: test_registry");
}

TEST(PluginLoader, ReportsALibraryRemovedAfterTheScan) {
  const auto work = work_directory("loader-removed");
  std::filesystem::copy_file(good / "manannan_test_registry.so", work / "manannan_test_registry.so");
  PluginLoader loader(std::make_shared<RecordingLogger>());
  loader.scan({work});
  std::filesystem::remove(work / "manannan_test_registry.so");
  EXPECT_TRUE(contains(creation_error(loader, PluginType::registry, "test_registry", registry_document), "cannot load plugin test_registry: "));
}
}  // namespace
