#include "predictfun/order/builder.hpp"

#include <chrono>
#include <random>

namespace predictfun::order {
namespace {

Result<Uint256> default_salt() {
  std::random_device source;
  std::uniform_int_distribution<std::uint32_t> distribution{0U, 2'147'483'648U};
  return Uint256::parse(std::to_string(distribution(source)));
}

std::uint64_t default_clock() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

OrderBuilder::OrderBuilder(BuilderOptions options) : options_(std::move(options)) {
  if (!options_.salt_provider)
    options_.salt_provider = default_salt;
  if (!options_.clock_provider)
    options_.clock_provider = default_clock;
  auto addresses = protocol_addresses(options_.chain_id);
  if (addresses)
    addresses_ = addresses.value();
}

Result<UnsignedOrder> OrderBuilder::build(const BuildOrderInput &input) const {
  if (input.signer.empty() && !options_.predict_account)
    return Error{ErrorCode::invalid_argument, "order signer is required", "signer"};
  const auto signer = options_.predict_account.value_or(input.signer);
  const auto maker = options_.predict_account.value_or(
      input.maker.value_or(input.signer));
  if (!options_.predict_account && maker != signer)
    return Error{ErrorCode::invalid_argument,
                 "maker and signer must match for an EOA order", "maker"};
  if (input.side != ContractSide::buy && input.side != ContractSide::sell)
    return Error{ErrorCode::invalid_argument, "order side is unknown", "side"};
  if (input.token_id.is_zero() || input.maker_amount.is_zero() ||
      input.taker_amount.is_zero())
    return Error{ErrorCode::invalid_quantity,
                 "token and order amounts must be positive", "amount"};
  auto salt = input.salt ? Result<Uint256>{*input.salt} : options_.salt_provider();
  if (!salt)
    return salt.error();
  const auto now = options_.clock_provider();
  std::uint64_t expiration = 0;
  if (input.strategy == ExecutionStrategy::market) {
    expiration = now + 300U;
  } else {
    expiration = input.expires_at_unix_seconds.value_or(4'102'444'800ULL);
    if (expiration <= now)
      return Error{ErrorCode::invalid_argument,
                   "limit order expiration must be in the future", "expiration"};
  }
  auto expiration_u = Uint256::parse(std::to_string(expiration));
  if (!expiration_u)
    return expiration_u.error();
  return UnsignedOrder{salt.value(),
                       maker,
                       signer,
                       input.taker,
                       input.token_id,
                       input.maker_amount,
                       input.taker_amount,
                       expiration_u.value(),
                       input.nonce,
                       input.fee_rate_bps,
                       input.side,
                       input.signature_type};
}

Result<Hash32> OrderBuilder::digest(const UnsignedOrder &value,
                                    MarketContractKind kind) const {
  if (addresses_.ctf_exchange.empty())
    return Error{ErrorCode::invalid_argument, "unsupported Predict chain id",
                 "chain_id"};
  return typed_data_digest(value, options_.chain_id,
                           exchange_address(addresses_, kind));
}

} // namespace predictfun::order
