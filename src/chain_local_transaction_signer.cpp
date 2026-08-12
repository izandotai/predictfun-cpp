#include "predictfun/chain/local_transaction_signer.hpp"

namespace predictfun::chain {

Result<RawTransaction>
sign_legacy_transaction(const PopulatedTransaction &transaction,
                        const order::LocalSigner &signer) {
  if (transaction.from != signer.address())
    return Error{ErrorCode::invalid_argument,
                 "transaction sender does not match local signer",
                 "transaction.from"};
  return sign_legacy_transaction(
      transaction, [&signer](const Hash32 &digest) {
        return signer.sign_digest(digest);
      });
}

} // namespace predictfun::chain
