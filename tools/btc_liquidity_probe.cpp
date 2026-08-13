#include "predictfun/analysis/liquidity.hpp"
#include "predictfun/net/http.hpp"
#include "predictfun/public_rest/client.hpp"

#include <boost/asio/io_context.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using predictfun::Result;
using predictfun::analysis::MarketBuyQuote;

struct Options {
  bool include_5m{true};
  bool include_15m{true};
  std::optional<std::string> jsonl_path;
};

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
    } else {
      std::cerr << "usage: predictfun_btc_liquidity_probe "
                   "[--5m|--15m|--both] [--jsonl FILE]\n";
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
  std::string name;
  std::string best_bid;
  std::string best_ask;
  std::vector<std::pair<std::string, MarketBuyQuote>> budgets;
};

class Probe : public std::enable_shared_from_this<Probe> {
public:
  Probe(predictfun::public_rest::PublicRestClient &client, Options options)
      : client_(client), options_(std::move(options)) {}

  void start() {
    if (options_.jsonl_path) {
      journal_.open(*options_.jsonl_path, std::ios::app);
      if (!journal_) {
        std::cerr << "cannot open JSONL output: " << *options_.jsonl_path
                  << '\n';
        failed_ = true;
        return;
      }
    }
    if (options_.include_5m)
      request_interval("5m", 300U);
    if (options_.include_15m)
      request_interval("15m", 900U);
  }

  [[nodiscard]] bool successful() const noexcept {
    return pending_ == 0U && !failed_;
  }

private:
  void request_interval(std::string label, std::uint64_t seconds) {
    ++pending_;
    const auto epoch = current_window_epoch(seconds);
    const auto slug = "btc-updown-" + label + '-' + std::to_string(epoch);
    client_.async_get_category(
        slug,
        predictfun::net::RequestContext::with_timeout(std::chrono::seconds{15}),
        [self = shared_from_this(), label,
         slug](Result<predictfun::Category> category) {
          self->on_category(label, slug, std::move(category));
        });
  }

  void on_category(const std::string &label, const std::string &slug,
                   Result<predictfun::Category> category) {
    if (!category) {
      report_error(label, "category", category.error());
      finish_one();
      return;
    }
    if (category.value().markets.empty()) {
      std::cerr << label << " current category contains no market: " << slug
                << '\n';
      failed_ = true;
      finish_one();
      return;
    }
    const auto market = category.value().markets.front();
    client_.async_get_orderbook(
        market.id, market.decimal_precision,
        predictfun::net::RequestContext::with_timeout(std::chrono::seconds{15}),
        [self = shared_from_this(), label, slug,
         market](Result<predictfun::Orderbook> book) mutable {
          self->on_orderbook(label, slug, std::move(market), std::move(book));
        });
  }

  void on_orderbook(const std::string &label, const std::string &slug,
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

    std::cout << "\nBTC " << label << " CURRENT\n"
              << "  category " << slug << '\n'
              << "  market   " << market.id.value << " | " << market.title
              << '\n'
              << "  book age timestamp " << book.value().update_timestamp_ms
              << " | fee " << market.fee_rate_bps << " bps\n";
    print_outcome(up.value());
    print_outcome(down.value());
    write_jsonl(label, slug, market, book.value(), up.value(), down.value());
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
      auto quote =
          predictfun::analysis::quote_market_buy_value(value.value(), asks);
      if (!quote)
        return quote.error();
      result.budgets.emplace_back(budget, std::move(quote.value()));
    }
    return result;
  }

  static void print_outcome(const OutcomeQuotes &outcome) {
    std::cout << "  " << outcome.name << " bid/ask " << outcome.best_bid << '/'
              << outcome.best_ask << '\n';
    for (const auto &[budget, quote] : outcome.budgets) {
      std::cout << "    $" << std::setw(2) << budget << "  "
                << (quote.complete ? "FULL    " : "PARTIAL ") << "spent $"
                << std::setw(7) << format_wei(quote.spent_value_wei, 4U)
                << "  shares " << std::setw(9)
                << format_wei(quote.shares_wei, 4U) << "  VWAP " << std::setw(7)
                << format_wei(quote.vwap_price_wei, 4U) << "  worst "
                << std::setw(7) << format_wei(quote.worst_price_wei, 4U)
                << "  levels " << quote.levels_consumed << '\n';
    }
  }

  void write_jsonl(const std::string &label, const std::string &slug,
                   const predictfun::Market &market,
                   const predictfun::Orderbook &book, const OutcomeQuotes &up,
                   const OutcomeQuotes &down) {
    if (!journal_)
      return;
    journal_ << "{\"schema\":\"predictfun.btc_liquidity.v1\""
             << ",\"captured_at_ms\":"
             << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count()
             << ",\"interval\":\"" << label << "\""
             << ",\"category_slug\":\"" << json_escape(slug) << "\""
             << ",\"market_id\":" << market.id.value
             << ",\"book_timestamp_ms\":" << book.update_timestamp_ms
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
    for (const auto &[budget, quote] : outcome.budgets) {
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
               << "\",\"levels\":" << quote.levels_consumed << '}';
    }
    journal_ << "]}";
  }

  void report_error(const std::string &label, std::string_view stage,
                    const predictfun::Error &error) {
    std::cerr << label << ' ' << stage
              << " failed: code=" << static_cast<int>(error.code)
              << " http=" << error.http_status << " field=" << error.field
              << " message=" << error.message << '\n';
    failed_ = true;
  }

  void finish_one() {
    if (pending_ > 0U)
      --pending_;
  }

  predictfun::public_rest::PublicRestClient &client_;
  Options options_;
  std::ofstream journal_;
  std::size_t pending_{0};
  bool failed_{false};
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
  auto probe = std::make_shared<Probe>(client, *options);
  probe->start();
  io.run();
  return probe->successful() ? EXIT_SUCCESS : EXIT_FAILURE;
}
