#include "predictfun/net/rate_limiter.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>
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
  const auto eligibility = std::max({now, bucket.updated,
                                     bucket.blocked_until});
  if (eligibility > bucket.updated) {
    const auto elapsed =
        std::chrono::duration<double>(eligibility - bucket.updated).count();
    bucket.tokens = std::min(
        bucket.capacity, bucket.tokens + elapsed * bucket.tokens_per_second);
    bucket.updated = eligibility;
  }
  if (bucket.tokens >= 1.0)
    return eligibility - now;
  const auto seconds = (1.0 - bucket.tokens) / bucket.tokens_per_second;
  return eligibility - now +
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
  const auto global_delay = wait_for(global_, now);
  consume_at(global_, now + global_delay);

  auto endpoint_delay = std::chrono::steady_clock::duration::zero();
  const auto endpoint_name = std::string{endpoint};
  const auto blocked = endpoint_blocked_until_.find(endpoint_name);
  if (blocked != endpoint_blocked_until_.end() && blocked->second > now)
    endpoint_delay = blocked->second - now;
  auto endpoint_it = endpoints_.find(endpoint_name);
  if (endpoint_it != endpoints_.end()) {
    endpoint_delay =
        std::max(endpoint_delay, wait_for(endpoint_it->second, now));
    consume_at(endpoint_it->second, now + endpoint_delay);
  }
  const auto delay = std::max(global_delay, endpoint_delay);
  return std::chrono::ceil<std::chrono::milliseconds>(delay);
}

void RateLimiter::penalize(std::string_view endpoint,
                           std::chrono::steady_clock::time_point now,
                           std::chrono::milliseconds retry_after,
                           bool global) {
  if (retry_after < std::chrono::milliseconds::zero())
    retry_after = std::chrono::milliseconds::zero();
  std::scoped_lock lock(mutex_);
  const auto until = now + retry_after;
  if (global)
    global_.blocked_until = std::max(global_.blocked_until, until);
  const auto endpoint_name = std::string{endpoint};
  auto &endpoint_until = endpoint_blocked_until_[endpoint_name];
  endpoint_until = std::max(endpoint_until, until);
  const auto found = endpoints_.find(endpoint_name);
  if (found != endpoints_.end())
    found->second.blocked_until =
        std::max(found->second.blocked_until, until);
}

std::chrono::milliseconds parse_retry_after(
    std::string_view value, std::chrono::system_clock::time_point now,
    std::chrono::milliseconds fallback, std::chrono::milliseconds maximum) {
  if (maximum < std::chrono::milliseconds::zero())
    throw std::invalid_argument("Retry-After maximum must not be negative");
  const auto bounded = [maximum](std::chrono::milliseconds candidate) {
    return std::clamp(candidate, std::chrono::milliseconds::zero(), maximum);
  };
  std::uint64_t seconds = 0U;
  const auto numeric =
      std::from_chars(value.data(), value.data() + value.size(), seconds);
  if (!value.empty() && numeric.ec == std::errc{} &&
      numeric.ptr == value.data() + value.size()) {
    constexpr auto maximum_safe_seconds =
        static_cast<std::uint64_t>(
            std::chrono::milliseconds::max().count() / 1'000);
    const auto safe = std::min(seconds, maximum_safe_seconds);
    return bounded(std::chrono::milliseconds{safe * 1'000U});
  }

  std::tm parsed{};
  std::istringstream input{std::string{value}};
  input.imbue(std::locale::classic());
  input >> std::get_time(&parsed, "%a, %d %b %Y %H:%M:%S GMT");
  if (!input.fail() && input.peek() == std::char_traits<char>::eof()) {
#if defined(_WIN32)
    const auto timestamp = _mkgmtime64(&parsed);
#else
    const auto timestamp = timegm(&parsed);
#endif
    if (timestamp >= 0) {
      const auto target = std::chrono::system_clock::from_time_t(
          static_cast<std::time_t>(timestamp));
      return bounded(std::chrono::ceil<std::chrono::milliseconds>(target - now));
    }
  }
  return bounded(fallback);
}

} // namespace predictfun::net
