#include "predictfun/lifecycle/tracker.hpp"
#include "predictfun/order/amounts.hpp"
#include "predictfun/types/decimal.hpp"
#include "predictfun/types/exact_number.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using boost::multiprecision::cpp_int;
using predictfun::ContractSide;
using predictfun::Price;
using predictfun::Uint256;

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": CHECK failed: " #condition << '\n';                      \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

class DeterministicRandom {
public:
  explicit DeterministicRandom(std::uint64_t seed) : state_(seed) {}

  std::uint64_t next() noexcept {
    state_ ^= state_ << 13U;
    state_ ^= state_ >> 7U;
    state_ ^= state_ << 17U;
    return state_;
  }

  std::uint64_t bounded(std::uint64_t exclusive) noexcept {
    return exclusive == 0U ? 0U : next() % exclusive;
  }

private:
  std::uint64_t state_;
};

cpp_int integer(const Uint256 &value) {
  cpp_int result = 0;
  for (const char digit : value.to_string()) {
    result *= 10;
    result += static_cast<unsigned int>(digit - '0');
  }
  return result;
}

std::uint64_t power10(std::uint8_t exponent) {
  std::uint64_t value = 1U;
  for (std::uint8_t index = 0U; index < exponent; ++index)
    value *= 10U;
  return value;
}

cpp_int power10_big(std::size_t exponent) {
  cpp_int value = 1;
  for (std::size_t index = 0U; index < exponent; ++index)
    value *= 10;
  return value;
}

Uint256 uint256(const cpp_int &value) {
  return Uint256::parse(value.convert_to<std::string>()).value();
}

void price_roundtrip_and_complement_properties() {
  DeterministicRandom random{0x26f00d5eedULL};
  for (std::uint8_t precision = 0U; precision <= 18U; ++precision) {
    const auto scale = power10(precision);
    for (std::size_t iteration = 0U; iteration < 300U; ++iteration) {
      const auto ticks = random.bounded(scale + 1U);
      const auto price = Price::from_ticks(ticks, precision);
      CHECK(price);
      if (!price)
        continue;
      const auto parsed = Price::parse(price.value().to_string(), precision);
      CHECK(parsed && parsed.value() == price.value());
      const auto complement = price.value().complement();
      CHECK(complement.ticks() + price.value().ticks() == scale);
      CHECK(complement.complement() == price.value());
    }
  }
}

void decimal_to_wei_properties() {
  DeterministicRandom random{0xdec1a1ULL};
  const cpp_int wei_scale = power10_big(18U);
  for (std::size_t iteration = 0U; iteration < 3'000U; ++iteration) {
    const auto whole = random.bounded(1'000'000U);
    const auto fraction_size = static_cast<std::size_t>(random.bounded(19U));
    std::string fraction;
    fraction.reserve(fraction_size);
    for (std::size_t index = 0U; index < fraction_size; ++index) {
      fraction.push_back(
          static_cast<char>('0' + static_cast<char>(random.bounded(10U))));
    }
    std::string text = std::to_string(whole);
    if (!fraction.empty()) {
      text.push_back('.');
      text += fraction;
    }

    const auto converted = predictfun::order::decimal_to_wei(text);
    CHECK(converted);
    if (!converted)
      continue;
    cpp_int expected = cpp_int{whole} * wei_scale;
    if (!fraction.empty()) {
      cpp_int fraction_value = 0;
      for (const char digit : fraction) {
        fraction_value *= 10;
        fraction_value += static_cast<unsigned int>(digit - '0');
      }
      expected += fraction_value * power10_big(18U - fraction.size());
    }
    CHECK(integer(converted.value()) == expected);
  }
}

void significant_digit_properties() {
  DeterministicRandom random{0x51a91f1ca7ULL};
  for (std::size_t iteration = 0U; iteration < 3'000U; ++iteration) {
    cpp_int original =
        cpp_int{random.next()} * power10_big(random.bounded(40U));
    original += random.next();
    const auto value = uint256(original);
    const auto digits = static_cast<std::size_t>(1U + random.bounded(20U));
    const auto retained =
        predictfun::order::retain_significant_digits(value, digits);
    CHECK(retained);
    if (!retained)
      continue;
    const auto result = integer(retained.value());
    CHECK(result <= original);
    const auto original_text = original.convert_to<std::string>();
    const auto retained_text = result.convert_to<std::string>();
    CHECK(retained_text.size() == original_text.size());
    const auto prefix = std::min(digits, original_text.size());
    CHECK(retained_text.substr(0U, prefix) == original_text.substr(0U, prefix));
    for (std::size_t index = prefix; index < retained_text.size(); ++index)
      CHECK(retained_text[index] == '0');
  }
}

void limit_amount_properties() {
  DeterministicRandom random{0xa110ca7eULL};
  const cpp_int wei_scale = power10_big(18U);
  for (std::size_t iteration = 0U; iteration < 3'000U; ++iteration) {
    const cpp_int price =
        cpp_int{1U + random.bounded(1'000U)} * power10_big(15U);
    const cpp_int quantity = cpp_int{1U + random.bounded(10'000U)} * wei_scale;
    for (const auto side : {ContractSide::buy, ContractSide::sell}) {
      const auto amounts = predictfun::order::limit_amounts(
          {side, uint256(price), uint256(quantity)});
      CHECK(amounts);
      if (!amounts)
        continue;
      const auto collateral = (price * quantity) / wei_scale;
      CHECK(integer(amounts.value().amount) == quantity);
      CHECK(integer(amounts.value().price_per_share_wei) == price);
      if (side == ContractSide::buy) {
        CHECK(integer(amounts.value().maker_amount) == collateral);
        CHECK(integer(amounts.value().taker_amount) == quantity);
      } else {
        CHECK(integer(amounts.value().maker_amount) == quantity);
        CHECK(integer(amounts.value().taker_amount) == collateral);
      }
    }
  }
}

std::string order_hash(std::uint64_t value) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(66U, '0');
  result[0] = '0';
  result[1] = 'x';
  for (std::size_t index = 0U; index < 16U; ++index) {
    result[result.size() - 1U - index] =
        digits[static_cast<std::size_t>(value & 0xfU)];
    value >>= 4U;
  }
  return result;
}

void lifecycle_safety_properties() {
  DeterministicRandom random{0x11fec7c1eULL};
  for (std::size_t iteration = 1U; iteration <= 2'000U; ++iteration) {
    predictfun::lifecycle::OrderTracker tracker;
    const auto hash = order_hash(iteration);
    CHECK(tracker.begin_submission(
        hash, predictfun::ExactDecimal::parse("1").value()));
    CHECK(!tracker.begin_submission(
        hash, predictfun::ExactDecimal::parse("1").value()));

    predictfun::MutationOutcome<predictfun::CreateOrderReceipt> outcome;
    outcome.reconciliation_key = hash;
    if ((random.next() & 1U) != 0U) {
      outcome.disposition = predictfun::MutationDisposition::ambiguous;
      outcome.ambiguity =
          predictfun::Error{predictfun::ErrorCode::ambiguous_submission,
                            "fault-injected response loss",
                            {}};
      CHECK(tracker.apply_create_outcome(hash, outcome));
      const auto *tracked = tracker.find(hash);
      CHECK(tracked != nullptr);
      CHECK(tracked &&
            tracked->state == predictfun::OrderLifecycleState::ambiguous);
      CHECK(tracked && tracked->reconciliation_required);
      CHECK(tracked && !tracked->terminal());
      CHECK(tracked && !tracked->safe_to_rebuild_with_new_nonce());
    } else {
      outcome.receipt = predictfun::CreateOrderReceipt{"OK", "order", hash, {}};
      CHECK(tracker.apply_create_outcome(hash, outcome));
      if ((random.next() & 1U) != 0U)
        CHECK(tracker.mark_book_removed(hash));
      const auto *tracked = tracker.find(hash);
      CHECK(tracked != nullptr);
      CHECK(tracked && !tracked->terminal());
      CHECK(tracked && !tracked->safe_to_rebuild_with_new_nonce());
    }

    tracker.require_reconciliation(iteration);
    const auto *tracked = tracker.find(hash);
    CHECK(tracked && tracked->reconciliation_required);
    CHECK(tracked && !tracked->safe_to_rebuild_with_new_nonce());
  }
}

} // namespace

int main() {
  price_roundtrip_and_complement_properties();
  decimal_to_wei_properties();
  significant_digit_properties();
  limit_amount_properties();
  lifecycle_safety_properties();
  if (failures != 0)
    std::cerr << failures << " property assertion(s) failed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
