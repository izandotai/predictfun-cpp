#pragma once

#include "predictfun/types/evm.hpp"
#include "predictfun/types/exact_number.hpp"
#include "predictfun/types/order.hpp"

#include <array>
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
