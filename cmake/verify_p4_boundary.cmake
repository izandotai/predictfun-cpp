file(GLOB_RECURSE P4_FILES
    "${ROOT}/include/predictfun/order/*.hpp"
    "${ROOT}/include/predictfun/types/order.hpp"
    "${ROOT}/src/local_signer.cpp"
    "${ROOT}/src/order_*.cpp"
)

foreach(FILE_PATH IN LISTS P4_FILES)
    file(READ "${FILE_PATH}" CONTENT)
    string(TOLOWER "${CONTENT}" LOWER_CONTENT)
    foreach(FORBIDDEN
        "getenv" "ifstream" ".env" "eth_sendrawtransaction"
        "submit_order" "place_order" "cancel_order"
        "http://" "https://")
        string(FIND "${LOWER_CONTENT}" "${FORBIDDEN}" FOUND)
        if(NOT FOUND EQUAL -1)
            message(FATAL_ERROR
                "P4 pure-order boundary violation in ${FILE_PATH}: ${FORBIDDEN}")
        endif()
    endforeach()
endforeach()

foreach(FILE_PATH
    "${ROOT}/include/predictfun/order/amounts.hpp"
    "${ROOT}/src/order_amounts.cpp")
    file(READ "${FILE_PATH}" CONTENT)
    string(TOLOWER "${CONTENT}" LOWER_CONTENT)
    foreach(FORBIDDEN "double" "float" "stod" "strtod")
        string(FIND "${LOWER_CONTENT}" "${FORBIDDEN}" FOUND)
        if(NOT FOUND EQUAL -1)
            message(FATAL_ERROR
                "P4 venue amount math must be integer-only: ${FILE_PATH}: ${FORBIDDEN}")
        endif()
    endforeach()
endforeach()

message(STATUS
    "P4 order boundary verified: integer-only math and no transport, RPC, env, or mutation path")
