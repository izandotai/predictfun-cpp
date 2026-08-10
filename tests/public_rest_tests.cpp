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

  void async_get(HttpRequest request, RequestContext,
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

} // namespace

int main() {
  test_protocol_targets();
  test_api_key_is_header_only();
  test_mainnet_requires_key();
  test_retry_and_http_classification();
  test_rate_limiter_reservations();
  test_cancel_during_rate_wait();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "predictfun-cpp public REST tests passed\n";
  return EXIT_SUCCESS;
}
