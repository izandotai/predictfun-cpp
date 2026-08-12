#pragma once

#include "predictfun/types/order.hpp"

#include <span>
#include <string>
#include <string_view>

namespace predictfun::order {

inline constexpr std::string_view protocol_name =
    "predict.fun CTF Exchange";
inline constexpr std::string_view protocol_version = "1";

[[nodiscard]] Result<Hash32> keccak256(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<Hash32> keccak256(std::string_view text);
[[nodiscard]] std::string to_hex(const Hash32 &hash);
[[nodiscard]] Result<Hash32> order_struct_hash(const UnsignedOrder &order);
[[nodiscard]] Result<Hash32>
domain_separator(ChainId chain_id, const EvmAddress &verifying_contract);
[[nodiscard]] Result<Hash32>
typed_data_digest(const UnsignedOrder &order, ChainId chain_id,
                  const EvmAddress &verifying_contract);

} // namespace predictfun::order
