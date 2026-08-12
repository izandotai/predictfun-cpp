#pragma once

#include "predictfun/types/evm.hpp"
#include "predictfun/types/secret.hpp"

#include <string>

namespace predictfun {

struct AuthMessage {
  std::string message;
};

struct AuthProof {
  EvmAddress signer;
  std::string signature;
  std::string message;
};

class WalletJwt {
public:
  WalletJwt() = default;
  explicit WalletJwt(std::string token);

  WalletJwt(const WalletJwt &) = delete;
  WalletJwt &operator=(const WalletJwt &) = delete;
  WalletJwt(WalletJwt &&) noexcept = default;
  WalletJwt &operator=(WalletJwt &&) noexcept = default;

  [[nodiscard]] std::string_view view() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

private:
  SecretString token_;
};

} // namespace predictfun
