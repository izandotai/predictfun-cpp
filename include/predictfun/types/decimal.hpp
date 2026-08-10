#pragma once

#include "predictfun/types/error.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace predictfun {

class FixedDecimal {
public:
  static constexpr std::uint8_t max_scale = 18;

  FixedDecimal() = default;
  FixedDecimal(std::uint64_t units, std::uint8_t scale)
      : units_(units), scale_(scale) {}

  [[nodiscard]] static Result<FixedDecimal> parse(std::string_view text);
  [[nodiscard]] Result<FixedDecimal>
  rescale_exact(std::uint8_t target_scale) const;

  [[nodiscard]] std::uint64_t units() const noexcept { return units_; }
  [[nodiscard]] std::uint8_t scale() const noexcept { return scale_; }
  [[nodiscard]] std::string to_string() const;

  friend bool operator==(const FixedDecimal &, const FixedDecimal &) = default;

private:
  std::uint64_t units_{0};
  std::uint8_t scale_{0};
};

class Price {
public:
  Price() = default;

  [[nodiscard]] static Result<Price> parse(std::string_view text,
                                           std::uint8_t decimal_precision);
  [[nodiscard]] static Result<Price> from_ticks(std::uint64_t ticks,
                                                std::uint8_t decimal_precision);

  [[nodiscard]] std::uint64_t ticks() const noexcept { return ticks_; }
  [[nodiscard]] std::uint8_t precision() const noexcept { return precision_; }
  [[nodiscard]] std::uint64_t tick_scale() const noexcept {
    return tick_scale_;
  }
  [[nodiscard]] Price complement() const noexcept;
  [[nodiscard]] std::string to_string() const;

  friend bool operator==(const Price &, const Price &) = default;

private:
  Price(std::uint64_t ticks, std::uint8_t precision, std::uint64_t tick_scale)
      : ticks_(ticks), precision_(precision), tick_scale_(tick_scale) {}

  std::uint64_t ticks_{0};
  std::uint8_t precision_{0};
  std::uint64_t tick_scale_{1};
};

} // namespace predictfun
