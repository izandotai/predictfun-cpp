#include "predictfun/analysis/liquidity.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

predictfun::Result<predictfun::Price> price(std::string_view value) {
  return predictfun::Price::parse(value, 2U);
}

predictfun::Result<predictfun::FixedDecimal> quantity(std::string_view value) {
  return predictfun::FixedDecimal::parse(value);
}

} // namespace

int main() {
  const auto ask_50 = price("0.50");
  const auto ask_75 = price("0.75");
  const auto bid_45 = price("0.45");
  const auto quantity_10 = quantity("10");
  const auto budget =
      predictfun::Uint256::parse("10000000000000000000"); // 10 collateral

  if (!ask_50 || !ask_75 || !bid_45 || !quantity_10 || !budget) {
    std::cerr << "could not construct the exact example inputs\n";
    return EXIT_FAILURE;
  }

  const std::vector<predictfun::PriceLevel> asks{
      {ask_50.value(), quantity_10.value()},
      {ask_75.value(), quantity_10.value()},
  };
  const std::vector<predictfun::PriceLevel> bids{
      {bid_45.value(), quantity_10.value()},
  };

  const auto buy =
      predictfun::analysis::quote_market_buy_value(budget.value(), asks);
  const auto round_trip = predictfun::analysis::quote_immediate_round_trip(
      budget.value(), asks, bids);
  if (!buy || !round_trip) {
    std::cerr << "the book did not satisfy the exact-quote preconditions\n";
    return EXIT_FAILURE;
  }

  std::cout << "buy complete=" << buy.value().complete
            << " spent_wei=" << buy.value().spent_value_wei.to_string()
            << " shares_wei=" << buy.value().shares_wei.to_string()
            << " vwap_wei=" << buy.value().vwap_price_wei.to_string()
            << " levels=" << buy.value().levels_consumed << '\n';
  std::cout << "round_trip_complete=" << round_trip.value().complete
            << " recovered_wei="
            << round_trip.value().recovered_value_wei.to_string()
            << " visible_book_loss_wei="
            << round_trip.value().book_loss_value_wei.to_string() << '\n';
  return EXIT_SUCCESS;
}
