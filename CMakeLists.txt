cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
project(RingBufferBenchmark)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(benchmark)

add_executable(RingBufferBenchmark
    RingBufferBenchmark.cpp
    aligned_alloc.hpp
    compile_time_utilities.hpp
    scope_guard.hpp
    spsc_queue.hpp
    spsc_ring_buffer.hpp
    spsc_ring_buffer_cached.hpp
    spsc_ring_buffer_heap.hpp
    follyProducerConsumerQueue.h
    rigtorpSPSCQueue.h
    moodycamel/atomicops.h
    moodycamel/readwriterqueue.h
)

set_property(TARGET RingBufferBenchmark PROPERTY CXX_STANDARD 17)
target_link_libraries(RingBufferBenchmark benchmark)
target_compile_options(RingBufferBenchmark PRIVATE -DMOODYCAMEL_CACHE_LINE_SIZE=128)
