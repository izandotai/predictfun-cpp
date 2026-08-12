#include "predictfun/net/reconnect.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>

namespace predictfun::net {
namespace {

std::uint64_t unique_seed() noexcept {
  static std::atomic<std::uint64_t> sequence{0x9e3779b97f4a7c15ULL};
  const auto clock = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return clock ^ sequence.fetch_add(0x9e3779b97f4a7c15ULL,
                                    std::memory_order_relaxed);
}

} // namespace

ReconnectBackoff::ReconnectBackoff(ReconnectPolicy policy)
    : policy_(policy), current_(policy.initial),
      random_state_(policy.jitter_seed == 0U ? unique_seed()
                                             : policy.jitter_seed) {
  if (policy_.initial <= std::chrono::milliseconds::zero() ||
      policy_.maximum < policy_.initial)
    throw std::invalid_argument("invalid reconnect delay range");
  if (policy_.maximum.count() >
      std::numeric_limits<std::int64_t>::max() / 3)
    throw std::invalid_argument("reconnect maximum is too large");
  if (policy_.jitter_percent > 100U)
    throw std::invalid_argument("reconnect jitter must not exceed 100 percent");
  if (policy_.storm_attempts == 0U ||
      policy_.storm_window <= std::chrono::milliseconds::zero() ||
      policy_.storm_cooldown <= std::chrono::milliseconds::zero())
    throw std::invalid_argument("invalid reconnect storm policy");
}

ReconnectDecision
ReconnectBackoff::next(std::chrono::steady_clock::time_point now) {
  if (now < cooldown_until_) {
    return {std::chrono::ceil<std::chrono::milliseconds>(cooldown_until_ - now),
            true};
  }

  while (!attempts_.empty() && now - attempts_.front() > policy_.storm_window)
    attempts_.pop_front();
  attempts_.push_back(now);
  if (attempts_.size() > policy_.storm_attempts) {
    attempts_.clear();
    cooldown_until_ = now + policy_.storm_cooldown;
    return {policy_.storm_cooldown, true};
  }

  const auto delay = jitter(current_);
  if (current_ >= policy_.maximum / 2)
    current_ = policy_.maximum;
  else
    current_ = std::min(current_ * 2, policy_.maximum);
  return {delay, false};
}

void ReconnectBackoff::mark_stable() noexcept {
  current_ = policy_.initial;
  attempts_.clear();
  cooldown_until_ = {};
}

std::chrono::milliseconds
ReconnectBackoff::jitter(std::chrono::milliseconds base) noexcept {
  if (policy_.jitter_percent == 0U)
    return base;
  random_state_ ^= random_state_ << 13U;
  random_state_ ^= random_state_ >> 7U;
  random_state_ ^= random_state_ << 17U;

  const auto count = base.count();
  const auto percent = static_cast<std::int64_t>(policy_.jitter_percent);
  const auto span =
      (count / 100) * percent + ((count % 100) * percent) / 100;
  const auto width = static_cast<std::uint64_t>(span) * 2U + 1U;
  const auto sample = static_cast<std::int64_t>(random_state_ % width);
  const auto candidate = count + sample - span;
  return std::chrono::milliseconds{
      std::clamp(candidate, std::chrono::milliseconds{1}.count(),
                 policy_.maximum.count())};
}

} // namespace predictfun::net
