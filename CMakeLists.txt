cmake_minimum_required(VERSION 3.28)
project(tx_scheduler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(scheduler src/main.cpp)

target_compile_options(scheduler PRIVATE -Wall -Wextra -g -fsanitize=address,undefined)
target_link_options(scheduler PRIVATE -fsanitize=address,undefined)
