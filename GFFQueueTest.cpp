#include "GFFQueue1.hpp"
#include "GFFQueue2.hpp"
#include "GFFQueue3.hpp"
#include "GFFQueue4.hpp"
#include "GFFQueue5.hpp"
#include "BenchmarkSupport.hpp"
#include "Platform.hpp"
#include <new>

template<typename type>
static void GFFQueueTest(benchmark::State& state) {
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

QUEUE_BENCH(GFFQueueTest, GFFQueue1);
QUEUE_BENCH(GFFQueueTest, GFFQueue2);
QUEUE_BENCH(GFFQueueTest, GFFQueue3);
QUEUE_BENCH(GFFQueueTest, GFFQueue4);
QUEUE_BENCH(GFFQueueTest, GFFQueue5);
