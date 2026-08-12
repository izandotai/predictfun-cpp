#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace predictfun::net {

struct ReconnectPolicy {
  std::chrono::milliseconds initial{250};
  std::chrono::milliseconds maximum{10'000};
  std::uint32_t jitter_percent{20U};
  std::size_t storm_attempts{8U};
  std::chrono::milliseconds storm_window{30'000};
  std::chrono::milliseconds storm_cooldown{60'000};
  // A zero seed is replaced with a process-local, non-secret unique seed.
  std::uint64_t jitter_seed{0U};
};

struct ReconnectDecision {
  std::chrono::milliseconds delay{};
  bool storm_cooldown{false};
};

// Stateful reconnect controller. Backoff resets only after the caller has
// reached a semantically live state, never merely after a TCP/TLS open.
class ReconnectBackoff {
public:
  explicit ReconnectBackoff(ReconnectPolicy policy = {});

  [[nodiscard]] ReconnectDecision
  next(std::chrono::steady_clock::time_point now);
  void mark_stable() noexcept;

private:
  [[nodiscard]] std::chrono::milliseconds
  jitter(std::chrono::milliseconds base) noexcept;

  ReconnectPolicy policy_;
  std::chrono::milliseconds current_;
  std::deque<std::chrono::steady_clock::time_point> attempts_;
  std::chrono::steady_clock::time_point cooldown_until_{};
  std::uint64_t random_state_{0U};
};

} // namespace predictfun::net
