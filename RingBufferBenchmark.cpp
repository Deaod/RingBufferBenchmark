#include <benchmark/benchmark.h>
#include "ce_queue.hpp"
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
#include "DummyContainer.hpp"
#include "Platform.hpp"
#include "BenchmarkSupport.hpp"
#include <chrono>
#include <thread>
#include <new>
#include <array>
#include <memory>

constexpr bool WANT_BACKGROUND_LOAD = false;

template<typename type>
static void QueuePushPop(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue = new type{};
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        PREPARE_THREAD(Thread1Affinity);
        typename type::value_type elem{};
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.push(elem));
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));
    } else if (state.thread_index == 1) {
        PREPARE_THREAD(Thread2Affinity);
        typename type::value_type elem{};
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.pop(elem));
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));

        if (q.is_empty() == false) {
            state.SkipWithError("Not Empty after test");
        }

        delete queue;
        queue = nullptr;
    } else {
        PREPARE_THREAD(Thread2Affinity);
        while (queue.load() != nullptr) {}
    }
}

template<typename type>
static void QueueEmplacePop(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        auto p = new type{};
        queue = p;
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        PREPARE_THREAD(Thread1Affinity);
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.emplace());
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));
    } else if (state.thread_index == 1) {
        PREPARE_THREAD(Thread2Affinity);
        typename type::value_type elem{};
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.pop(elem));
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));

        //typename type::value_type e;
        //while (q.pop(e)) {}
        //if (q.is_empty() == false) {
        //    state.SkipWithError("Not Empty after test");
        //}

        delete queue;
        queue = nullptr;
    } else {
        PREPARE_THREAD(Thread2Affinity);
        while (queue.load() != nullptr) {}
    }
}

template<typename type>
static void QueueEmplaceConsume(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        queue = new type{};
    } else {
        while (queue.load(std::memory_order_relaxed) == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        PREPARE_THREAD(Thread1Affinity);
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.emplace());
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));
    } else if (state.thread_index == 1) {
        PREPARE_THREAD(Thread2Affinity);
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.consume([](typename type::value_type&) { return true; }));
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));

        if (q.is_empty() == false) {
            state.SkipWithError("Not Empty after test");
        }

        delete queue;
        queue = nullptr;
    } else {
        PREPARE_THREAD(Thread2Affinity);
        while (queue.load() != nullptr) {}
    }
}

template<typename type>
static void QueueEmplaceDiscard(benchmark::State& state) {
    static std::atomic<type*> queue = nullptr;

    if (state.thread_index == 0) {
        type* p = new type{};
        while (p->emplace()) {}
        queue = p;
    } else {
        while (queue.load() == nullptr) {}
    }

    type& q = *queue;
    if (state.thread_index == 0) {
        PREPARE_THREAD(Thread1Affinity);
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                counter -= int(q.emplace());
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));
    } else if (state.thread_index == 1) {
        PREPARE_THREAD(Thread2Affinity);
        for (auto _ : state) {
            int counter = 10000;
            while (counter > 0) {
                typename type::value_type* p = q.front();
                if (p) {
                    q.discard();
                    counter -= 1;
                }
            }
        }
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * sizeof(typename type::value_type));

        while (q.front()) {
            q.discard();
        }
        
        if (q.is_empty() == false) {
            state.SkipWithError("Not Empty after test");
        }

        delete queue;
        queue = nullptr;
    } else {
        PREPARE_THREAD(Thread2Affinity);
        while (queue.load() == nullptr) {}
        for (auto _ : state) {
        }
        while (queue.load() != nullptr) {}
    }
}

template<typename T, size_t S>
struct folly_pcq_adapter : folly::ProducerConsumerQueue<T> {

    using base_type = folly::ProducerConsumerQueue<T>;

    folly_pcq_adapter() : base_type(S) {}

    bool push(const T& e) {
        return this->write(e);
    }

    template<typename... Args>
    bool emplace(Args&&... args) {
        return this->write(std::forward<Args>(args)...);
    }

    T* front() {
        return this->frontPtr();
    }

    void discard() {
        this->popFront();
    }

    bool pop(T& e) {
        return this->read(e);
    }

    bool is_empty() {
        return this->isEmpty();
    }
};

template<typename T, size_t S>
struct boost_adapter : boost::lockfree::spsc_queue<T> {

    using base_type = boost::lockfree::spsc_queue<T>;

    boost_adapter() : base_type(S) {}

    template<typename... Args>
    bool emplace(Args&&... args) {
        return this->push(T{ std::forward<Args>(args)... });
    }

    template<typename Callable>
    bool consume(Callable&& f) {
        return this->consume_one(f);
    }

    bool is_empty() {
        return this->empty();
    }

    T* front() {
        if (this->read_available() > 0) {
            return &base_type::front();
        }
        return nullptr;
    }

    void discard() {
        this->pop();
    }
};

template<typename T, size_t S>
struct moodycamel_adapter : moodycamel::ReaderWriterQueue<T> {

    using base_type = moodycamel::ReaderWriterQueue<T>;

    moodycamel_adapter() : base_type(S) {};

    bool push(const T& e) {
        return this->try_enqueue(e);
    }

    template<typename... Args>
    bool emplace(Args&&... args) {
        return this->try_emplace(std::forward<Args>(args)...);
    }

    template<typename Callable>
    bool consume(Callable&& f) {
        T* p = this->peek();
        if (p) {
            std::forward<Callable>(f)(*p);
            return this->pop();
        }
        return false;
    }

    T* front() {
        return this->peek();
    }

    void discard() {
        this->pop();
    }

    using base_type::pop;
    bool pop(T& e) {
        return this->try_dequeue(e);
    }

    bool is_empty() {
        return this->size_approx() == 0;
    }
};

//QUEUE_BENCH(QueuePushPop, folly_pcq_adapter);
//QUEUE_BENCH(QueuePushPop, boost_adapter);
//QUEUE_BENCH(QueuePushPop, deaod::spsc_queue);
//QUEUE_BENCH(QueuePushPop, spsc_queue_chunked_ptr);
//QUEUE_BENCH(QueuePushPop, moodycamel_adapter);
//
//QUEUE_BENCH(QueueEmplacePop, folly_pcq_adapter);
//QUEUE_BENCH(QueueEmplacePop, deaod::spsc_queue);
//QUEUE_BENCH(QueueEmplacePop, spsc_queue_chunked_ptr);
//QUEUE_BENCH(QueueEmplacePop, moodycamel_adapter);
//
//QUEUE_BENCH(QueueEmplaceConsume, folly_pcq_adapter);
//QUEUE_BENCH(QueueEmplaceConsume, boost_adapter);
//QUEUE_BENCH(QueueEmplaceConsume, deaod::spsc_queue);
//QUEUE_BENCH(QueueEmplaceConsume, spsc_queue_chunked_ptr);
//
//QUEUE_BENCH(QueueEmplaceDiscard, folly_pcq_adapter);
//QUEUE_BENCH(QueueEmplaceDiscard, boost_adapter);
//QUEUE_BENCH(QueueEmplaceDiscard, deaod::spsc_queue);
//QUEUE_BENCH(QueueEmplaceDiscard, moodycamel_adapter);

int main(int argc, char** argv) {
    PREPARE_PROCESS();

    std::atomic_bool b{ WANT_BACKGROUND_LOAD == false };
    std::thread load{ [&b] {
        PREPARE_THREAD(Thread2Affinity);
        while (b.load() == false) {}
    } };

    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    ::benchmark::RunSpecifiedBenchmarks();
    
    b.store(true);
    load.join();
}
