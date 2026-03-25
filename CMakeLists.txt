cmake_minimum_required(VERSION 3.20)

project(TSVRA VERSION 1.0.0 LANGUAGES CXX)

# Set the C++ standard.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(tsvra_apply_compile_options target_name)
    if (MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /permissive-
            /EHsc
        )
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target_name} PRIVATE
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
            -fpermissive
        )
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Wformat=2
            -Wnull-dereference
            -Wdouble-promotion
            -Wsign-conversion
            -Woverloaded-virtual
        )
    endif()
endfunction()

function(tsvra_set_runtime_output target_name)
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${PROJECT_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${PROJECT_SOURCE_DIR}/bin
    )
endfunction()

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
target_include_directories(tsvra_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tsvra_lib PRIVATE Threads::Threads)
tsvra_apply_compile_options(tsvra_lib)

# Executable
add_executable(tsvra src/main.cpp)
target_link_libraries(tsvra PRIVATE tsvra_lib Threads::Threads)
tsvra_apply_compile_options(tsvra)

# Enable -Werror for our own code.
# Note: GCC 16.0.0 has a known pthread-related compile issue in the standard library
# (ext/concurrence.h and bits/std_mutex.h).
# This is a standard-library bug, not a project bug, so -Werror remains disabled for now.
# Uncomment it again after the standard library is fixed.
# target_compile_options(tsvra PRIVATE -Werror)

tsvra_set_runtime_output(tsvra)

# Tests
enable_testing()
add_executable(tsvra_tests tests/test_main.cpp)
target_link_libraries(tsvra_tests PRIVATE tsvra_lib Threads::Threads)
tsvra_apply_compile_options(tsvra_tests)
tsvra_set_runtime_output(tsvra_tests)
add_test(NAME tsvra_tests COMMAND $<TARGET_FILE:tsvra_tests>)
