file(READ "${ROOT}/tools/testnet_acceptance.cpp" TOOL)
file(READ "${ROOT}/tools/testnet_acceptance_support.cpp" SUPPORT)
file(READ "${ROOT}/tools/testnet_acceptance_support.hpp" HEADER)

set(BOUNDARY "${TOOL}${SUPPORT}${HEADER}")
string(TOLOWER "${BOUNDARY}" LOWER_BOUNDARY)

foreach(FORBIDDEN
    "getenv(" "--private-key" "--private-key-file" "mnemonic"
    "bnb_mainnet" "chain id 56" "chain_id\\\":56"
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
    "validate_testnet_position_write_gate"
    "position-probe"
    "position-execute"
    "async_erc20_decimals"
    "eth_call preflight"
    "--execute"
    "--confirm"
    "--evidence"
    "--secret-env-file"
    "regular non-symlink"
    "PREDICTFUN_BNB_TESTNET_PRIVATE_KEY"
    "testnet wallet address does not match --owner"
    "interactive console"
    "blind_retry_performed")
    string(FIND "${BOUNDARY}" "${REQUIRED}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR
            "Testnet acceptance safety gate is missing: ${REQUIRED}")
    endif()
endforeach()

message(STATUS
    "Testnet acceptance boundary verified: testnet-only, explicit write gate, operator-authorized local or interactive signer, evidence, no blind retry")
