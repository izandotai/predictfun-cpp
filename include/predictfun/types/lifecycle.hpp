#pragma once

#include "predictfun/types/exact_number.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace predictfun {

enum class OrderLifecycleState {
  submission_pending,
  accepted,
  open,
  partially_filled,
  filled,
  book_removed,
  cancelled,
  expired,
  rejected,
  transaction_pending,
  transaction_failed,
  ambiguous,
  unknown
};

struct TrackedOrder {
  std::string order_hash;
  std::optional<std::string> order_id;
  ExactDecimal amount;
  ExactDecimal amount_filled;
  OrderLifecycleState state{OrderLifecycleState::submission_pending};
  bool reconciliation_required{false};
  std::uint64_t stream_generation{0};
  std::int64_t last_event_timestamp_ms{0};
  std::string reason;

  [[nodiscard]] bool terminal() const noexcept;
  [[nodiscard]] bool safe_to_rebuild_with_new_nonce() const noexcept;
};

struct ReconciliationReport {
  std::size_t tracked{0U};
  std::size_t observed{0U};
  std::vector<std::string> unresolved_hashes;

  [[nodiscard]] bool complete() const noexcept {
    return unresolved_hashes.empty();
  }
};

} // namespace predictfun
