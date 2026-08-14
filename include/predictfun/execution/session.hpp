#pragma once

#include "predictfun/lifecycle/journal.hpp"
#include "predictfun/private_rest/client.hpp"
#include "predictfun/trading/client.hpp"

#include <boost/asio/any_io_executor.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace predictfun::execution {

struct SessionOptions {
  std::size_t reconciliation_page_size{1'000U};
  std::size_t maximum_reconciliation_pages{100U};
};

struct SubmissionResult {
  TrackedOrder tracked_order;
  std::optional<MutationOutcome<CreateOrderReceipt>> mutation;
  std::optional<Error> rejection;

  [[nodiscard]] bool acknowledged() const noexcept {
    return mutation && mutation->acknowledged();
  }
  [[nodiscard]] bool ambiguous() const noexcept {
    return mutation && mutation->disposition == MutationDisposition::ambiguous;
  }
  [[nodiscard]] bool rejected() const noexcept { return rejection.has_value(); }
};

struct ReconciliationResult {
  ReconciliationReport report;
  std::size_t pages{0U};
  std::size_t records{0U};
};

namespace protocol {

// Predict.fun orders encode token quantities with 18 protocol decimals.  A
// BUY receives takerAmount shares; a SELL offers makerAmount shares.
[[nodiscard]] Result<ExactDecimal>
tracked_share_amount(const CreateOrderRequest &request);

} // namespace protocol

template <class T> using Handler = std::function<void(Result<T>)>;

// Durable, serialized host-facing order session.
//
// The session journals an order hash before mutation dispatch, never retries a
// create mutation, records explicit rejections as terminal, quarantines
// ambiguous outcomes, and performs bounded full REST reconciliation after a
// private-stream restart.  It contains no credentials, signer, wallet or RPC.
class DurableOrderSession {
public:
  [[nodiscard]] static Result<DurableOrderSession>
  open(boost::asio::any_io_executor executor,
       std::shared_ptr<trading::TradingClient> trading_client,
       std::shared_ptr<private_rest::PrivateRestClient> private_rest_client,
       std::filesystem::path journal_path, SessionOptions options = {},
       lifecycle::JournalOptions journal_options = {});

  ~DurableOrderSession();

  DurableOrderSession(const DurableOrderSession &) = delete;
  DurableOrderSession &operator=(const DurableOrderSession &) = delete;
  DurableOrderSession(DurableOrderSession &&) noexcept;
  DurableOrderSession &operator=(DurableOrderSession &&) noexcept;

  void async_submit_order(CreateOrderRequest request,
                          net::RequestContext context,
                          Handler<SubmissionResult> handler);

  // Call this after each private WSS generation change.  The operation marks
  // every nonterminal order as quarantined before fetching all REST pages and
  // only then clears reconciliation flags for orders actually observed.
  void async_reconcile(std::uint64_t stream_generation,
                       net::RequestContext context,
                       Handler<ReconciliationResult> handler);

  void async_apply_wallet_event(WalletEvent event,
                                Handler<TrackedOrder> handler);
  void async_mark_book_removed(std::string hash, Handler<TrackedOrder> handler);
  void async_snapshot(Handler<std::vector<TrackedOrder>> handler);

  [[nodiscard]] std::size_t recovered_records() const noexcept;
  [[nodiscard]] bool ignored_truncated_tail() const noexcept;

private:
  struct Impl;
  explicit DurableOrderSession(std::shared_ptr<Impl> impl)
      : impl_(std::move(impl)) {}

  std::shared_ptr<Impl> impl_;
};

} // namespace predictfun::execution
