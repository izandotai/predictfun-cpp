#include "predictfun/types/order.hpp"

namespace predictfun {
namespace {

Result<EvmAddress> address(const char *value) { return EvmAddress::parse(value); }

Result<ProtocolAddresses> make_addresses(
    const char *yield_exchange, const char *yield_neg_exchange,
    const char *yield_neg_adapter, const char *yield_tokens,
    const char *yield_neg_tokens, const char *exchange,
    const char *neg_exchange, const char *neg_adapter, const char *tokens,
    const char *neg_tokens, const char *usdt, const char *kernel,
    const char *validator) {
  auto a = address(yield_exchange);
  auto b = address(yield_neg_exchange);
  auto c = address(yield_neg_adapter);
  auto d = address(yield_tokens);
  auto e = address(yield_neg_tokens);
  auto f = address(exchange);
  auto g = address(neg_exchange);
  auto h = address(neg_adapter);
  auto i = address(tokens);
  auto j = address(neg_tokens);
  auto k = address(usdt);
  auto l = address(kernel);
  auto m = address(validator);
  if (!a || !b || !c || !d || !e || !f || !g || !h || !i || !j || !k ||
      !l || !m) {
    return Error{ErrorCode::protocol_error,
                 "embedded Predict contract registry is invalid", {}};
  }
  return ProtocolAddresses{a.value(), b.value(), c.value(), d.value(),
                           e.value(), f.value(), g.value(), h.value(),
                           i.value(), j.value(), k.value(), l.value(),
                           m.value()};
}

} // namespace

Result<ProtocolAddresses> protocol_addresses(ChainId chain_id) {
  if (chain_id == ChainId::bnb_mainnet) {
    return make_addresses(
        "0x6bEb5a40C032AFc305961162d8204CDA16DECFa5",
        "0x8A289d458f5a134bA40015085A8F50Ffb681B41d",
        "0x41dCe1A4B8FB5e6327701750aF6231B7CD0B2A40",
        "0x9400F8Ad57e9e0F352345935d6D3175975eb1d9F",
        "0xF64b0b318AAf83BD9071110af24D24445719A07F",
        "0x8BC070BEdAB741406F4B1Eb65A72bee27894B689",
        "0x365fb81bd4A24D6303cd2F19c349dE6894D8d58A",
        "0xc3Cf7c252f65E0d8D88537dF96569AE94a7F1A6E",
        "0x22DA1810B194ca018378464a58f6Ac2B10C9d244",
        "0x22DA1810B194ca018378464a58f6Ac2B10C9d244",
        "0x55d398326f99059fF775485246999027B3197955",
        "0xBAC849bB641841b44E965fB01A4Bf5F074f84b4D",
        "0x845ADb2C711129d4f3966735eD98a9F09fC4cE57");
  }
  if (chain_id == ChainId::bnb_testnet) {
    return make_addresses(
        "0x8a6B4Fa700A1e310b106E7a48bAFa29111f66e89",
        "0x95D5113bc50eD201e319101bbca3e0E250662fCC",
        "0xb74aea04bdeBE912Aa425bC9173F9668e6f11F99",
        "0x38BF1cbD66d174bb5F3037d7068E708861D68D7f",
        "0x26e865CbaAe99b62fbF9D18B55c25B5E079A93D5",
        "0x2A6413639BD3d73a20ed8C95F634Ce198ABbd2d7",
        "0xd690b2bd441bE36431F6F6639D7Ad351e7B29680",
        "0x285c1B939380B130D7EBd09467b93faD4BA623Ed",
        "0x2827AAef52D71910E8FBad2FfeBC1B6C2DA37743",
        "0x2827AAef52D71910E8FBad2FfeBC1B6C2DA37743",
        "0xB32171ecD878607FFc4F8FC0bCcE6852BB3149E0",
        "0xBAC849bB641841b44E965fB01A4Bf5F074f84b4D",
        "0x845ADb2C711129d4f3966735eD98a9F09fC4cE57");
  }
  return Error{ErrorCode::invalid_argument, "unsupported Predict chain id",
               "chain_id"};
}

EvmAddress exchange_address(const ProtocolAddresses &addresses,
                            MarketContractKind kind) noexcept {
  if (kind.is_yield_bearing)
    return kind.is_neg_risk ? addresses.yield_bearing_neg_risk_ctf_exchange
                            : addresses.yield_bearing_ctf_exchange;
  return kind.is_neg_risk ? addresses.neg_risk_ctf_exchange
                          : addresses.ctf_exchange;
}

} // namespace predictfun
