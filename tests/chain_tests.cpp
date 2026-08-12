#include "predictfun/chain/abi.hpp"
#include "predictfun/chain/approvals.hpp"
#include "predictfun/chain/client.hpp"
#include "predictfun/chain/operations.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>

#include <deque>
#include <iostream>
#include <utility>
#include <vector>

namespace {

int failures = 0;
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": CHECK failed: " #condition << '\n';                     \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

using predictfun::Error;
using predictfun::ErrorCode;
using predictfun::Result;

class ScriptedTransport final : public predictfun::net::HttpTransport {
public:
  explicit ScriptedTransport(boost::asio::any_io_executor executor)
      : executor_(std::move(executor)) {}

  void push(predictfun::net::HttpResponse response) {
    responses_.emplace_back(std::move(response));
  }

  void async_request(predictfun::net::HttpRequest request,
                     predictfun::net::RequestContext,
                     predictfun::net::ResponseHandler handler) override {
    requests.push_back(std::move(request));
    auto response = std::move(responses_.front());
    responses_.pop_front();
    boost::asio::dispatch(executor_,
                          [response = std::move(response),
                           handler = std::move(handler)]() mutable {
                            handler(std::move(response));
                          });
  }

  std::vector<predictfun::net::HttpRequest> requests;

private:
  boost::asio::any_io_executor executor_;
  std::deque<Result<predictfun::net::HttpResponse>> responses_;
};

predictfun::net::HttpResponse response(std::string body) {
  return predictfun::net::HttpResponse{200, std::move(body), {}};
}

predictfun::EvmAddress address(const char *value) {
  return predictfun::EvmAddress::parse(value).value();
}

void test_abi() {
  using namespace predictfun::chain::abi;
  const auto owner = address("0x1111111111111111111111111111111111111111");
  const auto spender = address("0x2222222222222222222222222222222222222222");
  const auto balance = erc20_balance_of(owner);
  const auto allowance = erc20_allowance(owner, spender);
  const auto approved = erc1155_is_approved_for_all(owner, spender);
  CHECK(balance && balance.value().starts_with("0x70a08231"));
  CHECK(allowance && allowance.value().starts_with("0xdd62ed3e"));
  CHECK(approved && approved.value().starts_with("0xe985e9c5"));
  CHECK(decode_quantity("0x38").value().to_string() == "56");
  CHECK(!decode_quantity("0x038"));
  CHECK(decode_word_bool(
      "0x0000000000000000000000000000000000000000000000000000000000000001")
            .value());
}

void test_approval_plan() {
  using namespace predictfun;
  using namespace predictfun::chain;
  auto buy = approval_steps(
      ChainId::bnb_mainnet,
      ApprovalScope{ApprovalOperation::trade, false, false,
                    ApprovalTradeSide::buy});
  CHECK(buy && buy.value().size() == 1U);
  CHECK(buy.value()[0].kind == ApprovalKind::erc20_allowance);

  auto neg_sell = approval_steps(
      ChainId::bnb_mainnet,
      ApprovalScope{ApprovalOperation::trade, true, false,
                    ApprovalTradeSide::sell});
  CHECK(neg_sell && neg_sell.value().size() == 2U);
  CHECK(neg_sell.value()[0].kind == ApprovalKind::erc1155_operator);
  CHECK(neg_sell.value()[1].kind == ApprovalKind::erc1155_operator);

  auto split = approval_steps(
      ChainId::bnb_testnet,
      ApprovalScope{ApprovalOperation::split, false, true,
                    ApprovalTradeSide::both});
  CHECK(split && split.value().size() == 1U);
  auto tx = approval_transaction(split.value()[0]);
  CHECK(tx && tx.value().data.starts_with("0x095ea7b3"));
  CHECK(!approval_steps(
      ChainId::bnb_mainnet,
      ApprovalScope{ApprovalOperation::convert, false, false,
                    ApprovalTradeSide::both}));
}

void test_position_operations() {
  using namespace predictfun;
  using namespace predictfun::chain;
  Bytes32 condition{};
  condition.back() = 0x42U;
  auto amount = Uint256::parse("1000000").value();
  auto split = split_transaction(
      ChainId::bnb_testnet,
      PositionOperation{condition, amount, false, false});
  CHECK(split);
  CHECK(split && split.value().data.starts_with("0x72ce4275"));
  CHECK(split && split.value().data.size() == 2U + (4U + 8U * 32U) * 2U);
  CHECK(split && split.value().data ==
      "0x72ce4275000000000000000000000000b32171ecd878607ffc4f8fc0bcce6852bb3149e00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000004200000000000000000000000000000000000000000000000000000000000000a000000000000000000000000000000000000000000000000000000000000f4240000000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000000000000002");

  auto neg_merge = merge_transaction(
      ChainId::bnb_testnet,
      PositionOperation{condition, amount, true, true});
  CHECK(neg_merge);
  CHECK(neg_merge && neg_merge.value().data.starts_with("0xb10c5c17"));

  auto redeem = redeem_transaction(
      ChainId::bnb_testnet,
      RedeemOperation{condition, 2U, true, false, amount});
  CHECK(redeem);
  CHECK(redeem && redeem.value().data.starts_with("0xdbeccb23"));
  CHECK(redeem && redeem.value().data ==
      "0xdbeccb23000000000000000000000000000000000000000000000000000000000000004200000000000000000000000000000000000000000000000000000000000000400000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f4240");

  Bytes32 market{};
  market.front() = 0x11U;
  auto convert = convert_transaction(
      ChainId::bnb_mainnet,
      ConvertOperation{market, Uint256::parse("3").value(), amount, false});
  CHECK(convert);
  CHECK(convert && convert.value().data.starts_with("0xc64748c4"));

  auto account = address("0x1111111111111111111111111111111111111111");
  auto routed = split_transaction(
      ChainId::bnb_testnet,
      PositionOperation{condition, amount, false, false},
      RouteOptions{TransactionRoute::predict_account, account});
  CHECK(routed);
  CHECK(routed && routed.value().to == account);
  CHECK(routed && routed.value().data.starts_with("0xe9ae5c53"));
  CHECK(!split_transaction(
      ChainId::bnb_testnet,
      PositionOperation{condition, amount, false, false},
      RouteOptions{TransactionRoute::predict_account, std::nullopt}));
}

void test_cancel_operations() {
  using namespace predictfun;
  using namespace predictfun::chain;
  SignedOrder order;
  order.salt = Uint256::parse("1").value();
  order.maker = address("0x1111111111111111111111111111111111111111");
  order.signer = order.maker;
  order.token_id = Uint256::parse("2").value();
  order.maker_amount = Uint256::parse("3").value();
  order.taker_amount = Uint256::parse("4").value();
  order.expiration = Uint256::parse("5").value();
  order.nonce = Uint256::parse("6").value();
  order.fee_rate_bps = Uint256::parse("7").value();
  order.side = ContractSide::buy;
  order.signature_type = SignatureType::eoa;
  order.signature = "0x010203";
  const std::array orders{order};
  auto cancel = cancel_orders_transaction(
      ChainId::bnb_testnet, orders, MarketContractKind{false, false});
  CHECK(cancel && cancel.value().has_value());
  CHECK(cancel && cancel.value() &&
        cancel.value()->data.starts_with("0xfa950b48"));
  CHECK(cancel && cancel.value() && cancel.value()->data ==
      "0xfa950b4800000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000010000000000000000000000001111111111111111111111111111111111111111000000000000000000000000111111111111111111111111111111111111111100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000300000000000000000000000000000000000000000000000000000000000000040000000000000000000000000000000000000000000000000000000000000005000000000000000000000000000000000000000000000000000000000000000600000000000000000000000000000000000000000000000000000000000000070000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001a000000000000000000000000000000000000000000000000000000000000000030102030000000000000000000000000000000000000000000000000000000000");
  CHECK(cancel && cancel.value() &&
        cancel.value()->to ==
            protocol_addresses(ChainId::bnb_testnet).value().ctf_exchange);
  const std::span<const SignedOrder> none;
  auto empty = cancel_orders_transaction(
      ChainId::bnb_testnet, none, MarketContractKind{});
  CHECK(empty && !empty.value());
}

void test_chain_client() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(R"({"jsonrpc":"2.0","id":1,"result":"0x38"})"));
  transport->push(response(R"({"jsonrpc":"2.0","id":2,"result":"0x000000000000000000000000000000000000000000000000000000000000002a"})"));
  predictfun::chain::ClientOptions options;
  options.expected_chain_id = predictfun::ChainId::bnb_mainnet;
  options.endpoint = predictfun::RpcEndpoint{"rpc.example", "443", "/rpc", true};
  predictfun::chain::ChainClient client(io.get_executor(), transport, options);
  bool called = false;
  client.async_erc20_balance(
      address("0x3333333333333333333333333333333333333333"),
      address("0x1111111111111111111111111111111111111111"),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&](Result<predictfun::Uint256> result) {
        called = true;
        CHECK(result && result.value().to_string() == "42");
      });
  io.run();
  CHECK(called);
  CHECK(transport->requests.size() == 2U);
  CHECK(transport->requests[0].body.find("eth_chainId") != std::string::npos);
  CHECK(transport->requests[1].body.find("eth_call") != std::string::npos);
  CHECK(transport->requests[1].body.find("70a08231") != std::string::npos);
}

void test_wrong_chain() {
  boost::asio::io_context io;
  auto transport = std::make_shared<ScriptedTransport>(io.get_executor());
  transport->push(response(R"({"jsonrpc":"2.0","id":1,"result":"0x61"})"));
  predictfun::chain::ClientOptions options;
  options.expected_chain_id = predictfun::ChainId::bnb_mainnet;
  options.endpoint = predictfun::RpcEndpoint{"rpc.example", "443", "/", true};
  predictfun::chain::ChainClient client(io.get_executor(), transport, options);
  bool called = false;
  client.async_erc20_balance(
      address("0x3333333333333333333333333333333333333333"),
      address("0x1111111111111111111111111111111111111111"),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{1}),
      [&](Result<predictfun::Uint256> result) {
        called = true;
        CHECK(!result && result.error().code == ErrorCode::unsupported_chain);
      });
  io.run();
  CHECK(called);
  CHECK(transport->requests.size() == 1U);
}

} // namespace

int main() {
  test_abi();
  test_approval_plan();
  test_position_operations();
  test_cancel_operations();
  test_chain_client();
  test_wrong_chain();
  if (failures != 0) {
    std::cerr << failures << " chain test(s) failed\n";
    return 1;
  }
  std::cout << "chain tests passed\n";
  return 0;
}
