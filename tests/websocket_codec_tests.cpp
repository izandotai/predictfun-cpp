#include "predictfun/codec/public_websocket.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <variant>

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

using namespace predictfun;

codec::PrecisionResolver precision() {
  return [](MarketId id) -> std::optional<std::uint8_t> {
    return id.value == 42U ? std::optional<std::uint8_t>{2U} : std::nullopt;
  };
}

void test_requests() {
  const PublicTopic topic{PublicTopicKind::orderbook, 42U, 2U};
  auto subscribe = codec::encode_subscribe_request(7U, topic);
  CHECK(subscribe);
  CHECK(subscribe.value() ==
        R"({"method":"subscribe","requestId":7,"params":["predictOrderbook/42"]})");
  auto unsubscribe = codec::encode_unsubscribe_request(8U, topic);
  CHECK(unsubscribe);
  CHECK(unsubscribe.value().find("unsubscribe") != std::string::npos);
  auto heartbeat = codec::encode_heartbeat_response(123456789);
  CHECK(heartbeat);
  CHECK(heartbeat.value() == R"({"method":"heartbeat","data":123456789})");
  CHECK(!codec::public_topic_name(
      PublicTopic{PublicTopicKind::orderbook, 42U, std::nullopt}));
}

void test_envelopes_and_orderbook() {
  auto ack = codec::decode_public_ws_frame(
      R"({"type":"R","requestId":7,"success":true})", precision());
  CHECK(ack && std::holds_alternative<SubscriptionResponse>(ack.value()));

  auto rejected = codec::decode_public_ws_frame(
      R"({"type":"R","requestId":8,"success":false,"error":{"code":"NO","message":"denied"}})",
      precision());
  CHECK(rejected);
  CHECK(std::get<SubscriptionResponse>(rejected.value()).error->code == "NO");

  auto heartbeat = codec::decode_public_ws_frame(
      R"({"type":"M","topic":"heartbeat","data":123456789})", precision());
  CHECK(heartbeat && std::get<HeartbeatMessage>(heartbeat.value()).timestamp_ms ==
                         123456789);

  constexpr auto book = R"({
    "type":"M","topic":"predictOrderbook/42","data":{
      "version":9,"marketId":42,"updateTimestampMs":1000,"orderCount":2,
      "asks":[[0.62,4.5]],"bids":[[0.61,3.25]],
      "settlementsPending":{"asks":[[0.63,1]],"bids":[[0.60,2]]}
    }})";
  auto decoded = codec::decode_public_ws_frame(book, precision());
  CHECK(decoded && std::holds_alternative<OrderbookMessage>(decoded.value()));
  if (decoded && std::holds_alternative<OrderbookMessage>(decoded.value())) {
    const auto &message = std::get<OrderbookMessage>(decoded.value());
    CHECK(message.schema_version == 9U);
    CHECK(message.book.market_id.value == 42U);
    CHECK(message.book.settlement_levels_pending.has_value());
  }

  auto mismatch = codec::decode_public_ws_frame(
      R"({"type":"M","topic":"predictTradingStatus/42","data":{"kind":"tradingStatusUpdate","tsMs":1000,"marketId":43,"tradingStatus":"OPEN"}})",
      precision());
  CHECK(!mismatch);
  auto unknown = codec::decode_public_ws_frame(
      R"({"type":"M","topic":"futurePublicTopic/42","data":{}})",
      precision());
  CHECK(unknown &&
        std::holds_alternative<UnknownPublicMessage>(unknown.value()));

  auto exact_decimal_timestamp = codec::decode_public_ws_frame(
      R"({"type":"M","topic":"predictCategoryChanged/9","data":{"kind":"categoryChanged","patchKind":"UPDATED","tsMs":1786529414095.0,"categoryId":9,"slug":"btc-updown-5m-1786529700"}})",
      precision());
  CHECK(exact_decimal_timestamp &&
        std::holds_alternative<CategoryChangedMessage>(
            exact_decimal_timestamp.value()));
  if (exact_decimal_timestamp &&
      std::holds_alternative<CategoryChangedMessage>(
          exact_decimal_timestamp.value())) {
    CHECK(std::get<CategoryChangedMessage>(exact_decimal_timestamp.value())
              .timestamp_ms == 1786529414095LL);
  }

  auto fractional_timestamp = codec::decode_public_ws_frame(
      R"({"type":"M","topic":"predictCategoryChanged/9","data":{"kind":"categoryChanged","tsMs":1786529414095.5,"categoryId":9,"slug":"btc-updown-5m-1786529700"}})",
      precision());
  CHECK(!fractional_timestamp);
}

void test_bounds() {
  codec::DecodeLimits limits;
  limits.max_body_bytes = 16U;
  auto result = codec::decode_public_ws_frame(
      R"({"type":"M","topic":"heartbeat","data":1})", precision(), limits);
  CHECK(!result);
  CHECK(result.error().code == ErrorCode::websocket_frame_too_large);
}

} // namespace

int main() {
  test_requests();
  test_envelopes_and_orderbook();
  test_bounds();
  if (failures != 0)
    std::cerr << failures << " websocket codec checks failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
