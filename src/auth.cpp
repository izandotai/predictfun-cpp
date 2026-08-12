#include "predictfun/auth/client.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <utility>

namespace predictfun::auth {
namespace {

namespace asio = boost::asio;

Error http_error(ErrorCode code, int status, std::string message,
                 std::chrono::milliseconds retry = {}) {
  Error error{code, std::move(message), {}};
  error.http_status = status;
  error.retry_after_ms = static_cast<std::uint64_t>(
      std::max<std::int64_t>(0, retry.count()));
  return error;
}

} // namespace

struct AuthClient::Impl : public std::enable_shared_from_this<Impl> {
  using RawHandler = std::function<void(Result<net::HttpResponse>)>;

  Impl(asio::any_io_executor executor_value,
       std::shared_ptr<net::HttpTransport> transport_value,
       ClientOptions options_value)
      : executor(std::move(executor_value)),
        transport(std::move(transport_value)), options(std::move(options_value)),
        limiter(options.rate_limiter
                    ? options.rate_limiter
                    : std::make_shared<net::RateLimiter>(options.rate_limits)) {
    if (!transport)
      throw std::invalid_argument("Predict.fun auth transport is required");
  }

  void request(net::HttpMethod method, std::string target, std::string body,
               net::RequestContext context, RawHandler handler) {
    asio::dispatch(executor,
                   [self = shared_from_this(), method, target = std::move(target),
                    body = std::move(body), context = std::move(context),
                    handler = std::move(handler)]() mutable {
                     self->reserve_and_send(
                         method, std::move(target), std::move(body),
                         std::move(context), std::move(handler));
                   });
  }

  void reserve_and_send(net::HttpMethod method, std::string target,
                        std::string body, net::RequestContext context,
                        RawHandler handler) {
    if (context.cancel.stop_requested())
      return handler(Error{ErrorCode::cancelled, "request cancelled", {}});
    const auto now = std::chrono::steady_clock::now();
    if (context.deadline != std::chrono::steady_clock::time_point{} &&
        context.deadline <= now)
      return handler(Error{ErrorCode::deadline_exceeded,
                           "request deadline exceeded", {}});
    const auto wait = limiter->reserve(target, now);
    if (wait == std::chrono::milliseconds::zero())
      return send(method, std::move(target), std::move(body),
                  std::move(context), std::move(handler));
    if (context.deadline != std::chrono::steady_clock::time_point{} &&
        now + wait >= context.deadline)
      return handler(Error{ErrorCode::deadline_exceeded,
                           "rate limit wait exceeds request deadline", {}});
    auto timer = std::make_shared<asio::steady_timer>(executor);
    timer->expires_after(wait);
    auto cancellation = std::make_shared<
        std::optional<std::stop_callback<std::function<void()>>>>();
    cancellation->emplace(
        context.cancel,
        std::function<void()>{[executor = executor, timer] {
          asio::dispatch(executor, [timer] { timer->cancel(); });
        }});
    timer->async_wait(
        [self = shared_from_this(), timer, cancellation, method,
         target = std::move(target), body = std::move(body),
         context = std::move(context),
         handler = std::move(handler)](
            const boost::system::error_code &error) mutable {
          cancellation->reset();
          if (error)
            return handler(
                Error{ErrorCode::cancelled, "request wait cancelled", {}});
          self->send(method, std::move(target), std::move(body),
                     std::move(context), std::move(handler));
        });
  }

  void send(net::HttpMethod method, std::string target, std::string body,
            net::RequestContext context, RawHandler handler) {
    if (context.cancel.stop_requested())
      return handler(Error{ErrorCode::cancelled, "request cancelled", {}});
    if (context.deadline != std::chrono::steady_clock::time_point{} &&
        context.deadline <= std::chrono::steady_clock::now()) {
      return handler(Error{ErrorCode::deadline_exceeded,
                           "request deadline exceeded", {}});
    }

    std::string key;
    if (options.api_key) {
      try {
        key = options.api_key();
      } catch (...) {
        return handler(Error{ErrorCode::authentication_required,
                             "API key provider failed", {}});
      }
    }
    if (options.environment == Environment::bnb_mainnet && key.empty()) {
      return handler(Error{ErrorCode::authentication_required,
                           "Predict.fun mainnet requires an API key", {}});
    }

    net::HttpRequest request;
    request.method = method;
    request.host = options.environment == Environment::bnb_mainnet
                       ? "api.predict.fun"
                       : "api-testnet.predict.fun";
    request.target = std::move(target);
    request.use_tls = true;
    request.body = std::move(body);
    if (!key.empty())
      request.headers.push_back(net::Header{"x-api-key", std::move(key)});

    const auto endpoint = request.target;
    transport->async_request(
        std::move(request), std::move(context),
        [self = shared_from_this(), endpoint,
         handler = std::move(handler)](
            Result<net::HttpResponse> result) mutable {
          if (!result)
            return handler(result.error());
          const auto status = result.value().status;
          if (status >= 200 && status < 300)
            return handler(std::move(result));
          if (status >= 300 && status < 400) {
            return handler(http_error(ErrorCode::http_redirect, status,
                                      "HTTP redirect rejected"));
          }
          if (status == 401 || status == 403) {
            return handler(http_error(ErrorCode::authentication_required,
                                      status,
                                      "Predict.fun authentication rejected"));
          }
          if (status == 429) {
            const auto retry = net::parse_retry_after(
                result.value().header("Retry-After"),
                std::chrono::system_clock::now(),
                std::chrono::milliseconds{500});
            self->limiter->penalize(endpoint,
                                    std::chrono::steady_clock::now(), retry);
            return handler(http_error(ErrorCode::rate_limited, status,
                                      "Predict.fun rate limit exceeded",
                                      retry));
          }
          if (status >= 500) {
            return handler(http_error(ErrorCode::http_server_error, status,
                                      "Predict.fun server error"));
          }
          return handler(http_error(ErrorCode::http_client_error, status,
                                    "Predict.fun request rejected"));
        });
  }

  asio::any_io_executor executor;
  std::shared_ptr<net::HttpTransport> transport;
  ClientOptions options;
  std::shared_ptr<net::RateLimiter> limiter;
};

AuthClient::AuthClient(asio::any_io_executor executor,
                       std::shared_ptr<net::HttpTransport> transport,
                       ClientOptions options)
    : impl_(std::make_shared<Impl>(std::move(executor), std::move(transport),
                                   std::move(options))) {}

AuthClient::~AuthClient() = default;
AuthClient::AuthClient(AuthClient &&) noexcept = default;
AuthClient &AuthClient::operator=(AuthClient &&) noexcept = default;

void AuthClient::async_get_message(net::RequestContext context,
                                   Handler<AuthMessage> handler) {
  const auto limits = impl_->options.codec_limits;
  impl_->request(net::HttpMethod::get, std::string{protocol::auth_message_target()},
                 {}, std::move(context),
                 [handler = std::move(handler),
                  limits](Result<net::HttpResponse> raw) mutable {
                   if (!raw)
                     return handler(raw.error());
                   handler(codec::decode_auth_message_response(
                       raw.value().body, limits));
                 });
}

void AuthClient::async_exchange_token(AuthProof proof,
                                      net::RequestContext context,
                                      Handler<WalletJwt> handler) {
  const auto limits = impl_->options.codec_limits;
  auto body = codec::encode_auth_proof(proof, limits);
  if (!body) {
    asio::dispatch(impl_->executor,
                   [handler = std::move(handler), error = body.error()]() mutable {
                     handler(std::move(error));
                   });
    return;
  }
  impl_->request(net::HttpMethod::post,
                 std::string{protocol::auth_exchange_target()},
                 std::move(body.value()), std::move(context),
                 [handler = std::move(handler),
                  limits](Result<net::HttpResponse> raw) mutable {
                   if (!raw)
                     return handler(raw.error());
                   handler(codec::decode_auth_token_response(raw.value().body,
                                                             limits));
                 });
}

void AuthClient::async_authenticate(std::shared_ptr<MessageSigner> signer,
                                    net::RequestContext context,
                                    Handler<WalletJwt> handler) {
  if (!signer) {
    asio::dispatch(impl_->executor,
                   [handler = std::move(handler)]() mutable {
                     handler(Error{ErrorCode::invalid_argument,
                                   "message signer is required", "signer"});
                   });
    return;
  }

  auto shared_context = std::make_shared<net::RequestContext>(context);
  auto owner = impl_;
  async_get_message(
      std::move(context),
      [owner, signer = std::move(signer), shared_context,
       handler = std::move(handler)](Result<AuthMessage> message) mutable {
        if (!message)
          return handler(message.error());
        if (shared_context->cancel.stop_requested()) {
          return handler(
              Error{ErrorCode::cancelled, "authentication cancelled", {}});
        }
        auto challenge =
            std::make_shared<std::string>(std::move(message.value().message));
        try {
          signer->async_sign_message(
              *challenge,
              [owner, signer, shared_context, challenge,
               handler = std::move(handler)](
                  Result<std::string> signature) mutable {
                if (!signature)
                  return handler(signature.error());
                if (shared_context->cancel.stop_requested()) {
                  return handler(Error{ErrorCode::cancelled,
                                       "authentication cancelled", {}});
                }
                AuthProof proof{signer->signer_address(),
                                std::move(signature.value()),
                                std::move(*challenge)};
                const auto limits = owner->options.codec_limits;
                auto body = codec::encode_auth_proof(proof, limits);
                if (!body)
                  return handler(body.error());
                owner->request(
                    net::HttpMethod::post,
                    std::string{protocol::auth_exchange_target()},
                    std::move(body.value()), *shared_context,
                    [handler = std::move(handler),
                     limits](Result<net::HttpResponse> raw) mutable {
                      if (!raw)
                        return handler(raw.error());
                      handler(codec::decode_auth_token_response(
                          raw.value().body, limits));
                    });
              });
        } catch (...) {
          handler(Error{ErrorCode::authentication_required,
                        "message signer failed", "signer"});
        }
      });
}

} // namespace predictfun::auth
