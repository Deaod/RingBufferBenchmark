#include "ChunkedQueue1.hpp"
#include "ChunkedQueue2.hpp"
#include "ChunkedQueue3.hpp"
#include "ChunkedQueue4.hpp"
#include "ChunkedQueue5.hpp"
#include "ChunkedQueue6.hpp"
#include "ChunkedQueue7.hpp"
#include "BenchmarkSupport.hpp"
#include "Platform.hpp"
#include <new>

template<typename type>
static void ChunkedQueueTest(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue.store(new type{});
    } else {
        while (queue.load() == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        PREPARE_THREAD(Thread1Affinity);
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.Enqueue());
            }
        }
    } else if (state.thread_index == 1) {
        PREPARE_THREAD(Thread2Affinity);
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.Dequeue([](typename type::value_type&&) {}));
            }
        }

        delete queue.load();
        queue.store(nullptr);
    }
    state.SetItemsProcessed(state.iterations() * 10000);
    state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));
}

QUEUE_BENCH(ChunkedQueueTest, ChunkedQueue1);
QUEUE_BENCH(ChunkedQueueTest, ChunkedQueue2);
QUEUE_BENCH(ChunkedQueueTest, ChunkedQueue3);
QUEUE_BENCH(ChunkedQueueTest, ChunkedQueue4);
QUEUE_BENCH(ChunkedQueueTest, ChunkedQueue5);
QUEUE_BENCH(ChunkedQueueTest, ChunkedQueue6);
QUEUE_BENCH(ChunkedQueueTest, ChunkedQueue7);
