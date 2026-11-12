// The aws_route53 registry. Configuration: records across zones with
// normalised names, the credential cascade (base block, AWS_* environment,
// complete per-record blocks, ${NAME} expansion) and every refusal. Requests,
// against a local endpoint: the signed GET that finds the record's A value
// among other sets, the UPSERT that sets it, and error statuses.

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "fixtures/http_server.h"
#include "fixtures/support.h"
#include "manannan/plugin_loader.h"
#include "manannan/version.h"

namespace {
using manannan::PluginLoader;
using manannan::PluginType;
using manannan::RegistryPlugin;
using manannan::test::contains;
using manannan::test::HttpRequest;
using manannan::test::HttpResponse;
using manannan::test::HttpServer;
using manannan::test::RecordingLogger;
using manannan::test::route53_plugin_dir;
using manannan::test::ScopedEnvironment;

const std::string base_credentials = "    credentials: {access_key_id: AKIDBASE, secret_access_key: base}\n";

class Route53 : public ::testing::Test {
 protected:
  Route53() { loader.scan({route53_plugin_dir}); }

  manannan::LoadedPlugin create(const std::string& settings) {
    return loader.create(PluginType::registry, "aws_route53", YAML::Load("registry:\n  aws_route53:\n" + settings));
  }

  std::vector<std::string> records(const std::string& settings) {
    auto loaded = create(settings);
    return static_cast<RegistryPlugin&>(loaded.instance()).records();
  }

  std::string error(const std::string& settings) {
    try {
      (void)create(settings);
    } catch (const std::exception& failure) {
      return failure.what();
    }
    return {};
  }

  std::shared_ptr<RecordingLogger> logger = std::make_shared<RecordingLogger>();
  PluginLoader loader{logger};
  ScopedEnvironment key{"AWS_ACCESS_KEY_ID", std::nullopt};
  ScopedEnvironment secret{"AWS_SECRET_ACCESS_KEY", std::nullopt};
  ScopedEnvironment token{"AWS_SESSION_TOKEN", std::nullopt};
};

TEST_F(Route53, ManagesRecordsAcrossZonesWithNormalisedNames) {
  EXPECT_EQ(records(base_credentials +
                    "    records:\n"
                    "      - {name: Macnessa.Macha.Network., hosted_zone_id: /hostedzone/Z111}\n"
                    "      - {name: gbni.athenasip.org, hosted_zone_id: Z222, ttl: 300}\n"),
            (std::vector<std::string>{"macnessa.macha.network", "gbni.athenasip.org"}));
  EXPECT_TRUE(logger->logged(manannan::loggers::DEBUG,
                             "record macnessa.macha.network in zone Z111 with TTL 60 using the base credentials"));
  EXPECT_TRUE(logger->logged(manannan::loggers::DEBUG, "record gbni.athenasip.org in zone Z222 with TTL 300"));
}

TEST_F(Route53, ARecordWithItsOwnCredentialsNeedsNoBase) {
  EXPECT_EQ(records("    records:\n"
                    "      - name: gbni.athenasip.org\n"
                    "        hosted_zone_id: Z222\n"
                    "        credentials: {access_key_id: AKIDOWN, secret_access_key: own}\n")
                .size(),
            1u);
  EXPECT_TRUE(logger->logged(manannan::loggers::DEBUG, "using its own credentials"));
}

TEST_F(Route53, BaseCredentialsFallBackToTheEnvironment) {
  const std::string settings = "    records:\n      - {name: a.example.com, hosted_zone_id: Z1}\n";
  const auto refused = error(settings);
  EXPECT_TRUE(contains(refused, "a.example.com") && contains(refused, "no credentials")) << refused;
  ScopedEnvironment environment_key("AWS_ACCESS_KEY_ID", "AKIDENV");
  ScopedEnvironment environment_secret("AWS_SECRET_ACCESS_KEY", "env");
  EXPECT_EQ(records(settings).size(), 1u);
  EXPECT_EQ(records("    credentials: {session_token: only}\n" + settings).size(), 1u);
}

TEST_F(Route53, PerRecordCredentialsNeverBorrowFromTheBase) {
  EXPECT_TRUE(contains(error(base_credentials +
                             "    records:\n"
                             "      - name: a.example.com\n"
                             "        hosted_zone_id: Z1\n"
                             "        credentials: {access_key_id: AKIDOWN}\n"),
                       "credentials need access_key_id and secret_access_key"));
}

TEST_F(Route53, ExpandsEnvironmentReferences) {
  const std::string settings =
      "    records:\n"
      "      - name: a.example.com\n"
      "        hosted_zone_id: Z1\n"
      "        credentials: {access_key_id: '${MANANNAN_TEST_KEY}', secret_access_key: '${MANANNAN_TEST_SECRET}'}\n";
  ScopedEnvironment variable_key("MANANNAN_TEST_KEY", "AKIDVAR");
  {
    ScopedEnvironment variable_secret("MANANNAN_TEST_SECRET", "var");
    EXPECT_EQ(records(settings).size(), 1u);
  }
  EXPECT_TRUE(contains(error(settings), "credentials need"));
  // Only the whole value is a reference.
  EXPECT_EQ(records("    records:\n"
                    "      - name: a.example.com\n"
                    "        hosted_zone_id: Z1\n"
                    "        credentials: {access_key_id: 'x${MANANNAN_TEST_KEY}', secret_access_key: '${}'}\n")
                .size(),
            1u);
}

TEST_F(Route53, RefusesConfigurationMistakes) {
  const std::string one = "    records:\n      - {name: a.example.com, hosted_zone_id: Z1}\n";
  const std::vector<std::pair<std::string, std::string>> cases{
      {"    other: 1\n" + one, "unknown setting 'other'"},
      {"    hosted_zone_id: Z1\n    record_name: a.example.com\n", "set hosted_zone_id and name on each entry"},
      {"    endpoint: http://host/path\n" + one, "'endpoint' must be a host name"},
      {"    endpoint: 'http://'\n" + one, "'endpoint' must be a host name"},
      {"    endpoint: [a]\n" + one, "'endpoint' must be a single value"},
      {"    signing_region: ''\n" + one, "signing_region must be set"},
      {"    timeout_seconds: 0\n" + one, "timeout_seconds must be positive"},
      {"    ttl: soon\n" + one, "'ttl' must be a positive number"},
      {"    records: []\n", "'records' must list at least one record"},
      {"    records: a.example.com\n", "'records' must list at least one record"},
      {"    records: [a.example.com]\n", "records[0] must be a mapping"},
      {"    records:\n      - {name: a.example.com, hosted_zone: Z1}\n", "unknown setting 'hosted_zone'"},
      {"    records:\n      - {hosted_zone_id: Z1}\n", "'name' is required"},
      {"    records:\n      - {name: '.', hosted_zone_id: Z1}\n", "'name' is empty"},
      {"    records:\n      - {name: a.example.com, hosted_zone_id: /hostedzone/}\n", "'hosted_zone_id' is empty"},
      {"    records:\n      - {name: [a], hosted_zone_id: Z1}\n", "'name' must be a single value"},
      {"    records:\n      - {name: a.example.com, hosted_zone_id: Z1, ttl: 0}\n", "'ttl' must be a positive"},
      {"    records:\n      - {name: a.example.com, hosted_zone_id: Z1}\n      - {name: A.example.com., hosted_zone_id: Z2}\n",
       "record a.example.com is listed twice"},
  };
  for (const auto& [settings, expected] : cases) {
    const auto refused = error(base_credentials + settings);
    EXPECT_TRUE(contains(refused, expected)) << settings << "gave: " << refused;
  }
  EXPECT_TRUE(contains(error("    5\n"), "registry.aws_route53 must be a mapping"));
  EXPECT_TRUE(contains(error("    credentials: AKID\n" + one), "'credentials' must be a mapping"));
  EXPECT_TRUE(contains(error("    credentials: {key: x}\n" + one), "credentials: unknown setting 'key'"));
}

// A local Route 53: answers ListResourceRecordSets with the given body and
// records every request.
class Route53Requests : public Route53 {
 protected:
  std::string settings(const std::string& extra, const std::string& credentials) const {
    return "    endpoint: " + server.url() + "\n" + credentials + extra +
           "    records:\n"
           "      - {name: a.example.com, hosted_zone_id: /hostedzone/Z1, ttl: 300}\n"
           "      - {name: '*.example.com', hosted_zone_id: Z1}\n";
  }

  RegistryPlugin& registry(const std::string& extra = "", const std::string& credentials = base_credentials) {
    loaded.emplace(create(settings(extra, credentials)));
    return static_cast<RegistryPlugin&>(loaded->instance());
  }

  int status = 200;
  std::string list_body;
  HttpServer server{[this](const HttpRequest&) { return HttpResponse{status, list_body}; }};
  std::optional<manannan::LoadedPlugin> loaded;
};

std::string record_set(const std::string& name, const std::string& type, const std::string& value) {
  return "<ResourceRecordSet><Name>" + name + "</Name><Type>" + type +
         "</Type><TTL>60</TTL><ResourceRecords><ResourceRecord><Value>" + value +
         "</Value></ResourceRecord></ResourceRecords></ResourceRecordSet>";
}

TEST_F(Route53Requests, ReadsTheARecordAmongOtherSets) {
  list_body = "<ListResourceRecordSetsResponse><ResourceRecordSets>" + record_set("a.example.co.", "A", "192.0.2.9") +
              record_set("a.example.com.", "TXT", "&quot;text&quot;") +
              record_set("A.Example.Com.", "A", "192.0.2.1&amp;lt;") + "</ResourceRecordSets></ListResourceRecordSetsResponse>";
  EXPECT_EQ(registry().get_value("a.example.com"), "192.0.2.1&lt;");

  const auto requests = server.requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].method, "GET");
  EXPECT_EQ(requests[0].path(), "/2013-04-01/hostedzone/Z1/rrset");
  EXPECT_EQ(requests[0].query(), "maxitems=1&name=a.example.com.&type=A");
  EXPECT_EQ(requests[0].header("host"), server.authority());
  EXPECT_TRUE(contains(requests[0].header("authorization"), "AWS4-HMAC-SHA256 Credential=AKIDBASE/"));
  EXPECT_TRUE(contains(requests[0].header("authorization"), "/us-east-1/route53/aws4_request"));
  EXPECT_EQ(requests[0].header("x-amz-content-sha256"), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(requests[0].header("x-amz-security-token"), "");
  EXPECT_EQ(requests[0].header("user-agent"), std::string("Manannan/") + manannan::version);
}

TEST_F(Route53Requests, DecodesEscapedNames) {
  list_body = record_set("\\052.example.com.", "A", "192.0.2.5");
  EXPECT_EQ(registry().get_value("*.example.com"), "192.0.2.5");
  EXPECT_EQ(server.requests()[0].query(), "maxitems=1&name=%2A.example.com.&type=A");
}

TEST_F(Route53Requests, AnAbsentRecordIsEmpty) {
  list_body = "<ListResourceRecordSetsResponse><ResourceRecordSets>" + record_set("b.example.com.", "A", "192.0.2.9") +
              "<ResourceRecordSet><Name>a.example.com.</Name><Type>A</Type><AliasTarget/></ResourceRecordSet>" +
              "<ResourceRecordSet><Type>A</Type></ResourceRecordSet>" + record_set("trailing\\05", "A", "1") +
              "</ResourceRecordSets></ListResourceRecordSetsResponse>";
  EXPECT_EQ(registry().get_value("a.example.com"), "");
  EXPECT_TRUE(logger->logged(manannan::loggers::DEBUG, "a.example.com has no A record"));
}

TEST_F(Route53Requests, UpsertsTheRecordWithItsTtl) {
  registry("    signing_region: eu-west-1\n").set_value("a.example.com", "<&>\"'");
  const auto requests = server.requests();
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].method, "POST");
  EXPECT_EQ(requests[0].path(), "/2013-04-01/hostedzone/Z1/rrset/");
  EXPECT_EQ(requests[0].header("content-type"), "application/xml");
  EXPECT_TRUE(contains(requests[0].header("authorization"), "/eu-west-1/route53/aws4_request"));
  EXPECT_EQ(requests[0].body,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<ChangeResourceRecordSetsRequest xmlns=\"https://route53.amazonaws.com/doc/2013-04-01/\">"
            "<ChangeBatch><Changes><Change><Action>UPSERT</Action><ResourceRecordSet><Name>a.example.com.</Name>"
            "<Type>A</Type><TTL>300</TTL><ResourceRecords><ResourceRecord><Value>&lt;&amp;&gt;&quot;&apos;</Value>"
            "</ResourceRecord></ResourceRecords></ResourceRecordSet></Change></Changes></ChangeBatch>"
            "</ChangeResourceRecordSetsRequest>");
  EXPECT_TRUE(logger->logged(manannan::loggers::DEBUG, "Route 53 accepted the update for a.example.com"));
}

TEST_F(Route53Requests, SendsTheSessionToken) {
  list_body = record_set("a.example.com.", "A", "192.0.2.1");
  EXPECT_EQ(registry("", "    credentials: {access_key_id: AKIDTMP, secret_access_key: tmp, session_token: TOKEN}\n")
                .get_value("a.example.com"),
            "192.0.2.1");
  const auto request = server.requests().at(0);
  EXPECT_EQ(request.header("x-amz-security-token"), "TOKEN");
  EXPECT_TRUE(contains(request.header("authorization"), "Credential=AKIDTMP/"));
  EXPECT_TRUE(contains(request.header("authorization"), "SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-security-token,"));
}

TEST_F(Route53Requests, ReportsErrorStatusesWithTheirBody) {
  status = 400;
  list_body = "<ErrorResponse>InvalidChangeBatch</ErrorResponse>";
  auto& plugin = registry();
  try {
    (void)plugin.get_value("a.example.com");
    FAIL();
  } catch (const std::runtime_error& failure) {
    EXPECT_STREQ(failure.what(), "Route 53 returned HTTP 400: <ErrorResponse>InvalidChangeBatch</ErrorResponse>");
  }
  status = 301;
  EXPECT_THROW(plugin.set_value("a.example.com", "192.0.2.1"), std::runtime_error);
}

TEST_F(Route53Requests, RefusesUnknownRecords) {
  auto& plugin = registry();
  EXPECT_THROW((void)plugin.get_value("b.example.com"), std::runtime_error);
  EXPECT_THROW(plugin.set_value("b.example.com", "192.0.2.1"), std::runtime_error);
  EXPECT_TRUE(server.requests().empty());
}

TEST_F(Route53, ReportsAnUnreachableEndpoint) {
  const auto port = manannan::test::closed_port();
  auto loaded = create("    endpoint: http://127.0.0.1:" + std::to_string(port) + "/\n" + base_credentials +
                       "    records:\n      - {name: a.example.com, hosted_zone_id: Z1}\n");
  auto& plugin = static_cast<RegistryPlugin&>(loaded.instance());
  try {
    (void)plugin.get_value("a.example.com");
    FAIL();
  } catch (const std::runtime_error& failure) {
    EXPECT_TRUE(contains(failure.what(), "Route 53 request failed: ")) << failure.what();
  }
}

TEST_F(Route53, AcceptsAnHttpsEndpointWithAPort) {
  EXPECT_EQ(records("    endpoint: https://route53.example.test:8443/\n" + base_credentials +
                    "    records:\n      - {name: a.example.com, hosted_zone_id: Z1}\n")
                .size(),
            1u);
}
}  // namespace
