#include "predictfun/net/websocket.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": CHECK failed: " #condition << '\n';                    \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using predictfun::Result;

class EchoWebSocketServer {
public:
  EchoWebSocketServer()
      : acceptor_(io_, tcp::endpoint{asio::ip::address_v4::loopback(), 0}),
        port_(acceptor_.local_endpoint().port()), thread_([this](std::stop_token) {
          boost::system::error_code error;
          tcp::socket socket(io_);
          acceptor_.accept(socket, error);
          if (error)
            return;
          beast::flat_buffer request_buffer;
          http::request<http::string_body> request;
          http::read(socket, request_buffer, request, error);
          if (error)
            return;
          {
            std::scoped_lock lock(mutex_);
            target_ = std::string{request.target()};
            api_key_ = std::string{request["x-api-key"]};
          }
          websocket::stream<tcp::socket> stream(std::move(socket));
          stream.accept(request, error);
          if (error)
            return;
          beast::flat_buffer frame;
          stream.read(frame, error);
          if (error)
            return;
          stream.text(true);
          stream.write(frame.data(), error);
          stream.close(websocket::close_code::normal, error);
        }) {}

  ~EchoWebSocketServer() {
    boost::system::error_code ignored;
    acceptor_.close(ignored);
  }

  [[nodiscard]] unsigned short port() const noexcept { return port_; }
  [[nodiscard]] std::string target() const {
    std::scoped_lock lock(mutex_);
    return target_;
  }
  [[nodiscard]] std::string api_key() const {
    std::scoped_lock lock(mutex_);
    return api_key_;
  }

private:
  asio::io_context io_;
  tcp::acceptor acceptor_;
  unsigned short port_;
  mutable std::mutex mutex_;
  std::string target_;
  std::string api_key_;
  std::jthread thread_;
};

void test_real_handshake_and_text_roundtrip() {
  EchoWebSocketServer server;
  asio::io_context io;
  auto channel = std::make_shared<predictfun::net::BeastWebSocketChannel>(
      io.get_executor());
  predictfun::net::WebSocketRequest request;
  request.host = "127.0.0.1";
  request.port = std::to_string(server.port());
  request.target = "/ws";
  request.use_tls = false;
  request.headers.push_back({"x-api-key", "local-only-secret"});
  std::optional<Result<std::string>> read_result;
  channel->async_open(
      request, predictfun::net::RequestContext::with_timeout(
                   std::chrono::seconds{2}),
      [channel, &read_result](Result<std::monostate> opened) {
        CHECK(opened);
        if (!opened)
          return;
        channel->async_write(
            "hello", [channel, &read_result](Result<std::monostate> written) {
              CHECK(written);
              if (!written)
                return;
              channel->async_read([&read_result](Result<std::string> result) {
                read_result.emplace(std::move(result));
              });
            });
      });
  io.run_for(std::chrono::seconds{3});
  CHECK(read_result.has_value());
  CHECK(read_result && *read_result && read_result->value() == "hello");
  CHECK(server.target() == "/ws");
  CHECK(server.api_key() == "local-only-secret");
  CHECK(predictfun::net::sanitized_websocket_summary(request).find(
            "local-only-secret") == std::string::npos);
}

void test_secret_target_rejected_before_network() {
  asio::io_context io;
  auto channel = std::make_shared<predictfun::net::BeastWebSocketChannel>(
      io.get_executor());
  predictfun::net::WebSocketRequest request;
  request.host = "127.0.0.1";
  request.port = "1";
  request.target = "/ws?apiKey=forbidden";
  request.use_tls = false;
  std::optional<Result<std::monostate>> result;
  channel->async_open(
      request, predictfun::net::RequestContext::with_timeout(
                   std::chrono::seconds{1}),
      [&result](Result<std::monostate> value) {
        result.emplace(std::move(value));
      });
  io.run();
  CHECK(result.has_value());
  CHECK(result && !*result);
  CHECK(result && result->error().code == predictfun::ErrorCode::invalid_argument);
}

} // namespace

int main() {
  test_real_handshake_and_text_roundtrip();
  test_secret_target_rejected_before_network();
  if (failures != 0)
    std::cerr << failures << " WebSocket transport checks failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
