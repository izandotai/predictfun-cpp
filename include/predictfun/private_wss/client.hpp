#pragma once

#include "predictfun/codec/private_websocket.hpp"
#include "predictfun/net/websocket.hpp"
#include "predictfun/types/secret.hpp"

#include <boost/asio/any_io_executor.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace predictfun::private_wss {

using ApiKeyProvider = std::function<std::string()>;
using JwtProvider = std::function<SecretString()>;
using EventReadyHandler = std::function<void()>;

struct ClientOptions {
  std::string host{"ws.predict.fun"};
  std::string port{"443"};
  std::string target{"/ws"};
  bool use_tls{true};
  ApiKeyProvider api_key;
  JwtProvider jwt;
  codec::DecodeLimits decode_limits;
  net::WebSocketLimits transport_limits;
  std::size_t max_pending_events{1'024U};
  std::chrono::milliseconds connect_timeout{15'000};
  std::chrono::milliseconds subscription_ack_timeout{10'000};
  std::chrono::milliseconds heartbeat_timeout{35'000};
  std::chrono::milliseconds reconnect_initial{250};
  std::chrono::milliseconds reconnect_max{10'000};
};

struct ClientStats {
  std::uint64_t generation{0};
  std::uint64_t reconnects{0};
  std::uint64_t messages{0};
  std::uint64_t malformed_frames{0};
  std::uint64_t queue_overflows{0};
  std::uint64_t reconciliation_requests{0};
};

class PrivateWsClient {
public:
  PrivateWsClient(boost::asio::any_io_executor executor,
                  ClientOptions options = {},
                  net::WebSocketChannelFactory channel_factory = {});
  ~PrivateWsClient();

  PrivateWsClient(const PrivateWsClient &) = delete;
  PrivateWsClient &operator=(const PrivateWsClient &) = delete;
  PrivateWsClient(PrivateWsClient &&) noexcept;
  PrivateWsClient &operator=(PrivateWsClient &&) noexcept;

  void start(EventReadyHandler event_ready = {});
  void stop();

  // Must be called only after the host has loaded REST orders, positions and
  // activity for the supplied generation and applied any queued events.
  void mark_reconciled(std::uint64_t generation);

  [[nodiscard]] std::optional<PrivateWsEvent> try_pop_event();
  [[nodiscard]] PrivateWsState state() const noexcept;
  [[nodiscard]] ClientStats stats() const;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace predictfun::private_wss
