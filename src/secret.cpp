#include "predictfun/types/secret.hpp"

#include <utility>

namespace predictfun {

void secure_erase(std::string &value) noexcept {
  volatile char *bytes = value.empty() ? nullptr : value.data();
  for (std::size_t index = 0; index < value.size(); ++index)
    bytes[index] = '\0';
  value.clear();
}

SecretString::SecretString(std::string value) : value_(value) {
  secure_erase(value);
}

SecretString::~SecretString() { clear(); }

SecretString::SecretString(SecretString &&other) noexcept
    : value_(other.value_) {
  other.clear();
}

SecretString &SecretString::operator=(SecretString &&other) noexcept {
  if (this != &other) {
    clear();
    value_ = other.value_;
    other.clear();
  }
  return *this;
}

std::string_view SecretString::view() const noexcept { return value_; }

bool SecretString::empty() const noexcept { return value_.empty(); }

void SecretString::clear() noexcept { secure_erase(value_); }

} // namespace predictfun
