#pragma once

#include "predictfun/chain/transaction.hpp"
#include "predictfun/order/local_signer.hpp"

namespace predictfun::chain {

// Optional adapter. Linking predictfun::chain never links local key custody;
// callers opt into this target explicitly.
[[nodiscard]] Result<RawTransaction>
sign_legacy_transaction(const PopulatedTransaction &transaction,
                        const order::LocalSigner &signer);

} // namespace predictfun::chain
