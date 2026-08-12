#include "predictfun/types/exact_number.hpp"

#include <algorithm>
#include <cctype>

namespace predictfun {
namespace {

constexpr std::string_view uint256_max =
    "115792089237316195423570985008687907853269984665640564039457584007913129639935";

bool digits(std::string_view text) {
  return !text.empty() &&
         std::ranges::all_of(text, [](unsigned char value) {
           return std::isdigit(value) != 0;
         });
}

} // namespace

Result<ExactDecimal> ExactDecimal::parse(std::string_view text) {
  if (text.empty())
    return Error{ErrorCode::invalid_decimal, "decimal is empty", {}};
  std::size_t position = text.front() == '-' ? 1U : 0U;
  if (position == text.size())
    return Error{ErrorCode::invalid_decimal, "decimal has no digits", {}};
  bool dot = false;
  std::size_t digit_count = 0U;
  for (; position < text.size(); ++position) {
    const auto value = static_cast<unsigned char>(text[position]);
    if (std::isdigit(value) != 0) {
      ++digit_count;
      continue;
    }
    if (text[position] == '.' && !dot) {
      dot = true;
      continue;
    }
    return Error{ErrorCode::invalid_decimal,
                 "decimal must use plain base-10 notation", {}};
  }
  if (digit_count == 0U || text.back() == '.')
    return Error{ErrorCode::invalid_decimal, "decimal is incomplete", {}};
  return ExactDecimal{std::string{text}};
}

Result<Uint256> Uint256::parse(std::string_view text) {
  if (!digits(text))
    return Error{ErrorCode::invalid_field,
                 "uint256 must contain base-10 digits", {}};
  const auto first = text.find_first_not_of('0');
  const auto canonical = first == std::string_view::npos ? std::string_view{"0"}
                                                         : text.substr(first);
  if (canonical.size() > uint256_max.size() ||
      (canonical.size() == uint256_max.size() && canonical > uint256_max)) {
    return Error{ErrorCode::numeric_overflow, "value exceeds uint256", {}};
  }
  return Uint256{std::string{canonical}};
}

} // namespace predictfun
