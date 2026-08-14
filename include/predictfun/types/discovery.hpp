#pragma once

#include "predictfun/types/category.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace predictfun {

enum class LastSaleQuoteType { bid, ask, unknown };
enum class LastSaleOutcome { yes, no, unknown };
enum class LastSaleStrategy { market, limit, unknown };

struct MarketLastSale {
  EnumValue<LastSaleQuoteType> quote_type;
  EnumValue<LastSaleOutcome> outcome;
  ExactDecimal price_in_currency;
  EnumValue<LastSaleStrategy> strategy;
};

struct SearchResults {
  std::vector<Category> categories;
  std::vector<Market> markets;
};

} // namespace predictfun
