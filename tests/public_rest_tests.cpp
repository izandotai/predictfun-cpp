#include "predictfun/net/rate_limiter.hpp"
#include "predictfun/public_rest/client.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <stop_token>
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
using predictfun::net::RequestContext;

class ScriptedTransport final : public predictfun::net::HttpTransport {
public:
  explicit ScriptedTransport(boost::asio::any_io_executor executor)
      : executor_(std::move(executor)) {}

  void push(HttpResponse response) {
    responses_.emplace_back(std::move(response));
  }
  void push(Error error) { responses_.emplace_back(std::move(error)); }

  void async_request(HttpRequest request, RequestContext,
                     predictfun::net::ResponseHandler handler) override {
    requests.push_back(std::move(request));
    if (responses_.empty()) {
      boost::asio::dispatch(
          executor_, [handler = std::move(handler)]() mutable {
            handler(Error{
                ErrorCode::protocol_error, "mock response queue is empty", {}});
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

constexpr auto market_json = R"({
  "success": true,
  "data": {
    "id": 42,
    "title": "BTC Up or Down",
    "question": "Will BTC finish up?",
    "tradingStatus": "OPEN",
    "status": "REGISTERED",
    "decimalPrecision": 2,
    "isNegRisk": false,
    "isYieldBearing": false,
    "feeRateBps": 0,
    "outcomes": []
  }
})";

std::string match_json() {
  return R"({"success":true,"cursor":"next","data":[{"market":{"id":42,"title":"BTC Up or Down","question":"Will BTC finish up?","tradingStatus":"OPEN","status":"REGISTERED","decimalPrecision":2,"isNegRisk":false,"isYieldBearing":false,"feeRateBps":100,"outcomes":[]},"taker":{"quoteType":"Ask","amount":"2.5","price":"0.60","outcome":{"name":"Up","indexSet":1,"onChainId":"123"},"signer":"0x1111111111111111111111111111111111111111","fee":{"amount":"0.01","type":"COLLATERAL"}},"amountFilled":"2.0","priceExecuted":"0.61","makers":[{"quoteType":"Bid","amount":"2.0","price":"0.61","outcome":{"name":"Up","indexSet":1,"onChainId":"123"},"signer":"0x2222222222222222222222222222222222222222","fee":{"amount":"0.02","type":"SHARES"}}],"transactionHash":"0xabc","executedAt":"2026-08-12T00:00:00Z"}]})";
}

HttpResponse response(int status, std::string body = {}) {
  HttpResponse value;
  value.status = status;
  value.body = std::move(body);
  return value;
}

void test_protocol_targets() {
  using namespace predictfun::public_rest;

  MarketsQuery markets;
  markets.first = 50U;
  markets.after = "next/page";
  markets.status = "OPEN";
  markets.market_variant = "CRYPTO_5M";
  auto target = protocol::markets_target(markets);
  CHECK(target);
  CHECK(
      target.value() ==
      "/v1/"
      "markets?first=50&after=next%2Fpage&status=OPEN&marketVariant=CRYPTO_5M");

  TimeseriesQuery series;
  series.metric = "price";
  series.resolution = "1m";
  series.from = 100;
  series.to = 200;
  series.limit = 25U;
  auto series_target =
      protocol::timeseries_target(predictfun::MarketId{42U}, series);
  CHECK(series_target);
  CHECK(series_target.value() ==
        "/v1/markets/42/"
        "timeseries?metric=price&resolution=1m&from=100&to=200&limit=25");

  series.from = 201;
  CHECK(!protocol::timeseries_target(predictfun::MarketId{42U}, series));
  CHECK(!protocol::market_target(predictfun::MarketId{}));
  CHECK(!protocol::category_target("../secret"));

  MatchesQuery matches;
  matches.first = 20U;
  matches.after = "cursor+/=";
  matches.category = "crypto";
  matches.market_id = predictfun::MarketId{42U};
  matches.min_value_usdt_wei = predictfun::Uint256::parse("1000").value();
  matches.signer_address = predictfun::EvmAddress::parse(
      "0x1111111111111111111111111111111111111111").value();
  matches.is_signer_maker = true;
  const auto matches_target = protocol::matches_target(matches);
  CHECK(matches_target);
  CHECK(matches_target && matches_target.value() ==
      "/v1/orders/matches?first=20&after=cursor%2B%2F%3D&category=crypto&marketId=42&minValueUsdtWei=1000&signerAddress=0x1111111111111111111111111111111111111111&isSignerMaker=true");
}

void test_matches_codec_and_client() {
  const auto decoded = predictfun::codec::decode_matches_response(match_json());
  CHECK(decoded);
  CHECK(decoded && decoded.value().matches.size() == 1U);
  CHECK(decoded && decoded.value().matches[0].makers.size() == 1U);
  CHECK(decoded && decoded.value().matches[0].price_executed.to_string() ==
                       "0.61");
  CHECK(decoded && decoded.value().matches[0].taker.fee &&
        decoded.value().matches[0].taker.fee->amount.to_string() == "0.01");

  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(200, match_json()));
  predictfun::public_rest::ClientOptions options;
  options.environment = predictfun::Environment::bnb_mainnet;
  options.api_key = [] { return "top-secret-key"; };
  predictfun::public_rest::PublicRestClient client(io.get_executor(), transport,
                                                   options);
  predictfun::public_rest::MatchesQuery query;
  query.market_id = predictfun::MarketId{42U};
  std::optional<Result<predictfun::MatchesPage>> result;
  client.async_get_matches(
      query, RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](Result<predictfun::MatchesPage> value) {
        result.emplace(std::move(value));
      });
  io.run();
  CHECK(result && *result);
  CHECK(transport->requests.size() == 1U);
  CHECK(!transport->requests.empty() &&
        transport->requests[0].target == "/v1/orders/matches?marketId=42");
}

void test_api_key_is_header_only() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(200, market_json));

  predictfun::public_rest::ClientOptions options;
  options.environment = predictfun::Environment::bnb_mainnet;
  options.api_key = [] { return "top-secret-key"; };
  predictfun::public_rest::PublicRestClient client(io.get_executor(), transport,
                                                   options);
  std::optional<Result<predictfun::Market>> result;
  client.async_get_market(predictfun::MarketId{42U},
                          RequestContext::with_timeout(std::chrono::seconds{1}),
                          [&result](Result<predictfun::Market> value) {
                            result.emplace(std::move(value));
                          });
  io.run();

  CHECK(result.has_value());
  CHECK(*result);
  CHECK(result->value().id.value == 42U);
  CHECK(transport->requests.size() == 1U);
  if (transport->requests.empty())
    return;
  const auto &request = transport->requests.front();
  CHECK(request.host == "api.predict.fun");
  CHECK(request.target == "/v1/markets/42");
  CHECK(request.target.find("top-secret-key") == std::string::npos);
  CHECK(request.headers.size() == 1U);
  CHECK(request.headers.front().name == "x-api-key");
  CHECK(request.headers.front().value == "top-secret-key");
  CHECK(predictfun::net::sanitized_request_summary(request) ==
        "GET api.predict.fun:443/v1/markets/42");
}

void test_mainnet_requires_key() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  predictfun::public_rest::ClientOptions options;
  options.environment = predictfun::Environment::bnb_mainnet;
  predictfun::public_rest::PublicRestClient client(io.get_executor(), transport,
                                                   options);
  std::optional<Result<predictfun::Market>> result;
  client.async_get_market(predictfun::MarketId{42U},
                          RequestContext::with_timeout(std::chrono::seconds{1}),
                          [&result](Result<predictfun::Market> value) {
                            result.emplace(std::move(value));
                          });
  io.run();
  CHECK(result.has_value());
  CHECK(!*result);
  CHECK(result->error().code == ErrorCode::authentication_required);
  CHECK(transport->requests.empty());
}

void test_retry_and_http_classification() {
  {
    boost::asio::io_context io;
    auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
    auto throttled = response(429);
    throttled.headers.push_back({"Retry-After", "0"});
    transport->push(std::move(throttled));
    transport->push(response(200, market_json));
    predictfun::public_rest::ClientOptions options;
    options.max_get_retries = 1U;
    predictfun::public_rest::PublicRestClient client(io.get_executor(),
                                                     transport, options);
    std::optional<Result<predictfun::Market>> result;
    client.async_get_market(
        predictfun::MarketId{42U},
        RequestContext::with_timeout(std::chrono::seconds{1}),
        [&result](Result<predictfun::Market> value) {
          result.emplace(std::move(value));
        });
    io.run();
    CHECK(result.has_value());
    CHECK(*result);
    CHECK(transport->requests.size() == 2U);
  }

  {
    boost::asio::io_context io;
    auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
    transport->push(response(302));
    predictfun::public_rest::PublicRestClient client(io.get_executor(),
                                                     transport);
    std::optional<Result<predictfun::Market>> result;
    client.async_get_market(
        predictfun::MarketId{42U},
        RequestContext::with_timeout(std::chrono::seconds{1}),
        [&result](Result<predictfun::Market> value) {
          result.emplace(std::move(value));
        });
    io.run();
    CHECK(result.has_value());
    CHECK(!*result);
    CHECK(result->error().code == ErrorCode::http_redirect);
    CHECK(result->error().http_status == 302);
  }
}

void test_rate_limiter_reservations() {
  predictfun::net::RateLimitPolicy policy;
  policy.global_requests_per_minute = 1U;
  predictfun::net::RateLimiter limiter(policy);
  const auto now = std::chrono::steady_clock::now();
  const auto first = limiter.reserve("market", now);
  const auto second = limiter.reserve("market", now);
  const auto third = limiter.reserve("market", now);
  CHECK(first == std::chrono::milliseconds{0});
  CHECK(second >= std::chrono::milliseconds{59'999});
  CHECK(second <= std::chrono::milliseconds{60'001});
  CHECK(third >= std::chrono::milliseconds{119'999});
  CHECK(third <= std::chrono::milliseconds{120'001});
}

void test_rate_limiter_cooldowns_and_retry_after() {
  predictfun::net::RateLimitPolicy policy;
  policy.global_requests_per_minute = 60'000U;
  policy.endpoint_requests_per_minute.emplace("market", 60'000U);
  predictfun::net::RateLimiter limiter(policy);
  const auto now = std::chrono::steady_clock::now();
  limiter.penalize("market", now, std::chrono::seconds{5});
  CHECK(limiter.reserve("market", now) >= std::chrono::seconds{5});
  CHECK(limiter.reserve("orderbook", now) < std::chrono::milliseconds{10});

  predictfun::net::RateLimiter global_limiter(policy);
  global_limiter.penalize("market", now, std::chrono::seconds{4}, true);
  CHECK(global_limiter.reserve("orderbook", now) >=
        std::chrono::seconds{4});

  const auto epoch = std::chrono::system_clock::from_time_t(0);
  CHECK(predictfun::net::parse_retry_after(
            "7", epoch, std::chrono::milliseconds{1}) ==
        std::chrono::seconds{7});
  CHECK(predictfun::net::parse_retry_after(
            "Thu, 01 Jan 1970 00:01:00 GMT", epoch,
            std::chrono::milliseconds{1}) == std::chrono::seconds{60});
  CHECK(predictfun::net::parse_retry_after(
            "invalid", epoch, std::chrono::milliseconds{321}) ==
        std::chrono::milliseconds{321});
  CHECK(predictfun::net::parse_retry_after(
            "999", epoch, std::chrono::milliseconds{1},
            std::chrono::seconds{9}) == std::chrono::seconds{9});
}

void test_rate_wait_consumes_reservation_once() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(200, market_json));
  transport->push(response(200, market_json));
  predictfun::public_rest::ClientOptions options;
  options.rate_limits.global_requests_per_minute = 60'000U;
  predictfun::public_rest::PublicRestClient client(io.get_executor(), transport,
                                                   options);
  std::optional<Result<predictfun::Market>> first;
  std::optional<Result<predictfun::Market>> second;
  client.async_get_market(
      predictfun::MarketId{42U},
      RequestContext::with_timeout(std::chrono::seconds{1}),
      [&first](Result<predictfun::Market> value) {
        first.emplace(std::move(value));
      });
  client.async_get_market(
      predictfun::MarketId{42U},
      RequestContext::with_timeout(std::chrono::seconds{1}),
      [&second](Result<predictfun::Market> value) {
        second.emplace(std::move(value));
      });
  io.run();
  CHECK(first && *first);
  CHECK(second && *second);
  CHECK(transport->requests.size() == 2U);
}

void test_cancel_during_rate_wait() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(200, market_json));
  predictfun::public_rest::ClientOptions options;
  options.rate_limits.global_requests_per_minute = 1U;
  predictfun::public_rest::PublicRestClient client(io.get_executor(), transport,
                                                   options);
  std::optional<Result<predictfun::Market>> first;
  std::optional<Result<predictfun::Market>> second;
  std::stop_source stop;
  client.async_get_market(predictfun::MarketId{42U},
                          RequestContext::with_timeout(std::chrono::seconds{1}),
                          [&first](Result<predictfun::Market> value) {
                            first.emplace(std::move(value));
                          });
  client.async_get_market(
      predictfun::MarketId{42U},
      RequestContext::with_timeout(std::chrono::minutes{2}, stop.get_token()),
      [&second](Result<predictfun::Market> value) {
        second.emplace(std::move(value));
      });
  boost::asio::steady_timer timer(io);
  timer.expires_after(std::chrono::milliseconds{2});
  timer.async_wait(
      [&stop](const boost::system::error_code &) { stop.request_stop(); });
  io.run();

  CHECK(first.has_value());
  CHECK(*first);
  CHECK(second.has_value());
  CHECK(!*second);
  CHECK(second->error().code == ErrorCode::cancelled);
  CHECK(transport->requests.size() == 1U);
}

void test_shared_limiter_propagates_server_cooldown() {
  boost::asio::io_context io;
  auto limiter = std::make_shared<predictfun::net::RateLimiter>();

  auto first_transport =
      std::make_shared<ScriptedTransport>(io.get_executor());
  auto throttled = response(429);
  throttled.headers.push_back({"Retry-After", "1"});
  first_transport->push(std::move(throttled));

  predictfun::public_rest::ClientOptions options;
  options.max_get_retries = 0U;
  options.rate_limiter = limiter;
  predictfun::public_rest::PublicRestClient first_client(
      io.get_executor(), first_transport, options);
  std::optional<Result<predictfun::Market>> first;
  first_client.async_get_market(
      predictfun::MarketId{42U},
      RequestContext::with_timeout(std::chrono::seconds{1}),
      [&first](Result<predictfun::Market> value) {
        first.emplace(std::move(value));
      });
  io.run();

  CHECK(first.has_value());
  CHECK(!*first);
  CHECK(first->error().code == ErrorCode::rate_limited);
  CHECK(first_transport->requests.size() == 1U);

  io.restart();
  auto second_transport =
      std::make_shared<ScriptedTransport>(io.get_executor());
  second_transport->push(response(200, market_json));
  predictfun::public_rest::PublicRestClient second_client(
      io.get_executor(), second_transport, options);
  std::optional<Result<predictfun::Market>> second;
  second_client.async_get_market(
      predictfun::MarketId{42U},
      RequestContext::with_timeout(std::chrono::milliseconds{10}),
      [&second](Result<predictfun::Market> value) {
        second.emplace(std::move(value));
      });
  io.run();

  CHECK(second.has_value());
  CHECK(!*second);
  CHECK(second->error().code == ErrorCode::deadline_exceeded);
  CHECK(second_transport->requests.empty());
}

} // namespace

int main() {
  test_protocol_targets();
  test_api_key_is_header_only();
  test_mainnet_requires_key();
  test_retry_and_http_classification();
  test_rate_limiter_reservations();
  test_rate_limiter_cooldowns_and_retry_after();
  test_rate_wait_consumes_reservation_once();
  test_cancel_during_rate_wait();
  test_shared_limiter_propagates_server_cooldown();
  test_matches_codec_and_client();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "predictfun-cpp public REST tests passed\n";
  return EXIT_SUCCESS;
}
