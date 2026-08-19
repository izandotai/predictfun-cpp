#include "predictfun/order/local_signer.hpp"

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

Hash32 hash(std::string_view value) {
  Hash32 result{};
  auto nibble = [](char ch) {
    if (ch >= '0' && ch <= '9')
      return ch - '0';
    if (ch >= 'a' && ch <= 'f')
      return ch - 'a' + 10;
    return -1;
  };
  if (value.size() != 66U || value.substr(0, 2) != "0x")
    std::exit(2);
  for (std::size_t index = 0; index < result.size(); ++index) {
    const auto high = nibble(value[2U + index * 2U]);
    const auto low = nibble(value[3U + index * 2U]);
    if (high < 0 || low < 0)
      std::exit(2);
    result[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return result;
}

} // namespace

int main() {
  auto signer = predictfun::order::LocalSigner::create(
      SecretString{"0x0000000000000000000000000000000000000000000000000000000000000001"});
  check(signer && signer.value().address().to_string() ==
                      "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf",
        "fixture private key derives the expected EVM address");
  if (signer) {
    const auto order_digest = hash(
        "0xf4fb49f1a37c5f2b370008929b40d065b2cf21ed6822d5d8eca32073905ecb6d");
    auto direct = signer.value().sign_digest(order_digest);
    check(direct && direct.value() ==
                        "0x8eca05786bc562a6833e0658fbd824498ada3beba34fc3bf63b8dd4d04dc6048"
                        "67f70fa27072a56cea9a2a37d00df47dc5c780b867e51b615c8326cf37ef08461b",
          "recoverable EOA signature matches ethers golden vector");

    const auto kernel_digest = hash(
        "0x90191bf5f56668a500b04b747df6c0c9dc997845baa360d717bc6c71fce30d96");
    auto personal = signer.value().sign_personal_message_32(kernel_digest);
    check(personal && personal.value() ==
                          "0x8a44003b1358f0063ba448411489fcf891f736de2d286aeea0cb22f32863ec9c"
                          "3be689469bed6df2d09fc06eaaa2fa91abd55583e0395f5003a721fec6a9a73a1b",
          "Predict Account personal signature matches ethers golden vector");

    auto generic_personal = signer.value().sign_personal_message(
        std::string_view{reinterpret_cast<const char *>(kernel_digest.data()),
                         kernel_digest.size()});
    check(generic_personal && personal && generic_personal.value() == personal.value(),
          "generic EIP-191 signer preserves the 32-byte golden vector");

    auto challenge = signer.value().sign_personal_message("dynamic challenge");
    auto repeated = signer.value().sign_personal_message("dynamic challenge");
    check(challenge && repeated && challenge.value() == repeated.value() &&
              challenge.value().size() == 132U &&
              challenge.value().starts_with("0x"),
          "arbitrary authentication challenge is signed deterministically");
  }

  auto malformed = predictfun::order::LocalSigner::create(SecretString{"bad"});
  check(!malformed && malformed.error().field == "private_key" &&
            malformed.error().message.find("bad") == std::string::npos,
        "invalid key diagnostics never include secret material");

  auto zero = predictfun::order::LocalSigner::create(
      SecretString{"0000000000000000000000000000000000000000000000000000000000000000"});
  check(!zero && zero.error().field == "private_key",
        "zero secp256k1 scalar is rejected");
  auto overflow = predictfun::order::LocalSigner::create(
      SecretString{"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"});
  check(!overflow && overflow.error().field == "private_key",
        "out-of-range secp256k1 scalar is rejected");

  if (failures != 0)
    return 1;
  std::cout << "local signer vectors passed\n";
  return 0;
}
