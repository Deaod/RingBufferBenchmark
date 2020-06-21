#include "FastForward1.hpp"
#include "FastForward2.hpp"
#include "FastForward3.hpp"
#include "FastForward4.hpp"
#include "FastForward5.hpp"
#include "FastForward6.hpp"
#include "BenchmarkSupport.hpp"
#include "Platform.hpp"
#include <new>

template<typename type>
static void FastForwardTest(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue.store(new type{});
    } else {
        while (queue.load() == nullptr) {}
    }

    static typename type::value_type value{};
    type& q = *queue;
    if (state.thread_index == 0) {
        PREPARE_THREAD(Thread1Affinity);
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.Enqueue(&value));
            }
        }
    } else if (state.thread_index == 1) {
        PREPARE_THREAD(Thread2Affinity);
        typename type::value_type* out;
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.Dequeue(out));
            }
        }

        delete queue.load();
        queue.store(nullptr);
    }
    state.SetItemsProcessed(state.iterations() * 10000);
    state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));
}

QUEUE_BENCH_FOR_SIZE(FastForwardTest, FastForward1, 8);
QUEUE_BENCH_FOR_SIZE(FastForwardTest, FastForward2, 8);
QUEUE_BENCH_FOR_SIZE(FastForwardTest, FastForward3, 8);
QUEUE_BENCH_FOR_SIZE(FastForwardTest, FastForward4, 8);
QUEUE_BENCH_FOR_SIZE(FastForwardTest, FastForward5, 8);
QUEUE_BENCH_FOR_SIZE(FastForwardTest, FastForward6, 8);
