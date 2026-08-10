cmake_minimum_required(VERSION 3.25)
project(predictfun_package_consumer LANGUAGES CXX)

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

find_package(predictfun CONFIG REQUIRED)

add_executable(package_consumer main.cpp)
target_compile_features(package_consumer PRIVATE cxx_std_20)
target_link_libraries(package_consumer
    PRIVATE
        predictfun::public_rest
        predictfun::public_wss
)
