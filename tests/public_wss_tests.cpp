#include "predictfun/public_wss/client.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": CHECK failed: " #condition << '\n';                    \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

namespace asio = boost::asio;
using namespace predictfun;

class ScriptedChannel final : public net::WebSocketChannel {
public:
  explicit ScriptedChannel(asio::any_io_executor executor)
      : executor_(std::move(executor)) {}

  void async_open(net::WebSocketRequest request, net::RequestContext,
                  net::OpenHandler handler) override {
    open_request = std::move(request);
    asio::dispatch(executor_, [handler = std::move(handler)]() mutable {
      handler(std::monostate{});
    });
  }

  void async_read(net::ReadHandler handler) override {
    std::scoped_lock lock(mutex_);
    read_handler_ = std::move(handler);
  }

  void async_write(std::string text, net::WriteHandler handler) override {
    writes.push_back(std::move(text));
    asio::dispatch(executor_, [handler = std::move(handler)]() mutable {
      handler(std::monostate{});
    });
  }

  void async_close(net::CloseHandler handler) override {
    asio::dispatch(executor_, [handler = std::move(handler)]() mutable {
      handler(std::monostate{});
    });
  }

  void cancel() override {
    std::scoped_lock lock(mutex_);
    read_handler_ = {};
  }

  void emit(std::string frame) {
    net::ReadHandler handler;
    {
      std::scoped_lock lock(mutex_);
      handler = std::move(read_handler_);
    }
    CHECK(static_cast<bool>(handler));
    if (handler) {
      asio::dispatch(executor_,
                     [handler = std::move(handler),
                      frame = std::move(frame)]() mutable {
                       handler(std::move(frame));
                     });
    }
  }

  net::WebSocketRequest open_request;
  std::vector<std::string> writes;

private:
  asio::any_io_executor executor_;
  std::mutex mutex_;
  net::ReadHandler read_handler_;
};

struct Harness {
  asio::io_context io;
  std::vector<std::shared_ptr<ScriptedChannel>> channels;

  public_wss::ClientOptions options() {
    public_wss::ClientOptions value;
    value.api_key = [] { return std::string{"test-secret"}; };
    value.reconnect_initial = std::chrono::milliseconds{1};
    value.reconnect_max = std::chrono::milliseconds{2};
    value.subscription_ack_timeout = std::chrono::seconds{2};
    value.heartbeat_timeout = std::chrono::seconds{2};
    return value;
  }

  net::WebSocketChannelFactory factory() {
    return [this] {
      auto channel =
          std::make_shared<ScriptedChannel>(io.get_executor());
      channels.push_back(channel);
      return channel;
    };
  }

  void pump(std::chrono::milliseconds duration = std::chrono::milliseconds{2}) {
    io.restart();
    io.run_for(duration);
  }
};

PublicTopic orderbook() {
  return PublicTopic{PublicTopicKind::orderbook, 42U, 2U};
}

std::string ack(std::int64_t request_id = 1) {
  return std::string{"{\"type\":\"R\",\"requestId\":"} +
         std::to_string(request_id) + ",\"success\":true}";
}

std::string book(std::int64_t timestamp) {
  return std::string{
             "{\"type\":\"M\",\"topic\":\"predictOrderbook/42\","
             "\"data\":{\"version\":1,\"marketId\":42,"
             "\"updateTimestampMs\":"} +
         std::to_string(timestamp) +
         ",\"orderCount\":2,\"asks\":[[0.52,2]],"
         "\"bids\":[[0.51,3]],\"settlementsPending\":0}}";
}

void drain(public_wss::PublicWsClient &client) {
  while (client.try_pop_event()) {
  }
}

void test_live_and_heartbeat() {
  Harness harness;
  auto options = harness.options();
  public_wss::PublicWsClient client(harness.io.get_executor(), options,
                                    harness.factory());
  client.start({orderbook()});
  harness.pump();
  CHECK(harness.channels.size() == 1U);
  auto channel = harness.channels.front();
  CHECK(channel->open_request.target == "/ws");
  CHECK(channel->open_request.headers.size() == 1U);
  CHECK(channel->open_request.headers.front().name == "x-api-key");
  CHECK(net::sanitized_websocket_summary(channel->open_request).find(
            "test-secret") == std::string::npos);
  CHECK(channel->writes.size() == 1U);

  channel->emit(ack());
  harness.pump();
  CHECK(client.state() == PublicWsState::synchronizing);
  channel->emit(book(1000));
  harness.pump();
  CHECK(client.state() == PublicWsState::live);
  channel->emit(
      R"({"type":"M","topic":"heartbeat","data":123456789})");
  harness.pump();
  CHECK(channel->writes.back() ==
        R"({"method":"heartbeat","data":123456789})");
  client.stop();
  harness.pump();
}

void test_regression_reconnect_and_resubscribe() {
  Harness harness;
  auto options = harness.options();
  public_wss::PublicWsClient client(harness.io.get_executor(), options,
                                    harness.factory());
  client.start({orderbook()});
  harness.pump();
  auto first = harness.channels.front();
  first->emit(ack());
  harness.pump();
  first->emit(book(2000));
  harness.pump();
  drain(client);
  first->emit(book(1999));
  harness.pump(std::chrono::milliseconds{10});
  CHECK(harness.channels.size() >= 2U);
  CHECK(harness.channels.back()->writes.size() == 1U);
  CHECK(client.stats().stale_frames == 1U);
  CHECK(client.stats().reconnects >= 1U);
  client.stop();
  harness.pump();
}

void test_bounded_queue_resync() {
  Harness harness;
  auto options = harness.options();
  options.max_pending_events = 2U;
  public_wss::PublicWsClient client(harness.io.get_executor(), options,
                                    harness.factory());
  client.start({orderbook()});
  harness.pump();
  auto first = harness.channels.front();
  drain(client);
  first->emit(ack());
  harness.pump();
  drain(client);
  first->emit(book(1000));
  harness.pump();
  first->emit(book(1001));
  harness.pump(std::chrono::milliseconds{10});
  CHECK(client.stats().queue_overflows >= 1U);
  CHECK(client.stats().reconnects >= 1U);
  client.stop();
  harness.pump();
}

void test_dynamic_subscription_uses_live_session() {
  Harness harness;
  auto options = harness.options();
  public_wss::PublicWsClient client(harness.io.get_executor(), options,
                                    harness.factory());
  client.start({orderbook()});
  harness.pump();
  auto channel = harness.channels.front();
  channel->emit(ack());
  harness.pump();
  channel->emit(book(1000));
  harness.pump();
  CHECK(client.state() == PublicWsState::live);

  client.subscribe(
      PublicTopic{PublicTopicKind::market_changed, 43U, std::nullopt});
  harness.pump();
  CHECK(channel->writes.size() == 2U);
  if (channel->writes.size() == 2U) {
    CHECK(channel->writes.back().find("predictMarketChanged/43") !=
          std::string::npos);
  }
  client.stop();
  harness.pump();
}

} // namespace

int main() {
  test_live_and_heartbeat();
  test_regression_reconnect_and_resubscribe();
  test_bounded_queue_resync();
  test_dynamic_subscription_uses_live_session();
  if (failures != 0)
    std::cerr << failures << " public WSS checks failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
