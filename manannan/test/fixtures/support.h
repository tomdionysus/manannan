#pragma once
// Shared test support: a logger that records what it is given, environment
// variables set for one scope, files written inside the build tree, and the
// directories the build placed the plugins in.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "manannan/loggers/logger.h"

namespace manannan::test {

class RecordingLogger final : public loggers::Logger {
 public:
  struct Entry {
    loggers::LogLevel level;
    std::string text;
  };

  void debug(const std::string& log) override { add(loggers::DEBUG, log); }
  void info(const std::string& log) override { add(loggers::INFO, log); }
  void warn(const std::string& log) override { add(loggers::WARN, log); }
  void error(const std::string& log) override { add(loggers::ERROR, log); }
  void raw(const std::string& log) override { add(loggers::INFO, log); }
  void set_level(loggers::LogLevel new_level) override { level = new_level; }
  std::shared_ptr<Logger> base_logger() override { return nullptr; }

  // Every message at the given level, in order.
  std::vector<std::string> at(loggers::LogLevel wanted) const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> result;
    for (const auto& entry : entries_)
      if (entry.level == wanted) result.push_back(entry.text);
    return result;
  }

  bool logged(loggers::LogLevel wanted, const std::string& part) const {
    for (const auto& text : at(wanted))
      if (text.find(part) != std::string::npos) return true;
    return false;
  }

  std::optional<loggers::LogLevel> level;

 private:
  void add(loggers::LogLevel level_of, const std::string& text) {
    std::lock_guard lock(mutex_);
    entries_.push_back({level_of, text});
  }

  mutable std::mutex mutex_;
  std::vector<Entry> entries_;
};

// Sets (or, given nullopt, unsets) an environment variable until the end of the scope.
class ScopedEnvironment {
 public:
  ScopedEnvironment(std::string name, const std::optional<std::string>& value) : name_(std::move(name)) {
    if (const auto* old = std::getenv(name_.c_str())) previous_ = old;
    if (value)
      ::setenv(name_.c_str(), value->c_str(), 1);
    else
      ::unsetenv(name_.c_str());
  }
  ~ScopedEnvironment() {
    if (previous_)
      ::setenv(name_.c_str(), previous_->c_str(), 1);
    else
      ::unsetenv(name_.c_str());
  }
  ScopedEnvironment(const ScopedEnvironment&) = delete;
  ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

 private:
  std::string name_;
  std::optional<std::string> previous_;
};

// A fresh directory for one test, inside the build tree.
inline std::filesystem::path work_directory(const std::string& name) {
  const auto path = std::filesystem::path(MANANNAN_TEST_WORK_DIR) / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

inline std::filesystem::path write_file(const std::filesystem::path& path, const std::string& text) {
  std::ofstream file(path, std::ios::trunc);
  file << text;
  return path;
}

inline bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

inline const std::filesystem::path check_ip_plugin_dir{MANANNAN_TEST_CHECK_IP_DIR};
inline const std::filesystem::path route53_plugin_dir{MANANNAN_TEST_ROUTE53_DIR};
inline const std::filesystem::path test_plugin_dir{MANANNAN_TEST_PLUGIN_DIR};

}  // namespace manannan::test
