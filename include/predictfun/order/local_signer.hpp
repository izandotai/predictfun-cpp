#pragma once

#include "predictfun/order/signature.hpp"
#include "predictfun/types/secret.hpp"

#include <memory>
#include <string>

namespace predictfun::order {

// Optional, separately linked local signer. It accepts a key only through an
// explicit move-only in-memory value; it never reads files or environment
// variables and never includes the key in an Error.
class LocalSigner {
public:
  [[nodiscard]] static Result<LocalSigner> create(SecretString private_key);

  ~LocalSigner();
  LocalSigner(const LocalSigner &) = delete;
  LocalSigner &operator=(const LocalSigner &) = delete;
  LocalSigner(LocalSigner &&) noexcept;
  LocalSigner &operator=(LocalSigner &&) noexcept;

  [[nodiscard]] const EvmAddress &address() const noexcept;
  [[nodiscard]] Result<std::string>
  sign_digest(const Hash32 &digest) const;
  [[nodiscard]] Result<std::string>
  sign_personal_message_32(const Hash32 &message) const;

private:
  struct Impl;
  explicit LocalSigner(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

} // namespace predictfun::order
