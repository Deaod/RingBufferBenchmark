cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
project(RingBufferBenchmark)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(benchmark)
find_package(folly CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(RingBufferBenchmark
    DummyContainer.hpp
    Platform.hpp
    BenchmarkSupport.hpp

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
    ce_queue.hpp
    rigtorpSPSCQueue.h
    moodycamel/atomicops.h
    moodycamel/readwriterqueue.h
    
    LamportQueue2.hpp
    LamportQueue3.hpp
    LamportQueue4.hpp
    LamportQueue5.hpp
    LamportQueue6.hpp
    LamportQueue7.hpp
    LamportQueueTest.cpp

    FastForward1.hpp
    FastForward2.hpp
    FastForward3.hpp
    FastForward4.hpp
    FastForward5.hpp
    FastForwardTest.cpp

    MCRingBuffer1.hpp
    MCRingBuffer2.hpp
    MCRingBuffer3.hpp
    MCRingBuffer4.hpp
    MCRingBuffer5.hpp
    MCRingBuffer6.hpp
    MCRingBufferTest.cpp

    GFFQueue1.hpp
    GFFQueue2.hpp
    GFFQueue3.hpp
    GFFQueue4.hpp
    GFFQueueTest.cpp

    ChunkedQueue1.hpp
    ChunkedQueue2.hpp
    ChunkedQueue3.hpp
    ChunkedQueue4.hpp
    ChunkedQueue5.hpp
    ChunkedQueue6.hpp
    ChunkedQueue7.hpp
    ChunkedQueueTest.cpp
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

