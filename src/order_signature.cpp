#include "predictfun/order/signature.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace predictfun::order {
namespace {

constexpr std::string_view kernel_name = "Kernel";
constexpr std::string_view kernel_version = "0.3.1";
constexpr std::string_view kernel_message_type = "Kernel(bytes32 hash)";
constexpr std::string_view domain_type =
    "EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)";

using Word = std::array<std::uint8_t, 32>;

Word as_word(const Hash32 &hash) {
  Word result{};
  std::copy(hash.begin(), hash.end(), result.begin());
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

Result<Hash32> hash_words(std::initializer_list<Word> words) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(words.size() * 32U);
  for (const auto &item : words)
    bytes.insert(bytes.end(), item.begin(), item.end());
  return keccak256(bytes);
}

} // namespace

Result<std::string> validate_evm_signature(std::string signature) {
  if (signature.size() != 132U || signature[0] != '0' ||
      signature[1] != 'x' ||
      !std::all_of(signature.begin() + 2, signature.end(),
                   [](unsigned char ch) { return std::isxdigit(ch) != 0; })) {
    return Error{ErrorCode::invalid_field,
                 "signature must be a 65-byte 0x-prefixed hex value",
                 "signature"};
  }
  std::transform(signature.begin() + 2, signature.end(),
                 signature.begin() + 2, [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return signature;
}

Result<Hash32> predict_account_signing_digest(
    const Hash32 &message_hash, ChainId chain_id,
    const EvmAddress &predict_account) {
  auto domain_type_hash = keccak256(domain_type);
  auto name_hash = keccak256(kernel_name);
  auto version_hash = keccak256(kernel_version);
  auto message_type_hash = keccak256(kernel_message_type);
  if (!domain_type_hash || !name_hash || !version_hash || !message_type_hash)
    return Error{ErrorCode::protocol_error,
                 "cannot hash Predict Account signature domain", {}};
  auto domain = hash_words(
      {as_word(domain_type_hash.value()), as_word(name_hash.value()),
       as_word(version_hash.value()), word(static_cast<std::uint64_t>(chain_id)),
       word(predict_account)});
  auto wrapped_message = hash_words(
      {as_word(message_type_hash.value()), as_word(message_hash)});
  if (!domain)
    return domain.error();
  if (!wrapped_message)
    return wrapped_message.error();
  std::array<std::uint8_t, 66> bytes{};
  bytes[0] = 0x19;
  bytes[1] = 0x01;
  std::copy(domain.value().begin(), domain.value().end(), bytes.begin() + 2);
  std::copy(wrapped_message.value().begin(), wrapped_message.value().end(),
            bytes.begin() + 34);
  return keccak256(bytes);
}

Result<std::string> predict_account_signature_envelope(
    const EvmAddress &validator, std::string owner_signature) {
  auto validated = validate_evm_signature(std::move(owner_signature));
  if (!validated)
    return validated.error();
  constexpr char digits[] = "0123456789abcdef";
  std::string result{"0x01"};
  result.reserve(174U);
  for (const auto byte : validator.bytes()) {
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 0x0fU]);
  }
  result.append(validated.value().substr(2));
  return result;
}

Result<SignedOrder> sign_eoa_order(const OrderBuilder &builder,
                                   const UnsignedOrder &order,
                                   MarketContractKind kind,
                                   const DigestSignatureProvider &signer) {
  if (!signer)
    return Error{ErrorCode::invalid_argument, "order signer is missing",
                 "signer"};
  auto digest = builder.digest(order, kind);
  if (!digest)
    return digest.error();
  auto signature = signer(digest.value());
  if (!signature)
    return signature.error();
  auto validated = validate_evm_signature(std::move(signature.value()));
  if (!validated)
    return validated.error();
  SignedOrder result;
  static_cast<UnsignedOrder &>(result) = order;
  result.signature = std::move(validated.value());
  return result;
}

Result<SignedOrder> sign_predict_account_order(
    const OrderBuilder &builder, const UnsignedOrder &order,
    MarketContractKind kind, const EvmAddress &predict_account,
    const EvmAddress &validator,
    const DigestSignatureProvider &sign_personal_digest) {
  if (!sign_personal_digest)
    return Error{ErrorCode::invalid_argument,
                 "Predict Account signer is missing", "signer"};
  if (order.maker != predict_account || order.signer != predict_account)
    return Error{ErrorCode::invalid_argument,
                 "Predict Account must be both order maker and signer",
                 "predict_account"};
  auto order_digest = builder.digest(order, kind);
  if (!order_digest)
    return order_digest.error();
  auto kernel_digest = predict_account_signing_digest(
      order_digest.value(), builder.chain_id(), predict_account);
  if (!kernel_digest)
    return kernel_digest.error();
  auto owner_signature = sign_personal_digest(kernel_digest.value());
  if (!owner_signature)
    return owner_signature.error();
  auto envelope = predict_account_signature_envelope(
      validator, std::move(owner_signature.value()));
  if (!envelope)
    return envelope.error();
  SignedOrder result;
  static_cast<UnsignedOrder &>(result) = order;
  result.signature = std::move(envelope.value());
  return result;
}

} // namespace predictfun::order
