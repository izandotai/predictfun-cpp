#include "predictfun/codec/private_websocket.hpp"

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

std::string event(std::string_view type, std::string extra = {}) {
  return std::string{R"({"type":"M","topic":"predictWalletEvents/secret.jwt.value","data":{"type":")"} +
         std::string{type} +
         R"(","orderId":"123","orderHash":"0xabc","walletAddress":"0x1111111111111111111111111111111111111111","timestamp":1736696400000,"details":{"marketId":42,"outcomeIndex":0,"marketQuestion":"Will X happen?","outcome":"YES","quoteType":"BID","quantity":"100.000","quantityFilled":"0.000","price":"0.620","value":"62.00","valueFilled":"0.00","strategyType":"LIMIT","categorySlug":"crypto"})" +
         extra + "}}";
}

void test_subscription_and_base_event() {
  auto request = codec::encode_wallet_subscribe_request(7U, "a.b_c-d");
  CHECK(request);
  CHECK(request.value().find("predictWalletEvents/a.b_c-d") != std::string::npos);
  CHECK(!codec::encode_wallet_subscribe_request(7U, "bad/token"));

  auto decoded = codec::decode_private_ws_frame(event("orderAccepted"));
  CHECK(decoded && std::holds_alternative<WalletEvent>(decoded.value()));
  if (decoded && std::holds_alternative<WalletEvent>(decoded.value())) {
    const auto &value = std::get<WalletEvent>(decoded.value());
    CHECK(value.type.value == WalletEventType::order_accepted);
    CHECK(value.details.market_id.value == 42U);
    CHECK(value.details.quantity.to_string() == "100.000");
  }
}

void test_transaction_and_redaction() {
  auto decoded = codec::decode_private_ws_frame(event(
      "orderTransactionSuccess",
      R"(,"settlementId":"999","fill":{"executedPriceWei":"620000000000000000","executedSizeWei":"100000000000000000000","executedValueWei":"62000000"},"isMaker":true,"fee":{"amountWei":"1000","type":"COLLATERAL"})"));
  CHECK(decoded);
  if (decoded) {
    const auto &value = std::get<WalletEvent>(decoded.value());
    CHECK(value.fill->executed_value_wei.to_string() == "62000000");
    CHECK(value.fee->type.value == WalletFeeType::collateral);
  }

  const std::string secret = "super.secret.jwt";
  auto malformed = codec::decode_private_ws_frame(
      std::string{"{\"type\":\"M\",\"topic\":\"predictWalletEvents/"} +
      secret);
  CHECK(!malformed);
  CHECK(malformed.error().message.find(secret) == std::string::npos);

  auto rejected = codec::decode_private_ws_frame(
      R"({"type":"R","requestId":1,"success":false,"error":{"code":"invalid_credentials","message":"predictWalletEvents/secret.jwt.value"}})"
  );
  CHECK(rejected);
  CHECK(std::get<SubscriptionResponse>(rejected.value()).error->message.empty());
}

} // namespace

int main() {
  test_subscription_and_base_event();
  test_transaction_and_redaction();
  if (failures != 0)
    std::cerr << failures << " private websocket codec checks failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
