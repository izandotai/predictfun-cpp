#include "predictfun/codec/private_rest.hpp"

#include <glaze/glaze.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace predictfun::codec {
namespace {

constexpr auto read_options = glz::opts{.error_on_unknown_keys = false};

struct WireReferral {
  std::optional<std::string> code;
  std::optional<std::string> status;
};
struct WirePoints {
  std::optional<glz::raw_json> total;
  std::optional<std::uint64_t> rank;
};
struct WireAccount {
  std::optional<std::string> name;
  std::optional<std::string> address;
  std::optional<std::string> imageUrl;
  std::optional<WireReferral> referral;
  std::optional<WirePoints> points;
};
struct WireAccountResponse {
  std::optional<bool> success;
  std::optional<WireAccount> data;
};

struct WireReferralRequestData {
  std::string referralCode;
};
struct WireReferralRequest {
  WireReferralRequestData data;
};
struct WireReferralResponse {
  std::optional<bool> success;
};

struct WireOutcome {
  std::optional<std::string> name;
  std::optional<std::uint64_t> indexSet;
  std::optional<glz::raw_json> onChainId;
};
struct WirePosition {
  std::optional<std::string> id;
  std::optional<glz::raw_json> market;
  std::optional<WireOutcome> outcome;
  std::optional<glz::raw_json> amount;
  std::optional<glz::raw_json> valueUsd;
  std::optional<glz::raw_json> averageBuyPriceUsd;
  std::optional<glz::raw_json> pnlUsd;
};
struct WirePositionsResponse {
  std::optional<bool> success;
  std::optional<std::string> cursor;
  std::optional<std::vector<WirePosition>> data;
};

struct WireContractOrder {
  std::optional<std::string> hash;
  std::optional<glz::raw_json> salt;
  std::optional<std::string> maker;
  std::optional<std::string> signer;
  std::optional<std::string> taker;
  std::optional<glz::raw_json> tokenId;
  std::optional<glz::raw_json> makerAmount;
  std::optional<glz::raw_json> takerAmount;
  std::optional<glz::raw_json> expiration;
  std::optional<glz::raw_json> nonce;
  std::optional<glz::raw_json> feeRateBps;
  std::optional<std::uint32_t> side;
  std::optional<std::uint32_t> signatureType;
  std::optional<std::string> signature;
};
struct WireOrderRecord {
  std::optional<WireContractOrder> order;
  std::optional<std::string> id;
  std::optional<std::uint64_t> marketId;
  std::optional<std::string> currency;
  std::optional<glz::raw_json> amount;
  std::optional<glz::raw_json> amountFilled;
  std::optional<bool> isNegRisk;
  std::optional<bool> isYieldBearing;
  std::optional<std::string> strategy;
  std::optional<std::string> status;
  std::optional<glz::raw_json> rewardEarningRate;
};
struct WireOrdersResponse {
  std::optional<bool> success;
  std::optional<std::string> cursor;
  std::optional<std::vector<WireOrderRecord>> data;
};
struct WireOrderResponse {
  std::optional<bool> success;
  std::optional<WireOrderRecord> data;
};

struct WireFee {
  std::optional<glz::raw_json> amount;
  std::optional<std::string> type;
};
struct WireActivityOrder {
  std::optional<std::string> quoteType;
  std::optional<glz::raw_json> amount;
  std::optional<glz::raw_json> price;
  std::optional<WireFee> fee;
};
struct WireActivity {
  std::optional<std::string> name;
  std::optional<std::string> createdAt;
  std::optional<std::string> transactionHash;
  std::optional<glz::raw_json> amountFilled;
  std::optional<glz::raw_json> priceExecuted;
  std::optional<WireActivityOrder> order;
  std::optional<glz::raw_json> market;
  std::optional<WireOutcome> outcome;
};
struct WireActivityResponse {
  std::optional<bool> success;
  std::optional<std::string> cursor;
  std::optional<std::vector<WireActivity>> data;
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

bool bounded(const std::string &value, const DecodeLimits &limits) {
  return value.size() <= limits.max_string_bytes;
}

Result<std::string> raw_scalar(const glz::raw_json &raw, std::string field) {
  const auto text = std::string_view{raw.str};
  if (text.size() >= 2U && text.front() == '"' && text.back() == '"') {
    std::string value;
    if (glz::read_json(value, text))
      return invalid("invalid quoted numeric value", std::move(field));
    return value;
  }
  return std::string{text};
}

Result<ExactDecimal> exact(const glz::raw_json &raw, std::string field) {
  auto text = raw_scalar(raw, field);
  if (!text)
    return text.error();
  auto value = ExactDecimal::parse(text.value());
  if (!value) {
    auto error = value.error();
    error.field = std::move(field);
    return error;
  }
  return value.value();
}

Result<Uint256> uint256(const glz::raw_json &raw, std::string field) {
  auto text = raw_scalar(raw, field);
  if (!text)
    return text.error();
  auto value = Uint256::parse(text.value());
  if (!value) {
    auto error = value.error();
    error.field = std::move(field);
    return error;
  }
  return value.value();
}

Result<Market> market(const glz::raw_json &raw, const DecodeLimits &limits) {
  std::string envelope = R"({"success":true,"data":)";
  envelope.append(raw.str);
  envelope.push_back('}');
  return decode_market_response(envelope, limits);
}

Result<PrivateOutcome> outcome(const WireOutcome &wire, std::string prefix) {
  PrivateOutcome result;
  result.name = wire.name;
  result.index_set = wire.indexSet;
  if (wire.onChainId) {
    auto id = uint256(*wire.onChainId, prefix + ".onChainId");
    if (!id)
      return id.error();
    result.on_chain_id = std::move(id.value());
  }
  return result;
}

template <class Enum> EnumValue<Enum> enum_value(Enum value, std::string raw) {
  return EnumValue<Enum>{value, std::move(raw)};
}

EnumValue<OrderStrategy> strategy(const std::string &raw) {
  if (raw == "MARKET")
    return enum_value(OrderStrategy::market, raw);
  if (raw == "LIMIT")
    return enum_value(OrderStrategy::limit, raw);
  return enum_value(OrderStrategy::unknown, raw);
}

EnumValue<OrderStatus> status(const std::string &raw) {
  if (raw == "OPEN")
    return enum_value(OrderStatus::open, raw);
  if (raw == "MATCHED" || raw == "FILLED")
    return enum_value(OrderStatus::matched, raw);
  if (raw == "CANCELLED" || raw == "CANCELED")
    return enum_value(OrderStatus::cancelled, raw);
  if (raw == "EXPIRED")
    return enum_value(OrderStatus::expired, raw);
  if (raw == "INVALIDATED")
    return enum_value(OrderStatus::invalidated, raw);
  if (raw == "FAILED" || raw == "REJECTED")
    return enum_value(OrderStatus::failed, raw);
  return enum_value(OrderStatus::unknown, raw);
}

Result<ContractOrder> contract_order(const WireContractOrder &wire,
                                     std::string prefix) {
  if (!wire.hash)
    return missing(prefix + ".hash");
  if (!wire.salt)
    return missing(prefix + ".salt");
  if (!wire.maker)
    return missing(prefix + ".maker");
  if (!wire.signer)
    return missing(prefix + ".signer");
  if (!wire.taker)
    return missing(prefix + ".taker");
  if (!wire.tokenId)
    return missing(prefix + ".tokenId");
  if (!wire.makerAmount)
    return missing(prefix + ".makerAmount");
  if (!wire.takerAmount)
    return missing(prefix + ".takerAmount");
  if (!wire.expiration)
    return missing(prefix + ".expiration");
  if (!wire.nonce)
    return missing(prefix + ".nonce");
  if (!wire.feeRateBps)
    return missing(prefix + ".feeRateBps");
  if (!wire.side)
    return missing(prefix + ".side");
  if (!wire.signatureType)
    return missing(prefix + ".signatureType");
  if (!wire.signature)
    return missing(prefix + ".signature");

  auto salt = uint256(*wire.salt, prefix + ".salt");
  auto maker = EvmAddress::parse(*wire.maker);
  auto signer = EvmAddress::parse(*wire.signer);
  auto token = uint256(*wire.tokenId, prefix + ".tokenId");
  auto maker_amount = uint256(*wire.makerAmount, prefix + ".makerAmount");
  auto taker_amount = uint256(*wire.takerAmount, prefix + ".takerAmount");
  auto expiration = uint256(*wire.expiration, prefix + ".expiration");
  auto nonce = uint256(*wire.nonce, prefix + ".nonce");
  auto fee = uint256(*wire.feeRateBps, prefix + ".feeRateBps");
  if (!salt)
    return salt.error();
  if (!maker) {
    auto e = maker.error();
    e.field = prefix + ".maker";
    return e;
  }
  if (!signer) {
    auto e = signer.error();
    e.field = prefix + ".signer";
    return e;
  }
  if (!token)
    return token.error();
  if (!maker_amount)
    return maker_amount.error();
  if (!taker_amount)
    return taker_amount.error();
  if (!expiration)
    return expiration.error();
  if (!nonce)
    return nonce.error();
  if (!fee)
    return fee.error();

  std::optional<EvmAddress> taker;
  if (*wire.taker != "0x0000000000000000000000000000000000000000") {
    auto parsed = EvmAddress::parse(*wire.taker);
    if (!parsed) {
      auto e = parsed.error();
      e.field = prefix + ".taker";
      return e;
    }
    taker = parsed.value();
  }
  const auto side = *wire.side == 0U   ? ContractSide::buy
                    : *wire.side == 1U ? ContractSide::sell
                                       : ContractSide::unknown;
  SignatureType signature_type = SignatureType::unknown;
  if (*wire.signatureType == 0U)
    signature_type = SignatureType::eoa;
  else if (*wire.signatureType == 1U)
    signature_type = SignatureType::poly_proxy;
  else if (*wire.signatureType == 2U)
    signature_type = SignatureType::poly_gnosis_safe;

  return ContractOrder{
      *wire.hash,
      std::move(salt.value()),
      maker.value(),
      signer.value(),
      taker,
      std::move(token.value()),
      std::move(maker_amount.value()),
      std::move(taker_amount.value()),
      std::move(expiration.value()),
      std::move(nonce.value()),
      std::move(fee.value()),
      enum_value(side, std::to_string(*wire.side)),
      enum_value(signature_type, std::to_string(*wire.signatureType)),
      *wire.signature};
}

Result<OrderRecord> order_record(const WireOrderRecord &item,
                                 const std::string &prefix) {
  if (!item.order)
    return missing(prefix + ".order");
  if (!item.id)
    return missing(prefix + ".id");
  if (!item.marketId)
    return missing(prefix + ".marketId");
  if (!item.currency)
    return missing(prefix + ".currency");
  if (!item.amount)
    return missing(prefix + ".amount");
  if (!item.amountFilled)
    return missing(prefix + ".amountFilled");
  if (!item.isNegRisk)
    return missing(prefix + ".isNegRisk");
  if (!item.isYieldBearing)
    return missing(prefix + ".isYieldBearing");
  if (!item.strategy)
    return missing(prefix + ".strategy");
  if (!item.status)
    return missing(prefix + ".status");
  if (!item.rewardEarningRate)
    return missing(prefix + ".rewardEarningRate");
  auto contract = contract_order(*item.order, prefix + ".order");
  auto amount = exact(*item.amount, prefix + ".amount");
  auto filled = exact(*item.amountFilled, prefix + ".amountFilled");
  auto reward = exact(*item.rewardEarningRate, prefix + ".rewardEarningRate");
  if (!contract)
    return contract.error();
  if (!amount)
    return amount.error();
  if (!filled)
    return filled.error();
  if (!reward)
    return reward.error();
  return OrderRecord{std::move(contract.value()),
                     *item.id,
                     MarketId{*item.marketId},
                     *item.currency,
                     std::move(amount.value()),
                     std::move(filled.value()),
                     *item.isNegRisk,
                     *item.isYieldBearing,
                     strategy(*item.strategy),
                     status(*item.status),
                     std::move(reward.value())};
}

} // namespace

Result<std::string> encode_referral_request(std::string_view referral_code) {
  if (referral_code.size() != 5U ||
      !std::ranges::all_of(referral_code, [](unsigned char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= 'a' && character <= 'z');
      }))
    return Error{ErrorCode::invalid_argument,
                 "referral code must contain exactly 5 ASCII letters or digits",
                 "referral_code"};

  std::string json;
  const WireReferralRequest request{
      WireReferralRequestData{std::string{referral_code}}};
  if (const auto error = glz::write_json(request, json); error)
    return Error{ErrorCode::protocol_error,
                 "failed to encode Predict.fun referral request",
                 {}};
  return json;
}

Result<bool> decode_referral_response(std::string_view json,
                                      const DecodeLimits &limits) {
  auto wire = parse_wire<WireReferralResponse>(json, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().success)
    return missing("success");
  if (!*wire.value().success)
    return invalid("Predict.fun rejected the referral assignment", "success");
  return true;
}

Result<Account> decode_account_response(std::string_view json,
                                        const DecodeLimits &limits) {
  auto wire = parse_wire<WireAccountResponse>(json, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().success || !*wire.value().success)
    return invalid("Predict.fun returned an unsuccessful response", "success");
  if (!wire.value().data)
    return missing("data");
  const auto &data = *wire.value().data;
  if (!data.name)
    return missing("data.name");
  if (!data.address)
    return missing("data.address");
  if (!bounded(*data.name, limits) ||
      (data.imageUrl && !bounded(*data.imageUrl, limits)))
    return invalid("account string exceeds configured limit", "data");
  auto address = EvmAddress::parse(*data.address);
  if (!address) {
    auto e = address.error();
    e.field = "data.address";
    return e;
  }

  Account account{*data.name, address.value(), data.imageUrl, {}, {}};
  if (data.referral) {
    const auto has_code = data.referral->code.has_value();
    const auto has_status = data.referral->status.has_value();
    if (has_code != has_status)
      return missing(has_code ? "data.referral.status"
                              : "data.referral.code");
    // New EOAs can legitimately receive an empty referral object. Referral
    // metadata is ancillary account information, not account identity or an
    // authentication prerequisite, so preserve it only when it is complete.
    if (has_code)
      account.referral =
          ReferralInfo{*data.referral->code, *data.referral->status};
  }
  if (data.points) {
    if (!data.points->total)
      return missing("data.points.total");
    auto total = exact(*data.points->total, "data.points.total");
    if (!total)
      return total.error();
    account.points = PointsInfo{std::move(total.value()), data.points->rank};
  }
  return account;
}

Result<PositionsPage> decode_positions_response(std::string_view json,
                                                const DecodeLimits &limits) {
  auto wire = parse_wire<WirePositionsResponse>(json, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().success || !*wire.value().success)
    return invalid("Predict.fun returned an unsuccessful response", "success");
  if (!wire.value().data)
    return missing("data");
  if (wire.value().data->size() > limits.max_markets)
    return Error{ErrorCode::too_many_items, "too many positions", "data"};
  PositionsPage page{wire.value().cursor, {}};
  page.positions.reserve(wire.value().data->size());
  for (std::size_t index = 0; index < wire.value().data->size(); ++index) {
    const auto &item = (*wire.value().data)[index];
    const auto prefix = "data[" + std::to_string(index) + "]";
    if (!item.id)
      return missing(prefix + ".id");
    if (!item.market)
      return missing(prefix + ".market");
    if (!item.outcome)
      return missing(prefix + ".outcome");
    if (!item.amount)
      return missing(prefix + ".amount");
    if (!item.valueUsd)
      return missing(prefix + ".valueUsd");
    if (!item.averageBuyPriceUsd)
      return missing(prefix + ".averageBuyPriceUsd");
    if (!item.pnlUsd)
      return missing(prefix + ".pnlUsd");
    auto parsed_market = market(*item.market, limits);
    auto parsed_outcome = outcome(*item.outcome, prefix + ".outcome");
    auto amount = exact(*item.amount, prefix + ".amount");
    auto value = exact(*item.valueUsd, prefix + ".valueUsd");
    auto average =
        exact(*item.averageBuyPriceUsd, prefix + ".averageBuyPriceUsd");
    auto pnl = exact(*item.pnlUsd, prefix + ".pnlUsd");
    if (!parsed_market)
      return parsed_market.error();
    if (!parsed_outcome)
      return parsed_outcome.error();
    if (!amount)
      return amount.error();
    if (!value)
      return value.error();
    if (!average)
      return average.error();
    if (!pnl)
      return pnl.error();
    page.positions.push_back(
        Position{*item.id, std::move(parsed_market.value()),
                 std::move(parsed_outcome.value()), std::move(amount.value()),
                 std::move(value.value()), std::move(average.value()),
                 std::move(pnl.value())});
  }
  return page;
}

Result<OrdersPage> decode_orders_response(std::string_view json,
                                          const DecodeLimits &limits) {
  auto wire = parse_wire<WireOrdersResponse>(json, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().success || !*wire.value().success)
    return invalid("Predict.fun returned an unsuccessful response", "success");
  if (!wire.value().data)
    return missing("data");
  if (wire.value().data->size() > limits.max_markets)
    return Error{ErrorCode::too_many_items, "too many orders", "data"};
  OrdersPage page{wire.value().cursor, {}};
  page.orders.reserve(wire.value().data->size());
  for (std::size_t index = 0; index < wire.value().data->size(); ++index) {
    const auto &item = (*wire.value().data)[index];
    const auto prefix = "data[" + std::to_string(index) + "]";
    auto parsed = order_record(item, prefix);
    if (!parsed)
      return parsed.error();
    page.orders.push_back(std::move(parsed.value()));
  }
  return page;
}

Result<OrderRecord> decode_order_response(std::string_view json,
                                          const DecodeLimits &limits) {
  auto wire = parse_wire<WireOrderResponse>(json, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().success || !*wire.value().success)
    return invalid("Predict.fun returned an unsuccessful response", "success");
  if (!wire.value().data)
    return missing("data");
  return order_record(*wire.value().data, "data");
}

Result<ActivityPage> decode_activity_response(std::string_view json,
                                              const DecodeLimits &limits) {
  auto wire = parse_wire<WireActivityResponse>(json, limits);
  if (!wire)
    return wire.error();
  if (!wire.value().success || !*wire.value().success)
    return invalid("Predict.fun returned an unsuccessful response", "success");
  if (!wire.value().data)
    return missing("data");
  if (wire.value().data->size() > limits.max_markets)
    return Error{ErrorCode::too_many_items, "too many activity events", "data"};
  ActivityPage page{wire.value().cursor, {}};
  page.events.reserve(wire.value().data->size());
  for (std::size_t index = 0; index < wire.value().data->size(); ++index) {
    const auto &item = (*wire.value().data)[index];
    const auto prefix = "data[" + std::to_string(index) + "]";
    if (!item.name)
      return missing(prefix + ".name");
    if (!item.createdAt)
      return missing(prefix + ".createdAt");
    ActivityEvent event{
        *item.name, *item.createdAt, item.transactionHash, {}, {}, {}, {}, {}};
    if (item.amountFilled) {
      auto value = exact(*item.amountFilled, prefix + ".amountFilled");
      if (!value)
        return value.error();
      event.amount_filled = std::move(value.value());
    }
    if (item.priceExecuted) {
      auto value = exact(*item.priceExecuted, prefix + ".priceExecuted");
      if (!value)
        return value.error();
      event.price_executed = std::move(value.value());
    }
    if (item.order) {
      if (!item.order->quoteType || !item.order->amount || !item.order->price)
        return missing(prefix + ".order");
      auto amount = exact(*item.order->amount, prefix + ".order.amount");
      auto price = exact(*item.order->price, prefix + ".order.price");
      if (!amount)
        return amount.error();
      if (!price)
        return price.error();
      ActivityOrder order{*item.order->quoteType,
                          std::move(amount.value()),
                          std::move(price.value()),
                          {}};
      if (item.order->fee) {
        if (!item.order->fee->amount || !item.order->fee->type)
          return missing(prefix + ".order.fee");
        auto fee =
            exact(*item.order->fee->amount, prefix + ".order.fee.amount");
        if (!fee)
          return fee.error();
        order.fee = ActivityFee{std::move(fee.value()), *item.order->fee->type};
      }
      event.order = std::move(order);
    }
    if (item.market) {
      auto parsed = market(*item.market, limits);
      if (!parsed)
        return parsed.error();
      event.market = std::move(parsed.value());
    }
    if (item.outcome) {
      auto parsed = outcome(*item.outcome, prefix + ".outcome");
      if (!parsed)
        return parsed.error();
      event.outcome = std::move(parsed.value());
    }
    page.events.push_back(std::move(event));
  }
  return page;
}

} // namespace predictfun::codec
