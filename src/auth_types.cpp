#include "predictfun/types/auth.hpp"

#include <utility>

namespace predictfun {

WalletJwt::WalletJwt(std::string token) : token_(std::move(token)) {}

std::string_view WalletJwt::view() const noexcept { return token_.view(); }

bool WalletJwt::empty() const noexcept { return token_.empty(); }

} // namespace predictfun
