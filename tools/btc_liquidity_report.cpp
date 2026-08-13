#include <glaze/glaze.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

constexpr auto read_options = glz::opts{.error_on_unknown_keys = false};

struct WireQuote {
  std::string budget_pusd;
  bool complete{false};
  std::string vwap;
  std::string worst_price;
  bool round_trip_complete{false};
  std::string round_trip_book_loss_pusd;
};

struct WireOutcome {
  std::string name;
  std::vector<WireQuote> quotes;
};

struct WireSample {
  std::string schema;
  std::int64_t captured_at_ms{0};
  std::string interval;
  std::uint64_t window_duration_seconds{0};
  std::uint64_t seconds_remaining{0};
  std::uint64_t window_start_epoch{0};
  std::uint64_t market_id{0};
  std::uint32_t fee_rate_bps{0};
  std::vector<WireOutcome> outcomes;
};

struct Key {
  std::string interval;
  std::string phase;
  std::string outcome;
  std::string budget;

  friend bool operator<(const Key &left, const Key &right) {
    return std::tie(left.interval, left.phase, left.outcome, left.budget) <
           std::tie(right.interval, right.phase, right.outcome, right.budget);
  }
};

struct Aggregate {
  std::size_t samples{0};
  std::size_t buy_complete{0};
  std::size_t round_trip_complete{0};
  long double vwap_sum{0};
  long double worst_price_sum{0};
  long double book_loss_sum{0};
  long double book_loss_max{0};
  std::set<std::uint64_t> windows;
};

bool decimal(std::string_view text, long double &value) {
  try {
    std::size_t consumed = 0;
    const std::string storage{text};
    value = std::stold(storage, &consumed);
    return consumed == storage.size() && std::isfinite(value) && value >= 0;
  } catch (...) {
    return false;
  }
}

std::string percent(std::size_t count, std::size_t total) {
  if (total == 0U)
    return "--";
  std::ostringstream output;
  output << std::fixed << std::setprecision(1)
         << (100.0L * static_cast<long double>(count) /
             static_cast<long double>(total))
         << '%';
  return output.str();
}

std::string phase(std::uint64_t remaining, std::uint64_t duration) {
  if (duration == 0U)
    return "?";
  const auto elapsed = duration > remaining ? duration - remaining : 0U;
  const auto quarter = std::min<std::uint64_t>((elapsed * 4U) / duration, 3U);
  return "Q" + std::to_string(quarter + 1U);
}

int run(const std::string &path) {
  std::ifstream input{path};
  if (!input) {
    std::cerr << "cannot open liquidity journal: " << path << '\n';
    return 1;
  }

  std::map<Key, Aggregate> aggregates;
  std::set<std::uint64_t> markets;
  std::map<std::pair<std::string, std::string>, std::set<std::uint64_t>>
      coverage;
  std::set<std::uint32_t> fees;
  std::int64_t first_ms = std::numeric_limits<std::int64_t>::max();
  std::int64_t last_ms = 0;
  std::size_t rows = 0;
  std::size_t line_number = 0;
  std::string line;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty())
      continue;
    WireSample sample;
    const auto error = glz::read<read_options>(sample, line);
    if (error) {
      std::cerr << "invalid JSONL at line " << line_number << ": "
                << glz::format_error(error, line) << '\n';
      return 1;
    }
    if (sample.schema != "predictfun.btc_liquidity.v2" ||
        sample.captured_at_ms <= 0 || sample.interval.empty() ||
        sample.window_duration_seconds == 0U ||
        sample.seconds_remaining > sample.window_duration_seconds ||
        sample.market_id == 0U || sample.outcomes.size() != 2U ||
        sample.window_start_epoch + sample.window_duration_seconds <
            sample.window_start_epoch) {
      std::cerr << "invalid liquidity sample at line " << line_number << '\n';
      return 1;
    }

    ++rows;
    markets.insert(sample.market_id);
    fees.insert(sample.fee_rate_bps);
    first_ms = std::min(first_ms, sample.captured_at_ms);
    last_ms = std::max(last_ms, sample.captured_at_ms);
    coverage[{sample.interval,
              phase(sample.seconds_remaining, sample.window_duration_seconds)}]
        .insert(sample.window_start_epoch);
    for (const auto &outcome : sample.outcomes) {
      if (outcome.name.empty()) {
        std::cerr << "missing outcome at line " << line_number << '\n';
        return 1;
      }
      for (const auto &quote : outcome.quotes) {
        long double vwap = 0;
        long double worst = 0;
        long double loss = 0;
        if (!decimal(quote.vwap, vwap) || !decimal(quote.worst_price, worst) ||
            !decimal(quote.round_trip_book_loss_pusd, loss)) {
          std::cerr << "invalid quote decimal at line " << line_number << '\n';
          return 1;
        }
        auto &aggregate = aggregates[Key{
            sample.interval,
            phase(sample.seconds_remaining, sample.window_duration_seconds),
            outcome.name, quote.budget_pusd}];
        ++aggregate.samples;
        aggregate.buy_complete += quote.complete ? 1U : 0U;
        aggregate.round_trip_complete += quote.round_trip_complete ? 1U : 0U;
        aggregate.vwap_sum += vwap;
        aggregate.worst_price_sum += worst;
        aggregate.book_loss_sum += loss;
        aggregate.book_loss_max = std::max(aggregate.book_loss_max, loss);
        aggregate.windows.insert(sample.window_start_epoch);
      }
    }
  }

  if (rows == 0U) {
    std::cerr << "liquidity journal is empty\n";
    return 1;
  }

  const auto elapsed_seconds =
      last_ms >= first_ms
          ? static_cast<long double>(last_ms - first_ms) / 1000.0L
          : 0.0L;
  std::cout << "BTC LIQUIDITY REPORT\n"
            << "rows " << rows << " | markets " << markets.size()
            << " | elapsed " << std::fixed << std::setprecision(1)
            << elapsed_seconds << "s | fee bps ";
  bool first_fee = true;
  for (const auto fee : fees) {
    if (!first_fee)
      std::cout << ',';
    first_fee = false;
    std::cout << fee;
  }
  std::cout << " (reported, not deducted)\n\n"
            << "TF   PHASE SIDE  BUDGET  N    WIN  BUY-FULL  RT-FULL  AVG-VWAP  "
               "AVG-WORST  AVG-BOOK-LOSS  MAX-BOOK-LOSS\n";

  for (const auto &[key, aggregate] : aggregates) {
    const auto divisor = static_cast<long double>(aggregate.samples);
    std::cout << std::left << std::setw(4) << key.interval << ' '
              << std::setw(5) << key.phase << ' ' << std::setw(5) << key.outcome
              << " $" << std::right << std::setw(5) << key.budget << "  "
              << std::setw(4) << aggregate.samples << "  " << std::setw(3)
              << aggregate.windows.size() << "  " << std::setw(8)
              << percent(aggregate.buy_complete, aggregate.samples) << "  "
              << std::setw(7)
              << percent(aggregate.round_trip_complete, aggregate.samples)
              << "  " << std::fixed << std::setprecision(4) << std::setw(8)
              << (aggregate.vwap_sum / divisor) << "  " << std::setw(9)
              << (aggregate.worst_price_sum / divisor) << "  $" << std::setw(13)
              << (aggregate.book_loss_sum / divisor) << "  $" << std::setw(13)
              << aggregate.book_loss_max << '\n';
  }

  std::cout << "\nWINDOW COVERAGE\nTF   Q1  Q2  Q3  Q4  COMPLETE\n";
  std::set<std::string> intervals;
  for (const auto &[key, _] : coverage)
    intervals.insert(key.first);
  for (const auto &interval : intervals) {
    std::array<std::size_t, 4U> counts{};
    std::set<std::uint64_t> complete;
    bool first_phase = true;
    for (std::size_t index = 0; index < counts.size(); ++index) {
      const auto key = std::make_pair(interval, "Q" + std::to_string(index + 1U));
      const auto found = coverage.find(key);
      if (found == coverage.end()) {
        complete.clear();
        first_phase = false;
        continue;
      }
      counts[index] = found->second.size();
      if (first_phase) {
        complete = found->second;
        first_phase = false;
      } else {
        std::set<std::uint64_t> intersection;
        std::set_intersection(complete.begin(), complete.end(),
                              found->second.begin(), found->second.end(),
                              std::inserter(intersection, intersection.begin()));
        complete = std::move(intersection);
      }
    }
    std::cout << std::left << std::setw(4) << interval;
    for (const auto count : counts)
      std::cout << ' ' << std::right << std::setw(3) << count;
    std::cout << "  " << std::setw(8) << complete.size() << '\n';
  }

  std::cout << "\nBook loss is the same-snapshot ask-to-bid round trip and "
               "excludes venue fees.\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: predictfun_btc_liquidity_report JSONL\n";
    return 1;
  }
  return run(argv[1]);
}
