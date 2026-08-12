#include "predictfun/public_rest/client.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stop_token>
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

constexpr auto market_json = R"({
  "success":true,
  "data":{"id":42,"title":"BTC","question":"Up?","tradingStatus":"OPEN",
  "status":"REGISTERED","decimalPrecision":2,"isNegRisk":false,
  "isYieldBearing":false,"feeRateBps":0,"outcomes":[]}
})";

predictfun::net::HttpResponse success() {
  return predictfun::net::HttpResponse{200, market_json, {}};
}

class DuplicateCompletionTransport final
    : public predictfun::net::HttpTransport {
public:
  explicit DuplicateCompletionTransport(boost::asio::any_io_executor executor)
      : executor_(std::move(executor)) {}

  void async_request(predictfun::net::HttpRequest,
                     predictfun::net::RequestContext,
                     predictfun::net::ResponseHandler handler) override {
    auto duplicate = handler;
    boost::asio::dispatch(executor_, [handler = std::move(handler)]() mutable {
      handler(success());
    });
    boost::asio::dispatch(
        executor_,
        [handler = std::move(duplicate)]() mutable { handler(success()); });
  }

private:
  boost::asio::any_io_executor executor_;
};

class DeferredCompletionTransport final
    : public predictfun::net::HttpTransport {
public:
  void async_request(predictfun::net::HttpRequest,
                     predictfun::net::RequestContext,
                     predictfun::net::ResponseHandler handler) override {
    pending = std::move(handler);
  }

  void complete() {
    if (!pending)
      return;
    auto copy = pending;
    pending(success());
    copy(success());
  }

  predictfun::net::ResponseHandler pending;
};

void duplicate_transport_completion_is_delivered_once() {
  boost::asio::io_context io;
  auto transport =
      std::make_shared<DuplicateCompletionTransport>(io.get_executor());
  predictfun::public_rest::PublicRestClient client(io.get_executor(),
                                                   transport);
  std::size_t completions = 0U;
  client.async_get_market(
      predictfun::MarketId{42U},
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&completions](predictfun::Result<predictfun::Market> result) {
        CHECK(result);
        ++completions;
      });
  io.run();
  CHECK(completions == 1U);
}

void late_response_after_cancel_is_ignored() {
  boost::asio::io_context io;
  auto transport = std::make_shared<DeferredCompletionTransport>();
  predictfun::public_rest::PublicRestClient client(io.get_executor(),
                                                   transport);
  std::stop_source stop;
  std::size_t completions = 0U;
  std::optional<predictfun::ErrorCode> code;
  client.async_get_market(
      predictfun::MarketId{42U},
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1},
                                                    stop.get_token()),
      [&completions, &code](predictfun::Result<predictfun::Market> result) {
        ++completions;
        if (!result)
          code = result.error().code;
      });

  boost::asio::steady_timer cancel_timer(io);
  cancel_timer.expires_after(std::chrono::milliseconds{1});
  cancel_timer.async_wait(
      [&stop](const boost::system::error_code &) { stop.request_stop(); });
  boost::asio::steady_timer response_timer(io);
  response_timer.expires_after(std::chrono::milliseconds{2});
  response_timer.async_wait([transport](const boost::system::error_code &) {
    transport->complete();
  });
  io.run();

  CHECK(completions == 1U);
  CHECK(code == predictfun::ErrorCode::cancelled);
}

} // namespace

int main() {
  duplicate_transport_completion_is_delivered_once();
  late_response_after_cancel_is_ignored();
  if (failures != 0)
    std::cerr << failures << " fault-injection assertion(s) failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
