//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The loggers: the standard-stream logger's levels, streams and line format,
// as text or JSON with every string escaped, and the scoped logger's prefix
// and forwarding.

#include <gtest/gtest.h>

#include <iostream>
#include <memory>
#include <regex>
#include <sstream>

#include "fixtures/support.h"
#include "manannan/loggers/logger_scoped.h"
#include "manannan/loggers/logger_stdio.h"
#include "manannan/loggers/util.h"

namespace {
using manannan::loggers::LoggerScoped;
using manannan::loggers::LoggerStdIO;
using manannan::test::contains;
using manannan::test::RecordingLogger;

// Captures std::cout and std::cerr for the life of the object.
class CapturedStreams {
 public:
  CapturedStreams() : _out(std::cout.rdbuf(captured_out.rdbuf())), _err(std::cerr.rdbuf(captured_err.rdbuf())) {}
  ~CapturedStreams() {
    std::cout.rdbuf(_out);
    std::cerr.rdbuf(_err);
  }
  std::ostringstream captured_out;
  std::ostringstream captured_err;

 private:
  std::streambuf* _out;
  std::streambuf* _err;
};

TEST(Logger, ZuluTimeIsIso8601Utc) { EXPECT_TRUE(std::regex_match(manannan::loggers::get_zulu_time(), std::regex(R"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ)"))); }

TEST(Logger, StdioWritesEachLevelAtOrAboveItsThreshold) {
  CapturedStreams streams;
  LoggerStdIO logger(manannan::loggers::DEBUG);
  logger.debug("one");
  logger.info("two");
  logger.warn("three");
  logger.raw("four");
  logger.error("five");
  const auto out = streams.captured_out.str();
  EXPECT_TRUE(std::regex_search(out, std::regex(R"(Z \[DEBUG\] one\n)")));
  EXPECT_TRUE(std::regex_search(out, std::regex(R"(Z \[INFO \] two\n)")));
  EXPECT_TRUE(std::regex_search(out, std::regex(R"(Z \[WARN \] three\n)")));
  EXPECT_TRUE(std::regex_search(out, std::regex(R"(Z \[-----\] four\n)")));
  EXPECT_TRUE(std::regex_search(streams.captured_err.str(), std::regex(R"(Z \[ERROR\] five\n)")));
  EXPECT_EQ(logger.base_logger(), nullptr);
}

TEST(Logger, StdioDropsLevelsBelowItsThreshold) {
  CapturedStreams streams;
  LoggerStdIO logger(manannan::loggers::INFO);
  logger.debug("hidden");
  logger.set_level(manannan::loggers::ERROR);
  logger.info("hidden");
  logger.warn("hidden");
  logger.error("shown");
  EXPECT_EQ(streams.captured_out.str(), "");
  EXPECT_NE(streams.captured_err.str().find("shown"), std::string::npos);
}

TEST(Logger, StdioWritesJsonLinesWhenAsked) {
  CapturedStreams streams;
  LoggerStdIO logger(manannan::loggers::INFO, manannan::loggers::LogFormat::json);
  logger.info("said \"hi\"\\\n\t\x01");
  logger.raw("r");
  logger.error("e");
  logger.set_format(manannan::loggers::LogFormat::text);
  logger.warn("plain");
  const auto out = streams.captured_out.str();
  EXPECT_TRUE(std::regex_search(out, std::regex(R"(^\{"time":"[0-9T:Z-]+","level":"info","message":"said \\"hi\\"\\\\\\n\\t\\u0001"\}\n)"))) << out;
  EXPECT_TRUE(contains(out, "\"level\":\"raw\",\"message\":\"r\"}"));
  EXPECT_TRUE(std::regex_search(out, std::regex(R"(Z \[WARN \] plain\n)")));
  EXPECT_TRUE(contains(streams.captured_err.str(), "\"level\":\"error\",\"message\":\"e\"}"));
}

TEST(Logger, JsonStringsEscapeQuotesBackslashesAndControls) { EXPECT_EQ(manannan::loggers::json_string("a\"b\\c\rd\x1f"), "\"a\\\"b\\\\c\\rd\\u001f\""); }

TEST(Logger, ScopedPrefixesAndForwards) {
  auto base = std::make_shared<RecordingLogger>();
  LoggerScoped logger("plug", base);
  logger.debug("a");
  logger.info("b");
  logger.warn("c");
  logger.error("d");
  logger.raw("e");
  EXPECT_EQ(base->at(manannan::loggers::DEBUG), std::vector<std::string>{"(plug) a"});
  EXPECT_EQ(base->at(manannan::loggers::INFO), (std::vector<std::string>{"(plug) b", "(plug) e"}));
  EXPECT_EQ(base->at(manannan::loggers::WARN), std::vector<std::string>{"(plug) c"});
  EXPECT_EQ(base->at(manannan::loggers::ERROR), std::vector<std::string>{"(plug) d"});
  EXPECT_EQ(logger.base_logger(), base);
  logger.set_level(manannan::loggers::WARN);
  EXPECT_EQ(base->level, manannan::loggers::WARN);
}

TEST(Logger, ScopedWithoutABaseIgnoresLevelChanges) {
  LoggerScoped logger("plug", nullptr);
  EXPECT_NO_THROW(logger.set_level(manannan::loggers::DEBUG));
}
}  // namespace
