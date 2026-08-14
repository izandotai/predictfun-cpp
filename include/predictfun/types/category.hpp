#pragma once

#include "predictfun/types/market.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace predictfun {

enum class CategoryStatus { open, resolved, removed, unknown };

// Tag ids are bigint strings in the wire schema. Keep them as text so the SDK
// never truncates identifiers that exceed a native integer.
struct Tag {
  std::string id;
  std::string name;
  std::optional<std::int32_t> level;
  std::optional<std::string> parent_id;
  std::optional<std::int32_t> maker_rebate_bps;
};

struct CategoryStatistics {
  ExactDecimal total_liquidity_usd;
  ExactDecimal volume_total_usd;
  ExactDecimal volume_24h_usd;
  std::uint64_t holders_count{0};
};

struct Category {
  std::uint64_t id{0};
  std::string slug;
  std::string title;
  std::optional<std::string> short_title;
  std::optional<std::string> description;
  std::optional<std::string> image_url;
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
  std::vector<Tag> tags;
  std::vector<Market> markets;
  std::optional<std::string> resolution_provider;
  std::optional<std::string> parent_slug;
  std::optional<CategoryStatistics> stats;
  std::optional<std::string> teams_json;
  std::optional<std::string> variant_details_json;
};

struct CategoriesPage {
  std::optional<std::string> cursor;
  std::vector<Category> categories;
};

} // namespace predictfun
