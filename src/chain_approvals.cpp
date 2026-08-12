#include "predictfun/chain/approvals.hpp"

#include "predictfun/chain/abi.hpp"

#include <algorithm>
#include <string_view>

namespace predictfun::chain {
namespace {

struct Copy {
  std::string_view label;
  std::string_view description;
};

Copy copy(ApprovalKind kind, bool neg_exchange, bool neg_adapter,
          bool conditional_tokens) {
  if (kind == ApprovalKind::erc20_allowance) {
    if (neg_adapter)
      return {"Multi-Outcome Split Allowance",
              "Grants the adapter permission to use collateral to split positions."};
    if (conditional_tokens)
      return {"Split Allowance",
              "Grants permission to use collateral to split positions."};
    if (neg_exchange)
      return {"Multi-Outcome Allowance",
              "Grants the multi-outcome exchange permission to use collateral."};
    return {"Exchange Allowance",
            "Grants the exchange permission to use collateral to trade."};
  }
  if (neg_adapter)
    return {"Approve Multi-Outcome Adapter",
            "Allows the multi-outcome adapter to manage outcome tokens."};
  if (neg_exchange)
    return {"Approve Multi-Outcome",
            "Allows interaction with multi-outcome markets."};
  return {"Approve Exchange", "Allows interaction with the exchange."};
}

ApprovalStep step(ApprovalKind kind, EvmAddress spender, EvmAddress token,
                  bool neg_exchange = false, bool neg_adapter = false,
                  bool conditional_tokens = false) {
  const auto text = copy(kind, neg_exchange, neg_adapter, conditional_tokens);
  const auto prefix = kind == ApprovalKind::erc20_allowance
                          ? "ERC20_ALLOWANCE:"
                          : "ERC1155_APPROVAL:";
  return ApprovalStep{std::string{prefix} + spender.to_string(), kind, spender,
                      token, std::string{text.label},
                      std::string{text.description}};
}

} // namespace

Result<std::vector<ApprovalStep>> approval_steps(ChainId chain_id,
                                                 const ApprovalScope &scope) {
  auto registry = protocol_addresses(chain_id);
  if (!registry) return registry.error();
  const auto &a = registry.value();
  const auto exchange = exchange_address(
      a, MarketContractKind{scope.is_neg_risk, scope.is_yield_bearing});
  const auto ctf = scope.is_yield_bearing
                       ? (scope.is_neg_risk
                              ? a.yield_bearing_neg_risk_conditional_tokens
                              : a.yield_bearing_conditional_tokens)
                       : (scope.is_neg_risk ? a.neg_risk_conditional_tokens
                                            : a.conditional_tokens);
  const auto adapter = scope.is_yield_bearing
                           ? a.yield_bearing_neg_risk_adapter
                           : a.neg_risk_adapter;
  std::vector<ApprovalStep> result;
  const auto erc1155 = [&](EvmAddress spender, bool neg_exchange,
                           bool neg_adapter) {
    return step(ApprovalKind::erc1155_operator, spender, ctf, neg_exchange,
                neg_adapter);
  };
  const auto erc20 = [&](EvmAddress spender, bool neg_exchange,
                         bool neg_adapter, bool conditional_tokens) {
    return step(ApprovalKind::erc20_allowance, spender, a.usdt, neg_exchange,
                neg_adapter, conditional_tokens);
  };

  switch (scope.operation) {
  case ApprovalOperation::trade:
    if (scope.side != ApprovalTradeSide::buy)
      result.push_back(erc1155(exchange, scope.is_neg_risk, false));
    if (scope.is_neg_risk)
      result.push_back(erc1155(adapter, false, true));
    if (scope.side != ApprovalTradeSide::sell)
      result.push_back(erc20(exchange, scope.is_neg_risk, false, false));
    return result;
  case ApprovalOperation::split:
    result.push_back(scope.is_neg_risk
                         ? erc20(adapter, false, true, false)
                         : erc20(ctf, false, false, true));
    return result;
  case ApprovalOperation::merge:
  case ApprovalOperation::redeem:
    if (scope.is_neg_risk)
      result.push_back(erc1155(adapter, false, true));
    return result;
  case ApprovalOperation::convert:
    if (!scope.is_neg_risk)
      return Error{ErrorCode::invalid_argument,
                   "convert approvals require a negative-risk market",
                   "is_neg_risk"};
    result.push_back(erc1155(adapter, false, true));
    return result;
  }
  return Error{ErrorCode::invalid_argument, "unknown approval operation",
               "operation"};
}

Result<std::vector<ApprovalStep>>
all_approval_steps(ChainId chain_id, std::optional<bool> is_yield_bearing) {
  const std::array tracks = {false, true};
  std::vector<ApprovalStep> result;
  for (const auto track : tracks) {
    if (is_yield_bearing && track != *is_yield_bearing) continue;
    for (const auto neg : {false, true}) {
      for (const auto operation : {ApprovalOperation::trade,
                                   ApprovalOperation::split,
                                   ApprovalOperation::merge,
                                   ApprovalOperation::redeem}) {
        auto steps = approval_steps(chain_id,
                                    ApprovalScope{operation, neg, track,
                                                  ApprovalTradeSide::both});
        if (!steps) return steps.error();
        for (auto &candidate : steps.value()) {
          if (std::ranges::none_of(result, [&](const ApprovalStep &existing) {
                return existing.id == candidate.id;
              }))
            result.push_back(std::move(candidate));
        }
      }
    }
  }
  return result;
}

Result<UnsignedTransaction>
approval_transaction(const ApprovalStep &step, std::optional<Uint256> amount,
                     bool approved) {
  Result<std::string> data =
      step.kind == ApprovalKind::erc1155_operator
          ? abi::erc1155_set_approval_for_all(step.spender, approved)
          : abi::erc20_approve(
                step.spender,
                approved
                    ? amount.value_or(Uint256::parse(
                                          "115792089237316195423570985008687907853269984665640564039457584007913129639935")
                                          .value())
                    : Uint256{});
  if (!data) return data.error();
  return UnsignedTransaction{step.token, std::move(data.value()), Uint256{}};
}

} // namespace predictfun::chain
