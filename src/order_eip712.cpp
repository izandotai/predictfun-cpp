#include "predictfun/order/eip712.hpp"

#include <boost/multiprecision/cpp_int.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace predictfun::order {
namespace {

using boost::multiprecision::cpp_int;
using Word = std::array<std::uint8_t, 32>;

constexpr std::string_view domain_type =
    "EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)";
constexpr std::string_view order_type =
    "Order(uint256 salt,address maker,address signer,address taker,uint256 tokenId,uint256 makerAmount,uint256 takerAmount,uint256 expiration,uint256 nonce,uint256 feeRateBps,uint8 side,uint8 signatureType)";

Result<Word> word(const Uint256 &value) {
  cpp_int parsed = 0;
  for (const auto ch : value.to_string()) {
    parsed *= 10;
    parsed += static_cast<unsigned int>(ch - '0');
  }
  Word result{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[result.size() - 1U - index] =
        static_cast<std::uint8_t>((parsed & 0xff).convert_to<unsigned int>());
    parsed >>= 8;
  }
  if (parsed != 0)
    return Error{ErrorCode::numeric_overflow, "uint256 exceeds 32 bytes", {}};
  return result;
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
  using MdPtr = std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)>;
  using CtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
  MdPtr md{EVP_MD_fetch(nullptr, "KECCAK-256", nullptr), EVP_MD_free};
  CtxPtr ctx{EVP_MD_CTX_new(), EVP_MD_CTX_free};
  if (!md || !ctx || EVP_DigestInit_ex(ctx.get(), md.get(), nullptr) != 1 ||
      EVP_DigestUpdate(ctx.get(), bytes.data(), bytes.size()) != 1) {
    return Error{ErrorCode::protocol_error,
                 "OpenSSL KECCAK-256 initialization failed", {}};
  }
  Hash32 result{};
  unsigned int length = 0;
  if (EVP_DigestFinal_ex(ctx.get(), result.data(), &length) != 1 ||
      length != result.size()) {
    return Error{ErrorCode::protocol_error,
                 "OpenSSL KECCAK-256 finalization failed", {}};
  }
  return result;
}

Result<Hash32> keccak256(std::string_view text) {
  return keccak256(std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
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
  auto type_hash = keccak256(domain_type);
  auto name_hash = keccak256(protocol_name);
  auto version_hash = keccak256(protocol_version);
  if (!type_hash || !name_hash || !version_hash)
    return Error{ErrorCode::protocol_error, "cannot hash EIP-712 domain", {}};
  return hash_words({as_word(type_hash.value()), as_word(name_hash.value()),
                     as_word(version_hash.value()),
                     word(static_cast<std::uint64_t>(chain_id)),
                     word(verifying_contract)});
}

Result<Hash32> typed_data_digest(const UnsignedOrder &order, ChainId chain_id,
                                 const EvmAddress &verifying_contract) {
  auto domain = domain_separator(chain_id, verifying_contract);
  auto structure = order_struct_hash(order);
  if (!domain)
    return domain.error();
  if (!structure)
    return structure.error();
  std::array<std::uint8_t, 66> bytes{};
  bytes[0] = 0x19;
  bytes[1] = 0x01;
  std::copy(domain.value().begin(), domain.value().end(), bytes.begin() + 2);
  std::copy(structure.value().begin(), structure.value().end(),
            bytes.begin() + 34);
  return keccak256(bytes);
}

} // namespace predictfun::order
