#include "predictfun/codec/auth.hpp"

#include <glaze/glaze.hpp>

#include <optional>
#include <string>
#include <utility>

namespace predictfun::codec {
namespace {

struct WireAuthMessageData {
  std::optional<std::string> message;
};

struct WireAuthMessageResponse {
  std::optional<bool> success;
  std::optional<WireAuthMessageData> data;
};

struct WireAuthTokenData {
  std::optional<std::string> token;
};

struct WireAuthTokenResponse {
  std::optional<bool> success;
  std::optional<WireAuthTokenData> data;
};

struct WireAuthProof {
  std::string signer;
  std::string signature;
  std::string message;
};

constexpr auto read_options = glz::opts{.error_on_unknown_keys = false};

template <class Wire>
Result<Wire> parse(std::string_view json, const AuthCodecLimits &limits) {
  if (json.size() > limits.max_body_bytes) {
    return Error{ErrorCode::body_too_large,
                 "authentication response exceeds configured limit", {}};
  }
  Wire wire;
  if (glz::read<read_options>(wire, json)) {
    // Never attach the input or Glaze's source excerpt: auth responses can
    // contain bearer tokens.
    return Error{ErrorCode::malformed_json,
                 "authentication response is malformed JSON", {}};
  }
  return wire;
}

Error missing(std::string field) {
  return Error{ErrorCode::missing_field,
               "authentication response is missing a required field",
               std::move(field)};
}

} // namespace

Result<AuthMessage>
decode_auth_message_response(std::string_view json,
                             const AuthCodecLimits &limits) {
  auto parsed = parse<WireAuthMessageResponse>(json, limits);
  if (!parsed)
    return parsed.error();
  auto &wire = parsed.value();
  if (!wire.success)
    return missing("success");
  if (!*wire.success)
    return Error{ErrorCode::venue_rejected,
                 "Predict.fun rejected the authentication request", {}};
  if (!wire.data)
    return missing("data");
  if (!wire.data->message)
    return missing("data.message");
  if (wire.data->message->empty() ||
      wire.data->message->size() > limits.max_message_bytes) {
    return Error{ErrorCode::invalid_field,
                 "authentication message has an invalid length",
                 "data.message"};
  }
  return AuthMessage{std::move(*wire.data->message)};
}

Result<WalletJwt> decode_auth_token_response(std::string_view json,
                                             const AuthCodecLimits &limits) {
  auto parsed = parse<WireAuthTokenResponse>(json, limits);
  if (!parsed)
    return parsed.error();
  auto &wire = parsed.value();
  if (!wire.success)
    return missing("success");
  if (!*wire.success)
    return Error{ErrorCode::venue_rejected,
                 "Predict.fun rejected the authentication proof", {}};
  if (!wire.data)
    return missing("data");
  if (!wire.data->token)
    return missing("data.token");
  if (wire.data->token->empty() ||
      wire.data->token->size() > limits.max_token_bytes) {
    return Error{ErrorCode::invalid_field,
                 "authentication token has an invalid length", "data.token"};
  }
  return WalletJwt{std::move(*wire.data->token)};
}

Result<std::string> encode_auth_proof(const AuthProof &proof,
                                      const AuthCodecLimits &limits) {
  if (proof.signer.empty())
    return Error{ErrorCode::invalid_argument, "signer address is required",
                 "signer"};
  if (proof.signature.empty() ||
      proof.signature.size() > limits.max_signature_bytes) {
    return Error{ErrorCode::invalid_argument,
                 "signature has an invalid length", "signature"};
  }
  if (proof.message.empty() || proof.message.size() > limits.max_message_bytes) {
    return Error{ErrorCode::invalid_argument,
                 "authentication message has an invalid length", "message"};
  }
  WireAuthProof wire{proof.signer.to_string(), proof.signature, proof.message};
  auto encoded = glz::write_json(wire);
  if (!encoded) {
    return Error{ErrorCode::protocol_error,
                 "authentication proof JSON encoding failed", {}};
  }
  if (encoded->size() > limits.max_body_bytes) {
    return Error{ErrorCode::body_too_large,
                 "authentication proof exceeds configured limit", {}};
  }
  return std::move(*encoded);
}

} // namespace predictfun::codec
