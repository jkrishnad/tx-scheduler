cmake_minimum_required(VERSION 3.28)
project(tx_scheduler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Debug when no build type is given
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Compile options
add_compile_options(-Wall -Wextra)
add_compile_options("$<$<CONFIG:Debug>:-g;-fsanitize=address,undefined>")
add_compile_options("$<$<CONFIG:Release>:-O2>")

# Link options
add_link_options("$<$<CONFIG:Debug>:-fsanitize=address,undefined>")

# Library
add_library(tx_core src/stats.cpp src/socket.cpp src/shm.cpp src/agave_link.cpp src/base58.cpp)
target_include_directories(tx_core PUBLIC src)

# Executables
add_executable(scheduler src/main.cpp)
add_executable(tests tests/tests.cpp)

# Linking
target_link_libraries(scheduler PRIVATE tx_core)
target_link_libraries(tests PRIVATE tx_core)
