#pragma once

#include "predictfun/types/exact_number.hpp"
#include "predictfun/types/market.hpp"
#include "predictfun/types/private_rest.hpp"
#include "predictfun/types/websocket.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace predictfun {

enum class WalletEventType {
  order_accepted,
  order_not_accepted,
  order_expired,
  order_cancelled,
  order_transaction_submitted,
  order_transaction_success,
  order_transaction_failed,
  unknown
};

enum class WalletOutcome { yes, no, unknown };
enum class QuoteType { bid, ask, unknown };
enum class SettlementKind { sale, purchase, unknown };
enum class WalletFeeType { collateral, shares, unknown };

struct WalletEventDetails {
  MarketId market_id;
  std::uint64_t outcome_index{0};
  std::string market_question;
  EnumValue<WalletOutcome> outcome;
  EnumValue<QuoteType> quote_type;
  ExactDecimal quantity;
  ExactDecimal quantity_filled;
  ExactDecimal price;
  ExactDecimal value;
  ExactDecimal value_filled;
  EnumValue<OrderStrategy> strategy_type;
  std::string category_slug;
};

struct WalletFill {
  Uint256 executed_price_wei;
  Uint256 executed_size_wei;
  Uint256 executed_value_wei;
};

struct WalletFee {
  Uint256 amount_wei;
  EnumValue<WalletFeeType> type;
};

struct WalletEvent {
  EnumValue<WalletEventType> type;
  std::string order_id;
  std::string order_hash;
  EvmAddress wallet_address;
  std::int64_t timestamp_ms{0};
  WalletEventDetails details;
  std::optional<std::string> reason;
  std::optional<EnumValue<SettlementKind>> kind;
  std::optional<std::string> settlement_id;
  std::optional<WalletFill> fill;
  std::optional<bool> is_maker;
  std::optional<WalletFee> fee;
};

using PrivateWsMessage =
    std::variant<SubscriptionResponse, HeartbeatMessage, WalletEvent>;

enum class PrivateWsState {
  stopped,
  connecting,
  subscribing,
  reconciliation_required,
  live,
  degraded,
  reconnect_wait
};

struct PrivateWsStateEvent {
  PrivateWsState state{PrivateWsState::stopped};
  std::string reason;
  std::uint64_t generation{0};
};

struct PrivateWsDataEvent {
  WalletEvent event;
  // False until the host has reconciled REST state for this generation.
  bool reconciled{false};
  std::uint64_t generation{0};
};

using PrivateWsEvent = std::variant<PrivateWsStateEvent, PrivateWsDataEvent>;

} // namespace predictfun
