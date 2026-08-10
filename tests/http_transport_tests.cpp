#include "predictfun/net/http.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": CHECK failed: " #condition << '\n';                      \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using predictfun::ErrorCode;
using predictfun::Result;
using predictfun::net::HttpRequest;
using predictfun::net::HttpResponse;
using predictfun::net::RequestContext;

class OneShotServer {
public:
  OneShotServer(std::string wire_response,
                std::chrono::milliseconds response_delay = {})
      : acceptor_(io_, tcp::endpoint{asio::ip::address_v4::loopback(), 0}),
        port_(acceptor_.local_endpoint().port()),
        thread_([this, wire_response = std::move(wire_response),
                 response_delay](std::stop_token) {
          boost::system::error_code error;
          tcp::socket socket(io_);
          acceptor_.accept(socket, error);
          if (error)
            return;
          asio::streambuf input;
          asio::read_until(socket, input, "\r\n\r\n", error);
          if (response_delay.count() > 0)
            std::this_thread::sleep_for(response_delay);
          asio::write(socket, asio::buffer(wire_response), error);
          socket.shutdown(tcp::socket::shutdown_both, error);
          socket.close(error);
        }) {}

  ~OneShotServer() {
    boost::system::error_code ignored;
    acceptor_.close(ignored);
  }

  [[nodiscard]] unsigned short port() const noexcept { return port_; }

private:
  asio::io_context io_;
  tcp::acceptor acceptor_;
  unsigned short port_;
  std::jthread thread_;
};

Result<HttpResponse>
perform(unsigned short port, predictfun::net::TransportLimits limits = {},
        std::chrono::milliseconds timeout = std::chrono::seconds{1}) {
  asio::io_context io;
  auto transport = std::make_shared<predictfun::net::BeastHttpTransport>(
      io.get_executor(), limits);
  HttpRequest request;
  request.host = "127.0.0.1";
  request.port = std::to_string(port);
  request.target = "/probe?visible=true";
  request.use_tls = false;
  std::optional<Result<HttpResponse>> result;
  transport->async_get(std::move(request),
                       RequestContext::with_timeout(timeout),
                       [&result](Result<HttpResponse> value) {
                         result.emplace(std::move(value));
                       });
  io.run();
  if (!result) {
    return predictfun::Error{
        ErrorCode::protocol_error, "transport did not complete", {}};
  }
  return std::move(*result);
}

void test_success_and_headers() {
  OneShotServer server{
      "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nX-Test: yes\r\n"
      "Content-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}"};
  auto result = perform(server.port());
  CHECK(result);
  if (!result)
    return;
  CHECK(result.value().status == 200);
  CHECK(result.value().body == "{\"ok\":true}");
  CHECK(result.value().header("x-test") == "yes");
}

void test_body_limit() {
  OneShotServer server{
      "HTTP/1.1 200 OK\r\nContent-Length: 32\r\nConnection: close\r\n\r\n"
      "01234567890123456789012345678901"};
  predictfun::net::TransportLimits limits;
  limits.max_body_bytes = 8U;
  auto result = perform(server.port(), limits);
  CHECK(!result);
  CHECK(result.error().code == ErrorCode::body_too_large);
}

void test_truncated_body() {
  OneShotServer server{
      "HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\n{}"};
  auto result = perform(server.port());
  CHECK(!result);
  CHECK(result.error().code == ErrorCode::body_truncated);
}

void test_deadline() {
  OneShotServer server{
      "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}",
      std::chrono::milliseconds{100}};
  auto result = perform(server.port(), {}, std::chrono::milliseconds{10});
  CHECK(!result);
  CHECK(result.error().code == ErrorCode::deadline_exceeded);
}

void test_pre_cancel_and_secret_target_rejection() {
  {
    asio::io_context io;
    auto transport = std::make_shared<predictfun::net::BeastHttpTransport>(
        io.get_executor());
    std::stop_source source;
    source.request_stop();
    HttpRequest request;
    request.host = "127.0.0.1";
    request.port = "1";
    request.use_tls = false;
    std::optional<Result<HttpResponse>> result;
    transport->async_get(std::move(request),
                         RequestContext::with_timeout(std::chrono::seconds{1},
                                                      source.get_token()),
                         [&result](Result<HttpResponse> value) {
                           result.emplace(std::move(value));
                         });
    io.run();
    CHECK(result.has_value());
    CHECK(!*result);
    CHECK(result->error().code == ErrorCode::cancelled);
  }

  {
    asio::io_context io;
    auto transport = std::make_shared<predictfun::net::BeastHttpTransport>(
        io.get_executor());
    HttpRequest request;
    request.host = "127.0.0.1";
    request.port = "1";
    request.target = "/v1/markets?x-api-key=forbidden";
    request.use_tls = false;
    std::optional<Result<HttpResponse>> result;
    transport->async_get(std::move(request),
                         RequestContext::with_timeout(std::chrono::seconds{1}),
                         [&result](Result<HttpResponse> value) {
                           result.emplace(std::move(value));
                         });
    io.run();
    CHECK(result.has_value());
    CHECK(!*result);
    CHECK(result->error().code == ErrorCode::invalid_argument);
  }
}

} // namespace

int main() {
  test_success_and_headers();
  test_body_limit();
  test_truncated_body();
  test_deadline();
  test_pre_cancel_and_secret_target_rejection();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "predictfun-cpp HTTP transport tests passed\n";
  return EXIT_SUCCESS;
}
