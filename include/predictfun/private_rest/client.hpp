#pragma once

#include "predictfun/codec/private_rest.hpp"
#include "predictfun/net/http.hpp"
#include "predictfun/net/rate_limiter.hpp"
#include "predictfun/types/auth.hpp"
#include "predictfun/types/trading.hpp"

#include <boost/asio/any_io_executor.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace predictfun::private_rest {

using ApiKeyProvider = std::function<std::string()>;
using JwtProvider = std::function<SecretString()>;

struct ClientOptions {
  Environment environment{Environment::bnb_testnet};
  ApiKeyProvider api_key;
  JwtProvider jwt;
  codec::DecodeLimits decode_limits;
  net::RateLimitPolicy rate_limits;
  std::shared_ptr<net::RateLimiter> rate_limiter;
  std::size_t max_get_retries{2U};
};

struct PageQuery {
  std::optional<std::size_t> first;
  std::optional<std::string> after;
};

struct PositionsQuery : PageQuery {
  std::optional<MarketId> market_id;
  std::optional<bool> is_resolved;
  std::optional<std::string> sort;
};

struct OrdersQuery : PageQuery {
  std::optional<std::string> status;
};

struct ActivityQuery : PageQuery {
  std::vector<std::string> event_types;
};

namespace protocol {
[[nodiscard]] Result<std::string> positions_target(const PositionsQuery &query);
[[nodiscard]] Result<std::string> orders_target(const OrdersQuery &query);
[[nodiscard]] Result<std::string> order_target(std::string_view hash);
[[nodiscard]] Result<std::string> activity_target(const ActivityQuery &query);
} // namespace protocol

template <class T> using Handler = std::function<void(Result<T>)>;

class PrivateRestClient {
public:
  PrivateRestClient(boost::asio::any_io_executor executor,
                    std::shared_ptr<net::HttpTransport> transport,
                    ClientOptions options);
  ~PrivateRestClient();

  PrivateRestClient(const PrivateRestClient &) = delete;
  PrivateRestClient &operator=(const PrivateRestClient &) = delete;
  PrivateRestClient(PrivateRestClient &&) noexcept;
  PrivateRestClient &operator=(PrivateRestClient &&) noexcept;

  void async_get_account(net::RequestContext context, Handler<Account> handler);
  void async_get_positions(PositionsQuery query, net::RequestContext context,
                           Handler<PositionsPage> handler);
  void async_get_orders(OrdersQuery query, net::RequestContext context,
                        Handler<OrdersPage> handler);
  void async_get_order(std::string hash, net::RequestContext context,
                       Handler<OrderRecord> handler);
  void async_get_activity(ActivityQuery query, net::RequestContext context,
                          Handler<ActivityPage> handler);

  // Referral assignment mutates remote account state. It is deliberately
  // dispatched at most once: ambiguous transport/server outcomes must be
  // reconciled with async_get_account() and are never replayed automatically.
  void async_set_referral(
      std::string referral_code, net::RequestContext context,
      Handler<MutationOutcome<bool>> handler);

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace predictfun::private_rest
