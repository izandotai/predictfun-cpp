#include "testnet_acceptance_support.hpp"

#include "predictfun/chain/abi.hpp"

#include <charconv>
#include <string>

namespace predictfun::tools {
namespace {

Error invalid(std::string message, std::string field = {}) {
  return Error{ErrorCode::invalid_argument, std::move(message),
               std::move(field)};
}

Result<ApprovalScope> parse_scope(std::string_view value) {
  ApprovalScope scope;
  if (value == "trade-buy") {
    scope.operation = ApprovalOperation::trade;
    scope.side = ApprovalTradeSide::buy;
  } else if (value == "trade-sell") {
    scope.operation = ApprovalOperation::trade;
    scope.side = ApprovalTradeSide::sell;
  } else if (value == "split") {
    scope.operation = ApprovalOperation::split;
  } else if (value == "merge") {
    scope.operation = ApprovalOperation::merge;
  } else if (value == "redeem") {
    scope.operation = ApprovalOperation::redeem;
  } else if (value == "convert") {
    scope.operation = ApprovalOperation::convert;
    scope.is_neg_risk = true;
  } else {
    return invalid("unknown approval scope", "scope");
  }
  return scope;
}

Result<TestnetPositionOperation> parse_position_operation(
    std::string_view value) {
  if (value == "split") return TestnetPositionOperation::split;
  if (value == "merge") return TestnetPositionOperation::merge;
  if (value == "redeem") return TestnetPositionOperation::redeem;
  if (value == "convert") return TestnetPositionOperation::convert;
  return invalid("unknown position operation", "operation");
}

Result<Uint256> parse_uint256(std::string_view value, std::string_view field) {
  auto parsed = Uint256::parse(value);
  if (!parsed)
    return invalid("option requires a canonical uint256", std::string{field});
  return parsed.value();
}

Result<std::string_view> next_value(std::span<const std::string_view> arguments,
                                    std::size_t &index, std::string_view flag) {
  ++index;
  if (index >= arguments.size() || arguments[index].starts_with("--"))
    return invalid("option requires a value", std::string{flag});
  return arguments[index];
}

} // namespace

TestnetAcceptanceReadiness evaluate_testnet_acceptance_readiness(
    const Uint256 &native_balance, const Uint256 &collateral_balance,
    std::span<const ApprovalCheck> approvals) {
  TestnetAcceptanceReadiness readiness;
  readiness.gas_ready = !native_balance.is_zero();
  readiness.collateral_ready = !collateral_balance.is_zero();
  readiness.approval_total = approvals.size();
  for (const auto &approval : approvals)
    if (!approval.satisfied)
      ++readiness.approval_missing;
  return readiness;
}

std::string testnet_acceptance_next_action(
    const TestnetAcceptanceReadiness &readiness) {
  if (!readiness.gas_ready)
    return "fund native BNB testnet gas through an official BNB faucet";
  if (!readiness.collateral_ready)
    return "obtain registered Predict BNB-testnet collateral through an "
           "official Predict channel, then rerun probe";
  if (readiness.approval_missing != 0U)
    return "run the exact scoped approval shown by this probe";
  return "run a read-only position probe for the exact market operation";
}

Result<TestnetAcceptanceOptions> parse_testnet_acceptance_arguments(
    std::span<const std::string_view> arguments) {
  TestnetAcceptanceOptions options;
  if (arguments.empty())
    return options;
  if (arguments.front() == "--help" || arguments.front() == "-h") {
    options.help = true;
    return options;
  }
  if (arguments.front() == "probe") {
    options.mode = TestnetAcceptanceMode::probe;
  } else if (arguments.front() == "approve") {
    options.mode = TestnetAcceptanceMode::approve;
  } else if (arguments.front() == "position-probe") {
    options.mode = TestnetAcceptanceMode::position_probe;
    options.position.emplace();
  } else if (arguments.front() == "position-execute") {
    options.mode = TestnetAcceptanceMode::position_execute;
    options.position.emplace();
  } else {
    return invalid(
        "first argument must be probe, approve, position-probe, or position-execute",
        "mode");
  }

  for (std::size_t index = 1U; index < arguments.size(); ++index) {
    const auto flag = arguments[index];
    if (flag == "--help" || flag == "-h") {
      options.help = true;
      continue;
    }
    if (flag == "--execute") {
      options.execute = true;
      continue;
    }
    if (flag == "--neg-risk") {
      options.scope.is_neg_risk = true;
      if (options.position) options.position->is_neg_risk = true;
      continue;
    }
    if (flag == "--yield-bearing") {
      options.scope.is_yield_bearing = true;
      if (options.position) options.position->is_yield_bearing = true;
      continue;
    }

    auto value = next_value(arguments, index, flag);
    if (!value)
      return value.error();
    if (flag == "--owner") {
      auto owner = EvmAddress::parse(value.value());
      if (!owner)
        return owner.error();
      options.owner = owner.value();
    } else if (flag == "--scope") {
      if (options.position)
        return invalid("--scope is not valid for position operations", "scope");
      const bool neg_risk = options.scope.is_neg_risk;
      const bool yield_bearing = options.scope.is_yield_bearing;
      auto scope = parse_scope(value.value());
      if (!scope)
        return scope.error();
      options.scope = scope.value();
      options.scope.is_neg_risk = options.scope.is_neg_risk || neg_risk;
      options.scope.is_yield_bearing = yield_bearing;
    } else if (flag == "--operation") {
      if (!options.position)
        return invalid("--operation requires a position mode", "operation");
      auto operation = parse_position_operation(value.value());
      if (!operation) return operation.error();
      options.position->operation = operation.value();
    } else if (flag == "--condition-id") {
      if (!options.position)
        return invalid("--condition-id requires a position mode",
                       "condition_id");
      auto condition = chain::abi::decode_bytes32(value.value());
      if (!condition) return condition.error();
      options.position->condition_id = condition.value();
    } else if (flag == "--neg-risk-on-chain-id") {
      if (!options.position)
        return invalid("--neg-risk-on-chain-id requires a position mode",
                       "neg_risk_on_chain_id");
      auto identifier = chain::abi::decode_bytes32(value.value());
      if (!identifier) return identifier.error();
      options.position->neg_risk_on_chain_id = identifier.value();
    } else if (flag == "--amount") {
      if (!options.position)
        return invalid("--amount requires a position mode", "amount");
      auto amount = parse_uint256(value.value(), "amount");
      if (!amount) return amount.error();
      options.position->amount = amount.value();
    } else if (flag == "--approval-amount") {
      if (options.position)
        return invalid("--approval-amount is not valid for position operations",
                       "approval_amount");
      auto amount = parse_uint256(value.value(), "approval_amount");
      if (!amount || amount.value().is_zero())
        return invalid("--approval-amount requires a positive uint256",
                       "approval_amount");
      options.approval_amount = amount.value();
    } else if (flag == "--index-set") {
      if (!options.position)
        return invalid("--index-set requires a position mode", "index_set");
      auto index_set = parse_uint256(value.value(), "index_set");
      if (!index_set) return index_set.error();
      options.position->index_set = index_set.value();
    } else if (flag == "--token-id") {
      if (!options.position)
        return invalid("--token-id requires a position mode", "token_id");
      auto token_id = parse_uint256(value.value(), "token_id");
      if (!token_id) return token_id.error();
      options.position->token_ids.push_back(token_id.value());
    } else if (flag == "--evidence") {
      options.evidence_path = std::filesystem::path{value.value()};
    } else if (flag == "--secret-env-file") {
      options.secret_env_file = std::filesystem::path{value.value()};
    } else if (flag == "--confirm") {
      options.confirmation = value.value();
    } else if (flag == "--rpc-host") {
      options.endpoint.host = value.value();
    } else if (flag == "--rpc-port") {
      options.endpoint.port = value.value();
    } else if (flag == "--rpc-target") {
      options.endpoint.target = value.value();
    } else {
      return invalid("unknown option", std::string{flag});
    }
  }

  if (options.help)
    return options;
  if (options.owner.empty())
    return invalid("owner is required", "owner");
  if (options.endpoint.host.empty() || options.endpoint.port.empty() ||
      options.endpoint.target.empty())
    return invalid("RPC endpoint fields must not be empty", "rpc");
  if (!options.endpoint.target.starts_with('/'))
    return invalid("RPC target must start with '/'", "rpc_target");
  if (options.scope.operation == ApprovalOperation::convert &&
      !options.scope.is_neg_risk)
    return invalid("convert requires --neg-risk", "scope");
  if (options.position) {
    auto valid = validate_testnet_position_plan(*options.position);
    if (!valid) return valid.error();
    options.scope = position_approval_scope(*options.position);
  }
  if (!options.secret_env_file.empty() &&
      options.mode != TestnetAcceptanceMode::approve &&
      options.mode != TestnetAcceptanceMode::position_execute)
    return invalid("--secret-env-file is valid only for write modes",
                   "secret_env_file");
  return options;
}

std::string approval_scope_code(const ApprovalScope &scope) {
  std::string operation;
  switch (scope.operation) {
  case ApprovalOperation::trade:
    operation = scope.side == ApprovalTradeSide::buy    ? "trade-buy"
                : scope.side == ApprovalTradeSide::sell ? "trade-sell"
                                                        : "trade-both";
    break;
  case ApprovalOperation::split:
    operation = "split";
    break;
  case ApprovalOperation::merge:
    operation = "merge";
    break;
  case ApprovalOperation::redeem:
    operation = "redeem";
    break;
  case ApprovalOperation::convert:
    operation = "convert";
    break;
  }
  operation += scope.is_neg_risk ? ":neg-risk" : ":standard";
  operation += scope.is_yield_bearing ? ":yield" : ":regular";
  return operation;
}

std::string testnet_approval_confirmation(const EvmAddress &owner,
                                          const ApprovalScope &scope,
                                          const std::optional<Uint256> &amount) {
  auto confirmation = "APPROVE PREDICT BNB TESTNET 97 " + owner.to_string() +
                      " " + approval_scope_code(scope);
  if (amount) confirmation += " amount=" + amount->to_string();
  return confirmation;
}

Result<bool>
validate_testnet_write_gate(const TestnetAcceptanceOptions &options) {
  if (options.mode != TestnetAcceptanceMode::approve)
    return invalid("write gate is only valid in approve mode", "mode");
  if (!options.execute)
    return invalid("approve mode requires --execute", "execute");
  if (options.owner.empty())
    return invalid("approve mode requires an owner", "owner");
  if (options.evidence_path.empty())
    return invalid("approve mode requires --evidence", "evidence");
  const auto expected =
      testnet_approval_confirmation(options.owner, options.scope,
                                    options.approval_amount);
  if (options.confirmation != expected)
    return invalid("confirmation phrase does not match owner and scope",
                   "confirm");
  return true;
}

ApprovalScope position_approval_scope(const TestnetPositionPlan &plan) {
  ApprovalOperation operation = ApprovalOperation::split;
  switch (*plan.operation) {
  case TestnetPositionOperation::split:
    operation = ApprovalOperation::split;
    break;
  case TestnetPositionOperation::merge:
    operation = ApprovalOperation::merge;
    break;
  case TestnetPositionOperation::redeem:
    operation = ApprovalOperation::redeem;
    break;
  case TestnetPositionOperation::convert:
    operation = ApprovalOperation::convert;
    break;
  }
  return ApprovalScope{operation,
                       *plan.operation == TestnetPositionOperation::convert
                           ? true
                           : plan.is_neg_risk,
                       plan.is_yield_bearing, ApprovalTradeSide::both};
}

bool position_approval_satisfies_plan(const TestnetPositionPlan &plan,
                                      const ApprovalCheck &approval) {
  const auto at_least = [](const Uint256 &available, const Uint256 &required) {
    const auto &left = available.to_string();
    const auto &right = required.to_string();
    return left.size() != right.size() ? left.size() > right.size()
                                       : left >= right;
  };
  if (approval.satisfied) return true;
  return approval.step.kind == ApprovalKind::erc20_allowance && plan.amount &&
         approval.allowance && at_least(*approval.allowance, *plan.amount);
}

bool position_approvals_satisfy_plan(
    const TestnetPositionPlan &plan, std::span<const ApprovalCheck> approvals) {
  return std::all_of(approvals.begin(), approvals.end(),
                     [&](const ApprovalCheck &approval) {
                       return position_approval_satisfies_plan(plan, approval);
                     });
}

std::string testnet_position_operation_code(const TestnetPositionPlan &plan) {
  const char *operation = "split";
  switch (*plan.operation) {
  case TestnetPositionOperation::split:
    operation = "split";
    break;
  case TestnetPositionOperation::merge:
    operation = "merge";
    break;
  case TestnetPositionOperation::redeem:
    operation = "redeem";
    break;
  case TestnetPositionOperation::convert:
    operation = "convert";
    break;
  }
  return std::string{operation} +
         (*plan.operation == TestnetPositionOperation::convert ||
                  plan.is_neg_risk
              ? ":neg-risk"
              : ":standard") +
         (plan.is_yield_bearing ? ":yield" : ":regular");
}

std::string testnet_position_confirmation(const EvmAddress &owner,
                                           const TestnetPositionPlan &plan) {
  const auto identifier =
      *plan.operation == TestnetPositionOperation::convert
          ? chain::abi::encode_hex(*plan.neg_risk_on_chain_id)
          : chain::abi::encode_hex(*plan.condition_id);
  std::string tokens;
  for (const auto &token : plan.token_ids) {
    if (!tokens.empty()) tokens += ',';
    tokens += token.to_string();
  }
  return "EXECUTE PREDICT BNB TESTNET 97 " + owner.to_string() + " " +
         testnet_position_operation_code(plan) + " " + identifier + " " +
         (plan.amount ? plan.amount->to_string() : "all") + " " +
         (plan.index_set ? plan.index_set->to_string() : "none") + " " +
         tokens;
}

Result<bool> validate_testnet_position_plan(const TestnetPositionPlan &plan) {
  if (!plan.operation)
    return invalid("position mode requires --operation", "operation");
  const bool convert = *plan.operation == TestnetPositionOperation::convert;
  if (convert) {
    if (!plan.neg_risk_on_chain_id)
      return invalid("convert requires --neg-risk-on-chain-id",
                     "neg_risk_on_chain_id");
    if (!plan.index_set || plan.index_set->is_zero())
      return invalid("convert requires a positive --index-set", "index_set");
  } else if (!plan.condition_id) {
    return invalid("operation requires --condition-id", "condition_id");
  }
  if (*plan.operation != TestnetPositionOperation::redeem) {
    if (!plan.amount || plan.amount->is_zero())
      return invalid("operation requires a positive --amount", "amount");
  } else {
    if (!plan.index_set || (plan.index_set->to_string() != "1" &&
                            plan.index_set->to_string() != "2"))
      return invalid("redeem requires --index-set 1 or 2", "index_set");
    if (plan.is_neg_risk && (!plan.amount || plan.amount->is_zero()))
      return invalid("negative-risk redeem requires a positive --amount",
                     "amount");
  }
  const bool paired_operation =
      *plan.operation == TestnetPositionOperation::split ||
      *plan.operation == TestnetPositionOperation::merge;
  const auto minimum_tokens = paired_operation ? 2U : 1U;
  if (plan.token_ids.size() < minimum_tokens)
    return invalid("operation requires token ids for balance evidence",
                   "token_id");
  if (paired_operation && plan.token_ids.size() != 2U)
    return invalid("split and merge require exactly two token ids",
                   "token_id");
  if (*plan.operation == TestnetPositionOperation::redeem &&
      plan.token_ids.size() > 2U)
    return invalid("redeem accepts at most two token ids", "token_id");
  for (const auto &token : plan.token_ids)
    if (token.is_zero())
      return invalid("token id must be positive", "token_id");
  for (std::size_t left = 0; left < plan.token_ids.size(); ++left)
    for (std::size_t right = left + 1; right < plan.token_ids.size(); ++right)
      if (plan.token_ids[left].to_string() ==
          plan.token_ids[right].to_string())
        return invalid("token ids must be unique", "token_id");
  return true;
}

Result<bool>
validate_testnet_position_write_gate(const TestnetAcceptanceOptions &options) {
  if (options.mode != TestnetAcceptanceMode::position_execute ||
      !options.position)
    return invalid("position write gate requires position-execute mode",
                   "mode");
  if (!options.execute)
    return invalid("position-execute requires --execute", "execute");
  if (options.evidence_path.empty())
    return invalid("position-execute requires --evidence", "evidence");
  const auto expected =
      testnet_position_confirmation(options.owner, *options.position);
  if (options.confirmation != expected)
    return invalid("confirmation phrase does not match the exact operation plan",
                   "confirm");
  return true;
}

std::string testnet_acceptance_usage() {
  return R"(predictfun_testnet_acceptance - explicitly gated BNB testnet evidence

Read-only probe (default authority):
  predictfun_testnet_acceptance probe --owner 0x... [options]

Minimal scoped approval (writes only after every gate passes):
  predictfun_testnet_acceptance approve --owner 0x... --scope SCOPE \
    [--approval-amount UINT256] --execute --confirm "PHRASE" \
    --evidence FILE.jsonl [options]

Read-only position preflight (prints exact transaction and confirmation):
  predictfun_testnet_acceptance position-probe --owner 0x... \
    --operation split --condition-id 0x... --amount BASE_UNITS \
    --token-id YES_ID --token-id NO_ID [options]

Execute one preflighted position transaction:
  predictfun_testnet_acceptance position-execute [same exact plan] \
    --execute --confirm "PHRASE" --evidence FILE.jsonl

Operator-authorized local secret input for either write mode:
  --secret-env-file .env.local

SCOPE: trade-buy | trade-sell | split | merge | redeem | convert
Options: --neg-risk --yield-bearing --rpc-host HOST --rpc-port PORT
         --rpc-target /PATH --evidence FILE.jsonl --secret-env-file FILE
         --approval-amount UINT256

Position fields: --operation split|merge|redeem|convert, --condition-id 0x...,
 --neg-risk-on-chain-id 0x..., --amount UINT256, --index-set UINT256, and
 repeated --token-id UINT256 values used for before/after balance evidence.

Run probe first. It prints the exact confirmation phrase. Write modes read the
EOA key from an interactive hidden prompt unless --secret-env-file is supplied.
That file must contain PREDICTFUN_BNB_TESTNET_PRIVATE_KEY and may contain the
matching PREDICTFUN_BNB_TESTNET_WALLET_ADDRESS. Raw key arguments and process
environment variables remain unsupported. The key and file path never enter
evidence or diagnostics. The tool is hard-wired to BNB testnet chain id 97 and
never blindly retries a write.)";
}

} // namespace predictfun::tools
