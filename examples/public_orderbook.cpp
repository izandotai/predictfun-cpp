#include "predictfun/net/http.hpp"
#include "predictfun/public_rest/client.hpp"

#include <boost/asio/io_context.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

template <class T> bool parse_unsigned(std::string_view text, T &value) {
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size();
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: predictfun_example_public_orderbook "
                 "MARKET_ID DECIMAL_PRECISION\n"
                 "This example is read-only and uses BNB testnet.\n";
    return EXIT_FAILURE;
  }

  std::uint64_t market_id = 0U;
  std::uint16_t precision = 0U;
  if (!parse_unsigned(std::string_view{argv[1]}, market_id) ||
      !parse_unsigned(std::string_view{argv[2]}, precision) ||
      market_id == 0U || precision > predictfun::FixedDecimal::max_scale) {
    std::cerr << "invalid market id or decimal precision\n";
    return EXIT_FAILURE;
  }

  boost::asio::io_context io;
  auto transport =
      std::make_shared<predictfun::net::BeastHttpTransport>(io.get_executor());
  predictfun::public_rest::ClientOptions options;
  options.environment = predictfun::Environment::bnb_testnet;
  predictfun::public_rest::PublicRestClient client(
      io.get_executor(), std::move(transport), std::move(options));

  int exit_code = EXIT_FAILURE;
  client.async_get_orderbook(
      predictfun::MarketId{market_id}, static_cast<std::uint8_t>(precision),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{15}),
      [&exit_code](predictfun::Result<predictfun::Orderbook> result) {
        if (!result) {
          std::cerr << "orderbook read failed: code="
                    << static_cast<int>(result.error().code)
                    << " http=" << result.error().http_status << '\n';
          return;
        }
        const auto no_book = predictfun::derive_no_book(result.value());
        std::cout << "market=" << result.value().market_id.value
                  << " yes_bids=" << result.value().yes_bids.size()
                  << " yes_asks=" << result.value().yes_asks.size()
                  << " no_bids=" << no_book.no_bids.size()
                  << " no_asks=" << no_book.no_asks.size() << '\n';
        exit_code = EXIT_SUCCESS;
      });
  io.run();
  return exit_code;
}
