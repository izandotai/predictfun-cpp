#include "predictfun/net/rate_limiter.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace predictfun::net {

RateLimiter::RateLimiter(RateLimitPolicy policy) : global_{} {
  const auto make_bucket = [](std::size_t requests_per_minute) {
    if (requests_per_minute == 0U)
      throw std::invalid_argument("rate limit must be positive");
    Bucket bucket;
    bucket.capacity = static_cast<double>(requests_per_minute);
    bucket.tokens = bucket.capacity;
    bucket.tokens_per_second = bucket.capacity / 60.0;
    bucket.updated = std::chrono::steady_clock::now();
    return bucket;
  };
  global_ = make_bucket(policy.global_requests_per_minute);
  for (const auto &[endpoint, limit] : policy.endpoint_requests_per_minute)
    endpoints_.emplace(endpoint, make_bucket(limit));
}

std::chrono::steady_clock::duration
RateLimiter::wait_for(Bucket &bucket,
                      std::chrono::steady_clock::time_point now) {
  if (now > bucket.updated) {
    const auto elapsed =
        std::chrono::duration<double>(now - bucket.updated).count();
    bucket.tokens = std::min(
        bucket.capacity, bucket.tokens + elapsed * bucket.tokens_per_second);
    bucket.updated = now;
  }
  const auto reservation_floor = std::max(now, bucket.updated);
  if (bucket.tokens >= 1.0)
    return reservation_floor - now;
  const auto seconds = (1.0 - bucket.tokens) / bucket.tokens_per_second;
  return reservation_floor - now +
         std::chrono::duration_cast<std::chrono::steady_clock::duration>(
             std::chrono::duration<double>(seconds));
}

void RateLimiter::consume_at(Bucket &bucket,
                             std::chrono::steady_clock::time_point when) {
  (void)wait_for(bucket, when);
  bucket.tokens = std::max(0.0, bucket.tokens - 1.0);
}

std::chrono::milliseconds
RateLimiter::reserve(std::string_view endpoint,
                     std::chrono::steady_clock::time_point now) {
  std::scoped_lock lock(mutex_);
  auto delay = wait_for(global_, now);
  auto endpoint_it = endpoints_.find(std::string{endpoint});
  if (endpoint_it != endpoints_.end())
    delay = std::max(delay, wait_for(endpoint_it->second, now));
  const auto reserved = now + delay;
  consume_at(global_, reserved);
  if (endpoint_it != endpoints_.end())
    consume_at(endpoint_it->second, reserved);
  return std::chrono::ceil<std::chrono::milliseconds>(delay);
}

} // namespace predictfun::net
