cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
project(RingBufferBenchmark)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(benchmark)

add_executable(RingBufferBenchmark RingBufferBenchmark.cpp)
set_property(TARGET RingBufferBenchmark PROPERTY CXX_STANDARD 17)
target_link_libraries(RingBufferBenchmark benchmark)
