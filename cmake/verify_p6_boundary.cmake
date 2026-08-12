file(READ "${ROOT}/include/predictfun/chain/client.hpp" CLIENT)
file(READ "${ROOT}/src/chain_client.cpp" IMPLEMENTATION)
file(READ "${ROOT}/include/predictfun/chain/approvals.hpp" APPROVALS)
file(READ "${ROOT}/src/chain_approvals.cpp" APPROVALS_IMPLEMENTATION)
file(READ "${ROOT}/include/predictfun/chain/abi.hpp" ABI)
file(READ "${ROOT}/src/chain_abi.cpp" ABI_IMPLEMENTATION)
file(READ "${ROOT}/include/predictfun/chain/operations.hpp" OPERATIONS)
file(READ "${ROOT}/src/chain_operations.cpp" OPERATIONS_IMPLEMENTATION)

set(P6_CHAIN_BOUNDARY
    "${CLIENT}${IMPLEMENTATION}${APPROVALS}${APPROVALS_IMPLEMENTATION}"
    "${ABI}${ABI_IMPLEMENTATION}${OPERATIONS}${OPERATIONS_IMPLEMENTATION}")

foreach(FORBIDDEN ".env" "getenv(" "PRIVATE_KEY" "mnemonic" "eth_sendTransaction")
  string(FIND "${P6_CHAIN_BOUNDARY}" "${FORBIDDEN}" FOUND)
  if(NOT FOUND EQUAL -1)
    message(FATAL_ERROR "P6 chain boundary contains forbidden authority: ${FORBIDDEN}")
  endif()
endforeach()

if(NOT IMPLEMENTATION MATCHES "unsupported_chain")
  message(FATAL_ERROR "P6 chain client must fail closed on a wrong chain")
endif()
