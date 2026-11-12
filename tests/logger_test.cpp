// The loggers: the standard-stream logger's levels, streams and line format,
// and the scoped logger's prefix and forwarding.

#include <gtest/gtest.h>

#include <iostream>
#include <memory>
#include <regex>
#include <sstream>

#include "fixtures/support.h"
#include "manannan/loggers/logger_scoped.h"
#include "manannan/loggers/logger_stdio.h"
#include "manannan/util.h"

namespace {
using manannan::loggers::LoggerScoped;
using manannan::loggers::LoggerStdIO;
using manannan::test::RecordingLogger;

// Captures std::cout and std::cerr for the life of the object.
class CapturedStreams {
 public:
  CapturedStreams() : out_(std::cout.rdbuf(captured_out.rdbuf())), err_(std::cerr.rdbuf(captured_err.rdbuf())) {}
  ~CapturedStreams() {
    std::cout.rdbuf(out_);
    std::cerr.rdbuf(err_);
  }
  std::ostringstream captured_out;
  std::ostringstream captured_err;

 private:
  std::streambuf* out_;
  std::streambuf* err_;
};

TEST(Logger, ZuluTimeIsIso8601Utc) { EXPECT_TRUE(std::regex_match(manannan::get_zulu_time(), std::regex(R"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ)"))); }

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
