file(GLOB_RECURSE P3_FILES
    "${ROOT}/include/predictfun/auth/*.hpp"
    "${ROOT}/include/predictfun/private_rest/*.hpp"
    "${ROOT}/include/predictfun/private_wss/*.hpp"
    "${ROOT}/include/predictfun/codec/auth.hpp"
    "${ROOT}/include/predictfun/types/auth.hpp"
    "${ROOT}/include/predictfun/types/evm.hpp"
    "${ROOT}/include/predictfun/types/secret.hpp"
    "${ROOT}/src/auth.cpp"
    "${ROOT}/src/auth_codec.cpp"
    "${ROOT}/src/auth_types.cpp"
    "${ROOT}/src/private_rest.cpp"
    "${ROOT}/src/private_wss.cpp"
    "${ROOT}/src/private_codec.cpp"
    "${ROOT}/src/private_websocket_codec.cpp"
    "${ROOT}/src/evm.cpp"
    "${ROOT}/src/secret.cpp"
)

foreach(FILE_PATH IN LISTS P3_FILES)
    file(READ "${FILE_PATH}" CONTENT)
    string(TOLOWER "${CONTENT}" LOWER_CONTENT)
    foreach(FORBIDDEN
        "private_key" "private key" "mnemonic" "seed phrase"
        "getenv" "ifstream" "eth_sendrawtransaction"
        "submit_order" "place_order" "cancel_order" "create_order")
        string(FIND "${LOWER_CONTENT}" "${FORBIDDEN}" FOUND)
        if(NOT FOUND EQUAL -1)
            message(FATAL_ERROR
                "P3 auth boundary violation in ${FILE_PATH}: ${FORBIDDEN}")
        endif()
    endforeach()
endforeach()

foreach(SOURCE_PATH
    "${ROOT}/src/auth.cpp"
    "${ROOT}/src/private_rest.cpp")
    file(READ "${SOURCE_PATH}" AUTH_SOURCE)
    string(TOLOWER "${AUTH_SOURCE}" AUTH_LOWER)
    foreach(FORBIDDEN "apikey=" "api_key=" "x-api-key=" "?token=" "?jwt=")
        string(FIND "${AUTH_LOWER}" "${FORBIDDEN}" FOUND)
        if(NOT FOUND EQUAL -1)
            message(FATAL_ERROR
                "P3 credentials must never be placed in the HTTP target: ${FORBIDDEN}")
        endif()
    endforeach()
endforeach()

message(STATUS
    "P3 auth boundary verified: no private-key ownership, env-file reads, RPC, or mutation path")
