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

Result<MarketSellQuote>
quote_market_sell_shares(const Uint256 &shares_wei,
                         const std::vector<PriceLevel> &bids) {
  auto requested = integer(shares_wei);
  if (!requested)
    return requested.error();
  if (requested.value() <= 0) {
    return Error{ErrorCode::invalid_quantity,
                 "market sell shares must be greater than zero", "shares"};
  }

  cpp_int sold = 0;
  cpp_int returned = 0;
  cpp_int weighted_price = 0;
  cpp_int worst_price = 0;
  cpp_int previous_price = protocol_scale + 1;
  std::size_t consumed_levels = 0;

  for (const auto &level : bids) {
    const auto price = price_wei(level.price);
    const auto quantity = quantity_wei(level.quantity);
    if (price <= 0 || price > protocol_scale || quantity <= 0) {
      return Error{ErrorCode::invalid_orderbook,
                   "bid level must have price in (0,1] and positive quantity",
                   {}};
    }
    if (previous_price < price) {
      return Error{ErrorCode::invalid_orderbook,
                   "bid levels must be sorted best-to-worst",
                   {}};
    }
    previous_price = price;

    const cpp_int remaining = requested.value() - sold;
    if (remaining <= 1)
      break;
    const cpp_int consumed_quantity =
        quantity < remaining ? quantity : remaining;
    if (consumed_quantity <= 0)
      continue;

    sold += consumed_quantity;
    returned += (price * consumed_quantity) / protocol_scale;
    weighted_price += price * consumed_quantity;
    worst_price = price;
    ++consumed_levels;
  }

  const auto unsold = requested.value() - sold;
  const auto vwap = sold > 0 ? weighted_price / sold : cpp_int{0};
  auto sold_u = uint256(sold);
  auto unsold_u = uint256(unsold);
  auto returned_u = uint256(returned);
  auto vwap_u = uint256(vwap);
  auto worst_u = uint256(worst_price);
  if (!sold_u || !unsold_u || !returned_u || !vwap_u || !worst_u) {
    return Error{
        ErrorCode::numeric_overflow, "liquidity quote exceeds uint256", {}};
  }

  return MarketSellQuote{shares_wei,
                         std::move(sold_u.value()),
                         std::move(unsold_u.value()),
                         std::move(returned_u.value()),
                         std::move(vwap_u.value()),
                         std::move(worst_u.value()),
                         consumed_levels,
                         unsold <= 1};
}

Result<ImmediateRoundTripQuote>
quote_immediate_round_trip(const Uint256 &value_wei,
                           const std::vector<PriceLevel> &asks,
                           const std::vector<PriceLevel> &bids) {
  auto buy = quote_market_buy_value(value_wei, asks);
  if (!buy)
    return buy.error();
  if (buy.value().shares_wei.is_zero()) {
    return ImmediateRoundTripQuote{buy.value(), MarketSellQuote{}, Uint256{},
                                   Uint256{}, false};
  }

  auto sell = quote_market_sell_shares(buy.value().shares_wei, bids);
  if (!sell)
    return sell.error();
  auto spent = integer(buy.value().spent_value_wei);
  auto recovered = integer(sell.value().returned_value_wei);
  if (!spent || !recovered)
    return Error{
        ErrorCode::numeric_overflow, "round-trip value exceeds uint256", {}};
  const cpp_int raw_loss = spent.value() - recovered.value();
  const cpp_int loss = raw_loss > 0 ? raw_loss : cpp_int{0};
  auto loss_u = uint256(loss);
  if (!loss_u)
    return loss_u.error();

  return ImmediateRoundTripQuote{
      buy.value(), sell.value(), sell.value().returned_value_wei,
      std::move(loss_u.value()), buy.value().complete && sell.value().complete};
}

} // namespace predictfun::analysis
