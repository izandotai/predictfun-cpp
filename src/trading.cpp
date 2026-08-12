#include "predictfun/trading/client.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace predictfun::trading {
namespace {

namespace asio = boost::asio;

bool hash32(std::string_view value) {
  return value.size() == 66U && value.starts_with("0x") &&
         std::ranges::all_of(value.substr(2U), [](unsigned char character) {
           return std::isxdigit(character) != 0;
         });
}

bool safe_id(std::string_view value) {
  return !value.empty() && value.size() <= 256U &&
         std::ranges::all_of(value, [](unsigned char character) {
           return character >= 0x21U && character <= 0x7eU;
         });
}

Error http_error(ErrorCode code, int status, std::string message) {
  Error error{code, std::move(message), {}};
  error.http_status = status;
  return error;
}

template <class T>
MutationOutcome<T> ambiguous(Error error, std::string key) {
  error.code = ErrorCode::ambiguous_submission;
  return MutationOutcome<T>{MutationDisposition::ambiguous, std::nullopt,
                            std::move(error), std::move(key)};
}

} // namespace

namespace protocol {

Result<std::string> validate_create_order(const CreateOrderRequest &request) {
  if (!hash32(request.order_hash))
    return Error{ErrorCode::invalid_argument,
                 "order hash must be 0x followed by 64 hex characters",
                 "order_hash"};
  if (request.order.signature.empty() ||
      request.order.signature.size() > 200U)
    return Error{ErrorCode::invalid_argument,
                 "signature must contain at most 200 characters",
                 "order.signature"};
  if (request.order.side != ContractSide::buy &&
      request.order.side != ContractSide::sell)
    return Error{ErrorCode::invalid_argument, "unsupported order side",
                 "order.side"};
  if (request.order.signature_type != SignatureType::eoa &&
      request.order.signature_type != SignatureType::poly_proxy &&
      request.order.signature_type != SignatureType::poly_gnosis_safe)
    return Error{ErrorCode::invalid_argument,
                 "unsupported signature type", "order.signature_type"};
  if (request.price_per_share_wei.is_zero())
    return Error{ErrorCode::invalid_price,
                 "price per share must be positive",
                 "price_per_share_wei"};
  if (request.order.maker_amount.is_zero() ||
      request.order.taker_amount.is_zero())
    return Error{ErrorCode::invalid_quantity,
                 "maker and taker amounts must be positive", "order"};
  if (request.strategy == ExecutionStrategy::market &&
      request.is_post_only.value_or(false))
    return Error{ErrorCode::invalid_argument,
                 "market orders cannot be post-only", "is_post_only"};
  return request.order_hash;
}

Result<std::string> validate_order_ids(const std::vector<std::string> &ids) {
  if (ids.empty() || ids.size() > 100U)
    return Error{ids.empty() ? ErrorCode::invalid_argument
                             : ErrorCode::too_many_items,
                 "order id list must contain between 1 and 100 entries",
                 "ids"};
  if (!std::ranges::all_of(ids, safe_id))
    return Error{ErrorCode::invalid_argument, "invalid order id", "ids"};
  return "ids:" + ids.front() + ":" + std::to_string(ids.size());
}

Result<std::string>
validate_order_hashes(const std::vector<std::string> &hashes) {
  if (hashes.empty() || hashes.size() > 100U)
    return Error{hashes.empty() ? ErrorCode::invalid_argument
                                : ErrorCode::too_many_items,
                 "order hash list must contain between 1 and 100 entries",
                 "hashes"};
  if (!std::ranges::all_of(hashes, hash32))
    return Error{ErrorCode::invalid_argument,
                 "every order hash must be 0x followed by 64 hex characters",
                 "hashes"};
  return "hashes:" + hashes.front() + ":" +
         std::to_string(hashes.size());
}

} // namespace protocol

struct TradingClient::Impl : public std::enable_shared_from_this<Impl> {
  using Decoder = std::function<Result<CreateOrderReceipt>(std::string_view)>;

  Impl(asio::any_io_executor executor_value,
       std::shared_ptr<net::HttpTransport> transport_value,
       ClientOptions options_value)
      : executor(std::move(executor_value)),
        transport(std::move(transport_value)), options(std::move(options_value)),
        limiter(options.rate_limits) {
    if (!transport)
      throw std::invalid_argument("Predict.fun trading transport is required");
    if (!options.jwt)
      throw std::invalid_argument("Predict.fun JWT provider is required");
  }

  template <class Receipt> struct Operation
      : public std::enable_shared_from_this<Operation<Receipt>> {
    std::shared_ptr<Impl> owner;
    std::string endpoint;
    std::string target;
    std::string body;
    std::string reconciliation_key;
    net::RequestContext context;
    Handler<MutationOutcome<Receipt>> handler;
    std::function<Result<Receipt>(std::string_view)> decode;
    std::shared_ptr<asio::steady_timer> timer;
    std::optional<std::stop_callback<std::function<void()>>> stop_callback;
    bool dispatched{false};

    void start() {
      if (!handler) return;
      if (context.cancel.stop_requested())
        return finish(Error{ErrorCode::cancelled, "request cancelled", {}});
      const auto now = std::chrono::steady_clock::now();
      if (context.deadline != std::chrono::steady_clock::time_point{} &&
          context.deadline <= now)
        return finish(Error{ErrorCode::deadline_exceeded,
                            "request deadline exceeded", {}});
      const auto wait = owner->limiter.reserve(endpoint, now);
      if (wait.count() == 0) return send();
      if (context.deadline != std::chrono::steady_clock::time_point{} &&
          now + wait >= context.deadline)
        return finish(Error{ErrorCode::deadline_exceeded,
                            "rate limit wait exceeds request deadline", {}});
      timer = std::make_shared<asio::steady_timer>(owner->executor);
      timer->expires_after(wait);
      timer->async_wait([self = this->shared_from_this()](
                            const boost::system::error_code &error) {
        if (error)
          return self->finish(
              Error{ErrorCode::cancelled, "request wait cancelled", {}});
        self->send();
      });
    }

    void send() {
      if (!handler) return;
      if (context.cancel.stop_requested())
        return finish(Error{ErrorCode::cancelled, "request cancelled", {}});
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
      if (owner->options.environment == Environment::bnb_mainnet && key.empty())
        return finish(Error{ErrorCode::authentication_required,
                            "Predict.fun mainnet requires an API key", {}});
      if (jwt.empty()) {
        secure_erase(key);
        return finish(Error{ErrorCode::authentication_required,
                            "Predict.fun trading endpoint requires a JWT", {}});
      }

      net::HttpRequest request;
      request.host = owner->options.environment == Environment::bnb_mainnet
                         ? "api.predict.fun"
                         : "api-testnet.predict.fun";
      request.target = target;
      request.use_tls = true;
      request.body = body;
      if (!key.empty()) request.headers.push_back({"x-api-key", std::move(key)});
      request.headers.push_back(
          {"Authorization", "Bearer " + std::string{jwt.view()}});
      jwt.clear();
      dispatched = true;
      owner->transport->async_post(
          std::move(request), context,
          [self = this->shared_from_this()](Result<net::HttpResponse> result) {
            self->handle(std::move(result));
          });
    }

    void handle(Result<net::HttpResponse> result) {
      if (!result)
        return finish(ambiguous<Receipt>(result.error(), reconciliation_key));
      const auto &response = result.value();
      if (response.status >= 200 && response.status < 300) {
        auto parsed = decode(response.body);
        if (!parsed)
          return finish(ambiguous<Receipt>(parsed.error(), reconciliation_key));
        return finish(MutationOutcome<Receipt>{
            MutationDisposition::acknowledged, std::move(parsed.value()),
            std::nullopt, reconciliation_key});
      }
      if (response.status >= 300 && response.status < 400)
        return finish(http_error(ErrorCode::http_redirect, response.status,
                                 "HTTP redirect rejected"));
      if (response.status == 401 || response.status == 403)
        return finish(http_error(ErrorCode::authentication_required,
                                 response.status,
                                 "Predict.fun authentication rejected"));
      if (response.status == 429 || response.status >= 500) {
        const auto code = response.status == 429 ? ErrorCode::rate_limited
                                                 : ErrorCode::http_server_error;
        return finish(ambiguous<Receipt>(
            http_error(code, response.status,
                       response.status == 429 ? "Predict.fun rate limit exceeded"
                                              : "Predict.fun server error"),
            reconciliation_key));
      }
      return finish(http_error(response.status == 410
                                   ? ErrorCode::venue_rejected
                                   : ErrorCode::http_client_error,
                               response.status,
                               "Predict.fun mutation rejected"));
    }

    void arm_cancellation() {
      stop_callback.emplace(
          context.cancel,
          std::function<void()>{[weak = this->weak_from_this(),
                                 executor = owner->executor] {
            asio::dispatch(executor, [weak] {
              if (auto self = weak.lock()) {
                if (self->timer) self->timer->cancel();
                if (self->dispatched)
                  self->finish(ambiguous<Receipt>(
                      Error{ErrorCode::cancelled,
                            "request cancelled after mutation dispatch", {}},
                      self->reconciliation_key));
                else
                  self->finish(
                      Error{ErrorCode::cancelled, "request cancelled", {}});
              }
            });
          }});
    }

    void finish(Result<MutationOutcome<Receipt>> result) {
      if (!handler) return;
      auto completion = std::move(handler);
      completion(std::move(result));
    }
  };

  template <class Receipt>
  void post(std::string endpoint, std::string target, Result<std::string> body,
            Result<std::string> reconciliation_key,
            net::RequestContext context,
            Handler<MutationOutcome<Receipt>> handler,
            std::function<Result<Receipt>(std::string_view)> decode) {
    if (!reconciliation_key || !body) {
      auto error = !reconciliation_key ? reconciliation_key.error() : body.error();
      asio::dispatch(executor,
                     [handler = std::move(handler),
                      error = std::move(error)]() mutable {
                       handler(std::move(error));
                     });
      return;
    }
    auto operation = std::make_shared<Operation<Receipt>>();
    operation->owner = shared_from_this();
    operation->endpoint = std::move(endpoint);
    operation->target = std::move(target);
    operation->body = std::move(body.value());
    operation->reconciliation_key = std::move(reconciliation_key.value());
    operation->context = std::move(context);
    operation->handler = std::move(handler);
    operation->decode = std::move(decode);
    operation->arm_cancellation();
    asio::dispatch(executor, [operation] { operation->start(); });
  }

  asio::any_io_executor executor;
  std::shared_ptr<net::HttpTransport> transport;
  ClientOptions options;
  net::RateLimiter limiter;
};

TradingClient::TradingClient(asio::any_io_executor executor,
                             std::shared_ptr<net::HttpTransport> transport,
                             ClientOptions options)
    : impl_(std::make_shared<Impl>(std::move(executor), std::move(transport),
                                   std::move(options))) {}
TradingClient::~TradingClient() = default;
TradingClient::TradingClient(TradingClient &&) noexcept = default;
TradingClient &TradingClient::operator=(TradingClient &&) noexcept = default;

void TradingClient::async_create_order(
    CreateOrderRequest request, net::RequestContext context,
    Handler<MutationOutcome<CreateOrderReceipt>> handler) {
  const auto limits = impl_->options.decode_limits;
  auto validation = protocol::validate_create_order(request);
  auto body = validation ? codec::encode_create_order_request(request)
                         : Result<std::string>{validation.error()};
  impl_->post<CreateOrderReceipt>(
      "create_order", "/v1/orders", std::move(body), std::move(validation),
      std::move(context), std::move(handler),
      [limits](std::string_view json) {
        return codec::decode_create_order_response(json, limits);
      });
}

void TradingClient::async_remove_order_ids(
    std::vector<std::string> ids, net::RequestContext context,
    Handler<MutationOutcome<RemoveOrdersReceipt>> handler) {
  const auto limits = impl_->options.decode_limits;
  auto validation = protocol::validate_order_ids(ids);
  auto body = validation ? codec::encode_remove_order_ids_request(ids)
                         : Result<std::string>{validation.error()};
  impl_->post<RemoveOrdersReceipt>(
      "remove_order_ids", "/v1/orders/remove", std::move(body),
      std::move(validation), std::move(context), std::move(handler),
      [limits](std::string_view json) {
        return codec::decode_remove_orders_response(json, limits);
      });
}

void TradingClient::async_remove_order_hashes(
    std::vector<std::string> hashes, net::RequestContext context,
    Handler<MutationOutcome<RemoveOrdersReceipt>> handler) {
  const auto limits = impl_->options.decode_limits;
  auto validation = protocol::validate_order_hashes(hashes);
  auto body = validation ? codec::encode_remove_order_hashes_request(hashes)
                         : Result<std::string>{validation.error()};
  impl_->post<RemoveOrdersReceipt>(
      "remove_order_hashes", "/orders/remove-by-hash", std::move(body),
      std::move(validation), std::move(context), std::move(handler),
      [limits](std::string_view json) {
        return codec::decode_remove_order_hashes_response(json, limits);
      });
}

} // namespace predictfun::trading
