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
  std::optional<glz::raw_json> team;
  std::optional<glz::raw_json> variantData;
  std::optional<glz::raw_json> variantDetails;
};

struct WireVariantData {
  std::optional<std::string> type;
  std::optional<std::string> priceFeedProvider;
  std::optional<std::string> priceFeedSymbol;
  std::optional<std::string> priceFeedId;
  std::optional<glz::raw_json> startPrice;
  std::optional<glz::raw_json> endPrice;
};

struct WireTag {
  std::optional<std::string> id;
  std::optional<std::string> name;
  std::optional<std::int32_t> level;
  std::optional<std::string> parentId;
  std::optional<std::int32_t> makerRebateBps;
};

struct WireRewardPeriod {
  std::optional<std::int32_t> hourlyRate;
  std::optional<std::string> startsAt;
  std::optional<std::string> endsAt;
};

struct WireMarketRewards {
  std::optional<WireRewardPeriod> current;
  std::optional<std::vector<WireRewardPeriod>> schedule;
};

struct WireMarketStatistics {
  std::optional<glz::raw_json> totalLiquidityUsd;
  std::optional<glz::raw_json> liquidity3cAskUsd;
  std::optional<glz::raw_json> volumeTotalUsd;
  std::optional<glz::raw_json> volume24hUsd;
};

struct WireCategoryStatistics {
  std::optional<glz::raw_json> totalLiquidityUsd;
  std::optional<glz::raw_json> volumeTotalUsd;
  std::optional<glz::raw_json> volume24hUsd;
  std::optional<std::uint64_t> holdersCount;
};

struct WireMarket {
  std::optional<std::uint64_t> id;
  std::optional<std::string> imageUrl;
  std::optional<std::string> title;
  std::optional<std::string> question;
  std::optional<std::string> description;
  std::optional<std::string> conditionId;
  std::optional<std::uint64_t> questionIndex;
  std::optional<std::string> tradingStatus;
  std::optional<std::string> status;
  std::optional<bool> isVisible;
  std::optional<std::uint32_t> decimalPrecision;
  std::optional<bool> isNegRisk;
  std::optional<bool> isYieldBearing;
  std::optional<std::uint32_t> feeRateBps;
  std::optional<WireOutcome> resolution;
  std::optional<std::string> oracleQuestionId;
  std::optional<std::string> categorySlug;
  std::optional<std::string> resolverAddress;
  std::optional<glz::raw_json> spreadThreshold;
  std::optional<glz::raw_json> shareThreshold;
  std::optional<bool> isBoosted;
  std::optional<std::string> boostStartsAt;
  std::optional<std::string> boostEndsAt;
  std::optional<std::vector<std::string>> polymarketConditionIds;
  std::optional<std::string> kalshiMarketTicker;
  std::optional<std::string> createdAt;
  std::optional<std::string> marketVariant;
  std::optional<WireVariantData> variantData;
  std::optional<glz::raw_json> variantDetails;
  std::optional<glz::raw_json> team;
  std::optional<std::string> marketType;
  std::optional<WireMarketRewards> rewards;
  std::optional<WireMarketStatistics> stats;
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
  std::optional<std::string> imageUrl;
  std::optional<std::string> startsAt;
  std::optional<std::string> endsAt;
  std::optional<std::string> createdAt;
  std::optional<std::string> publishedAt;
  std::optional<std::string> marketVariant;
  std::optional<std::string> negRiskOnChainId;
  std::optional<WireVariantData> variantData;
  std::optional<glz::raw_json> variantDetails;
  std::optional<bool> isNegRisk;
  std::optional<bool> isYieldBearing;
  std::optional<bool> isVisible;
  std::optional<std::string> status;
  std::optional<std::string> resolutionProvider;
  std::optional<std::string> parentSlug;
  std::optional<WireCategoryStatistics> stats;
  std::optional<glz::raw_json> teams;
  std::optional<std::vector<WireTag>> tags;
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

struct WireMatchFee {
  std::optional<glz::raw_json> amount;
  std::optional<std::string> type;
};

struct WirePrivateOutcome {
  std::optional<std::string> name;
  std::optional<std::uint64_t> indexSet;
  std::optional<glz::raw_json> onChainId;
};

struct WireMatchLeg {
  std::optional<std::string> quoteType;
  std::optional<glz::raw_json> amount;
  std::optional<glz::raw_json> price;
  std::optional<glz::raw_json> outcome;
  std::optional<std::string> signer;
  std::optional<WireMatchFee> fee;
};

struct WireMatch {
  std::optional<glz::raw_json> market;
  std::optional<WireMatchLeg> taker;
  std::optional<glz::raw_json> amountFilled;
  std::optional<glz::raw_json> priceExecuted;
  std::optional<std::vector<WireMatchLeg>> makers;
  std::optional<std::string> transactionHash;
  std::optional<std::string> executedAt;
};

struct WireMatchesResponse {
  std::optional<bool> success;
  std::optional<std::string> cursor;
  std::optional<std::vector<WireMatch>> data;
};

struct WireTagsResponse {
  std::optional<bool> success;
  std::optional<std::vector<WireTag>> data;
};

struct WireMarketStatisticsResponse {
  std::optional<bool> success;
  std::optional<WireMarketStatistics> data;
};

struct WireMarketLastSale {
  std::optional<std::string> quoteType;
  std::optional<std::string> outcome;
  std::optional<glz::raw_json> priceInCurrency;
  std::optional<std::string> strategy;
};

struct WireMarketLastSaleResponse {
  std::optional<bool> success;
  glz::raw_json data;
};

struct WireSearchData {
  std::optional<std::vector<WireCategory>> categories;
  std::optional<std::vector<WireMarket>> markets;
};

struct WireSearchResponse {
  std::optional<bool> success;
  std::optional<WireSearchData> data;
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
  if (raw == "PRICE_PROPOSED")
    return enum_value(MarketStatus::price_proposed, raw);
  if (raw == "PRICE_DISPUTED")
    return enum_value(MarketStatus::price_disputed, raw);
  if (raw == "PAUSED")
    return enum_value(MarketStatus::paused, raw);
  if (raw == "UNPAUSED")
    return enum_value(MarketStatus::unpaused, raw);
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

EnumValue<LastSaleQuoteType>
parse_last_sale_quote_type(const std::string &raw) {
  if (raw == "Bid" || raw == "BID")
    return enum_value(LastSaleQuoteType::bid, raw);
  if (raw == "Ask" || raw == "ASK")
    return enum_value(LastSaleQuoteType::ask, raw);
  return enum_value(LastSaleQuoteType::unknown, raw);
}

EnumValue<LastSaleOutcome> parse_last_sale_outcome(const std::string &raw) {
  if (raw == "Yes" || raw == "YES")
    return enum_value(LastSaleOutcome::yes, raw);
  if (raw == "No" || raw == "NO")
    return enum_value(LastSaleOutcome::no, raw);
  return enum_value(LastSaleOutcome::unknown, raw);
}

EnumValue<LastSaleStrategy> parse_last_sale_strategy(const std::string &raw) {
  if (raw == "MARKET")
    return enum_value(LastSaleStrategy::market, raw);
  if (raw == "LIMIT")
    return enum_value(LastSaleStrategy::limit, raw);
  return enum_value(LastSaleStrategy::unknown, raw);
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

Result<ExactDecimal> parse_exact_raw(const glz::raw_json &raw,
                                     std::string field) {
  std::string unquoted;
  const auto text = unquote_raw(raw, unquoted);
  auto parsed = ExactDecimal::parse(text);
  if (!parsed) {
    auto error = parsed.error();
    error.field = std::move(field);
    return error;
  }
  return parsed.value();
}

Result<std::optional<std::string>>
bounded_raw_json(const std::optional<glz::raw_json> &raw,
                 const DecodeLimits &limits, std::string field) {
  if (!raw)
    return std::optional<std::string>{};
  const auto text = std::string_view{raw->str};
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos || text.substr(first, 4U) == "null")
    return std::optional<std::string>{};
  if (text.size() > limits.max_embedded_json_bytes)
    return Error{ErrorCode::body_too_large,
                 "embedded JSON exceeds configured limit", std::move(field)};
  return std::optional<std::string>{std::string{text}};
}

Result<MarketStatistics>
convert_market_statistics(const WireMarketStatistics &wire,
                          std::string prefix) {
  if (!wire.totalLiquidityUsd)
    return missing(prefix + ".totalLiquidityUsd");
  if (!wire.volumeTotalUsd)
    return missing(prefix + ".volumeTotalUsd");
  if (!wire.volume24hUsd)
    return missing(prefix + ".volume24hUsd");
  auto liquidity =
      parse_exact_raw(*wire.totalLiquidityUsd, prefix + ".totalLiquidityUsd");
  auto total =
      parse_exact_raw(*wire.volumeTotalUsd, prefix + ".volumeTotalUsd");
  auto day = parse_exact_raw(*wire.volume24hUsd, prefix + ".volume24hUsd");
  if (!liquidity)
    return liquidity.error();
  if (!total)
    return total.error();
  if (!day)
    return day.error();
  MarketStatistics stats{std::move(liquidity.value()), std::move(total.value()),
                         std::move(day.value()), std::nullopt};
  if (wire.liquidity3cAskUsd) {
    auto three_cent =
        parse_exact_raw(*wire.liquidity3cAskUsd, prefix + ".liquidity3cAskUsd");
    if (!three_cent)
      return three_cent.error();
    stats.liquidity_3c_ask_usd = std::move(three_cent.value());
  }
  return stats;
}

Result<CategoryStatistics>
convert_category_statistics(const WireCategoryStatistics &wire,
                            std::string prefix) {
  if (!wire.totalLiquidityUsd)
    return missing(prefix + ".totalLiquidityUsd");
  if (!wire.volumeTotalUsd)
    return missing(prefix + ".volumeTotalUsd");
  if (!wire.volume24hUsd)
    return missing(prefix + ".volume24hUsd");
  if (!wire.holdersCount)
    return missing(prefix + ".holdersCount");
  auto liquidity =
      parse_exact_raw(*wire.totalLiquidityUsd, prefix + ".totalLiquidityUsd");
  auto total =
      parse_exact_raw(*wire.volumeTotalUsd, prefix + ".volumeTotalUsd");
  auto day = parse_exact_raw(*wire.volume24hUsd, prefix + ".volume24hUsd");
  if (!liquidity)
    return liquidity.error();
  if (!total)
    return total.error();
  if (!day)
    return day.error();
  return CategoryStatistics{std::move(liquidity.value()),
                            std::move(total.value()), std::move(day.value()),
                            *wire.holdersCount};
}

Result<PrivateOutcome> convert_private_outcome(const glz::raw_json &raw,
                                               std::string prefix) {
  WirePrivateOutcome wire;
  const auto error = glz::read<read_options>(wire, std::string_view{raw.str});
  if (error)
    return invalid("match outcome contains malformed JSON", std::move(prefix));
  PrivateOutcome outcome;
  outcome.name = wire.name;
  outcome.index_set = wire.indexSet;
  if (wire.onChainId) {
    std::string unquoted;
    const auto text = unquote_raw(*wire.onChainId, unquoted);
    auto id = Uint256::parse(text);
    if (!id) {
      auto parse_error = id.error();
      parse_error.field = prefix + ".onChainId";
      return parse_error;
    }
    outcome.on_chain_id = std::move(id.value());
  }
  return outcome;
}

Result<MatchOrderLeg> convert_match_leg(const WireMatchLeg &wire,
                                        std::string prefix) {
  if (!wire.quoteType)
    return missing(prefix + ".quoteType");
  if (!wire.amount)
    return missing(prefix + ".amount");
  if (!wire.price)
    return missing(prefix + ".price");
  if (!wire.outcome)
    return missing(prefix + ".outcome");
  if (!wire.signer)
    return missing(prefix + ".signer");
  auto amount = parse_exact_raw(*wire.amount, prefix + ".amount");
  auto price = parse_exact_raw(*wire.price, prefix + ".price");
  auto outcome = convert_private_outcome(*wire.outcome, prefix + ".outcome");
  auto signer = EvmAddress::parse(*wire.signer);
  if (!amount)
    return amount.error();
  if (!price)
    return price.error();
  if (!outcome)
    return outcome.error();
  if (!signer) {
    auto error = signer.error();
    error.field = prefix + ".signer";
    return error;
  }
  MatchOrderLeg leg{*wire.quoteType,          std::move(amount.value()),
                    std::move(price.value()), std::move(outcome.value()),
                    signer.value(),           {}};
  if (wire.fee) {
    if (!wire.fee->amount)
      return missing(prefix + ".fee.amount");
    if (!wire.fee->type)
      return missing(prefix + ".fee.type");
    auto fee = parse_exact_raw(*wire.fee->amount, prefix + ".fee.amount");
    if (!fee)
      return fee.error();
    leg.fee = MatchFee{std::move(fee.value()), *wire.fee->type};
  }
  return leg;
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

Result<std::optional<CryptoUpDownVariantData>>
convert_crypto_variant(const std::optional<WireVariantData> &wire,
                       const DecodeLimits &limits, std::string field) {
  if (!wire || !wire->type || *wire->type != "CRYPTO_UP_DOWN")
    return std::optional<CryptoUpDownVariantData>{};

  const auto strings_valid =
      string_within_limit(*wire->type, limits) &&
      (!wire->priceFeedProvider ||
       string_within_limit(*wire->priceFeedProvider, limits)) &&
      (!wire->priceFeedSymbol ||
       string_within_limit(*wire->priceFeedSymbol, limits)) &&
      (!wire->priceFeedId || string_within_limit(*wire->priceFeedId, limits));
  if (!strings_valid)
    return invalid("crypto variant string exceeds configured limit", field);

  CryptoUpDownVariantData value;
  value.type = *wire->type;
  value.price_feed_provider = wire->priceFeedProvider;
  value.price_feed_symbol = wire->priceFeedSymbol;
  value.price_feed_id = wire->priceFeedId;
  if (wire->startPrice) {
    auto parsed = parse_decimal_raw(*wire->startPrice, field + ".startPrice");
    if (!parsed)
      return parsed.error();
    value.start_price = parsed.value();
  }
  if (wire->endPrice) {
    auto parsed = parse_decimal_raw(*wire->endPrice, field + ".endPrice");
    if (!parsed)
      return parsed.error();
    value.end_price = parsed.value();
  }
  return std::optional<CryptoUpDownVariantData>{std::move(value)};
}

Result<RewardPeriod> convert_reward_period(const WireRewardPeriod &wire,
                                           const DecodeLimits &limits,
                                           std::string prefix) {
  if (!wire.hourlyRate)
    return missing(prefix + ".hourlyRate");
  if (!wire.startsAt)
    return missing(prefix + ".startsAt");
  if (!wire.endsAt)
    return missing(prefix + ".endsAt");
  if (!string_within_limit(*wire.startsAt, limits) ||
      !string_within_limit(*wire.endsAt, limits))
    return invalid("reward period string exceeds configured limit",
                   std::move(prefix));
  return RewardPeriod{*wire.hourlyRate, *wire.startsAt, *wire.endsAt};
}

Result<MarketRewards> convert_rewards(const WireMarketRewards &wire,
                                      const DecodeLimits &limits,
                                      std::string prefix) {
  MarketRewards rewards;
  if (wire.current) {
    auto current =
        convert_reward_period(*wire.current, limits, prefix + ".current");
    if (!current)
      return current.error();
    rewards.current = std::move(current.value());
  }
  if (wire.schedule) {
    if (wire.schedule->size() > limits.max_reward_periods)
      return Error{ErrorCode::too_many_items,
                   "reward schedule exceeds configured item limit",
                   prefix + ".schedule"};
    rewards.schedule.reserve(wire.schedule->size());
    for (std::size_t i = 0; i < wire.schedule->size(); ++i) {
      auto period = convert_reward_period((*wire.schedule)[i], limits,
                                          prefix + ".schedule[" +
                                              std::to_string(i) + "]");
      if (!period)
        return period.error();
      rewards.schedule.push_back(std::move(period.value()));
    }
  }
  return rewards;
}

Result<Outcome> convert_outcome(const WireOutcome &wire, std::uint8_t precision,
                                const DecodeLimits &limits,
                                std::string prefix) {
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
  auto team = bounded_raw_json(wire.team, limits, prefix + ".team");
  auto variant =
      bounded_raw_json(wire.variantData, limits, prefix + ".variantData");
  auto details =
      bounded_raw_json(wire.variantDetails, limits, prefix + ".variantDetails");
  if (!team)
    return team.error();
  if (!variant)
    return variant.error();
  if (!details)
    return details.error();
  outcome.team_json = std::move(team.value());
  outcome.variant_data_json = std::move(variant.value());
  outcome.variant_details_json = std::move(details.value());
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
  if (wire.polymarketConditionIds && wire.polymarketConditionIds->size() >
                                         limits.max_polymarket_condition_ids) {
    return Error{ErrorCode::too_many_items,
                 "market contains too many Polymarket condition ids",
                 "data[].polymarketConditionIds"};
  }
  const auto optional_string_valid = [&limits](const auto &value) {
    return !value || string_within_limit(*value, limits);
  };
  if (!string_within_limit(*wire.title, limits) ||
      !string_within_limit(*wire.question, limits) ||
      !optional_string_valid(wire.imageUrl) ||
      !optional_string_valid(wire.description) ||
      !optional_string_valid(wire.conditionId) ||
      !string_within_limit(*wire.tradingStatus, limits) ||
      !string_within_limit(*wire.status, limits) ||
      !optional_string_valid(wire.oracleQuestionId) ||
      !optional_string_valid(wire.resolverAddress) ||
      !optional_string_valid(wire.boostStartsAt) ||
      !optional_string_valid(wire.boostEndsAt) ||
      !optional_string_valid(wire.kalshiMarketTicker) ||
      !optional_string_valid(wire.categorySlug) ||
      !optional_string_valid(wire.createdAt) ||
      !optional_string_valid(wire.marketVariant) ||
      !optional_string_valid(wire.marketType)) {
    return invalid("market string exceeds configured limit", "data[]");
  }
  if (wire.polymarketConditionIds &&
      std::ranges::any_of(*wire.polymarketConditionIds,
                          [&limits](const std::string &value) {
                            return !string_within_limit(value, limits);
                          }))
    return invalid("Polymarket condition id exceeds configured limit",
                   "data[].polymarketConditionIds");

  Market market;
  market.id = MarketId{*wire.id};
  market.title = *wire.title;
  market.question = *wire.question;
  market.image_url = wire.imageUrl;
  market.description = wire.description;
  market.condition_id = wire.conditionId;
  market.question_index = wire.questionIndex;
  market.trading_status = parse_trading_status(*wire.tradingStatus);
  market.status = parse_market_status(*wire.status);
  market.is_visible = wire.isVisible;
  market.decimal_precision = static_cast<std::uint8_t>(*wire.decimalPrecision);
  market.is_neg_risk = *wire.isNegRisk;
  market.is_yield_bearing = *wire.isYieldBearing;
  market.fee_rate_bps = *wire.feeRateBps;
  market.oracle_question_id = wire.oracleQuestionId;
  market.resolver_address = wire.resolverAddress;
  market.category_slug = wire.categorySlug;
  market.created_at = wire.createdAt;
  market.market_variant = wire.marketVariant;
  market.is_boosted = wire.isBoosted;
  market.boost_starts_at = wire.boostStartsAt;
  market.boost_ends_at = wire.boostEndsAt;
  if (wire.polymarketConditionIds)
    market.polymarket_condition_ids = *wire.polymarketConditionIds;
  market.kalshi_market_ticker = wire.kalshiMarketTicker;
  market.market_type = wire.marketType;
  auto market_variant =
      convert_crypto_variant(wire.variantData, limits, "data[].variantData");
  if (!market_variant)
    return market_variant.error();
  market.crypto_up_down = std::move(market_variant.value());
  if (wire.spreadThreshold) {
    auto threshold =
        parse_exact_raw(*wire.spreadThreshold, "data[].spreadThreshold");
    if (!threshold)
      return threshold.error();
    market.spread_threshold = std::move(threshold.value());
  }
  if (wire.shareThreshold) {
    auto threshold =
        parse_exact_raw(*wire.shareThreshold, "data[].shareThreshold");
    if (!threshold)
      return threshold.error();
    market.share_threshold = std::move(threshold.value());
  }
  if (wire.rewards) {
    auto rewards = convert_rewards(*wire.rewards, limits, "data[].rewards");
    if (!rewards)
      return rewards.error();
    market.rewards = std::move(rewards.value());
  }
  if (wire.stats) {
    auto stats = convert_market_statistics(*wire.stats, "data[].stats");
    if (!stats)
      return stats.error();
    market.stats = std::move(stats.value());
  }
  auto team = bounded_raw_json(wire.team, limits, "data[].team");
  auto details =
      bounded_raw_json(wire.variantDetails, limits, "data[].variantDetails");
  if (!team)
    return team.error();
  if (!details)
    return details.error();
  market.team_json = std::move(team.value());
  market.variant_details_json = std::move(details.value());
  if (wire.resolution) {
    auto resolution =
        convert_outcome(*wire.resolution, market.decimal_precision, limits,
                        "data[].resolution");
    if (!resolution)
      return resolution.error();
    market.resolution = std::move(resolution.value());
  }
  market.outcomes.reserve(wire.outcomes->size());
  for (std::size_t i = 0; i < wire.outcomes->size(); ++i) {
    auto outcome =
        convert_outcome((*wire.outcomes)[i], market.decimal_precision, limits,
                        "data[].outcomes[" + std::to_string(i) + "]");
    if (!outcome)
      return outcome.error();
    market.outcomes.push_back(std::move(outcome.value()));
  }
  return market;
}

Result<Tag> convert_tag(const WireTag &wire, const DecodeLimits &limits,
                        std::string prefix) {
  if (!wire.id)
    return missing(prefix + ".id");
  if (!wire.name)
    return missing(prefix + ".name");
  if (!string_within_limit(*wire.id, limits) ||
      !string_within_limit(*wire.name, limits) ||
      (wire.parentId && !string_within_limit(*wire.parentId, limits))) {
    return invalid("tag string exceeds configured limit", std::move(prefix));
  }
  return Tag{*wire.id, *wire.name, wire.level, wire.parentId,
             wire.makerRebateBps};
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
  if (wire.tags && wire.tags->size() > limits.max_tags) {
    return Error{ErrorCode::too_many_items, "category contains too many tags",
                 "data[].tags"};
  }
  const auto strings_valid =
      string_within_limit(*wire.slug, limits) &&
      string_within_limit(*wire.title, limits) &&
      (!wire.shortTitle || string_within_limit(*wire.shortTitle, limits)) &&
      (!wire.description || string_within_limit(*wire.description, limits)) &&
      (!wire.imageUrl || string_within_limit(*wire.imageUrl, limits)) &&
      (!wire.startsAt || string_within_limit(*wire.startsAt, limits)) &&
      (!wire.endsAt || string_within_limit(*wire.endsAt, limits)) &&
      (!wire.createdAt || string_within_limit(*wire.createdAt, limits)) &&
      (!wire.publishedAt || string_within_limit(*wire.publishedAt, limits)) &&
      (!wire.marketVariant ||
       string_within_limit(*wire.marketVariant, limits)) &&
      (!wire.negRiskOnChainId ||
       string_within_limit(*wire.negRiskOnChainId, limits)) &&
      (!wire.resolutionProvider ||
       string_within_limit(*wire.resolutionProvider, limits)) &&
      (!wire.parentSlug || string_within_limit(*wire.parentSlug, limits));
  if (!strings_valid)
    return invalid("category string exceeds configured limit", "data[]");

  Category category;
  category.id = *wire.id;
  category.slug = *wire.slug;
  category.title = *wire.title;
  category.short_title = wire.shortTitle;
  category.description = wire.description;
  category.image_url = wire.imageUrl;
  category.starts_at = wire.startsAt;
  category.ends_at = wire.endsAt;
  category.created_at = wire.createdAt;
  category.published_at = wire.publishedAt;
  category.market_variant = wire.marketVariant;
  category.neg_risk_on_chain_id = wire.negRiskOnChainId;
  auto category_variant =
      convert_crypto_variant(wire.variantData, limits, "data[].variantData");
  if (!category_variant)
    return category_variant.error();
  category.crypto_up_down = std::move(category_variant.value());
  category.is_neg_risk = *wire.isNegRisk;
  category.is_yield_bearing = *wire.isYieldBearing;
  category.is_visible = *wire.isVisible;
  category.status = parse_category_status(*wire.status);
  category.resolution_provider = wire.resolutionProvider;
  category.parent_slug = wire.parentSlug;
  if (wire.stats) {
    auto stats = convert_category_statistics(*wire.stats, "data[].stats");
    if (!stats)
      return stats.error();
    category.stats = std::move(stats.value());
  }
  auto teams = bounded_raw_json(wire.teams, limits, "data[].teams");
  auto details =
      bounded_raw_json(wire.variantDetails, limits, "data[].variantDetails");
  if (!teams)
    return teams.error();
  if (!details)
    return details.error();
  category.teams_json = std::move(teams.value());
  category.variant_details_json = std::move(details.value());
  if (wire.tags) {
    category.tags.reserve(wire.tags->size());
    for (std::size_t i = 0; i < wire.tags->size(); ++i) {
      auto tag = convert_tag((*wire.tags)[i], limits,
                             "data[].tags[" + std::to_string(i) + "]");
      if (!tag)
        return tag.error();
      category.tags.push_back(std::move(tag.value()));
    }
  }
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
    levels.push_back(std::move(level.value()));
  }
  std::ranges::sort(
      levels, [ascending](const PriceLevel &left, const PriceLevel &right) {
        return ascending ? left.price.ticks() < right.price.ticks()
                         : left.price.ticks() > right.price.ticks();
      });

  std::vector<PriceLevel> normalized;
  normalized.reserve(levels.size());
  for (auto &level : levels) {
    if (normalized.empty() ||
        normalized.back().price.ticks() != level.price.ticks()) {
      normalized.push_back(std::move(level));
      continue;
    }
    const auto scale =
        std::max(normalized.back().quantity.scale(), level.quantity.scale());
    auto left = normalized.back().quantity.rescale_exact(scale);
    auto right = level.quantity.rescale_exact(scale);
    if (!left || !right ||
        right.value().units() >
            std::numeric_limits<std::uint64_t>::max() - left.value().units()) {
      return Error{ErrorCode::invalid_quantity,
                   "duplicate book quantity aggregation overflowed", field};
    }
    normalized.back().quantity =
        FixedDecimal{left.value().units() + right.value().units(), scale};
  }
  return normalized;
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

  auto asks =
      convert_levels(*wire.asks, decimal_precision, limits, true, "data.asks");
  if (!asks)
    return asks.error();
  auto bids =
      convert_levels(*wire.bids, decimal_precision, limits, false, "data.bids");
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
      auto pending_asks =
          convert_levels(*levels.asks, decimal_precision, limits, true,
                         "data.settlementsPending.asks");
      if (!pending_asks)
        return pending_asks.error();
      auto pending_bids =
          convert_levels(*levels.bids, decimal_precision, limits, false,
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

Result<MatchesPage> decode_matches_response(std::string_view json,
                                            const DecodeLimits &limits) {
  auto parsed = parse_wire<WireMatchesResponse>(json, limits);
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
  if (wire.data->size() > limits.max_matches)
    return Error{ErrorCode::too_many_items,
                 "match page exceeds configured item limit", "data"};
  MatchesPage page{wire.cursor, {}};
  page.matches.reserve(wire.data->size());
  for (std::size_t index = 0; index < wire.data->size(); ++index) {
    const auto &item = (*wire.data)[index];
    const auto prefix = "data[" + std::to_string(index) + "]";
    if (!item.market)
      return missing(prefix + ".market");
    if (!item.taker)
      return missing(prefix + ".taker");
    if (!item.amountFilled)
      return missing(prefix + ".amountFilled");
    if (!item.priceExecuted)
      return missing(prefix + ".priceExecuted");
    if (!item.makers)
      return missing(prefix + ".makers");
    if (!item.transactionHash)
      return missing(prefix + ".transactionHash");
    if (!item.executedAt)
      return missing(prefix + ".executedAt");
    if (item.makers->size() > limits.max_makers_per_match)
      return Error{ErrorCode::too_many_items, "too many maker legs",
                   prefix + ".makers"};
    std::string market_envelope{R"({"success":true,"data":)"};
    market_envelope.append(item.market->str);
    market_envelope.push_back('}');
    auto market = decode_market_response(market_envelope, limits);
    auto taker = convert_match_leg(*item.taker, prefix + ".taker");
    auto amount = parse_exact_raw(*item.amountFilled, prefix + ".amountFilled");
    auto price =
        parse_exact_raw(*item.priceExecuted, prefix + ".priceExecuted");
    if (!market)
      return market.error();
    if (!taker)
      return taker.error();
    if (!amount)
      return amount.error();
    if (!price)
      return price.error();
    MatchEvent event{std::move(market.value()),
                     std::move(taker.value()),
                     std::move(amount.value()),
                     std::move(price.value()),
                     {},
                     *item.transactionHash,
                     *item.executedAt};
    event.makers.reserve(item.makers->size());
    for (std::size_t maker_index = 0; maker_index < item.makers->size();
         ++maker_index) {
      auto maker = convert_match_leg((*item.makers)[maker_index],
                                     prefix + ".makers[" +
                                         std::to_string(maker_index) + "]");
      if (!maker)
        return maker.error();
      event.makers.push_back(std::move(maker.value()));
    }
    page.matches.push_back(std::move(event));
  }
  return page;
}

Result<std::vector<Tag>> decode_tags_response(std::string_view json,
                                              const DecodeLimits &limits) {
  auto parsed = parse_wire<WireTagsResponse>(json, limits);
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
  if (wire.data->size() > limits.max_tags)
    return Error{ErrorCode::too_many_items,
                 "tag list exceeds configured item limit", "data"};

  std::vector<Tag> tags;
  tags.reserve(wire.data->size());
  for (std::size_t i = 0; i < wire.data->size(); ++i) {
    auto tag =
        convert_tag((*wire.data)[i], limits, "data[" + std::to_string(i) + "]");
    if (!tag)
      return tag.error();
    tags.push_back(std::move(tag.value()));
  }
  return tags;
}

Result<MarketStatistics>
decode_market_statistics_response(std::string_view json,
                                  const DecodeLimits &limits) {
  auto parsed = parse_wire<WireMarketStatisticsResponse>(json, limits);
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
  return convert_market_statistics(*wire.data, "data");
}

Result<std::optional<MarketLastSale>>
decode_market_last_sale_response(std::string_view json,
                                 const DecodeLimits &limits) {
  auto parsed = parse_wire<WireMarketLastSaleResponse>(json, limits);
  if (!parsed)
    return parsed.error();
  const auto &wire = parsed.value();
  if (!wire.success)
    return missing("success");
  if (!*wire.success)
    return Error{ErrorCode::venue_rejected,
                 "Predict.fun returned success=false", "success"};
  if (wire.data.str.empty())
    return missing("data");

  const auto raw = std::string_view{wire.data.str};
  const auto first = raw.find_first_not_of(" \t\r\n");
  if (first != std::string_view::npos && raw.substr(first, 4U) == "null")
    return std::optional<MarketLastSale>{};

  WireMarketLastSale sale;
  const auto parse_error = glz::read<read_options>(sale, raw);
  if (parse_error)
    return invalid("last sale contains malformed JSON", "data");
  if (!sale.quoteType)
    return missing("data.quoteType");
  if (!sale.outcome)
    return missing("data.outcome");
  if (!sale.priceInCurrency)
    return missing("data.priceInCurrency");
  if (!sale.strategy)
    return missing("data.strategy");
  if (!string_within_limit(*sale.quoteType, limits) ||
      !string_within_limit(*sale.outcome, limits) ||
      !string_within_limit(*sale.strategy, limits))
    return invalid("last sale string exceeds configured limit", "data");
  auto price = parse_exact_raw(*sale.priceInCurrency, "data.priceInCurrency");
  if (!price)
    return price.error();
  return std::optional<MarketLastSale>{MarketLastSale{
      parse_last_sale_quote_type(*sale.quoteType),
      parse_last_sale_outcome(*sale.outcome), std::move(price.value()),
      parse_last_sale_strategy(*sale.strategy)}};
}

Result<SearchResults> decode_search_response(std::string_view json,
                                             const DecodeLimits &limits) {
  auto parsed = parse_wire<WireSearchResponse>(json, limits);
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
  if (!wire.data->categories)
    return missing("data.categories");
  if (!wire.data->markets)
    return missing("data.markets");
  if (wire.data->categories->size() > limits.max_search_results_per_type)
    return Error{ErrorCode::too_many_items,
                 "search category results exceed configured item limit",
                 "data.categories"};
  if (wire.data->markets->size() > limits.max_search_results_per_type)
    return Error{ErrorCode::too_many_items,
                 "search market results exceed configured item limit",
                 "data.markets"};

  SearchResults results;
  results.categories.reserve(wire.data->categories->size());
  results.markets.reserve(wire.data->markets->size());
  for (const auto &item : *wire.data->categories) {
    auto category = convert_category(item, limits);
    if (!category)
      return category.error();
    results.categories.push_back(std::move(category.value()));
  }
  for (const auto &item : *wire.data->markets) {
    auto market = convert_market(item, limits);
    if (!market)
      return market.error();
    results.markets.push_back(std::move(market.value()));
  }
  return results;
}

} // namespace predictfun::codec
