cmake_minimum_required(VERSION 3.25)

project(predictfun_cpp VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(PREDICTFUN_BUILD_TESTS "Build predictfun-cpp tests" ON)
option(PREDICTFUN_BUILD_TOOLS "Build predictfun-cpp read-only tools" ON)
option(PREDICTFUN_BUILD_LOCAL_SIGNER
       "Build the separately linkable in-memory secp256k1 signer" ON)
option(PREDICTFUN_WARNINGS_AS_ERRORS "Treat warnings in predictfun-cpp as errors" ON)
set(PREDICTFUN_GLAZE_SOURCE_DIR "" CACHE PATH "Optional pre-verified local Glaze v7.8.4 source")
set(PREDICTFUN_BOOST_SOURCE_DIR "" CACHE PATH "Optional pre-verified local Boost 1.87.0 source")
set(PREDICTFUN_SECP256K1_SOURCE_DIR "" CACHE PATH
    "Optional pre-verified local libsecp256k1 v0.7.1 source")

if(PREDICTFUN_GLAZE_SOURCE_DIR)
    FetchContent_Declare(glaze SOURCE_DIR "${PREDICTFUN_GLAZE_SOURCE_DIR}")
else()
    FetchContent_Declare(
        glaze
        URL https://github.com/stephenberry/glaze/archive/refs/tags/v7.8.4.tar.gz
        URL_HASH SHA256=65331A8F8FFA56A3C5990EB31119DB8AFC1CEC0B48DE57B4EBBC9C1286EA74A3
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif()
FetchContent_MakeAvailable(glaze)

set(BOOST_INCLUDE_LIBRARIES beast asio)
set(BOOST_ENABLE_CMAKE ON)
set(BOOST_SKIP_INSTALL_RULES ON)
if(PREDICTFUN_BOOST_SOURCE_DIR)
    FetchContent_Declare(Boost
        SOURCE_DIR "${PREDICTFUN_BOOST_SOURCE_DIR}"
        EXCLUDE_FROM_ALL
    )
else()
    FetchContent_Declare(
        Boost
        URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz
        URL_HASH SHA256=7DA75F171837577A52BBF217E17F8EA576C7C246E4594D617BFDE7FAFD408BE5
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        EXCLUDE_FROM_ALL
    )
endif()
FetchContent_MakeAvailable(Boost)

if(PREDICTFUN_BUILD_LOCAL_SIGNER)
    set(SECP256K1_ENABLE_MODULE_ECDH OFF CACHE BOOL "" FORCE)
    set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE)
    set(SECP256K1_ENABLE_MODULE_EXTRAKEYS OFF CACHE BOOL "" FORCE)
    set(SECP256K1_ENABLE_MODULE_SCHNORRSIG OFF CACHE BOOL "" FORCE)
    set(SECP256K1_ENABLE_MODULE_MUSIG OFF CACHE BOOL "" FORCE)
    set(SECP256K1_ENABLE_MODULE_ELLSWIFT OFF CACHE BOOL "" FORCE)
    set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
    set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
    set(SECP256K1_BUILD_CTIME_TESTS OFF CACHE BOOL "" FORCE)
    set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SECP256K1_INSTALL ON CACHE BOOL "" FORCE)
    if(PREDICTFUN_SECP256K1_SOURCE_DIR)
        FetchContent_Declare(secp256k1
            SOURCE_DIR "${PREDICTFUN_SECP256K1_SOURCE_DIR}"
            EXCLUDE_FROM_ALL
        )
    else()
        FetchContent_Declare(
            secp256k1
            URL https://github.com/bitcoin-core/secp256k1/archive/refs/tags/v0.7.1.tar.gz
            URL_HASH SHA256=958F204DBAFC117E73A2604285DC2EB2A5128344D3499C114DCBA5DE54CB7A9E
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            EXCLUDE_FROM_ALL
        )
    endif()
    FetchContent_MakeAvailable(secp256k1)
endif()

# Boost's CMake targets are used only to resolve and validate the pinned source
# tree.  Keep concrete include directories in the build interface so our
# install export does not accidentally depend on FetchContent's private target
# names (boost_beast, boost_asio, ...).  Installed consumers resolve the public
# Boost::beast/Boost::asio targets through predictfunConfig.cmake instead.
file(GLOB PREDICTFUN_BOOST_LIBRARY_DIRS LIST_DIRECTORIES TRUE
    "${Boost_SOURCE_DIR}/libs/*"
    "${Boost_SOURCE_DIR}/libs/numeric/*"
)
set(PREDICTFUN_BOOST_BUILD_INCLUDE_DIRS "")
foreach(PREDICTFUN_BOOST_LIBRARY_DIR IN LISTS PREDICTFUN_BOOST_LIBRARY_DIRS)
    if(IS_DIRECTORY "${PREDICTFUN_BOOST_LIBRARY_DIR}/include")
        list(APPEND PREDICTFUN_BOOST_BUILD_INCLUDE_DIRS
            "${PREDICTFUN_BOOST_LIBRARY_DIR}/include"
        )
    endif()
endforeach()

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

function(predictfun_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(PREDICTFUN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(PREDICTFUN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(predictfun_types STATIC
    src/decimal.cpp
    src/exact_number.cpp
    src/evm.cpp
    src/secret.cpp
    src/auth_types.cpp
    src/orderbook.cpp
    src/chain_types.cpp
)
add_library(predictfun::types ALIAS predictfun_types)
target_compile_features(predictfun_types PUBLIC cxx_std_20)
target_include_directories(predictfun_types
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
predictfun_enable_warnings(predictfun_types)

add_library(predictfun_codec STATIC
    src/codec.cpp
    src/auth_codec.cpp
    src/private_codec.cpp
    src/trading_codec.cpp
    src/private_websocket_codec.cpp
    src/websocket_codec.cpp
)
add_library(predictfun::codec ALIAS predictfun_codec)
target_compile_features(predictfun_codec PUBLIC cxx_std_20)
target_include_directories(predictfun_codec
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(predictfun_codec
    PUBLIC predictfun::types
    PRIVATE $<BUILD_INTERFACE:glaze::glaze>
)
predictfun_enable_warnings(predictfun_codec)

add_library(predictfun_net STATIC
    src/http.cpp
    src/rate_limiter.cpp
    src/websocket.cpp
)
add_library(predictfun::net ALIAS predictfun_net)
target_compile_features(predictfun_net PUBLIC cxx_std_20)
target_include_directories(predictfun_net
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
foreach(PREDICTFUN_BOOST_INCLUDE_DIR IN LISTS PREDICTFUN_BOOST_BUILD_INCLUDE_DIRS)
    target_include_directories(predictfun_net SYSTEM PUBLIC
        $<BUILD_INTERFACE:${PREDICTFUN_BOOST_INCLUDE_DIR}>
    )
endforeach()
target_link_libraries(predictfun_net
    PUBLIC
        OpenSSL::SSL OpenSSL::Crypto
)
if(WIN32)
    target_link_libraries(predictfun_net PUBLIC ws2_32 mswsock crypt32)
endif()
predictfun_enable_warnings(predictfun_net)
if(MINGW)
    # Beast's TLS/WebSocket templates produce more COFF sections than the
    # default MinGW object limit in Debug builds.
    target_compile_options(predictfun_net PRIVATE -Wa,-mbig-obj)
endif()

add_library(predictfun_public_rest STATIC
    src/public_rest.cpp
)
add_library(predictfun::public_rest ALIAS predictfun_public_rest)
target_compile_features(predictfun_public_rest PUBLIC cxx_std_20)
target_include_directories(predictfun_public_rest
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(predictfun_public_rest
    PUBLIC predictfun::codec predictfun::net
)
predictfun_enable_warnings(predictfun_public_rest)

add_library(predictfun_public_wss STATIC
    src/public_wss.cpp
)
add_library(predictfun::public_wss ALIAS predictfun_public_wss)
target_compile_features(predictfun_public_wss PUBLIC cxx_std_20)
target_include_directories(predictfun_public_wss
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(predictfun_public_wss
    PUBLIC predictfun::codec predictfun::net
)
predictfun_enable_warnings(predictfun_public_wss)

add_library(predictfun_auth STATIC
    src/auth.cpp
)
add_library(predictfun::auth ALIAS predictfun_auth)
target_compile_features(predictfun_auth PUBLIC cxx_std_20)
target_include_directories(predictfun_auth
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(predictfun_auth
    PUBLIC predictfun::codec predictfun::net
)
predictfun_enable_warnings(predictfun_auth)

add_library(predictfun_private_rest STATIC
    src/private_rest.cpp
)
add_library(predictfun::private_rest ALIAS predictfun_private_rest)
target_compile_features(predictfun_private_rest PUBLIC cxx_std_20)
target_include_directories(predictfun_private_rest
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(predictfun_private_rest
    PUBLIC predictfun::codec predictfun::net
)
predictfun_enable_warnings(predictfun_private_rest)

add_library(predictfun_private_wss STATIC
    src/private_wss.cpp
)
add_library(predictfun::private_wss ALIAS predictfun_private_wss)
target_compile_features(predictfun_private_wss PUBLIC cxx_std_20)
target_include_directories(predictfun_private_wss
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(predictfun_private_wss
    PUBLIC predictfun::codec predictfun::net
)
predictfun_enable_warnings(predictfun_private_wss)

add_library(predictfun_trading STATIC
    src/trading.cpp
)
add_library(predictfun::trading ALIAS predictfun_trading)
target_compile_features(predictfun_trading PUBLIC cxx_std_20)
target_include_directories(predictfun_trading
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(predictfun_trading
    PUBLIC predictfun::codec predictfun::net
)
predictfun_enable_warnings(predictfun_trading)

add_library(predictfun_lifecycle STATIC
    src/lifecycle.cpp
    src/lifecycle_journal.cpp
)
add_library(predictfun::lifecycle ALIAS predictfun_lifecycle)
target_compile_features(predictfun_lifecycle PUBLIC cxx_std_20)
target_include_directories(predictfun_lifecycle
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(predictfun_lifecycle PUBLIC predictfun::types)
predictfun_enable_warnings(predictfun_lifecycle)

add_library(predictfun_order STATIC
    src/order_types.cpp
    src/order_amounts.cpp
    src/order_eip712.cpp
    src/order_builder.cpp
    src/order_signature.cpp
)
add_library(predictfun::order ALIAS predictfun_order)
target_compile_features(predictfun_order PUBLIC cxx_std_20)
target_include_directories(predictfun_order
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
foreach(PREDICTFUN_BOOST_INCLUDE_DIR IN LISTS PREDICTFUN_BOOST_BUILD_INCLUDE_DIRS)
    target_include_directories(predictfun_order SYSTEM PRIVATE
        $<BUILD_INTERFACE:${PREDICTFUN_BOOST_INCLUDE_DIR}>
    )
endforeach()
target_link_libraries(predictfun_order
    PUBLIC predictfun::types
    PRIVATE OpenSSL::Crypto
)
predictfun_enable_warnings(predictfun_order)

add_library(predictfun_chain STATIC
    src/chain_abi.cpp
    src/chain_approvals.cpp
    src/chain_client.cpp
    src/chain_operations.cpp
    src/chain_transaction.cpp
)
add_library(predictfun::chain ALIAS predictfun_chain)
target_compile_features(predictfun_chain PUBLIC cxx_std_20)
target_include_directories(predictfun_chain
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(predictfun_chain
    PUBLIC predictfun::types predictfun::net predictfun::order
    PRIVATE $<BUILD_INTERFACE:glaze::glaze>
)
predictfun_enable_warnings(predictfun_chain)

if(PREDICTFUN_BUILD_LOCAL_SIGNER)
    add_library(predictfun_local_signer STATIC
        src/local_signer.cpp
    )
    add_library(predictfun::local_signer ALIAS predictfun_local_signer)
    target_compile_features(predictfun_local_signer PUBLIC cxx_std_20)
    target_include_directories(predictfun_local_signer
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(predictfun_local_signer
        PUBLIC predictfun::order
        # Embed the audited C objects in this archive. Installed consumers do
        # not need a hidden signing dependency and read-only targets remain
        # unaffected.
        PRIVATE $<BUILD_INTERFACE:secp256k1_objs>
    )
    predictfun_enable_warnings(predictfun_local_signer)

    add_library(predictfun_local_transaction_signer STATIC
        src/chain_local_transaction_signer.cpp
    )
    add_library(predictfun::local_transaction_signer ALIAS
        predictfun_local_transaction_signer)
    target_compile_features(predictfun_local_transaction_signer PUBLIC cxx_std_20)
    target_include_directories(predictfun_local_transaction_signer
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(predictfun_local_transaction_signer
        PUBLIC predictfun::chain predictfun::local_signer
    )
    predictfun_enable_warnings(predictfun_local_transaction_signer)
endif()

set_target_properties(predictfun_types PROPERTIES EXPORT_NAME types)
set_target_properties(predictfun_codec PROPERTIES EXPORT_NAME codec)
set_target_properties(predictfun_net PROPERTIES EXPORT_NAME net)
set_target_properties(predictfun_public_rest PROPERTIES EXPORT_NAME public_rest)
set_target_properties(predictfun_public_wss PROPERTIES EXPORT_NAME public_wss)
set_target_properties(predictfun_auth PROPERTIES EXPORT_NAME auth)
set_target_properties(predictfun_private_rest PROPERTIES EXPORT_NAME private_rest)
set_target_properties(predictfun_private_wss PROPERTIES EXPORT_NAME private_wss)
set_target_properties(predictfun_trading PROPERTIES EXPORT_NAME trading)
set_target_properties(predictfun_lifecycle PROPERTIES EXPORT_NAME lifecycle)
set_target_properties(predictfun_order PROPERTIES EXPORT_NAME order)
set_target_properties(predictfun_chain PROPERTIES EXPORT_NAME chain)
if(PREDICTFUN_BUILD_LOCAL_SIGNER)
    set_target_properties(predictfun_local_signer PROPERTIES
        EXPORT_NAME local_signer)
    set_target_properties(predictfun_local_transaction_signer PROPERTIES
        EXPORT_NAME local_transaction_signer)
endif()

set(PREDICTFUN_INSTALL_TARGETS
    predictfun_types predictfun_codec predictfun_net predictfun_public_rest
    predictfun_public_wss predictfun_auth predictfun_private_rest
    predictfun_private_wss predictfun_trading predictfun_lifecycle
    predictfun_order predictfun_chain)
if(PREDICTFUN_BUILD_LOCAL_SIGNER)
    list(APPEND PREDICTFUN_INSTALL_TARGETS predictfun_local_signer
        predictfun_local_transaction_signer)
endif()
install(TARGETS ${PREDICTFUN_INSTALL_TARGETS}
    EXPORT predictfunTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
configure_package_config_file(
    cmake/predictfunConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/predictfunConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/predictfun
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/predictfunConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(EXPORT predictfunTargets
    FILE predictfunTargets.cmake
    NAMESPACE predictfun::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/predictfun
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/predictfunConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/predictfunConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/predictfun
)

if(PREDICTFUN_BUILD_TOOLS)
    add_executable(predictfun_read_only_probe tools/read_only_probe.cpp)
    target_compile_features(predictfun_read_only_probe PRIVATE cxx_std_20)
    target_link_libraries(predictfun_read_only_probe
        PRIVATE predictfun::public_rest
    )
    predictfun_enable_warnings(predictfun_read_only_probe)

    add_executable(predictfun_public_wss_probe tools/public_wss_probe.cpp)
    target_compile_features(predictfun_public_wss_probe PRIVATE cxx_std_20)
    target_link_libraries(predictfun_public_wss_probe
        PRIVATE predictfun::public_wss
    )
    predictfun_enable_warnings(predictfun_public_wss_probe)

    add_executable(predictfun_recovery_example tools/recovery_example.cpp)
    target_compile_features(predictfun_recovery_example PRIVATE cxx_std_20)
    target_link_libraries(predictfun_recovery_example
        PRIVATE predictfun::lifecycle
    )
    predictfun_enable_warnings(predictfun_recovery_example)
endif()

if(PREDICTFUN_BUILD_TESTS)
    enable_testing()

    add_executable(predictfun_codec_tests tests/codec_tests.cpp)
    target_compile_features(predictfun_codec_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_codec_tests PRIVATE predictfun::codec)
    target_compile_definitions(predictfun_codec_tests PRIVATE
        PREDICTFUN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    predictfun_enable_warnings(predictfun_codec_tests)

    add_test(NAME codec COMMAND predictfun_codec_tests)

    add_executable(predictfun_public_rest_tests tests/public_rest_tests.cpp)
    target_compile_features(predictfun_public_rest_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_public_rest_tests
        PRIVATE predictfun::public_rest
    )
    predictfun_enable_warnings(predictfun_public_rest_tests)
    add_test(NAME public_rest COMMAND predictfun_public_rest_tests)

    add_executable(predictfun_auth_tests tests/auth_tests.cpp)
    target_compile_features(predictfun_auth_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_auth_tests PRIVATE predictfun::auth)
    predictfun_enable_warnings(predictfun_auth_tests)
    add_test(NAME auth COMMAND predictfun_auth_tests)

    add_executable(predictfun_private_rest_tests tests/private_rest_tests.cpp)
    target_compile_features(predictfun_private_rest_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_private_rest_tests
        PRIVATE predictfun::private_rest
    )
    predictfun_enable_warnings(predictfun_private_rest_tests)
    add_test(NAME private_rest COMMAND predictfun_private_rest_tests)

    add_executable(predictfun_private_websocket_codec_tests
        tests/private_websocket_codec_tests.cpp
    )
    target_compile_features(predictfun_private_websocket_codec_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_private_websocket_codec_tests
        PRIVATE predictfun::codec
    )
    predictfun_enable_warnings(predictfun_private_websocket_codec_tests)
    add_test(NAME private_websocket_codec COMMAND predictfun_private_websocket_codec_tests)

    add_executable(predictfun_private_wss_tests tests/private_wss_tests.cpp)
    target_compile_features(predictfun_private_wss_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_private_wss_tests
        PRIVATE predictfun::private_wss
    )
    predictfun_enable_warnings(predictfun_private_wss_tests)
    add_test(NAME private_wss COMMAND predictfun_private_wss_tests)

    add_executable(predictfun_trading_tests tests/trading_tests.cpp)
    target_compile_features(predictfun_trading_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_trading_tests PRIVATE predictfun::trading)
    predictfun_enable_warnings(predictfun_trading_tests)
    add_test(NAME trading COMMAND predictfun_trading_tests)

    add_executable(predictfun_lifecycle_tests tests/lifecycle_tests.cpp)
    target_compile_features(predictfun_lifecycle_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_lifecycle_tests
        PRIVATE predictfun::lifecycle predictfun::codec)
    predictfun_enable_warnings(predictfun_lifecycle_tests)
    add_test(NAME lifecycle COMMAND predictfun_lifecycle_tests)

    add_executable(predictfun_order_tests tests/order_tests.cpp)
    target_compile_features(predictfun_order_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_order_tests PRIVATE predictfun::order)
    predictfun_enable_warnings(predictfun_order_tests)
    add_test(NAME order COMMAND predictfun_order_tests)

    add_executable(predictfun_chain_tests tests/chain_tests.cpp)
    target_compile_features(predictfun_chain_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_chain_tests PRIVATE predictfun::chain)
    predictfun_enable_warnings(predictfun_chain_tests)
    add_test(NAME chain COMMAND predictfun_chain_tests)

    if(PREDICTFUN_BUILD_LOCAL_SIGNER)
        add_executable(predictfun_local_signer_tests
            tests/local_signer_tests.cpp
        )
        target_compile_features(predictfun_local_signer_tests PRIVATE cxx_std_20)
        target_link_libraries(predictfun_local_signer_tests
            PRIVATE predictfun::local_signer
        )
        predictfun_enable_warnings(predictfun_local_signer_tests)
        add_test(NAME local_signer COMMAND predictfun_local_signer_tests)

        add_executable(predictfun_chain_local_transaction_signer_tests
            tests/chain_local_transaction_signer_tests.cpp
        )
        target_compile_features(predictfun_chain_local_transaction_signer_tests
            PRIVATE cxx_std_20)
        target_link_libraries(predictfun_chain_local_transaction_signer_tests
            PRIVATE predictfun::local_transaction_signer
        )
        predictfun_enable_warnings(
            predictfun_chain_local_transaction_signer_tests)
        add_test(NAME chain_local_transaction_signer
            COMMAND predictfun_chain_local_transaction_signer_tests)
    endif()

    add_executable(predictfun_http_transport_tests
        tests/http_transport_tests.cpp
    )
    target_compile_features(predictfun_http_transport_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_http_transport_tests
        PRIVATE predictfun::net
    )
    predictfun_enable_warnings(predictfun_http_transport_tests)
    add_test(NAME http_transport COMMAND predictfun_http_transport_tests)

    add_executable(predictfun_websocket_codec_tests
        tests/websocket_codec_tests.cpp
    )
    target_compile_features(predictfun_websocket_codec_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_websocket_codec_tests
        PRIVATE predictfun::codec
    )
    predictfun_enable_warnings(predictfun_websocket_codec_tests)
    add_test(NAME websocket_codec COMMAND predictfun_websocket_codec_tests)

    add_executable(predictfun_public_wss_tests tests/public_wss_tests.cpp)
    target_compile_features(predictfun_public_wss_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_public_wss_tests
        PRIVATE predictfun::public_wss
    )
    predictfun_enable_warnings(predictfun_public_wss_tests)
    add_test(NAME public_wss COMMAND predictfun_public_wss_tests)

    add_executable(predictfun_websocket_transport_tests
        tests/websocket_transport_tests.cpp
    )
    target_compile_features(predictfun_websocket_transport_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_websocket_transport_tests
        PRIVATE predictfun::net
    )
    predictfun_enable_warnings(predictfun_websocket_transport_tests)
    if(MINGW)
        target_compile_options(predictfun_websocket_transport_tests
            PRIVATE -Wa,-mbig-obj)
    endif()
    add_test(NAME websocket_transport COMMAND predictfun_websocket_transport_tests)

    add_test(
        NAME p0_boundary
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_p0_boundary.cmake
    )
    add_test(
        NAME p1_boundary
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_p1_boundary.cmake
    )
    add_test(
        NAME p2_boundary
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_p2_boundary.cmake
    )
    add_test(
        NAME p3_boundary
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_p3_boundary.cmake
    )
    add_test(
        NAME p4_boundary
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_p4_boundary.cmake
    )
    add_test(
        NAME p5_boundary
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_p5_boundary.cmake
    )
    add_test(
        NAME p6_boundary
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_p6_boundary.cmake
    )
endif()
