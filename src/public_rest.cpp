#include "predictfun/public_rest/client.hpp"

#include "predictfun/codec/private_rest.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <utility>

namespace predictfun::public_rest {
namespace {

namespace asio = boost::asio;

bool safe_token(std::string_view value) {
  return !value.empty() &&
         std::ranges::all_of(value, [](unsigned char character) {
           return std::isalnum(character) || character == '_' ||
                  character == '-' || character == '.';
         });
}

std::string percent_encode(std::string_view value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(value.size());
  for (const auto raw : value) {
    const auto character = static_cast<unsigned char>(raw);
    if (std::isalnum(character) || character == '-' || character == '_' ||
        character == '.' || character == '~') {
      encoded.push_back(static_cast<char>(character));
    } else {
      encoded.push_back('%');
      encoded.push_back(hex[(character >> 4U) & 0xFU]);
      encoded.push_back(hex[character & 0xFU]);
    }
  }
  return encoded;
}

void add_query(std::string &target, std::string_view name,
               std::string_view value) {
  target.push_back(target.find('?') == std::string::npos ? '?' : '&');
  target.append(name);
  target.push_back('=');
  target.append(percent_encode(value));
}

template <class Query>
Result<std::string> collection_target(std::string base, const Query &query) {
  if (query.first && (*query.first == 0U || *query.first > 1'000U)) {
    return Error{ErrorCode::invalid_argument,
                 "first must be between 1 and 1000", "first"};
  }
  if (query.status && !safe_token(*query.status))
    return Error{ErrorCode::invalid_argument, "invalid status", "status"};
  if (query.market_variant && !safe_token(*query.market_variant)) {
    return Error{ErrorCode::invalid_argument, "invalid market variant",
                 "marketVariant"};
  }
  if (query.first)
    add_query(base, "first", std::to_string(*query.first));
  if (query.after)
    add_query(base, "after", *query.after);
  if (query.status)
    add_query(base, "status", *query.status);
  if (query.market_variant)
    add_query(base, "marketVariant", *query.market_variant);
  return base;
}

std::chrono::milliseconds retry_after(const net::HttpResponse &response,
                                      std::size_t attempt) {
  return net::parse_retry_after(
      response.header("Retry-After"), std::chrono::system_clock::now(),
      std::chrono::milliseconds{100U
                                << std::min<std::size_t>(attempt, 6U)});
}

Error http_error(ErrorCode code, int status, std::string message,
                 std::chrono::milliseconds retry = {}) {
  Error error{code, std::move(message), {}};
  error.http_status = status;
  error.retry_after_ms =
      static_cast<std::uint64_t>(std::max<std::int64_t>(0, retry.count()));
  return error;
}

bool supported_position_sort(std::string_view value) {
  return value == "AMOUNT_DESC" || value == "EVENT_BLOCK_ASC" ||
         value == "EVENT_BLOCK_DESC" || value == "SHARES_VALUE_DESC" ||
         value == "RETURN_DESC";
}

} // namespace

namespace protocol {

Result<std::string> markets_target(const MarketsQuery &query) {
  return collection_target("/v1/markets", query);
}

Result<std::string> market_target(MarketId market_id) {
  if (market_id.value == 0U)
    return Error{ErrorCode::invalid_argument, "market id must be positive",
                 "market_id"};
  return std::format("/v1/markets/{}", market_id.value);
}

Result<std::string> categories_target(const CategoriesQuery &query) {
  return collection_target("/v1/categories", query);
}

Result<std::string> category_target(std::string_view slug) {
  if (!safe_token(slug))
    return Error{ErrorCode::invalid_argument, "invalid category slug", "slug"};
  return "/v1/categories/" + percent_encode(slug);
}

Result<std::string> orderbook_target(MarketId market_id) {
  auto market = market_target(market_id);
  if (!market)
    return market.error();
  return market.value() + "/orderbook";
}

Result<std::string> timeseries_target(MarketId market_id,
                                      const TimeseriesQuery &query) {
  auto market = market_target(market_id);
  if (!market)
    return market.error();
  if (!safe_token(query.metric))
    return Error{ErrorCode::invalid_argument, "invalid timeseries metric",
                 "metric"};
  if (query.resolution && !safe_token(*query.resolution)) {
    return Error{ErrorCode::invalid_argument, "invalid timeseries resolution",
                 "resolution"};
  }
  if (query.from && query.to && *query.from > *query.to)
    return Error{ErrorCode::invalid_argument, "from must not exceed to",
                 "from"};
  if (query.limit && (*query.limit == 0U || *query.limit > 100'000U)) {
    return Error{ErrorCode::invalid_argument,
                 "timeseries limit must be between 1 and 100000", "limit"};
  }
  auto target = market.value() + "/timeseries";
  add_query(target, "metric", query.metric);
  if (query.resolution)
    add_query(target, "resolution", *query.resolution);
  if (query.from)
    add_query(target, "from", std::to_string(*query.from));
  if (query.to)
    add_query(target, "to", std::to_string(*query.to));
  if (query.limit)
    add_query(target, "limit", std::to_string(*query.limit));
  if (query.after)
    add_query(target, "after", *query.after);
  return target;
}

Result<std::string> latest_timeseries_target(MarketId market_id,
                                             std::string_view metric) {
  if (!safe_token(metric))
    return Error{ErrorCode::invalid_argument, "invalid timeseries metric",
                 "metric"};
  auto market = market_target(market_id);
  if (!market)
    return market.error();
  auto target = market.value() + "/timeseries/latest";
  add_query(target, "metric", metric);
  return target;
}

Result<std::string> matches_target(const MatchesQuery &query) {
  if (query.first && (*query.first == 0U || *query.first > 1'000U))
    return Error{ErrorCode::invalid_argument,
                 "first must be between 1 and 1000", "first"};
  if (query.category && !safe_token(*query.category))
    return Error{ErrorCode::invalid_argument, "invalid category", "category"};
  if (query.market_id && query.market_id->value == 0U)
    return Error{ErrorCode::invalid_argument,
                 "market id must be positive", "market_id"};
  if (query.signer_address && query.signer_address->empty())
    return Error{ErrorCode::invalid_argument,
                 "signer address must not be zero", "signer_address"};
  auto target = std::string{"/v1/orders/matches"};
  if (query.first) add_query(target, "first", std::to_string(*query.first));
  if (query.after) add_query(target, "after", *query.after);
  if (query.category) add_query(target, "category", *query.category);
  if (query.market_id)
    add_query(target, "marketId", std::to_string(query.market_id->value));
  if (query.min_value_usdt_wei)
    add_query(target, "minValueUsdtWei",
              query.min_value_usdt_wei->to_string());
  if (query.signer_address)
    add_query(target, "signerAddress", query.signer_address->to_string());
  if (query.is_signer_maker)
    add_query(target, "isSignerMaker",
              *query.is_signer_maker ? "true" : "false");
  return target;
}

Result<std::string> tags_target() { return std::string{"/v1/tags"}; }

Result<std::string> market_statistics_target(MarketId market_id) {
  auto market = market_target(market_id);
  if (!market)
    return market.error();
  return market.value() + "/stats";
}

Result<std::string> market_last_sale_target(MarketId market_id) {
  auto market = market_target(market_id);
  if (!market)
    return market.error();
  return market.value() + "/last-sale";
}

Result<std::string> search_target(const SearchQuery &query) {
  if (query.query.empty() || query.query.size() > 256U)
    return Error{ErrorCode::invalid_argument,
                 "search query must contain between 1 and 256 bytes",
                 "query"};
  if (query.limit && (*query.limit == 0U || *query.limit > 25U))
    return Error{ErrorCode::invalid_argument,
                 "search limit must be between 1 and 25", "limit"};
  auto target = std::string{"/v1/search"};
  add_query(target, "query", query.query);
  if (query.include_resolved)
    add_query(target, "includeResolved",
              *query.include_resolved ? "true" : "false");
  if (query.limit)
    add_query(target, "limit", std::to_string(*query.limit));
  return target;
}

Result<std::string>
address_positions_target(const EvmAddress &address,
                         const AddressPositionsQuery &query) {
  if (address.empty())
    return Error{ErrorCode::invalid_argument,
                 "position address must not be zero", "address"};
  if (query.first && (*query.first == 0U || *query.first > 1'000U))
    return Error{ErrorCode::invalid_argument,
                 "first must be between 1 and 1000", "first"};
  if (query.market_id && query.market_id->value == 0U)
    return Error{ErrorCode::invalid_argument,
                 "market id must be positive", "market_id"};
  if (query.sort && !supported_position_sort(*query.sort))
    return Error{ErrorCode::invalid_argument,
                 "unsupported position sort", "sort"};

  auto target = "/v1/positions/" + address.to_string();
  if (query.first)
    add_query(target, "first", std::to_string(*query.first));
  if (query.after)
    add_query(target, "after", *query.after);
  if (query.market_id)
    add_query(target, "marketId", std::to_string(query.market_id->value));
  if (query.is_resolved)
    add_query(target, "isResolved", *query.is_resolved ? "true" : "false");
  if (query.sort)
    add_query(target, "sort", *query.sort);
  return target;
}

} // namespace protocol

struct PublicRestClient::Impl : public std::enable_shared_from_this<Impl> {
  using RawHandler = std::function<void(Result<net::HttpResponse>)>;

  Impl(asio::any_io_executor executor_value,
       std::shared_ptr<net::HttpTransport> transport_value,
       ClientOptions options_value)
      : executor(std::move(executor_value)),
        transport(std::move(transport_value)),
        options(std::move(options_value)),
        limiter(options.rate_limiter
                    ? options.rate_limiter
                    : std::make_shared<net::RateLimiter>(options.rate_limits)) {
    if (!transport)
      throw std::invalid_argument("Predict.fun REST transport is required");
  }

  struct RequestOperation
      : public std::enable_shared_from_this<RequestOperation> {
    std::shared_ptr<Impl> owner;
    std::string endpoint;
    std::string target;
    net::RequestContext context;
    RawHandler handler;
    std::size_t attempt{0U};
    std::shared_ptr<asio::steady_timer> timer;
    std::optional<std::stop_callback<std::function<void()>>> stop_callback;

    void start(bool reserve_budget = true) {
      if (context.cancel.stop_requested())
        return finish(Error{ErrorCode::cancelled, "request cancelled", {}});
      if (context.deadline != std::chrono::steady_clock::time_point{} &&
          context.deadline <= std::chrono::steady_clock::now()) {
        return finish(Error{
            ErrorCode::deadline_exceeded, "request deadline exceeded", {}});
      }
      const auto wait = reserve_budget
                            ? owner->limiter->reserve(
                                  endpoint, std::chrono::steady_clock::now())
                            : std::chrono::milliseconds{0};
      if (wait.count() > 0) {
        return schedule(wait, false);
      }
      send();
    }

    void send() {
      std::string key;
      if (owner->options.api_key) {
        try {
          key = owner->options.api_key();
        } catch (...) {
          return finish(Error{ErrorCode::authentication_required,
                              "API key provider failed",
                              {}});
        }
      }
      if (owner->options.environment == Environment::bnb_mainnet &&
          key.empty()) {
        return finish(Error{ErrorCode::authentication_required,
                            "Predict.fun mainnet requires an API key",
                            {}});
      }

      net::HttpRequest request;
      request.host = owner->options.environment == Environment::bnb_mainnet
                         ? "api.predict.fun"
                         : "api-testnet.predict.fun";
      request.target = target;
      request.use_tls = true;
      if (!key.empty())
        request.headers.push_back(net::Header{"x-api-key", std::move(key)});
      owner->transport->async_get(
          std::move(request), context,
          [self = shared_from_this()](Result<net::HttpResponse> result) {
            self->handle(std::move(result));
          });
    }

    void handle(Result<net::HttpResponse> result) {
      if (!result)
        return finish(result.error());
      const auto &response = result.value();
      if (response.status >= 200 && response.status < 300)
        return finish(std::move(result));
      if (response.status >= 300 && response.status < 400) {
        return finish(http_error(ErrorCode::http_redirect, response.status,
                                 "HTTP redirect rejected"));
      }
      if (response.status == 401 || response.status == 403) {
        return finish(http_error(ErrorCode::authentication_required,
                                 response.status,
                                 "Predict.fun authentication rejected"));
      }
      if (response.status == 429) {
        const auto delay = retry_after(response, attempt);
        owner->limiter->penalize(endpoint, std::chrono::steady_clock::now(),
                                 delay);
        if (attempt++ < owner->options.max_get_retries)
          return schedule(delay, false);
        return finish(http_error(ErrorCode::rate_limited, response.status,
                                 "Predict.fun rate limit exceeded", delay));
      }
      if (response.status >= 500) {
        const auto delay = retry_after(response, attempt);
        if (attempt++ < owner->options.max_get_retries)
          return schedule(delay, true);
        return finish(http_error(ErrorCode::http_server_error, response.status,
                                 "Predict.fun server error", delay));
      }
      finish(http_error(ErrorCode::http_client_error, response.status,
                        "Predict.fun request rejected"));
    }

    void schedule(std::chrono::milliseconds delay, bool reserve_budget) {
      const auto now = std::chrono::steady_clock::now();
      if (context.deadline != std::chrono::steady_clock::time_point{} &&
          now + delay >= context.deadline) {
        return finish(Error{ErrorCode::deadline_exceeded,
                            "retry would exceed request deadline",
                            {}});
      }
      timer = std::make_shared<asio::steady_timer>(owner->executor);
      timer->expires_after(delay);
      timer->async_wait([self = shared_from_this(), reserve_budget](
                            const boost::system::error_code &error) {
        if (error)
          return self->finish(
              Error{ErrorCode::cancelled, "request wait cancelled", {}});
        self->start(reserve_budget);
      });
    }

    void finish(Result<net::HttpResponse> result) {
      if (!handler)
        return;
      auto completion = std::move(handler);
      completion(std::move(result));
    }

    void arm_cancellation() {
      stop_callback.emplace(
          context.cancel, std::function<void()>{[weak = weak_from_this(),
                                                 executor = owner->executor] {
            asio::dispatch(executor, [weak] {
              if (auto self = weak.lock()) {
                if (self->timer)
                  self->timer->cancel();
                self->finish(
                    Error{ErrorCode::cancelled, "request cancelled", {}});
              }
            });
          }});
    }
  };

  void get(std::string endpoint, Result<std::string> target,
           net::RequestContext context, RawHandler handler) {
    if (!target) {
      asio::dispatch(executor, [handler = std::move(handler),
                                error = target.error()]() mutable {
        handler(std::move(error));
      });
      return;
    }
    auto operation = std::make_shared<RequestOperation>();
    operation->owner = shared_from_this();
    operation->endpoint = std::move(endpoint);
    operation->target = std::move(target.value());
    operation->context = std::move(context);
    operation->handler = std::move(handler);
    operation->arm_cancellation();
    asio::dispatch(executor, [operation] { operation->start(); });
  }

  asio::any_io_executor executor;
  std::shared_ptr<net::HttpTransport> transport;
  ClientOptions options;
  std::shared_ptr<net::RateLimiter> limiter;
};

PublicRestClient::PublicRestClient(
    asio::any_io_executor executor,
    std::shared_ptr<net::HttpTransport> transport, ClientOptions options)
    : impl_(std::make_shared<Impl>(std::move(executor), std::move(transport),
                                   std::move(options))) {}

PublicRestClient::~PublicRestClient() = default;
PublicRestClient::PublicRestClient(PublicRestClient &&) noexcept = default;
PublicRestClient &
PublicRestClient::operator=(PublicRestClient &&) noexcept = default;

void PublicRestClient::async_get_markets(MarketsQuery query,
                                         net::RequestContext context,
                                         Handler<MarketsPage> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("markets", protocol::markets_target(query), std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw)
                 return handler(raw.error());
               handler(
                   codec::decode_markets_response(raw.value().body, limits));
             });
}

void PublicRestClient::async_get_market(MarketId market_id,
                                        net::RequestContext context,
                                        Handler<Market> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("market", protocol::market_target(market_id), std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw)
                 return handler(raw.error());
               handler(codec::decode_market_response(raw.value().body, limits));
             });
}

void PublicRestClient::async_get_categories(CategoriesQuery query,
                                            net::RequestContext context,
                                            Handler<CategoriesPage> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get(
      "categories", protocol::categories_target(query), std::move(context),
      [handler = std::move(handler),
       limits](Result<net::HttpResponse> raw) mutable {
        if (!raw)
          return handler(raw.error());
        handler(codec::decode_categories_response(raw.value().body, limits));
      });
}

void PublicRestClient::async_get_category(std::string slug,
                                          net::RequestContext context,
                                          Handler<Category> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("category", protocol::category_target(slug), std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw)
                 return handler(raw.error());
               handler(
                   codec::decode_category_response(raw.value().body, limits));
             });
}

void PublicRestClient::async_get_orderbook(MarketId market_id,
                                           std::uint8_t decimal_precision,
                                           net::RequestContext context,
                                           Handler<Orderbook> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("orderbook", protocol::orderbook_target(market_id),
             std::move(context),
             [handler = std::move(handler), limits,
              decimal_precision](Result<net::HttpResponse> raw) mutable {
               if (!raw)
                 return handler(raw.error());
               handler(codec::decode_orderbook_response(
                   raw.value().body, decimal_precision, limits));
             });
}

void PublicRestClient::async_get_timeseries(MarketId market_id,
                                            TimeseriesQuery query,
                                            net::RequestContext context,
                                            Handler<TimeseriesPage> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("timeseries", protocol::timeseries_target(market_id, query),
             std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw)
                 return handler(raw.error());
               handler(
                   codec::decode_timeseries_response(raw.value().body, limits));
             });
}

void PublicRestClient::async_get_latest_timeseries(
    MarketId market_id, std::string metric, net::RequestContext context,
    Handler<TimeseriesPoint> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("timeseries_latest",
             protocol::latest_timeseries_target(market_id, metric),
             std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw)
                 return handler(raw.error());
               handler(codec::decode_latest_timeseries_response(
                   raw.value().body, limits));
             });
}

void PublicRestClient::async_get_matches(MatchesQuery query,
                                         net::RequestContext context,
                                         Handler<MatchesPage> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("matches", protocol::matches_target(query), std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw) return handler(raw.error());
               handler(codec::decode_matches_response(raw.value().body,
                                                       limits));
             });
}

void PublicRestClient::async_get_tags(net::RequestContext context,
                                      Handler<std::vector<Tag>> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("tags", protocol::tags_target(), std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw)
                 return handler(raw.error());
               handler(codec::decode_tags_response(raw.value().body, limits));
             });
}

void PublicRestClient::async_get_market_statistics(
    MarketId market_id, net::RequestContext context,
    Handler<MarketStatistics> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("market_stats", protocol::market_statistics_target(market_id),
             std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw)
                 return handler(raw.error());
               handler(codec::decode_market_statistics_response(
                   raw.value().body, limits));
             });
}

void PublicRestClient::async_get_market_last_sale(
    MarketId market_id, net::RequestContext context,
    Handler<std::optional<MarketLastSale>> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("market_last_sale",
             protocol::market_last_sale_target(market_id), std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw)
                 return handler(raw.error());
               handler(codec::decode_market_last_sale_response(
                   raw.value().body, limits));
             });
}

void PublicRestClient::async_search(SearchQuery query,
                                    net::RequestContext context,
                                    Handler<SearchResults> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("search", protocol::search_target(query), std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw)
                 return handler(raw.error());
               handler(codec::decode_search_response(raw.value().body,
                                                       limits));
             });
}

void PublicRestClient::async_get_positions_by_address(
    EvmAddress address, AddressPositionsQuery query,
    net::RequestContext context, Handler<PositionsPage> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("address_positions",
             protocol::address_positions_target(address, query),
             std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw)
                 return handler(raw.error());
               handler(codec::decode_positions_response(raw.value().body,
                                                          limits));
             });
}

} // namespace predictfun::public_rest
