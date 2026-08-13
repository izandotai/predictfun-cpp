#include "predictfun/analysis/liquidity.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <string>

namespace predictfun::analysis {
namespace {

using boost::multiprecision::cpp_int;

const cpp_int protocol_scale = cpp_int{1'000'000'000} * 1'000'000'000;

Result<cpp_int> integer(const Uint256 &value) {
  cpp_int result = 0;
  for (const auto ch : value.to_string()) {
    if (ch < '0' || ch > '9')
      return Error{ErrorCode::invalid_field, "invalid uint256", {}};
    result *= 10;
    result += static_cast<unsigned int>(ch - '0');
  }
  return result;
}

Result<Uint256> uint256(const cpp_int &value) {
  if (value < 0)
    return Error{ErrorCode::numeric_overflow, "negative uint256", {}};
  return Uint256::parse(value.convert_to<std::string>());
}

cpp_int power_of_ten(std::uint8_t exponent) {
  cpp_int result = 1;
  for (std::uint8_t index = 0; index < exponent; ++index)
    result *= 10;
  return result;
}

cpp_int price_wei(const Price &value) {
  return (cpp_int{value.ticks()} * protocol_scale) / value.tick_scale();
}

cpp_int quantity_wei(const FixedDecimal &value) {
  return cpp_int{value.units()} * power_of_ten(static_cast<std::uint8_t>(
                                      FixedDecimal::max_scale - value.scale()));
}

} // namespace

Result<MarketBuyQuote>
quote_market_buy_value(const Uint256 &value_wei,
                       const std::vector<PriceLevel> &asks) {
  auto requested = integer(value_wei);
  if (!requested)
    return requested.error();
  if (requested.value() <= 0) {
    return Error{ErrorCode::invalid_quantity,
                 "market buy value must be greater than zero", "value"};
  }

  cpp_int spent = 0;
  cpp_int shares = 0;
  cpp_int weighted_price = 0;
  cpp_int worst_price = 0;
  cpp_int previous_price = 0;
  std::size_t consumed_levels = 0;

  for (const auto &level : asks) {
    const auto price = price_wei(level.price);
    const auto quantity = quantity_wei(level.quantity);
    if (price <= 0 || price > protocol_scale || quantity <= 0) {
      return Error{ErrorCode::invalid_orderbook,
                   "ask level must have price in (0,1] and positive quantity",
                   {}};
    }
    if (previous_price > price) {
      return Error{ErrorCode::invalid_orderbook,
                   "ask levels must be sorted best-to-worst",
                   {}};
    }
    previous_price = price;

    const auto remaining = requested.value() - spent;
    if (remaining <= 1)
      break;

    const auto level_cost = (price * quantity) / protocol_scale;
    cpp_int consumed_quantity = quantity;
    cpp_int consumed_cost = level_cost;
    if (level_cost > remaining) {
      consumed_quantity = (remaining * protocol_scale) / price;
      if (consumed_quantity <= 0)
        break;
      consumed_cost = (price * consumed_quantity) / protocol_scale;
    }
    if (consumed_quantity <= 0 || consumed_cost <= 0)
      continue;

    spent += consumed_cost;
    shares += consumed_quantity;
    weighted_price += price * consumed_quantity;
    worst_price = price;
    ++consumed_levels;
  }

  const auto unspent = requested.value() - spent;
  const auto vwap = shares > 0 ? weighted_price / shares : cpp_int{0};
  auto spent_u = uint256(spent);
  auto unspent_u = uint256(unspent);
  auto shares_u = uint256(shares);
  auto vwap_u = uint256(vwap);
  auto worst_u = uint256(worst_price);
  if (!spent_u || !unspent_u || !shares_u || !vwap_u || !worst_u) {
    return Error{
        ErrorCode::numeric_overflow, "liquidity quote exceeds uint256", {}};
  }

  return MarketBuyQuote{value_wei,
                        std::move(spent_u.value()),
                        std::move(unspent_u.value()),
                        std::move(shares_u.value()),
                        std::move(vwap_u.value()),
                        std::move(worst_u.value()),
                        consumed_levels,
                        unspent <= 1};
}

} // namespace predictfun::analysis
