#pragma once

#include "predictfun/types/error.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace predictfun {

// Lossless decimal value used for API fields whose range can exceed native
// integer types. The canonical text is retained; callers explicitly choose
// when and how to round it for order arithmetic.
class ExactDecimal {
public:
  ExactDecimal() = default;

  [[nodiscard]] static Result<ExactDecimal> parse(std::string_view text);
  [[nodiscard]] const std::string &to_string() const noexcept { return text_; }

  friend bool operator==(const ExactDecimal &, const ExactDecimal &) = default;

private:
  explicit ExactDecimal(std::string text) : text_(std::move(text)) {}
  std::string text_{"0"};
};

// Canonical base-10 uint256. This is deliberately not silently converted to
// double or uint64_t; EIP-712/order code can consume the exact digits later.
class Uint256 {
public:
  Uint256() = default;

  [[nodiscard]] static Result<Uint256> parse(std::string_view text);
  [[nodiscard]] const std::string &to_string() const noexcept { return text_; }
  [[nodiscard]] bool is_zero() const noexcept { return text_ == "0"; }

  friend bool operator==(const Uint256 &, const Uint256 &) = default;

private:
  explicit Uint256(std::string text) : text_(std::move(text)) {}
  std::string text_{"0"};
};

} // namespace predictfun
