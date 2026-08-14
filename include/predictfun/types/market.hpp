#pragma once

#include "predictfun/types/decimal.hpp"
#include "predictfun/types/exact_number.hpp"

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
  price_proposed,
  price_disputed,
  paused,
  unpaused,
  // Retained for older API payloads observed before the documented lifecycle
  // enum was expanded.
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
  // Sport-specific schemas evolve independently of the trading API. Preserve
  // their bounded JSON without forcing SDK consumers to upgrade for every new
  // league or provider payload.
  std::optional<std::string> team_json;
  std::optional<std::string> variant_data_json;
  std::optional<std::string> variant_details_json;
};

struct RewardPeriod {
  std::int32_t hourly_rate{0};
  std::string starts_at;
  std::string ends_at;
};

struct MarketRewards {
  std::optional<RewardPeriod> current;
  std::vector<RewardPeriod> schedule;
};

struct MarketStatistics {
  ExactDecimal total_liquidity_usd;
  ExactDecimal volume_total_usd;
  ExactDecimal volume_24h_usd;
  // Added to the official schema after the statistics endpoint first shipped.
  // Optional keeps historical/testnet payloads decodable.
  std::optional<ExactDecimal> liquidity_3c_ask_usd;
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
  // Chain identifiers are optional because future/unregistered market windows
  // may be published before their contracts are finalized.
  std::optional<std::string> condition_id;
  std::optional<std::uint64_t> question_index;
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

  // Extended fields documented by the current Predict API. They remain
  // optional because future windows and older testnet payloads can be sparse.
  std::optional<std::string> image_url;
  std::optional<std::string> description;
  std::optional<bool> is_visible;
  std::optional<Outcome> resolution;
  std::optional<std::string> oracle_question_id;
  std::optional<std::string> resolver_address;
  std::optional<ExactDecimal> spread_threshold;
  std::optional<ExactDecimal> share_threshold;
  std::optional<bool> is_boosted;
  std::optional<std::string> boost_starts_at;
  std::optional<std::string> boost_ends_at;
  std::vector<std::string> polymarket_condition_ids;
  std::optional<std::string> kalshi_market_ticker;
  std::optional<std::string> market_type;
  std::optional<MarketRewards> rewards;
  std::optional<MarketStatistics> stats;
  std::optional<std::string> team_json;
  std::optional<std::string> variant_details_json;
};

struct MarketsPage {
  std::optional<std::string> cursor;
  std::vector<Market> markets;
};

} // namespace predictfun
