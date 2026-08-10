#pragma once

#include "predictfun/types/market.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace predictfun {

enum class CategoryStatus { open, resolved, removed, unknown };

struct Category {
  std::uint64_t id{0};
  std::string slug;
  std::string title;
  std::optional<std::string> short_title;
  std::optional<std::string> description;
  bool is_neg_risk{false};
  bool is_yield_bearing{false};
  bool is_visible{false};
  EnumValue<CategoryStatus> status;
  std::vector<Market> markets;
};

struct CategoriesPage {
  std::optional<std::string> cursor;
  std::vector<Category> categories;
};

} // namespace predictfun
