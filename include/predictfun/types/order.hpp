#pragma once

#include "predictfun/types/evm.hpp"
#include "predictfun/types/exact_number.hpp"
#include "predictfun/types/private_rest.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace predictfun {

enum class ChainId : std::uint64_t { bnb_mainnet = 56, bnb_testnet = 97 };
enum class ExecutionStrategy { market, limit };

struct ProtocolAddresses {
  EvmAddress yield_bearing_ctf_exchange;
  EvmAddress yield_bearing_neg_risk_ctf_exchange;
  EvmAddress yield_bearing_neg_risk_adapter;
  EvmAddress yield_bearing_conditional_tokens;
  EvmAddress yield_bearing_neg_risk_conditional_tokens;
  EvmAddress ctf_exchange;
  EvmAddress neg_risk_ctf_exchange;
  EvmAddress neg_risk_adapter;
  EvmAddress conditional_tokens;
  EvmAddress neg_risk_conditional_tokens;
  EvmAddress usdt;
  EvmAddress kernel;
  EvmAddress ecdsa_validator;
};

[[nodiscard]] Result<ProtocolAddresses> protocol_addresses(ChainId chain_id);

struct MarketContractKind {
  bool is_neg_risk{false};
  bool is_yield_bearing{false};
};

[[nodiscard]] EvmAddress
exchange_address(const ProtocolAddresses &addresses,
                 MarketContractKind kind) noexcept;

struct UnsignedOrder {
  Uint256 salt;
  EvmAddress maker;
  EvmAddress signer;
  std::optional<EvmAddress> taker; // null is the public zero-address taker.
  Uint256 token_id;
  Uint256 maker_amount;
  Uint256 taker_amount;
  Uint256 expiration;
  Uint256 nonce;
  Uint256 fee_rate_bps;
  ContractSide side{ContractSide::buy};
  SignatureType signature_type{SignatureType::eoa};
};

struct SignedOrder : UnsignedOrder {
  std::string signature;
};

struct OrderAmounts {
  Uint256 last_price_wei;
  Uint256 price_per_share_wei;
  Uint256 maker_amount;
  Uint256 taker_amount;
  Uint256 amount;
  Uint256 slippage_bps;
  bool is_min_amount_out{false};
};

struct OrderDepthLevel {
  Uint256 price_wei;
  Uint256 quantity_wei;
};

struct OrderDepth {
  std::vector<OrderDepthLevel> asks;
  std::vector<OrderDepthLevel> bids;
};

struct LimitAmountsInput {
  ContractSide side{ContractSide::buy};
  Uint256 price_per_share_wei;
  Uint256 quantity_wei;
};

struct MarketQuantityInput {
  ContractSide side{ContractSide::buy};
  Uint256 quantity_wei;
  Uint256 slippage_bps;
  bool is_min_amount_out{false};
};

struct MarketValueInput {
  Uint256 value_wei;
  Uint256 slippage_bps;
  bool is_min_amount_out{false};
};

struct BuildOrderInput {
  ExecutionStrategy strategy{ExecutionStrategy::limit};
  ContractSide side{ContractSide::buy};
  Uint256 token_id;
  Uint256 maker_amount;
  Uint256 taker_amount;
  Uint256 fee_rate_bps;
  EvmAddress signer;
  std::optional<EvmAddress> maker;
  std::optional<EvmAddress> taker;
  Uint256 nonce;
  std::optional<Uint256> salt;
  SignatureType signature_type{SignatureType::eoa};
  std::optional<std::uint64_t> expires_at_unix_seconds;
};

using Hash32 = std::array<std::uint8_t, 32>;

} // namespace predictfun
