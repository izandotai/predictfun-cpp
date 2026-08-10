#pragma once

#include "predictfun/types/decimal.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace predictfun {

struct TimeseriesPoint {
  std::int64_t timestamp_ms{0};
  FixedDecimal value;
};

struct TimeseriesPage {
  std::optional<std::string> cursor;
  std::string resolution;
  std::vector<TimeseriesPoint> points;
};

} // namespace predictfun
