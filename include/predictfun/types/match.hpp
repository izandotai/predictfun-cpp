#pragma once

#include "predictfun/types/evm.hpp"
#include "predictfun/types/exact_number.hpp"
#include "predictfun/types/market.hpp"
#include "predictfun/types/private_rest.hpp"

#include <optional>
#include <string>
#include <vector>

namespace predictfun {

struct MatchFee {
  ExactDecimal amount;
  std::string type;
};

struct MatchOrderLeg {
  std::string quote_type;
  ExactDecimal amount;
  ExactDecimal price;
  PrivateOutcome outcome;
  EvmAddress signer;
  std::optional<MatchFee> fee;
};

struct MatchEvent {
  Market market;
  MatchOrderLeg taker;
  ExactDecimal amount_filled;
  ExactDecimal price_executed;
  std::vector<MatchOrderLeg> makers;
  std::string transaction_hash;
  std::string executed_at;
};

struct MatchesPage {
  std::optional<std::string> cursor;
  std::vector<MatchEvent> matches;
};

} // namespace predictfun
