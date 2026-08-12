#include "predictfun/auth/client.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

using predictfun::Error;
using predictfun::ErrorCode;
using predictfun::Result;
using predictfun::net::HttpRequest;
using predictfun::net::HttpResponse;

class ScriptedTransport final : public predictfun::net::HttpTransport {
public:
  explicit ScriptedTransport(boost::asio::any_io_executor executor)
      : executor_(std::move(executor)) {}

  void push(HttpResponse response) { responses_.emplace_back(std::move(response)); }

  void async_request(HttpRequest request, predictfun::net::RequestContext,
                     predictfun::net::ResponseHandler handler) override {
    requests.push_back(std::move(request));
    if (responses_.empty()) {
      boost::asio::dispatch(
          executor_, [handler = std::move(handler)]() mutable {
            handler(Error{ErrorCode::protocol_error,
                          "mock response queue is empty", {}});
          });
      return;
    }
    auto response = std::move(responses_.front());
    responses_.pop_front();
    boost::asio::dispatch(executor_,
                          [handler = std::move(handler),
                           response = std::move(response)]() mutable {
                            handler(std::move(response));
                          });
  }

  std::vector<HttpRequest> requests;

private:
  boost::asio::any_io_executor executor_;
  std::deque<Result<HttpResponse>> responses_;
};

class ScriptedSigner final : public predictfun::auth::MessageSigner {
public:
  explicit ScriptedSigner(boost::asio::any_io_executor executor,
                          predictfun::EvmAddress address)
      : executor_(std::move(executor)), address_(address) {}

  predictfun::EvmAddress signer_address() const override { return address_; }

  void async_sign_message(std::string message,
                          predictfun::auth::Handler<std::string> handler) override {
    signed_message = std::move(message);
    boost::asio::dispatch(executor_, [handler = std::move(handler)]() mutable {
      handler(std::string{"0x1234"});
    });
  }

  std::string signed_message;

private:
  boost::asio::any_io_executor executor_;
  predictfun::EvmAddress address_;
};

HttpResponse response(int status, std::string body) {
  HttpResponse value;
  value.status = status;
  value.body = std::move(body);
  return value;
}

predictfun::EvmAddress address() {
  auto parsed = predictfun::EvmAddress::parse(
      "0x1111111111111111111111111111111111111111");
  CHECK(parsed);
  return parsed.value();
}

void test_address_and_codecs() {
  auto parsed = predictfun::EvmAddress::parse(
      "0xAAbbccDDeeFF0011223344556677889900aAbBcC");
  CHECK(parsed);
  CHECK(parsed && parsed.value().to_string() ==
                      "0xaabbccddeeff0011223344556677889900aabbcc");
  CHECK(!predictfun::EvmAddress::parse("0x1234"));
  CHECK(!predictfun::EvmAddress::parse(
      "0x0000000000000000000000000000000000000000"));

  auto message = predictfun::codec::decode_auth_message_response(
      R"({"success":true,"data":{"message":"sign this exact text"}})");
  CHECK(message);
  CHECK(message && message.value().message == "sign this exact text");

  auto token = predictfun::codec::decode_auth_token_response(
      R"({"success":true,"data":{"token":"sensitive.jwt.value"}})");
  CHECK(token);
  CHECK(token && token.value().view() == "sensitive.jwt.value");

  auto malformed = predictfun::codec::decode_auth_token_response(
      R"({"success":true,"data":{"token":"must-not-leak")");
  CHECK(!malformed);
  CHECK(malformed.error().message.find("must-not-leak") == std::string::npos);

  predictfun::AuthProof proof{address(), "0x1234", "sign this exact text"};
  auto encoded = predictfun::codec::encode_auth_proof(proof);
  CHECK(encoded);
  CHECK(encoded && encoded.value().find("\"signer\":\"0x111111") !=
                       std::string::npos);
  CHECK(encoded && encoded.value().find("\"signature\":\"0x1234\"") !=
                       std::string::npos);
}

void test_mainnet_requires_api_key() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  predictfun::auth::ClientOptions options;
  options.environment = predictfun::Environment::bnb_mainnet;
  predictfun::auth::AuthClient client(io.get_executor(), transport, options);
  std::optional<Result<predictfun::AuthMessage>> result;
  client.async_get_message(
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](Result<predictfun::AuthMessage> value) {
        result.emplace(std::move(value));
      });
  io.run();
  CHECK(result.has_value());
  CHECK(!*result);
  CHECK(result->error().code == ErrorCode::authentication_required);
  CHECK(transport->requests.empty());
}

void test_full_authentication_flow() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(
      200, R"({"success":true,"data":{"message":"dynamic challenge"}})"));
  transport->push(response(
      200, R"({"success":true,"data":{"token":"private.jwt.token"}})"));

  predictfun::auth::ClientOptions options;
  options.environment = predictfun::Environment::bnb_mainnet;
  options.api_key = [] { return "secret-api-key"; };
  predictfun::auth::AuthClient client(io.get_executor(), transport, options);
  auto signer = std::make_shared<ScriptedSigner>(io.get_executor(), address());
  std::optional<Result<predictfun::WalletJwt>> result;
  client.async_authenticate(
      signer,
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](Result<predictfun::WalletJwt> value) {
        result.emplace(std::move(value));
      });
  io.run();

  CHECK(result.has_value());
  CHECK(*result);
  CHECK(result && result->value().view() == "private.jwt.token");
  CHECK(signer->signed_message == "dynamic challenge");
  CHECK(transport->requests.size() == 2U);
  if (transport->requests.size() != 2U)
    return;
  const auto &get = transport->requests[0];
  CHECK(get.method == predictfun::net::HttpMethod::get);
  CHECK(get.target == "/v1/auth/message");
  CHECK(get.headers.size() == 1U);
  CHECK(get.headers[0].name == "x-api-key");
  CHECK(get.headers[0].value == "secret-api-key");
  CHECK(predictfun::net::sanitized_request_summary(get) ==
        "GET api.predict.fun:443/v1/auth/message");

  const auto &post = transport->requests[1];
  CHECK(post.method == predictfun::net::HttpMethod::post);
  CHECK(post.target == "/v1/auth");
  CHECK(post.content_type == "application/json");
  CHECK(post.body.find("dynamic challenge") != std::string::npos);
  CHECK(post.body.find("0x1234") != std::string::npos);
  CHECK(post.target.find("secret-api-key") == std::string::npos);
  CHECK(predictfun::net::sanitized_request_summary(post) ==
        "POST api.predict.fun:443/v1/auth");
}

void test_http_error_does_not_parse_secret_body() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(401, "server-secret-details"));
  predictfun::auth::AuthClient client(io.get_executor(), transport);
  std::optional<Result<predictfun::AuthMessage>> result;
  client.async_get_message(
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](Result<predictfun::AuthMessage> value) {
        result.emplace(std::move(value));
      });
  io.run();
  CHECK(result.has_value());
  CHECK(!*result);
  CHECK(result->error().code == ErrorCode::authentication_required);
  CHECK(result->error().message.find("server-secret-details") ==
        std::string::npos);
}

} // namespace

int main() {
  test_address_and_codecs();
  test_mainnet_requires_api_key();
  test_full_authentication_flow();
  test_http_error_does_not_parse_secret_body();
  if (failures != 0)
    std::cerr << failures << " test(s) failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
