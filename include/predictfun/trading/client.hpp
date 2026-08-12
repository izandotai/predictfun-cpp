#pragma once

#include "predictfun/codec/trading.hpp"
#include "predictfun/net/http.hpp"
#include "predictfun/net/rate_limiter.hpp"
#include "predictfun/private_rest/client.hpp"
#include "predictfun/types/auth.hpp"

#include <boost/asio/any_io_executor.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace predictfun::trading {

using ApiKeyProvider = std::function<std::string()>;
using JwtProvider = std::function<SecretString()>;

struct ClientOptions {
  Environment environment{Environment::bnb_testnet};
  ApiKeyProvider api_key;
  JwtProvider jwt;
  codec::DecodeLimits decode_limits;
  net::RateLimitPolicy rate_limits;
};

namespace protocol {
[[nodiscard]] Result<std::string>
validate_create_order(const CreateOrderRequest &request);
[[nodiscard]] Result<std::string>
validate_order_ids(const std::vector<std::string> &ids);
[[nodiscard]] Result<std::string>
validate_order_hashes(const std::vector<std::string> &hashes);
} // namespace protocol

template <class T> using Handler = std::function<void(Result<T>)>;

class TradingClient {
public:
  TradingClient(boost::asio::any_io_executor executor,
                std::shared_ptr<net::HttpTransport> transport,
                ClientOptions options);
  ~TradingClient();

  TradingClient(const TradingClient &) = delete;
  TradingClient &operator=(const TradingClient &) = delete;
  TradingClient(TradingClient &&) noexcept;
  TradingClient &operator=(TradingClient &&) noexcept;

  // Mutations are intentionally never retried. If transport/server state makes
  // the result unknowable after dispatch, the outcome is `ambiguous` and the
  // reconciliation key must be queried before any caller-initiated retry.
  void async_create_order(
      CreateOrderRequest request, net::RequestContext context,
      Handler<MutationOutcome<CreateOrderReceipt>> handler);
  void async_remove_order_ids(
      std::vector<std::string> ids, net::RequestContext context,
      Handler<MutationOutcome<RemoveOrdersReceipt>> handler);
  void async_remove_order_hashes(
      std::vector<std::string> hashes, net::RequestContext context,
      Handler<MutationOutcome<RemoveOrdersReceipt>> handler);

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace predictfun::trading
