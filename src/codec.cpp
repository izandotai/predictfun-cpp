#include "predictfun/codec/public_rest.hpp"

#include <glaze/glaze.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace predictfun::codec {
namespace detail {

struct WireBestQuote {
  std::optional<glz::raw_json> price;
  std::optional<glz::raw_json> size;
};

struct WireOutcome {
  std::optional<WireBestQuote> bestAsk;
  std::optional<WireBestQuote> bestBid;
  std::optional<std::uint64_t> indexSet;
  std::optional<std::string> name;
  std::optional<std::string> onChainId;
  std::optional<std::string> status;
};

struct WireMarket {
  std::optional<std::uint64_t> id;
  std::optional<std::string> title;
  std::optional<std::string> question;
  std::optional<std::string> tradingStatus;
  std::optional<std::string> status;
  std::optional<std::uint32_t> decimalPrecision;
  std::optional<bool> isNegRisk;
  std::optional<bool> isYieldBearing;
  std::optional<std::uint32_t> feeRateBps;
  std::optional<std::vector<WireOutcome>> outcomes;
};

struct WireMarketsResponse {
  std::optional<std::string> cursor;
  std::optional<std::vector<WireMarket>> data;
  std::optional<bool> success;
};

struct WireMarketResponse {
  std::optional<WireMarket> data;
  std::optional<bool> success;
};

struct WireCategory {
  std::optional<std::uint64_t> id;
  std::optional<std::string> slug;
  std::optional<std::string> title;
  std::optional<std::string> shortTitle;
  std::optional<std::string> description;
  std::optional<bool> isNegRisk;
  std::optional<bool> isYieldBearing;
  std::optional<bool> isVisible;
  std::optional<std::string> status;
  std::optional<std::vector<WireMarket>> markets;
};

struct WireCategoriesResponse {
  std::optional<std::string> cursor;
  std::optional<std::vector<WireCategory>> data;
  std::optional<bool> success;
};

struct WireCategoryResponse {
  std::optional<WireCategory> data;
  std::optional<bool> success;
};

struct WireTimeseriesPoint {
  std::optional<glz::raw_json> x;
  std::optional<glz::raw_json> y;
};

struct WireTimeseriesData {
  std::optional<std::string> resolution;
  std::optional<std::vector<WireTimeseriesPoint>> series;
};

struct WireTimeseriesResponse {
  std::optional<std::string> cursor;
  std::optional<WireTimeseriesData> data;
  std::optional<bool> success;
};

struct WireTimeseriesLatestResponse {
  std::optional<WireTimeseriesPoint> data;
  std::optional<bool> success;
};

struct WireLastOrderSettled {
  std::optional<std::string> id;
  std::optional<std::string> kind;
  std::optional<std::uint64_t> marketId;
  std::optional<std::string> outcome;
  std::optional<glz::raw_json> price;
  std::optional<std::string> side;
};

struct WireOrderbook {
  std::optional<std::vector<std::array<glz::raw_json, 2>>> asks;
  std::optional<std::vector<std::array<glz::raw_json, 2>>> bids;
  std::optional<WireLastOrderSettled> lastOrderSettled;
  std::optional<std::uint64_t> marketId;
  std::optional<std::uint64_t> orderCount;
  std::optional<glz::raw_json> settlementsPending;
  std::optional<std::int64_t> updateTimestampMs;
};

struct WirePendingSettlementLevels {
  std::optional<std::vector<std::array<glz::raw_json, 2>>> asks;
  std::optional<std::vector<std::array<glz::raw_json, 2>>> bids;
};

struct WireOrderbookResponse {
  std::optional<WireOrderbook> data;
  std::optional<bool> success;
};

constexpr auto read_options = glz::opts{
    .error_on_unknown_keys = false,
};

Error missing(std::string field) {
  return Error{ErrorCode::missing_field, "required field is missing",
               std::move(field)};
}

Error invalid(std::string message, std::string field) {
  return Error{ErrorCode::invalid_field, std::move(message), std::move(field)};
}

bool string_within_limit(const std::string &value, const DecodeLimits &limits) {
  return value.size() <= limits.max_string_bytes;
}

template <class Enum> EnumValue<Enum> enum_value(Enum value, std::string raw) {
  return EnumValue<Enum>{value, std::move(raw)};
}

EnumValue<TradingStatus> parse_trading_status(const std::string &raw) {
  if (raw == "OPEN")
    return enum_value(TradingStatus::open, raw);
  if (raw == "MATCHING_NOT_ENABLED")
    return enum_value(TradingStatus::matching_not_enabled, raw);
  if (raw == "CANCEL_ONLY")
    return enum_value(TradingStatus::cancel_only, raw);
  if (raw == "CLOSED")
    return enum_value(TradingStatus::closed, raw);
  return enum_value(TradingStatus::unknown, raw);
}

EnumValue<MarketStatus> parse_market_status(const std::string &raw) {
  if (raw == "REGISTERED")
    return enum_value(MarketStatus::registered, raw);
  if (raw == "OPEN")
    return enum_value(MarketStatus::open, raw);
  if (raw == "RESOLVING")
    return enum_value(MarketStatus::resolving, raw);
  if (raw == "RESOLVED")
    return enum_value(MarketStatus::resolved, raw);
  if (raw == "REMOVED")
    return enum_value(MarketStatus::removed, raw);
  return enum_value(MarketStatus::unknown, raw);
}

EnumValue<OutcomeStatus> parse_outcome_status(const std::string &raw) {
  if (raw == "WON")
    return enum_value(OutcomeStatus::won, raw);
  if (raw == "LOST")
    return enum_value(OutcomeStatus::lost, raw);
  if (raw == "VOIDED")
    return enum_value(OutcomeStatus::voided, raw);
  return enum_value(OutcomeStatus::unknown, raw);
}

EnumValue<CategoryStatus> parse_category_status(const std::string &raw) {
  if (raw == "OPEN")
    return enum_value(CategoryStatus::open, raw);
  if (raw == "RESOLVED")
    return enum_value(CategoryStatus::resolved, raw);
  if (raw == "REMOVED")
    return enum_value(CategoryStatus::removed, raw);
  return enum_value(CategoryStatus::unknown, raw);
}

EnumValue<OrderKind> parse_order_kind(const std::string &raw) {
  if (raw == "LIMIT")
    return enum_value(OrderKind::limit, raw);
  if (raw == "MARKET")
    return enum_value(OrderKind::market, raw);
  return enum_value(OrderKind::unknown, raw);
}

EnumValue<ContractOutcome> parse_contract_outcome(const std::string &raw) {
  if (raw == "Yes" || raw == "YES")
    return enum_value(ContractOutcome::yes, raw);
  if (raw == "No" || raw == "NO")
    return enum_value(ContractOutcome::no, raw);
  return enum_value(ContractOutcome::unknown, raw);
}

EnumValue<BookSide> parse_book_side(const std::string &raw) {
  if (raw == "Bid" || raw == "BID")
    return enum_value(BookSide::bid, raw);
  if (raw == "Ask" || raw == "ASK")
    return enum_value(BookSide::ask, raw);
  return enum_value(BookSide::unknown, raw);
}

std::string_view unquote_raw(const glz::raw_json &raw, std::string &storage) {
  const auto text = std::string_view{raw.str};
  if (text.size() >= 2U && text.front() == '"' && text.back() == '"') {
    auto parse_error = glz::read_json(storage, text);
    if (!parse_error) {
      return storage;
    }
  }
  return text;
}

Result<FixedDecimal> parse_decimal_raw(const glz::raw_json &raw,
                                       std::string field) {
  std::string unquoted;
  const auto text = unquote_raw(raw, unquoted);
  auto parsed = FixedDecimal::parse(text);
  if (!parsed) {
    auto error = parsed.error();
    error.field = std::move(field);
    return error;
  }
  return parsed.value();
}

Result<Price> parse_price_raw(const glz::raw_json &raw, std::uint8_t precision,
                              std::string field) {
  std::string unquoted;
  const auto text = unquote_raw(raw, unquoted);
  auto parsed = Price::parse(text, precision);
  if (!parsed) {
    auto error = parsed.error();
    error.field = std::move(field);
    return error;
  }
  return parsed.value();
}

Result<BestQuote> convert_best_quote(const WireBestQuote &wire,
                                     std::uint8_t precision,
                                     std::string field) {
  if (!wire.price)
    return missing(field + ".price");
  if (!wire.size)
    return missing(field + ".size");
  auto price = parse_price_raw(*wire.price, precision, field + ".price");
  if (!price)
    return price.error();
  auto size = parse_decimal_raw(*wire.size, field + ".size");
  if (!size)
    return size.error();
  if (size.value().units() == 0U) {
    return Error{ErrorCode::invalid_quantity, "quote size must be positive",
                 field + ".size"};
  }
  return BestQuote{price.value(), size.value()};
}

Result<Outcome> convert_outcome(const WireOutcome &wire, std::uint8_t precision,
                                const DecodeLimits &limits, std::size_t index) {
  const auto prefix = "data[].outcomes[" + std::to_string(index) + "]";
  if (!wire.name)
    return missing(prefix + ".name");
  if (!wire.indexSet)
    return missing(prefix + ".indexSet");
  if (!wire.onChainId)
    return missing(prefix + ".onChainId");
  if (!string_within_limit(*wire.name, limits) ||
      !string_within_limit(*wire.onChainId, limits)) {
    return invalid("outcome string exceeds configured limit", prefix);
  }

  Outcome outcome;
  outcome.name = *wire.name;
  outcome.index_set = *wire.indexSet;
  outcome.on_chain_id = *wire.onChainId;
  if (wire.bestBid) {
    auto quote =
        convert_best_quote(*wire.bestBid, precision, prefix + ".bestBid");
    if (!quote)
      return quote.error();
    outcome.best_bid = quote.value();
  }
  if (wire.bestAsk) {
    auto quote =
        convert_best_quote(*wire.bestAsk, precision, prefix + ".bestAsk");
    if (!quote)
      return quote.error();
    outcome.best_ask = quote.value();
  }
  if (wire.status) {
    outcome.status = parse_outcome_status(*wire.status);
  }
  return outcome;
}

Result<Market> convert_market(const WireMarket &wire,
                              const DecodeLimits &limits) {
  if (!wire.id)
    return missing("data[].id");
  if (!wire.title)
    return missing("data[].title");
  if (!wire.question)
    return missing("data[].question");
  if (!wire.tradingStatus)
    return missing("data[].tradingStatus");
  if (!wire.status)
    return missing("data[].status");
  if (!wire.decimalPrecision)
    return missing("data[].decimalPrecision");
  if (!wire.isNegRisk)
    return missing("data[].isNegRisk");
  if (!wire.isYieldBearing)
    return missing("data[].isYieldBearing");
  if (!wire.feeRateBps)
    return missing("data[].feeRateBps");
  if (!wire.outcomes)
    return missing("data[].outcomes");
  if (*wire.decimalPrecision > FixedDecimal::max_scale) {
    return Error{ErrorCode::unsupported_precision,
                 "market precision exceeds 18 digits",
                 "data[].decimalPrecision"};
  }
  if (wire.outcomes->size() > limits.max_outcomes_per_market) {
    return Error{ErrorCode::too_many_items, "market contains too many outcomes",
                 "data[].outcomes"};
  }
  if (!string_within_limit(*wire.title, limits) ||
      !string_within_limit(*wire.question, limits) ||
      !string_within_limit(*wire.tradingStatus, limits) ||
      !string_within_limit(*wire.status, limits)) {
    return invalid("market string exceeds configured limit", "data[]");
  }

  Market market;
  market.id = MarketId{*wire.id};
  market.title = *wire.title;
  market.question = *wire.question;
  market.trading_status = parse_trading_status(*wire.tradingStatus);
  market.status = parse_market_status(*wire.status);
  market.decimal_precision = static_cast<std::uint8_t>(*wire.decimalPrecision);
  market.is_neg_risk = *wire.isNegRisk;
  market.is_yield_bearing = *wire.isYieldBearing;
  market.fee_rate_bps = *wire.feeRateBps;
  market.outcomes.reserve(wire.outcomes->size());
  for (std::size_t i = 0; i < wire.outcomes->size(); ++i) {
    auto outcome = convert_outcome((*wire.outcomes)[i],
                                   market.decimal_precision, limits, i);
    if (!outcome)
      return outcome.error();
    market.outcomes.push_back(std::move(outcome.value()));
  }
  return market;
}

Result<Category> convert_category(const WireCategory &wire,
                                  const DecodeLimits &limits) {
  if (!wire.id)
    return missing("data[].id");
  if (!wire.slug)
    return missing("data[].slug");
  if (!wire.title)
    return missing("data[].title");
  if (!wire.isNegRisk)
    return missing("data[].isNegRisk");
  if (!wire.isYieldBearing)
    return missing("data[].isYieldBearing");
  if (!wire.isVisible)
    return missing("data[].isVisible");
  if (!wire.status)
    return missing("data[].status");
  if (wire.markets && wire.markets->size() > limits.max_markets) {
    return Error{ErrorCode::too_many_items,
                 "category contains too many markets", "data[].markets"};
  }
  const auto strings_valid =
      string_within_limit(*wire.slug, limits) &&
      string_within_limit(*wire.title, limits) &&
      (!wire.shortTitle || string_within_limit(*wire.shortTitle, limits)) &&
      (!wire.description || string_within_limit(*wire.description, limits));
  if (!strings_valid)
    return invalid("category string exceeds configured limit", "data[]");

  Category category;
  category.id = *wire.id;
  category.slug = *wire.slug;
  category.title = *wire.title;
  category.short_title = wire.shortTitle;
  category.description = wire.description;
  category.is_neg_risk = *wire.isNegRisk;
  category.is_yield_bearing = *wire.isYieldBearing;
  category.is_visible = *wire.isVisible;
  category.status = parse_category_status(*wire.status);
  if (wire.markets) {
    category.markets.reserve(wire.markets->size());
    for (const auto &item : *wire.markets) {
      auto market = convert_market(item, limits);
      if (!market)
        return market.error();
      category.markets.push_back(std::move(market.value()));
    }
  }
  return category;
}

Result<TimeseriesPoint>
convert_timeseries_point(const WireTimeseriesPoint &wire, std::string prefix) {
  if (!wire.x)
    return missing(prefix + ".x");
  if (!wire.y)
    return missing(prefix + ".y");
  auto x = parse_decimal_raw(*wire.x, prefix + ".x");
  if (!x)
    return x.error();
  if (x.value().scale() != 0U ||
      x.value().units() > static_cast<std::uint64_t>(
                              std::numeric_limits<std::int64_t>::max())) {
    return invalid("timeseries x must be a non-negative integer",
                   prefix + ".x");
  }
  auto y = parse_decimal_raw(*wire.y, prefix + ".y");
  if (!y)
    return y.error();
  return TimeseriesPoint{static_cast<std::int64_t>(x.value().units()),
                         y.value()};
}

Result<PriceLevel> convert_level(const std::array<glz::raw_json, 2> &wire,
                                 std::uint8_t precision, std::string field) {
  auto price = parse_price_raw(wire[0], precision, field + "[0]");
  if (!price)
    return price.error();
  auto quantity = parse_decimal_raw(wire[1], field + "[1]");
  if (!quantity)
    return quantity.error();
  if (quantity.value().units() == 0U) {
    return Error{ErrorCode::invalid_quantity, "book quantity must be positive",
                 field + "[1]"};
  }
  return PriceLevel{price.value(), quantity.value()};
}

Result<std::vector<PriceLevel>>
convert_levels(const std::vector<std::array<glz::raw_json, 2>> &wire,
               std::uint8_t precision, const DecodeLimits &limits,
               bool ascending, std::string field) {
  if (wire.size() > limits.max_book_levels_per_side) {
    return Error{ErrorCode::too_many_items,
                 "order-book side exceeds configured level limit", field};
  }
  std::vector<PriceLevel> levels;
  levels.reserve(wire.size());
  for (std::size_t i = 0; i < wire.size(); ++i) {
    auto level = convert_level(wire[i], precision,
                               field + "[" + std::to_string(i) + "]");
    if (!level)
      return level.error();
    if (!levels.empty()) {
      const auto previous = levels.back().price.ticks();
      const auto current = level.value().price.ticks();
      const bool valid_order =
          ascending ? current > previous : current < previous;
      if (!valid_order) {
        return Error{ErrorCode::invalid_orderbook,
                     "book prices are unsorted or duplicated", field};
      }
    }
    levels.push_back(std::move(level.value()));
  }
  return levels;
}

Result<LastOrderSettled> convert_last_order(const WireLastOrderSettled &wire,
                                            std::uint8_t precision,
                                            const DecodeLimits &limits) {
  if (!wire.id)
    return missing("data.lastOrderSettled.id");
  if (!wire.kind)
    return missing("data.lastOrderSettled.kind");
  if (!wire.marketId)
    return missing("data.lastOrderSettled.marketId");
  if (!wire.outcome)
    return missing("data.lastOrderSettled.outcome");
  if (!wire.price)
    return missing("data.lastOrderSettled.price");
  if (!wire.side)
    return missing("data.lastOrderSettled.side");
  if (!string_within_limit(*wire.id, limits) ||
      !string_within_limit(*wire.kind, limits) ||
      !string_within_limit(*wire.outcome, limits) ||
      !string_within_limit(*wire.side, limits)) {
    return invalid("last settled order string exceeds configured limit",
                   "data.lastOrderSettled");
  }
  auto price =
      parse_price_raw(*wire.price, precision, "data.lastOrderSettled.price");
  if (!price)
    return price.error();
  return LastOrderSettled{
      *wire.id,
      parse_order_kind(*wire.kind),
      MarketId{*wire.marketId},
      parse_contract_outcome(*wire.outcome),
      price.value(),
      parse_book_side(*wire.side),
  };
}

Result<Orderbook> convert_orderbook(const WireOrderbook &wire,
                                    std::uint8_t decimal_precision,
                                    const DecodeLimits &limits) {
  if (!wire.marketId)
    return missing("data.marketId");
  if (!wire.updateTimestampMs)
    return missing("data.updateTimestampMs");
  if (!wire.asks)
    return missing("data.asks");
  if (!wire.bids)
    return missing("data.bids");
  if (*wire.updateTimestampMs <= 0) {
    return invalid("book timestamp must be positive", "data.updateTimestampMs");
  }

  auto asks = convert_levels(*wire.asks, decimal_precision, limits, true,
                             "data.asks");
  if (!asks)
    return asks.error();
  auto bids = convert_levels(*wire.bids, decimal_precision, limits, false,
                             "data.bids");
  if (!bids)
    return bids.error();
  if (!asks.value().empty() && !bids.value().empty() &&
      bids.value().front().price.ticks() >=
          asks.value().front().price.ticks()) {
    return Error{ErrorCode::invalid_orderbook,
                 "best bid must be below best ask", "data"};
  }

  Orderbook book;
  book.market_id = MarketId{*wire.marketId};
  book.update_timestamp_ms = *wire.updateTimestampMs;
  book.decimal_precision = decimal_precision;
  book.yes_asks = std::move(asks.value());
  book.yes_bids = std::move(bids.value());
  book.order_count = wire.orderCount;
  if (wire.lastOrderSettled) {
    auto last =
        convert_last_order(*wire.lastOrderSettled, decimal_precision, limits);
    if (!last)
      return last.error();
    if (last.value().market_id != book.market_id) {
      return Error{ErrorCode::invalid_orderbook,
                   "last settled order belongs to another market",
                   "data.lastOrderSettled.marketId"};
    }
    book.last_order_settled = std::move(last.value());
  }
  if (wire.settlementsPending) {
    auto pending =
        parse_decimal_raw(*wire.settlementsPending, "data.settlementsPending");
    if (pending) {
      book.settlements_pending = pending.value();
    } else {
      WirePendingSettlementLevels levels;
      const auto parse_error =
          glz::read<read_options>(levels, wire.settlementsPending->str);
      if (parse_error || !levels.asks || !levels.bids) {
        return invalid("pending settlements must be a decimal or level object",
                       "data.settlementsPending");
      }
      auto pending_asks = convert_levels(*levels.asks, decimal_precision,
                                         limits, true,
                                         "data.settlementsPending.asks");
      if (!pending_asks)
        return pending_asks.error();
      auto pending_bids = convert_levels(*levels.bids, decimal_precision,
                                         limits, false,
                                         "data.settlementsPending.bids");
      if (!pending_bids)
        return pending_bids.error();
      book.settlement_levels_pending = PendingSettlementLevels{
          std::move(pending_bids.value()), std::move(pending_asks.value())};
    }
  }
  return book;
}

template <class Wire>
Result<Wire> parse_wire(std::string_view json, const DecodeLimits &limits) {
  if (json.size() > limits.max_body_bytes) {
    return Error{
        ErrorCode::body_too_large, "JSON body exceeds configured limit", {}};
  }
  Wire wire;
  const auto parse_error = glz::read<read_options>(wire, json);
  if (parse_error) {
    return Error{
        ErrorCode::malformed_json,
        glz::format_error(parse_error, json),
        {},
    };
  }
  return wire;
}

} // namespace detail

using namespace detail;

Result<MarketsPage> decode_markets_response(std::string_view json,
                                            const DecodeLimits &limits) {
  auto parsed = parse_wire<WireMarketsResponse>(json, limits);
  if (!parsed)
    return parsed.error();
  const auto &wire = parsed.value();
  if (!wire.success)
    return missing("success");
  if (!*wire.success) {
    return Error{ErrorCode::venue_rejected,
                 "Predict.fun returned success=false", "success"};
  }
  if (!wire.data)
    return missing("data");
  if (wire.data->size() > limits.max_markets) {
    return Error{ErrorCode::too_many_items,
                 "market page exceeds configured item limit", "data"};
  }

  MarketsPage page;
  page.cursor = wire.cursor;
  page.markets.reserve(wire.data->size());
  for (const auto &item : *wire.data) {
    auto market = convert_market(item, limits);
    if (!market)
      return market.error();
    page.markets.push_back(std::move(market.value()));
  }
  return page;
}

Result<Market> decode_market_response(std::string_view json,
                                      const DecodeLimits &limits) {
  auto parsed = parse_wire<WireMarketResponse>(json, limits);
  if (!parsed)
    return parsed.error();
  const auto &wire = parsed.value();
  if (!wire.success)
    return missing("success");
  if (!*wire.success)
    return Error{ErrorCode::venue_rejected,
                 "Predict.fun returned success=false", "success"};
  if (!wire.data)
    return missing("data");
  return convert_market(*wire.data, limits);
}

Result<CategoriesPage> decode_categories_response(std::string_view json,
                                                  const DecodeLimits &limits) {
  auto parsed = parse_wire<WireCategoriesResponse>(json, limits);
  if (!parsed)
    return parsed.error();
  const auto &wire = parsed.value();
  if (!wire.success)
    return missing("success");
  if (!*wire.success)
    return Error{ErrorCode::venue_rejected,
                 "Predict.fun returned success=false", "success"};
  if (!wire.data)
    return missing("data");
  if (wire.data->size() > limits.max_categories) {
    return Error{ErrorCode::too_many_items,
                 "category page exceeds configured item limit", "data"};
  }
  CategoriesPage page;
  page.cursor = wire.cursor;
  page.categories.reserve(wire.data->size());
  for (const auto &item : *wire.data) {
    auto category = convert_category(item, limits);
    if (!category)
      return category.error();
    page.categories.push_back(std::move(category.value()));
  }
  return page;
}

Result<Category> decode_category_response(std::string_view json,
                                          const DecodeLimits &limits) {
  auto parsed = parse_wire<WireCategoryResponse>(json, limits);
  if (!parsed)
    return parsed.error();
  const auto &wire = parsed.value();
  if (!wire.success)
    return missing("success");
  if (!*wire.success)
    return Error{ErrorCode::venue_rejected,
                 "Predict.fun returned success=false", "success"};
  if (!wire.data)
    return missing("data");
  return convert_category(*wire.data, limits);
}

Result<Orderbook> decode_orderbook_response(std::string_view json,
                                            std::uint8_t decimal_precision,
                                            const DecodeLimits &limits) {
  if (decimal_precision > FixedDecimal::max_scale) {
    return Error{ErrorCode::unsupported_precision,
                 "book precision exceeds 18 digits", "decimalPrecision"};
  }
  auto parsed = parse_wire<WireOrderbookResponse>(json, limits);
  if (!parsed)
    return parsed.error();
  const auto &response = parsed.value();
  if (!response.success)
    return missing("success");
  if (!*response.success) {
    return Error{ErrorCode::venue_rejected,
                 "Predict.fun returned success=false", "success"};
  }
  if (!response.data)
    return missing("data");
  return convert_orderbook(*response.data, decimal_precision, limits);
}

Result<Orderbook> decode_orderbook_payload(std::string_view json,
                                           std::uint8_t decimal_precision,
                                           const DecodeLimits &limits) {
  if (decimal_precision > FixedDecimal::max_scale) {
    return Error{ErrorCode::unsupported_precision,
                 "book precision exceeds 18 digits", "decimalPrecision"};
  }
  auto parsed = parse_wire<WireOrderbook>(json, limits);
  if (!parsed)
    return parsed.error();
  return convert_orderbook(parsed.value(), decimal_precision, limits);
}

Result<TimeseriesPage> decode_timeseries_response(std::string_view json,
                                                  const DecodeLimits &limits) {
  auto parsed = parse_wire<WireTimeseriesResponse>(json, limits);
  if (!parsed)
    return parsed.error();
  const auto &wire = parsed.value();
  if (!wire.success)
    return missing("success");
  if (!*wire.success)
    return Error{ErrorCode::venue_rejected,
                 "Predict.fun returned success=false", "success"};
  if (!wire.data)
    return missing("data");
  if (!wire.data->resolution)
    return missing("data.resolution");
  if (!wire.data->series)
    return missing("data.series");
  if (!string_within_limit(*wire.data->resolution, limits))
    return invalid("timeseries resolution exceeds configured limit",
                   "data.resolution");
  if (wire.data->series->size() > limits.max_timeseries_points) {
    return Error{ErrorCode::too_many_items,
                 "timeseries exceeds configured point limit", "data.series"};
  }
  TimeseriesPage page;
  page.cursor = wire.cursor;
  page.resolution = *wire.data->resolution;
  page.points.reserve(wire.data->series->size());
  std::int64_t previous = -1;
  for (std::size_t i = 0; i < wire.data->series->size(); ++i) {
    auto point = convert_timeseries_point(
        (*wire.data->series)[i], "data.series[" + std::to_string(i) + "]");
    if (!point)
      return point.error();
    if (point.value().timestamp_ms <= previous) {
      return Error{ErrorCode::invalid_field,
                   "timeseries timestamps must be strictly increasing",
                   "data.series"};
    }
    previous = point.value().timestamp_ms;
    page.points.push_back(std::move(point.value()));
  }
  return page;
}

Result<TimeseriesPoint>
decode_latest_timeseries_response(std::string_view json,
                                  const DecodeLimits &limits) {
  auto parsed = parse_wire<WireTimeseriesLatestResponse>(json, limits);
  if (!parsed)
    return parsed.error();
  const auto &wire = parsed.value();
  if (!wire.success)
    return missing("success");
  if (!*wire.success)
    return Error{ErrorCode::venue_rejected,
                 "Predict.fun returned success=false", "success"};
  if (!wire.data)
    return missing("data");
  return convert_timeseries_point(*wire.data, "data");
}

} // namespace predictfun::codec
