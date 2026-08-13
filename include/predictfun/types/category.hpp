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
  std::optional<std::string> starts_at;
  std::optional<std::string> ends_at;
  std::optional<std::string> created_at;
  std::optional<std::string> published_at;
  std::optional<std::string> market_variant;
  // Required by negative-risk convertPositions; absent on standard categories.
  std::optional<std::string> neg_risk_on_chain_id;
  std::optional<CryptoUpDownVariantData> crypto_up_down;
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
