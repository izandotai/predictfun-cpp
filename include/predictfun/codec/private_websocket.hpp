#pragma once

#include "predictfun/codec/public_rest.hpp"
#include "predictfun/types/private_websocket.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace predictfun::codec {

// The JWT is present only because Predict's protocol requires it in the
// subscription topic. Callers must treat the returned frame as sensitive.
[[nodiscard]] Result<std::string>
encode_wallet_subscribe_request(std::uint64_t request_id,
                                std::string_view jwt);

[[nodiscard]] Result<PrivateWsMessage>
decode_private_ws_frame(std::string_view json,
                        const DecodeLimits &limits = {});

} // namespace predictfun::codec
