#include "predictfun/order/eip712.hpp"

#include "core/crypto/eip712.hpp"
#include "core/units/u256.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <vector>

namespace predictfun::order {
namespace {

using Word = std::array<std::uint8_t, 32>;

constexpr std::string_view order_type =
    "Order(uint256 salt,address maker,address signer,address taker,uint256 tokenId,uint256 makerAmount,uint256 takerAmount,uint256 expiration,uint256 nonce,uint256 feeRateBps,uint8 side,uint8 signatureType)";

Result<Word> word(const Uint256 &value) {
  try {
    return izan::units::U256::from_dec(value.to_string()).be;
  } catch (const std::exception &) {
    return Error{ErrorCode::numeric_overflow, "uint256 exceeds 32 bytes", {}};
  }
}

Word word(std::uint64_t value) {
  Word result{};
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    result[result.size() - 1U - index] = static_cast<std::uint8_t>(value & 0xffU);
    value >>= 8U;
  }
  return result;
}

Word word(const EvmAddress &value) {
  Word result{};
  std::copy(value.bytes().begin(), value.bytes().end(), result.begin() + 12);
  return result;
}

void append(std::vector<std::uint8_t> &out, const Word &value) {
  out.insert(out.end(), value.begin(), value.end());
}

Word as_word(const Hash32 &hash) {
  Word result{};
  std::copy(hash.begin(), hash.end(), result.begin());
  return result;
}

Result<Hash32> hash_words(std::initializer_list<Word> words) {
  std::vector<std::uint8_t> encoded;
  encoded.reserve(words.size() * 32U);
  for (const auto &item : words)
    append(encoded, item);
  return keccak256(encoded);
}

} // namespace

Result<Hash32> keccak256(std::span<const std::uint8_t> bytes) {
  return izan::crypto::eip712::keccak256(bytes);
}

Result<Hash32> keccak256(std::string_view text) {
  return izan::crypto::eip712::keccak256(text);
}

std::string to_hex(const Hash32 &hash) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result{"0x"};
  result.reserve(66U);
  for (const auto byte : hash) {
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 0x0fU]);
  }
  return result;
}

Result<Hash32> order_struct_hash(const UnsignedOrder &order) {
  auto type_hash = keccak256(order_type);
  auto salt = word(order.salt);
  auto token = word(order.token_id);
  auto maker_amount = word(order.maker_amount);
  auto taker_amount = word(order.taker_amount);
  auto expiration = word(order.expiration);
  auto nonce = word(order.nonce);
  auto fee = word(order.fee_rate_bps);
  if (!type_hash || !salt || !token || !maker_amount || !taker_amount ||
      !expiration || !nonce || !fee)
    return Error{ErrorCode::numeric_overflow,
                 "order contains an invalid uint256", {}};
  Word zero{};
  return hash_words({
      as_word(type_hash.value()), salt.value(), word(order.maker),
      word(order.signer), order.taker ? word(*order.taker) : zero,
      token.value(), maker_amount.value(), taker_amount.value(),
      expiration.value(), nonce.value(), fee.value(),
      word(static_cast<std::uint64_t>(order.side)),
      word(static_cast<std::uint64_t>(order.signature_type))});
}

Result<Hash32> domain_separator(ChainId chain_id,
                                const EvmAddress &verifying_contract) {
  izan::crypto::eip712::EthAddress contract{};
  std::copy(verifying_contract.bytes().begin(),
            verifying_contract.bytes().end(), contract.begin());
  return izan::crypto::eip712::domain_separator(
      protocol_name, protocol_version, static_cast<std::uint64_t>(chain_id),
      contract);
}

Result<Hash32> typed_data_digest(const UnsignedOrder &order, ChainId chain_id,
                                 const EvmAddress &verifying_contract) {
  auto domain = domain_separator(chain_id, verifying_contract);
  auto structure = order_struct_hash(order);
  if (!domain)
    return domain.error();
  if (!structure)
    return structure.error();
  return izan::crypto::eip712::typed_digest(domain.value(), structure.value());
}

} // namespace predictfun::order
