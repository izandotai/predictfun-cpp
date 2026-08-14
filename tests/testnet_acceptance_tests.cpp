#include "testnet_acceptance_support.hpp"

#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ << "\n";  \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

predictfun::tools::TestnetAcceptanceOptions
parse(std::initializer_list<std::string_view> values) {
  std::vector<std::string_view> arguments{values};
  auto parsed =
      predictfun::tools::parse_testnet_acceptance_arguments(arguments);
  CHECK(parsed);
  return parsed ? parsed.value()
                : predictfun::tools::TestnetAcceptanceOptions{};
}

void test_probe_defaults_to_read_only() {
  auto options =
      parse({"probe", "--owner", "0x1111111111111111111111111111111111111111"});
  CHECK(options.mode == predictfun::tools::TestnetAcceptanceMode::probe);
  CHECK(!options.execute);
  CHECK(options.endpoint.use_tls);
  CHECK(options.scope.operation == predictfun::ApprovalOperation::trade);
  CHECK(options.scope.side == predictfun::ApprovalTradeSide::buy);
}

void test_exact_write_gate() {
  auto options =
      parse({"approve", "--owner", "0x1111111111111111111111111111111111111111",
             "--scope", "trade-sell", "--neg-risk", "--execute", "--evidence",
             "acceptance.jsonl", "--confirm", "wrong"});
  auto rejected = predictfun::tools::validate_testnet_write_gate(options);
  CHECK(!rejected);

  options.confirmation = predictfun::tools::testnet_approval_confirmation(
      options.owner, options.scope);
  auto accepted = predictfun::tools::validate_testnet_write_gate(options);
  CHECK(accepted && accepted.value());
  CHECK(options.confirmation == "APPROVE PREDICT BNB TESTNET 97 " +
                                    options.owner.to_string() +
                                    " trade-sell:neg-risk:regular");
}

void test_exact_approval_amount_is_bound_to_confirmation() {
  auto options = parse(
      {"approve", "--owner", "0x1111111111111111111111111111111111111111",
       "--scope", "split", "--approval-amount", "1000000000000000000",
       "--execute", "--evidence", "acceptance.jsonl", "--confirm", "wrong"});
  CHECK(options.approval_amount &&
        options.approval_amount->to_string() == "1000000000000000000");
  options.confirmation = predictfun::tools::testnet_approval_confirmation(
      options.owner, options.scope, options.approval_amount);
  CHECK(options.confirmation ==
        "APPROVE PREDICT BNB TESTNET 97 " + options.owner.to_string() +
            " split:standard:regular amount=1000000000000000000");
  CHECK(predictfun::tools::validate_testnet_write_gate(options));
}

void test_gate_requires_execute_and_evidence() {
  auto options =
      parse({"approve", "--owner", "0x1111111111111111111111111111111111111111",
             "--scope", "split"});
  options.confirmation = predictfun::tools::testnet_approval_confirmation(
      options.owner, options.scope);
  CHECK(!predictfun::tools::validate_testnet_write_gate(options));
  options.execute = true;
  CHECK(!predictfun::tools::validate_testnet_write_gate(options));
  options.evidence_path = "acceptance.jsonl";
  CHECK(predictfun::tools::validate_testnet_write_gate(options));
}

void test_operator_authorized_secret_file_is_write_mode_only() {
  auto approval = parse(
      {"approve", "--owner", "0x1111111111111111111111111111111111111111",
       "--scope", "split", "--execute", "--evidence", "acceptance.jsonl",
       "--secret-env-file", ".env.local", "--confirm", "wrong"});
  CHECK(approval.secret_env_file == std::filesystem::path{".env.local"});

  const std::vector<std::string_view> read_only{
      "probe", "--owner", "0x1111111111111111111111111111111111111111",
      "--secret-env-file", ".env.local"};
  CHECK(!predictfun::tools::parse_testnet_acceptance_arguments(read_only));
}

void test_invalid_or_ambiguous_arguments_fail_closed() {
  const std::vector<std::string_view> missing_owner{"probe"};
  CHECK(!predictfun::tools::parse_testnet_acceptance_arguments(missing_owner));
  const std::vector<std::string_view> unknown{
      "probe", "--owner", "0x1111111111111111111111111111111111111111",
      "--private-key", "secret"};
  CHECK(!predictfun::tools::parse_testnet_acceptance_arguments(unknown));
  const std::vector<std::string_view> malformed_target{
      "probe", "--owner", "0x1111111111111111111111111111111111111111",
      "--rpc-target", "not-absolute"};
  CHECK(
      !predictfun::tools::parse_testnet_acceptance_arguments(malformed_target));
}

void test_position_probe_is_read_only_and_exact() {
  auto options = parse(
      {"position-probe", "--owner",
       "0x1111111111111111111111111111111111111111", "--operation",
       "split", "--condition-id",
       "0x2222222222222222222222222222222222222222222222222222222222222222",
       "--amount", "1000000", "--token-id", "101", "--token-id",
       "202"});
  CHECK(options.mode ==
        predictfun::tools::TestnetAcceptanceMode::position_probe);
  CHECK(options.position.has_value());
  CHECK(!options.execute);
  CHECK(options.position->token_ids.size() == 2U);
  CHECK(options.scope.operation == predictfun::ApprovalOperation::split);
  CHECK(predictfun::tools::testnet_position_confirmation(
            options.owner, *options.position) ==
        "EXECUTE PREDICT BNB TESTNET 97 " + options.owner.to_string() +
            " split:standard:regular "
            "0x2222222222222222222222222222222222222222222222222222222222222222 "
            "1000000 none 101,202");
}

void test_position_write_gate_binds_every_operation_field() {
  auto options = parse(
      {"position-execute", "--owner",
       "0x1111111111111111111111111111111111111111", "--operation",
       "redeem", "--condition-id",
       "0x3333333333333333333333333333333333333333333333333333333333333333",
       "--index-set", "2", "--token-id", "303", "--execute",
       "--evidence", "position.jsonl", "--confirm", "wrong"});
  CHECK(!predictfun::tools::validate_testnet_position_write_gate(options));
  options.confirmation = predictfun::tools::testnet_position_confirmation(
      options.owner, *options.position);
  CHECK(predictfun::tools::validate_testnet_position_write_gate(options));
  options.position->token_ids.front() = predictfun::Uint256::parse("304").value();
  CHECK(!predictfun::tools::validate_testnet_position_write_gate(options));
}

void test_incomplete_position_plans_fail_closed() {
  const std::vector<std::string_view> no_operation{
      "position-probe", "--owner",
      "0x1111111111111111111111111111111111111111"};
  CHECK(!predictfun::tools::parse_testnet_acceptance_arguments(no_operation));
  const std::vector<std::string_view> no_tokens{
      "position-probe", "--owner",
      "0x1111111111111111111111111111111111111111", "--operation",
      "split", "--condition-id",
      "0x2222222222222222222222222222222222222222222222222222222222222222",
      "--amount", "1"};
  CHECK(!predictfun::tools::parse_testnet_acceptance_arguments(no_tokens));
  const std::vector<std::string_view> bad_redeem{
      "position-probe", "--owner",
      "0x1111111111111111111111111111111111111111", "--operation",
      "redeem", "--condition-id",
      "0x2222222222222222222222222222222222222222222222222222222222222222",
      "--index-set", "3", "--token-id", "1"};
  CHECK(!predictfun::tools::parse_testnet_acceptance_arguments(bad_redeem));
  const std::vector<std::string_view> too_many_split_tokens{
      "position-probe", "--owner",
      "0x1111111111111111111111111111111111111111", "--operation",
      "split", "--condition-id",
      "0x2222222222222222222222222222222222222222222222222222222222222222",
      "--amount", "1", "--token-id", "1", "--token-id", "2",
      "--token-id", "3"};
  CHECK(!predictfun::tools::parse_testnet_acceptance_arguments(
      too_many_split_tokens));
  const std::vector<std::string_view> duplicate_merge_tokens{
      "position-probe", "--owner",
      "0x1111111111111111111111111111111111111111", "--operation",
      "merge", "--condition-id",
      "0x2222222222222222222222222222222222222222222222222222222222222222",
      "--amount", "1", "--token-id", "7", "--token-id", "7"};
  CHECK(!predictfun::tools::parse_testnet_acceptance_arguments(
      duplicate_merge_tokens));
  const std::vector<std::string_view> too_many_redeem_tokens{
      "position-probe", "--owner",
      "0x1111111111111111111111111111111111111111", "--operation",
      "redeem", "--condition-id",
      "0x2222222222222222222222222222222222222222222222222222222222222222",
      "--index-set", "1", "--token-id", "1", "--token-id", "2",
      "--token-id", "3"};
  CHECK(!predictfun::tools::parse_testnet_acceptance_arguments(
      too_many_redeem_tokens));
}

void test_acceptance_readiness_explains_the_next_safe_action() {
  using predictfun::ApprovalCheck;
  using predictfun::ApprovalKind;
  using predictfun::ApprovalStep;
  using predictfun::EvmAddress;
  using predictfun::Uint256;

  const auto token =
      EvmAddress::parse("0x1111111111111111111111111111111111111111")
          .value();
  const auto spender =
      EvmAddress::parse("0x2222222222222222222222222222222222222222")
          .value();
  std::vector<ApprovalCheck> approvals{
      ApprovalCheck{ApprovalStep{"COLLATERAL", ApprovalKind::erc20_allowance,
                                 spender, token, "Approve", "Approve"},
                    false, std::nullopt}};

  auto readiness = predictfun::tools::evaluate_testnet_acceptance_readiness(
      Uint256::parse("300000000000000000").value(), Uint256{}, approvals);
  CHECK(readiness.gas_ready);
  CHECK(!readiness.collateral_ready);
  CHECK(readiness.approval_total == 1U);
  CHECK(readiness.approval_missing == 1U);
  CHECK(!readiness.fully_ready());
  CHECK(predictfun::tools::testnet_acceptance_next_action(readiness).find(
            "official Predict channel") != std::string::npos);

  approvals.front().satisfied = true;
  readiness = predictfun::tools::evaluate_testnet_acceptance_readiness(
      Uint256::parse("1").value(), Uint256::parse("1").value(), approvals);
  CHECK(readiness.fully_ready());
  CHECK(predictfun::tools::testnet_acceptance_next_action(readiness).find(
            "position probe") != std::string::npos);
}

void test_position_gate_accepts_exact_bounded_allowance() {
  using predictfun::ApprovalCheck;
  using predictfun::ApprovalKind;
  using predictfun::ApprovalStep;
  using predictfun::EvmAddress;
  using predictfun::Uint256;
  using predictfun::tools::TestnetPositionOperation;
  using predictfun::tools::TestnetPositionPlan;

  const auto token =
      EvmAddress::parse("0x1111111111111111111111111111111111111111")
          .value();
  const auto spender =
      EvmAddress::parse("0x2222222222222222222222222222222222222222")
          .value();
  TestnetPositionPlan plan;
  plan.operation = TestnetPositionOperation::split;
  plan.amount = Uint256::parse("1000000000000000000").value();
  std::vector<ApprovalCheck> approvals{ApprovalCheck{
      ApprovalStep{"COLLATERAL", ApprovalKind::erc20_allowance, spender, token,
                   "Approve", "Approve"},
      false, Uint256::parse("1000000000000000000").value()}};
  CHECK(predictfun::tools::position_approval_satisfies_plan(plan,
                                                            approvals.front()));
  CHECK(predictfun::tools::position_approvals_satisfy_plan(plan, approvals));
  approvals.front().allowance = Uint256::parse("999999999999999999").value();
  CHECK(!predictfun::tools::position_approval_satisfies_plan(
      plan, approvals.front()));
  CHECK(!predictfun::tools::position_approvals_satisfy_plan(plan, approvals));
  approvals.front().satisfied = true;
  CHECK(predictfun::tools::position_approvals_satisfy_plan(plan, approvals));
}

} // namespace

int main() {
  test_probe_defaults_to_read_only();
  test_exact_write_gate();
  test_exact_approval_amount_is_bound_to_confirmation();
  test_gate_requires_execute_and_evidence();
  test_operator_authorized_secret_file_is_write_mode_only();
  test_invalid_or_ambiguous_arguments_fail_closed();
  test_position_probe_is_read_only_and_exact();
  test_position_write_gate_binds_every_operation_field();
  test_incomplete_position_plans_fail_closed();
  test_acceptance_readiness_explains_the_next_safe_action();
  test_position_gate_accepts_exact_bounded_allowance();
  return failures == 0 ? 0 : 1;
}
