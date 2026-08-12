#include "predictfun/codec/private_rest.hpp"
#include "predictfun/lifecycle/tracker.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace {

int failures = 0;
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": CHECK failed: " #condition << '\n';                    \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

constexpr auto hash =
    "0x0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

predictfun::ExactDecimal exact(std::string_view value) {
  return predictfun::ExactDecimal::parse(value).value();
}

predictfun::OrderRecord order(std::string status, std::string filled) {
  const auto json = std::string{R"({"success":true,"data":{"order":{"hash":")"} +
      hash +
      R"(","salt":"1","maker":"0x1111111111111111111111111111111111111111","signer":"0x2222222222222222222222222222222222222222","taker":"0x0000000000000000000000000000000000000000","tokenId":"123","makerAmount":"1000000000000000000","takerAmount":"500000000000000000","expiration":"0","nonce":"7","feeRateBps":"100","side":0,"signatureType":0,"signature":"0xsig"},"id":"o1","marketId":42,"currency":"USDT","amount":"1","amountFilled":")" +
      filled + R"(","isNegRisk":false,"isYieldBearing":true,"strategy":"LIMIT","status":")" +
      status + R"(","rewardEarningRate":"0"}})";
  return predictfun::codec::decode_order_response(json).value();
}

predictfun::WalletEvent wallet(predictfun::WalletEventType type,
                               std::string filled) {
  return predictfun::WalletEvent{
      predictfun::EnumValue<predictfun::WalletEventType>{type, "EVENT"},
      "o1", hash,
      predictfun::EvmAddress::parse(
          "0x1111111111111111111111111111111111111111").value(),
      42,
      predictfun::WalletEventDetails{
          predictfun::MarketId{42U}, 1U, "BTC up?",
          predictfun::EnumValue<predictfun::WalletOutcome>{
              predictfun::WalletOutcome::yes, "Yes"},
          predictfun::EnumValue<predictfun::QuoteType>{
              predictfun::QuoteType::bid, "Bid"},
          exact("1"), exact(filled), exact("0.5"), exact("0.5"),
          exact("0.2"),
          predictfun::EnumValue<predictfun::OrderStrategy>{
              predictfun::OrderStrategy::limit, "LIMIT"},
          "crypto"},
      {}, {}, {}, {}, {}, {}};
}

void test_ambiguous_submission_requires_reconciliation() {
  predictfun::lifecycle::OrderTracker tracker;
  CHECK(tracker.begin_submission(hash, exact("1")));
  predictfun::MutationOutcome<predictfun::CreateOrderReceipt> outcome;
  outcome.disposition = predictfun::MutationDisposition::ambiguous;
  outcome.ambiguity = predictfun::Error{
      predictfun::ErrorCode::ambiguous_submission, "read timed out", {}};
  outcome.reconciliation_key = hash;
  CHECK(tracker.apply_create_outcome(hash, outcome));
  const auto *tracked = tracker.find(hash);
  CHECK(tracked);
  CHECK(tracked && tracked->state ==
                       predictfun::OrderLifecycleState::ambiguous);
  CHECK(tracked && tracked->reconciliation_required);
  CHECK(tracked && !tracked->safe_to_rebuild_with_new_nonce());

  const auto report = tracker.reconcile(2U, {order("OPEN", "0.25")});
  CHECK(report.complete());
  tracked = tracker.find(hash);
  CHECK(tracked && tracked->state ==
                       predictfun::OrderLifecycleState::partially_filled);
  CHECK(tracked && !tracked->reconciliation_required);
}

void test_wallet_and_terminal_transitions() {
  predictfun::lifecycle::OrderTracker tracker;
  CHECK(tracker.begin_submission(hash, exact("1")));
  predictfun::MutationOutcome<predictfun::CreateOrderReceipt> ack;
  ack.receipt = predictfun::CreateOrderReceipt{"OK", "o1", hash, {}};
  ack.reconciliation_key = hash;
  CHECK(tracker.apply_create_outcome(hash, ack));
  CHECK(tracker.find(hash)->state ==
        predictfun::OrderLifecycleState::accepted);
  CHECK(tracker.apply_wallet_event(
      wallet(predictfun::WalletEventType::order_transaction_success, "0.4")));
  CHECK(tracker.find(hash)->state ==
        predictfun::OrderLifecycleState::partially_filled);
  CHECK(tracker.apply_rest_order(order("MATCHED", "1")));
  CHECK(tracker.find(hash)->state == predictfun::OrderLifecycleState::filled);
  CHECK(tracker.find(hash)->terminal());
  CHECK(!tracker.find(hash)->safe_to_rebuild_with_new_nonce());
}

void test_book_removal_is_not_chain_cancel() {
  predictfun::lifecycle::OrderTracker tracker;
  CHECK(tracker.begin_submission(hash, exact("1")));
  CHECK(tracker.mark_book_removed(hash));
  CHECK(tracker.find(hash)->state ==
        predictfun::OrderLifecycleState::book_removed);
  CHECK(tracker.find(hash)->reconciliation_required);
  CHECK(!tracker.find(hash)->terminal());
  CHECK(!tracker.find(hash)->safe_to_rebuild_with_new_nonce());
}

void test_disconnect_does_not_invent_terminal_state() {
  predictfun::lifecycle::OrderTracker tracker;
  CHECK(tracker.begin_submission(hash, exact("1")));
  tracker.require_reconciliation(7U);
  auto report = tracker.reconcile(7U, {});
  CHECK(!report.complete());
  CHECK(report.unresolved_hashes.size() == 1U);
  CHECK(tracker.find(hash)->state ==
        predictfun::OrderLifecycleState::submission_pending);
  CHECK(tracker.find(hash)->reconciliation_required);
}

} // namespace

int main() {
  test_ambiguous_submission_requires_reconciliation();
  test_wallet_and_terminal_transitions();
  test_book_removal_is_not_chain_cancel();
  test_disconnect_does_not_invent_terminal_state();
  if (failures != 0) std::cerr << failures << " test(s) failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
