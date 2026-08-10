#include "predictfun/net/http.hpp"
#include "predictfun/public_rest/client.hpp"

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

int main(int argc, char **argv) {
  const bool mainnet = argc > 1 && std::string{argv[1]} == "--mainnet";
  const char *api_key = std::getenv("PREDICT_FUN_API_KEY");
  if (mainnet && (api_key == nullptr || *api_key == '\0')) {
    std::cerr << "PREDICT_FUN_API_KEY is required for the mainnet probe\n";
    return EXIT_FAILURE;
  }

  boost::asio::io_context io;
  auto transport =
      std::make_shared<predictfun::net::BeastHttpTransport>(io.get_executor());
  predictfun::public_rest::ClientOptions options;
  options.environment = mainnet ? predictfun::Environment::bnb_mainnet
                                : predictfun::Environment::bnb_testnet;
  if (api_key != nullptr && *api_key != '\0') {
    options.api_key = [key = std::string{api_key}] { return key; };
  }
  predictfun::public_rest::PublicRestClient client(io.get_executor(), transport,
                                                   std::move(options));
  predictfun::public_rest::MarketsQuery query;
  query.first = 5U;
  query.status = "OPEN";
  int exit_code = EXIT_FAILURE;
  client.async_get_markets(
      std::move(query),
      predictfun::net::RequestContext::with_timeout(std::chrono::seconds{15}),
      [&client,
       &exit_code](predictfun::Result<predictfun::MarketsPage> result) {
        if (!result) {
          std::cerr << "read-only probe failed: code="
                    << static_cast<int>(result.error().code)
                    << " http=" << result.error().http_status
                    << " field=" << result.error().field << '\n';
          return;
        }
        std::cout << "read-only probe ok: markets="
                  << result.value().markets.size()
                  << " cursor=" << (result.value().cursor ? "present" : "none")
                  << '\n';
        for (const auto &market : result.value().markets) {
          std::cout << "market " << market.id.value << " precision="
                    << static_cast<unsigned>(market.decimal_precision)
                    << " outcomes=" << market.outcomes.size() << '\n';
        }
        if (result.value().markets.empty()) {
          std::cerr << "read-only probe returned no open markets\n";
          return;
        }
        const auto market_id = result.value().markets.front().id;
        const auto precision = result.value().markets.front().decimal_precision;
        client.async_get_market(
            market_id,
            predictfun::net::RequestContext::with_timeout(
                std::chrono::seconds{15}),
            [&client, &exit_code, market_id,
             precision](predictfun::Result<predictfun::Market> market) {
              if (!market) {
                std::cerr << "single-market probe failed: code="
                          << static_cast<int>(market.error().code)
                          << " http=" << market.error().http_status << '\n';
                return;
              }
              client.async_get_orderbook(
                  market_id, precision,
                  predictfun::net::RequestContext::with_timeout(
                      std::chrono::seconds{15}),
                  [&client,
                   &exit_code](predictfun::Result<predictfun::Orderbook> book) {
                    if (!book) {
                      std::cerr << "orderbook probe failed: code="
                                << static_cast<int>(book.error().code)
                                << " http=" << book.error().http_status << '\n';
                      return;
                    }
                    std::cout << "orderbook probe ok: bids="
                              << book.value().yes_bids.size()
                              << " asks=" << book.value().yes_asks.size()
                              << '\n';
                    predictfun::public_rest::CategoriesQuery categories;
                    categories.first = 2U;
                    client.async_get_categories(
                        std::move(categories),
                        predictfun::net::RequestContext::with_timeout(
                            std::chrono::seconds{15}),
                        [&exit_code](
                            predictfun::Result<predictfun::CategoriesPage>
                                page) {
                          if (!page) {
                            std::cerr << "categories probe failed: code="
                                      << static_cast<int>(page.error().code)
                                      << " http=" << page.error().http_status
                                      << '\n';
                            return;
                          }
                          std::cout << "categories probe ok: categories="
                                    << page.value().categories.size() << '\n';
                          exit_code = EXIT_SUCCESS;
                        });
                  });
            });
      });
  io.run();
  return exit_code;
}
