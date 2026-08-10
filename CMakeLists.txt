cmake_minimum_required(VERSION 3.25)

project(predictfun_cpp VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(PREDICTFUN_BUILD_TESTS "Build predictfun-cpp tests" ON)
option(PREDICTFUN_BUILD_TOOLS "Build predictfun-cpp read-only tools" ON)
option(PREDICTFUN_WARNINGS_AS_ERRORS "Treat warnings in predictfun-cpp as errors" ON)
set(PREDICTFUN_GLAZE_SOURCE_DIR "" CACHE PATH "Optional pre-verified local Glaze v7.8.4 source")
set(PREDICTFUN_BOOST_SOURCE_DIR "" CACHE PATH "Optional pre-verified local Boost 1.87.0 source")

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
    src/orderbook.cpp
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

set_target_properties(predictfun_types PROPERTIES EXPORT_NAME types)
set_target_properties(predictfun_codec PROPERTIES EXPORT_NAME codec)
set_target_properties(predictfun_net PROPERTIES EXPORT_NAME net)
set_target_properties(predictfun_public_rest PROPERTIES EXPORT_NAME public_rest)

install(TARGETS predictfun_types predictfun_codec predictfun_net predictfun_public_rest
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

    add_executable(predictfun_http_transport_tests
        tests/http_transport_tests.cpp
    )
    target_compile_features(predictfun_http_transport_tests PRIVATE cxx_std_20)
    target_link_libraries(predictfun_http_transport_tests
        PRIVATE predictfun::net
    )
    predictfun_enable_warnings(predictfun_http_transport_tests)
    add_test(NAME http_transport COMMAND predictfun_http_transport_tests)

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
endif()
