#pragma once

#include "predictfun/codec/public_rest.hpp"
#include "predictfun/types/private_rest.hpp"

#include <string_view>

namespace predictfun::codec {

[[nodiscard]] Result<Account>
decode_account_response(std::string_view json, const DecodeLimits &limits = {});
[[nodiscard]] Result<PositionsPage>
decode_positions_response(std::string_view json, const DecodeLimits &limits = {});
[[nodiscard]] Result<OrdersPage>
decode_orders_response(std::string_view json, const DecodeLimits &limits = {});
[[nodiscard]] Result<OrderRecord>
decode_order_response(std::string_view json, const DecodeLimits &limits = {});
[[nodiscard]] Result<ActivityPage>
decode_activity_response(std::string_view json, const DecodeLimits &limits = {});

} // namespace predictfun::codec
