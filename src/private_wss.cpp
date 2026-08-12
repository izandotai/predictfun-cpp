#include "predictfun/private_wss/client.hpp"
#include "predictfun/codec/public_websocket.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace predictfun::private_wss {
namespace {
namespace asio = boost::asio;
}

struct PrivateWsClient::Impl
    : public std::enable_shared_from_this<PrivateWsClient::Impl> {
  Impl(asio::any_io_executor executor_value, ClientOptions options_value,
       net::WebSocketChannelFactory factory_value)
      : executor(std::move(executor_value)), strand(asio::make_strand(executor)),
        options(std::move(options_value)), factory(std::move(factory_value)),
        reconnect_timer(strand), heartbeat_timer(strand), ack_timer(strand) {
    if (!factory) {
      const auto channel_executor = executor;
      const auto limits = options.transport_limits;
      factory = [channel_executor, limits] {
        return std::make_shared<net::BeastWebSocketChannel>(channel_executor,
                                                            limits);
      };
    }
  }

  void begin(EventReadyHandler ready) {
    if (started)
      return;
    event_ready = std::move(ready);
    if (options.max_pending_events == 0U)
      return hard_stop("event queue capacity must be positive");
    if (options.target != "/ws" || options.host.empty() ||
        options.port.empty())
      return hard_stop("private WebSocket endpoint must use the /ws path");
    if (!options.api_key || !options.jwt)
      return hard_stop("private WebSocket API key and JWT providers are required");
    started = true;
    reconnect_delay = options.reconnect_initial;
    connect();
  }

  void hard_stop(std::string reason) {
    started = false;
    session_open = false;
    awaiting_ack = false;
    reconciled = false;
    reconnect_timer.cancel();
    heartbeat_timer.cancel();
    ack_timer.cancel();
    if (channel)
      channel->cancel();
    channel.reset();
    set_state(PrivateWsState::stopped, std::move(reason), true);
  }

  void request_stop() { hard_stop("stopped by caller"); }

  void connect() {
    if (!started)
      return;
    {
      std::scoped_lock lock(stats_mutex);
      ++stats_value.generation;
      state_generation = stats_value.generation;
    }
    session_open = false;
    awaiting_ack = false;
    reconciled = false;
    set_state(PrivateWsState::connecting, "connecting", true);
    channel = factory();
    if (!channel)
      return schedule_reconnect("WebSocket channel factory returned null");

    net::WebSocketRequest request;
    request.host = options.host;
    request.port = options.port;
    request.target = options.target;
    request.use_tls = options.use_tls;
    std::string api_key;
    try {
      api_key = options.api_key();
    } catch (...) {
      return schedule_reconnect("private WebSocket API key provider failed");
    }
    if (api_key.empty())
      return schedule_reconnect("private WebSocket API key is empty");
    request.headers.push_back(net::Header{"x-api-key", std::move(api_key)});
    channel->async_open(
        std::move(request),
        net::RequestContext::with_timeout(options.connect_timeout),
        [weak = weak_from_this(), generation = state_generation](
            Result<std::monostate> result) {
          if (auto self = weak.lock()) {
            asio::dispatch(self->strand,
                           [self, generation,
                            result = std::move(result)]() mutable {
                             self->opened(generation, std::move(result));
                           });
          }
        });
  }

  void opened(std::uint64_t generation, Result<std::monostate> result) {
    if (!started || generation != state_generation)
      return;
    if (!result)
      return schedule_reconnect(result.error().message);
    session_open = true;
    reconnect_delay = options.reconnect_initial;
    set_state(PrivateWsState::subscribing, "connected; subscribing", true);
    arm_heartbeat();
    read_next(generation);
    subscribe();
    arm_ack_timeout(generation);
  }

  void subscribe() {
    SecretString jwt;
    try {
      jwt = options.jwt();
    } catch (...) {
      return schedule_reconnect("private WebSocket JWT provider failed");
    }
    if (jwt.empty())
      return schedule_reconnect("private WebSocket JWT is empty");
    const auto request_id = next_request_id++;
    auto encoded =
        codec::encode_wallet_subscribe_request(request_id, jwt.view());
    jwt.clear();
    if (!encoded)
      return schedule_reconnect(encoded.error().message);
    pending_request_id = static_cast<std::int64_t>(request_id);
    awaiting_ack = true;
    channel->async_write(
        std::move(encoded.value()),
        [weak = weak_from_this(), generation = state_generation](
            Result<std::monostate> result) {
          if (auto self = weak.lock()) {
            asio::dispatch(self->strand,
                           [self, generation,
                            result = std::move(result)]() mutable {
                             if (!self->started ||
                                 generation != self->state_generation)
                               return;
                             if (!result)
                               self->schedule_reconnect(result.error().message);
                           });
          }
        });
  }

  void read_next(std::uint64_t generation) {
    if (!started || generation != state_generation || !channel)
      return;
    channel->async_read(
        [weak = weak_from_this(), generation](Result<std::string> result) {
          if (auto self = weak.lock()) {
            asio::dispatch(self->strand,
                           [self, generation,
                            result = std::move(result)]() mutable {
                             self->received(generation, std::move(result));
                           });
          }
        });
  }

  void received(std::uint64_t generation, Result<std::string> result) {
    if (!started || generation != state_generation)
      return;
    if (!result)
      return schedule_reconnect(result.error().message);
    increment_stat(&ClientStats::messages);
    auto decoded = codec::decode_private_ws_frame(result.value(),
                                                   options.decode_limits);
    secure_erase(result.value());
    if (!decoded) {
      increment_stat(&ClientStats::malformed_frames);
      return schedule_reconnect(decoded.error().message);
    }
    if (auto *heartbeat = std::get_if<HeartbeatMessage>(&decoded.value())) {
      echo_heartbeat(*heartbeat);
      read_next(generation);
      return;
    }
    if (auto *response =
            std::get_if<SubscriptionResponse>(&decoded.value())) {
      handle_response(*response);
      read_next(generation);
      return;
    }
    auto event = std::move(std::get<WalletEvent>(decoded.value()));
    if (!enqueue(PrivateWsDataEvent{std::move(event), reconciled,
                                    state_generation})) {
      increment_stat(&ClientStats::queue_overflows);
      return schedule_reconnect("bounded event queue overflow; reconciliation required");
    }
    read_next(generation);
  }

  void echo_heartbeat(const HeartbeatMessage &heartbeat) {
    arm_heartbeat();
    auto encoded = codec::encode_heartbeat_response(heartbeat.timestamp_ms);
    if (!encoded)
      return schedule_reconnect(encoded.error().message);
    channel->async_write(
        std::move(encoded.value()),
        [weak = weak_from_this(), generation = state_generation](
            Result<std::monostate> result) {
          if (auto self = weak.lock()) {
            asio::dispatch(self->strand,
                           [self, generation,
                            result = std::move(result)]() mutable {
                             if (self->started &&
                                 generation == self->state_generation &&
                                 !result)
                               self->schedule_reconnect(result.error().message);
                           });
          }
        });
  }

  void handle_response(const SubscriptionResponse &response) {
    if (!awaiting_ack || response.request_id != pending_request_id)
      return schedule_reconnect("unexpected private subscription response id");
    awaiting_ack = false;
    ack_timer.cancel();
    if (!response.success) {
      const auto reason = response.error
                              ? response.error->code
                              : std::string{"subscription rejected"};
      return schedule_reconnect(reason);
    }
    reconciled = false;
    increment_stat(&ClientStats::reconciliation_requests);
    set_state(PrivateWsState::reconciliation_required,
              "wallet subscription active; REST reconciliation required",
              true);
  }

  void reconcile(std::uint64_t generation) {
    if (!started || generation != state_generation || awaiting_ack)
      return;
    reconciled = true;
    set_state(PrivateWsState::live,
              "wallet events live; REST reconciliation complete", true);
  }

  void arm_heartbeat() {
    heartbeat_timer.expires_after(options.heartbeat_timeout);
    heartbeat_timer.async_wait(
        [weak = weak_from_this(), generation = state_generation](
            const boost::system::error_code &error) {
          if (error)
            return;
          if (auto self = weak.lock()) {
            if (self->started && generation == self->state_generation)
              self->schedule_reconnect("server heartbeat deadline exceeded");
          }
        });
  }

  void arm_ack_timeout(std::uint64_t generation) {
    ack_timer.expires_after(options.subscription_ack_timeout);
    ack_timer.async_wait(
        [weak = weak_from_this(), generation](
            const boost::system::error_code &error) {
          if (error)
            return;
          if (auto self = weak.lock()) {
            if (self->started && generation == self->state_generation &&
                self->awaiting_ack)
              self->schedule_reconnect(
                  "private subscription acknowledgement timed out");
          }
        });
  }

  void schedule_reconnect(std::string reason) {
    if (!started)
      return;
    heartbeat_timer.cancel();
    ack_timer.cancel();
    if (channel)
      channel->cancel();
    channel.reset();
    session_open = false;
    awaiting_ack = false;
    reconciled = false;
    increment_stat(&ClientStats::reconnects);
    set_state(PrivateWsState::reconnect_wait, std::move(reason), true);
    const auto delay = reconnect_delay;
    reconnect_delay = std::min(reconnect_delay * 2, options.reconnect_max);
    reconnect_timer.expires_after(delay);
    reconnect_timer.async_wait(
        [weak = weak_from_this()](const boost::system::error_code &error) {
          if (error)
            return;
          if (auto self = weak.lock())
            self->connect();
        });
  }

  bool enqueue(PrivateWsEvent event) {
    EventReadyHandler notify;
    {
      std::scoped_lock lock(queue_mutex);
      if (events.size() >= options.max_pending_events)
        return false;
      events.push_back(std::move(event));
      notify = event_ready;
    }
    if (notify)
      notify();
    return true;
  }

  void set_state(PrivateWsState value, std::string reason, bool force) {
    const auto previous = state_value.exchange(value);
    if (!force && previous == value)
      return;
    if (!enqueue(PrivateWsStateEvent{value, std::move(reason),
                                     state_generation})) {
      std::scoped_lock lock(queue_mutex);
      events.clear();
      events.push_back(PrivateWsStateEvent{
          PrivateWsState::degraded, "state queue overflow", state_generation});
    }
  }

  std::optional<PrivateWsEvent> pop() {
    std::scoped_lock lock(queue_mutex);
    if (events.empty())
      return std::nullopt;
    auto event = std::move(events.front());
    events.pop_front();
    return event;
  }

  ClientStats stats() const {
    std::scoped_lock lock(stats_mutex);
    return stats_value;
  }

  void increment_stat(std::uint64_t ClientStats::*field) {
    std::scoped_lock lock(stats_mutex);
    ++(stats_value.*field);
  }

  asio::any_io_executor executor;
  asio::strand<asio::any_io_executor> strand;
  ClientOptions options;
  net::WebSocketChannelFactory factory;
  std::shared_ptr<net::WebSocketChannel> channel;
  asio::steady_timer reconnect_timer;
  asio::steady_timer heartbeat_timer;
  asio::steady_timer ack_timer;
  std::uint64_t next_request_id{1};
  std::int64_t pending_request_id{0};
  std::uint64_t state_generation{0};
  std::chrono::milliseconds reconnect_delay{250};
  std::atomic<PrivateWsState> state_value{PrivateWsState::stopped};
  mutable std::mutex queue_mutex;
  mutable std::mutex stats_mutex;
  std::deque<PrivateWsEvent> events;
  EventReadyHandler event_ready;
  ClientStats stats_value;
  bool started{false};
  bool session_open{false};
  bool awaiting_ack{false};
  bool reconciled{false};
};

PrivateWsClient::PrivateWsClient(
    boost::asio::any_io_executor executor, ClientOptions options,
    net::WebSocketChannelFactory channel_factory)
    : impl_(std::make_shared<Impl>(std::move(executor), std::move(options),
                                  std::move(channel_factory))) {}

PrivateWsClient::~PrivateWsClient() {
  if (impl_) {
    auto impl = impl_;
    asio::dispatch(impl->strand, [impl] { impl->request_stop(); });
  }
}

PrivateWsClient::PrivateWsClient(PrivateWsClient &&) noexcept = default;
PrivateWsClient &PrivateWsClient::operator=(PrivateWsClient &&) noexcept =
    default;

void PrivateWsClient::start(EventReadyHandler event_ready) {
  auto impl = impl_;
  asio::dispatch(impl->strand,
                 [impl, event_ready = std::move(event_ready)]() mutable {
                   impl->begin(std::move(event_ready));
                 });
}

void PrivateWsClient::stop() {
  auto impl = impl_;
  asio::dispatch(impl->strand, [impl] { impl->request_stop(); });
}

void PrivateWsClient::mark_reconciled(std::uint64_t generation) {
  auto impl = impl_;
  asio::dispatch(impl->strand,
                 [impl, generation] { impl->reconcile(generation); });
}

std::optional<PrivateWsEvent> PrivateWsClient::try_pop_event() {
  return impl_->pop();
}

PrivateWsState PrivateWsClient::state() const noexcept {
  return impl_->state_value.load();
}

ClientStats PrivateWsClient::stats() const { return impl_->stats(); }

} // namespace predictfun::private_wss
