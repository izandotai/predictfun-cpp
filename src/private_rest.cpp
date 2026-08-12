#include "predictfun/private_rest/client.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace predictfun::private_rest {
namespace {

namespace asio = boost::asio;

bool safe_token(std::string_view value) {
  return !value.empty() &&
         std::ranges::all_of(value, [](unsigned char character) {
           return std::isalnum(character) || character == '_' ||
                  character == '-' || character == '.';
         });
}

bool order_hash(std::string_view value) {
  return value.size() == 66U && value.starts_with("0x") &&
         std::ranges::all_of(value.substr(2U), [](unsigned char character) {
           return std::isxdigit(character) != 0;
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

Result<std::string> add_page(std::string target, const PageQuery &query) {
  if (query.first && (*query.first == 0U || *query.first > 1'000U))
    return Error{ErrorCode::invalid_argument,
                 "first must be between 1 and 1000", "first"};
  if (query.first) add_query(target, "first", std::to_string(*query.first));
  if (query.after) add_query(target, "after", *query.after);
  return target;
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

} // namespace

namespace protocol {

Result<std::string> positions_target(const PositionsQuery &query) {
  auto target = add_page("/v1/positions", query);
  if (!target) return target.error();
  if (query.market_id) {
    if (query.market_id->value == 0U)
      return Error{ErrorCode::invalid_argument,
                   "market id must be positive", "market_id"};
    add_query(target.value(), "marketId", std::to_string(query.market_id->value));
  }
  if (query.is_resolved)
    add_query(target.value(), "isResolved", *query.is_resolved ? "true" : "false");
  if (query.sort) {
    if (!safe_token(*query.sort))
      return Error{ErrorCode::invalid_argument, "invalid position sort", "sort"};
    add_query(target.value(), "sort", *query.sort);
  }
  return target.value();
}

Result<std::string> orders_target(const OrdersQuery &query) {
  auto target = add_page("/v1/orders", query);
  if (!target) return target.error();
  if (query.status) {
    if (!safe_token(*query.status))
      return Error{ErrorCode::invalid_argument, "invalid order status", "status"};
    add_query(target.value(), "status", *query.status);
  }
  return target.value();
}

Result<std::string> order_target(std::string_view hash) {
  if (!order_hash(hash))
    return Error{ErrorCode::invalid_argument,
                 "order hash must be 0x followed by 64 hex characters",
                 "hash"};
  return "/v1/orders/" + std::string{hash};
}

Result<std::string> activity_target(const ActivityQuery &query) {
  auto target = add_page("/v1/account/activity", query);
  if (!target) return target.error();
  for (const auto &event : query.event_types) {
    if (!safe_token(event))
      return Error{ErrorCode::invalid_argument, "invalid activity event type",
                   "event_types"};
    add_query(target.value(), "eventTypes", event);
  }
  return target.value();
}

} // namespace protocol

struct PrivateRestClient::Impl : public std::enable_shared_from_this<Impl> {
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
      throw std::invalid_argument("Predict.fun private REST transport is required");
    if (!options.jwt)
      throw std::invalid_argument("Predict.fun JWT provider is required");
  }

  struct Operation : public std::enable_shared_from_this<Operation> {
    std::shared_ptr<Impl> owner;
    std::string endpoint;
    std::string target;
    net::RequestContext context;
    RawHandler handler;
    std::size_t attempt{0U};
    std::shared_ptr<asio::steady_timer> timer;
    std::optional<std::stop_callback<std::function<void()>>> stop_callback;

    void start(bool reserve_budget = true) {
      if (!handler) return;
      if (context.cancel.stop_requested())
        return finish(Error{ErrorCode::cancelled, "request cancelled", {}});
      if (context.deadline != std::chrono::steady_clock::time_point{} &&
          context.deadline <= std::chrono::steady_clock::now())
        return finish(Error{ErrorCode::deadline_exceeded,
                            "request deadline exceeded", {}});
      const auto wait = reserve_budget
                            ? owner->limiter->reserve(
                                  endpoint, std::chrono::steady_clock::now())
                            : std::chrono::milliseconds{0};
      if (wait.count() > 0) return schedule(wait, false);
      send();
    }

    void send() {
      std::string key;
      SecretString jwt;
      try {
        if (owner->options.api_key) key = owner->options.api_key();
        jwt = owner->options.jwt();
      } catch (...) {
        secure_erase(key);
        return finish(Error{ErrorCode::authentication_required,
                            "credential provider failed", {}});
      }
      if (owner->options.environment == Environment::bnb_mainnet && key.empty()) {
        return finish(Error{ErrorCode::authentication_required,
                            "Predict.fun mainnet requires an API key", {}});
      }
      if (jwt.empty()) {
        secure_erase(key);
        return finish(Error{ErrorCode::authentication_required,
                            "Predict.fun private endpoint requires a JWT", {}});
      }

      net::HttpRequest request;
      request.host = owner->options.environment == Environment::bnb_mainnet
                         ? "api.predict.fun"
                         : "api-testnet.predict.fun";
      request.target = target;
      request.use_tls = true;
      if (!key.empty()) request.headers.push_back({"x-api-key", std::move(key)});
      request.headers.push_back(
          {"Authorization", "Bearer " + std::string{jwt.view()}});
      jwt.clear();
      owner->transport->async_get(
          std::move(request), context,
          [self = shared_from_this()](Result<net::HttpResponse> result) {
            self->handle(std::move(result));
          });
    }

    void handle(Result<net::HttpResponse> result) {
      if (!result) return finish(result.error());
      const auto &response = result.value();
      if (response.status >= 200 && response.status < 300)
        return finish(std::move(result));
      if (response.status >= 300 && response.status < 400)
        return finish(http_error(ErrorCode::http_redirect, response.status,
                                 "HTTP redirect rejected"));
      if (response.status == 401 || response.status == 403)
        return finish(http_error(ErrorCode::authentication_required,
                                 response.status,
                                 "Predict.fun authentication rejected"));
      if (response.status == 429 || response.status >= 500) {
        const auto delay = retry_after(response, attempt);
        if (response.status == 429)
          owner->limiter->penalize(endpoint, std::chrono::steady_clock::now(),
                                   delay);
        if (attempt++ < owner->options.max_get_retries)
          return schedule(delay, response.status >= 500);
        return finish(http_error(response.status == 429
                                     ? ErrorCode::rate_limited
                                     : ErrorCode::http_server_error,
                                 response.status,
                                 response.status == 429
                                     ? "Predict.fun rate limit exceeded"
                                     : "Predict.fun server error",
                                 delay));
      }
      return finish(http_error(ErrorCode::http_client_error, response.status,
                               "Predict.fun request rejected"));
    }

    void schedule(std::chrono::milliseconds delay, bool reserve_budget) {
      const auto now = std::chrono::steady_clock::now();
      if (context.deadline != std::chrono::steady_clock::time_point{} &&
          now + delay >= context.deadline)
        return finish(Error{ErrorCode::deadline_exceeded,
                            "retry would exceed request deadline", {}});
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
      if (!handler) return;
      auto completion = std::move(handler);
      completion(std::move(result));
    }

    void arm_cancellation() {
      stop_callback.emplace(
          context.cancel, std::function<void()>{[weak = weak_from_this(),
                                                 executor = owner->executor] {
            asio::dispatch(executor, [weak] {
              if (auto self = weak.lock()) {
                if (self->timer) self->timer->cancel();
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
    auto operation = std::make_shared<Operation>();
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

PrivateRestClient::PrivateRestClient(
    asio::any_io_executor executor, std::shared_ptr<net::HttpTransport> transport,
    ClientOptions options)
    : impl_(std::make_shared<Impl>(std::move(executor), std::move(transport),
                                   std::move(options))) {}
PrivateRestClient::~PrivateRestClient() = default;
PrivateRestClient::PrivateRestClient(PrivateRestClient &&) noexcept = default;
PrivateRestClient &PrivateRestClient::operator=(PrivateRestClient &&) noexcept =
    default;

void PrivateRestClient::async_get_account(net::RequestContext context,
                                          Handler<Account> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("account", std::string{"/v1/account"}, std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw) return handler(raw.error());
               handler(codec::decode_account_response(raw.value().body, limits));
             });
}

void PrivateRestClient::async_get_positions(PositionsQuery query,
                                            net::RequestContext context,
                                            Handler<PositionsPage> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("positions", protocol::positions_target(query), std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw) return handler(raw.error());
               handler(codec::decode_positions_response(raw.value().body,
                                                        limits));
             });
}

void PrivateRestClient::async_get_orders(OrdersQuery query,
                                         net::RequestContext context,
                                         Handler<OrdersPage> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("orders", protocol::orders_target(query), std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw) return handler(raw.error());
               handler(codec::decode_orders_response(raw.value().body, limits));
             });
}

void PrivateRestClient::async_get_order(std::string hash,
                                        net::RequestContext context,
                                        Handler<OrderRecord> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("order", protocol::order_target(hash), std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw) return handler(raw.error());
               handler(codec::decode_order_response(raw.value().body, limits));
             });
}

void PrivateRestClient::async_get_activity(ActivityQuery query,
                                           net::RequestContext context,
                                           Handler<ActivityPage> handler) {
  const auto limits = impl_->options.decode_limits;
  impl_->get("account_activity", protocol::activity_target(query),
             std::move(context),
             [handler = std::move(handler),
              limits](Result<net::HttpResponse> raw) mutable {
               if (!raw) return handler(raw.error());
               handler(codec::decode_activity_response(raw.value().body,
                                                       limits));
             });
}

} // namespace predictfun::private_rest
