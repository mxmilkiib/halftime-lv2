include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.3)
FetchContent_MakeAvailable(Catch2)

# Single glob — new test files are picked up automatically
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(halftime_tests ${TEST_SOURCES})
target_include_directories(halftime_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${LV2_INCLUDE_DIRS})
target_link_libraries(halftime_tests PRIVATE Catch2::Catch2WithMain)
target_compile_options(halftime_tests PRIVATE -O2 -Wall -Wextra)

# Sanitiser target reuses the same glob — no duplication
add_executable(halftime_tests_san ${TEST_SOURCES})
target_include_directories(halftime_tests_san PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${LV2_INCLUDE_DIRS})
target_link_libraries(halftime_tests_san PRIVATE Catch2::Catch2WithMain)
target_compile_options(halftime_tests_san PRIVATE
    -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(halftime_tests_san PRIVATE
    -fsanitize=address,undefined)

include(CTest)
include(Catch)
catch_discover_tests(halftime_tests)
