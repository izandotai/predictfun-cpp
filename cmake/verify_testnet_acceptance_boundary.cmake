file(READ "${ROOT}/tools/testnet_acceptance.cpp" TOOL)
file(READ "${ROOT}/tools/testnet_acceptance_support.cpp" SUPPORT)
file(READ "${ROOT}/tools/testnet_acceptance_support.hpp" HEADER)

set(BOUNDARY "${TOOL}${SUPPORT}${HEADER}")
string(TOLOWER "${BOUNDARY}" LOWER_BOUNDARY)

foreach(FORBIDDEN
    "getenv(" ".env" "--private-key" "--private-key-file" "mnemonic"
    "std::ifstream" "bnb_mainnet" "chain id 56" "chain_id\\\":56"
    "retry_after" "retry_count" "max_retries")
    string(FIND "${LOWER_BOUNDARY}" "${FORBIDDEN}" FOUND)
    if(NOT FOUND EQUAL -1)
        message(FATAL_ERROR
            "Testnet acceptance authority violation: ${FORBIDDEN}")
    endif()
endforeach()

foreach(REQUIRED
    "ChainId::bnb_testnet"
    "validate_testnet_write_gate"
    "--execute"
    "--confirm"
    "--evidence"
    "interactive console"
    "blind_retry_performed")
    string(FIND "${BOUNDARY}" "${REQUIRED}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR
            "Testnet acceptance safety gate is missing: ${REQUIRED}")
    endif()
endforeach()

message(STATUS
    "Testnet acceptance boundary verified: testnet-only, explicit write gate, interactive signer, evidence, no blind retry")
