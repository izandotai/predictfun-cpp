#include "predictfun/chain/operations.hpp"

#include "predictfun/chain/abi.hpp"
#include "predictfun/order/eip712.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace predictfun::chain {
namespace {

using Word = std::array<std::uint8_t, 32>;

Result<Uint256> uint_value(std::uint64_t value) {
  return Uint256::parse(std::to_string(value));
}

Result<Word> uint_word(std::uint64_t value) {
  auto parsed = uint_value(value);
  if (!parsed) return parsed.error();
  return abi::word(parsed.value());
}

bool zero(const Bytes32 &value) {
  return std::ranges::all_of(value, [](std::uint8_t byte) { return byte == 0U; });
}

Result<ProtocolAddresses> registry(ChainId chain_id) {
  return protocol_addresses(chain_id);
}

EvmAddress conditional_tokens(const ProtocolAddresses &addresses,
                              bool neg_risk, bool yield) {
  if (yield)
    return neg_risk ? addresses.yield_bearing_neg_risk_conditional_tokens
                    : addresses.yield_bearing_conditional_tokens;
  return neg_risk ? addresses.neg_risk_conditional_tokens
                  : addresses.conditional_tokens;
}

EvmAddress neg_adapter(const ProtocolAddresses &addresses, bool yield) {
  return yield ? addresses.yield_bearing_neg_risk_adapter
               : addresses.neg_risk_adapter;
}

Result<UnsignedTransaction> transaction(EvmAddress to,
                                        Result<std::string> data) {
  if (!data) return data.error();
  return UnsignedTransaction{to, std::move(data.value()), Uint256{}};
}

void append(std::vector<std::uint8_t> &out, const Word &word) {
  out.insert(out.end(), word.begin(), word.end());
}

Result<std::vector<std::uint8_t>> encode_order_tuple(const SignedOrder &order) {
  auto salt = abi::word(order.salt);
  auto token = abi::word(order.token_id);
  auto maker_amount = abi::word(order.maker_amount);
  auto taker_amount = abi::word(order.taker_amount);
  auto expiration = abi::word(order.expiration);
  auto nonce = abi::word(order.nonce);
  auto fee = abi::word(order.fee_rate_bps);
  auto side = uint_word(static_cast<std::uint8_t>(order.side));
  auto signature_type = uint_word(static_cast<std::uint8_t>(order.signature_type));
  auto signature = abi::decode_hex(order.signature);
  if (!salt || !token || !maker_amount || !taker_amount || !expiration ||
      !nonce || !fee || !side || !signature_type)
    return Error{ErrorCode::numeric_overflow,
                 "cancel order contains a value outside its ABI type", {}};
  if (!signature || signature.value().empty())
    return Error{ErrorCode::invalid_field,
                 "cancel order requires a non-empty hex signature",
                 "orders[].signature"};

  constexpr std::size_t fields = 13U;
  auto signature_offset = uint_word(fields * 32U);
  auto signature_length = uint_word(signature.value().size());
  if (!signature_offset || !signature_length)
    return Error{ErrorCode::numeric_overflow,
                 "cancel order signature length exceeds ABI limits", {}};

  std::vector<std::uint8_t> encoded;
  const auto padded = ((signature.value().size() + 31U) / 32U) * 32U;
  encoded.reserve((fields + 1U) * 32U + padded);
  append(encoded, salt.value());
  append(encoded, abi::word(order.maker));
  append(encoded, abi::word(order.signer));
  append(encoded, order.taker ? abi::word(*order.taker) : Word{});
  append(encoded, token.value());
  append(encoded, maker_amount.value());
  append(encoded, taker_amount.value());
  append(encoded, expiration.value());
  append(encoded, nonce.value());
  append(encoded, fee.value());
  append(encoded, side.value());
  append(encoded, signature_type.value());
  append(encoded, signature_offset.value());
  append(encoded, signature_length.value());
  encoded.insert(encoded.end(), signature.value().begin(),
                 signature.value().end());
  encoded.resize(encoded.size() + padded - signature.value().size(), 0U);
  return encoded;
}

Result<std::string> encode_cancel_orders(std::span<const SignedOrder> orders) {
  constexpr std::string_view signature =
      "cancelOrders((uint256,address,address,address,uint256,uint256,uint256,uint256,uint256,uint256,uint8,uint8,bytes)[])";
  auto selector = order::keccak256(signature);
  auto root_offset = uint_word(32U);
  auto count = uint_word(orders.size());
  if (!selector || !root_offset || !count)
    return Error{ErrorCode::numeric_overflow,
                 "cancel order array exceeds ABI limits", {}};

  std::vector<std::vector<std::uint8_t>> tuples;
  tuples.reserve(orders.size());
  for (const auto &order : orders) {
    auto encoded = encode_order_tuple(order);
    if (!encoded) return encoded.error();
    tuples.push_back(std::move(encoded.value()));
  }

  std::vector<std::uint8_t> encoded;
  encoded.insert(encoded.end(), selector.value().begin(),
                 selector.value().begin() + 4U);
  append(encoded, root_offset.value());
  append(encoded, count.value());
  std::size_t offset = orders.size() * 32U;
  for (const auto &tuple : tuples) {
    auto tuple_offset = uint_word(offset);
    if (!tuple_offset) return tuple_offset.error();
    append(encoded, tuple_offset.value());
    offset += tuple.size();
  }
  for (const auto &tuple : tuples)
    encoded.insert(encoded.end(), tuple.begin(), tuple.end());
  return abi::encode_hex(encoded);
}

Result<bool> validate_position(const PositionOperation &operation) {
  if (zero(operation.condition_id))
    return Error{ErrorCode::invalid_argument,
                 "condition id must not be zero", "condition_id"};
  if (operation.amount.is_zero())
    return Error{ErrorCode::invalid_argument,
                 "position amount must be greater than zero", "amount"};
  return true;
}

} // namespace

Result<UnsignedTransaction>
route_transaction(ChainId chain_id, UnsignedTransaction transaction,
                  const RouteOptions &route) {
  (void)chain_id;
  if (route.route == TransactionRoute::eoa) return transaction;
  if (!route.predict_account || route.predict_account->empty())
    return Error{ErrorCode::invalid_argument,
                 "Predict Account routing requires its account address",
                 "predict_account"};
  auto inner = abi::decode_hex(transaction.data);
  if (!inner) return inner.error();
  std::vector<std::uint8_t> execution_calldata;
  execution_calldata.reserve(20U + 32U + inner.value().size());
  execution_calldata.insert(execution_calldata.end(),
                            transaction.to.bytes().begin(),
                            transaction.to.bytes().end());
  auto value = abi::word(transaction.value);
  if (!value) return value.error();
  execution_calldata.insert(execution_calldata.end(), value.value().begin(),
                            value.value().end());
  execution_calldata.insert(execution_calldata.end(), inner.value().begin(),
                            inner.value().end());
  const std::array<Word, 1> words{Word{}};
  auto data = abi::encode_call_with_bytes("execute(bytes32,bytes)", words,
                                          execution_calldata);
  if (!data) return data.error();
  return UnsignedTransaction{*route.predict_account, std::move(data.value()),
                             Uint256{}};
}

Result<UnsignedTransaction>
split_transaction(ChainId chain_id, const PositionOperation &operation,
                  const RouteOptions &route) {
  auto valid = validate_position(operation);
  if (!valid) return valid.error();
  auto addresses = registry(chain_id);
  if (!addresses) return addresses.error();
  Result<UnsignedTransaction> direct = Error{};
  if (operation.is_neg_risk) {
    auto amount = abi::word(operation.amount);
    if (!amount) return amount.error();
    const std::array words{abi::word(operation.condition_id), amount.value()};
    direct = transaction(neg_adapter(addresses.value(), operation.is_yield_bearing),
                         abi::encode_call("splitPosition(bytes32,uint256)", words));
  } else {
    const std::array before{abi::word(addresses.value().usdt), Word{},
                            abi::word(operation.condition_id)};
    const std::array values{uint_value(1U).value(), uint_value(2U).value()};
    const std::array<Word, 1> after{abi::word(operation.amount).value()};
    direct = transaction(
        conditional_tokens(addresses.value(), false, operation.is_yield_bearing),
        abi::encode_call_with_uint_array(
            "splitPosition(address,bytes32,bytes32,uint256[],uint256)",
            before, values, after));
  }
  if (!direct) return direct.error();
  return route_transaction(chain_id, std::move(direct.value()), route);
}

Result<UnsignedTransaction>
merge_transaction(ChainId chain_id, const PositionOperation &operation,
                  const RouteOptions &route) {
  auto valid = validate_position(operation);
  if (!valid) return valid.error();
  auto addresses = registry(chain_id);
  if (!addresses) return addresses.error();
  Result<UnsignedTransaction> direct = Error{};
  if (operation.is_neg_risk) {
    auto amount = abi::word(operation.amount);
    if (!amount) return amount.error();
    const std::array words{abi::word(operation.condition_id), amount.value()};
    direct = transaction(neg_adapter(addresses.value(), operation.is_yield_bearing),
                         abi::encode_call("mergePositions(bytes32,uint256)", words));
  } else {
    const std::array before{abi::word(addresses.value().usdt), Word{},
                            abi::word(operation.condition_id)};
    const std::array values{uint_value(1U).value(), uint_value(2U).value()};
    const std::array<Word, 1> after{abi::word(operation.amount).value()};
    direct = transaction(
        conditional_tokens(addresses.value(), false, operation.is_yield_bearing),
        abi::encode_call_with_uint_array(
            "mergePositions(address,bytes32,bytes32,uint256[],uint256)",
            before, values, after));
  }
  if (!direct) return direct.error();
  return route_transaction(chain_id, std::move(direct.value()), route);
}

Result<UnsignedTransaction>
redeem_transaction(ChainId chain_id, const RedeemOperation &operation,
                   const RouteOptions &route) {
  if (zero(operation.condition_id))
    return Error{ErrorCode::invalid_argument,
                 "condition id must not be zero", "condition_id"};
  if (operation.index_set != 1U && operation.index_set != 2U)
    return Error{ErrorCode::invalid_argument,
                 "redeem index set must be 1 or 2", "index_set"};
  auto addresses = registry(chain_id);
  if (!addresses) return addresses.error();
  Result<UnsignedTransaction> direct = Error{};
  if (operation.is_neg_risk) {
    if (!operation.amount || operation.amount->is_zero())
      return Error{ErrorCode::invalid_argument,
                   "negative-risk redeem requires a positive amount", "amount"};
    const auto zero_value = uint_value(0U).value();
    const std::array values{
        operation.index_set == 1U ? *operation.amount : zero_value,
        operation.index_set == 2U ? *operation.amount : zero_value};
    const std::array<Word, 1> before{abi::word(operation.condition_id)};
    direct = transaction(
        neg_adapter(addresses.value(), operation.is_yield_bearing),
        abi::encode_call_with_uint_array(
            "redeemPositions(bytes32,uint256[])", before, values));
  } else {
    const std::array before{abi::word(addresses.value().usdt), Word{},
                            abi::word(operation.condition_id)};
    const std::array values{uint_value(operation.index_set).value()};
    direct = transaction(
        conditional_tokens(addresses.value(), false, operation.is_yield_bearing),
        abi::encode_call_with_uint_array(
            "redeemPositions(address,bytes32,bytes32,uint256[])", before,
            values));
  }
  if (!direct) return direct.error();
  return route_transaction(chain_id, std::move(direct.value()), route);
}

Result<UnsignedTransaction>
convert_transaction(ChainId chain_id, const ConvertOperation &operation,
                    const RouteOptions &route) {
  if (zero(operation.neg_risk_on_chain_id))
    return Error{ErrorCode::invalid_argument,
                 "negative-risk market id must not be zero",
                 "neg_risk_on_chain_id"};
  if (operation.index_set.is_zero() || operation.amount.is_zero())
    return Error{ErrorCode::invalid_argument,
                 "convert index set and amount must be greater than zero",
                 operation.index_set.is_zero() ? "index_set" : "amount"};
  auto addresses = registry(chain_id);
  if (!addresses) return addresses.error();
  auto index = abi::word(operation.index_set);
  auto amount = abi::word(operation.amount);
  if (!index || !amount)
    return Error{ErrorCode::numeric_overflow,
                 "convert values exceed their ABI types", {}};
  const std::array words{abi::word(operation.neg_risk_on_chain_id),
                         index.value(), amount.value()};
  auto direct = transaction(
      neg_adapter(addresses.value(), operation.is_yield_bearing),
      abi::encode_call("convertPositions(bytes32,uint256,uint256)", words));
  if (!direct) return direct.error();
  return route_transaction(chain_id, std::move(direct.value()), route);
}

Result<std::optional<UnsignedTransaction>>
cancel_orders_transaction(ChainId chain_id,
                          std::span<const SignedOrder> orders,
                          MarketContractKind market,
                          const RouteOptions &route) {
  if (orders.empty()) return std::optional<UnsignedTransaction>{};
  auto addresses = registry(chain_id);
  if (!addresses) return addresses.error();
  auto data = encode_cancel_orders(orders);
  if (!data) return data.error();
  auto routed = route_transaction(
      chain_id,
      UnsignedTransaction{exchange_address(addresses.value(), market),
                          std::move(data.value()), Uint256{}},
      route);
  if (!routed) return routed.error();
  return std::optional<UnsignedTransaction>{std::move(routed.value())};
}

} // namespace predictfun::chain
