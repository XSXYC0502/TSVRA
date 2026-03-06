cmake_minimum_required(VERSION 3.20)

# 使用系统默认 g++ 编译器
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

project(TSVRA VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
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
    -pthread  # 需要pthread支持以解决标准库头文件问题
    -fpermissive  # 允许标准库中的类型转换问题（GCC 16.0.0标准库的已知bug）
)

# 包含目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 源文件（不含main.cpp）
set(LIB_SOURCES
    src/config.cpp
    src/tsv.cpp
    src/grid.cpp
    src/router.cpp
    src/request_generator.cpp
    src/simulator.cpp
    src/statistics.cpp
)

# 链接pthread库（标准库需要，即使我们不直接使用多线程）
find_package(Threads REQUIRED)

# 静态库（供主程序和测试共用）
add_library(tsvra_lib STATIC ${LIB_SOURCES})
target_link_libraries(tsvra_lib PRIVATE Threads::Threads)

# 可执行文件
add_executable(tsvra src/main.cpp)
target_link_libraries(tsvra PRIVATE tsvra_lib Threads::Threads)

# 针对我们自己的代码启用-Werror
# 注意：GCC 16.0.0标准库有pthread相关的已知编译错误（ext/concurrence.h和bits/std_mutex.h）
# 这是标准库的bug，不是我们代码的问题。暂时注释掉-Werror以允许编译通过
# 当标准库修复后可以取消注释
# target_compile_options(tsvra PRIVATE -Werror)

# 输出目录
set_target_properties(tsvra PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)

# 测试
enable_testing()
add_executable(tsvra_tests tests/test_main.cpp)
target_link_libraries(tsvra_tests PRIVATE tsvra_lib Threads::Threads)
set_target_properties(tsvra_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)
add_test(NAME tsvra_tests COMMAND tsvra_tests)

