#pragma once

#include "predictfun/types/evm.hpp"
#include "predictfun/types/exact_number.hpp"
#include "predictfun/types/market.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace predictfun {

struct ReferralInfo {
  std::string code;
  std::string status;
};

struct PointsInfo {
  ExactDecimal total;
  std::optional<std::uint64_t> rank;
};

struct Account {
  std::string name;
  EvmAddress address;
  std::optional<std::string> image_url;
  std::optional<ReferralInfo> referral;
  std::optional<PointsInfo> points;
};

struct PrivateOutcome {
  std::optional<std::string> name;
  std::optional<std::uint64_t> index_set;
  std::optional<Uint256> on_chain_id;
};

struct Position {
  std::string id;
  Market market;
  PrivateOutcome outcome;
  ExactDecimal amount;
  ExactDecimal value_usd;
  ExactDecimal average_buy_price_usd;
  ExactDecimal pnl_usd;
};

struct PositionsPage {
  std::optional<std::string> cursor;
  std::vector<Position> positions;
};

enum class ContractSide : std::uint8_t { buy = 0, sell = 1, unknown = 255 };
enum class SignatureType : std::uint8_t {
  eoa = 0,
  poly_proxy = 1,
  poly_gnosis_safe = 2,
  unknown = 255
};
enum class OrderStrategy { market, limit, unknown };
enum class OrderStatus {
  open,
  matched,
  cancelled,
  expired,
  invalidated,
  failed,
  unknown
};

struct ContractOrder {
  std::string hash;
  Uint256 salt;
  EvmAddress maker;
  EvmAddress signer;
  std::optional<EvmAddress> taker; // null means the zero/open taker address.
  Uint256 token_id;
  Uint256 maker_amount;
  Uint256 taker_amount;
  Uint256 expiration;
  Uint256 nonce;
  Uint256 fee_rate_bps;
  EnumValue<ContractSide> side;
  EnumValue<SignatureType> signature_type;
  std::string signature;
};

struct OrderRecord {
  ContractOrder order;
  std::string id;
  MarketId market_id;
  std::string currency;
  ExactDecimal amount;
  ExactDecimal amount_filled;
  bool is_neg_risk{false};
  bool is_yield_bearing{false};
  EnumValue<OrderStrategy> strategy;
  EnumValue<OrderStatus> status;
  ExactDecimal reward_earning_rate;
};

struct OrdersPage {
  std::optional<std::string> cursor;
  std::vector<OrderRecord> orders;
};

struct ActivityFee {
  ExactDecimal amount;
  std::string type;
};

struct ActivityOrder {
  std::string quote_type;
  ExactDecimal amount;
  ExactDecimal price;
  std::optional<ActivityFee> fee;
};

struct ActivityEvent {
  std::string name;
  std::string created_at;
  std::optional<std::string> transaction_hash;
  std::optional<ExactDecimal> amount_filled;
  std::optional<ExactDecimal> price_executed;
  std::optional<ActivityOrder> order;
  std::optional<Market> market;
  std::optional<PrivateOutcome> outcome;
};

struct ActivityPage {
  std::optional<std::string> cursor;
  std::vector<ActivityEvent> events;
};

} // namespace predictfun
