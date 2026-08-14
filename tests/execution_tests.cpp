#include "predictfun/execution/session.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

constexpr auto order_hash =
    "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

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
  return HttpResponse{status, std::move(body), {}};
}

predictfun::Uint256 uint256(std::string_view value) {
  return predictfun::Uint256::parse(value).value();
}

predictfun::EvmAddress address(std::string_view value) {
  return predictfun::EvmAddress::parse(value).value();
}

predictfun::CreateOrderRequest request() {
  predictfun::CreateOrderRequest value;
  value.order_hash = order_hash;
  value.price_per_share_wei = uint256("500000000000000000");
  value.strategy = predictfun::ExecutionStrategy::limit;
  value.order.salt = uint256("1");
  value.order.maker = address("0x1111111111111111111111111111111111111111");
  value.order.signer = address("0x2222222222222222222222222222222222222222");
  value.order.token_id = uint256("123");
  value.order.maker_amount = uint256("1000000000000000000");
  value.order.taker_amount = uint256("2000000000000000000");
  value.order.expiration = uint256("0");
  value.order.nonce = uint256("7");
  value.order.fee_rate_bps = uint256("100");
  value.order.side = predictfun::ContractSide::buy;
  value.order.signature_type = predictfun::SignatureType::eoa;
  value.order.signature = "0x" + std::string(130U, 'a');
  return value;
}

predictfun::trading::ClientOptions trading_options() {
  predictfun::trading::ClientOptions value;
  value.environment = predictfun::Environment::bnb_testnet;
  value.api_key = [] { return "test-api-key"; };
  value.jwt = [] { return predictfun::SecretString{"test-jwt"}; };
  return value;
}

predictfun::private_rest::ClientOptions rest_options() {
  predictfun::private_rest::ClientOptions value;
  value.environment = predictfun::Environment::bnb_testnet;
  value.api_key = [] { return "test-api-key"; };
  value.jwt = [] { return predictfun::SecretString{"test-jwt"}; };
  value.max_get_retries = 0U;
  return value;
}

std::filesystem::path journal_path(std::string_view name) {
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("predictfun-execution-" + std::string{name} + "-" +
          std::to_string(stamp) + ".journal");
}

struct SessionFixture {
  boost::asio::io_context io;
  std::shared_ptr<ScriptedTransport> transport{
      std::make_shared<ScriptedTransport>(io.get_executor())};
  std::shared_ptr<predictfun::trading::TradingClient> trading{
      std::make_shared<predictfun::trading::TradingClient>(
          io.get_executor(), transport, trading_options())};
  std::shared_ptr<predictfun::private_rest::PrivateRestClient> rest{
      std::make_shared<predictfun::private_rest::PrivateRestClient>(
          io.get_executor(), transport, rest_options())};
};

predictfun::execution::DurableOrderSession
open_session(SessionFixture &fixture, const std::filesystem::path &path,
             predictfun::execution::SessionOptions options = {}) {
  auto opened = predictfun::execution::DurableOrderSession::open(
      fixture.io.get_executor(), fixture.trading, fixture.rest, path, options);
  CHECK(opened);
  return std::move(opened.value());
}

std::string order_record_json(std::string_view status,
                              std::string_view filled) {
  return std::string{R"({"order":{"hash":")"} + order_hash +
         R"(","salt":"1","maker":"0x1111111111111111111111111111111111111111","signer":"0x2222222222222222222222222222222222222222","taker":"0x0000000000000000000000000000000000000000","tokenId":"123","makerAmount":"1000000000000000000","takerAmount":"2000000000000000000","expiration":"0","nonce":"7","feeRateBps":"100","side":0,"signatureType":0,"signature":"0xsig"},"id":"o1","marketId":42,"currency":"USDT","amount":"2","amountFilled":")" +
         std::string{filled} +
         R"(","isNegRisk":false,"isYieldBearing":true,"strategy":"LIMIT","status":")" +
         std::string{status} + R"(","rewardEarningRate":"0"})";
}

void test_protocol_amount_is_exact() {
  auto buy = request();
  auto amount = predictfun::execution::protocol::tracked_share_amount(buy);
  CHECK(amount && amount.value().to_string() == "2");

  auto sell = request();
  sell.order.side = predictfun::ContractSide::sell;
  sell.order.maker_amount = uint256("1250000000000000000");
  amount = predictfun::execution::protocol::tracked_share_amount(sell);
  CHECK(amount && amount.value().to_string() == "1.25");
}

void test_acknowledged_submission_is_journaled() {
  SessionFixture fixture;
  const auto path = journal_path("ack");
  fixture.transport->push(response(
      201,
      std::string{
          R"({"success":true,"data":{"code":"OK","orderId":"o1","orderHash":")"} +
          order_hash + R"("}})"));
  auto session = open_session(fixture, path);
  std::optional<Result<predictfun::execution::SubmissionResult>> submitted;
  session.async_submit_order(
      request(),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&submitted](auto result) { submitted.emplace(std::move(result)); });
  fixture.io.run();
  CHECK(submitted && *submitted);
  CHECK(submitted && *submitted && submitted->value().acknowledged());
  CHECK(submitted && *submitted &&
        submitted->value().tracked_order.amount.to_string() == "2");
  CHECK(submitted && *submitted &&
        submitted->value().tracked_order.state ==
            predictfun::OrderLifecycleState::accepted);

  auto replay = predictfun::lifecycle::OrderJournal(path).replay();
  CHECK(replay && replay.value().records_applied == 2U);
  CHECK(replay && replay.value().orders.size() == 1U);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

void test_explicit_rejection_is_terminal_and_durable() {
  SessionFixture fixture;
  const auto path = journal_path("reject");
  fixture.transport->push(response(400, R"({"success":false})"));
  auto session = open_session(fixture, path);
  std::optional<Result<predictfun::execution::SubmissionResult>> submitted;
  session.async_submit_order(
      request(),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&submitted](auto result) { submitted.emplace(std::move(result)); });
  fixture.io.run();
  CHECK(submitted && *submitted);
  CHECK(submitted && *submitted && submitted->value().rejected());
  CHECK(submitted && *submitted &&
        submitted->value().tracked_order.state ==
            predictfun::OrderLifecycleState::rejected);
  CHECK(submitted && *submitted &&
        submitted->value().tracked_order.safe_to_rebuild_with_new_nonce());

  auto reopened = predictfun::lifecycle::PersistentOrderTracker::open(path);
  CHECK(reopened);
  CHECK(reopened && reopened.value().find(order_hash)->terminal());
  CHECK(reopened &&
        !reopened.value().find(order_hash)->reconciliation_required);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

void test_ambiguous_submission_is_not_retried() {
  SessionFixture fixture;
  const auto path = journal_path("ambiguous");
  fixture.transport->push(response(503, R"({"message":"temporary"})"));
  fixture.transport->push(response(201, R"({"success":true})"));
  auto session = open_session(fixture, path);
  std::optional<Result<predictfun::execution::SubmissionResult>> submitted;
  session.async_submit_order(
      request(),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&submitted](auto result) { submitted.emplace(std::move(result)); });
  fixture.io.run();
  CHECK(submitted && *submitted);
  CHECK(submitted && *submitted && submitted->value().ambiguous());
  CHECK(submitted && *submitted &&
        submitted->value().tracked_order.reconciliation_required);
  CHECK(fixture.transport->requests.size() == 1U);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

void test_invalid_order_never_reaches_journal_or_transport() {
  SessionFixture fixture;
  const auto path = journal_path("invalid");
  auto session = open_session(fixture, path);
  auto invalid = request();
  invalid.order.signature.clear();
  std::optional<Result<predictfun::execution::SubmissionResult>> submitted;
  session.async_submit_order(
      std::move(invalid),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&submitted](auto result) { submitted.emplace(std::move(result)); });
  fixture.io.run();
  CHECK(submitted && !*submitted);
  CHECK(fixture.transport->requests.empty());
  auto replay = predictfun::lifecycle::OrderJournal(path).replay();
  CHECK(replay && replay.value().records_applied == 0U);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

void test_reconciliation_fetches_every_page_before_release() {
  SessionFixture fixture;
  const auto path = journal_path("reconcile");
  fixture.transport->push(response(
      201,
      std::string{
          R"({"success":true,"data":{"code":"OK","orderId":"o1","orderHash":")"} +
          order_hash + R"("}})"));
  auto session = open_session(fixture, path,
                              predictfun::execution::SessionOptions{1U, 4U});
  std::optional<Result<predictfun::execution::SubmissionResult>> submitted;
  session.async_submit_order(
      request(),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&submitted](auto result) { submitted.emplace(std::move(result)); });
  fixture.io.run();
  CHECK(submitted && *submitted);

  fixture.io.restart();
  fixture.transport->push(
      response(200, std::string{R"({"success":true,"cursor":"next","data":[)"} +
                        order_record_json("OPEN", "0.5") + "]}"));
  fixture.transport->push(response(200, R"({"success":true,"data":[]})"));
  std::optional<Result<predictfun::execution::ReconciliationResult>> result;
  session.async_reconcile(
      9U,
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](auto value) { result.emplace(std::move(value)); });
  fixture.io.run();
  CHECK(result && *result);
  CHECK(result && *result && result->value().pages == 2U);
  CHECK(result && *result && result->value().records == 1U);
  CHECK(result && *result && result->value().report.complete());
  CHECK(fixture.transport->requests.size() == 3U);
  CHECK(fixture.transport->requests.size() >= 3U &&
        fixture.transport->requests[1].target == "/v1/orders?first=1");
  CHECK(fixture.transport->requests.size() >= 3U &&
        fixture.transport->requests[2].target ==
            "/v1/orders?first=1&after=next");

  fixture.io.restart();
  std::optional<Result<std::vector<predictfun::TrackedOrder>>> snapshot;
  session.async_snapshot(
      [&snapshot](auto value) { snapshot.emplace(std::move(value)); });
  fixture.io.run();
  CHECK(snapshot && *snapshot && snapshot->value().size() == 1U);
  CHECK(snapshot && *snapshot &&
        snapshot->value()[0].state ==
            predictfun::OrderLifecycleState::partially_filled);
  CHECK(snapshot && *snapshot && !snapshot->value()[0].reconciliation_required);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

void test_reconciliation_rejects_repeated_cursor() {
  SessionFixture fixture;
  const auto path = journal_path("repeated-cursor");
  fixture.transport->push(response(
      201,
      std::string{
          R"({"success":true,"data":{"code":"OK","orderId":"o1","orderHash":")"} +
          order_hash + R"("}})"));
  auto session = open_session(fixture, path,
                              predictfun::execution::SessionOptions{1U, 4U});
  std::optional<Result<predictfun::execution::SubmissionResult>> submitted;
  session.async_submit_order(
      request(),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&submitted](auto result) { submitted.emplace(std::move(result)); });
  fixture.io.run();
  CHECK(submitted && *submitted);

  fixture.io.restart();
  fixture.transport->push(
      response(200, R"({"success":true,"cursor":"repeat","data":[]})"));
  fixture.transport->push(
      response(200, R"({"success":true,"cursor":"repeat","data":[]})"));
  std::optional<Result<predictfun::execution::ReconciliationResult>> result;
  session.async_reconcile(
      10U,
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&result](auto value) { result.emplace(std::move(value)); });
  fixture.io.run();
  CHECK(result && !*result);
  CHECK(result && !*result &&
        result->error().code == predictfun::ErrorCode::protocol_error);

  fixture.io.restart();
  std::optional<Result<std::vector<predictfun::TrackedOrder>>> snapshot;
  session.async_snapshot(
      [&snapshot](auto value) { snapshot.emplace(std::move(value)); });
  fixture.io.run();
  CHECK(snapshot && *snapshot && snapshot->value().size() == 1U);
  CHECK(snapshot && *snapshot && snapshot->value()[0].reconciliation_required);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

} // namespace

int main() {
  test_protocol_amount_is_exact();
  test_acknowledged_submission_is_journaled();
  test_explicit_rejection_is_terminal_and_durable();
  test_ambiguous_submission_is_not_retried();
  test_invalid_order_never_reaches_journal_or_transport();
  test_reconciliation_fetches_every_page_before_release();
  test_reconciliation_rejects_repeated_cursor();
  if (failures != 0)
    std::cerr << failures << " test(s) failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
