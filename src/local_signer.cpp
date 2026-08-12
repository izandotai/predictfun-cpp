#include "predictfun/order/local_signer.hpp"

#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace predictfun::order {
namespace {

void secure_erase_bytes(std::span<std::uint8_t> bytes) noexcept {
  volatile std::uint8_t *cursor = bytes.empty() ? nullptr : bytes.data();
  for (std::size_t index = 0; index < bytes.size(); ++index)
    cursor[index] = 0U;
}

int nibble(char ch) {
  const auto value = static_cast<unsigned char>(ch);
  if (value >= static_cast<unsigned char>('0') &&
      value <= static_cast<unsigned char>('9'))
    return static_cast<int>(value - static_cast<unsigned char>('0'));
  const auto lower = static_cast<unsigned char>(std::tolower(value));
  if (lower >= static_cast<unsigned char>('a') &&
      lower <= static_cast<unsigned char>('f'))
    return static_cast<int>(lower - static_cast<unsigned char>('a')) + 10;
  return -1;
}

Result<std::array<std::uint8_t, 32>> parse_private_key(SecretString key) {
  const auto text = key.view();
  const auto offset = text.size() == 66U && text.substr(0, 2) == "0x" ? 2U : 0U;
  if (text.size() - offset != 64U)
    return Error{ErrorCode::invalid_field,
                 "private key must contain exactly 32 bytes", "private_key"};
  std::array<std::uint8_t, 32> result{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    const auto high = nibble(text[offset + index * 2U]);
    const auto low = nibble(text[offset + index * 2U + 1U]);
    if (high < 0 || low < 0) {
      secure_erase_bytes(result);
      return Error{ErrorCode::invalid_field,
                   "private key must be hexadecimal", "private_key"};
    }
    result[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return result;
}

std::string signature_hex(const std::array<std::uint8_t, 64> &compact,
                          int recovery_id) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result{"0x"};
  result.reserve(132U);
  for (const auto byte : compact) {
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 0x0fU]);
  }
  const auto v = static_cast<std::uint8_t>(27 + recovery_id);
  result.push_back(digits[v >> 4U]);
  result.push_back(digits[v & 0x0fU]);
  return result;
}

Result<EvmAddress> address_from_public_key(
    const secp256k1_context *context,
    const std::array<std::uint8_t, 32> &private_key) {
  secp256k1_pubkey public_key{};
  if (secp256k1_ec_pubkey_create(context, &public_key, private_key.data()) != 1)
    return Error{ErrorCode::invalid_field, "private key is not valid",
                 "private_key"};
  std::array<std::uint8_t, 65> serialized{};
  auto serialized_size = serialized.size();
  if (secp256k1_ec_pubkey_serialize(context, serialized.data(),
                                    &serialized_size, &public_key,
                                    SECP256K1_EC_UNCOMPRESSED) != 1 ||
      serialized_size != serialized.size())
    return Error{ErrorCode::protocol_error,
                 "cannot derive an EVM public key", {}};
  auto digest = keccak256(std::span<const std::uint8_t>{serialized}.subspan(1));
  secure_erase_bytes(serialized);
  if (!digest)
    return digest.error();
  constexpr char digits[] = "0123456789abcdef";
  std::string encoded{"0x"};
  encoded.reserve(42U);
  for (auto iterator = digest.value().end() - 20; iterator != digest.value().end();
       ++iterator) {
    encoded.push_back(digits[*iterator >> 4U]);
    encoded.push_back(digits[*iterator & 0x0fU]);
  }
  return EvmAddress::parse(encoded);
}

} // namespace

struct LocalSigner::Impl {
  std::array<std::uint8_t, 32> private_key{};
  secp256k1_context *context{nullptr};
  EvmAddress address;

  ~Impl() {
    secure_erase_bytes(private_key);
    if (context != nullptr)
      secp256k1_context_destroy(context);
  }
};

LocalSigner::LocalSigner(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
LocalSigner::~LocalSigner() = default;
LocalSigner::LocalSigner(LocalSigner &&) noexcept = default;
LocalSigner &LocalSigner::operator=(LocalSigner &&) noexcept = default;

Result<LocalSigner> LocalSigner::create(SecretString private_key) {
  auto decoded = parse_private_key(std::move(private_key));
  if (!decoded)
    return decoded.error();
  auto impl = std::make_unique<Impl>();
  impl->private_key = decoded.value();
  secure_erase_bytes(decoded.value());
  impl->context = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
  if (impl->context == nullptr)
    return Error{ErrorCode::protocol_error,
                 "cannot create secp256k1 signing context", {}};
  if (secp256k1_ec_seckey_verify(impl->context, impl->private_key.data()) != 1)
    return Error{ErrorCode::invalid_field, "private key is not valid",
                 "private_key"};
  auto derived = address_from_public_key(impl->context, impl->private_key);
  if (!derived)
    return derived.error();
  impl->address = derived.value();
  return LocalSigner{std::move(impl)};
}

const EvmAddress &LocalSigner::address() const noexcept { return impl_->address; }

Result<std::string> LocalSigner::sign_digest(const Hash32 &digest) const {
  secp256k1_ecdsa_recoverable_signature signature{};
  if (secp256k1_ecdsa_sign_recoverable(
          impl_->context, &signature, digest.data(), impl_->private_key.data(),
          nullptr, nullptr) != 1)
    return Error{ErrorCode::protocol_error,
                 "secp256k1 signing failed", {}};
  std::array<std::uint8_t, 64> compact{};
  int recovery_id = 0;
  if (secp256k1_ecdsa_recoverable_signature_serialize_compact(
          impl_->context, compact.data(), &recovery_id, &signature) != 1)
    return Error{ErrorCode::protocol_error,
                 "cannot serialize secp256k1 signature", {}};
  auto encoded = signature_hex(compact, recovery_id);
  secure_erase_bytes(compact);
  return encoded;
}

Result<std::string>
LocalSigner::sign_personal_message_32(const Hash32 &message) const {
  constexpr std::string_view prefix = "\x19"
                                      "Ethereum Signed Message:\n32";
  std::vector<std::uint8_t> payload;
  payload.reserve(prefix.size() + message.size());
  payload.insert(payload.end(), prefix.begin(), prefix.end());
  payload.insert(payload.end(), message.begin(), message.end());
  auto digest = keccak256(payload);
  secure_erase_bytes(payload);
  if (!digest)
    return digest.error();
  return sign_digest(digest.value());
}

} // namespace predictfun::order
