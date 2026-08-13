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

} // namespace

int main() {
  test_probe_defaults_to_read_only();
  test_exact_write_gate();
  test_gate_requires_execute_and_evidence();
  test_invalid_or_ambiguous_arguments_fail_closed();
  test_position_probe_is_read_only_and_exact();
  test_position_write_gate_binds_every_operation_field();
  test_incomplete_position_plans_fail_closed();
  return failures == 0 ? 0 : 1;
}
