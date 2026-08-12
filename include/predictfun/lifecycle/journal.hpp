#pragma once

#include "predictfun/lifecycle/tracker.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace predictfun::lifecycle {

struct JournalOptions {
  std::size_t maximum_record_bytes{64U * 1024U};
  // Flushes the C++ stream and asks the operating system to commit the file to
  // stable storage before publishing the corresponding in-memory transition.
  bool flush_after_append{true};
};

struct JournalReplay {
  std::vector<TrackedOrder> orders;
  std::size_t records_applied{0U};
  std::uint64_t valid_bytes{0U};
  bool ignored_truncated_tail{false};
};

// Append-only, checksummed lifecycle journal. It never stores an API key,
// JWT, private key, signature, request body or raw venue response.
class OrderJournal {
public:
  explicit OrderJournal(std::filesystem::path path,
                        JournalOptions options = {});

  [[nodiscard]] Result<bool> initialize();
  [[nodiscard]] Result<bool> append(const TrackedOrder &order);
  [[nodiscard]] Result<bool>
  append(const std::vector<TrackedOrder> &orders);
  [[nodiscard]] Result<JournalReplay> replay() const;

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path path_;
  JournalOptions options_;
};

// Transactional facade: an in-memory lifecycle transition is journaled before
// it becomes visible to the caller. On restart, terminal states are restored
// and all nonterminal states are quarantined until complete REST reconciliation.
class PersistentOrderTracker {
public:
  [[nodiscard]] static Result<PersistentOrderTracker>
  open(std::filesystem::path path, JournalOptions options = {});

  [[nodiscard]] Result<TrackedOrder *>
  begin_submission(std::string hash, ExactDecimal amount);
  [[nodiscard]] Result<bool>
  apply_create_outcome(
      std::string_view hash,
      const MutationOutcome<CreateOrderReceipt> &outcome);
  [[nodiscard]] Result<bool> apply_rest_order(const OrderRecord &order);
  [[nodiscard]] Result<bool> apply_wallet_event(const WalletEvent &event);
  [[nodiscard]] Result<bool> mark_book_removed(std::string_view hash);
  [[nodiscard]] Result<bool>
  require_reconciliation(std::uint64_t stream_generation);
  [[nodiscard]] Result<ReconciliationReport>
  reconcile(std::uint64_t stream_generation,
            const std::vector<OrderRecord> &complete_order_snapshot);

  [[nodiscard]] TrackedOrder *find(std::string_view hash) noexcept {
    return tracker_.find(hash);
  }
  [[nodiscard]] const TrackedOrder *
  find(std::string_view hash) const noexcept {
    return tracker_.find(hash);
  }
  [[nodiscard]] std::vector<TrackedOrder> snapshot() const {
    return tracker_.snapshot();
  }
  [[nodiscard]] std::size_t size() const noexcept { return tracker_.size(); }
  [[nodiscard]] std::size_t recovered_records() const noexcept {
    return recovered_records_;
  }
  [[nodiscard]] bool ignored_truncated_tail() const noexcept {
    return ignored_truncated_tail_;
  }

private:
  PersistentOrderTracker(OrderJournal journal, OrderTracker tracker,
                         std::size_t recovered_records,
                         bool ignored_truncated_tail)
      : journal_(std::move(journal)), tracker_(std::move(tracker)),
        recovered_records_(recovered_records),
        ignored_truncated_tail_(ignored_truncated_tail) {}

  OrderJournal journal_;
  OrderTracker tracker_;
  std::size_t recovered_records_{0U};
  bool ignored_truncated_tail_{false};
};

} // namespace predictfun::lifecycle
