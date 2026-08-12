#pragma once

#include "predictfun/types/category.hpp"
#include "predictfun/types/error.hpp"
#include "predictfun/types/market.hpp"
#include "predictfun/types/match.hpp"
#include "predictfun/types/orderbook.hpp"
#include "predictfun/types/timeseries.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace predictfun::codec {

struct DecodeLimits {
  std::size_t max_body_bytes{2U * 1024U * 1024U};
  std::size_t max_markets{1'000U};
  std::size_t max_categories{1'000U};
  std::size_t max_outcomes_per_market{256U};
  std::size_t max_book_levels_per_side{20'000U};
  std::size_t max_timeseries_points{100'000U};
  std::size_t max_matches{10'000U};
  std::size_t max_makers_per_match{10'000U};
  std::size_t max_string_bytes{256U * 1024U};
};

[[nodiscard]] Result<MarketsPage>
decode_markets_response(std::string_view json, const DecodeLimits &limits = {});

[[nodiscard]] Result<Market>
decode_market_response(std::string_view json, const DecodeLimits &limits = {});

[[nodiscard]] Result<CategoriesPage>
decode_categories_response(std::string_view json,
                           const DecodeLimits &limits = {});

[[nodiscard]] Result<Category>
decode_category_response(std::string_view json,
                         const DecodeLimits &limits = {});

[[nodiscard]] Result<Orderbook>
decode_orderbook_response(std::string_view json, std::uint8_t decimal_precision,
                          const DecodeLimits &limits = {});

[[nodiscard]] Result<Orderbook>
decode_orderbook_payload(std::string_view json, std::uint8_t decimal_precision,
                         const DecodeLimits &limits = {});

[[nodiscard]] Result<TimeseriesPage>
decode_timeseries_response(std::string_view json,
                           const DecodeLimits &limits = {});

[[nodiscard]] Result<TimeseriesPoint>
decode_latest_timeseries_response(std::string_view json,
                                  const DecodeLimits &limits = {});

[[nodiscard]] Result<MatchesPage>
decode_matches_response(std::string_view json,
                        const DecodeLimits &limits = {});

} // namespace predictfun::codec
