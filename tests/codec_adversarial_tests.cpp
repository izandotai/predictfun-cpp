#include "predictfun/codec/auth.hpp"
#include "predictfun/codec/private_rest.hpp"
#include "predictfun/codec/private_websocket.hpp"
#include "predictfun/codec/public_rest.hpp"
#include "predictfun/codec/public_websocket.hpp"
#include "predictfun/codec/trading.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
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

class DeterministicRandom {
public:
  std::uint64_t next() noexcept {
    state_ ^= state_ << 13U;
    state_ ^= state_ >> 7U;
    state_ ^= state_ << 17U;
    return state_;
  }

private:
  std::uint64_t state_{0xf022ba11adULL};
};

void decode_everything(std::string_view input) {
  predictfun::codec::DecodeLimits limits;
  limits.max_body_bytes = 1'024U;
  limits.max_markets = 32U;
  limits.max_categories = 32U;
  limits.max_outcomes_per_market = 16U;
  limits.max_book_levels_per_side = 64U;
  limits.max_timeseries_points = 64U;
  limits.max_matches = 32U;
  limits.max_makers_per_match = 32U;
  limits.max_string_bytes = 128U;
  const auto resolver =
      [](predictfun::MarketId) -> std::optional<std::uint8_t> { return 2U; };

  (void)predictfun::codec::decode_markets_response(input, limits);
  (void)predictfun::codec::decode_market_response(input, limits);
  (void)predictfun::codec::decode_categories_response(input, limits);
  (void)predictfun::codec::decode_category_response(input, limits);
  (void)predictfun::codec::decode_orderbook_response(input, 2U, limits);
  (void)predictfun::codec::decode_orderbook_payload(input, 2U, limits);
  (void)predictfun::codec::decode_timeseries_response(input, limits);
  (void)predictfun::codec::decode_latest_timeseries_response(input, limits);
  (void)predictfun::codec::decode_matches_response(input, limits);
  (void)predictfun::codec::decode_account_response(input, limits);
  (void)predictfun::codec::decode_positions_response(input, limits);
  (void)predictfun::codec::decode_orders_response(input, limits);
  (void)predictfun::codec::decode_order_response(input, limits);
  (void)predictfun::codec::decode_activity_response(input, limits);
  (void)predictfun::codec::decode_private_ws_frame(input, limits);
  (void)predictfun::codec::decode_public_ws_frame(input, resolver, limits);
  (void)predictfun::codec::decode_create_order_response(input, limits);
  (void)predictfun::codec::decode_remove_orders_response(input, limits);
  (void)predictfun::codec::decode_remove_order_hashes_response(input, limits);
}

void deterministic_adversarial_corpus() {
  const std::vector<std::string> corpus{
      {},
      "{",
      "[]",
      "null",
      "true",
      "0",
      "\0\0\0",
      R"({"success":true})",
      R"({"success":false,"data":{}})",
      R"({"success":true,"data":[]})",
      R"({"success":true,"data":{"token":"secret-marker"}})",
      std::string(1'025U, 'x'),
      std::string{"{\"success\":true,\"data\":\""} + std::string(1'000U, 'a') +
          "\"}"};
  for (const auto &input : corpus)
    decode_everything(input);
}

void deterministic_mutation_smoke() {
  DeterministicRandom random;
  constexpr std::string_view alphabet =
      "{}[],:\"\\/truefalsenull0123456789-.eEabcdefghijklmnopqrstuvwxyz";
  for (std::size_t iteration = 0U; iteration < 2'000U; ++iteration) {
    const auto length = static_cast<std::size_t>(random.next() % 1'200U);
    std::string input;
    input.reserve(length);
    for (std::size_t index = 0U; index < length; ++index) {
      input.push_back(alphabet[static_cast<std::size_t>(
          random.next() % static_cast<std::uint64_t>(alphabet.size()))]);
    }
    decode_everything(input);
  }
}

void auth_errors_never_echo_secret_input() {
  const std::string marker = "jwt-super-secret-do-not-echo";
  const auto malformed = std::string{"{\"success\":true,\"data\":{"} + marker;
  const auto token = predictfun::codec::decode_auth_token_response(malformed);
  CHECK(!token);
  if (!token)
    CHECK(token.error().message.find(marker) == std::string::npos);
  const auto message =
      predictfun::codec::decode_auth_message_response(malformed);
  CHECK(!message);
  if (!message)
    CHECK(message.error().message.find(marker) == std::string::npos);
}

void bounded_inputs_fail_before_parsing() {
  predictfun::codec::DecodeLimits limits;
  limits.max_body_bytes = 8U;
  const auto public_result =
      predictfun::codec::decode_markets_response(std::string(9U, 'x'), limits);
  CHECK(!public_result);
  CHECK(public_result.error().code == predictfun::ErrorCode::body_too_large);

  predictfun::codec::AuthCodecLimits auth_limits;
  auth_limits.max_body_bytes = 8U;
  const auto auth_result = predictfun::codec::decode_auth_token_response(
      std::string(9U, 'x'), auth_limits);
  CHECK(!auth_result);
  CHECK(auth_result.error().code == predictfun::ErrorCode::body_too_large);
}

} // namespace

int main() {
  deterministic_adversarial_corpus();
  deterministic_mutation_smoke();
  auth_errors_never_echo_secret_input();
  bounded_inputs_fail_before_parsing();
  if (failures != 0)
    std::cerr << failures << " adversarial codec assertion(s) failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
