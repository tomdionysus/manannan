#include <arpa/inet.h>
#include <curl/curl.h>
#include <memory>
#include <stdexcept>
#include <string>
#include "manannan/plugin.h"
#include "manannan/version.h"
namespace {
std::size_t append_data(char* data, std::size_t size, std::size_t count, void* target) {
  static_cast<std::string*>(target)->append(data, size * count); return size * count;
}
std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  const auto last = value.find_last_not_of(" \t\r\n");
  return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1);
}
class AmazonCheckIp final : public manannan::SourcePlugin {
 public:
  AmazonCheckIp(const YAML::Node& config, std::shared_ptr<manannan::loggers::Logger> logger)
      : endpoint_private(config["endpoint"].as<std::string>("https://checkip.amazonaws.com/")),
        timeout_private(config["timeout_seconds"].as<long>(10)), logger_private(std::move(logger)) {
    if (timeout_private <= 0) throw std::runtime_error("timeout_seconds must be positive");
    logger_private->debug("initialising Amazon Check IP source");
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
      throw std::runtime_error("cannot initialise libcurl");  // LCOV_EXCL_LINE
  }
  ~AmazonCheckIp() override {
    logger_private->debug("destroy()");
    curl_global_cleanup();
  }
  std::string get_value() override {
    logger_private->debug("get_value()");
    logger_private->debug("requesting public IP from " + endpoint_private);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("cannot initialise HTTP client");  // LCOV_EXCL_LINE
    std::string response;
    curl_easy_setopt(curl.get(), CURLOPT_URL, endpoint_private.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, append_data);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, timeout_private);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, user_agent.c_str());
    const auto result = curl_easy_perform(curl.get());
    long status = 0; curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (result != CURLE_OK) throw std::runtime_error(std::string("check IP request failed: ") + curl_easy_strerror(result));
    if (status != 200) throw std::runtime_error("check IP returned HTTP " + std::to_string(status));
    response = trim(response);
    in_addr address{};
    if (inet_pton(AF_INET, response.c_str(), &address) != 1) throw std::runtime_error("check IP returned invalid IPv4 value");
    logger_private->debug("external address is " + response);
    return response;
  }
 private:
  const std::string user_agent = std::string("Manannan/") + manannan::version;
  std::string endpoint_private;
  long timeout_private;
  std::shared_ptr<manannan::loggers::Logger> logger_private;
};
manannan::Plugin* create(const YAML::Node& config, std::shared_ptr<manannan::loggers::Logger> logger) {
  logger->debug("create()");
  return new AmazonCheckIp(config, std::move(logger));
}
void destroy(manannan::Plugin* plugin) { delete plugin; }
const manannan::PluginDescriptor descriptor{manannan::plugin_abi_version, manannan::PluginType::source,
  "amazon_check_ip", "source.amazon_check_ip", create, destroy};
}
extern "C" __attribute__((visibility("default"))) const manannan::PluginDescriptor* manannan_plugin_descriptor() {
  return &descriptor;
}
