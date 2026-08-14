foreach(REQUIRED
    "${ROOT}/include/predictfun/execution/session.hpp"
    "${ROOT}/src/execution_session.cpp"
    "${ROOT}/tests/execution_tests.cpp")
    if(NOT EXISTS "${REQUIRED}")
        message(FATAL_ERROR "Durable execution surface is missing: ${REQUIRED}")
    endif()
endforeach()

file(READ "${ROOT}/src/execution_session.cpp" EXECUTION_SOURCE)
string(TOLOWER "${EXECUTION_SOURCE}" LOWER_EXECUTION_SOURCE)

foreach(FORBIDDEN
    "getenv" ".env.local" "dotenv" "private_key" "private key"
    "eth_sendrawtransaction")
    string(FIND "${LOWER_EXECUTION_SOURCE}" "${FORBIDDEN}" FOUND)
    if(NOT FOUND EQUAL -1)
        message(FATAL_ERROR
            "Durable execution boundary violation: ${FORBIDDEN}")
    endif()
endforeach()

string(REGEX MATCHALL "async_create_order" CREATE_CALLS
       "${LOWER_EXECUTION_SOURCE}")
list(LENGTH CREATE_CALLS CREATE_CALL_COUNT)
if(NOT CREATE_CALL_COUNT EQUAL 1)
    message(FATAL_ERROR
        "Durable execution must have one mutation dispatch path; found ${CREATE_CALL_COUNT}")
endif()

string(FIND "${LOWER_EXECUTION_SOURCE}" "begin_submission" JOURNAL_POSITION)
string(FIND "${LOWER_EXECUTION_SOURCE}" "async_create_order" DISPATCH_POSITION)
if(JOURNAL_POSITION EQUAL -1 OR DISPATCH_POSITION EQUAL -1 OR
   NOT JOURNAL_POSITION LESS DISPATCH_POSITION)
    message(FATAL_ERROR
        "Durable execution must journal before mutation dispatch")
endif()

message(STATUS
    "Durable execution boundary verified: journal-first, single-attempt, no secrets or RPC")
