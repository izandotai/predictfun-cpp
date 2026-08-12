#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace predictfun::net {

struct RateLimitPolicy {
  std::size_t global_requests_per_minute{240U};
  std::unordered_map<std::string, std::size_t> endpoint_requests_per_minute;
};

class RateLimiter {
public:
  explicit RateLimiter(RateLimitPolicy policy = {});

  [[nodiscard]] std::chrono::milliseconds
  reserve(std::string_view endpoint, std::chrono::steady_clock::time_point now);

  // Applies a server-directed cooldown to the selected endpoint budget.
  // Set global when the server explicitly indicates a key-wide cooldown.
  // Concurrent callers observe the cooldown on their next reservation.
  void penalize(std::string_view endpoint,
                std::chrono::steady_clock::time_point now,
                std::chrono::milliseconds retry_after,
                bool global = false);

private:
  struct Bucket {
    double tokens{0.0};
    double capacity{0.0};
    double tokens_per_second{0.0};
    std::chrono::steady_clock::time_point updated{};
    std::chrono::steady_clock::time_point blocked_until{};
  };

  static std::chrono::steady_clock::duration
  wait_for(Bucket &bucket, std::chrono::steady_clock::time_point now);
  static void consume_at(Bucket &bucket,
                         std::chrono::steady_clock::time_point when);

  std::mutex mutex_;
  Bucket global_;
  std::unordered_map<std::string, Bucket> endpoints_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      endpoint_blocked_until_;
};

// Parses Retry-After delta-seconds or IMF-fixdate. Invalid or past values use
// fallback; all results are bounded by maximum.
[[nodiscard]] std::chrono::milliseconds parse_retry_after(
    std::string_view value, std::chrono::system_clock::time_point now,
    std::chrono::milliseconds fallback,
    std::chrono::milliseconds maximum = std::chrono::seconds{60});

} // namespace predictfun::net
