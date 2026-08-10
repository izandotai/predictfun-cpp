#include "predictfun/public_wss/client.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <variant>

namespace {

template <class T> bool parse_number(const char *text, T &value) {
  const std::string input{text};
  const auto [end, error] =
      std::from_chars(input.data(), input.data() + input.size(), value);
  return error == std::errc{} && end == input.data() + input.size();
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: predictfun_public_wss_probe MARKET_ID PRECISION\n";
    return EXIT_FAILURE;
  }
  std::uint64_t market_id = 0;
  unsigned precision = 0;
  if (!parse_number(argv[1], market_id) || market_id == 0U ||
      !parse_number(argv[2], precision) ||
      precision > predictfun::FixedDecimal::max_scale) {
    std::cerr << "market id or precision is invalid\n";
    return EXIT_FAILURE;
  }
  const char *api_key = std::getenv("PREDICT_FUN_API_KEY");
  if (api_key == nullptr || *api_key == '\0') {
    std::cerr << "PREDICT_FUN_API_KEY is required for the read-only WSS probe\n";
    return EXIT_FAILURE;
  }

  boost::asio::io_context io;
  predictfun::public_wss::ClientOptions options;
  options.api_key = [key = std::string{api_key}] { return key; };
  predictfun::public_wss::PublicWsClient client(io.get_executor(),
                                                std::move(options));
  boost::asio::steady_timer deadline(io.get_executor());
  deadline.expires_after(std::chrono::seconds{30});
  int exit_code = EXIT_FAILURE;
  deadline.async_wait([&](const boost::system::error_code &error) {
    if (!error) {
      std::cerr << "read-only WSS probe timed out before a fresh orderbook\n";
      client.stop();
    }
  });

  client.start(
      {predictfun::PublicTopic{predictfun::PublicTopicKind::orderbook,
                               market_id,
                               static_cast<std::uint8_t>(precision)}},
      [&] {
        while (auto event = client.try_pop_event()) {
          if (const auto *state =
                  std::get_if<predictfun::PublicWsStateEvent>(&*event)) {
            std::cout << "public WSS state="
                      << static_cast<int>(state->state)
                      << " generation=" << state->generation << '\n';
          } else if (const auto *data =
                         std::get_if<predictfun::PublicWsDataEvent>(&*event)) {
            const auto *book =
                std::get_if<predictfun::OrderbookMessage>(&data->message);
            if (book == nullptr)
              continue;
            std::cout << "read-only WSS probe ok: market="
                      << book->book.market_id.value
                      << " bids=" << book->book.yes_bids.size()
                      << " asks=" << book->book.yes_asks.size()
                      << " generation=" << data->generation << '\n';
            exit_code = EXIT_SUCCESS;
            deadline.cancel();
            client.stop();
          }
        }
      });
  io.run();
  return exit_code;
}
