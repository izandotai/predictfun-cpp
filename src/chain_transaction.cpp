#include "predictfun/chain/transaction.hpp"

#include "predictfun/chain/abi.hpp"
#include "predictfun/order/eip712.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <vector>

namespace predictfun::chain {
namespace {

using boost::multiprecision::cpp_int;

void append_length(std::vector<std::uint8_t> &out, std::size_t value) {
  std::array<std::uint8_t, sizeof(std::size_t)> encoded{};
  auto cursor = encoded.end();
  while (value != 0U) {
    *--cursor = static_cast<std::uint8_t>(value & 0xffU);
    value >>= 8U;
  }
  out.push_back(static_cast<std::uint8_t>(0xb7U +
      static_cast<std::uint8_t>(encoded.end() - cursor)));
  out.insert(out.end(), cursor, encoded.end());
}

std::vector<std::uint8_t> rlp_bytes(std::span<const std::uint8_t> value) {
  if (value.size() == 1U && value.front() < 0x80U)
    return {value.front()};
  std::vector<std::uint8_t> out;
  if (value.size() <= 55U) {
    out.push_back(static_cast<std::uint8_t>(0x80U + value.size()));
  } else {
    append_length(out, value.size());
  }
  out.insert(out.end(), value.begin(), value.end());
  return out;
}

std::vector<std::uint8_t> rlp_integer(const Uint256 &value) {
  cpp_int integer{value.to_string()};
  std::vector<std::uint8_t> bytes;
  while (integer != 0) {
    bytes.push_back(static_cast<std::uint8_t>(
        (integer & 0xff).convert_to<unsigned int>()));
    integer >>= 8;
  }
  std::reverse(bytes.begin(), bytes.end());
  return rlp_bytes(bytes);
}

std::vector<std::uint8_t> rlp_integer(const cpp_int &value) {
  return rlp_integer(Uint256::parse(value.convert_to<std::string>()).value());
}

std::vector<std::uint8_t>
rlp_list(std::span<const std::vector<std::uint8_t>> fields) {
  std::size_t size = 0U;
  for (const auto &field : fields) size += field.size();
  std::vector<std::uint8_t> out;
  if (size <= 55U) {
    out.push_back(static_cast<std::uint8_t>(0xc0U + size));
  } else {
    std::array<std::uint8_t, sizeof(std::size_t)> encoded{};
    auto cursor = encoded.end();
    auto remaining = size;
    while (remaining != 0U) {
      *--cursor = static_cast<std::uint8_t>(remaining & 0xffU);
      remaining >>= 8U;
    }
    out.push_back(static_cast<std::uint8_t>(
        0xf7U + static_cast<std::uint8_t>(encoded.end() - cursor)));
    out.insert(out.end(), cursor, encoded.end());
  }
  for (const auto &field : fields)
    out.insert(out.end(), field.begin(), field.end());
  return out;
}

Result<std::vector<std::uint8_t>> transaction_data(std::string_view value) {
  auto bytes = abi::decode_hex(value);
  if (!bytes) {
    auto error = bytes.error();
    error.field = "transaction.data";
    return error;
  }
  return bytes.value();
}

Result<std::array<std::vector<std::uint8_t>, 6>> base_fields(
    const PopulatedTransaction &transaction) {
  auto data = transaction_data(transaction.data);
  if (!data) return data.error();
  return std::array<std::vector<std::uint8_t>, 6>{
      rlp_integer(transaction.nonce), rlp_integer(transaction.gas_price),
      rlp_integer(transaction.gas_limit), rlp_bytes(transaction.to.bytes()),
      rlp_integer(transaction.value), rlp_bytes(data.value())};
}

Result<std::array<std::uint8_t, 65>> signature_bytes(std::string_view value) {
  auto decoded = abi::decode_hex(value);
  if (!decoded || decoded.value().size() != 65U)
    return Error{ErrorCode::invalid_field,
                 "transaction signature must contain 65 bytes", "signature"};
  std::array<std::uint8_t, 65> result{};
  std::copy(decoded.value().begin(), decoded.value().end(), result.begin());
  if (result.back() != 27U && result.back() != 28U)
    return Error{ErrorCode::invalid_field,
                 "transaction signature recovery id must be 27 or 28",
                 "signature"};
  return result;
}

std::vector<std::uint8_t> trim_word(
    const std::array<std::uint8_t, 65> &signature, std::size_t offset) {
  const auto begin = signature.begin() + static_cast<std::ptrdiff_t>(offset);
  const auto end = begin + 32;
  const auto first = std::find_if(begin, end,
                                  [](std::uint8_t value) { return value != 0U; });
  return {first, end};
}

} // namespace

Result<Hash32>
legacy_transaction_signing_digest(const PopulatedTransaction &transaction) {
  auto fields = base_fields(transaction);
  if (!fields) return fields.error();
  const auto chain = cpp_int{static_cast<std::uint64_t>(transaction.chain_id)};
  std::array<std::vector<std::uint8_t>, 9> signing_fields{
      fields.value()[0], fields.value()[1], fields.value()[2],
      fields.value()[3], fields.value()[4], fields.value()[5],
      rlp_integer(chain), rlp_bytes(std::span<const std::uint8_t>{}),
      rlp_bytes(std::span<const std::uint8_t>{})};
  const auto encoded = rlp_list(signing_fields);
  return order::keccak256(encoded);
}

Result<RawTransaction>
sign_legacy_transaction(const PopulatedTransaction &transaction,
                        const TransactionDigestSigner &signer) {
  if (!signer)
    return Error{ErrorCode::invalid_argument,
                 "transaction digest signer is required", "signer"};
  auto digest = legacy_transaction_signing_digest(transaction);
  if (!digest) return digest.error();
  auto signed_digest = signer(digest.value());
  if (!signed_digest) return signed_digest.error();
  auto signature = signature_bytes(signed_digest.value());
  if (!signature) return signature.error();
  auto fields = base_fields(transaction);
  if (!fields) return fields.error();
  const cpp_int recovery = signature.value().back() - 27U;
  const cpp_int chain = static_cast<std::uint64_t>(transaction.chain_id);
  const cpp_int v = recovery + 35 + chain * 2;
  std::array<std::vector<std::uint8_t>, 9> signed_fields{
      fields.value()[0], fields.value()[1], fields.value()[2],
      fields.value()[3], fields.value()[4], fields.value()[5],
      rlp_integer(v), rlp_bytes(trim_word(signature.value(), 0U)),
      rlp_bytes(trim_word(signature.value(), 32U))};
  const auto encoded = rlp_list(signed_fields);
  auto hash = order::keccak256(encoded);
  if (!hash) return hash.error();
  return RawTransaction{abi::encode_hex(encoded), order::to_hex(hash.value())};
}

Result<std::string> raw_transaction_hash(std::string_view raw_transaction) {
  auto bytes = abi::decode_hex(raw_transaction);
  if (!bytes || bytes.value().empty())
    return Error{ErrorCode::invalid_field,
                 "raw transaction must be non-empty 0x-prefixed bytes",
                 "raw_transaction"};
  auto hash = order::keccak256(bytes.value());
  if (!hash) return hash.error();
  return order::to_hex(hash.value());
}

} // namespace predictfun::chain
