#pragma once

#include "predictfun/order/builder.hpp"

#include <functional>
#include <string>

namespace predictfun::order {

using DigestSignatureProvider =
    std::function<Result<std::string>(const Hash32 &digest)>;

[[nodiscard]] Result<std::string>
validate_evm_signature(std::string signature);
[[nodiscard]] Result<Hash32>
predict_account_signing_digest(const Hash32 &message_hash, ChainId chain_id,
                               const EvmAddress &predict_account);
[[nodiscard]] Result<std::string>
predict_account_signature_envelope(const EvmAddress &validator,
                                   std::string owner_signature);

[[nodiscard]] Result<SignedOrder>
sign_eoa_order(const OrderBuilder &builder, const UnsignedOrder &order,
               MarketContractKind kind,
               const DigestSignatureProvider &signer);

// `sign_personal_digest` receives the Kernel digest. The provider must apply
// Ethereum personal-sign semantics to those 32 raw bytes, matching
// ethers Signer::signMessage(Uint8Array).
[[nodiscard]] Result<SignedOrder> sign_predict_account_order(
    const OrderBuilder &builder, const UnsignedOrder &order,
    MarketContractKind kind, const EvmAddress &predict_account,
    const EvmAddress &validator,
    const DigestSignatureProvider &sign_personal_digest);

} // namespace predictfun::order
