cmake_minimum_required(VERSION 3.25)
project(predictfun_package_consumer LANGUAGES CXX)

find_package(predictfun CONFIG REQUIRED)

add_executable(package_consumer main.cpp)
target_compile_features(package_consumer PRIVATE cxx_std_20)
target_link_libraries(package_consumer PRIVATE predictfun::codec)
