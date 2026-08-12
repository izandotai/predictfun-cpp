#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace predictfun {

enum class ErrorCode {
  invalid_argument,
  cancelled,
  deadline_exceeded,
  dns_failure,
  connect_failure,
  tls_failure,
  write_failure,
  read_failure,
  websocket_handshake_failure,
  websocket_closed,
  websocket_frame_too_large,
  websocket_queue_overflow,
  subscription_rejected,
  heartbeat_timeout,
  timestamp_regression,
  body_truncated,
  body_too_large,
  rate_limited,
  authentication_required,
  http_redirect,
  http_client_error,
  http_server_error,
  protocol_error,
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
  ambiguous_submission,
  remote_error,
  unsupported_chain,
  execution_reverted,
};

struct Error {
  ErrorCode code{ErrorCode::invalid_argument};
  std::string message;
  std::string field;
  int http_status{0};
  std::uint64_t retry_after_ms{0};
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
