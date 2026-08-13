#include "testnet_acceptance_support.hpp"

#include "predictfun/chain/abi.hpp"
#include "predictfun/chain/client.hpp"
#include "predictfun/chain/local_transaction_signer.hpp"
#include "predictfun/net/http.hpp"
#include "predictfun/order/local_signer.hpp"
#include "predictfun/types/secret.hpp"

#include <boost/asio/io_context.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {

using predictfun::ApprovalCheck;
using predictfun::ApprovalProgress;
using predictfun::ApprovalProgressState;
using predictfun::ApprovalRunReport;
using predictfun::ApprovalStep;
using predictfun::ChainId;
using predictfun::Error;
using predictfun::ErrorCode;
using predictfun::Result;
using predictfun::TransactionExecutionState;
using predictfun::Uint256;
using predictfun::UnsignedTransaction;
using predictfun::chain::ChainClient;
using predictfun::tools::TestnetAcceptanceOptions;
using predictfun::tools::TestnetPositionOperation;
using predictfun::tools::TestnetPositionPlan;

struct Snapshot {
  Uint256 collateral_balance;
  std::uint8_t collateral_decimals{0};
  std::vector<ApprovalCheck> approvals;
};

struct PositionSnapshot {
  Uint256 collateral_balance;
  std::uint8_t collateral_decimals{0};
  std::vector<Uint256> token_balances;
  std::vector<ApprovalCheck> approvals;
};

std::string json_escape(std::string_view input) {
  std::string output;
  output.reserve(input.size() + 8U);
  for (const char value : input) {
    switch (value) {
    case '\\':
      output += "\\\\";
      break;
    case '"':
      output += "\\\"";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(value) >= 0x20U)
        output += value;
      break;
    }
  }
  return output;
}

std::int64_t unix_milliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

class EvidenceWriter {
public:
  static Result<EvidenceWriter> open(const std::filesystem::path &path) {
    std::error_code issue;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, issue);
      if (issue)
        return Error{ErrorCode::storage_failure,
                     "cannot create evidence directory", "evidence"};
    }
    EvidenceWriter writer;
    writer.stream_.open(path, std::ios::binary | std::ios::app);
    if (!writer.stream_)
      return Error{ErrorCode::storage_failure, "cannot open evidence file",
                   "evidence"};
    writer.path_ = path;
    return writer;
  }

  EvidenceWriter() = default;
  EvidenceWriter(const EvidenceWriter &) = delete;
  EvidenceWriter &operator=(const EvidenceWriter &) = delete;
  EvidenceWriter(EvidenceWriter &&) noexcept = default;
  EvidenceWriter &operator=(EvidenceWriter &&) noexcept = default;

  bool write(std::string_view event, std::string_view fields = {}) {
    stream_ << "{\"schema\":\"predictfun.testnet.acceptance.v1\","
               "\"timestamp_unix_ms\":"
            << unix_milliseconds() << ",\"event\":\"" << json_escape(event)
            << '"';
    if (!fields.empty())
      stream_ << ',' << fields;
    stream_ << "}\n";
    stream_.flush();
    return static_cast<bool>(stream_);
  }

  const std::filesystem::path &path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
  std::ofstream stream_;
};

std::string error_fields(const Error &error) {
  return "\"error_code\":" + std::to_string(static_cast<unsigned>(error.code)) +
         ",\"error_field\":\"" + json_escape(error.field) +
         "\",\"error_message\":\"" + json_escape(error.message) + '"';
}

const char *approval_state(ApprovalProgressState state) {
  switch (state) {
  case ApprovalProgressState::checking:
    return "checking";
  case ApprovalProgressState::skipped:
    return "skipped";
  case ApprovalProgressState::submitting:
    return "submitting";
  case ApprovalProgressState::confirmed:
    return "confirmed";
  case ApprovalProgressState::failed:
    return "failed";
  }
  return "unknown";
}

std::string approval_check_fields(const ApprovalCheck &check,
                                  std::string_view phase) {
  std::string fields =
      "\"phase\":\"" + std::string{phase} + "\",\"step_id\":\"" +
      json_escape(check.step.id) + "\",\"kind\":\"" +
      (check.step.kind == predictfun::ApprovalKind::erc20_allowance
           ? "erc20_allowance"
           : "erc1155_operator") +
      "\",\"token\":\"" + check.step.token.to_string() + "\",\"spender\":\"" +
      check.step.spender.to_string() +
      "\",\"satisfied\":" + (check.satisfied ? "true" : "false");
  if (check.allowance)
    fields +=
        ",\"allowance_base_units\":\"" + check.allowance->to_string() + '"';
  return fields;
}

void print_error(std::string_view stage, const Error &error) {
  std::cerr << stage << " failed: code=" << static_cast<unsigned>(error.code)
            << " field=" << error.field << " message=" << error.message << '\n';
}

Result<predictfun::SecretString> read_private_key() {
  std::cerr << "EOA private key (hidden; never stored): " << std::flush;
#ifdef _WIN32
  const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  DWORD original_mode = 0;
  if (input == INVALID_HANDLE_VALUE || input == nullptr ||
      !GetConsoleMode(input, &original_mode)) {
    std::cerr << '\n';
    return Error{ErrorCode::invalid_argument,
                 "private key requires an interactive console", "stdin"};
  }
  if (!SetConsoleMode(input, original_mode & ~ENABLE_ECHO_INPUT)) {
    std::cerr << '\n';
    return Error{ErrorCode::invalid_argument, "cannot disable console echo",
                 "stdin"};
  }
  std::string value;
  std::getline(std::cin, value);
  SetConsoleMode(input, original_mode);
#else
  if (!isatty(STDIN_FILENO)) {
    std::cerr << '\n';
    return Error{ErrorCode::invalid_argument,
                 "private key requires an interactive console", "stdin"};
  }
  termios original{};
  if (tcgetattr(STDIN_FILENO, &original) != 0) {
    std::cerr << '\n';
    return Error{ErrorCode::invalid_argument, "cannot inspect console mode",
                 "stdin"};
  }
  auto hidden = original;
  hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) != 0) {
    std::cerr << '\n';
    return Error{ErrorCode::invalid_argument, "cannot disable console echo",
                 "stdin"};
  }
  std::string value;
  std::getline(std::cin, value);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
#endif
  std::cerr << '\n';
  if (!std::cin || value.empty()) {
    predictfun::secure_erase(value);
    return Error{ErrorCode::invalid_argument, "private key input was empty",
                 "stdin"};
  }
  return predictfun::SecretString{std::move(value)};
}

using SnapshotHandler = std::function<void(Result<Snapshot>)>;

void read_snapshot(ChainClient &client, const TestnetAcceptanceOptions &options,
                   const std::vector<ApprovalStep> &steps,
                   SnapshotHandler handler) {
  auto registry = predictfun::protocol_addresses(ChainId::bnb_testnet);
  if (!registry)
    return handler(registry.error());
  client.async_erc20_decimals(
      registry.value().usdt,
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{15}),
      [&client, collateral = registry.value().usdt, owner = options.owner,
       steps,
       handler = std::move(handler)](Result<std::uint8_t> decimals) mutable {
        if (!decimals)
          return handler(decimals.error());
        client.async_erc20_balance(
            collateral, owner,
            predictfun::net::RequestContext::with_timeout(
                std::chrono::seconds{15}),
            [&client, owner, steps, decimals = decimals.value(),
             handler = std::move(handler)](Result<Uint256> balance) mutable {
              if (!balance)
                return handler(balance.error());
              client.async_check_approvals(
                  owner, steps,
                  predictfun::net::RequestContext::with_timeout(
                      std::chrono::seconds{15}),
                  [balance = std::move(balance.value()), decimals,
                   handler = std::move(handler)](
                      Result<std::vector<ApprovalCheck>> approvals) mutable {
                    if (!approvals)
                      return handler(approvals.error());
                    handler(Snapshot{std::move(balance), decimals,
                                     std::move(approvals.value())});
                  });
            });
      });
}

void record_snapshot(EvidenceWriter *evidence, const Snapshot &snapshot,
                     std::string_view phase) {
  std::cout << phase << " collateral base units: "
            << snapshot.collateral_balance.to_string() << " (decimals="
            << static_cast<unsigned>(snapshot.collateral_decimals) << ")\n";
  if (evidence)
    evidence->write(
        "balance_snapshot",
        "\"phase\":\"" + std::string{phase} +
            "\",\"asset\":\"collateral\",\"balance_base_units\":\"" +
            snapshot.collateral_balance.to_string() +
            "\",\"decimals\":" + std::to_string(snapshot.collateral_decimals));
  for (const auto &check : snapshot.approvals) {
    std::cout << "  " << check.step.id << " "
              << (check.satisfied ? "SATISFIED" : "MISSING") << '\n';
    if (evidence)
      evidence->write("approval_snapshot", approval_check_fields(check, phase));
  }
}

Result<UnsignedTransaction>
position_transaction(const TestnetPositionPlan &plan) {
  switch (*plan.operation) {
  case TestnetPositionOperation::split:
    return predictfun::chain::split_transaction(
        ChainId::bnb_testnet,
        predictfun::PositionOperation{*plan.condition_id, *plan.amount,
                                      plan.is_neg_risk, plan.is_yield_bearing});
  case TestnetPositionOperation::merge:
    return predictfun::chain::merge_transaction(
        ChainId::bnb_testnet,
        predictfun::PositionOperation{*plan.condition_id, *plan.amount,
                                      plan.is_neg_risk, plan.is_yield_bearing});
  case TestnetPositionOperation::redeem:
    return predictfun::chain::redeem_transaction(
        ChainId::bnb_testnet,
        predictfun::RedeemOperation{
            *plan.condition_id,
            static_cast<std::uint8_t>(plan.index_set->to_string() == "1" ? 1U
                                                                         : 2U),
            plan.is_neg_risk, plan.is_yield_bearing, plan.amount});
  case TestnetPositionOperation::convert:
    return predictfun::chain::convert_transaction(
        ChainId::bnb_testnet, predictfun::ConvertOperation{
                                  *plan.neg_risk_on_chain_id, *plan.index_set,
                                  *plan.amount, plan.is_yield_bearing});
  }
  return Error{ErrorCode::invalid_argument, "unknown position operation",
               "operation"};
}

Result<predictfun::EvmAddress>
position_token_contract(const TestnetPositionPlan &plan) {
  auto registry = predictfun::protocol_addresses(ChainId::bnb_testnet);
  if (!registry)
    return registry.error();
  const bool neg =
      *plan.operation == TestnetPositionOperation::convert || plan.is_neg_risk;
  if (plan.is_yield_bearing)
    return neg ? registry.value().yield_bearing_neg_risk_conditional_tokens
               : registry.value().yield_bearing_conditional_tokens;
  return neg ? registry.value().neg_risk_conditional_tokens
             : registry.value().conditional_tokens;
}

using PositionSnapshotHandler = std::function<void(Result<PositionSnapshot>)>;

void read_position_snapshot(ChainClient &client,
                            const TestnetAcceptanceOptions &options,
                            const std::vector<ApprovalStep> &steps,
                            PositionSnapshotHandler handler) {
  auto registry = predictfun::protocol_addresses(ChainId::bnb_testnet);
  auto token = position_token_contract(*options.position);
  if (!registry)
    return handler(registry.error());
  if (!token)
    return handler(token.error());
  client.async_erc20_decimals(
      registry.value().usdt,
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{15}),
      [&client, collateral = registry.value().usdt, owner = options.owner,
       token = token.value(), steps, token_ids = options.position->token_ids,
       handler = std::move(handler)](Result<std::uint8_t> decimals) mutable {
        if (!decimals)
          return handler(decimals.error());
        client.async_erc20_balance(
            collateral, owner,
            predictfun::net::RequestContext::with_timeout(
                std::chrono::seconds{15}),
            [&client, owner, token, steps = std::move(steps),
             token_ids = std::move(token_ids), decimals = decimals.value(),
             handler = std::move(handler)](
                Result<Uint256> collateral_balance) mutable {
              if (!collateral_balance)
                return handler(collateral_balance.error());
              auto state = std::make_shared<PositionSnapshot>();
              state->collateral_balance = std::move(collateral_balance.value());
              state->collateral_decimals = decimals;
              state->token_balances.resize(token_ids.size());
              auto read_token =
                  std::make_shared<std::function<void(std::size_t)>>();
              *read_token = [&client, owner, token, steps = std::move(steps),
                             token_ids = std::move(token_ids), state,
                             read_token, handler = std::move(handler)](
                                std::size_t index) mutable {
                if (index == token_ids.size()) {
                  client.async_check_approvals(
                      owner, steps,
                      predictfun::net::RequestContext::with_timeout(
                          std::chrono::seconds{15}),
                      [state, handler = std::move(handler)](
                          Result<std::vector<ApprovalCheck>>
                              approvals) mutable {
                        if (!approvals)
                          return handler(approvals.error());
                        state->approvals = std::move(approvals.value());
                        handler(std::move(*state));
                      });
                  return;
                }
                client.async_erc1155_balance(
                    token, owner, token_ids[index],
                    predictfun::net::RequestContext::with_timeout(
                        std::chrono::seconds{15}),
                    [index, state, read_token,
                     handler](Result<Uint256> balance) mutable {
                      if (!balance)
                        return handler(balance.error());
                      state->token_balances[index] = std::move(balance.value());
                      (*read_token)(index + 1U);
                    });
              };
              (*read_token)(0U);
            });
      });
}

void record_position_snapshot(EvidenceWriter *evidence,
                              const TestnetPositionPlan &plan,
                              const PositionSnapshot &snapshot,
                              std::string_view phase) {
  std::cout << phase << " collateral base units: "
            << snapshot.collateral_balance.to_string() << " (decimals="
            << static_cast<unsigned>(snapshot.collateral_decimals) << ")\n";
  if (evidence)
    evidence->write(
        "balance_snapshot",
        "\"phase\":\"" + std::string{phase} +
            "\",\"asset\":\"collateral\",\"balance_base_units\":\"" +
            snapshot.collateral_balance.to_string() +
            "\",\"decimals\":" + std::to_string(snapshot.collateral_decimals));
  for (std::size_t index = 0; index < plan.token_ids.size(); ++index) {
    std::cout << "  token " << plan.token_ids[index].to_string() << ": "
              << snapshot.token_balances[index].to_string() << '\n';
    if (evidence)
      evidence->write("position_snapshot",
                      "\"phase\":\"" + std::string{phase} +
                          "\",\"token_id\":\"" +
                          plan.token_ids[index].to_string() +
                          "\",\"balance_base_units\":\"" +
                          snapshot.token_balances[index].to_string() + '"');
  }
  for (const auto &check : snapshot.approvals) {
    std::cout << "  " << check.step.id << " "
              << (check.satisfied ? "SATISFIED" : "MISSING") << '\n';
    if (evidence)
      evidence->write("approval_snapshot", approval_check_fields(check, phase));
  }
}

std::string transaction_plan_fields(const UnsignedTransaction &transaction,
                                    const TestnetPositionPlan &plan) {
  auto bytes = predictfun::chain::abi::decode_hex(transaction.data);
  std::string calldata_hash;
  if (bytes) {
    auto hash = predictfun::order::keccak256(bytes.value());
    if (hash)
      calldata_hash = predictfun::order::to_hex(hash.value());
  }
  return "\"operation\":\"" +
         predictfun::tools::testnet_position_operation_code(plan) +
         "\",\"to\":\"" + transaction.to.to_string() + "\",\"value_wei\":\"" +
         transaction.value.to_string() + "\",\"calldata\":\"" +
         json_escape(transaction.data) + "\",\"calldata_hash\":\"" +
         json_escape(calldata_hash) + '"';
}

int run_position(const TestnetAcceptanceOptions &options) {
  const auto &plan = *options.position;
  auto transaction = position_transaction(plan);
  if (!transaction) {
    print_error("position plan", transaction.error());
    return EXIT_FAILURE;
  }
  auto approval_plan = predictfun::chain::approval_steps(
      ChainId::bnb_testnet, predictfun::tools::position_approval_scope(plan));
  if (!approval_plan) {
    print_error("approval plan", approval_plan.error());
    return EXIT_FAILURE;
  }

  std::optional<EvidenceWriter> evidence;
  if (!options.evidence_path.empty()) {
    auto opened = EvidenceWriter::open(options.evidence_path);
    if (!opened) {
      print_error("evidence", opened.error());
      return EXIT_FAILURE;
    }
    evidence.emplace(std::move(opened.value()));
    if (!evidence->write(
            "session_started",
            "\"mode\":\"" +
                std::string{options.mode ==
                                    predictfun::tools::TestnetAcceptanceMode::
                                        position_probe
                                ? "position-probe"
                                : "position-execute"} +
                "\",\"chain_id\":97,\"owner\":\"" + options.owner.to_string() +
                "\"," + transaction_plan_fields(transaction.value(), plan))) {
      std::cerr << "cannot append evidence\n";
      return EXIT_FAILURE;
    }
  }

  std::cout << "operation: "
            << predictfun::tools::testnet_position_operation_code(plan)
            << "\ncontract: " << transaction.value().to.to_string()
            << "\nvalue wei: " << transaction.value().value.to_string()
            << "\ncalldata: " << transaction.value().data << '\n';

  boost::asio::io_context io;
  auto transport =
      std::make_shared<predictfun::net::BeastHttpTransport>(io.get_executor());
  predictfun::chain::ClientOptions client_options;
  client_options.expected_chain_id = ChainId::bnb_testnet;
  client_options.endpoint = options.endpoint;
  ChainClient client(io.get_executor(), transport, client_options);
  int exit_code = EXIT_FAILURE;

  client.async_chain_id(
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{15}),
      [&](Result<ChainId> chain) {
        if (!chain) {
          print_error("chain validation", chain.error());
          if (evidence)
            evidence->write("chain_validation_failed",
                            error_fields(chain.error()));
          return;
        }
        if (evidence)
          evidence->write("chain_verified", "\"chain_id\":97");
        read_position_snapshot(
            client, options, approval_plan.value(),
            [&](Result<PositionSnapshot> before) {
              if (!before) {
                print_error("preflight snapshot", before.error());
                if (evidence)
                  evidence->write("preflight_failed",
                                  error_fields(before.error()));
                return;
              }
              record_position_snapshot(evidence ? &*evidence : nullptr, plan,
                                       before.value(), "before");
              const bool approvals_satisfied = std::all_of(
                  before.value().approvals.begin(),
                  before.value().approvals.end(),
                  [](const ApprovalCheck &check) { return check.satisfied; });

              client.async_call(
                  predictfun::CallRequest{transaction.value().to,
                                          transaction.value().data,
                                          options.owner},
                  predictfun::BlockTag::latest,
                  predictfun::net::RequestContext::with_timeout(
                      std::chrono::seconds{20}),
                  [&, approvals_satisfied, before = std::move(before.value())](
                      Result<std::string> simulation) mutable {
                    if (!simulation) {
                      print_error("eth_call preflight", simulation.error());
                      if (evidence)
                        evidence->write("operation_preflight_failed",
                                        error_fields(simulation.error()));
                      std::cerr << "no transaction sent\n";
                      return;
                    }
                    if (evidence)
                      evidence->write("operation_preflight_passed",
                                      "\"approvals_satisfied\":" +
                                          std::string{approvals_satisfied
                                                          ? "true"
                                                          : "false"});
                    std::cout
                        << "eth_call preflight: PASS\nconfirmation: "
                        << predictfun::tools::testnet_position_confirmation(
                               options.owner, plan)
                        << '\n';
                    if (options.mode ==
                        predictfun::tools::TestnetAcceptanceMode::
                            position_probe) {
                      if (evidence)
                        evidence->write("position_probe_complete");
                      exit_code = EXIT_SUCCESS;
                      return;
                    }
                    if (!approvals_satisfied) {
                      std::cerr << "required approval is missing; run the "
                                   "separate approve flow first\n";
                      if (evidence)
                        evidence->write("operation_execution_blocked",
                                        "\"reason\":\"approval_missing\"");
                      return;
                    }
                    auto secret = read_private_key();
                    if (!secret) {
                      print_error("signer input", secret.error());
                      return;
                    }
                    auto signer = predictfun::order::LocalSigner::create(
                        std::move(secret.value()));
                    if (!signer) {
                      print_error("signer", signer.error());
                      if (evidence)
                        evidence->write("signer_rejected",
                                        error_fields(signer.error()));
                      return;
                    }
                    if (signer.value().address() != options.owner) {
                      std::cerr << "signer address does not match --owner\n";
                      if (evidence)
                        evidence->write("signer_rejected",
                                        "\"reason\":\"owner_mismatch\"");
                      return;
                    }
                    if (evidence &&
                        !evidence->write("write_authorized",
                                         "\"chain_id\":97,\"owner\":\"" +
                                             options.owner.to_string() + "\"," +
                                             transaction_plan_fields(
                                                 transaction.value(), plan))) {
                      std::cerr
                          << "cannot persist write authorization evidence\n";
                      return;
                    }
                    auto signer_holder =
                        std::make_shared<predictfun::order::LocalSigner>(
                            std::move(signer.value()));
                    predictfun::ReceiptWaitOptions wait;
                    wait.poll_interval = std::chrono::milliseconds{300};
                    client.async_execute_transaction(
                        options.owner, transaction.value(),
                        [signer_holder](const predictfun::Hash32 &digest) {
                          return signer_holder->sign_digest(digest);
                        },
                        wait,
                        predictfun::net::RequestContext::with_timeout(
                            std::chrono::minutes{3}),
                        [&, signer_holder](
                            Result<predictfun::TransactionExecution> result) {
                          if (!result) {
                            print_error("operation execution", result.error());
                            if (evidence)
                              evidence->write("operation_execution_failed",
                                              error_fields(result.error()));
                            return;
                          }
                          const auto &execution = result.value();
                          std::string fields =
                              "\"transaction_hash\":\"" +
                              json_escape(
                                  execution.raw_transaction.transaction_hash) +
                              "\",\"state\":\"" +
                              (execution.state ==
                                       TransactionExecutionState::confirmed
                                   ? "confirmed"
                               : execution.state ==
                                       TransactionExecutionState::reverted
                                   ? "reverted"
                                   : "outcome_unknown") +
                              "\",\"blind_retry_performed\":false";
                          if (execution.receipt &&
                              execution.receipt->block_number)
                            fields +=
                                ",\"block_number\":\"" +
                                execution.receipt->block_number->to_string() +
                                '"';
                          if (evidence)
                            evidence->write("operation_transaction_result",
                                            fields);
                          if (!execution.confirmed_success()) {
                            std::cerr
                                << "operation was not confirmed; no retry "
                                   "was attempted\n";
                            return;
                          }
                          read_position_snapshot(
                              client, options, approval_plan.value(),
                              [&,
                               signer_holder](Result<PositionSnapshot> after) {
                                if (!after) {
                                  print_error("postflight snapshot",
                                              after.error());
                                  if (evidence)
                                    evidence->write(
                                        "postflight_failed",
                                        error_fields(after.error()));
                                  return;
                                }
                                record_position_snapshot(
                                    evidence ? &*evidence : nullptr, plan,
                                    after.value(), "after");
                                if (evidence)
                                  evidence->write(
                                      "position_execute_complete",
                                      "\"result\":\"confirmed\","
                                      "\"blind_retry_performed\":false");
                                exit_code = EXIT_SUCCESS;
                              });
                        });
                  });
            });
      });
  io.run();
  if (evidence)
    std::cout << "evidence: " << evidence->path().string() << '\n';
  return exit_code;
}

int run(const TestnetAcceptanceOptions &options) {
  if (options.position)
    return run_position(options);
  auto planned =
      predictfun::chain::approval_steps(ChainId::bnb_testnet, options.scope);
  if (!planned) {
    print_error("approval plan", planned.error());
    return EXIT_FAILURE;
  }

  std::optional<EvidenceWriter> evidence;
  if (!options.evidence_path.empty()) {
    auto opened = EvidenceWriter::open(options.evidence_path);
    if (!opened) {
      print_error("evidence", opened.error());
      return EXIT_FAILURE;
    }
    evidence.emplace(std::move(opened.value()));
    if (!evidence->write(
            "session_started",
            "\"mode\":\"" +
                std::string{
                    options.mode ==
                            predictfun::tools::TestnetAcceptanceMode::probe
                        ? "probe"
                        : "approve"} +
                "\",\"chain_id\":97,\"owner\":\"" + options.owner.to_string() +
                "\",\"scope\":\"" +
                predictfun::tools::approval_scope_code(options.scope) + '"')) {
      std::cerr << "cannot append evidence\n";
      return EXIT_FAILURE;
    }
  }

  boost::asio::io_context io;
  auto transport =
      std::make_shared<predictfun::net::BeastHttpTransport>(io.get_executor());
  predictfun::chain::ClientOptions client_options;
  client_options.expected_chain_id = ChainId::bnb_testnet;
  client_options.endpoint = options.endpoint;
  ChainClient client(io.get_executor(), transport, client_options);
  int exit_code = EXIT_FAILURE;

  client.async_chain_id(
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{15}),
      [&](Result<ChainId> chain) {
        if (!chain) {
          print_error("chain validation", chain.error());
          if (evidence)
            evidence->write("chain_validation_failed",
                            error_fields(chain.error()));
          return;
        }
        std::cout << "BNB testnet chain verified: 97\n";
        if (evidence)
          evidence->write("chain_verified", "\"chain_id\":97");
        read_snapshot(
            client, options, planned.value(), [&](Result<Snapshot> before) {
              if (!before) {
                print_error("preflight snapshot", before.error());
                if (evidence)
                  evidence->write("preflight_failed",
                                  error_fields(before.error()));
                return;
              }
              record_snapshot(evidence ? &*evidence : nullptr, before.value(),
                              "before");
              std::cout << "confirmation: "
                        << predictfun::tools::testnet_approval_confirmation(
                               options.owner, options.scope)
                        << '\n';
              if (options.mode ==
                  predictfun::tools::TestnetAcceptanceMode::probe) {
                if (evidence)
                  evidence->write("probe_complete");
                exit_code = EXIT_SUCCESS;
                return;
              }

              std::vector<ApprovalStep> missing;
              for (const auto &check : before.value().approvals)
                if (!check.satisfied)
                  missing.push_back(check.step);
              if (missing.empty()) {
                std::cout << "all selected approvals are already satisfied; "
                             "no transaction sent\n";
                if (evidence)
                  evidence->write("approve_complete",
                                  "\"result\":\"already_satisfied\","
                                  "\"transactions_sent\":0");
                exit_code = EXIT_SUCCESS;
                return;
              }

              auto secret = read_private_key();
              if (!secret) {
                print_error("signer input", secret.error());
                if (evidence)
                  evidence->write("signer_rejected",
                                  error_fields(secret.error()));
                return;
              }
              auto signer = predictfun::order::LocalSigner::create(
                  std::move(secret.value()));
              if (!signer) {
                print_error("signer", signer.error());
                if (evidence)
                  evidence->write("signer_rejected",
                                  error_fields(signer.error()));
                return;
              }
              if (signer.value().address() != options.owner) {
                std::cerr << "signer address does not match --owner\n";
                if (evidence)
                  evidence->write("signer_rejected",
                                  "\"reason\":\"owner_mismatch\"");
                return;
              }

              if (evidence &&
                  !evidence->write("write_authorized",
                                   "\"chain_id\":97,\"owner\":\"" +
                                       options.owner.to_string() +
                                       "\",\"scope\":\"" +
                                       predictfun::tools::approval_scope_code(
                                           options.scope) +
                                       "\",\"missing_steps\":" +
                                       std::to_string(missing.size()))) {
                std::cerr << "cannot persist write authorization evidence\n";
                return;
              }

              auto signer_holder =
                  std::make_shared<predictfun::order::LocalSigner>(
                      std::move(signer.value()));
              predictfun::ApprovalRunOptions run_options;
              run_options.skip_satisfied = true;
              run_options.stop_on_error = true;
              run_options.receipt_wait.poll_interval =
                  std::chrono::milliseconds{300};
              client.async_run_approvals(
                  options.owner, options.owner, std::move(missing), {},
                  [signer_holder](const predictfun::Hash32 &digest) {
                    return signer_holder->sign_digest(digest);
                  },
                  run_options,
                  [&](const ApprovalProgress &progress) {
                    std::cout << "  " << progress.step.id << " "
                              << approval_state(progress.state) << '\n';
                    if (evidence) {
                      std::string fields = "\"step_id\":\"" +
                                           json_escape(progress.step.id) +
                                           "\",\"state\":\"" +
                                           approval_state(progress.state) + '"';
                      if (progress.transaction_hash)
                        fields += ",\"transaction_hash\":\"" +
                                  json_escape(*progress.transaction_hash) + '"';
                      if (progress.issue)
                        fields += ',' + error_fields(*progress.issue);
                      evidence->write("approval_progress", fields);
                    }
                  },
                  predictfun::net::RequestContext::with_timeout(
                      std::chrono::minutes{3}),
                  [&, signer_holder](Result<ApprovalRunReport> report) {
                    if (!report) {
                      print_error("approval execution", report.error());
                      if (evidence)
                        evidence->write("approval_execution_failed",
                                        error_fields(report.error()));
                      return;
                    }
                    for (const auto &step : report.value().steps) {
                      if (!evidence || !step.transaction)
                        continue;
                      const auto &transaction = *step.transaction;
                      std::string fields =
                          "\"step_id\":\"" + json_escape(step.step.id) +
                          "\",\"state\":\"" +
                          (transaction.state ==
                                   TransactionExecutionState::confirmed
                               ? "confirmed"
                           : transaction.state ==
                                   TransactionExecutionState::reverted
                               ? "reverted"
                               : "outcome_unknown") +
                          "\",\"transaction_hash\":\"" +
                          json_escape(
                              transaction.raw_transaction.transaction_hash) +
                          '"';
                      if (transaction.receipt) {
                        if (transaction.receipt->block_number)
                          fields +=
                              ",\"block_number\":\"" +
                              transaction.receipt->block_number->to_string() +
                              '"';
                        if (transaction.receipt->status)
                          fields += ",\"receipt_status\":\"" +
                                    transaction.receipt->status->to_string() +
                                    '"';
                        if (transaction.receipt->gas_used)
                          fields += ",\"gas_used\":\"" +
                                    transaction.receipt->gas_used->to_string() +
                                    '"';
                      }
                      evidence->write("transaction_result", fields);
                    }
                    if (!report.value().success) {
                      std::cerr << "approval run did not confirm every step; "
                                   "no retry was attempted\n";
                      if (evidence)
                        evidence->write("approve_complete",
                                        "\"result\":\"not_confirmed\","
                                        "\"blind_retry_performed\":false");
                      return;
                    }
                    read_snapshot(
                        client, options, planned.value(),
                        [&, signer_holder](Result<Snapshot> after) {
                          if (!after) {
                            print_error("postflight snapshot", after.error());
                            if (evidence)
                              evidence->write("postflight_failed",
                                              error_fields(after.error()));
                            return;
                          }
                          record_snapshot(evidence ? &*evidence : nullptr,
                                          after.value(), "after");
                          const bool all_satisfied =
                              std::all_of(after.value().approvals.begin(),
                                          after.value().approvals.end(),
                                          [](const ApprovalCheck &check) {
                                            return check.satisfied;
                                          });
                          if (evidence)
                            evidence->write(
                                "approve_complete",
                                "\"result\":\"" +
                                    std::string{all_satisfied
                                                    ? "confirmed"
                                                    : "postcheck_failed"} +
                                    "\",\"blind_retry_performed\":false");
                          if (all_satisfied)
                            exit_code = EXIT_SUCCESS;
                        });
                  });
            });
      });
  io.run();
  if (evidence)
    std::cout << "evidence: " << evidence->path().string() << '\n';
  return exit_code;
}

} // namespace

int main(int argc, char **argv) {
  std::vector<std::string_view> arguments;
  arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
  for (int index = 1; index < argc; ++index)
    arguments.emplace_back(argv[index]);
  auto parsed =
      predictfun::tools::parse_testnet_acceptance_arguments(arguments);
  if (!parsed) {
    print_error("arguments", parsed.error());
    std::cerr << '\n' << predictfun::tools::testnet_acceptance_usage() << '\n';
    return EXIT_FAILURE;
  }
  if (parsed.value().help || arguments.empty()) {
    std::cout << predictfun::tools::testnet_acceptance_usage() << '\n';
    return EXIT_SUCCESS;
  }
  if (parsed.value().mode ==
      predictfun::tools::TestnetAcceptanceMode::approve) {
    auto gate = predictfun::tools::validate_testnet_write_gate(parsed.value());
    if (!gate) {
      print_error("write gate", gate.error());
      return EXIT_FAILURE;
    }
  } else if (parsed.value().mode ==
             predictfun::tools::TestnetAcceptanceMode::position_execute) {
    auto gate =
        predictfun::tools::validate_testnet_position_write_gate(parsed.value());
    if (!gate) {
      print_error("position write gate", gate.error());
      return EXIT_FAILURE;
    }
  }
  return run(parsed.value());
}
