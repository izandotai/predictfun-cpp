file(GLOB_RECURSE P2_FILES
    "${ROOT}/include/predictfun/public_wss/*"
    "${ROOT}/include/predictfun/net/websocket.hpp"
    "${ROOT}/include/predictfun/codec/public_websocket.hpp"
    "${ROOT}/include/predictfun/types/websocket.hpp"
    "${ROOT}/src/public_wss.cpp"
    "${ROOT}/src/websocket.cpp"
    "${ROOT}/src/websocket_codec.cpp"
)

foreach(FILE_PATH IN LISTS P2_FILES)
    file(READ "${FILE_PATH}" CONTENT)
    string(TOLOWER "${CONTENT}" LOWER_CONTENT)
    foreach(FORBIDDEN
        "private key" "private_key" "mnemonic" "seed phrase"
        "wallet" "signer" "eth_sendrawtransaction"
        "submit_order" "place_order" "cancel_order")
        string(FIND "${LOWER_CONTENT}" "${FORBIDDEN}" FOUND)
        if(NOT FOUND EQUAL -1)
            message(FATAL_ERROR "P2 public WSS boundary violation in ${FILE_PATH}: ${FORBIDDEN}")
        endif()
    endforeach()
endforeach()

file(READ "${ROOT}/src/public_wss.cpp" CLIENT_SOURCE)
string(TOLOWER "${CLIENT_SOURCE}" CLIENT_LOWER)
foreach(FORBIDDEN "apikey=" "api_key=" "x-api-key=" "?token=" "?jwt=")
    string(FIND "${CLIENT_LOWER}" "${FORBIDDEN}" FOUND)
    if(NOT FOUND EQUAL -1)
        message(FATAL_ERROR "P2 credentials must never be placed in the WebSocket target: ${FORBIDDEN}")
    endif()
endforeach()

message(STATUS "P2 boundary verification passed")
