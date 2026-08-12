#pragma once

#include "predictfun/types/decimal.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace predictfun {

enum class Environment { bnb_mainnet, bnb_testnet };

struct MarketId {
  std::uint64_t value{0};
  friend bool operator==(const MarketId &, const MarketId &) = default;
};

enum class TradingStatus {
  open,
  matching_not_enabled,
  cancel_only,
  closed,
  unknown
};
enum class MarketStatus {
  registered,
  open,
  resolving,
  resolved,
  removed,
  unknown
};
enum class OutcomeStatus { won, lost, voided, unknown };

template <class Enum> struct EnumValue {
  Enum value{Enum::unknown};
  std::string raw;
};

struct BestQuote {
  Price price;
  FixedDecimal size;
};

struct Outcome {
  std::string name;
  std::uint64_t index_set{0};
  std::string on_chain_id;
  std::optional<BestQuote> best_bid;
  std::optional<BestQuote> best_ask;
  std::optional<EnumValue<OutcomeStatus>> status;
};

// Present when marketVariant is CRYPTO_UP_DOWN. Prices are optional because
// Predict publishes future windows before their boundary prices are known.
struct CryptoUpDownVariantData {
  std::string type;
  std::optional<std::string> price_feed_provider;
  std::optional<std::string> price_feed_symbol;
  std::optional<std::string> price_feed_id;
  std::optional<FixedDecimal> start_price;
  std::optional<FixedDecimal> end_price;
};

struct Market {
  MarketId id;
  std::string title;
  std::string question;
  EnumValue<TradingStatus> trading_status;
  EnumValue<MarketStatus> status;
  std::uint8_t decimal_precision{0};
  bool is_neg_risk{false};
  bool is_yield_bearing{false};
  std::uint32_t fee_rate_bps{0};
  std::optional<std::string> category_slug;
  std::optional<std::string> created_at;
  std::optional<std::string> market_variant;
  std::optional<CryptoUpDownVariantData> crypto_up_down;
  std::vector<Outcome> outcomes;
};

struct MarketsPage {
  std::optional<std::string> cursor;
  std::vector<Market> markets;
};

} // namespace predictfun
