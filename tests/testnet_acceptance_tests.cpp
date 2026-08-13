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

} // namespace

int main() {
  test_probe_defaults_to_read_only();
  test_exact_write_gate();
  test_gate_requires_execute_and_evidence();
  test_invalid_or_ambiguous_arguments_fail_closed();
  return failures == 0 ? 0 : 1;
}
