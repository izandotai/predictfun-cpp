#include "predictfun/codec/public_websocket.hpp"

#include <glaze/glaze.hpp>

#include <charconv>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace predictfun::codec {
namespace {

struct WireResponseError {
  std::optional<std::string> code;
  std::optional<std::string> message;
};

struct WireEnvelope {
  std::optional<std::string> type;
  std::optional<std::int64_t> requestId;
  std::optional<bool> success;
  std::optional<WireResponseError> error;
  std::optional<std::string> topic;
  std::optional<glz::raw_json> data;
};

struct WireOrderbookVersion {
  std::optional<std::uint64_t> version;
};

struct WireTradingStatus {
  std::optional<std::string> kind;
  std::optional<std::int64_t> tsMs;
  std::optional<std::uint64_t> marketId;
  std::optional<std::string> tradingStatus;
};

struct WireStatusOutcome {
  std::optional<std::string> label;
  std::optional<glz::raw_json> price;
  std::optional<std::uint64_t> indexSet;
  std::optional<std::string> onChainId;
};

struct WireMarketStatus {
  std::optional<std::string> kind;
  std::optional<std::int64_t> tsMs;
  std::optional<std::uint64_t> marketId;
  std::optional<std::string> status;
  std::optional<std::vector<WireStatusOutcome>> marketOutcomes;
};

struct WireMarketChanged {
  std::optional<std::string> kind;
  std::optional<std::string> patchKind;
  std::optional<std::int64_t> tsMs;
  std::optional<std::uint64_t> marketId;
};

struct WireCategoryChanged {
  std::optional<std::string> kind;
  std::optional<std::string> patchKind;
  std::optional<std::int64_t> tsMs;
  std::optional<std::uint64_t> categoryId;
  std::optional<std::string> slug;
};

constexpr auto read_options = glz::opts{.error_on_unknown_keys = false};

Error missing(std::string field) {
  return Error{ErrorCode::missing_field, "required field is missing",
               std::move(field)};
}

Error invalid(std::string message, std::string field) {
  return Error{ErrorCode::invalid_field, std::move(message), std::move(field)};
}

template <class Wire>
Result<Wire> parse_wire(std::string_view json, const DecodeLimits &limits) {
  if (json.size() > limits.max_body_bytes) {
    return Error{ErrorCode::websocket_frame_too_large,
                 "WebSocket frame exceeds configured limit", {}};
  }
  Wire wire;
  const auto error = glz::read<read_options>(wire, json);
  if (error) {
    return Error{ErrorCode::malformed_json,
                 glz::format_error(error, json), {}};
  }
  return wire;
}

std::string_view decimal_lexeme(const glz::raw_json &raw) {
  std::string_view value = raw.str;
  if (value.size() >= 2U && value.front() == '"' && value.back() == '"')
    value = value.substr(1U, value.size() - 2U);
  return value;
}

EnumValue<TradingStatus> trading_status(std::string raw) {
  auto value = TradingStatus::unknown;
  if (raw == "OPEN")
    value = TradingStatus::open;
  else if (raw == "MATCHING_NOT_ENABLED")
    value = TradingStatus::matching_not_enabled;
  else if (raw == "CANCEL_ONLY")
    value = TradingStatus::cancel_only;
  else if (raw == "CLOSED")
    value = TradingStatus::closed;
  return EnumValue<TradingStatus>{value, std::move(raw)};
}

EnumValue<MarketStatus> market_status(std::string raw) {
  auto value = MarketStatus::unknown;
  if (raw == "REGISTERED")
    value = MarketStatus::registered;
  else if (raw == "OPEN")
    value = MarketStatus::open;
  else if (raw == "RESOLVING")
    value = MarketStatus::resolving;
  else if (raw == "RESOLVED")
    value = MarketStatus::resolved;
  else if (raw == "REMOVED")
    value = MarketStatus::removed;
  return EnumValue<MarketStatus>{value, std::move(raw)};
}

Result<std::uint64_t> topic_parameter(std::string_view topic,
                                      std::string_view prefix) {
  if (!topic.starts_with(prefix))
    return invalid("topic prefix does not match payload", "topic");
  const auto suffix = topic.substr(prefix.size());
  std::uint64_t value = 0;
  const auto [end, error] =
      std::from_chars(suffix.data(), suffix.data() + suffix.size(), value);
  if (error != std::errc{} || end != suffix.data() + suffix.size() ||
      value == 0U) {
    return invalid("topic parameter must be a positive integer", "topic");
  }
  return value;
}

Result<HeartbeatMessage> decode_heartbeat(const glz::raw_json &raw) {
  std::int64_t timestamp = 0;
  const auto error = glz::read_json(timestamp, raw.str);
  if (error || timestamp <= 0)
    return invalid("heartbeat timestamp must be a positive integer", "data");
  return HeartbeatMessage{timestamp};
}

Result<OrderbookMessage>
decode_orderbook(const std::string &topic, const glz::raw_json &raw,
                 const PrecisionResolver &precision_for_market,
                 const DecodeLimits &limits) {
  auto parameter = topic_parameter(topic, "predictOrderbook/");
  if (!parameter)
    return parameter.error();
  const MarketId market_id{parameter.value()};
  const auto precision = precision_for_market(market_id);
  if (!precision) {
    return Error{ErrorCode::unsupported_precision,
                 "orderbook decimal precision is unavailable", "topic"};
  }
  auto book = decode_orderbook_payload(raw.str, *precision, limits);
  if (!book)
    return book.error();
  if (book.value().market_id != market_id) {
    return invalid("orderbook marketId does not match topic", "data.marketId");
  }
  auto version = parse_wire<WireOrderbookVersion>(raw.str, limits);
  if (!version)
    return version.error();
  if (!version.value().version)
    return missing("data.version");
  return OrderbookMessage{*version.value().version, std::move(book.value())};
}

Result<TradingStatusMessage> decode_trading_status(const std::string &topic,
                                                   const glz::raw_json &raw,
                                                   const DecodeLimits &limits) {
  auto parameter = topic_parameter(topic, "predictTradingStatus/");
  if (!parameter)
    return parameter.error();
  auto wire = parse_wire<WireTradingStatus>(raw.str, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().kind)
    return missing("data.kind");
  if (*wire.value().kind != "tradingStatusUpdate")
    return invalid("unexpected trading status kind", "data.kind");
  if (!wire.value().tsMs)
    return missing("data.tsMs");
  if (!wire.value().marketId)
    return missing("data.marketId");
  if (!wire.value().tradingStatus)
    return missing("data.tradingStatus");
  if (*wire.value().tsMs <= 0 || *wire.value().marketId != parameter.value())
    return invalid("trading status identity or timestamp is invalid", "data");
  return TradingStatusMessage{*wire.value().tsMs,
                              MarketId{*wire.value().marketId},
                              trading_status(*wire.value().tradingStatus)};
}

Result<MarketStatusMessage>
decode_market_status(const std::string &topic, const glz::raw_json &raw,
                     const PrecisionResolver &precision_for_market,
                     const DecodeLimits &limits) {
  auto parameter = topic_parameter(topic, "predictMarketStatus/");
  if (!parameter)
    return parameter.error();
  auto wire = parse_wire<WireMarketStatus>(raw.str, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().kind)
    return missing("data.kind");
  if (*wire.value().kind != "marketStatusUpdate")
    return invalid("unexpected market status kind", "data.kind");
  if (!wire.value().tsMs)
    return missing("data.tsMs");
  if (!wire.value().marketId)
    return missing("data.marketId");
  if (!wire.value().status)
    return missing("data.status");
  if (*wire.value().tsMs <= 0 || *wire.value().marketId != parameter.value())
    return invalid("market status identity or timestamp is invalid", "data");

  const MarketId market_id{*wire.value().marketId};
  MarketStatusMessage message{*wire.value().tsMs, market_id,
                              market_status(*wire.value().status), {}};
  if (!wire.value().marketOutcomes)
    return message;
  if (wire.value().marketOutcomes->size() > limits.max_outcomes_per_market) {
    return Error{ErrorCode::too_many_items,
                 "market status contains too many outcomes",
                 "data.marketOutcomes"};
  }
  const auto precision = precision_for_market(market_id);
  if (!precision)
    return Error{ErrorCode::unsupported_precision,
                 "market status decimal precision is unavailable", "topic"};
  for (const auto &outcome : *wire.value().marketOutcomes) {
    if (!outcome.label)
      return missing("data.marketOutcomes[].label");
    if (!outcome.price)
      return missing("data.marketOutcomes[].price");
    if (!outcome.indexSet)
      return missing("data.marketOutcomes[].indexSet");
    if (!outcome.onChainId)
      return missing("data.marketOutcomes[].onChainId");
    if (outcome.label->size() > limits.max_string_bytes ||
        outcome.onChainId->size() > limits.max_string_bytes) {
      return invalid("market outcome string exceeds configured limit",
                     "data.marketOutcomes[]");
    }
    auto price = Price::parse(decimal_lexeme(*outcome.price), *precision);
    if (!price)
      return price.error();
    message.outcomes.push_back(MarketStatusOutcome{
        *outcome.label, price.value(), *outcome.indexSet, *outcome.onChainId});
  }
  return message;
}

Result<MarketChangedMessage> decode_market_changed(const std::string &topic,
                                                   const glz::raw_json &raw,
                                                   const DecodeLimits &limits) {
  auto parameter = topic_parameter(topic, "predictMarketChanged/");
  if (!parameter)
    return parameter.error();
  auto wire = parse_wire<WireMarketChanged>(raw.str, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().kind || *wire.value().kind != "marketChanged")
    return invalid("unexpected market changed kind", "data.kind");
  if (!wire.value().tsMs)
    return missing("data.tsMs");
  if (!wire.value().marketId)
    return missing("data.marketId");
  if (*wire.value().tsMs <= 0 || *wire.value().marketId != parameter.value())
    return invalid("market change identity or timestamp is invalid", "data");
  return MarketChangedMessage{*wire.value().tsMs,
                              MarketId{*wire.value().marketId},
                              wire.value().patchKind};
}

Result<CategoryChangedMessage>
decode_category_changed(const std::string &topic, const glz::raw_json &raw,
                        const DecodeLimits &limits) {
  auto parameter = topic_parameter(topic, "predictCategoryChanged/");
  if (!parameter)
    return parameter.error();
  auto wire = parse_wire<WireCategoryChanged>(raw.str, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().kind || *wire.value().kind != "categoryChanged")
    return invalid("unexpected category changed kind", "data.kind");
  if (!wire.value().tsMs)
    return missing("data.tsMs");
  if (!wire.value().categoryId)
    return missing("data.categoryId");
  if (!wire.value().slug)
    return missing("data.slug");
  if (*wire.value().tsMs <= 0 ||
      *wire.value().categoryId != parameter.value())
    return invalid("category change identity or timestamp is invalid", "data");
  if (wire.value().slug->size() > limits.max_string_bytes)
    return invalid("category slug exceeds configured limit", "data.slug");
  return CategoryChangedMessage{*wire.value().tsMs,
                                *wire.value().categoryId,
                                *wire.value().slug,
                                wire.value().patchKind};
}

Result<std::string> encode_topic_request(std::string_view method,
                                         std::uint64_t request_id,
                                         const PublicTopic &topic) {
  auto name = public_topic_name(topic);
  if (!name)
    return name.error();
  if (request_id >
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return invalid("request id exceeds signed 64-bit range", "requestId");
  }
  return std::format(R"({{"method":"{}","requestId":{},"params":["{}"]}})",
                     method, request_id, name.value());
}

} // namespace

Result<std::string> public_topic_name(const PublicTopic &topic) {
  if (topic.parameter == 0U)
    return invalid("topic parameter must be positive", "topic.parameter");
  switch (topic.kind) {
  case PublicTopicKind::orderbook:
    if (!topic.decimal_precision ||
        *topic.decimal_precision > FixedDecimal::max_scale) {
      return Error{ErrorCode::unsupported_precision,
                   "orderbook topic requires a supported decimal precision",
                   "topic.decimal_precision"};
    }
    return std::format("predictOrderbook/{}", topic.parameter);
  case PublicTopicKind::trading_status:
    return std::format("predictTradingStatus/{}", topic.parameter);
  case PublicTopicKind::market_status:
    return std::format("predictMarketStatus/{}", topic.parameter);
  case PublicTopicKind::market_changed:
    return std::format("predictMarketChanged/{}", topic.parameter);
  case PublicTopicKind::category_changed:
    return std::format("predictCategoryChanged/{}", topic.parameter);
  }
  return invalid("unsupported public topic", "topic.kind");
}

Result<std::string> encode_subscribe_request(std::uint64_t request_id,
                                             const PublicTopic &topic) {
  return encode_topic_request("subscribe", request_id, topic);
}

Result<std::string> encode_unsubscribe_request(std::uint64_t request_id,
                                               const PublicTopic &topic) {
  return encode_topic_request("unsubscribe", request_id, topic);
}

Result<std::string> encode_heartbeat_response(std::int64_t timestamp_ms) {
  if (timestamp_ms <= 0)
    return invalid("heartbeat timestamp must be positive", "data");
  return std::format(R"({{"method":"heartbeat","data":{}}})", timestamp_ms);
}

Result<PublicWsMessage>
decode_public_ws_frame(std::string_view json,
                       const PrecisionResolver &precision_for_market,
                       const DecodeLimits &limits) {
  auto envelope = parse_wire<WireEnvelope>(json, limits);
  if (!envelope)
    return envelope.error();
  const auto &wire = envelope.value();
  if (!wire.type)
    return missing("type");
  if (*wire.type == "R") {
    if (!wire.requestId)
      return missing("requestId");
    if (!wire.success)
      return missing("success");
    SubscriptionResponse response{*wire.requestId, *wire.success, std::nullopt};
    if (!response.success) {
      if (!wire.error || !wire.error->code)
        return missing("error.code");
      if (wire.error->code->size() > limits.max_string_bytes ||
          (wire.error->message &&
           wire.error->message->size() > limits.max_string_bytes)) {
        return invalid("subscription error exceeds configured limit", "error");
      }
      response.error = SubscriptionError{*wire.error->code,
                                         wire.error->message.value_or("")};
    }
    return PublicWsMessage{std::move(response)};
  }
  if (*wire.type != "M")
    return invalid("unknown WebSocket envelope type", "type");
  if (!wire.topic)
    return missing("topic");
  if (!wire.data)
    return missing("data");
  if (wire.topic->size() > limits.max_string_bytes)
    return invalid("topic exceeds configured limit", "topic");

  if (*wire.topic == "heartbeat") {
    auto message = decode_heartbeat(*wire.data);
    if (!message)
      return message.error();
    return PublicWsMessage{message.value()};
  }
  if (wire.topic->starts_with("predictOrderbook/")) {
    auto message = decode_orderbook(*wire.topic, *wire.data,
                                    precision_for_market, limits);
    if (!message)
      return message.error();
    return PublicWsMessage{std::move(message.value())};
  }
  if (wire.topic->starts_with("predictTradingStatus/")) {
    auto message = decode_trading_status(*wire.topic, *wire.data, limits);
    if (!message)
      return message.error();
    return PublicWsMessage{std::move(message.value())};
  }
  if (wire.topic->starts_with("predictMarketStatus/")) {
    auto message = decode_market_status(*wire.topic, *wire.data,
                                        precision_for_market, limits);
    if (!message)
      return message.error();
    return PublicWsMessage{std::move(message.value())};
  }
  if (wire.topic->starts_with("predictMarketChanged/")) {
    auto message = decode_market_changed(*wire.topic, *wire.data, limits);
    if (!message)
      return message.error();
    return PublicWsMessage{std::move(message.value())};
  }
  if (wire.topic->starts_with("predictCategoryChanged/")) {
    auto message = decode_category_changed(*wire.topic, *wire.data, limits);
    if (!message)
      return message.error();
    return PublicWsMessage{std::move(message.value())};
  }
  return PublicWsMessage{UnknownPublicMessage{*wire.topic}};
}

} // namespace predictfun::codec
