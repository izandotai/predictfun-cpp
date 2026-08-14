#include "predictfun/lifecycle/tracker.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace predictfun {

bool TrackedOrder::terminal() const noexcept {
  return state == OrderLifecycleState::filled ||
         state == OrderLifecycleState::cancelled ||
         state == OrderLifecycleState::expired ||
         state == OrderLifecycleState::rejected ||
         state == OrderLifecycleState::transaction_failed;
}

bool TrackedOrder::safe_to_rebuild_with_new_nonce() const noexcept {
  return state == OrderLifecycleState::cancelled ||
         state == OrderLifecycleState::expired ||
         state == OrderLifecycleState::rejected ||
         state == OrderLifecycleState::transaction_failed;
}

} // namespace predictfun

namespace predictfun::lifecycle {
namespace {

bool valid_hash(std::string_view hash) {
  return hash.size() == 66U && hash.starts_with("0x") &&
         std::ranges::all_of(hash.substr(2U), [](unsigned char value) {
           return std::isxdigit(value) != 0;
         });
}

struct NormalDecimal {
  bool negative{false};
  std::string integer;
  std::string fraction;
};

NormalDecimal normalize(std::string_view text) {
  NormalDecimal result;
  if (!text.empty() && text.front() == '-') {
    result.negative = true;
    text.remove_prefix(1U);
  }
  const auto dot = text.find('.');
  result.integer = std::string{text.substr(0U, dot)};
  if (dot != std::string_view::npos)
    result.fraction = std::string{text.substr(dot + 1U)};
  const auto nonzero = result.integer.find_first_not_of('0');
  result.integer =
      nonzero == std::string::npos ? "0" : result.integer.substr(nonzero);
  while (!result.fraction.empty() && result.fraction.back() == '0')
    result.fraction.pop_back();
  if (result.integer == "0" && result.fraction.empty())
    result.negative = false;
  return result;
}

int magnitude_compare(const NormalDecimal &left, const NormalDecimal &right) {
  if (left.integer.size() != right.integer.size())
    return left.integer.size() < right.integer.size() ? -1 : 1;
  if (left.integer != right.integer)
    return left.integer < right.integer ? -1 : 1;
  const auto width = std::max(left.fraction.size(), right.fraction.size());
  for (std::size_t index = 0; index < width; ++index) {
    const auto l = index < left.fraction.size() ? left.fraction[index] : '0';
    const auto r = index < right.fraction.size() ? right.fraction[index] : '0';
    if (l != r)
      return l < r ? -1 : 1;
  }
  return 0;
}

int compare(const ExactDecimal &left, const ExactDecimal &right) {
  const auto l = normalize(left.to_string());
  const auto r = normalize(right.to_string());
  if (l.negative != r.negative)
    return l.negative ? -1 : 1;
  const auto magnitude = magnitude_compare(l, r);
  return l.negative ? -magnitude : magnitude;
}

bool zero(const ExactDecimal &value) {
  return compare(value, ExactDecimal::parse("0").value()) == 0;
}

void derive_fill_state(TrackedOrder &tracked) {
  if (zero(tracked.amount_filled))
    return;
  tracked.state = compare(tracked.amount_filled, tracked.amount) >= 0
                      ? OrderLifecycleState::filled
                      : OrderLifecycleState::partially_filled;
}

Error unknown(std::string_view hash) {
  return Error{ErrorCode::invalid_argument, "order is not tracked",
               std::string{hash}};
}

} // namespace

Result<TrackedOrder *> OrderTracker::begin_submission(std::string hash,
                                                      ExactDecimal amount) {
  if (!valid_hash(hash))
    return Error{ErrorCode::invalid_argument,
                 "order hash must be 0x followed by 64 hex characters", "hash"};
  if (normalize(amount.to_string()).negative || zero(amount))
    return Error{ErrorCode::invalid_quantity,
                 "tracked order amount must be positive", "amount"};
  if (orders_.contains(hash))
    return Error{ErrorCode::invalid_argument, "order hash is already tracked",
                 "hash"};
  auto [iterator, inserted] = orders_.emplace(
      hash, TrackedOrder{hash,
                         {},
                         std::move(amount),
                         ExactDecimal::parse("0").value(),
                         OrderLifecycleState::submission_pending,
                         false,
                         0U,
                         0,
                         {}});
  (void)inserted;
  return &iterator->second;
}

Result<bool> OrderTracker::apply_create_outcome(
    std::string_view hash, const MutationOutcome<CreateOrderReceipt> &outcome) {
  auto *tracked = find(hash);
  if (!tracked)
    return unknown(hash);
  if (outcome.disposition == MutationDisposition::ambiguous) {
    tracked->state = OrderLifecycleState::ambiguous;
    tracked->reconciliation_required = true;
    tracked->reason = outcome.ambiguity ? outcome.ambiguity->message
                                        : "mutation result is ambiguous";
    return true;
  }
  if (!outcome.receipt)
    return Error{ErrorCode::protocol_error,
                 "acknowledged create outcome has no receipt", "receipt"};
  if (outcome.receipt->order_hash != tracked->order_hash)
    return Error{ErrorCode::protocol_error,
                 "create receipt hash does not match tracked order", "hash"};
  tracked->order_id = outcome.receipt->order_id;
  tracked->state = OrderLifecycleState::accepted;
  tracked->reason = outcome.receipt->code;
  return true;
}

Result<bool> OrderTracker::mark_submission_rejected(std::string_view hash,
                                                    const Error &error) {
  auto *tracked = find(hash);
  if (!tracked)
    return unknown(hash);
  if (tracked->state == OrderLifecycleState::rejected)
    return false;
  if (tracked->state != OrderLifecycleState::submission_pending)
    return Error{ErrorCode::protocol_error,
                 "submission rejection conflicts with a later order state",
                 "state"};
  tracked->state = OrderLifecycleState::rejected;
  tracked->reconciliation_required = false;
  tracked->reason =
      error.message.empty() ? "order submission rejected" : error.message;
  return true;
}

Result<bool> OrderTracker::apply_rest_order(const OrderRecord &order) {
  auto *tracked = find(order.order.hash);
  if (!tracked)
    return unknown(order.order.hash);
  tracked->order_id = order.id;
  tracked->amount = order.amount;
  tracked->amount_filled = order.amount_filled;
  tracked->reason = order.status.raw;
  switch (order.status.value) {
  case OrderStatus::open:
    tracked->state = OrderLifecycleState::open;
    derive_fill_state(*tracked);
    break;
  case OrderStatus::matched:
    tracked->state = OrderLifecycleState::filled;
    break;
  case OrderStatus::cancelled:
    tracked->state = OrderLifecycleState::cancelled;
    break;
  case OrderStatus::expired:
    tracked->state = OrderLifecycleState::expired;
    break;
  case OrderStatus::invalidated:
    // INVALIDATED is terminal at the venue (for example nonce/allowance state
    // made the resting order unusable). It cannot be treated as an open or
    // successfully cancelled order.
    tracked->state = OrderLifecycleState::rejected;
    break;
  case OrderStatus::failed:
    tracked->state = OrderLifecycleState::rejected;
    break;
  case OrderStatus::unknown:
    tracked->state = OrderLifecycleState::unknown;
    tracked->reconciliation_required = true;
    return true;
  }
  tracked->reconciliation_required = false;
  return true;
}

Result<bool> OrderTracker::apply_wallet_event(const WalletEvent &event) {
  auto *tracked = find(event.order_hash);
  if (!tracked)
    return unknown(event.order_hash);
  tracked->order_id = event.order_id.empty()
                          ? tracked->order_id
                          : std::optional<std::string>{event.order_id};
  tracked->last_event_timestamp_ms = event.timestamp_ms;
  tracked->amount = event.details.quantity;
  tracked->amount_filled = event.details.quantity_filled;
  tracked->reason = event.reason.value_or(event.type.raw);
  switch (event.type.value) {
  case WalletEventType::order_accepted:
    tracked->state = OrderLifecycleState::open;
    derive_fill_state(*tracked);
    break;
  case WalletEventType::order_not_accepted:
    tracked->state = OrderLifecycleState::rejected;
    break;
  case WalletEventType::order_expired:
    tracked->state = OrderLifecycleState::expired;
    break;
  case WalletEventType::order_cancelled:
    tracked->state = OrderLifecycleState::cancelled;
    break;
  case WalletEventType::order_transaction_submitted:
    tracked->state = OrderLifecycleState::transaction_pending;
    break;
  case WalletEventType::order_transaction_success:
    tracked->state = OrderLifecycleState::accepted;
    derive_fill_state(*tracked);
    break;
  case WalletEventType::order_transaction_failed:
    tracked->state = OrderLifecycleState::transaction_failed;
    break;
  case WalletEventType::unknown:
    tracked->state = OrderLifecycleState::unknown;
    tracked->reconciliation_required = true;
    return true;
  }
  tracked->reconciliation_required = false;
  return true;
}

Result<bool> OrderTracker::mark_book_removed(std::string_view hash) {
  auto *tracked = find(hash);
  if (!tracked)
    return unknown(hash);
  tracked->state = OrderLifecycleState::book_removed;
  tracked->reason =
      "removed from off-chain orderbook; on-chain order remains valid";
  tracked->reconciliation_required = true;
  return true;
}

void OrderTracker::require_reconciliation(std::uint64_t stream_generation) {
  for (auto &[hash, order] : orders_) {
    (void)hash;
    if (order.terminal())
      continue;
    order.reconciliation_required = true;
    order.stream_generation = stream_generation;
  }
}

ReconciliationReport OrderTracker::reconcile(
    std::uint64_t stream_generation,
    const std::vector<OrderRecord> &complete_order_snapshot) {
  ReconciliationReport report{orders_.size(), 0U, {}};
  std::unordered_set<std::string> observed;
  for (const auto &order : complete_order_snapshot) {
    if (!orders_.contains(order.order.hash))
      continue;
    observed.insert(order.order.hash);
    ++report.observed;
    (void)apply_rest_order(order);
    if (auto *tracked = find(order.order.hash))
      tracked->stream_generation = stream_generation;
  }
  for (auto &[hash, order] : orders_) {
    if (order.terminal() || observed.contains(hash))
      continue;
    order.reconciliation_required = true;
    order.stream_generation = stream_generation;
    report.unresolved_hashes.push_back(hash);
  }
  return report;
}

TrackedOrder *OrderTracker::find(std::string_view hash) noexcept {
  const auto iterator = orders_.find(std::string{hash});
  return iterator == orders_.end() ? nullptr : &iterator->second;
}

const TrackedOrder *OrderTracker::find(std::string_view hash) const noexcept {
  const auto iterator = orders_.find(std::string{hash});
  return iterator == orders_.end() ? nullptr : &iterator->second;
}

std::vector<TrackedOrder> OrderTracker::snapshot() const {
  std::vector<TrackedOrder> result;
  result.reserve(orders_.size());
  for (const auto &[hash, order] : orders_) {
    (void)hash;
    result.push_back(order);
  }
  std::ranges::sort(result, {}, &TrackedOrder::order_hash);
  return result;
}

Result<bool> OrderTracker::restore(const std::vector<TrackedOrder> &orders) {
  std::unordered_map<std::string, TrackedOrder> recovered;
  recovered.reserve(orders.size());
  for (const auto &order : orders) {
    if (!valid_hash(order.order_hash))
      return Error{ErrorCode::journal_corrupt,
                   "journal contains an invalid order hash", "order_hash"};
    if (normalize(order.amount.to_string()).negative || zero(order.amount))
      return Error{ErrorCode::journal_corrupt,
                   "journal contains a non-positive order amount", "amount"};
    if (normalize(order.amount_filled.to_string()).negative)
      return Error{ErrorCode::journal_corrupt,
                   "journal contains a negative filled amount",
                   "amount_filled"};
    if (!recovered.emplace(order.order_hash, order).second)
      return Error{ErrorCode::journal_corrupt,
                   "journal recovery produced duplicate order hashes",
                   "order_hash"};
  }
  orders_ = std::move(recovered);
  return true;
}

} // namespace predictfun::lifecycle
