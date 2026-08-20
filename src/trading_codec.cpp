#include "predictfun/codec/trading.hpp"

#include <glaze/glaze.hpp>

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace predictfun::codec {
namespace {

constexpr auto read_options = glz::opts{.error_on_unknown_keys = false};

struct WireContractOrderWrite {
  std::string hash;
  std::string salt;
  std::string maker;
  std::string signer;
  std::string taker;
  std::string tokenId;
  std::string makerAmount;
  std::string takerAmount;
  std::uint64_t expiration{0U};
  std::string nonce;
  std::string feeRateBps;
  std::uint32_t side{0U};
  std::uint32_t signatureType{0U};
  std::string signature;
};

struct WireCreateOrderDataWrite {
  std::string pricePerShare;
  std::string strategy;
  std::optional<std::string> slippageBps;
  std::optional<bool> isFillOrKill;
  std::optional<bool> isPostOnly;
  std::optional<std::string> reservedBalancePolicy;
  std::optional<bool> isMinAmountOut;
  std::optional<std::string> selfTradePrevention;
  WireContractOrderWrite order;
};

struct WireCreateOrderWrite {
  WireCreateOrderDataWrite data;
};

struct WireRemoveIdsDataWrite {
  std::vector<std::string> ids;
};
struct WireRemoveIdsWrite {
  WireRemoveIdsDataWrite data;
};
struct WireRemoveHashesDataWrite {
  std::vector<std::string> hashes;
};
struct WireRemoveHashesWrite {
  WireRemoveHashesDataWrite data;
};

struct WireCreateReceipt {
  std::optional<std::string> code;
  std::optional<std::string> orderId;
  std::optional<std::string> orderHash;
  std::optional<std::string> removalLockedUntil;
};
struct WireCreateResponse {
  std::optional<bool> success;
  std::optional<WireCreateReceipt> data;
};
struct WireRemoveResponse {
  std::optional<bool> success;
  std::optional<std::vector<std::string>> removed;
  std::optional<std::vector<std::string>> noop;
};

Error missing(std::string field) {
  return Error{ErrorCode::missing_field, "required field is missing",
               std::move(field)};
}

Error invalid(std::string message, std::string field) {
  return Error{ErrorCode::invalid_field, std::move(message), std::move(field)};
}

template <class T>
Result<T> parse_wire(std::string_view json, const DecodeLimits &limits) {
  if (json.size() > limits.max_body_bytes)
    return Error{ErrorCode::body_too_large,
                 "response exceeds configured body limit",
                 {}};
  T value;
  const auto error = glz::read<read_options>(value, json);
  if (error)
    return Error{ErrorCode::malformed_json,
                 "Predict.fun response contains malformed JSON",
                 {}};
  return value;
}

Result<std::uint64_t> expiration(const Uint256 &value) {
  std::uint64_t parsed = 0U;
  const auto &text = value.to_string();
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    return Error{ErrorCode::numeric_overflow,
                 "expiration does not fit the API int64 field",
                 "order.expiration"};
  if (parsed > static_cast<std::uint64_t>(INT64_MAX))
    return Error{ErrorCode::numeric_overflow,
                 "expiration exceeds the API int64 range", "order.expiration"};
  return parsed;
}

std::string strategy(ExecutionStrategy value) {
  return value == ExecutionStrategy::market ? "MARKET" : "LIMIT";
}

std::string self_trade_prevention(SelfTradePrevention value) {
  switch (value) {
  case SelfTradePrevention::cancel_maker:
    return "CANCEL_MAKER";
  case SelfTradePrevention::cancel_taker:
    return "CANCEL_TAKER";
  case SelfTradePrevention::cancel_both:
    return "CANCEL_BOTH";
  }
  return {};
}

std::string reserved_balance_policy(ReservedBalancePolicy value) {
  switch (value) {
  case ReservedBalancePolicy::reject_market_order:
    return "REJECT_MARKET_ORDER";
  }
  return {};
}

Result<std::string> write_json(const auto &value) {
  auto encoded = glz::write_json(value);
  if (!encoded)
    return Error{
        ErrorCode::protocol_error, "failed to encode Predict.fun request", {}};
  return std::move(encoded.value());
}

} // namespace

Result<std::string>
encode_create_order_request(const CreateOrderRequest &request) {
  auto expires = expiration(request.order.expiration);
  if (!expires)
    return expires.error();

  const auto side = request.order.side == ContractSide::buy ? 0U : 1U;
  std::uint32_t signature_type = 0U;
  if (request.order.signature_type == SignatureType::poly_proxy)
    signature_type = 1U;
  else if (request.order.signature_type == SignatureType::poly_gnosis_safe)
    signature_type = 2U;

  WireCreateOrderDataWrite data;
  data.pricePerShare = request.price_per_share_wei.to_string();
  data.strategy = strategy(request.strategy);
  if (request.slippage_bps)
    data.slippageBps = request.slippage_bps->to_string();
  data.isFillOrKill = request.is_fill_or_kill;
  data.isPostOnly = request.is_post_only;
  if (request.reserved_balance_policy)
    data.reservedBalancePolicy =
        reserved_balance_policy(*request.reserved_balance_policy);
  data.isMinAmountOut = request.is_min_amount_out;
  if (request.self_trade_prevention)
    data.selfTradePrevention =
        self_trade_prevention(*request.self_trade_prevention);
  data.order = WireContractOrderWrite{
      request.order_hash,
      request.order.salt.to_string(),
      request.order.maker.to_string(),
      request.order.signer.to_string(),
      request.order.taker ? request.order.taker->to_string()
                          : "0x0000000000000000000000000000000000000000",
      request.order.token_id.to_string(),
      request.order.maker_amount.to_string(),
      request.order.taker_amount.to_string(),
      expires.value(),
      request.order.nonce.to_string(),
      request.order.fee_rate_bps.to_string(),
      side,
      signature_type,
      request.order.signature};
  return write_json(WireCreateOrderWrite{std::move(data)});
}

Result<std::string>
encode_remove_order_ids_request(const std::vector<std::string> &ids) {
  return write_json(WireRemoveIdsWrite{WireRemoveIdsDataWrite{ids}});
}

Result<std::string>
encode_remove_order_hashes_request(const std::vector<std::string> &hashes) {
  return write_json(WireRemoveHashesWrite{WireRemoveHashesDataWrite{hashes}});
}

Result<CreateOrderReceipt>
decode_create_order_response(std::string_view json,
                             const DecodeLimits &limits) {
  auto wire = parse_wire<WireCreateResponse>(json, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().success || !*wire.value().success)
    return invalid("Predict.fun returned an unsuccessful response", "success");
  if (!wire.value().data)
    return missing("data");
  const auto &data = *wire.value().data;
  if (!data.code)
    return missing("data.code");
  if (!data.orderId)
    return missing("data.orderId");
  if (!data.orderHash)
    return missing("data.orderHash");
  if (data.code->size() > limits.max_string_bytes ||
      data.orderId->size() > limits.max_string_bytes ||
      data.orderHash->size() > limits.max_string_bytes ||
      (data.removalLockedUntil &&
       data.removalLockedUntil->size() > limits.max_string_bytes))
    return invalid("order receipt string exceeds configured limit", "data");
  return CreateOrderReceipt{*data.code, *data.orderId, *data.orderHash,
                            data.removalLockedUntil};
}

Result<RemoveOrdersReceipt>
decode_remove_orders_response(std::string_view json,
                              const DecodeLimits &limits) {
  auto wire = parse_wire<WireRemoveResponse>(json, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().success || !*wire.value().success)
    return invalid("Predict.fun returned an unsuccessful response", "success");
  if (!wire.value().removed)
    return missing("removed");
  if (!wire.value().noop)
    return missing("noop");
  if (wire.value().removed->size() + wire.value().noop->size() > 200U)
    return Error{ErrorCode::too_many_items, "too many removal results",
                 "removed"};
  for (const auto &value : *wire.value().removed)
    if (value.size() > limits.max_string_bytes)
      return invalid("removal result exceeds configured limit", "removed");
  for (const auto &value : *wire.value().noop)
    if (value.size() > limits.max_string_bytes)
      return invalid("removal result exceeds configured limit", "noop");
  return RemoveOrdersReceipt{std::move(*wire.value().removed),
                             std::move(*wire.value().noop)};
}

Result<RemoveOrdersReceipt>
decode_remove_order_hashes_response(std::string_view json,
                                    const DecodeLimits &limits) {
  auto wire = parse_wire<WireRemoveResponse>(json, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().success && !wire.value().removed && !wire.value().noop)
    return RemoveOrdersReceipt{};
  return decode_remove_orders_response(json, limits);
}

} // namespace predictfun::codec
