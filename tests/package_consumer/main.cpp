#include "predictfun/codec/public_websocket.hpp"
#include "predictfun/public_rest/client.hpp"
#include "predictfun/public_wss/client.hpp"
#include "predictfun/types/decimal.hpp"

int main() {
  const auto price = predictfun::Price::parse("0.42", 2);
  const auto target = predictfun::public_rest::protocol::market_target(
      predictfun::MarketId{42U});
  const auto topic = predictfun::codec::public_topic_name(
      predictfun::PublicTopic{predictfun::PublicTopicKind::orderbook, 42U, 2U});
  return price && price.value().ticks() == 42U && target && topic &&
                 target.value() == "/v1/markets/42" &&
                 topic.value() == "predictOrderbook/42"
             ? 0
             : 1;
}
