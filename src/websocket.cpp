#include "predictfun/net/websocket.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#ifdef _WIN32
#include <wincrypt.h>
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <deque>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>

namespace predictfun::net {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using PlainWebSocket = websocket::stream<beast::tcp_stream>;
using TlsWebSocket = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

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

Error channel_error(ErrorCode code, std::string_view stage,
                    const beast::error_code &error) {
  return Error{code,
               std::format("{} failed: ec={} ({})", stage, error.value(),
                           error.category().name()),
               {}};
}

Error closed_error() {
  return Error{ErrorCode::websocket_closed, "WebSocket is not open", {}};
}

bool target_contains_secret(std::string target) {
  std::ranges::transform(target, target.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  return target.find("apikey=") != std::string::npos ||
         target.find("api_key=") != std::string::npos ||
         target.find("x-api-key=") != std::string::npos ||
         target.find("jwt=") != std::string::npos ||
         target.find("token=") != std::string::npos;
}

struct PendingWrite {
  std::string text;
  WriteHandler handler;
};

} // namespace

struct BeastWebSocketChannel::Impl
    : public std::enable_shared_from_this<BeastWebSocketChannel::Impl> {
  Impl(asio::any_io_executor executor_value, WebSocketLimits limits_value)
      : executor(std::move(executor_value)), limits(limits_value),
        tls_context(make_tls_context()), resolver(executor),
        deadline_timer(executor) {}

  void open(WebSocketRequest request_value, RequestContext context_value,
            OpenHandler handler_value) {
    if (opening || opened) {
      return dispatch_open(std::move(handler_value),
                           Error{ErrorCode::protocol_error,
                                 "WebSocket channel is already active", {}});
    }
    request = std::move(request_value);
    context = std::move(context_value);
    open_handler = std::move(handler_value);
    opening = true;
    cancelled = false;
    deadline_fired = false;

    if (context.cancel.stop_requested())
      return finish_open(Error{ErrorCode::cancelled,
                               "WebSocket connect cancelled", {}});
    if (request.host.empty() || request.port.empty() ||
        request.target.empty() || request.target.front() != '/') {
      return finish_open(Error{ErrorCode::invalid_argument,
                               "WebSocket host, port and target are required",
                               "request"});
    }
    if (target_contains_secret(request.target)) {
      return finish_open(Error{ErrorCode::invalid_argument,
                               "credentials are forbidden in WebSocket targets",
                               "request.target"});
    }
    if (context.deadline == std::chrono::steady_clock::time_point{}) {
      context.deadline = std::chrono::steady_clock::now() +
                         limits.default_connect_timeout;
    }
    if (context.deadline <= std::chrono::steady_clock::now()) {
      return finish_open(Error{ErrorCode::deadline_exceeded,
                               "WebSocket connect deadline already expired",
                               {}});
    }

    stop_callback.emplace(
        context.cancel,
        std::function<void()>{[weak = weak_from_this(), executor = executor] {
          asio::dispatch(executor, [weak] {
            if (auto self = weak.lock()) {
              self->cancelled = true;
              self->cancel_io();
            }
          });
        }});
    deadline_timer.expires_at(context.deadline);
    deadline_timer.async_wait(
        [self = shared_from_this()](const beast::error_code &error) {
          if (!error) {
            self->deadline_fired = true;
            self->cancel_io();
          }
        });

    if (request.use_tls) {
      tls_stream =
          std::make_unique<TlsWebSocket>(executor, *tls_context);
      if (!SSL_set_tlsext_host_name(tls_stream->next_layer().native_handle(),
                                    request.host.c_str()) ||
          !SSL_set1_host(tls_stream->next_layer().native_handle(),
                         request.host.c_str())) {
        return finish_open(Error{ErrorCode::tls_failure,
                                 "TLS hostname verification setup failed", {}});
      }
      configure(*tls_stream);
    } else {
      plain_stream = std::make_unique<PlainWebSocket>(executor);
      configure(*plain_stream);
    }

    resolver.async_resolve(
        request.host, request.port,
        [self = shared_from_this()](const beast::error_code &error,
                                    tcp::resolver::results_type results) {
          if (error)
            return self->fail_open(ErrorCode::dns_failure, "resolve", error);
          self->connect(std::move(results));
        });
  }

  template <class Stream> void configure(Stream &stream) {
    stream.read_message_max(limits.max_frame_bytes);
    const auto headers = request.headers;
    stream.set_option(websocket::stream_base::decorator(
        [headers](websocket::request_type &wire) {
          wire.set(http::field::user_agent, "predictfun-cpp/0.1");
          for (const auto &header : headers)
            wire.set(header.name, header.value);
        }));
  }

  void connect(tcp::resolver::results_type results) {
    if (tls_stream) {
      beast::get_lowest_layer(*tls_stream).expires_at(context.deadline);
      beast::get_lowest_layer(*tls_stream)
          .async_connect(
              results,
              [self = shared_from_this()](
                  const beast::error_code &error,
                  const tcp::resolver::results_type::endpoint_type &) {
                if (error)
                  return self->fail_open(ErrorCode::connect_failure, "connect",
                                         error);
                self->tls_handshake();
              });
      return;
    }
    beast::get_lowest_layer(*plain_stream).expires_at(context.deadline);
    beast::get_lowest_layer(*plain_stream)
        .async_connect(
            results,
            [self = shared_from_this()](
                const beast::error_code &error,
                const tcp::resolver::results_type::endpoint_type &) {
              if (error)
                return self->fail_open(ErrorCode::connect_failure, "connect",
                                       error);
              self->websocket_handshake();
            });
  }

  void tls_handshake() {
    tls_stream->next_layer().async_handshake(
        ssl::stream_base::client,
        [self = shared_from_this()](const beast::error_code &error) {
          if (error)
            return self->fail_open(ErrorCode::tls_failure, "TLS handshake",
                                   error);
          self->websocket_handshake();
        });
  }

  void websocket_handshake() {
    auto callback = [self = shared_from_this()](const beast::error_code &error) {
      if (error) {
        auto result = channel_error(ErrorCode::websocket_handshake_failure,
                                    "WebSocket handshake", error);
        result.http_status =
            static_cast<int>(self->handshake_response.result_int());
        return self->finish_open(std::move(result));
      }
      self->opening = false;
      self->opened = true;
      self->deadline_timer.cancel();
      self->stop_callback.reset();
      if (self->tls_stream)
        beast::get_lowest_layer(*self->tls_stream).expires_never();
      if (self->plain_stream)
        beast::get_lowest_layer(*self->plain_stream).expires_never();
      self->finish_open(std::monostate{});
    };
    if (tls_stream) {
      tls_stream->async_handshake(handshake_response, request.host,
                                  request.target, std::move(callback));
    } else {
      plain_stream->async_handshake(handshake_response, request.host,
                                    request.target, std::move(callback));
    }
  }

  void read(ReadHandler handler) {
    if (!opened)
      return dispatch_read(std::move(handler), closed_error());
    if (read_active) {
      return dispatch_read(std::move(handler),
                           Error{ErrorCode::protocol_error,
                                 "only one WebSocket read may be active", {}});
    }
    read_active = true;
    auto callback = [self = shared_from_this(),
                     handler = std::move(handler)](
                        const beast::error_code &error, std::size_t) mutable {
      self->read_active = false;
      if (error) {
        self->opened = false;
        if (error == websocket::error::closed)
          return self->dispatch_read(std::move(handler), closed_error());
        if (error == websocket::error::message_too_big) {
          return self->dispatch_read(
              std::move(handler),
              Error{ErrorCode::websocket_frame_too_large,
                    "WebSocket frame exceeds configured limit", {}});
        }
        return self->dispatch_read(
            std::move(handler),
            channel_error(ErrorCode::read_failure, "WebSocket read", error));
      }
      const bool text = self->tls_stream ? self->tls_stream->got_text()
                                         : self->plain_stream->got_text();
      if (!text) {
        self->read_buffer.consume(self->read_buffer.size());
        return self->dispatch_read(
            std::move(handler),
            Error{ErrorCode::protocol_error,
                  "binary WebSocket frames are not supported", {}});
      }
      auto value = beast::buffers_to_string(self->read_buffer.data());
      self->read_buffer.consume(self->read_buffer.size());
      self->dispatch_read(std::move(handler), std::move(value));
    };
    if (tls_stream)
      tls_stream->async_read(read_buffer, std::move(callback));
    else
      plain_stream->async_read(read_buffer, std::move(callback));
  }

  void write(std::string text, WriteHandler handler) {
    if (!opened)
      return dispatch_write(std::move(handler), closed_error());
    if (text.size() > limits.max_frame_bytes) {
      return dispatch_write(
          std::move(handler),
          Error{ErrorCode::websocket_frame_too_large,
                "outbound WebSocket frame exceeds configured limit", {}});
    }
    writes.push_back(PendingWrite{std::move(text), std::move(handler)});
    if (!write_active)
      start_write();
  }

  void start_write() {
    if (writes.empty() || !opened)
      return;
    write_active = true;
    auto callback = [self = shared_from_this()](const beast::error_code &error,
                                                std::size_t) {
      self->write_active = false;
      auto pending = std::move(self->writes.front());
      self->writes.pop_front();
      if (error) {
        self->opened = false;
        self->dispatch_write(
            std::move(pending.handler),
            channel_error(ErrorCode::write_failure, "WebSocket write", error));
        self->fail_queued_writes(closed_error());
        return;
      }
      self->dispatch_write(std::move(pending.handler), std::monostate{});
      self->start_write();
    };
    if (tls_stream) {
      tls_stream->text(true);
      tls_stream->async_write(asio::buffer(writes.front().text),
                              std::move(callback));
    } else {
      plain_stream->text(true);
      plain_stream->async_write(asio::buffer(writes.front().text),
                                std::move(callback));
    }
  }

  void close(CloseHandler handler) {
    if (!opened) {
      cancel_io();
      return dispatch_close(std::move(handler), std::monostate{});
    }
    opened = false;
    auto callback = [self = shared_from_this(),
                     handler = std::move(handler)](
                        const beast::error_code &error) mutable {
      self->cancel_io();
      if (error && error != websocket::error::closed) {
        return self->dispatch_close(
            std::move(handler),
            channel_error(ErrorCode::websocket_closed, "WebSocket close",
                          error));
      }
      self->dispatch_close(std::move(handler), std::monostate{});
    };
    if (tls_stream)
      tls_stream->async_close(websocket::close_code::normal,
                              std::move(callback));
    else
      plain_stream->async_close(websocket::close_code::normal,
                                std::move(callback));
  }

  void cancel_io() {
    resolver.cancel();
    beast::error_code ignored;
    if (tls_stream)
      beast::get_lowest_layer(*tls_stream).socket().close(ignored);
    if (plain_stream)
      beast::get_lowest_layer(*plain_stream).socket().close(ignored);
    opened = false;
    opening = false;
  }

  void fail_open(ErrorCode code, std::string_view stage,
                 const beast::error_code &error) {
    if (cancelled || context.cancel.stop_requested()) {
      return finish_open(
          Error{ErrorCode::cancelled, "WebSocket connect cancelled", {}});
    }
    if (deadline_fired || error == beast::error::timeout) {
      return finish_open(Error{ErrorCode::deadline_exceeded,
                               "WebSocket connect deadline exceeded", {}});
    }
    finish_open(channel_error(code, stage, error));
  }

  void finish_open(Result<std::monostate> result) {
    deadline_timer.cancel();
    stop_callback.reset();
    if (!result) {
      opening = false;
      cancel_io();
    }
    auto handler = std::move(open_handler);
    if (handler)
      asio::dispatch(executor, [handler = std::move(handler),
                                result = std::move(result)]() mutable {
        handler(std::move(result));
      });
  }

  void fail_queued_writes(const Error &error) {
    while (!writes.empty()) {
      auto handler = std::move(writes.front().handler);
      writes.pop_front();
      dispatch_write(std::move(handler), error);
    }
  }

  void dispatch_open(OpenHandler handler, Result<std::monostate> result) {
    asio::dispatch(executor, [handler = std::move(handler),
                              result = std::move(result)]() mutable {
      if (handler)
        handler(std::move(result));
    });
  }

  void dispatch_read(ReadHandler handler, Result<std::string> result) {
    asio::dispatch(executor, [handler = std::move(handler),
                              result = std::move(result)]() mutable {
      if (handler)
        handler(std::move(result));
    });
  }

  void dispatch_write(WriteHandler handler, Result<std::monostate> result) {
    asio::dispatch(executor, [handler = std::move(handler),
                              result = std::move(result)]() mutable {
      if (handler)
        handler(std::move(result));
    });
  }

  void dispatch_close(CloseHandler handler, Result<std::monostate> result) {
    asio::dispatch(executor, [handler = std::move(handler),
                              result = std::move(result)]() mutable {
      if (handler)
        handler(std::move(result));
    });
  }

  asio::any_io_executor executor;
  WebSocketLimits limits;
  std::shared_ptr<ssl::context> tls_context;
  tcp::resolver resolver;
  asio::steady_timer deadline_timer;
  WebSocketRequest request;
  RequestContext context;
  OpenHandler open_handler;
  std::unique_ptr<PlainWebSocket> plain_stream;
  std::unique_ptr<TlsWebSocket> tls_stream;
  websocket::response_type handshake_response;
  beast::flat_buffer read_buffer;
  std::deque<PendingWrite> writes;
  std::optional<std::stop_callback<std::function<void()>>> stop_callback;
  bool opening{false};
  bool opened{false};
  bool read_active{false};
  bool write_active{false};
  bool cancelled{false};
  bool deadline_fired{false};
};

BeastWebSocketChannel::BeastWebSocketChannel(
    boost::asio::any_io_executor executor, WebSocketLimits limits)
    : impl_(std::make_shared<Impl>(std::move(executor), limits)) {}

BeastWebSocketChannel::~BeastWebSocketChannel() = default;

void BeastWebSocketChannel::async_open(WebSocketRequest request,
                                       RequestContext context,
                                       OpenHandler handler) {
  auto impl = impl_;
  asio::dispatch(impl->executor,
                 [impl, request = std::move(request),
                  context = std::move(context),
                  handler = std::move(handler)]() mutable {
                   impl->open(std::move(request), std::move(context),
                              std::move(handler));
                 });
}

void BeastWebSocketChannel::async_read(ReadHandler handler) {
  auto impl = impl_;
  asio::dispatch(impl->executor,
                 [impl, handler = std::move(handler)]() mutable {
                   impl->read(std::move(handler));
                 });
}

void BeastWebSocketChannel::async_write(std::string text,
                                        WriteHandler handler) {
  auto impl = impl_;
  asio::dispatch(impl->executor,
                 [impl, text = std::move(text),
                  handler = std::move(handler)]() mutable {
                   impl->write(std::move(text), std::move(handler));
                 });
}

void BeastWebSocketChannel::async_close(CloseHandler handler) {
  auto impl = impl_;
  asio::dispatch(impl->executor,
                 [impl, handler = std::move(handler)]() mutable {
                   impl->close(std::move(handler));
                 });
}

void BeastWebSocketChannel::cancel() {
  auto impl = impl_;
  asio::dispatch(impl->executor, [impl] { impl->cancel_io(); });
}

std::string sanitized_websocket_summary(const WebSocketRequest &request) {
  const auto query = request.target.find('?');
  const auto path = request.target.substr(0, query);
  return std::format("WSS {}:{}{}", request.host, request.port, path);
}

} // namespace predictfun::net
