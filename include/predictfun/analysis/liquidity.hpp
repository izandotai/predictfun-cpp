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

// Exact, read-only execution estimate for selling a fixed number of outcome
// shares into one outcome's bid ladder. Share and collateral fields use the
// protocol's 18-decimal integer representation.
struct MarketSellQuote {
  Uint256 requested_shares_wei;
  Uint256 sold_shares_wei;
  Uint256 unsold_shares_wei;
  Uint256 returned_value_wei;
  Uint256 vwap_price_wei;
  Uint256 worst_price_wei;
  std::size_t levels_consumed{0};
  bool complete{false};
};

// Immediate public-book round trip: spend collateral across asks, then sell
// the resulting shares across bids from the same snapshot. The loss excludes
// venue fees and therefore isolates spread plus visible depth impact.
struct ImmediateRoundTripQuote {
  MarketBuyQuote buy;
  MarketSellQuote sell;
  Uint256 recovered_value_wei;
  Uint256 book_loss_value_wei;
  bool complete{false};
};

// The ask ladder must be sorted from best (lowest) to worst (highest).
// A valid but insufficient ladder returns complete=false rather than an error.
[[nodiscard]] Result<MarketBuyQuote>
quote_market_buy_value(const Uint256 &value_wei,
                       const std::vector<PriceLevel> &asks);

// The bid ladder must be sorted from best (highest) to worst (lowest).
// A valid but insufficient ladder returns complete=false rather than an error.
[[nodiscard]] Result<MarketSellQuote>
quote_market_sell_shares(const Uint256 &shares_wei,
                         const std::vector<PriceLevel> &bids);

[[nodiscard]] Result<ImmediateRoundTripQuote>
quote_immediate_round_trip(const Uint256 &value_wei,
                           const std::vector<PriceLevel> &asks,
                           const std::vector<PriceLevel> &bids);

} // namespace predictfun::analysis
