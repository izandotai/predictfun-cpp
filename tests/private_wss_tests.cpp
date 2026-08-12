#include "predictfun/private_wss/client.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstdlib>
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

  private_wss::ClientOptions options() {
    private_wss::ClientOptions value;
    value.api_key = [] { return std::string{"api-secret"}; };
    value.jwt = [] { return SecretString{"jwt.secret.value"}; };
    value.reconnect_initial = std::chrono::milliseconds{1};
    value.reconnect_max = std::chrono::milliseconds{2};
    value.subscription_ack_timeout = std::chrono::seconds{2};
    value.heartbeat_timeout = std::chrono::seconds{2};
    return value;
  }
  net::WebSocketChannelFactory factory() {
    return [this] {
      auto channel = std::make_shared<ScriptedChannel>(io.get_executor());
      channels.push_back(channel);
      return channel;
    };
  }
  void pump(std::chrono::milliseconds duration = std::chrono::milliseconds{2}) {
    io.restart();
    io.run_for(duration);
  }
};

std::string wallet_event() {
  return R"({"type":"M","topic":"predictWalletEvents/jwt.secret.value","data":{"type":"orderAccepted","orderId":"123","orderHash":"0xabc","walletAddress":"0x1111111111111111111111111111111111111111","timestamp":1736696400000,"details":{"marketId":42,"outcomeIndex":0,"marketQuestion":"Will X happen?","outcome":"YES","quoteType":"BID","quantity":"1.0","quantityFilled":"0","price":"0.5","value":"0.5","valueFilled":"0","strategyType":"LIMIT","categorySlug":"crypto"}}})";
}

void drain(private_wss::PrivateWsClient &client) {
  while (client.try_pop_event()) {
  }
}

void test_reconciliation_gate_and_reconnect() {
  Harness harness;
  auto options = harness.options();
  private_wss::PrivateWsClient client(harness.io.get_executor(), options,
                                      harness.factory());
  client.start();
  harness.pump();
  CHECK(harness.channels.size() == 1U);
  auto channel = harness.channels.front();
  CHECK(channel->open_request.target == "/ws");
  CHECK(channel->open_request.headers.size() == 1U);
  CHECK(net::sanitized_websocket_summary(channel->open_request).find(
            "api-secret") == std::string::npos);
  CHECK(channel->writes.size() == 1U);
  CHECK(channel->writes.front().find("jwt.secret.value") != std::string::npos);

  channel->emit(R"({"type":"R","requestId":1,"success":true})");
  harness.pump();
  CHECK(client.state() == PrivateWsState::reconciliation_required);
  const auto generation = client.stats().generation;
  channel->emit(wallet_event());
  harness.pump();
  bool found_unreconciled = false;
  while (auto event = client.try_pop_event()) {
    if (auto *data = std::get_if<PrivateWsDataEvent>(&*event)) {
      found_unreconciled = !data->reconciled;
    }
  }
  CHECK(found_unreconciled);
  client.mark_reconciled(generation);
  harness.pump();
  CHECK(client.state() == PrivateWsState::live);

  drain(client);
  channel->emit(wallet_event());
  harness.pump();
  bool found_reconciled = false;
  while (auto event = client.try_pop_event()) {
    if (auto *data = std::get_if<PrivateWsDataEvent>(&*event))
      found_reconciled = data->reconciled;
  }
  CHECK(found_reconciled);

  channel->emit(R"({"type":"M","topic":"heartbeat","data":12345})");
  harness.pump();
  CHECK(channel->writes.back() == R"({"method":"heartbeat","data":12345})");

  channel->emit("malformed jwt.secret.value");
  const auto stale_generation = generation;
  client.mark_reconciled(stale_generation);
  harness.pump(std::chrono::milliseconds{10});
  CHECK(client.stats().reconnects >= 1U);
  CHECK(harness.channels.size() >= 2U);
  CHECK(client.state() != PrivateWsState::live);
  auto state_seen = false;
  while (auto event = client.try_pop_event()) {
    if (auto *state = std::get_if<PrivateWsStateEvent>(&*event)) {
      CHECK(state->reason.find("jwt.secret.value") == std::string::npos);
      state_seen = true;
    }
  }
  CHECK(state_seen);
  client.stop();
  harness.pump();
}

} // namespace

int main() {
  test_reconciliation_gate_and_reconnect();
  if (failures != 0)
    std::cerr << failures << " private WSS checks failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
