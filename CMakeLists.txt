cmake_minimum_required(VERSION 3.16)
project(flight_log_analyzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(flight_log_analyzer
    src/main.cpp
    src/ulog_parser.cpp
    src/checks.cpp
    src/report.cpp
)
target_compile_options(flight_log_analyzer PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(flight_log_analyzer PRIVATE -static)