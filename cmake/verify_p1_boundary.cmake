file(GLOB_RECURSE P1_PUBLIC_HEADERS
    "${ROOT}/include/predictfun/net/*.hpp"
    "${ROOT}/include/predictfun/public_rest/*.hpp"
)
set(P1_FILES
    ${P1_PUBLIC_HEADERS}
    "${ROOT}/src/http.cpp"
    "${ROOT}/src/rate_limiter.cpp"
    "${ROOT}/src/public_rest.cpp"
)

set(FORBIDDEN_PATTERNS
    "PRIVATE_KEY"
    "secp256k1"
    "sign_typed"
    "wallet"
    "submit_order"
    "create_order"
    "cancel_order"
    "place_order"
    "rpc"
    "getenv"
    "ifstream"
)

foreach(path IN LISTS P1_FILES)
    file(READ "${path}" content)
    string(TOLOWER "${content}" lower_content)
    foreach(pattern IN LISTS FORBIDDEN_PATTERNS)
        string(TOLOWER "${pattern}" lower_pattern)
        if(lower_content MATCHES "${lower_pattern}")
            message(FATAL_ERROR "P1 read-only boundary violation in ${path}: ${pattern}")
        endif()
    endforeach()
endforeach()

message(STATUS "P1 boundary verified: public REST has no signer, wallet, RPC, env-file, or order symbols")
