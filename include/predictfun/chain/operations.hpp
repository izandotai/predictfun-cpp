#pragma once

#include "predictfun/types/chain.hpp"

#include <optional>
#include <span>

namespace predictfun::chain {

enum class TransactionRoute { eoa, predict_account };

struct RouteOptions {
  TransactionRoute route{TransactionRoute::eoa};
  std::optional<EvmAddress> predict_account;
};

[[nodiscard]] Result<UnsignedTransaction>
route_transaction(ChainId chain_id, UnsignedTransaction transaction,
                  const RouteOptions &route = {});

[[nodiscard]] Result<UnsignedTransaction>
split_transaction(ChainId chain_id, const PositionOperation &operation,
                  const RouteOptions &route = {});
[[nodiscard]] Result<UnsignedTransaction>
merge_transaction(ChainId chain_id, const PositionOperation &operation,
                  const RouteOptions &route = {});
[[nodiscard]] Result<UnsignedTransaction>
redeem_transaction(ChainId chain_id, const RedeemOperation &operation,
                   const RouteOptions &route = {});
[[nodiscard]] Result<UnsignedTransaction>
convert_transaction(ChainId chain_id, const ConvertOperation &operation,
                    const RouteOptions &route = {});

[[nodiscard]] Result<std::optional<UnsignedTransaction>>
cancel_orders_transaction(ChainId chain_id,
                          std::span<const SignedOrder> orders,
                          MarketContractKind market,
                          const RouteOptions &route = {});

} // namespace predictfun::chain
