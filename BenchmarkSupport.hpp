#pragma once

#include <benchmark/benchmark.h>
#include "DummyContainer.hpp"
#include <cstddef>

inline void configure_queue(benchmark::internal::Benchmark* bench) {
    bench->Threads(2);
    bench->Repetitions(200);
}

#define QUEUE_BENCH_FOR_SIZE(Func, Template, Size)                                                                  \
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 12) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 13) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 14) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 15) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 16) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 17) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 18) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 19) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 20) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 21) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 22) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 23) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 24) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 25) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 26) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 27) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 28) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 29) / Size>)->Apply(configure_queue);\
    BENCHMARK_TEMPLATE(Func, Template<DummyContainer<Size>, (std::size_t(1) << 30) / Size>)->Apply(configure_queue);

#define QUEUE_BENCH(Func, Template)           \
    QUEUE_BENCH_FOR_SIZE(Func, Template,  8); \
    QUEUE_BENCH_FOR_SIZE(Func, Template, 16); \
    QUEUE_BENCH_FOR_SIZE(Func, Template, 24); \
    QUEUE_BENCH_FOR_SIZE(Func, Template, 32); \
    QUEUE_BENCH_FOR_SIZE(Func, Template, 40); \
    QUEUE_BENCH_FOR_SIZE(Func, Template, 48); \
    QUEUE_BENCH_FOR_SIZE(Func, Template, 56); \
    QUEUE_BENCH_FOR_SIZE(Func, Template, 64);

