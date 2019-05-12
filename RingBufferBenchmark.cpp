#include <benchmark/benchmark.h>
#include "spsc_queue.hpp"
#include "spsc_ring_buffer.hpp"
#include "spsc_ring_buffer_cached.hpp"
#include "spsc_ring_buffer_heap.hpp"
#include "follyProducerConsumerQueue.h"
#include "rigtorpSPSCQueue.h"
#include "moodycamel/readwriterqueue.h"
#include <immintrin.h>
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

void configure_queue(benchmark::internal::Benchmark* bench) {
    bench->Threads(2);
    bench->Repetitions(200);
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

template<typename type>
static void Queue(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue = new type{};
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                bool result = q.produce();
                counter += int(result);
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));
    } else {
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                counter += int(q.consume([](typename type::value_type*){ return true; }));
            }
        }

        if (q.is_empty() == false) {
            state.SkipWithError("Not Empty after test");
        }

        delete queue;
        queue = nullptr;
    }
}

template<typename type>
static void RigtorpQueue(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue = new type{size_t(1) << (state.range(0) - ctu::log2_v<sizeof(typename type::value_type)>)};
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                bool result = q.try_emplace();
                counter += int(result);
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));
    } else {
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                auto ptr = q.front();
                if (ptr != nullptr) {
                    q.pop();
                    counter += 1;
                }
            }
        }

        if (q.empty() == false) {
            state.SkipWithError("Not Empty after test");
        }

        delete queue;
        queue = nullptr;
    }
}

template<typename type>
static void MoodycamelQueue(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue = new type{(size_t(1) << (state.range(0) - ctu::log2_v<sizeof(typename type::value_type)>)) - 1};
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                bool result = q.try_emplace();
                counter += int(result);
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));
    } else {
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                auto ptr = q.peek();
                if (ptr != nullptr) {
                    q.pop();
                    counter += 1;
                }
            }
        }

        if (q.size_approx() != 0) {
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

BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<15>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<16>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<17>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<18>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<19>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<20>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<21>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<22>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<23>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<24>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<25>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<26>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<27>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<28>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<29>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_masked<30>)->Apply(configure_benchmark);

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

BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<15>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<16>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<17>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<18>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<19>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<20>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<21>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<22>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<23>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<24>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<25>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<26>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<27>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<28>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<29>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_cached_masked<30>)->Apply(configure_benchmark);

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

template<size_t>
struct DummyContainer;

template<>
struct DummyContainer<64> {
    char dummy[64];
    DummyContainer() {
        _mm256_storeu_ps((float*)dummy, _mm256_setzero_ps());
        _mm256_storeu_ps((float*)(dummy + 32), _mm256_setzero_ps());
    }
};

template<>
struct DummyContainer<56> {
    char dummy[56];
    DummyContainer() {
        _mm256_storeu_ps((float*)dummy, _mm256_setzero_ps());
        _mm_storeu_ps((float*)(dummy + 32), _mm_setzero_ps());
        *((uint64_t*)(dummy + 48)) = 0;
    }
};

template<>
struct DummyContainer<48> {
    char dummy[48];
    DummyContainer() {
        _mm256_storeu_ps((float*)dummy, _mm256_setzero_ps());
        _mm_storeu_ps((float*)(dummy + 32), _mm_setzero_ps());
    }
};

template<>
struct DummyContainer<40> {
    char dummy[40];
    DummyContainer() {
        _mm256_storeu_ps((float*)dummy, _mm256_setzero_ps());
        *((uint64_t*)(dummy + 32)) = 0;
    }
};

template<>
struct DummyContainer<32> {
    char dummy[32];
    DummyContainer() {
        _mm256_storeu_ps((float*)dummy, _mm256_setzero_ps());
    }
};

template<>
struct DummyContainer<24> {
    char dummy[24];
    DummyContainer() {
        _mm_storeu_ps((float*)dummy, _mm_setzero_ps());
        *((uint64_t*)(dummy + 16)) = 0;
    }
};

template<>
struct DummyContainer<16> {
    char dummy[16];
    DummyContainer() {
        _mm_storeu_ps((float*)dummy, _mm_setzero_ps());
    }
};

template<>
struct DummyContainer<8> {
    char dummy[8]{};
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

#if 1
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 26>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<8>, 27>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<16>, 26>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<24>, 26>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<32>, 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<40>, 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<48>, 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<56>, 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>,  9>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue<DummyContainer<64>, 24>)->Apply(configure_queue);
#endif

//
// 
//

#if 1
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 26>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<8>, 27>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<16>, 26>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<24>, 26>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<32>, 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<40>, 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<48>, 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<56>, 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 9>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached<DummyContainer<64>, 24>)->Apply(configure_queue);

#endif

//
//
//

#if 1
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 26>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, size_t(1) << 27>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, size_t(1) << 26>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, size_t(1) << 26>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, size_t(1) << 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, size_t(1) << 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, size_t(1) << 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, size_t(1) << 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 9>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, size_t(1) << 24>)->Apply(configure_queue);
#endif

BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<8>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<16>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<24>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<32>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<40>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<48>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<56>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<64>>)->Apply(configure_folly_queue);

BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueueCached<DummyContainer<8>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueueCached<DummyContainer<16>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueueCached<DummyContainer<24>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueueCached<DummyContainer<32>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueueCached<DummyContainer<40>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueueCached<DummyContainer<48>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueueCached<DummyContainer<56>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueueCached<DummyContainer<64>>)->Apply(configure_folly_queue);

BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<8>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<16>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<24>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<32>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<40>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<48>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<56>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<64>>)->Apply(configure_folly_queue);

BENCHMARK_MAIN();
