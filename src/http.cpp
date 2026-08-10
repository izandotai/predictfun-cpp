#include "predictfun/net/http.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#ifdef _WIN32
#include <wincrypt.h>
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <memory>
#include <optional>
#include <utility>

namespace predictfun::net {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using TlsStream = beast::ssl_stream<beast::tcp_stream>;

bool iequals(std::string_view left, std::string_view right) noexcept {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](char a, char b) {
                      return std::tolower(static_cast<unsigned char>(a)) ==
                             std::tolower(static_cast<unsigned char>(b));
                    });
}

#ifdef _WIN32
void load_root_certificates(ssl::context &context) {
  X509_STORE *store = SSL_CTX_get_cert_store(context.native_handle());
  HCERTSTORE windows_store = CertOpenSystemStoreA(0, "ROOT");
  if (windows_store == nullptr)
    return;
  PCCERT_CONTEXT certificate = nullptr;
  while ((certificate = CertEnumCertificatesInStore(windows_store,
                                                    certificate)) != nullptr) {
    const unsigned char *bytes = certificate->pbCertEncoded;
    if (X509 *x509 = d2i_X509(nullptr, &bytes,
                              static_cast<long>(certificate->cbCertEncoded))) {
      (void)X509_STORE_add_cert(store, x509);
      X509_free(x509);
    }
  }
  CertCloseStore(windows_store, 0);
}
#else
void load_root_certificates(ssl::context &context) {
  (void)SSL_CTX_set_default_verify_paths(context.native_handle());
}
#endif

std::shared_ptr<ssl::context> make_tls_context() {
  auto context = std::make_shared<ssl::context>(ssl::context::tls_client);
  context->set_options(ssl::context::default_workarounds |
                       ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                       ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);
  context->set_verify_mode(ssl::verify_peer);
  load_root_certificates(*context);
  return context;
}

Error transport_error(ErrorCode code, std::string_view stage,
                      const beast::error_code &error) {
  return Error{code,
               std::format("{} failed: ec={} ({})", stage, error.value(),
                           error.category().name()),
               {}};
}

class HttpSession final : public std::enable_shared_from_this<HttpSession> {
public:
  HttpSession(asio::any_io_executor executor,
              std::shared_ptr<ssl::context> tls_context, TransportLimits limits,
              HttpRequest request, RequestContext context,
              ResponseHandler handler)
      : executor_(std::move(executor)), tls_context_(std::move(tls_context)),
        limits_(limits), request_(std::move(request)),
        context_(std::move(context)), handler_(std::move(handler)),
        resolver_(executor_), deadline_timer_(executor_) {}

  void start() {
    if (context_.cancel.stop_requested()) {
      return finish(Error{ErrorCode::cancelled, "request cancelled", {}});
    }
    if (request_.host.empty() || request_.port.empty() ||
        request_.target.empty() || request_.target.front() != '/') {
      return finish(Error{ErrorCode::invalid_argument,
                          "HTTP host, port and absolute target are required",
                          "request"});
    }
    std::string lowered_target = request_.target;
    std::ranges::transform(lowered_target, lowered_target.begin(),
                           [](unsigned char value) {
                             return static_cast<char>(std::tolower(value));
                           });
    if (lowered_target.find("x-api-key=") != std::string::npos ||
        lowered_target.find("api_key=") != std::string::npos ||
        lowered_target.find("apikey=") != std::string::npos) {
      return finish(Error{ErrorCode::invalid_argument,
                          "credentials are forbidden in HTTP targets",
                          "request.target"});
    }
    if (context_.deadline == std::chrono::steady_clock::time_point{})
      context_.deadline =
          std::chrono::steady_clock::now() + limits_.default_timeout;
    if (context_.deadline <= std::chrono::steady_clock::now()) {
      return finish(Error{ErrorCode::deadline_exceeded,
                          "request deadline already expired",
                          {}});
    }

    stop_callback_.emplace(context_.cancel,
                           std::function<void()>{[weak = weak_from_this(),
                                                  executor = executor_]() {
                             asio::dispatch(executor, [weak] {
                               if (auto self = weak.lock()) {
                                 self->cancelled_ = true;
                                 self->cancel_io();
                               }
                             });
                           }});
    deadline_timer_.expires_at(context_.deadline);
    deadline_timer_.async_wait(
        [self = shared_from_this()](const beast::error_code &ec) {
          if (!ec) {
            self->deadline_fired_ = true;
            self->cancel_io();
          }
        });

    if (request_.use_tls) {
      tls_stream_ = std::make_unique<TlsStream>(executor_, *tls_context_);
      if (!SSL_set_tlsext_host_name(tls_stream_->native_handle(),
                                    request_.host.c_str()) ||
          !SSL_set1_host(tls_stream_->native_handle(), request_.host.c_str())) {
        return finish(Error{ErrorCode::tls_failure,
                            "TLS hostname verification setup failed",
                            {}});
      }
    } else {
      plain_stream_ = std::make_unique<beast::tcp_stream>(executor_);
    }

    resolver_.async_resolve(
        request_.host, request_.port,
        [self = shared_from_this()](const beast::error_code &ec,
                                    tcp::resolver::results_type results) {
          if (ec)
            return self->fail(ErrorCode::dns_failure, "resolve", ec);
          self->connect(std::move(results));
        });
  }

private:
  void connect(tcp::resolver::results_type results) {
    if (tls_stream_) {
      beast::get_lowest_layer(*tls_stream_).expires_at(context_.deadline);
      beast::get_lowest_layer(*tls_stream_)
          .async_connect(
              results, [self = shared_from_this()](
                           const beast::error_code &ec,
                           const tcp::resolver::results_type::endpoint_type &) {
                if (ec)
                  return self->fail(ErrorCode::connect_failure, "connect", ec);
                self->handshake();
              });
      return;
    }
    plain_stream_->expires_at(context_.deadline);
    plain_stream_->async_connect(
        results, [self = shared_from_this()](
                     const beast::error_code &ec,
                     const tcp::resolver::results_type::endpoint_type &) {
          if (ec)
            return self->fail(ErrorCode::connect_failure, "connect", ec);
          self->write();
        });
  }

  void handshake() {
    tls_stream_->async_handshake(
        ssl::stream_base::client,
        [self = shared_from_this()](const beast::error_code &ec) {
          if (ec)
            return self->fail(ErrorCode::tls_failure, "TLS handshake", ec);
          self->write();
        });
  }

  void write() {
    wire_request_ =
        http::request<http::empty_body>{http::verb::get, request_.target, 11};
    wire_request_.set(http::field::host, request_.host);
    wire_request_.set(http::field::user_agent, "predictfun-cpp/0.1");
    wire_request_.set(http::field::accept, "application/json");
    wire_request_.keep_alive(false);
    for (const auto &header : request_.headers)
      wire_request_.set(header.name, header.value);

    auto callback = [self = shared_from_this()](const beast::error_code &ec,
                                                std::size_t) {
      if (ec)
        return self->fail(ErrorCode::write_failure, "write", ec);
      self->read();
    };
    if (tls_stream_)
      http::async_write(*tls_stream_, wire_request_, std::move(callback));
    else
      http::async_write(*plain_stream_, wire_request_, std::move(callback));
  }

  void read() {
    parser_.body_limit(limits_.max_body_bytes);
    auto callback = [self = shared_from_this()](const beast::error_code &ec,
                                                std::size_t) {
      if (ec == http::error::body_limit) {
        return self->finish(Error{ErrorCode::body_too_large,
                                  "HTTP body exceeds configured limit",
                                  {}});
      }
      if (ec == http::error::partial_message) {
        return self->finish(Error{
            ErrorCode::body_truncated, "HTTP response body was truncated", {}});
      }
      if (ec)
        return self->fail(ErrorCode::read_failure, "read", ec);
      self->complete_response();
    };
    if (tls_stream_)
      http::async_read(*tls_stream_, buffer_, parser_, std::move(callback));
    else
      http::async_read(*plain_stream_, buffer_, parser_, std::move(callback));
  }

  void complete_response() {
    auto wire = parser_.release();
    HttpResponse response;
    response.status = static_cast<int>(wire.result_int());
    response.body = std::move(wire.body());
    for (const auto &field : wire.base())
      response.headers.push_back(
          Header{std::string(field.name_string()), std::string(field.value())});
    finish(std::move(response));
  }

  void fail(ErrorCode code, std::string_view stage,
            const beast::error_code &ec) {
    if (cancelled_ || context_.cancel.stop_requested())
      return finish(Error{ErrorCode::cancelled, "request cancelled", {}});
    if (deadline_fired_ || ec == beast::error::timeout)
      return finish(
          Error{ErrorCode::deadline_exceeded, "request deadline exceeded", {}});
    finish(transport_error(code, stage, ec));
  }

  void cancel_io() {
    resolver_.cancel();
    beast::error_code ignored;
    if (tls_stream_)
      beast::get_lowest_layer(*tls_stream_).socket().close(ignored);
    if (plain_stream_)
      plain_stream_->socket().close(ignored);
  }

  void finish(Result<HttpResponse> result) {
    if (finished_.exchange(true))
      return;
    deadline_timer_.cancel();
    stop_callback_.reset();
    cancel_io();
    auto handler = std::move(handler_);
    asio::dispatch(executor_, [handler = std::move(handler),
                               result = std::move(result)]() mutable {
      handler(std::move(result));
    });
  }

  asio::any_io_executor executor_;
  std::shared_ptr<ssl::context> tls_context_;
  TransportLimits limits_;
  HttpRequest request_;
  RequestContext context_;
  ResponseHandler handler_;
  tcp::resolver resolver_;
  asio::steady_timer deadline_timer_;
  std::unique_ptr<TlsStream> tls_stream_;
  std::unique_ptr<beast::tcp_stream> plain_stream_;
  http::request<http::empty_body> wire_request_;
  beast::flat_buffer buffer_;
  http::response_parser<http::string_body> parser_;
  std::optional<std::stop_callback<std::function<void()>>> stop_callback_;
  std::atomic<bool> finished_{false};
  bool cancelled_{false};
  bool deadline_fired_{false};
};

} // namespace

struct BeastHttpTransport::Impl {
  explicit Impl(asio::any_io_executor executor_value,
                TransportLimits limits_value)
      : executor(std::move(executor_value)), limits(limits_value),
        tls_context(make_tls_context()) {}

  asio::any_io_executor executor;
  TransportLimits limits;
  std::shared_ptr<ssl::context> tls_context;
};

std::string_view HttpResponse::header(std::string_view name) const noexcept {
  const auto found =
      std::find_if(headers.begin(), headers.end(), [name](const Header &item) {
        return iequals(item.name, name);
      });
  return found == headers.end() ? std::string_view{} : found->value;
}

RequestContext RequestContext::with_timeout(std::chrono::milliseconds timeout,
                                            std::stop_token cancel_value) {
  RequestContext context;
  context.deadline = std::chrono::steady_clock::now() + timeout;
  context.cancel = cancel_value;
  return context;
}

BeastHttpTransport::BeastHttpTransport(asio::any_io_executor executor,
                                       TransportLimits limits)
    : impl_(std::make_shared<Impl>(std::move(executor), limits)) {}

BeastHttpTransport::~BeastHttpTransport() = default;

void BeastHttpTransport::async_get(HttpRequest request, RequestContext context,
                                   ResponseHandler handler) {
  if (!handler)
    return;
  auto session = std::make_shared<HttpSession>(
      impl_->executor, impl_->tls_context, impl_->limits, std::move(request),
      std::move(context), std::move(handler));
  asio::dispatch(impl_->executor, [session] { session->start(); });
}

bool is_secret_header(std::string_view name) noexcept {
  return iequals(name, "x-api-key") || iequals(name, "authorization") ||
         iequals(name, "cookie") || iequals(name, "set-cookie");
}

std::string sanitized_request_summary(const HttpRequest &request) {
  const auto query = request.target.find('?');
  const auto path = request.target.substr(0, query);
  return std::format("GET {}:{}{}", request.host, request.port, path);
}

} // namespace predictfun::net
