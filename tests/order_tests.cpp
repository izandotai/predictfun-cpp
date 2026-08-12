#include "predictfun/order/amounts.hpp"
#include "predictfun/order/builder.hpp"
#include "predictfun/order/signature.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using namespace predictfun;

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

Uint256 uint(std::string_view value) {
  auto result = Uint256::parse(value);
  if (!result) {
    std::cerr << "fixture uint parse failed\n";
    std::exit(2);
  }
  return result.value();
}

Uint256 wei(std::string_view value) {
  auto result = predictfun::order::decimal_to_wei(value);
  if (!result) {
    std::cerr << "fixture decimal parse failed: " << value << '\n';
    std::exit(2);
  }
  return result.value();
}

EvmAddress addr(std::string_view value) {
  auto result = EvmAddress::parse(value);
  if (!result) {
    std::cerr << "fixture address parse failed\n";
    std::exit(2);
  }
  return result.value();
}

Hash32 hash(std::string_view value) {
  if (value.size() != 66U || value.substr(0, 2) != "0x") {
    std::cerr << "fixture hash parse failed\n";
    std::exit(2);
  }
  Hash32 result{};
  auto nibble = [](char ch) -> int {
    if (ch >= '0' && ch <= '9')
      return ch - '0';
    if (ch >= 'a' && ch <= 'f')
      return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
      return ch - 'A' + 10;
    return -1;
  };
  for (std::size_t index = 0; index < result.size(); ++index) {
    const auto high = nibble(value[2U + index * 2U]);
    const auto low = nibble(value[3U + index * 2U]);
    if (high < 0 || low < 0) {
      std::cerr << "fixture hash parse failed\n";
      std::exit(2);
    }
    result[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return result;
}

void amount_vectors() {
  auto buy = predictfun::order::limit_amounts(
      {ContractSide::buy, wei("2"), wei("5")});
  check(buy && buy.value().maker_amount == wei("10") &&
            buy.value().taker_amount == wei("5"),
        "official limit BUY vector");

  auto rounded = predictfun::order::limit_amounts(
      {ContractSide::buy, uint("381000000000000000"),
       uint("18001999999999999475712")});
  check(rounded && rounded.value().maker_amount ==
                       uint("6858381000000000000000") &&
            rounded.value().taker_amount == uint("18001000000000000000000"),
        "official significant-digit vector");

  OrderDepth book{{{wei("0.5"), wei("3")}, {wei("0.88"), wei("4")}},
                  {{wei("0.9"), wei("2")}, {wei("0.5"), wei("3")}}};
  auto market_buy = predictfun::order::market_amounts(
      {ContractSide::buy, wei("5"), uint("0"), false}, book);
  check(market_buy && market_buy.value().price_per_share_wei == wei("0.652") &&
            market_buy.value().maker_amount == wei("4.4") &&
            market_buy.value().taker_amount == wei("5"),
        "official market BUY depth vector");
  auto market_sell = predictfun::order::market_amounts(
      {ContractSide::sell, wei("5"), uint("0"), false}, book);
  check(market_sell && market_sell.value().price_per_share_wei == wei("0.66") &&
            market_sell.value().maker_amount == wei("5") &&
            market_sell.value().taker_amount == wei("2.5"),
        "official market SELL depth vector");

  OrderDepth value_book{{{wei("0.25"), wei("2")},
                          {wei("0.75"), wei("2")}}, {}};
  auto by_value = predictfun::order::market_amounts(
      MarketValueInput{wei("2"), uint("0"), false}, value_book);
  check(by_value && by_value.value().maker_amount == wei("3") &&
            by_value.value().taker_amount == wei("4") &&
            by_value.value().price_per_share_wei == wei("0.5"),
        "official market BUY-by-value vector");

  OrderDepth slippage{{{wei("0.27"), wei("100")},
                         {wei("0.3"), wei("200")}},
                        {{wei("0.27"), wei("100")},
                         {wei("0.25"), wei("200")}}};
  auto buffered = predictfun::order::market_amounts(
      {ContractSide::buy, wei("100"), uint("500"), false}, slippage);
  check(buffered && buffered.value().maker_amount == wei("28.35"),
        "BUY slippage inflates collateral");
  auto min_out = predictfun::order::market_amounts(
      {ContractSide::buy, wei("100"), uint("500"), true}, slippage);
  check(min_out && min_out.value().maker_amount == wei("27") &&
            min_out.value().taker_amount == wei("95"),
        "BUY min-out slippage deflates shares");
}

void hashing_and_builder_vectors() {
  auto empty_hash = predictfun::order::keccak256(std::string_view{});
  auto abc_hash = predictfun::order::keccak256("abc");
  check(empty_hash && predictfun::order::to_hex(empty_hash.value()) ==
                          "0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470",
        "Ethereum Keccak-256 empty vector");
  check(abc_hash && predictfun::order::to_hex(abc_hash.value()) ==
                        "0x4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45",
        "Ethereum Keccak-256 abc vector");

  predictfun::order::OrderBuilder builder({
      ChainId::bnb_mainnet,
      [] { return Result<Uint256>{uint("1234")}; },
      [] { return std::uint64_t{2'000'000'000}; },
      {}});
  const auto signer = addr("0x1111111111111111111111111111111111111111");
  auto order = builder.build({ExecutionStrategy::limit,
                              ContractSide::buy,
                              uint("123"),
                              wei("10"),
                              wei("5"),
                              uint("0"),
                              signer,
                              {},
                              {},
                              uint("1"),
                              {},
                              SignatureType::eoa,
                              {}});
  check(order && order.value().expiration == uint("4102444800") &&
            order.value().salt == uint("1234") && !order.value().taker,
        "deterministic limit builder");
  auto market = builder.build({ExecutionStrategy::market,
                               ContractSide::sell,
                               uint("456"),
                               wei("5"),
                               wei("10"),
                               uint("0"),
                               signer,
                               {},
                               {},
                               uint("0"),
                               {},
                               SignatureType::eoa,
                               {}});
  check(market && market.value().expiration == uint("2000000300"),
        "market order uses five-minute expiry");

  auto addresses = protocol_addresses(ChainId::bnb_mainnet);
  check(addresses &&
            exchange_address(addresses.value(), {false, false}).to_string() ==
                "0x8bc070bedab741406f4b1eb65a72bee27894b689" &&
            exchange_address(addresses.value(), {true, true}).to_string() ==
                "0x8a289d458f5a134ba40015085a8f50ffb681b41d",
        "official mainnet exchange registry");
  if (order) {
    auto structure = predictfun::order::order_struct_hash(order.value());
    auto digest = builder.digest(order.value(), {false, false});
    auto neg_yield = builder.digest(order.value(), {true, true});
    check(structure && predictfun::order::to_hex(structure.value()) ==
                           "0x205254cb93e72001661e5676325182ece1e3f02d52508030424119c858dd7b85",
          "official SDK EIP-712 struct golden vector");
    check(digest && predictfun::order::to_hex(digest.value()) ==
                        "0xf4fb49f1a37c5f2b370008929b40d065b2cf21ed6822d5d8eca32073905ecb6d",
          "official SDK mainnet standard digest golden vector");
    check(neg_yield && predictfun::order::to_hex(neg_yield.value()) ==
                           "0x0653077a8cf8610614c725948651150194c51cd41f3f3f25023af0f310118edf",
          "official SDK mainnet neg-risk yield digest golden vector");
  }

  predictfun::order::OrderBuilder testnet({
      ChainId::bnb_testnet,
      [] { return Result<Uint256>{uint("1234")}; },
      [] { return std::uint64_t{2'000'000'000}; },
      {}});
  if (order) {
    auto digest = testnet.digest(order.value(), {false, false});
    check(digest && predictfun::order::to_hex(digest.value()) ==
                        "0xfb624376b5a6a88e73295cf7489374fad4787a3ac4ca86870a24d1f5668f1734",
          "official SDK testnet standard digest golden vector");
  }
}

void signature_vectors() {
  const auto order_digest =
      hash("0xf4fb49f1a37c5f2b370008929b40d065b2cf21ed6822d5d8eca32073905ecb6d");
  const auto account = addr("0x2222222222222222222222222222222222222222");
  auto kernel_digest = predictfun::order::predict_account_signing_digest(
      order_digest, ChainId::bnb_mainnet, account);
  check(kernel_digest && predictfun::order::to_hex(kernel_digest.value()) ==
                             "0x90191bf5f56668a500b04b747df6c0c9dc997845baa360d717bc6c71fce30d96",
        "official SDK Predict Account Kernel digest golden vector");

  const std::string dummy_signature = "0x" + std::string(128U, 'a') + "1b";
  const auto validator = addr("0x845adb2c711129d4f3966735ed98a9f09fc4ce57");
  auto envelope = predictfun::order::predict_account_signature_envelope(
      validator, dummy_signature);
  check(envelope && envelope.value() ==
                        "0x01845adb2c711129d4f3966735ed98a9f09fc4ce57" +
                            dummy_signature.substr(2),
        "Predict Account signature envelope");
  check(!predictfun::order::validate_evm_signature("0x1234"),
        "short EVM signature is rejected");

  predictfun::order::OrderBuilder builder({
      ChainId::bnb_mainnet,
      [] { return Result<Uint256>{uint("1234")}; },
      [] { return std::uint64_t{2'000'000'000}; },
      account});
  auto unsigned_order = builder.build({ExecutionStrategy::limit,
                                       ContractSide::buy,
                                       uint("123"),
                                       wei("1"),
                                       wei("2"),
                                       uint("0"),
                                       {},
                                       {},
                                       {},
                                       uint("0"),
                                       {},
                                       SignatureType::eoa,
                                       {}});
  check(unsigned_order && unsigned_order.value().maker == account &&
            unsigned_order.value().signer == account,
        "Predict Account builder assigns maker and signer");
  bool callback_received_golden = false;
  if (unsigned_order) {
    auto signed_order = predictfun::order::sign_predict_account_order(
        builder, unsigned_order.value(), {false, false}, account, validator,
        [&](const Hash32 &digest) -> Result<std::string> {
          callback_received_golden = digest.size() == 32U;
          return dummy_signature;
        });
    check(signed_order && signed_order.value().signature.rfind("0x01", 0) == 0,
          "Predict Account external signing flow");
    check(callback_received_golden,
          "Predict Account signer receives a bounded 32-byte digest");
  }
}

} // namespace

int main() {
  amount_vectors();
  hashing_and_builder_vectors();
  signature_vectors();
  if (failures != 0)
    return 1;
  std::cout << "order vectors passed\n";
  return 0;
}
