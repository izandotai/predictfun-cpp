#include "predictfun/public_wss/client.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include <algorithm>
#include <atomic>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <ranges>
#include <set>
#include <string_view>
#include <type_traits>
#include <utility>

namespace predictfun::public_wss {
namespace {

namespace asio = boost::asio;

bool snapshot_backed(PublicTopicKind kind) {
  return kind == PublicTopicKind::orderbook ||
         kind == PublicTopicKind::trading_status ||
         kind == PublicTopicKind::market_status;
}

std::optional<std::int64_t> message_timestamp(const PublicWsMessage &message) {
  return std::visit(
      [](const auto &value) -> std::optional<std::int64_t> {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, OrderbookMessage>)
          return value.book.update_timestamp_ms;
        if constexpr (std::is_same_v<Value, TradingStatusMessage> ||
                      std::is_same_v<Value, MarketStatusMessage> ||
                      std::is_same_v<Value, MarketChangedMessage> ||
                      std::is_same_v<Value, CategoryChangedMessage>)
          return value.timestamp_ms;
        return std::nullopt;
      },
      message);
}

std::optional<PublicTopicKind> message_kind(const PublicWsMessage &message) {
  return std::visit(
      [](const auto &value) -> std::optional<PublicTopicKind> {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, OrderbookMessage>)
          return PublicTopicKind::orderbook;
        if constexpr (std::is_same_v<Value, TradingStatusMessage>)
          return PublicTopicKind::trading_status;
        if constexpr (std::is_same_v<Value, MarketStatusMessage>)
          return PublicTopicKind::market_status;
        if constexpr (std::is_same_v<Value, MarketChangedMessage>)
          return PublicTopicKind::market_changed;
        if constexpr (std::is_same_v<Value, CategoryChangedMessage>)
          return PublicTopicKind::category_changed;
        return std::nullopt;
      },
      message);
}

std::optional<std::uint64_t> message_parameter(const PublicWsMessage &message) {
  return std::visit(
      [](const auto &value) -> std::optional<std::uint64_t> {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, OrderbookMessage>)
          return value.book.market_id.value;
        if constexpr (std::is_same_v<Value, TradingStatusMessage> ||
                      std::is_same_v<Value, MarketStatusMessage> ||
                      std::is_same_v<Value, MarketChangedMessage>)
          return value.market_id.value;
        if constexpr (std::is_same_v<Value, CategoryChangedMessage>)
          return value.category_id;
        return std::nullopt;
      },
      message);
}

struct TopicRuntime {
  PublicTopic topic;
  bool acknowledged{false};
  bool fresh{false};
  std::optional<std::int64_t> last_timestamp;
};

struct PendingRequest {
  bool subscribe{true};
  std::string topic_name;
};

} // namespace

struct PublicWsClient::Impl
    : public std::enable_shared_from_this<PublicWsClient::Impl> {
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

  void begin(std::vector<PublicTopic> topics_value,
             EventReadyHandler event_ready_value) {
    if (started)
      return;
    event_ready = std::move(event_ready_value);
    if (options.max_pending_events == 0U) {
      return hard_stop("event queue capacity must be positive");
    }
    if (options.target != "/ws" || options.host.empty() ||
        options.port.empty()) {
      return hard_stop("public WebSocket endpoint must use the /ws path");
    }
    if (!options.api_key) {
      return hard_stop("public WebSocket API key is required");
    }
    if (topics_value.empty())
      return hard_stop("at least one public topic is required");

    std::set<std::string> unique;
    for (auto &topic : topics_value) {
      auto name = codec::public_topic_name(topic);
      if (!name)
        return hard_stop(name.error().message);
      if (!unique.insert(name.value()).second)
        return hard_stop("duplicate public WebSocket topic");
      topics.emplace(name.value(),
                     TopicRuntime{std::move(topic), false, false, std::nullopt});
    }
    started = true;
    reconnect_delay = options.reconnect_initial;
    connect();
  }

  void hard_stop(std::string reason) {
    started = false;
    session_open = false;
    reconnect_timer.cancel();
    heartbeat_timer.cancel();
    ack_timer.cancel();
    if (channel)
      channel->cancel();
    channel.reset();
    pending.clear();
    set_state(PublicWsState::stopped, std::move(reason), true);
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
    for (auto &[name, runtime] : topics) {
      (void)name;
      runtime.acknowledged = false;
      runtime.fresh = false;
      runtime.last_timestamp.reset();
    }
    pending.clear();
    set_state(PublicWsState::connecting, "connecting", true);
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
      return schedule_reconnect("public WebSocket API key provider failed");
    }
    if (api_key.empty())
      return schedule_reconnect("public WebSocket API key is empty");
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
    set_state(PublicWsState::subscribing, "connected; subscribing", true);
    arm_heartbeat();
    read_next(generation);
    for (const auto &[name, runtime] : topics) {
      (void)runtime;
      send_subscription(name, true);
    }
    arm_ack_timeout(generation);
  }

  void send_subscription(const std::string &name, bool is_subscribe) {
    const auto found = topics.find(name);
    if (found == topics.end())
      return;
    const auto request_id = next_request_id++;
    auto encoded = is_subscribe
                       ? codec::encode_subscribe_request(request_id,
                                                         found->second.topic)
                       : codec::encode_unsubscribe_request(request_id,
                                                           found->second.topic);
    if (!encoded)
      return schedule_reconnect(encoded.error().message);
    pending.emplace(static_cast<std::int64_t>(request_id),
                    PendingRequest{is_subscribe, name});
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
    const auto resolver = [this](MarketId market_id)
        -> std::optional<std::uint8_t> {
      for (const auto &[name, runtime] : topics) {
        (void)name;
        if (runtime.topic.parameter == market_id.value &&
            runtime.topic.decimal_precision)
          return runtime.topic.decimal_precision;
      }
      return std::nullopt;
    };
    auto decoded = codec::decode_public_ws_frame(
        result.value(), resolver, options.decode_limits);
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
    if (std::holds_alternative<UnknownPublicMessage>(decoded.value())) {
      set_state(PublicWsState::degraded,
                "unknown public topic received; ignored", true);
      read_next(generation);
      return;
    }
    handle_data(std::move(decoded.value()));
    if (started && generation == state_generation)
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
    const auto found = pending.find(response.request_id);
    if (found == pending.end())
      return schedule_reconnect("unexpected subscription response id");
    const auto request = found->second;
    pending.erase(found);
    if (!response.success) {
      const auto reason = response.error
                              ? response.error->code
                              : std::string{"subscription rejected"};
      return schedule_reconnect(reason);
    }
    const auto topic = topics.find(request.topic_name);
    if (request.subscribe) {
      if (topic == topics.end())
        return schedule_reconnect("acknowledged topic is no longer desired");
      topic->second.acknowledged = true;
      if (!snapshot_backed(topic->second.topic.kind))
        topic->second.fresh = true;
    } else if (topic != topics.end()) {
      topics.erase(topic);
    }
    update_freshness_state();
    if (pending.empty())
      ack_timer.cancel();
  }

  void handle_data(PublicWsMessage message) {
    const auto kind = message_kind(message);
    const auto parameter = message_parameter(message);
    if (!kind || !parameter)
      return schedule_reconnect("message cannot be mapped to a public topic");
    const auto found = std::find_if(
        topics.begin(), topics.end(), [&](const auto &item) {
          return item.second.topic.kind == *kind &&
                 item.second.topic.parameter == *parameter;
        });
    if (found == topics.end())
      return schedule_reconnect("message arrived for an unsubscribed topic");
    auto &runtime = found->second;
    const auto timestamp = message_timestamp(message);
    if (timestamp && runtime.last_timestamp &&
        *timestamp < *runtime.last_timestamp) {
      increment_stat(&ClientStats::stale_frames);
      return schedule_reconnect("topic timestamp regressed; resync required");
    }
    if (timestamp)
      runtime.last_timestamp = timestamp;
    runtime.fresh = true;
    update_freshness_state();
    if (!enqueue(PublicWsDataEvent{runtime.topic, std::move(message), true,
                                   state_generation})) {
      increment_stat(&ClientStats::queue_overflows);
      schedule_reconnect("bounded event queue overflow; resync required");
    }
  }

  void update_freshness_state() {
    if (!started)
      return;
    const bool all_acknowledged =
        std::ranges::all_of(topics, [](const auto &item) {
          return item.second.acknowledged;
        });
    if (!all_acknowledged) {
      set_state(PublicWsState::subscribing, "awaiting subscription acks",
                false);
      return;
    }
    const bool all_fresh = std::ranges::all_of(topics, [](const auto &item) {
      return item.second.fresh;
    });
    if (!all_fresh) {
      set_state(PublicWsState::synchronizing,
                "subscription acknowledged; awaiting best-effort snapshots",
                false);
      return;
    }
    set_state(PublicWsState::live, "all desired public topics are fresh", false);
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
    ack_timer.async_wait([weak = weak_from_this(), generation](
                             const boost::system::error_code &error) {
      if (error)
        return;
      if (auto self = weak.lock()) {
        if (self->started && generation == self->state_generation &&
            !self->pending.empty())
          self->schedule_reconnect("subscription acknowledgement timed out");
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
    pending.clear();
    for (auto &[name, runtime] : topics) {
      (void)name;
      runtime.acknowledged = false;
      runtime.fresh = false;
      runtime.last_timestamp.reset();
    }
    increment_stat(&ClientStats::reconnects);
    set_state(PublicWsState::reconnect_wait, std::move(reason), true);
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

  void add_topic(PublicTopic topic) {
    auto name = codec::public_topic_name(topic);
    if (!name) {
      set_state(PublicWsState::degraded, name.error().message, true);
      return;
    }
    if (topics.contains(name.value()))
      return;
    topics.emplace(name.value(),
                   TopicRuntime{std::move(topic), false, false, std::nullopt});
    if (started && session_open && channel) {
      send_subscription(name.value(), true);
      arm_ack_timeout(state_generation);
    }
  }

  void remove_topic(PublicTopic topic) {
    auto name = codec::public_topic_name(topic);
    if (!name)
      return;
    if (!topics.contains(name.value()))
      return;
    if (started && session_open && channel) {
      send_subscription(name.value(), false);
      arm_ack_timeout(state_generation);
      return;
    }
    topics.erase(name.value());
  }

  bool enqueue(PublicWsEvent event) {
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

  void set_state(PublicWsState value, std::string reason, bool force) {
    const auto previous = state_value.exchange(value);
    if (!force && previous == value)
      return;
    PublicWsStateEvent event{value, std::move(reason), state_generation};
    if (!enqueue(std::move(event))) {
      EventReadyHandler notify;
      {
        std::scoped_lock lock(queue_mutex);
        events.clear();
        events.push_back(PublicWsStateEvent{
            PublicWsState::degraded, "state queue overflow", state_generation});
        notify = event_ready;
      }
      if (notify)
        notify();
    }
  }

  std::optional<PublicWsEvent> pop() {
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
  std::map<std::string, TopicRuntime, std::less<>> topics;
  std::map<std::int64_t, PendingRequest> pending;
  std::uint64_t next_request_id{1};
  std::uint64_t state_generation{0};
  std::chrono::milliseconds reconnect_delay{250};
  std::atomic<PublicWsState> state_value{PublicWsState::stopped};
  mutable std::mutex queue_mutex;
  mutable std::mutex stats_mutex;
  std::deque<PublicWsEvent> events;
  EventReadyHandler event_ready;
  ClientStats stats_value;
  bool started{false};
  bool session_open{false};
};

PublicWsClient::PublicWsClient(
    boost::asio::any_io_executor executor, ClientOptions options,
    net::WebSocketChannelFactory channel_factory)
    : impl_(std::make_shared<Impl>(std::move(executor), std::move(options),
                                  std::move(channel_factory))) {}

PublicWsClient::~PublicWsClient() {
  if (impl_) {
    auto impl = impl_;
    asio::dispatch(impl->strand, [impl] { impl->request_stop(); });
  }
}

PublicWsClient::PublicWsClient(PublicWsClient &&) noexcept = default;
PublicWsClient &PublicWsClient::operator=(PublicWsClient &&) noexcept = default;

void PublicWsClient::start(std::vector<PublicTopic> topics,
                           EventReadyHandler event_ready) {
  auto impl = impl_;
  asio::dispatch(impl->strand,
                 [impl, topics = std::move(topics),
                  event_ready = std::move(event_ready)]() mutable {
                   impl->begin(std::move(topics), std::move(event_ready));
                 });
}

void PublicWsClient::stop() {
  auto impl = impl_;
  asio::dispatch(impl->strand, [impl] { impl->request_stop(); });
}

void PublicWsClient::subscribe(PublicTopic topic) {
  auto impl = impl_;
  asio::dispatch(impl->strand,
                 [impl, topic = std::move(topic)]() mutable {
                   impl->add_topic(std::move(topic));
                 });
}

void PublicWsClient::unsubscribe(PublicTopic topic) {
  auto impl = impl_;
  asio::dispatch(impl->strand,
                 [impl, topic = std::move(topic)]() mutable {
                   impl->remove_topic(std::move(topic));
                 });
}

std::optional<PublicWsEvent> PublicWsClient::try_pop_event() {
  return impl_->pop();
}

PublicWsState PublicWsClient::state() const noexcept {
  return impl_->state_value.load();
}

ClientStats PublicWsClient::stats() const { return impl_->stats(); }

} // namespace predictfun::public_wss
