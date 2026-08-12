#pragma once

#include "predictfun/types/error.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace predictfun {

class EvmAddress {
public:
  EvmAddress() = default;

  [[nodiscard]] static Result<EvmAddress> parse(std::string_view text);
  [[nodiscard]] std::string to_string() const;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] const std::array<std::uint8_t, 20> &bytes() const noexcept;

  friend bool operator==(const EvmAddress &, const EvmAddress &) = default;

private:
  explicit EvmAddress(std::array<std::uint8_t, 20> bytes);
  std::array<std::uint8_t, 20> bytes_{};
};

} // namespace predictfun
