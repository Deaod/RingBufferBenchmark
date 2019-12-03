#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, std::size_t SIZE>
struct ChunkedQueue7 {
    using value_type = T;
    static const auto align = 128;
    static const auto chunk_size_bytes = (1 << 15) - (3 * align);

    static const auto chunk_max_size = chunk_size_bytes / sizeof(value_type);
    static const auto chunk_count =
        ((SIZE + chunk_max_size - 1) / chunk_max_size);
    static const auto chunk_size =
        (SIZE / chunk_count) + (((SIZE % chunk_count) == 0) ? 0 : 1);

    struct alignas(align) chunk {
        chunk() :
            tail((T*)buffer.data()),
            head_cache((T*)buffer.data()),
            head((T*)buffer.data()),
            tail_cache((T*)buffer.data())
        {}

        alignas(align) std::array<std::byte, sizeof(T) * chunk_size> buffer;
        alignas(align) std::atomic<T*> tail;
        mutable T* head_cache;
        alignas(align) std::atomic<T*> head;
        mutable T* tail_cache;
        alignas(align) chunk* next;
    };

    alignas(align) std::atomic<chunk*> tail_chunk;
    alignas(align) std::atomic<chunk*> head_chunk;
    alignas(align) std::array<chunk, chunk_count> chunks;

    ChunkedQueue7() :
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

        T* t = tc->tail.load(std::memory_order_relaxed);
        T* n = (t + 1);
        if (n == (T*)tc->buffer.data() + chunk_size) {
            n = (T*)tc->buffer.data();
        }

        T* h = tc->head_cache;
        if (n == h) {
            h = tc->head_cache = tc->head.load(std::memory_order_acquire);
            if (n == h) {
                tc = tc->next;
                if (tc == head_chunk.load(std::memory_order_acquire))
                    return 0;

                t = tc->tail.load(std::memory_order_relaxed);
                n = (t + 1);
                if (n == (T*)tc->buffer.data() + chunk_size) {
                    n = (T*)tc->buffer.data();
                }

                // this _next line is exactly where it needs to be, unless you
                // like deadlocks.
                tc->head_cache = tc->head.load(std::memory_order_acquire);

                new(t) T(std::forward<Args>(args)...);
                tc->tail.store(n, std::memory_order_release);
                tail_chunk.store(tc, std::memory_order_release);
                return 1;
            }
        }

        new(t) T(std::forward<Args>(args)...);
        tc->tail.store(n, std::memory_order_release);

        return 1;
    }

    template<typename Callable>
    int Dequeue(Callable&& f) {
        chunk* hc = head_chunk.load(std::memory_order_relaxed);

        T* h = hc->head.load(std::memory_order_relaxed);
        T* t = hc->tail_cache;

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

                T* elem = std::launder(h);
                std::invoke(std::forward<Callable>(f), std::move(*elem));
                elem->~T();

                h += 1;
                if (h == (T*)hc->buffer.data() + chunk_size) {
                    h = (T*)hc->buffer.data();
                }
                hc->head.store(h, std::memory_order_release);
                head_chunk.store(hc, std::memory_order_release);
                return 1;
            }
        }

        T* elem = std::launder(h);
        std::invoke(std::forward<Callable>(f), std::move(*elem));
        elem->~T();

        h += 1;
        if (h == (T*)hc->buffer.data() + chunk_size) {
            h = (T*)hc->buffer.data();
        }
        hc->head.store(h, std::memory_order_release);
        return 1;
    }
};
