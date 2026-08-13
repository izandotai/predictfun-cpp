#include "predictfun/order/local_signer.hpp"

#include "core/crypto/eth.hpp"
#include "core/crypto/secp256k1_key.hpp"

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

std::string signature_hex(const izan::crypto::EcdsaSignature &signature) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result{"0x"};
  result.reserve(132U);
  for (const auto byte : signature.r) {
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 0x0fU]);
  }
  for (const auto byte : signature.s) {
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 0x0fU]);
  }
  const auto v = static_cast<std::uint8_t>(27U + signature.y_parity);
  result.push_back(digits[v >> 4U]);
  result.push_back(digits[v & 0x0fU]);
  return result;
}

} // namespace

struct LocalSigner::Impl {
  izan::crypto::Secp256k1PrivateKey private_key;
  EvmAddress address;

  Impl(izan::crypto::Secp256k1PrivateKey key, EvmAddress derived)
      : private_key(std::move(key)), address(derived) {}
};

LocalSigner::LocalSigner(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
LocalSigner::~LocalSigner() = default;
LocalSigner::LocalSigner(LocalSigner &&) noexcept = default;
LocalSigner &LocalSigner::operator=(LocalSigner &&) noexcept = default;

Result<LocalSigner> LocalSigner::create(SecretString private_key) {
  auto decoded = parse_private_key(std::move(private_key));
  if (!decoded)
    return decoded.error();
  auto guarded = izan::crypto::Secp256k1PrivateKey::from_bytes(decoded.value());
  secure_erase_bytes(decoded.value());
  if (!guarded)
    return Error{ErrorCode::invalid_field, "private key is not valid",
                 "private_key"};
  auto public_key = guarded->public_key_uncompressed();
  auto derived = EvmAddress::parse(izan::crypto::eth_address(public_key));
  secure_erase_bytes(public_key);
  if (!derived)
    return derived.error();
  return LocalSigner{std::make_unique<Impl>(std::move(*guarded),
                                             derived.value())};
}

const EvmAddress &LocalSigner::address() const noexcept { return impl_->address; }

Result<std::string> LocalSigner::sign_digest(const Hash32 &digest) const {
  auto signature = impl_->private_key.sign_digest(digest);
  if (!signature)
    return Error{ErrorCode::protocol_error,
                 "secp256k1 signing failed", {}};
  return signature_hex(*signature);
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
