cmake_minimum_required(VERSION 3.18)
project(halftime-lv2 C CXX)

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

# DPF-based plugin with GUI (LV2 + VST2 + VST3 + CLAP + standalone)
option(BUILD_DPF_PLUGIN "Build the DPF plugin with GUI" ON)
if(BUILD_DPF_PLUGIN)
    add_subdirectory(dpf)

    dpf_add_plugin(halftime-dpf
        TARGETS lv2 vst2 vst3 clap jack
        UI_TYPE opengl
        FILES_DSP
            dpf-plugin/HalftimeDPF.cpp
        FILES_UI
            dpf-plugin/HalftimeUI.cpp
    )

    target_include_directories(halftime-dpf PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/dpf-plugin"
    )
endif()
