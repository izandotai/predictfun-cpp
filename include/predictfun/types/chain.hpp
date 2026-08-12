#pragma once

#include "predictfun/types/evm.hpp"
#include "predictfun/types/exact_number.hpp"
#include "predictfun/types/order.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace predictfun {

using Bytes32 = std::array<std::uint8_t, 32>;

enum class BlockTag { latest, safe, finalized, pending };

struct RpcEndpoint {
  std::string host;
  std::string port{"443"};
  std::string target{"/"};
  bool use_tls{true};
};

[[nodiscard]] RpcEndpoint default_rpc_endpoint(ChainId chain_id);

struct CallRequest {
  EvmAddress to;
  std::string data;
  std::optional<EvmAddress> from;
};

struct UnsignedTransaction {
  EvmAddress to;
  std::string data;
  Uint256 value;
};

struct PopulatedTransaction {
  ChainId chain_id{ChainId::bnb_testnet};
  EvmAddress from;
  EvmAddress to;
  Uint256 nonce;
  Uint256 gas_price;
  Uint256 gas_limit;
  Uint256 value;
  std::string data;
};

struct RawTransaction {
  std::string bytes;
  std::string transaction_hash;
};

enum class TransactionSubmissionState { accepted, ambiguous };

struct TransactionSubmission {
  TransactionSubmissionState state{TransactionSubmissionState::accepted};
  std::string transaction_hash;
  std::optional<Error> ambiguity;
};

struct ReceiptWaitOptions {
  std::chrono::milliseconds poll_interval{300};
};

struct TransactionReceipt {
  std::string transaction_hash;
  std::optional<std::string> block_hash;
  std::optional<Uint256> block_number;
  std::optional<Uint256> status;
  std::optional<Uint256> gas_used;
  std::optional<Uint256> effective_gas_price;

  [[nodiscard]] bool confirmed_success() const noexcept {
    return status && status->to_string() == "1";
  }
  [[nodiscard]] bool confirmed_revert() const noexcept {
    return status && status->is_zero();
  }
};

enum class TransactionExecutionState {
  confirmed,
  reverted,
  outcome_unknown,
};

// Retains the locally signed bytes and deterministic transaction hash even
// when the transport response or receipt wait is ambiguous. Callers can
// reconcile outcome_unknown without ever rebuilding or blindly resending.
struct TransactionExecution {
  TransactionExecutionState state{TransactionExecutionState::outcome_unknown};
  PopulatedTransaction transaction;
  RawTransaction raw_transaction;
  TransactionSubmission submission;
  std::optional<TransactionReceipt> receipt;
  std::optional<Error> issue;

  [[nodiscard]] bool confirmed_success() const noexcept {
    return state == TransactionExecutionState::confirmed && receipt &&
           receipt->confirmed_success();
  }
};

enum class ApprovalOperation { trade, split, merge, redeem, convert };
enum class ApprovalKind { erc1155_operator, erc20_allowance };
enum class ApprovalTradeSide { both, buy, sell };

struct ApprovalScope {
  ApprovalOperation operation{ApprovalOperation::trade};
  bool is_neg_risk{false};
  bool is_yield_bearing{false};
  ApprovalTradeSide side{ApprovalTradeSide::both};
};

struct ApprovalStep {
  std::string id;
  ApprovalKind kind{ApprovalKind::erc20_allowance};
  EvmAddress spender;
  EvmAddress token;
  std::string label;
  std::string description;

  friend bool operator==(const ApprovalStep &, const ApprovalStep &) = default;
};

struct ApprovalCheck {
  ApprovalStep step;
  bool satisfied{false};
  std::optional<Uint256> allowance;
};

enum class ApprovalProgressState {
  checking,
  skipped,
  submitting,
  confirmed,
  failed,
};

struct ApprovalProgress {
  ApprovalStep step;
  ApprovalProgressState state{ApprovalProgressState::checking};
  std::optional<std::string> transaction_hash;
  std::optional<Error> issue;
};

struct ApprovalStepResult {
  ApprovalStep step;
  ApprovalProgressState state{ApprovalProgressState::failed};
  std::optional<TransactionExecution> transaction;
  std::optional<Error> issue;
};

struct ApprovalRunOptions {
  bool skip_satisfied{true};
  bool stop_on_error{true};
  ReceiptWaitOptions receipt_wait;
};

struct ApprovalRunReport {
  bool success{false};
  std::vector<ApprovalStepResult> steps;
};

struct PositionOperation {
  Bytes32 condition_id{};
  Uint256 amount;
  bool is_neg_risk{false};
  bool is_yield_bearing{false};
};

struct RedeemOperation {
  Bytes32 condition_id{};
  std::uint8_t index_set{1U};
  bool is_neg_risk{false};
  bool is_yield_bearing{false};
  std::optional<Uint256> amount;
};

struct ConvertOperation {
  Bytes32 neg_risk_on_chain_id{};
  Uint256 index_set;
  Uint256 amount;
  bool is_yield_bearing{false};
};

} // namespace predictfun
