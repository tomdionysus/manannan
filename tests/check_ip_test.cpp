// The amazon_check_ip source, against a local endpoint: it returns the
// trimmed IPv4 address and refuses an error status, a body that is not an
// IPv4 address, an unreachable endpoint and a timeout that is not positive.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "fixtures/http_server.h"
#include "fixtures/support.h"
#include "manannan/plugin_loader.h"
#include "manannan/version.h"

namespace {
using manannan::PluginLoader;
using manannan::PluginType;
using manannan::test::check_ip_plugin_dir;
using manannan::test::contains;
using manannan::test::HttpRequest;
using manannan::test::HttpResponse;
using manannan::test::HttpServer;
using manannan::test::RecordingLogger;

class CheckIp : public ::testing::Test {
 protected:
  CheckIp() { loader.scan({check_ip_plugin_dir}); }

  manannan::LoadedPlugin create(const std::string& settings) {
    return loader.create(PluginType::source, "amazon_check_ip", YAML::Load("source:\n  amazon_check_ip: " + settings));
  }

  std::string value_from(const std::string& endpoint) {
    auto loaded = create("{endpoint: '" + endpoint + "', timeout_seconds: 5}");
    return static_cast<manannan::SourcePlugin&>(loaded.instance()).get_value();
  }

  std::string error_from(const std::string& endpoint) {
    try {
      (void)value_from(endpoint);
    } catch (const std::exception& error) {
      return error.what();
    }
    return {};
  }

  std::shared_ptr<RecordingLogger> logger = std::make_shared<RecordingLogger>();
  PluginLoader loader{logger};
};

TEST_F(CheckIp, ReturnsTheTrimmedAddress) {
  HttpServer server([](const HttpRequest&) { return HttpResponse{200, " 198.51.100.7\r\n"}; });
  EXPECT_EQ(value_from(server.url("/ip")), "198.51.100.7");
  const auto requests = server.requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].method, "GET");
  EXPECT_EQ(requests[0].target, "/ip");
  EXPECT_EQ(requests[0].header("user-agent"), std::string("Manannan/") + manannan::version);
  EXPECT_TRUE(logger->logged(manannan::loggers::DEBUG, "external address is 198.51.100.7"));
}

TEST_F(CheckIp, RefusesAnErrorStatus) {
  HttpServer server([](const HttpRequest&) { return HttpResponse{503, "198.51.100.7"}; });
  EXPECT_EQ(error_from(server.url()), "check IP returned HTTP 503");
}

TEST_F(CheckIp, RefusesABodyThatIsNotAnIpv4Address) {
  for (const auto* body : {"", "   \n", "2001:db8::1", "198.51.100.300", "<html>"}) {
    HttpServer server([body](const HttpRequest&) { return HttpResponse{200, body}; });
    EXPECT_EQ(error_from(server.url()), "check IP returned invalid IPv4 value") << body;
  }
}

TEST_F(CheckIp, ReportsAnUnreachableEndpoint) {
  const auto port = manannan::test::closed_port();
  EXPECT_TRUE(contains(error_from("http://127.0.0.1:" + std::to_string(port) + "/"), "check IP request failed: "));
}

TEST_F(CheckIp, NeedsAPositiveTimeout) {
  for (const auto* settings : {"{timeout_seconds: 0}", "{timeout_seconds: -1}"}) {
    try {
      (void)create(settings);
      FAIL() << settings;
    } catch (const std::runtime_error& error) {
      EXPECT_TRUE(contains(error.what(), "timeout_seconds must be positive")) << settings;
    }
  }
}

TEST_F(CheckIp, DefaultsToAmazon) {
  EXPECT_NO_THROW((void)create("{}"));
  EXPECT_TRUE(logger->logged(manannan::loggers::DEBUG, "initialising Amazon Check IP source"));
}
}  // namespace
