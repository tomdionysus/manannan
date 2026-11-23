//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The daemon end to end: usage and --version, refusal of a bad configuration,
// an unknown setting or a registry with no records, the migrate command, a
// full run of the bundled plugins against local check IP and Route 53
// endpoints until SIGTERM with the keys from a secrets file and a setting
// from a flag, and the executable's arguments, streams, exit status and JSON
// log format.

#include <gtest/gtest.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "fixtures/http_server.h"
#include "fixtures/support.h"
#include "manannan/daemon/application.h"
#include "manannan/version.h"

extern char** environ;

namespace {
using manannan::daemon::run_application;
using manannan::test::check_ip_plugin_dir;
using manannan::test::contains;
using manannan::test::HttpRequest;
using manannan::test::HttpResponse;
using manannan::test::HttpServer;
using manannan::test::RecordingLogger;
using manannan::test::route53_plugin_dir;
using manannan::test::ScopedEnvironment;
using manannan::test::test_plugin_dir;
using manannan::test::work_directory;
using manannan::test::write_file;

struct Outcome {
  int status;
  std::string out;
  std::string err;
  std::shared_ptr<RecordingLogger> logger;
};

Outcome run(const std::vector<std::string>& arguments) {
  ScopedEnvironment level("MANANNAN_LOG_LEVEL", std::nullopt);
  ScopedEnvironment format("MANANNAN_LOG_FORMAT", std::nullopt);
  auto logger = std::make_shared<RecordingLogger>();
  std::ostringstream out;
  std::ostringstream err;
  const auto status = run_application(arguments, logger, out, err);
  return {status, out.str(), err.str(), logger};
}

std::string configuration(const std::string& body) { return write_file(work_directory("application") / "config.yaml", body).string(); }

std::string secrets(const std::string& body) {
  const auto path = write_file(work_directory("application-secrets") / "secrets.yaml", body);
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace);
  return path.string();
}

const std::string no_records = "manannan:\n  cadence: 1s\n  source: test_source\n  registries: [test_registry]\n  plugin_directories: [" +
                               (test_plugin_dir / "good").string() + "]\nsource:\n  test_source: {value: x}\nregistry:\n  test_registry: {records: []}\n";

TEST(Application, PrintsItsVersion) {
  const auto outcome = run({"--version"});
  EXPECT_EQ(outcome.status, 0);
  EXPECT_EQ(outcome.out, std::string("manannan ") + manannan::version + "\n");
}

TEST(Application, RefusesArgumentsItDoesNotTake) {
  for (const auto& arguments : {std::vector<std::string>{"a.yaml"}, std::vector<std::string>{"--config"}, std::vector<std::string>{"--"},
                                std::vector<std::string>{"migrate", "a", "b"}}) {
    const auto outcome = run(arguments);
    EXPECT_EQ(outcome.status, 2);
    EXPECT_TRUE(contains(outcome.err, "usage: manannan [--config <config.yaml>]"));
  }
}

TEST(Application, RefusesAConfigurationItCannotUse) {
  for (const auto& arguments : {std::vector<std::string>{"--config", (work_directory("application") / "absent.yaml").string()},
                                std::vector<std::string>{"--config=" + configuration(no_records), "--no-such-setting", "1"}}) {
    const auto outcome = run(arguments);
    EXPECT_EQ(outcome.status, 1);
    EXPECT_EQ(outcome.logger->at(manannan::loggers::ERROR).size(), 1u);
  }
}

TEST(Application, RefusesARegistryWithNoRecords) {
  const auto outcome = run({"--config", configuration(no_records), "--log-level", "debug"});
  EXPECT_EQ(outcome.status, 1);
  EXPECT_TRUE(outcome.logger->logged(manannan::loggers::ERROR, "registry test_registry has no records configured"));
  EXPECT_EQ(outcome.logger->level, manannan::loggers::DEBUG);
}

TEST(Application, MigratesA03Installation) {
  const auto directory = work_directory("application-migrate");
  write_file(directory / "manannan.yaml", "manannan:\n  cadence: 5m\n  log_level: debug\n");
  write_file(directory / "environment", "AWS_SECRET_ACCESS_KEY=s\n");
  const auto paths = std::vector<std::string>{(directory / "manannan.yaml").string(), (directory / "environment").string(),
                                              (directory / "config.yaml").string(), (directory / "secrets.yaml").string()};
  auto outcome = run({"migrate", paths[0], paths[1], paths[2], paths[3]});
  EXPECT_EQ(outcome.status, 0) << outcome.err;
  EXPECT_TRUE(contains(outcome.out, "migrated"));
  EXPECT_TRUE(std::filesystem::exists(paths[3]));

  write_file(directory / "environment", "OTHER=1\n");
  outcome = run({"migrate", paths[0], paths[1], paths[2], paths[3]});
  EXPECT_EQ(outcome.status, 1);
  EXPECT_TRUE(contains(outcome.err, "manannan migrate: ")) << outcome.err;
}

TEST(Application, PublishesTheAddressUntilSignalled) {
  const auto main_thread = ::pthread_self();
  std::mutex mutex;
  std::string registered;
  int checks = 0;

  // The third check ends the run: by then the first cycle has written the
  // record and the second has found it current.
  HttpServer check_ip([&](const HttpRequest&) {
    if (++checks == 3) ::pthread_kill(main_thread, SIGTERM);
    return HttpResponse{200, "203.0.113.7\n"};
  });
  HttpServer route53([&](const HttpRequest& request) {
    std::lock_guard lock(mutex);
    if (request.method == "POST") {
      const auto start = request.body.find("<Value>") + 7;
      registered = request.body.substr(start, request.body.find("</Value>") - start);
      return HttpResponse{200, "<ChangeResourceRecordSetsResponse/>"};
    }
    return HttpResponse{200, registered.empty() ? std::string("<ListResourceRecordSetsResponse/>")
                                                : "<ResourceRecordSet><Name>home.example.com.</Name><Type>A</Type>"
                                                  "<ResourceRecords><ResourceRecord><Value>" +
                                                      registered +
                                                      "</Value></ResourceRecord></ResourceRecords>"
                                                      "</ResourceRecordSet>"};
  });

  // The keys come from the secrets file; the cadence from a flag.
  const auto outcome = run(
      {"--config",
       configuration("manannan:\n  cadence: 5m\n  source: amazon_check_ip\n  registries: [aws_route53]\n"
                     "  plugin_directories: [" +
                     check_ip_plugin_dir.string() + ", " + route53_plugin_dir.string() + "]\nsource:\n  amazon_check_ip:\n    endpoint: " + check_ip.url() +
                     "\nregistry:\n  aws_route53:\n    endpoint: " + route53.url() + "\n    records:\n      - {name: home.example.com, hosted_zone_id: Z1}\n"),
       "--secrets", secrets("registry:\n  aws_route53:\n    credentials: {access_key_id: AKID, secret_access_key: secret}\n"), "--manannan-cadence=10ms"});

  EXPECT_EQ(outcome.status, 0);
  EXPECT_EQ(registered, "203.0.113.7");
  EXPECT_TRUE(outcome.logger->logged(manannan::loggers::INFO, "managing aws_route53:home.example.com"));
  EXPECT_TRUE(outcome.logger->logged(manannan::loggers::INFO, std::string("Manannan ") + manannan::version + " started"));
  EXPECT_EQ(outcome.logger->at(manannan::loggers::INFO).size(), 6u);  // two loads, managing, started, set, shutdown
  EXPECT_TRUE(outcome.logger->logged(manannan::loggers::INFO, "set aws_route53:home.example.com to 203.0.113.7"));
  EXPECT_TRUE(outcome.logger->at(manannan::loggers::ERROR).empty());

  // One write, then reads only: the record is already current.
  int posts = 0;
  for (const auto& request : route53.requests()) posts += request.method == "POST";
  EXPECT_EQ(posts, 1);
}
// Runs the built daemon with the given arguments; returns its exit status and
// everything it wrote to standard output and standard error.
std::pair<int, std::string> run_daemon(const std::vector<std::string>& arguments) {
  int pipe_ends[2];
  if (::pipe(pipe_ends) != 0) return {-1, {}};
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, pipe_ends[0]);
  std::vector<std::string> storage{MANANNAN_TEST_DAEMON};
  storage.insert(storage.end(), arguments.begin(), arguments.end());
  std::vector<char*> argv;
  for (auto& argument : storage) argv.push_back(argument.data());
  argv.push_back(nullptr);
  pid_t child = 0;
  const auto spawned = posix_spawn(&child, MANANNAN_TEST_DAEMON, &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  ::close(pipe_ends[1]);
  std::string output;
  char buffer[1024];
  for (ssize_t received; (received = ::read(pipe_ends[0], buffer, sizeof(buffer))) > 0;) output.append(buffer, static_cast<std::size_t>(received));
  ::close(pipe_ends[0]);
  if (spawned != 0) return {-1, output};
  int status = 0;
  ::waitpid(child, &status, 0);
  return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, output};
}

TEST(Daemon, ReportsItsVersionAndUsage) {
  EXPECT_EQ(run_daemon({"--version"}), (std::pair<int, std::string>{0, std::string("manannan ") + manannan::version + "\n"}));
  const auto [status, output] = run_daemon({"stray"});
  EXPECT_EQ(status, 2);
  EXPECT_TRUE(contains(output, "usage: manannan [--config <config.yaml>]"));
}

TEST(Daemon, LogsWhyItCannotStart) {
  const auto [status, output] = run_daemon({"--config", (work_directory("daemon") / "absent.yaml").string()});
  EXPECT_EQ(status, 1);
  EXPECT_TRUE(contains(output, "[ERROR] ")) << output;
}

TEST(Daemon, LogsAsJsonWhenAsked) {
  ScopedEnvironment format("MANANNAN_LOG_FORMAT", "json");
  const auto [status, output] = run_daemon({"--config", configuration(no_records)});
  EXPECT_EQ(status, 1);
  EXPECT_TRUE(std::regex_search(output, std::regex(R"(\{"time":"[0-9T:Z-]+","level":"error","message":"registry test_registry has no records configured"\})")))
      << output;
}
}  // namespace
