cmake_minimum_required(VERSION 3.20)

# Use the system default g++ compiler.
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

project(TSVRA VERSION 1.0.0 LANGUAGES CXX)

# Set the C++ standard.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler options
add_compile_options(
    -Wall
    -Wextra
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Wformat=2
    -Wmisleading-indentation
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
    -Wnull-dereference
    -Wuseless-cast
    -Wdouble-promotion
    -Wsign-conversion
    -Woverloaded-virtual
    -pthread  # Needed for pthread support required by the standard library headers.
    -fpermissive  # Allow standard-library type-conversion issues (known GCC 16.0.0 STL bug).
)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Source files (excluding main.cpp)
set(LIB_SOURCES
    src/config.cpp
    src/tsv.cpp
    src/grid.cpp
    src/router.cpp
    src/request_generator.cpp
    src/simulator.cpp
    src/statistics.cpp
)

# Link pthread because the standard library requires it, even without direct multithreading.
find_package(Threads REQUIRED)

# Static library shared by the main program and tests
add_library(tsvra_lib STATIC ${LIB_SOURCES})
target_link_libraries(tsvra_lib PRIVATE Threads::Threads)

# Executable
add_executable(tsvra src/main.cpp)
target_link_libraries(tsvra PRIVATE tsvra_lib Threads::Threads)

# Enable -Werror for our own code.
# Note: GCC 16.0.0 has a known pthread-related compile issue in the standard library
# (ext/concurrence.h and bits/std_mutex.h).
# This is a standard-library bug, not a project bug, so -Werror remains disabled for now.
# Uncomment it again after the standard library is fixed.
# target_compile_options(tsvra PRIVATE -Werror)

# Output directory
set_target_properties(tsvra PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)

# Tests
enable_testing()
add_executable(tsvra_tests tests/test_main.cpp)
target_link_libraries(tsvra_tests PRIVATE tsvra_lib Threads::Threads)
set_target_properties(tsvra_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)
add_test(NAME tsvra_tests COMMAND tsvra_tests)
