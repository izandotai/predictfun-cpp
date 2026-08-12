#pragma once

#include <string>
#include <string_view>

namespace predictfun {

void secure_erase(std::string &value) noexcept;

class SecretString {
public:
  SecretString() = default;
  explicit SecretString(std::string value);
  ~SecretString();

  SecretString(const SecretString &) = delete;
  SecretString &operator=(const SecretString &) = delete;
  SecretString(SecretString &&other) noexcept;
  SecretString &operator=(SecretString &&other) noexcept;

  [[nodiscard]] std::string_view view() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  void clear() noexcept;

private:
  std::string value_;
};

} // namespace predictfun
