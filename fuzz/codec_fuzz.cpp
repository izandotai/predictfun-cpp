#include "predictfun/codec/auth.hpp"
#include "predictfun/codec/private_rest.hpp"
#include "predictfun/codec/private_websocket.hpp"
#include "predictfun/codec/public_rest.hpp"
#include "predictfun/codec/public_websocket.hpp"
#include "predictfun/codec/trading.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  const auto input =
      std::string_view{reinterpret_cast<const char *>(data), size};
  predictfun::codec::DecodeLimits limits;
  limits.max_body_bytes = 64U * 1024U;
  limits.max_markets = 128U;
  limits.max_categories = 128U;
  limits.max_outcomes_per_market = 64U;
  limits.max_book_levels_per_side = 512U;
  limits.max_timeseries_points = 512U;
  limits.max_matches = 128U;
  limits.max_makers_per_match = 128U;
  limits.max_string_bytes = 4U * 1024U;
  const auto resolver =
      [](predictfun::MarketId) -> std::optional<std::uint8_t> { return 2U; };

  (void)predictfun::codec::decode_markets_response(input, limits);
  (void)predictfun::codec::decode_market_response(input, limits);
  (void)predictfun::codec::decode_orderbook_response(input, 2U, limits);
  (void)predictfun::codec::decode_timeseries_response(input, limits);
  (void)predictfun::codec::decode_matches_response(input, limits);
  (void)predictfun::codec::decode_account_response(input, limits);
  (void)predictfun::codec::decode_positions_response(input, limits);
  (void)predictfun::codec::decode_orders_response(input, limits);
  (void)predictfun::codec::decode_activity_response(input, limits);
  (void)predictfun::codec::decode_private_ws_frame(input, limits);
  (void)predictfun::codec::decode_public_ws_frame(input, resolver, limits);
  (void)predictfun::codec::decode_create_order_response(input, limits);
  (void)predictfun::codec::decode_remove_orders_response(input, limits);
  predictfun::codec::AuthCodecLimits auth_limits;
  auth_limits.max_body_bytes = limits.max_body_bytes;
  auth_limits.max_message_bytes = limits.max_string_bytes;
  auth_limits.max_signature_bytes = limits.max_string_bytes;
  auth_limits.max_token_bytes = limits.max_string_bytes;
  (void)predictfun::codec::decode_auth_token_response(input, auth_limits);
  (void)predictfun::codec::decode_auth_message_response(input, auth_limits);
  return 0;
}
