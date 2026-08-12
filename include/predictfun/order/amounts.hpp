#pragma once

#include "predictfun/types/order.hpp"

#include <string_view>

namespace predictfun::order {

inline constexpr std::uint8_t protocol_decimals = 18;

[[nodiscard]] Result<Uint256> decimal_to_wei(std::string_view decimal);
[[nodiscard]] Result<Uint256>
retain_significant_digits(const Uint256 &value, std::size_t digits);
[[nodiscard]] Result<OrderAmounts>
limit_amounts(const LimitAmountsInput &input);
[[nodiscard]] Result<OrderAmounts>
market_amounts(const MarketQuantityInput &input, const OrderDepth &book);
[[nodiscard]] Result<OrderAmounts>
market_amounts(const MarketValueInput &input, const OrderDepth &book);

} // namespace predictfun::order
