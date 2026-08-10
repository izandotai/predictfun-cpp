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

private:
  struct Bucket {
    double tokens{0.0};
    double capacity{0.0};
    double tokens_per_second{0.0};
    std::chrono::steady_clock::time_point updated{};
  };

  static std::chrono::steady_clock::duration
  wait_for(Bucket &bucket, std::chrono::steady_clock::time_point now);
  static void consume_at(Bucket &bucket,
                         std::chrono::steady_clock::time_point when);

  std::mutex mutex_;
  Bucket global_;
  std::unordered_map<std::string, Bucket> endpoints_;
};

} // namespace predictfun::net
