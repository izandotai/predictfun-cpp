#include "predictfun/analysis/liquidity.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

predictfun::Price price(std::string_view value) {
  auto parsed = predictfun::Price::parse(value, 2);
  if (!parsed)
    std::abort();
  return parsed.value();
}

predictfun::FixedDecimal quantity(std::string_view value) {
  auto parsed = predictfun::FixedDecimal::parse(value);
  if (!parsed)
    std::abort();
  return parsed.value();
}

predictfun::Uint256 wei(std::string_view value) {
  std::string digits{value};
  const auto point = digits.find('.');
  std::size_t fraction = 0;
  if (point != std::string::npos) {
    fraction = digits.size() - point - 1U;
    digits.erase(point, 1U);
  }
  digits.append(18U - fraction, '0');
  auto parsed = predictfun::Uint256::parse(digits);
  if (!parsed)
    std::abort();
  return parsed.value();
}

void complete_quote_crosses_levels() {
  const std::vector<predictfun::PriceLevel> asks{
      {price("0.50"), quantity("10")}, {price("0.75"), quantity("10")}};
  auto quote = predictfun::analysis::quote_market_buy_value(wei("10"), asks);
  check(quote.has_value(), "complete quote succeeds");
  if (!quote)
    return;
  check(quote.value().complete, "ten-dollar quote is complete");
  check(quote.value().levels_consumed == 2,
        "complete quote consumes two levels");
  check(quote.value().spent_value_wei ==
            predictfun::Uint256::parse("9999999999999999999").value(),
        "complete quote preserves integer rounding");
  check(quote.value().vwap_price_wei ==
            predictfun::Uint256::parse("599999999999999999").value(),
        "complete quote reports floor-rounded integer VWAP");
  check(quote.value().worst_price_wei == wei("0.75"),
        "complete quote reports worst price");
}

void insufficient_quote_is_not_an_error() {
  const std::vector<predictfun::PriceLevel> asks{
      {price("0.50"), quantity("10")}, {price("0.75"), quantity("10")}};
  auto quote = predictfun::analysis::quote_market_buy_value(wei("20"), asks);
  check(quote.has_value(), "insufficient depth still returns a quote");
  if (!quote)
    return;
  check(!quote.value().complete, "insufficient quote is marked incomplete");
  check(quote.value().spent_value_wei == wei("12.5"),
        "insufficient quote reports executable spend");
  check(quote.value().shares_wei == wei("20"),
        "insufficient quote reports executable shares");
}

void empty_and_invalid_books() {
  auto empty = predictfun::analysis::quote_market_buy_value(wei("10"), {});
  check(empty && !empty.value().complete &&
            empty.value().spent_value_wei.is_zero(),
        "empty book returns a zero incomplete quote");

  const std::vector<predictfun::PriceLevel> unsorted{
      {price("0.75"), quantity("1")}, {price("0.50"), quantity("1")}};
  auto invalid =
      predictfun::analysis::quote_market_buy_value(wei("1"), unsorted);
  check(!invalid &&
            invalid.error().code == predictfun::ErrorCode::invalid_orderbook,
        "unsorted asks are rejected");

  auto zero = predictfun::analysis::quote_market_buy_value(wei("0"), {});
  check(!zero && zero.error().code == predictfun::ErrorCode::invalid_quantity,
        "zero budget is rejected");
}

} // namespace

int main() {
  complete_quote_crosses_levels();
  insufficient_quote_is_not_an_error();
  empty_and_invalid_books();
  if (failures != 0)
    return EXIT_FAILURE;
  std::cout << "liquidity tests passed\n";
  return EXIT_SUCCESS;
}
