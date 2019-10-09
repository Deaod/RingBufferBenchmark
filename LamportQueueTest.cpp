#include "LamportQueue2.hpp"
#include "LamportQueue3.hpp"
#include "LamportQueue4.hpp"
#include "LamportQueue5.hpp"
#include "LamportQueue6.hpp"
#include "LamportQueue7.hpp"
#include "BenchmarkSupport.hpp"
#include "Platform.hpp"
#include <new>

template<typename type>
static void LamportQueueTest(benchmark::State& state) {
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

QUEUE_BENCH(LamportQueueTest, LamportQueue2);
QUEUE_BENCH(LamportQueueTest, LamportQueue3);
QUEUE_BENCH(LamportQueueTest, LamportQueue4);
QUEUE_BENCH(LamportQueueTest, LamportQueue5);
QUEUE_BENCH(LamportQueueTest, LamportQueue6);
QUEUE_BENCH(LamportQueueTest, LamportQueue7);
