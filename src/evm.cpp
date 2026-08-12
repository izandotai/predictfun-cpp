#include "predictfun/types/evm.hpp"

#include <algorithm>

namespace predictfun {
namespace {

int hex_value(char value) noexcept {
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return -1;
}

} // namespace

EvmAddress::EvmAddress(std::array<std::uint8_t, 20> bytes) : bytes_(bytes) {}

Result<EvmAddress> EvmAddress::parse(std::string_view text) {
  if (text.size() != 42U || text[0] != '0' || text[1] != 'x') {
    return Error{ErrorCode::invalid_argument,
                 "EVM address must be 0x followed by 40 hexadecimal digits",
                 "address"};
  }
  std::array<std::uint8_t, 20> bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto high = hex_value(text[2U + index * 2U]);
    const auto low = hex_value(text[3U + index * 2U]);
    if (high < 0 || low < 0) {
      return Error{ErrorCode::invalid_argument,
                   "EVM address contains a non-hexadecimal digit", "address"};
    }
    bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  if (std::ranges::all_of(bytes, [](std::uint8_t value) { return value == 0; })) {
    return Error{ErrorCode::invalid_argument,
                 "zero EVM address is not a valid identity", "address"};
  }
  return EvmAddress{bytes};
}

std::string EvmAddress::to_string() const {
  constexpr char hex[] = "0123456789abcdef";
  std::string result(42U, '0');
  result[0] = '0';
  result[1] = 'x';
  for (std::size_t index = 0; index < bytes_.size(); ++index) {
    result[2U + index * 2U] = hex[bytes_[index] >> 4U];
    result[3U + index * 2U] = hex[bytes_[index] & 0x0FU];
  }
  return result;
}

bool EvmAddress::empty() const noexcept {
  return std::ranges::all_of(bytes_, [](std::uint8_t value) { return value == 0; });
}

const std::array<std::uint8_t, 20> &EvmAddress::bytes() const noexcept {
  return bytes_;
}

} // namespace predictfun
