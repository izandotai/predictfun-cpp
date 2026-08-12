#pragma once

#include "predictfun/types/chain.hpp"

#include <vector>

namespace predictfun::chain {

[[nodiscard]] Result<std::vector<ApprovalStep>>
approval_steps(ChainId chain_id, const ApprovalScope &scope);

[[nodiscard]] Result<std::vector<ApprovalStep>>
all_approval_steps(ChainId chain_id,
                   std::optional<bool> is_yield_bearing = std::nullopt);

[[nodiscard]] Result<UnsignedTransaction>
approval_transaction(const ApprovalStep &step,
                     std::optional<Uint256> amount = std::nullopt,
                     bool approved = true);

} // namespace predictfun::chain
