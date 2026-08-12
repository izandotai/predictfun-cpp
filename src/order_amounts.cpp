#include "predictfun/order/amounts.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

namespace predictfun::order {
namespace {

using boost::multiprecision::cpp_int;

const cpp_int precision = cpp_int{1'000'000'000} * 1'000'000'000;
const cpp_int minimum_quantity = cpp_int{10'000'000'000'000'000};
const cpp_int minimum_value = precision;
const cpp_int basis_points = 10'000;

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

cpp_int truncate_significant(cpp_int value, std::size_t digits) {
  if (value == 0)
    return value;
  auto text = value.convert_to<std::string>();
  if (text.size() <= digits)
    return value;
  cpp_int divisor = 1;
  for (std::size_t index = digits; index < text.size(); ++index)
    divisor *= 10;
  return (value / divisor) * divisor;
}

struct ProcessedBook {
  cpp_int quantity{0};
  cpp_int weighted_price{0};
  cpp_int last_price{0};
};

Result<ProcessedBook> process_book(const std::vector<OrderDepthLevel> &levels,
                                   const cpp_int &requested) {
  ProcessedBook result;
  for (const auto &level : levels) {
    auto price = integer(level.price_wei);
    auto quantity = integer(level.quantity_wei);
    if (!price)
      return price.error();
    if (!quantity)
      return quantity.error();
    if (price.value() <= 0 || price.value() > precision ||
        quantity.value() <= 0) {
      return Error{ErrorCode::invalid_orderbook,
                   "order book level must have price in (0,1] and positive quantity",
                   {}};
    }
    const auto remaining = requested - result.quantity;
    if (remaining <= 0)
      break;
    const cpp_int consumed =
        remaining < quantity.value() ? remaining : quantity.value();
    result.quantity += consumed;
    result.weighted_price += price.value() * consumed;
    result.last_price = price.value();
  }
  if (result.quantity < requested) {
    return Error{ErrorCode::invalid_orderbook,
                 "order book does not contain the requested liquidity", {}};
  }
  return result;
}

Result<OrderAmounts> market_quantity(const MarketQuantityInput &input,
                                     const OrderDepth &book) {
  auto raw_quantity = integer(input.quantity_wei);
  auto slippage = integer(input.slippage_bps);
  if (!raw_quantity)
    return raw_quantity.error();
  if (!slippage)
    return slippage.error();
  const auto quantity = truncate_significant(raw_quantity.value(), 5U);
  if (quantity < minimum_quantity) {
    return Error{ErrorCode::invalid_quantity,
                 "order quantity is below the protocol minimum", "quantity"};
  }
  if (input.side != ContractSide::buy && input.side != ContractSide::sell) {
    return Error{ErrorCode::invalid_argument, "order side is unknown", "side"};
  }

  const auto &levels =
      input.side == ContractSide::buy ? book.asks : book.bids;
  auto processed = process_book(levels, quantity);
  if (!processed)
    return processed.error();

  const auto average = processed.value().weighted_price / quantity;
  cpp_int maker = 0;
  cpp_int taker = 0;
  if (input.side == ContractSide::buy && input.is_min_amount_out) {
    maker = processed.value().weighted_price / precision;
    const auto signed_shares = processed.value().last_price > 0
                                   ? processed.value().weighted_price /
                                         processed.value().last_price
                                   : cpp_int{0};
    taker = slippage.value() > basis_points
                ? cpp_int{0}
                : (signed_shares * (basis_points - slippage.value())) /
                      basis_points;
  } else if (input.side == ContractSide::buy) {
    const auto base = (processed.value().last_price * quantity) / precision;
    if (slippage.value() > 0) {
      const cpp_int inflated =
          (base * (basis_points + slippage.value())) / basis_points;
      maker = inflated < quantity ? inflated : quantity;
    } else {
      maker = base;
    }
    taker = quantity;
  } else {
    maker = quantity;
    const auto base = (processed.value().last_price * quantity) / precision;
    taker = slippage.value() > basis_points
                ? cpp_int{0}
                : (base * (basis_points - slippage.value())) / basis_points;
  }

  auto last = uint256(processed.value().last_price);
  auto avg = uint256(average);
  auto maker_u = uint256(maker);
  auto taker_u = uint256(taker);
  auto amount = uint256(quantity);
  if (!last)
    return last.error();
  if (!avg)
    return avg.error();
  if (!maker_u)
    return maker_u.error();
  if (!taker_u)
    return taker_u.error();
  if (!amount)
    return amount.error();
  return OrderAmounts{std::move(last.value()), std::move(avg.value()),
                      std::move(maker_u.value()), std::move(taker_u.value()),
                      std::move(amount.value()), input.slippage_bps,
                      input.is_min_amount_out};
}

} // namespace

Result<Uint256> decimal_to_wei(std::string_view decimal) {
  if (decimal.empty() || decimal.front() == '-' || decimal.front() == '+') {
    return Error{ErrorCode::invalid_decimal,
                 "expected an unsigned plain decimal", {}};
  }
  const auto point = decimal.find('.');
  if (point != std::string_view::npos &&
      decimal.find('.', point + 1U) != std::string_view::npos) {
    return Error{ErrorCode::invalid_decimal,
                 "decimal contains more than one point", {}};
  }
  const auto whole = point == std::string_view::npos ? decimal
                                                      : decimal.substr(0, point);
  const auto fraction = point == std::string_view::npos
                            ? std::string_view{}
                            : decimal.substr(point + 1U);
  if (whole.empty() || fraction.size() > protocol_decimals ||
      (point != std::string_view::npos && fraction.empty())) {
    return Error{ErrorCode::unsupported_precision,
                 "decimal cannot be represented with 18 places", {}};
  }
  const auto all_digits = [](std::string_view value) {
    return std::ranges::all_of(value, [](unsigned char ch) {
      return std::isdigit(ch) != 0;
    });
  };
  if (!all_digits(whole) || !all_digits(fraction)) {
    return Error{ErrorCode::invalid_decimal,
                 "decimal must use plain base-10 notation", {}};
  }
  std::string digits{whole};
  digits.append(fraction);
  digits.append(protocol_decimals - fraction.size(), '0');
  const auto first = digits.find_first_not_of('0');
  return Uint256::parse(first == std::string::npos ? "0" : digits.substr(first));
}

Result<Uint256> retain_significant_digits(const Uint256 &value,
                                          std::size_t digits) {
  if (digits == 0U)
    return Error{ErrorCode::invalid_argument,
                 "significant digit count must be positive", "digits"};
  auto parsed = integer(value);
  if (!parsed)
    return parsed.error();
  return uint256(truncate_significant(parsed.value(), digits));
}

Result<OrderAmounts> limit_amounts(const LimitAmountsInput &input) {
  if (input.side != ContractSide::buy && input.side != ContractSide::sell)
    return Error{ErrorCode::invalid_argument, "order side is unknown", "side"};
  auto raw_price = integer(input.price_per_share_wei);
  auto raw_quantity = integer(input.quantity_wei);
  if (!raw_price)
    return raw_price.error();
  if (!raw_quantity)
    return raw_quantity.error();
  const auto price = truncate_significant(raw_price.value(), 3U);
  const auto quantity = truncate_significant(raw_quantity.value(), 5U);
  if (quantity < minimum_quantity)
    return Error{ErrorCode::invalid_quantity,
                 "order quantity is below the protocol minimum", "quantity"};
  if (price <= 0)
    return Error{ErrorCode::invalid_price,
                 "price must be greater than zero", "price"};
  const auto collateral = (price * quantity) / precision;
  const auto maker = input.side == ContractSide::buy ? collateral : quantity;
  const auto taker = input.side == ContractSide::buy ? quantity : collateral;
  auto price_u = uint256(price);
  auto quantity_u = uint256(quantity);
  auto maker_u = uint256(maker);
  auto taker_u = uint256(taker);
  auto zero = Uint256::parse("0");
  if (!price_u || !quantity_u || !maker_u || !taker_u || !zero)
    return Error{ErrorCode::numeric_overflow,
                 "order amount exceeds uint256", {}};
  return OrderAmounts{price_u.value(), price_u.value(), maker_u.value(),
                      taker_u.value(), quantity_u.value(), zero.value(), false};
}

Result<OrderAmounts> market_amounts(const MarketQuantityInput &input,
                                    const OrderDepth &book) {
  return market_quantity(input, book);
}

Result<OrderAmounts> market_amounts(const MarketValueInput &input,
                                    const OrderDepth &book) {
  auto value = integer(input.value_wei);
  if (!value)
    return value.error();
  if (value.value() < minimum_value)
    return Error{ErrorCode::invalid_quantity,
                 "market buy value is below one collateral token", "value"};

  cpp_int shares = 0;
  cpp_int spent = 0;
  for (const auto &level : book.asks) {
    auto price = integer(level.price_wei);
    auto quantity = integer(level.quantity_wei);
    if (!price)
      return price.error();
    if (!quantity)
      return quantity.error();
    if (price.value() <= 0 || price.value() > precision ||
        quantity.value() <= 0)
      return Error{ErrorCode::invalid_orderbook, "invalid ask level", {}};
    const auto remaining = value.value() - spent;
    if (remaining <= 0)
      break;
    const auto level_cost = (price.value() * quantity.value()) / precision;
    if (level_cost <= remaining) {
      shares += quantity.value();
      spent += level_cost;
    } else {
      const auto partial = (remaining * precision) / price.value();
      shares += partial;
      spent += (price.value() * partial) / precision;
    }
  }
  if (shares <= 0)
    return Error{ErrorCode::invalid_orderbook,
                 "order book has no buyable liquidity", {}};
  auto quantity = uint256(truncate_significant(shares, 5U));
  if (!quantity)
    return quantity.error();
  return market_quantity(MarketQuantityInput{ContractSide::buy,
                                             quantity.value(),
                                             input.slippage_bps,
                                             input.is_min_amount_out},
                         book);
}

} // namespace predictfun::order
