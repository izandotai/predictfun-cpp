#include "predictfun/types/decimal.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>

namespace predictfun {
namespace {

constexpr std::array<std::uint64_t, 19> powers_of_ten{
    1ULL,
    10ULL,
    100ULL,
    1'000ULL,
    10'000ULL,
    100'000ULL,
    1'000'000ULL,
    10'000'000ULL,
    100'000'000ULL,
    1'000'000'000ULL,
    10'000'000'000ULL,
    100'000'000'000ULL,
    1'000'000'000'000ULL,
    10'000'000'000'000ULL,
    100'000'000'000'000ULL,
    1'000'000'000'000'000ULL,
    10'000'000'000'000'000ULL,
    100'000'000'000'000'000ULL,
    1'000'000'000'000'000'000ULL,
};

Result<std::uint64_t> checked_multiply(std::uint64_t value,
                                       std::uint64_t factor) {
  if (factor != 0U &&
      value > std::numeric_limits<std::uint64_t>::max() / factor) {
    return Error{
        ErrorCode::numeric_overflow, "decimal exceeds uint64 capacity", {}};
  }
  return value * factor;
}

} // namespace

Result<FixedDecimal> FixedDecimal::parse(std::string_view text) {
  if (text.empty() || text.front() == '-' || text.front() == '+') {
    return Error{
        ErrorCode::invalid_decimal, "expected an unsigned decimal", {}};
  }

  const auto exponent_pos = text.find_first_of("eE");
  const auto significand = text.substr(0, exponent_pos);
  const auto dot_pos = significand.find('.');
  if (dot_pos != std::string_view::npos &&
      significand.find('.', dot_pos + 1U) != std::string_view::npos) {
    return Error{
        ErrorCode::invalid_decimal, "decimal has more than one point", {}};
  }

  std::string digits;
  digits.reserve(significand.size());
  std::size_t fractional_digits = 0;
  bool saw_digit = false;
  for (std::size_t i = 0; i < significand.size(); ++i) {
    const char ch = significand[i];
    if (ch == '.') {
      continue;
    }
    if (ch < '0' || ch > '9') {
      return Error{ErrorCode::invalid_decimal,
                   "decimal contains an invalid character",
                   {}};
    }
    saw_digit = true;
    digits.push_back(ch);
    if (dot_pos != std::string_view::npos && i > dot_pos) {
      ++fractional_digits;
    }
  }
  if (!saw_digit || (dot_pos == 0U && significand.size() == 1U)) {
    return Error{ErrorCode::invalid_decimal, "decimal has no digits", {}};
  }

  int exponent = 0;
  if (exponent_pos != std::string_view::npos) {
    const auto exponent_text = text.substr(exponent_pos + 1U);
    if (exponent_text.empty()) {
      return Error{ErrorCode::invalid_decimal, "decimal exponent is empty", {}};
    }
    const auto *begin = exponent_text.data();
    const auto *end = begin + exponent_text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, exponent);
    if (ec != std::errc{} || ptr != end) {
      return Error{
          ErrorCode::invalid_decimal, "decimal exponent is invalid", {}};
    }
  }

  while (digits.size() > 1U && digits.front() == '0') {
    digits.erase(digits.begin());
  }

  std::uint64_t units = 0;
  const auto [ptr, ec] =
      std::from_chars(digits.data(), digits.data() + digits.size(), units);
  if (ec != std::errc{} || ptr != digits.data() + digits.size()) {
    return Error{ErrorCode::numeric_overflow,
                 "decimal significand exceeds uint64 capacity",
                 {}};
  }

  const auto signed_scale = static_cast<long long>(fractional_digits) -
                            static_cast<long long>(exponent);
  if (signed_scale < 0) {
    const auto zeros = static_cast<std::size_t>(-signed_scale);
    if (zeros > max_scale) {
      return Error{ErrorCode::numeric_overflow,
                   "decimal exponent exceeds supported capacity",
                   {}};
    }
    auto grown = checked_multiply(units, powers_of_ten[zeros]);
    if (!grown) {
      return grown.error();
    }
    units = grown.value();
    return FixedDecimal{units, 0};
  }
  if (signed_scale > max_scale) {
    return Error{ErrorCode::unsupported_precision,
                 "decimal scale exceeds 18 digits",
                 {}};
  }

  auto scale = static_cast<std::uint8_t>(signed_scale);
  while (scale > 0U && units % 10U == 0U) {
    units /= 10U;
    --scale;
  }
  return FixedDecimal{units, scale};
}

Result<FixedDecimal>
FixedDecimal::rescale_exact(std::uint8_t target_scale) const {
  if (target_scale > max_scale) {
    return Error{
        ErrorCode::unsupported_precision, "target scale exceeds 18 digits", {}};
  }
  if (target_scale == scale_) {
    return *this;
  }
  if (target_scale > scale_) {
    const auto factor =
        powers_of_ten[static_cast<std::size_t>(target_scale - scale_)];
    auto grown = checked_multiply(units_, factor);
    if (!grown) {
      return grown.error();
    }
    return FixedDecimal{grown.value(), target_scale};
  }

  const auto divisor =
      powers_of_ten[static_cast<std::size_t>(scale_ - target_scale)];
  if (units_ % divisor != 0U) {
    return Error{ErrorCode::invalid_decimal,
                 "decimal is not aligned to the requested scale",
                 {}};
  }
  return FixedDecimal{units_ / divisor, target_scale};
}

std::string FixedDecimal::to_string() const {
  if (scale_ == 0U) {
    return std::to_string(units_);
  }
  auto digits = std::to_string(units_);
  if (digits.size() <= scale_) {
    digits.insert(0U, static_cast<std::size_t>(scale_) + 1U - digits.size(),
                  '0');
  }
  digits.insert(digits.size() - scale_, 1U, '.');
  return digits;
}

Result<Price> Price::parse(std::string_view text,
                           std::uint8_t decimal_precision) {
  auto decimal = FixedDecimal::parse(text);
  if (!decimal) {
    auto error = decimal.error();
    error.code = ErrorCode::invalid_price;
    return error;
  }
  auto aligned = decimal.value().rescale_exact(decimal_precision);
  if (!aligned) {
    auto error = aligned.error();
    error.code = ErrorCode::invalid_price;
    return error;
  }
  return from_ticks(aligned.value().units(), decimal_precision);
}

Result<Price> Price::from_ticks(std::uint64_t ticks,
                                std::uint8_t decimal_precision) {
  if (decimal_precision > FixedDecimal::max_scale) {
    return Error{ErrorCode::unsupported_precision,
                 "price precision exceeds 18 digits", "decimalPrecision"};
  }
  const auto scale = powers_of_ten[decimal_precision];
  if (ticks > scale) {
    return Error{ErrorCode::invalid_price, "price must be between zero and one",
                 "price"};
  }
  return Price{ticks, decimal_precision, scale};
}

Price Price::complement() const noexcept {
  return Price{tick_scale_ - ticks_, precision_, tick_scale_};
}

std::string Price::to_string() const {
  return FixedDecimal{ticks_, precision_}.to_string();
}

} // namespace predictfun
