#include "predictfun/chain/abi.hpp"

#include "predictfun/order/eip712.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <array>
#include <cctype>

namespace predictfun::chain::abi {
namespace {

using boost::multiprecision::cpp_int;

int nibble(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return 10 + value - 'a';
  if (value >= 'A' && value <= 'F') return 10 + value - 'A';
  return -1;
}

Result<Uint256> uint_from_bytes(std::span<const std::uint8_t> bytes) {
  cpp_int value = 0;
  for (const auto byte : bytes) {
    value <<= 8;
    value += byte;
  }
  return Uint256::parse(value.convert_to<std::string>());
}

} // namespace

Result<std::vector<std::uint8_t>> decode_hex(std::string_view hex) {
  if (!hex.starts_with("0x") || (hex.size() % 2U) != 0U)
    return Error{ErrorCode::invalid_field,
                 "hex bytes must use an even-length 0x prefix", "hex"};
  std::vector<std::uint8_t> bytes;
  bytes.reserve((hex.size() - 2U) / 2U);
  for (std::size_t index = 2U; index < hex.size(); index += 2U) {
    const auto high = nibble(hex[index]);
    const auto low = nibble(hex[index + 1U]);
    if (high < 0 || low < 0)
      return Error{ErrorCode::invalid_field, "hex contains a non-hex digit",
                   "hex"};
    bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
  }
  return bytes;
}

std::string encode_hex(std::span<const std::uint8_t> bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result{"0x"};
  result.reserve(2U + bytes.size() * 2U);
  for (const auto byte : bytes) {
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 0x0fU]);
  }
  return result;
}

Result<Uint256> decode_quantity(std::string_view hex) {
  if (!hex.starts_with("0x") || hex.size() < 3U)
    return Error{ErrorCode::invalid_field,
                 "RPC quantity must be 0x-prefixed", "result"};
  if (hex.size() > 3U && hex[2] == '0')
    return Error{ErrorCode::invalid_field,
                 "RPC quantity has a leading zero", "result"};
  cpp_int value = 0;
  for (std::size_t index = 2U; index < hex.size(); ++index) {
    const auto part = nibble(hex[index]);
    if (part < 0)
      return Error{ErrorCode::invalid_field,
                   "RPC quantity contains a non-hex digit", "result"};
    value <<= 4;
    value += part;
  }
  if (value < 0 || value >= (cpp_int{1} << 256))
    return Error{ErrorCode::numeric_overflow, "RPC quantity exceeds uint256",
                 "result"};
  return Uint256::parse(value.convert_to<std::string>());
}

Result<Uint256> decode_word_uint256(std::string_view hex) {
  auto bytes = decode_hex(hex);
  if (!bytes) return bytes.error();
  if (bytes.value().size() != 32U)
    return Error{ErrorCode::invalid_field,
                 "ABI uint256 result must contain one word", "result"};
  return uint_from_bytes(bytes.value());
}

Result<bool> decode_word_bool(std::string_view hex) {
  auto value = decode_word_uint256(hex);
  if (!value) return value.error();
  if (value.value().to_string() == "0") return false;
  if (value.value().to_string() == "1") return true;
  return Error{ErrorCode::invalid_field, "ABI bool is not 0 or 1", "result"};
}

Result<Bytes32> decode_bytes32(std::string_view hex) {
  auto bytes = decode_hex(hex);
  if (!bytes) return bytes.error();
  if (bytes.value().size() != 32U)
    return Error{ErrorCode::invalid_field, "bytes32 must contain 32 bytes",
                 "hex"};
  Bytes32 result{};
  std::copy(bytes.value().begin(), bytes.value().end(), result.begin());
  return result;
}

Result<std::string> encode_quantity(const Uint256 &value) {
  cpp_int parsed{value.to_string()};
  if (parsed == 0) return std::string{"0x0"};
  std::string digits;
  constexpr char table[] = "0123456789abcdef";
  while (parsed > 0) {
    digits.push_back(table[(parsed & 0xf).convert_to<unsigned int>()]);
    parsed >>= 4;
  }
  std::reverse(digits.begin(), digits.end());
  return "0x" + digits;
}

Result<std::array<std::uint8_t, 32>> word(const Uint256 &value) {
  cpp_int parsed{value.to_string()};
  std::array<std::uint8_t, 32> result{};
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[result.size() - 1U - index] =
        static_cast<std::uint8_t>((parsed & 0xff).convert_to<unsigned int>());
    parsed >>= 8;
  }
  if (parsed != 0)
    return Error{ErrorCode::numeric_overflow, "uint256 exceeds ABI word",
                 "value"};
  return result;
}

std::array<std::uint8_t, 32> word(const EvmAddress &value) {
  std::array<std::uint8_t, 32> result{};
  std::copy(value.bytes().begin(), value.bytes().end(), result.begin() + 12U);
  return result;
}

std::array<std::uint8_t, 32> word(bool value) {
  std::array<std::uint8_t, 32> result{};
  result.back() = value ? 1U : 0U;
  return result;
}

std::array<std::uint8_t, 32> word(const Bytes32 &value) { return value; }

Result<std::string>
encode_call(std::string_view signature,
            std::span<const std::array<std::uint8_t, 32>> words) {
  auto hash = order::keccak256(signature);
  if (!hash) return hash.error();
  std::vector<std::uint8_t> encoded;
  encoded.reserve(4U + words.size() * 32U);
  encoded.insert(encoded.end(), hash.value().begin(), hash.value().begin() + 4U);
  for (const auto &value : words)
    encoded.insert(encoded.end(), value.begin(), value.end());
  return encode_hex(encoded);
}

Result<std::string> encode_call_with_uint_array(
    std::string_view signature,
    std::span<const std::array<std::uint8_t, 32>> words_before,
    std::span<const Uint256> values,
    std::span<const std::array<std::uint8_t, 32>> words_after) {
  auto hash = order::keccak256(signature);
  if (!hash) return hash.error();
  const auto head_words = words_before.size() + 1U + words_after.size();
  auto offset = word(Uint256::parse(std::to_string(head_words * 32U)).value());
  auto length = word(Uint256::parse(std::to_string(values.size())).value());
  if (!offset || !length)
    return Error{ErrorCode::numeric_overflow,
                 "dynamic ABI offset/length exceeds uint256", {}};
  std::vector<std::uint8_t> encoded;
  encoded.reserve(4U + (head_words + 1U + values.size()) * 32U);
  encoded.insert(encoded.end(), hash.value().begin(), hash.value().begin() + 4U);
  const auto append = [&encoded](const auto &value) {
    encoded.insert(encoded.end(), value.begin(), value.end());
  };
  for (const auto &value : words_before) append(value);
  append(offset.value());
  for (const auto &value : words_after) append(value);
  append(length.value());
  for (const auto &value : values) {
    auto encoded_value = word(value);
    if (!encoded_value) return encoded_value.error();
    append(encoded_value.value());
  }
  return encode_hex(encoded);
}

Result<std::string> encode_call_with_bytes(
    std::string_view signature,
    std::span<const std::array<std::uint8_t, 32>> words_before,
    std::span<const std::uint8_t> value) {
  auto hash = order::keccak256(signature);
  if (!hash) return hash.error();
  auto offset = word(
      Uint256::parse(std::to_string((words_before.size() + 1U) * 32U)).value());
  auto length = word(Uint256::parse(std::to_string(value.size())).value());
  if (!offset || !length)
    return Error{ErrorCode::numeric_overflow,
                 "dynamic ABI offset/length exceeds uint256", {}};
  const auto padded = ((value.size() + 31U) / 32U) * 32U;
  std::vector<std::uint8_t> encoded;
  encoded.reserve(4U + (words_before.size() + 2U) * 32U + padded);
  encoded.insert(encoded.end(), hash.value().begin(), hash.value().begin() + 4U);
  for (const auto &item : words_before)
    encoded.insert(encoded.end(), item.begin(), item.end());
  encoded.insert(encoded.end(), offset.value().begin(), offset.value().end());
  encoded.insert(encoded.end(), length.value().begin(), length.value().end());
  encoded.insert(encoded.end(), value.begin(), value.end());
  encoded.resize(encoded.size() + padded - value.size(), 0U);
  return encode_hex(encoded);
}

Result<std::string> erc20_balance_of(const EvmAddress &owner) {
  const std::array<std::array<std::uint8_t, 32>, 1> words{word(owner)};
  return encode_call("balanceOf(address)", words);
}

Result<std::string> erc20_decimals() {
  return encode_call("decimals()", {});
}

Result<std::string> erc20_allowance(const EvmAddress &owner,
                                    const EvmAddress &spender) {
  const std::array words{word(owner), word(spender)};
  return encode_call("allowance(address,address)", words);
}

Result<std::string> erc20_approve(const EvmAddress &spender,
                                  const Uint256 &amount) {
  auto encoded_amount = word(amount);
  if (!encoded_amount) return encoded_amount.error();
  const std::array words{word(spender), encoded_amount.value()};
  return encode_call("approve(address,uint256)", words);
}

Result<std::string> erc1155_balance_of(const EvmAddress &owner,
                                       const Uint256 &token_id) {
  auto encoded_token = word(token_id);
  if (!encoded_token) return encoded_token.error();
  const std::array words{word(owner), encoded_token.value()};
  return encode_call("balanceOf(address,uint256)", words);
}

Result<std::string>
erc1155_is_approved_for_all(const EvmAddress &owner,
                            const EvmAddress &operator_address) {
  const std::array words{word(owner), word(operator_address)};
  return encode_call("isApprovedForAll(address,address)", words);
}

Result<std::string>
erc1155_set_approval_for_all(const EvmAddress &operator_address,
                             bool approved) {
  const std::array words{word(operator_address), word(approved)};
  return encode_call("setApprovalForAll(address,bool)", words);
}

} // namespace predictfun::chain::abi
