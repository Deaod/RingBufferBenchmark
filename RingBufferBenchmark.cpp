#include <benchmark/benchmark.h>
#include "spsc_queue.hpp"
#include "spsc_queue_release.hpp"
#include "spsc_ring_buffer.hpp"
#include "spsc_ring_buffer_cached.hpp"
#include "spsc_ring_buffer_chunked.hpp"
#include "spsc_ring_buffer_heap.hpp"
#include <folly/ProducerConsumerQueue.h>
#include <folly/concurrency/UnboundedQueue.h>
#include <folly/concurrency/DynamicBoundedQueue.h>
#include <boost/lockfree/spsc_queue.hpp>
#include "rigtorpSPSCQueue.h"
#include "moodycamel/readwriterqueue.h"
#include <immintrin.h>
#include <chrono>
#include <thread>
#include <new>
#include <array>
#include <memory>

#if defined(_WIN32)
#include <Windows.h>
#define ASSIGN_THREAD_AFFINITY(affinity) do { SetThreadAffinityMask(GetCurrentThread(), affinity); } while(0)
#else
#define ASSIGN_THREAD_AFFINITY(affinity) do { } while(0)
#endif

constexpr uint64_t Thread1Affinity = 1 << 0;
constexpr uint64_t Thread2Affinity = 1 << 2;

void configure_benchmark(benchmark::internal::Benchmark* bench) {
    bench->Threads(2);
    bench->Repetitions(200);

    bench->ArgNames({ "Elem Size" });

    bench->Args({ 8 });
    bench->Args({ 16 });
    bench->Args({ 24 });
    bench->Args({ 32 });
    bench->Args({ 40 });
    bench->Args({ 48 });
    bench->Args({ 56 });
    bench->Args({ 72 });
    bench->Args({ 88 });
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
    static std::atomic<type*> buffer = nullptr;

    if (state.thread_index == 0) {
        buffer = new type{};
    } else {
        while (buffer.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& b = *buffer;
    if (state.thread_index == 0) {
        ASSIGN_THREAD_AFFINITY(Thread1Affinity);
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
        ASSIGN_THREAD_AFFINITY(Thread2Affinity);
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                bool result = b.consume([](const void*, ptrdiff_t) { return true; });
                counter += int(result);
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * state.range(0));

        if (b.is_empty() == false) {
            state.SkipWithError("Not Empty after test");
        }

        delete buffer;
        buffer = nullptr;
    }
}

template<typename type>
static void FollyQueue(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue = new type{ (uint32_t(1) << state.range(0)) / uint32_t(sizeof(typename type::value_type)) };
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        ASSIGN_THREAD_AFFINITY(Thread1Affinity);
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
        ASSIGN_THREAD_AFFINITY(Thread2Affinity);
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
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));

        if (q.isEmpty() == false) {
            state.SkipWithError("Not Empty after test");
        }

        delete queue;
        queue = nullptr;
    }
}

template<typename type, typename element_type>
static void FollyUnboundedQueue(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue = new type{};
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        ASSIGN_THREAD_AFFINITY(Thread1Affinity);
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                q.enqueue(element_type{});
                counter += 1;
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(element_type));
    } else {
        ASSIGN_THREAD_AFFINITY(Thread2Affinity);
        element_type e;
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                if (q.try_dequeue().has_value()) {
                    counter += 1;
                }
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(element_type));

        if (q.empty() == false) {
            state.SkipWithError("Not Empty after test");
        }

        delete queue;
        queue = nullptr;
    }
}

template<typename type, typename element_type>
static void FollyDBQueue(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue = new type{ (uint32_t(1) << state.range(0)) / uint32_t(sizeof(element_type)) };
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        ASSIGN_THREAD_AFFINITY(Thread1Affinity);
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                counter += int(q.try_enqueue(element_type{}));
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(element_type));
    } else {
        ASSIGN_THREAD_AFFINITY(Thread2Affinity);
        element_type e;
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                if (q.try_dequeue(e)) {
                    counter += 1;
                }
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(element_type));

        if (q.empty() == false) {
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
        ASSIGN_THREAD_AFFINITY(Thread1Affinity);
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
        ASSIGN_THREAD_AFFINITY(Thread2Affinity);
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                counter += int(q.consume([](typename type::value_type*) { return true; }));
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));

        if (q.is_empty() == false) {
            state.SkipWithError("Not Empty after test");
        }

        delete queue;
        queue = nullptr;
    }
}

template<typename type>
static void QueueRel(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue = new type{};
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        ASSIGN_THREAD_AFFINITY(Thread1Affinity);
        auto t = std::make_unique<std::array<typename type::value_type, 10000>>();
        auto d = t->data();
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.emplace());
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));
    } else {
        ASSIGN_THREAD_AFFINITY(Thread2Affinity);
        auto t = std::make_unique<std::array<typename type::value_type, 10000>>();
        auto d = t->data();
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.consume([](typename type::value_type*) { return true; }));
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));

        if (q.is_empty() == false) {
            state.SkipWithError("Not Empty after test");
        }

        delete queue;
        queue = nullptr;
    }
}

template<typename type>
static void QueueMultiple(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue = new type{};
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        ASSIGN_THREAD_AFFINITY(Thread1Affinity);
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
        ASSIGN_THREAD_AFFINITY(Thread2Affinity);
        int64_t counter = 0;
        int64_t target = 0;
        for (auto _ : state) {
            target += 10000;
            while (counter < target) {
                counter += q.consume_all([](typename type::value_type* val) { return true; });
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));

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
        queue = new type{ (uint32_t(1) << state.range(0)) / uint32_t(sizeof(typename type::value_type)) };
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        ASSIGN_THREAD_AFFINITY(Thread1Affinity);
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
        ASSIGN_THREAD_AFFINITY(Thread2Affinity);
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
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));

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
        queue = new type{ (uint32_t(1) << state.range(0)) / uint32_t(sizeof(typename type::value_type)) };
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        ASSIGN_THREAD_AFFINITY(Thread1Affinity);
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
        ASSIGN_THREAD_AFFINITY(Thread2Affinity);
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
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));

        if (q.size_approx() != 0) {
            state.SkipWithError("Not Empty after test");
        }

        delete queue;
        queue = nullptr;
    }
}

template<typename type>
static void BoostQueue(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue = new type{ (uint32_t(1) << state.range(0)) / uint32_t(sizeof(typename type::value_type)) };
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        ASSIGN_THREAD_AFFINITY(Thread1Affinity);
        typename type::value_type value{};
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                bool result = q.push(value);
                counter += int(result);
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));
    } else {
        ASSIGN_THREAD_AFFINITY(Thread2Affinity);
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                bool result = q.consume_one([](typename type::value_type&){});
                counter += int(result);
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));

        if (q.empty() == false) {
            state.SkipWithError("Not Empty after test");
        }

        delete queue;
        queue = nullptr;
    }
}

template<typename queue_type>
static void QueueLatency(benchmark::State& state) {
    constexpr auto LoadBias = 0;
    
    static std::atomic<queue_type*> queue1 = nullptr;
    static std::atomic<queue_type*> queue2 = nullptr;

    if (state.thread_index == 0) {
        queue1 = new queue_type{};
        for (int i = 0; i < LoadBias; i += 1) queue1.load()->produce();
        while (queue2.load() == nullptr) {}
    } else {
        queue2 = new queue_type{};
        for (int i = 0; i < LoadBias; i += 1) queue2.load()->produce();
        while (queue1.load() == nullptr) {}
    }

    queue_type& q1 = *queue1;
    queue_type& q2 = *queue2;
    if (state.thread_index == 0) {
        ASSIGN_THREAD_AFFINITY(Thread1Affinity);
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                q1.produce();
                while (false == q2.consume([](typename queue_type::value_type*) { return true; }));
                counter += 1;
            }
        }

        //if (q1.is_empty() == false || q2.is_empty() == false) {
        //    state.SkipWithError("Not Empty after test");
        //}

        delete queue1;
        delete queue2;
        queue1 = nullptr;
        queue2 = nullptr;
    } else {
        ASSIGN_THREAD_AFFINITY(Thread2Affinity);
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                while (false == q1.consume([](typename queue_type::value_type*) { return true; }));
                q2.produce();
                counter += 1;
            }
        }
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

BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<15>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<16>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<17>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<18>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<19>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<20>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<21>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<22>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<23>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<24>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<25>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<26>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<27>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<28>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<29>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_chunked<30>)->Apply(configure_benchmark);

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

template<size_t n>
struct DummyContainer {
    char dummy[n]{};
};

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
        __m256 a = _mm256_setzero_ps();
        _mm256_storeu_ps((float*)dummy, a);
        _mm256_storeu_ps((float*)(dummy + 24), a);
    }
};

template<>
struct DummyContainer<48> {
    char dummy[48];
    DummyContainer() {
        __m256 a = _mm256_setzero_ps();
        _mm256_storeu_ps((float*)dummy, a);
        _mm_storeu_ps((float*)(dummy + 32), _mm256_castps256_ps128(a));
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

#if 1
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(FollyUnboundedQueue, folly::USPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_queue);
#endif

BENCHMARK_TEMPLATE(FollyDBQueue, folly::DSPSCQueue<DummyContainer<8>, false, 12>, DummyContainer<8>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyDBQueue, folly::DSPSCQueue<DummyContainer<16>, false, 11>, DummyContainer<16>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyDBQueue, folly::DSPSCQueue<DummyContainer<24>, false, 10>, DummyContainer<24>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyDBQueue, folly::DSPSCQueue<DummyContainer<32>, false, 10>, DummyContainer<32>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyDBQueue, folly::DSPSCQueue<DummyContainer<40>, false, 9>, DummyContainer<40>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyDBQueue, folly::DSPSCQueue<DummyContainer<48>, false, 9>, DummyContainer<48>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyDBQueue, folly::DSPSCQueue<DummyContainer<56>, false, 9>, DummyContainer<56>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(FollyDBQueue, folly::DSPSCQueue<DummyContainer<64>, false, 9>, DummyContainer<64>)->Apply(configure_folly_queue);

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

#if 1
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 26>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<8>, 27>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<16>, 26>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<24>, 26>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<32>, 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<40>, 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<48>, 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<56>, 25>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 9>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 10>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 11>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_cached_ptr<DummyContainer<64>, 24>)->Apply(configure_queue);
#endif

#if 1
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 15) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 16) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 17) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 18) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 19) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 20) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 21) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 22) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 23) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 24) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 25) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 26) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 27) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 28) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 29) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<8>, (size_t(1) << 30) / 8>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 15) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 16) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 17) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 18) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 19) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 20) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 21) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 22) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 23) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 24) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 25) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 26) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 27) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 28) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 29) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<16>, (size_t(1) << 30) / 16>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 15) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 16) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 17) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 18) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 19) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 20) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 21) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 22) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 23) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 24) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 25) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 26) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 27) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 28) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 29) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<24>, (size_t(1) << 30) / 24>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 15) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 16) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 17) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 18) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 19) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 20) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 21) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 22) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 23) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 24) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 25) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 26) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 27) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 28) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 29) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<32>, (size_t(1) << 30) / 32>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 15) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 16) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 17) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 18) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 19) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 20) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 21) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 22) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 23) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 24) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 25) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 26) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 27) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 28) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 29) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<40>, (size_t(1) << 30) / 40>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 15) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 16) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 17) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 18) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 19) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 20) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 21) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 22) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 23) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 24) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 25) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 26) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 27) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 28) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 29) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<48>, (size_t(1) << 30) / 48>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 15) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 16) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 17) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 18) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 19) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 20) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 21) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 22) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 23) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 24) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 25) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 26) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 27) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 28) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 29) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<56>, (size_t(1) << 30) / 56>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 15) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 16) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 17) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 18) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 19) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 20) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 21) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 22) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 23) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 24) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 25) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 26) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 27) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 28) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 29) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked<DummyContainer<64>, (size_t(1) << 30) / 64>)->Apply(configure_queue);
#endif

#if 1
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 15) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 16) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 17) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 18) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 19) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 20) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 21) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 22) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 23) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 24) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 25) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 26) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 27) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 28) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 29) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<8>, (size_t(1) << 30) / 8>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 15) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 16) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 17) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 18) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 19) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 20) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 21) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 22) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 23) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 24) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 25) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 26) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 27) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 28) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 29) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<16>, (size_t(1) << 30) / 16>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 15) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 16) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 17) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 18) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 19) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 20) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 21) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 22) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 23) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 24) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 25) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 26) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 27) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 28) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 29) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<24>, (size_t(1) << 30) / 24>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 15) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 16) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 17) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 18) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 19) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 20) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 21) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 22) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 23) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 24) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 25) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 26) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 27) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 28) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 29) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<32>, (size_t(1) << 30) / 32>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 15) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 16) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 17) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 18) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 19) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 20) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 21) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 22) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 23) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 24) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 25) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 26) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 27) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 28) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 29) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<40>, (size_t(1) << 30) / 40>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 15) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 16) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 17) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 18) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 19) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 20) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 21) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 22) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 23) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 24) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 25) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 26) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 27) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 28) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 29) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<48>, (size_t(1) << 30) / 48>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 15) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 16) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 17) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 18) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 19) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 20) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 21) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 22) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 23) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 24) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 25) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 26) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 27) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 28) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 29) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<56>, (size_t(1) << 30) / 56>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 15) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 16) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 17) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 18) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 19) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 20) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 21) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 22) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 23) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 24) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 25) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 26) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 27) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 28) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 29) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(Queue, spsc_queue_chunked_ptr<DummyContainer<64>, (size_t(1) << 30) / 64>)->Apply(configure_queue);
#endif

BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<8>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<16>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<24>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<32>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<40>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<48>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<56>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(RigtorpQueue, rigtorp::SPSCQueue<DummyContainer<64>>)->Apply(configure_folly_queue);

BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<8>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<16>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<24>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<32>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<40>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<48>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<56>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(MoodycamelQueue, moodycamel::ReaderWriterQueue<DummyContainer<64>>)->Apply(configure_folly_queue);

BENCHMARK_TEMPLATE(BoostQueue, boost::lockfree::spsc_queue<DummyContainer<8>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(BoostQueue, boost::lockfree::spsc_queue<DummyContainer<16>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(BoostQueue, boost::lockfree::spsc_queue<DummyContainer<24>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(BoostQueue, boost::lockfree::spsc_queue<DummyContainer<32>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(BoostQueue, boost::lockfree::spsc_queue<DummyContainer<40>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(BoostQueue, boost::lockfree::spsc_queue<DummyContainer<48>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(BoostQueue, boost::lockfree::spsc_queue<DummyContainer<56>>)->Apply(configure_folly_queue);
BENCHMARK_TEMPLATE(BoostQueue, boost::lockfree::spsc_queue<DummyContainer<64>>)->Apply(configure_folly_queue);

BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 26>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue<uint64_t, 27>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 26>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached<uint64_t, 27>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 26>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_cached_ptr<uint64_t, 27>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 26>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked<uint64_t, size_t(1) << 27>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 12>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 13>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 14>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 15>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 17>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 18>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 19>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 20>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 21>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 22>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 23>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 25>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 26>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueLatency, spsc_queue_chunked_ptr<uint64_t, size_t(1) << 27>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 15) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 16) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 17) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 18) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 19) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 20) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 21) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 22) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 23) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 24) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 25) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 26) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 27) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 28) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 29) / 8>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<8>, (size_t(1) << 30) / 8>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 15) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 16) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 17) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 18) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 19) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 20) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 21) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 22) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 23) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 24) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 25) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 26) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 27) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 28) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 29) / 16>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<16>, (size_t(1) << 30) / 16>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 15) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 16) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 17) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 18) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 19) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 20) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 21) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 22) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 23) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 24) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 25) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 26) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 27) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 28) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 29) / 24>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<24>, (size_t(1) << 30) / 24>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 15) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 16) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 17) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 18) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 19) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 20) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 21) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 22) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 23) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 24) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 25) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 26) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 27) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 28) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 29) / 32>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<32>, (size_t(1) << 30) / 32>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 15) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 16) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 17) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 18) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 19) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 20) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 21) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 22) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 23) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 24) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 25) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 26) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 27) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 28) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 29) / 40>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<40>, (size_t(1) << 30) / 40>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 15) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 16) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 17) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 18) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 19) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 20) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 21) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 22) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 23) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 24) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 25) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 26) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 27) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 28) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 29) / 48>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<48>, (size_t(1) << 30) / 48>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 15) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 16) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 17) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 18) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 19) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 20) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 21) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 22) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 23) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 24) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 25) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 26) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 27) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 28) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 29) / 56>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<56>, (size_t(1) << 30) / 56>)->Apply(configure_queue);

BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 15) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 16) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 17) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 18) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 19) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 20) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 21) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 22) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 23) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 24) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 25) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 26) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 27) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 28) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 29) / 64>)->Apply(configure_queue);
BENCHMARK_TEMPLATE(QueueRel, deaod::spsc_queue<DummyContainer<64>, (size_t(1) << 30) / 64>)->Apply(configure_queue);

BENCHMARK_MAIN();
