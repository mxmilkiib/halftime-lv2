cmake_minimum_required(VERSION 3.18)
project(halftime-lv2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Strict flags — catches the common audio-thread mistakes
add_compile_options(
    -Wall -Wextra -Wpedantic
    -O3 -ffast-math
    -fvisibility=hidden
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LV2 REQUIRED lv2)

add_library(halftime MODULE
    src/plugin.cpp
)

target_include_directories(halftime PRIVATE
    include/
    ${LV2_INCLUDE_DIRS}
)

set_target_properties(halftime PROPERTIES
    PREFIX ""
    OUTPUT_NAME "halftime"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/halftime.lv2"
)

# Copy TTL into bundle directory alongside the .so
configure_file(
    "${CMAKE_SOURCE_DIR}/halftime.lv2/manifest.ttl"
    "${CMAKE_BINARY_DIR}/halftime.lv2/manifest.ttl"
    COPYONLY
)

# Install target
include(GNUInstallDirs)
set(LV2_INSTALL_DIR "$ENV{HOME}/.lv2" CACHE PATH "LV2 bundle install directory")

install(
    DIRECTORY "${CMAKE_BINARY_DIR}/halftime.lv2"
    DESTINATION "${LV2_INSTALL_DIR}"
)

# Tests
add_subdirectory(tests)
