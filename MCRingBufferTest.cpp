#include "MCRingBuffer1.hpp"
#include "MCRingBuffer2.hpp"
#include "MCRingBuffer3.hpp"
#include "MCRingBuffer4.hpp"
#include "MCRingBuffer5.hpp"
#include "MCRingBuffer6.hpp"
#include "MCRingBuffer7.hpp"
#include "BenchmarkSupport.hpp"
#include "Platform.hpp"
#include <new>

template<typename type>
static void MCRingBufferTest(benchmark::State& state) {
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

QUEUE_BENCH(MCRingBufferTest, MCRingBuffer1);
QUEUE_BENCH(MCRingBufferTest, MCRingBuffer2);
QUEUE_BENCH(MCRingBufferTest, MCRingBuffer3);
QUEUE_BENCH(MCRingBufferTest, MCRingBuffer4);
QUEUE_BENCH(MCRingBufferTest, MCRingBuffer5);
QUEUE_BENCH(MCRingBufferTest, MCRingBuffer6);
QUEUE_BENCH(MCRingBufferTest, MCRingBuffer7);
