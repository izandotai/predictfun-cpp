#pragma once

#include "predictfun/types/exact_number.hpp"
#include "predictfun/types/orderbook.hpp"

#include <cstddef>
#include <vector>

namespace predictfun::analysis {

// Exact, read-only execution estimate for spending a fixed amount of
// collateral against one outcome's ask ladder. All monetary fields use the
// protocol's 18-decimal integer representation.
struct MarketBuyQuote {
  Uint256 requested_value_wei;
  Uint256 spent_value_wei;
  Uint256 unspent_value_wei;
  Uint256 shares_wei;
  Uint256 vwap_price_wei;
  Uint256 worst_price_wei;
  std::size_t levels_consumed{0};
  bool complete{false};
};

// The ask ladder must be sorted from best (lowest) to worst (highest).
// A valid but insufficient ladder returns complete=false rather than an error.
[[nodiscard]] Result<MarketBuyQuote>
quote_market_buy_value(const Uint256 &value_wei,
                       const std::vector<PriceLevel> &asks);

} // namespace predictfun::analysis
