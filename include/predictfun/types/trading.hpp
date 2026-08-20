#pragma once

#include "predictfun/types/order.hpp"

#include <optional>
#include <string>
#include <vector>

namespace predictfun {

enum class SelfTradePrevention { cancel_maker, cancel_taker, cancel_both };
enum class ReservedBalancePolicy { reject_market_order };
enum class MutationDisposition { acknowledged, ambiguous };

struct CreateOrderRequest {
  SignedOrder order;
  std::string order_hash;
  Uint256 price_per_share_wei;
  ExecutionStrategy strategy{ExecutionStrategy::limit};
  std::optional<Uint256> slippage_bps;
  std::optional<bool> is_fill_or_kill;
  std::optional<bool> is_post_only;
  std::optional<ReservedBalancePolicy> reserved_balance_policy;
  std::optional<bool> is_min_amount_out;
  std::optional<SelfTradePrevention> self_trade_prevention;
};

struct CreateOrderReceipt {
  std::string code;
  std::string order_id;
  std::string order_hash;
  std::optional<std::string> removal_locked_until;
};

struct RemoveOrdersReceipt {
  std::vector<std::string> removed;
  std::vector<std::string> noop;
};

template <class T> struct MutationOutcome {
  MutationDisposition disposition{MutationDisposition::acknowledged};
  std::optional<T> receipt;
  std::optional<Error> ambiguity;
  std::string reconciliation_key;

  [[nodiscard]] bool acknowledged() const noexcept {
    return disposition == MutationDisposition::acknowledged;
  }
};

} // namespace predictfun
