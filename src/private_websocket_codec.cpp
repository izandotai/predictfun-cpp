#include "predictfun/codec/private_websocket.hpp"

#include <glaze/glaze.hpp>

#include <charconv>
#include <cctype>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace predictfun::codec {
namespace {

struct WireResponseError {
  std::optional<std::string> code;
};

struct WireEnvelope {
  std::optional<std::string> type;
  std::optional<std::int64_t> requestId;
  std::optional<bool> success;
  std::optional<WireResponseError> error;
  std::optional<std::string> topic;
  std::optional<glz::raw_json> data;
};

struct WireDetails {
  std::optional<std::uint64_t> marketId;
  std::optional<std::uint64_t> outcomeIndex;
  std::optional<std::string> marketQuestion;
  std::optional<std::string> outcome;
  std::optional<std::string> quoteType;
  std::optional<glz::raw_json> quantity;
  std::optional<glz::raw_json> quantityFilled;
  std::optional<glz::raw_json> price;
  std::optional<glz::raw_json> value;
  std::optional<glz::raw_json> valueFilled;
  std::optional<std::string> strategyType;
  std::optional<std::string> categorySlug;
};

struct WireFill {
  std::optional<glz::raw_json> executedPriceWei;
  std::optional<glz::raw_json> executedSizeWei;
  std::optional<glz::raw_json> executedValueWei;
};

struct WireFee {
  std::optional<glz::raw_json> amountWei;
  std::optional<std::string> type;
};

struct WireWalletEvent {
  std::optional<std::string> type;
  std::optional<glz::raw_json> orderId;
  std::optional<std::string> orderHash;
  std::optional<std::string> walletAddress;
  std::optional<glz::raw_json> timestamp;
  std::optional<WireDetails> details;
  std::optional<std::string> reason;
  std::optional<std::string> kind;
  std::optional<glz::raw_json> settlementId;
  std::optional<WireFill> fill;
  std::optional<bool> isMaker;
  std::optional<WireFee> fee;
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
  if (glz::read<read_options>(wire, json)) {
    // Do not format the input: private frames may contain a JWT in the topic.
    return Error{ErrorCode::malformed_json,
                 "malformed private WebSocket frame", {}};
  }
  return wire;
}

std::string_view scalar_lexeme(const glz::raw_json &raw) {
  std::string_view value = raw.str;
  if (value.size() >= 2U && value.front() == '"' && value.back() == '"')
    value = value.substr(1U, value.size() - 2U);
  return value;
}

Result<std::string> scalar_string(const glz::raw_json &raw,
                                  std::string field,
                                  const DecodeLimits &limits) {
  const auto value = scalar_lexeme(raw);
  if (value.empty() || value.size() > limits.max_string_bytes)
    return invalid("scalar string is empty or exceeds configured limit",
                   std::move(field));
  return std::string{value};
}

Result<std::int64_t> positive_int64(const glz::raw_json &raw,
                                    std::string field) {
  const auto lexeme = scalar_lexeme(raw);
  std::int64_t value = 0;
  const auto [end, error] =
      std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), value);
  if (error != std::errc{} || end != lexeme.data() + lexeme.size() ||
      value <= 0) {
    return invalid("timestamp must be a positive signed 64-bit integer",
                   std::move(field));
  }
  return value;
}

Result<ExactDecimal> decimal(const std::optional<glz::raw_json> &raw,
                             std::string field) {
  if (!raw)
    return missing(std::move(field));
  auto value = ExactDecimal::parse(scalar_lexeme(*raw));
  if (!value) {
    auto error = value.error();
    error.field = std::move(field);
    return error;
  }
  return value.value();
}

Result<Uint256> uint256(const std::optional<glz::raw_json> &raw,
                        std::string field) {
  if (!raw)
    return missing(std::move(field));
  auto value = Uint256::parse(scalar_lexeme(*raw));
  if (!value) {
    auto error = value.error();
    error.field = std::move(field);
    return error;
  }
  return value.value();
}

template <class Enum>
EnumValue<Enum> enumeration(Enum value, std::string raw) {
  return EnumValue<Enum>{value, std::move(raw)};
}

EnumValue<WalletEventType> event_type(std::string raw) {
  auto value = WalletEventType::unknown;
  if (raw == "orderAccepted") value = WalletEventType::order_accepted;
  else if (raw == "orderNotAccepted") value = WalletEventType::order_not_accepted;
  else if (raw == "orderExpired") value = WalletEventType::order_expired;
  else if (raw == "orderCancelled") value = WalletEventType::order_cancelled;
  else if (raw == "orderTransactionSubmitted") value = WalletEventType::order_transaction_submitted;
  else if (raw == "orderTransactionSuccess") value = WalletEventType::order_transaction_success;
  else if (raw == "orderTransactionFailed") value = WalletEventType::order_transaction_failed;
  return enumeration(value, std::move(raw));
}

EnumValue<WalletOutcome> outcome(std::string raw) {
  auto value = WalletOutcome::unknown;
  if (raw == "YES") value = WalletOutcome::yes;
  else if (raw == "NO") value = WalletOutcome::no;
  return enumeration(value, std::move(raw));
}

EnumValue<QuoteType> quote_type(std::string raw) {
  auto value = QuoteType::unknown;
  if (raw == "BID") value = QuoteType::bid;
  else if (raw == "ASK") value = QuoteType::ask;
  return enumeration(value, std::move(raw));
}

EnumValue<OrderStrategy> strategy(std::string raw) {
  auto value = OrderStrategy::unknown;
  if (raw == "MARKET") value = OrderStrategy::market;
  else if (raw == "LIMIT") value = OrderStrategy::limit;
  return enumeration(value, std::move(raw));
}

EnumValue<SettlementKind> settlement_kind(std::string raw) {
  auto value = SettlementKind::unknown;
  if (raw == "SALE") value = SettlementKind::sale;
  else if (raw == "PURCHASE") value = SettlementKind::purchase;
  return enumeration(value, std::move(raw));
}

EnumValue<WalletFeeType> fee_type(std::string raw) {
  auto value = WalletFeeType::unknown;
  if (raw == "COLLATERAL") value = WalletFeeType::collateral;
  else if (raw == "SHARES") value = WalletFeeType::shares;
  return enumeration(value, std::move(raw));
}

Result<WalletEventDetails> details(const WireDetails &wire,
                                   const DecodeLimits &limits) {
  if (!wire.marketId) return missing("data.details.marketId");
  if (!wire.outcomeIndex) return missing("data.details.outcomeIndex");
  if (!wire.marketQuestion) return missing("data.details.marketQuestion");
  if (!wire.outcome) return missing("data.details.outcome");
  if (!wire.quoteType) return missing("data.details.quoteType");
  if (!wire.strategyType) return missing("data.details.strategyType");
  if (!wire.categorySlug) return missing("data.details.categorySlug");
  if (wire.marketQuestion->size() > limits.max_string_bytes ||
      wire.categorySlug->size() > limits.max_string_bytes)
    return invalid("wallet event detail string exceeds configured limit",
                   "data.details");
  auto quantity = decimal(wire.quantity, "data.details.quantity");
  auto quantity_filled =
      decimal(wire.quantityFilled, "data.details.quantityFilled");
  auto price = decimal(wire.price, "data.details.price");
  auto value = decimal(wire.value, "data.details.value");
  auto value_filled = decimal(wire.valueFilled, "data.details.valueFilled");
  if (!quantity) return quantity.error();
  if (!quantity_filled) return quantity_filled.error();
  if (!price) return price.error();
  if (!value) return value.error();
  if (!value_filled) return value_filled.error();
  return WalletEventDetails{
      MarketId{*wire.marketId}, *wire.outcomeIndex, *wire.marketQuestion,
      outcome(*wire.outcome), quote_type(*wire.quoteType),
      std::move(quantity.value()), std::move(quantity_filled.value()),
      std::move(price.value()), std::move(value.value()),
      std::move(value_filled.value()), strategy(*wire.strategyType),
      *wire.categorySlug};
}

Result<WalletFill> fill(const WireFill &wire) {
  auto price = uint256(wire.executedPriceWei, "data.fill.executedPriceWei");
  auto size = uint256(wire.executedSizeWei, "data.fill.executedSizeWei");
  auto value = uint256(wire.executedValueWei, "data.fill.executedValueWei");
  if (!price) return price.error();
  if (!size) return size.error();
  if (!value) return value.error();
  return WalletFill{std::move(price.value()), std::move(size.value()),
                    std::move(value.value())};
}

Result<WalletEvent> decode_event(const glz::raw_json &raw,
                                 const DecodeLimits &limits) {
  auto parsed = parse_wire<WireWalletEvent>(raw.str, limits);
  if (!parsed) return parsed.error();
  auto &wire = parsed.value();
  if (!wire.type) return missing("data.type");
  if (!wire.orderId) return missing("data.orderId");
  if (!wire.orderHash) return missing("data.orderHash");
  if (!wire.walletAddress) return missing("data.walletAddress");
  if (!wire.timestamp) return missing("data.timestamp");
  if (!wire.details) return missing("data.details");
  if (wire.type->size() > limits.max_string_bytes ||
      wire.orderHash->size() > limits.max_string_bytes)
    return invalid("wallet event string exceeds configured limit", "data");
  auto id = scalar_string(*wire.orderId, "data.orderId", limits);
  auto address = EvmAddress::parse(*wire.walletAddress);
  auto timestamp = positive_int64(*wire.timestamp, "data.timestamp");
  auto event_details = details(*wire.details, limits);
  if (!id) return id.error();
  if (!address) return address.error();
  if (!timestamp) return timestamp.error();
  if (!event_details) return event_details.error();

  WalletEvent event{event_type(*wire.type), std::move(id.value()),
                    *wire.orderHash, std::move(address.value()),
                    timestamp.value(), std::move(event_details.value()),
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, std::nullopt};
  event.reason = wire.reason;
  if (wire.kind) event.kind = settlement_kind(*wire.kind);
  if (wire.settlementId) {
    auto settlement =
        scalar_string(*wire.settlementId, "data.settlementId", limits);
    if (!settlement) return settlement.error();
    event.settlement_id = std::move(settlement.value());
  }
  if (wire.fill) {
    auto decoded = fill(*wire.fill);
    if (!decoded) return decoded.error();
    event.fill = std::move(decoded.value());
  }
  event.is_maker = wire.isMaker;
  if (wire.fee) {
    if (!wire.fee->type) return missing("data.fee.type");
    auto amount = uint256(wire.fee->amountWei, "data.fee.amountWei");
    if (!amount) return amount.error();
    event.fee = WalletFee{std::move(amount.value()),
                          fee_type(*wire.fee->type)};
  }

  const auto type = event.type.value;
  if (type == WalletEventType::order_not_accepted && !event.reason)
    return missing("data.reason");
  const bool transaction =
      type == WalletEventType::order_transaction_submitted ||
      type == WalletEventType::order_transaction_success ||
      type == WalletEventType::order_transaction_failed;
  if (transaction && (!event.settlement_id || !event.fill))
    return missing("data.settlementId/fill");
  if (type == WalletEventType::order_transaction_submitted && !event.kind)
    return missing("data.kind");
  return event;
}

} // namespace

Result<std::string>
encode_wallet_subscribe_request(std::uint64_t request_id,
                                std::string_view jwt) {
  if (request_id == 0U ||
      request_id >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    return invalid("request id must fit a positive signed 64-bit integer",
                   "requestId");
  if (jwt.empty() || jwt.size() > 16U * 1024U)
    return invalid("JWT is empty or exceeds configured limit", "jwt");
  for (const unsigned char byte : jwt) {
    if (!(std::isalnum(byte) != 0 || byte == '-' || byte == '_' ||
          byte == '.'))
      return invalid("JWT contains a character unsafe for a topic", "jwt");
  }
  return std::format(
      R"({{"method":"subscribe","requestId":{},"params":["predictWalletEvents/{}"]}})",
      request_id, jwt);
}

Result<PrivateWsMessage>
decode_private_ws_frame(std::string_view json, const DecodeLimits &limits) {
  auto envelope = parse_wire<WireEnvelope>(json, limits);
  if (!envelope) return envelope.error();
  auto &wire = envelope.value();
  if (!wire.type) return missing("type");
  if (*wire.type == "R") {
    if (!wire.requestId) return missing("requestId");
    if (!wire.success) return missing("success");
    SubscriptionResponse response{*wire.requestId, *wire.success, std::nullopt};
    if (!response.success) {
      if (!wire.error || !wire.error->code) return missing("error.code");
      // Deliberately discard the server message because it may echo the
      // credential-bearing subscription topic.
      response.error = SubscriptionError{*wire.error->code, {}};
    }
    return PrivateWsMessage{std::move(response)};
  }
  if (*wire.type != "M")
    return invalid("unknown WebSocket envelope type", "type");
  if (!wire.topic) return missing("topic");
  if (!wire.data) return missing("data");
  if (*wire.topic == "heartbeat") {
    std::int64_t timestamp = 0;
    if (glz::read_json(timestamp, wire.data->str) || timestamp <= 0)
      return invalid("heartbeat timestamp must be positive", "data");
    return PrivateWsMessage{HeartbeatMessage{timestamp}};
  }
  if (!wire.topic->starts_with("predictWalletEvents/"))
    return invalid("unexpected private WebSocket topic", "topic");
  auto event = decode_event(*wire.data, limits);
  if (!event) return event.error();
  return PrivateWsMessage{std::move(event.value())};
}

} // namespace predictfun::codec
