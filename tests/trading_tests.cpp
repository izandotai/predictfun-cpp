#include "predictfun/trading/client.hpp"

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

constexpr auto hash_a =
    "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr auto hash_b =
    "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

class ScriptedTransport final : public predictfun::net::HttpTransport {
public:
  explicit ScriptedTransport(boost::asio::any_io_executor executor)
      : executor_(std::move(executor)) {}

  void push(HttpResponse response) {
    responses_.emplace_back(std::move(response));
  }
  void push(Error error) { responses_.emplace_back(std::move(error)); }

  void async_request(HttpRequest request, predictfun::net::RequestContext,
                     predictfun::net::ResponseHandler handler) override {
    requests.push_back(std::move(request));
    auto result =
        responses_.empty()
            ? Result<HttpResponse>{Error{
                  ErrorCode::protocol_error, "empty scripted queue", {}}}
            : std::move(responses_.front());
    if (!responses_.empty())
      responses_.pop_front();
    boost::asio::dispatch(executor_, [handler = std::move(handler),
                                      result = std::move(result)]() mutable {
      handler(std::move(result));
    });
  }

  std::vector<HttpRequest> requests;

private:
  boost::asio::any_io_executor executor_;
  std::deque<Result<HttpResponse>> responses_;
};

HttpResponse response(int status, std::string body) {
  HttpResponse value;
  value.status = status;
  value.body = std::move(body);
  return value;
}

predictfun::Uint256 uint256(std::string_view text) {
  return predictfun::Uint256::parse(text).value();
}

predictfun::EvmAddress address(std::string_view text) {
  return predictfun::EvmAddress::parse(text).value();
}

predictfun::CreateOrderRequest order_request() {
  predictfun::CreateOrderRequest request;
  request.order_hash = hash_a;
  request.price_per_share_wei = uint256("500000000000000000");
  request.strategy = predictfun::ExecutionStrategy::limit;
  request.is_post_only = true;
  request.self_trade_prevention = predictfun::SelfTradePrevention::cancel_taker;
  request.order.salt = uint256("1");
  request.order.maker = address("0x1111111111111111111111111111111111111111");
  request.order.signer = address("0x2222222222222222222222222222222222222222");
  request.order.token_id = uint256("123");
  request.order.maker_amount = uint256("1000000000000000000");
  request.order.taker_amount = uint256("2000000000000000000");
  request.order.expiration = uint256("0");
  request.order.nonce = uint256("7");
  request.order.fee_rate_bps = uint256("100");
  request.order.side = predictfun::ContractSide::buy;
  request.order.signature_type = predictfun::SignatureType::eoa;
  request.order.signature = "0x" + std::string(130U, 'a');
  return request;
}

predictfun::trading::ClientOptions options() {
  predictfun::trading::ClientOptions value;
  value.environment = predictfun::Environment::bnb_mainnet;
  value.api_key = [] { return "api-key-secret"; };
  value.jwt = [] { return predictfun::SecretString{"jwt-secret"}; };
  return value;
}

template <class T>
using MutationResult = Result<predictfun::MutationOutcome<T>>;

void test_codec() {
  auto request = order_request();
  request.slippage_bps = uint256("25");
  request.reserved_balance_policy =
      predictfun::ReservedBalancePolicy::reject_market_order;
  const auto encoded = predictfun::codec::encode_create_order_request(request);
  CHECK(encoded);
  CHECK(encoded &&
        encoded.value().find(R"("pricePerShare":"500000000000000000")") !=
            std::string::npos);
  CHECK(encoded &&
        encoded.value().find(R"("expiration":0)") != std::string::npos);
  CHECK(encoded && encoded.value().find(
                       R"("reservedBalancePolicy":"REJECT_MARKET_ORDER")") !=
                       std::string::npos);
  CHECK(encoded &&
        encoded.value().find(R"("selfTradePrevention":"CANCEL_TAKER")") !=
            std::string::npos);

  const auto receipt = predictfun::codec::decode_create_order_response(
      std::string{
          R"({"success":true,"data":{"code":"OK","orderId":"id-1","orderHash":")"} +
      hash_a + R"(","removalLockedUntil":"soon"}})");
  CHECK(receipt);
  CHECK(receipt && receipt.value().order_id == "id-1");

  const auto removed = predictfun::codec::decode_remove_orders_response(
      R"({"success":true,"removed":["id-1"],"noop":["id-2"]})");
  CHECK(removed);
  CHECK(removed && removed.value().removed.size() == 1U);
  const auto empty =
      predictfun::codec::decode_remove_order_hashes_response("{}");
  CHECK(empty);
  CHECK(empty && empty.value().removed.empty());
}

void test_create_acknowledged() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(
      201,
      std::string{
          R"({"success":true,"data":{"code":"OK","orderId":"id-1","orderHash":")"} +
          hash_a + R"("}})"));
  predictfun::trading::TradingClient client(io.get_executor(), transport,
                                            options());
  std::optional<MutationResult<predictfun::CreateOrderReceipt>> result;
  client.async_create_order(
      order_request(),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](auto value) { result.emplace(std::move(value)); });
  io.run();
  CHECK(result && *result);
  CHECK(result && *result && result->value().acknowledged());
  CHECK(result && *result && result->value().receipt &&
        result->value().receipt->order_hash == hash_a);
  CHECK(transport->requests.size() == 1U);
  if (transport->requests.empty())
    return;
  CHECK(transport->requests[0].target == "/v1/orders");
  CHECK(transport->requests[0].method == predictfun::net::HttpMethod::post);
  CHECK(transport->requests[0].headers.size() == 2U);
  CHECK(predictfun::net::sanitized_request_summary(transport->requests[0]) ==
        "POST api.predict.fun:443/v1/orders");
}

void test_explicit_rejection_is_not_ambiguous() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(400, R"({"success":false})"));
  predictfun::trading::TradingClient client(io.get_executor(), transport,
                                            options());
  std::optional<MutationResult<predictfun::CreateOrderReceipt>> result;
  client.async_create_order(
      order_request(),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](auto value) { result.emplace(std::move(value)); });
  io.run();
  CHECK(result && !*result);
  CHECK(result && result->error().code == ErrorCode::http_client_error);
  CHECK(transport->requests.size() == 1U);
}

void test_server_failure_is_ambiguous_and_never_retried() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(503, R"({"error":"later"})"));
  predictfun::trading::TradingClient client(io.get_executor(), transport,
                                            options());
  std::optional<MutationResult<predictfun::CreateOrderReceipt>> result;
  client.async_create_order(
      order_request(),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](auto value) { result.emplace(std::move(value)); });
  io.run();
  CHECK(result && *result);
  CHECK(result && *result && !result->value().acknowledged());
  CHECK(result && *result && result->value().ambiguity &&
        result->value().ambiguity->code == ErrorCode::ambiguous_submission);
  CHECK(result && *result && result->value().reconciliation_key == hash_a);
  CHECK(transport->requests.size() == 1U);
}

void test_transport_failure_is_ambiguous_and_never_retried() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(
      Error{ErrorCode::read_failure, "connection lost after write", {}});
  predictfun::trading::TradingClient client(io.get_executor(), transport,
                                            options());
  std::optional<MutationResult<predictfun::CreateOrderReceipt>> result;
  client.async_create_order(
      order_request(),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](auto value) { result.emplace(std::move(value)); });
  io.run();
  CHECK(result && *result && result->value().ambiguity);
  CHECK(transport->requests.size() == 1U);
}

void test_malformed_success_is_ambiguous() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(201, "{"));
  predictfun::trading::TradingClient client(io.get_executor(), transport,
                                            options());
  std::optional<MutationResult<predictfun::CreateOrderReceipt>> result;
  client.async_create_order(
      order_request(),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](auto value) { result.emplace(std::move(value)); });
  io.run();
  CHECK(result && *result && result->value().ambiguity);
  CHECK(transport->requests.size() == 1U);
}

void test_remove_variants() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(
      response(200, R"({"success":true,"removed":["id-1"],"noop":[]})"));
  transport->push(response(200, "{}"));
  predictfun::trading::TradingClient client(io.get_executor(), transport,
                                            options());
  std::optional<MutationResult<predictfun::RemoveOrdersReceipt>> ids;
  std::optional<MutationResult<predictfun::RemoveOrdersReceipt>> hashes;
  client.async_remove_order_ids(
      {"id-1"},
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&ids](auto value) { ids.emplace(std::move(value)); });
  client.async_remove_order_hashes(
      {hash_b},
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&hashes](auto value) { hashes.emplace(std::move(value)); });
  io.run();
  CHECK(ids && *ids && ids->value().acknowledged());
  CHECK(hashes && *hashes && hashes->value().acknowledged());
  CHECK(transport->requests.size() == 2U);
  if (transport->requests.size() != 2U)
    return;
  CHECK(transport->requests[0].target == "/v1/orders/remove");
  CHECK(transport->requests[1].target == "/orders/remove-by-hash");
}

void test_invalid_input_stops_before_transport() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  predictfun::trading::TradingClient client(io.get_executor(), transport,
                                            options());
  auto request = order_request();
  request.order_hash = "bad";
  std::optional<MutationResult<predictfun::CreateOrderReceipt>> result;
  client.async_create_order(
      std::move(request),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](auto value) { result.emplace(std::move(value)); });
  io.run();
  CHECK(result && !*result);
  CHECK(result && result->error().code == ErrorCode::invalid_argument);
  CHECK(transport->requests.empty());
}

} // namespace

int main() {
  test_codec();
  test_create_acknowledged();
  test_explicit_rejection_is_not_ambiguous();
  test_server_failure_is_ambiguous_and_never_retried();
  test_transport_failure_is_ambiguous_and_never_retried();
  test_malformed_success_is_ambiguous();
  test_remove_variants();
  test_invalid_input_stops_before_transport();
  if (failures != 0)
    std::cerr << failures << " test(s) failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
