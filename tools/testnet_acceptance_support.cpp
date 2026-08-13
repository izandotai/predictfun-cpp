#include "testnet_acceptance_support.hpp"

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

Result<std::string_view> next_value(std::span<const std::string_view> arguments,
                                    std::size_t &index, std::string_view flag) {
  ++index;
  if (index >= arguments.size() || arguments[index].starts_with("--"))
    return invalid("option requires a value", std::string{flag});
  return arguments[index];
}

} // namespace

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
  } else {
    return invalid("first argument must be probe or approve", "mode");
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
      continue;
    }
    if (flag == "--yield-bearing") {
      options.scope.is_yield_bearing = true;
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
      const bool neg_risk = options.scope.is_neg_risk;
      const bool yield_bearing = options.scope.is_yield_bearing;
      auto scope = parse_scope(value.value());
      if (!scope)
        return scope.error();
      options.scope = scope.value();
      options.scope.is_neg_risk = options.scope.is_neg_risk || neg_risk;
      options.scope.is_yield_bearing = yield_bearing;
    } else if (flag == "--evidence") {
      options.evidence_path = std::filesystem::path{value.value()};
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
                                          const ApprovalScope &scope) {
  return "APPROVE PREDICT BNB TESTNET 97 " + owner.to_string() + " " +
         approval_scope_code(scope);
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
      testnet_approval_confirmation(options.owner, options.scope);
  if (options.confirmation != expected)
    return invalid("confirmation phrase does not match owner and scope",
                   "confirm");
  return true;
}

std::string testnet_acceptance_usage() {
  return R"(predictfun_testnet_acceptance - explicitly gated BNB testnet evidence

Read-only probe (default authority):
  predictfun_testnet_acceptance probe --owner 0x... [options]

Minimal scoped approval (writes only after every gate passes):
  predictfun_testnet_acceptance approve --owner 0x... --scope SCOPE \
    --execute --confirm "PHRASE" --evidence FILE.jsonl [options]

SCOPE: trade-buy | trade-sell | split | merge | redeem | convert
Options: --neg-risk --yield-bearing --rpc-host HOST --rpc-port PORT
         --rpc-target /PATH --evidence FILE.jsonl

Run probe first. It prints the exact confirmation phrase. Approve then reads
the EOA key from an interactive hidden prompt. Keys are never accepted through
arguments, files, environment variables, evidence, or diagnostics. The tool
is hard-wired to BNB testnet chain id 97 and never blindly retries a write.)";
}

} // namespace predictfun::tools
