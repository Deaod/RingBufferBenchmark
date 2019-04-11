#include <benchmark/benchmark.h>
#include "spsc_ring_buffer.hpp"
#include "spsc_ring_buffer_cached.hpp"
#include "spsc_ring_buffer_heap.hpp"
#include "follyProducerConsumerQueue.h"
#include <chrono>
#include <thread>
#include <new>

void configure_benchmark(benchmark::internal::Benchmark* bench) {
    bench->Threads(2);
    bench->Repetitions(200);

    bench->ArgNames({"Elem Size"});

    bench->Args({   8 });
    bench->Args({  16 });
    bench->Args({  24 });
    bench->Args({  32 });
    bench->Args({  40 });
    bench->Args({  48 });
    bench->Args({  56 });
    bench->Args({  72 });
    bench->Args({  88 });
    bench->Args({ 104 });
    bench->Args({ 120 });
    bench->Args({ 152 });
    bench->Args({ 184 });
    bench->Args({ 216 });
    bench->Args({ 248 });
    bench->Args({ 280 });
    bench->Args({ 312 });
    bench->Args({ 344 });
    bench->Args({ 376 });
    bench->Args({ 408 });
    bench->Args({ 440 });
    bench->Args({ 472 });
    bench->Args({ 504 });
}

void configure_folly_queue(benchmark::internal::Benchmark* bench) {
    bench->Threads(2);
    bench->Repetitions(200);

    bench->ArgNames({ "Buffer Size (log2)" });

    bench->Args({ 15 });
    bench->Args({ 16 });
    bench->Args({ 17 });
    bench->Args({ 18 });
    bench->Args({ 19 });
    bench->Args({ 20 });
    bench->Args({ 21 });
    bench->Args({ 22 });
    bench->Args({ 23 });
    bench->Args({ 24 });
    bench->Args({ 25 });
    bench->Args({ 26 });
    bench->Args({ 27 });
    bench->Args({ 28 });
    bench->Args({ 29 });
    bench->Args({ 30 });
}

template<typename type>
static void RingBuffer(benchmark::State& state) {
    static auto* buffer = new type{};

    auto& b = *buffer;
    if (state.thread_index == 0) {
        auto size = state.range(0);
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                bool result = b.produce(size, [](void*) { return true; });
                counter += int(result);
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * state.range(0));
    } else {
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                bool result = b.consume([](const void*, ptrdiff_t) { return true; });
                counter += int(result);
            }
        }
    }

    if (b.is_empty() == false) {
        state.SkipWithError("Not Empty after test");
    }
}

template<typename type>
static void FollyQueue(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue = new type{ uint32_t(1) << (state.range(0) - ctu::log2_v<sizeof(typename type::value_type)>) };
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                bool result = q.write();
                counter += int(result);
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));
    } else {
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                auto ptr = q.frontPtr();
                if (ptr != nullptr) {
                    q.popFront();
                    counter += 1;
                }
            }
        }

        if (q.isEmpty() == false) {
            state.SkipWithError("Not Empty after test");
        }

        delete queue;
        queue = nullptr;
    }
}

BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<15>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<16>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<17>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<18>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<19>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<20>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<21>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<22>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<23>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<24>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<25>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<26>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<27>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<28>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<29>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<30>)->Apply(configure_benchmark);

BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<15>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<16>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<17>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<18>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<19>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<20>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<21>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<22>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<23>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<24>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<25>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<26>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<27>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<28>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<29>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached<30>)->Apply(configure_benchmark);

BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<15>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<16>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<17>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<18>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<19>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<20>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<21>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<22>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<23>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<24>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<25>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<26>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<27>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<28>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<29>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_heap<30>)->Apply(configure_benchmark);

template<size_t size>
struct DummyContainer {
    char dummy[size];
};

BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueue<DummyContainer<8>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueue<DummyContainer<16>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueue<DummyContainer<24>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueue<DummyContainer<32>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueue<DummyContainer<40>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueue<DummyContainer<48>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueue<DummyContainer<56>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueue<DummyContainer<64>>)->Apply(configure_folly_queue);

BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueueCached<DummyContainer<8>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueueCached<DummyContainer<16>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueueCached<DummyContainer<24>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueueCached<DummyContainer<32>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueueCached<DummyContainer<40>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueueCached<DummyContainer<48>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueueCached<DummyContainer<56>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyQueue, folly::ProducerConsumerQueueCached<DummyContainer<64>>)->Apply(configure_folly_queue);

BENCHMARK_MAIN();
