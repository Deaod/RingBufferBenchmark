#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, std::size_t SIZE>
struct ChunkedQueue3 {
    using value_type = T;
    static const auto align = 64;
    static const auto chunk_size_bytes = (1 << 15) - (3 * align);

    static const auto chunk_max_size = chunk_size_bytes / sizeof(value_type);
    static const auto chunk_count =
        ((SIZE + chunk_max_size - 1) / chunk_max_size);
    static const auto chunk_size =
        (SIZE / chunk_count) + (((SIZE % chunk_count) == 0) ? 0 : 1);

    struct alignas(align) chunk {
        chunk() :
            tail(),
            head_cache(),
            head(),
            tail_cache()
        {}

        alignas(align) std::atomic_size_t tail;
        mutable size_t head_cache;
        alignas(align) std::atomic_size_t head;
        mutable size_t tail_cache;
        alignas(align) chunk* next;
        alignas(align) std::array<std::byte, sizeof(T) * chunk_size> buffer;
    };

    alignas(align) std::atomic<chunk*> tail_chunk;
    alignas(align) std::atomic<chunk*> head_chunk;
    alignas(align) std::array<chunk, chunk_count> chunks;

    ChunkedQueue3() :
        chunks{},
        head_chunk(chunks.data()),
        tail_chunk(chunks.data())
    {
        for (size_t i = 0; i < chunk_count - 1; i += 1) {
            chunks[i].next = &chunks[i + 1];
        }
        chunks[chunk_count - 1].next = chunks.data();
    }

    template<typename... Args>
    int Enqueue(Args&&... args) {
        chunk* tc = tail_chunk.load(std::memory_order_relaxed);
        size_t t = tc->tail.load(std::memory_order_relaxed);
        size_t n = (t + 1);
        if (n == chunk_size) n = 0;

        size_t h = tc->head_cache;
        if (n == h) {
            h = tc->head_cache = tc->head.load(std::memory_order_acquire);
            if (n == h) {
                tc = tc->next;
                if (tc == head_chunk.load(std::memory_order_acquire))
                    return 0;

                t = tc->tail.load(std::memory_order_relaxed);
                n = (t + 1);
                if (n == chunk_size) n = 0;

                // this _next line is exactly where it needs to be, unless you
                // like deadlocks.
                tc->head_cache = tc->head.load(std::memory_order_acquire);

                new(tc->buffer.data() + t * sizeof(T))
                    T(std::forward<Args>(args)...);
                tc->tail.store(n, std::memory_order_release);
                tail_chunk.store(tc, std::memory_order_release);
                return 1;
            }
        }

        new(tc->buffer.data() + t * sizeof(T)) T(std::forward<Args>(args)...);
        tc->tail.store(n, std::memory_order_release);

        return 1;
    }

    template<typename Callable>
    int Dequeue(Callable&& f) {
        chunk* hc = head_chunk.load(std::memory_order_relaxed);

        auto h = hc->head.load(std::memory_order_relaxed);
        auto t = hc->tail_cache;

        if (t == h) {
            // this line needs to stay right here, before we load the latest
            // value of hc->tail
            chunk* tc = tail_chunk.load(std::memory_order_acquire);
            t = hc->tail_cache = hc->tail.load(std::memory_order_acquire);
            if (t == h) {
                if (hc == tc)
                    return 0;

                hc = hc->next;
                h = hc->head.load(std::memory_order_relaxed);
                t = hc->tail_cache = hc->tail.load(std::memory_order_acquire);

                if (t == h) return 0;

                T* elem = std::launder(reinterpret_cast<T*>(
                    hc->buffer.data() + h * sizeof(T)));
                std::invoke(std::forward<Callable>(f), std::move(*elem));
                elem->~T();

                h += 1;
                if (h == chunk_size) h = 0;
                hc->head.store(h, std::memory_order_release);
                head_chunk.store(hc, std::memory_order_release);
                return 1;
            }
        }

        T* elem = std::launder(reinterpret_cast<T*>(
            hc->buffer.data() + h * sizeof(T)));
        std::invoke(std::forward<Callable>(f), std::move(*elem));
        elem->~T();

        h += 1;
        if (h == chunk_size) h = 0;
        hc->head.store(h, std::memory_order_release);
        return 1;
    }
};
