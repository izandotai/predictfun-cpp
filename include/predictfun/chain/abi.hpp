#pragma once

#include "predictfun/types/chain.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace predictfun::chain::abi {

[[nodiscard]] Result<std::vector<std::uint8_t>> decode_hex(std::string_view hex);
[[nodiscard]] std::string encode_hex(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<Uint256> decode_quantity(std::string_view hex);
[[nodiscard]] Result<Uint256> decode_word_uint256(std::string_view hex);
[[nodiscard]] Result<bool> decode_word_bool(std::string_view hex);
[[nodiscard]] Result<Bytes32> decode_bytes32(std::string_view hex);
[[nodiscard]] Result<std::string> encode_quantity(const Uint256 &value);
[[nodiscard]] Result<std::array<std::uint8_t, 32>> word(const Uint256 &value);
[[nodiscard]] std::array<std::uint8_t, 32> word(const EvmAddress &value);
[[nodiscard]] std::array<std::uint8_t, 32> word(bool value);
[[nodiscard]] std::array<std::uint8_t, 32> word(const Bytes32 &value);

[[nodiscard]] Result<std::string>
encode_call(std::string_view signature,
            std::span<const std::array<std::uint8_t, 32>> words);
[[nodiscard]] Result<std::string>
encode_call_with_uint_array(
    std::string_view signature,
    std::span<const std::array<std::uint8_t, 32>> words_before,
    std::span<const Uint256> values,
    std::span<const std::array<std::uint8_t, 32>> words_after = {});
[[nodiscard]] Result<std::string>
encode_call_with_bytes(
    std::string_view signature,
    std::span<const std::array<std::uint8_t, 32>> words_before,
    std::span<const std::uint8_t> value);

[[nodiscard]] Result<std::string> erc20_balance_of(const EvmAddress &owner);
[[nodiscard]] Result<std::string> erc20_allowance(const EvmAddress &owner,
                                                 const EvmAddress &spender);
[[nodiscard]] Result<std::string> erc20_approve(const EvmAddress &spender,
                                               const Uint256 &amount);
[[nodiscard]] Result<std::string>
erc1155_balance_of(const EvmAddress &owner, const Uint256 &token_id);
[[nodiscard]] Result<std::string>
erc1155_is_approved_for_all(const EvmAddress &owner,
                            const EvmAddress &operator_address);
[[nodiscard]] Result<std::string>
erc1155_set_approval_for_all(const EvmAddress &operator_address, bool approved);

} // namespace predictfun::chain::abi
