#pragma once

#include "predictfun/codec/public_rest.hpp"
#include "predictfun/types/websocket.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace predictfun::codec {

using PrecisionResolver =
    std::function<std::optional<std::uint8_t>(MarketId)>;

[[nodiscard]] Result<std::string> public_topic_name(const PublicTopic &topic);

[[nodiscard]] Result<std::string>
encode_subscribe_request(std::uint64_t request_id, const PublicTopic &topic);

[[nodiscard]] Result<std::string>
encode_unsubscribe_request(std::uint64_t request_id, const PublicTopic &topic);

[[nodiscard]] Result<std::string>
encode_heartbeat_response(std::int64_t timestamp_ms);

[[nodiscard]] Result<PublicWsMessage>
decode_public_ws_frame(std::string_view json,
                       const PrecisionResolver &precision_for_market,
                       const DecodeLimits &limits = {});

} // namespace predictfun::codec
