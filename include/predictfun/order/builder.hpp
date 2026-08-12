#pragma once

#include "predictfun/order/eip712.hpp"

#include <chrono>
#include <functional>

namespace predictfun::order {

using SaltProvider = std::function<Result<Uint256>()>;
using ClockProvider = std::function<std::uint64_t()>;

struct BuilderOptions {
  ChainId chain_id{ChainId::bnb_testnet};
  SaltProvider salt_provider;
  ClockProvider clock_provider;
  std::optional<EvmAddress> predict_account;
};

class OrderBuilder {
public:
  explicit OrderBuilder(BuilderOptions options = {});

  [[nodiscard]] Result<UnsignedOrder> build(const BuildOrderInput &input) const;
  [[nodiscard]] Result<Hash32>
  digest(const UnsignedOrder &value, MarketContractKind kind) const;
  [[nodiscard]] ChainId chain_id() const noexcept { return options_.chain_id; }

private:
  BuilderOptions options_;
  ProtocolAddresses addresses_;
};

} // namespace predictfun::order
