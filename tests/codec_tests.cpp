#include "predictfun/codec/public_rest.hpp"
#include "predictfun/types/decimal.hpp"
#include "predictfun/types/orderbook.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": CHECK failed: " #condition << '\n';                      \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

std::string fixture(std::string_view name) {
  std::ifstream input(std::string{PREDICTFUN_FIXTURE_DIR} + "/" +
                          std::string{name},
                      std::ios::binary);
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

void test_decimal() {
  using predictfun::FixedDecimal;
  using predictfun::Price;

  auto decimal = FixedDecimal::parse("4108.2400");
  CHECK(decimal);
  CHECK(decimal.value().units() == 410824U);
  CHECK(decimal.value().scale() == 2U);
  CHECK(decimal.value().to_string() == "4108.24");

  auto exponent = FixedDecimal::parse("1.25e2");
  CHECK(exponent);
  CHECK(exponent.value().to_string() == "125");

  auto tiny = FixedDecimal::parse("0.000000000000000001");
  CHECK(tiny);
  CHECK(tiny.value().units() == 1U);
  CHECK(tiny.value().scale() == 18U);

  CHECK(!FixedDecimal::parse("-1"));
  CHECK(!FixedDecimal::parse("1.0.0"));
  CHECK(!FixedDecimal::parse("0.0000000000000000001"));

  auto price = Price::parse("0.62", 2);
  CHECK(price);
  CHECK(price.value().ticks() == 62U);
  CHECK(price.value().complement().ticks() == 38U);
  CHECK(price.value().complement().to_string() == "0.38");
  CHECK(!Price::parse("0.621", 2));
  CHECK(!Price::parse("1.01", 2));
}

void test_markets_fixture() {
  auto result =
      predictfun::codec::decode_markets_response(fixture("markets_open.json"));
  CHECK(result);
  if (!result)
    return;
  const auto &page = result.value();
  CHECK(page.cursor && *page.cursor == "sanitized-next-page");
  CHECK(page.markets.size() == 1U);
  const auto &market = page.markets.front();
  CHECK(market.id.value == 424242U);
  CHECK(market.condition_id &&
        *market.condition_id ==
            "0x1111111111111111111111111111111111111111111111111111111111111111");
  CHECK(market.question_index && *market.question_index == 7U);
  CHECK(market.trading_status.value == predictfun::TradingStatus::open);
  CHECK(market.status.value == predictfun::MarketStatus::registered);
  CHECK(market.decimal_precision == 2U);
  CHECK(market.outcomes.size() == 2U);
  CHECK(market.outcomes[0].best_ask.has_value());
  CHECK(market.outcomes[0].best_ask->price.ticks() == 62U);
  CHECK(market.outcomes[1].status.has_value());
  CHECK(market.outcomes[1].status->value == predictfun::OutcomeStatus::unknown);
  CHECK(market.outcomes[1].status->raw == "FUTURE_STATUS");
}

void test_crypto_category_fixture() {
  auto result = predictfun::codec::decode_categories_response(
      fixture("categories_crypto.json"));
  CHECK(result);
  if (!result)
    return;
  const auto &page = result.value();
  CHECK(page.cursor && *page.cursor == "next-crypto-page");
  CHECK(page.categories.size() == 1U);
  const auto &category = page.categories.front();
  CHECK(category.starts_at &&
        *category.starts_at == "2026-08-13T07:00:00.000Z");
  CHECK(category.ends_at &&
        *category.ends_at == "2026-08-13T07:05:00.000Z");
  CHECK(category.market_variant &&
        *category.market_variant == "CRYPTO_UP_DOWN");
  CHECK(category.neg_risk_on_chain_id &&
        *category.neg_risk_on_chain_id ==
            "0x2222222222222222222222222222222222222222222222222222222222222222");
  CHECK(category.crypto_up_down.has_value());
  CHECK(category.crypto_up_down->price_feed_symbol &&
        *category.crypto_up_down->price_feed_symbol == "BTCUSDT");
  CHECK(!category.crypto_up_down->start_price.has_value());
  CHECK(category.crypto_up_down->end_price &&
        category.crypto_up_down->end_price->to_string() == "65000.25");
  CHECK(category.markets.size() == 1U);
  const auto &market = category.markets.front();
  CHECK(market.category_slug && *market.category_slug == category.slug);
  CHECK(market.condition_id &&
        *market.condition_id ==
            "0x3333333333333333333333333333333333333333333333333333333333333333");
  CHECK(market.question_index && *market.question_index == 11U);
  CHECK(market.market_variant &&
        *market.market_variant == "CRYPTO_UP_DOWN");
  CHECK(market.crypto_up_down && market.crypto_up_down->price_feed_id &&
        *market.crypto_up_down->price_feed_id == "sanitized-feed-id");
  CHECK(market.outcomes[0].name == "Up");
  CHECK(market.outcomes[1].name == "Down");
}

void test_orderbook_fixture_and_no_view() {
  auto result = predictfun::codec::decode_orderbook_response(
      fixture("orderbook.json"), 2);
  CHECK(result);
  if (!result)
    return;
  const auto &book = result.value();
  CHECK(book.market_id.value == 424242U);
  CHECK(book.yes_asks.size() == 2U);
  CHECK(book.yes_bids.size() == 2U);
  CHECK(book.yes_asks[0].price.ticks() == 62U);
  CHECK(book.yes_bids[0].price.ticks() == 61U);
  CHECK(book.yes_asks[0].quantity.to_string() == "18.75");
  CHECK(book.last_order_settled.has_value());
  CHECK(book.last_order_settled->kind.value == predictfun::OrderKind::limit);
  CHECK(book.settlements_pending.has_value());
  CHECK(book.settlements_pending->to_string() == "1.25");

  const auto no = predictfun::derive_no_book(book);
  CHECK(no.no_bids.size() == 2U);
  CHECK(no.no_asks.size() == 2U);
  CHECK(no.no_bids[0].price.ticks() == 38U);
  CHECK(no.no_bids[1].price.ticks() == 30U);
  CHECK(no.no_asks[0].price.ticks() == 39U);
  CHECK(no.no_asks[1].price.ticks() == 45U);
  CHECK(no.no_bids[0].quantity == book.yes_asks[0].quantity);
}

void test_empty_and_string_numeric_book() {
  constexpr auto json = R"({
        "success": true,
        "data": {
            "marketId": 7,
            "updateTimestampMs": 1780000000999,
            "asks": [["0.010", "4108.2400"]],
            "bids": [],
            "lastOrderSettled": null
        }
    })";
  auto result = predictfun::codec::decode_orderbook_response(json, 3);
  CHECK(result);
  if (!result)
    return;
  CHECK(result.value().yes_asks[0].price.ticks() == 10U);
  CHECK(result.value().yes_asks[0].quantity.to_string() == "4108.24");
  CHECK(result.value().yes_bids.empty());
  CHECK(predictfun::derive_no_book(result.value()).no_asks.empty());
}

void test_rejections_and_bounds() {
  constexpr auto missing =
      R"({"success":true,"data":{"marketId":1,"asks":[],"bids":[]}})";
  auto missing_result =
      predictfun::codec::decode_orderbook_response(missing, 2);
  CHECK(!missing_result);
  CHECK(missing_result.error().code == predictfun::ErrorCode::missing_field);

  constexpr auto bad_tick =
      R"({"success":true,"data":{"marketId":1,"updateTimestampMs":1,"asks":[[0.011,1]],"bids":[]}})";
  CHECK(!predictfun::codec::decode_orderbook_response(bad_tick, 2));

  constexpr auto negative_quantity =
      R"({"success":true,"data":{"marketId":1,"updateTimestampMs":1,"asks":[[0.10,-1]],"bids":[]}})";
  CHECK(!predictfun::codec::decode_orderbook_response(negative_quantity, 2));

  constexpr auto unsorted =
      R"({"success":true,"data":{"marketId":1,"updateTimestampMs":1,"asks":[[0.20,1],[0.10,1]],"bids":[]}})";
  const auto sorted = predictfun::codec::decode_orderbook_response(unsorted, 2);
  CHECK(sorted);
  CHECK(sorted && sorted.value().yes_asks.size() == 2U &&
        sorted.value().yes_asks[0].price.ticks() == 10U &&
        sorted.value().yes_asks[1].price.ticks() == 20U);

  constexpr auto duplicated =
      R"({"success":true,"data":{"marketId":1,"updateTimestampMs":1,"asks":[[0.20,"1.25"],[0.10,1],[0.20,"2.5"]],"bids":[]}})";
  const auto merged = predictfun::codec::decode_orderbook_response(duplicated, 2);
  CHECK(merged);
  CHECK(merged && merged.value().yes_asks.size() == 2U &&
        merged.value().yes_asks[1].price.ticks() == 20U &&
        merged.value().yes_asks[1].quantity.to_string() == "3.75");

  constexpr auto crossed =
      R"({"success":true,"data":{"marketId":1,"updateTimestampMs":1,"asks":[[0.50,1]],"bids":[[0.50,1]]}})";
  CHECK(!predictfun::codec::decode_orderbook_response(crossed, 2));

  constexpr auto too_many =
      R"({"success":true,"data":{"marketId":1,"updateTimestampMs":1,"asks":[[0.50,1]],"bids":[]}})";
  predictfun::codec::DecodeLimits limits;
  limits.max_book_levels_per_side = 0;
  auto bounded =
      predictfun::codec::decode_orderbook_response(too_many, 2, limits);
  CHECK(!bounded);
  CHECK(bounded.error().code == predictfun::ErrorCode::too_many_items);

  predictfun::codec::DecodeLimits tiny_body;
  tiny_body.max_body_bytes = 8;
  auto body =
      predictfun::codec::decode_orderbook_response(too_many, 2, tiny_body);
  CHECK(!body);
  CHECK(body.error().code == predictfun::ErrorCode::body_too_large);

  CHECK(!predictfun::codec::decode_markets_response("{not-json"));
  CHECK(!predictfun::codec::decode_markets_response(
      R"({"success":false,"data":[]})"));
}

void test_unknown_top_level_status() {
  constexpr auto json = R"({
      "success": true,
      "data": [{
        "id": 9,
        "title": "Future state",
        "question": "Future?",
        "tradingStatus": "PAUSED_BY_FUTURE_RULE",
        "status": "FUTURE_LIFECYCLE",
        "decimalPrecision": 2,
        "isNegRisk": false,
        "isYieldBearing": false,
        "feeRateBps": 0,
        "outcomes": []
      }]
    })";
  auto result = predictfun::codec::decode_markets_response(json);
  CHECK(result);
  if (!result)
    return;
  CHECK(result.value().markets[0].trading_status.value ==
        predictfun::TradingStatus::unknown);
  CHECK(result.value().markets[0].trading_status.raw ==
        "PAUSED_BY_FUTURE_RULE");
  CHECK(result.value().markets[0].status.value ==
        predictfun::MarketStatus::unknown);
}

void test_category_and_timeseries_responses() {
  constexpr auto category_json = R"({
    "success": true,
    "data": {
      "id": 8,
      "slug": "crypto",
      "title": "Crypto",
      "shortTitle": "Crypto",
      "description": "Digital asset markets",
      "isNegRisk": false,
      "isYieldBearing": false,
      "isVisible": true,
      "status": "OPEN",
      "markets": []
    }
  })";
  auto category = predictfun::codec::decode_category_response(category_json);
  CHECK(category);
  if (category) {
    CHECK(category.value().id == 8U);
    CHECK(category.value().slug == "crypto");
    CHECK(category.value().is_visible);
    CHECK(category.value().status.value == predictfun::CategoryStatus::open);
  }

  constexpr auto categories_json = R"({
    "success": true,
    "cursor": "next",
    "data": [{
      "id": 8,
      "slug": "crypto",
      "title": "Crypto",
      "isNegRisk": false,
      "isYieldBearing": false,
      "isVisible": true,
      "status": "FUTURE_STATUS"
    }]
  })";
  auto categories =
      predictfun::codec::decode_categories_response(categories_json);
  CHECK(categories);
  if (categories) {
    CHECK(categories.value().categories.size() == 1U);
    CHECK(categories.value().categories[0].status.value ==
          predictfun::CategoryStatus::unknown);
    CHECK(categories.value().categories[0].status.raw == "FUTURE_STATUS");
  }

  constexpr auto timeseries_json = R"({
    "success": true,
    "cursor": "more",
    "data": {
      "resolution": "1m",
      "series": [
        {"x": 1780000000000, "y": "0.5125"},
        {"x": 1780000060000, "y": 0.53}
      ]
    }
  })";
  auto timeseries =
      predictfun::codec::decode_timeseries_response(timeseries_json);
  CHECK(timeseries);
  if (timeseries) {
    CHECK(timeseries.value().resolution == "1m");
    CHECK(timeseries.value().points.size() == 2U);
    CHECK(timeseries.value().points[0].timestamp_ms == 1780000000000LL);
    CHECK(timeseries.value().points[0].value.to_string() == "0.5125");
  }

  constexpr auto latest_json =
      R"({"success":true,"data":{"x":1780000060000,"y":"0.53"}})";
  auto latest =
      predictfun::codec::decode_latest_timeseries_response(latest_json);
  CHECK(latest);
  if (latest)
    CHECK(latest.value().value.to_string() == "0.53");

  predictfun::codec::DecodeLimits limits;
  limits.max_timeseries_points = 1U;
  auto bounded =
      predictfun::codec::decode_timeseries_response(timeseries_json, limits);
  CHECK(!bounded);
  CHECK(bounded.error().code == predictfun::ErrorCode::too_many_items);
}

void test_discovery_responses() {
  constexpr auto tags_json = R"({
    "success": true,
    "data": [{
      "id": "18446744073709551616000000000000000000",
      "name": "Crypto",
      "level": 2,
      "parentId": null,
      "makerRebateBps": 15
    }]
  })";
  auto tags = predictfun::codec::decode_tags_response(tags_json);
  CHECK(tags);
  CHECK(tags && tags.value().size() == 1U);
  CHECK(tags && tags.value()[0].id ==
                    "18446744073709551616000000000000000000");
  CHECK(tags && tags.value()[0].maker_rebate_bps == 15);

  constexpr auto stats_json = R"({
    "success": true,
    "data": {
      "totalLiquidityUsd": "12345678901234567890.125",
      "volumeTotalUsd": 456.75,
      "volume24hUsd": "12.50"
    }
  })";
  auto stats =
      predictfun::codec::decode_market_statistics_response(stats_json);
  CHECK(stats);
  CHECK(stats && stats.value().total_liquidity_usd.to_string() ==
                     "12345678901234567890.125");
  CHECK(stats && stats.value().volume_total_usd.to_string() == "456.75");

  constexpr auto sale_json = R"({
    "success": true,
    "data": {
      "quoteType": "Ask",
      "outcome": "Yes",
      "priceInCurrency": "0.625",
      "strategy": "LIMIT"
    }
  })";
  auto sale = predictfun::codec::decode_market_last_sale_response(sale_json);
  CHECK(sale);
  CHECK(sale && sale.value().has_value());
  CHECK(sale && sale.value() &&
        sale.value()->quote_type.value == predictfun::LastSaleQuoteType::ask);
  CHECK(sale && sale.value() &&
        sale.value()->price_in_currency.to_string() == "0.625");
  auto no_sale = predictfun::codec::decode_market_last_sale_response(
      R"({"success":true,"data":null})");
  CHECK(no_sale);
  CHECK(no_sale && !no_sale.value().has_value());

  constexpr auto search_json = R"({
    "success": true,
    "data": {
      "categories": [{
        "id": 8,
        "slug": "crypto",
        "title": "Crypto",
        "imageUrl": "https://example.invalid/crypto.png",
        "isNegRisk": false,
        "isYieldBearing": false,
        "isVisible": true,
        "status": "OPEN",
        "tags": [{"id":"9","name":"BTC","level":1}],
        "markets": []
      }],
      "markets": [{
        "id": 42,
        "title": "BTC Up or Down",
        "question": "Will BTC finish up?",
        "tradingStatus": "OPEN",
        "status": "REGISTERED",
        "decimalPrecision": 2,
        "isNegRisk": false,
        "isYieldBearing": false,
        "feeRateBps": 0,
        "outcomes": []
      }]
    }
  })";
  auto search = predictfun::codec::decode_search_response(search_json);
  CHECK(search);
  CHECK(search && search.value().categories.size() == 1U);
  CHECK(search && search.value().markets.size() == 1U);
  CHECK(search && search.value().categories[0].image_url.has_value());
  CHECK(search && search.value().categories[0].tags.size() == 1U);

  predictfun::codec::DecodeLimits limits;
  limits.max_tags = 0U;
  auto bounded = predictfun::codec::decode_tags_response(tags_json, limits);
  CHECK(!bounded);
  CHECK(bounded.error().code == predictfun::ErrorCode::too_many_items);
}

} // namespace

int main() {
  test_decimal();
  test_markets_fixture();
  test_crypto_category_fixture();
  test_orderbook_fixture_and_no_view();
  test_empty_and_string_numeric_book();
  test_rejections_and_bounds();
  test_unknown_top_level_status();
  test_category_and_timeseries_responses();
  test_discovery_responses();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "predictfun-cpp codec tests passed\n";
  return EXIT_SUCCESS;
}
