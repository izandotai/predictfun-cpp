#pragma once

#include "predictfun/types/market.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace predictfun {

enum class BookSide { bid, ask, unknown };
enum class OrderKind { limit, market, unknown };
enum class ContractOutcome { yes, no, unknown };

struct PriceLevel {
  Price price;
  FixedDecimal quantity;
};

struct LastOrderSettled {
  std::string id;
  EnumValue<OrderKind> kind;
  MarketId market_id;
  EnumValue<ContractOutcome> outcome;
  Price price;
  EnumValue<BookSide> side;
};

struct Orderbook {
  MarketId market_id;
  std::int64_t update_timestamp_ms{0};
  std::uint8_t decimal_precision{0};
  std::vector<PriceLevel> yes_bids;
  std::vector<PriceLevel> yes_asks;
  std::optional<std::uint64_t> order_count;
  std::optional<LastOrderSettled> last_order_settled;
  std::optional<FixedDecimal> settlements_pending;
};

struct NoOrderbookView {
  MarketId market_id;
  std::int64_t update_timestamp_ms{0};
  std::vector<PriceLevel> no_bids;
  std::vector<PriceLevel> no_asks;
};

[[nodiscard]] NoOrderbookView derive_no_book(const Orderbook &yes_book);

} // namespace predictfun
