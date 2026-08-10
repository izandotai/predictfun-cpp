#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace predictfun {

enum class ErrorCode {
  invalid_argument,
  body_too_large,
  malformed_json,
  missing_field,
  invalid_field,
  unsupported_precision,
  numeric_overflow,
  invalid_decimal,
  invalid_price,
  invalid_quantity,
  too_many_items,
  invalid_orderbook,
  venue_rejected,
};

struct Error {
  ErrorCode code{ErrorCode::invalid_argument};
  std::string message;
  std::string field;
};

template <class T> class Result {
public:
  Result(T value) : storage_(std::move(value)) {}
  Result(Error error) : storage_(std::move(error)) {}

  [[nodiscard]] bool has_value() const noexcept {
    return std::holds_alternative<T>(storage_);
  }

  explicit operator bool() const noexcept { return has_value(); }

  [[nodiscard]] const T &value() const {
    if (!has_value()) {
      throw std::logic_error("predictfun::Result has no value");
    }
    return std::get<T>(storage_);
  }

  [[nodiscard]] T &value() {
    if (!has_value()) {
      throw std::logic_error("predictfun::Result has no value");
    }
    return std::get<T>(storage_);
  }

  [[nodiscard]] const Error &error() const {
    if (has_value()) {
      throw std::logic_error("predictfun::Result has no error");
    }
    return std::get<Error>(storage_);
  }

private:
  std::variant<T, Error> storage_;
};

} // namespace predictfun
