#pragma once

#include "predictfun/codec/public_rest.hpp"
#include "predictfun/types/trading.hpp"

#include <string>
#include <string_view>

namespace predictfun::codec {

[[nodiscard]] Result<std::string>
encode_create_order_request(const CreateOrderRequest &request);
[[nodiscard]] Result<std::string>
encode_remove_order_ids_request(const std::vector<std::string> &ids);
[[nodiscard]] Result<std::string>
encode_remove_order_hashes_request(const std::vector<std::string> &hashes);

[[nodiscard]] Result<CreateOrderReceipt>
decode_create_order_response(std::string_view json,
                             const DecodeLimits &limits = {});
[[nodiscard]] Result<RemoveOrdersReceipt>
decode_remove_orders_response(std::string_view json,
                              const DecodeLimits &limits = {});
// The remove-by-hash endpoint currently documents an empty `{}` success body,
// while some deployments return the richer removed/noop envelope.
[[nodiscard]] Result<RemoveOrdersReceipt>
decode_remove_order_hashes_response(std::string_view json,
                                    const DecodeLimits &limits = {});

} // namespace predictfun::codec
