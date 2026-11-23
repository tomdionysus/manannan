#pragma once
// A loopback HTTP/1.1 server for the plugins' tests: it accepts connections
// on 127.0.0.1 on a port of its own, answers each request through a handler
// and records every request it saw. One request per connection.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace manannan::test {

struct HttpRequest {
  std::string method;
  std::string target;                          // path and query, as sent
  std::map<std::string, std::string> headers;  // names in lower case
  std::string body;

  std::string path() const { return target.substr(0, target.find('?')); }
  std::string query() const {
    const auto mark = target.find('?');
    return mark == std::string::npos ? std::string{} : target.substr(mark + 1);
  }
  std::string header(const std::string& name) const {
    const auto found = headers.find(name);
    return found == headers.end() ? std::string{} : found->second;
  }
};

struct HttpResponse {
  int status = 200;
  std::string body;
};

class HttpServer {
 public:
  using Handler = std::function<HttpResponse(const HttpRequest&)>;

  explicit HttpServer(Handler handler) : _handler(std::move(handler)) {
    _listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (_listener < 0) throw std::runtime_error("socket failed");
    const int yes = 1;
    ::setsockopt(_listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(_listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || ::listen(_listener, 16) < 0)
      throw std::runtime_error("bind or listen failed");
    socklen_t size = sizeof(address);
    ::getsockname(_listener, reinterpret_cast<sockaddr*>(&address), &size);
    _port = ntohs(address.sin_port);
    if (::pipe(_wake) < 0) throw std::runtime_error("pipe failed");
    _thread = std::thread([this] { serve(); });
  }

  ~HttpServer() {
    const char byte = 0;
    (void)!::write(_wake[1], &byte, 1);
    _thread.join();
    ::close(_listener);
    ::close(_wake[0]);
    ::close(_wake[1]);
  }

  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  std::uint16_t port() const { return _port; }
  std::string url(const std::string& path = "/") const { return "http://127.0.0.1:" + std::to_string(_port) + path; }
  std::string authority() const { return "127.0.0.1:" + std::to_string(_port); }

  std::vector<HttpRequest> requests() const {
    std::lock_guard lock(_mutex);
    return _requests;
  }

  // Waits until at least count requests have been received.
  bool wait_for(std::size_t count, std::chrono::milliseconds deadline = std::chrono::seconds(10)) const {
    std::unique_lock lock(_mutex);
    return _answered.wait_for(lock, deadline, [&] { return _requests.size() >= count; });
  }

 private:
  void serve() {
    for (;;) {
      pollfd fds[2] = {{_listener, POLLIN, 0}, {_wake[0], POLLIN, 0}};
      if (::poll(fds, 2, -1) < 0) continue;
      if (fds[1].revents) return;
      if (!(fds[0].revents & POLLIN)) continue;
      const int connection = ::accept(_listener, nullptr, nullptr);
      if (connection < 0) continue;
      answer(connection);
      ::close(connection);
    }
  }

  static std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
  }

  static bool send_all(int connection, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
      const auto written = ::send(connection, data.data() + sent, data.size() - sent, 0);
      if (written <= 0) return false;
      sent += static_cast<std::size_t>(written);
    }
    return true;
  }

  void answer(int connection) {
    std::string data;
    char buffer[4096];
    std::size_t header_end = std::string::npos;
    while ((header_end = data.find("\r\n\r\n")) == std::string::npos) {
      const auto received = ::recv(connection, buffer, sizeof(buffer), 0);
      if (received <= 0) return;
      data.append(buffer, static_cast<std::size_t>(received));
    }

    HttpRequest request;
    const auto head = data.substr(0, header_end);
    std::size_t line_start = 0;
    bool first = true;
    while (line_start <= head.size()) {
      auto line_end = head.find("\r\n", line_start);
      if (line_end == std::string::npos) line_end = head.size();
      const auto line = head.substr(line_start, line_end - line_start);
      if (first) {
        const auto space = line.find(' ');
        request.method = line.substr(0, space);
        request.target = line.substr(space + 1, line.rfind(' ') - space - 1);
        first = false;
      } else if (const auto colon = line.find(':'); colon != std::string::npos) {
        auto value = line.substr(colon + 1);
        value.erase(0, value.find_first_not_of(' '));
        request.headers[lower(line.substr(0, colon))] = value;
      }
      line_start = line_end + 2;
    }

    const auto length_header = request.header("content-length");
    const std::size_t length = length_header.empty() ? 0 : std::stoul(length_header);
    if (lower(request.header("expect")) == "100-continue" && !send_all(connection, "HTTP/1.1 100 Continue\r\n\r\n")) return;
    request.body = data.substr(header_end + 4);
    while (request.body.size() < length) {
      const auto received = ::recv(connection, buffer, sizeof(buffer), 0);
      if (received <= 0) return;
      request.body.append(buffer, static_cast<std::size_t>(received));
    }

    const auto response = _handler(request);
    // Record before replying: once the client has its answer, requests() must show the request.
    {
      std::lock_guard lock(_mutex);
      _requests.push_back(std::move(request));
    }
    _answered.notify_all();
    send_all(connection, "HTTP/1.1 " + std::to_string(response.status) + " Test\r\nContent-Length: " + std::to_string(response.body.size()) +
                             "\r\nConnection: close\r\n\r\n" + response.body);
  }

  Handler _handler;
  int _listener = -1;
  int _wake[2] = {-1, -1};
  std::uint16_t _port = 0;
  mutable std::mutex _mutex;
  mutable std::condition_variable _answered;
  std::vector<HttpRequest> _requests;
  std::thread _thread;
};

// A port nothing listens on: one a server had, after it has closed.
inline std::uint16_t closed_port() {
  HttpServer server([](const HttpRequest&) { return HttpResponse{}; });
  return server.port();
}

}  // namespace manannan::test
