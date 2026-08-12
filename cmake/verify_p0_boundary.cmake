# P0 is the original public-data model/codec boundary.  Later milestones add
# separate auth/order types beneath the same include roots, so enumerate the
# P0 surface instead of accidentally treating every future SDK file as P0.
set(P0_FILES
    "${ROOT}/include/predictfun/types/error.hpp"
    "${ROOT}/include/predictfun/types/decimal.hpp"
    "${ROOT}/include/predictfun/types/market.hpp"
    "${ROOT}/include/predictfun/types/category.hpp"
    "${ROOT}/include/predictfun/types/orderbook.hpp"
    "${ROOT}/include/predictfun/types/timeseries.hpp"
    "${ROOT}/include/predictfun/types/websocket.hpp"
    "${ROOT}/include/predictfun/codec/public_rest.hpp"
    "${ROOT}/include/predictfun/codec/public_websocket.hpp"
    "${ROOT}/src/decimal.cpp"
    "${ROOT}/src/orderbook.cpp"
    "${ROOT}/src/codec.cpp"
    "${ROOT}/src/websocket_codec.cpp"
)

set(FORBIDDEN_PATTERNS
    "PREDICT_FUN_API_KEY"
    "Authorization:"
    "PRIVATE_KEY"
    "boost/asio"
    "boost/beast"
    "openssl"
    "secp256k1"
    "sign_typed"
    "wallet"
    "submit_order"
    "create_order"
    "cancel_order"
    "api[_-]?key"
    "jwt"
    "rpc"
)

foreach(path IN LISTS P0_FILES)
    file(READ "${path}" content)
    string(TOLOWER "${content}" lower_content)
    foreach(pattern IN LISTS FORBIDDEN_PATTERNS)
        string(TOLOWER "${pattern}" lower_pattern)
        if(lower_content MATCHES "${lower_pattern}")
            message(FATAL_ERROR "P0 boundary violation in ${path}: ${pattern}")
        endif()
    endforeach()
endforeach()

message(STATUS "P0 boundary verified: no network, credential, signer, wallet, RPC, or order symbols")
