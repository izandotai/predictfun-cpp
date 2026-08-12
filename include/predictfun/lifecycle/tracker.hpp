#pragma once

#include "predictfun/types/lifecycle.hpp"
#include "predictfun/types/private_rest.hpp"
#include "predictfun/types/private_websocket.hpp"
#include "predictfun/types/trading.hpp"

#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace predictfun::lifecycle {

class OrderTracker {
public:
  [[nodiscard]] Result<TrackedOrder *>
  begin_submission(std::string hash, ExactDecimal amount);

  [[nodiscard]] Result<bool>
  apply_create_outcome(
      std::string_view hash,
      const MutationOutcome<CreateOrderReceipt> &outcome);
  [[nodiscard]] Result<bool> apply_rest_order(const OrderRecord &order);
  [[nodiscard]] Result<bool> apply_wallet_event(const WalletEvent &event);
  [[nodiscard]] Result<bool> mark_book_removed(std::string_view hash);

  void require_reconciliation(std::uint64_t stream_generation);
  [[nodiscard]] ReconciliationReport
  reconcile(std::uint64_t stream_generation,
            const std::vector<OrderRecord> &complete_order_snapshot);

  [[nodiscard]] TrackedOrder *find(std::string_view hash) noexcept;
  [[nodiscard]] const TrackedOrder *find(std::string_view hash) const noexcept;
  [[nodiscard]] std::vector<TrackedOrder> snapshot() const;
  [[nodiscard]] Result<bool>
  restore(const std::vector<TrackedOrder> &orders);
  [[nodiscard]] std::size_t size() const noexcept { return orders_.size(); }

private:
  std::unordered_map<std::string, TrackedOrder> orders_;
};

} // namespace predictfun::lifecycle
