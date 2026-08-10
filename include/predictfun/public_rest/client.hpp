#pragma once

#include "predictfun/codec/public_rest.hpp"
#include "predictfun/net/http.hpp"
#include "predictfun/net/rate_limiter.hpp"

#include <boost/asio/any_io_executor.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace predictfun::public_rest {

using ApiKeyProvider = std::function<std::string()>;

struct ClientOptions {
  Environment environment{Environment::bnb_testnet};
  ApiKeyProvider api_key;
  codec::DecodeLimits decode_limits;
  net::RateLimitPolicy rate_limits;
  std::size_t max_get_retries{2U};
};

struct MarketsQuery {
  std::optional<std::size_t> first;
  std::optional<std::string> after;
  std::optional<std::string> status;
  std::optional<std::string> market_variant;
};

struct CategoriesQuery {
  std::optional<std::size_t> first;
  std::optional<std::string> after;
  std::optional<std::string> status;
  std::optional<std::string> market_variant;
};

struct TimeseriesQuery {
  std::string metric;
  std::optional<std::string> resolution;
  std::optional<std::int64_t> from;
  std::optional<std::int64_t> to;
  std::optional<std::size_t> limit;
  std::optional<std::string> after;
};

namespace protocol {

[[nodiscard]] Result<std::string> markets_target(const MarketsQuery &query);
[[nodiscard]] Result<std::string> market_target(MarketId market_id);
[[nodiscard]] Result<std::string>
categories_target(const CategoriesQuery &query);
[[nodiscard]] Result<std::string> category_target(std::string_view slug);
[[nodiscard]] Result<std::string> orderbook_target(MarketId market_id);
[[nodiscard]] Result<std::string>
timeseries_target(MarketId market_id, const TimeseriesQuery &query);
[[nodiscard]] Result<std::string>
latest_timeseries_target(MarketId market_id, std::string_view metric);

} // namespace protocol

template <class T> using Handler = std::function<void(Result<T>)>;

class PublicRestClient {
public:
  PublicRestClient(boost::asio::any_io_executor executor,
                   std::shared_ptr<net::HttpTransport> transport,
                   ClientOptions options = {});
  ~PublicRestClient();

  PublicRestClient(const PublicRestClient &) = delete;
  PublicRestClient &operator=(const PublicRestClient &) = delete;
  PublicRestClient(PublicRestClient &&) noexcept;
  PublicRestClient &operator=(PublicRestClient &&) noexcept;

  void async_get_markets(MarketsQuery query, net::RequestContext context,
                         Handler<MarketsPage> handler);
  void async_get_market(MarketId market_id, net::RequestContext context,
                        Handler<Market> handler);
  void async_get_categories(CategoriesQuery query, net::RequestContext context,
                            Handler<CategoriesPage> handler);
  void async_get_category(std::string slug, net::RequestContext context,
                          Handler<Category> handler);
  void async_get_orderbook(MarketId market_id, std::uint8_t decimal_precision,
                           net::RequestContext context,
                           Handler<Orderbook> handler);
  void async_get_timeseries(MarketId market_id, TimeseriesQuery query,
                            net::RequestContext context,
                            Handler<TimeseriesPage> handler);
  void async_get_latest_timeseries(MarketId market_id, std::string metric,
                                   net::RequestContext context,
                                   Handler<TimeseriesPoint> handler);

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace predictfun::public_rest
