#pragma once

#include "predictfun/net/http.hpp"

#include <boost/asio/any_io_executor.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <variant>

namespace predictfun::net {

struct WebSocketRequest {
  std::string host;
  std::string port{"443"};
  std::string target{"/ws"};
  bool use_tls{true};
  std::vector<Header> headers;
};

struct WebSocketLimits {
  std::size_t max_frame_bytes{2U * 1024U * 1024U};
  std::chrono::milliseconds default_connect_timeout{15'000};
};

using OpenHandler = std::function<void(Result<std::monostate>)>;
using ReadHandler = std::function<void(Result<std::string>)>;
using WriteHandler = std::function<void(Result<std::monostate>)>;
using CloseHandler = std::function<void(Result<std::monostate>)>;

class WebSocketChannel {
public:
  virtual ~WebSocketChannel() = default;

  virtual void async_open(WebSocketRequest request, RequestContext context,
                          OpenHandler handler) = 0;
  virtual void async_read(ReadHandler handler) = 0;
  virtual void async_write(std::string text, WriteHandler handler) = 0;
  virtual void async_close(CloseHandler handler) = 0;
  virtual void cancel() = 0;
};

class BeastWebSocketChannel final : public WebSocketChannel {
public:
  explicit BeastWebSocketChannel(boost::asio::any_io_executor executor,
                                 WebSocketLimits limits = {});
  ~BeastWebSocketChannel() override;

  BeastWebSocketChannel(const BeastWebSocketChannel &) = delete;
  BeastWebSocketChannel &operator=(const BeastWebSocketChannel &) = delete;

  void async_open(WebSocketRequest request, RequestContext context,
                  OpenHandler handler) override;
  void async_read(ReadHandler handler) override;
  void async_write(std::string text, WriteHandler handler) override;
  void async_close(CloseHandler handler) override;
  void cancel() override;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

using WebSocketChannelFactory =
    std::function<std::shared_ptr<WebSocketChannel>()>;

[[nodiscard]] std::string
sanitized_websocket_summary(const WebSocketRequest &request);

} // namespace predictfun::net
