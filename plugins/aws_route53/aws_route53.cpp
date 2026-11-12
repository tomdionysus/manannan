#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "aws_sigv4.h"
#include "manannan/plugin.h"
#include "manannan/version.h"

namespace {

using manannan::aws::Credentials;

std::size_t append_data(char* data, std::size_t size, std::size_t count, void* target) {
  static_cast<std::string*>(target)->append(data, size * count);
  return size * count;
}

std::string environment(const std::string& name) {
  const auto* value = std::getenv(name.c_str());
  return value ? value : "";
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

// Rejects keys that are not recognised, so a misspelt setting fails loudly
// instead of being silently replaced by a default.
void check_keys(const YAML::Node& node, const std::string& where, std::initializer_list<const char*> allowed) {
  for (const auto& entry : node) {
    const auto key = entry.first.as<std::string>();
    if (std::none_of(allowed.begin(), allowed.end(), [&](const char* name) { return key == name; }))
      throw std::runtime_error(where + ": unknown setting '" + key + "'");
  }
}

std::string scalar(const YAML::Node& node, const char* key, const std::string& where, bool required) {
  const auto value = node[key];
  if (!value) {
    if (required) throw std::runtime_error(where + ": '" + key + "' is required");
    return {};
  }
  if (!value.IsScalar()) throw std::runtime_error(where + ": '" + key + "' must be a single value");
  return value.as<std::string>();
}

// A value of the form ${NAME} is read from the environment variable NAME.
std::string expand(const std::string& value) {
  if (value.size() > 3 && value.starts_with("${") && value.ends_with("}")) return environment(value.substr(2, value.size() - 3));
  return value;
}

// Reads a credentials block. The plugin-wide block falls back to the standard
// AWS_* environment variables for anything it leaves empty; a per-record block
// is complete in itself, since mixing keys from two identities is never right.
Credentials read_credentials(const YAML::Node& node, const std::string& where, bool environment_fallback) {
  Credentials credentials;
  if (node) {
    if (!node.IsMap()) throw std::runtime_error(where + ": 'credentials' must be a mapping");
    check_keys(node, where + " credentials", {"access_key_id", "secret_access_key", "session_token"});
    credentials.access_key_id = expand(scalar(node, "access_key_id", where, false));
    credentials.secret_access_key = expand(scalar(node, "secret_access_key", where, false));
    credentials.session_token = expand(scalar(node, "session_token", where, false));
  }
  if (environment_fallback) {
    if (credentials.access_key_id.empty()) credentials.access_key_id = environment("AWS_ACCESS_KEY_ID");
    if (credentials.secret_access_key.empty()) credentials.secret_access_key = environment("AWS_SECRET_ACCESS_KEY");
    if (credentials.session_token.empty()) credentials.session_token = environment("AWS_SESSION_TOKEN");
  }
  return credentials;
}

bool complete(const Credentials& credentials) { return !credentials.access_key_id.empty() && !credentials.secret_access_key.empty(); }

unsigned read_ttl(const YAML::Node& node, unsigned fallback, const std::string& where) {
  const auto value = node["ttl"];
  if (!value) return fallback;
  unsigned ttl = 0;
  try {
    ttl = value.as<unsigned>();
  } catch (const YAML::Exception&) {
    ttl = 0;
  }
  if (ttl == 0 || ttl > 2'147'483'647) throw std::runtime_error(where + ": 'ttl' must be a positive number of seconds");
  return ttl;
}

std::string xml_escape(const std::string& value) {
  std::string result;
  for (char character : value) {
    switch (character) {
      case '&':
        result += "&amp;";
        break;
      case '<':
        result += "&lt;";
        break;
      case '>':
        result += "&gt;";
        break;
      case '\"':
        result += "&quot;";
        break;
      case '\'':
        result += "&apos;";
        break;
      default:
        result += character;
    }
  }
  return result;
}

std::string xml_unescape(std::string value) {
  const std::vector<std::pair<std::string, std::string>> replacements{{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}};
  for (const auto& [encoded, plain] : replacements) {
    std::size_t position = 0;
    while ((position = value.find(encoded, position)) != std::string::npos) {
      value.replace(position, encoded.size(), plain);
      position += plain.size();
    }
  }
  return value;
}

// Route 53 returns names in lower case with characters outside [a-z0-9-_.] as
// three-digit octal escapes, for example "*" as "\052".
std::string decode_dns_name(const std::string& value) {
  std::string result;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 3 < value.size() && std::isdigit(static_cast<unsigned char>(value[i + 1])) &&
        std::isdigit(static_cast<unsigned char>(value[i + 2])) && std::isdigit(static_cast<unsigned char>(value[i + 3]))) {
      result += static_cast<char>(std::stoi(value.substr(i + 1, 3), nullptr, 8));
      i += 3;
    } else {
      result += value[i];
    }
  }
  return lowercase(result);
}

struct Record {
  std::string name;  // as reported to the service: lower case, no trailing dot
  std::string fqdn;  // as sent to Route 53: lower case, with trailing dot
  std::string zone;
  unsigned ttl;
  Credentials credentials;
};

class AwsRoute53 final : public manannan::RegistryPlugin {
 public:
  AwsRoute53(const YAML::Node& config, std::shared_ptr<manannan::loggers::Logger> logger) : logger_private(std::move(logger)) {
    const std::string where = "registry.aws_route53";
    if (!config.IsMap()) throw std::runtime_error(where + " must be a mapping");
    if (config["hosted_zone_id"] || config["record_name"]) throw std::runtime_error(where + ": set hosted_zone_id and name on each entry in 'records'");
    check_keys(config, where, {"endpoint", "signing_region", "timeout_seconds", "ttl", "credentials", "records"});

    // The endpoint is a host name, optionally with a port and an http:// or
    // https:// scheme (https when none is given).
    host_private = scalar(config, "endpoint", where, false);
    if (host_private.empty()) host_private = "route53.amazonaws.com";
    if (host_private.starts_with("https://")) {
      host_private.erase(0, 8);
    } else if (host_private.starts_with("http://")) {
      scheme_private = "http";
      host_private.erase(0, 7);
    }
    while (host_private.ends_with('/')) host_private.pop_back();
    if (host_private.empty() || host_private.find_first_of("/?#@ ") != std::string::npos)
      throw std::runtime_error(where + ": 'endpoint' must be a host name, optionally with a scheme and port");
    region_private = config["signing_region"].as<std::string>("us-east-1");
    timeout_private = config["timeout_seconds"].as<long>(10);
    if (region_private.empty() || timeout_private <= 0) throw std::runtime_error(where + ": signing_region must be set and timeout_seconds must be positive");
    const auto default_ttl = read_ttl(config, 60, where);
    const auto base_credentials = read_credentials(config["credentials"], where, true);

    const auto records = config["records"];
    if (!records || !records.IsSequence() || records.size() == 0) throw std::runtime_error(where + ": 'records' must list at least one record");
    for (std::size_t index = 0; index < records.size(); ++index) {
      const auto entry = records[index];
      const auto entry_where = where + ".records[" + std::to_string(index) + "]";
      if (!entry.IsMap()) throw std::runtime_error(entry_where + " must be a mapping");
      check_keys(entry, entry_where, {"name", "hosted_zone_id", "ttl", "credentials"});

      Record record;
      record.name = lowercase(scalar(entry, "name", entry_where, true));
      while (record.name.ends_with('.')) record.name.pop_back();
      if (record.name.empty()) throw std::runtime_error(entry_where + ": 'name' is empty");
      record.fqdn = record.name + ".";
      record.zone = scalar(entry, "hosted_zone_id", entry_where, true);
      if (record.zone.starts_with("/hostedzone/")) record.zone.erase(0, 12);
      if (record.zone.empty()) throw std::runtime_error(entry_where + ": 'hosted_zone_id' is empty");
      record.ttl = read_ttl(entry, default_ttl, entry_where);
      if (entry["credentials"]) {
        record.credentials = read_credentials(entry["credentials"], entry_where, false);
        if (!complete(record.credentials)) throw std::runtime_error(entry_where + ": credentials need access_key_id and secret_access_key");
      } else {
        if (!complete(base_credentials))
          throw std::runtime_error(entry_where + " (" + record.name + ") has no credentials: set " + where +
                                   ".credentials, AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY, or per-record credentials");
        record.credentials = base_credentials;
      }
      if (find(record.name)) throw std::runtime_error(where + ": record " + record.name + " is listed twice");
      logger_private->debug("record " + record.name + " in zone " + record.zone + " with TTL " + std::to_string(record.ttl) +
                            (entry["credentials"] ? " using its own credentials" : " using the base credentials"));
      records_private.push_back(std::move(record));
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw std::runtime_error("cannot initialise libcurl");  // LCOV_EXCL_LINE
  }

  ~AwsRoute53() override {
    logger_private->debug("destroy()");
    curl_global_cleanup();
  }

  std::vector<std::string> records() const override {
    std::vector<std::string> names;
    for (const auto& record : records_private) names.push_back(record.name);
    return names;
  }

  std::string get_value(const std::string& name) override {
    const auto& record = lookup(name);
    logger_private->debug("reading A record " + record.fqdn + " in zone " + record.zone);
    const auto query = "maxitems=1&name=" + manannan::aws::url_encode(record.fqdn) + "&type=A";
    const auto response = request(record, "GET", rrset_path(record), query, "");
    const std::regex set_pattern("<ResourceRecordSet>([\\s\\S]*?)</ResourceRecordSet>");
    const std::regex name_pattern("<Name>([^<]+)</Name>");
    const std::regex value_pattern("<ResourceRecord>\\s*<Value>([^<]+)</Value>");
    for (auto current = std::sregex_iterator(response.begin(), response.end(), set_pattern); current != std::sregex_iterator(); ++current) {
      const auto set = (*current)[1].str();
      std::smatch name_match;
      std::smatch value_match;
      if (std::regex_search(set, name_match, name_pattern) && decode_dns_name(xml_unescape(name_match[1].str())) == record.fqdn &&
          set.find("<Type>A</Type>") != std::string::npos && std::regex_search(set, value_match, value_pattern)) {
        const auto value = xml_unescape(value_match[1].str());
        logger_private->debug(record.name + " is registered as " + value);
        return value;
      }
    }
    logger_private->debug(record.name + " has no A record");
    return {};
  }

  void set_value(const std::string& name, const std::string& value) override {
    const auto& record = lookup(name);
    logger_private->debug("setting A record " + record.fqdn + " to " + value);
    const auto payload = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") +
                         "<ChangeResourceRecordSetsRequest xmlns=\"https://route53.amazonaws.com/doc/2013-04-01/\">"
                         "<ChangeBatch><Changes><Change><Action>UPSERT</Action><ResourceRecordSet><Name>" +
                         xml_escape(record.fqdn) + "</Name><Type>A</Type><TTL>" + std::to_string(record.ttl) +
                         "</TTL><ResourceRecords><ResourceRecord><Value>" + xml_escape(value) +
                         "</Value></ResourceRecord></ResourceRecords></ResourceRecordSet></Change></Changes></ChangeBatch>"
                         "</ChangeResourceRecordSetsRequest>";
    (void)request(record, "POST", rrset_path(record) + "/", "", payload);
    logger_private->debug("Route 53 accepted the update for " + record.name);
  }

 private:
  const Record* find(const std::string& name) const {
    for (const auto& record : records_private)
      if (record.name == name) return &record;
    return nullptr;
  }

  const Record& lookup(const std::string& name) const {
    const auto* record = find(name);
    if (!record) throw std::runtime_error("unknown record: " + name);
    return *record;
  }

  static std::string rrset_path(const Record& record) { return "/2013-04-01/hostedzone/" + record.zone + "/rrset"; }

  std::string request(const Record& record, const std::string& method, const std::string& path, const std::string& query, const std::string& payload) const {
    const auto base = scheme_private + "://" + host_private + path;
    logger_private->debug(method + " " + base);
    const auto signed_request =
        manannan::aws::sign_v4(method, host_private, path, query, payload, record.credentials, region_private, "route53", std::chrono::system_clock::now());
    const auto url = base + (query.empty() ? "" : "?" + query);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("cannot initialise HTTP client");  // LCOV_EXCL_LINE

    curl_slist* raw_headers = nullptr;
    const std::vector<std::string> values{
        "Authorization: " + signed_request.authorization, "x-amz-date: " + signed_request.amz_date, "x-amz-content-sha256: " + signed_request.content_sha256,
        record.credentials.session_token.empty() ? "" : "x-amz-security-token: " + record.credentials.session_token, "Content-Type: application/xml"};
    for (const auto& header : values)
      if (!header.empty()) raw_headers = curl_slist_append(raw_headers, header.c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(raw_headers, curl_slist_free_all);

    std::string response;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, append_data);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, timeout_private);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, user_agent.c_str());
    if (method == "POST") {
      curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
      curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, payload.c_str());
      curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    }
    const auto result = curl_easy_perform(curl.get());
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (result != CURLE_OK) throw std::runtime_error(std::string("Route 53 request failed: ") + curl_easy_strerror(result));
    if (status < 200 || status >= 300) throw std::runtime_error("Route 53 returned HTTP " + std::to_string(status) + ": " + response);
    return response;
  }

  const std::string user_agent = std::string("Manannan/") + manannan::version;
  std::string scheme_private = "https";
  std::string host_private;
  std::string region_private;
  long timeout_private{};
  std::vector<Record> records_private;
  std::shared_ptr<manannan::loggers::Logger> logger_private;
};

manannan::Plugin* create(const YAML::Node& config, std::shared_ptr<manannan::loggers::Logger> logger) {
  logger->debug("create()");
  return new AwsRoute53(config, std::move(logger));
}
void destroy(manannan::Plugin* plugin) { delete plugin; }
const manannan::PluginDescriptor descriptor{
    manannan::plugin_abi_version, manannan::PluginType::registry, "aws_route53", "registry.aws_route53", create, destroy};
}  // namespace

extern "C" __attribute__((visibility("default"))) const manannan::PluginDescriptor* manannan_plugin_descriptor() { return &descriptor; }
