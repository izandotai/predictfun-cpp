#include "predictfun/order/local_signer.hpp"

int main() {
  auto signer = predictfun::order::LocalSigner::create(
      predictfun::SecretString{
          "0000000000000000000000000000000000000000000000000000000000000001"});
  return signer ? 0 : 1;
}
