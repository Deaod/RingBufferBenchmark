#include <benchmark/benchmark.h>
#include "spsc_ring_buffer.hpp"
#include "aligned_alloc.hpp"
#include <chrono>
#include <thread>
#include <new>

constexpr int buffer_size_log2 = 16;
constexpr int content_align_log2 = 3;

void configure_benchmark(benchmark::internal::Benchmark* bench) {
    bench->Threads(2);

    bench->ArgNames({"Size"});

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
    bench->Args({ 312 });
    bench->Args({ 344 });
    bench->Args({ 376 });
    bench->Args({ 408 });
    bench->Args({ 440 });
    bench->Args({ 472 });
    bench->Args({ 504 });
}

template<typename type>
static void RingBuffer(benchmark::State& state) {
    static auto* buffer = new(aligned_alloc(type::align, sizeof(type))) type{};

    auto& b = *buffer;
    size_t calls = 0;
    if (state.thread_index == 0) {        
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                bool result = b.produce(state.range(0), [](void*) { return true; });
                counter += int(result);
                //calls += 1;
                //if (result == false) {
                //    state.PauseTiming();
                //    _sleep(0);
                //    //std::this_thread::sleep_for(std::chrono::seconds(0));
                //    state.ResumeTiming();
                //}
            }
            
        }
        //state.counters["produce_calls"].value += calls - state.iterations() * 10000;
        state.SetItemsProcessed(state.iterations() * 10000);
        state.SetBytesProcessed(state.iterations() * 10000 * state.range(0));
    } else {
        for (auto _ : state) {
            int counter = 0;
            while (counter < 10000) {
                bool result = b.consume([](const void*, ptrdiff_t) { return true; });
                counter += int(result);
                //calls += 1;
                //if (result == false) {
                //    state.PauseTiming();
                //    _sleep(0);
                //    //std::this_thread::sleep_for(std::chrono::milliseconds(0));
                //    state.ResumeTiming();
                //}
            }
        }
        //state.counters["consume_calls"].value += calls - state.iterations() * 10000;
        //state.SetItemsProcessed(state.iterations() * 10000);
    }

    if (b.is_empty() == false) {
        state.SkipWithError("Not Empty after test");
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

BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<15>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<16>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<17>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<18>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<19>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<20>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<21>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<22>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<23>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<24>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<25>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<26>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<27>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<28>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<29>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<30>)->Apply(configure_benchmark);

BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<15>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<16>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<17>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<18>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<19>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<20>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<21>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<22>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<23>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<24>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<25>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<26>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<27>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<28>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<29>)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<30>)->Apply(configure_benchmark);

BENCHMARK_MAIN();
