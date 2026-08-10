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
enum class MarketStatus { registered, resolving, resolved, removed, unknown };
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
  std::vector<Outcome> outcomes;
};

struct MarketsPage {
  std::optional<std::string> cursor;
  std::vector<Market> markets;
};

} // namespace predictfun
