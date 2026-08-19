#include "predictfun/private_rest/client.hpp"

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
using predictfun::net::HttpMethod;
using predictfun::net::HttpRequest;
using predictfun::net::HttpResponse;

class ScriptedTransport final : public predictfun::net::HttpTransport {
public:
  explicit ScriptedTransport(boost::asio::any_io_executor executor)
      : executor_(std::move(executor)) {}
  void push(HttpResponse response) {
    responses_.emplace_back(std::move(response));
  }
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

std::string market_json() {
  return R"({"id":42,"title":"BTC Up or Down","question":"Will BTC go up?","tradingStatus":"OPEN","status":"OPEN","decimalPrecision":2,"isNegRisk":false,"isYieldBearing":true,"feeRateBps":100,"marketVariant":"CRYPTO_UP_DOWN","variantData":{"type":"CRYPTO_UP_DOWN","priceFeedProvider":"PYTH","priceFeedSymbol":"BTCUSD","priceFeedId":"feed","startPrice":"100.0"},"outcomes":[{"name":"Up","indexSet":1,"onChainId":"123","bestBid":{"price":"0.51","size":"10"},"bestAsk":{"price":"0.52","size":"9"}},{"name":"Down","indexSet":2,"onChainId":"456"}]})";
}

std::string order_json(std::string_view hash) {
  return std::string{R"({"order":{"hash":")"} + std::string{hash} +
         R"(","salt":"1","maker":"0x1111111111111111111111111111111111111111","signer":"0x2222222222222222222222222222222222222222","taker":"0x0000000000000000000000000000000000000000","tokenId":"123","makerAmount":"1000000000000000000","takerAmount":"500000000000000000","expiration":"0","nonce":"7","feeRateBps":"100","side":0,"signatureType":0,"signature":"0xsig"},"id":"o1","marketId":42,"currency":"USDT","amount":"1","amountFilled":"0.5","isNegRisk":false,"isYieldBearing":true,"strategy":"LIMIT","status":"OPEN","rewardEarningRate":"0.125"})";
}

predictfun::private_rest::ClientOptions options() {
  predictfun::private_rest::ClientOptions value;
  value.environment = predictfun::Environment::bnb_mainnet;
  value.api_key = [] { return "api-key-secret"; };
  value.jwt = [] { return predictfun::SecretString{"jwt-secret"}; };
  return value;
}

void test_exact_types() {
  CHECK(predictfun::Uint256::parse("00042").value().to_string() == "42");
  CHECK(predictfun::Uint256::parse("1157920892373161954235709850086879078532699"
                                   "84665640564039457584007913129639935"));
  CHECK(!predictfun::Uint256::parse("115792089237316195423570985008687907853269"
                                    "984665640564039457584007913129639936"));
  CHECK(predictfun::ExactDecimal::parse("-0.001250"));
  CHECK(!predictfun::ExactDecimal::parse("1e-3"));
}

void test_codecs() {
  const auto referral_request =
      predictfun::codec::encode_referral_request("A1b2C");
  CHECK(referral_request);
  CHECK(referral_request &&
        referral_request.value() == R"({"data":{"referralCode":"A1b2C"}})");
  CHECK(!predictfun::codec::encode_referral_request("ABCD"));
  CHECK(!predictfun::codec::encode_referral_request("AB!23"));
  const auto referral_response =
      predictfun::codec::decode_referral_response(R"({"success":true})");
  CHECK(referral_response && referral_response.value());
  CHECK(!predictfun::codec::decode_referral_response(R"({"success":false})"));

  const auto account = predictfun::codec::decode_account_response(
      R"({"success":true,"data":{"name":"alice","address":"0x1111111111111111111111111111111111111111","imageUrl":"https://example.invalid/a.png","referral":{"code":"ABC","status":"LOCKED"},"points":{"total":"12.5","rank":7}}})");
  CHECK(account);
  CHECK(account && account.value().name == "alice");
  CHECK(account && account.value().points &&
        account.value().points->total.to_string() == "12.5");

  const auto eoa_without_referral =
      predictfun::codec::decode_account_response(
          R"({"success":true,"data":{"name":"","address":"0x1111111111111111111111111111111111111111","referral":{}}})");
  CHECK(eoa_without_referral);
  CHECK(eoa_without_referral && !eoa_without_referral.value().referral);

  const auto partial_referral = predictfun::codec::decode_account_response(
      R"({"success":true,"data":{"name":"alice","address":"0x1111111111111111111111111111111111111111","referral":{"code":"ABC"}}})");
  CHECK(partial_referral);
  CHECK(partial_referral && !partial_referral.value().referral);

  const auto unbound_referral = predictfun::codec::decode_account_response(
      R"({"success":true,"data":{"name":"alice","address":"0x1111111111111111111111111111111111111111","referral":{"status":"UNBOUND"}}})");
  CHECK(unbound_referral);
  CHECK(unbound_referral && !unbound_referral.value().referral);

  const auto positions = predictfun::codec::decode_positions_response(
      "{\"success\":true,\"cursor\":\"next\",\"data\":[{\"id\":\"p1\","
      "\"market\":" +
      market_json() +
      R"(,"outcome":{"name":"Up","indexSet":1,"onChainId":"123"},"amount":"1.234567890123456789","valueUsd":"0.8","averageBuyPriceUsd":"0.65","pnlUsd":"-0.02"}]})");
  CHECK(positions);
  CHECK(positions && positions.value().positions.size() == 1U);
  CHECK(positions && positions.value().positions[0].market.id.value == 42U);

  const auto orders = predictfun::codec::decode_orders_response(
      R"({"success":true,"cursor":"next","data":[{"order":{"hash":"0xhash","salt":"1","maker":"0x1111111111111111111111111111111111111111","signer":"0x2222222222222222222222222222222222222222","taker":"0x0000000000000000000000000000000000000000","tokenId":"123","makerAmount":"1000000000000000000","takerAmount":"500000000000000000","expiration":"0","nonce":"7","feeRateBps":"100","side":0,"signatureType":0,"signature":"0xsig"},"id":"o1","marketId":42,"currency":"USDT","amount":"1","amountFilled":"0.5","isNegRisk":false,"isYieldBearing":true,"strategy":"LIMIT","status":"OPEN","rewardEarningRate":"0.125"}]})");
  CHECK(orders);
  CHECK(orders && orders.value().orders[0].order.taker == std::nullopt);
  CHECK(orders && orders.value().orders[0].order.side.value ==
                      predictfun::ContractSide::buy);

  const auto hash =
      "0x0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  const auto order = predictfun::codec::decode_order_response(
      std::string{R"({"success":true,"data":)"} + order_json(hash) + "}");
  CHECK(order);
  CHECK(order && order.value().order.hash == hash);
  CHECK(order && order.value().amount_filled.to_string() == "0.5");

  auto invalidated_json = order_json(hash);
  const auto status_offset = invalidated_json.find("\"status\":\"OPEN\"");
  CHECK(status_offset != std::string::npos);
  invalidated_json.replace(status_offset,
                           std::string{"\"status\":\"OPEN\""}.size(),
                           "\"status\":\"INVALIDATED\"");
  const auto invalidated = predictfun::codec::decode_order_response(
      std::string{R"({"success":true,"data":)"} + invalidated_json + "}");
  CHECK(invalidated);
  CHECK(invalidated && invalidated.value().status.value ==
                           predictfun::OrderStatus::invalidated);

  const auto activity = predictfun::codec::decode_activity_response(
      "{\"success\":true,\"data\":[{\"name\":\"MATCH\",\"createdAt\":\"2026-08-"
      "12T00:00:00Z\",\"transactionHash\":\"0xtx\",\"amountFilled\":\"2\","
      "\"priceExecuted\":\"0.5\",\"order\":{\"quoteType\":\"Ask\",\"amount\":"
      "\"2\",\"price\":\"0.5\",\"fee\":{\"amount\":\"0.01\",\"type\":"
      "\"COLLATERAL\"}},\"market\":" +
      market_json() +
      R"(,"outcome":{"name":"Up","indexSet":1,"onChainId":"123"}}]})");
  CHECK(activity);
  CHECK(activity && activity.value().events[0].order &&
        activity.value().events[0].order->fee);

  const auto secret = std::string{"jwt-value-must-not-leak"};
  const auto malformed = predictfun::codec::decode_orders_response(
      "{\"success\":true,\"data\":[" + secret);
  CHECK(!malformed);
  CHECK(malformed.error().message.find(secret) == std::string::npos);
}

void test_targets() {
  predictfun::private_rest::PositionsQuery positions;
  positions.first = 25U;
  positions.after = "cursor+/=";
  positions.market_id = predictfun::MarketId{42U};
  positions.is_resolved = false;
  positions.sort = "VALUE_DESC";
  const auto target =
      predictfun::private_rest::protocol::positions_target(positions);
  CHECK(target);
  CHECK(target &&
        target.value() == "/v1/"
                          "positions?first=25&after=cursor%2B%2F%3D&marketId="
                          "42&isResolved=false&sort=VALUE_DESC");

  predictfun::private_rest::ActivityQuery activity;
  activity.event_types = {"MATCH", "SPLIT"};
  const auto activity_target =
      predictfun::private_rest::protocol::activity_target(activity);
  CHECK(activity_target);
  CHECK(activity_target &&
        activity_target.value().find("eventTypes=MATCH") != std::string::npos);
  CHECK(activity_target &&
        activity_target.value().find("eventTypes=SPLIT") != std::string::npos);

  const auto hash =
      "0x0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  const auto order = predictfun::private_rest::protocol::order_target(hash);
  CHECK(order);
  CHECK(order && order.value() == std::string{"/v1/orders/"} + hash);
  CHECK(!predictfun::private_rest::protocol::order_target("0xnot-a-hash"));
}

void test_authenticated_get() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(
      200,
      R"({"success":true,"data":{"name":"alice","address":"0x1111111111111111111111111111111111111111"}})"));
  predictfun::private_rest::PrivateRestClient client(io.get_executor(),
                                                     transport, options());
  std::optional<Result<predictfun::Account>> result;
  client.async_get_account(
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](Result<predictfun::Account> value) {
        result.emplace(std::move(value));
      });
  io.run();
  CHECK(result && *result);
  CHECK(transport->requests.size() == 1U);
  if (transport->requests.empty())
    return;
  const auto &request = transport->requests[0];
  CHECK(request.target == "/v1/account");
  CHECK(request.headers.size() == 2U);
  CHECK(request.headers[0].name == "x-api-key");
  CHECK(request.headers[1].name == "Authorization");
  CHECK(request.headers[1].value == "Bearer jwt-secret");
  const auto summary = predictfun::net::sanitized_request_summary(request);
  CHECK(summary == "GET api.predict.fun:443/v1/account");
  CHECK(summary.find("jwt-secret") == std::string::npos);
}

void test_missing_jwt_stops_before_transport() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  auto client_options = options();
  client_options.jwt = [] { return predictfun::SecretString{}; };
  predictfun::private_rest::PrivateRestClient client(
      io.get_executor(), transport, std::move(client_options));
  std::optional<Result<predictfun::Account>> result;
  client.async_get_account(
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](Result<predictfun::Account> value) {
        result.emplace(std::move(value));
      });
  io.run();
  CHECK(result && !*result);
  CHECK(result->error().code == ErrorCode::authentication_required);
  CHECK(transport->requests.empty());
}

void test_get_order_by_hash() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  const auto hash =
      "0x0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  transport->push(response(200, std::string{R"({"success":true,"data":)"} +
                                    order_json(hash) + "}"));
  predictfun::private_rest::PrivateRestClient client(io.get_executor(),
                                                     transport, options());
  std::optional<Result<predictfun::OrderRecord>> result;
  client.async_get_order(
      hash,
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](Result<predictfun::OrderRecord> value) {
        result.emplace(std::move(value));
      });
  io.run();
  CHECK(result && *result);
  CHECK(result && *result && result->value().order.hash == hash);
  CHECK(transport->requests.size() == 1U);
  CHECK(!transport->requests.empty() &&
        transport->requests[0].target == std::string{"/v1/orders/"} + hash);
}

void test_referral_is_single_shot() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(503, R"({"message":"temporary"})"));
  transport->push(response(201, R"({"success":true})"));
  auto client_options = options();
  client_options.max_get_retries = 9U;
  predictfun::private_rest::PrivateRestClient client(
      io.get_executor(), transport, std::move(client_options));
  std::optional<Result<predictfun::MutationOutcome<bool>>> result;
  client.async_set_referral(
      "A1b2C",
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](Result<predictfun::MutationOutcome<bool>> value) {
        result.emplace(std::move(value));
      });
  io.run();
  CHECK(result && *result);
  CHECK(result && *result &&
        result->value().disposition ==
            predictfun::MutationDisposition::ambiguous);
  CHECK(result && *result && result->value().ambiguity &&
        result->value().ambiguity->code == ErrorCode::http_server_error);
  CHECK(transport->requests.size() == 1U);
  if (transport->requests.empty())
    return;
  const auto &request = transport->requests.front();
  CHECK(request.method == HttpMethod::post);
  CHECK(request.target == "/v1/account/referral");
  CHECK(request.body == R"({"data":{"referralCode":"A1b2C"}})");
  CHECK(predictfun::net::sanitized_request_summary(request) ==
        "POST api.predict.fun:443/v1/account/referral");
}

void test_referral_success_and_validation() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(201, R"({"success":true})"));
  predictfun::private_rest::PrivateRestClient client(io.get_executor(),
                                                     transport, options());
  std::optional<Result<predictfun::MutationOutcome<bool>>> result;
  client.async_set_referral(
      "ABCDE",
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](Result<predictfun::MutationOutcome<bool>> value) {
        result.emplace(std::move(value));
      });
  io.run();
  CHECK(result && *result);
  CHECK(result && *result && result->value().acknowledged());
  CHECK(result && *result && result->value().receipt &&
        *result->value().receipt);
  CHECK(transport->requests.size() == 1U);

  boost::asio::io_context invalid_io;
  auto invalid_transport =
      std::make_shared<ScriptedTransport>(invalid_io.get_executor());
  predictfun::private_rest::PrivateRestClient invalid_client(
      invalid_io.get_executor(), invalid_transport, options());
  std::optional<Result<predictfun::MutationOutcome<bool>>> invalid_result;
  invalid_client.async_set_referral(
      "TOO-LONG",
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&invalid_result](Result<predictfun::MutationOutcome<bool>> value) {
        invalid_result.emplace(std::move(value));
      });
  invalid_io.run();
  CHECK(invalid_result && !*invalid_result);
  CHECK(invalid_result &&
        invalid_result->error().code == ErrorCode::invalid_argument);
  CHECK(invalid_transport->requests.empty());
}

} // namespace

int main() {
  test_exact_types();
  test_codecs();
  test_targets();
  test_authenticated_get();
  test_missing_jwt_stops_before_transport();
  test_get_order_by_hash();
  test_referral_is_single_shot();
  test_referral_success_and_validation();
  if (failures != 0)
    std::cerr << failures << " test(s) failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
