#include "predictfun/execution/session.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>

#include <algorithm>
#include <iterator>
#include <string>
#include <unordered_set>
#include <utility>

namespace predictfun::execution {
namespace asio = boost::asio;
namespace {

Error invalid_session(std::string message, std::string field = {}) {
  return Error{ErrorCode::invalid_argument, std::move(message),
               std::move(field)};
}

std::string protocol_decimal(std::string digits) {
  constexpr std::size_t decimals = 18U;
  if (digits.size() <= decimals) {
    digits.insert(0U, decimals + 1U - digits.size(), '0');
  }
  digits.insert(digits.size() - decimals, 1U, '.');
  while (digits.size() > 1U && digits.back() == '0')
    digits.pop_back();
  if (!digits.empty() && digits.back() == '.')
    digits.pop_back();
  return digits;
}

} // namespace

namespace protocol {

Result<ExactDecimal> tracked_share_amount(const CreateOrderRequest &request) {
  const Uint256 *raw = nullptr;
  if (request.order.side == ContractSide::buy)
    raw = &request.order.taker_amount;
  else if (request.order.side == ContractSide::sell)
    raw = &request.order.maker_amount;
  else
    return Error{ErrorCode::invalid_argument, "unsupported order side",
                 "order.side"};
  if (raw->is_zero())
    return Error{ErrorCode::invalid_quantity,
                 "tracked share amount must be positive", "order"};
  return ExactDecimal::parse(protocol_decimal(raw->to_string()));
}

} // namespace protocol

struct DurableOrderSession::Impl
    : public std::enable_shared_from_this<DurableOrderSession::Impl> {
  Impl(asio::any_io_executor executor_value,
       std::shared_ptr<trading::TradingClient> trading_value,
       std::shared_ptr<private_rest::PrivateRestClient> rest_value,
       lifecycle::PersistentOrderTracker tracker_value,
       SessionOptions options_value)
      : strand(asio::make_strand(std::move(executor_value))),
        trading_client(std::move(trading_value)),
        private_rest_client(std::move(rest_value)),
        tracker(std::move(tracker_value)), options(options_value),
        recovered(tracker.recovered_records()),
        truncated_tail(tracker.ignored_truncated_tail()) {}

  struct SubmitOperation
      : public std::enable_shared_from_this<SubmitOperation> {
    std::shared_ptr<Impl> owner;
    CreateOrderRequest request;
    net::RequestContext context;
    Handler<SubmissionResult> handler;
    std::string hash;

    void start() {
      auto valid = trading::protocol::validate_create_order(request);
      if (!valid)
        return finish(valid.error());
      auto amount = protocol::tracked_share_amount(request);
      if (!amount)
        return finish(amount.error());
      hash = request.order_hash;
      auto begun =
          owner->tracker.begin_submission(hash, std::move(amount.value()));
      if (!begun)
        return finish(begun.error());

      owner->trading_client->async_create_order(
          std::move(request), std::move(context),
          [self = shared_from_this()](
              Result<MutationOutcome<CreateOrderReceipt>> result) mutable {
            asio::dispatch(self->owner->strand,
                           [self, result = std::move(result)]() mutable {
                             self->on_result(std::move(result));
                           });
          });
    }

    void on_result(Result<MutationOutcome<CreateOrderReceipt>> result) {
      if (!result) {
        const auto rejection = result.error();
        auto persisted =
            owner->tracker.mark_submission_rejected(hash, rejection);
        if (!persisted)
          return finish(persisted.error());
        const auto *tracked = owner->tracker.find(hash);
        if (!tracked)
          return finish(Error{ErrorCode::protocol_error,
                              "journaled order disappeared", "hash"});
        return finish(SubmissionResult{*tracked, std::nullopt, rejection});
      }

      auto outcome = std::move(result.value());
      auto persisted = owner->tracker.apply_create_outcome(hash, outcome);
      if (!persisted)
        return finish(persisted.error());
      const auto *tracked = owner->tracker.find(hash);
      if (!tracked)
        return finish(Error{ErrorCode::protocol_error,
                            "journaled order disappeared", "hash"});
      finish(SubmissionResult{*tracked, std::move(outcome), std::nullopt});
    }

    void finish(Result<SubmissionResult> result) {
      if (!handler)
        return;
      auto completion = std::move(handler);
      completion(std::move(result));
    }
  };

  struct ReconcileOperation
      : public std::enable_shared_from_this<ReconcileOperation> {
    std::shared_ptr<Impl> owner;
    std::uint64_t generation{0U};
    net::RequestContext context;
    Handler<ReconciliationResult> handler;
    std::vector<OrderRecord> orders;
    std::unordered_set<std::string> cursors;
    std::size_t pages{0U};
    bool owns_active{false};

    void start() {
      if (owner->reconciliation_active)
        return finish(invalid_session("reconciliation is already active"));
      owner->reconciliation_active = true;
      owns_active = true;
      auto quarantined = owner->tracker.require_reconciliation(generation);
      if (!quarantined)
        return finish(quarantined.error());
      request_page(std::nullopt);
    }

    void request_page(std::optional<std::string> after) {
      private_rest::OrdersQuery query;
      query.first = owner->options.reconciliation_page_size;
      query.after = std::move(after);
      owner->private_rest_client->async_get_orders(
          std::move(query), context,
          [self = shared_from_this()](Result<OrdersPage> result) mutable {
            asio::dispatch(self->owner->strand,
                           [self, result = std::move(result)]() mutable {
                             self->on_page(std::move(result));
                           });
          });
    }

    void on_page(Result<OrdersPage> result) {
      if (!result)
        return finish(result.error());
      ++pages;
      auto page = std::move(result.value());
      orders.insert(orders.end(), std::make_move_iterator(page.orders.begin()),
                    std::make_move_iterator(page.orders.end()));

      if (page.cursor && !page.cursor->empty()) {
        if (!cursors.emplace(*page.cursor).second)
          return finish(Error{ErrorCode::protocol_error,
                              "orders pagination cursor repeated", "cursor"});
        if (pages >= owner->options.maximum_reconciliation_pages)
          return finish(Error{ErrorCode::too_many_items,
                              "orders reconciliation exceeded page bound",
                              "maximum_reconciliation_pages"});
        return request_page(std::move(page.cursor));
      }

      auto report = owner->tracker.reconcile(generation, orders);
      if (!report)
        return finish(report.error());
      finish(ReconciliationResult{std::move(report.value()), pages,
                                  orders.size()});
    }

    void finish(Result<ReconciliationResult> result) {
      if (owns_active)
        owner->reconciliation_active = false;
      if (!handler)
        return;
      auto completion = std::move(handler);
      completion(std::move(result));
    }
  };

  asio::strand<asio::any_io_executor> strand;
  std::shared_ptr<trading::TradingClient> trading_client;
  std::shared_ptr<private_rest::PrivateRestClient> private_rest_client;
  lifecycle::PersistentOrderTracker tracker;
  SessionOptions options;
  std::size_t recovered{0U};
  bool truncated_tail{false};
  bool reconciliation_active{false};
};

Result<DurableOrderSession> DurableOrderSession::open(
    asio::any_io_executor executor,
    std::shared_ptr<trading::TradingClient> trading_client,
    std::shared_ptr<private_rest::PrivateRestClient> private_rest_client,
    std::filesystem::path journal_path, SessionOptions options,
    lifecycle::JournalOptions journal_options) {
  if (!trading_client)
    return invalid_session("trading client is required", "trading_client");
  if (!private_rest_client)
    return invalid_session("private REST client is required",
                           "private_rest_client");
  if (options.reconciliation_page_size == 0U ||
      options.reconciliation_page_size > 1'000U)
    return invalid_session("reconciliation page size must be 1..1000",
                           "reconciliation_page_size");
  if (options.maximum_reconciliation_pages == 0U)
    return invalid_session("maximum reconciliation pages must be positive",
                           "maximum_reconciliation_pages");
  auto tracker = lifecycle::PersistentOrderTracker::open(
      std::move(journal_path), journal_options);
  if (!tracker)
    return tracker.error();
  return DurableOrderSession{std::make_shared<Impl>(
      std::move(executor), std::move(trading_client),
      std::move(private_rest_client), std::move(tracker.value()), options)};
}

DurableOrderSession::~DurableOrderSession() = default;
DurableOrderSession::DurableOrderSession(DurableOrderSession &&) noexcept =
    default;
DurableOrderSession &
DurableOrderSession::operator=(DurableOrderSession &&) noexcept = default;

void DurableOrderSession::async_submit_order(
    CreateOrderRequest request, net::RequestContext context,
    Handler<SubmissionResult> handler) {
  auto operation = std::make_shared<Impl::SubmitOperation>();
  operation->owner = impl_;
  operation->request = std::move(request);
  operation->context = std::move(context);
  operation->handler = std::move(handler);
  asio::dispatch(impl_->strand, [operation] { operation->start(); });
}

void DurableOrderSession::async_reconcile(
    std::uint64_t stream_generation, net::RequestContext context,
    Handler<ReconciliationResult> handler) {
  auto operation = std::make_shared<Impl::ReconcileOperation>();
  operation->owner = impl_;
  operation->generation = stream_generation;
  operation->context = std::move(context);
  operation->handler = std::move(handler);
  asio::dispatch(impl_->strand, [operation] { operation->start(); });
}

void DurableOrderSession::async_apply_wallet_event(
    WalletEvent event, Handler<TrackedOrder> handler) {
  auto impl = impl_;
  asio::dispatch(impl->strand, [impl, event = std::move(event),
                                handler = std::move(handler)]() mutable {
    auto changed = impl->tracker.apply_wallet_event(event);
    if (!changed)
      return handler(changed.error());
    const auto *tracked = impl->tracker.find(event.order_hash);
    if (!tracked)
      return handler(Error{ErrorCode::protocol_error,
                           "wallet order disappeared", "order_hash"});
    handler(*tracked);
  });
}

void DurableOrderSession::async_mark_book_removed(
    std::string hash, Handler<TrackedOrder> handler) {
  auto impl = impl_;
  asio::dispatch(impl->strand, [impl, hash = std::move(hash),
                                handler = std::move(handler)]() mutable {
    auto changed = impl->tracker.mark_book_removed(hash);
    if (!changed)
      return handler(changed.error());
    const auto *tracked = impl->tracker.find(hash);
    if (!tracked)
      return handler(
          Error{ErrorCode::protocol_error, "book order disappeared", "hash"});
    handler(*tracked);
  });
}

void DurableOrderSession::async_snapshot(
    Handler<std::vector<TrackedOrder>> handler) {
  auto impl = impl_;
  asio::dispatch(impl->strand, [impl, handler = std::move(handler)]() mutable {
    handler(impl->tracker.snapshot());
  });
}

std::size_t DurableOrderSession::recovered_records() const noexcept {
  return impl_ ? impl_->recovered : 0U;
}

bool DurableOrderSession::ignored_truncated_tail() const noexcept {
  return impl_ && impl_->truncated_tail;
}

} // namespace predictfun::execution
