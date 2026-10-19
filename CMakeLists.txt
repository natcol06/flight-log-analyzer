cmake_minimum_required(VERSION 3.16)
project(flight_log_analyzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----- The analyzer code, shared by the program and the tests -----
add_library(flight_core
    src/ulog_parser.cpp
    src/checks.cpp
    src/report.cpp
)
target_include_directories(flight_core PUBLIC src)
target_compile_options(flight_core PRIVATE -Wall -Wextra -Wpedantic)

# ----- The command-line program -----
add_executable(flight_log_analyzer src/main.cpp)
target_link_libraries(flight_log_analyzer PRIVATE flight_core)
target_compile_options(flight_log_analyzer PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(flight_log_analyzer PRIVATE -static)

# ----- Unit tests (GoogleTest is downloaded automatically) -----
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(run_tests
    tests/test_parser.cpp
    tests/test_checks.cpp
)
target_link_libraries(run_tests PRIVATE flight_core GTest::gtest_main)
target_link_options(run_tests PRIVATE -static)

include(GoogleTest)
gtest_discover_tests(run_tests)