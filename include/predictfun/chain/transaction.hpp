#pragma once

#include "predictfun/order/builder.hpp"
#include "predictfun/types/chain.hpp"

#include <functional>
#include <string>

namespace predictfun::chain {

using TransactionDigestSigner =
    std::function<Result<std::string>(const Hash32 &digest)>;

[[nodiscard]] Result<Hash32>
legacy_transaction_signing_digest(const PopulatedTransaction &transaction);

// The signer returns a canonical 65-byte EVM signature (r || s || 27/28).
// This is suitable for external wallets, HSMs and caller-owned key stores.
[[nodiscard]] Result<RawTransaction>
sign_legacy_transaction(const PopulatedTransaction &transaction,
                        const TransactionDigestSigner &signer);

[[nodiscard]] Result<std::string>
raw_transaction_hash(std::string_view raw_transaction);

} // namespace predictfun::chain
