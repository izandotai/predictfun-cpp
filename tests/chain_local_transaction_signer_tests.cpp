#include "predictfun/chain/local_transaction_signer.hpp"

#include <iostream>

int main() {
  using namespace predictfun;
  auto signer = order::LocalSigner::create(SecretString{
      "0x0000000000000000000000000000000000000000000000000000000000000001"});
  if (!signer) return 1;
  PopulatedTransaction transaction{
      ChainId::bnb_testnet, signer.value().address(),
      EvmAddress::parse("0x1111111111111111111111111111111111111111").value(),
      Uint256::parse("7").value(), Uint256::parse("3000000000").value(),
      Uint256::parse("52500").value(), Uint256::parse("12345").value(),
      "0x12345678"};
  auto signed_transaction =
      chain::sign_legacy_transaction(transaction, signer.value());
  if (!signed_transaction ||
      signed_transaction.value().transaction_hash !=
          "0xc65b76f46f76e6b2bc50848eff6ec36d96799e033c8c2486ebff9762603016f5")
    return 1;
  transaction.from =
      EvmAddress::parse("0x2222222222222222222222222222222222222222").value();
  if (chain::sign_legacy_transaction(transaction, signer.value())) return 1;
  std::cout << "local transaction signer vectors passed\n";
  return 0;
}
