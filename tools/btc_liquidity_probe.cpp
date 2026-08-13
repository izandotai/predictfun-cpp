#include "predictfun/analysis/liquidity.hpp"
#include "predictfun/net/http.hpp"
#include "predictfun/public_rest/client.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using predictfun::Result;
struct Options {
  bool include_5m{true};
  bool include_15m{true};
  std::optional<std::string> jsonl_path;
  std::optional<std::string> status_path;
  std::size_t samples{1U};
  std::chrono::milliseconds interval{5'000};
  bool quiet{false};
};

std::optional<std::uint64_t> unsigned_argument(std::string_view value) {
  if (value.empty())
    return std::nullopt;
  std::uint64_t result = 0;
  for (const auto ch : value) {
    if (ch < '0' || ch > '9')
      return std::nullopt;
    const auto digit = static_cast<std::uint64_t>(ch - '0');
    if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
      return std::nullopt;
    result = result * 10U + digit;
  }
  return result;
}

std::optional<Options> parse_options(int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--5m") {
      options.include_5m = true;
      options.include_15m = false;
    } else if (argument == "--15m") {
      options.include_5m = false;
      options.include_15m = true;
    } else if (argument == "--both") {
      options.include_5m = true;
      options.include_15m = true;
    } else if (argument == "--jsonl" && index + 1 < argc) {
      options.jsonl_path = argv[++index];
    } else if (argument == "--status-json" && index + 1 < argc) {
      options.status_path = argv[++index];
    } else if (argument == "--samples" && index + 1 < argc) {
      const auto value = unsigned_argument(argv[++index]);
      if (!value || *value > std::numeric_limits<std::size_t>::max())
        return std::nullopt;
      options.samples = static_cast<std::size_t>(*value);
    } else if (argument == "--interval-ms" && index + 1 < argc) {
      const auto value = unsigned_argument(argv[++index]);
      if (!value || *value < 1'000U ||
          *value > static_cast<std::uint64_t>(
                       std::chrono::milliseconds::max().count()))
        return std::nullopt;
      options.interval = std::chrono::milliseconds{*value};
    } else if (argument == "--quiet") {
      options.quiet = true;
    } else {
      std::cerr << "usage: predictfun_btc_liquidity_probe "
                   "[--5m|--15m|--both] [--jsonl FILE] "
                   "[--status-json FILE] [--samples N] [--interval-ms N] "
                   "[--quiet]\n"
                   "       samples=0 runs until interrupted; interval >=1000"
                   " ms\n";
      return std::nullopt;
    }
  }
  return options;
}

std::uint64_t current_window_epoch(std::uint64_t interval_seconds) {
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  const auto unsigned_now = static_cast<std::uint64_t>(std::max(now, 0LL));
  return (unsigned_now / interval_seconds) * interval_seconds;
}

std::string format_wei(const predictfun::Uint256 &value,
                       std::size_t fractional_digits = 4U) {
  std::string digits = value.to_string();
  constexpr std::size_t scale = 18U;
  if (digits.size() <= scale)
    digits.insert(0, scale + 1U - digits.size(), '0');
  const auto point = digits.size() - scale;
  std::string whole = digits.substr(0, point);
  std::string fraction = digits.substr(point);
  if (fractional_digits < fraction.size() &&
      fraction[fractional_digits] >= '5') {
    std::size_t cursor = fractional_digits;
    while (cursor > 0U && fraction[cursor - 1U] == '9') {
      fraction[cursor - 1U] = '0';
      --cursor;
    }
    if (cursor > 0U) {
      ++fraction[cursor - 1U];
    } else {
      cursor = whole.size();
      while (cursor > 0U && whole[cursor - 1U] == '9') {
        whole[cursor - 1U] = '0';
        --cursor;
      }
      if (cursor > 0U)
        ++whole[cursor - 1U];
      else
        whole.insert(whole.begin(), '1');
    }
  }
  if (fractional_digits < fraction.size())
    fraction.resize(fractional_digits);
  while (!fraction.empty() && fraction.back() == '0')
    fraction.pop_back();
  return fraction.empty() ? whole : whole + "." + fraction;
}

std::string json_escape(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const auto ch : value) {
    if (ch == '\\' || ch == '"')
      result.push_back('\\');
    result.push_back(ch);
  }
  return result;
}

struct OutcomeQuotes {
  struct Budget {
    std::string value;
    predictfun::analysis::ImmediateRoundTripQuote round_trip;
  };

  std::string name;
  std::string best_bid;
  std::string best_ask;
  std::vector<Budget> budgets;
};

class Probe : public std::enable_shared_from_this<Probe> {
public:
  Probe(predictfun::public_rest::PublicRestClient &client,
        boost::asio::io_context &io, Options options)
      : client_(client), options_(std::move(options)), timer_(io) {}

  void start() {
    if (options_.jsonl_path) {
      if (!ensure_parent_directory(*options_.jsonl_path)) {
        fatal_error_ = "cannot create JSONL output directory";
        write_status("failed");
        return;
      }
      journal_.open(*options_.jsonl_path, std::ios::app);
      if (!journal_) {
        std::cerr << "cannot open JSONL output: " << *options_.jsonl_path
                  << '\n';
        fatal_error_ = "cannot open JSONL output";
        write_status("failed");
        return;
      }
    }
    started_at_ms_ = now_ms();
    write_status("starting");
    request_round();
  }

  [[nodiscard]] bool successful() const noexcept {
    return fatal_error_.empty() && successful_intervals_ > 0U &&
           (options_.samples == 0U || errors_ == 0U);
  }

  [[nodiscard]] bool initialized() const noexcept {
    return fatal_error_.empty();
  }

  void stop() {
    stopping_ = true;
    timer_.cancel();
    write_status("stopped");
  }

private:
  static std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  static bool ensure_parent_directory(const std::string &path) {
    const std::filesystem::path output{path};
    if (!output.has_parent_path())
      return true;
    std::error_code error;
    std::filesystem::create_directories(output.parent_path(), error);
    return !error;
  }

  void request_round() {
    if (stopping_)
      return;
    waiting_ = false;
    ++round_;
    current_round_successes_ = 0U;
    current_round_errors_ = 0U;
    last_round_at_ms_ = now_ms();
    if (options_.include_5m)
      request_interval("5m", 300U);
    if (options_.include_15m)
      request_interval("15m", 900U);
  }

  void request_interval(std::string label, std::uint64_t seconds) {
    ++pending_;
    const auto epoch = current_window_epoch(seconds);
    const auto slug = "btc-updown-" + label + '-' + std::to_string(epoch);
    client_.async_get_category(
        slug,
        predictfun::net::RequestContext::with_timeout(std::chrono::seconds{15}),
        [self = shared_from_this(), label, slug, epoch,
         seconds](Result<predictfun::Category> category) {
          self->on_category(label, slug, epoch, seconds, std::move(category));
        });
  }

  void on_category(const std::string &label, const std::string &slug,
                   std::uint64_t window_start_epoch,
                   std::uint64_t window_duration_seconds,
                   Result<predictfun::Category> category) {
    if (!category) {
      report_error(label, "category", category.error());
      finish_one();
      return;
    }
    if (category.value().markets.empty()) {
      std::cerr << label << " current category contains no market: " << slug
                << '\n';
      ++errors_;
      ++current_round_errors_;
      last_error_ = label + " category contains no market: " + slug;
      finish_one();
      return;
    }
    const auto market = category.value().markets.front();
    client_.async_get_orderbook(
        market.id, market.decimal_precision,
        predictfun::net::RequestContext::with_timeout(std::chrono::seconds{15}),
        [self = shared_from_this(), label, slug, window_start_epoch,
         window_duration_seconds,
         market](Result<predictfun::Orderbook> book) mutable {
          self->on_orderbook(label, slug, window_start_epoch,
                             window_duration_seconds, std::move(market),
                             std::move(book));
        });
  }

  void on_orderbook(const std::string &label, const std::string &slug,
                    std::uint64_t window_start_epoch,
                    std::uint64_t window_duration_seconds,
                    predictfun::Market market,
                    Result<predictfun::Orderbook> book) {
    if (!book) {
      report_error(label, "orderbook", book.error());
      finish_one();
      return;
    }

    const auto no_book = predictfun::derive_no_book(book.value());
    const auto up =
        quote_outcome("UP", book.value().yes_bids, book.value().yes_asks);
    const auto down = quote_outcome("DOWN", no_book.no_bids, no_book.no_asks);
    if (!up || !down) {
      const auto &error = !up ? up.error() : down.error();
      report_error(label, "liquidity math", error);
      finish_one();
      return;
    }

    if (!options_.quiet) {
      std::cout << "\nBTC " << label << " CURRENT\n"
                << "  category " << slug << '\n'
                << "  market   " << market.id.value << " | " << market.title
                << '\n'
                << "  book age timestamp " << book.value().update_timestamp_ms
                << " | fee " << market.fee_rate_bps << " bps\n";
      print_outcome(up.value());
      print_outcome(down.value());
    }
    write_jsonl(label, slug, window_start_epoch, window_duration_seconds,
                market, book.value(), up.value(), down.value());
    ++successful_intervals_;
    ++current_round_successes_;
    last_success_at_ms_ = now_ms();
    finish_one();
  }

  Result<OutcomeQuotes>
  quote_outcome(std::string name,
                const std::vector<predictfun::PriceLevel> &bids,
                const std::vector<predictfun::PriceLevel> &asks) const {
    OutcomeQuotes result;
    result.name = std::move(name);
    result.best_bid = bids.empty() ? "--" : bids.front().price.to_string();
    result.best_ask = asks.empty() ? "--" : asks.front().price.to_string();
    for (const auto budget : {"10", "25", "50"}) {
      auto value = predictfun::Uint256::parse(std::string{budget} +
                                              "000000000000000000");
      if (!value)
        return value.error();
      auto quote = predictfun::analysis::quote_immediate_round_trip(
          value.value(), asks, bids);
      if (!quote)
        return quote.error();
      result.budgets.push_back(
          OutcomeQuotes::Budget{budget, std::move(quote.value())});
    }
    return result;
  }

  static void print_outcome(const OutcomeQuotes &outcome) {
    std::cout << "  " << outcome.name << " bid/ask " << outcome.best_bid << '/'
              << outcome.best_ask << '\n';
    for (const auto &budget_quote : outcome.budgets) {
      const auto &budget = budget_quote.value;
      const auto &round_trip = budget_quote.round_trip;
      const auto &quote = round_trip.buy;
      std::cout << "    $" << std::setw(2) << budget << "  "
                << (quote.complete ? "FULL    " : "PARTIAL ") << "spent $"
                << std::setw(7) << format_wei(quote.spent_value_wei, 4U)
                << "  shares " << std::setw(9)
                << format_wei(quote.shares_wei, 4U) << "  VWAP " << std::setw(7)
                << format_wei(quote.vwap_price_wei, 4U) << "  worst "
                << std::setw(7) << format_wei(quote.worst_price_wei, 4U)
                << "  levels " << quote.levels_consumed << "  RT "
                << (round_trip.complete ? "FULL" : "PART") << " -$"
                << format_wei(round_trip.book_loss_value_wei, 4U) << '\n';
    }
  }

  void write_jsonl(const std::string &label, const std::string &slug,
                   std::uint64_t window_start_epoch,
                   std::uint64_t window_duration_seconds,
                   const predictfun::Market &market,
                   const predictfun::Orderbook &book, const OutcomeQuotes &up,
                   const OutcomeQuotes &down) {
    if (!journal_)
      return;
    const auto captured_at_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    const auto captured_epoch =
        static_cast<std::uint64_t>(std::max<std::int64_t>(
            captured_at_ms / 1'000, static_cast<std::int64_t>(0)));
    const auto window_end_epoch = window_start_epoch + window_duration_seconds;
    const auto seconds_remaining = captured_epoch < window_end_epoch
                                       ? window_end_epoch - captured_epoch
                                       : 0U;
    const auto seconds_elapsed =
        captured_epoch > window_start_epoch
            ? std::min(captured_epoch - window_start_epoch,
                       window_duration_seconds)
            : 0U;
    journal_ << "{\"schema\":\"predictfun.btc_liquidity.v2\""
             << ",\"captured_at_ms\":" << captured_at_ms << ",\"interval\":\""
             << label << "\""
             << ",\"category_slug\":\"" << json_escape(slug) << "\""
             << ",\"window_start_epoch\":" << window_start_epoch
             << ",\"window_duration_seconds\":" << window_duration_seconds
             << ",\"seconds_elapsed\":" << seconds_elapsed
             << ",\"seconds_remaining\":" << seconds_remaining
             << ",\"market_id\":" << market.id.value
             << ",\"book_timestamp_ms\":" << book.update_timestamp_ms
             << ",\"sample_round\":" << round_
             << ",\"fee_rate_bps\":" << market.fee_rate_bps
             << ",\"outcomes\":[";
    write_json_outcome(up);
    journal_ << ',';
    write_json_outcome(down);
    journal_ << "]}\n";
    journal_.flush();
  }

  void write_json_outcome(const OutcomeQuotes &outcome) {
    journal_ << "{\"name\":\"" << outcome.name << "\""
             << ",\"best_bid\":\"" << outcome.best_bid << "\""
             << ",\"best_ask\":\"" << outcome.best_ask << "\",\"quotes\":[";
    bool first = true;
    for (const auto &budget_quote : outcome.budgets) {
      const auto &budget = budget_quote.value;
      const auto &round_trip = budget_quote.round_trip;
      const auto &quote = round_trip.buy;
      if (!first)
        journal_ << ',';
      first = false;
      journal_ << "{\"budget_pusd\":\"" << budget << "\""
               << ",\"complete\":" << (quote.complete ? "true" : "false")
               << ",\"spent_pusd\":\"" << format_wei(quote.spent_value_wei, 8U)
               << "\""
               << ",\"shares\":\"" << format_wei(quote.shares_wei, 8U)
               << "\",\"vwap\":\"" << format_wei(quote.vwap_price_wei, 8U)
               << "\",\"worst_price\":\""
               << format_wei(quote.worst_price_wei, 8U)
               << "\",\"levels\":" << quote.levels_consumed
               << ",\"round_trip_complete\":"
               << (round_trip.complete ? "true" : "false")
               << ",\"round_trip_recovered_pusd\":\""
               << format_wei(round_trip.recovered_value_wei, 8U)
               << "\",\"round_trip_book_loss_pusd\":\""
               << format_wei(round_trip.book_loss_value_wei, 8U) << "\"}";
    }
    journal_ << "]}";
  }

  void report_error(const std::string &label, std::string_view stage,
                    const predictfun::Error &error) {
    std::cerr << label << ' ' << stage
              << " failed: code=" << static_cast<int>(error.code)
              << " http=" << error.http_status << " field=" << error.field
              << " message=" << error.message << '\n';
    ++errors_;
    ++current_round_errors_;
    last_error_ = label + " " + std::string{stage} + ": " + error.message;
  }

  void finish_one() {
    if (pending_ > 0U)
      --pending_;
    if (pending_ != 0U)
      return;
    write_status(stopping_ ? "stopped" : "running");
    if (stopping_)
      return;
    if (options_.samples != 0U && round_ >= options_.samples)
      return;
    waiting_ = true;
    timer_.expires_after(options_.interval);
    timer_.async_wait(
        [self = shared_from_this()](const boost::system::error_code &error) {
          if (!error)
            self->request_round();
        });
  }

  void write_status(std::string_view state) const {
    if (!options_.status_path)
      return;
    if (!ensure_parent_directory(*options_.status_path))
      return;
    const std::filesystem::path destination{*options_.status_path};
    auto temporary = destination;
    temporary += ".tmp";
    std::ofstream output{temporary, std::ios::trunc};
    if (!output)
      return;
    output << "{\"schema\":\"predictfun.btc_liquidity.status.v1\""
           << ",\"state\":\"" << json_escape(state) << "\""
           << ",\"pid\":"
#ifdef _WIN32
           << static_cast<unsigned long>(::GetCurrentProcessId())
#else
           << static_cast<long>(::getpid())
#endif
           << ",\"started_at_ms\":" << started_at_ms_
           << ",\"updated_at_ms\":" << now_ms()
           << ",\"last_round_at_ms\":" << last_round_at_ms_
           << ",\"last_success_at_ms\":" << last_success_at_ms_
           << ",\"round\":" << round_
           << ",\"successful_intervals\":" << successful_intervals_
           << ",\"errors\":" << errors_
           << ",\"round_successes\":" << current_round_successes_
           << ",\"round_errors\":" << current_round_errors_
           << ",\"last_error\":\"" << json_escape(last_error_) << "\""
           << ",\"fatal_error\":\"" << json_escape(fatal_error_) << "\"}\n";
    output.close();
    if (!output)
      return;
    std::error_code error;
#ifdef _WIN32
    const auto replaced = ::MoveFileExW(
        temporary.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (replaced == 0)
      std::filesystem::remove(temporary, error);
#else
    std::filesystem::rename(temporary, destination, error);
#endif
  }

  predictfun::public_rest::PublicRestClient &client_;
  Options options_;
  boost::asio::steady_timer timer_;
  std::ofstream journal_;
  std::size_t pending_{0};
  std::size_t round_{0};
  std::size_t successful_intervals_{0};
  std::size_t errors_{0};
  std::size_t current_round_successes_{0};
  std::size_t current_round_errors_{0};
  std::int64_t started_at_ms_{0};
  std::int64_t last_round_at_ms_{0};
  std::int64_t last_success_at_ms_{0};
  std::string last_error_;
  std::string fatal_error_;
  bool waiting_{false};
  bool stopping_{false};
};

} // namespace

int main(int argc, char **argv) {
  const auto options = parse_options(argc, argv);
  if (!options)
    return EXIT_FAILURE;
  const char *api_key = std::getenv("PREDICT_FUN_API_KEY");
  if (api_key == nullptr || *api_key == '\0') {
    std::cerr << "PREDICT_FUN_API_KEY is required; the key is read only from "
                 "the process environment\n";
    return EXIT_FAILURE;
  }

  boost::asio::io_context io;
  auto transport =
      std::make_shared<predictfun::net::BeastHttpTransport>(io.get_executor());
  predictfun::public_rest::ClientOptions client_options;
  client_options.environment = predictfun::Environment::bnb_mainnet;
  client_options.api_key = [key = std::string{api_key}] { return key; };
  predictfun::public_rest::PublicRestClient client(io.get_executor(), transport,
                                                   std::move(client_options));
  auto probe = std::make_shared<Probe>(client, io, *options);
  probe->start();
  if (!probe->initialized())
    return EXIT_FAILURE;
  std::unique_ptr<boost::asio::signal_set> signals;
  if (options->samples == 0U) {
    signals = std::make_unique<boost::asio::signal_set>(io, SIGINT, SIGTERM);
    signals->async_wait(
        [probe, &io](const boost::system::error_code &error, int) {
          if (!error) {
            probe->stop();
            io.stop();
          }
        });
  }
  io.run();
  return probe->successful() ? EXIT_SUCCESS : EXIT_FAILURE;
}
