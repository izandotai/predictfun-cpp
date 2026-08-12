#pragma once

#include "predictfun/types/auth.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace predictfun::codec {

struct AuthCodecLimits {
  std::size_t max_body_bytes{64U * 1024U};
  std::size_t max_message_bytes{16U * 1024U};
  std::size_t max_signature_bytes{2U * 1024U};
  std::size_t max_token_bytes{32U * 1024U};
};

[[nodiscard]] Result<AuthMessage>
decode_auth_message_response(std::string_view json,
                             const AuthCodecLimits &limits = {});

[[nodiscard]] Result<WalletJwt>
decode_auth_token_response(std::string_view json,
                           const AuthCodecLimits &limits = {});

[[nodiscard]] Result<std::string>
encode_auth_proof(const AuthProof &proof,
                  const AuthCodecLimits &limits = {});

} // namespace predictfun::codec
