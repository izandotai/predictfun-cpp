#pragma once

#include "predictfun/chain/approvals.hpp"
#include "predictfun/types/chain.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace predictfun::tools {

enum class TestnetAcceptanceMode { probe, approve };

struct TestnetAcceptanceOptions {
  TestnetAcceptanceMode mode{TestnetAcceptanceMode::probe};
  EvmAddress owner;
  ApprovalScope scope{ApprovalOperation::trade, false, false,
                      ApprovalTradeSide::buy};
  RpcEndpoint endpoint{default_rpc_endpoint(ChainId::bnb_testnet)};
  std::filesystem::path evidence_path;
  std::string confirmation;
  bool execute{false};
  bool help{false};
};

[[nodiscard]] Result<TestnetAcceptanceOptions>
parse_testnet_acceptance_arguments(std::span<const std::string_view> arguments);

[[nodiscard]] std::string approval_scope_code(const ApprovalScope &scope);

[[nodiscard]] std::string
testnet_approval_confirmation(const EvmAddress &owner,
                              const ApprovalScope &scope);

[[nodiscard]] Result<bool>
validate_testnet_write_gate(const TestnetAcceptanceOptions &options);

[[nodiscard]] std::string testnet_acceptance_usage();

} // namespace predictfun::tools
