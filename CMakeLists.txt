cmake_minimum_required(VERSION 3.25)

project(predictfun_cpp VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(PREDICTFUN_BUILD_TESTS "Build predictfun-cpp tests" ON)
option(PREDICTFUN_WARNINGS_AS_ERRORS "Treat warnings in predictfun-cpp as errors" ON)
set(PREDICTFUN_GLAZE_SOURCE_DIR "" CACHE PATH "Optional pre-verified local Glaze v7.8.4 source")

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

set_target_properties(predictfun_types PROPERTIES EXPORT_NAME types)
set_target_properties(predictfun_codec PROPERTIES EXPORT_NAME codec)

install(TARGETS predictfun_types predictfun_codec
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
    add_test(
        NAME p0_boundary
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_p0_boundary.cmake
    )
endif()
