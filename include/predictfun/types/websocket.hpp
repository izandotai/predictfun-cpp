#pragma once

#include "predictfun/types/market.hpp"
#include "predictfun/types/orderbook.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace predictfun {

enum class PublicTopicKind {
  orderbook,
  trading_status,
  market_status,
  market_changed,
  category_changed
};

struct PublicTopic {
  PublicTopicKind kind{PublicTopicKind::orderbook};
  std::uint64_t parameter{0};
  std::optional<std::uint8_t> decimal_precision;

  friend bool operator==(const PublicTopic &, const PublicTopic &) = default;
};

struct SubscriptionError {
  std::string code;
  std::string message;
};

struct SubscriptionResponse {
  std::int64_t request_id{0};
  bool success{false};
  std::optional<SubscriptionError> error;
};

struct HeartbeatMessage {
  std::int64_t timestamp_ms{0};
};

struct OrderbookMessage {
  // Official documentation describes this as a payload/schema version. It is
  // deliberately not exposed as a market-data sequence number.
  std::uint64_t schema_version{0};
  Orderbook book;
};

struct TradingStatusMessage {
  std::int64_t timestamp_ms{0};
  MarketId market_id;
  EnumValue<TradingStatus> trading_status;
};

struct MarketStatusOutcome {
  std::string label;
  Price price;
  std::uint64_t index_set{0};
  std::string on_chain_id;
};

struct MarketStatusMessage {
  std::int64_t timestamp_ms{0};
  MarketId market_id;
  EnumValue<MarketStatus> status;
  std::vector<MarketStatusOutcome> outcomes;
};

struct MarketChangedMessage {
  std::int64_t timestamp_ms{0};
  MarketId market_id;
  std::optional<std::string> patch_kind;
};

struct CategoryChangedMessage {
  std::int64_t timestamp_ms{0};
  std::uint64_t category_id{0};
  std::string slug;
  std::optional<std::string> patch_kind;
};

struct UnknownPublicMessage {
  std::string topic;
};

using PublicWsMessage =
    std::variant<SubscriptionResponse, HeartbeatMessage, OrderbookMessage,
                 TradingStatusMessage, MarketStatusMessage,
                 MarketChangedMessage, CategoryChangedMessage,
                 UnknownPublicMessage>;

enum class PublicWsState {
  stopped,
  connecting,
  subscribing,
  synchronizing,
  live,
  degraded,
  reconnect_wait
};

struct PublicWsStateEvent {
  PublicWsState state{PublicWsState::stopped};
  std::string reason;
  std::uint64_t generation{0};
};

struct PublicWsDataEvent {
  PublicTopic topic;
  PublicWsMessage message;
  bool fresh{false};
  std::uint64_t generation{0};
};

using PublicWsEvent = std::variant<PublicWsStateEvent, PublicWsDataEvent>;

} // namespace predictfun
