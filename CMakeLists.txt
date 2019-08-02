cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
project(RingBufferBenchmark)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(benchmark)
find_package(folly CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(RingBufferBenchmark
    RingBufferBenchmark.cpp
    aligned_alloc.hpp
    compile_time_utilities.hpp
    scope_guard.hpp
    spsc_queue.hpp
    spsc_queue_release.hpp
    spsc_ring_buffer.hpp
    spsc_ring_buffer_cached.hpp
    spsc_ring_buffer_chunked.hpp
    spsc_ring_buffer_heap.hpp
    rigtorpSPSCQueue.h
    moodycamel/atomicops.h
    moodycamel/readwriterqueue.h
)

set_property(TARGET RingBufferBenchmark PROPERTY CXX_STANDARD 17)
target_link_libraries(RingBufferBenchmark benchmark Folly::folly Folly::folly_deps)
target_compile_options(RingBufferBenchmark PRIVATE -DMOODYCAMEL_CACHE_LINE_SIZE=128)

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    target_compile_options(RingBufferBenchmark PRIVATE "-mavx")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    target_compile_options(RingBufferBenchmark PRIVATE "-mavx")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    target_compile_options(RingBufferBenchmark PRIVATE "/arch:AVX" PRIVATE "/bigobj")
else()
    message(ERROR "Unknown compiler")
endif()

