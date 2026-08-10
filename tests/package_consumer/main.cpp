#include "predictfun/public_rest/client.hpp"
#include "predictfun/types/decimal.hpp"

int main() {
  const auto price = predictfun::Price::parse("0.42", 2);
  const auto target = predictfun::public_rest::protocol::market_target(
      predictfun::MarketId{42U});
  return price && price.value().ticks() == 42U && target &&
                 target.value() == "/v1/markets/42"
             ? 0
             : 1;
}
