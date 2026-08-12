#include "predictfun/net/reconnect.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": CHECK failed: " #condition << '\n';                    \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

void test_exponential_backoff_and_reset() {
  predictfun::net::ReconnectPolicy policy;
  policy.initial = std::chrono::milliseconds{10};
  policy.maximum = std::chrono::milliseconds{40};
  policy.jitter_percent = 0U;
  policy.storm_attempts = 100U;
  predictfun::net::ReconnectBackoff backoff(policy);
  const auto now = std::chrono::steady_clock::time_point{};
  CHECK(backoff.next(now).delay == std::chrono::milliseconds{10});
  CHECK(backoff.next(now + std::chrono::seconds{1}).delay ==
        std::chrono::milliseconds{20});
  CHECK(backoff.next(now + std::chrono::seconds{2}).delay ==
        std::chrono::milliseconds{40});
  CHECK(backoff.next(now + std::chrono::seconds{3}).delay ==
        std::chrono::milliseconds{40});
  backoff.mark_stable();
  CHECK(backoff.next(now + std::chrono::seconds{4}).delay ==
        std::chrono::milliseconds{10});
}

void test_reconnect_storm_cooldown() {
  predictfun::net::ReconnectPolicy policy;
  policy.initial = std::chrono::milliseconds{1};
  policy.maximum = std::chrono::milliseconds{8};
  policy.jitter_percent = 0U;
  policy.storm_attempts = 3U;
  policy.storm_window = std::chrono::seconds{10};
  policy.storm_cooldown = std::chrono::seconds{30};
  predictfun::net::ReconnectBackoff backoff(policy);
  const auto now = std::chrono::steady_clock::time_point{};
  CHECK(!backoff.next(now).storm_cooldown);
  CHECK(!backoff.next(now + std::chrono::seconds{1}).storm_cooldown);
  CHECK(!backoff.next(now + std::chrono::seconds{2}).storm_cooldown);
  const auto storm = backoff.next(now + std::chrono::seconds{3});
  CHECK(storm.storm_cooldown);
  CHECK(storm.delay == std::chrono::seconds{30});
  const auto held = backoff.next(now + std::chrono::seconds{8});
  CHECK(held.storm_cooldown);
  CHECK(held.delay == std::chrono::seconds{25});
}

void test_jitter_is_bounded_and_policy_is_validated() {
  predictfun::net::ReconnectPolicy policy;
  policy.initial = std::chrono::milliseconds{100};
  policy.maximum = std::chrono::milliseconds{1'000};
  policy.jitter_percent = 20U;
  policy.jitter_seed = 1U;
  policy.storm_attempts = 100U;
  predictfun::net::ReconnectBackoff backoff(policy);
  const auto decision =
      backoff.next(std::chrono::steady_clock::time_point{});
  CHECK(decision.delay >= std::chrono::milliseconds{80});
  CHECK(decision.delay <= std::chrono::milliseconds{120});

  bool rejected = false;
  try {
    policy.jitter_percent = 101U;
    predictfun::net::ReconnectBackoff invalid(policy);
    (void)invalid;
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  CHECK(rejected);
}

} // namespace

int main() {
  test_exponential_backoff_and_reset();
  test_reconnect_storm_cooldown();
  test_jitter_is_bounded_and_policy_is_validated();
  if (failures != 0)
    std::cerr << failures << " reconnect checks failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
