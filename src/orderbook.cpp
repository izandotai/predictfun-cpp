#include "predictfun/types/orderbook.hpp"

namespace predictfun {

NoOrderbookView derive_no_book(const Orderbook &yes_book) {
  NoOrderbookView result;
  result.market_id = yes_book.market_id;
  result.update_timestamp_ms = yes_book.update_timestamp_ms;
  result.no_bids.reserve(yes_book.yes_asks.size());
  result.no_asks.reserve(yes_book.yes_bids.size());

  for (const auto &ask : yes_book.yes_asks) {
    result.no_bids.push_back(PriceLevel{ask.price.complement(), ask.quantity});
  }
  for (const auto &bid : yes_book.yes_bids) {
    result.no_asks.push_back(PriceLevel{bid.price.complement(), bid.quantity});
  }
  return result;
}

} // namespace predictfun
