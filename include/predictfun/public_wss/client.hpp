#pragma once

#include "predictfun/codec/public_websocket.hpp"
#include "predictfun/net/websocket.hpp"

#include <boost/asio/any_io_executor.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace predictfun::public_wss {

using ApiKeyProvider = std::function<std::string()>;
using EventReadyHandler = std::function<void()>;

struct ClientOptions {
  std::string host{"ws.predict.fun"};
  std::string port{"443"};
  std::string target{"/ws"};
  bool use_tls{true};
  ApiKeyProvider api_key;
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
  std::uint64_t stale_frames{0};
  std::uint64_t queue_overflows{0};
};

class PublicWsClient {
public:
  PublicWsClient(boost::asio::any_io_executor executor,
                 ClientOptions options = {},
                 net::WebSocketChannelFactory channel_factory = {});
  ~PublicWsClient();

  PublicWsClient(const PublicWsClient &) = delete;
  PublicWsClient &operator=(const PublicWsClient &) = delete;
  PublicWsClient(PublicWsClient &&) noexcept;
  PublicWsClient &operator=(PublicWsClient &&) noexcept;

  void start(std::vector<PublicTopic> topics,
             EventReadyHandler event_ready = {});
  void stop();
  void subscribe(PublicTopic topic);
  void unsubscribe(PublicTopic topic);

  [[nodiscard]] std::optional<PublicWsEvent> try_pop_event();
  [[nodiscard]] PublicWsState state() const noexcept;
  [[nodiscard]] ClientStats stats() const;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace predictfun::public_wss
