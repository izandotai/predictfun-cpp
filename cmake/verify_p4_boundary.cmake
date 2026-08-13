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

# Business modules must consume the repository-wide audited crypto boundary,
# never instantiate another provider or raw curve implementation locally.
foreach(FILE_PATH
    "${ROOT}/src/order_eip712.cpp"
    "${ROOT}/src/local_signer.cpp")
    file(READ "${FILE_PATH}" CONTENT)
    string(TOLOWER "${CONTENT}" LOWER_CONTENT)
    foreach(FORBIDDEN
        "#include <openssl/" "#include <secp256k1"
        "evp_mdfetch" "secp256k1_context_create")
        string(FIND "${LOWER_CONTENT}" "${FORBIDDEN}" FOUND)
        if(NOT FOUND EQUAL -1)
            message(FATAL_ERROR
                "P4 direct crypto-provider violation in ${FILE_PATH}: ${FORBIDDEN}")
        endif()
    endforeach()
    string(FIND "${LOWER_CONTENT}" "core/crypto/" IZAN_FOUND)
    if(IZAN_FOUND EQUAL -1)
        message(FATAL_ERROR
            "P4 crypto boundary must use izan-crypto: ${FILE_PATH}")
    endif()
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
    "P4 order boundary verified: izan-crypto, integer-only math and no transport, RPC, env, or mutation path")
