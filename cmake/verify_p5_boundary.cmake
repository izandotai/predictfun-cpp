file(READ "${ROOT}/src/trading.cpp" TRADING_SOURCE)
string(TOLOWER "${TRADING_SOURCE}" LOWER_TRADING_SOURCE)

foreach(REQUIRED
    "${ROOT}/include/predictfun/lifecycle/tracker.hpp"
    "${ROOT}/include/predictfun/types/lifecycle.hpp"
    "${ROOT}/include/predictfun/types/match.hpp"
    "${ROOT}/src/lifecycle.cpp")
    if(NOT EXISTS "${REQUIRED}")
        message(FATAL_ERROR "P5 reconciliation surface is missing: ${REQUIRED}")
    endif()
endforeach()

foreach(FORBIDDEN
    "getenv" "ifstream" ".env.local" "dotenv" "private_key" "private key"
    "max_post_retries" "retry_post" "eth_sendrawtransaction")
    string(FIND "${LOWER_TRADING_SOURCE}" "${FORBIDDEN}" FOUND)
    if(NOT FOUND EQUAL -1)
        message(FATAL_ERROR
            "P5 trading boundary violation in src/trading.cpp: ${FORBIDDEN}")
    endif()
endforeach()

string(REGEX MATCHALL "async_post" POST_CALLS "${LOWER_TRADING_SOURCE}")
list(LENGTH POST_CALLS POST_CALL_COUNT)
if(NOT POST_CALL_COUNT EQUAL 1)
    message(FATAL_ERROR
        "P5 mutation transport must have one centralized async_post path; found ${POST_CALL_COUNT}")
endif()

message(STATUS
    "P5 trading boundary verified: no secrets, RPC, or automatic mutation retry path")
