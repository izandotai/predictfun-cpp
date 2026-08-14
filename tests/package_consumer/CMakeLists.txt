cmake_minimum_required(VERSION 3.28)
project(predictfun_package_consumer LANGUAGES CXX)

option(PREDICTFUN_PACKAGE_CONSUMER_WITH_SIGNER
       "Build the explicit local-signer package consumer" OFF)
set(PREDICTFUN_PACKAGE_VERSION "0.1.0" CACHE STRING
    "Exact installed predictfun package version required by this test")

include(FetchContent)
set(PREDICTFUN_BOOST_SOURCE_DIR "" CACHE PATH "Verified Boost 1.87 source")
if(NOT PREDICTFUN_BOOST_SOURCE_DIR)
    message(FATAL_ERROR "PREDICTFUN_BOOST_SOURCE_DIR is required for this isolated package test")
endif()
set(BOOST_INCLUDE_LIBRARIES beast asio)
set(BOOST_ENABLE_CMAKE ON)
set(BOOST_SKIP_INSTALL_RULES ON)
FetchContent_Declare(Boost
    SOURCE_DIR "${PREDICTFUN_BOOST_SOURCE_DIR}"
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Boost)

find_package(predictfun ${PREDICTFUN_PACKAGE_VERSION} EXACT CONFIG REQUIRED)

add_executable(package_consumer main.cpp)
target_compile_features(package_consumer PRIVATE cxx_std_20)
target_link_libraries(package_consumer
    PRIVATE
        predictfun::public_rest
        predictfun::public_wss
        predictfun::analysis
        predictfun::execution
)

add_executable(package_cookbook_exact
    ../../examples/exact_liquidity.cpp)
target_compile_features(package_cookbook_exact PRIVATE cxx_std_20)
target_link_libraries(package_cookbook_exact
    PRIVATE predictfun::analysis)

add_executable(package_cookbook_public_orderbook
    ../../examples/public_orderbook.cpp)
target_compile_features(package_cookbook_public_orderbook PRIVATE cxx_std_20)
target_link_libraries(package_cookbook_public_orderbook
    PRIVATE predictfun::public_rest)

if(PREDICTFUN_PACKAGE_CONSUMER_WITH_SIGNER)
    if(NOT TARGET predictfun::local_signer)
        message(FATAL_ERROR
            "Signer consumer requested, but installed package has no local_signer target")
    endif()
    add_executable(package_signer_consumer signer.cpp)
    target_compile_features(package_signer_consumer PRIVATE cxx_std_20)
    target_link_libraries(package_signer_consumer
        PRIVATE predictfun::local_signer)
endif()

enable_testing()
add_test(NAME installed_public_consumer COMMAND package_consumer)
add_test(NAME installed_cookbook_exact COMMAND package_cookbook_exact)
if(PREDICTFUN_PACKAGE_CONSUMER_WITH_SIGNER)
    add_test(NAME installed_signer_consumer COMMAND package_signer_consumer)
endif()
