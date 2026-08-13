#pragma once

#include "predictfun/chain/approvals.hpp"
#include "predictfun/types/chain.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace predictfun::tools {

enum class TestnetAcceptanceMode {
  probe,
  approve,
  position_probe,
  position_execute,
};

enum class TestnetPositionOperation { split, merge, redeem, convert };

struct TestnetPositionPlan {
  std::optional<TestnetPositionOperation> operation;
  std::optional<Bytes32> condition_id;
  std::optional<Bytes32> neg_risk_on_chain_id;
  std::optional<Uint256> amount;
  std::optional<Uint256> index_set;
  std::vector<Uint256> token_ids;
  bool is_neg_risk{false};
  bool is_yield_bearing{false};
};

struct TestnetAcceptanceOptions {
  TestnetAcceptanceMode mode{TestnetAcceptanceMode::probe};
  EvmAddress owner;
  ApprovalScope scope{ApprovalOperation::trade, false, false,
                      ApprovalTradeSide::buy};
  std::optional<TestnetPositionPlan> position;
  RpcEndpoint endpoint{default_rpc_endpoint(ChainId::bnb_testnet)};
  std::filesystem::path evidence_path;
  std::string confirmation;
  bool execute{false};
  bool help{false};
};

struct TestnetAcceptanceReadiness {
  bool gas_ready{false};
  bool collateral_ready{false};
  std::size_t approval_total{0};
  std::size_t approval_missing{0};

  [[nodiscard]] bool fully_ready() const noexcept {
    return gas_ready && collateral_ready && approval_missing == 0U;
  }
};

[[nodiscard]] TestnetAcceptanceReadiness
evaluate_testnet_acceptance_readiness(
    const Uint256 &native_balance, const Uint256 &collateral_balance,
    std::span<const ApprovalCheck> approvals);

[[nodiscard]] std::string
testnet_acceptance_next_action(const TestnetAcceptanceReadiness &readiness);

[[nodiscard]] Result<TestnetAcceptanceOptions>
parse_testnet_acceptance_arguments(std::span<const std::string_view> arguments);

[[nodiscard]] std::string approval_scope_code(const ApprovalScope &scope);

[[nodiscard]] std::string
testnet_approval_confirmation(const EvmAddress &owner,
                              const ApprovalScope &scope);

[[nodiscard]] Result<bool>
validate_testnet_write_gate(const TestnetAcceptanceOptions &options);

[[nodiscard]] ApprovalScope
position_approval_scope(const TestnetPositionPlan &plan);

[[nodiscard]] std::string
testnet_position_operation_code(const TestnetPositionPlan &plan);

[[nodiscard]] std::string
testnet_position_confirmation(const EvmAddress &owner,
                              const TestnetPositionPlan &plan);

[[nodiscard]] Result<bool>
validate_testnet_position_plan(const TestnetPositionPlan &plan);

[[nodiscard]] Result<bool>
validate_testnet_position_write_gate(const TestnetAcceptanceOptions &options);

[[nodiscard]] std::string testnet_acceptance_usage();

} // namespace predictfun::tools
